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

    const auto driveCurve = std::pow (drive, 1.85f);
    const auto biasCurve = std::pow (bias, 1.35f);
    const auto toneCurve = std::pow (tone, 0.82f);
    const auto wowCurve = std::pow (wow, 1.8f);
    const auto flutterCurve = std::pow (flutter, 1.7f);
    const auto mixCurve = std::pow (mix, 1.35f);
    const auto outputCurve = std::pow (output, 1.25f);

    const auto twoPi = juce::MathConstants<float>::twoPi;
    const auto speedScale = (speed == 0) ? 0.72f : (speed == 1) ? 1.0f : 1.38f;
    const auto wowFreq = (0.16f + wowCurve * 2.6f) * speedScale;
    const auto flutterFreq = (2.2f + flutterCurve * 12.5f) * (1.0f + speedScale * 0.24f);
    const auto tapeRandom = 0.2f + wowCurve * 0.5f + flutterCurve * 0.32f;

    float tapeCurve = 1.0f;
    float tapeHeadroom = 1.0f;
    float tapeBiasBoost = 0.0f;
    float tapeTexture = 0.0f;
    float tapeColor = 0.0f;

    switch (tapeType)
    {
        case 0: // J37
            tapeCurve = 1.5f;
            tapeHeadroom = 1.25f;
            tapeBiasBoost = 0.18f;
            tapeTexture = 0.42f;
            tapeColor = 0.18f;
            break;
        case 1: // Ampex 456
            tapeCurve = 1.9f;
            tapeHeadroom = 1.35f;
            tapeBiasBoost = 0.26f;
            tapeTexture = 0.5f;
            tapeColor = 0.28f;
            break;
        case 2: // Studer A800
            tapeCurve = 2.1f;
            tapeHeadroom = 1.5f;
            tapeBiasBoost = 0.32f;
            tapeTexture = 0.62f;
            tapeColor = 0.38f;
            break;
        case 3: // Chrome
        default:
            tapeCurve = 1.7f;
            tapeHeadroom = 1.1f;
            tapeBiasBoost = 0.14f;
            tapeTexture = 0.3f;
            tapeColor = 0.12f;
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
            const float randomMicro = std::sin (wowPhase * 0.7f + flutterPhase * 1.3f + channel * 1.7f);

            wowPhase += (twoPi * wowFreq) / sampleRate;
            flutterPhase += (twoPi * flutterFreq) / sampleRate;

            const float wowMapped = std::pow (wow, 1.7f);
            const float flutterMapped = std::pow (flutter, 1.6f);
            const float biasMapped = std::pow (bias, 1.35f);
            const float toneMapped = std::pow (tone, 0.9f);
            const float mixMapped = std::pow (mix, 1.3f);
            const float outputMapped = std::pow (output, 1.25f);

            const float wowMod = 1.0f + wowMapped * 0.16f * wowLfo;
            const float flutterMod = 1.0f + flutterMapped * 0.2f * flutterLfo;
            const float microMod = 1.0f + (tapeRandom + tapeTexture) * 0.14f * randomMicro;

            const float driveBoost = 1.0f + driveCurve * 2.9f * (0.82f + speedScale * 0.25f);
            const float signalPre = x * driveBoost;
            const float tapeBias = biasMapped * (0.75f + tapeBiasBoost) + (toneMapped * 0.54f) + tapeColor;
            const float softened = std::tanh (signalPre * (0.8f + driveCurve * 1.7f * tapeCurve));
            const float harmonic = softened + (x * (0.18f + toneMapped * 0.9f + tapeColor * 0.18f));
            const float biasDrive = std::tanh ((harmonic + lastBias * (0.35f + speedScale * 0.18f)) * (1.0f + tapeBias));

            const float soft = (biasDrive * (1.0f - 0.09f * toneMapped)) + (lastTapeSample * (0.2f + toneMapped * 0.25f + speedScale * 0.1f));
            lastTapeSample = soft;
            lastBias = biasDrive;

            const float tapeEdge = soft * wowMod * flutterMod * microMod;
            const float dry = x;
            const float warmMix = dry * (1.0f - mixMapped * 0.7f) + tapeEdge * (0.44f + mixMapped * 1.5f);
            const float finalOut = warmMix * (0.72f + outputMapped * 1.7f) * (0.92f + speedScale * 0.12f);

            channelData[i] = juce::jlimit (-0.999f, 0.999f, finalOut);
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
