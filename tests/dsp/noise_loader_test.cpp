// =============================================================================
//  The noise print: which sample of a WAV file is which sample of the loop.
//
//  Source/DSP/NoiseDecoder.h is the whole of the feature that can be wrong
//  without anyone hearing why - the format handling, the channel folding, the
//  rate conversion and the loop wrap - and it is deliberately JUCE-free so this
//  file can hold it. Two binaries are built from it by tests/dsp/run.sh, one
//  source, the same way simd_tanh_test.cpp is built twice:
//
//    noise_library   ThirdParty/dr_libs on the include path, J37_HAS_DRWAV 1
//    noise_absent    no dr_libs at all,                   J37_HAS_DRWAV 0
//
//  The second is not ceremony. A build that pruned ThirdParty/ still has to
//  compile and still has to say WHY nothing loaded - and the only way to know
//  that branch says the truth is to compile it and read what it says. Each
//  binary asserts its own shape (the -DJ37_EXPECT_DRWAV it was handed) and then
//  what that shape must do.
//
//  What the library side asserts, in the order the failures would hurt:
//
//    * 16-bit mono, 24-bit stereo and 32-bit float decode to the values that
//      were written, half an LSB apart at most;
//    * a mono file is the SAME samples on both sides rather than two invented
//      ones, and a float file outside [-1, 1) is left where it is - a print is a
//      recording and its level is the surface's;
//    * a 44.1 kHz print is converted to the engine's rate with the loop's own
//      seam wrapped, so a repeat does not click;
//    * an empty buffer, a truncated header, a NaN sample and a file over the
//      one-minute cap are each REFUSED with a reason, rather than half-loaded;
//    * and the stage itself: with no print loaded the audio is the synthesised
//      surface it always was, with one loaded the synthesised hiss is replaced
//      rather than doubled, and the print's contribution is exactly the sample
//      times the gain, sample for sample, over more than one pass of the loop.
//
//  The JUCE side - the file, the worker thread and the atomic hand-off - is
//  Source/DSP/NoiseLoader.h, and it is covered by
//  tests/dsp/check_headers_juce.sh, the same way IrStage.h is: the part that
//  needs a plugin to exist cannot be measured by a harness that has none.
// =============================================================================
#include "shim.h"

#include "DSP/NoiseDecoder.h"
#include "DSP/VinylStage.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#if ! defined (J37_EXPECT_DRWAV)
 #define J37_EXPECT_DRWAV J37_HAS_DRWAV
#endif

namespace
{

int gFailures = 0;

void check (bool condition, const std::string& what)
{
    if (condition)
    {
        std::printf ("  ok   %s\n", what.c_str());
        return;
    }

    ++gFailures;
    std::printf ("  FAIL %s\n", what.c_str());
}

//==============================================================================
//  A WAV file, written byte by byte.
//
//  Written here rather than checked in as a binary fixture for the reason the
//  repository checks in no binaries at all: a fixture is a file nobody can read
//  in a diff, and a test that WRITES the bytes it then decodes is the only one
//  that can say what the expected sample values are without a second tool.
//==============================================================================
struct WaveWriter
{
    std::vector<std::uint8_t> bytes;

    void u16 (std::uint32_t v)
    {
        bytes.push_back (static_cast<std::uint8_t> (v & 0xffu));
        bytes.push_back (static_cast<std::uint8_t> ((v >> 8) & 0xffu));
    }

    void u32 (std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            bytes.push_back (static_cast<std::uint8_t> ((v >> (8 * i)) & 0xffu));
    }

    void tag (const char* four)
    {
        for (int i = 0; i < 4; ++i)
            bytes.push_back (static_cast<std::uint8_t> (four[i]));
    }

    /** `interleaved` holds one value per channel per frame, in [-1, 1] for the
        integer formats. `bits` is 16, 24 or 32; `floating` selects IEEE float
        (format 3) instead of PCM (format 1). */
    static std::vector<std::uint8_t> make (int channels, int rate, int bits, bool floating,
                                           const std::vector<double>& interleaved)
    {
        WaveWriter w;
        const auto frames = interleaved.size() / static_cast<std::size_t> (channels);
        const auto bytesPerSample = bits / 8;
        const auto dataBytes = static_cast<std::uint32_t> (frames
                                 * static_cast<std::size_t> (channels * bytesPerSample));

        w.tag ("RIFF");
        w.u32 (36u + dataBytes);
        w.tag ("WAVE");

        w.tag ("fmt ");
        w.u32 (16);
        w.u16 (floating ? 3u : 1u);
        w.u16 (static_cast<std::uint32_t> (channels));
        w.u32 (static_cast<std::uint32_t> (rate));
        w.u32 (static_cast<std::uint32_t> (rate * channels * bytesPerSample));
        w.u16 (static_cast<std::uint32_t> (channels * bytesPerSample));
        w.u16 (static_cast<std::uint32_t> (bits));

        w.tag ("data");
        w.u32 (dataBytes);

        for (const auto value : interleaved)
        {
            if (floating)
            {
                const auto f = static_cast<float> (value);
                std::uint32_t raw = 0;
                std::memcpy (&raw, &f, sizeof (raw));
                w.u32 (raw);
                continue;
            }

            if (bits == 16)
            {
                const auto scaled = static_cast<std::int32_t> (std::lround (value * 32768.0));
                w.u16 (static_cast<std::uint32_t> (scaled & 0xffff));
            }
            else if (bits == 24)
            {
                const auto scaled = static_cast<std::int32_t> (std::lround (value * 8388608.0));
                const auto raw = static_cast<std::uint32_t> (scaled) & 0x00ffffffu;
                w.bytes.push_back (static_cast<std::uint8_t> (raw & 0xffu));
                w.bytes.push_back (static_cast<std::uint8_t> ((raw >> 8) & 0xffu));
                w.bytes.push_back (static_cast<std::uint8_t> ((raw >> 16) & 0xffu));
            }
        }

        return std::move (w.bytes);
    }
};

std::vector<double> ramp (std::size_t frames, int channels, double step)
{
    std::vector<double> values;
    values.reserve (frames * static_cast<std::size_t> (channels));

    for (std::size_t i = 0; i < frames; ++i)
        for (int c = 0; c < channels; ++c)
            values.push_back (-0.5 + step * static_cast<double> (i) + 0.25 * static_cast<double> (c));

    return values;
}

//==============================================================================
void testDecodeShapes()
{
    std::printf ("formats\n");

    // -- 16-bit mono ------------------------------------------------------------
    {
        const auto values = ramp (128, 1, 0.003);
        const auto file = WaveWriter::make (1, 48000, 16, false, values);

        j37dsp::NoisePrint print;
        const auto outcome = j37dsp::decodeWavMemory (file.data(), file.size(), print);

        check (outcome.status == j37dsp::NoiseDecodeStatus::ok,
               "a 16-bit mono WAV decodes");
        check (print.length == 128 && print.sampleRate == 48000.0,
               "the loop holds every frame at the file's rate");
        check (print.sourceFrames == 128 && print.sourceRate == 48000.0,
               "the file's own frame count and rate are reported");

        auto worst = 0.0f;
        for (std::size_t i = 0; i < print.length; ++i)
        {
            const auto written = static_cast<float> (std::lround (values[i] * 32768.0)) / 32768.0f;
            worst = std::max (worst, std::abs (print.left[i] - written));
            worst = std::max (worst, std::abs (print.right[i] - written));
        }

        check (worst <= 0.5f / 32768.0f,
               "every 16-bit sample is within half an LSB of what was written, and mono reads to both sides");
    }

    // -- 24-bit stereo ----------------------------------------------------------
    {
        const auto values = ramp (64, 2, 0.0015);
        const auto file = WaveWriter::make (2, 44100, 24, false, values);

        j37dsp::NoisePrint print;
        const auto outcome = j37dsp::decodeWavMemory (file.data(), file.size(), print);

        check (outcome.status == j37dsp::NoiseDecodeStatus::ok && print.length == 64,
               "a 24-bit stereo WAV decodes, 64 frames of it");

        auto worst = 0.0;
        for (std::size_t i = 0; i < print.length; ++i)
        {
            const auto writtenL = static_cast<double> (std::lround (values[2 * i] * 8388608.0)) / 8388608.0;
            const auto writtenR = static_cast<double> (std::lround (values[2 * i + 1] * 8388608.0)) / 8388608.0;
            worst = std::max (worst, std::abs (static_cast<double> (print.left[i]) - writtenL));
            worst = std::max (worst, std::abs (static_cast<double> (print.right[i]) - writtenR));
        }

        check (worst <= 1.0 / 8388608.0,
               "every 24-bit sample is within half an LSB of what was written");
        check (print.left[3] != print.right[3],
               "the two sides of a stereo file stay two sides");
    }

    // -- 32-bit float, including a value outside [-1, 1) ------------------------
    {
        const std::vector<double> values { 1.5, -1.25, 0.0, 0.75, -0.5, 0.125 };
        const auto file = WaveWriter::make (2, 96000, 32, true, values);

        j37dsp::NoisePrint print;
        const auto outcome = j37dsp::decodeWavMemory (file.data(), file.size(), print);

        check (outcome.status == j37dsp::NoiseDecodeStatus::ok,
               "a 32-bit float WAV decodes");

        auto exact = true;
        for (std::size_t i = 0; i < print.length && exact; ++i)
            exact = print.left[i] == static_cast<float> (values[2 * i])
                 && print.right[i] == static_cast<float> (values[2 * i + 1]);

        check (exact, "float samples are bit-exact");
        check (print.left[0] == 1.5f,
               "a float sample outside [-1, 1) is NOT clipped - a print is a recording, not a mix");
    }
}

//==============================================================================
void testRateConversion()
{
    std::printf ("rate\n");

    const auto frames = 1000;
    const auto values = ramp (frames, 1, 0.0005);
    const auto file = WaveWriter::make (1, 44100, 32, true, values);

    j37dsp::NoisePrint atFileRate;
    j37dsp::decodeWavMemory (file.data(), file.size(), atFileRate);

    j37dsp::NoisePrint atEngineRate;
    const auto outcome = j37dsp::decodeWavMemory (file.data(), file.size(), atEngineRate, 48000.0);

    const auto expected = static_cast<std::size_t> (std::llround (frames * 48000.0 / 44100.0));

    check (outcome.status == j37dsp::NoiseDecodeStatus::ok,
           "a 44.1 kHz file decodes straight to a 48 kHz engine rate");
    check (atEngineRate.length == expected,
           "the converted loop is as long as the ratio says (" + std::to_string (expected) + " frames)");
    check (atEngineRate.sampleRate == 48000.0 && atEngineRate.sourceRate == 44100.0,
           "the converted print still says which rate the file was at");

    // The seam: the last output sample interpolates towards the FIRST sample of
    // the loop rather than towards zero. A clamp here is a click once per repeat.
    j37dsp::NoisePrint converted;
    const auto ok = j37dsp::resamplePrint (atFileRate, 48000.0, converted);
    const auto last = converted.sample (0, converted.length - 1);
    const auto first = atFileRate.sample (0, 0);
    const auto lastSource = atFileRate.left[atFileRate.length - 1];

    check (ok && last > std::min (first, lastSource) && last < std::max (first, lastSource),
           "interpolating the last frame reaches into the loop's own first sample");

    // Identity: a rate conversion that is asked for the rate it already has must
    // not touch a single sample.
    j37dsp::NoisePrint same;
    const auto identity = j37dsp::resamplePrint (atFileRate, atFileRate.sampleRate, same);

    auto identical = identity && same.length == atFileRate.length;
    for (std::size_t i = 0; i < same.length && identical; ++i)
        identical = same.left[i] == atFileRate.left[i] && same.right[i] == atFileRate.right[i];

    check (identical, "converting to the rate it is already at is bit-exact");

    check (! j37dsp::resamplePrint (j37dsp::NoisePrint {}, 48000.0, same),
           "an empty print cannot be converted, and says so");

    // Failed conversion leaves the existing target untouched, so a caller can
    // retain the previously valid print rather than accidentally publishing a
    // stale-rate or partially constructed replacement.
    j37dsp::NoisePrint preserved = atFileRate;
    const auto preservedFirst = preserved.sample (0, 0);
    const auto failedConversion = j37dsp::resamplePrint (j37dsp::NoisePrint {}, 96000.0, preserved);
    check (! failedConversion && preserved.sampleRate == atFileRate.sampleRate
               && preserved.sample (0, 0) == preservedFirst,
           "failed rate conversion leaves the prior valid print unchanged");

    j37dsp::NoisePrint recovered;
    check (j37dsp::resamplePrint (preserved, 48000.0, recovered)
               && recovered.sampleRate == 48000.0
               && recovered.sourceRate == preserved.sourceRate,
           "a valid rate conversion recovers the retained source print");

    check (! j37dsp::resamplePrint (atFileRate,
                                    std::numeric_limits<double>::infinity(), recovered),
           "an infinite target rate is refused before output sizing");
    check (! j37dsp::resamplePrint (atFileRate, 1.0e300, recovered),
           "an unsupported target rate is refused before output sizing");

    auto oversizedSameRate = atFileRate;
    oversizedSameRate.sampleRate = 1.0;
    oversizedSameRate.length = static_cast<std::size_t> (j37dsp::maxNoisePrintSeconds + 1.0);
    oversizedSameRate.left.resize (oversizedSameRate.length, 0.0f);
    oversizedSameRate.right.resize (oversizedSameRate.length, 0.0f);
    check (! j37dsp::resamplePrint (oversizedSameRate, oversizedSameRate.sampleRate, recovered),
           "an oversized same-rate print is rejected before copying its buffers");
}

//==============================================================================
void testRefusals()
{
    std::printf ("refusals\n");

    j37dsp::NoisePrint print;

    const auto empty = j37dsp::decodeWavMemory (nullptr, 0, print);
    check (empty.status == j37dsp::NoiseDecodeStatus::unreadable && empty.reason[0] != '\0',
           "an empty buffer is refused with a reason");

    const std::vector<std::uint8_t> garbage { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
    const auto rejected = j37dsp::decodeWavMemory (garbage.data(), garbage.size(), print);
    check (rejected.status == j37dsp::NoiseDecodeStatus::unreadable,
           "bytes that are not a WAV are refused");

    // A header that promises 1000 frames and delivers one. Whatever dr_wav
    // chooses to do with the short read, the loader must never hand back more
    // frames than the file actually contains - a loop padded with zeros is a
    // dropout once per repeat.
    auto truncated = WaveWriter::make (1, 48000, 16, false, ramp (1000, 1, 0.0001));
    truncated.resize (44 + 2);
    const auto cut = j37dsp::decodeWavMemory (truncated.data(), truncated.size(), print);
    check (cut.frames <= 1 && print.length <= 1,
           "a truncated file yields only the frames that are really there, never a padded loop");

    auto oversizedHeader = WaveWriter::make (1, 8000, 16, false, { 0.0 });
    const auto oversizedSamples = static_cast<std::uint32_t> (
        61.0 * 8000.0 * 2.0);
    for (int i = 0; i < 4; ++i)
        oversizedHeader[40 + static_cast<std::size_t> (i)]
            = static_cast<std::uint8_t> ((oversizedSamples >> (8 * i)) & 0xffu);
    const auto oversized = j37dsp::decodeWavMemory (
        oversizedHeader.data(), oversizedHeader.size(), print);
    check (oversized.status == j37dsp::NoiseDecodeStatus::tooLong,
           "a truncated WAV declaring over a minute is rejected before oversized allocation");

    const std::vector<double> withNaN { 0.0, std::nan (""), 0.25, 0.5 };
    const auto nan = WaveWriter::make (2, 48000, 32, true, withNaN);
    const auto notFinite = j37dsp::decodeWavMemory (nan.data(), nan.size(), print);
    check (notFinite.status == j37dsp::NoiseDecodeStatus::nonFinite,
           "a file containing NaN is refused, not played");

    // Over the cap: 61 seconds of 8 kHz mono is 488,008 frames.
    const auto longFrames = static_cast<std::size_t> (61.0 * 8000.0);
    std::vector<double> silence (longFrames, 0.0);
    const auto tooLong = WaveWriter::make (1, 8000, 16, false, silence);
    const auto before = print.length;
    const auto capped = j37dsp::decodeWavMemory (tooLong.data(), tooLong.size(), print);
    check (capped.status == j37dsp::NoiseDecodeStatus::tooLong,
           "a print over the one-minute cap is refused rather than truncated");
    check (print.length == before,
           "a refused decode leaves the caller's print untouched");
}

//==============================================================================
//  The stage: a print is a REPLACEMENT for the synthesised surface, and the
//  arithmetic of that replacement is exact rather than approximately right.
//==============================================================================
j37dsp::NoisePrint makeTestPrint (std::size_t frames, double rate)
{
    j37dsp::NoisePrint print;
    print.sampleRate = rate;
    print.sourceRate = rate;
    print.sourceFrames = frames;
    print.length = frames;
    print.left.resize (frames);
    print.right.resize (frames);

    for (std::size_t i = 0; i < frames; ++i)
    {
        print.left[i] = static_cast<float> (std::sin (static_cast<double> (i) * 0.37)) * 0.5f;
        print.right[i] = -print.left[i];
    }

    return print;
}

/** A stage with every generator switched off, so what it outputs is exactly what
    the print contributed. WARMTH is off too: the RIAA tilt must not colour a
    test that is about the print itself. */
VinylStage silentStage()
{
    VinylStage stage;
    stage.riaaAmount = 0.0f;
    stage.rumbleAmount = 0.0f;
    stage.crackleAmount = 0.0f;
    stage.surfaceNoise = 0.0f;
    stage.speedModulation = 0.0f;
    stage.dustAmount = 0.0f;
    stage.scratchAmount = 0.0f;
    stage.warpAmount = 0.0f;
    stage.electricalAmount = 0.0f;
    stage.clickAmount = 0.0f;
    stage.generationTopLoss = 1.0f;
    stage.generationBottomLift = 1.0f;
    return stage;
}

void testStage()
{
    std::printf ("stage\n");

    const auto print = makeTestPrint (97, 48000.0);
    const auto gain = 0.25f;

    auto left = silentStage();
    auto right = silentStage();
    left.noisePrint = &print;
    right.noisePrint = &print;

    std::uint32_t randomL = 12345u;
    std::uint32_t randomR = 54321u;

    auto exact = true;
    std::vector<float> firstPass (print.length, 0.0f);
    std::vector<float> secondPass (print.length, 0.0f);

    for (std::size_t i = 0; i < print.length * 2; ++i)
    {
        const auto outL = left.process (0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, randomL, gain);
        const auto outR = right.process (0.0f, 1, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, randomR, gain);

        exact = exact
             && outL == print.sample (0, i) * gain
             && outR == print.sample (1, i) * gain;

        (i < print.length ? firstPass : secondPass)[i % print.length] = outL;
    }

    check (exact, "with the generators off, the stage's output IS the print's sample times the gain");
    check (firstPass == secondPass,
           "the loop wraps to exactly the same samples on the second pass, with no seam");

    // The replacement claim, and the one that would be a regression if the print
    // were ADDED to the synthesised surface instead of replacing it: a stage with
    // a print loaded and a 100 % surface must not produce the synth hiss at all.
    auto synthesised = silentStage();
    synthesised.surfaceNoise = 1.0f;

    auto printed = synthesised;
    printed.noisePrint = &print;

    std::uint32_t r1 = 999u;
    std::uint32_t r2 = 999u;

    const auto synthOnly = synthesised.process (0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, r1, 0.0f);
    const auto printAtZeroGain = printed.process (0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, r2, 0.0f);

    check (synthOnly != 0.0f, "the synthesised surface is what a build with no print plays");
    check (printAtZeroGain == 0.0f,
           "a loaded print REPLACES the synthesised hiss rather than doubling it");

    // ... and the print does not silence the deck's own faults: CLICKS still fire
    // under a print, because the print is the surface and a click is the damage.
    auto clicking = silentStage();
    clicking.noisePrint = &print;
    clicking.clickAmount = 1.0f;
    clicking.surfaceNoise = 0.0f;

    auto anyClick = false;
    std::uint32_t random = 7u;

    for (int i = 0; i < 20000 && ! anyClick; ++i)
        anyClick = std::abs (clicking.process (0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, random, 0.0f)) > 1.0e-4f;

    check (anyClick, "CLICKS still fires over a loaded print: the damage model survives the change");
}

//==============================================================================
void testBuildShape()
{
    std::printf ("build shape\n");

    check (J37_HAS_DRWAV == J37_EXPECT_DRWAV,
           "J37_HAS_DRWAV is " + std::to_string (J37_HAS_DRWAV)
               + ", the shape this binary was built to be");

    const auto values = ramp (32, 1, 0.01);
    const auto file = WaveWriter::make (1, 48000, 16, false, values);

    j37dsp::NoisePrint print;
    const auto outcome = j37dsp::decodeWavMemory (file.data(), file.size(), print);

    if (J37_EXPECT_DRWAV)
    {
        check (outcome.status == j37dsp::NoiseDecodeStatus::ok && print.length == 32,
               "this build decodes a WAV");
    }
    else
    {
        check (outcome.status == j37dsp::NoiseDecodeStatus::absent
                   && std::string (outcome.reason).find ("no WAV decoder") != std::string::npos,
               "a build with no decoder refuses the file and says which library is missing");
        check (print.empty(), "and leaves the loop empty rather than half-filled");
    }

    // The loop machinery is not part of the decoder and must be compiled and
    // correct in both shapes: a build with no decoder can still resample a print
    // another build produced, which is what a project opened from a session with
    // a compiled-in print would need.
    const auto handBuilt = makeTestPrint (64, 44100.0);
    j37dsp::NoisePrint converted;

    check (j37dsp::resamplePrint (handBuilt, 48000.0, converted)
               && converted.length == static_cast<std::size_t> (std::llround (64.0 * 48000.0 / 44100.0)),
           "the rate conversion compiles and runs in both shapes");
}

} // namespace

int main()
{
    std::printf ("noise_loader: J37_HAS_DRWAV %d (expected %d)\n", J37_HAS_DRWAV, J37_EXPECT_DRWAV);

    testBuildShape();

    // The decode tests are assertions ABOUT the decoder, so a build without one
    // does not run them: it asserts the absent shape instead (in testBuildShape)
    // and then the loop machinery, which is compiled in both builds.
    if (J37_HAS_DRWAV)
    {
        testDecodeShapes();
        testRateConversion();
        testRefusals();
    }

    testStage();

    if (gFailures == 0)
    {
        std::printf ("NOISE CHECK PASSED (0 failures)\n");
        return 0;
    }

    std::printf ("NOISE CHECK FAILED (%d failures)\n", gFailures);
    return 1;
}
