#!/usr/bin/env bash
#
# Builds and runs the multiband anti-phase harness.
#
#   tests/phase/run.sh
#
# The guard in Source/DSP/MultibandPhaseGuard.h is arithmetic on the two samples
# it is handed, so it runs here one frame at a time with no JUCE, no block size
# and no host - which is what makes its three claims measurable rather than
# audible: the Linkwitz-Riley bands sum flat, a healthy frame is untouched to the
# bit at full engagement, and a fault that lives in one band moves only that band.
#
# No extractor and no replica: the header is compiled as it ships.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${TMPDIR:-/tmp}/j37-phase-test"
CXX="${CXX:-g++}"

mkdir -p "${BUILD}"

# J37_SANITIZE=address,undefined puts the harness under those sanitizers, the
# same switch the other harnesses take; the guard runs per sample, so a stale
# filter state or a read past a band is exactly what they catch.
sanitize_flags=()
if [ -n "${J37_SANITIZE:-}" ]; then
    sanitize_flags=("-fsanitize=${J37_SANITIZE}" "-fno-sanitize-recover=all"
                    "-fno-omit-frame-pointer" "-g")
    echo "sanitizers: ${J37_SANITIZE}"
fi

"${CXX}" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
    "${sanitize_flags[@]}" \
    -I "${HERE}" -I "${HERE}/../../Source" \
    "${HERE}/phase_test.cpp" -o "${BUILD}/phase_test"

"${BUILD}/phase_test"
