#!/usr/bin/env bash
#
# Builds and runs the saved-state round-trip harness.
#
#   tests/state/run.sh
#
# The harness has no JUCE, no host and no audio backend: it cuts the shipping
# parameter table and the state functions out of Source/ at build time, compiles
# them against a stub of the toolkit, writes a session state, reads it back into a
# fresh instance and loads states an older build would have written. That makes it
# a few seconds of work on any machine with a C++17 compiler - fast enough to run
# on every pull request, and cheap enough to run under the sanitizers, which is
# where a bad pointer or an out-of-bounds read in the state path would otherwise
# only ever show up in someone's session.
#
# J37_SANITIZE=address,undefined builds the harness with those sanitizers and
# turns them loose on the whole round trip; see build-linux-debug in the workflow.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${TMPDIR:-/tmp}/j37-state-test"
CXX="${CXX:-g++}"

mkdir -p "${BUILD}"

python3 "${HERE}/extract.py" "${BUILD}"

sanitize_flags=()
if [ -n "${J37_SANITIZE:-}" ]; then
    sanitize_flags=("-fsanitize=${J37_SANITIZE}" "-fno-sanitize-recover=all"
                    "-fno-omit-frame-pointer" "-g")
    echo "sanitizers: ${J37_SANITIZE}"
fi

"${CXX}" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
    "${sanitize_flags[@]}" \
    -I "${HERE}" -I "${BUILD}" \
    "${HERE}/juce_stub.cpp" "${HERE}/state_test.cpp" -o "${BUILD}/state_test"

"${BUILD}/state_test"
