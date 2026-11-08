// GENERATED FILE - do not edit. Cut verbatim out of Source/ by tests/layout/extract.py.
auto remaining = getLocalBounds().reduced (14);
    EditorLayout layout;

    // Header and deck are fixed-height, so they keep their proportions at small panel
    // sizes and on high-DPI displays. The header is 84 px - tall enough for the two
    // rows of engine switches and the machine-state readout on its own band below.
    //
    // The deck is 278 px and was 322. The forty-four came out of the machine readouts,
    // which were a three-high vertical stack pinned to the deck's right edge and are
    // now three caption-over-value pairs side by side inside the 32 px band they share
    // with OVER and INSTRUMENT. Nothing was moved off the deck to buy it - the
    // stack is the only thing that was taller than the band it was in - and the deck
    // keeps one band per group, so no two of them share a row.
    //
    // It is the same forty-four that comes off the editor's minimum height, which is
    // the difference between a window that fits a 768 px laptop and one that does
    // not. The floor is not cosmetic: it is what the host sizes the window to and
    // what the user cannot drag below.
    layout.header = remaining.removeFromTop (84);
    remaining.removeFromTop (8);

    // The deck's height is the one number in this function that was still fixed
    // when everything else had learned to flex, and it was the reason a short
    // window looked broken rather than merely tight.
    //
    // The control grid below divides whatever it is given (grid.getHeight() /
    // gridRows), so it copes with being squeezed. The deck could not: its six
    // bands sit at absolute offsets inside a rectangle of exactly 278 px, so on
    // a 1366x768 laptop - where the window has to be clamped to about 664 px to
    // stay on the screen at all - the deck kept all 278 of them and the grid got
    // what was left, roughly 258 px for three rows of knobs and a 2x2 meter
    // block. The panel did not fit because the part of it that had to give was
    // the part that could not.
    //
    // So the deck now flexes between a floor and its preferred height, and the
    // grid is guaranteed its minimum FIRST - the order matters, because taking
    // the grid's minimum off the top is what stops the two bands from both
    // ending up squeezed.
    //
    // 236 is the deck at its tightest, and it is a number rather than a guess:
    // the six bands hold 216 px of content and start 6 px down, so 222 px is
    // what they need before any gap at all. The 14 px that is left over is six
    // gaps of two - which is why the gap floor below is 2 and not 4. A floor of
    // 4 would need 246 px, and asking for 236 against it is a deck whose badge
    // gets drawn 10 px outside its own bottom edge.
    constexpr int deckPreferredHeight = 278;
    constexpr int deckMinimumHeight = 236;
    constexpr int controlsMinimumHeight = 300;
    constexpr int deckToControlsGap = 8;

    const auto availableForDeck = juce::jmax (0, remaining.getHeight() - deckToControlsGap);

    // jlimit keeps this ordered even when the window is shorter than the two
    // minimums together, which is exactly when a naive min/max would invert and
    // hand removeFromTop a negative height.
    const auto wantedDeckHeight = juce::jlimit (deckMinimumHeight, deckPreferredHeight,
                                                availableForDeck - controlsMinimumHeight);

    layout.deck = remaining.removeFromTop (juce::jmin (availableForDeck, wantedDeckHeight));
    remaining.removeFromTop (deckToControlsGap);

    // The meters panel has to hold a 2 x 2 grid of dials, so it claims a share of the
    // width rather than a fixed pixel count. That keeps both rows legible whether the
    // editor is at its minimum size or opened large.
    const auto metersWidth = juce::jlimit (320, 460, juce::roundToInt (static_cast<float> (remaining.getWidth()) * 0.30f));
    layout.meters = remaining.removeFromRight (metersWidth);
    remaining.removeFromRight (14);
    layout.controls = remaining;

    return layout;
