#pragma once

/*
    Width, in both senses the machine has one.

    The user's WIDTH control is a mid/side matrix at the very end of the chain, so
    the protection stages (limiter, soft clip) see the widened peaks rather than a
    width applied after them. The medium's own width is a different number: a
    narrower track reads less low end per channel, which is a gentle bottom-end
    tilt on the tape signal eight hundred lines earlier.

    Both were inline in the tape engine, each with a paragraph of reasoning and a
    literal nobody could check: the matrix as `0.5f * (...)` twice, the control
    travel as a bare `* 2.0f`, the tilt as `jlimit (0.75f, 1.15f, ... 0.65f)`.

    They are here now, named and pure, with the arithmetic unchanged: the shipping
    numbers, term for term and in the same order.
*/

namespace j37dsp
{

/** The WIDTH control's travel: 0 ... 2.0.

    The parameter is 0 ... 1 and 50 % is unity Side, so the control is doubled
    before it reaches the matrix: 0 % is mono, 100 % is twice the side signal. The
    parameter's own end stops are the host contract (ParameterTable.cpp); this is
    the mapping from that travel to the matrix, and it is the only place the factor
    of two appears.
*/
inline constexpr float kStereoWidthTravel = 2.0f;

/** Mid and side are HALF the sum and difference, which is what makes a width of
    1.0 the identity and keeps the fold-down (2 x mid) the right level. */
inline constexpr float kStereoWidthHalf = 0.5f;

/** The matrix coefficient for a WIDTH control reading, clamped to its travel. */
inline float stereoWidthFromControl (float widthControl) noexcept
{
    const float width = widthControl * kStereoWidthTravel;

    return width < 0.0f ? 0.0f : (width > kStereoWidthTravel ? kStereoWidthTravel : width);
}

/** One frame of the width matrix, after the extension has been applied. */
struct StereoWidthFrame
{
    float left;
    float right;
};

/** The width matrix: mid stays, side is scaled by the control.

    `width` is the matrix coefficient, not the control reading - 1.0 leaves the
    frame exactly as it arrived, 0 folds it to mono, 2 doubles the side. The mid
    component is untouched by construction, so widening never changes the mono
    fold-down on its own.
*/
inline StereoWidthFrame stereoWidthFrame (float left, float right, float width) noexcept
{
    const float mid = kStereoWidthHalf * (left + right);
    const float side = kStereoWidthHalf * (left - right);

    return { mid + side * width, mid - side * width };
}

/** The narrowest the track-width tilt may go.

    A three-track deck's tracks are 0.82 of a two-track deck's width; the tilt is
    what turns that into a bottom-end shape, and 0.75 is the floor because below it
    the tilt stops reading as "less of the medium" and starts reading as a filter.
*/
inline constexpr float kTrackWidthTiltMin = 0.75f;

/** The widest the tilt may go: the same bound in the other direction, so a layout
    wider than the reference can only ever open the bottom end gently. */
inline constexpr float kTrackWidthTiltMax = 1.15f;

/** How much of the track's width ratio the tilt follows.

    Not 1.0: the full ratio (a three-track deck is 18 % narrower) as a bottom-end
    shape is a filter, audible as tone rather than as geometry. 0.65 keeps the
    direction and the ordering of the layouts while landing the three-track deck at
    0.88 - about a third of a dB of tilt, which is the range real track width sits
    in.
*/
inline constexpr float kTrackWidthTiltDepth = 0.65f;

/** The bottom-end tilt a track layout imposes, from its width scale.

    At the two-track default (scale 1.0) this is exactly 1.0, so the machine is
    bit-for-bit what it always was on the layout everything is calibrated against.
*/
inline float trackWidthTilt (float trackWidthScale) noexcept
{
    const float tilt = 1.0f + (trackWidthScale - 1.0f) * kTrackWidthTiltDepth;

    return tilt < kTrackWidthTiltMin ? kTrackWidthTiltMin
                                     : (tilt > kTrackWidthTiltMax ? kTrackWidthTiltMax : tilt);
}

} // namespace j37dsp
