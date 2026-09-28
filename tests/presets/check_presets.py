#!/usr/bin/env python3
"""
Checks the factory-preset table for the three ways it can quietly lie.

    python3 tests/presets/check_presets.py

The factory presets failed twice, in two different ways, and both produced the
same user-visible report - "the presets do not work" - with nothing in a
release build to say why:

  1. THE NAMES AND THE TABLE DISAGREED. getPresetNames() returned twenty names
     for a twenty-six row table. The preset box, the undo label and the dirty
     badge all index by POSITION, so the six rows past the gap loaded the sound
     the name above promised, and nothing crashed: the list simply lied.

  2. THE TABLE AND THE PARAMETERS DISAGREED. Fifteen sound-bearing parameters
     had no column, so they kept the session's value and every preset loaded
     "mostly" - the same preset gave different results depending on what had
     been loaded before it.

  3. A MAP KEY OR FIELD NAME WAS TYPED WRONG. The values are written
     `preset.someField`, so a misspelt field is a compile error, but a
     misspelt KEY - `{ "prempa", ... }` - is not: applyFactoryPreset()'s
     getParameter() guard drops it and the parameter keeps the session's value,
     silently.

None of these can fail at runtime, which is why C++ cannot catch them and why
this reads the source instead. It checks:

  * numFactoryPresets == len(getPresetNames()) == number of table rows
  * every table row's trailing comment is the name at the same INDEX
  * every key in factoryPresetValues() is a declared parameter id
  * every declared parameter id is either such a key, or named - with a reason -
    in the exclusion list the plugin itself declares
  * every `preset.x` the map reads is a field the FactoryPreset struct declares
  * the exclusion list here is the one in the C++ audit, so the two cannot
    disagree about what a preset is allowed to leave alone

The C++ side of the same check is the DEBUG block in the processor's
constructor. That one catches a wrong VALUE at a breakpoint; this one runs in
CI against a release build and names the offending id in the log.
"""

import re
import sys
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[2] / "Source" / "PluginProcessor.cpp"

# The count itself lives in the header, beside the declaration that uses it;
# everything else is in the .cpp. Both are read because the disagreement this
# checks for is precisely between a header constant and a .cpp list.
HEADER = Path(__file__).resolve().parents[2] / "Source" / "PluginProcessor.h"

# The five parameters a preset deliberately does not state, and why. Kept here
# as a literal so the check has something to hold the plugin to, and compared
# against the C++ list below - the two drifting apart is itself a bug.
EXPECTED_EXCLUSIONS = {
    "bypass",
    "delta",
    "ui_sounds",
    "transport",
    "spindown",
}


def fail(problems, message):
    problems.append(message)


def main() -> int:
    if not SOURCE.is_file():
        print(f"check_presets: {SOURCE} not found")
        return 1

    text = SOURCE.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")
    problems = []

    # ------------------------------------------------------------------
    #  The three sources of truth, sliced out of the one file.
    # ------------------------------------------------------------------

    count_match = re.search(
        r"numFactoryPresets\s*=\s*(\d+)", header)
    if count_match is None:
        print("check_presets: numFactoryPresets not found")
        return 1
    declared_count = int(count_match.group(1))

    names_block = re.search(
        r"juce::StringArray FirstAudioProcessor::getPresetNames\(\)\s*\{(.*?)\n\}",
        text, re.S)
    if names_block is None:
        print("check_presets: getPresetNames() not found")
        return 1
    names = re.findall(r'"([^"]+)"', names_block.group(1))

    table_start = text.index(
        "static const std::array<FactoryPreset, numFactoryPresets> table {{")
    table_end = text.index("\n    }};", table_start)
    table = text[table_start:table_end]

    # A row ends at the comment the table puts on every row. The comment is the
    # only thing in the table that says which sound a row is, so it is the row's
    # name for this check - and that is precisely why a name missing HERE, in
    # getPresetNames(), is a bug rather than a style choice.
    row_names = [
        m.group(1).strip()
        for m in re.finditer(r"\},  // ([^\n]*)$", table, re.M)
    ]

    map_start = text.index("factoryPresetValues (int index)")
    map_block = re.search(
        r"const auto values = std::map<juce::String, float>\s*\{(.*?)\n    \};",
        text[map_start:], re.S)
    if map_block is None:
        print("check_presets: the values map in factoryPresetValues() not found")
        return 1
    map_body = map_block.group(1)
    map_keys = re.findall(r'\{\s*"([^"]+)"\s*,', map_body)
    map_fields = re.findall(r"preset\.(\w+)", map_body)

    struct_start = text.index("struct FactoryPreset\n    {")
    struct_end = text.index("\n    };", struct_start)
    struct_fields = set(
        re.findall(r"^\s+(?:float|int)\s+(\w+)", text[struct_start:struct_end], re.M))

    declared_params = set(re.findall(r'ParameterID\s*\{\s*"([^"]+)"', text))

    # The exclusion list, parsed out of the C++ audit so it is the plugin's own
    # list rather than a second copy of it.
    exclusion_block = re.search(
        r"intentionallyNotPresettable\s*\{(.*?)\}", text, re.S)
    cpp_exclusions = set()
    if exclusion_block is not None:
        cpp_exclusions = set(re.findall(r'"([^"]+)"', exclusion_block.group(1)))

    # ------------------------------------------------------------------
    #  1. One count, written once, held by all three sources.
    # ------------------------------------------------------------------

    if len(names) != declared_count:
        fail(problems,
             f"getPresetNames() returns {len(names)} names but "
             f"numFactoryPresets is {declared_count}. Every row past the "
             f"short list is loaded under the name above it, because the box, "
             f"the undo label and the badge all index by POSITION.")

    if len(row_names) != declared_count:
        fail(problems,
             f"the table has {len(row_names)} named rows but numFactoryPresets "
             f"is {declared_count}.")

    if len(names) == len(row_names):
        for index, (name, row) in enumerate(zip(names, row_names)):
            if name != row:
                fail(problems,
                     f"row {index} is named {row!r} in the table but "
                     f"{name!r} in getPresetNames().")

    if len(set(names)) != len(names):
        duplicates = sorted({n for n in names if names.count(n) > 1})
        fail(problems, f"duplicate preset names: {', '.join(duplicates)}")

    # ------------------------------------------------------------------
    #  2. Every key is a parameter the plugin actually has.
    # ------------------------------------------------------------------

    for key in sorted(set(map_keys)):
        if key not in declared_params:
            fail(problems,
                 f"factoryPresetValues() writes the key {key!r}, which is not a "
                 f"parameter. applyFactoryPreset() drops it and the parameter "
                 f"keeps the session's value, so the preset loads 'mostly'.")

    # ------------------------------------------------------------------
    #  3. Every parameter is either in a preset or named as an exclusion.
    # ------------------------------------------------------------------

    if cpp_exclusions != EXPECTED_EXCLUSIONS:
        fail(problems,
             "the exclusion list in the constructor's audit is "
             f"{sorted(cpp_exclusions)} but this check expects "
             f"{sorted(EXPECTED_EXCLUSIONS)}. One of the two is out of date.")

    for parameter in sorted(declared_params - set(map_keys)):
        if parameter not in EXPECTED_EXCLUSIONS:
            fail(problems,
                 f"the parameter {parameter!r} is declared but no preset states "
                 f"it, so it keeps the session's value on every load. If that "
                 f"is deliberate, add it to EXPECTED_EXCLUSIONS in this file "
                 f"and to intentionallyNotPresettable in the processor, with "
                 f"the reason.")

    for parameter in sorted(EXPECTED_EXCLUSIONS):
        if parameter not in declared_params:
            fail(problems,
                 f"{parameter!r} is on the exclusion list but is not a "
                 f"parameter any more - the list is stale.")

    # ------------------------------------------------------------------
    #  4. Every field the map reads is a field the struct declares.
    # ------------------------------------------------------------------

    for field in sorted(set(map_fields)):
        if field not in struct_fields:
            fail(problems,
                 f"factoryPresetValues() reads preset.{field}, which the "
                 f"FactoryPreset struct does not declare.")

    # ------------------------------------------------------------------
    #  Report.
    # ------------------------------------------------------------------

    if problems:
        print("check_presets: the factory presets are not consistent.")
        print()
        for problem in problems:
            print(f"  - {problem}")
        print()
        print(f"{len(problems)} problem(s). The preset table, the name list and "
              f"the parameter layout must agree.")
        return 1

    print(f"check_presets: {declared_count} presets consistent.")
    print(f"  names           {len(names)}")
    print(f"  table rows      {len(row_names)}")
    print(f"  parameters set  {len(set(map_keys))} of {len(declared_params)}")
    print(f"  not presettable {', '.join(sorted(EXPECTED_EXCLUSIONS))}")
    print(f"  struct fields   {len(struct_fields)}, all read by the map")
    return 0


if __name__ == "__main__":
    sys.exit(main())
