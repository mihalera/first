#!/usr/bin/env python3
"""Splits FirstAudioProcessorEditor::resized() into one function per band.

resized() was a 1,561-line function: the header's grid, the deck's seven bands, the
tab bar, the knob strip's solve, the member band, the visibility pass, the meters and
the DEBUG overlap sweep, all in one body whose locals every one of them could see.
This cuts it into the bands the panel is actually made of.

Every band is a PURE MOVE: the statements are the same statements, in the same order,
cut out by matching their exact text and written into the new function's body. What
changes is only what has to - a band that read `layout.header` is handed its own
rectangle and reads `header`, a band that measured in the strip's locals is handed the
geometry the strip solved, and the member band's own cell arithmetic, which was a
lambda inside resized(), becomes the member function the panel's comments already call
placeDeckSwitch. A typo in any anchor fails the cut rather than changing the panel.

The layout harness cuts seven fragments out of the text this moves (the editor layout,
the grid, the band, the knob count, the solver, the member row and the placement) and
none of them is retyped here - so nothing this script does can weaken it. That is
checked rather than claimed: run tests/layout/extract.py before and after and diff the
generated .inc files.

Usage:  python3 tools/split_editor_resized.py [--check]
"""

from __future__ import annotations

import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parents[1]
EDITOR = REPO / "Source" / "PluginEditor.cpp"
HEADER = REPO / "Source" / "PluginEditor.h"

# The bands, each as the exact text that opens and closes it. Both anchors have to
# occur exactly once in the file being cut, so a band that has drifted fails here
# instead of being cut in the wrong place.
BANDS = {
    # The header's captions and its three-row switch grid.
    "header": (
        "    // Header captions: the small brand strip sits ABOVE the big title",
        "    statusLabel.setBorderSize (juce::BorderSize<int> (0, 26, 0, 0));",
    ),
    # The deck, part one: its band table and rows, up to the preset badge line.
    "deck.bands": (
        "    // ------------------------------------------------------------------\n"
        "    //  The deck.\n",
        "                                             static_cast<float> "
        "(presetBadgeLabel.getWidth()) - 4.0f));",
    ),
    # The controls panel's own heading, hint and section caption. It sits between the
    # deck's own blocks in the old body, and belongs to the controls panel.
    "controls.heading": (
        "    controlsHeadingLabel.setBounds (layout.controls.getX() + 18",
        "                                   juce::jmax (0, controlsHintLeft "
        "- controlsHeadingLabel.getRight() - 24),\n"
        "                                   17);",
    ),
    # The deck, part two: the window the 3D transport is framed in. It shares
    # reelCorridor with the rows above, so it is the same function.
    "deck.scene": (
        "    // The 3D transport's window: the deck's top-right block, which is the one",
        "                         reelCorridor - 6,\n"
        "                         58);",
    ),
    # The two rows of tabs-inside-tabs and the family buttons above them.
    "tabbar": (
        "    // ------------------------------------------------------------------\n"
        "    //  The tab bar.\n",
        "    tabFamilySetupButton.setVisible (true);",
    ),
    # The meters panel's four captions and the compressor's own readout pair.
    "meters.labels": (
        "    metersHeadingLabel.setBounds (layout.meters.getX() + 16,",
        "                                 layout.meters.getY() + 27, "
        "compressorLabelWidth, 14);",
    ),
    # The knob strip: the grid, the live page's flags, the solver, the placement loop
    # and the member band's top edge. The one band that RETURNS something.
    "strip": (
        "    // The grid starts below the divider paint() drew, with a little air under it. The",
        "    const auto memberRowY = (memberRows != 0 && knobCount > 0)\n"
        "                                ? grid.getBottom() - memberBandHeight\n"
        "                                : grid.getY();",
    ),
    # The member band's own cell arithmetic, which was the placeDeckSwitch lambda.
    "member": (
        "    const auto placeDeckSwitch = [&] (juce::Label& label, juce::Component& box,",
        "        box.setBounds (cell.getX() + 12, bandY,\n"
        "                       cell.getWidth() - 24, bandHeight);\n"
        "    };",
    ),
    # Which tab members exist - re-asserted on every resize.
    "visibility": (
        "    // Visibility for the deck switches is re-asserted here rather than only in",
        "    mixBar.setVisible (mixTab);",
    ),
    # Where the live page's members are, and the mix bar's own strip.
    "members": (
        "    if (mixTab)\n    {",
        "        else\n"
        "        {\n"
        "            placeEqCorner (outputEqHpButton, 1);\n"
        "            placeEqCorner (outputEqLpButton, 2);\n"
        "        }\n"
        "    }",
    ),
    # The meters themselves: four dials in a 2 x 2 grid inside the meters panel.
    "meters.dials": (
        "    // Four meters in a 2 x 2 grid inside the meters panel:",
        "    compressorMeterOut.setBounds (rightColumn, bottomRow, columnWidth, "
        "meterRowHeight);",
    ),
}

# The DEBUG overlap sweep, which is the tail of resized() rather than a band: it runs
# last, on the bounds every band above has just written.
OVERLAP_START = "#if DEBUG\n    // ------------------------------------------------------------------\n    //  The overlap check."

CALLS = """    placeHeader (layout.header);
    placeDeck (layout.deck);
    placeControls (layout);
    placeTabBar (layout.controls);

    const auto geometry = placeControlStrip (layout);
    updateTabVisibility (geometry);
    placeTabMembers (geometry);
    placeMeters (layout);

    verifyPanelHasNoOverlaps();
"""

# The flags the strip solves and the bands after it read, in the order the geometry
# struct declares them.
TAB_FLAGS = ("settingsTab", "autotuneTab", "delayTab", "characterTab", "frontEndTab",
             "shapersTab", "inEqTab", "outEqTab", "machineTab", "mixTab", "compressorTab")

GEOMETRY_FILL = "\n".join(
    ["", "    ControlGeometry geometry;", "    geometry.grid = grid;",
     "    geometry.cellWidth = cellWidth;", "    geometry.memberRowY = memberRowY;"]
    + [f"    geometry.{flag} = {flag};" for flag in TAB_FLAGS]
    + ["", "    return geometry;"]
)


def cut(text: str, band: str, start: str, end: str) -> tuple[str, str]:
    """Removes `band` from `text` and returns the remainder and the band itself."""
    for anchor, label in ((start, "opening"), (end, "closing")):
        if text.count(anchor) != 1:
            raise SystemExit(f"split_editor_resized: the {label} anchor of the `{band}` band "
                             f"appears {text.count(anchor)} times in {EDITOR.name}; the cut "
                             f"cannot be made:\n{anchor}")

    first = text.index(start)
    last = text.index(end, first) + len(end)
    return text[:first] + text[last:], text[first:last]


def renamed(text: str, pairs: list[tuple[str, str]]) -> str:
    """`text` with each (pattern, replacement) applied to its CODE lines.

    A whole-line comment is left alone: the prose in these bands names the same
    locals the code does, and a comment that said `geometry.grid` instead of `grid`
    would be spelling the argument rather than the thing it describes.
    """
    lines = []

    for line in text.split("\n"):
        if not line.lstrip().startswith("//"):
            for pattern, replacement in pairs:
                line = re.sub(pattern, replacement, line)
        lines.append(line)

    return "\n".join(lines)


def main() -> int:
    source = EDITOR.read_text()
    header = HEADER.read_text()

    for name, (start, end) in BANDS.items():
        if start not in source:
            raise SystemExit(f"split_editor_resized: the `{name}` band's opening anchor is "
                             f"not in {EDITOR.name} any more:\n{start}")

    # The overlap sweep is the tail: it runs to the file's last `}`.
    if source.count(OVERLAP_START) != 1:
        raise SystemExit("split_editor_resized: the DEBUG overlap sweep's banner appears "
                         f"{source.count(OVERLAP_START)} times; it is cut as the file's tail.")
    tail_at = source.index(OVERLAP_START)
    tail = source[tail_at:]
    if not tail.endswith("}\n"):
        raise SystemExit("split_editor_resized: the editor's source does not end in the "
                         "closing brace of resized(), so the overlap sweep is not its tail.")
    source = source[:tail_at]

    band = {}
    for name in BANDS:
        start, end = BANDS[name]
        source, band[name] = cut(source, name, start, end)

    # What is left of resized() is its signature, the texture rebuild it opens with,
    # and the layout it measures every band from. Everything after that marker - the
    # blank lines the bands were cut out of - is dropped: the bands are called in its
    # place.
    marker = "    const auto layout = getEditorLayout();\n"
    if source.count(marker) != 1:
        raise SystemExit("split_editor_resized: resized() no longer opens with "
                         f"getEditorLayout():\n{marker}")

    signature = "void FirstAudioProcessorEditor::resized()"
    if source.count(signature) != 1:
        raise SystemExit("split_editor_resized: resized() is not defined exactly once")

    head = source[:source.index(marker) + len(marker)]
    prefix = head[:head.index(signature)]
    body = head[head.index("{", head.index(signature)) + 1:].rstrip("\n")

    # The renames each band needs: a band handed its own rectangle names it, and a
    # band handed the strip's geometry measures in that.
    deck = renamed(band["deck.bands"], [(r"\blayout\.deck\b", "deck")]) + "\n" + \
        renamed(band["deck.scene"], [(r"\blayout\.deck\b", "deck")])
    tabbar = renamed(band["tabbar"], [(r"\blayout\.controls\b", "controlsPanel")])
    visibility = renamed(band["visibility"], [(rf"\b{flag}\b", f"geometry.{flag}")
                                              for flag in TAB_FLAGS])
    members = renamed(band["members"],
                      [(rf"\b{flag}\b", f"geometry.{flag}") for flag in TAB_FLAGS]
                      + [(r"\bgrid\b", "geometry.grid"),
                         (r"\bcellWidth\b", "geometry.cellWidth"),
                         (r"\bmemberRowY\b", "geometry.memberRowY"),
                         (r"\bplaceDeckSwitch \(", "placeDeckSwitch (geometry, ")])
    member_helper = renamed(band["member"],
                            [(r"\bgrid\b", "geometry.grid"),
                             (r"\bcellWidth\b", "geometry.cellWidth"),
                             (r"\bmemberRowY\b", "geometry.memberRowY")])
    header_block = renamed(band["header"], [(r"\blayout\.header\b", "header")])

    resized = f"""//==============================================================================
//  resized() and the bands it places.
//
//  resized() was one 1,561-line body: the header's grid, the deck's seven bands, the
//  tab bar, the knob strip's solve, the member band, the visibility pass and the
//  meters, all sharing one scope. It is one call per band now, and each band is a
//  function handed what it reads - the rectangle it owns, or the geometry the strip
//  solved. The order of the calls is the order of the old body and is not arbitrary;
//  see the declaration block in PluginEditor.h.
//
//  Every band below is the old body's text, moved - not retyped. The bands that the
//  layout harness cuts fragments out of (the grid, the band, the knob count, the
//  solver, the member row and the placement) are the reason it is a move: those
//  fragments are compiled by tests/layout, so a retyped statement would quietly stop
//  being the panel's own arithmetic.
//==============================================================================
void FirstAudioProcessorEditor::resized()
{{{body}

{CALLS}}}

//==============================================================================
//  The header: the brand strip, the title, the subtitle, and the one grid the five
//  engine switches, THEME, the language pair and the machine-state readout all sit
//  in - placed by a cursor that consumes each item's own width, which is what keeps
//  a row from overlapping itself however the panel is resized.
//==============================================================================
void FirstAudioProcessorEditor::placeHeader (juce::Rectangle<int> header)
{{
{header_block}
}}

//==============================================================================
//  The deck: six bands whose gaps compress but whose heights never do (see the band
//  table below), the seven lines inside them, and the window the 3D transport is
//  framed in - which shares reelCorridor with the rows, and is why it is here rather
//  than in the controls panel's own function.
//==============================================================================
void FirstAudioProcessorEditor::placeDeck (juce::Rectangle<int> deck)
{{
{deck}
}}

//==============================================================================
//  The controls panel's heading, its hint and the active page's own section caption -
//  the three captions above the divider, which the knob strip is laid out under.
//==============================================================================
void FirstAudioProcessorEditor::placeControls (const EditorLayout& layout)
{{
{band["controls.heading"]}
}}

//==============================================================================
//  The tab bar: the two rows of tabs-inside-tabs, re-flowed to the live family's page
//  count.
//==============================================================================
void FirstAudioProcessorEditor::placeTabBar (const juce::Rectangle<int>& controlsPanel)
{{
{tabbar}
}}

//==============================================================================
//  The knob strip: the solve, and the one band that hands anything on.
//
//  Its grid, the live page's flags, the solved tile and the member band's top edge are
//  what the member band, the visibility pass and the mix bar measure in, so they leave
//  here as a ControlGeometry rather than as locals the neighbouring bands could read.
//==============================================================================
FirstAudioProcessorEditor::ControlGeometry
FirstAudioProcessorEditor::placeControlStrip (const EditorLayout& layout)
{{
{band["strip"]}
{GEOMETRY_FILL}
}}

//==============================================================================
//  Which tab members exist. Re-asserted on EVERY resize rather than only on a tab
//  change: resized() also runs from setSize() in the constructor, before some of these
//  widgets had a parent, and a show/hide decision made only at tab-switch time left
//  them permanently visible on tabs that were not theirs.
//==============================================================================
void FirstAudioProcessorEditor::updateTabVisibility (const ControlGeometry& geometry)
{{
{visibility}
}}

//==============================================================================
//  Where the live page's members are: the mix bar's own strip, then one branch per
//  page that owns combos or pills in the member band.
//==============================================================================
void FirstAudioProcessorEditor::placeTabMembers (const ControlGeometry& geometry)
{{
{members}
}}

//==============================================================================
//  A tab member - a combo or a pill that is not a knob - in the member band's own cell
//  arithmetic: the same cell the knob above it would have had, a 17 px caption band,
//  and a 30 px control band centred under it.
//
//  This was a lambda inside resized(), which meant every caller had to be in that one
//  body and the cell arithmetic was only reachable from there; it is a member function
//  now because the band it measures in arrives as the geometry the strip solved. The
//  name is the lambda's, minus its capture list.
//==============================================================================
void FirstAudioProcessorEditor::placeDeckSwitch (const ControlGeometry& geometry,
                                                 juce::Label& label,
                                                 juce::Component& box,
                                                 int column, int columnsWide,
                                                 const juce::String& caption)
{{
{member_helper}
}}

//==============================================================================
//  The meters panel: its heading, hint and the compressor's readout pair, then the
//  four dials in a 2 x 2 grid - the rows sharing the available height, so the
//  arrangement holds at any panel size.
//==============================================================================
void FirstAudioProcessorEditor::placeMeters (const EditorLayout& layout)
{{
{band["meters.labels"]}

{band["meters.dials"]}
}}

//==============================================================================
//  The panel's overlap check, DEBUG only - a quadratic sweep on a path that runs
//  whenever the window moves, which is nothing to pay for in a development build and
//  nothing at all in a release one.
//
//  It runs LAST, on the bounds every band above has just written: a function called
//  after resized() returns would measure the rectangles of the PREVIOUS resize, which
//  is a stale rectangle and a collision that is not there.
//==============================================================================
void FirstAudioProcessorEditor::verifyPanelHasNoOverlaps() const
{{
{tail[:-2]}}}
"""

    EDITOR.write_text(prefix + resized)
    print(f"split_editor_resized: {EDITOR.name} is now "
          f"{len((prefix + resized).splitlines())} lines")

    for name, text in band.items():
        print(f"  {name:<16} {len(text.splitlines()):5d} lines")
    print(f"  overlap sweep    {len(tail.splitlines()) - 2:5d} lines")

    if "ControlGeometry" not in header:
        raise SystemExit("split_editor_resized: PluginEditor.h does not declare "
                         "ControlGeometry yet - the strip's geometry has nowhere to land.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
