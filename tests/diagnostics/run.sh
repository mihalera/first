#!/usr/bin/env bash
#
# Builds and runs the diagnostics harness.
#
#   tests/diagnostics/run.sh
#
# Source/Diagnostics/DiagCore.cpp and ShutdownWatchdog.cpp name no JUCE symbol,
# so this compiles them as they ship and runs the three states the plugin can be
# in: the switch off (no files), the switch on with a teardown that finishes (a
# phase trace and nothing else), and the switch on with a teardown that stops (a
# phase trace plus the stacks of the message, GL and audio threads). The stacks
# themselves are Windows-only in this build; on other platforms the report says
# so and the rest is checked the same way.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${TMPDIR:-/tmp}/j37-diagnostics-test"
CXX="${CXX:-g++}"

mkdir -p "${BUILD}"

platform_flags=()
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) platform_flags=("-ldbghelp") ;;
    *)                    platform_flags=("-pthread") ;;
esac

sanitize_flags=()
if [ -n "${J37_SANITIZE:-}" ]; then
    sanitize_flags=("-fsanitize=${J37_SANITIZE}" "-fno-sanitize-recover=all"
                    "-fno-omit-frame-pointer" "-g")
    echo "sanitizers: ${J37_SANITIZE}"
fi

"${CXX}" -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter \
    "${sanitize_flags[@]}" \
    -I "${HERE}/../../Source" \
    "${HERE}/diag_test.cpp" \
    "${HERE}/../../Source/Diagnostics/DiagCore.cpp" \
    "${HERE}/../../Source/Diagnostics/ShutdownWatchdog.cpp" \
    -o "${BUILD}/diag_test" \
    "${platform_flags[@]}"

"${BUILD}/diag_test" "${BUILD}"
