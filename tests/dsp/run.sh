#!/usr/bin/env bash
#
# Builds and runs the SUBFUND regression harness.
#
#   tests/dsp/run.sh
#
# The harness has no JUCE, no audio backend and no host dependency: it cuts the
# shipping generator out of Source/, compiles it against a small shim and renders
# audio through a reduced replica of the signal path. That makes it a few seconds
# of work on any machine with a C++17 compiler, which is why it can run on every
# pull request instead of only where the plugin is built.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${TMPDIR:-/tmp}/subfund-dsp-test"
CXX="${CXX:-g++}"

mkdir -p "${BUILD}"

python3 "${HERE}/extract.py" "${BUILD}/extracted_dsp.inc"

# J37_SANITIZE=address,undefined puts the harness, and with it the shipped DSP it
# renders, under those sanitizers. The harness is seconds of work, so the
# sanitizer pass rides along with the debug build in the workflow rather than
# waiting for a separate sanitized build of the plugin.
sanitize_flags=()
if [ -n "${J37_SANITIZE:-}" ]; then
    sanitize_flags=("-fsanitize=${J37_SANITIZE}" "-fno-sanitize-recover=all"
                    "-fno-omit-frame-pointer" "-g")
    echo "sanitizers: ${J37_SANITIZE}"
fi

"${CXX}" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
    "${sanitize_flags[@]}" \
    -I "${HERE}" -I "${BUILD}" \
    "${HERE}/subfund_test.cpp" -o "${BUILD}/subfund_test"

"${BUILD}/subfund_test"
