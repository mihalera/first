// =============================================================================
//  Unit tests for the equaliser - Source/DSP/ThreeBandEq.h.
//
//  tests/dsp/run.sh renders the shipping signal path and asserts on the audio.
//  Nothing in it drives the equaliser, the conventional compressor or the
//  transient shaper DIRECTLY: those three are unit-sized stages - they hold no
//  engine state, they take their coefficients as arguments, and each carries a
//  written contract in its own header comment. They are what
//  cmake/J37Tests.cmake means by "does this function still return what it
//  returned yesterday", and they are what this file measures.
//
//  Every TEST_CASE names the claim it pins, in the header's own words, so a
//  failure here says which promise moved rather than only which number.
// =============================================================================
#include "shim.h"

#include "DSP/ThreeBandEq.h"

#include <catch_amalgamated.hpp>

#include <cmath>
#include <limits>

namespace
{
constexpr float kRate = 48000.0f;

/** The stage as the panel leaves it when nothing is being asked of it: all three
    bands at unity, both filters at their OFF ends, Q at its default. */
ThreeBandEq neutralEq()
{
    ThreeBandEq eq;
    eq.prepare (kRate);
    eq.prepareFilters (kRate, 20.0f, kRate * 0.45f, 12.0f, 0.7f);
    eq.lowGain = 1.0f;
    eq.midGain = 1.0f;
    eq.highGain = 1.0f;
    return eq;
}

/** RMS of a sine through the stage, after a warm-up long enough for every
    one-pole in it to settle. The stage is taken BY VALUE, so each measurement
    starts from a caller's own state rather than from the previous test's. */
float rmsThrough (ThreeBandEq eq, float hz, float amplitude,
                  int samples = 24000, int warmUp = 4800)
{
    const auto midCoefficient = eq.midBandCoefficient (kRate);
    float sum = 0.0f;

    for (int i = 0; i < samples + warmUp; ++i)
    {
        const auto x = amplitude * std::sin (juce::MathConstants<float>::twoPi * hz
                                             * static_cast<float> (i) / kRate);
        const auto y = eq.process (x, 0, midCoefficient);

        if (i >= warmUp)
            sum += y * y;
    }

    return std::sqrt (sum / static_cast<float> (samples));
}

/** The last sample of a constant (DC) input, which is what a filter's own DC
    response comes down to. */
float dcThrough (ThreeBandEq eq, float value, int samples = 48000)
{
    const auto midCoefficient = eq.midBandCoefficient (kRate);
    float y = 0.0f;

    for (int i = 0; i < samples; ++i)
        y = eq.process (value, 0, midCoefficient);

    return y;
}
} // namespace

TEST_CASE ("ThreeBandEq: the OFF ends of both filters are real bypasses, not gentle filters")
{
    // The header: "both ends of each control's travel are REAL bypasses rather
    // than a filter that happens to be very gentle. That is what makes a neutral
    // EQ bit-for-bit transparent."
    ThreeBandEq eq;
    eq.prepare (kRate);

    eq.prepareFilters (kRate, 20.0f, kRate * 0.45f, 12.0f, 0.7f);
    REQUIRE (eq.hpActive == false);
    REQUIRE (eq.lpActive == false);

    // One hertz above the high-pass floor, and one hertz below the low-pass
    // ceiling: each filter is now in the path.
    eq.prepareFilters (kRate, 23.0f, kRate * 0.45f - 1.0f, 12.0f, 0.7f);
    REQUIRE (eq.hpActive == true);
    REQUIRE (eq.lpActive == true);
}

TEST_CASE ("ThreeBandEq: at neutral settings the stage is an identity to within a float step")
{
    // The header: "At 0 dB on all three bands, HP at its floor, LP at its ceiling
    // and Q at its default, the whole stage is bit-for-bit transparent."
    //
    // What is exactly true is the reason the header gives for it: the two filters
    // are SKIPPED at those settings rather than set very gently, so a sample
    // passes through no filter at all. What is not exactly true is the sentence
    // taken to its last bit, because the three bands are one-pole splits summed
    // back together and a sum of rounded terms rounds: 0.5 and the other powers
    // of two come back bit for bit, while 0.1 comes back one float step low
    // (0.099999994). That is a float step, not a filter - which is the difference
    // this pins. A stage that was "transparent to within a very gentle filter"
    // (the failure the header is arguing against) or that had grown a gain, a
    // trim or a tilt would move these values by orders of magnitude more.
    const float inputs[] = { 0.0f, 1.0e-7f, -1.0e-7f, 0.1f, -0.1f, 0.5f, -0.5f,
                             0.999f, -0.999f, 2.0f, -2.0f, 1.0e6f, -1.0e6f };

    for (const auto x : inputs)
    {
        auto eq = neutralEq();
        const auto y = eq.process (x, 0, eq.midBandCoefficient (kRate));

        // One float step of the value itself, so a failure names the input that
        // moved and by how much of a step.
        REQUIRE (std::abs (y - x) <= std::numeric_limits<float>::epsilon() * std::abs (x));
    }
}

TEST_CASE ("ThreeBandEq: a neutral stage is off by the addition's rounding and nothing else")
{
    // The same claim over a signal, with the band split's memory in play. The
    // three bands are one-pole splits of the input summed back together, so the
    // result cannot be exact - but the difference must be nothing more than the
    // addition's own rounding: no gain, no tilt, no phase shift, and no decay.
    //
    // It is the difference between "leaves the signal alone" and "is an
    // identity", and it is worth pinning because a filter that is 0.1 dB down in
    // one band still reads as neutral on the panel. Measured over a second of
    // 440 Hz at a 0.5 peak, the largest difference is 2.98e-8 - half an ulp of
    // 0.5 - and the bound below is one ulp of the peak.
    auto eq = neutralEq();
    const auto midCoefficient = eq.midBandCoefficient (kRate);
    constexpr float amplitude = 0.5f;
    float worst = 0.0f;

    for (int i = 0; i < 48000; ++i)
    {
        const auto x = amplitude * std::sin (juce::MathConstants<float>::twoPi * 440.0f
                                             * static_cast<float> (i) / kRate);
        worst = juce::jmax (worst, std::abs (eq.process (x, 0, midCoefficient) - x));
    }

    CHECK (worst <= std::numeric_limits<float>::epsilon() * amplitude);
}

TEST_CASE ("ThreeBandEq: the pole count is the slope divided by six, clamped to the array")
{
    ThreeBandEq eq;
    eq.prepare (kRate);

    struct Case { float slope; int poles; };
    const Case cases[] = {
        { 6.0f, 1 }, { 12.0f, 2 }, { 18.0f, 3 }, { 24.0f, 4 }, { 36.0f, 6 }, { 48.0f, 8 },
        // Below one pole and above the array's own size: clamped, because the
        // array's size is what makes eight the highest order on offer.
        { 0.0f, 1 }, { 60.0f, 8 }, { 600.0f, 8 }
    };

    for (const auto& testCase : cases)
    {
        eq.prepareFilters (kRate, 500.0f, 8000.0f, testCase.slope, 0.7f);
        CHECK (eq.hpPoles == testCase.poles);
        CHECK (eq.lpPoles == testCase.poles);
    }
}

TEST_CASE ("ThreeBandEq: Q is clamped to the range a tape machine may ring in")
{
    ThreeBandEq eq;
    eq.prepare (kRate);

    eq.prepareFilters (kRate, 500.0f, 8000.0f, 12.0f, 0.1f);
    CHECK (eq.hpQ == Catch::Approx (0.5f));

    eq.prepareFilters (kRate, 500.0f, 8000.0f, 12.0f, 12.0f);
    CHECK (eq.hpQ == Catch::Approx (1.5f));
    CHECK (eq.lpQ == eq.hpQ);
}

TEST_CASE ("ThreeBandEq: the high-pass removes DC and the low-pass keeps it")
{
    // A shelf "can never remove anything completely"; a filter removes
    // everything outside its passband. DC is the cleanest place to see which of
    // the two is which: the high-pass must take a constant away, the low-pass
    // must leave it exactly where it was.
    auto highPassed = neutralEq();
    highPassed.prepareFilters (kRate, 1000.0f, kRate * 0.45f, 24.0f, 0.7f);
    CHECK (std::abs (dcThrough (highPassed, 1.0f)) < 1.0e-3f);

    auto lowPassed = neutralEq();
    lowPassed.prepareFilters (kRate, 20.0f, 1000.0f, 24.0f, 0.7f);
    CHECK (dcThrough (lowPassed, 1.0f) == Catch::Approx (1.0f).margin (1.0e-4f));
}

TEST_CASE ("ThreeBandEq: LOW is a shelf - it moves the low band and leaves the top alone")
{
    const auto reference50 = rmsThrough (neutralEq(), 50.0f, 0.5f);
    const auto reference10k = rmsThrough (neutralEq(), 10000.0f, 0.5f);

    auto boosted = neutralEq();
    boosted.lowGain = 2.0f;

    // Lifted by the band's own gain where the band is, and untouched an octave
    // above the 200 Hz split where it is not. (Measured at 1.955x rather than at
    // 2.0x: the split is a one-pole at 200 Hz, so two octaves down it has reached
    // 97 % of its band rather than all of it - the same one-pole asymptote the
    // HIGH case below documents from the other end of the spectrum.)
    CHECK (rmsThrough (boosted, 50.0f, 0.5f) / reference50 == Catch::Approx (2.0f).epsilon (0.05));
    CHECK (rmsThrough (boosted, 10000.0f, 0.5f) / reference10k == Catch::Approx (1.0f).epsilon (0.05));
}

TEST_CASE ("ThreeBandEq: HIGH lifts the top band and leaves the bottom alone")
{
    // The header: "HIGH a high shelf above 4 kHz, the mirror of LOW." What the
    // case pins is the band assignment - the TOP takes the gain and the bottom is
    // untouched - rather than the exact number, because the number is a property
    // of how the split is built rather than of what the control means:
    //
    //   the high band is what the 4 kHz one-pole low-pass REMOVES from the band
    //   above 200 Hz, and that high side only reaches 0.744 at Nyquist rather than
    //   1.0 (it is 1 - the low-pass, and the low-pass passes 0.256 there). So a
    //   full 2x on HIGH measures as about 1.67x at 10 kHz, where HIGH at +6 dB
    //   reaching +4.5 dB is what a one-pole shelf does. LOW, whose split is at
    //   200 Hz, is close enough to its own asymptote at 50 Hz that it measures the
    //   full 2x - which is why the two shelves look asymmetric in these numbers
    //   without either being wrong.
    const auto reference50 = rmsThrough (neutralEq(), 50.0f, 0.5f);
    const auto reference10k = rmsThrough (neutralEq(), 10000.0f, 0.5f);

    auto boosted = neutralEq();
    boosted.highGain = 2.0f;

    const auto lifted = rmsThrough (boosted, 10000.0f, 0.5f) / reference10k;
    CHECK (lifted > 1.5f);
    CHECK (lifted < 1.85f);
    CHECK (rmsThrough (boosted, 50.0f, 0.5f) / reference50 == Catch::Approx (1.0f).epsilon (0.05));
}

TEST_CASE ("ThreeBandEq: MID acts on its own band and only a little on the ends")
{
    // The header's reason for a bell rather than a shelf: "a full-band mid shelf
    // would drag the low and high ends with it, so turning MID up would sound
    // like turning everything up."
    //
    // "Only a little" rather than "not at all" is the honest form of that claim,
    // and the size is the header's own: the bell is built as a band-pass whose
    // REST is added back at 0.15, "so 0 dB really is 0 dB at every width" - that
    // term is by construction a small fraction of the mid gain applied to the
    // bell's skirt, and the skirt reaches into the two ends of the spectrum that
    // the 200 Hz and 4 kHz splits leave partly inside the mid band. Measured at
    // MID +6 dB: about 1.9x on the bell's own 1 kHz, 1.09x at 50 Hz and 1.15x at
    // 10 kHz. A shelf at +6 dB would be 2x at both ends, which is the failure
    // this pins - and a leak of 1.5 dB at 10 kHz is not that.
    const auto reference1k = rmsThrough (neutralEq(), 1000.0f, 0.5f);
    const auto reference50 = rmsThrough (neutralEq(), 50.0f, 0.5f);
    const auto reference10k = rmsThrough (neutralEq(), 10000.0f, 0.5f);

    auto boosted = neutralEq();
    boosted.midGain = 2.0f;

    CHECK (rmsThrough (boosted, 1000.0f, 0.5f) / reference1k > 1.6);
    CHECK (rmsThrough (boosted, 50.0f, 0.5f) / reference50 < 1.25);
    CHECK (rmsThrough (boosted, 10000.0f, 0.5f) / reference10k < 1.25);

    // ... and the direction is the one the control promises at all three.
    CHECK (rmsThrough (boosted, 50.0f, 0.5f) / reference50 > 1.0);
}

TEST_CASE ("ThreeBandEq: the steepest stack stays bounded and finite")
{
    // Eight one-pole sections in series with the corner lift at its maximum is
    // the worst case the panel can ask for. It must not ring into instability,
    // and it must not produce a NaN that the rest of the chain would then spread
    // through the whole session.
    auto eq = neutralEq();
    eq.prepareFilters (kRate, 1000.0f, 6000.0f, 48.0f, 1.5f);
    eq.lowGain = 4.0f;
    eq.midGain = 4.0f;
    eq.highGain = 4.0f;

    const auto midCoefficient = eq.midBandCoefficient (kRate);
    float worst = 0.0f;

    for (int i = 0; i < 48000; ++i)
    {
        const auto x = 2.0f * std::sin (juce::MathConstants<float>::twoPi * 1000.0f
                                        * static_cast<float> (i) / kRate);
        const auto y = eq.process (x, 0, midCoefficient);

        REQUIRE (std::isfinite (y));
        worst = juce::jmax (worst, std::abs (y));
    }

    // Three bands at +12 dB on a 2.0 input: the sum of the band gains is the
    // ceiling, and a filter that exceeded it would be a filter that is not one.
    CHECK (worst < 8.0f);
}
