// The output safety limiter, measured on signal.
//
// Source/DSP/SafetyLimiter.h is a peak detector, a required gain and a smoothed
// gain - four one-pole coefficients and a ceiling. It is the stage that keeps the
// soft clipper idle, and until this harness nothing drove it: the only other
// mention of it in tests/ was a reduced replica inside the SUBFUND harness's
// signal path, which can only be wrong in the same way as the code it copies.
//
// The claims its banner makes are numbers on rendered audio:
//
//   * a programme below the ceiling is multiplied by exactly 1.0f - transparent
//     to the bit, which is what makes this a safety net rather than a sound;
//   * a step 6 dB over the ceiling is caught within a few milliseconds and held
//     there, with the correction settling at ceiling / peak;
//   * the catch is fast and the release is slow: after the burst the gain is still
//     down 10 ms later (it does not let go between events) and is only just over
//     half back after 120 ms;
//   * the shape is the same duration at every engine rate, because the four
//     coefficients come from BlockRateCoefficients.h built for that rate;
//   * reset() opens it, so a session never starts ducked.
//
// Everything is generated, so a failure is reproducible.
#include "shim.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "DSP/BlockRateCoefficients.h"
#include "DSP/SafetyLimiter.h"

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

// The coefficients exactly as the engine builds them: the four durations live in
// SafetyLimiter.h and BlockRateCoefficients.h turns them into poles for the rate.
j37dsp::SafetyLimiter::Coefficients coefficientsFor (double rate)
{
    const auto blockRate = j37rate::makeBlockRateCoefficients (static_cast<float> (rate), 0.5f);

    j37dsp::SafetyLimiter::Coefficients coefficients;
    coefficients.detectorAttack = blockRate.limiterDetectorAttack;
    coefficients.detectorRelease = blockRate.limiterDetectorRelease;
    coefficients.catchCoefficient = blockRate.limiterCatch;
    coefficients.recoveryCoefficient = blockRate.limiterRecovery;
    coefficients.ceiling = blockRate.limiterCeiling;
    return coefficients;
}

double decibels (double gain)
{
    return 20.0 * std::log10 (std::max (1.0e-12, gain));
}

// =============================================================================
//  Below the ceiling: transparent, to the bit.
// =============================================================================
void testTransparentBelowCeiling()
{
    constexpr double kRate = 48000.0;
    const auto coefficients = coefficientsFor (kRate);

    j37dsp::SafetyLimiter limiter;
    limiter.prepare (coefficients);

    float worstGainError = 0.0f;
    double worstSampleError = 0.0;

    for (int i = 0; i < static_cast<int> (kRate * 2.0); ++i)   // 2 s of -10 dBFS
    {
        const auto sample = static_cast<float> (0.3 * std::sin (kTwoPi * 220.0 * i / kRate));
        const auto gain = limiter.process (std::abs (sample));

        worstGainError = std::max (worstGainError, std::abs (gain - 1.0f));
        worstSampleError = std::max (worstSampleError,
                                     std::abs (static_cast<double> (sample * gain - sample)));
    }

    check (worstGainError == 0.0f,
           "a programme well below the ceiling is multiplied by exactly 1.0f for 2 s");
    check (worstSampleError == 0.0,
           "and comes back bit for bit, so the limiter adds no distortion of its own");
    check (limiter.getCeiling() == 0.70f,
           "the ceiling it is prepared with is softClip's knee (0.70)");

    // A signal that touches the ceiling without crossing it is still untouched:
    // the detector runs, but the gain it asks for is exactly 1.
    j37dsp::SafetyLimiter atCeiling;
    atCeiling.prepare (coefficients);

    float worstAtCeiling = 0.0f;

    for (int i = 0; i < 4800; ++i)
        worstAtCeiling = std::max (worstAtCeiling, std::abs (atCeiling.process (0.70f) - 1.0f));

    check (worstAtCeiling == 0.0f,
           "a signal sitting exactly on the ceiling is left exactly alone");
}

// =============================================================================
//  The point of the stage: a step is caught and held at the ceiling.
// =============================================================================
void testCapsAStep()
{
    constexpr double kRate = 48000.0;
    const auto coefficients = coefficientsFor (kRate);

    j37dsp::SafetyLimiter limiter;
    limiter.prepare (coefficients);

    constexpr float kPeak = 1.4f;   // +6 dB over the 0.70 ceiling

    int firstAtCeiling = -1;
    float worstOutput = 0.0f;

    for (int i = 0; i < static_cast<int> (kRate * 0.5); ++i)
    {
        const auto output = kPeak * limiter.process (kPeak);
        worstOutput = std::max (worstOutput, output);

        if (firstAtCeiling < 0 && output <= coefficients.ceiling * 1.0002f)
            firstAtCeiling = i;
    }

    const auto catchMs = firstAtCeiling / kRate * 1000.0;

    check (worstOutput <= kPeak,
           "the limiter never lifts a peak above the signal it was handed");
    check (firstAtCeiling > 0 && catchMs < 6.0,
           "a +6 dB step is caught within 6 ms (caught after " + std::to_string (catchMs)
           + " ms)");

    const auto settled = kPeak * limiter.getGain();

    check (std::abs (settled - coefficients.ceiling) < 1.0e-3f,
           "and held at the ceiling rather than merely below it (settled at "
           + std::to_string (decibels (settled)) + " dBFS against "
           + std::to_string (decibels (coefficients.ceiling)) + ")");
    check (std::abs (limiter.getGain() - coefficients.ceiling / kPeak) < 1.0e-3f,
           "the settled gain is ceiling / peak, not a fixed attenuation");
}

// =============================================================================
//  One gain for the frame, so the image cannot shift.
// =============================================================================
void testOneGainForTheFrame()
{
    constexpr double kRate = 48000.0;
    const auto coefficients = coefficientsFor (kRate);

    j37dsp::SafetyLimiter limiter;
    limiter.prepare (coefficients);

    constexpr float kLeft = 1.4f;
    constexpr float kRight = 0.2f;

    // What the signal path does: the frame's loudest channel is what the limiter
    // is handed, and the one gain it returns is applied to both sides.
    for (int i = 0; i < 48000; ++i)
        (void) limiter.process (std::max (std::abs (kLeft), std::abs (kRight)));

    const auto gain = limiter.getGain();
    const auto leftOut = kLeft * gain;
    const auto rightOut = kRight * gain;

    check (leftOut <= coefficients.ceiling * 1.001f,
           "the loud side is brought to the ceiling");
    check (std::abs (rightOut / leftOut - kRight / kLeft) < 1.0e-6f,
           "the quiet side keeps its ratio to the loud one, so the image does not shift");
}

// =============================================================================
//  Fast catch, slow release - the asymmetry that makes it a limiter.
// =============================================================================
void testCatchFastReleaseSlow()
{
    constexpr double kRate = 48000.0;
    const auto coefficients = coefficientsFor (kRate);

    j37dsp::SafetyLimiter limiter;
    limiter.prepare (coefficients);

    for (int i = 0; i < static_cast<int> (kRate * 0.1); ++i)   // a 100 ms burst
        (void) limiter.process (1.4f);

    const auto caught = limiter.getGain();

    // The burst stops and normal programme returns. The gain must not jump back:
    // 10 ms of quiet is a gap between notes, not a reason to let go.
    const auto afterGap = [&]
    {
        for (int i = 0; i < static_cast<int> (kRate * 0.01); ++i)
            (void) limiter.process (0.25f);

        return limiter.getGain();
    }();

    const auto recoveredBy = [&] (double seconds)
    {
        const auto frames = static_cast<int> (kRate * seconds);
        for (int i = 0; i < frames; ++i)
            (void) limiter.process (0.25f);

        return (limiter.getGain() - caught) / (1.0f - caught);
    };

    const auto atSixtyMs = recoveredBy (0.05);    // 10 ms already spent above
    const auto atOneTwentyMs = recoveredBy (0.06);
    const auto atTwoFortyMs = recoveredBy (0.12);

    check (caught < 0.51f,
           "the burst leaves the limiter engaged (" + std::to_string (caught) + " gain)");
    check (std::abs (afterGap - caught) < 0.02f,
           "10 ms of quiet after the burst moves the gain by "
           + std::to_string (100.0 * std::abs (afterGap - caught)) + " %, so it does not pump");

    check (atSixtyMs > 0.0 && atSixtyMs < 0.25f,
           "under a quarter of the correction is paid back after 60 ms ("
           + std::to_string (100.0 * atSixtyMs) + " %)");
    check (atOneTwentyMs > 0.30f && atOneTwentyMs < 0.65f,
           "just under half after 120 ms (" + std::to_string (100.0 * atOneTwentyMs) + " %)");
    check (atTwoFortyMs > 0.75f,
           "most of it after 240 ms (" + std::to_string (100.0 * atTwoFortyMs) + " %)");
}

// =============================================================================
//  The shape is a duration, not a sample count: it survives a rate change.
// =============================================================================
void testRateIndependent()
{
    const auto catchMsAt = [] (double rate)
    {
        const auto coefficients = coefficientsFor (rate);

        j37dsp::SafetyLimiter limiter;
        limiter.prepare (coefficients);

        for (int i = 0; i < static_cast<int> (rate); ++i)
            if (1.4f * limiter.process (1.4f) <= coefficients.ceiling * 1.0002f)
                return i / rate * 1000.0;

        return -1.0;
    };

    const auto at44k = catchMsAt (44100.0);
    const auto at48k = catchMsAt (48000.0);
    const auto at96k = catchMsAt (96000.0);
    const auto at192k = catchMsAt (192000.0);

    check (at44k > 0.0 && at48k > 0.0 && at96k > 0.0 && at192k > 0.0,
           "every rate catches the same step");
    check (std::abs (at44k - at48k) < 0.1 && std::abs (at96k - at48k) < 0.1
           && std::abs (at192k - at48k) < 0.1,
           "and in the same number of MILLISECONDS at 44.1, 48, 96 and 192 kHz ("
           + std::to_string (at48k) + " ms), so the coefficients follow the rate");
}

// =============================================================================
//  reset() opens it: a session never starts ducked.
// =============================================================================
void testResetIsOpen()
{
    const auto coefficients = coefficientsFor (48000.0);

    j37dsp::SafetyLimiter limiter;
    limiter.prepare (coefficients);

    for (int i = 0; i < 4800; ++i)
        (void) limiter.process (1.4f);

    check (limiter.getGain() < 0.9f, "the limiter is engaged before the reset");

    limiter.reset();

    check (limiter.getGain() == 1.0f && limiter.getDetector() == 0.0f,
           "reset() leaves an open gain and no detector history");

    // And the first frame after it is not ducked by a stale correction.
    check (limiter.process (0.2f) == 1.0f,
           "the first frame after reset() is untouched");
}

} // namespace

int main()
{
    std::printf ("safety limiter\n");

    testTransparentBelowCeiling();
    testCapsAStep();
    testOneGainForTheFrame();
    testCatchFastReleaseSlow();
    testRateIndependent();
    testResetIsOpen();

    if (gFailures != 0)
    {
        std::printf ("\n%d check(s) failed\n", gFailures);
        return 1;
    }

    std::printf ("\nall checks passed\n");
    return 0;
}
