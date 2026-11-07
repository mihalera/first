// =============================================================================
//  How far a reading has to be off before the corrector is allowed to move it,
//  how far it may move it, and where the target sits.
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
//  So there is a DEAD ZONE: a reading within TOLERANCE of the target is not
//  corrected at all - ratio 1, bit for bit, no glide, no pull. Ten cents is the
//  default because it is under the just-noticeable difference for a trained
//  singer (about 5-10 cents) and well under the ~25 cents at which an untrained
//  listener hears "out of tune"; the control reaches 0, so the zone can be all
//  but switched off, and 50, so a wide vibrato can pass through it.
//
//  The zone is applied BEFORE TUNE AMOUNT scales the pull, not after: at half
//  amount the corrector is a gentler version of the same opinion, not a
//  differently-tuned one. And it is a zone on the READING, so the same note drifts
//  in and out of correction as it moves - which is exactly how a singer hears it.
//
//  RANGE is the grip: how many semitones, at most, the corrector may pull. One
//  semitone is the default, and it is the classic behaviour - a corrector pulls a
//  note onto the grid rather than transposing it, and a wider grip would chase a
//  reading the detector is reporting an octave away. The control reaches twelve,
//  so a deliberately heavy correction (or a scale whose nearest note is further
//  off) can travel the whole octave.
//
//  OCTAVE transposes the corrected result by whole octaves, AFTER the grip has
//  been taken. It is a separate control because it answers a separate question:
//  RANGE is how far off the target a reading may be, OCTAVE is where the target
//  sits. The result is clamped to the shifter's own 0.5..2 range, so one octave
//  either way is exact and a correction stacked on top of it gives way first.
//  TUNE AMOUNT still scales this, like every other pull: zero is off, and the
//  stage is not in the path at all.
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

    /** RANGE's own limits, in semitones, and its documented default. */
    static constexpr float minRangeSemitones = 1.0f;
    static constexpr float maxRangeSemitones = 12.0f;
    static constexpr float defaultRangeSemitones = 1.0f;

    /** OCTAVE's own limits, in whole octaves. The shifter reads between 0.5 and
        2.0, so one octave either way is the widest this stage can deliver
        without asking it for a ratio it would clamp. */
    static constexpr int minOctave = -1;
    static constexpr int maxOctave = 1;

    /** The shifter's own read range. A correction plus an octave can land
        outside it; the octave is the part that must survive, so the combined
        ratio is clamped here before it is handed on. */
    static constexpr float minRatio = 0.5f;
    static constexpr float maxRatio = 2.0f;

    /** How many cents `targetHz` is above `detectedHz` (negative = below). */
    static float cents (float detectedHz, float targetHz) noexcept
        {
            if (! std::isfinite (detectedHz) || ! std::isfinite (targetHz)
                    || detectedHz <= 1.0f || targetHz <= 1.0f)
                return 0.0f;

            // Guard against ratio overflow: clamp ratio to reasonable bounds
            constexpr float freqMaxRatio = 1e6f;
            const auto ratio = std::max (1.0f / freqMaxRatio, std::min (freqMaxRatio, targetHz / detectedHz));
            return 1200.0f * std::log2 (ratio);
        }

    /** True when the reading is close enough to the target to leave alone.

        `toleranceCents` is TOLERANCE, defaulting to the documented +-10 cents for
        callers and harnesses that ask the question without a control in hand. A
        zero tolerance still leaves a reading at exactly the target alone; it does
        not turn the dead zone off, because a corrector that pulls an in-tune note
        by a float step is still a corrector with nothing to do.

        The thousandth of a cent of slack is for the float round trip, not for the
        rule: a caller that asks for exactly ten cents has to compute a frequency
        through a log and back, and the answer lands a hair either side of the
        boundary. Without the slack the same musical distance would be corrected
        or not depending on which way the last bit fell.
    */
    static bool inDeadZone (float detectedHz, float targetHz,
                            float toleranceCents = deadZoneCents) noexcept
    {
        const auto tolerance = juce::jlimit (0.0f, 100.0f, toleranceCents);
        return std::abs (cents (detectedHz, targetHz)) <= tolerance + 1.0e-3f;
    }

    /** RANGE as the ratio it permits: one semitone -> 2^(1/12). */
    static float gripRatio (float rangeSemitones = defaultRangeSemitones) noexcept
    {
        const auto range = juce::jlimit (minRangeSemitones, maxRangeSemitones,
                                         rangeSemitones);
        return std::pow (2.0f, range / 12.0f);
    }

    /** OCTAVE as the ratio it transposes by: +1 -> 2, -1 -> 0.5. */
    static float octaveRatio (int octave = 0) noexcept
    {
            constexpr float maxRatio = 8.0f;  // +/- 3 octaves max
            const auto clampedOctave = juce::jlimit (-3, 3, octave);
            return juce::jlimit (0.125f, 8.0f, std::pow (2.0f, static_cast<float> (clampedOctave)));
        }

    /** The pitch ratio the shifter should read at.

        `amount` is TUNE AMOUNT, 0..1: it scales the DEPTH of the pull only, and
        zero is the stage's off switch - the caller does not need a separate
        boolean. The speed of the glide is SPEED's job at the call site, so half
        amount is half the pull on a note outside the zone, and inside it only
        OCTAVE can move the reading. `rangeSemitones` is RANGE and `octave` is
        OCTAVE; both default to the values that reproduce the rule that shipped
        before they became controls (a one-semitone grip, no transposition).
    */
    static float ratio (float detectedHz, float targetHz, float amount,
                        float toleranceCents = deadZoneCents,
                        float rangeSemitones = defaultRangeSemitones,
                        int octave = 0) noexcept
    {
        const auto depth = juce::jlimit (0.0f, 1.0f, amount);

        if (depth <= 0.0f || ! std::isfinite (detectedHz) || ! std::isfinite (targetHz)
            || detectedHz <= 0.0f || targetHz <= 0.0f)
            return 1.0f;

        // The dead zone leaves the reading at ratio 1 - its own note. OCTAVE is
        // still applied afterwards, because "leave the tuning human" and "play it
        // an octave up" are different requests and the second one is explicit.
        auto ideal = 1.0f;

        if (! inDeadZone (detectedHz, targetHz, toleranceCents))
        {
            const auto grip = gripRatio (rangeSemitones);
            ideal = juce::jlimit (1.0f / grip, grip, targetHz / detectedHz);
        }

        const auto transposed = juce::jlimit (minRatio, maxRatio,
                                              ideal * octaveRatio (octave));

        return 1.0f + (transposed - 1.0f) * depth;
    }
};

} // namespace j37dsp
