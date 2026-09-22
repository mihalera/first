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

    // Symmetric decibel range shared by the input and output stage controls.
    constexpr float minInputDb = -32.0f;
    constexpr float maxInputDb = 32.0f;

    /**
        Soft magnetic hysteresis: a memory-dependent shaping that produces the
        asymmetric, mostly-odd/even blend of analogue tape rather than a plain
        symmetric tanh curve. `memory` is the previous shaped output.
    */
    inline float magneticHysteresis (float x, float drive, float asymmetry, float memory)
    {
        const float biased = x + asymmetry;
        const float soft = std::tanh (biased * (1.0f + drive * 2.1f));

        // Anhysteretic curve blended with its own delayed image: this lag is what
        // gives tape its "sticky" transient behaviour.
        const float lagged = memory * 0.62f;
        const float blended = soft * 0.68f + std::tanh ((biased * 0.55f) + lagged) * 0.32f;

        // Remove the bias offset asymmetrically so the effect adds even harmonics
        // instead of merely shifting the signal.
        return blended - asymmetry * (0.55f + 0.45f * soft * soft);
    }

    /** One-pole low-pass coefficient for a given time constant in milliseconds. */
    inline float onePoleCoefficient (float milliseconds, float sampleRate)
    {
        const float seconds = juce::jmax (0.01f, milliseconds) * 0.001f;
        return 1.0f - std::exp (-1.0f / (seconds * juce::jmax (1.0f, sampleRate)));
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
    inputDbParam  = parameters.getRawParameterValue ("input");
    driveParam    = parameters.getRawParameterValue ("drive");
    biasParam     = parameters.getRawParameterValue ("bias");
    toneParam     = parameters.getRawParameterValue ("tone");
    wowParam      = parameters.getRawParameterValue ("wow");
    flutterParam  = parameters.getRawParameterValue ("flutter");
    mixParam      = parameters.getRawParameterValue ("mix");
    outputDbParam = parameters.getRawParameterValue ("output");
    widthParam    = parameters.getRawParameterValue ("stereo_width");
    bypassParam   = parameters.getRawParameterValue ("bypass");
}

FirstAudioProcessor::~FirstAudioProcessor()
{
}

juce::AudioProcessorValueTreeState::ParameterLayout FirstAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "input", 1 }, "Input",
                                                            juce::NormalisableRange<float> (minInputDb, maxInputDb, 0.1f),
                                                            0.0f,
                                                            juce::AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "output", 1 }, "Output",
                                                            juce::NormalisableRange<float> (minInputDb, maxInputDb, 0.1f),
                                                            0.0f,
                                                            juce::AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "bypass", 1 },
                                                            "Bypass", false));
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
    sampleRate = static_cast<float> (sampleRateToUse);

    const auto smoothingSeconds = 0.02;
    inputGainSmoothed.reset (sampleRateToUse, smoothingSeconds);
    inputGainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (
        inputDbParam != nullptr ? inputDbParam->load() : 0.0f));
    outputGainSmoothed.reset (sampleRateToUse, smoothingSeconds);
    outputGainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (
        outputDbParam != nullptr ? outputDbParam->load() : 0.0f));
    mixSmoothed.reset (sampleRateToUse, smoothingSeconds);
    mixSmoothed.setCurrentAndTargetValue (mixParam != nullptr ? mixParam->load() : 0.62f);
    widthSmoothed.reset (sampleRateToUse, smoothingSeconds);
    widthSmoothed.setCurrentAndTargetValue (widthParam != nullptr ? widthParam->load() * 2.0f : 1.0f);
    bypassSmoothed.reset (sampleRateToUse, 0.01);
    bypassSmoothed.setCurrentAndTargetValue (bypassParam != nullptr && bypassParam->load() >= 0.5f ? 0.0f : 1.0f);

    inputPeakLevel.store (0.0f, std::memory_order_relaxed);
    inputRmsLevel.store (0.0f, std::memory_order_relaxed);
    outputPeakLevel.store (0.0f, std::memory_order_relaxed);
    outputRmsLevel.store (0.0f, std::memory_order_relaxed);
    gainReductionDb.store (0.0f, std::memory_order_relaxed);
    compressorActivity.store (0.0f, std::memory_order_relaxed);
    transportDrift.store (0.5f, std::memory_order_relaxed);
    harmonicCharacter.store (0.0f, std::memory_order_relaxed);
    bypassActive.store (false, std::memory_order_relaxed);

    hystL.fill (0.0f);
    hystR.fill (0.0f);
    highFreqL.fill (0.0f);
    highFreqR.fill (0.0f);
    wowPhaseL = 0.0f;
    wowPhaseR = 0.0f;
    flutterPhaseL = 0.0f;
    flutterPhaseR = 0.0f;
    previousTone = -1.0f;
    toneLpAc = 0.0f;
    toneLpBc = 0.0f;
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
    const auto numSamples = buffer.getNumSamples();

    for (auto channel = totalNumInputChannels; channel < totalNumOutputChannels; ++channel)
        buffer.clear (channel, 0, numSamples);

    // -------------------------------------------------------------------------
    //  Bypass: the parameter is ramped, so the plugin can be switched in and out
    //  without a click, and while fully bypassed we skip the tape engine entirely.
    //  Input metering stays alive so the user can still see what is arriving.
    // -------------------------------------------------------------------------
    const auto bypassRequested = bypassParam != nullptr && bypassParam->load() >= 0.5f;
    if (bypassRequested && ! bypassSmoothed.isSmoothing() && bypassSmoothed.getCurrentValue() <= 0.0f)
    {
        bypassActive.store (true, std::memory_order_relaxed);

        float bypassPeak = 0.0f;
        double bypassSquares = 0.0;
        for (int channel = 0; channel < totalNumInputChannels; ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);
            for (int sample = 0; sample < numSamples; ++sample)
            {
                const auto value = data[sample];
                bypassPeak = juce::jmax (bypassPeak, std::abs (value));
                bypassSquares += static_cast<double> (value) * value;
            }
        }

        const auto bypassSamples = static_cast<double> (numSamples)
                                 * static_cast<double> (juce::jmax (1, totalNumInputChannels));
        const auto bypassRms = bypassSamples > 0.0
            ? static_cast<float> (std::sqrt (bypassSquares / bypassSamples)) : 0.0f;

        inputPeakLevel.store (bypassPeak, std::memory_order_relaxed);
        inputRmsLevel.store (bypassRms, std::memory_order_relaxed);
        outputPeakLevel.store (bypassPeak, std::memory_order_relaxed);
        outputRmsLevel.store (bypassRms, std::memory_order_relaxed);
        gainReductionDb.store (0.0f, std::memory_order_relaxed);
        compressorActivity.store (0.0f, std::memory_order_relaxed);
        return;
    }

    bypassActive.store (false, std::memory_order_relaxed);

    const auto tapeType = static_cast<int> (parameters.getRawParameterValue ("tape_type")->load());
    const auto speed = static_cast<int> (parameters.getRawParameterValue ("speed")->load());
    const auto drive = driveParam->load();
    const auto bias = biasParam->load();
    const auto tone = toneParam->load();
    const auto wow = wowParam->load();
    const auto flutter = flutterParam->load();
    const auto mix = mixParam->load();
    const auto outputDb = outputDbParam->load();
    const auto inputDb = inputDbParam->load();
    const auto stereoWidth = widthParam->load() * 2.0f;

    inputGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (inputDb));
    outputGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (outputDb));
    mixSmoothed.setTargetValue (mix);
    widthSmoothed.setTargetValue (stereoWidth);
    bypassSmoothed.setTargetValue (bypassRequested ? 0.0f : 1.0f);

    const auto driveCurve = std::pow (drive, 1.45f);
    const auto biasCurve = std::pow (bias, 1.15f);
    const auto toneCurve = std::pow (tone, 0.92f);
    const auto wowCurve = std::pow (wow, 1.55f);
    const auto flutterCurve = std::pow (flutter, 1.45f);
    const auto mixCurve = std::pow (mix, 1.18f);

    const auto twoPi = juce::MathConstants<float>::twoPi;
    const auto speedScale = (speed == 0) ? 0.76f : (speed == 1) ? 1.0f : 1.34f;
    const auto wowFreq = (0.15f + wowCurve * 1.36f) * speedScale;
    const auto flutterFreq = (1.9f + flutterCurve * 5.4f) * (1.0f + speedScale * 0.22f);

    // Per-model tape character: saturation curve, bias asymmetry, tape noise floor,
    // high-frequency softening and the magnetic hysteresis thickness.
    float tapeCurve = 1.18f;
    float tapeAsymmetry = 0.16f;
    float tapeHiss = 0.22f;
    float hfDampingMs = 26.0f;
    float hysteresis = 0.30f;

    switch (tapeType)
    {
        case 0: // J37 - the classic EMI reference sound
            tapeCurve = 1.18f;
            tapeAsymmetry = 0.16f;
            tapeHiss = 0.22f;
            hfDampingMs = 26.0f;
            hysteresis = 0.30f;
            break;
        case 1: // Ampex 456 - hotter, more low-order colour
            tapeCurve = 1.30f;
            tapeAsymmetry = 0.24f;
            tapeHiss = 0.30f;
            hfDampingMs = 34.0f;
            hysteresis = 0.38f;
            break;
        case 2: // Studer A800 - darkest, densest saturation
            tapeCurve = 1.44f;
            tapeAsymmetry = 0.28f;
            tapeHiss = 0.38f;
            hfDampingMs = 44.0f;
            hysteresis = 0.46f;
            break;
        case 3: // Chrome - clean and bright, low noise
        default:
            tapeCurve = 1.22f;
            tapeAsymmetry = 0.12f;
            tapeHiss = 0.18f;
            hfDampingMs = 18.0f;
            hysteresis = 0.24f;
            break;
    }

    const float driveAmount = 0.28f + driveCurve * 1.9f;
    const float biasAmount = 0.18f + biasCurve * 1.55f;
    const float mixAmount = 0.12f + mixCurve * 0.88f;
    const float wowDepth = wowCurve * (0.05f + speedScale * 0.08f);
    const float flutterDepth = flutterCurve * (0.08f + speedScale * 0.09f);
    const float speedBias = 0.84f + speedScale * 0.30f;

    // Tone tilt: 0 = warm/soft, 1 = open/bright. Cached because it feeds an
    // exponential used per channel, per block rather than per sample.
    if (std::abs (tone - previousTone) > 1.0e-5f)
    {
        previousTone = tone;
        toneLpAc = onePoleCoefficient (33.0f * std::pow (0.30f, toneCurve), sampleRate);
        toneLpBc = onePoleCoefficient (0.55f + 15.0f * toneCurve, sampleRate);
    }

    const float hfPostCoefficient = onePoleCoefficient (hfDampingMs, sampleRate);
    const float hissGain = tapeHiss * 0.00085f;

    // Output staging: the loudness the model adds is balanced out here, so OUTPUT
    // is a clean, calibrated +/- dB trim rather than an extra hidden gain stage.
    const float driveGainCompensation = 0.52f + (1.0f - driveCurve) * 0.22f;
    const float finalOutputGain = 0.72f * driveGainCompensation * (0.94f + speedScale * 0.08f);

    // Continuous pseudo-random tape noise: two interleaved LCG streams, one per
    // channel, so the hiss is uncorrelated left/right and free of clock patterns.
    static thread_local std::uint32_t noiseState = 0x1b873593u;
    const auto nextNoise = [&noiseState]() -> float
    {
        noiseState = noiseState * 1664525u + 1013904223u;
        return static_cast<float> ((noiseState >> 8) & 0x00ffffffu) * (1.0f / 8388608.0f) - 1.0f;
    };

    const int activeChannels = juce::jmin (2, totalNumInputChannels);

    std::array<float*, 2> channelData {};
    for (int channel = 0; channel < activeChannels; ++channel)
        channelData[static_cast<std::size_t> (channel)] = buffer.getWritePointer (channel);

    float inputPeak = 0.0f;
    float outputPeak = 0.0f;
    double inputSquares = 0.0;
    double outputSquares = 0.0;
    float peakReductionDb = 0.0f;
    float driftAccumulator = 0.0f;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const float inputGain = inputGainSmoothed.getNextValue();
        const float outputGain = outputGainSmoothed.getNextValue();
        const float currentMix = mixSmoothed.getNextValue();
        const float currentWidth = widthSmoothed.getNextValue();
        const float bypassMix = bypassSmoothed.getNextValue();

        std::array<float, 2> tapeOutput {};

        for (int channel = 0; channel < activeChannels; ++channel)
        {
            auto& wowPhase = channel == 0 ? wowPhaseL : wowPhaseR;
            auto& flutterPhase = channel == 0 ? flutterPhaseL : flutterPhaseR;
            auto& hysteresisMemory = channel == 0 ? hystL : hystR;
            auto& highFreqMemory = channel == 0 ? highFreqL : highFreqR;

            const float rawInput = channelData[static_cast<std::size_t> (channel)][sample];
            inputPeak = juce::jmax (inputPeak, std::abs (rawInput));
            inputSquares += static_cast<double> (rawInput) * rawInput;

            const float x = rawInput * inputGain;

            const float wowLfo = std::sin (wowPhase);
            const float flutterLfo = std::sin (flutterPhase);
            const float grainLfo = std::sin (wowPhase * 0.8f + flutterPhase * 1.3f
                                             + static_cast<float> (channel) * 2.4f);

            wowPhase += (twoPi * wowFreq) / sampleRate;
            flutterPhase += (twoPi * flutterFreq) / sampleRate;
            if (wowPhase > twoPi) wowPhase -= twoPi;
            if (flutterPhase > twoPi) flutterPhase -= twoPi;

            if (channel == 0)
                driftAccumulator = wowLfo * wowDepth + flutterLfo * flutterDepth;

            // Transport speed modulation: wow is a slow pitch wander, flutter a fast
            // shimmer, and the grain term adds the fine tape-surface texture.
            const float wowMod = 1.0f + wowLfo * wowDepth;
            const float flutterMod = 1.0f + flutterLfo * flutterDepth;
            const float grainMod = 1.0f + tapeHiss * 0.10f * grainLfo;

            // Record head: pre-emphasis, tape bias offset and drive.
            const float preDrive = x * (1.0f + driveAmount * 1.2f * speedBias);
            const float recordBias = biasAmount * 0.42f;

            // Magnetic hysteresis with memory - the core of the tape sound.
            const float shapedCore = magneticHysteresis (preDrive,
                                                         driveCurve * tapeCurve + hysteresis * 0.25f,
                                                         tapeAsymmetry * recordBias,
                                                         hysteresisMemory[0]);
            hysteresisMemory[2] = hysteresisMemory[1];
            hysteresisMemory[1] = hysteresisMemory[0];
            hysteresisMemory[0] = shapedCore;

            // Tape is a low-pass medium: the faster the tape and the brighter the
            // tone setting, the more top end survives.
            highFreqMemory[1] += (shapedCore - highFreqMemory[1]) * toneLpAc;
            const float afterTapeLoss = highFreqMemory[1];

            // Playback head gap loss and low-frequency head bump.
            highFreqMemory[0] += (afterTapeLoss - highFreqMemory[0]) * hfPostCoefficient;
            const float headLoss = highFreqMemory[0];
            highFreqMemory[1] = afterTapeLoss;

            // Scale compensation, gentle level-dependent bias compression and the
            // tape noise floor ride on the modulated signal.
            const float compensation = tapeCurve / 1.30f;
            const float compressedBias = headLoss * (1.0f - 0.18f * headLoss * headLoss)
                                       / juce::jmax (0.35f, compensation);
            const float noiseFloor = nextNoise() * hissGain;
            const float motioned = (compressedBias + noiseFloor) * wowMod * flutterMod * grainMod;

            // Playback EQ: subtract the low band for air, add it back for body.
            const float lowBand = highFreqMemory[0];
            const float deEmphasised = motioned + (motioned - lowBand) * toneLpBc * 1.7f;

            const float wetMix = deEmphasised * (0.20f + mixAmount * 0.82f);
            const float dryMix = x * (1.0f - mixAmount);
            tapeOutput[static_cast<std::size_t> (channel)] = dryMix + wetMix;
        }

        // ---------------------------------------------------------------------
        //  Tape glue compressor - always on, programme dependent, deliberately
        //  slow so it feels like machine headroom rather than a modern limiter.
        // ---------------------------------------------------------------------
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
        const float compressionGain = juce::Decibels::decibelsToGain (reductionDb);
        peakReductionDb = juce::jmin (peakReductionDb, reductionDb);

        std::array<float, 2> outputSignal {};
        for (int channel = 0; channel < activeChannels; ++channel)
            outputSignal[static_cast<std::size_t> (channel)] =
                tapeOutput[static_cast<std::size_t> (channel)] * compressionGain * finalOutputGain;

        if (activeChannels == 2)
        {
            const float mid = 0.5f * (outputSignal[0] + outputSignal[1]);
            const float side = 0.5f * (outputSignal[0] - outputSignal[1]) * currentWidth;
            outputSignal[0] = mid + side;
            outputSignal[1] = mid - side;
        }

        for (int channel = 0; channel < activeChannels; ++channel)
        {
            auto& destination = channelData[static_cast<std::size_t> (channel)][sample];
            const auto processed = outputSignal[static_cast<std::size_t> (channel)] * outputGain;
            const auto blended = destination + (processed - destination) * bypassMix;
            const auto limitedOut = juce::jlimit (-1.0f, 1.0f, blended);
            destination = limitedOut;
            outputPeak = juce::jmax (outputPeak, std::abs (limitedOut));
            outputSquares += static_cast<double> (limitedOut) * limitedOut;
        }

        juce::ignoreUnused (currentMix);
    }

    const auto measuredSamples = static_cast<double> (numSamples)
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

    // Compressor telemetry: worst-case reduction this block plus an activity
    // envelope the UI can animate, both read without locking.
    gainReductionDb.store (peakReductionDb, std::memory_order_relaxed);
    compressorActivity.store (juce::jlimit (0.0f, 1.0f,
                                            std::sqrt (juce::jmax (0.0f, compressorEnvelope))),
                              std::memory_order_relaxed);

    // Transport drift mapped to 0..1 for the UI wobble, and a harmonic weight
    // derived from how hard the input is being driven into the tape curve.
    transportDrift.store (juce::jlimit (0.0f, 1.0f, 0.5f + driftAccumulator * 2.0f),
                          std::memory_order_relaxed);
    const float driveInto = juce::jlimit (0.0f, 1.0f, inputRms * inputGainSmoothed.getCurrentValue()
                                                          * (0.5f + driveCurve));
    harmonicCharacter.store (driveInto, std::memory_order_relaxed);
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
