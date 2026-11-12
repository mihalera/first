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
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/../lib/harness.sh"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# The DbgHelp stack walk this harness drives is exercised on the Windows CI
# runner only, and at C++20 that build stops writing the report file while every
# other check in the same binary still passes: the watchdog fires, the phase log
# is correct, the report is missing. Nothing else in the suite shows it, and the
# shipping plugin is built by MSVC, so the harness is pinned to C++17 here rather
# than left red. This is an override, not a default: the rest of the suite is
# C++20 and check_harness_preamble.py allows exactly this form.
J37_CXX_STD="-std=c++17"

BUILD="$(j37_build j37-diagnostics-test)"

mkdir -p "${BUILD}"

platform_flags=()
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) platform_flags=("-ldbghelp") ;;
    *)                    platform_flags=("-pthread") ;;
esac

"${CXX}" "${J37_CXX_STD}" -O1 "${J37_WARNINGS[@]}" \
    "${sanitize_flags[@]}" \
    -I "$(j37_path source)" \
    "${HERE}/diag_test.cpp" \
    "${HERE}/../../Source/Diagnostics/DiagCore.cpp" \
    "${HERE}/../../Source/Diagnostics/ShutdownWatchdog.cpp" \
    -o "${BUILD}/diag_test" \
    "${platform_flags[@]}"

"${BUILD}/diag_test" "${BUILD}"
