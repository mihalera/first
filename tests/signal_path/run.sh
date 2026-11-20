#!/usr/bin/env bash
#
# Builds and runs the SIGNAL PATH block-setup harness.
#
#   tests/signal_path/run.sh
#
# The harness has no JUCE, no audio backend and no host dependency: it cuts the
# shipping block-setup helpers out of Source/ProcessorSignalPath.cpp (by
# signature, brace-matched, via extract.py) and drives them directly. That makes
# it a few seconds of work on any machine with a C++ compiler, which is why it
# can run on every pull request instead of only where the plugin is built.
#
# What is measured:
#
#   * configureTrackGeometry  the track-layout geometry per block. The caller in
#     the shipping file clamps the TRACKS parameter before it calls, so the
#     helper's own clamp is the second line of defence; this harness is the
#     caller that does NOT clamp, and the point is that the helper still holds:
#     out-of-range indices arrive at the OFF layout or the right table row,
#     never past the end of a three-row constexpr array. Every in-range layout
#     is checked against the shipping numbers in Source/DSP/TrackBleed.h, so
#     the state fields, the noise scale and the tables cannot drift apart.
#   * nextNoise               the tape-floor LCG. The hot loop calls it per
#     sample, so it is pinned for its invariants rather than its exact values:
#     reproducible for a given seed (the property the comment on the shipping
#     helper promises), bounded inside (-1, 1), not stuck at zero and
#     cycle-length correct (32 bits, so the state must return to its seed
#     exactly once per 2^32 steps - an early repeat is a broken LCG that the
#     ear would hear as a periodic hiss pattern).
#
# J37_SANITIZE=address,undefined rides along with the workflow's debug step, the
# same way the other harnesses do - and here it is the out-of-range index that
# earns its place: an unsanitised table read past a three-element array is
# exactly what the sanitizers exist to catch.
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/../lib/harness.sh"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$(j37_build j37-signal-path-test)"

# The generated include says `#include "shim.h"` and `#include "DSP/..."`, so
# both the shared shim directory and Source/ are on the include path - the same
# shape tests/dsp/run.sh uses.
SHIM="$(j37_path tests)/dsp"
SOURCE="$(j37_path source)"

mkdir -p "${BUILD}"

python3 "${HERE}/extract.py" "${BUILD}/extracted_signal_path.inc"

"${CXX}" "${J37_CXX_STD}" -O2 "${J37_WARNINGS[@]}" \
    "${sanitize_flags[@]}" \
    -I "${HERE}" -I "${SHIM}" -I "${SOURCE}" \
    "${HERE}/signal_path_test.cpp" -o "${BUILD}/signal_path_test"

"${BUILD}/signal_path_test"
