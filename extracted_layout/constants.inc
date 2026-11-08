// GENERATED FILE - do not edit. Cut verbatim out of Source/ by tests/layout/extract.py.
// The divider the grid starts under, and the air between two rows of
// tiles - read out of Source/ rather than repeated here.
constexpr int controlsDividerOffset = 92;
constexpr int knobRowGap = 8;

// The cap's divisions and the indicator's own length. Ring r is cut at
// radius * (knobGrooveFirst + r * knobGrooveStride / knobGrooveSteps),
// and the rib runs out to knobPointerLength of that same radius - both
// out of drawRotarySlider, so a knob that grew a longer indicator, or a
// face cut with a different ladder, cannot leave the checks below
// measuring the numbers this file used to carry.
constexpr float knobPointerLength = 0.72f;
constexpr int knobGrooveRings = 6;
constexpr float knobGrooveSteps = 7.0f;
constexpr float knobGrooveFirst = 0.22f;
constexpr float knobGrooveStride = 0.76f;

// 1 while the indicator bead is painted after the cap's first solid part
// (the skirt) rather than before it - see extract.py. The collar stands a
// few pixels inside the groove the bead rides, so the other order shaves
// the bead's inner half off under the knob.
constexpr int knobBeadOverCap = 1;

// The floor under every list's inset, and how many places place their
// text through comboTextInset - the deck's lists and the tab pages' are
// drawn by two look-and-feels, and both have to ask this one function.
constexpr int comboTextMinimumInset = 3;
constexpr int comboTextInsetCallSites = 2;
