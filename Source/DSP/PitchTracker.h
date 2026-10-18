// =============================================================================
//  The pitch tracker: the note the autotune stage corrects towards, by YIN.
//
//  Driven on signal by tests/pitch/pitch_test.cpp, which includes this header
//  rather than a copy cut out of PluginProcessor.h, and instantiated by
//  tests/dsp/check_headers_juce.sh against real JUCE. The conventions of
//  Source/DSP/ are stated in J37Math.h.
// =============================================================================
#pragma once

#include <array>
#include <cmath>
#include <cstddef>

//==============================================================================
/**
    The pitch the autotune stage corrects towards, measured by YIN.

    What this replaces counted ZERO CROSSINGS of the input and inferred a
    frequency from their rate. That is a rate detector, not a pitch detector: on
    a signal with strong harmonics it locks onto whichever overtone supplies the
    crossings (a guitar's second harmonic reads an octave high, which is how a
    "corrector" can leave a note exactly where it was), and on NOISE - which has
    as many crossings as it has samples - it reports a confident frequency that
    is nothing but hiss, so the correction chased the noise floor instead of the
    note. Both of those were reported, in one sentence, as "autotune does
    nothing": it moved the pitch towards the wrong number, or towards a number
    that was not a pitch at all.

    YIN is a PERIOD detector, and it answers both: it looks for the lag at which
    the waveform best predicts itself (the difference function), normalises that
    difference by the mean difference over every shorter lag - which is what
    removes the octave errors a bare autocorrelation makes - and reports the
    first lag that falls under a threshold, with a confidence of one minus the
    normalised difference there. A noise floor has no such lag and reports no
    confidence, and the caller reads that as leave this alone.

    The analysis runs at a FIXED rate: the input is decimated by whatever integer
    takes the session rate nearest to 12 kHz, so the lag range means the same
    frequencies (47 Hz to 1.5 kHz) at every sample rate and the cost per second
    is the same too. The estimate is refreshed every 256 analysis frames - about
    20 ms - which is fast enough to follow a note and slow enough that the
    difference function is a few percent of one core, and it is only ever paid
    while the stage is switched on.
*/
struct PitchTracker
{
    // A 512-frame analysis window gives the difference function 256 frames of
    // overlap for lags up to 256 frames: 47 Hz at the 12 kHz analysis rate.
    static constexpr int analysisSize = 512;
    static constexpr int maxTau = 256;
    static constexpr int minTau = 8;
    static constexpr int tauCount = maxTau;
    static constexpr int differenceSize = analysisSize - maxTau;
    static constexpr float analysisTargetRate = 12000.0f;
    static constexpr float threshold = 0.15f;
    static constexpr float silentRms = 1.0e-4f;
    static constexpr float octaveGuardLog2 = 0.15f;

    /** Prepares the decimation for a rate. Called from setOversamplingFactor,
        with the ENGINE's rate rather than the session's, because that is the rate
        the frames it is pushed arrive at. */
    void prepare (double sampleRateIn)
    {
        const auto sampleRate = sampleRateIn > 1.0 ? sampleRateIn : 48000.0;
        decimation = juce::jmax (1, juce::roundToInt (sampleRate / analysisTargetRate));
        analysisRate = static_cast<float> (sampleRate) / static_cast<float> (decimation);
        reset();
    }

    void reset() noexcept
    {
        window.fill (0.0f);
        writeIndex = 0;
        sinceAnalysis = 0;
        framesSinceReset = 0;
        decimationCounter = 0;
        decimationSum = 0.0f;
        frequencyValue = 0.0f;
        periodValue = 0.0f;
        confidenceValue = 0.0f;
        lastFrequency = 0.0f;
        lastConfidence = 0.0f;
        for (auto& value : difference) value = 0.0f;
        for (auto& value : normalised) value = 0.0f;
    }

    /** One input frame of the signal to be tracked (the mono sum of the pair). */
    void push (float sample) noexcept
    {
        // The decimation is a box average: a cheap low-pass whose passband
        // covers everything the analysis cares about and whose stopband is what
        // keeps the lag search from confusing the top octave.
        decimationSum += sample;

        if (++decimationCounter < decimation)
            return;

        window[static_cast<std::size_t> (writeIndex)] = decimationSum
                                                       / static_cast<float> (decimation);
        decimationCounter = 0;
        decimationSum = 0.0f;
        writeIndex = (writeIndex + 1) % analysisSize;
        ++framesSinceReset;

        if (++sinceAnalysis >= maxTau)
        {
            sinceAnalysis = 0;
            analyse();
        }
    }

    /** The detected fundamental in Hz, held between analyses. Zero until a
        reading exists. */
    float frequency() const noexcept { return frequencyValue; }

    /** The period of that fundamental in frames of the rate it was pushed at -
        what the shifter's grains are measured in. Zero with no reading. */
    float periodFrames() const noexcept { return periodValue; }

    /** One minus the normalised difference at the chosen lag: 1 is a perfectly
        periodic signal, 0 is noise or silence. */
    float confidence() const noexcept { return confidenceValue; }

    /** The decimated rate the lag search runs at. The caller needs it because
        `periodFrames()` is measured in frames of THIS rate, while the shifter it
        drives runs at the session rate: one is a multiple of the other. */
    float analysisRateHz() const noexcept { return analysisRate; }

private:
    void analyse() noexcept
    {
        // A reading needs a full window behind it: until that many decimated
        // frames have gone in, part of what the difference function reads is
        // still zeros, and a difference against zeros reads as a very confident
        // pitch at the length of the gap.
        if (framesSinceReset < analysisSize)
        {
            confidenceValue = 0.0f;
            return;
        }

        const auto at = [this] (int index) noexcept
        {
            return window[static_cast<std::size_t> ((writeIndex + index) % analysisSize)];
        };

        auto energy = 0.0f;
        for (int frame = 0; frame < differenceSize; ++frame)
        {
            const auto value = at (frame);
            energy += value * value;
        }

        if (std::sqrt (energy / static_cast<float> (differenceSize)) < silentRms)
        {
            confidenceValue = 0.0f;          // silence has no pitch; hold the last
            return;
        }

        // The difference function, over every lag the normalisation needs - 
        // including the very short ones, because it is the MEAN over all shorter
        // lags that makes the normalisation mean what it means.
        for (int tau = 1; tau <= maxTau; ++tau)
        {
            auto sum = 0.0f;
            for (int frame = 0; frame < differenceSize; ++frame)
            {
                const auto delta = at (frame) - at (frame + tau);
                sum += delta * delta;
            }

            difference[static_cast<std::size_t> (tau - 1)] = sum;
        }

        // Cumulative mean normalisation: d'(tau) = d(tau) * tau / sum(d(1..tau)).
        auto running = 0.0f;
        for (int tau = 1; tau <= maxTau; ++tau)
        {
            const auto index = static_cast<std::size_t> (tau - 1);
            running += difference[index];
            normalised[index] = running > 0.0f
                                    ? difference[index] * static_cast<float> (tau) / running
                                    : 1.0f;
        }

        // The first lag under the threshold, walked on to its own minimum: that
        // is YIN's rule, and the walking is what keeps the estimate on the true
        // period rather than on the first lag that squeaked under the line.
        auto tau = -1;

        for (int candidate = minTau; candidate <= maxTau; ++candidate)
        {
            const auto index = static_cast<std::size_t> (candidate - 1);

            if (normalised[index] < threshold)
            {
                while (candidate < maxTau
                       && normalised[static_cast<std::size_t> (candidate)] < normalised[index])
                    ++candidate;

                tau = candidate;
                break;
            }
        }

        auto confidence = 0.0f;

        if (tau < 0)
        {
            // Nothing under the threshold: the deepest minimum is the only
            // candidate left, and it is only a pitch if it is deep at all.
            auto deepest = minTau;

            for (int candidate = minTau; candidate <= maxTau; ++candidate)
                if (normalised[static_cast<std::size_t> (candidate - 1)]
                        < normalised[static_cast<std::size_t> (deepest - 1)])
                    deepest = candidate;

            if (normalised[static_cast<std::size_t> (deepest - 1)] < 0.5f)
                tau = deepest;
        }

        if (tau < 0)
        {
            confidenceValue = 0.0f;
            return;
        }

        confidence = juce::jlimit (0.0f, 1.0f, 1.0f - normalised[static_cast<std::size_t> (tau - 1)]);

        // Parabolic interpolation between the chosen lag and its neighbours, on
        // the NORMALISED difference: the refinement is what makes the estimate
        // accurate enough to hear, and using d' rather than d keeps it on the
        // same curve the choice was made on.
        auto refined = static_cast<float> (tau);

        if (tau > minTau && tau < maxTau)
        {
            const auto before = normalised[static_cast<std::size_t> (tau - 2)];
            const auto here = normalised[static_cast<std::size_t> (tau - 1)];
            const auto after = normalised[static_cast<std::size_t> (tau)];
            const auto denominator = 2.0f * (2.0f * here - before - after);

            if (std::abs (denominator) > 1.0e-9f)
                refined += (after - before) / denominator;
        }

        auto hz = analysisRate / refined;

        // The octave guard. A period detector's one characteristic failure is a
        // strong second harmonic reading an octave high, and it is recognisable:
        // the reading is near-exactly an octave away AND less confident than the
        // note it is replacing. Staying on the note the voice was singing is
        // right in that case, and one refresh of latency on a genuine octave jump
        // is not something a corrector can be heard to have.
        if (lastFrequency > 0.0f && confidence < lastConfidence - 0.2f)
        {
            auto best = hz;
            auto bestError = std::abs (std::log2 (hz / lastFrequency));

            for (const auto candidate : { hz * 0.5f, hz * 2.0f })
            {
                const auto error = std::abs (std::log2 (candidate / lastFrequency));

                // Only an octave move is a candidate, and only if it actually
                // lands closer to what was last heard - so a genuine leap to a
                // fifth is left alone, and a wrong octave is not.
                if (std::abs (error - 1.0f) < octaveGuardLog2 && error < bestError)
                {
                    best = candidate;
                    bestError = error;
                }
            }

            hz = best;
        }

        frequencyValue = hz;
        periodValue = analysisRate > 0.0f ? analysisRate / hz : 0.0f;
        confidenceValue = confidence;
        lastFrequency = hz;
        lastConfidence = confidence;
    }

    std::array<float, analysisSize> window {};
    std::array<float, tauCount> difference {};
    std::array<float, tauCount> normalised {};
    int writeIndex = 0;
    int sinceAnalysis = 0;
    int framesSinceReset = 0;
    int decimation = 4;
    int decimationCounter = 0;
    float decimationSum = 0.0f;
    float analysisRate = 12000.0f;
    float frequencyValue = 0.0f;
    float periodValue = 0.0f;
    float confidenceValue = 0.0f;
    float lastFrequency = 0.0f;
    float lastConfidence = 0.0f;
};
