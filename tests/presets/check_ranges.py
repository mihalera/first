#!/usr/bin/env python3
"""
Checks every factory preset value against the parameter's own range.

    tests/presets/check_ranges.sh

check_presets.py answers "does this key exist and is it a number". This answers
the next question: is the number one the parameter can hold. A value outside the
range does not fail, does not warn and does not crash - the load clamps it - so
a preset that asks a 0..1 control for 1.1 is stored, displayed and compared as
1.0 while the JSON keeps saying something else. That is exactly what four
factory presets did before this harness existed.

The ranges come from tests/presets/range_dump.cpp, which asks the shipping table
through the state harness's stub toolkit, so nothing here re-parses C++ ranges
and nothing can drift from them.

What it checks:

  * every float value lands on the parameter's step grid and inside its range
  * every choice value is a whole number inside the list
  * every switch is 0 or 1
  * the parameter each preset names is one the dump found - a range table that
    is missing an id would otherwise make every check below it vacuous
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PRESET_DIR = os.path.join(ROOT, "Source", "Presets", "Factory")
CMAKE = os.path.join(ROOT, "CMakeLists.txt")

# Values are compared after the same snap a load performs, so the tolerance only
# has to absorb float32 rounding of a decimal that was written to six places.
TOLERANCE = 1.0e-4

if len(sys.argv) != 2:
    sys.exit("check_ranges: usage: check_ranges.py <ranges.txt>")

problems = []

ranges = {}
for line in open(sys.argv[1], encoding="utf-8"):
    parts = line.rstrip("\n").split("|")
    if len(parts) < 6 or not parts[0]:
        continue
    ident, kind = parts[0], parts[1]
    try:
        low, high, step = float(parts[2]), float(parts[3]), float(parts[4])
    except ValueError:
        sys.exit("check_ranges: unreadable range line for %r" % ident)
    ranges[ident] = {"kind": kind, "low": low, "high": high, "step": step}

if not ranges:
    sys.exit("check_ranges: the range dump is empty - tests/presets/range_dump.cpp "
             "printed nothing")

cmake = open(CMAKE, encoding="utf-8").read()
block = re.search(r"set\(FACTORY_PRESET_FILES(.*?)\)", cmake, re.S)
if not block:
    sys.exit("check_ranges: FACTORY_PRESET_FILES not found in CMakeLists.txt")
order = re.findall(r"\$\{FACTORY_PRESET_DIR\}/([a-z0-9_]+)\.json", block.group(1))
if not order:
    sys.exit("check_ranges: FACTORY_PRESET_FILES is empty")


def snap(info, value):
    """What a load does with the value: snap to the step, then clamp."""
    snapped = value
    if info["step"] > 0.0:
        snapped = info["low"] + info["step"] * round((value - info["low"]) / info["step"])
    return min(info["high"], max(info["low"], snapped))


checked = 0

for name in order:
    path = os.path.join(PRESET_DIR, name + ".json")
    if not os.path.exists(path):
        problems.append("%s.json is registered but missing" % name)
        continue
    preset = json.loads(open(path, encoding="utf-8").read())

    for key, value in preset.items():
        if key == "name":
            continue
        if key not in ranges:
            problems.append("%s.json: \"%s\" is not in the range dump" % (name, key))
            continue

        info = ranges[key]
        checked += 1

        if info["kind"] == "choice":
            limit = int(info["high"])
            if value != round(value):
                problems.append("%s.json: \"%s\" is %s; a choice is a whole index"
                                % (name, key, value))
            elif not 0 <= value <= limit:
                problems.append("%s.json: \"%s\" is index %s of %d" % (name, key, value, limit + 1))
            continue

        if info["kind"] == "bool":
            if value not in (0, 1):
                problems.append("%s.json: \"%s\" is a switch set to %s" % (name, key, value))
            continue

        settled = snap(info, float(value))
        if abs(settled - float(value)) > TOLERANCE:
            problems.append("%s.json: \"%s\" is %s, which clamps to %s outside %s..%s "
                            "step %s" % (name, key, value, round(settled, 6),
                                         info["low"], info["high"], info["step"] or "none"))

if problems:
    for problem in problems:
        print("check_ranges: " + problem)
    sys.exit(1)

print("check_ranges: %d values across %d presets, every one inside its parameter's range"
      % (checked, len(order)))
