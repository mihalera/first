#pragma once

/*
    The bleed between the two record heads, as one named unit.

    A modelled deck has two tracks side by side, and the head's fringing field
    reaches the neighbouring one: each channel hears a delayed, attenuated copy of
    the OTHER track, and it hears it where the machine put it on the tape rather
    than where the user's WIDTH control later puts the image.

    The arithmetic is a single cross-feed per channel - each side pulled a fraction
    of the way toward the other side's delayed sample - plus the layout table that
    says how much and how late. Both lived inline in a 1,700-line function: the
    cross-feed under the sample loop, the table at the block-rate geometry setup
    eight hundred lines above it, with the reasoning split between them.

    They are here now, pure, so tests/transport can check the geometry directly
    instead of inferring it from a rendered stereo image. The numbers are the
    shipping numbers.
*/

#include <array>

namespace j37dsp
{

/** How much of the other track leaks in, per layout.

        2-track                0.020
        2+3 (4-track as 2)     0.010
        3-track                0.055

    From the head's fringing field: the closer the tracks, the more leaks. A
    three-track deck's tracks are narrowest and nearest, so it leaks most; the 2+3
    layout has a whole track between the two channels, so it leaks least. These are
    real crosstalk figures - about -40 to -25 dB - and the leak is deliberately
    small: it should be felt as the image sitting slightly differently, not heard.

    Index order matches the TRACKS parameter (0 = 2-track, 1 = 2+3, 2 = 3-track).
*/
inline constexpr std::array<float, 3> kTrackCrosstalk { 0.020f, 0.010f, 0.055f };

/** How long the leakage takes to arrive, per layout, in milliseconds.

        2-track                0.018
        2+3 (4-track as 2)     0.042
        3-track                0.009

    The fringing path is short but it is not instant, and on a multitrack the
    spacing between the tracks is what sets it - hence a tiny DELAY rather than an
    instantaneous blend. A fraction of a millisecond in every real layout, so it
    reads as thickening rather than as an echo, which is exactly what track-to-track
    bleed is.
*/
inline constexpr std::array<float, 3> kTrackSpacingMs { 0.018f, 0.042f, 0.009f };

/** One frame of the bleed stage, after the two tracks have been cross-fed. */
struct TrackBleedFrame
{
    float left;
    float right;
};

/** The cross-feed itself: each side moves a fraction of the way toward the other
    side's delayed sample.

    `neighbourLeft` is what the RIGHT track wrote (that is the sample reaching the
    left channel) and `neighbourRight` is what the left track wrote - the crossing
    is the whole point, so the two are named for where they LAND, not where they
    came from. Because the same fraction is used in both directions the leak is
    symmetric: the image narrows a touch and neither side gains on the other.

    A crosstalk of 0 is the identity, which is what a mono layout gets, and no
    input can leave the range its own pair spans: the result is a convex step
    toward the neighbour at every value - so an extreme neighbour cannot make it
    run away, only arrive at it.
*/
inline TrackBleedFrame trackBleedFrame (float selfLeft, float selfRight,
                                       float neighbourLeft, float neighbourRight,
                                       float crosstalk) noexcept
{
    return { selfLeft + (neighbourLeft - selfLeft) * crosstalk,
             selfRight + (neighbourRight - selfRight) * crosstalk };
}

} // namespace j37dsp
