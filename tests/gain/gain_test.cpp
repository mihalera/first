// The AUTO GAIN compensator, measured on signal.
//
// Source/DSP/AutoGainCompensator.h is the stage that compares the finished
// signal against the reference taken after the input trim and pays back what the
// chain lost. It is a gain applied to every sample and, until this harness, the
// one gain stage in the chain that nothing measured. The claims its banner makes
// are all numbers on rendered audio:
//
//   * a programme that lost 6 dB comes back at the level it arrived at, and the
//     correction converges in the 450 ms its coefficient describes;
//   * it only ever restores - a programme that arrives louder is left exactly
//     alone, and a loss deeper than 12 dB is only restored by 12;
//   * silence is not a measurement: a silent frame returns unity and holds the
//     correction instead of driving it to its limit;
//   * a short dip is an average's business, not a transient's - the correction
//     must not chase it.
//
// The signals are generated, so a failure is reproducible.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

#include "DSP/AutoGainCompensator.h"

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

// The coefficient the engine builds for the compensator: 450 ms, stated as a
// duration at the engine's rate (Source/ProcessorSignalPath.cpp, onePoleCoefficient).
float compensationCoefficient (double rate, double milliseconds = 450.0)
{
    const auto seconds = milliseconds * 0.001;
    return static_cast<float> (1.0 - std::exp (-1.0 / (seconds * rate)));
}

double decibels (double gain)
{
    return 20.0 * std::log10 (std::max (1.0e-12, gain));
}

/** A two-channel sine, so the frame power the compensator reads is a real
    programme power rather than a constant. */
struct StereoProgramme
{
    double rate = 48000.0;
    double frequency = 1000.0;
    double amplitude = 0.25;
    double phase = 0.0;

    std::array<float, 2> next() noexcept
    {
        const auto value = static_cast<float> (amplitude * std::sin (phase));
        phase += kTwoPi * frequency / rate;

        if (phase > kTwoPi)
            phase -= kTwoPi;

        // The two sides are deliberately not identical: a phase offset gives the
        // frame a mid and a side, which is what the power sum sees in the plugin.
        const auto other = static_cast<float> (amplitude * std::sin (phase - 0.4));
        return { value, other };
    }
};

float framePower (const std::array<float, 2>& frame) noexcept
{
    return frame[0] * frame[0] + frame[1] * frame[1];
}

// =============================================================================
//  Idle: nothing lost, nothing done - and exactly.
// =============================================================================
void testIdleIsTransparent()
{
    j37dsp::AutoGainCompensator compensator;
    const auto coefficient = compensationCoefficient (48000.0);

    StereoProgramme programme;
    float worstGainError = 0.0f;
    int frames = 0;

    for (; frames < 96000; ++frames)  // 2 s
    {
        const auto frame = programme.next();
        const auto power = framePower (frame);
        const auto gain = compensator.process (power, power, coefficient);

        worstGainError = std::max (worstGainError, std::abs (gain - 1.0f));
    }

    check (worstGainError == 0.0f,
           "a programme that lost nothing is multiplied by exactly 1.0f, for 2 s");
    check (compensator.getSmoothedDb() == 0.0f,
           "the correction stays at exactly 0 dB rather than near it");
    check (frames == 96000, "the harness ran the frames it claims");
}

// =============================================================================
//  The point of the stage: a loss is paid back, in the time the pole describes.
// =============================================================================
void testRestoresALossOnSignal()
{
    j37dsp::AutoGainCompensator compensator;
    const auto coefficient = compensationCoefficient (48000.0);

    StereoProgramme programme;
    constexpr double kMachineLoss = 0.5;   // -6 dB

    double inputSquares = 0.0;
    double outputSquares = 0.0;
    int measured = 0;
    float worstGain = 0.0f;

    for (int i = 0; i < 144000; ++i)   // 3 s: more than six time constants
    {
        const auto frame = programme.next();
        const auto referencePower = framePower (frame);

        const std::array<float, 2> machine {
            static_cast<float> (frame[0] * kMachineLoss),
            static_cast<float> (frame[1] * kMachineLoss)
        };

        const auto gain = compensator.process (referencePower, framePower (machine), coefficient);
        worstGain = std::max (worstGain, gain);

        // The last second is the settled reading: the first two are the ramp.
        if (i >= 96000)
        {
            for (int channel = 0; channel < 2; ++channel)
            {
                const auto out = machine[static_cast<std::size_t> (channel)] * gain;
                inputSquares += static_cast<double> (frame[static_cast<std::size_t> (channel)])
                              * frame[static_cast<std::size_t> (channel)];
                outputSquares += static_cast<double> (out) * out;
            }

            ++measured;
        }
    }

    const auto outputLevel = decibels (std::sqrt (outputSquares / (2.0 * measured)));
    const auto inputLevel = decibels (std::sqrt (inputSquares / (2.0 * measured)));

    check (std::abs (outputLevel - inputLevel) < 0.05,
           "a -6 dB programme returns to its input level (input " + std::to_string (inputLevel)
           + " dBFS, output " + std::to_string (outputLevel) + " dBFS)");
    check (std::abs (compensator.getSmoothedDb() - 6.0f) < 0.05f,
           "the settled correction is 6 dB, not more (" + std::to_string (compensator.getSmoothedDb())
           + " dB)");
    check (worstGain <= 2.0f + 1.0e-5f,
           "the correction never overshoots the loss it is restoring");
}

// =============================================================================
//  It is a compensator, not a boost stage: a louder programme is left alone.
// =============================================================================
void testNeverBoosts()
{
    j37dsp::AutoGainCompensator compensator;
    const auto coefficient = compensationCoefficient (48000.0);

    StereoProgramme programme;
    float worstGain = 0.0f;

    for (int i = 0; i < 96000; ++i)
    {
        const auto frame = programme.next();
        const auto referencePower = framePower (frame);

        const std::array<float, 2> machine {
            static_cast<float> (frame[0] * 2.0),
            static_cast<float> (frame[1] * 2.0)
        };

        worstGain = std::max (worstGain,
                              compensator.process (referencePower, framePower (machine), coefficient));
    }

    check (worstGain == 1.0f && compensator.getSmoothedDb() == 0.0f,
           "a programme 6 dB above the reference is left exactly where it was");
}

// =============================================================================
//  The ceiling is a limit, not a target.
// =============================================================================
void testRestorationCeiling()
{
    j37dsp::AutoGainCompensator compensator;
    const auto coefficient = compensationCoefficient (48000.0);

    const auto machineLoss = std::pow (10.0, -30.0 / 20.0);   // -30 dB
    const auto ceilingGain = static_cast<float> (
        std::pow (10.0, j37dsp::AutoGainCompensator::maximumRestorationDb / 20.0));

    float worstGain = 0.0f;

    for (int i = 0; i < 192000; ++i)   // 4 s
    {
        const auto referencePower = 0.125f;                        // two 0.25 channels
        const auto outputPower = static_cast<float> (referencePower * machineLoss * machineLoss);
        worstGain = std::max (worstGain, compensator.process (referencePower, outputPower, coefficient));
    }

    check (worstGain <= ceilingGain + 1.0e-5f,
           "a -30 dB programme is never lifted by more than the 12 dB ceiling (peak "
           + std::to_string (decibels (worstGain)) + " dB)");
    check (compensator.getSmoothedDb() > 11.9f,
           "the correction settles at the ceiling rather than stopping short ("
           + std::to_string (compensator.getSmoothedDb()) + " dB)");
}

// =============================================================================
//  The pole: 450 ms is 450 ms, at any rate.
// =============================================================================
void testTimeConstantAndRate()
{
    const auto atRate = [] (double rate)
    {
        j37dsp::AutoGainCompensator compensator;
        const auto coefficient = compensationCoefficient (rate);
        const auto frames = static_cast<int> (rate * 0.45);   // exactly one time constant

        for (int i = 0; i < frames; ++i)
            compensator.process (0.125f, 0.03125f, coefficient);   // -6 dB

        return compensator.getSmoothedDb();
    };

    const auto at48k = atRate (48000.0);
    const auto at96k = atRate (96000.0);

    // One time constant is 1 - 1/e of the target: 6 * 0.6321 = 3.793 dB.
    const auto expected = static_cast<float> (6.0 * (1.0 - std::exp (-1.0)));

    check (std::abs (at48k - expected) < 0.02f,
           "after 450 ms at 48 kHz the correction is " + std::to_string (at48k)
           + " of 6 dB (one time constant is " + std::to_string (expected) + ")");
    check (std::abs (at96k - at48k) < 0.01f,
           "the same 450 ms at 96 kHz lands in the same place (" + std::to_string (at96k)
           + " dB)");
}

// =============================================================================
//  Silence holds the state; it does not become a measurement.
// =============================================================================
void testSilenceHoldsTheCorrection()
{
    j37dsp::AutoGainCompensator compensator;
    const auto coefficient = compensationCoefficient (48000.0);

    StereoProgramme programme;

    for (int i = 0; i < 144000; ++i)   // settle a -6 dB correction
    {
        const auto frame = programme.next();
        compensator.process (framePower (frame), framePower (frame) * 0.25f, coefficient);
    }

    const auto held = compensator.getSmoothedDb();
    float worstDuringSilence = 0.0f;

    for (int i = 0; i < 48000; ++i)   // 1 s of silence
        worstDuringSilence = std::max (worstDuringSilence,
                                       std::abs (compensator.process (0.125f, 0.0f, coefficient) - 1.0f));

    check (worstDuringSilence == 0.0f,
           "a silent frame returns exactly unity for as long as it lasts");
    check (compensator.getSmoothedDb() == held,
           "silence does not move the held correction (" + std::to_string (held)
           + " dB before and after)");

    const auto resumed = compensator.process (0.125f, 0.03125f, coefficient);

    check (resumed >= static_cast<float> (std::pow (10.0, held / 20.0)) - 1.0e-6f,
           "the programme resumes from the held correction rather than from unity ("
           + std::to_string (decibels (resumed)) + " dB)");
    check (resumed < 2.0f, "and resumes toward the loss, not past it");
}

// =============================================================================
//  A short dip is not a level error: the pole is 450 ms on purpose.
// =============================================================================
void testDoesNotChaseAShortDip()
{
    j37dsp::AutoGainCompensator compensator;
    const auto coefficient = compensationCoefficient (48000.0);

    StereoProgramme programme;

    for (int i = 0; i < 48000; ++i)   // 1 s of a healthy programme
        (void) compensator.process (0.125f, 0.125f, coefficient);

    for (int i = 0; i < 480; ++i)     // a 10 ms dip of 6 dB
        (void) compensator.process (0.125f, 0.03125f, coefficient);

    const auto afterDip = compensator.getSmoothedDb();

    check (afterDip < 0.35f,
           "a 10 ms dip moves the correction by " + std::to_string (afterDip)
           + " dB rather than the 3.8 dB a 450 ms window would take");

    // And it comes back down when the dip ends, rather than staying engaged.
    for (int i = 0; i < 96000; ++i)   // 2 s
        (void) compensator.process (0.125f, 0.125f, coefficient);

    check (compensator.getSmoothedDb() < 0.005f,
           "and the correction releases when the programme is healthy again ("
           + std::to_string (compensator.getSmoothedDb()) + " dB)");
}

} // namespace

int main()
{
    std::printf ("auto gain compensator\n");

    testIdleIsTransparent();
    testRestoresALossOnSignal();
    testNeverBoosts();
    testRestorationCeiling();
    testTimeConstantAndRate();
    testSilenceHoldsTheCorrection();
    testDoesNotChaseAShortDip();

    if (gFailures != 0)
    {
        std::printf ("\n%d check(s) failed\n", gFailures);
        return 1;
    }

    std::printf ("\nall checks passed\n");
    return 0;
}
