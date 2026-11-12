#pragma once

/*
    The per-frame modulation multipliers: how much the record head's rate is moved
    this sample, and by which of the machine's three shortcomings.

    TapeModulationDepth.h says how deep each mechanism is; this turns those depths
    into the three gains that are actually applied. Three expressions, seven bare
    literals, in the middle of the per-sample loop:

        irregularDepth  = wowDepth + mechanics*0.006 + wear*0.003 + wearWowCoupling
        irregularOffset = drift * mechanics * 0.010
        wowMod          = (1 + wowLfo*wowDepth' + offset) * headGapMask * speedError
        flutterMod      = (1 + flutterLfo*flutterDepth*flutterScale) * headGapMask * speedError
        grainMod        = 1 + tapeHiss*0.10 * activityGate * grainLfo

    Named and pure here, with the reasoning the engine carried in comment blocks
    above them. The arithmetic is the shipping arithmetic: same terms, same order,
    same association - these multiply a delay line's rate, so a reordered term is a
    different machine.

    Everything is a pure function of the block's constants and the frame's LFO
    values: no state, no rate, no allocation.
*/

namespace j37mod
{

/** How much MECHANICS adds to the wander depth.

    MECHANICS is the transport - dry bearings, a slack belt - and it makes the wow
    that already exists less even rather than adding a second oscillator. 0.006 per
    unit keeps it a texture on top of the control's own depth: at full MECHANICS it
    is a little over half of WOW's floor, which is the range where it reads as a
    tired machine rather than as a broken one.
*/
inline constexpr float kMechanicsIrregularDepth = 0.006f;

/** How much WEAR adds to the wander depth.

    Half of MECHANICS on purpose. WEAR is the medium - patchy contact against the
    head - and the same wear that thickens the wander also dulls it, so it must not
    be the same size as the transport's own contribution or the two stop being
    distinguishable.
*/
inline constexpr float kWearIrregularDepth = 0.003f;

/** The mechanical drift's share of the wander, per unit of the drift state.

    The drift is a slow random walk the machine already keeps per channel, and this
    is how far it moves the head's rate: 0.010 keeps it under the periodic wow, so
    a drifting machine sounds loose rather than out of tune.
*/
inline constexpr float kDriftIrregularOffset = 0.010f;

/** How much the tape's own hiss modulates the grain.

    The grain is the head's contact noise made audible as a rate wobble: 0.10 of
    the hiss amount, gated by the transport's activity so a stopped machine does
    not wobble silently.
*/
inline constexpr float kGrainPerHiss = 0.10f;

/** The wander depth the wow LFO is multiplied by, after MECHANICS and WEAR.

    `wowDepth` is already the control's own depth (TapeModulationDepth.h);
    `wearWowCoupling` is the part the medium and the transport own between them, so
    a worn machine wanders even with WOW closed.
*/
inline float irregularDepth (float wowDepth, float mechanics, float wear,
                            float wearWowCoupling) noexcept
{
    return wowDepth + mechanics * kMechanicsIrregularDepth + wear * kWearIrregularDepth
           + wearWowCoupling;
}

/** The wandering DC the mechanical drift puts on the wow, this frame. */
inline float irregularOffset (float driftState, float mechanics) noexcept
{
    return driftState * mechanics * kDriftIrregularOffset;
}

/** How far the head's rate is moved by the wow, this frame.

    `headGapMask` is the dark-head damping that softens the pitch shimmer at the top
    of the TONE travel before it softens anything else, and `speedError` is the
    transport's own frame: START deepens the modulation and runs the whole wet path
    flat until the machine reaches speed, which is what turns the ramp into a pitch
    ramp rather than a level fade.
*/
inline float wowModulation (float wowLfo, float depth, float offset,
                            float headGapMask, float speedError) noexcept
{
    return (1.0f + wowLfo * depth + offset) * headGapMask * speedError;
}

/** How far the flutter moves it, this frame.

    `flutterScale` is the smoothed head-gap scale on the flutter alone - the same
    TONE macro that damps the shimmer damps the flutter, which is why it is a
    separate input rather than folded into the mask.
*/
inline float flutterModulation (float flutterLfo, float flutterDepth, float flutterScale,
                                float headGapMask, float speedError) noexcept
{
    return (1.0f + flutterLfo * flutterDepth * flutterScale) * headGapMask * speedError;
}

/** The grain: the head's contact noise as a rate wobble, gated by transport
    activity so a stopped machine does not wobble into silence. */
inline float grainModulation (float tapeHiss, float activityGate, float grainLfo) noexcept
{
    return 1.0f + tapeHiss * kGrainPerHiss * activityGate * grainLfo;
}

/** The three multipliers a frame applies to the head's rate. */
struct ModulationFrame
{
    float wow;
    float flutter;
    float grain;
};

} // namespace j37mod
