#!/usr/bin/env bash
#
# Builds and runs the MIX crossfade harness.
#
#   tests/mix_crossfader/run.sh
#
# The crossfade in Source/DSP/MixCrossfader.h is the gain pair for one MIX
# position plus the two gated legs the engine sums, so it runs here one frame at a
# time with no JUCE, no block size and no host - which is what makes its claims
# measurable rather than audible: 0 % is the input bit for bit, 100 % is the tape
# path, an uncorrelated pair holds its level across the travel while a correlated
# one is the +3 dB the law costs, the travel is continuous, and a closed transport
# gate empties BOTH legs.
#
# No extractor, no replica and no shim: the header names nothing of JUCE, so it is
# compiled as it ships with nothing but Source/ on the include path.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${TMPDIR:-/tmp}/j37-mix-crossfader-test"
CXX="${CXX:-g++}"

mkdir -p "${BUILD}"

# J37_SANITIZE=address,undefined puts the harness under those sanitizers, the same
# switch the other harnesses take. The stage is arithmetic on floats with no state,
# so what they would catch here is an out-of-range read in the harness rather than
# in the header.
sanitize_flags=()
if [ -n "${J37_SANITIZE:-}" ]; then
    sanitize_flags=("-fsanitize=${J37_SANITIZE}" "-fno-sanitize-recover=all"
                    "-fno-omit-frame-pointer" "-g")
    echo "sanitizers: ${J37_SANITIZE}"
fi

"${CXX}" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
    "${sanitize_flags[@]}" \
    -I "${HERE}/../../Source" \
    "${HERE}/mix_crossfader_test.cpp" -o "${BUILD}/mix_crossfader_test"

"${BUILD}/mix_crossfader_test"
