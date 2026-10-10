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

One more thing is checked rather than cut, because it cannot be compiled: the
editor's constructor is a list of calls, one per phase, and the header declares
those phases in the order it calls them. A phase the constructor stops calling
still compiles and still links - it just leaves the panel without everything
that phase built, which shows up as a knob with no attachment or a switch no
handler hears, somewhere else. So the two ends are read out of Source/ and
compared: every declared phase defined exactly once, called exactly once, in
the declared order.

Usage:  python3 tests/layout/extract.py <output-dir>
"""

from __future__ import annotations

import argparse
import pathlib
import re

REPO = pathlib.Path(__file__).resolve().parents[2]
EDITOR = REPO / "Source" / "PluginEditor.cpp"
HEADER = REPO / "Source" / "PluginEditor.h"

# The editor's constructor, declared as a list of calls - and the header block
# that declares the phases, under this title, in the order they must be called.
CONSTRUCTOR = "FirstAudioProcessorEditor::FirstAudioProcessorEditor (FirstAudioProcessor& p)"
PHASE_TITLE = "//  The constructor's phases, in the order it calls them."
PHASE_DECLARATION = re.compile(r"^\s*void (\w+)\s*\(\s*\);\s*$")
PHASE_CALL = re.compile(r"^\s*(\w+)\s*\(\s*\);\s*$", re.M)


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


def body_of(text: str, signature: str) -> str:
    """The text between the braces of `signature`'s own block.

    Skipping comments and string literals in one pass, because the block being
    read is a constructor full of both.
    """
    if text.count(signature) != 1:
        raise SystemExit(f"extract.py: `{signature}` appears {text.count(signature)} times in "
                         f"{EDITOR.name}; the layout harness cannot read its body.")

    open_at = text.index("{", text.index(signature))
    position = open_at
    depth = 0

    while position < len(text):
        c = text[position]

        if c in "\"'":
            quote = c
            position += 1
            while position < len(text) and text[position] != quote:
                position += 2 if text[position] == "\\" else 1
        elif c == "/" and position + 1 < len(text) and text[position + 1] == "/":
            end = text.find("\n", position)
            position = len(text) if end < 0 else end
        elif c == "/" and position + 1 < len(text) and text[position + 1] == "*":
            end = text.find("*/", position + 2)
            position = len(text) if end < 0 else end + 2
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return text[open_at + 1:position]

        position += 1

    raise SystemExit(f"extract.py: the block opened by `{signature}` is never closed.")


def check_constructor_phases(source: str, header: str) -> list[str]:
    """The constructor's phases, checked end to end, and returned in order.

    The header declares them under its own rule comment, in the order the
    constructor has to call them; the constructor calls one per statement. The
    compiler only sees that both ends exist, so a phase that is declared, then
    defined, then never called is a clean build and a broken panel. This is the
    one place that can see it.
    """
    if PHASE_TITLE not in header:
        raise SystemExit(f"extract.py: the header no longer carries `{PHASE_TITLE}`; the "
                         f"editor's constructor-split phases are declared there.")

    declarations: list[str] = []
    for line in header[header.index(PHASE_TITLE):].split("\n")[1:]:
        declaration = PHASE_DECLARATION.match(line)
        if declaration:
            declarations.append(declaration.group(1))
        elif declarations and not line.strip():
            break

    if len(declarations) < 10:
        raise SystemExit(f"extract.py: only {len(declarations)} constructor phases found under "
                         f"`{PHASE_TITLE}`; the editor's build is one call per phase.")

    calls = PHASE_CALL.findall(body_of(source, CONSTRUCTOR))
    problems: list[str] = []

    for name in declarations:
        if calls.count(name) == 0:
            problems.append(f"{name}() is declared but the constructor never calls it, so "
                            f"the panel is built without it")
        elif calls.count(name) > 1:
            problems.append(f"{name}() is called {calls.count(name)} times")

        definitions = len(re.findall(r"FirstAudioProcessorEditor::" + re.escape(name) + r"\s*\(\s*\)",
                                     source))
        if definitions == 0:
            problems.append(f"{name}() is declared but has no definition in {EDITOR.name}")
        elif definitions > 1:
            problems.append(f"{name}() is defined {definitions} times in {EDITOR.name}")

    for name in calls:
        if name not in declarations:
            problems.append(f"the constructor calls {name}(), which the header's phase block "
                            f"does not declare")

    if sorted(calls) == sorted(declarations) and calls != declarations:
        problems.append("the constructor calls the phases in a different order than the header "
                        "declares them, and a phase may rely on the ones before it having run")

    if problems:
        raise SystemExit("extract.py: the editor's constructor and its declared phases disagree - "
                         "the layout harness reads one from PluginEditor.cpp and the other from "
                         "PluginEditor.h:\n  - " + "\n  - ".join(problems))

    return declarations


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("output_dir", nargs="?", default="extracted_layout",
                        help="directory the cut fragments are written to "
                             "(default: ./extracted_layout)")
    arguments = parser.parse_args()

    destination = pathlib.Path(arguments.output_dir)
    destination.mkdir(parents=True, exist_ok=True)
    source = EDITOR.read_text()

    phases = check_constructor_phases(source, HEADER.read_text())
    print(f"constructor phases: {len(phases)} declared, defined once and called once, "
          f"in the declared order")

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
