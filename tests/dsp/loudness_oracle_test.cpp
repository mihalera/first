// =============================================================================
//  The loudness meter against libebur128: an independent measurement of the same
//  audio, on every run, instead of a note about a measurement somebody made once.
//
//  WHY THIS EXISTS. tests/dsp/loudness_test.cpp checks the meter against the
//  standard and against EBU Tech 3341's own three signals - but the one check that
//  says the CURVE is BS.1770's rather than a self-consistent approximation of it
//  was made outside this repository: the meter was run beside libebur128 on the
//  same frames and the two agreed within 0.06 LU. That sentence has been in the
//  harness header and in docs/library-candidates.md ever since, and nothing in CI
//  can reproduce it. This is that comparison, in the tree, as a gate.
//
//  WHAT IT COMPARES. Two implementations of the same specification, fed the SAME
//  buffer: the shipping LoudnessMeter (one-pole 400 ms window, the number the
//  panel shows) and libebur128's momentary loudness (a rectangular 400 ms block,
//  the reference reading). Both read LUFS, so the difference is the claim.
//
//  WHERE THE TWO ARE EXPECTED TO DIFFER, and why the tolerance is a band rather
//  than an equality: the windows are different SHAPES, not different lengths. On a
//  steady signal they converge to the same value - which is what is measured here.
//  On a signal that starts or stops they do not, by design: a one-pole decays
//  instead of ending, so a tone's onset reads low for a few hundred milliseconds
//  and its release reads high. Measuring the settled value is the honest
//  comparison, so every case feeds three seconds and reads the last frame.
//
//  The gate: every steady signal agrees within 0.25 LU, which is tight enough to
//  catch a wrong curve (the shared-filter-state defect this meter shipped with read
//  2.4 LU high on a mono signal, and the zeroed-coefficient defect read 0.69 LU
//  low) and loose enough for libebur128's own coefficient fitting, which sits about
//  0.04 LU above the exact digital response where this meter sits 0.005 above it.
//
//  libebur128 is MIT, vendored under ThirdParty/libebur128 at v1.2.6, and it is a
//  DEV dependency: it is compiled into this harness and into nothing the plugin
//  ships. tests/dsp/run.sh skips this binary when the tree is absent (a pruned
//  checkout) or when the C library is not available on the platform, and says so.
// =============================================================================

#include "shim.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "DSP/LoudnessMeter.h"

#include "ebur128.h"

namespace
{
constexpr double kTwoPi = 6.283185307179586;

/** The band the two implementations are allowed to disagree in. See the banner. */
constexpr double kToleranceLu = 0.25;

constexpr double kSeconds = 3.0;

int gFailures = 0;
double gWorstDeviation = 0.0;

void check (bool condition, const std::string& what)
{
    std::printf ("  %-4s %s\n", condition ? "PASS" : "FAIL", what.c_str());

    if (! condition)
        ++gFailures;
}

struct StereoBuffer
{
    std::vector<float> interleaved; ///< L, R, L, R ...
    std::size_t frames = 0;
};

/** A steady tone whose per-channel RMS is `rmsDb` dBFS, in both channels. */
StereoBuffer makeTone (double frequency, double rmsDb, double rate)
{
    StereoBuffer buffer;
    buffer.frames = static_cast<std::size_t> (rate * kSeconds);
    buffer.interleaved.resize (buffer.frames * 2);

    // A sine's RMS is its amplitude / sqrt(2), so an amplitude of
    // sqrt(2) * 10^(dB/20) gives that RMS. Both channels carry it, which is the
    // case BS.1770 sums.
    const auto amplitude = std::pow (10.0, rmsDb / 20.0) * std::sqrt (2.0);

    for (std::size_t i = 0; i < buffer.frames; ++i)
    {
        const auto x = static_cast<float> (amplitude
                                           * std::sin (kTwoPi * frequency
                                                       * static_cast<double> (i) / rate));
        buffer.interleaved[2 * i] = x;
        buffer.interleaved[2 * i + 1] = x;
    }

    return buffer;
}

/** White noise at a per-channel RMS of `rmsDb` dBFS, from a deterministic LCG -
    the same samples on every machine and every run, so a failure is repeatable. */
StereoBuffer makeNoise (double rmsDb, double rate, unsigned int seed)
{
    StereoBuffer buffer;
    buffer.frames = static_cast<std::size_t> (rate * kSeconds);
    buffer.interleaved.resize (buffer.frames * 2);

    const auto rms = std::pow (10.0, rmsDb / 20.0);

    // A uniform generator in [-1, 1) has an RMS of 1/sqrt(3), so scaling by
    // sqrt(3) * the wanted RMS gives the level the name promises.
    const auto scale = rms * std::sqrt (3.0);
    auto state = seed;

    for (std::size_t i = 0; i < buffer.frames * 2; ++i)
    {
        state = state * 1664525u + 1013904223u;
        const auto uniform = static_cast<double> (state) / 2147483648.0 - 1.0;
        buffer.interleaved[i] = static_cast<float> (uniform * scale);
    }

    return buffer;
}

/** The shipping meter's settled reading for a buffer, driven the way the engine
    drives it: prepare() then reset(), and the window coefficient built once from
    the rate. */
float meterReading (const StereoBuffer& buffer, double rate)
{
    LoudnessMeter meter;
    meter.prepare (rate);
    meter.reset();

    const auto window = LoudnessMeter::makeWindowCoefficient (static_cast<float> (rate));
    auto reading = -70.0f;

    for (std::size_t i = 0; i < buffer.frames; ++i)
        reading = meter.processFrame (buffer.interleaved[2 * i], buffer.interleaved[2 * i + 1],
                                      window);

    return reading;
}

/** libebur128's momentary reading for the same buffer. Returns false when the
    reference refuses to initialise or to give an answer, so a broken build of the
    oracle fails loudly rather than comparing against nothing. */
bool referenceReading (const StereoBuffer& buffer, double rate, double& reading)
{
    auto* state = ebur128_init (2, static_cast<unsigned long> (rate), EBUR128_MODE_M);
    if (state == nullptr)
        return false;

    // Without this the reference treats both channels as unknown and weights them
    // as zeros; with it, both are front channels and sum exactly as BS.1770 says.
    ebur128_set_channel (state, 0, EBUR128_LEFT);
    ebur128_set_channel (state, 1, EBUR128_RIGHT);

    const auto add = ebur128_add_frames_float (state, buffer.interleaved.data(), buffer.frames);
    if (add != EBUR128_SUCCESS)
    {
        ebur128_destroy (&state);
        return false;
    }

    const auto ok = ebur128_loudness_momentary (state, &reading) == EBUR128_SUCCESS;
    ebur128_destroy (&state);

    return ok;
}

void compare (const char* name, const StereoBuffer& buffer, double rate)
{
    const auto ours = static_cast<double> (meterReading (buffer, rate));
    double reference = 0.0;

    if (! referenceReading (buffer, rate, reference))
    {
        check (false, std::string (name) + ": the reference gave no reading");
        return;
    }

    const auto deviation = std::fabs (ours - reference);
    gWorstDeviation = std::fmax (gWorstDeviation, deviation);

    char detail[256];
    std::snprintf (detail, sizeof (detail), "ours %8.3f  reference %8.3f  delta %.3f LU", ours,
                   reference, deviation);

    check (deviation <= kToleranceLu, std::string (name) + ": " + detail);
}

} // namespace

int main()
{
    int major = 0, minor = 0, patch = 0;
    ebur128_get_version (&major, &minor, &patch);
    std::printf ("loudness oracle: reference libebur128 %d.%d.%d, tolerance %.2f LU\n", major,
                 minor, patch, kToleranceLu);

    // The curve at three places on the spectrum, because the K-weighting is a
    // high-shelf above 1.5 kHz and a high-pass at 38 Hz: a meter that is right at
    // 1 kHz and wrong at either end is a meter that matches on the calibration
    // tone and lies about a mix.
    compare ("1 kHz sine, -20 dBFS, 48 kHz", makeTone (1000.0, -20.0, 48000.0), 48000.0);
    compare ("100 Hz sine, -20 dBFS, 48 kHz", makeTone (100.0, -20.0, 48000.0), 48000.0);
    compare ("50 Hz sine, -20 dBFS, 48 kHz", makeTone (50.0, -20.0, 48000.0), 48000.0);
    compare ("10 kHz sine, -20 dBFS, 48 kHz", makeTone (10000.0, -20.0, 48000.0), 48000.0);
    compare ("15 kHz sine, -20 dBFS, 48 kHz", makeTone (15000.0, -20.0, 48000.0), 48000.0);

    // The level axis: a meter that reads right at -20 and drifts at the ends is a
    // meter whose LINEARITY claim is local to the level it was calibrated at.
    compare ("1 kHz sine, -3 dBFS, 48 kHz", makeTone (1000.0, -3.0, 48000.0), 48000.0);
    compare ("1 kHz sine, -40 dBFS, 48 kHz", makeTone (1000.0, -40.0, 48000.0), 48000.0);
    compare ("1 kHz sine, -60 dBFS, 48 kHz", makeTone (1000.0, -60.0, 48000.0), 48000.0);

    // Broadband, where no single bin can excuse a coefficient that is off.
    compare ("white noise, -20 dBFS, 48 kHz", makeNoise (-20.0, 48000.0, 0x5eed), 48000.0);

    // The rate axis. The coefficients are rebuilt from the rate, and the reference
    // builds its own from the same rate, so this is where a change that quietly
    // stopped following the session's rate shows up.
    compare ("1 kHz sine, -20 dBFS, 44.1 kHz", makeTone (1000.0, -20.0, 44100.0), 44100.0);
    compare ("1 kHz sine, -20 dBFS, 96 kHz", makeTone (1000.0, -20.0, 96000.0), 96000.0);
    compare ("1 kHz sine, -20 dBFS, 192 kHz", makeTone (1000.0, -20.0, 192000.0), 192000.0);

    std::printf ("\nworst deviation from the reference: %.3f LU (gate %.2f)\n", gWorstDeviation,
                 kToleranceLu);
    std::printf ("%s\n", gFailures == 0 ? "loudness oracle: PASSED" : "loudness oracle: FAILED");

    return gFailures == 0 ? 0 : 1;
}
