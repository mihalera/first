/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <array>

namespace
{
    constexpr float minTrack = 0.0f;
    constexpr float maxTrack = 1.0f;

    inline float jsStyleNonlinearity (float x, float drive, float density)
    {
        const float signal = x * (1.0f + drive * 2.5f);
        const float wobble = std::sin (signal * (1.8f + density * 4.2f));
        const float shaped = signal + wobble * (0.22f + density * 0.45f);
        return std::tanh (shaped * (0.8f + drive * 1.55f));
    }
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

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "input", 1 }, "Input",
                                                            juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f),
                                                            0.0f,
                                                            juce::AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "auto_glue", 1 },
                                                            "Auto Glue", true));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "stereo_width", 1 },
                                                            "Stereo Width",
                                                            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
                                                            0.5f,
                                                            juce::AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<juce::AudioParameterChoice> ("tape_type", "Tape Type",
                                                            juce::StringArray { "J37", "Ampex 456", "Studer A800", "Chrome" },
                                                            0));
    layout.add (std::make_unique<juce::AudioParameterChoice> ("speed", "Speed",
                                                            juce::StringArray { "7.5 ips", "15 ips", "30 ips" },
                                                            1));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("drive", "Drive", minTrack, maxTrack, 0.42f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("bias", "Bias", minTrack, maxTrack, 0.36f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("tone", "Tone", minTrack, maxTrack, 0.58f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("wow", "Wow", minTrack, maxTrack, 0.14f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("flutter", "Flutter", minTrack, maxTrack, 0.18f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("mix", "Mix", minTrack, maxTrack, 0.62f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("output", "Output", minTrack, maxTrack, 0.68f));

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
void FirstAudioProcessor::prepareToPlay (double sampleRateToUse, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    this->sampleRate = static_cast<float> (sampleRateToUse);
    inputGainSmoothed.reset (sampleRateToUse, 0.02);
    inputGainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (
        parameters.getRawParameterValue ("input")->load()));
    inputPeakLevel.store (0.0f, std::memory_order_relaxed);
    inputRmsLevel.store (0.0f, std::memory_order_relaxed);
    outputPeakLevel.store (0.0f, std::memory_order_relaxed);
    outputRmsLevel.store (0.0f, std::memory_order_relaxed);
    wowPhaseL = 0.0f;
    wowPhaseR = 0.0f;
    flutterPhaseL = 0.0f;
    flutterPhaseR = 0.0f;
    tapeLastL = 0.0f;
    tapeLastR = 0.0f;
    tapeBiasL = 0.0f;
    tapeBiasR = 0.0f;
    compressorEnvelope = 0.0f;
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
    const auto totalNumInputChannels = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto channel = totalNumInputChannels; channel < totalNumOutputChannels; ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    const auto tapeType = static_cast<int> (parameters.getRawParameterValue ("tape_type")->load());
    const auto speed = static_cast<int> (parameters.getRawParameterValue ("speed")->load());
    const auto drive = parameters.getRawParameterValue ("drive")->load();
    const auto bias = parameters.getRawParameterValue ("bias")->load();
    const auto tone = parameters.getRawParameterValue ("tone")->load();
    const auto wow = parameters.getRawParameterValue ("wow")->load();
    const auto flutter = parameters.getRawParameterValue ("flutter")->load();
    const auto mix = parameters.getRawParameterValue ("mix")->load();
    const auto output = parameters.getRawParameterValue ("output")->load();
    const auto inputDb = parameters.getRawParameterValue ("input")->load();
    const auto autoGlue = parameters.getRawParameterValue ("auto_glue")->load() >= 0.5f;
    const auto stereoWidth = parameters.getRawParameterValue ("stereo_width")->load() * 2.0f;

    inputGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (inputDb));

    const auto driveCurve = std::pow (drive, 1.45f);
    const auto biasCurve = std::pow (bias, 1.15f);
    const auto toneCurve = std::pow (tone, 0.92f);
    const auto wowCurve = std::pow (wow, 1.55f);
    const auto flutterCurve = std::pow (flutter, 1.45f);
    const auto mixCurve = std::pow (mix, 1.18f);
    const auto outputCurve = std::pow (output, 1.12f);

    const auto twoPi = juce::MathConstants<float>::twoPi;
    const auto speedScale = (speed == 0) ? 0.76f : (speed == 1) ? 1.0f : 1.34f;
    const auto wowFreq = (0.15f + wowCurve * 1.36f) * speedScale;
    const auto flutterFreq = (1.9f + flutterCurve * 5.4f) * (1.0f + speedScale * 0.22f);

    float tapeCurve = 1.12f;
    float tapeBiasBoost = 0.16f;
    float tapeTexture = 0.20f;
    float tapeColor = 0.12f;

    switch (tapeType)
    {
        case 0: // J37
            tapeCurve = 1.18f;
            tapeBiasBoost = 0.16f;
            tapeTexture = 0.22f;
            tapeColor = 0.12f;
            break;
        case 1: // Ampex 456
            tapeCurve = 1.30f;
            tapeBiasBoost = 0.24f;
            tapeTexture = 0.30f;
            tapeColor = 0.18f;
            break;
        case 2: // Studer A800
            tapeCurve = 1.44f;
            tapeBiasBoost = 0.28f;
            tapeTexture = 0.38f;
            tapeColor = 0.22f;
            break;
        case 3: // Chrome
        default:
            tapeCurve = 1.22f;
            tapeBiasBoost = 0.12f;
            tapeTexture = 0.18f;
            tapeColor = 0.09f;
            break;
    }

    const float driveAmount = 0.28f + driveCurve * 1.9f;
    const float biasAmount = 0.18f + biasCurve * 1.55f;
    const float toneAmount = 0.55f + toneCurve * 1.0f;
    const float mixAmount = 0.12f + mixCurve * 0.88f;
    const float outputAmount = 0.70f + outputCurve * 1.0f;
    const float wowDepth = wowCurve * (0.05f + speedScale * 0.08f);
    const float flutterDepth = flutterCurve * (0.08f + speedScale * 0.09f);
    const float speedBias = 0.84f + speedScale * 0.30f;
    const float finalOutputGain = outputAmount * (0.92f + speedScale * 0.12f);
    const int activeChannels = juce::jmin (2, totalNumInputChannels);

    std::array<float*, 2> channelData {};
    for (int channel = 0; channel < activeChannels; ++channel)
        channelData[static_cast<std::size_t> (channel)] = buffer.getWritePointer (channel);

    float inputPeak = 0.0f;
    float outputPeak = 0.0f;
    double inputSquares = 0.0;
    double outputSquares = 0.0;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const float inputGain = inputGainSmoothed.getNextValue();
        std::array<float, 2> tapeOutput {};

        for (int channel = 0; channel < activeChannels; ++channel)
        {
            auto& wowPhase = channel == 0 ? wowPhaseL : wowPhaseR;
            auto& flutterPhase = channel == 0 ? flutterPhaseL : flutterPhaseR;
            auto& lastTapeSample = channel == 0 ? tapeLastL : tapeLastR;
            auto& lastBias = channel == 0 ? tapeBiasL : tapeBiasR;

            const float rawInput = channelData[static_cast<std::size_t> (channel)][sample];
            inputPeak = juce::jmax (inputPeak, std::abs (rawInput));
            inputSquares += static_cast<double> (rawInput) * rawInput;

            const float x = rawInput * inputGain;
            const float wowLfo = std::sin (wowPhase);
            const float flutterLfo = std::sin (flutterPhase);
            const float microRandom = std::sin (wowPhase * 0.8f + flutterPhase * 1.3f + channel * 2.4f);

            wowPhase += (twoPi * wowFreq) / sampleRate;
            flutterPhase += (twoPi * flutterFreq) / sampleRate;

            const float wowMod = 1.0f + wowLfo * wowDepth;
            const float flutterMod = 1.0f + flutterLfo * flutterDepth;
            const float microMod = 1.0f + (tapeTexture * 0.18f + tapeColor * 0.12f) * microRandom;

            const float preDrive = x * (1.0f + driveAmount * 1.2f * speedBias);
            const float preBias = preDrive + lastBias * (0.12f + biasCurve * 0.22f + speedScale * 0.05f);
            const float biasCompensation = 0.92f + tapeBiasBoost * 0.30f;

            const float saturation = std::tanh (preBias * (0.82f + driveAmount * 1.1f * tapeCurve * biasCompensation));
            const float jsCurve = jsStyleNonlinearity (preDrive, driveCurve, tapeTexture);
            const float harmonicLift = std::tanh (preDrive * (0.85f + tapeColor * 1.2f)
                                                   + x * (0.08f + toneAmount * 0.2f));
            const float tapeBody = saturation * (0.82f + biasAmount * 0.7f)
                                 + jsCurve * (0.28f + toneCurve * 0.42f)
                                 + harmonicLift * (0.22f + toneCurve * 0.62f);
            const float memoryMix = tapeBody * 0.74f + lastTapeSample * 0.26f;

            lastTapeSample = memoryMix;
            lastBias = tapeBody;

            const float motioned = memoryMix * wowMod * flutterMod * microMod;
            const float dryMix = x * (1.0f - mixAmount);
            const float wetMix = motioned * (0.20f + mixAmount * 0.82f);
            tapeOutput[static_cast<std::size_t> (channel)] = dryMix + wetMix;
        }

        float detectorPower = 0.0f;
        if (activeChannels > 0)
        {
            for (int channel = 0; channel < activeChannels; ++channel)
            {
                const auto signal = tapeOutput[static_cast<std::size_t> (channel)];
                detectorPower += signal * signal;
            }
            detectorPower /= static_cast<float> (activeChannels);
        }

        const float envelopeLevel = juce::jlimit (0.0f, 1.0f,
                                                   std::sqrt (juce::jmax (0.0f, compressorEnvelope)));
        const float attackSeconds = 0.18f + envelopeLevel * 0.32f;
        const float releaseSeconds = 0.90f + envelopeLevel * 2.60f;
        const float timeConstant = detectorPower > compressorEnvelope ? attackSeconds : releaseSeconds;
        const float envelopeCoefficient = std::exp (-1.0f / (juce::jmax (1.0f, sampleRate) * timeConstant));
        compressorEnvelope = envelopeCoefficient * compressorEnvelope
                           + (1.0f - envelopeCoefficient) * detectorPower;

        float compressionGain = 1.0f;
        if (autoGlue)
        {
            constexpr float thresholdDb = -16.0f;
            constexpr float kneeDb = 8.0f;
            constexpr float ratio = 1.22f;
            constexpr float reductionLimitDb = -3.0f;

            const float envelopeDb = juce::Decibels::gainToDecibels (
                std::sqrt (juce::jmax (0.0f, compressorEnvelope)), -100.0f);
            const float overshootDb = envelopeDb - thresholdDb;
            float reductionDb = 0.0f;

            if (overshootDb > -kneeDb * 0.5f)
            {
                if (overshootDb < kneeDb * 0.5f)
                {
                    const auto kneeProgress = overshootDb + kneeDb * 0.5f;
                    reductionDb = -(1.0f - 1.0f / ratio)
                                * kneeProgress * kneeProgress / (2.0f * kneeDb);
                }
                else
                {
                    reductionDb = -(1.0f - 1.0f / ratio) * overshootDb;
                }
            }

            reductionDb = juce::jmax (reductionLimitDb, reductionDb);
            compressionGain = juce::Decibels::decibelsToGain (reductionDb);
        }

        std::array<float, 2> outputSignal {};
        for (int channel = 0; channel < activeChannels; ++channel)
            outputSignal[static_cast<std::size_t> (channel)] =
                tapeOutput[static_cast<std::size_t> (channel)] * compressionGain * finalOutputGain;

        if (activeChannels == 2)
        {
            const float mid = 0.5f * (outputSignal[0] + outputSignal[1]);
            const float side = 0.5f * (outputSignal[0] - outputSignal[1]) * stereoWidth;
            outputSignal[0] = mid + side;
            outputSignal[1] = mid - side;
        }

        for (int channel = 0; channel < activeChannels; ++channel)
        {
            const float limitedOut = juce::jlimit (-0.999f, 0.999f,
                                                   outputSignal[static_cast<std::size_t> (channel)]);
            channelData[static_cast<std::size_t> (channel)][sample] = limitedOut;
            outputPeak = juce::jmax (outputPeak, std::abs (limitedOut));
            outputSquares += static_cast<double> (limitedOut) * limitedOut;
        }
    }

    const auto measuredSamples = static_cast<double> (buffer.getNumSamples())
                               * static_cast<double> (juce::jmax (1, activeChannels));
    const float inputRms = measuredSamples > 0.0
        ? static_cast<float> (std::sqrt (inputSquares / measuredSamples)) : 0.0f;
    const float outputRms = measuredSamples > 0.0
        ? static_cast<float> (std::sqrt (outputSquares / measuredSamples)) : 0.0f;

    const auto retainPeakUntilConsumed = [] (std::atomic<float>& publishedPeak, float blockPeak)
    {
        auto accumulatedPeak = publishedPeak.load (std::memory_order_relaxed);
        while (accumulatedPeak < blockPeak
               && ! publishedPeak.compare_exchange_weak (accumulatedPeak, blockPeak,
                                                          std::memory_order_relaxed,
                                                          std::memory_order_relaxed))
        {
        }
    };

    retainPeakUntilConsumed (inputPeakLevel, inputPeak);
    inputRmsLevel.store (inputRms, std::memory_order_relaxed);
    retainPeakUntilConsumed (outputPeakLevel, outputPeak);
    outputRmsLevel.store (outputRms, std::memory_order_relaxed);
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
