#!/usr/bin/env bash
#
# Builds and runs the pitch-stage harness.
#
#   tests/pitch/run.sh
#
# The harness has no JUCE, no host and no audio backend: it includes the shipping
# PitchTracker and RealtimePitchShifter from Source/DSP/ - the headers the plugin
# itself compiles, not a copy of their text - and drives
# them with generated signal - notes, notes with harmonics, notes buried in noise,
# silence, and tones through the shifter at several ratios. That makes it a
# couple of seconds of work on any machine with a C++17 compiler, fast enough to
# run on every pull request, and cheap enough to run under the sanitizers, which
# is where an out-of-range lag or a read past the delay line would otherwise only
# ever show up in someone's session.
#
# J37_SANITIZE=address,undefined builds the harness with those sanitizers and
# turns them loose on the whole run; see build-linux-debug in the workflow.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "${HERE}/../.." && pwd)"
BUILD="${TMPDIR:-/tmp}/j37-pitch-test"
CXX="${CXX:-g++}"

# The generated include names the stages as `#include "DSP/..."`, so the compiler
# needs Source/ on its include path - the same reason tests/dsp/run.sh puts it there.
# Source/ is part of the checkout, so this is the plugin's own header being
# compiled, not a copy of it.
SOURCE="${REPO}/Source"

mkdir -p "${BUILD}"

python3 "${HERE}/extract.py" "${BUILD}/extracted_pitch.inc"

sanitize_flags=()
if [ -n "${J37_SANITIZE:-}" ]; then
    sanitize_flags=("-fsanitize=${J37_SANITIZE}" "-fno-sanitize-recover=all"
                    "-fno-omit-frame-pointer" "-g")
    echo "sanitizers: ${J37_SANITIZE}"
fi

"${CXX}" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
    "${sanitize_flags[@]}" \
    -I "${HERE}" -I "${BUILD}" -I "${SOURCE}" \
    "${HERE}/pitch_test.cpp" -o "${BUILD}/pitch_test"

"${BUILD}/pitch_test"

# The same harness a second time with the corrector's OTHER engine, which is the
# pair of switches the plugin's own build has: -DJ37_CORRECTOR_USE_STRETCH=1 (the
# CMake option, OFF by default) and the library's include path. The binary above
# is the shape the plugin ships; this one is the shape the option selects. Both
# are run because the two make different promises, and both promises are asserted
# rather than described - the numbers behind the option's OFF default are printed
# by this run.
STRETCH="${REPO}/ThirdParty/signalsmith-stretch/include"

if [ -f "${STRETCH}/signalsmith-stretch/signalsmith-stretch.h" ]; then
    echo
    echo "--- the same harness with the corrector's vocoder engine (J37_CORRECTOR_USE_STRETCH) ---"

    "${CXX}" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
        "${sanitize_flags[@]}" -DJ37_CORRECTOR_USE_STRETCH=1 \
        -I "${HERE}" -I "${BUILD}" -I "${SOURCE}" -I "${STRETCH}" \
        "${HERE}/pitch_test.cpp" -o "${BUILD}/pitch_test_stretch"

    "${BUILD}/pitch_test_stretch"
else
    echo
    echo "note: ThirdParty/signalsmith-stretch is not checked out - the vocoder shape of"
    echo "      the corrector was not compiled or measured by this run."
fi
