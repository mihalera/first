/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <array>
#include <cstdint>

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

        Harmonics are the whole point of this function, so the structure is deliberate:

          - A symmetric tanh generates only ODD harmonics (3rd, 5th...), which read as
            "harder" or "edgier".
          - The asymmetry offset biases the curve so it is no longer symmetric about
            zero. That is what generates EVEN harmonics (2nd, 4th...), and even
            harmonics are the ones heard as warmth, body and "bigger than the source".
          - Two tanh stages with different slopes mean the harmonic content grows
            gradually with level instead of switching on at a threshold, so quiet
            passages stay clean and loud ones bloom - the behaviour of real tape.
    */
    inline float magneticHysteresis (float x, float drive, float asymmetry, float memory)
    {
        const float biased = x + asymmetry;

        // The drive term is exponential in the control value, so the curve keeps bending
        // further as DRIVE is turned up instead of flattening out once tanh has already
        // saturated. That progressive compression is the analogue nonlinearity: without
        // it the top half of the control would add nothing but level.
        const float flux = biased * (1.0f + drive * 2.1f);
        const float soft = std::tanh (flux);

        // A second, gentler saturation stage before the hysteresis. Real magnetic
        // domains respond to the flux in two regions - a soft initial permeability and
        // a harder knee - and stacking two tanh curves with different slopes is what
        // gives low-order harmonics that grow gradually rather than appearing at once.
        const float preSaturated = std::tanh (biased * (0.62f + drive * 1.35f));

        // Anhysteretic curve blended with its own delayed image: this lag is what
        // gives tape its "sticky" transient behaviour.
        const float lagged = memory * 0.62f;
        const float blended = soft * 0.42f
                            + preSaturated * 0.26f
                            + std::tanh ((biased * 0.55f) + lagged) * 0.32f;

        // Remove the bias offset asymmetrically so the effect adds even harmonics
        // instead of merely shifting the signal. The squared term makes the asymmetry
        // level-dependent, mirroring how real bias interacts with signal amplitude.
        return blended - asymmetry * (0.55f + 0.45f * soft * soft);
    }

    /** One-pole low-pass coefficient for a given time constant in milliseconds. */
    inline float onePoleCoefficient (float milliseconds, float sampleRate)
    {
        const float seconds = juce::jmax (0.01f, milliseconds) * 0.001f;
        return 1.0f - std::exp (-1.0f / (seconds * juce::jmax (1.0f, sampleRate)));
    }

    /**
        Soft clipper for the very end of the chain.

        A hard `jlimit (-1, 1)` is the one thing this plugin must not do: it turns any
        overshoot into a flat-topped square edge, which is what "digital clipping" sounds
        like and it is not a tape behaviour. Tape saturates progressively and rolls off,
        so the same treatment is applied at the output.

        The curve is a linear region that blends smoothly into a saturating exponential,
        chosen because it is monotonic, has no jump in value at the knee, and is exactly
        linear for small inputs. That last property matters: below the knee the output is
        bit-for-bit the input, so normal material passes through completely untouched and
        the stage only engages when the signal actually approaches full scale.

        `ceiling` is deliberately just under 1.0 so the output cannot reach full scale even
        when driven hard. That keeps the plugin from hitting the host's own hard limit, and
        it means the resulting distortion is always the gentle kind rather than a wrap.

        Below the knee the response is 1:1. Above it, the output approaches the ceiling
        asymptotically, so no input, however large, can push the result past 0.985.
    */
    inline float softClip (float x) noexcept
    {
        constexpr float ceiling = 0.985f;

        // Below this the curve is 1:1, so quiet and normal-level material is transparent.
        constexpr float knee = 0.70f;

        const auto magnitude = std::abs (x);
        if (magnitude <= knee)
            return x;

        const auto sign = x < 0.0f ? -1.0f : 1.0f;
        const auto excess = magnitude - knee;

        // The value is continuous at the knee (it equals `knee` there) and the slope only
        // changes gradually, so there is no audible corner where saturation begins. The
        // remaining headroom above the knee is what the exponential curve gets to spend.
        const auto compressed = knee + (1.0f - std::exp (-excess)) * (ceiling - knee);
        return sign * juce::jmin (ceiling, compressed);
    }

    /**
        Soft-knee gain computer. `envelopeDb` is the detector level in dBFS.
    */
    inline float softKneeReductionDb (float envelopeDb, float thresholdDb, float kneeDb, float ratio) noexcept
    {
        const float halfKnee = kneeDb * 0.5f;
        const float overshootDb = envelopeDb - thresholdDb;

        if (overshootDb <= -halfKnee)
            return 0.0f;

        if (overshootDb < halfKnee)
        {
            const auto kneeProgress = overshootDb + halfKnee;
            return -(1.0f - 1.0f / ratio) * kneeProgress * kneeProgress / (2.0f * kneeDb);
        }

        return -(1.0f - 1.0f / ratio) * overshootDb;
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
    tapeTypeParam  = parameters.getRawParameterValue ("tape_type");
    speedParam     = parameters.getRawParameterValue ("speed");
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

    // Knob taper only. This skew shapes how knob travel maps onto the parameter value;
    // it has nothing to do with the sound. The analogue nonlinearity lives in the DSP
    // itself (the power curves and the stacked tanh stages in processBlock), which are
    // deliberately NOT linearised - see the note there.
    //
    // The skew is centred low so the gentle end of each control gets more travel, which
    // is where an analogue control is actually judged, while still reaching its maximum.
    const auto percentageRange = [] (float centre)
    {
        juce::NormalisableRange<float> range (minTrack, maxTrack, 0.001f);
        range.setSkewForCentre (centre);
        return range;
    };

    layout.add (std::make_unique<juce::AudioParameterFloat> ("drive", "Drive", percentageRange (0.45f), 0.42f,
                                                            juce::AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("bias", "Bias", percentageRange (0.40f), 0.36f,
                                                            juce::AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("tone", "Tone", percentageRange (0.50f), 0.58f,
                                                            juce::AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("wow", "Wow", percentageRange (0.35f), 0.14f,
                                                            juce::AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("flutter", "Flutter", percentageRange (0.35f), 0.18f,
                                                            juce::AudioParameterFloatAttributes().withLabel ("%")));

    // MIX is a true crossfade, so 50 % is the neutral centre. The default sits there
    // rather than at 62 %, where the control looked like it was doing nothing because
    // the wet path was almost fully in already.
    layout.add (std::make_unique<juce::AudioParameterFloat> ("mix", "Mix", minTrack, maxTrack, 0.50f));

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

void FirstAudioProcessor::setCurrentProgram (int)
{
}

const juce::String FirstAudioProcessor::getProgramName (int)
{
    return {};
}

void FirstAudioProcessor::changeProgramName (int, const juce::String&)
{
}

//==============================================================================
void FirstAudioProcessor::prepareToPlay (double sampleRateToUse, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    sampleRate = static_cast<float> (sampleRateToUse);
    // Stated as a double on purpose: SmoothedValue::reset takes the ramp length in seconds
    // as a double, and `auto` here would have deduced the same type silently. Naming it
    // makes the intent explicit and keeps the literal from looking like a float that lost
    // its suffix.
    const double smoothingSeconds = 0.02;
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
    inputPeakDb.store (-70.0f, std::memory_order_relaxed);
    inputRmsDb.store (-70.0f, std::memory_order_relaxed);
    inputLufs.store (-70.0f, std::memory_order_relaxed);
    inputVuDb.store (-70.0f, std::memory_order_relaxed);
    inputCombinedDb.store (-70.0f, std::memory_order_relaxed);
    inputClipping.store (false, std::memory_order_relaxed);
    outputPeakLevel.store (0.0f, std::memory_order_relaxed);
    outputRmsLevel.store (0.0f, std::memory_order_relaxed);
    outputPeakDb.store (-70.0f, std::memory_order_relaxed);
    outputRmsDb.store (-70.0f, std::memory_order_relaxed);
    outputLufs.store (-70.0f, std::memory_order_relaxed);
    outputVuDb.store (-70.0f, std::memory_order_relaxed);
    outputCombinedDb.store (-70.0f, std::memory_order_relaxed);
    outputClipping.store (false, std::memory_order_relaxed);
    inputGainReductionDb.store (0.0f, std::memory_order_relaxed);
    outputGainReductionDb.store (0.0f, std::memory_order_relaxed);
    inputCompressorActivity.store (0.0f, std::memory_order_relaxed);
    outputCompressorActivity.store (0.0f, std::memory_order_relaxed);
    compressorActivity.store (0.0f, std::memory_order_relaxed);
    transportDrift.store (0.5f, std::memory_order_relaxed);
    harmonicCharacter.store (0.0f, std::memory_order_relaxed);
    bypassActive.store (false, std::memory_order_relaxed);

    hystL.fill (0.0f);
    hystR.fill (0.0f);
    highFreqL.fill (0.0f);
    highFreqR.fill (0.0f);

    // The hiss band-limit state is per channel and must start empty, or a rate switch would
    // carry a stale filter state into the first block and produce a click.
    hissLowPassL = 0.0f;
    hissLowPassR = 0.0f;

    wowPhaseL = 0.0f;
    wowPhaseR = 0.0f;
    flutterPhaseL = 0.0f;
    flutterPhaseR = 0.0f;
    previousTone = -1.0f;
    toneLpAc = 0.0f;
    toneLpBc = 0.0f;

    // The sample rate changed, so every time-domain constant has to be rebuilt.
    // The tone filters are cached rather than recomputed per block, and their
    // coefficients depend on the rate, so there is exactly one place that is
    // allowed to own them: this method. Phase accumulators are reset as well,
    // otherwise a rate switch would leave wow/flutter at a stale phase and click.
    resetSampleRateDependentState();

    // The tape noise generator is a per-instance LCG so that every plugin instance
    // and every render pass is deterministic, rather than sharing one thread_local
    // stream whose content would depend on how many instances happen to exist.
    noiseState = 0x1b873593u;

    // The safety limiter must start open, otherwise a stale gain from the previous
    // session would duck the first block audibly.
    preLimiterDetector = 0.0f;
    limiterGain = 1.0f;

    harmonicAnalyser.reset();
    evenHarmonicRatio.store (0.0f, std::memory_order_relaxed);
    oddHarmonicRatio.store (0.0f, std::memory_order_relaxed);

    // Loudness metering. The K-weighting filters are built from the sample rate here,
    // so the LUFS reading is correct at every supported rate rather than being tuned
    // for one of them.
    outputLoudness.prepare (sampleRateToUse);
    outputLoudness.reset();
    inputLoudness.prepare (sampleRateToUse);
    inputLoudness.reset();
    vuAverage = 0.0f;
    inputVuAverage = 0.0f;

    inputCompressor.reset();
    outputCompressor.reset();

    // The final compensation starts from unity so the first block is not nudged by a
    // stale correction from a previous session or sample rate.
    smoothedCompensationDb = 0.0f;
}

void FirstAudioProcessor::resetSampleRateDependentState()
{
    // Wow and flutter are very low frequency modulators, but they are advanced as
    // phase increments per sample, so a stale phase after a rate change is audible
    // as a click. Starting them from zero makes the transport restart cleanly.
    wowPhaseL = 0.0f;
    wowPhaseR = 0.0f;
    flutterPhaseL = 0.0f;
    flutterPhaseR = 0.0f;

    // Force the tone filter cache to rebuild against the new rate.
    previousTone = -1.0f;
    if (toneParam != nullptr)
        updateToneCoefficients (toneParam->load());
}

void FirstAudioProcessor::updateToneCoefficients (float toneValue)
{
    // Tone tilt: 0 = warm/soft, 1 = open/bright. Both coefficients are one-pole
    // filters expressed in milliseconds, so they scale with the sample rate.
    const auto toneCurve = std::pow (toneValue, 0.92f);
    toneLpAc = onePoleCoefficient (33.0f * std::pow (0.30f, toneCurve), sampleRate);
    toneLpBc = onePoleCoefficient (0.55f + 15.0f * toneCurve, sampleRate);
    previousTone = toneValue;
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
                // Note: getReadPointer() hands back `const float*`, so the value has to
                // be read as a float here - with a 64-bit `SampleType` build (which the
                // VST3 target can be generated as) `auto` would deduce double and every
                // call below would be ambiguous.
                const auto value = static_cast<float> (data[sample]);
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

        // While fully bypassed the plugin is transparent, so every loudness view reads
        // the dry signal and the clipping lamp reflects what is actually passing through.
        const auto bypassPeakDb = juce::Decibels::gainToDecibels (bypassPeak, -70.0f);
        const auto bypassRmsDb = juce::Decibels::gainToDecibels (bypassRms, -70.0f);
        outputPeakDb.store (bypassPeakDb, std::memory_order_relaxed);
        outputRmsDb.store (bypassRmsDb, std::memory_order_relaxed);
        outputLufs.store (bypassRmsDb, std::memory_order_relaxed);
        outputVuDb.store (bypassRmsDb, std::memory_order_relaxed);
        outputCombinedDb.store (bypassPeakDb * 0.25f + bypassRmsDb * 0.75f,
                                std::memory_order_relaxed);
        outputClipping.store (bypassPeak > 1.0f, std::memory_order_relaxed);

        // Bypassed, the input and output are the same signal, so the input meter reports
        // the same four-way reading.
        inputPeakDb.store (bypassPeakDb, std::memory_order_relaxed);
        inputRmsDb.store (bypassRmsDb, std::memory_order_relaxed);
        inputLufs.store (bypassRmsDb, std::memory_order_relaxed);
        inputVuDb.store (bypassRmsDb, std::memory_order_relaxed);
        inputCombinedDb.store (bypassPeakDb * 0.25f + bypassRmsDb * 0.75f,
                               std::memory_order_relaxed);
        inputClipping.store (bypassPeak > 1.0f, std::memory_order_relaxed);
        inputGainReductionDb.store (0.0f, std::memory_order_relaxed);
        outputGainReductionDb.store (0.0f, std::memory_order_relaxed);
        inputCompressorActivity.store (0.0f, std::memory_order_relaxed);
        outputCompressorActivity.store (0.0f, std::memory_order_relaxed);
        compressorActivity.store (0.0f, std::memory_order_relaxed);
        return;
    }

    bypassActive.store (false, std::memory_order_relaxed);

    const auto tapeType = static_cast<int> (tapeTypeParam->load());
    const auto speed = static_cast<int> (speedParam->load());
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

    // -------------------------------------------------------------------------
    //  Analogue transfer curves.
    //
    //  These are deliberately NONLINEAR and must stay that way: they are the model of
    //  the machine, not a control taper. A tape stage bends gently at low levels and
    //  enters saturation hard as it is pushed, and that progressive bending is what
    //  produces the harmonics. Linearising them (as they briefly were) removes the
    //  analogue character entirely.
    //
    //  The knob taper is handled by the skewed NormalisableRange in
    //  createParameterLayout, so these power curves are free to be the sound-shaping
    //  functions they should be.
    // -------------------------------------------------------------------------
    const auto driveCurve = std::pow (drive, 1.45f);
    const auto biasCurve = std::pow (bias, 1.30f);
    const auto wowCurve = std::pow (wow, 1.55f);
    const auto flutterCurve = std::pow (flutter, 1.45f);

    // MIX is the one control that must stay linear: it is a plain dry/wet crossfade,
    // and any curve on it would only make the blend disagree with its own readout.
    const auto mixCurve = mix;

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

    // Dry/wet blend. MIX is a genuine crossfade: at 0 % the signal is untouched dry
    // and at 100 % it is fully through the tape path. Previously the wet side had a
    // 12 % floor and the dry side was never fully removed, so MIX could not reach a
    // clean bypass or a fully saturated signal and its travel felt dead at the ends.
    const float wetGain = mixCurve;
    const float dryGain = 1.0f - mixCurve;
    const float wowDepth = wowCurve * (0.05f + speedScale * 0.08f);
    const float flutterDepth = flutterCurve * (0.08f + speedScale * 0.09f);
    const float speedBias = 0.84f + speedScale * 0.30f;

    // Tone tilt: 0 = warm/soft, 1 = open/bright. Both coefficients are one-pole
    // filters in milliseconds, so they are rebuilt whenever the tone control moves,
    // and also whenever the sample rate changes (see resetSampleRateDependentState).
    if (std::abs (tone - previousTone) > 1.0e-5f)
        updateToneCoefficients (tone);

    const float hfPostCoefficient = onePoleCoefficient (hfDampingMs / speedScale, sampleRate);

    // Tape hiss is a continuous noise floor, so its density is expressed per sample and
    // therefore scales with the sample rate.
    //
    // Two things are needed for this to stay correct up to 192 kHz:
    //
    //   1. The noise is BAND-LIMITED by the coefficient below rather than running white all
    //      the way to Nyquist. Real tape hiss comes from the medium and stops well short of
    //      the top octave; left white it would spread over 96 kHz at a 192 kHz rate, which
    //      sounds like a bright digital hiss instead of tape. Filtering it also means the
    //      noise occupies a fixed bandwidth at every rate, which is what makes point 2 work.
    //
    //   2. Because the bandwidth is now fixed, the gain no longer needs the sqrt(rate)
    //      compensation - that term existed only to keep total white-noise energy constant
    //      as more samples were added per second. Adding it on top of the band limit would
    //      double-compensate and the hiss would get louder as the rate went up, which is
    //      exactly the bug this replaces.
    const float hissGain = tapeHiss * 0.00085f;

    // onePoleCoefficient takes MILLISECONDS, so the 16 kHz corner is converted to the
    // equivalent time constant first: 1 / (2*pi*f). Passing 16000 here would be read as a
    // 16-second time constant, which would all but remove the hiss instead of shaping it.
    const float hissBandLimit = onePoleCoefficient (1000.0f / (juce::MathConstants<float>::twoPi * 16000.0f),
                                                    sampleRate);

    // Output staging: the loudness the model adds is balanced out here, so OUTPUT
    // is a clean, calibrated +/- dB trim rather than an extra hidden gain stage.
    //
    // This is the STATIC calibration only: it compensates for the fixed gain the
    // saturation curve adds at a nominal level. The level that is actually lost inside
    // the shaper as it clamps is tracked per sample in the tape loop (driveCompensation),
    // so the two do not fight each other - one sets the operating level, the other keeps
    // the stage gain-neutral as DRIVE and the signal level move.
    const float driveGainCompensation = 0.52f + (1.0f - driveCurve) * 0.22f;
    const float finalOutputGain = 0.72f * driveGainCompensation * (0.94f + speedScale * 0.08f);

    // Continuous pseudo-random tape noise: a 32-bit LCG held per instance, so the
    // hiss is uncorrelated between instances and reproducible for a given one. A
    // thread_local stream would instead couple all plugin instances together and
    // make the noise depend on how many of them happen to be running.
    const auto nextNoise = [] (std::uint32_t& state) -> float
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<float> ((state >> 8) & 0x00ffffffu) * (1.0f / 8388608.0f) - 1.0f;
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
    float inputPeakReductionDb = 0.0f;
    float inputEnvelopeActivity = 0.0f;
    float driftAccumulator = 0.0f;

    // Reference power for the final gain compensation: the power of the signal straight
    // after the INPUT trim, before the first glue compressor touches it. That is the
    // level the user dialled in with INPUT, so it is what the plugin's output should
    // still be tracking once the tape stage and both compressors have had their way
    // with the signal. Accumulated across this block only - the correction it drives is
    // itself smoothed, so no block-to-block history is needed here.
    float referenceBlockPower = 0.0f;

    // Loudness metering accumulators for this block. currentLufs is carried out of the
    // per-sample loop because the K-weighted follower is stateful across the whole block.
    float currentLufs = -70.0f;
    bool clippingThisBlock = false;
    float currentInputLufs = -70.0f;
    bool inputClippingThisBlock = false;

    // The input side of the K-weighted meter needs the raw signal, but the buffer is
    // processed in place, so the dry input of the current sample is stashed here before
    // the channel loop overwrites it. Two slots are enough for a stereo frame.
    std::array<float, 2> inputChainHistory {};

    // Slow one-pole coefficient for the compensation itself. Deliberately far slower
    // than either compressor, so the correction settles on the programme level instead
    // of pumping along with the transients.
    const float compensationCoefficient = 1.0f - std::exp (
        -1.0f / (juce::jmax (1.0f, sampleRate) * 0.45f));

    // -------------------------------------------------------------------------
    //  Glue compressor operating points.
    //
    //  The two stages are independent processors, but each one is calibrated from
    //  the trim control that feeds it. Turning INPUT up pushes this scaled signal
    //  into the input stage, so that stage clamps down sooner and more firmly;
    //  OUTPUT works the same way on the output stage, which then spends the trimmed
    //  level on the output trim. A negative trim backs a stage off completely and a
    //  positive one leans on it hard, which is what makes the two controls feel
    //  like real gain staging rather than plain volume.
    // -------------------------------------------------------------------------
    const float inputDriveLoad = juce::jlimit (0.0f, 1.0f, (inputDb + 24.0f) / 48.0f);
    const float outputDriveLoad = juce::jlimit (0.0f, 1.0f, (outputDb + 24.0f) / 48.0f);

    const float inputThresholdDb = -17.0f + inputDriveLoad * 14.0f;    // -17 .. -3 dB
    const float inputKneeDb = 10.0f - inputDriveLoad * 3.0f;           // 10 .. 7 dB
    const float inputCompressorRatio = 1.15f + inputDriveLoad * 0.25f; // 1.15 .. 1.40 : 1
    const float inputReductionLimitDb = -(2.0f + inputDriveLoad * 5.0f);

    const float outputThresholdDb = -18.0f + outputDriveLoad * 15.0f;  // -18 .. -3 dB
    const float outputKneeDb = 7.0f - outputDriveLoad * 2.0f;          // 7 .. 5 dB
    const float outputCompressorRatio = 1.16f + outputDriveLoad * 0.18f;
    const float outputReductionLimitDb = -(2.5f + outputDriveLoad * 5.0f);

    // Makeup is part of each stage, and it is driven by the same trim control: a
    // boosted trim pays its reduction back and a trimmed-down one simply backs off,
    // so neither stage can quietly undo the balance the user dialled in.
    const float inputDegree = juce::jlimit (0.0f, 1.0f, (inputDb + 16.0f) / 32.0f);
    const float outputDegree = juce::jlimit (0.0f, 1.0f, (outputDb + 16.0f) / 32.0f);
    const float inputMakeupFraction = 0.30f + inputDegree * 0.35f;
    const float outputMakeupFraction = 0.35f + outputDegree * 0.35f;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const float inputGain = inputGainSmoothed.getNextValue();
        const float outputGain = outputGainSmoothed.getNextValue();
        const float currentWidth = widthSmoothed.getNextValue();
        const float bypassMix = bypassSmoothed.getNextValue();

        // Per-sample reference power for the final compensation, reset each iteration.
        referenceBlockPower = 0.0f;

        // The dry/wet blend is handled per channel below, so the smoothed mix value
        // only has to be advanced once per sample to stay in step with the others.
        mixSmoothed.getNextValue();

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
            inputChainHistory[static_cast<std::size_t> (channel)] = rawInput;

            // Input-side VU ballistic and clipping, measured on the raw signal arriving
            // at the plugin so the INPUT meter shows what the host is actually sending.
            const auto inputVuCoefficient = 1.0f - std::exp (-1.0f / (sampleRate * 0.3f));
            inputVuAverage += (std::abs (rawInput) - inputVuAverage) * inputVuCoefficient;
            if (std::abs (rawInput) > 1.0f)
                inputClippingThisBlock = true;

            // Input trim, then the input stage glue compressor. The detector runs on
            // the trimmed signal, which is the level the INPUT control is asking for,
            // and the resulting gain is folded into that same signal - so the tape
            // hears one consistent, controlled level rather than a re-trim.
            const float inputTrimmed = rawInput * inputGain;

            // Track the power of the post-INPUT, pre-first-compressor signal. Only the
            // first channel contributes so the reference is a mono measurement, which
            // keeps it independent of how the stereo material is panned.
            if (channel == 0)
                referenceBlockPower += inputTrimmed * inputTrimmed;
            // ------------------------------------------------------------------
            //  Input stage glue compressor. It sits straight after the input trim,
            //  so the signal that reaches the tape is always the controlled one and
            //  the INPUT control is what drives this stage - exactly like hitting a
            //  recorder input harder. Fully independent of the output stage: this
            //  detector only ever sees the trimmed input signal.
            // ------------------------------------------------------------------
            const float inputEnvelopeDb = inputCompressor.processDetection (
                inputTrimmed * inputTrimmed, sampleRate, 0.16f, 0.85f, inputDriveLoad);
            const float inputReductionDb = juce::jmax (inputReductionLimitDb,
                                                       softKneeReductionDb (inputEnvelopeDb,
                                                                            inputThresholdDb,
                                                                            inputKneeDb,
                                                                            inputCompressorRatio));
            const float inputCompressionGain = juce::Decibels::decibelsToGain (inputReductionDb);

            inputPeakReductionDb = juce::jmin (inputPeakReductionDb, inputReductionDb);
            inputEnvelopeActivity = juce::jmax (inputEnvelopeActivity,
                                                inputCompressor.getEnvelopeActivity());

            const float x = inputTrimmed * inputCompressionGain;

            const float wowLfo = std::sin (wowPhase);
            const float flutterLfo = std::sin (flutterPhase);
            const float grainLfo = std::sin (wowPhase * 0.8f + flutterPhase * 1.3f
                                             + static_cast<float> (channel) * 2.4f);

            wowPhase += (twoPi * wowFreq) / sampleRate;
            flutterPhase += (twoPi * flutterFreq) / sampleRate;
            // Wrap rather than a single subtraction: at very low rates, or with a
            // high wow/flutter setting, one increment can exceed a full turn and a
            // lone `-= twoPi` would leave the phase running away unbounded.
            wowPhase = std::fmod (wowPhase, twoPi);
            flutterPhase = std::fmod (flutterPhase, twoPi);

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

            // Measure what the shaper actually produced, comparing its input against its
            // output. Harmonics are the reason this plugin exists, so the character is
            // observed rather than assumed: the analyser separates the even content
            // (warmth, from the bias asymmetry) from the odd content (edge, from the
            // symmetric tanh). Only the left channel is measured, since the two are driven
            // identically and doubling the analyser would cost twice as much for the same
            // reading.
            if (channel == 0)
                harmonicAnalyser.analyse (preDrive, shapedCore, sampleRate);

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

            // Nonlinearity costs level: the two tanh stages in magneticHysteresis clamp
            // the signal, so the harmonic character they add would be accompanied by a
            // gain drop that made DRIVE feel like a volume trim. This estimates how much
            // amplitude the shaper removed (it tracks the shaper's own output level) and
            // pays it back, so the stage stays roughly gain-neutral and the added
            // harmonics are heard as tone rather than as a level change.
            const float shapedLevel = std::abs (shapedCore);
            const float shaperLoss = juce::jlimit (0.0f, 1.0f,
                                                   driveAmount * 0.30f * (1.0f - shapedLevel * 0.85f));
            const float driveCompensation = 1.0f + shaperLoss * 1.35f;

            const float compressedBias = headLoss * (1.0f - 0.18f * headLoss * headLoss)
                                       / juce::jmax (0.35f, compensation)
                                       * juce::jlimit (0.45f, 2.2f, driveCompensation);

            // The hiss is band-limited rather than white, so its spectrum is the same at
            // 44.1 kHz and 192 kHz and it reads as tape noise instead of digital hiss. The
            // filter state is per channel so the two sides stay uncorrelated.
            auto& hissLowPass = channel == 0 ? hissLowPassL : hissLowPassR;
            const float rawHiss = nextNoise (noiseState) * hissGain;
            hissLowPass += (rawHiss - hissLowPass) * hissBandLimit;

            // The band limit costs most of the noise power, so the gain is compensated by
            // the inverse of the filter's RMS response. Deriving it from the coefficient
            // rather than a fixed number keeps the perceived level flat at every rate.
            const float hissLevelCompensation = 1.0f / std::sqrt (juce::jmax (0.05f, hissBandLimit));
            const float noiseFloor = hissLowPass * hissLevelCompensation;

            const float motioned = (compressedBias + noiseFloor) * wowMod * flutterMod * grainMod;

            // Playback EQ: subtract the low band for air, add it back for body.
            const float lowBand = highFreqMemory[0];
            const float deEmphasised = motioned + (motioned - lowBand) * toneLpBc * 1.7f;

            // Equal-gain crossfade between the dry input and the fully processed tape
            // signal. The wet path is level-matched in finalOutputGain, so 0 % is a
            // transparent dry signal and 100 % is all tape, with no dip in the middle.
            const float wetMix = deEmphasised * wetGain;
            const float dryMix = x * dryGain;
            tapeOutput[static_cast<std::size_t> (channel)] = dryMix + wetMix;
        }

        // ---------------------------------------------------------------------
        //  Output stage glue compressor. It is immediately before the output trim,
        //  so the headroom it creates is spent directly on the OUTPUT control and
        //  the level leaving the plugin stays calibrated. Independent of the input
        //  stage: this detector only sees the tape output, and its operating point
        //  follows the OUTPUT trim.
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

        const float envelopeDb = outputCompressor.processDetection (detectorPower, sampleRate,
                                                                    0.20f, 1.00f, outputDriveLoad);
        const float reductionDb = juce::jmax (outputReductionLimitDb,
                                              softKneeReductionDb (envelopeDb,
                                                                   outputThresholdDb,
                                                                   outputKneeDb,
                                                                   outputCompressorRatio));
        const float compressionGain = juce::Decibels::decibelsToGain (reductionDb);
        peakReductionDb = juce::jmin (peakReductionDb, reductionDb);

        // Each stage pays back part of the reduction it applied, weighted by how hard
        // its trim control is driving it, so both together cannot leave the machine
        // quieter than it arrived.
        const float inputMakeup = juce::Decibels::decibelsToGain (-inputPeakReductionDb
                                                                  * inputMakeupFraction);
        const float outputMakeup = juce::Decibels::decibelsToGain (-reductionDb
                                                                   * outputMakeupFraction);
        const float stageGain = compressionGain * outputMakeup * finalOutputGain * inputMakeup;

        // ---------------------------------------------------------------------
        //  Final gain compensation.
        //
        //  The tape stage, the two glue compressors and their makeup all change the
        //  level, so what leaves the plugin is not necessarily the level the user
        //  dialled in with INPUT. This compares the finished signal against the
        //  reference taken straight after the input trim and before the first
        //  compressor, and pays back whatever was lost or gained, so the plugin holds
        //  its output level as DRIVE, TAPE TYPE, SPEED and MIX are changed instead of
        //  drifting louder or quieter with every edit.
        //
        //  The correction is smoothed with a slow one-pole, so it tracks the overall
        //  programme level rather than fighting the moment-to-moment compression. That
        //  keeps the compressors' punch intact: it corrects the average, not transients.
        // ---------------------------------------------------------------------
        float compensationGain = 1.0f;
        if (referenceBlockPower > 0.0f)
        {
            // Power of the signal as it leaves the compressors and tape stage, but
            // BEFORE this compensation is folded in - measuring the compensated output
            // would make the correction chase its own tail.
            float postCompressorPower = 0.0f;
            for (int channel = 0; channel < activeChannels; ++channel)
            {
                const auto signal = tapeOutput[static_cast<std::size_t> (channel)] * stageGain;
                postCompressorPower += signal * signal;
            }

            if (postCompressorPower > 1.0e-12f)
            {
                // How far the finished signal has fallen below the reference taken after
                // the input trim. Positive means level was lost and needs paying back.
                const auto levelRatioDb = juce::Decibels::gainToDecibels (
                    std::sqrt (referenceBlockPower / postCompressorPower), 0.0f);

                // Only ever restore, never exaggerate: the correction may recover a loss
                // but must not become an extra boost stage of its own.
                const auto targetCompensationDb = juce::jlimit (0.0f, 12.0f, levelRatioDb);
                smoothedCompensationDb += (targetCompensationDb - smoothedCompensationDb)
                                        * compensationCoefficient;
                compensationGain = juce::Decibels::decibelsToGain (smoothedCompensationDb);
            }
        }

        std::array<float, 2> outputSignal {};
        for (int channel = 0; channel < activeChannels; ++channel)
            outputSignal[static_cast<std::size_t> (channel)] =
                tapeOutput[static_cast<std::size_t> (channel)] * stageGain * compensationGain;

        if (activeChannels == 2)
        {
            const float mid = 0.5f * (outputSignal[0] + outputSignal[1]);
            const float side = 0.5f * (outputSignal[0] - outputSignal[1]) * currentWidth;
            outputSignal[0] = mid + side;
            outputSignal[1] = mid - side;
        }

        // Apply the output trim before limiting, not after. The limiter has to be the last
        // thing that touches the level, otherwise a boost on the OUTPUT control would push
        // the signal straight past the ceiling it just established and the clipper would
        // be doing the work instead.
        for (int channel = 0; channel < activeChannels; ++channel)
            outputSignal[static_cast<std::size_t> (channel)] *= outputGain;

        // ---------------------------------------------------------------------
        //  Program-dependent safety limiter.
        //
        //  This exists so the soft clipper downstream stays idle. It watches the loudest
        //  channel of the frame and pulls the gain back whenever the signal is heading for
        //  the ceiling, then releases slowly enough to stay musical.
        //
        //  It is a GAIN stage rather than a shaper, so it adds no harmonics of its own:
        //  the only distortion in the output is the tape stage's, which is intentional.
        //  Normal material is limited transparently and only genuinely excessive level
        //  ever reaches the clipper.
        // ---------------------------------------------------------------------
        float framePeak = 0.0f;
        for (int channel = 0; channel < activeChannels; ++channel)
            framePeak = juce::jmax (framePeak, std::abs (outputSignal[static_cast<std::size_t> (channel)]));

        // A very fast attack catches transients before they overshoot; the release is
        // short enough to recover between events without pumping on sustained material.
        const auto limiterCeiling = 0.94f;
        const auto detectorAttack = 1.0f - std::exp (-1.0f / (sampleRate * 0.0005f));
        const auto detectorRelease = 1.0f - std::exp (-1.0f / (sampleRate * 0.080f));
        const auto detectorCoefficient = framePeak > preLimiterDetector ? detectorAttack
                                                                        : detectorRelease;
        preLimiterDetector += (framePeak - preLimiterDetector) * detectorCoefficient;

        // Gain required to bring the peak back to the ceiling. It is smoothed separately
        // from the detector so the correction is continuous and cannot click.
        const auto requiredGain = preLimiterDetector > limiterCeiling
                                    ? limiterCeiling / preLimiterDetector
                                    : 1.0f;
        const auto gainSmoothing = requiredGain < limiterGain
                                     ? 1.0f - std::exp (-1.0f / (sampleRate * 0.0004f))
                                     : 1.0f - std::exp (-1.0f / (sampleRate * 0.120f));
        limiterGain += (requiredGain - limiterGain) * gainSmoothing;

        for (int channel = 0; channel < activeChannels; ++channel)
            outputSignal[static_cast<std::size_t> (channel)] *= limiterGain;

        for (int channel = 0; channel < activeChannels; ++channel)
        {
            auto& destination = channelData[static_cast<std::size_t> (channel)][sample];
            const auto blended = destination + (outputSignal[static_cast<std::size_t> (channel)]
                                                 - destination) * bypassMix;

            // Protection for the output, in two stages:
            //
            //   1. The safety limiter above keeps normal programme below the ceiling.
            //   2. The soft clipper only bends what still overshoots - a peak faster than
            //      the limiter's attack - so it is the last resort, not the mechanism that
            //      keeps the level in range.
            //
            // The flag therefore means "the soft clipper had to act", not merely "the
            // signal was loud", so the meter warns about real distortion.
            if (std::abs (blended) > 0.985f)
                clippingThisBlock = true;

            const auto limitedOut = softClip (blended);
            destination = limitedOut;
            outputPeak = juce::jmax (outputPeak, std::abs (limitedOut));
            outputSquares += static_cast<double> (limitedOut) * limitedOut;

            // VU ballistic: a 300 ms average, fed once per channel so it measures the
            // same programme average a hardware VU would.
            const auto vuCoefficient = 1.0f - std::exp (-1.0f / (sampleRate * 0.3f));
            vuAverage += (std::abs (limitedOut) - vuAverage) * vuCoefficient;
        }

        // K-weighted loudness runs on the final stereo frame, after the width stage, so
        // it reports what actually leaves the plugin.
        if (activeChannels == 2)
            currentLufs = outputLoudness.processFrame (outputSignal[0] * outputGain,
                                                       outputSignal[1] * outputGain, sampleRate);
        else if (activeChannels == 1)
            currentLufs = outputLoudness.processFrame (outputSignal[0] * outputGain,
                                                       outputSignal[0] * outputGain, sampleRate);

        // The INPUT meter runs the same K-weighting on the raw signal at the plugin's
        // own input, so the two meters can be compared directly. The channels are read
        // back from the buffer because the dry input was overwritten in place.
        if (activeChannels == 2)
            currentInputLufs = inputLoudness.processFrame (inputChainHistory[0], inputChainHistory[1],
                                                           sampleRate);
        else if (activeChannels == 1)
            currentInputLufs = inputLoudness.processFrame (inputChainHistory[0], inputChainHistory[0],
                                                           sampleRate);
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

    // -------------------------------------------------------------------------
    //  Four-way loudness meter.
    //
    //  Each view is converted to dB and then averaged with equal weight, so no single
    //  scale can dominate the combined reading:
    //
    //    dB   - true peak of the block, the only view that reports clipping
    //    RMS  - electrical average of the whole block
    //    LUFS - K-weighted, so it tracks perceived loudness rather than voltage
    //    VU   - 300 ms ballistic average, the classic programme-level display
    //
    //  Averaging in dB (rather than linear) keeps the four views comparable, because
    //  all four are already level scales; converting to linear first would let a single
    //  silent view drag the result toward -infinity.
    // -------------------------------------------------------------------------
    const auto outputPeakDbValue = juce::Decibels::gainToDecibels (outputPeak, -70.0f);
    const auto outputRmsDbValue = juce::Decibels::gainToDecibels (outputRms, -70.0f);
    const auto outputVuDbValue = juce::Decibels::gainToDecibels (vuAverage, -70.0f);

    constexpr float viewWeight = 0.25f;
    const auto combinedDb = outputPeakDbValue * viewWeight
                          + outputRmsDbValue * viewWeight
                          + currentLufs * viewWeight
                          + outputVuDbValue * viewWeight;

    outputPeakDb.store (outputPeakDbValue, std::memory_order_relaxed);
    outputRmsDb.store (outputRmsDbValue, std::memory_order_relaxed);
    outputLufs.store (currentLufs, std::memory_order_relaxed);
    outputVuDb.store (outputVuDbValue, std::memory_order_relaxed);
    outputCombinedDb.store (combinedDb, std::memory_order_relaxed);
    outputClipping.store (clippingThisBlock, std::memory_order_relaxed);

    // Same four-way treatment for the input side, so the two meters are directly
    // comparable: the difference between them is what the plugin did to the level.
    const auto inputPeakDbValue = juce::Decibels::gainToDecibels (inputPeak, -70.0f);
    const auto inputRmsDbValue = juce::Decibels::gainToDecibels (inputRms, -70.0f);
    const auto inputVuDbValue = juce::Decibels::gainToDecibels (inputVuAverage, -70.0f);

    const auto inputCombinedValue = inputPeakDbValue * viewWeight
                                  + inputRmsDbValue * viewWeight
                                  + currentInputLufs * viewWeight
                                  + inputVuDbValue * viewWeight;

    inputPeakDb.store (inputPeakDbValue, std::memory_order_relaxed);
    inputRmsDb.store (inputRmsDbValue, std::memory_order_relaxed);
    inputLufs.store (currentInputLufs, std::memory_order_relaxed);
    inputVuDb.store (inputVuDbValue, std::memory_order_relaxed);
    inputCombinedDb.store (inputCombinedValue, std::memory_order_relaxed);
    inputClipping.store (inputClippingThisBlock, std::memory_order_relaxed);

    // Harmonic character, measured on the shaper earlier in the block. Published so the
    // panel can show the even/odd balance the tape stage is actually producing.
    evenHarmonicRatio.store (harmonicAnalyser.getEvenRatio(), std::memory_order_relaxed);
    oddHarmonicRatio.store (harmonicAnalyser.getOddRatio(), std::memory_order_relaxed);

    // Blocks of zero samples arrive during silence (and with some host buffer sizes),
    // and by then the per-sample smoothing would never have been advanced.
    inputGainSmoothed.skip (numSamples);
    outputGainSmoothed.skip (numSamples);
    mixSmoothed.skip (numSamples);
    widthSmoothed.skip (numSamples);
    bypassSmoothed.skip (numSamples);

    // Glue compressor telemetry: worst-case reduction this block plus an activity
    // envelope the UI can animate, both read without locking. Each stage publishes
    // its own reduction and activity so the editor can give it a dedicated meter.
    inputGainReductionDb.store (inputPeakReductionDb, std::memory_order_relaxed);
    outputGainReductionDb.store (peakReductionDb, std::memory_order_relaxed);
    inputCompressorActivity.store (juce::jlimit (0.0f, 1.0f, inputEnvelopeActivity),
                                   std::memory_order_relaxed);
    outputCompressorActivity.store (juce::jlimit (0.0f, 1.0f,
                                                  outputCompressor.getEnvelopeActivity()),
                                    std::memory_order_relaxed);
    compressorActivity.store (juce::jlimit (0.0f, 1.0f,
                                            juce::jmax (inputEnvelopeActivity,
                                                        outputCompressor.getEnvelopeActivity())),
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
