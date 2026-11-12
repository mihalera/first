#pragma once

/*
    The frame half of the anti-phase guard: how much of the pair's energy sits in
    the DIFFERENCE (the sides opposite) rather than the SUM, and how fast the guard
    acts on it.

    The rotation itself is Source/DSP/MultibandPhaseGuard.h - per band, as an
    angle, orthogonal. What stayed inline in the tape engine was the detector and
    the engagement around it: mid/side energies, the two one-poles that smooth
    them, the normalised opposition, the bound, and the relax-when-off rule. Six
    lines of arithmetic with four bare literals (1.0e-9, 1.0e-3, 1.0e-5, 0 ... 1)
    between two long paragraphs of reasoning.

    Here they are named, with the reasoning, and reachable from tests. The
    arithmetic is the shipping arithmetic, term for term and in the same order: a
    detector that is off by a factor of two recovers the wrong amount of a
    genuinely inverted pair, and that is audible.

    Pure functions over the caller's own state: the engine keeps the two smoothed
    accumulators, these functions advance and read them. No allocation, no rate,
    no member state - safe on the audio thread and callable from a harness.
*/

namespace j37dsp
{

/** How much of the pair's energy sits in the SUM rather than the DIFFERENCE.

    Positive means the sides agree (normal stereo); negative means they oppose,
    which is the fault the guard exists for. Shifted by the magnitude below, this
    is the ratio the guard reads, so a louder or quieter programme cannot move it.
*/
inline float antiPhaseFrameEnergy (float mid, float side) noexcept
{
    return mid * mid - side * side;
}

/** The frame's total energy: the normaliser the opposition is divided by. */
inline float antiPhaseFrameMagnitude (float mid, float side) noexcept
{
    return mid * mid + side * side;
}

/** The floor on the smoothed normaliser.

    A silent passage (or a digitally exact mono fold) must not divide by nothing;
    1e-9 is small enough that any real frame dominates it and large enough that
    -product / magnitude stays finite. It is applied AFTER the one-pole, so the
    accumulator itself is never poisoned by a division.
*/
inline constexpr float kAntiPhaseMagnitudeFloor = 1.0e-9f;

/** The seed the normaliser is given once per block, before any frame has been seen.

    Larger than the per-frame floor (1e-3 rather than 1e-9) because the first
    frames of a block carry no history: a seed at the floor would let a single
    quiet frame before the first loud one normalise the opposition to 1 and slam
    the guard on. At 1e-3 the guard waits for the programme, which is what the
    smoothing is for.
*/
inline constexpr float kAntiPhaseSeedMagnitude = 1.0e-3f;

/** Below this the guard is not working at all, so the rotation is not called.

    A correction of 1e-5 is -100 dB of rotation: inaudible, and skipping it keeps
    the per-band guard out of the path for a clean frame, which is what makes a
    frame with no fault come back bit for bit.
*/
inline constexpr float kAntiPhaseEngageThreshold = 1.0e-5f;

/** One frame of the correlation envelope: a symmetric one-pole on the energy.

    Symmetric on purpose - the same coefficient for rising and falling means the
    guard cannot be made to react faster than it relaxes, so a transient that
    momentarily looks inverted cannot engage it and a real inversion cannot be
    held off by alternating material.
*/
inline float antiPhaseTrackEnergy (float product, float energy, float coefficient) noexcept
{
    return product + (energy - product) * coefficient;
}

/** The normaliser's one-pole, on the same coefficient, floored as described. */
inline float antiPhaseTrackMagnitude (float magnitude, float frameMagnitude,
                                      float coefficient) noexcept
{
    const float tracked = magnitude + (frameMagnitude - magnitude) * coefficient;

    return tracked < kAntiPhaseMagnitudeFloor ? kAntiPhaseMagnitudeFloor : tracked;
}

/** The frame's opposition, 0 ... 1.

    Only an actually negative correlation pulls the guard into action, and the
    ratio is bounded, so the guard can only ever REDUCE the opposition and never
    invert a healthy image. A positive product is 0: a frame whose energy is in the
    sum is a stereo image, not a fault.
*/
inline float antiPhaseOpposition (float product, float magnitude) noexcept
{
    if (product >= 0.0f)
        return 0.0f;

    const float ratio = magnitude < kAntiPhaseMagnitudeFloor ? kAntiPhaseMagnitudeFloor : magnitude;
    const float opposition = -product / ratio;

    return opposition > 1.0f ? 1.0f : opposition;
}

/** The engagement one-pole, with the switch.

    With the guard ON it walks toward the measured opposition; with it OFF it walks
    toward zero through the SAME pole, so turning the switch off unwinds the
    rotation smoothly instead of restoring the image in one sample - a click, which
    is the one thing this stage must never make. The detector keeps running either
    way, because the readout is the panel's warning that the sides are in
    opposition.
*/
inline float antiPhaseEngage (float correction, float opposition, float coefficient,
                              bool guardOn) noexcept
{
    const float target = guardOn ? opposition : 0.0f;

    return correction + (target - correction) * coefficient;
}

/** Whether the correction is large enough to be worth rotating the bands for. */
inline bool antiPhaseIsEngaged (float correction) noexcept
{
    return correction > kAntiPhaseEngageThreshold;
}

} // namespace j37dsp
