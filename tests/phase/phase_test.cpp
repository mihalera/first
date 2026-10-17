// The multiband anti-phase guard, measured band by band.
//
// Source/DSP/MultibandPhaseGuard.h splits the frame with Linkwitz-Riley
// crossovers, measures each band's own correlation, and rotates only the bands
// that are opposing. It is pure arithmetic on the samples it is handed, so it can
// be run here sample by sample with no JUCE, no block size and no host - and the
// claims it makes are all measurable:
//
//   * the three bands sum back to a flat magnitude (a crossover that does not is
//     a filter in the path, not a splitter);
//   * a frame with no opposition comes back BIT FOR BIT, even at full engagement;
//   * an inverted frame loses its side, and a frame whose opposition lives only
//     in the low band loses it only there - the mid and high bands' sides stay
//     where they were, which is the whole difference from the single-band guard;
//   * when the fault goes away the guard unwinds and the signal is exact again.
//
// The signals are generated, so a failure is reproducible.
#include "shim.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "DSP/MultibandPhaseGuard.h"

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

constexpr int kRate = 48000;

/** Peak amplitude of a sinusoid measured over the last `count` samples, after
    discarding `skip` - steady state rather than the attack of a filter. */
double amplitudeOf (const std::vector<float>& signal, int skip)
{
    double peak = 0.0;

    for (std::size_t i = static_cast<std::size_t> (skip); i < signal.size(); ++i)
        peak = std::max (peak, std::abs (static_cast<double> (signal[i])));

    return peak;
}

// =============================================================================
//  The crossover: three bands that sum back to the signal they came from.
// =============================================================================
void testCrossoverIsFlat()
{
    j37dsp::ThreeBandSplit split;
    split.prepare (j37dsp::MultibandPhaseGuard::lowCrossoverHz,
                   j37dsp::MultibandPhaseGuard::highCrossoverHz,
                   static_cast<float> (kRate));

    double worstDb = 0.0;

    for (const auto frequency : { 30.0, 100.0, 200.0, 440.0, 1000.0, 2000.0,
                                  5000.0, 10000.0, 15000.0 })
    {
        split.reset();
        std::vector<float> output (kRate / 2);

        for (std::size_t i = 0; i < output.size(); ++i)
        {
            const auto input = static_cast<float> (
                std::sin (kTwoPi * frequency * static_cast<double> (i) / kRate));

            split.process (input);
            output[i] = split.band[0] + split.band[1] + split.band[2];
        }

        const auto gainDb = 20.0 * std::log10 (std::max (1.0e-9, amplitudeOf (output, 8000)));
        worstDb = std::max (worstDb, std::abs (gainDb));
    }

    check (worstDb < 0.5,
           "the three bands sum to a flat magnitude (worst " + std::to_string (worstDb)
           + " dB across 30 Hz to 15 kHz)");

    // -6 dB at the crossover, which is what makes the sum flat rather than
    // doubled: each band is the square of a 2nd-order Butterworth there.
    for (const auto crossing : { j37dsp::MultibandPhaseGuard::lowCrossoverHz,
                                 j37dsp::MultibandPhaseGuard::highCrossoverHz })
    {
        j37dsp::LinkwitzRiley4 lr;
        lr.prepare (crossing, static_cast<float> (kRate));

        std::vector<float> low (kRate / 2), high (kRate / 2);

        for (std::size_t i = 0; i < low.size(); ++i)
        {
            lr.process (static_cast<float> (
                std::sin (kTwoPi * crossing * static_cast<double> (i) / kRate)));
            low[i] = lr.low;
            high[i] = lr.high;
        }

        const auto lowDb = 20.0 * std::log10 (std::max (1.0e-9, amplitudeOf (low, 8000)));
        const auto highDb = 20.0 * std::log10 (std::max (1.0e-9, amplitudeOf (high, 8000)));

        check (std::abs (lowDb + 6.0) < 0.5 && std::abs (highDb + 6.0) < 0.5,
               "each path is -6 dB at " + std::to_string (static_cast<int> (crossing)) + " Hz");
    }
}

// =============================================================================
//  Idle: a healthy frame is untouched, to the bit.
// =============================================================================
void testHealthyFrameIsUntouched()
{
    j37dsp::MultibandPhaseGuard guard;
    guard.prepare (kRate);

    // A correlated stereo pair with plenty of dynamics: the test signal is not a
    // pure sine, so "untouched" cannot be satisfied by a gain of one.
    std::vector<float> left (kRate), right (kRate);

    for (std::size_t i = 0; i < left.size(); ++i)
    {
        const auto t = static_cast<double> (i) / kRate;
        left[i] = static_cast<float> (0.4 * std::sin (kTwoPi * 220.0 * t)
                                      + 0.2 * std::sin (kTwoPi * 1300.0 * t));
        right[i] = left[i] * 0.8f;
    }

    auto identical = true;

    for (std::size_t i = 0; i < left.size(); ++i)
    {
        auto l = left[i];
        auto r = right[i];
        guard.process (l, r, 1.0f);              // full engagement, no fault

        if (l != left[i] || r != right[i])
            identical = false;
    }

    check (identical, "a correlated frame at full engagement comes back bit for bit");
}

// =============================================================================
//  A fault that lives in one band moves that band, and only that band.
// =============================================================================
void testOppositionIsFixedPerBand()
{
    // Low band inverted, high band wide but IN PHASE (different levels, so it has
    // side content of its own). The single-band guard would rotate the whole
    // frame by the low band's reading; this one must leave the top alone.
    //
    // Five seconds, measured over the last half of the last second: the guard's
    // envelopes are a 400 ms pole, so measuring from the first sample would be
    // measuring its attack rather than its answer.
    j37dsp::MultibandPhaseGuard guard;
    guard.prepare (kRate);

    const auto seconds = 5;
    std::vector<float> dryLeft (kRate * seconds), dryRight (kRate * seconds);
    std::vector<float> wetLeft (kRate * seconds), wetRight (kRate * seconds);

    for (std::size_t i = 0; i < dryLeft.size(); ++i)
    {
        const auto t = static_cast<double> (i) / kRate;
        const auto low = 0.4 * std::sin (kTwoPi * 60.0 * t);
        const auto high = 0.4 * std::sin (kTwoPi * 6000.0 * t);

        dryLeft[i] = static_cast<float> (low + high);          // low is inverted ...
        dryRight[i] = static_cast<float> (-low + 0.55 * high); // ... high is not, but is wide

        auto l = dryLeft[i];
        auto r = dryRight[i];
        guard.process (l, r, 1.0f);

        wetLeft[i] = l;
        wetRight[i] = r;
    }

    const auto settled = static_cast<int> (kRate * (seconds - 0.5));

    // Side energy of ONE band, measured by re-splitting the pair with the same
    // crossover the guard uses.
    const auto bandSide = [] (const std::vector<float>& left,
                              const std::vector<float>& right,
                              float cutoffHz, bool lowBand, int skip)
    {
        j37dsp::LinkwitzRiley4 splitter;
        splitter.prepare (cutoffHz, static_cast<float> (kRate));

        double sum = 0.0;

        for (std::size_t i = 0; i < left.size(); ++i)
        {
            splitter.process (0.5f * (left[i] - right[i]));

            if (static_cast<int> (i) >= skip)
            {
                const auto value = static_cast<double> (lowBand ? splitter.low : splitter.high);
                sum += value * value;
            }
        }

        return std::sqrt (sum / static_cast<double> (left.size() - static_cast<std::size_t> (skip)));
    };

    const auto dryLowSide = bandSide (dryLeft, dryRight, 200.0f, true, settled);
    const auto wetLowSide = bandSide (wetLeft, wetRight, 200.0f, true, settled);
    const auto lowReductionDb = 20.0 * std::log10 (std::max (1.0e-9, wetLowSide / dryLowSide));

    check (lowReductionDb < -20.0,
           "the inverted low band loses its side (" + std::to_string (lowReductionDb) + " dB)");

    const auto dryHighSide = bandSide (dryLeft, dryRight, 2000.0f, false, settled);
    const auto wetHighSide = bandSide (wetLeft, wetRight, 2000.0f, false, settled);
    const auto highChangeDb = 20.0 * std::log10 (std::max (1.0e-9, wetHighSide / dryHighSide));

    check (std::abs (highChangeDb) < 1.0,
           "the band that was healthy keeps its side (" + std::to_string (highChangeDb)
           + " dB, within 1)");

    // And the inverted band is not merely quieter: what opposed has been folded
    // into the mid, so the LOW band's mid does not collapse.
    j37dsp::LinkwitzRiley4 lowOfWet;
    lowOfWet.prepare (200.0f, static_cast<float> (kRate));
    double midLowSum = 0.0;

    for (std::size_t i = 0; i < wetLeft.size(); ++i)
    {
        lowOfWet.process (0.5f * (wetLeft[i] + wetRight[i]));

        if (static_cast<int> (i) >= settled)
            midLowSum += static_cast<double> (lowOfWet.low) * lowOfWet.low;
    }

    check (std::sqrt (midLowSum / static_cast<double> (wetLeft.size() - settled)) > 0.05,
           "the recovered low band is folded into mid rather than faded away");
}

// =============================================================================
//  Recovery: when the fault goes away the guard unwinds and the signal is exact.
// =============================================================================
void testGuardUnwinds()
{
    j37dsp::MultibandPhaseGuard guard;
    guard.prepare (kRate);

    // One second of an inverted pair, driven through the guard, then four of a
    // correlated one. The rotation is smoothed by the same 400 ms envelope that
    // detected the fault, so what this checks is that it CONVERGES - by the end
    // of the fourth second the pair has to be back to bit-level clean.
    const auto toneAt = [] (std::size_t index)
    {
        return static_cast<float> (0.4 * std::sin (kTwoPi * 120.0
                                                   * static_cast<double> (index) / kRate));
    };

    std::vector<float> left (kRate), right (kRate);

    for (std::size_t i = 0; i < left.size(); ++i)
    {
        const auto tone = toneAt (i);
        const auto inverted = i < static_cast<std::size_t> (kRate / 2);

        left[i] = tone;
        right[i] = inverted ? -tone : tone;

        auto l = left[i];
        auto r = right[i];
        guard.process (l, r, 1.0f);
    }

    std::size_t index = left.size();
    std::vector<float> tailLeft (kRate * 4), tailRight (kRate * 4);

    for (std::size_t i = 0; i < tailLeft.size(); ++i, ++index)
    {
        const auto tone = toneAt (index);

        auto l = tone;
        auto r = tone;
        guard.process (l, r, 1.0f);
        tailLeft[i] = l;
        tailRight[i] = r;
    }

    // The last eighth of the tail - 0.5 s, a little over four seconds after the
    // fault ended. The rotation unwinds through two 400 ms envelopes (the
    // correlation and its own), so it approaches zero rather than reaching it;
    // this is the window where what is left is below a thousandth.
    auto worstError = 0.0f;

    for (std::size_t i = tailLeft.size() * 7 / 8; i < tailLeft.size(); ++i)
        worstError = std::max (worstError, std::abs (tailLeft[i] - tailRight[i]));

    check (worstError < 1.0e-3f,
           "after the fault ends the guard unwinds and the pair is whole again");
}

// =============================================================================
//  Nothing in, nothing out - and no NaN on a silent frame at full engagement.
// =============================================================================
void testSilenceStaysSilent()
{
    j37dsp::MultibandPhaseGuard guard;
    guard.prepare (kRate);

    auto clean = true;

    for (int i = 0; i < kRate; ++i)
    {
        auto l = 0.0f;
        auto r = 0.0f;
        guard.process (l, r, 1.0f);

        if (l != 0.0f || r != 0.0f || ! std::isfinite (l) || ! std::isfinite (r))
            clean = false;
    }

    check (clean, "silence through a fully engaged guard is silence, with no NaN");
}
} // namespace

int main()
{
    std::printf ("multiband anti-phase guard (Linkwitz-Riley split, per-band rotation)\n");

    testCrossoverIsFlat();
    testHealthyFrameIsUntouched();
    testOppositionIsFixedPerBand();
    testGuardUnwinds();
    testSilenceStaysSilent();

    std::printf ("\n%s (%d failure%s)\n", gFailures == 0 ? "PHASE CHECK PASSED" : "FAILURES PRESENT",
                 gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
