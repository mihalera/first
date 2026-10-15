// =============================================================================
//  K-weighted loudness measurement (ITU-R BS.1770 / EBU R128).
//
//  It was measured by tests/dsp/loudness_test.cpp before it moved here, and that
//  harness now includes THIS header rather than a copy of the text cut out of
//  PluginProcessor.h - which is the point of the move: a stage that lives in its
//  own header can be included by a harness directly, so the extractor stops
//  cutting code out of a 7,600-line file by string span.
//
//  The conventions of Source/DSP/ are stated in J37Math.h.
// =============================================================================
#pragma once

#include "J37Math.h"

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

    Each channel carries its own filter history. The two channels of a frame are
    two signals, and a biquad's state is part of the signal, not part of the
    curve: run the right channel through the coefficients that the left channel
    just left behind and the right is filtered by the left's tail rather than by
    its own past. That is what a single pair of state variables did, and it made
    the meter read high on a mono signal (the second pass responded to the first
    channel's history, so the one signal was counted about twice) and read the
    stereo sum as 0.57 LU instead of 3 - a meter that is fed both channels and
    reports something that is neither of them.

    The reading reaches the panel and nothing else. AUTO GAIN does not read this
    meter: the compensator in PluginProcessor.cpp measures block power on the
    signal itself. So a wrong meter here is a wrong NUMBER rather than a wrong
    level - the number a user sets levels by, which is why it has a harness of its
    own rather than a line in another stage's.
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

    /**
        Clears the filter history and the running mean square, and leaves the
        rate-dependent coefficients exactly as prepare() built them.

        Both halves of that matter. The engine calls prepare() and then reset(),
        in that order, so a reset that also zeroed the coefficients would drop the
        K-weighting back to a flat pass-through for the rest of the session - which
        is what `shelf = {}` did: a meter reading 0.69 dB low on every project,
        with nothing on screen to say so. A reset that left the history in place
        would instead carry the previous session's signal into a freshly prepared
        one.
    */
    void reset() noexcept
    {
        shelf.clearState();
        highPass.clearState();
        meanSquare = 0.0f;
    }

    /**
        The window coefficient for a given rate, built once per block.

        This is a function of the sample rate alone - the 400 ms window is fixed -
        so recomputing it inside the per-frame loop was an std::exp per stereo frame
        per meter, two meters deep, for a constant. The caller builds it in
        prepare() / on a rate change and hands it in.
    */
    static float makeWindowCoefficient (float sampleRate) noexcept
    {
        return j37math::exp (-1.0f / (juce::jmax (1.0f, sampleRate) * 0.4f));
    }

    /** Feeds one stereo frame and returns the current loudness in LUFS. */
    float processFrame (float left, float right, float windowCoefficient) noexcept
    {
        const auto weightedLeft = highPass.process (shelf.process (left, 0), 0);
        const auto weightedRight = highPass.process (shelf.process (right, 1), 1);

        // BS.1770 sums the per-channel mean squares; the channels here are already
        // gain-weighted equally, so it is a plain sum.
        const auto frameMeanSquare = weightedLeft * weightedLeft
                                   + weightedRight * weightedRight;

        // A 400 ms sliding window, implemented as a one-pole that is close enough for
        // a live display while staying cheap and block-size independent.
        const auto coefficient = windowCoefficient;
        meanSquare = coefficient * meanSquare + (1.0f - coefficient) * frameMeanSquare;

        const auto loudness = -0.691f + 10.0f * j37math::log10 (juce::jmax (1.0e-12f, meanSquare));
        return juce::jmax (-70.0f, loudness);
    }

private:
    /** One biquad's coefficients with one history per channel (0 = left, 1 = right).
        The coefficients are shared because both channels carry the same curve; the
        history is not, because the two channels are different signals. */
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;

        float x1[2] {}, x2[2] {}, y1[2] {}, y2[2] {};

        float process (float x, int channel) noexcept
        {
            const auto y = b0 * x + b1 * x1[channel] + b2 * x2[channel]
                         - a1 * y1[channel] - a2 * y2[channel];
            x2[channel] = x1[channel];
            x1[channel] = x;
            y2[channel] = y1[channel];
            y1[channel] = y;
            return y;
        }

        void clearState() noexcept
        {
            for (int channel = 0; channel < 2; ++channel)
                x1[channel] = x2[channel] = y1[channel] = y2[channel] = 0.0f;
        }
    };

    Biquad shelf;
    Biquad highPass;
    float meanSquare = 0.0f;
};
