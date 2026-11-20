#!/usr/bin/env python3
"""
The dirty-badge ID list, against the parameter table itself.

    python3 tests/parameters/check_dirty_badge.py

The editor's "edited" badge lights from one hand-maintained ID list in
Source/PluginProcessor.cpp (`parametersTrackedForDirtyBadge`). A new parameter
added to Source/ParameterTable.cpp but not to that list is a knob the user can
turn without the preset ever being marked dirty - a clean build and a silent
UI omission, exactly the drift class check_parameters.py exists for on the
wiring side. This is the same comparison in the other direction: the badge
list has to name every registered parameter, and nothing besides.

Known limitation, stated rather than hidden: it reads Source/ as text, so an
ID that appears only inside a comment counts as present - what this catches is
a forgotten registration, not an obfuscated one.
"""

import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parents[2]
parameter_table = (root / "Source" / "ParameterTable.cpp").read_text(encoding="utf-8")
processor = (root / "Source" / "PluginProcessor.cpp").read_text(encoding="utf-8")

registered = set(re.findall(r'ParameterID\s*\{\s*"([^"]+)"', parameter_table))
if not registered:
    sys.exit("check_dirty_badge: no ParameterID registrations found in "
             "Source/ParameterTable.cpp; the table may have moved")

anchor = "parametersTrackedForDirtyBadge"
if anchor not in processor:
    sys.exit("check_dirty_badge: `parametersTrackedForDirtyBadge` is gone from "
             "Source/PluginProcessor.cpp; the badge list moved or was renamed")

start = processor.index("static const auto ids", processor.index(anchor))
end = processor.index("return ids;", start)
tracked = set(re.findall(r'"([^"]+)"', processor[start:end]))

missing = sorted(registered - tracked)
unknown = sorted(tracked - registered)

print(f"check_dirty_badge: registered={len(registered)} tracked={len(tracked)}")
if missing:
    print("check_dirty_badge: registered but NOT badge-tracked (turning the knob "
          "will not mark the preset dirty):", ", ".join(missing))
if unknown:
    print("check_dirty_badge: badge-tracked but NOT registered (stale ids in the "
          "badge list):", ", ".join(unknown))

raise SystemExit(bool(missing or unknown))
