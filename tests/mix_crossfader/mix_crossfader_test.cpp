// The MIX crossfade, measured on signal.
//
// Source/DSP/MixCrossfader.h is the dry/wet gain pair (a quarter turn of cos/sin,
// so dry^2 + wet^2 is 1 at every position) and the two gated legs the engine sums.
// Until this harness the law was three lines inside processTapeEngine, and the only
// thing that read them was a text check in tests/transport, which can see that a
// gate is multiplied in but not that the crossfade is smooth, that 0 % is the input
// and 100 % is the tape path, or that the midpoint is +3 dB on correlated material.
//
// The claims, all of them numbers here:
//
//   * 0 % is exactly (dry 1, wet 0) and 100 % is exactly (wet 1, dry ~ -4.4e-8) -
//     the one rounding the law has, stated rather than hidden;
//   * dry^2 + wet^2 stays 1 across the whole travel, so an UNCORRELATED pair is
//     held at one level from end to end - measured as RMS on rendered signal;
//   * on CORRELATED material the midpoint is +3 dB (0.707 + 0.707), which is the
//     cost of that law and is pinned so it cannot be \"fixed\" silently;
//   * the travel is continuous: a 1000-step ramp moves neither gain by more than
//     0.0016, so nothing about the law can step the output;
//   * BOTH legs carry the transport gate, so STOP is silence even at MIX 0, and
//     the gate scales the two legs linearly instead of letting go of one;
//   * the position is clamped, NaN included, exactly as juce::jlimit clamped it.
//
// Everything is generated, so a failure is reproducible.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "DSP/MixCrossfader.h"

namespace
{
int gFailures = 0;

void check (bool condition, const std::string& what)
{
    std::printf ("%s %s\n", condition ? "  ok  " : "FAILED", what.c_str());

    if (! condition)
        ++gFailures;
}

constexpr double kTwoPi = 6.283185307179586;

using Gains = j37dsp::MixCrossfader::Gains;

// =============================================================================
//  The endpoints: 0 % is the input, 100 % is all tape.
// =============================================================================
void testEndpoints()
{
    const auto dry = j37dsp::MixCrossfader::gainsFor (0.0f);
    const auto wet = j37dsp::MixCrossfader::gainsFor (1.0f);

    check (dry.dry == 1.0f && dry.wet == 0.0f,
           "MIX 0 % is (dry 1, wet 0), exactly");

    check (wet.wet == 1.0f && std::abs (wet.dry) < 1.0e-7f,
           "MIX 100 % is all tape (wet exactly 1, dry " + std::to_string (wet.dry)
           + " - cos(pi/2) in float)");

    // Through the legs: MIX 0 passes the dry signal bit for bit, which is the
    // claim the signal path's own comment makes about `machineDryInput`.
    constexpr float kDry = 0.37f;
    constexpr float kWet = 0.93f;

    const auto atZero = j37dsp::MixCrossfader::dryLeg (kDry, dry, 1.0f)
                      + j37dsp::MixCrossfader::wetLeg (kWet, dry, 1.0f);

    check (atZero == kDry,
           "MIX 0 % returns the dry input bit for bit rather than several dB off it");

    const auto atFull = j37dsp::MixCrossfader::dryLeg (kDry, wet, 1.0f)
                      + j37dsp::MixCrossfader::wetLeg (kWet, wet, 1.0f);

    check (std::abs (atFull - kWet) < 1.0e-7f,
           "MIX 100 % returns the tape path (" + std::to_string (atFull) + " against "
           + std::to_string (kWet) + ")");

    // SUBFUND rides the wet gain, so MIX 0 takes the undertones with it.
    check (dry.wet == 0.0f && wet.wet == 1.0f,
           "the wet gain the SUBFUND join rides is 0 at MIX 0 % and 1 at 100 %");
}

// =============================================================================
//  The law: equal power across the travel, and what it costs on a correlated pair.
// =============================================================================
void testEqualPower()
{
    float worstPowerError = 0.0f;
    float worstMixError = 0.0f;
    float previousDry = 1.0f;
    float previousWet = 0.0f;
    float worstStep = 0.0f;
    bool monotone = true;

    for (int i = 0; i <= 1000; ++i)
    {
        const auto gain = j37dsp::MixCrossfader::gainsFor (i / 1000.0f);

        worstPowerError = std::max (worstPowerError,
                                    std::abs (gain.dry * gain.dry + gain.wet * gain.wet - 1.0f));
        worstStep = std::max (worstStep, std::abs (gain.dry - previousDry));
        worstStep = std::max (worstStep, std::abs (gain.wet - previousWet));

        if (gain.dry > previousDry + 1.0e-7f || gain.wet < previousWet - 1.0e-7f)
            monotone = false;

        previousDry = gain.dry;
        previousWet = gain.wet;
    }

    const auto middle = j37dsp::MixCrossfader::gainsFor (0.5f);
    worstMixError = std::abs (middle.dry - middle.wet);

    check (worstPowerError < 1.0e-5f,
           "dry^2 + wet^2 stays 1 across the whole travel (worst "
           + std::to_string (worstPowerError) + ")");
    check (monotone, "one leg rises and the other falls across the travel, with no wobble");
    check (worstMixError < 1.0e-6f && std::abs (middle.dry - 0.70710678f) < 1.0e-6f,
           "the midpoint is 1/sqrt(2) on both legs (" + std::to_string (middle.dry) + ")");

    // The documented cost of the law, pinned: on a CORRELATED pair the midpoint is
    // the sum of the two legs, which is sqrt(2) = +3 dB.
    check (std::abs (middle.dry + middle.wet - 1.4142135f) < 1.0e-6f,
           "the midpoint of a correlated pair is +3 dB ("
           + std::to_string (middle.dry + middle.wet) + "), which is the law's cost");

    // Continuity: a 1000-step travel of the control moves neither gain by more
    // than 0.0016, so nothing the law produces can be heard as a step.
    check (worstStep < 0.0016f,
           "a 1000-step travel moves neither gain by more than 0.0016 (worst "
           + std::to_string (worstStep) + ")");
}

// =============================================================================
//  On rendered signal: the level is held for an uncorrelated pair, and +3 dB at
//  the middle for a correlated one.
// =============================================================================
void testLevelOnRenderedSignal()
{
    constexpr double kRate = 48000.0;
    constexpr double kAmplitude = 0.25;
    constexpr int kBlock = 4800;   // 100 ms per position: long enough for any phase

    const auto renderRms = [&] (double wetFrequency, float mix)
    {
        const auto gain = j37dsp::MixCrossfader::gainsFor (mix);
        double squares = 0.0;
        double dryPhase = 0.0;
        double wetPhase = 0.0;

        for (int i = 0; i < kBlock; ++i)
        {
            const auto dry = static_cast<float> (kAmplitude * std::sin (dryPhase));
            const auto wet = static_cast<float> (kAmplitude * std::sin (wetPhase));

            // What the engine sums, gate open.
            const auto out = j37dsp::MixCrossfader::dryLeg (dry, gain, 1.0f)
                           + j37dsp::MixCrossfader::wetLeg (wet, gain, 1.0f);
            squares += static_cast<double> (out) * out;

            dryPhase += kTwoPi * 1000.0 / kRate;
            wetPhase += kTwoPi * wetFrequency / kRate;
        }

        return std::sqrt (squares / kBlock);
    };

    // Uncorrelated: a 1 kHz tone against a 3.137 kHz one, incommensurate so the
    // two never sit in a fixed phase relationship.
    const auto atZero = renderRms (3137.0, 0.0f);
    const auto atMiddle = renderRms (3137.0, 0.5f);
    const auto atFull = renderRms (3137.0, 1.0f);

    const auto decibels = [] (double value, double reference)
    {
        return 20.0 * std::log10 (std::max (1.0e-12, value / reference));
    };

    check (std::abs (decibels (atMiddle, atZero)) < 0.5,
           "an uncorrelated pair holds its level through the middle of the travel ("
           + std::to_string (decibels (atMiddle, atZero)) + " dB against the endpoint)");
    check (std::abs (decibels (atFull, atZero)) < 0.5,
           "and at the far end (" + std::to_string (decibels (atFull, atZero))
           + " dB against MIX 0 %)");

    // Correlated: one tone on both legs, which is the common case in the plugin -
    // the input and the machine's version of it.
    const auto correlatedMiddle = renderRms (1000.0, 0.5f);
    const auto correlatedZero = renderRms (1000.0, 0.0f);

    check (std::abs (decibels (correlatedMiddle, correlatedZero) - 3.01) < 0.2,
           "a correlated pair is +3 dB at the midpoint ("
           + std::to_string (decibels (correlatedMiddle, correlatedZero))
           + " dB), which is documented rather than fixed");
}

// =============================================================================
//  Both legs carry the transport gate - the half the transport check cannot see.
// =============================================================================
void testBothLegsAreGated()
{
    const auto gains = j37dsp::MixCrossfader::gainsFor (0.35f);

    constexpr float kDry = 0.6f;
    constexpr float kWet = 0.4f;
    constexpr float kGate = 0.5f;

    const auto open = j37dsp::MixCrossfader::dryLeg (kDry, gains, 1.0f)
                    + j37dsp::MixCrossfader::wetLeg (kWet, gains, 1.0f);
    const auto half = j37dsp::MixCrossfader::dryLeg (kDry, gains, kGate)
                    + j37dsp::MixCrossfader::wetLeg (kWet, gains, kGate);

    check (std::abs (half - open * kGate) < 1.0e-7f,
           "a gate at 0.5 halves BOTH legs, so the crossfade's balance is unchanged by STOP");

    check (j37dsp::MixCrossfader::dryLeg (kDry, gains, 0.0f) == 0.0f
           && j37dsp::MixCrossfader::wetLeg (kWet, gains, 0.0f) == 0.0f,
           "a closed gate passes nothing from either leg");

    // The claim STOP depends on: at MIX 0 the machine IS the input's path, and a
    // stop there is still a stop - the input does not survive on the dry leg.
    const auto atZero = j37dsp::MixCrossfader::gainsFor (0.0f);
    const auto stopped = j37dsp::MixCrossfader::dryLeg (kDry, atZero, 0.0f)
                       + j37dsp::MixCrossfader::wetLeg (kWet, atZero, 0.0f);

    check (stopped == 0.0f,
           "MIX 0 % on a stopped machine is silence, not the input with the tape gone");
}

// =============================================================================
//  The clamp: 0 .. 1, NaN included, exactly as juce::jlimit answered.
// =============================================================================
void testClamp()
{
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto nanGains = j37dsp::MixCrossfader::gainsFor (nan);

    check (j37dsp::MixCrossfader::clampMix (-0.5f) == 0.0f
           && j37dsp::MixCrossfader::clampMix (1.5f) == 1.0f,
           "a position outside 0 .. 1 is clamped rather than followed");
    check (nanGains.dry == 1.0f && nanGains.wet == 0.0f,
           "a NaN position reads as MIX 0 %, which is what juce::jlimit answered");

    // Clamping is idempotent, so reading the position once for the hot-path probe
    // and again inside the law cannot move it.
    for (int i = 0; i <= 100; ++i)
    {
        const auto value = i / 100.0f;

        if (j37dsp::MixCrossfader::clampMix (j37dsp::MixCrossfader::clampMix (value)) != value)
        {
            check (false, "clamping a position twice changed it");
            return;
        }
    }

    check (true, "clamping a position twice changes nothing");
}

} // namespace

int main()
{
    std::printf ("mix crossfader\n");

    testEndpoints();
    testEqualPower();
    testLevelOnRenderedSignal();
    testBothLegsAreGated();
    testClamp();

    if (gFailures != 0)
    {
        std::printf ("\n%d check(s) failed\n", gFailures);
        return 1;
    }

    std::printf ("\nall checks passed\n");
    return 0;
}
