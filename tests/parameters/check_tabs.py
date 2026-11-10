#!/usr/bin/env python3
"""
Checks the panel's PAGE table against itself, without compiling anything.

    python3 tests/parameters/check_tabs.py

Source/GUI/PanelTabs.h is a constexpr table and setCurrentTab() static_asserts
three things about it - the page count, that every knob is on exactly one page,
and that every page has a subtitle. Those asserts are the real contract, but they
only run in a build: a checkout with no compiler, a review on a phone, and the
moment before a push are exactly when a page edited by hand is wrong in a way
nobody sees until CI.

This file reads the same table as text and asks the same questions, plus the four
that came out of the family split:

  * the page count matches the page count in the header and in the subtitle list
  * every knob index is on exactly one page, and none is out of range
  * every page's family is one of the declared families
  * the families hold the counts setTabFamily() asserts (9 / 6 / 4 / 3)
  * SETUP is the LAST family - the engine switches are not part of the sound,
    which is why they are the one family whose position is a rule
  * the AUTOTUNE and COMP pages stay in FX rather than drifting into RACK: they
    process the performance, they are not pedals
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TABS = os.path.join(ROOT, "Source", "GUI", "PanelTabs.h")
HEADER = os.path.join(ROOT, "Source", "PluginEditor.h")
TEXT = os.path.join(ROOT, "Source", "GUI", "PanelText.h")

FAMILY_NAMES = {0: "TAPE", 1: "FX", 2: "RACK", 3: "SETUP"}
FAMILY_COUNTS = {0: 9, 1: 6, 2: 4, 3: 3}

problems = []


def read(path):
    with open(path, encoding="utf-8") as handle:
        return handle.read()


source = read(TABS)

# ---------------------------------------------------------------------------
# 1. The pages.
# ---------------------------------------------------------------------------
page_count_match = re.search(r"numTabPages\s*=\s*(\d+)", source)
if not page_count_match:
    sys.exit("check_tabs: numTabPages not found")

num_pages = int(page_count_match.group(1))

# Each entry is  { "NAME", "hint...", count, family, { indices } },
entries = re.findall(
    r'\{\s*"([A-Z ]+)",\s*(?:.*?),\s*(\d+),\s*(\d+),\s*\{([^}]*)\}\s*\}',
    source,
    re.S)

if len(entries) != num_pages:
    problems.append("the table holds %d pages, numTabPages says %d"
                    % (len(entries), num_pages))

titles = re.search(r"tabSubtitles\s*\{\s*\{(.*?)\}\s*\};", source, re.S)
if not titles:
    sys.exit("check_tabs: tabSubtitles not found")

subtitles = re.findall(r'"([^"]*)"', titles.group(1))
if len(subtitles) != len(entries):
    problems.append("%d pages carry %d subtitles" % (len(entries), len(subtitles)))

# ---------------------------------------------------------------------------
# 2. The knobs: each on exactly one page, none out of range.
# ---------------------------------------------------------------------------
control_count_match = re.search(r"controlCount\s*=\s*(\d+)", read(TEXT))
if not control_count_match:
    sys.exit("check_tabs: controlCount not found in PanelText.h")

control_count = int(control_count_match.group(1))

seen = {}
for name, declared, family, indices in entries:
    listed = [int(index) for index in re.findall(r"\d+", indices)]

    if int(declared) != len(listed):
        problems.append('page "%s" declares %s controls and lists %d'
                        % (name, declared, len(listed)))

    for index in listed:
        if index >= control_count:
            problems.append('page "%s" lists knob %d, past controlCount %d'
                            % (name, index, control_count))
        seen.setdefault(index, []).append(name)

for index in range(control_count):
    pages = seen.get(index, [])
    if not pages:
        problems.append("knob %d is on no page - it can never be reached" % index)
    elif len(pages) > 1:
        problems.append("knob %d is on %d pages (%s)" % (index, len(pages), ", ".join(pages)))

# ---------------------------------------------------------------------------
# 3. The families.
# ---------------------------------------------------------------------------
counts = {}
for name, _, family, _ in entries:
    family = int(family)
    if family not in FAMILY_NAMES:
        problems.append('page "%s" claims family %d, which does not exist' % (name, family))
        continue
    counts[family] = counts.get(family, 0) + 1

for family, expected in FAMILY_COUNTS.items():
    if counts.get(family, 0) != expected:
        problems.append("%s holds %d pages, setTabFamily() asserts %d"
                        % (FAMILY_NAMES[family], counts.get(family, 0), expected))

# SETUP is the last family in the table as well as in the bar: the phone's page
# list is the table in order, and a SETTINGS page above the sound pages there
# would put the engine switches in the middle of the rack.
last_family = int(entries[-1][2]) if entries else -1
if last_family != 3:
    problems.append("the last page belongs to family %d, SETUP (3) must be last" % last_family)

for page in ("AUTOTUNE", "COMP"):
    match = [entry for entry in entries if entry[0] == page]
    if not match:
        problems.append('the "%s" page is missing' % page)
    elif int(match[0][2]) != 1:
        problems.append('"%s" left the FX family: it processes the performance, '
                        "it is not a pedal" % page)

# ---------------------------------------------------------------------------
# 4. The editor agrees with the table.
# ---------------------------------------------------------------------------
editor_header = read(HEADER)

family_count_match = re.search(r"tabFamilyCount\s*=\s*(\d+)", editor_header)
if not family_count_match:
    problems.append("tabFamilyCount not found in PluginEditor.h")
elif int(family_count_match.group(1)) != len(FAMILY_NAMES):
    problems.append("tabFamilyCount is %s, the table has %d families"
                    % (family_count_match.group(1), len(FAMILY_NAMES)))

array_match = re.search(r"tabFamilyTabCount\s*\{([^}]*)\}", editor_header)
if not array_match:
    problems.append("tabFamilyTabCount not found in PluginEditor.h")
else:
    declared = [int(value) for value in re.findall(r"\d+", array_match.group(1))]
    expected = [FAMILY_COUNTS[family] for family in sorted(FAMILY_COUNTS)]
    if declared != expected:
        problems.append("tabFamilyTabCount is %s, the table holds %s" % (declared, expected))

# ---------------------------------------------------------------------------
if problems:
    for problem in problems:
        print("check_tabs: " + problem)
    sys.exit(1)

print("check_tabs: %d pages, %d knobs each on one page, families %s"
      % (num_pages, control_count,
         ", ".join("%s %d" % (FAMILY_NAMES[family], FAMILY_COUNTS[family])
                   for family in sorted(FAMILY_COUNTS))))
