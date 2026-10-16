// =============================================================================
//  Unit tests for the transient designer - Source/DSP/TransientShaper.h.
//
//  The stage's header makes four claims a unit test can hold it to, and each one
//  is a claim a user would report as a bug if it stopped being true: that 0/0 is
//  bit-for-bit transparent (the A/B the panel invites), that ATTACK acts on the
//  LEADING edge only, that SUSTAIN acts on the TAIL only - with the sign the
//  name promises, which is the one the header spends a paragraph explaining - and
//  that the applied gain is bounded, so the stage can be dramatic without being
//  able to invert or blow up the signal.
//
//  The signal is a PLUCKED note rather than a step. A step is the natural way to
//  see ATTACK, but its tail is exactly zero, and a gain of four on zero is still
//  zero: the naive step signal cannot observe SUSTAIN at all. A note that decays
//  while the control acts on it can see both, and it is what the stage sees in
//  use.
// =============================================================================
#include "shim.h"

#include "DSP/TransientShaper.h"

#include <catch_amalgamated.hpp>

#include <cmath>
#include <vector>

namespace
{
constexpr float kRate = 48000.0f;

// The engine's own three poles, built the way the block-rate path builds them:
// a ~1 ms fast detector, a ~50 ms slow reference, and a ~1 ms smoother on the
// applied gain.
const float kFastCoefficient = 1.0f - std::exp (-1.0f / (kRate * 0.001f));
const float kSlowCoefficient = 1.0f - std::exp (-1.0f / (kRate * 0.050f));
const float kGainCoefficient = 1.0f - std::exp (-1.0f / (kRate * 0.001f));

constexpr int kLead = 200;          // samples of silence before the note
constexpr int kDecaySamples = 6000; // 125 ms of decaying tone
constexpr float kDecaySeconds = 0.02f;

/** A plucked note: silence, then an exponential decay, then silence.
    The decay is faster than the stage's 50 ms slow follower, which is what puts
    the tail BELOW the programme level - and a negative transient reading is the
    half SUSTAIN acts on. */
std::vector<float> pluck (float amplitude = 0.5f, int tailSamples = 1200)
{
    std::vector<float> signal (kLead, 0.0f);

    for (int i = 0; i < kDecaySamples; ++i)
        signal.push_back (amplitude * std::exp (-static_cast<float> (i)
                                                / (kRate * kDecaySeconds)));

    for (int i = 0; i < tailSamples; ++i)
        signal.push_back (0.0f);

    return signal;
}

std::vector<float> through (float attackAmount, float sustainAmount, const std::vector<float>& signal)
{
    TransientShaper shaper;
    std::vector<float> out;
    out.reserve (signal.size());

    for (const auto x : signal)
        out.push_back (shaper.process (x, attackAmount, sustainAmount,
                                       kFastCoefficient, kSlowCoefficient, kGainCoefficient));

    return out;
}

/** The largest magnitude in a window of the output, in samples from the start. */
float peakIn (const std::vector<float>& out, std::size_t from, std::size_t count)
{
    float peak = 0.0f;

    for (std::size_t i = from; i < juce::jmin (out.size(), from + count); ++i)
        peak = juce::jmax (peak, std::abs (out[i]));

    return peak;
}

/** RMS over a window, which is what "the tail" means for a note. */
float rmsIn (const std::vector<float>& out, std::size_t from, std::size_t count)
{
    float sum = 0.0f;
    const auto last = juce::jmin (out.size(), from + count);

    for (std::size_t i = from; i < last; ++i)
        sum += out[i] * out[i];

    const auto length = static_cast<float> (juce::jmax<std::size_t> (1, last - from));
    return std::sqrt (sum / length);
}

// The two windows every case below measures. The edge is the first 30 samples of
// the note, where the 1 ms detector is climbing and the 50 ms reference is still
// near the floor; the tail is 50 to 100 ms in, by which time the note has fallen
// faster than the reference has and the reading has gone negative.
constexpr std::size_t kEdgeFrom = kLead;
constexpr std::size_t kEdgeCount = 30;
constexpr std::size_t kTailFrom = kLead + 2400;
constexpr std::size_t kTailCount = 2400;
} // namespace

TEST_CASE ("TransientShaper: the edge is the first 30 samples of the note")
{
    // Sample 199 is the last of the silence and 200 the first of the note, so the
    // window below is the rising edge itself rather than the programme after it.
    // Stated once, here, because every ATTACK case reads it.
    const auto signal = pluck();
    REQUIRE (signal[kLead - 1] == 0.0f);
    CHECK (signal[kLead] == 0.5f);
}

TEST_CASE ("TransientShaper: both amounts at zero return the input bit for bit")
{
    // "At both amounts zero this returns the input unchanged, bit for bit - the
    // envelopes still run, so turning the control back up does not restart them
    // from rest, but the gain stays exactly 1 and the stage is transparent."
    // A user's A/B depends on exactly this, so it is asserted exactly.
    const auto signal = pluck();
    const auto transparent = through (0.0f, 0.0f, signal);

    for (std::size_t i = 0; i < signal.size(); ++i)
        REQUIRE (transparent[i] == signal[i]);
}

TEST_CASE ("TransientShaper: ATTACK acts on the leading edge, in both directions")
{
    const auto signal = pluck();
    const auto transparent = through (0.0f, 0.0f, signal);
    const auto sharpened = through (1.0f, 0.0f, signal);
    const auto softened = through (-1.0f, 0.0f, signal);

    const auto reference = peakIn (transparent, kEdgeFrom, kEdgeCount);

    CHECK (peakIn (sharpened, kEdgeFrom, kEdgeCount) > reference);
    CHECK (peakIn (softened, kEdgeFrom, kEdgeCount) < reference);
}

TEST_CASE ("TransientShaper: ATTACK leaves the tail where it was")
{
    // "ATTACK never touches a decay and SUSTAIN never touches an edge - which is
    // what makes the two controls mean what their names say."
    const auto signal = pluck();
    const auto transparent = through (0.0f, 0.0f, signal);
    const auto sharpened = through (1.0f, 0.0f, signal);

    CHECK (rmsIn (sharpened, kTailFrom, kTailCount)
           == Catch::Approx (rmsIn (transparent, kTailFrom, kTailCount)).epsilon (0.02));
}

TEST_CASE ("TransientShaper: SUSTAIN +1 lifts what follows the edge and -1 shortens it")
{
    // The header spends its longest paragraph on this sign, because the naive
    // implementation gets it backwards: the transient reading is NEGATIVE on a
    // tail, so the negative half has to be SUBTRACTED for "+1 lifts the tail" to
    // be true. This is the assertion that would have caught the inverted version.
    const auto signal = pluck();
    const auto transparent = through (0.0f, 0.0f, signal);
    const auto lifted = through (0.0f, 1.0f, signal);
    const auto shortened = through (0.0f, -1.0f, signal);

    const auto reference = rmsIn (transparent, kTailFrom, kTailCount);

    // The tail is the whole point: it has to be audible in the first place, or
    // the two comparisons below would both be "0 > 0".
    REQUIRE (reference > 1.0e-3f);
    CHECK (rmsIn (lifted, kTailFrom, kTailCount) > reference);
    CHECK (rmsIn (shortened, kTailFrom, kTailCount) < reference);
}

TEST_CASE ("TransientShaper: SUSTAIN leaves the edge where it was")
{
    const auto signal = pluck();
    const auto transparent = through (0.0f, 0.0f, signal);
    const auto lifted = through (0.0f, 1.0f, signal);

    CHECK (peakIn (lifted, kEdgeFrom, kEdgeCount)
           == Catch::Approx (peakIn (transparent, kEdgeFrom, kEdgeCount)).epsilon (0.02));
}

TEST_CASE ("TransientShaper: the applied gain is bounded at both ends")
{
    // "A bounded gain: +/-1 on either control is at most about 3x/+10 dB of lift
    // and about a quarter of the original on a full cut, so the stage can be
    // dramatic without ever being able to invert or blow up the signal."
    const auto signal = pluck (0.5f, 400);

    for (const auto& pair : { std::pair<float, float> { 1.0f, -1.0f },
                              std::pair<float, float> { -1.0f, 1.0f },
                              std::pair<float, float> { 1.0f, 1.0f },
                              std::pair<float, float> { -1.0f, -1.0f } })
    {
        const auto out = through (pair.first, pair.second, signal);

        for (std::size_t i = 0; i < signal.size(); ++i)
        {
            REQUIRE (std::isfinite (out[i]));
            REQUIRE (std::abs (out[i]) <= 4.0f * std::abs (signal[i]) + 1.0e-9f);
            REQUIRE (std::abs (out[i]) >= 0.25f * std::abs (signal[i]) - 1.0e-9f);
        }
    }
}

TEST_CASE ("TransientShaper: silence in, silence out, and no division by an empty envelope")
{
    TransientShaper shaper;
    float out = 1.0f;

    for (int i = 0; i < 4800; ++i)
        out = shaper.process (0.0f, 1.0f, 1.0f, kFastCoefficient, kSlowCoefficient, kGainCoefficient);

    CHECK (out == 0.0f);

    // Then a signal, from an envelope that has only ever seen zero: the
    // normalisation is floored at 1e-6 for exactly this case, and an unfloored
    // division would put a NaN here that the rest of the chain would carry.
    for (int i = 0; i < 4800; ++i)
    {
        const auto x = 0.5f * std::sin (juce::MathConstants<float>::twoPi * 440.0f
                                        * static_cast<float> (i) / kRate);
        out = shaper.process (x, 1.0f, 1.0f, kFastCoefficient, kSlowCoefficient, kGainCoefficient);
        REQUIRE (std::isfinite (out));
    }
}
