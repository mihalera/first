#!/usr/bin/env python3
"""Fail when a build log carries a compiler warning from this repository's code.

A warning from ``Source/`` or ``tests/`` is a caller to fix; a warning from
``JUCE/``, ``ThirdParty/`` or a fetched ``deps/`` tree is somebody else's code and
is reported as an ignored count only. The rule is deliberately one-sided: the
project's own sources are held to zero, and vendored code is never read.

Usage:

    python3 tests/build/check_compiler_warnings.py <log> [<log> ...]
    python3 tests/build/check_compiler_warnings.py --self-test

Exit status: 0 when no warning comes from own code, 1 when one does, 2 when a
log named on the command line is missing (a wiring mistake, not a warning).

Both spellings a compiler uses are read - GCC/Clang's ``file:line:col: warning:``
and MSVC's ``file(line,col): warning C4244:`` - because the gate runs in the
Linux, macOS, Windows and Android jobs alike.
"""

import re
import sys

#  A warning line's location: GCC/Clang first, then MSVC. Each captures the path
#  and the line number so a failure can name the place, not just the message.
GCC_LOCATION = re.compile(
    r"(?P<path>[^\s:()\[\]]+?):(?P<line>\d+):(?P<column>\d+):\s*(?:fatal\s+)?warning:")
MSVC_LOCATION = re.compile(
    r"(?P<path>[^\s:()\[\]]+?)\((?P<line>\d+)(?:,\s*(?P<column>\d+))?\):\s*warning\s+C\d+:")

#  CI steps are tee'd, so a line can still carry the timestamp Actions puts in
#  front of every one of them.
TIMESTAMP = re.compile(r"^\d{4}-\d\d-\d\dT[\d:.]+Z ")
#  A downloaded Actions log keeps the colour codes the runner wrote.
COLOUR_CODES = re.compile(r"\x1b\[[0-9;]*m")

#  A directory that makes a path somebody else's code, whatever else it holds.
FOREIGN_DIRECTORIES = {"ThirdParty", "JUCE", "deps", "_deps", "modules"}
#  The roots this repository owns.
OWN_DIRECTORIES = {"Source", "tests"}


def classify(path):
    """'own', 'foreign' or 'other' - which side of the gate a path is on."""
    segments = [s for s in path.replace("\\", "/").split("/") if s not in ("", ".")]
    if any(segment in FOREIGN_DIRECTORIES for segment in segments):
        return "foreign"
    if any(segment in OWN_DIRECTORIES for segment in segments):
        return "own"
    return "other"


def scan(lines):
    """The own-code warnings in `lines`, and how many foreign ones were ignored.

    Returns (warnings, ignored): `warnings` is a list of (path, line, text) with
    duplicates counted once, `ignored` is the number of compiler warnings that
    came from vendored trees or from outside the repository.
    """
    warnings = {}
    ignored = 0
    for raw in lines:
        line = TIMESTAMP.sub("", COLOUR_CODES.sub("", raw.rstrip("\n")))
        match = GCC_LOCATION.search(line) or MSVC_LOCATION.search(line)
        if match is None:
            continue

        path = match.group("path")
        kind = classify(path)
        if kind == "foreign":
            ignored += 1
            continue
        if kind != "own":
            continue

        #  The message starts where the location ends; drop the column/location
        #  prefix so the report reads like the compiler's own line.
        text = line[match.end():].strip() or line.strip()
        key = (path, int(match.group("line")), text)
        warnings[key] = warnings.get(key, 0) + 1
    return warnings, ignored


def report(warnings, ignored, source):
    """Print one block per log; returns the number of own-code warnings."""
    for (path, line, text), count in sorted(warnings.items()):
        suffix = f"  (x{count})" if count > 1 else ""
        print(f"{path}:{line}: warning: {text}{suffix}")
    total = sum(warnings.values())
    print(
        f"check_compiler_warnings: {total} warning(s) from Source/ and tests/ "
        f"({ignored} from JUCE/ThirdParty ignored) - {source}"
    )
    return total


def read_log(path):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            return handle.readlines()
    except FileNotFoundError:
        print(f"check_compiler_warnings: no such log: {path}", file=sys.stderr)
        return None


SELF_TEST_FIXTURES = [
    #  (line, expected classification) - GCC, Clang, MSVC, and the trees the gate
    #  must never look at.
    ("../Source/DSP/Fx/FxPitch.h:541:22: warning: comparing floating-point "
     "with '==' or '!=' is unsafe [-Wfloat-equal]", "own"),
    ("../../tests/parameter_audio.cpp:282:76: warning: conversion to 'std::size_t' "
     "may change the sign of the result [-Wsign-conversion]", "own"),
    ("D:/a/first/first/Source/GUI/PanelPaint.h:41:5: warning: unused variable 'x' "
     "[-Wunused-variable]", "own"),
    ("C:\\a\\first\\Source\\PluginEditor.cpp(120,4): warning C4244: "
     "'argument': conversion from 'double' to 'float'", "own"),
    ("../../ThirdParty/eigen/Eigen/src/Core/PacketMath.h:77:3: warning: "
     "unused parameter 'x' [-Wunused-parameter]", "foreign"),
    ("../deps/foleys_gui_magic/modules/foleys_gui_magic/Helpers/x.h:9:1: warning: "
     "declaration shadows a member [-Wshadow]", "foreign"),
    ("../JUCE/modules/juce_core/system/juce_String.cpp:12:3: warning: "
     "unused variable [-Wunused-variable]", "foreign"),
    ("/usr/include/c++/13/bits/std_function.h:431:9: warning: unused parameter "
     "'__x' [-Wunused-parameter]", "other"),
    ("[ 42%] Built target first", "other"),
]


def self_test():
    failures = 0
    for line, expected in SELF_TEST_FIXTURES:
        warnings, ignored = scan([line + "\n"])
        found = "own" if warnings else ("foreign" if ignored else "other")
        if found != expected:
            print(f"self-test FAILED: {line!r} classified {found}, expected {expected}")
            failures += 1
    if failures:
        return 1
    print(f"check_compiler_warnings: self-test passed ({len(SELF_TEST_FIXTURES)} lines)")
    return 0


def main(argv):
    if "--self-test" in argv:
        return self_test()

    logs = [arg for arg in argv[1:] if not arg.startswith("-")]
    if not logs:
        print("usage: check_compiler_warnings.py LOG [LOG ...] | --self-test", file=sys.stderr)
        return 2

    total = 0
    for path in logs:
        lines = read_log(path)
        if lines is None:
            return 2
        warnings, ignored = scan(lines)
        total += report(warnings, ignored, path)
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
