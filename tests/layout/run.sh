#!/usr/bin/env bash
#
# Builds and runs the knob layout harness.
#
#   tests/layout/run.sh
#
# The harness has no JUCE, no window and no host dependency: it cuts the strip's
# arithmetic out of Source/PluginEditor.cpp, compiles it against a stub of the
# toolkit it binds to, and walks every page at four panel sizes. That makes it
# seconds of work on any machine with a C++17 compiler, so a change to the knob
# geometry can be measured on every pull request instead of only where the
# plugin is built - and measured as the disc the paint routine will draw, not as
# the numbers a comment claims.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${TMPDIR:-/tmp}/j37-layout-test"
CXX="${CXX:-g++}"

mkdir -p "${BUILD}"

python3 "${HERE}/extract.py" "${BUILD}"

# J37_SANITIZE=address,undefined puts the harness, and the layout arithmetic it
# compiles out of Source/, under those sanitizers - see the sanitizer step in
# build-linux-debug.
sanitize_flags=()
if [ -n "${J37_SANITIZE:-}" ]; then
    sanitize_flags=("-fsanitize=${J37_SANITIZE}" "-fno-sanitize-recover=all"
                    "-fno-omit-frame-pointer" "-g")
    echo "sanitizers: ${J37_SANITIZE}"
fi

"${CXX}" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
    "${sanitize_flags[@]}" \
    -I "${HERE}" -I "${BUILD}" \
    "${HERE}/layout_test.cpp" -o "${BUILD}/layout_test"

"${BUILD}/layout_test"
