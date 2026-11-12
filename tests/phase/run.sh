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
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/../lib/harness.sh"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$(j37_build j37-phase-test)"

mkdir -p "${BUILD}"

# J37_SANITIZE=address,undefined puts the harness under those sanitizers, the
# same switch the other harnesses take; the guard runs per sample, so a stale
# filter state or a read past a band is exactly what they catch.

"${CXX}" "${J37_CXX_STD}" -O2 "${J37_WARNINGS[@]}" \
    "${sanitize_flags[@]}" \
    -I "${HERE}" -I "$(j37_path source)" \
    "${HERE}/phase_test.cpp" -o "${BUILD}/phase_test"

"${BUILD}/phase_test"
