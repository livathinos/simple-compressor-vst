/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
SimpleCompressorAudioProcessor::SimpleCompressorAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
    using namespace Params;
    const auto& params = GetParams();
  
    auto floatHelper = [&apvts = this->apvts, &params](auto& param, const auto& paramName)
    {
        param = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(params.at(paramName)));
        jassert(param != nullptr);
    };
    
    auto choiceHelper = [&apvts = this->apvts, &params](auto& param, const auto& paramName)
    {
        param = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(params.at(paramName)));
        jassert(param != nullptr);
    };
    
    auto boolHelper = [&apvts = this->apvts, &params](auto& param, const auto& paramName)
    {
        param = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(params.at(paramName)));
        jassert(param != nullptr);
    };
    
    floatHelper(compressor.attack, Names::Attack_Low_Band);
    floatHelper(compressor.release, Names::Release_Low_Band);
    floatHelper(compressor.threshold, Names::Threshold_Low_Band);
    
    choiceHelper(compressor.ratio, Names::Ratio_Low_Band);
    
    boolHelper(compressor.bypassed, Names::Bypassed_Low_Band);
    
    floatHelper(lowMidCrossover, Names::Low_Mid_Crossover_Freq);
    floatHelper(midHighCrossover, Names::Mid_High_Crossover_Freq);
    
    LP1.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
    HP1.setType(juce::dsp::LinkwitzRileyFilterType::highpass);
    
    AP2.setType(juce::dsp::LinkwitzRileyFilterType::allpass);
    
    LP2.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
    HP2.setType(juce::dsp::LinkwitzRileyFilterType::highpass);
    
}

SimpleCompressorAudioProcessor::~SimpleCompressorAudioProcessor()
{
}

//==============================================================================
const juce::String SimpleCompressorAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SimpleCompressorAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool SimpleCompressorAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool SimpleCompressorAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double SimpleCompressorAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int SimpleCompressorAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int SimpleCompressorAudioProcessor::getCurrentProgram()
{
    return 0;
}

void SimpleCompressorAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String SimpleCompressorAudioProcessor::getProgramName (int index)
{
    return {};
}

void SimpleCompressorAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void SimpleCompressorAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    juce::dsp::ProcessSpec spec;
    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = getTotalNumOutputChannels();
    spec.sampleRate = sampleRate;
    
    compressor.prepare(spec);
    
    LP1.prepare(spec);
    HP1.prepare(spec);
    
    AP2.prepare(spec);
    
    LP2.prepare(spec);
    HP2.prepare(spec);
    
    for (auto& buffer : filterBuffers)
    {
        buffer.setSize(spec.numChannels, samplesPerBlock);
    }
}

void SimpleCompressorAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SimpleCompressorAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void SimpleCompressorAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());
    
    for (auto& filterBuffer : filterBuffers)
    {
        filterBuffer = buffer;
    }
    
    auto lowMidCutoffFreq = lowMidCrossover->get();
    LP1.setCutoffFrequency(lowMidCutoffFreq);
    HP1.setCutoffFrequency(lowMidCutoffFreq);
    
    auto midHighCutoffFreq = midHighCrossover->get();
    AP2.setCutoffFrequency(midHighCutoffFreq);
    LP2.setCutoffFrequency(midHighCutoffFreq);
    HP2.setCutoffFrequency(midHighCutoffFreq);
    
    auto filterBufferBlockZero = juce::dsp::AudioBlock<float>(filterBuffers[0]);
    auto filterBufferBlockOne = juce::dsp::AudioBlock<float>(filterBuffers[1]);
    auto filterBufferBlockTwo = juce::dsp::AudioBlock<float>(filterBuffers[2]);
    
    auto filterBufferContextZero = juce::dsp::ProcessContextReplacing<float>(filterBufferBlockZero);
    auto filterBufferContextOne = juce::dsp::ProcessContextReplacing<float>(filterBufferBlockOne);
    auto filterBufferContextTwo = juce::dsp::ProcessContextReplacing<float>(filterBufferBlockTwo);
    
    LP1.process(filterBufferContextZero);
    AP2.process(filterBufferContextZero);
    
    HP1.process(filterBufferContextOne);
    filterBuffers[2] = filterBuffers[1];
    LP2.process(filterBufferContextOne);
    
    HP2.process(filterBufferContextTwo);
    
    auto numSamples = buffer.getNumSamples();
    auto numChannels = buffer.getNumChannels();
    
    if (compressor.bypassed->get())
        return;
    
    buffer.clear();
    
    auto addFilterBand = [nc = numChannels, ns = numSamples](auto& inputBuffer, const auto& source)
    {
        for (auto i = 0; i < nc; i++)
        {
            inputBuffer.addFrom(i, 0, source, i, 0, ns);
        }
    };
    
    addFilterBand(buffer, filterBuffers[0]);
    addFilterBand(buffer, filterBuffers[1]);
    addFilterBand(buffer, filterBuffers[2]);
}

//==============================================================================
bool SimpleCompressorAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* SimpleCompressorAudioProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor(*this);
}

//==============================================================================
void SimpleCompressorAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void SimpleCompressorAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

juce::AudioProcessorValueTreeState::ParameterLayout SimpleCompressorAudioProcessor::createParameterLayout()
{
    APVTS::ParameterLayout layout;
    using namespace juce;
    using namespace Params;
    const auto& params = GetParams();
    
    layout.add(
      std::make_unique<AudioParameterFloat>(
        params.at(Names::Threshold_Low_Band),
        params.at(Names::Threshold_Low_Band),
        NormalisableRange<float>(-60, 12, 1, 1),
        0
      )
    );
    
    auto attackReleaseRange = NormalisableRange<float>(5, 500, 1, 1);
    
    layout.add(
      std::make_unique<AudioParameterFloat>(
        params.at(Names::Attack_Low_Band),
        params.at(Names::Attack_Low_Band),
        attackReleaseRange,
        50
      )
    );
    layout.add(
      std::make_unique<AudioParameterFloat>(
        params.at(Names::Release_Low_Band),
        params.at(Names::Release_Low_Band),
        attackReleaseRange,
        250
      )
    );
    
    auto choices  = std::vector<double>{1, 1.5, 2, 3, 4, 5, 6, 7, 8, 10, 20, 50, 100};
    juce::StringArray sa;
    
    for (auto choice : choices) {
        sa.add(juce::String(choice, 1));
    }
    
    layout.add(std::make_unique<AudioParameterChoice>(params.at(Names::Ratio_Low_Band), params.at(Names::Ratio_Low_Band), sa, 3));
    layout.add(std::make_unique<AudioParameterBool>(params.at(Names::Bypassed_Low_Band),params.at(Names::Bypassed_Low_Band), false));
    
    layout.add(
      std::make_unique<AudioParameterFloat>(
        params.at(Names::Low_Mid_Crossover_Freq),
        params.at(Names::Low_Mid_Crossover_Freq),
        NormalisableRange<float>(20, 999, 1, 1),
        400
      )
    );
  
    layout.add(
      std::make_unique<AudioParameterFloat>(
        params.at(Names::Mid_High_Crossover_Freq),
        params.at(Names::Mid_High_Crossover_Freq),
        NormalisableRange<float>(1000, 20000, 1, 1),
        2000
      )
    );
    return layout;
}
//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SimpleCompressorAudioProcessor();
}
