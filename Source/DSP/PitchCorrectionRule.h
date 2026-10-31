// =============================================================================
//  How far a reading has to be off before the corrector is allowed to move it.
//
//  The engine's autotune hands this file the note the detector heard and the note
//  the scale says it should be, and takes back the ratio the shifter should read
//  at. Until this header existed that was one line -
//
//      ratio = 1 + (jlimit (0.94, 1.06, target / detected) - 1) * amount
//
//  - which pulls EVERY reading, including one that is already within a few cents.
//  That is what makes a corrector sound like a corrector: a voice is never
//  exactly on the grid, and a stage that never lets it be slightly sharp or flat
//  removes the part of the performance the ear reads as human.
//
//  So there is a DEAD ZONE: a reading within +-10 cents of the target is not
//  corrected at all - ratio 1, bit for bit, no glide, no pull. Ten cents is under
//  the just-noticeable difference for a trained singer (about 5-10 cents) and
//  well under the ~25 cents at which an untrained listener hears "out of tune",
//  so what is inside the zone is character and what is outside it is the error
//  the control is for.
//
//  The zone is applied BEFORE TUNE AMOUNT scales the pull, not after: at half
//  amount the corrector is a gentler version of the same opinion, not a
//  differently-tuned one. And it is a zone on the READING, so the same note drifts
//  in and out of correction as it moves - which is exactly how a singer hears it.
//
//  Outside the zone the arithmetic is the one that shipped: the grip is clamped
//  to a semitone either way (+-6 %), because a corrector pulls a note onto the
//  grid rather than transposing it, and a wider grip would chase a reading the
//  detector is reporting an octave away.
//
//  Pinned by tests/pitch, which drives the rule at exact cent values either side
//  of the boundary - the dead zone is a claim about a number, and a claim about a
//  number belongs in a harness rather than in a comment.
// =============================================================================
#pragma once

#include <cmath>

namespace j37dsp
{

struct PitchCorrectionRule
{
    /** The band around the target in which nothing is corrected. */
    static constexpr float deadZoneCents = 10.0f;

    /** A semitone either way, as a ratio. The shifter's own clamp is wider
        (0.5..2); this is the corrector's grip, not the shifter's range. */
    static constexpr float maxRatio = 1.06f;
    static constexpr float minRatio = 0.94f;

    /** How many cents `targetHz` is above `detectedHz` (negative = below). */
    static float cents (float detectedHz, float targetHz) noexcept
    {
        if (! std::isfinite (detectedHz) || ! std::isfinite (targetHz)
            || detectedHz <= 0.0f || targetHz <= 0.0f)
            return 0.0f;

        return 1200.0f * std::log2 (targetHz / detectedHz);
    }

    /** True when the reading is close enough to the target to leave alone.

        The thousandth of a cent of slack is for the float round trip, not for the
        rule: a caller that asks for exactly ten cents has to compute a frequency
        through a log and back, and the answer lands a hair either side of the
        boundary. Without the slack the same musical distance would be corrected
        or not depending on which way the last bit fell.
    */
    static bool inDeadZone (float detectedHz, float targetHz) noexcept
    {
        return std::abs (cents (detectedHz, targetHz)) <= deadZoneCents + 1.0e-3f;
    }

    /** The pitch ratio the shifter should read at.

        `amount` is TUNE AMOUNT, 0..1: it scales the DEPTH of the pull as well as
        the speed of the glide at the call site, so half amount is half the pull
        on a note that is outside the zone and no pull at all on one inside it.
    */
    static float ratio (float detectedHz, float targetHz, float amount) noexcept
    {
        const auto depth = juce::jlimit (0.0f, 1.0f, amount);

        if (depth <= 0.0f || ! std::isfinite (detectedHz) || ! std::isfinite (targetHz)
            || detectedHz <= 0.0f || targetHz <= 0.0f)
            return 1.0f;

        if (inDeadZone (detectedHz, targetHz))
            return 1.0f;

        const auto ideal = juce::jlimit (minRatio, maxRatio, targetHz / detectedHz);

        return 1.0f + (ideal - 1.0f) * depth;
    }
};

} // namespace j37dsp
