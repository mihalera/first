#!/usr/bin/env python3
"""Extracts the knob strip's arithmetic into fragments the layout harness compiles.

The strip is a chain of integer arithmetic with no renderer anywhere in it: an
editor layout becomes a grid, the grid becomes a column count, the column count
becomes a tile, and the tile becomes the disc the LookAndFeel is handed. A
replica that retypes those numbers cannot see the class of bug this guards - the
one the 780 x 664 floor shipped, where four fixed columns, a chrome constant and
a row count that each looked reasonable together put a 4 px disc on the MACHINE
page - so the harness compiles the real text instead of a copy of it.

Six fragments are cut, each verbatim and each by the text that makes it
unambiguous: the radial budget (KnobMetrics and computeKnobMetrics), the editor
layout's own body, the grid the page lays its controls in, the member band, the
strip solver, and the tile placement. They are written as separate includes
because they are statement blocks that have to land inside functions the
hand-written harness declares - the alternative is a template, and then the
compiled text is the template's and not the plugin's.

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
        # The grid a page's controls are laid out in, and the member band pinned
        # to its foot: what the strip is given to stand in.
        "grid.inc": span(source,
                         "auto grid = layout.controls.reduced (14);",
                         "grid.removeFromBottom (8);"),
        "band.inc": span(source,
                         "constexpr int memberBandHeight = 50;",
                         "? grid.getHeight() - memberBandHeight\n"
                         "                                    : grid.getHeight();"),
        # The solver and the placement: the two halves the strip is made of.
        "solver.inc": span(source,
                           "constexpr int knobTileChrome = 46;",
                           "\n    const auto knobSlotWidth = knobColumns > 0 "
                           "? grid.getWidth() / knobColumns : grid.getWidth();"),
        "placement.inc": span(source,
                              "for (int slot = 0; slot < tabControlCount; ++slot)",
                              "controls[i].setBounds (sliderBounds);\n    }"),
        # The constants the fragments above are measured from. The strip declares
        # its own knobRowGap; this copy is what the harness's own checks read, so
        # a change there cannot leave the test measuring against the old gap.
        "constants.inc": "\n".join([
            "// The divider the grid starts under, and the air between two rows of",
            "// tiles - read out of Source/ rather than repeated here.",
            f"constexpr int controlsDividerOffset = {divider};",
            f"constexpr int knobRowGap = {row_gap};",
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
