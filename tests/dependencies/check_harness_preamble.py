#!/usr/bin/env python3
"""
Checks that every harness uses tests/lib/harness.sh instead of its own preamble.

    python3 tests/dependencies/check_harness_preamble.py

Twenty-eight harnesses used to derive the repository root, the compiler, the
sanitizer flags, the build directory and every dependency's include path by hand.
That is what made a one-line dependency change - signalsmith-linear becoming a
sibling checkout instead of a directory inside the stretch one - into three edits
in three syntaxes, one of which failed silently because a comment sat inside a
backslash-continued argument list.

The seam only stays a seam if nothing quietly goes back to spelling it out, so
this check enforces four rules over tests/**/*.sh:

  1. every harness sources tests/lib/harness.sh;
  2. no harness defaults the compiler itself (CXX="${CXX:-...");
  3. no harness builds its own sanitizer_flags array;
  4. include flags name a dependency - -I "$(j37_path dr_libs)" - rather than a
     path into Source/ or ThirdParty/, and every name asked for is one the
     preamble knows.

Comments are ignored: the harnesses explain their include choices at length, and
those explanations are the documentation for why a given binary sees a library.

Exit code 0 when every harness uses the seam, 1 otherwise.
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LIB = os.path.join(ROOT, "tests", "lib", "harness.sh")
SOURCE_LINE = "lib/harness.sh"

problems = []

with open(LIB, encoding="utf-8") as handle:
    library = handle.read()

known = set(re.findall(r"^\s{8}([a-z_/]+)\)\s+printf", library, re.M))
if not known:
    problems.append("tests/lib/harness.sh: no j37_path names could be read")

FORBIDDEN = [
    (re.compile(r'CXX="\$\{CXX:-'), 'defaults the compiler instead of using the preamble'),
    (re.compile(r'^\s*sanitize_flags=\(\)', re.M), 'builds its own sanitizer_flags array'),
    (re.compile(r'-std=c\+\+[0-9]+'), 'picks the C++ standard itself'),
    (re.compile(r'\s-Wall'), 'picks the warning set itself'),
]

# The shipping include roots. A harness may still add a directory of its own -
# tests/presets reaches tests/state, tests/unit reaches tests/dsp for the shared
# shim, tests/editor adds its stub tree - and it may still name a .cpp by path as
# a compile input, which is not an include path at all. What it may not do is
# spell out where a dependency lives.
SHIPPING_ROOTS = re.compile(
    r'-(?:I|isystem)\s+"\$\{(?:REPO|HERE)\}(?:/\.\./\.\.)?/(?:Source|ThirdParty|JUCE)')


def harnesses():
    for path in sorted(glob.glob(os.path.join(ROOT, "tests", "**", "*.sh"), recursive=True)):
        if os.path.dirname(path) == os.path.join(ROOT, "tests", "lib"):
            continue
        yield path


count = 0
for path in harnesses():
    count += 1
    rel = os.path.relpath(path, ROOT).replace("\\", "/")
    text = open(path, encoding="utf-8").read()

    if SOURCE_LINE not in text:
        problems.append("%s does not source tests/lib/harness.sh" % rel)

    # Strip whole-line comments so prose about a dependency is not mistaken for a
    # hand-written flag.
    code = "\n".join(line for line in text.splitlines() if not line.lstrip().startswith("#"))

    for pattern, description in FORBIDDEN:
        if pattern.search(code):
            problems.append("%s %s" % (rel, description))

    match = SHIPPING_ROOTS.search(code)
    if match:
        problems.append("%s writes the shipping include path %s by hand - ask the preamble "
                        "with $(j37_path <name>)" % (rel, match.group(0)))

    for name in re.findall(r"j37_path ([a-z_/]+)\)", code):
        if name not in known:
            problems.append("%s asks j37_path for unknown dependency '%s'" % (rel, name))

print("check_harness_preamble: %d harness(es), %d include name(s) known to the preamble"
      % (count, len(known)))

if problems:
    for problem in problems:
        print("check_harness_preamble: " + problem)
    sys.exit(1)

print("check_harness_preamble: every harness uses the shared preamble")
