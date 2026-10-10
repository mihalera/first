#!/usr/bin/env python3
"""Cuts the shipping parameter table and saved-state code into the harness's includes.

The harness checks the round trip a session actually makes: a state written by
getStateInformation() is read back by setStateInformation() into the same
parameters, and a state written by an OLDER build is migrated exactly once on the
way in. None of that can be measured against a copy of the code - a copy of the
parameter table would agree with a copy of the migration by construction - so
every line the harness runs is cut out of Source/ at build time:

    extracted_preamble.inc   the parameter table's own constants, the six model
                             name lists from the header, the saved-state format
                             helpers, and the group functions
                             createParameterLayout() is built from
    extracted_members.inc    createParameterLayout() itself, plus the four
                             processor functions the round trip runs through
    mark_preset_clean.inc    the badge bookkeeping setStateInformation() calls,
                             cut out of the class in the header
    expected_parameters.h    how many parameters the shipping table builds

Nothing here retypes a line of the plugin: the pieces are located by the text
that makes them unambiguous, and a piece that has been renamed or reshaped stops
the build with the piece named, rather than quietly measuring something else.

Usage:  python3 tests/state/extract.py <output-dir>
"""

from __future__ import annotations

import argparse
import pathlib
import re

REPO = pathlib.Path(__file__).resolve().parents[2]
PROCESSOR = REPO / "Source" / "PluginProcessor.cpp"
HEADER = REPO / "Source" / "PluginProcessor.h"

# The float constants the parameter table's ranges are built from. Each is one
# line at file scope in the processor's own anonymous namespace.
CONSTANT_ANCHORS = [
    "constexpr float minTrack =",
    "constexpr float maxTrack =",
    "constexpr float minInputDb =",
    "constexpr float maxInputDb =",
]

# The six model lists in the header: a count, an array sized by it, and the
# function that turns it into the juce::StringArray a choice parameter takes -
# the same list the panel's combo box reads, which is why they are cut rather
# than restated here.
NAME_LISTS = ["tapeStock", "valveType", "ampType", "transformerType", "digitalType", "vinylType"]

# The processor's own definitions the harness runs. Each is cut whole, from its
# signature through its closing brace.
MEMBER_FUNCTIONS = [
    "juce::AudioProcessorValueTreeState::ParameterLayout FirstAudioProcessor::createParameterLayout()",
    "void FirstAudioProcessor::copyToCompareSlot (int slot)",
    "void FirstAudioProcessor::updateCompareDirty()",
    "void FirstAudioProcessor::getStateInformation (juce::MemoryBlock& destData)",
    "void FirstAudioProcessor::setStateInformation (const void* data, int sizeInBytes)",
]

GROUP_SIGNATURE = re.compile(r"^    void (add[A-Za-z]+) \(ParamLayout& layout\)$", re.M)


def brace_end(text: str, start: int) -> int:
    """The index just past the brace that closes the block opened at or after
    `start`, skipping comments and string/char literals in one pass.

    The scan is a single pass over the real text rather than a filtered copy: a
    filtered copy has different offsets from the text it came from, and this
    function returns an index INTO that text.
    """
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

    raise SystemExit("extract.py: a block was opened but never closed")


def cut_block(text: str, anchor: str, trailing_semicolon: bool = False) -> str:
    """The anchor plus the block it opens, through the closing brace."""
    if text.count(anchor) != 1:
        raise SystemExit(f"extract.py: `{anchor}` appears {text.count(anchor)} times in the "
                         f"source; the state harness cannot tell them apart.")
    start = text.index(anchor)
    end = brace_end(text, start)
    piece = text[start:end]
    if trailing_semicolon and piece.rstrip().endswith("}"):
        piece = piece.rstrip() + ";"
    return piece


def cut_line(text: str, anchor: str) -> str:
    matches = [line for line in text.split("\n") if line.strip().startswith(anchor)]
    if len(matches) != 1:
        raise SystemExit(f"extract.py: `{anchor}` matches {len(matches)} lines in the source; "
                         f"the state harness cannot tell them apart.")
    return matches[0].strip()


def cut_banner_block(text: str, title: str) -> int:
    """Where the banner comment that introduces a section begins.

    Both sections the harness cuts carry a note explaining WHY the code is shaped
    the way it is, and that note is part of what is being measured, so the cut
    starts at the rule above the title rather than at the title itself. The rule
    is found by walking back over the comment lines instead of by matching a
    fixed string, because one of the two banners is indented and the other is
    not.
    """
    title_at = text.index(title)
    line_start = text.rindex("\n", 0, title_at) + 1
    position = line_start

    while position > 0:
        previous_end = position - 1
        previous_start = text.rindex("\n", 0, previous_end) + 1
        line = text[previous_start:previous_end].strip()

        if line.startswith("//") and len(line) > 12 and set(line[2:].strip()) == {"="}:
            return previous_start

        if not line.startswith("//"):
            break

        position = previous_start

    return line_start


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("output_dir", nargs="?", default="extracted_state")
    args = parser.parse_args()

    destination = pathlib.Path(args.output_dir)
    destination.mkdir(parents=True, exist_ok=True)

    processor = PROCESSOR.read_text()
    header = HEADER.read_text()

    # ------------------------------------------------------------------
    #  Everything that has to be at file scope before the harness's stand-in
    #  processor is declared.
    # ------------------------------------------------------------------
    preamble = ["// GENERATED FILE - do not edit. Cut verbatim out of Source/ by tests/state/extract.py.",
                "",
                "// ---- the range constants the parameter table is built from (PluginProcessor.cpp) ----",
                "namespace",
                "{"]
    for anchor in CONSTANT_ANCHORS:
        preamble.append("    " + cut_line(processor, anchor))
    preamble.append("} // namespace")
    preamble.append("")

    for name in NAME_LISTS:
        preamble.append(f"// ---- the {name} model list (PluginProcessor.h) ----")
        preamble.append(cut_line(header, f"inline constexpr int {name}Count"))
        preamble.append(cut_block(header,
                                  f"inline constexpr std::array<const char*, {name}Count> {name}Names",
                                  trailing_semicolon=True))
        preamble.append(cut_block(header, f"inline juce::StringArray {name}NameList()"))
        preamble.append("")

    # The saved-state format: its property name, its version, the migration and
    # the capture helper. Cut from the banner above them so the note explaining
    # WHY the marker exists travels with the code that implements it.
    format_start = cut_banner_block(processor, "//  Saved-state format")
    helpers_end = brace_end(processor, processor.index("juce::ValueTree captureState", format_start))
    preamble.append("// ---- the saved-state format marker and its helpers (PluginProcessor.cpp) ----")
    preamble.append(processor[format_start:helpers_end])
    preamble.append("")

    # The group functions the table is built from, with the banner that explains
    # the split, through the namespace's closing brace.
    groups_start = cut_banner_block(processor, "//  The parameter table, built in groups.")
    namespace_at = processor.index("namespace\n{\n", groups_start)
    groups_end = brace_end(processor, namespace_at)
    while processor[groups_end] != "\n":
        groups_end += 1          # keep the `// namespace` marker on the closing brace
    groups = processor[groups_start:groups_end]
    found = GROUP_SIGNATURE.findall(groups)
    if len(found) < 10:
        raise SystemExit(f"extract.py: found {len(found)} parameter group functions inside the "
                         f"table's namespace; the table is built by named groups.")
    preamble.append("// ---- the parameter table's group functions (PluginProcessor.cpp) ----")
    preamble.append(groups)
    preamble.append("")
    (destination / "extracted_preamble.inc").write_text("\n".join(preamble))
    print(f"wrote {destination / 'extracted_preamble.inc'} "
          f"({len(preamble)} lines, {len(found)} parameter groups)")

    # ------------------------------------------------------------------
    #  The functions themselves.
    # ------------------------------------------------------------------
    members = ["// GENERATED FILE - do not edit. Cut verbatim out of Source/ by tests/state/extract.py.", ""]
    for signature in MEMBER_FUNCTIONS:
        members.append(f"// ---- {signature.split(' ')[-1]} ----")
        members.append(cut_block(processor, signature))
        members.append("")
    (destination / "extracted_members.inc").write_text("\n".join(members))
    print(f"wrote {destination / 'extracted_members.inc'} ({len(members)} lines)")

    badge = cut_block(header, "    void markPresetClean (const juce::String& name)")
    (destination / "mark_preset_clean.inc").write_text(
        "// GENERATED FILE - do not edit. Cut verbatim out of Source/PluginProcessor.h by\n"
        "// tests/state/extract.py: the badge bookkeeping a session load performs.\n"
        + badge + "\n")
    print(f"wrote {destination / 'mark_preset_clean.inc'} ({len(badge.splitlines())} lines)")

    # The parameter count the table is expected to hold, counted the way the
    # preset checker counts ids - so a group the extractor no longer cuts cannot
    # quietly leave parameters out of the harness's table.
    parameters = len(re.findall(r"std::make_unique<juce::AudioParameter", processor))
    (destination / "expected_parameters.h").write_text(
        "// GENERATED FILE - do not edit. See tests/state/extract.py.\n"
        "// How many parameters the shipping parameter table builds. Counted from the source,\n"
        "// so a group the extractor does not cut is a failing check rather than a table that\n"
        "// quietly holds fewer parameters than the plugin has.\n"
        f"constexpr int expectedParameterCount = {parameters};\n")
    print(f"wrote {destination / 'expected_parameters.h'} (expectedParameterCount = {parameters})")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
