#!/usr/bin/env python3
"""
Checks that every macro the build DEFINES for the compiler is READ by the code
the build compiles - and that the two switches the plugin's vendor libraries
depend on are the ones their library actually looks at.

    python3 tests/definitions/check_definitions.py

The defect this exists for was found in `cmake/J37VendorLibraries.cmake`, which
discovered Intel IPP and Apple vDSP, linked them, and then defined J37_HAVE_IPP
and J37_HAVE_VDSP for the plugin - two macros that no file under `Source/` has
ever tested. The build was telling the compiler something that meant nothing to
anybody, and the file's own prose read as if the DSP took a vDSP path and a
portable path depending on them. It could not have: a definition with no reader
is not a feature, it is a claim about the build that nobody checks.

That is the general shape - `CMakeLists.txt` defines something, `Source/` reads
it, or one of the two is wrong - and it is the same shape
`tests/parameters/check_parameters.py` checks for parameter ids and
`tests/shaders/check_shaders.py` checks for uniform names. It is a text check
here for the same reason they are: both sides are text, both can drift, and
nothing in the C++ compiler or in CMake compares them.

What this check reads:

  * `CMakeLists.txt` and every `cmake/*.cmake`, comments removed, for
    `target_compile_definitions(...)` arguments and for `list(APPEND
    J37_*_DEFINITIONS ...)` items that name a `J37_*` macro. Variable
    references (`${J37_RENDER_DEFINITIONS}`) are not definitions and are
    skipped by name.
  * every file under `Source/`, comments removed, for a preprocessor read of
    that macro (`#if`, `#elif`, `#ifdef`, `#ifndef`, `defined(...)`).
  * `JUCE/modules/juce_dsp/`, for the IPP switch. This half needs the submodule:
    when it is not checked out the check says so and passes, because the rest of
    this suite runs without it and a missing submodule is not a defect in the
    build.

What it deliberately does NOT check:

  * Macros `Source/` defines for ITSELF - `J37_HAS_RTNEURAL` and the rest come
    from `Source/DSP/OptionalLibraries.h` and are probed at compile time rather
    than set by CMake. Two of them (SoundTouch, signalsmith-stretch) say in
    their own entries that nothing reads them yet; that is a statement about a
    stage that has not adopted them, not a build macro with no reader, and this
    check is not the place to relitigate it.
  * What a macro does when it IS read. This is a wiring check: a name that is
    read and then ignored passes here and fails in the audio, where
    `tests/dsp/` and the harnesses live.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SOURCE = os.path.join(ROOT, "Source")
CMAKE_DIR = os.path.join(ROOT, "cmake")
JUCE_DSP = os.path.join(ROOT, "JUCE", "modules", "juce_dsp")

# The switch JUCE's own FFT engine is gated on, and the flag that does not exist
# in JUCE 9.0.4 however often it is named online. Both spellings are asserted
# below, in opposite directions.
JUCE_IPP_SWITCH = "_IPP_SEQUENTIAL_STATIC"
JUCE_IPP_FLAG_THAT_DOES_NOT_EXIST = "JUCE_DSP_USE_INTEL_IPP"

problems = []


def read(path):
    with open(path, encoding="utf-8") as handle:
        return handle.read()


def without_comments(text):
    """C and C++ comments removed, because prose about a macro is not a use of one
    - this repository explains itself in comments, and a check that does not
    strip them reads its own explanation back as evidence. Preprocessor lines are
    the point of this check and are left alone."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def without_cmake_comments(text):
    """The same for CMake, whose comments start with `#` - including the `#` of a
    `# comment`, and not the `#` of anything else, since CMake has no
    preprocessor of its own."""
    return re.sub(r"(?m)#[^\n]*", " ", text)


def call_arguments(text, name):
    """The argument list of every `name(...)` call, with the parens matched."""
    found = []

    for match in re.finditer(r"\b%s\s*\(" % re.escape(name), text):
        depth = 1
        index = match.end()

        while index < len(text) and depth:
            if text[index] == "(":
                depth += 1
            elif text[index] == ")":
                depth -= 1
            index += 1

        found.append(text[match.end():index - 1])

    return found


# ---------------------------------------------------------------------------
# 1. What the build defines, and who reads it.
# ---------------------------------------------------------------------------
definitions = {}

cmake_sources = [os.path.join(ROOT, "CMakeLists.txt")]

for name in sorted(os.listdir(CMAKE_DIR)):
    if name.endswith(".cmake"):
        cmake_sources.append(os.path.join(CMAKE_DIR, name))

for path in cmake_sources:
    relative = os.path.relpath(path, ROOT)
    text = without_cmake_comments(read(path))

    bodies = call_arguments(text, "target_compile_definitions")

    # `list(APPEND J37_RENDER_DEFINITIONS J37_RENDER_BACKEND_VULKAN=1)` - the
    # items after the list's own name are definitions like any other.
    for match in re.finditer(r"\blist\s*\(\s*APPEND\s+([A-Za-z0-9_]+)([^)]*)\)", text):
        bodies.append(match.group(2))

    for body in bodies:
        for macro in re.finditer(r"(?<![${])\bJ37_[A-Z0-9_]+\b", body):
            definitions.setdefault(macro.group(0), set()).add(relative)

if not definitions:
    problems.append("no J37_* macro is defined for the compiler anywhere in %s or %s - the "
                    "extraction above is reading something that is not the build"
                    % (os.path.relpath(cmake_sources[0], ROOT), os.path.relpath(CMAKE_DIR, ROOT)))

readers = {}
source_files = []

for directory, _, files in os.walk(SOURCE):
    for name in sorted(files):
        source_files.append(os.path.join(directory, name))

for path in source_files:
    text = without_comments(read(path))

    for macro in definitions:
        for match in re.finditer(r"\b%s\b" % re.escape(macro), text):
            line = text.rfind("\n", 0, match.start()) + 1
            line = text[line:text.find("\n", match.start())]

            if re.match(r"\s*#\s*(if|elif|ifdef|ifndef)\b", line) or \
               re.search(r"\bdefined\s*\(?\s*%s\b" % re.escape(macro), line):
                readers.setdefault(macro, set()).add(os.path.relpath(path, ROOT))
                break

for macro in sorted(set(definitions) - set(readers)):
    problems.append("%s defines %s for the compiler and no file under Source/ reads it: a "
                    "definition with no reader tells the build nothing"
                    % (" and ".join(sorted(definitions[macro])), macro))

# ---------------------------------------------------------------------------
# 2. The vendor switches, against the library that reads them.
#
# This is the pair the finding above came out of, so the check keeps an eye on
# it: the IPP switch has to be one JUCE's own sources gate on, it has to actually
# be thrown by cmake/J37VendorLibraries.cmake, and the flag that only exists in
# forum posts has to stay out of the build.
# ---------------------------------------------------------------------------
vendor = os.path.join(CMAKE_DIR, "J37VendorLibraries.cmake")
vendor_text = None

if os.path.isfile(vendor):
    vendor_text = without_cmake_comments(read(vendor))
else:
    problems.append("%s is not there, so the two vendor switches could not be read at all"
                    % os.path.relpath(vendor, ROOT))

if vendor_text is not None and JUCE_IPP_FLAG_THAT_DOES_NOT_EXIST in vendor_text:
    problems.append("%s defines %s, which JUCE 9.0.4 does not read anywhere: its Intel IPP "
                    "FFT engine is gated on the IPP linking-model macros instead"
                    % (os.path.relpath(vendor, ROOT), JUCE_IPP_FLAG_THAT_DOES_NOT_EXIST))

juce_checked = False

if os.path.isdir(JUCE_DSP):
    juce_checked = True

    gates = []

    for directory, _, files in os.walk(JUCE_DSP):
        for name in files:
            if name.endswith((".cpp", ".h")):
                gates.append(os.path.join(directory, name))

    readers_in_juce = [os.path.relpath(path, ROOT) for path in gates
                       if JUCE_IPP_SWITCH in without_comments(read(path))]

    if not readers_in_juce:
        problems.append("JUCE's sources no longer mention %s, so the switch %s throws is not "
                        "the one JUCE's FFT engine is gated on any more"
                        % (JUCE_IPP_SWITCH, os.path.relpath(vendor, ROOT)))

    if vendor_text is not None and JUCE_IPP_SWITCH not in vendor_text:
        problems.append("%s no longer throws %s, the IPP linking-model macro JUCE's FFT engine "
                        "is compiled under: an IPP install would be linked and accelerate nothing"
                        % (os.path.relpath(vendor, ROOT), JUCE_IPP_SWITCH))

if problems:
    for problem in problems:
        print("check_definitions: " + problem)
    sys.exit(1)

for macro in sorted(definitions):
    print("check_definitions: %-32s defined in %-28s read in %d file(s)"
          % (macro, " and ".join(sorted(definitions[macro])), len(readers[macro])))

print("check_definitions: %d macro(s) the build hands the compiler, all read under Source/"
      % len(definitions))

if juce_checked and vendor_text is not None:
    print("check_definitions: %s is thrown by %s and gated in %d JUCE source file(s)"
          % (JUCE_IPP_SWITCH, os.path.relpath(vendor, ROOT), len(readers_in_juce)))
elif not juce_checked:
    print("check_definitions: JUCE/modules/juce_dsp is not checked out, so the IPP switch was "
          "not compared against JUCE's own sources - this half did not run")
