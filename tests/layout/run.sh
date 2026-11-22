#!/usr/bin/env bash
#
# Builds and runs the knob layout harness.
#
#   tests/layout/run.sh
#
# The harness has no JUCE, no window and no host dependency: it cuts the strip's
# arithmetic out of Source/PluginEditor.cpp, compiles it against a stub of the
# toolkit it binds to, and walks every page at four panel sizes. That makes it
# seconds of work on any machine with a C++17 compiler, so a change to the knob
# geometry can be measured on every pull request instead of only where the
# plugin is built - and measured as the disc the paint routine will draw, not as
# the numbers a comment claims.
#
# The same run also checks the editor's constructor against the phases the header
# declares: every phase defined once, called once, in the declared order. Both
# halves of a split whose failure is a control that quietly never gets built - see
# the note in extract.py - and this is the only job that sees it.
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/../lib/harness.sh"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$(j37_build j37-layout-test)"

mkdir -p "${BUILD}"

python3 "${HERE}/extract.py" "${BUILD}"

# J37_SANITIZE=address,undefined puts the harness, and the layout arithmetic it
# compiles out of Source/, under those sanitizers - see the sanitizer step in
# build-linux-debug.

"${CXX}" "${J37_CXX_STD}" -O2 "${J37_WARNINGS[@]}" \
    "${sanitize_flags[@]}" \
    -I "${HERE}" -I "$(j37_path source)" -I "${BUILD}" \
    "${HERE}/layout_test.cpp" -o "${BUILD}/layout_test"

"${BUILD}/layout_test"

# ---------------------------------------------------------------------------
#  The page chips' captions. The knob harness above measures the grid; this one
#  measures the bar above it: for every page and every supported panel width, the
#  caption has to fit the button the bar gives it. It exists because the paint
#  routine clamped its shrink and then clipped the overflow, which no check could
#  see - a clipped caption is neither a crash nor a warning.
# ---------------------------------------------------------------------------
SRC="$(j37_path source)"
"${CXX}" "${J37_CXX_STD}" -O1 "${J37_WARNINGS[@]}" -I "${SRC}"     "${HERE}/chip_captions.cpp" -o "${BUILD}/chip_captions"
"${BUILD}/chip_captions"

# A geometry check cannot see which font a button carries, so the two lines that
# make the geometry true are pinned too: the bar sizes each caption, and the paint
# ellipsises instead of clipping. Removing either one puts the cut captions back.
grep -q "fittedChipCaptionHeight" "${SRC}/GUI/J37LookAndFeel.cpp" || {
    echo "the paint no longer fits a chip caption to its button (see Source/GUI/ChipCaption.h)" >&2
    exit 1
}
grep -q "chipCaptionComponentId" "${SRC}/PluginEditor.cpp" || {
    echo "the page bar no longer tags its chips, so their captions are not fitted" >&2
    exit 1
}
if grep -q "Justification::centred, false" "${SRC}/GUI/J37LookAndFeel.cpp"; then
    echo "a button caption can be clipped again: an ellipsise-if-too-big is off" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
#  The deck's dust and the header's state badge. Both were fixed by eye once -
#  a corridor that was an 86 x 14 px strip at the right end of the badge line,
#  and a caption pushed 26 px into its pill by a left-only border - and neither
#  could be measured, because the dust was Box2D bodies and the inset was written
#  inline in the layout. They live in Source/GUI/DeckDust.h and
#  Source/GUI/StatusBadge.h now, and this runs them: thirty seconds of animation
#  at the timer's rate, mapped with the same functions paint() calls, plus the
#  margins and lamp clearance of the caption box.
# ---------------------------------------------------------------------------
"${CXX}" "${J37_CXX_STD}" -O1 "${J37_WARNINGS[@]}" -I "${SRC}" \
    "${HERE}/deck_dust_test.cpp" -o "${BUILD}/deck_dust_test"
"${BUILD}/deck_dust_test"

# A measurement of a header nobody uses is a measurement of nothing, so the four
# call sites that make these the panel's own arithmetic are pinned: the deck's
# corridor comes from the header, the paint routine maps with it, the invalidator
# dirties its rectangles, and the caption's box comes from the badge's.
grep -q "j37panel::dust::corridorForDeck" "${SRC}/PluginEditor.cpp" || {
    echo "placeDeck no longer takes the dust's corridor from GUI/DeckDust.h" >&2
    exit 1
}
grep -q "j37panel::dust::screenX" "${SRC}/GUI/EditorPaint.cpp" || {
    echo "the paint routine no longer maps the motes with GUI/DeckDust.h" >&2
    exit 1
}
grep -q "j37panel::dust::orbInvalidation" "${SRC}/GUI/EditorTimer.cpp" || {
    echo "the dust invalidator no longer uses the rect the paint routine draws" >&2
    exit 1
}
grep -A4 "captionBox = j37panel::badge::textArea" "${SRC}/PluginEditor.cpp" \
    | grep -q "Justification::centred" || {
    echo "the state caption is no longer centred in the box GUI/StatusBadge.h gives it" >&2
    exit 1
}
