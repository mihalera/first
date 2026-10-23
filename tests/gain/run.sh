#!/usr/bin/env bash
#
# Builds and runs the AUTO GAIN compensator harness.
#
#   tests/gain/run.sh
#
# The compensator in Source/DSP/AutoGainCompensator.h is arithmetic on the two
# frame powers it is handed and one coefficient, so it runs here one frame at a
# time with no JUCE, no block size and no host - which is what makes its claims
# measurable rather than audible: a healthy programme is untouched to the bit, a
# -6 dB programme comes back at the level it arrived at, nothing is ever boosted,
# the ceiling holds at 12 dB, silence holds the state instead of driving it, and a
# 10 ms dip is not chased.
#
# No extractor and no replica: the header is compiled as it ships.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${TMPDIR:-/tmp}/j37-gain-test"
CXX="${CXX:-g++}"

mkdir -p "${BUILD}"

# J37_SANITIZE=address,undefined puts the harness under those sanitizers, the same
# switch the other harnesses take. The stage runs per sample with one float of
# state, so what they would catch here is an out-of-range read in the harness
# rather than in the header - which is why this rides along with the others'
# sanitizer step rather than waiting for one of its own.
sanitize_flags=()
if [ -n "${J37_SANITIZE:-}" ]; then
    sanitize_flags=("-fsanitize=${J37_SANITIZE}" "-fno-sanitize-recover=all"
                    "-fno-omit-frame-pointer" "-g")
    echo "sanitizers: ${J37_SANITIZE}"
fi

"${CXX}" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
    "${sanitize_flags[@]}" \
    -I "${HERE}/../../Source" \
    "${HERE}/gain_test.cpp" -o "${BUILD}/gain_test"

"${BUILD}/gain_test"
