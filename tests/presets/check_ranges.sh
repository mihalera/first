#!/usr/bin/env bash
#
# Builds the preset range check and runs it.
#
#   tests/presets/check_ranges.sh
#
# The ranges a preset must respect live in Source/ParameterTable.cpp, built
# through named locals, helpers and constants, so they are read out of the
# shipping table itself rather than re-parsed in Python: tests/state/extract.py
# cuts createParameterLayout() out of the source, tests/presets/range_dump.cpp
# prints one line per parameter against the same stub toolkit the state harness
# uses, and tests/presets/check_ranges.py compares every preset value with it.
#
# A few seconds on any machine with a C++17 compiler, and it catches the class of
# defect that cannot fail at runtime - a preset asking for a value the parameter
# clamps, which loads as a different sound than the file describes.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "${HERE}/../.." && pwd)"
BUILD="${TMPDIR:-/tmp}/j37-preset-ranges"
CXX="${CXX:-g++}"

mkdir -p "${BUILD}"

python3 "${REPO}/tests/state/extract.py" "${BUILD}" > /dev/null

"${CXX}" -std=c++17 -O1 -I "${REPO}/tests/state" -I "${BUILD}" \
    "${HERE}/range_dump.cpp" "${REPO}/tests/state/juce_stub.cpp" \
    -o "${BUILD}/range_dump"

"${BUILD}/range_dump" > "${BUILD}/ranges.txt"

python3 "${HERE}/check_ranges.py" "${BUILD}/ranges.txt"
