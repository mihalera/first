// =============================================================================
//  The dynamics family: the noise gate, the guitarist's spike suppressor, the
//  de-esser, the dynamic equaliser - and the multiband compressor that replaces
//  the conventional single-band one on the finished signal.
//
//  Four of these are the same shape: a detector, a threshold, and a gain applied
//  to something narrower than the whole signal (a band, a side, a single
//  sample). The fifth, MultibandCompressor, is the same shape three times over
//  plus an upward stage, and it is the only stage here that the signal path
//  calls directly rather than through FxRack, because it is the plugin's
//  COMP page and not a pedal.
//
//  Linked detection is the rule wherever a stage acts on stereo: both channels
//  are given the gain the louder of them asked for, because two independent
//  detectors pull the image toward whichever side moved first. The rack's
//  creative effects may be per-channel; the dynamics are not.
// =============================================================================
#pragma once

#include "FxPrimitives.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace j37dsp::fx
{

// =============================================================================
//  Noise gate. Threshold from the amount, one fast attack and one slow release,
//  and hysteresis so a signal hovering at the threshold does not chatter.
// =============================================================================
class Gate
{
public:
    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        detector.setTimes (1.0f, 80.0f, rate);
        gainEnvelope.setTimes (1.0f, 150.0f, rate);
        reset();
    }

    void reset() noexcept
    {
        detector.reset();
        gainEnvelope.reset (1.0f);
        gain = 1.0f;
        open = true;
    }

    void process (float& left, float& right, float amount) noexcept
    {
        const auto depth = clamp01 (amount);
        const auto thresholdDb = -60.0f + 54.0f * depth;
        const auto threshold = std::pow (10.0f, thresholdDb * 0.05f);
        const auto closeThreshold = threshold * 0.7f;

        const auto peak = std::max (std::abs (left), std::abs (right));
        const auto envelope = detector.process (peak);

        if (open)
            open = envelope > closeThreshold;
        else
            open = envelope > threshold;

        const auto target = open ? 1.0f : 0.0f;
        gain += (target - gain) * (target > gain ? 0.35f : 0.002f);
        gain = flushDenormal (gain);
        gainEnvelope.process (gain);

        left *= gain;
        right *= gain;
    }

private:
    EnvelopeFollower detector, gainEnvelope;
    double rate = 48000.0;
    float gain = 1.0f;
    bool open = true;
};

// =============================================================================
//  Anti-spike.
//
//  A guitar string's clicks, a pick hitting a pickup and a cable being knocked
//  all put a burst of samples into the input that no compressor is fast enough
//  to catch: by the time a detector has moved, the burst is in the file. The
//  stage is therefore not a compressor. It is a median of the last three
//  samples, which by construction cannot pass a one-sample spike, blended
//  against a slew limit that stops a two-sample edge from arriving whole.
//
//  It is deliberately dumb and deliberately not a filter: it does not know what
//  the programme is, so it cannot damage it - the note either is in the median or
//  it is removed, and the amount decides how much of the repair is applied.
// =============================================================================
class AntiSpike
{
public:
    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        reset();
    }

    void reset() noexcept
    {
        historyLeft = { 0.0f, 0.0f, 0.0f };
        historyRight = { 0.0f, 0.0f, 0.0f };
        lastLeft = lastRight = 0.0f;
        filled = 0;
    }

    void process (float& left, float& right, float amount) noexcept
    {
        const auto mix = clamp01 (amount);
        const auto dryLeft = left;
        const auto dryRight = right;

        historyLeft[0] = historyLeft[1];
        historyLeft[1] = historyLeft[2];
        historyLeft[2] = left;
        historyRight[0] = historyRight[1];
        historyRight[1] = historyRight[2];
        historyRight[2] = right;

        if (filled < 3)
            ++filled;

        const auto medianLeft = filled < 3 ? left : middleOfThree (historyLeft);
        const auto medianRight = filled < 3 ? right : middleOfThree (historyRight);

        // The slew limit is stated in signal units per sample and scaled by how
        // hard the stage is working: at half travel a genuine transient is
        // allowed to move twice as fast as at full.
        const auto maximumStep = 0.25f / std::max (0.1f, mix);
        const auto limitedLeft = lastLeft + std::clamp (medianLeft - lastLeft, -maximumStep, maximumStep);
        const auto limitedRight = lastRight + std::clamp (medianRight - lastRight, -maximumStep, maximumStep);

        lastLeft = limitedLeft;
        lastRight = limitedRight;

        left = dryLeft + (limitedLeft - dryLeft) * mix;
        right = dryRight + (limitedRight - dryRight) * mix;
    }

private:
    static float middleOfThree (const std::array<float, 3>& values) noexcept
    {
        const auto a = values[0], b = values[1], c = values[2];
        return std::max (std::min (a, b), std::min (std::max (a, b), c));
    }

    EnvelopeFollower detector;
    std::array<float, 3> historyLeft {}, historyRight {};
    double rate = 48000.0;
    float lastLeft = 0.0f, lastRight = 0.0f;
    int filled = 0;
};

// =============================================================================
//  De-esser: a split-band compressor that only ever acts on the sibilance band.
//
//  Splitting at a fixed 5 kHz and attenuating only the high half is what keeps
//  the body of the word intact - a broadband compressor turned up far enough to
//  catch an "s" is a compressor that ducks the whole voice on every "s", which is
//  the pumping de-essers are hated for. The two halves sum back to the input, so
//  at zero reduction the stage is bit-exact.
// =============================================================================
class DeEsser
{
public:
    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        splitterLeft.setCutoff (5000.0f, rate);
        splitterRight.setCutoff (5000.0f, rate);
        detector.setTimes (0.5f, 30.0f, rate);
        reset();
    }

    void reset() noexcept
    {
        splitterLeft.reset();
        splitterRight.reset();
        detector.reset();
        reduction = 1.0f;
    }

    void process (float& left, float& right, float amount) noexcept
    {
        const auto depth = clamp01 (amount);
        const auto lowLeft = splitterLeft.lowPass (left);
        const auto lowRight = splitterRight.lowPass (right);
        const auto highLeft = left - lowLeft;
        const auto highRight = right - lowRight;

        const auto envelope = detector.process (std::max (std::abs (highLeft), std::abs (highRight)));
        const auto threshold = std::pow (10.0f, (-40.0f + 20.0f * (1.0f - depth)) * 0.05f);

        auto target = 1.0f;

        if (envelope > threshold)
        {
            const auto excess = std::min (1.0f, (envelope - threshold) / std::max (threshold, 1.0e-9f));
            const auto maximumReductionDb = 14.0f * depth;
            target = std::pow (10.0f, -maximumReductionDb * excess * 0.05f);
        }

        reduction += (target - reduction) * (target < reduction ? 0.4f : 0.01f);
        reduction = flushDenormal (reduction);

        left = lowLeft + highLeft * reduction;
        right = lowRight + highRight * reduction;
    }

private:
    OnePole splitterLeft, splitterRight;
    EnvelopeFollower detector;
    double rate = 48000.0;
    float reduction = 1.0f;
};

// =============================================================================
//  Dynamic equaliser: one band, cut only when that band is loud.
//
//  A static cut at 300 Hz removes the mud from the mix that needed it and hollows
//  out the one that did not; a dynamic cut responds to the programme instead.
//  The band is built as the difference of two low-passes, so it sums back to the
//  input exactly and the stage is transparent below its threshold.
// =============================================================================
class DynamicEqualizer
{
public:
    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        lowerLeft.setCutoff (150.0f, rate);
        lowerRight.setCutoff (150.0f, rate);
        upperLeft.setCutoff (600.0f, rate);
        upperRight.setCutoff (600.0f, rate);
        detector.setTimes (5.0f, 120.0f, rate);
        reset();
    }

    void reset() noexcept
    {
        lowerLeft.reset();
        lowerRight.reset();
        upperLeft.reset();
        upperRight.reset();
        detector.reset();
        reduction = 1.0f;
    }

    void process (float& left, float& right, float amount) noexcept
    {
        const auto depth = clamp01 (amount);

        const auto lowLeft = lowerLeft.lowPass (left);
        const auto lowRight = lowerRight.lowPass (right);
        const auto highLeft = upperLeft.lowPass (left);
        const auto highRight = upperRight.lowPass (right);

        const auto bandLeft = highLeft - lowLeft;
        const auto bandRight = highRight - lowRight;

        const auto envelope = detector.process (std::max (std::abs (bandLeft), std::abs (bandRight)));
        const auto threshold = 0.08f;

        auto target = 1.0f;

        if (envelope > threshold)
        {
            const auto excess = std::min (1.0f, (envelope - threshold) / threshold);
            target = std::pow (10.0f, -10.0f * depth * excess * 0.05f);
        }

        reduction += (target - reduction) * (target < reduction ? 0.15f : 0.01f);
        reduction = flushDenormal (reduction);

        left = (left - bandLeft) + bandLeft * reduction;
        right = (right - bandRight) + bandRight * reduction;
    }

private:
    OnePole lowerLeft, lowerRight, upperLeft, upperRight;
    EnvelopeFollower detector;
    double rate = 48000.0;
    float reduction = 1.0f;
};

// =============================================================================
//  Multiband compressor with an upward stage.
//
//  The COMP page used to be one feed-forward compressor on the finished signal.
//  It is now three of them, split at 120 Hz and 1.2 kHz - the two corners that
//  put the bass, the body of an instrument and the presence range in separate
//  hands, which is the split a mastering compressor actually uses - plus an
//  UPWARD stage that lifts what sits below its threshold toward it.
//
//  Upward compression is the other direction from the downward one and it is not
//  a makeup gain: the downward stage turns the loud down, the upward stage turns
//  the quiet up, and only the second changes the dynamic range at the bottom of
//  the programme. Both are reported through the same MIX, so the page's knob
//  still means "how much of the compressor reaches the output".
//
//  The split is phase-coherent by construction (three branches of two cascaded
//  one-poles that sum back to the input), and the band gains are linked across
//  the two channels so the image does not wander.
// =============================================================================
class MultibandCompressor
{
public:
    struct Settings
    {
        float thresholdDb = -18.0f;
        float ratio = 4.0f;
        float attackMs = 10.0f;
        float releaseMs = 120.0f;
        float kneeDb = 6.0f;
        float makeupDb = 0.0f;
        float mix = 1.0f;
        float upward = 0.0f;      ///< 0..1, how much of the upward stage is in
    };

    static constexpr int bands = 3;

    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;

        for (auto& split : states)
        {
            split.lowSplitter.setCutoff (120.0f, rate);
            split.lowSplitter2.setCutoff (120.0f, rate);
            split.highSplitter.setCutoff (1200.0f, rate);
            split.highSplitter2.setCutoff (1200.0f, rate);
        }

        for (auto& band : bandStates)
            band.detector.setTimes (10.0f, 120.0f, rate);

        upwardDetector.setTimes (20.0f, 200.0f, rate);
        reset();
    }

    void reset() noexcept
    {
        for (auto& split : states)
        {
            split.lowSplitter.reset();
            split.lowSplitter2.reset();
            split.highSplitter.reset();
            split.highSplitter2.reset();
        }

        for (auto& band : bandStates)
        {
            band.detector.reset();
            band.gain = 1.0f;
        }

        upwardDetector.reset();
        upwardGain = 1.0f;
    }

    void process (float& left, float& right, const Settings& settings) noexcept
    {
        const auto mix = clamp01 (settings.mix);

        if (mix <= 1.0e-5f)
            return;

        const auto makeup = std::pow (10.0f, settings.makeupDb * 0.05f);
        const auto thresholdDb = settings.thresholdDb;
        const auto ratio = std::max (1.0f, settings.ratio);
        const auto attack = smoothingCoefficient (settings.attackMs);
        const auto release = smoothingCoefficient (settings.releaseMs);

        // Split both sides first, so each band's DETECTOR can be fed the louder
        // of the two channels and both sides then take that one gain. Splitting
        // and detecting per channel would let the sides compress at different
        // instants, which is where image wander on a transient comes from - the
        // same rule the input and output glue stages already state.
        std::array<float, bands> leftBands {}, rightBands {};
        splitIntoBands (left, states[0], leftBands);
        splitIntoBands (right, states[1], rightBands);

        float compressedLeft = 0.0f, compressedRight = 0.0f;

        for (std::size_t band = 0; band < bands; ++band)
        {
            auto& state = bandStates[band];
            const auto peak = std::max (std::abs (leftBands[band]), std::abs (rightBands[band]));
            state.detector.process (peak);

            const auto levelDb = 20.0f * std::log10 (std::max (state.detector.value(), 1.0e-9f));
            const auto over = levelDb - thresholdDb;
            const auto halfKnee = std::max (0.0f, settings.kneeDb) * 0.5f;
            const auto slope = 1.0f - 1.0f / ratio;
            auto reductionDb = 0.0f;

            if (over > halfKnee)
            {
                reductionDb = over * slope;
            }
            else if (halfKnee > 0.0f && over > -halfKnee)
            {
                // The same quadratic soft knee the single-band stage used: the
                // ratio arrives gradually across the knee rather than switching on
                // at the threshold.
                const auto position = (over + halfKnee) / (2.0f * halfKnee);
                reductionDb = slope * position * position * halfKnee;
            }

            const auto targetGain = std::pow (10.0f, -reductionDb * 0.05f);

            const auto coefficient = targetGain < state.gain ? attack : release;
            state.gain += (targetGain - state.gain) * coefficient;
            state.gain = flushDenormal (state.gain);

            compressedLeft += leftBands[band] * state.gain;
            compressedRight += rightBands[band] * state.gain;
        }

        compressedLeft *= makeup;
        compressedRight *= makeup;

        // The upward stage: quiet programme is lifted toward a fixed -30 dBFS
        // floor. It runs on the sum, after the bands, because it is a statement
        // about the programme's level rather than about any one band.
        const auto upward = clamp01 (settings.upward);

        if (upward > 1.0e-5f)
        {
            const auto peak = std::max (std::abs (compressedLeft), std::abs (compressedRight));
            upwardDetector.process (peak);

            const auto levelDb = 20.0f * std::log10 (std::max (upwardDetector.value(), 1.0e-9f));
            const auto deficit = std::clamp (-30.0f - levelDb, 0.0f, 18.0f);
            const auto targetGain = std::pow (10.0f, deficit * upward * 0.05f);

            upwardGain += (targetGain - upwardGain) * smoothingCoefficient (200.0f);
            upwardGain = flushDenormal (upwardGain);

            compressedLeft *= upwardGain;
            compressedRight *= upwardGain;
        }

        left += (compressedLeft - left) * mix;
        right += (compressedRight - right) * mix;
    }

private:
    float smoothingCoefficient (float milliseconds) const noexcept
    {
        const auto seconds = std::max (0.05f, milliseconds) * 0.001f;
        return 1.0f - std::exp (-1.0f / (seconds * static_cast<float> (rate)));
    }

    struct SplitState
    {
        OnePole lowSplitter, lowSplitter2, highSplitter, highSplitter2;
    };

    struct BandState
    {
        EnvelopeFollower detector;
        float gain = 1.0f;
    };

    void splitIntoBands (float input, SplitState& split, std::array<float, bands>& out) noexcept
    {
        const auto low = split.lowSplitter2.lowPass (split.lowSplitter.lowPass (input));
        const auto belowHigh = split.highSplitter2.lowPass (split.highSplitter.lowPass (input));

        out[0] = low;
        out[1] = belowHigh - low;
        out[2] = input - belowHigh;
    }

    std::array<SplitState, 2> states;
    std::array<BandState, bands> bandStates;
    EnvelopeFollower upwardDetector;
    double rate = 48000.0;
    float upwardGain = 1.0f;
};

} // namespace j37dsp::fx
