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
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PRESET_DIR = os.path.join(ROOT, "Source", "Presets", "Factory")
CMAKE = os.path.join(ROOT, "CMakeLists.txt")
PROCESSOR = os.path.join(ROOT, "Source", "PluginProcessor.cpp")

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

ids_block = re.search(r"static const auto ids = std::array\s*\{(.*?)\};", processor, re.S)
if not ids_block:
    sys.exit("check_presets: the registered id list not found in PluginProcessor.cpp")
ids = set(re.findall(r'"([a-z0-9_]+)"', ids_block.group(1)))
ids |= set(re.findall(r'ParameterID \{ "([a-z0-9_]+)"', processor))

excluded_block = re.search(
    r"intentionallyNotPresettable\s*\{(.*?)\};", processor, re.S)
if not excluded_block:
    sys.exit("check_presets: intentionallyNotPresettable not found in PluginProcessor.cpp")
excluded = set(re.findall(r'"([a-z0-9_]+)"', excluded_block.group(1)))

# ---------------------------------------------------------------------------
# 3. The data itself.
# ---------------------------------------------------------------------------
covered = set()
names = []

for name in listed:
    path = os.path.join(PRESET_DIR, name + ".json")
    if not os.path.exists(path):
        continue
    try:
        preset = json.loads(read(path))
    except ValueError as error:
        problems.append("%s.json does not parse: %s" % (name, error))
        continue

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
if problems:
    for problem in problems:
        print("check_presets: " + problem)
    sys.exit(1)

print("check_presets: %d presets consistent, %d of %d parameters set, %d excluded"
      % (len(listed), len(covered), len(ids), len(excluded)))
