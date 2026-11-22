/*  ============================================================================
    The machine-state badge: where its caption sits inside its pill.

    WHY THIS IS A HEADER OF ITS OWN. The badge is the header's only caption on a
    pill, and it was placed twice - once by the layout as a label with a left-only
    border, and once by the paint routine as a pill around that label - with the
    two insets written in different places. A left-only inset is not centred: on a
    pill a third of the header wide, the caption started 26 px in, ran its length
    and then stopped in air, which reads as a crooked line rather than a centred
    state. The lamp sits at the pill's left edge and owns that side, so the fix is
    not "centre in the whole pill" either - it is symmetric margins that clear the
    lamp.

    So the inset is stated once, as a symmetric margin, and the pill's own padding
    is stated beside it. tests/layout/deck_dust_test.cpp measures the margins, the
    centres and the lamp clearance over a range of pill widths; the old placement
    is one of the cases it must reject.
  ============================================================================ */

#pragma once

namespace j37panel {
namespace badge {

// The lamp: its centre is this far from the pill's left edge (the paint routine
// draws it at pill.x + 15, with a 3.5 px disc inside a soft glow), and it is
// drawn this big. The caption has to clear the whole disc plus a hair.
inline constexpr int lampCentreX = 15;
inline constexpr int lampRadius = 4;
inline constexpr int lampClearance = 4;

// The pill's own padding around the label, on all four sides.
inline constexpr int pillPadX = 2;
inline constexpr int pillPadY = 2;

// The caption's margin, SYMMETRIC: the lamp lives inside the left one.
inline constexpr int textInset = 28;

struct Box {
    int x = 0, y = 0, w = 0, h = 0;
};

/** The caption's box inside a pill: the pill, inset by the same margin each side. */
inline Box textArea(Box pill) noexcept {
    return {pill.x + textInset, pill.y + pillPadY, pill.w - 2 * textInset, pill.h - 2 * pillPadY};
}

/** The left edge of the lamp's circle, from the pill's left edge. */
inline int lampRight() noexcept { return lampCentreX + lampRadius; }

} // namespace badge
} // namespace j37panel
