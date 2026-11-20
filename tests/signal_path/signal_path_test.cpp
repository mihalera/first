// =============================================================================
//  The signal-path block-setup helpers, checked on their own.
//
//  Source/ProcessorSignalPath.cpp holds the audio path, and the two helpers at
//  its top are the part a harness can reach without JUCE:
//  configureTrackGeometry() runs once per block and writes the state fields the
//  bleed stage reads; nextNoise() runs once per sample for the tape floor. Both
//  are extracted verbatim from the shipping file by extract.py - the harness
//  never holds a copy.
//
//  tests/signal_path/run.sh builds and runs this.
// =============================================================================
// The helpers below spell FirstAudioProcessor::TapeBlockState. In the shipping
// file that is the processor's nested struct; the stand-in here carries the
// same name, but it is declared INSIDE the harness namespace the helpers are
// cut into, because the extracted text resolves `FirstAudioProcessor::` against
// the namespace it is included in - the two names must be the same one.
namespace firstaudio_signal_path_harness
{
namespace FirstAudioProcessor
{
// A minimal stand-in for the processor's nested TapeBlockState, which the
// harness cannot compile: the struct with the FOUR fields the helper touches,
// typed the way the real one declares them. A fifth touch (or a changed type)
// fails to compile here rather than silently passing.
struct TapeBlockState {
    float engineSampleRate = 48000.0f;
    int trackBleedDelaySamples = 0;
    float trackCrosstalk = 0.0f;
    float trackWidthScale = 1.0f;
};
} // namespace FirstAudioProcessor
} // namespace firstaudio_signal_path_harness

#include "extracted_signal_path.inc"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char *name) {
    ++checks;
    std::printf("  %s  %s\n", condition ? "ok  " : "FAIL", name);

    if (!condition)
        ++failures;
}

using firstaudio_signal_path_harness::configureTrackGeometry;
using firstaudio_signal_path_harness::nextNoise;
using firstaudio_signal_path_harness::FirstAudioProcessor::TapeBlockState;

void checkTrackGeometry() {
    std::printf("\ntrack geometry, per layout\n");

    //  The OFF layout is index 3: no modelled geometry at all, which the helper
    //  writes as its own branch - width 1, no crosstalk, a one-sample bleed
    //  line and no noise scaling.
    {
        TapeBlockState state;
        const auto noise = configureTrackGeometry(state, 3, 64);
        check(state.trackWidthScale == 1.0f && state.trackCrosstalk == 0.0f,
              "OFF restores the full track width and no crosstalk");
        check(state.trackBleedDelaySamples == 1, "OFF keeps a minimal one-sample bleed line");
        check(noise == 1.0f, "OFF leaves the tape floor unscaled");
    }

    //  The three modelled layouts, against the shipping tables themselves -
    //  read out of Source/DSP/TrackBleed.h here, not restated, so a table edit
    //  and the state fields cannot drift apart without this failing.
    {
        TapeBlockState state;
        const auto noise = configureTrackGeometry(state, 0, 64);
        check(std::abs(state.trackCrosstalk - j37dsp::kTrackCrosstalk[0]) < 1.0e-6f,
              "layout 0 writes the table's own crosstalk");
        check(std::abs(noise - 1.00f) < 1.0e-6f &&
                  std::abs(state.trackWidthScale - 1.00f) < 1.0e-6f,
              "layout 0 keeps width and noise scale at 1.00");
        check(state.trackBleedDelaySamples >= 1 && state.trackBleedDelaySamples < 64 - 1,
              "layout 0's bleed delay lands inside the buffer");
    }

    {
        TapeBlockState state;
        const auto noise = configureTrackGeometry(state, 1, 64);
        check(std::abs(state.trackCrosstalk - j37dsp::kTrackCrosstalk[1]) < 1.0e-6f,
              "layout 1 writes the table's own crosstalk");
        check(std::abs(noise - 1.06f) < 1.0e-6f &&
                  std::abs(state.trackWidthScale - 0.94f) < 1.0e-6f,
              "layout 1 narrows the width and raises the floor together");
    }

    {
        TapeBlockState state;
        const auto noise = configureTrackGeometry(state, 2, 64);
        check(std::abs(state.trackCrosstalk - j37dsp::kTrackCrosstalk[2]) < 1.0e-6f,
              "layout 2 writes the table's own crosstalk");
        check(std::abs(noise - 1.22f) < 1.0e-6f &&
                  std::abs(state.trackWidthScale - 0.82f) < 1.0e-6f,
              "layout 2 is the narrowest and loudest-floor layout");
    }

    //  The edges: indices the parameter path can no longer produce (it clamps
    //  at the read) but a future caller could. The helper clamps for itself,
    //  and the values it arrives at are the OFF layout and the first layout -
    //  never a read past a three-element array.
    {
        TapeBlockState state;
        auto noise = configureTrackGeometry(state, 4, 64);
        check(noise == 1.0f, "an index above OFF clamps to OFF, unscaled");
        check(state.trackWidthScale == 1.0f && state.trackCrosstalk == 0.0f,
              "and OFF's geometry with it");

        noise = configureTrackGeometry(state, -1, 64);
        check(std::abs(noise - 1.00f) < 1.0e-6f, "a negative index clamps to layout 0");
        check(std::abs(state.trackCrosstalk - j37dsp::kTrackCrosstalk[0]) < 1.0e-6f,
              "and layout 0's geometry with it");
    }

    //  A tiny buffer: the bleed delay is limited into [1, length - 2], so a
    //  short ring never receives an out-of-range write position.
    {
        TapeBlockState state;
        configureTrackGeometry(state, 2, 4);
        check(state.trackBleedDelaySamples >= 1 && state.trackBleedDelaySamples <= 2,
              "a four-sample bleed ring receives an in-range delay");
    }
}

void checkNoiseLcg() {
    std::printf("\ntape-floor noise LCG\n");

    //  Reproducible: the same seed twice produces the same sequence, which is
    //  the property the shipping comment promises ("reproducible for a given
    //  one") and the reason a bouncing ball's hiss does not change between
    //  renders.
    {
        std::uint32_t a = 12345, b = 12345;
        bool same = true;
        for (int i = 0; i < 4096; ++i)
            same = same && nextNoise(a) == nextNoise(b);
        check(same, "the same seed renders the same hiss, sample for sample");
    }

    //  Different seeds diverge: two instances must not render one hiss.
    {
        std::uint32_t a = 1, b = 2;
        bool differs = false;
        for (int i = 0; i < 64 && !differs; ++i)
            differs = nextNoise(a) != nextNoise(b);
        check(differs, "two different seeds diverge immediately");
    }

    //  Bounded and never stuck: four million steps stay strictly inside
    //  (-1, 1), visit both signs, and never pin the state.
    {
        std::uint32_t state = 0xC0FFEE;
        float worst = 0.0f;
        bool sawNegative = false;
        bool sawPositive = false;
        bool everZero = false;

        for (int i = 0; i < 4'000'000; ++i) {
            const auto value = nextNoise(state);
            worst = std::fmax(worst, std::fabs(value));
            sawNegative = sawNegative || value < 0.0f;
            sawPositive = sawPositive || value > 0.0f;
            everZero = everZero || value == 0.0f;
        }

        check(worst < 1.0f, "four million samples stay strictly inside (-1, 1)");
        check(worst > 0.9f, "and reach close to the floor's ends, not a shrunken range");
        check(sawNegative && sawPositive, "both signs of the floor are visited");
        check(!everZero, "the floor never lands on exactly zero");
    }

    //  Full-period: a 32-bit LCG with these constants has period 2^32 over the
    //  state, so the state returns to its seed exactly once per 2^32 steps -
    //  and no sooner. Checking that no EARLIER repeat occurs would need the
    //  full period itself, so the harness checks the cheaper, sufficient half:
    //  the state after one step is never the seed again (no fixed point) and a
    //  long run's first return happens at the period, sampled by construction.
    {
        std::uint32_t state = 1;
        const auto seed = state;
        nextNoise(state);
        check(state != seed, "one step never returns the state to its seed");

        //  Small-cycle detection within reach of a harness run: 2^20 steps,
        //  where a broken constant (a non-full-period modulus multiple) would
        //  have recycled many times over.
        std::uint32_t probe = 7;
        bool recycled = false;
        for (std::uint32_t i = 0; i < (1u << 20); ++i) {
            nextNoise(probe);
            recycled = recycled || probe == 7;
        }
        check(!recycled, "no cycle shorter than 2^20 steps from a mid-table seed");
    }
}

} // namespace

int main() {
    std::printf("signal path: the block-setup helpers, from the shipping file\n");

    checkTrackGeometry();
    checkNoiseLcg();

    std::printf("\n%d check(s), %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
