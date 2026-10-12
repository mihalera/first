// =============================================================================
//  Regression harness for the K-weighted loudness meter.
//
//  The meter is not a display detail. The engine's AUTO GAIN reads it and folds
//  the reading back into the output level, so a meter calibrated wrong is not a
//  number on a panel - it is the machine walking its own output level somewhere
//  the user did not ask for, on every project with AUTO GAIN switched on. None
//  of that fails to build and none of it looks wrong: a meter reading 3 LU high
//  is exactly as plausible on screen as one reading right, and the only place
//  the difference shows is in a session somebody has already mixed.
//
//  So the claims the meter's own doc comment makes are measured here, on signal,
//  against the standard it names (ITU-R BS.1770: the K-weighting curve, the
//  -0.691 dB offset, the 400 ms window):
//
//    1. CALIBRATION. A 1 kHz sine whose RMS is -20 dBFS reads -20.0 LUFS. That
//       is the point BS.1770's offset exists to place, and the anchor every
//       other number in this file is relative to.
//    2. LINEARITY. Ten dB louder reads ten LU louder, at any level the meter
//       covers - so the reading is a level and not a shape.
//    3. RATE INDEPENDENCE. The same tone reads the same at 44.1, 48, 88.2, 96
//       and 192 kHz, and the WINDOW behaves the same at two of them. The first
//       is the reading the engine gets, the second is what says the 400 ms time
//       constant is built from the rate the meter was prepared for rather than
//       frozen at whichever rate it was written at.
//    4. THE CHANNELS SUM. The same signal in both channels reads 3.01 LU above
//       one channel alone, because BS.1770 sums channel mean squares. AUTO GAIN
//       depends on that sum being the sum: a stereo machine is not 3 LU louder
//       than a mono one by accident, and a meter that averaged instead of summed
//       would walk the output level by 3 LU the moment a user went from mono to
//       stereo - or the moment the plugin was instantiated as a mono instance.
//    5. A MACHINE AT REST MEASURES THE FLOOR. A fresh meter fed silence reads
//       its floor, and a meter that has been reading a tone comes back to it -
//       the 400 ms window has to be a window and not a memory. A meter that
//       keeps reporting a faded copy of the last tone is a gain compensator that
//       keeps turning the output down on a stopped machine.
//
//  The meter is cut out of Source/PluginProcessor.h by tests/dsp/extract.py and
//  #included below, so this harness measures the shipping struct rather than a
//  copy of it that can drift. Same shape as the SUBFUND harness next to it: no
//  JUCE, no host, seconds of work.
// =============================================================================

#include "shim.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "extracted_dsp.inc"

namespace
{
constexpr double kTwoPi = 6.283185307179586;
constexpr float kFloor = -70.0f;

int gFailures = 0;

void check (bool condition, const std::string& what)
{
    std::printf ("  %-4s %s\n", condition ? "PASS" : "FAIL", what.c_str());
    if (! condition)
        ++gFailures;
}

std::string format (const char* pattern, double a)
{
    std::array<char, 256> buffer {};
    std::snprintf (buffer.data(), buffer.size(), pattern, a);
    return std::string (buffer.data());
}

std::string format2 (const char* pattern, double a, double b)
{
    std::array<char, 256> buffer {};
    std::snprintf (buffer.data(), buffer.size(), pattern, a, b);
    return std::string (buffer.data());
}

/** A meter and the coefficient its caller builds for it, driven frame by frame
    exactly the way the engine drives it: the window coefficient is a function of
    the rate alone (LoudnessMeter::makeWindowCoefficient) and is built once. */
struct MeterRun
{
    explicit MeterRun (double sampleRate)
        : rate (sampleRate),
          window (LoudnessMeter::makeWindowCoefficient (static_cast<float> (sampleRate)))
    {
        meter.prepare (sampleRate);
    }

    /**
        Feeds `seconds` of a sine at `frequency` whose per-channel RMS is
        `rmsDb` dBFS, and returns the last reading.

        `stereo` puts the same signal in both channels. The default is ONE
        channel, because that is the case BS.1770 calibrates: a mono signal at
        -20 dBFS RMS reads -20 LKFS, and the same signal duplicated to both
        channels measures 3 LU above it (claim 4).
    */
    float feedTone (double frequency, double rmsDb, double seconds, bool stereo = false)
    {
        const auto amplitude = std::pow (10.0, rmsDb / 20.0) * std::sqrt (2.0);

        auto reading = kFloor;
        for (std::size_t i = 0, count = frames (seconds); i < count; ++i)
        {
            const auto x = static_cast<float> (amplitude
                                               * std::sin (kTwoPi * frequency
                                                           * static_cast<double> (i) / rate));
            reading = meter.processFrame (x, stereo ? x : 0.0f, window);
        }

        return reading;
    }

    /** Feeds `seconds` of digital silence and returns the last reading. */
    float feedSilence (double seconds)
    {
        auto reading = kFloor;
        for (std::size_t i = 0, count = frames (seconds); i < count; ++i)
            reading = meter.processFrame (0.0f, 0.0f, window);

        return reading;
    }

    std::size_t frames (double seconds) const
    {
        return static_cast<std::size_t> (seconds * rate);
    }

    double rate;
    float window;
    LoudnessMeter meter;
};

/** The reference tone: 1 kHz, per-channel RMS `rmsDb` dBFS, 2 s - about five of
    the 400 ms window's time constants, so the reading has settled to within a
    fraction of a percent of its steady value before it is taken. */
constexpr double kReferenceFrequency = 1000.0;
constexpr double kSettlingSeconds = 2.0;

float settledReading (double rate, double rmsDb, bool stereo = false)
{
    MeterRun run (rate);
    return run.feedTone (kReferenceFrequency, rmsDb, kSettlingSeconds, stereo);
}

// -----------------------------------------------------------------------------
//  1. Calibration
// -----------------------------------------------------------------------------
void testCalibration()
{
    std::printf ("\n1. A 1 kHz sine at -20 dBFS RMS reads -20.0 LUFS\n");

    const auto reading = settledReading (48000.0, -20.0);
    std::printf ("       measured %.3f LUFS (want -20.0, the point BS.1770's offset exists to place)\n",
                 static_cast<double> (reading));

    check (std::abs (static_cast<double> (reading) + 20.0) <= 0.6,
           format ("the meter reads %.3f LUFS for a -20 dBFS RMS 1 kHz sine, within 0.6 of -20.0",
                   static_cast<double> (reading)));
}

// -----------------------------------------------------------------------------
//  2. Linearity
// -----------------------------------------------------------------------------
void testLinearity()
{
    std::printf ("\n2. The reading is a level: ten dB louder reads ten LU louder\n");

    const auto quiet = settledReading (48000.0, -40.0);
    const auto loud = settledReading (48000.0, -20.0);
    const auto difference = static_cast<double> (loud) - static_cast<double> (quiet);

    std::printf ("       -40 dBFS -> %.3f LUFS, -20 dBFS -> %.3f LUFS, difference %.3f LU\n",
                 static_cast<double> (quiet), static_cast<double> (loud), difference);

    check (std::abs (difference - 20.0) <= 0.3,
           format ("ten dB of level moves the reading %.3f LU, within 0.3 of ten", difference));

    // A third point, so a readout that is merely steep rather than linear is
    // not mistaken for a calibrated one: -6 dB, three quarters of the way up
    // this meter's travel.
    const auto mid = settledReading (48000.0, -6.0);
    const auto midDifference = static_cast<double> (mid) - static_cast<double> (loud);
    std::printf ("       -6 dBFS -> %.3f LUFS, %.3f LU above the -20 dBFS reading\n",
                 static_cast<double> (mid), midDifference);

    check (std::abs (midDifference - 14.0) <= 0.3,
           format ("fourteen dB of level moves the reading %.3f LU, within 0.3 of fourteen",
                   midDifference));
}

// -----------------------------------------------------------------------------
//  3. Rate independence
// -----------------------------------------------------------------------------
void testRateIndependence()
{
    std::printf ("\n3. The reading and the window belong to the rate the meter was prepared for\n");

    const std::array<double, 5> rates { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
    const auto reference = settledReading (48000.0, -20.0);

    for (const auto rate : rates)
    {
        const auto reading = settledReading (rate, -20.0);
        check (std::abs (static_cast<double> (reading) - static_cast<double> (reference)) <= 0.4,
               format2 ("at %.0f Hz the reference tone reads %.3f LUFS, within 0.4 of the 48 kHz reading",
                        rate, static_cast<double> (reading)));
    }

    // The window itself: how far the reading falls in the first 200 ms of
    // silence after that tone. A 400 ms one-pole falls 2.17 LU in 200 ms.
    // Pinning it at two rates is what catches a time constant that ignores the
    // rate - a coefficient built for 48 kHz and reused at 96 kHz would fall
    // 4.34 LU here instead, which is a window of 200 ms rather than 400.
    const auto dropAt = [] (double rate)
    {
        MeterRun run (rate);
        const auto settled = run.feedTone (kReferenceFrequency, -20.0, kSettlingSeconds);
        return static_cast<double> (settled)
             - static_cast<double> (run.feedSilence (0.2));
    };

    const auto dropAt48 = dropAt (48000.0);
    const auto dropAt96 = dropAt (96000.0);
    std::printf ("       the first 200 ms of silence cost %.3f LU at 48 kHz and %.3f LU at 96 kHz\n",
                 dropAt48, dropAt96);

    check (std::abs (dropAt48 - 2.17) <= 1.0,
           format ("the 400 ms window falls %.3f LU in 200 ms, within 1.0 of 2.17", dropAt48));
    check (std::abs (dropAt48 - dropAt96) <= 0.3,
           format ("the same window falls within 0.3 LU at 48 kHz and 96 kHz (%.3f apart)",
                   std::abs (dropAt48 - dropAt96)));
}

// -----------------------------------------------------------------------------
//  4. The channels sum
// -----------------------------------------------------------------------------
void testStereoSum()
{
    std::printf ("\n4. The same signal in both channels reads 3.01 LU above one channel\n");

    const auto mono = settledReading (48000.0, -20.0);
    const auto stereo = settledReading (48000.0, -20.0, true);
    const auto difference = static_cast<double> (stereo) - static_cast<double> (mono);

    std::printf ("       one channel %.3f LUFS, both channels %.3f LUFS, %.3f LU apart\n",
                 static_cast<double> (mono), static_cast<double> (stereo), difference);

    check (std::abs (difference - 3.01) <= 0.15,
           format ("doubling the channels moves the reading %.3f LU, within 0.15 of 3.01",
                   difference));
}

// -----------------------------------------------------------------------------
//  5. A machine at rest measures the floor
// -----------------------------------------------------------------------------
void testReturnToTheFloor()
{
    std::printf ("\n5. Silence reads the floor, and the window comes back to it\n");

    MeterRun fresh (48000.0);
    const auto firstFrameOfSilence = fresh.feedSilence (1.0);
    check (firstFrameOfSilence <= kFloor + 0.01f,
           format ("a meter fed silence from the start reads %.3f LUFS, the floor", 
                   static_cast<double> (firstFrameOfSilence)));

    MeterRun run (48000.0);
    const auto settled = run.feedTone (kReferenceFrequency, -20.0, kSettlingSeconds);
    const auto afterTwoSeconds = run.feedSilence (2.0);
    const auto afterTwelveSeconds = run.feedSilence (10.0);

    std::printf ("       after the tone %.3f LUFS, 2 s of silence -> %.3f, 12 s -> %.3f\n",
                 static_cast<double> (settled), static_cast<double> (afterTwoSeconds),
                 static_cast<double> (afterTwelveSeconds));

    check (static_cast<double> (settled) - static_cast<double> (afterTwoSeconds) >= 10.0,
           format ("two seconds of silence drop the reading %.3f LU, at least ten",
                   static_cast<double> (settled) - static_cast<double> (afterTwoSeconds)));

    check (afterTwelveSeconds <= kFloor + 0.01f,
           format ("twelve seconds of silence reach the floor: %.3f LUFS",
                   static_cast<double> (afterTwelveSeconds)));
}
} // namespace

int main()
{
    std::printf ("LOUDNESS K-weighted meter regression harness\n");

    testCalibration();
    testLinearity();
    testRateIndependence();
    testStereoSum();
    testReturnToTheFloor();

    std::printf ("\n%s (%d failure%s)\n", gFailures == 0 ? "ALL CHECKS PASSED" : "FAILURES PRESENT",
                 gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
