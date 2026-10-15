#!/usr/bin/env python3
"""
Checks that every registered parameter is REACHED, and that everything reached is
registered.

    python3 tests/parameters/check_parameters.py

The complaint this exists for is a user pressing a control and hearing nothing -
"the knobs do nothing". Two failures produce exactly that, and neither is a
compile error, a crash, or anything a debug build says a word about:

  1. A FETCH THAT MISSES. The processor wires itself to the parameter table by
     NAME:

         driveParam = parameters.getRawParameterValue ("drive");

     `getRawParameterValue()` returns a POINTER, and a name that is not in the
     table returns nullptr rather than throwing. Every use site is therefore
     written defensively - `if (driveParam != nullptr)` - which turns a misspelt
     or renamed id into a control that is attached, automatable, saved in the
     session and read by nothing. The sound simply does not change, and the only
     evidence is a typo in one string. `"drivee"` and `"drive"` compile to the
     same thing.

  2. A PARAMETER NOTHING READS. A registered parameter that no `getRawParameterValue`
     (the engine) and no `getParameter` (the editor's attachment, a picker's own
     blend) ever names is a row in the host's automation list that does nothing at
     all - the host offers it, the session stores it, and no code path consumes it.

  3. A HANDLE WITH NO READER. A fetched `xxxParam` that is never dereferenced
     again - declared in PluginProcessor.h, assigned in the constructor, read
     nowhere - is the same silent nothing as (1) with the typo replaced by an
     unfinished refactor.

Three of these are invisible to the compiler and all three are visible to a text
read, which is why this is a Python check beside the preset and translation ones
rather than another harness: it compares two lists that can drift.

What it can and cannot see:

  * It reads `Source/` as text. A name that appears only in a COMMENT counts as
    reached, the same limitation tests/presets/check_presets.py has, and it is
    worth stating rather than hiding: what this catches is a wiring edit, not a
    deliberately obfuscated one.
  * It checks NAME-level agreement, not audibility. A parameter whose value is
    read and then multiplied by zero is wired by this check's definition and
    silent in fact; proving otherwise needs the audio, which is what
    tests/dsp/ renders.

The one registration that is deliberately read only through `getParameter` - the
interface LANGUAGE, which is a setting of the editor and not of the machine - is
written down in INTENTIONALLY_EDITOR_ONLY below rather than tolerated by a
weakened rule, and every run prints it, so an exception cannot hide in a passing
check.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SOURCE = os.path.join(ROOT, "Source")
TABLE = os.path.join(SOURCE, "ParameterTable.cpp")

# The one registration that is not a control on the machine: the interface
# LANGUAGE is a setting of the EDITOR, saved with the session so a collaborator
# opens what the sender saw. It is reached through the ComboBox attachment in
# Source/GUI/EditorSetup.cpp rather than through a DSP handle, which is why it
# has no `getRawParameterValue` - and why naming it here is better than
# weakening the check for everything else.
INTENTIONALLY_EDITOR_ONLY = {
    "language": "Source/GUI/EditorSetup.cpp - bound to the ComboBox, read by the panel",
}

problems = []


def read(path):
    with open(path, encoding="utf-8") as handle:
        return handle.read()


def source_files():
    """Every C++ file the plugin builds, as (path, list of lines)."""
    for directory, _, names in os.walk(SOURCE):
        for name in sorted(names):
            if name.endswith((".cpp", ".h", ".mm")):
                path = os.path.join(directory, name)
                yield os.path.relpath(path, ROOT), read(path).splitlines()


FILES = list(source_files())
if not FILES:
    sys.exit("check_parameters: no C++ files under Source/")


def hits(pattern):
    """(name, file, line number) for every `pattern` match, in tree order."""
    found = []
    compiled = re.compile(pattern)
    for path, lines in FILES:
        for number, line in enumerate(lines, 1):
            for match in compiled.finditer(line):
                found.append((match.group(1), path, number))
    return found


# ---------------------------------------------------------------------------
# 1. The table: what the plugin registers with the host.
# ---------------------------------------------------------------------------
table = read(TABLE)
registered = re.findall(r"juce::ParameterID \{\s*\"([^\"]+)\"", table)
if not registered:
    sys.exit("check_parameters: no `juce::ParameterID { \"id\"` registration found in "
             "Source/ParameterTable.cpp; the table may have moved")

duplicates = sorted({name for name in registered if registered.count(name) > 1})
for name in duplicates:
    problems.append("Source/ParameterTable.cpp registers \"%s\" more than once: the host's "
                    "parameter list is keyed by id, so the two rows are one" % name)

# ---------------------------------------------------------------------------
# 2. What the engine and the editor reach for, by name.
#
#    Both spellings matter and they are not interchangeable: getRawParameterValue
#    is the engine's handle (a pointer into the parameter's atomic), and
#    getParameter is the editor's (an AudioProcessorParameter, which is what an
#    attachment and a picker need). A typo is equally silent in either.
# ---------------------------------------------------------------------------
# The ids each lookup is asked for, by the name in its parentheses. The member it
# is stored into is a separate read (section 3), because the two lists do not have
# the same length and are not the same question: one is what the engine REACHES,
# the other is what it KEEPS.
fetch_ids = [name for name, _, _ in hits(r"getRawParameterValue\s*\(\s*\"([^\"]+)\"")]
lookup_ids = [name for name, _, _ in hits(r"getParameter\s*\(\s*\"([^\"]+)\"")]
reached = set(fetch_ids) | set(lookup_ids)

for name in sorted(reached - set(registered)):
    where = sorted({path for candidate, path, _ in hits(r"getParameter\s*\(\s*\"([^\"]+)\"")
                    if candidate == name}
                   | {path for candidate, path, _ in hits(r"getRawParameterValue\s*\(\s*\"([^\"]+)\"")
                      if candidate == name})
    problems.append("\"%s\" is read by name in %s but is not registered in "
                    "Source/ParameterTable.cpp - a name the table does not hold reaches "
                    "nothing (getRawParameterValue returns nullptr, getParameter returns "
                    "nullptr), so the control is silent"
                    % (name, ", ".join(where)))

for name in registered:
    if name in reached or name in INTENTIONALLY_EDITOR_ONLY:
        continue
    problems.append("Source/ParameterTable.cpp registers \"%s\", which nothing reads: no "
                    "getRawParameterValue and no getParameter names it, so the host offers a "
                    "parameter that no code path consumes" % name)

for name in sorted(set(INTENTIONALLY_EDITOR_ONLY) - set(registered)):
    problems.append("INTENTIONALLY_EDITOR_ONLY names \"%s\", which is not a registered "
                    "parameter any more" % name)

# ---------------------------------------------------------------------------
# 3. The handles the engine keeps: fetched, and then actually read.
#
#    A line counts as a READ when it is neither the member's declaration
#    (PluginProcessor.h) nor the fetch that assigns it. The declaration and the
#    fetch are the two lines every wired parameter has; the third is the engine.
# ---------------------------------------------------------------------------
DECLARATION = re.compile(r"std::atomic<float>\s*\*\s*(\w+Param)\s*=")
FETCH_LINE = re.compile(r"^\s*(\w+Param)\s*=\s*[^;]*getRawParameterValue")

declared = set()
for path, lines in FILES:
    for line in lines:
        match = DECLARATION.search(line)
        if match:
            declared.add(match.group(1))

fetched_members = set()
for name, path, number in hits(r"(\w+Param)\s*=\s*(?:parameters|state|processor\.parameters)\s*"
                               r"\.?\s*getRawParameterValue\s*\(\s*\"([^\"]+)\""):
    fetched_members.add(name)

for member in sorted(fetched_members - declared):
    problems.append("%s is assigned from a parameter but is not declared as a member of the "
                    "processor in Source/PluginProcessor.h" % member)

for member in sorted(declared - fetched_members):
    problems.append("%s is declared on the processor but no getRawParameterValue assigns it, "
                    "so whatever it holds is never a parameter value" % member)

for member in sorted(declared | fetched_members):
    reads = []
    for path, lines in FILES:
        for number, line in enumerate(lines, 1):
            if re.search(r"\b%s\b" % re.escape(member), line) is None:
                continue
            if DECLARATION.search(line) or FETCH_LINE.match(line):
                continue
            reads.append("%s:%d" % (path, number))

    if not reads:
        problems.append("%s is fetched from the table and read nowhere else: the parameter "
                        "is wired to a handle the engine never dereferences, which is the "
                        "\"the knob does nothing\" case" % member)

# ---------------------------------------------------------------------------
if problems:
    for problem in problems:
        print("check_parameters: " + problem)
    sys.exit(1)

print("check_parameters: %d parameters registered, %d fetched by the engine and %d read "
      "through the editor's own lookup"
      % (len(registered), len(set(fetch_ids)), len(set(lookup_ids) - set(fetch_ids))))
print("check_parameters: %d of %d handles are read on the signal path, %d named lookups all "
      "resolve"
      % (len(fetched_members), len(declared | fetched_members), len(reached)))
for name, why in sorted(INTENTIONALLY_EDITOR_ONLY.items()):
    print("check_parameters: \"%s\" is read only through the editor - %s" % (name, why))
