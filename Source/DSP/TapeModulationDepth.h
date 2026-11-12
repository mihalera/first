#pragma once

/*
    How DEEP the machine wanders, and how far its bias follows the speed.

    TapeModulation.h owns the four shaping curves, the speed scale and the two LFO
    rates. What stayed behind in the engine was the other half of the same idea:
    the three numbers those curves are turned into - the wow depth, the flutter
    depth and the bias scale - each of them a curve times a floor plus a slope per
    unit of speed, times the instrument's own transport scale.

    Six literals (0.05, 0.08, 0.08, 0.09, 0.84, 0.30), three lines, in the middle
    of a 1,400-line function. Named here, with the reasons, and reachable from a
    harness. The arithmetic is the shipping arithmetic: same terms, same order.

    Pure: no state, no rate, no JUCE. The caller supplies the block's curve values
    and its speed scale, exactly as TapeModulation does.
*/

namespace j37mod
{

/** The wow depth a machine has with WOW closed and the speed at nothing.

    Not zero: a real deck's capstan is never perfectly round, and that residual is
    part of what makes a tape sound like a tape at any setting. 0.05 is the floor a
    running machine keeps.
*/
inline constexpr float kWowDepthFloor = 0.05f;

/** How much the speed adds to the wow: a slower machine wanders deeper, because
    the same eccentricity takes longer to come round. 0.08 per unit of speed keeps
    the effect in the range that reads as wow rather than as vibrato. */
inline constexpr float kWowDepthPerSpeed = 0.08f;

/** The flutter depth at FLUTTER closed. Higher than the wow floor because the
    capstan's own resonance is audible even with the control down - it is the
    "stiffness" of the machine rather than its wander. */
inline constexpr float kFlutterDepthFloor = 0.08f;

/** How much the speed adds to the flutter: slightly more than it adds to the wow,
    because a faster capstan feeds its own resonance harder. */
inline constexpr float kFlutterDepthPerSpeed = 0.09f;

/** The bias scale at the slowest speed.

    Bias follows the speed - a slow machine biases less - and the scale is
    normalised so a running machine (speed 1.0) sits at 1.14, not at 1.0: this is
    the machine's own calibration curve, not a trim, and the engine's pre-emphasis
    already assumes it.
*/
inline constexpr float kSpeedBiasBase = 0.84f;

/** How much the bias scale follows the speed: 0.30 per unit, so a coasting
    machine biases down as it slows and the curve softens with it. */
inline constexpr float kSpeedBiasPerSpeed = 0.30f;

/** One block's three depths. */
struct ModulationDepths
{
    /** The wander the wow LFO is multiplied by, before the per-frame additions
        (MECHANICS, WEAR and the wear coupling) the engine makes. */
    float wowDepth = 0.0f;

    /** The wander the flutter LFO is multiplied by. */
    float flutterDepth = 0.0f;

    /** The scale on the pre-emphasis: how much the machine's bias follows its
        speed. */
    float speedBias = 1.0f;
};

/** Builds the block's three depths from the two curves, the speed scale and the
    instrument's own transport scale.

    `instrumentTransportScale` is the voicing's: some instruments are meant to be
    played with the modulation deeper than a deck-return-to-zero, and the engine
    has already decided that before it gets here. It scales both depths and nothing
    else - the bias follows the speed alone, because bias is the machine's, not the
    performance's.
*/
inline ModulationDepths makeModulationDepths (float wowCurve, float flutterCurve,
                                              float speedScale,
                                              float instrumentTransportScale) noexcept
{
    ModulationDepths depths;

    depths.wowDepth = wowCurve * (kWowDepthFloor + speedScale * kWowDepthPerSpeed)
                      * instrumentTransportScale;
    depths.flutterDepth = flutterCurve
                          * (kFlutterDepthFloor + speedScale * kFlutterDepthPerSpeed)
                          * instrumentTransportScale;
    depths.speedBias = kSpeedBiasBase + speedScale * kSpeedBiasPerSpeed;

    return depths;
}

} // namespace j37mod
