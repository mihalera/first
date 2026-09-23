/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <cmath>

//==============================================================================
/**
    One glue compressor stage. The two stages in this plugin are completely
    independent: each keeps its own detector envelope and its own gain computer,
    and neither reads the other's state. They are driven purely by the signal
    that reaches them and by the Input / Output parameters.

    Time constants are SEMI-AUTOMATIC - there are no attack or release controls.
    The stage listens to how the signal itself behaves and adapts both constants
    as it goes, along three independent axes:

      1. Programme level - the harder the stage is being driven, the slower it moves,
         which is what makes it read as machine headroom rather than a limiter.
      2. Transient vs sustained material - it tracks a fast and a slow view of the
         signal; when the fast view runs ahead of the slow one we are on a transient,
         so attack is quickened to catch it. When the two agree we are in sustained
         programme, so attack is relaxed and release is stretched, letting the stage
         breathe with the music instead of pumping.
      3. Envelope fill - as the detector fills up, release lengthens further, so a
         dense passage is held together and a sparse one recovers quickly.
*/
struct GlueCompressor
{
    float envelope = 0.0f;

    // A second, much slower follower used only to tell transients apart from sustained
    // programme. Keeping it separate from `envelope` means the adaptation never feeds
    // back into the gain computation itself.
    float slowEnvelope = 0.0f;

    void reset() noexcept
    {
        envelope = 0.0f;
        slowEnvelope = 0.0f;
    }

    /** Envelope level of the most recent block, as 0..1 linear activity. */
    float getEnvelopeActivity() const noexcept
    {
        return juce::jlimit (0.0f, 1.0f, std::sqrt (juce::jmax (0.0f, envelope)));
    }

    /** 0 = sustained programme, 1 = sharp transient. Used by the UI to show activity. */
    float getTransientAmount() const noexcept
    {
        const auto fast = std::sqrt (juce::jmax (0.0f, envelope));
        const auto slow = std::sqrt (juce::jmax (0.0f, slowEnvelope));
        if (slow <= 1.0e-6f)
            return 0.0f;

        return juce::jlimit (0.0f, 1.0f, (fast - slow) / slow * 1.6f);
    }

    /**
        Pushes signal power through the detector; call once per sample.

        attackBaseSeconds and releaseBaseSeconds are the nominal constants. They are
        then adapted by the three axes described above, so the caller never has to
        schedule attack or release by hand - that is the semi-automatic behaviour.
    */
    float processDetection (float detectorPower, float sampleRate,
                            float attackBaseSeconds, float releaseBaseSeconds,
                            float loadFactor) noexcept
    {
        const float safeRate = juce::jmax (1.0f, sampleRate);

        // -- Axis 3: how full the detector already is --------------------------
        const float envelopeLevel = getEnvelopeActivity();

        // -- Axis 2: transient or sustained? ----------------------------------
        // A time constant roughly a hundred times slower than the detector tracks the
        // running programme level. Comparing the two is enough to tell a drum hit
        // (fast spikes above the slow average) from a sustained pad or vocal line.
        const float slowTimeConstant = juce::jmax (0.05f, releaseBaseSeconds * 3.5f);
        const float slowCoefficient = std::exp (-1.0f / (safeRate * slowTimeConstant));
        slowEnvelope = slowCoefficient * slowEnvelope
                     + (1.0f - slowCoefficient) * detectorPower;

        const float transientAmount = getTransientAmount();

        // -- Axis 1: how hard the stage is being driven -----------------------
        // Load lengthens both constants, so a stage being leaned on turns slow and
        // dense while an idle one stays quick and transparent.
        const float loadStretch = 1.0f + loadFactor * 1.8f;

        // Attack: quick on transients so nothing is missed, relaxed on sustained
        // material so the stage does not clamp the body of the sound. The transient
        // term dominates the level term, because catching a peak matters more.
        const float transientSpeedUp = 1.0f - transientAmount * 0.72f;
        const float attackSeconds = attackBaseSeconds * loadStretch
                                  * juce::jlimit (0.25f, 1.6f, transientSpeedUp)
                                  * (1.0f + envelopeLevel * 0.9f);

        // Release: long on sustained programme and when the detector is full, short on
        // isolated transients so the stage reopens before the next event. This is what
        // gives the classic auto-release feel - dense passages stay together, sparse
        // ones breathe.
        const float releaseStretch = 1.0f
                                   + (1.0f - transientAmount) * 1.15f
                                   + envelopeLevel * 2.4f;
        const float releaseSeconds = releaseBaseSeconds * (1.0f + loadFactor * 2.2f)
                                   * releaseStretch;

        const float timeConstant = detectorPower > envelope ? attackSeconds : releaseSeconds;
        const float coefficient = std::exp (-1.0f / (safeRate * timeConstant));
        envelope = coefficient * envelope + (1.0f - coefficient) * detectorPower;

        return juce::Decibels::gainToDecibels (std::sqrt (juce::jmax (0.0f, envelope)), -100.0f);
    }
};

//==============================================================================
/**
    Together-loudness (LUFS) measurement, following the ITU-R BS.1770 / EBU R128
    weightings: a high-shelf and a high-pass, then mean-square over the measurement
    window. Only the K-weighting is implemented - this is a live meter, not an
    offline loudness normaliser, so gating and true-peak are deliberately left out.

    The filters are biquads in direct form I, rebuilt from the sample rate in
    prepare(), so the reading is correct at every supported rate: 44.1, 48, 88.2,
    96, 176.4 and 192 kHz. The coefficients come from tan(pi * f0 / rate), which is
    exact at any rate, so no additional scaling is needed for a 192 kHz session.
*/
struct LoudnessMeter
{
    void prepare (double sampleRate) noexcept
    {
        const auto rate = juce::jmax (8000.0, sampleRate);

        // Stage 1: high-shelf, roughly +4 dB above 1.5 kHz, as specified for K-weighting.
        {
            const auto f0 = 1681.974450955533;
            const auto gainDb = 3.999843853973347;
            const auto q = 0.7071752369554196;

            const auto k = std::tan (juce::MathConstants<double>::pi * f0 / rate);
            const auto vh = std::pow (10.0, gainDb / 20.0);
            const auto vb = std::pow (vh, 0.4996667741545416);
            const auto denominator = 1.0 + k / q + k * k;

            shelf.b0 = static_cast<float> ((vh + vb * k / q + k * k) / denominator);
            shelf.b1 = static_cast<float> (2.0 * (k * k - vh) / denominator);
            shelf.b2 = static_cast<float> ((vh - vb * k / q + k * k) / denominator);
            shelf.a1 = static_cast<float> (2.0 * (k * k - 1.0) / denominator);
            shelf.a2 = static_cast<float> ((1.0 - k / q + k * k) / denominator);
        }

        // Stage 2: high-pass at 38 Hz, the second half of the K-weighting curve.
        {
            const auto f0 = 38.13547087602444;
            const auto q = 0.5003270373238773;

            const auto k = std::tan (juce::MathConstants<double>::pi * f0 / rate);
            const auto denominator = 1.0 + k / q + k * k;

            highPass.b0 = static_cast<float> (1.0 / denominator);
            highPass.b1 = static_cast<float> (-2.0 / denominator);
            highPass.b2 = static_cast<float> (1.0 / denominator);
            highPass.a1 = static_cast<float> (2.0 * (k * k - 1.0) / denominator);
            highPass.a2 = static_cast<float> ((1.0 - k / q + k * k) / denominator);
        }
    }

    void reset() noexcept
    {
        shelf = {};
        highPass = {};
        meanSquare = 0.0f;
    }

    /** Feeds one stereo frame and returns the current loudness in LUFS. */
    float processFrame (float left, float right, float sampleRate) noexcept
    {
        const auto weightedLeft = highPass.process (shelf.process (left));
        const auto weightedRight = highPass.process (shelf.process (right));

        // BS.1770 sums the per-channel mean squares; the channels here are already
        // gain-weighted equally, so it is a plain sum.
        const auto frameMeanSquare = weightedLeft * weightedLeft
                                   + weightedRight * weightedRight;

        // A 400 ms sliding window, implemented as a one-pole that is close enough for
        // a live display while staying cheap and block-size independent.
        const auto coefficient = std::exp (-1.0f / (juce::jmax (1.0f, sampleRate) * 0.4f));
        meanSquare = coefficient * meanSquare + (1.0f - coefficient) * frameMeanSquare;

        const auto loudness = -0.691f + 10.0f * std::log10 (juce::jmax (1.0e-12f, meanSquare));
        return juce::jmax (-70.0f, loudness);
    }

private:
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;

        float process (float x) noexcept
        {
            const auto y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;
            return y;
        }
    };

    Biquad shelf;
    Biquad highPass;
    float meanSquare = 0.0f;
};

//==============================================================================
/**
    Live harmonic analysis of a nonlinear stage.

    The analogue character of this plugin lives in the harmonics its shaper adds, so rather
    than trusting the curve on paper, the stage is measured: Goertzel filters run at the 2nd
    and 3rd harmonic of a tracked fundamental and report how much energy is even (2nd)
    against odd (3rd) relative to it.

    Those two bins are enough to characterise the stage because the split they show is the
    one that matters: even content is what the bias asymmetry contributes and reads as
    warmth, odd content is what the symmetric tanh contributes and reads as edge. Measuring
    more bins would cost more for no extra insight into that balance.

    Two things make this usable as a live meter: it runs only every N samples so the cost is
    negligible, and it uses the shaper's own input and output, so what it reports is the
    distortion that was actually produced, not a prediction.

    This lives in the header rather than in an anonymous namespace in the .cpp because the
    processor holds one by value as a member, and a member's type has to be visible where
    the class is declared.
*/
struct HarmonicAnalyser
{
    void reset() noexcept
    {
        evenRatio = 0.0f;
        oddRatio = 0.0f;
        fundamentalLevel = 0.0f;
        trackedFrequency = 220.0f;
        samplesSinceCrossing = 1;
        sampleCounter = 0;
        previousPositive = true;
        previous1 = 0.0f;
        previous2 = 0.0f;
        lastMagnitude = 0.0f;
        windowCounter = 0;
    }

    /**
        Feeds one sample of the shaper's input and output.

        Return value: true once a fresh harmonic reading has been produced this call, false
        while the measurement is still accumulating or while there is too little signal to
        measure. Callers that only want the running values can use the getters instead and
        ignore the return entirely.
    */
    bool analyse (float shaperInput, float shaperOutput, float sampleRate)
    {
        // The fundamental is taken from the zero-crossing rate of the input, which is cheap
        // and needs no FFT. The estimate is heavily smoothed because it only has to be in
        // the right region for the harmonic bins to line up.
        const auto absInput = std::abs (shaperInput);
        if ((shaperInput >= 0.0f) != previousPositive && absInput > 1.0e-4f)
        {
            // samplesSinceCrossing is a period in SAMPLES, so it is converted to a frequency
            // by dividing the rate. It used to be passed through a jlimit with frequency
            // bounds, which was dimensionally wrong: clamping a sample count against a Hz
            // range silently picked the wrong branch and the estimate only worked because
            // the bounds happened to be wide. The period itself is what needs guarding, so
            // it is clamped to a sane sample range and the resulting frequency is bounded
            // separately below.
            const auto periodSamples = static_cast<float> (
                juce::jlimit (2, juce::jmax (2, static_cast<int> (sampleRate)), samplesSinceCrossing));
            const auto instantFrequency = sampleRate / periodSamples;
            trackedFrequency += (instantFrequency - trackedFrequency) * 0.05f;
            samplesSinceCrossing = 0;
        }

        previousPositive = shaperInput >= 0.0f;
        ++samplesSinceCrossing;

        // Only measure while there is real signal, and only occasionally.
        if (absInput < 1.0e-3f)
        {
            fundamentalLevel += (0.0f - fundamentalLevel) * 0.05f;
            return false;
        }

        // Measure at a fixed RATE rather than every fixed number of samples, so the update
        // frequency of the readout is the same at 44.1 kHz and 192 kHz. At a fixed stride
        // the analyser would run four times more often per second on a 192 kHz session,
        // costing four times as much for a display that updates at 30 Hz regardless.
        const auto stride = juce::jmax (16, juce::roundToInt (sampleRate / 700.0f));

        if (++sampleCounter < stride)
            return true;

        sampleCounter = 0;

        // Bound the tracked frequency to a range that is valid at any sample rate. The lower
        // edge is a musical floor and the upper edge is kept clear of Nyquist, and the
        // maximum is taken with jmax so the two can never cross over - passing inverted
        // bounds to jlimit would return an undefined value rather than the nearest limit.
        const auto maxFrequency = juce::jmax (60.0f, sampleRate * 0.45f);
        const auto frequency = juce::jlimit (30.0f, maxFrequency, trackedFrequency);

        // The output is captured explicitly: a lambda has no access to the enclosing
        // function's parameters unless they are named in the capture list.
        const auto ratioAt = [this, shaperOutput, frequency, sampleRate, maxFrequency] (float bin)
        {
            if (bin * frequency >= maxFrequency)
                return 0.0f;

            return std::abs (goertzel (shaperOutput, bin * frequency, sampleRate));
        };

        const auto fundamental = juce::jmax (1.0e-6f, std::abs (goertzel (shaperInput,
                                                                         frequency, sampleRate)));
        const auto second = ratioAt (2.0f);
        const auto third = ratioAt (3.0f);

        // Relative to the fundamental, so the reading is meaningful at any level: this is a
        // distortion ratio, not an absolute power.
        const auto even = second / fundamental;
        const auto odd = third / fundamental;

        const auto smoothing = 0.15f;
        evenRatio += (even - evenRatio) * smoothing;
        oddRatio += (odd - oddRatio) * smoothing;
        fundamentalLevel += (fundamental - fundamentalLevel) * smoothing;

        return true;
    }

    /** Second-harmonic content relative to the fundamental - warmth and body. */
    float getEvenRatio() const noexcept { return evenRatio; }

    /** Third-harmonic content relative to the fundamental - edge and density. */
    float getOddRatio() const noexcept { return oddRatio; }

    /** How much level the shaper saw, so the display can dim when there is no signal. */
    float getFundamentalLevel() const noexcept { return fundamentalLevel; }

private:
    /**
        Single-bin magnitude estimate, evaluated over the most recent window so it is
        independent of the block size. Used instead of an FFT because only a couple of bins
        are needed and this costs a fraction of a full transform.
    */
    float goertzel (float sample, float frequency, float sampleRate) noexcept
    {
        const auto omega = juce::MathConstants<float>::twoPi * frequency / sampleRate;
        const auto coefficient = 2.0f * std::cos (omega);

        // The window length is a fixed TIME, not a fixed number of samples, so the bin width
        // stays the same at every supported rate. A fixed sample count would make the window
        // shrink with the sample rate - at 192 kHz 512 samples is only 2.7 ms and the bin
        // width balloons to 375 Hz, which is wider than the 1 kHz gap between harmonics and
        // makes the even/odd split meaningless.
        //
        // 11.6 ms is the window that 512 samples gives at 44.1 kHz, so the behaviour at the
        // lower rates is unchanged and the higher ones now match it.
        const auto samplesPerWindow = juce::jmax (64, juce::roundToInt (0.0116 * sampleRate));

        const auto current = sample + coefficient * previous1 - previous2;
        previous2 = previous1;
        previous1 = current;

        // The window is reset periodically rather than run forever, which keeps the
        // recurrence from accumulating numerical error over a long session.
        if (++windowCounter >= samplesPerWindow)
        {
            // Power at the bin, from the final two states of the recurrence.
            const auto power = previous1 * previous1 + previous2 * previous2
                             - coefficient * previous1 * previous2;

            previous1 = 0.0f;
            previous2 = 0.0f;
            windowCounter = 0;

            lastMagnitude = std::sqrt (juce::jmax (0.0f, power))
                          / static_cast<float> (samplesPerWindow);
            lastMagnitude = juce::jlimit (0.0f, 4.0f, lastMagnitude);
        }

        return lastMagnitude;
    }

    float trackedFrequency = 220.0f;
    float evenRatio = 0.0f;
    float oddRatio = 0.0f;
    float fundamentalLevel = 0.0f;
    int samplesSinceCrossing = 1;
    int sampleCounter = 0;
    bool previousPositive = true;

    // Goertzel recurrence state and its measurement window.
    float previous1 = 0.0f;
    float previous2 = 0.0f;
    float lastMagnitude = 0.0f;
    int windowCounter = 0;
};

//==============================================================================
/**
    The tape machine. Signal flow, in order:

      input trim -> input glue compressor -> record head (bias + magnetic hysteresis)
      -> tape low-pass and head-gap loss -> tape noise floor and wow/flutter
      -> playback EQ tilt -> output glue compressor -> final gain compensation
      -> output trim -> stereo width
*/
class FirstAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    FirstAudioProcessor();
    ~FirstAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRateToUse, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState parameters;

    //==============================================================================
    //  Live telemetry published by the audio thread and consumed by the editor.
    //  Peaks are exchanged (consumed) by the meter; the rest are plain readings.
    float getInputPeakLevel() noexcept { return inputPeakLevel.exchange (0.0f, std::memory_order_relaxed); }
    float getInputRmsLevel() const noexcept { return inputRmsLevel.load (std::memory_order_relaxed); }
    float getOutputPeakLevel() noexcept { return outputPeakLevel.exchange (0.0f, std::memory_order_relaxed); }
    float getOutputRmsLevel() const noexcept { return outputRmsLevel.load (std::memory_order_relaxed); }

    // Input-side loudness, measured on the same four views as the output so the two
    // meters can be read against each other.
    float getInputPeakDb() const noexcept { return inputPeakDb.load (std::memory_order_relaxed); }
    float getInputRmsDb() const noexcept { return inputRmsDb.load (std::memory_order_relaxed); }
    float getInputLufs() const noexcept { return inputLufs.load (std::memory_order_relaxed); }
    float getInputVuDb() const noexcept { return inputVuDb.load (std::memory_order_relaxed); }
    float getInputCombinedDb() const noexcept { return inputCombinedDb.load (std::memory_order_relaxed); }
    bool isInputClipping() const noexcept { return inputClipping.load (std::memory_order_relaxed); }

    //==============================================================================
    //  Four-way loudness metering.
    //
    //  The meters panel shows the same signal four different ways, because each one
    //  answers a different question and no single scale is right for all of them:
    //
    //    RMS   - the honest electrical average, the engineer's baseline
    //    LUFS  - K-weighted, so it reflects perceived loudness rather than volts
    //    VU    - the classic 300 ms ballistic average, deliberately slower and forgiving
    //    dB    - peak dBFS, the only one that tells you about clipping
    //
    //  The combined reading weights each contribution equally (25 % each), which makes
    //  it deliberately blind to the weaknesses of any one scale: peak alone would jump
    //  on transients, LUFS alone would ignore them, VU alone would smooth too much.
    //==============================================================================
    float getOutputPeakDb() const noexcept { return outputPeakDb.load (std::memory_order_relaxed); }
    float getOutputRmsDb() const noexcept { return outputRmsDb.load (std::memory_order_relaxed); }
    float getOutputLufs() const noexcept { return outputLufs.load (std::memory_order_relaxed); }
    float getOutputVuDb() const noexcept { return outputVuDb.load (std::memory_order_relaxed); }

    /** Equal-weighted (25 % each) blend of the four loudness views, in dB. */
    float getOutputCombinedDb() const noexcept { return outputCombinedDb.load (std::memory_order_relaxed); }

    /** True while the output is clipping, for the meter's peak lamp. */
    bool isOutputClipping() const noexcept { return outputClipping.load (std::memory_order_relaxed); }

    //==============================================================================
    //  Harmonic character telemetry.
    //
    //  These report what the tape shaper is measurably producing, as ratios against the
    //  fundamental. They are the honest answer to "is this adding the right kind of
    //  distortion": even harmonics are warmth and body, odd harmonics are edge and
    //  density, and looking like analogue tape means having both with even content
    //  present rather than pure odd-order harshness.
    //==============================================================================
    float getEvenHarmonicRatio() const noexcept { return evenHarmonicRatio.load (std::memory_order_relaxed); }
    float getOddHarmonicRatio() const noexcept { return oddHarmonicRatio.load (std::memory_order_relaxed); }

    /** Gain reduction of the input stage compressor in dB (always <= 0). */
    float getInputGainReductionDb() const noexcept { return inputGainReductionDb.load (std::memory_order_relaxed); }

    /** Gain reduction of the output stage compressor in dB (always <= 0). */
    float getOutputGainReductionDb() const noexcept { return outputGainReductionDb.load (std::memory_order_relaxed); }

    /** Detector activity of the input stage compressor, 0..1, for its own meter. */
    float getInputCompressorActivity() const noexcept { return inputCompressorActivity.load (std::memory_order_relaxed); }

    /** Detector activity of the output stage compressor, 0..1, for its own meter. */
    float getOutputCompressorActivity() const noexcept { return outputCompressorActivity.load (std::memory_order_relaxed); }

    /** Total gain reduction of both glue stages in dB (always <= 0). */
    float getGainReductionDb() const noexcept
    {
        return juce::jlimit (-24.0f, 0.0f,
                             inputGainReductionDb.load (std::memory_order_relaxed)
                             + outputGainReductionDb.load (std::memory_order_relaxed));
    }

    /** Envelope of the tape glue compressors as a 0..1 linear activity value. */
    float getCompressorActivity() const noexcept { return compressorActivity.load (std::memory_order_relaxed); }

    /** Instantaneous transport drift (wow/flutter), normalised to 0..1 around 0.5. */
    float getTransportDrift() const noexcept { return transportDrift.load (std::memory_order_relaxed); }

    /** Tone macro (tape/speed character crossfade), 0..1, for the editor. */
    float getToneMacro() const noexcept { return characterParam != nullptr ? characterParam->load() : 0.0f; }

    /** Harmonic weight of the last block, 0..1, used for UI colour animation. */
    float getHarmonicCharacter() const noexcept { return harmonicCharacter.load (std::memory_order_relaxed); }

    /** True when the last processed block was fully bypassed. */
    bool isBypassed() const noexcept { return bypassActive.load (std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** Rebuilds every time-domain constant from the current sample rate. */
    void resetSampleRateDependentState();

    /** Recomputes the cached tone filter coefficients for the current rate. */
    void updateToneCoefficients (float toneValue);

    // Cached parameter pointers: avoids repeated string lookups on the audio thread.
    std::atomic<float>* inputDbParam = nullptr;
    std::atomic<float>* driveParam = nullptr;
    std::atomic<float>* biasParam = nullptr;
    std::atomic<float>* toneParam = nullptr;
    std::atomic<float>* characterParam = nullptr;
    std::atomic<float>* wowParam = nullptr;
    std::atomic<float>* flutterParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* outputDbParam = nullptr;
    std::atomic<float>* widthParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* tapeTypeParam = nullptr;
    std::atomic<float>* speedParam = nullptr;

    float sampleRate = 44100.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> inputGainSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputGainSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> widthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> bypassSmoothed;

    std::atomic<float> inputPeakLevel { 0.0f };
    std::atomic<float> inputRmsLevel { 0.0f };
    std::atomic<float> inputPeakDb { -70.0f };
    std::atomic<float> inputRmsDb { -70.0f };
    std::atomic<float> inputLufs { -70.0f };
    std::atomic<float> inputVuDb { -70.0f };
    std::atomic<float> inputCombinedDb { -70.0f };
    std::atomic<bool> inputClipping { false };
    std::atomic<float> outputPeakLevel { 0.0f };
    std::atomic<float> outputRmsLevel { 0.0f };
    std::atomic<float> outputPeakDb { -70.0f };
    std::atomic<float> outputRmsDb { -70.0f };
    std::atomic<float> outputLufs { -70.0f };
    std::atomic<float> outputVuDb { -70.0f };
    std::atomic<float> outputCombinedDb { -70.0f };
    std::atomic<bool> outputClipping { false };
    std::atomic<float> evenHarmonicRatio { 0.0f };
    std::atomic<float> oddHarmonicRatio { 0.0f };
    std::atomic<float> inputGainReductionDb { 0.0f };
    std::atomic<float> outputGainReductionDb { 0.0f };
    std::atomic<float> inputCompressorActivity { 0.0f };
    std::atomic<float> outputCompressorActivity { 0.0f };
    std::atomic<float> compressorActivity { 0.0f };
    std::atomic<float> transportDrift { 0.5f };
    std::atomic<float> harmonicCharacter { 0.0f };
    std::atomic<bool> bypassActive { false };

    // Per-channel tape state: 3-element hysteresis memory (current, previous, older)
    // plus a 2-element high-frequency post-emphasis memory.
    std::array<float, 3> hystL {};
    std::array<float, 3> hystR {};
    std::array<float, 2> highFreqL {};
    std::array<float, 2> highFreqR {};

    // One-pole state for the hiss band-limit, per channel. Kept separate from the tape
    // filters so the noise colour cannot drift when the tone control moves.
    float hissLowPassL = 0.0f;
    float hissLowPassR = 0.0f;

    float wowPhaseL = 0.0f;
    float wowPhaseR = 0.0f;
    float flutterPhaseL = 0.0f;
    float flutterPhaseR = 0.0f;

    float previousTone = -1.0f;
    float toneLpAc = 0.0f;
    float toneLpBc = 0.0f;

    // Per-instance tape noise generator. Kept as an object member rather than a
    // thread_local static so that instances never share one stream and the output
    // is reproducible for a given instance.
    std::uint32_t noiseState = 0x1b873593u;

    // Noise-path levelling. The hiss gain tracks the programme power with a fast attack
    // and a slow release so the noise floor is CONSTANT while signal plays and fades
    // only in true pauses: if the hiss rode the compressor instead, pauses got LOUDER
    // than programme (release pulls the level back up onto the hiss), which is exactly
    // backwards for a tape machine. Both floats are audio-thread only.
    float noiseBlockPower = 0.0f;
    float noiseEnvelope = 0.0f;

    // Two independent glue stages, each with its own detector envelope. The input
    // stage runs straight after the input trim, the output stage straight before
    // the output trim; neither reads the other's state.
    GlueCompressor inputCompressor;
    GlueCompressor outputCompressor;

    // Measures the harmonics the tape shaper is actually producing, separating even from
    // odd. This is the observable signature of the analogue character and drives the
    // HARMONICS display.
    HarmonicAnalyser harmonicAnalyser;

    // Output safety limiter, the stage that keeps the signal below the soft clipper so it
    // almost never has to act. `preLimiterDetector` is a fast peak follower and
    // `limiterGain` is the smoothed gain it applies; keeping them separate gives the
    // classic brick-wall shape - instant catch, musical release.
    float preLimiterDetector = 0.0f;
    float limiterGain = 1.0f;

    // Final gain compensation. `smoothedCompensationDb` is the slow, programme-level
    // correction applied after the last compressor. It is driven by comparing the
    // reference power taken straight after the input trim (before the first compressor)
    // against the power the chain actually produced. Audio-thread only, so a plain float.
    float smoothedCompensationDb = 0.0f;

    // K-weighted loudness, run on the plugin output and on the reference point so the
    // panel can show both ends of the chain on the same scale.
    LoudnessMeter outputLoudness;
    LoudnessMeter inputLoudness;

    // Classic 300 ms VU ballistic average, kept separate from the RMS so the VU meter
    // has the slow, forgiving movement that makes it useful for programme level.
    float vuAverage = 0.0f;
    float inputVuAverage = 0.0f;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FirstAudioProcessor)
};
