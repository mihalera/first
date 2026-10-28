/*  ============================================================================
    The deck's vertical rhythm, solved instead of written down twice.

    WHY THIS IS A HEADER OF ITS OWN. The deck used to be a table of seven bands
    with a fixed height each, and the rows inside them were placed with their own
    literals: a 15 px caption and a 30 px control in a 32 px band. Every number
    was reasonable on its own and the sum was not - each control overhung its band
    by thirteen pixels, straight into the caption band of the row below. At the
    preferred 278 px deck that was a one- to three-pixel kiss; at the 236 px floor
    the gaps compress to their minimum and the overhang became eleven pixels, so
    the type lists were painted over the captions of the row beneath them (the
    BPM / ANTI-PHASE / SUBFUND captions the user reported as "the lists are on top
    of the labels").

    So the bands are solved from the height they are given:
      * a control row is a caption plus a control, and it is the ROW's height -
        nothing inside a row is ever taller than the row it is in;
      * when the deck is short, the caption and the control shrink together (30 ->
        24 px of control) before any gap does, because an unreadable caption is a
        worse trade than a smaller box;
      * the gaps only take what is left over, floored at two pixels, so the deck
        always fits the rectangle it was handed.

    It is JUCE-free on purpose: tests/layout compiles it and walks every deck
    height from the floor to the preferred one, asserting that no band overlaps
    the next and that no band is shorter than the content it holds. The harness
    cannot see the panel, but it can see the arithmetic that put one control on
    top of another.
  ============================================================================ */

#pragma once

#include <algorithm>
#include <array>

namespace j37panel {

struct DeckBands {
    // The deck, top to bottom. The names are the headings drawn on the panel.
    enum Band {
        heading = 0,  // "Deck" and the build id
        model = 1,    // SPEED / VINYL SPEED / BPM
        switches = 2, // INSTRUMENT, HARMONICS, SUBFUND, ANTI-PHASE
        types = 3,    // the six type selectors
        transport = 4, // the transport keys and the mode switch
        preset = 5,   // the preset chain
        badge = 6,    // the preset badge line
        bandCount = 7
    };

    // Where the first band starts, inside the deck. Six pixels of air under the
    // panel's own top edge, which is what keeps the heading off the frame - and
    // the same six at the bottom, so the badge line never lands on the hairline
    // the deck draws along its own bottom edge.
    static constexpr int topOffset = 6;
    static constexpr int bottomInset = 6;

    // What each band HOLDS, at the reference size. A control row is its caption
    // plus its control; the two are solved together below.
    static constexpr int headingHeight = 16;
    static constexpr int presetHeight = 32;
    static constexpr int badgeHeight = 14;

    // The control row, solved. 30 / 15 is the reference (the size the panel was
    // drawn at); 24 / 12 is the floor, and both move together so the row is never
    // taller than the sum of the two.
    int controlHeight = 30;
    int captionHeight = 15;
    int rowHeight = 45;

    // The gap between two bands. Compresses to minimumGap before any row does.
    int gap = 5;
    static constexpr int minimumGap = 2;
    static constexpr int maximumGap = 10;

    // The deck heights this is written for: the panel's floor and the size it
    // prefers. Between them the control row slides from its floor to its
    // reference; outside them it is clamped, so a deck taller than the preferred
    // size grows its gaps and never its rows.
    static constexpr int referenceHeight = 278;
    static constexpr int minimumHeight = 236;

    std::array<int, bandCount> top {};
    std::array<int, bandCount> height {};

    static DeckBands solve(int deckHeight) noexcept {
        DeckBands bands;

        // The row first: how much control the height can pay for. Six pixels of
        // control across the 42 px between the floor and the reference, rounded,
        // then clamped - so the reference size draws exactly the numbers the panel
        // was designed at, and the floor draws the smallest row that still holds a
        // caption and a box.
        const auto span = referenceHeight - minimumHeight;
        const auto grown = std::clamp(deckHeight - minimumHeight, 0, span);
        bands.controlHeight = 24 + (grown * 6 + span / 2) / span;
        bands.controlHeight = std::clamp(bands.controlHeight, 24, 30);
        bands.captionHeight = std::clamp(bands.controlHeight / 2, 12, 15);
        bands.rowHeight = bands.controlHeight + bands.captionHeight;

        // What the deck has to hold, ignoring the gaps.
        const auto content = headingHeight + 4 * bands.rowHeight + presetHeight + badgeHeight;

        // Then the gaps take the remainder, floored and capped. Capping them means
        // a very tall deck keeps its bands together at the top instead of pulling
        // them apart into a ladder.
        const auto spare = deckHeight - topOffset - bottomInset - content;
        bands.gap = std::clamp(spare / (bandCount - 1), minimumGap, maximumGap);

        auto cursor = topOffset;
        for (int band = 0; band < bandCount; ++band) {
            bands.top[static_cast<std::size_t>(band)] = cursor;

            const auto height = band == heading      ? headingHeight
                                : band == preset     ? presetHeight
                                : band == badge      ? badgeHeight
                                                     : bands.rowHeight;
            bands.height[static_cast<std::size_t>(band)] = height;
            cursor += height + bands.gap;
        }

        return bands;
    }

    /** The bottom edge of the last band. At or below the deck's own height, always. */
    int bottom() const noexcept { return top.back() + height.back(); }
};

} // namespace j37panel
