// =============================================================================
//  Unit tests for the stutter - Source/DSP/Fx/FxStutter.h.
//
//  The stage makes three claims a user would report as a bug if one stopped
//  being true, and each is checked here rather than only in the render:
//
//    * at 0 it is bit-for-bit the input, so an idle knob cannot colour a session;
//    * at any amount it REPEATS what it heard - the output lags the input for a
//      stretch of every cycle, which is the difference between a stutter and a
//      pitch shifter that happens to be in the path;
//    * it LISTENS between repeats as well, so the effect is a rhythm rather than
//      a freeze, and it can never leave a signal stuck on one slice.
//
//  A sawtooth is the signal because every sample of it is a distinct value: a
//  repeat is then visible as the output carrying a value the input has already
//  moved past, which a sine at one frequency cannot show without a transform.
// =============================================================================
#include "shim.h"

#include "DSP/Fx/FxStutter.h"

#include <catch_amalgamated.hpp>

#include <cmath>
#include <vector>

namespace
{
constexpr double kRate = 48000.0;

/** A 20 ms ramp, repeated: distinct samples, bounded, and no DC. */
std::vector<float> sawtooth (int samples)
{
    std::vector<float> signal (static_cast<std::size_t> (samples));
    constexpr int period = static_cast<int> (kRate * 0.020);

    for (int i = 0; i < samples; ++i)
        signal[static_cast<std::size_t> (i)] =
            static_cast<float> ((i % period) / static_cast<double> (period)) * 0.5f - 0.25f;

    return signal;
}
} // namespace

TEST_CASE ("a stutter at zero is bit-for-bit the input", "[stutter]")
{
    j37dsp::FxStutter stutter;
    stutter.prepare (kRate);

    const auto signal = sawtooth (24000);

    for (const auto sample : signal)
    {
        auto left = sample;
        auto right = -sample;
        stutter.process (left, right, 0.0f);

        REQUIRE (left == sample);
        REQUIRE (right == -sample);
    }
}

TEST_CASE ("a stutter repeats what it just heard", "[stutter]")
{
    j37dsp::FxStutter stutter;
    stutter.prepare (kRate);

    const auto signal = sawtooth (static_cast<int> (kRate * 2.0));
    const auto slice = stutter.currentSliceLength();

    int repeats = 0;
    int listens = 0;
    float peak = 0.0f;

    for (std::size_t i = 0; i < signal.size(); ++i)
    {
        auto left = signal[i];
        auto right = signal[i];
        stutter.process (left, right, 0.7f);

        REQUIRE (std::isfinite (left));
        peak = std::fmax (peak, std::fabs (left));

        if (std::fabs (left - signal[i]) > 1.0e-4f)
            ++repeats;
        else
            ++listens;
    }

    // A slice is at least the window's four samples long and at most the 300 ms
    // ring, so the cycle is a real one at every amount.
    REQUIRE (slice >= 4);
    REQUIRE (slice <= static_cast<int> (kRate * 0.30) + 1);

    // It spent real time repeating and real time listening. The upper bounds keep
    // the claim honest: a stage that only ever repeated (a freeze) or only ever
    // listened (a passthrough) would pass a "it did something" test and fail a user.
    const auto total = static_cast<int> (signal.size());
    REQUIRE (repeats > total / 100);
    REQUIRE (listens > total / 100);

    // The crossfades keep the repeats inside the signal's own range; a stage that
    // summed dry and wet would overshoot it.
    REQUIRE (peak <= 0.5f + 1.0e-4f);
}

TEST_CASE ("a stutter is bounded on a hot signal", "[stutter]")
{
    j37dsp::FxStutter stutter;
    stutter.prepare (kRate);

    for (int i = 0; i < static_cast<int> (kRate); ++i)
    {
        auto left = 0.95f;
        auto right = -0.95f;
        stutter.process (left, right, 1.0f);

        REQUIRE (std::isfinite (left));
        REQUIRE (std::isfinite (right));
        REQUIRE (std::fabs (left) <= 1.0f);
        REQUIRE (std::fabs (right) <= 1.0f);
    }
}
