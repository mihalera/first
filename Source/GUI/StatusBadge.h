/*  ============================================================================
    The machine-state badge: a lamp and its caption, and nothing behind them.

    WHY THIS IS A HEADER OF ITS OWN. The badge has been wrong every time for the
    same reason: it was a PLATE, and the plate was sized by the space it had
    rather than by the words on it. First a pill expanded past the label so it
    hung off the header's own frame; then a pill a third of the header wide with
    the caption pushed 26 px into it by a left-only border; then the same wide
    pill with the caption centred in all that air. Each time the report was "the
    sign is crooked", and each time the cause was a caption sitting on a plate far
    bigger than the caption.

    There is no plate now. The badge is the lamp and the words, on the panel's own
    metal, in the header's second row where LEVELS ON/OFF ends. Its width is the
    measured text plus the lamp's reach - that is the whole of it - so it cannot be
    wider than the sign however much room the row has.

    tests/layout/deck_dust_test.cpp measures exactly that: the badge is wider than
    its caption by the lamp's reach and by nothing else, the lamp does not reach
    the first glyph, and the plate this replaced fails both checks.
  ============================================================================ */

#pragma once

namespace j37panel {
namespace badge {

// The lamp's solid disc, and the bloom it draws around itself. The bloom is what
// `lampGlow` of overhang is for: it is counted in the badge's reach so the
// invalidation rectangle cannot crop it.
inline constexpr int lampRadius = 4;
inline constexpr int lampGlow = 2;

// Between the lamp's disc and the first glyph.
inline constexpr int lampGap = 8;

// Everything the badge adds to the left of a caption: bloom, lamp, gap.
inline constexpr int lampReach = lampGlow + 2 * lampRadius + lampGap;

struct Box {
    int x = 0, y = 0, w = 0, h = 0;
};

/** The badge around a caption box: the caption, plus the lamp's reach on the left.

    No padding on the other three sides, because there is no plate to pad: the
    background behind the words is the panel itself.
*/
inline Box badgeForCaption(int captionX, int captionY, int captionWidth,
                           int captionHeight) noexcept {
    return {captionX - lampReach, captionY, captionWidth + lampReach, captionHeight};
}

/** The lamp's centre, given the badge's left edge. */
inline int lampCentreX(Box badge) noexcept { return badge.x + lampGlow + lampRadius; }

/** Where the lamp's disc ends: the caption has to start at least `lampGap` later. */
inline int lampDiscRight(Box badge) noexcept { return badge.x + lampGlow + 2 * lampRadius; }

} // namespace badge
} // namespace j37panel
