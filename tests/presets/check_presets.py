#!/usr/bin/env python3
"""
Checks the factory presets, which now live as JSON under Source/Presets/Factory.

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

  3. A KEY WAS TYPED WRONG. A misspelt key in a JSON file is not a compile
     error: the loader records it, applyFactoryPreset()'s getParameter() guard
     drops it, and the parameter silently keeps the session's value.

  4. TWO LITERALS IN THE C++ EXCLUSION LIST TOUCHED. A missing comma between two
     entries does not fail to compile - C++ concatenates adjacent string
     literals - so the list held one name that is not a parameter and neither of
     the two keys it was meant to hold. The debug audit asserted on both, and
     this file read the two quoted halves as two entries, so nothing was caught.
     It catches the shape now: an adjacent pair of literals is a failure.

None of these can fail at runtime, which is why C++ cannot catch them and why
this reads the source and the data instead.

What it checks now, against the JSON rather than a C++ table:

  * every file in Source/Presets/Factory is named in FACTORY_PRESET_FILES, and
    every file named there exists - the build graph and the data cannot drift
  * every file parses, and every name is non-empty and unique
  * every key is a real, registered parameter id
  * every value is a number (a string or an object is a typo the loader would
    turn into the sentinel, and the debug audit would then fail on it)
  * every registered parameter is either set by some preset or listed in the
    C++ intentionallyNotPresettable set, and the two lists are compared
  * the guide in docs/presets.md has exactly one row per preset, so a preset that
    ships without an explanation fails here
  * the two extremes mean what their names say. MAXIMUM is the ceiling of every
    other preset - no preset drives a parameter past it, which is what makes it
    "everything turned up" - and MINIMUM is a passthrough: the machine lists at
    their Off entry, every source down, the saturation mix and every effect at
    zero. Both drifted before: MAXIMUM left the reverb, the vinyl print, the IR
    and the whole rack at zero while other presets used them, and MINIMUM ran
    the J37 at full saturation with the mix at zero, so its own name was a lie.
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PRESET_DIR = os.path.join(ROOT, "Source", "Presets", "Factory")
CMAKE = os.path.join(ROOT, "CMakeLists.txt")

# The registered parameter ids are read out of the source that registers them, which
# is TWO files since PluginProcessor.cpp was split: the table itself (every
# `ParameterID { "id" }` a group adds) is Source/ParameterTable.cpp, and the two id
# lists that are not the table - the parameters the editor's dirty badge watches, and
# the ones deliberately kept out of every preset - stayed in the processor beside the
# constructor that uses them. Reading only one of the two would silently check the
# presets against a shorter id set, which is the failure this file exists to catch.
PROCESSOR = os.path.join(ROOT, "Source", "PluginProcessor.cpp")
PARAMETER_TABLE = os.path.join(ROOT, "Source", "ParameterTable.cpp")

problems = []


def read(path):
    with open(path, encoding="utf-8") as handle:
        return handle.read()


# ---------------------------------------------------------------------------
# 1. The preset list, from CMake, and the files on disk.
# ---------------------------------------------------------------------------
cmake = read(CMAKE)
block = re.search(r"set\(FACTORY_PRESET_FILES(.*?)\)", cmake, re.S)
if not block:
    sys.exit("check_presets: FACTORY_PRESET_FILES not found in CMakeLists.txt")

listed = re.findall(r"\$\{FACTORY_PRESET_DIR\}/([A-Za-z0-9_]+)\.json", block.group(1))
if len(listed) != len(set(listed)):
    problems.append("FACTORY_PRESET_FILES names the same file twice")

on_disk = sorted(f[:-5] for f in os.listdir(PRESET_DIR) if f.endswith(".json"))
missing = sorted(set(on_disk) - set(listed))
extra = sorted(set(listed) - set(on_disk))
for name in missing:
    problems.append("on disk but not built: %s.json" % name)
for name in extra:
    problems.append("built but not on disk: %s.json" % name)

if not listed:
    sys.exit("check_presets: FACTORY_PRESET_FILES is empty")

# ---------------------------------------------------------------------------
# 2. The registered parameter ids, and the deliberate exclusions, from source.
# ---------------------------------------------------------------------------
processor = read(PROCESSOR)
table = read(PARAMETER_TABLE)

ids_block = re.search(r"static const auto ids = std::array\s*\{(.*?)\};", processor, re.S)
if not ids_block:
    sys.exit("check_presets: the registered id list not found in PluginProcessor.cpp")
badge = set(re.findall(r'"([a-z0-9_]+)"', ids_block.group(1)))
registered = set(re.findall(r'ParameterID \{ "([a-z0-9_]+)"', table))
if not registered:
    sys.exit("check_presets: no registered parameters found in ParameterTable.cpp")
ids = badge | registered

excluded_block = re.search(
    r"intentionallyNotPresettable\s*\{(.*?)\};", processor, re.S)
if not excluded_block:
    sys.exit("check_presets: intentionallyNotPresettable not found in PluginProcessor.cpp")
exclusions = excluded_block.group(1)
excluded = set(re.findall(r'"([a-z0-9_]+)"', exclusions))

# Adjacent string literals concatenate, so `"a" "b"` is one name in C++ and two in
# a regex. That is exactly the bug this grew a check for: a dropped comma in the
# list above hid two keys from the audit and no check could see it.
if re.search(r'"[a-z0-9_]+"\s*\r?\n?\s*"[a-z0-9_]+"', exclusions):
    problems.append("intentionallyNotPresettable has two adjacent string literals - "
                    "a missing comma between two keys")

# ---------------------------------------------------------------------------
# 3. The data itself.
# ---------------------------------------------------------------------------
covered = set()
names = []
values = {}

for name in listed:
    path = os.path.join(PRESET_DIR, name + ".json")
    if not os.path.exists(path):
        continue
    try:
        preset = json.loads(read(path))
    except ValueError as error:
        problems.append("%s.json does not parse: %s" % (name, error))
        continue

    values[name] = preset

    label = preset.get("name")
    if not isinstance(label, str) or not label.strip():
        problems.append("%s.json has no usable \"name\"" % name)
    else:
        names.append(label)

    for key, value in preset.items():
        if key == "name":
            continue
        if key not in ids:
            problems.append("%s.json: \"%s\" is not a registered parameter" % (name, key))
            continue
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            problems.append("%s.json: \"%s\" is %s, not a number" % (name, key, type(value).__name__))
            continue
        covered.add(key)

duplicates = {n for n in names if names.count(n) > 1}
for name in sorted(duplicates):
    problems.append("two presets are both called \"%s\"" % name)

missing_from_presets = sorted(ids - covered - excluded)
for key in missing_from_presets:
    problems.append("parameter \"%s\" is in no preset and is not excluded" % key)

stale_exclusions = sorted(excluded - ids)
for key in stale_exclusions:
    problems.append("intentionallyNotPresettable names \"%s\", which is not a parameter" % key)

# ---------------------------------------------------------------------------
# 4. The two extremes.
# ---------------------------------------------------------------------------
# Which parameters are lists rather than amounts, read from the table: a choice
# value is an index, and the last entry of a machine list is "Off", so index
# order is not amount order and those keys cannot be compared across presets.
choice_ids = set(re.findall(
    r"AudioParameterChoice> \(juce::ParameterID \{ \"([a-z0-9_]+)\"", table))

# A switch is not an amount either: "maximum" for BYPASS, DELTA or POLARITY is not
# "switched on harder", the transport is a play state and the UI sounds are not
# the sound. Rack SPEED is a rate whose neutral is its centre, not zero.
not_amounts = choice_ids | {
    "bypass", "delta", "polarity", "transport", "spindown", "ui_sounds", "language",
    "fx_speed",
}

top = values.get("maximum", {})
for other_name, other in values.items():
    if other_name == "maximum":
        continue
    for key, value in other.items():
        if key == "name" or key in not_amounts or key not in top:
            continue
        if top[key] < value:
            problems.append("maximum.json leaves \"%s\" at %s, below %s.json's %s - it is "
                            "the ceiling, not a second preset" % (key, top[key], other_name, value))

# MINIMUM is a null test, not the bottom of every range: -60 dB of compression is
# the loudest threshold there is and a 2 kHz lowpass is a sound, so its knobs sit
# where the signal passes untouched. Anything not listed below is transparent at
# zero, which is what makes this check catch a "minimum" that quietly processes.
transparent = {
    "tone": 0.5, "character": 0.5, "stereo_width": 0.5, "fx_speed": 0.5,
    "in_hp_freq": 20.0, "out_hp_freq": 20.0,
    "in_lp_freq": 20000.0, "out_lp_freq": 20000.0,
    "in_eq_order": 0, "out_eq_order": 0, "in_eq_q": 0.7, "out_eq_q": 0.7,
    "comp_threshold": 0.0, "comp_ratio": 1.0, "comp_attack": 0.1, "comp_release": 10.0,
    # Corners whose bottom is already above zero, so zero is out of range rather
    # than quieter: an echo and a room that cut nothing, and a one-semitone range.
    "delay_lowcut": 20.0, "reverb_lowcut": 20.0, "autotune_range": 1.0,
    "tape_type": 12, "valve_type": 6, "amp_type": 6, "transformer_type": 5,
    "digital_type": 5, "vinyl_type": 6, "speed": 1, "delay_type": 0,
    "transport": 1, "st_link": 1.0, "anti_phase_guard": 1, "auto_gain": 0,
}

floor = values.get("minimum", {})
for key, value in floor.items():
    if key == "name":
        continue
    expected = transparent.get(key, 0.0)
    if value != expected:
        problems.append("minimum.json sets \"%s\" to %s where a transparent signal needs "
                        "%s - it is a null test, not the bottom of the range"
                        % (key, value, expected))

# ---------------------------------------------------------------------------
# 5. The guide that explains them.
# ---------------------------------------------------------------------------
# A preset nobody can explain is a preset nobody uses, and the doc is the only
# place that says what each one is FOR - the JSON can only say what it sets.
GUIDE = os.path.join(ROOT, "docs", "presets.md")
if not os.path.exists(GUIDE):
    problems.append("docs/presets.md is missing; it explains what every factory preset is for")
else:
    guide = read(GUIDE)
    for label in names:
        if guide.count("| **%s** |" % label) != 1:
            problems.append("docs/presets.md explains \"%s\" %d time(s); it needs exactly one row"
                            % (label, guide.count("| **%s** |" % label)))

# ---------------------------------------------------------------------------
if problems:
    for problem in problems:
        print("check_presets: " + problem)
    sys.exit(1)

print("check_presets: minimum is a passthrough, maximum is the ceiling of the other %d"
      % (len(listed) - 1))
print("check_presets: %d presets consistent, %d of %d parameters set, %d excluded"
      % (len(listed), len(covered), len(ids), len(excluded)))
print("check_presets: %d registered in ParameterTable.cpp, %d watched by the dirty badge in "
      "PluginProcessor.cpp, %d excluded from every preset"
      % (len(registered), len(badge), len(excluded)))
