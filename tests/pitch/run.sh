#!/usr/bin/env bash
#
# Builds and runs the pitch-stage harness.
#
#   tests/pitch/run.sh
#
# The harness has no JUCE, no host and no audio backend: it cuts the shipping
# PitchTracker and RealtimePitchShifter out of Source/ at build time and drives
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
BUILD="${TMPDIR:-/tmp}/j37-pitch-test"
CXX="${CXX:-g++}"

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
    -I "${HERE}" -I "${BUILD}" \
    "${HERE}/pitch_test.cpp" -o "${BUILD}/pitch_test"

"${BUILD}/pitch_test"
