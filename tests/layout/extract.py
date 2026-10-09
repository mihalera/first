#!/usr/bin/env python3
"""Extracts the knob strip's arithmetic into fragments the layout harness compiles.

The strip is a chain of integer arithmetic with no renderer anywhere in it: an
editor layout becomes a grid, the grid becomes a column count, the column count
becomes a tile, and the tile becomes the disc the LookAndFeel is handed. A
replica that retypes those numbers cannot see the class of bug this guards - the
one the 780 x 664 floor shipped, where four fixed columns, a chrome constant and
a row count that each looked reasonable together put a 4 px disc on the MACHINE
page - so the harness compiles the real text instead of a copy of it.

Eight fragments are cut, each verbatim and each by the text that makes it
unambiguous: the radial budget (KnobMetrics and computeKnobMetrics), the editor
layout's own body, the grid the page lays its controls in, the knob count the
strip is handed, the member band, the strip solver, the member band's top edge,
the tile placement, the cap's own divisions with the length of the indicator that
runs among them and the order it is painted in, and the inset every list's value
text starts at. They are written as
separate includes because they are statement blocks that have to land inside
functions the hand-written harness declares - the alternative is a template, and
then the compiled text is the template's and not the plugin's.

The fragments are cut in the order the plugin declares them, because that is
the order the harness has to compile them in: the knob count is what the band
and the solver are measured from, and the solver is what the placement walks.

Usage:  python3 tests/layout/extract.py <output-dir>
"""

from __future__ import annotations

import argparse
import pathlib
import re

REPO = pathlib.Path(__file__).resolve().parents[2]
EDITOR = REPO / "Source" / "PluginEditor.cpp"


def brace_block(text: str, anchor: str) -> str:
    """Cuts `anchor` out of `text` through the brace that closes its own block."""
    if text.count(anchor) != 1:
        raise SystemExit(f"extract.py: `{anchor}` appears {text.count(anchor)} times "
                         f"in {EDITOR.name}; the layout harness cannot tell them apart.")
    start = text.index(anchor)
    position = text.index("{", start)
    depth = 0
    while True:
        if text[position] == "{":
            depth += 1
        elif text[position] == "}":
            depth -= 1
            if depth == 0:
                break
        position += 1
    return text[start:position + 1]


def span(text: str, start: str, end: str) -> str:
    """Cuts the text between two anchors, both included."""
    if text.count(start) != 1:
        raise SystemExit(f"extract.py: `{start}` appears {text.count(start)} times "
                         f"in {EDITOR.name}; the layout harness cannot tell them apart.")
    first = text.index(start)
    last = text.index(end, first) + len(end)
    return text[first:last]


def constant(text: str, pattern: str, name: str) -> str:
    match = re.search(pattern, text)
    if match is None:
        raise SystemExit(f"extract.py: no `{name}` found in {EDITOR.name}. The layout "
                         f"harness measures the strip against it.")
    return match.group(1)


def ladder(text: str, pattern: str, name: str) -> list[str]:
    """The numbers `pattern` captures, from the one place in `text` it matches."""
    matches = re.findall(pattern, text)
    if len(matches) != 1:
        raise SystemExit(f"extract.py: `{name}` matches {len(matches)} places in "
                         f"{EDITOR.name}; the layout harness cannot tell them apart.")
    found = matches[0]
    return list(found) if isinstance(found, tuple) else [found]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("output_dir", nargs="?", default="extracted_layout",
                        help="directory the cut fragments are written to "
                             "(default: ./extracted_layout)")
    arguments = parser.parse_args()

    destination = pathlib.Path(arguments.output_dir)
    destination.mkdir(parents=True, exist_ok=True)
    source = EDITOR.read_text()

    divider = constant(source, r"controlsDividerOffset = (\d+);", "controlsDividerOffset")
    row_gap = constant(source, r"constexpr int knobRowGap = (\d+);", "knobRowGap")

    # The cap's own turning rings and how far the indicator runs among them. The
    # complaint this measures is a tip that lands ON a division, so neither
    # number can be restated in the harness: both are read out of the paint
    # routine, and a redrawn ladder moves the check with it.
    pointer_length = ladder(source, r"const auto pointerLength = radius \* ([0-9.]+)f;",
                            "the indicator's length")[0]
    groove_rings = ladder(source, r"for \(int ring = 1; ring <= (\d+); \+\+ring\)",
                          "the turning rings")[0]
    groove_steps = ladder(source,
                          r"const auto t = static_cast<float> \(ring\) / ([0-9.]+)f;",
                          "the turning rings' step")[0]
    groove_first, groove_stride = ladder(
        source, r"const auto ringRadius = radius \* \(([0-9.]+)f \+ t \* ([0-9.]+)f\);",
        "the turning rings' span")

    # Where the bead's paint sits in the routine. It is drawn LAST on purpose: the
    # collar stands a few pixels inside the groove the bead rides, so a bead
    # painted before the cap comes out with its inner half shaved off - a lamp
    # under the knob, which is what it looked like. That is a property of the
    # routine's SEQUENCE rather than of any number in it, so it is the one thing
    # here that is measured from the order the two blocks appear in.
    bead_at = source.find("g.fillEllipse (tracer.x")
    cap_at = source.find("const auto skirt = radius")
    if bead_at < 0 or cap_at < 0:
        raise SystemExit(f"extract.py: the indicator bead's paint and the cap's "
                         f"first solid part are not both in {EDITOR.name}. The "
                         f"layout harness measures their order in drawRotarySlider.")
    bead_over_cap = 1 if bead_at > cap_at else 0

    # The lists' text inset. The floor is a requirement rather than a proportion,
    # so it is read out with the function, and the CALL SITES are counted: the two
    # list styles are drawn by two look-and-feels, and "one of them was padded and
    # the other was walled" is exactly the state this guards against.
    combo_floor = constant(source, r"constexpr int comboTextMinimumInset = (\d+);",
                           "comboTextMinimumInset")
    combo_calls = source.count("comboTextInset (box.getHeight(), panelHeight)")

    fragments = {
        # The radial budget: one pure function of the paint area and the camera.
        "metrics.inc": "\n".join([
            brace_block(source, "struct KnobMetrics") + ";",
            "",
            brace_block(source, "KnobMetrics computeKnobMetrics"),
        ]),
        # The editor layout, from the outer padding to the four bands it returns.
        "editor_layout.inc": span(source,
                                  "auto remaining = getLocalBounds().reduced (14);",
                                  "return layout;"),
        # The grid a page's controls are laid out in - the strip's own frame.
        "grid.inc": span(source,
                         "auto grid = layout.controls.reduced (14);",
                         "grid.removeFromBottom (8);"),
        "band.inc": span(source,
                         "const auto knobAreaHeight = (memberRows != 0 && knobCount > 0)",
                         ": grid.getHeight();"),
        # How many knobs the live page has to place, and which list they come
        # from: the COMP page's own six on the one page the tab table cannot
        # describe, the table's count everywhere else.
        "knob_count.inc": span(source,
                               "const auto knobCount = compressorTab ?",
                               ": tabControlCount;"),
        # The solver and the placement: the two halves the strip is made of.
        "solver.inc": span(source,
                           "constexpr int knobTileChrome = 46;",
                           "\n    const auto knobSlotWidth = knobColumns > 0 "
                           "? grid.getWidth() / knobColumns : grid.getWidth();"),
        # The member band's top edge - the strip the knobs must stay above.
        "member_row.inc": span(source,
                               "const auto memberRowY = (memberRows != 0 && knobCount > 0)",
                               ": grid.getY();"),
        # The band's own height, read out of Source/ rather than repeated in the
        # harness: the strip is measured against it, so a change there cannot
        # leave the test measuring against the old number.
        "band_height.inc": "constexpr int memberBandHeight = "
                           + constant(source, r"constexpr int memberBandHeight = (\d+);",
                                      "memberBandHeight") + ";",
        "placement.inc": span(source,
                              "for (int slot = 0; slot < knobCount; ++slot)",
                              "control.setBounds (sliderBounds);\n    }"),
        # Where a list's value text starts, as a pure function of the field it is
        # written in and the panel it stands on - so the padding every list on the
        # panel uses can be measured at every window size without a window.
        "combo_text.inc": brace_block(source, "int comboTextInset"),
        # The constants the fragments above are measured from. The strip declares
        # its own knobRowGap; this copy is what the harness's own checks read, so
        # a change there cannot leave the test measuring against the old gap.
        "constants.inc": "\n".join([
            "// The divider the grid starts under, and the air between two rows of",
            "// tiles - read out of Source/ rather than repeated here.",
            f"constexpr int controlsDividerOffset = {divider};",
            f"constexpr int knobRowGap = {row_gap};",
            "",
            "// The cap's divisions and the indicator's own length. Ring r is cut at",
            "// radius * (knobGrooveFirst + r * knobGrooveStride / knobGrooveSteps),",
            "// and the rib runs out to knobPointerLength of that same radius - both",
            "// out of drawRotarySlider, so a knob that grew a longer indicator, or a",
            "// face cut with a different ladder, cannot leave the checks below",
            "// measuring the numbers this file used to carry.",
            f"constexpr float knobPointerLength = {pointer_length}f;",
            f"constexpr int knobGrooveRings = {groove_rings};",
            f"constexpr float knobGrooveSteps = {groove_steps}f;",
            f"constexpr float knobGrooveFirst = {groove_first}f;",
            f"constexpr float knobGrooveStride = {groove_stride}f;",
            "",
            "// 1 while the indicator bead is painted after the cap's first solid part",
            "// (the skirt) rather than before it - see extract.py. The collar stands a",
            "// few pixels inside the groove the bead rides, so the other order shaves",
            "// the bead's inner half off under the knob.",
            f"constexpr int knobBeadOverCap = {bead_over_cap};",
            "",
            "// The floor under every list's inset, and how many places place their",
            "// text through comboTextInset - the deck's lists and the tab pages' are",
            "// drawn by two look-and-feels, and both have to ask this one function.",
            f"constexpr int comboTextMinimumInset = {combo_floor};",
            f"constexpr int comboTextInsetCallSites = {combo_calls};",
        ]),
    }

    for name, text in fragments.items():
        (destination / name).write_text(
            f"// GENERATED FILE - do not edit. Cut verbatim out of Source/ by "
            f"tests/layout/extract.py.\n{text}\n")
        print(f"wrote {destination / name} ({len(text.splitlines())} lines)")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
