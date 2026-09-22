/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr float minTrack = 0.0f;
    constexpr float maxTrack = 1.0f;
}

//==============================================================================
FirstAudioProcessor::FirstAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
       parameters (*this, nullptr, "TAPE_J37", createParameterLayout())
#endif
{
}

FirstAudioProcessor::~FirstAudioProcessor()
{
}

juce::AudioProcessorValueTreeState::ParameterLayout FirstAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterChoice> ("tape_type", "Tape Type",
                                                            juce::StringArray { "J37", "Ampex 456", "Studer A800", "Chrome" },
                                                            0));
    layout.add (std::make_unique<juce::AudioParameterChoice> ("speed", "Speed",
                                                            juce::StringArray { "7.5 ips", "15 ips", "30 ips" },
                                                            1));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("drive", "Drive", minTrack, maxTrack, 0.58f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("bias", "Bias", minTrack, maxTrack, 0.36f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("tone", "Tone", minTrack, maxTrack, 0.52f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("wow", "Wow", minTrack, maxTrack, 0.18f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("flutter", "Flutter", minTrack, maxTrack, 0.22f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("mix", "Mix", minTrack, maxTrack, 0.82f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("output", "Output", minTrack, maxTrack, 0.86f));

    return layout;
}

//==============================================================================
const juce::String FirstAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool FirstAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool FirstAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool FirstAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double FirstAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int FirstAudioProcessor::getNumPrograms()
{
    return 1;
}

int FirstAudioProcessor::getCurrentProgram()
{
    return 0;
}

void FirstAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String FirstAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return {};
}

void FirstAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void FirstAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    this->sampleRate = static_cast<float> (sampleRate);
    wowPhaseL = 0.0f;
    wowPhaseR = 0.0f;
    flutterPhaseL = 0.0f;
    flutterPhaseR = 0.0f;
    tapeLastL = 0.0f;
    tapeLastR = 0.0f;
    tapeBiasL = 0.0f;
    tapeBiasR = 0.0f;
}

void FirstAudioProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool FirstAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void FirstAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);

    juce::ScopedNoDenormals noDenormals;
    const auto totalNumInputChannels  = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    const auto tapeType = static_cast<int> (
        parameters.getRawParameterValue ("tape_type")->load());

    const auto speed = static_cast<int> (
        parameters.getRawParameterValue ("speed")->load());

    const auto drive = parameters.getRawParameterValue ("drive")->load();
    const auto bias = parameters.getRawParameterValue ("bias")->load();
    const auto tone = parameters.getRawParameterValue ("tone")->load();
    const auto wow = parameters.getRawParameterValue ("wow")->load();
    const auto flutter = parameters.getRawParameterValue ("flutter")->load();
    const auto mix = parameters.getRawParameterValue ("mix")->load();
    const auto output = parameters.getRawParameterValue ("output")->load();

    const auto twoPi = juce::MathConstants<float>::twoPi;
    const auto speedScale = (speed == 0) ? 0.72f : (speed == 1) ? 1.0f : 1.38f;
    const auto wowFreq = (0.22f + wow * 3.6f) * speedScale;
    const auto flutterFreq = (2.8f + flutter * 17.0f) * (1.0f + speedScale * 0.2f);

    float tapeCurve = 1.0f;
    float tapeHeadroom = 1.0f;
    float tapeBiasBoost = 0.0f;

    switch (tapeType)
    {
        case 0: // J37
            tapeCurve = 1.5f;
            tapeHeadroom = 1.25f;
            tapeBiasBoost = 0.18f;
            break;
        case 1: // Ampex 456
            tapeCurve = 1.9f;
            tapeHeadroom = 1.35f;
            tapeBiasBoost = 0.26f;
            break;
        case 2: // Studer A800
            tapeCurve = 2.1f;
            tapeHeadroom = 1.5f;
            tapeBiasBoost = 0.32f;
            break;
        case 3: // Chrome
        default:
            tapeCurve = 1.7f;
            tapeHeadroom = 1.1f;
            tapeBiasBoost = 0.14f;
            break;
    }

    for (int channel = 0; channel < totalNumInputChannels; ++channel)
    {
        auto* channelData = buffer.getWritePointer (channel);
        auto& wowPhase = channel == 0 ? wowPhaseL : wowPhaseR;
        auto& flutterPhase = channel == 0 ? flutterPhaseL : flutterPhaseR;
        auto& lastTapeSample = channel == 0 ? tapeLastL : tapeLastR;
        auto& lastBias = channel == 0 ? tapeBiasL : tapeBiasR;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float x = channelData[i];
            const float wowLfo = std::sin (wowPhase);
            const float flutterLfo = std::sin (flutterPhase);

            wowPhase += (twoPi * wowFreq) / sampleRate;
            flutterPhase += (twoPi * flutterFreq) / sampleRate;

            const float wowMod = 1.0f + wow * 0.11f * wowLfo;
            const float flutterMod = 1.0f + flutter * 0.08f * flutterLfo;

            const float signalPre = x * (1.0f + drive * 4.0f);
            const float tapeBias = bias * (0.8f + tapeBiasBoost) + (tone * 0.25f);
            const float warmed = std::tanh (signalPre * (1.0f + (drive * 1.2f * tapeCurve))) * (1.0f + tapeHeadroom * 0.2f);
            const float harmonic = warmed + (x * (0.2f + tone * 0.75f));
            const float biasDrive = std::tanh ((harmonic + lastBias * 0.4f) * (1.0f + tapeBias));

            const float soft = (biasDrive * (1.0f - 0.18f * tone)) + (lastTapeSample * (0.18f * tone));
            lastTapeSample = soft;
            lastBias = biasDrive;

            const float wet = soft * wowMod * flutterMod;
            const float dry = x;
            const float outputSignal = dry * (1.0f - mix) + wet * mix;
            const float finalOut = outputSignal * (0.7f + output * 1.2f);

            channelData[i] = juce::jlimit (-1.0f, 1.0f, finalOut);
        }
    }
}

//==============================================================================
bool FirstAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* FirstAudioProcessor::createEditor()
{
    return new FirstAudioProcessorEditor (*this);
}

//==============================================================================
void FirstAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void FirstAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, static_cast<size_t> (sizeInBytes)));

    if (xmlState != nullptr)
        parameters.replaceState (juce::ValueTree::fromXml (*xmlState));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FirstAudioProcessor();
}
