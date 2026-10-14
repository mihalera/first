#!/usr/bin/env python3
"""The split of Source/PluginProcessor.cpp, as a set of moves of exact text.

Every span below is cut by matching its own bytes and is asserted to occur
EXACTLY ONCE, so a block that was renamed, reindented or duplicated fails the
move instead of producing a file that is almost the shipping one. Nothing here
retypes a line of the plugin: the new files are the spans, concatenated in the
order they already had.

What moves, and what each move costs:

  Source/ParameterTable.cpp      the 23 group functions and createParameterLayout(),
                                 plus the four range constants they are built from
                                 (minTrack / maxTrack / minInputDb / maxInputDb were
                                 read by nothing else in the file).
  Source/ProcessorPresets.cpp    the full-state undo action and the two functions it
                                 swaps state through, the FactoryPresets table, and
                                 the preset/compare/A-B code that indexes both.
  Source/ProcessorPresets.h      the two FactoryPresets entry points the constructor's
                                 debug audit calls from ABOVE their definition - the
                                 forward declaration that used to sit in the .cpp.
  Source/ProcessorStateFormat.h  the saved-state marker, its migration and the
                                 capture helper. A header because the state is written
                                 from two files now, and an anonymous namespace inside
                                 it on purpose: that is the linkage these had in the
                                 .cpp (one copy per translation unit), and the four
                                 names are pure functions with no state between them,
                                 so two copies cannot disagree the way a shared
                                 `inline` object could.

No span is reformatted. The one deliberate change inside a moved span is nothing at
all - the state-format block keeps its four-space indentation, which is what it has
in the header's anonymous namespace too.
"""

from __future__ import annotations

import pathlib
import sys

REPO = pathlib.Path(__file__).resolve().parents[1]
PROCESSOR = REPO / "Source" / "PluginProcessor.cpp"


def brace_end(text: str, start: int) -> int:
    """The index just past the brace that closes the block opened at or after
    `start`, skipping comments and string/char literals. Same pass as the state
    harness uses, so a block boundary means the same thing in both."""
    depth = 0
    seen = False
    i = start
    n = len(text)

    while i < n:
        c = text[i]

        if c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            i = n if j < 0 else j + 1
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
        elif c in "\"'":
            quote = c
            i += 1
            while i < n and text[i] != quote:
                i += 2 if text[i] == "\\" else 1
            i += 1
        else:
            if c == "{":
                depth += 1
                seen = True
            elif c == "}":
                depth -= 1
                if seen and depth == 0:
                    return i + 1
            i += 1

    raise SystemExit("split_processor: a block was opened but never closed")


def banner_start(text: str, title: str) -> int:
    """Where the rule line above a banner comment's title begins."""
    if text.count(title) != 1:
        raise SystemExit(f"split_processor: `{title}` appears {text.count(title)} times")
    title_at = text.index(title)
    position = text.rindex("\n", 0, title_at) + 1

    while position > 0:
        previous_end = position - 1
        previous_start = text.rindex("\n", 0, previous_end) + 1
        line = text[previous_start:previous_end].strip()

        if line.startswith("//") and len(line) > 12 and set(line[2:].strip()) == {"="}:
            return previous_start

        if not line.startswith("//"):
            break

        position = previous_start

    return position


def exact(text: str, anchor: str, label: str) -> str:
    if text.count(anchor) != 1:
        raise SystemExit(f"split_processor: {label}: the anchor appears "
                         f"{text.count(anchor)} times, not once")
    return anchor


def cut(text: str, start: int, end: int, label: str) -> tuple[str, str]:
    span = text[start:end]
    if not span.strip():
        raise SystemExit(f"split_processor: {label} is empty")
    return text[:start] + text[end:], span


def main() -> int:
    src = PROCESSOR.read_text()
    original = src
    moved: dict[str, str] = {}

    # ---- the four range constants the parameter table is built from --------
    min_max_track = exact(src, "    constexpr float minTrack = 0.0f;\n"
                               "    constexpr float maxTrack = 1.0f;\n", "minTrack/maxTrack")
    min_max_db = exact(src, "    // Symmetric decibel range shared by the input and "
                            "output stage controls.\n"
                            "    constexpr float minInputDb = -32.0f;\n"
                            "    constexpr float maxInputDb = 32.0f;\n", "min/maxInputDb")

    constants = ("namespace\n{\n"
                 + min_max_track.rstrip("\n") + "\n\n"
                 + min_max_db.rstrip("\n") + "\n}\n")

    src = src.replace(min_max_track, "", 1)
    src = src.replace(min_max_db, "", 1)

    # ---- the saved-state format -------------------------------------------
    state_start = banner_start(src, "//  Saved-state format")
    state_end = brace_end(src, src.index("juce::ValueTree captureState "
                                         "(juce::AudioProcessorValueTreeState& parameters)",
                                         state_start))
    src, state_format = cut(src, state_start, state_end, "the saved-state format block")
    moved["ProcessorStateFormat.h"] = state_format

    # ---- the factory preset forward declaration ---------------------------
    forward_start = banner_start(src, "//  The factory presets are defined further down this file")
    forward_ns = src.index("namespace FactoryPresets", forward_start)
    forward_end = brace_end(src, forward_ns)
    src, forward = cut(src, forward_start, forward_end, "the FactoryPresets forward declaration")
    moved["ProcessorPresets.h"] = forward

    # ---- the state swap (undo action + the two functions it calls) --------
    swap_start = banner_start(src, "//  Full-state undo. Preset applications and A/B recalls are "
                                   "recorded as one")
    swap_end = brace_end(src, src.index("void FirstAudioProcessor::applyStateWithUndo "
                                        "(const juce::ValueTree& targetState,", swap_start))
    src, swap = cut(src, swap_start, swap_end, "the full-state undo block")

    # ---- the preset table and everything that reads it --------------------
    presets_start = banner_start(src, "//  Factory presets.")
    presets_end = brace_end(src, src.index("bool FirstAudioProcessor::deleteUserPreset "
                                           "(const juce::String& name)", presets_start))
    src, presets = cut(src, presets_start, presets_end, "the preset table and its readers")
    moved["ProcessorPresets.cpp"] = swap + "\n\n" + presets

    # ---- the parameter table ----------------------------------------------
    table_start = banner_start(src, "//  The parameter table, built in groups.")
    table_end = brace_end(src, src.index("juce::AudioProcessorValueTreeState::ParameterLayout "
                                         "FirstAudioProcessor::createParameterLayout()", table_start))
    src, table = cut(src, table_start, table_end, "the parameter table")
    moved["ParameterTable.cpp"] = constants + "\n" + table

    if src == original:
        raise SystemExit("split_processor: nothing was cut; the anchors no longer match")
    for name, text in moved.items():
        if not text.strip():
            raise SystemExit(f"split_processor: {name} would be empty")

    (REPO / "Source" / "ProcessorStateFormat.h").write_text(
        STATE_FORMAT_BANNER + moved["ProcessorStateFormat.h"].rstrip("\n") + "\n}\n")
    (REPO / "Source" / "ProcessorPresets.h").write_text(
        PRESETS_HEADER_BANNER + moved["ProcessorPresets.h"].rstrip("\n") + "\n")
    (REPO / "Source" / "ProcessorPresets.cpp").write_text(
        PRESETS_SOURCE_BANNER + moved["ProcessorPresets.cpp"].rstrip("\n") + "\n")
    (REPO / "Source" / "ParameterTable.cpp").write_text(
        PARAMETER_TABLE_BANNER + moved["ParameterTable.cpp"].rstrip("\n") + "\n")
    PROCESSOR.write_text(src)

    for name in sorted(moved):
        print(f"wrote Source/{name}: {len(moved[name].splitlines())} moved lines")
    print(f"PluginProcessor.cpp: {len(original.splitlines())} -> {len(src.splitlines())} lines")
    return 0


STATE_FORMAT_BANNER = """// =============================================================================
//  The saved-state format: the marker a written state carries, the migration that
//  reads an older one, and the capture helper the writer goes through.
//
//  Moved out of PluginProcessor.cpp - prose and all, by matching its exact text -
//  because the state is written from two files now: the processor's own
//  getStateInformation()/setStateInformation() stayed where the processor is, and
//  the preset and A/B code that writes the same tree through captureState() is
//  Source/ProcessorPresets.cpp. A copy of this in each would be exactly the drift
//  tests/state exists to catch, and that harness compiles THIS text: it cuts the
//  banner below and everything under it, so the migration a session runs is the
//  shipping migration rather than a restatement of it.
//
//  Two things about the shape are deliberate:
//
//    * The block below sits in an ANONYMOUS namespace, which is unusual in a header
//      and is the point here. That is the linkage these four names had in the .cpp -
//      one copy per translation unit, internal linkage - and all four are pure
//      functions and constants with no state between them, so two copies cannot
//      disagree. `liveThemeChoice` in Source/GUI/PanelPaint.h is the counter-example
//      that forced `inline` there: it is an OBJECT two files had to share, and a
//      header without `inline` would have handed each of them its own.
//    * Nothing else changed. The indentation is the .cpp's, unchanged: the block was
//      written inside an anonymous namespace there and is inside one here.
// =============================================================================
#pragma once

#include <JuceHeader.h>

namespace
{
"""

PRESETS_HEADER_BANNER = """// =============================================================================
//  The two FactoryPresets entry points the processor's constructor reaches from
//  ABOVE the table's definition.
//
//  This is the forward declaration that used to sit in the middle of
//  PluginProcessor.cpp, and it is a header because the table it promises is now
//  defined in another translation unit (Source/ProcessorPresets.cpp). The debug
//  audit inside the constructor is the only code that calls them from outside that
//  file - and it is inside `#if DEBUG`, which is how the missing declaration this
//  block exists to fix stayed invisible: every release build compiled the mistake
//  out of existence.
// =============================================================================
#pragma once

"""

PRESETS_SOURCE_BANNER = """// =============================================================================
//  The presets: the factory table, the preset box's own files, the A/B compare
//  slots, and the whole-state undo they all go through.
//
//  Moved out of PluginProcessor.cpp by matching its exact text, in three spans that
//  kept their order: the state-swap action and the two functions it swaps state
//  through, the FactoryPresets table, and the code that indexes it. Nothing was
//  retyped; the prose came with the code because in this repository the note that
//  explains why a thing is shaped the way it is IS part of the thing.
//
//  What the move cost is the include list and nothing else. The state-format
//  helpers the preset writer stamps a file with are Shared with the processor's own
//  save/load, so they are Source/ProcessorStateFormat.h; the table's entry points
//  are declared in Source/ProcessorPresets.h for the constructor's debug audit; and
//  FactoryPresetIndex.h - the generated list of preset files - is read here rather
//  than there, because this is where the table that reads it lives.
// =============================================================================
#include "PluginProcessor.h"
#include "ProcessorPresets.h"
#include "ProcessorStateFormat.h"

// The generated preset list from CMakeLists.txt: one entry per file under
// Source/Presets/Factory, in the order the preset box shows them. It is read HERE
// rather than in the processor because the table built from it is here.
#include <FactoryPresetIndex.h>

#include <map>
#include <vector>

"""

PARAMETER_TABLE_BANNER = """// =============================================================================
//  The parameter table: the 23 groups createParameterLayout() is built from, and
//  the four range constants they are built with.
//
//  Moved out of PluginProcessor.cpp, prose and all, by matching its exact text. The
//  table is 1,300 lines of what is essentially DATA - one registration per
//  parameter, each with the note that says what it does - and it was sitting in the
//  middle of the signal path, which is the file a reader opens to find out what the
//  machine does to the audio.
//
//  Nothing about the table changed, and that is the whole safety argument: a saved
//  session, a factory preset and a host's automation all address parameters by name,
//  and the order the groups are called in IS the table's order. The groups keep
//  their bodies, their order and their call order.
//
//  Two things had to travel with it, both of them things only it used:
//
//    * createParameterLayout() itself. It is declared in PluginProcessor.h (the
//      constructor's initialiser list calls it), so it needs no header of its own -
//      just this file, where the groups it calls are.
//    * minTrack, maxTrack, minInputDb and maxInputDb, the four range constants. They
//      were in the processor's file-local namespace and read by nothing after the
//      table left, so they left with it - which is what keeps this a move rather
//      than a copy: a constant in two files is two constants.
//
//  tests/state/extract.py cuts the groups, the constants and createParameterLayout()
//  out of THIS file and compiles them, and counts the parameters they build (109) -
//  so a group that stops being extracted is a failing check rather than a table that
//  quietly holds fewer parameters than the plugin has.
// =============================================================================
#include "PluginProcessor.h"

"""


if __name__ == "__main__":
    sys.exit(main())
