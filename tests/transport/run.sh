#!/usr/bin/env bash
#
# Builds and runs the transport regression harness.
#
#   tests/transport/run.sh
#
# The harness has no JUCE, no audio backend and no host dependency: it cuts the
# shipping TransportRig out of Source/PluginProcessor.h and drives it frame by
# frame, watching `platter()` - the one number the engine's level, modulation and
# speed response are all functions of. That makes it a few seconds of work on any
# machine with a C++17 compiler, which is why it can run on every pull request
# instead of only where the plugin is built.
#
# It exists because the two defects this transport shipped with were GESTURES -
# START did nothing on a running machine, and STOP approached rest without ever
# arriving - and a gesture is not visible in the source. It is visible in what the
# rig does over the seconds after a key is pressed.
#
# J37_SANITIZE=address,undefined rides along with the debug build in the workflow,
# the same way the other three harnesses do.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${TMPDIR:-/tmp}/j37-transport-test"
CXX="${CXX:-g++}"

mkdir -p "${BUILD}"

python3 "${HERE}/extract.py" "${BUILD}/extracted_transport.inc"

sanitize_flags=()
if [ -n "${J37_SANITIZE:-}" ]; then
    sanitize_flags=("-fsanitize=${J37_SANITIZE}" "-fno-sanitize-recover=all"
                    "-fno-omit-frame-pointer" "-g")
    echo "sanitizers: ${J37_SANITIZE}"
fi

"${CXX}" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
    "${sanitize_flags[@]}" \
    -I "${HERE}" -I "${BUILD}" \
    "${HERE}/transport_test.cpp" -o "${BUILD}/transport_test"

"${BUILD}/transport_test"
