// =============================================================================
//  Unit tests for the conventional compressor on the finished signal -
//  Source/DSP/StandardCompressor.h.
//
//  Every claim here is one a user can hear and would report as a bug: that a
//  signal under the threshold passes at unity, that above it the stage reduces by
//  the ratio's slope and no more, that the knee blends between those two instead
//  of stepping, that MAKEUP adds exactly the decibels on the label, that RELEASE
//  brings the gain back rather than parking it, and that the extremes of the
//  parameter range produce finite numbers rather than a NaN that the rest of the
//  chain would carry to the output.
//
//  The measurements are RMS of a sine, because that is what the stage's own
//  detector measures: the kill argument is its `envelopeDb`, so the expected
//  reduction is arithmetic on the input's RMS level rather than a guess about a
//  waveform. Each render is long enough for a 10 ms attack to settle and is
//  measured over the tail, where the detector has stopped moving.
// =============================================================================
#include "shim.h"

#include "DSP/StandardCompressor.h"

#include <catch_amalgamated.hpp>

#include <cmath>
#include <vector>

namespace
{
constexpr float kRate = 48000.0f;
constexpr float kToneHz = 440.0f;

struct Parameters
{
    float thresholdDb = -20.0f;
    float ratio = 4.0f;
    float attackMs = 10.0f;
    float releaseMs = 100.0f;
    float kneeDb = 0.1f;
    float makeupDb = 0.0f;
};

/** The stage-level of a sine of amplitude `amplitude`, which is what the
    detector's envelope converges to and therefore the level its gain computer
    sees. */
float sineLevelDb (float amplitude)
{
    return 20.0f * std::log10 (amplitude / std::sqrt (2.0f));
}

/** The amplitude of a sine at a given level, which is the input every case below
    is driven with: the stage's own detector measures mean square, so stating the
    input as a level rather than as an amplitude is stating it in the unit the
    kill argument uses. */
float amplitudeForLevelDb (float levelDb)
{
    return std::sqrt (2.0f) * juce::Decibels::decibelsToGain (levelDb);
}

/** Renders a tone of the given amplitude for `seconds` through one compressor
    and hands back the channel, so a caller can measure any window of it. */
std::vector<float> render (float amplitude, float seconds, const Parameters& p,
                           float rate = kRate)
{
    StandardCompressor compressor;
    const auto samples = static_cast<int> (seconds * rate);
    std::vector<float> out;
    out.reserve (static_cast<std::size_t> (samples));

    for (int i = 0; i < samples; ++i)
    {
        const auto x = amplitude * std::sin (juce::MathConstants<float>::twoPi * kToneHz
                                             * static_cast<float> (i) / rate);
        out.push_back (compressor.process (x, rate, p.thresholdDb, p.ratio,
                                          p.attackMs, p.releaseMs, p.kneeDb, p.makeupDb));
    }

    return out;
}

float rmsIn (const std::vector<float>& out, std::size_t from, std::size_t count)
{
    const auto last = juce::jmin (out.size(), from + count);
    float sum = 0.0f;

    for (std::size_t i = from; i < last; ++i)
        sum += out[i] * out[i];

    const auto length = static_cast<float> (juce::jmax<std::size_t> (1, last - from));
    return std::sqrt (sum / length);
}

/** The level a render settles at, measured over the last half second of it. */
float settledLevelDb (const std::vector<float>& out)
{
    const auto window = static_cast<std::size_t> (0.5f * kRate);
    REQUIRE (out.size() > window);
    return 20.0f * std::log10 (rmsIn (out, out.size() - window, window));
}
} // namespace

TEST_CASE ("StandardCompressor: a signal under the threshold passes at unity")
{
    Parameters p;
    p.thresholdDb = -12.0f;
    p.kneeDb = 6.0f; // the knee reaches down to -15 dBFS, still above the input

    const auto inputLevel = sineLevelDb (0.05f); // about -29 dBFS
    REQUIRE (inputLevel < p.thresholdDb - p.kneeDb * 0.5f);

    const auto out = render (0.05f, 1.0f, p);
    CHECK (settledLevelDb (out) == Catch::Approx (inputLevel).margin (0.1));
}

TEST_CASE ("StandardCompressor: above the knee the reduction is the ratio's slope")
{
    // The defining property of a ratio, and the number a mixing engineer checks
    // against a meter: 10 dB more in must be 10/ratio dB more out.
    //
    // Measured as a DIFFERENCE rather than as an absolute level on purpose. The
    // detector is a squaring follower whose attack is ten times faster than its
    // release, so on a sine it settles about 1.6 dB ABOVE the true mean square -
    // which the case below this one measures. That bias is a property of the
    // waveform and the two time constants, not of the level, so it cancels
    // exactly in a difference and the slope comes out clean.
    Parameters p;
    p.thresholdDb = -20.0f;
    p.kneeDb = 0.1f;
    p.ratio = 4.0f;

    // Both levels are above the knee's top at -19.95 dBFS, so both sit on the
    // slope rather than in the knee.
    REQUIRE (sineLevelDb (amplitudeForLevelDb (-19.0f)) > p.thresholdDb + p.kneeDb * 0.5f);

    const auto loud = settledLevelDb (render (amplitudeForLevelDb (-9.0f), 1.0f, p));
    const auto quieter = settledLevelDb (render (amplitudeForLevelDb (-19.0f), 1.0f, p));

    CHECK (loud - quieter == Catch::Approx (10.0f / p.ratio).margin (0.3));
}

TEST_CASE ("StandardCompressor: a sine at the threshold is already just into the slope")
{
    // The stage reduces slightly BELOW the nominal threshold on a sine, because
    // the detector's 10 ms attack against its 100 ms release settles above the
    // true mean square. Measured here rather than argued away: it is the
    // documented shape of a feed-forward detector with asymmetric ballistics (a
    // peak-ish reading of a sine is higher than its RMS), and a user comparing
    // the panel against an RMS meter is seeing exactly this - so the behaviour
    // belongs pinned, at a bounded size, in the suite that pins the rest.
    Parameters p;
    p.thresholdDb = -20.0f;
    p.kneeDb = 0.01f;
    p.ratio = 4.0f;

    const auto reductionDb = p.thresholdDb
                           - settledLevelDb (render (amplitudeForLevelDb (p.thresholdDb), 1.0f, p));

    CHECK (reductionDb > 0.0f);
    CHECK (reductionDb < 3.0f);
}

TEST_CASE ("StandardCompressor: a higher ratio reduces further")
{
    Parameters p;
    p.thresholdDb = -20.0f;
    p.kneeDb = 0.1f;

    p.ratio = 2.0f;
    const auto gentle = settledLevelDb (render (0.5f, 1.0f, p));

    p.ratio = 8.0f;
    const auto firm = settledLevelDb (render (0.5f, 1.0f, p));

    CHECK (firm < gentle);
}

TEST_CASE ("StandardCompressor: the knee blends instead of stepping")
{
    // The whole of the soft knee is what happens between the two ends of its
    // travel, so it is measured as three 4 dB steps up the curve rather than as
    // one level in the middle of it. A knee built as a step would give 4 dB out
    // below and 1 dB out above with nothing in between, and there would be no
    // step on the curve whose answer is between those two numbers.
    Parameters p;
    p.thresholdDb = -20.0f;
    p.kneeDb = 12.0f; // spans -26 dBFS to -14 dBFS
    p.ratio = 4.0f;

    const auto out = [&p] (float levelDb)
    {
        return settledLevelDb (render (amplitudeForLevelDb (levelDb), 1.0f, p));
    };
    const auto step = [&out] (float from, float to) { return out (to) - out (from); };

    // Well below the knee: 1:1, so 4 dB in is 4 dB out.
    CHECK (step (-34.0f, -30.0f) == Catch::Approx (4.0f).margin (0.4));

    // Well above it: the full slope, so 4 dB in is 1 dB out.
    CHECK (step (-10.0f, -6.0f) == Catch::Approx (4.0f / p.ratio).margin (0.3));

    // Across it: strictly between the two, which is the blend itself.
    const auto across = step (-22.0f, -18.0f);
    CHECK (across > 4.0f / p.ratio);
    CHECK (across < 4.0f);

    // And the curve never turns back: a response that fell as the input rose
    // would be a gate rather than a compressor.
    CHECK (out (-34.0f) < out (-22.0f));
    CHECK (out (-22.0f) < out (-18.0f));
    CHECK (out (-18.0f) < out (-10.0f));
}

TEST_CASE ("StandardCompressor: MAKEUP adds exactly the decibels on the label")
{
    Parameters p;
    p.thresholdDb = -20.0f;
    p.kneeDb = 0.1f;

    const auto without = settledLevelDb (render (0.5f, 1.0f, p));

    p.makeupDb = 6.0f;
    const auto with = settledLevelDb (render (0.5f, 1.0f, p));

    CHECK (with - without == Catch::Approx (6.0f).margin (0.1));
}

TEST_CASE ("StandardCompressor: a ratio below one behaves as ratio one rather than as a gate")
{
    // The guard is `juce::jmax (1.0f, ratio)`, and it matters: at ratio 0 the
    // unguarded curve is (1 - 1/0), i.e. infinite gain reduction on a divider.
    // The documented behaviour for anything under 1:1 is no compression at all.
    Parameters p;
    p.thresholdDb = -30.0f;
    p.ratio = 0.1f;
    p.kneeDb = 0.0f;

    const auto inputLevel = sineLevelDb (0.5f);
    const auto out = render (0.5f, 1.0f, p);

    CHECK (settledLevelDb (out) == Catch::Approx (inputLevel).margin (0.3));
}

TEST_CASE ("StandardCompressor: RELEASE recovers the gain instead of parking it")
{
    // A burst well over the threshold, then a quiet passage well under it. The
    // gain must climb monotonically back towards unity, not stay where the burst
    // left it - that is the whole difference between a compressor and a squasher.
    Parameters p;
    p.thresholdDb = -30.0f;
    p.ratio = 8.0f;
    p.attackMs = 2.0f;
    p.releaseMs = 300.0f;
    p.kneeDb = 0.0f;

    constexpr float quiet = 0.03f;
    const auto quietLevel = sineLevelDb (quiet);

    StandardCompressor compressor;
    std::vector<float> out;

    const auto push = [&] (float amplitude, int samples)
    {
        for (int i = 0; i < samples; ++i)
        {
            const auto x = amplitude * std::sin (juce::MathConstants<float>::twoPi * kToneHz
                                                 * static_cast<float> (out.size()) / kRate);
            out.push_back (compressor.process (x, kRate, p.thresholdDb, p.ratio,
                                               p.attackMs, p.releaseMs, p.kneeDb, p.makeupDb));
        }
    };

    push (0.8f, static_cast<int> (0.3f * kRate));  // the burst
    const auto burstEnd = out.size();
    push (quiet, static_cast<int> (3.2f * kRate)); // the quiet passage

    // Expressed as the gain the stage applied, so the three windows are directly
    // comparable: each is the output RMS minus what the input's own level was.
    const auto appliedDb = [&] (std::size_t from, std::size_t count)
    {
        return 20.0f * std::log10 (rmsIn (out, from, count)) - quietLevel;
    };

    const auto atBurst = 20.0f * std::log10 (rmsIn (out, burstEnd - static_cast<std::size_t> (0.05f * kRate),
                                                    static_cast<std::size_t> (0.05f * kRate)))
                       - sineLevelDb (0.8f);
    const auto justAfter = appliedDb (burstEnd, static_cast<std::size_t> (0.02f * kRate));
    const auto later = appliedDb (burstEnd + static_cast<std::size_t> (1.0f * kRate),
                                  static_cast<std::size_t> (0.02f * kRate));
    const auto settled = appliedDb (out.size() - static_cast<std::size_t> (0.2f * kRate),
                                    static_cast<std::size_t> (0.2f * kRate));

    CHECK (atBurst < -6.0f);           // the burst is genuinely being reduced
    CHECK (justAfter > atBurst);       // and the gain starts back up the moment it ends
    CHECK (later > justAfter);
    CHECK (settled > later);
    CHECK (settled == Catch::Approx (0.0f).margin (0.2)); // fully recovered, not parked
}

TEST_CASE ("StandardCompressor: silence stays silent and the parameter extremes stay finite")
{
    StandardCompressor compressor;

    float out = 1.0f;
    for (int i = 0; i < 4800; ++i)
        out = compressor.process (0.0f, kRate, -20.0f, 4.0f, 10.0f, 100.0f, 6.0f, 0.0f);

    CHECK (out == 0.0f);

    // The extremes, including a sample rate of zero (the coefficient builder
    // floors it) and a gain reduction of -inf if the ratio guard ever went away.
    for (const auto rate : { 0.0f, 1.0f, kRate, 192000.0f })
    {
        StandardCompressor extreme;

        for (int i = 0; i < 512; ++i)
        {
            const auto x = (i % 2 == 0 ? 1.0f : -1.0f);
            const auto y = extreme.process (x, rate, -1000.0f, 0.0f, 0.0f, 0.0f, 0.0f, 100.0f);
            REQUIRE (std::isfinite (y));
        }
    }
}

TEST_CASE ("StandardCompressor: instances keep their own detector state")
{
    // "Deliberately separate from GlueCompressor, including its detector state" -
    // and, in the plugin, one per channel. A fresh instance fed a quiet signal
    // must not show the reduction a driven one accumulated.
    Parameters p;
    p.thresholdDb = -30.0f;
    p.ratio = 8.0f;
    p.kneeDb = 0.0f;

    StandardCompressor driven;
    for (int i = 0; i < static_cast<int> (kRate); ++i)
    {
        const auto x = 0.8f * std::sin (juce::MathConstants<float>::twoPi * kToneHz
                                        * static_cast<float> (i) / kRate);
        driven.process (x, kRate, p.thresholdDb, p.ratio, p.attackMs, p.releaseMs, p.kneeDb, p.makeupDb);
    }

    const auto quietLevel = sineLevelDb (0.03f);
    StandardCompressor fresh;
    float sum = 0.0f;
    const auto samples = static_cast<int> (0.3f * kRate);

    for (int i = 0; i < samples; ++i)
    {
        const auto x = 0.03f * std::sin (juce::MathConstants<float>::twoPi * kToneHz
                                         * static_cast<float> (i) / kRate);
        const auto y = fresh.process (x, kRate, p.thresholdDb, p.ratio, p.attackMs, p.releaseMs, p.kneeDb, p.makeupDb);
        sum += y * y;
    }

    const auto level = 20.0f * std::log10 (std::sqrt (sum / static_cast<float> (samples)));
    CHECK (level > quietLevel - 7.0f); // no more than the detector's own attack ramp
}
