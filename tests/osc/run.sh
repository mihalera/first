#!/usr/bin/env bash
#
# Builds and runs the OSC harness.
#
#   tests/osc/run.sh
#
# Source/Net/Osc.h and Source/Net/OscSurface.h are STAGED: they are compiled by the
# plugin and constructed by nothing, so no build and no user can tell whether the
# decoder pads a string correctly. That is what this runs - the codec against the
# bytes the specification fixes, in both directions, plus the address surface's
# refusals. No JUCE, no socket, no toolkit: Source/ is on the include path and the
# headers are JUCE-free on purpose, so this is a few seconds of work on every pull
# request rather than a manual instruction.
#
# J37_SANITIZE=address,undefined rides along with the workflow's debug step, the same
# way the other harnesses do - and here it earns its place in a different way from
# the DSP ones: the decoder reads lengths out of a packet, which is the shape of code
# an address sanitizer is for.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${TMPDIR:-/tmp}/j37-osc-test"
CXX="${CXX:-g++}"

# The headers are included as `Net/Osc.h`, the way the plugin includes them, so
# Source/ is on the path.
SOURCE="${HERE}/../../Source"

mkdir -p "${BUILD}"

sanitize_flags=()
if [ -n "${J37_SANITIZE:-}" ]; then
    sanitize_flags=("-fsanitize=${J37_SANITIZE}" "-fno-sanitize-recover=all"
                    "-fno-omit-frame-pointer" "-g")
    echo "sanitizers: ${J37_SANITIZE}"
fi

"${CXX}" -std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter \
    "${sanitize_flags[@]}" \
    -I "${SOURCE}" \
    "${HERE}/osc_test.cpp" -o "${BUILD}/osc_test"

"${BUILD}/osc_test"
