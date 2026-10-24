// =============================================================================
//  fuzz_noise_decoder - Source/DSP/NoiseDecoder.h, fed bytes it never wrote.
//
//  The decoder is the one place in the plugin that reads a file a user picked,
//  and dr_wav is the one library in the tree that parses untrusted bytes. The
//  harness beside this directory (tests/dsp/noise_loader_test.cpp) proves the
//  decoder handles the files it was written against; it cannot say what a
//  TRUNCATED header, a channel count of zero or a data chunk claiming more frames
//  than it holds does. That is the input space this file exists to walk.
//
//  What it asserts is the header's own CONTRACT, not a number:
//
//    * a refusal names its reason - an empty string is a panel that says
//      "INVALID PRINT ()" and a user with nothing to go on;
//    * a refusal leaves the caller's print untouched and reports no frames, so a
//      load that failed cannot half-replace the loop that is playing;
//    * an acceptance is a shape the stage can actually play: a non-zero length,
//      both channels at least that long, a positive rate, and every sample
//      finite - "ok" and then a NaN would pass every other test in this tree;
//    * the same bytes decode to the same print, so a load is deterministic.
//
//  Two shapes, one source:
//
//    * WITH libFuzzer (clang's -fsanitize=fuzzer, which defines
//      LLVMFuzzerTestOneInput's caller) the mutations come from the fuzzer's own
//      coverage-guided engine, seeded with the corpus `--dump-seeds` writes.
//    * WITHOUT it (this file's `main`, g++) the mutations are a fixed,
//      reproducible sweep: truncations, bit flips, header fields, appended
//      garbage and pure random buffers. Every run walks the same bytes, which is
//      what makes it a gate rather than a lottery - a machine without clang
//      still exercises the refusals instead of skipping the harness.
//
//  The bytes are built here rather than committed as a corpus: a WAV is a header
//  and a buffer, and a binary fixture in the tree is a file nobody can diff when
//  the format it claims to test changes.
// =============================================================================
#include "DSP/NoiseDecoder.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

[[noreturn]] void failed (const std::string& why)
{
    std::fprintf (stderr, "fuzz_noise_decoder: %s\n", why.c_str());
    std::abort();
}

//==============================================================================
/** How often the sweep saw each verdict, and which refusals it reached. A count
    table is the cheapest proof that the sweep is exploring rather than re-reading
    one header a few thousand times. */
struct Stats
{
    std::size_t runs = 0;
    std::size_t accepted = 0;
    std::size_t refused = 0;
    std::vector<const char*> reasons;

    /** The refusal strings are literals in the decoder, so their addresses are
        the identity: one entry per branch reached, not one per refused input. */
    void remember (const char* reason)
    {
        for (const auto* seen : reasons)
            if (seen == reason)
                return;

        reasons.push_back (reason);
    }
};

//==============================================================================
/** A canonical RIFF/WAVE file.

    `flavour` picks the sample material: 0 a sine, 1 a ramp, 2 a float file. The
    three are the three decodings the panel offers (16-bit PCM, 24-bit PCM and
    32-bit float), which is why the seeds are three files rather than one. */
Bytes makeWav (int bitsPerSample, int channels, std::size_t frames, int flavour)
{
    const auto bytesPerSample = static_cast<std::uint32_t> (bitsPerSample / 8);
    const auto blockAlign = static_cast<std::uint16_t> (channels * bytesPerSample);
    const auto dataBytes = static_cast<std::uint32_t> (frames * blockAlign);
    const auto isFloat = flavour == 2;

    Bytes file;
    file.reserve (44 + dataBytes);

    const auto tag = [&file] (const char* text) {
        for (int i = 0; i < 4; ++i)
            file.push_back (static_cast<std::uint8_t> (text[i]));
    };
    const auto le16 = [&file] (std::uint16_t value) {
        file.push_back (static_cast<std::uint8_t> (value & 0xff));
        file.push_back (static_cast<std::uint8_t> ((value >> 8) & 0xff));
    };
    const auto le32 = [&file] (std::uint32_t value) {
        for (int i = 0; i < 4; ++i)
            file.push_back (static_cast<std::uint8_t> ((value >> (8 * i)) & 0xff));
    };

    tag ("RIFF");
    le32 (36 + dataBytes);
    tag ("WAVE");
    tag ("fmt ");
    le32 (16);
    le16 (static_cast<std::uint16_t> (isFloat ? 3 : 1));   // 3 = WAVE_FORMAT_IEEE_FLOAT
    le16 (static_cast<std::uint16_t> (channels));
    le32 (48000);
    le32 (48000 * blockAlign);
    le16 (blockAlign);
    le16 (static_cast<std::uint16_t> (bitsPerSample));
    tag ("data");
    le32 (dataBytes);

    for (std::size_t frame = 0; frame < frames; ++frame)
    {
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto phase = static_cast<float> (frame) * 0.01f
                             + static_cast<float> (channel) * 0.25f;
            const auto value = flavour == 0 ? 0.25f * std::sin (phase)
                             : flavour == 1 ? static_cast<float> (frame % 100) / 100.0f
                                            : 0.5f * std::sin (phase * 3.0f);

            if (isFloat)
            {
                std::uint32_t bits = 0;
                std::memcpy (&bits, &value, sizeof (bits));
                le32 (bits);
            }
            else if (bitsPerSample == 16)
            {
                le16 (static_cast<std::uint16_t> (
                    static_cast<std::int16_t> (value * 32000.0f)));
            }
            else
            {
                const auto wide = static_cast<std::int32_t> (value * 8000000.0f);
                file.push_back (static_cast<std::uint8_t> (wide & 0xff));
                file.push_back (static_cast<std::uint8_t> ((wide >> 8) & 0xff));
                file.push_back (static_cast<std::uint8_t> ((wide >> 16) & 0xff));
            }
        }
    }

    return file;
}

std::vector<Bytes> seeds()
{
    return { makeWav (16, 2, 512, 0), makeWav (24, 1, 256, 1), makeWav (32, 2, 512, 2) };
}

//==============================================================================
/** Decode one input and hold it to the contract above. */
void exercise (const std::uint8_t* data, std::size_t size, Stats& stats)
{
    j37dsp::NoisePrint print;
    const auto outcome = j37dsp::decodeWavMemory (data, size, print);

    ++stats.runs;

    if (outcome.status == j37dsp::NoiseDecodeStatus::ok)
    {
        ++stats.accepted;

        if (print.empty())
            failed ("status ok, but the print it filled is empty");

        if (print.length != outcome.frames)
            failed ("status ok, but the reported frame count is not the length");

        if (print.sampleRate <= 0.0 || print.sourceRate <= 0.0)
            failed ("status ok, but the print declares no rate");

        for (std::size_t i = 0; i < print.length; ++i)
            if (! std::isfinite (print.left[i]) || ! std::isfinite (print.right[i]))
                failed ("status ok, but the print holds a sample that is not finite");

        // Deterministic: the same bytes into a second decoder must produce the
        // same loop. A decode that read past its buffer would already have been
        // caught by the sanitizer, but a decode that picked a different branch on
        // the same input would not.
        j37dsp::NoisePrint again;
        const auto repeat = j37dsp::decodeWavMemory (data, size, again);

        if (repeat.status != outcome.status || again.length != print.length)
            failed ("the same bytes did not decode the same way twice");

        for (std::size_t i = 0; i < print.length; ++i)
            if (again.left[i] != print.left[i] || again.right[i] != print.right[i])
                failed ("the same bytes produced different samples twice");
    }
    else
    {
        ++stats.refused;

        if (outcome.reason == nullptr || outcome.reason[0] == '\0')
            failed ("a refusal with no reason - the panel would print an empty bracket");

        if (outcome.frames != 0)
            failed ("a refusal that still reports decoded frames");

        if (! print.empty())
            failed ("a refusal that still filled the caller's print");

        stats.remember (outcome.reason);
    }
}

void exercise (const Bytes& bytes, Stats& stats)
{
    exercise (bytes.data(), bytes.size(), stats);
}

//==============================================================================
/** The fixed sweep. Every mutation here is reproducible: a failure names the
    index that produced it, so the bytes can be rebuilt by hand. */
void sweep (const Bytes& seed, Stats& stats)
{
    exercise (seed, stats);

    // A header that claims more than the file holds, at every seam a reader can
    // stop at: mid-chunk-id, mid-size, mid-format, mid-data.
    for (std::size_t cut = seed.size() / 8; cut < seed.size(); cut += seed.size() / 8)
    {
        Bytes truncated (seed.begin(), seed.begin() + static_cast<std::ptrdiff_t> (cut));
        exercise (truncated, stats);
    }

    // Every bit of the sixty-byte header region, one at a time. The interesting
    // ones are the format tag, the channel count, the rate and the two sizes; the
    // sweep does not get to decide which of those matter.
    for (std::size_t byte = 0; byte < 60 && byte < seed.size(); ++byte)
    {
        for (int bit = 0; bit < 8; ++bit)
        {
            Bytes flipped = seed;
            flipped[byte] = static_cast<std::uint8_t> (flipped[byte] ^ (1u << bit));
            exercise (flipped, stats);
        }
    }

    // The fields a damaged file most often has wrong, set to the values that used
    // to matter to a naive reader: zero channels, zero rate, a maximum size.
    const auto overwrite = [&stats, &seed] (std::size_t offset, const Bytes& value) {
        if (offset + value.size() > seed.size())
            return;

        Bytes edited = seed;
        std::memcpy (edited.data() + offset, value.data(), value.size());
        exercise (edited, stats);
    };

    for (const auto& channels : { Bytes { 0, 0 }, Bytes { 0xff, 0xff }, Bytes { 3, 0 } })
        overwrite (22, channels);

    for (const auto& rate : { Bytes { 0, 0, 0, 0 }, Bytes { 0xff, 0xff, 0xff, 0xff },
                              Bytes { 1, 0, 0, 0 } })
        overwrite (24, rate);

    for (const auto& width : { Bytes { 0, 0 }, Bytes { 0xff, 0xff } })
        overwrite (34, width);

    for (const auto& blockAlign : { Bytes { 0, 0 }, Bytes { 0xff, 0xff } })
        overwrite (32, blockAlign);

    for (const auto& riffSize : { Bytes { 0, 0, 0, 0 }, Bytes { 0xff, 0xff, 0xff, 0xff } })
        overwrite (4, riffSize);

    // The fmt chunk's own size. 0xfffffff8 is the value that made dr_wav skip the
    // chunk with a negative relative seek and read it again forever, which is the
    // reason NoiseDecoder.h refuses a chunk list that does not fit the file.
    for (const auto& fmtSize : { Bytes { 0, 0, 0, 0 }, Bytes { 0xff, 0xff, 0xff, 0xff },
                                 Bytes { 0xf8, 0xff, 0xff, 0xff } })
        overwrite (16, fmtSize);

    for (const auto& dataSize : { Bytes { 0, 0, 0, 0 }, Bytes { 0xff, 0xff, 0xff, 0x7f },
                                  Bytes { 0x10, 0, 0, 0 } })
        overwrite (40, dataSize);

    // Garbage after a valid file: the trailing-chunk path a hand-edited file has.
    for (std::size_t extra = 1; extra <= 17; extra += 8)
    {
        Bytes appended = seed;
        appended.insert (appended.end(), extra, static_cast<std::uint8_t> (0xa5));
        exercise (appended, stats);
    }

    // A header with no data chunk at all.
    exercise (Bytes (seed.begin(), seed.begin() + 36), stats);
}

/** Buffers with no structure whatsoever, from a fixed linear congruential
    generator - the addresses a fuzzer would otherwise only reach by luck. */
void randomBuffers (Stats& stats)
{
    for (const std::size_t length : { std::size_t (0), std::size_t (1), std::size_t (4),
                                      std::size_t (12), std::size_t (44), std::size_t (45),
                                      std::size_t (200), std::size_t (2000) })
    {
        Bytes bytes (length);
        std::uint32_t state = 0x2545f491u;

        for (auto& byte : bytes)
        {
            state = state * 1664525u + 1013904223u;
            byte = static_cast<std::uint8_t> (state >> 24);
        }

        exercise (bytes, stats);
    }
}

void report (const Stats& stats)
{
    std::printf ("  ok   %zu inputs: %zu decoded, %zu refused, %zu distinct reasons\n",
                 stats.runs, stats.accepted, stats.refused, stats.reasons.size());

    for (const auto* reason : stats.reasons)
        std::printf ("         refused: %s\n", reason);
}

} // namespace

// `main` and LLVMFuzzerTestOneInput both live OUTSIDE the anonymous namespace on
// purpose: a `main` inside one is not the program's entry point, and a C-linkage
// symbol inside one is internal, which is a fuzzer that links and never runs.
#ifndef J37_FUZZ_LIBFUZZER
int main (int argc, char** argv)
{
    // The corpus the libFuzzer shape starts from, built from the same three files
    // the sweep mutates. `tests/fuzz/run.sh` writes it, and the fuzzer's own
    // coverage then decides where to go next.
    if (argc == 3 && std::strcmp (argv[1], "--dump-seeds") == 0)
    {
        const auto corpus = seeds();
        const char* names[] = { "seed_16bit_stereo.wav", "seed_24bit_mono.wav",
                                "seed_float_stereo.wav" };
        int written = 0;

        for (std::size_t i = 0; i < corpus.size(); ++i)
        {
            const auto path = std::string (argv[2]) + "/" + names[i];
            auto* file = std::fopen (path.c_str(), "wb");

            if (file == nullptr)
            {
                std::fprintf (stderr, "fuzz_noise_decoder: cannot write %s\n", path.c_str());
                return 1;
            }

            const auto written_here = std::fwrite (corpus[i].data(), 1, corpus[i].size(), file);
            std::fclose (file);

            if (written_here != corpus[i].size())
            {
                std::fprintf (stderr, "fuzz_noise_decoder: short write to %s\n", path.c_str());
                return 1;
            }

            ++written;
        }

        std::printf ("  ok   wrote %d seed files\n", written);
        return 0;
    }

    Stats stats;

    // Files named on the command line first: that is how a reproducer found by
    // the fuzzer is re-run through this binary.
    for (int i = 1; i < argc; ++i)
    {
        auto* file = std::fopen (argv[i], "rb");

        if (file == nullptr)
        {
            std::fprintf (stderr, "fuzz_noise_decoder: cannot read %s\n", argv[i]);
            return 1;
        }

        Bytes bytes;
        std::uint8_t chunk[4096];
        std::size_t got = 0;

        while ((got = std::fread (chunk, 1, sizeof (chunk), file)) > 0)
            bytes.insert (bytes.end(), chunk, chunk + got);

        std::fclose (file);
        exercise (bytes, stats);
    }

    for (const auto& seed : seeds())
        sweep (seed, stats);

    randomBuffers (stats);
    report (stats);
    return 0;
}
#else
// The fuzzer supplies main; the entry point is the same contract, one input at a
// time. `Stats` is intentionally not used here: a fuzzer's report is its coverage,
// and a counter printed per input would cost more than the decode.
extern "C" int LLVMFuzzerTestOneInput (const std::uint8_t* data, std::size_t size)
{
    Stats stats;
    exercise (data, size, stats);
    return 0;
}
#endif
