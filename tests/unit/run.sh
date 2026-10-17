#!/usr/bin/env bash
#
# Builds and runs the DSP UNIT tests - the ones written against Catch2.
#
#   tests/unit/run.sh
#
# WHY THIS IS SEPARATE FROM tests/dsp/run.sh
#
# run.sh renders audio through the shipping signal path and asserts on what came
# out; that is the right tool for a signal-path regression and the wrong one for
# "does this one function still return what it returned yesterday". The two
# questions have different costs (a render versus a call), different failure
# stories (the plugin sounds wrong versus a number moved) and different owners -
# so they are different binaries. cmake/J37Tests.cmake states the split from the
# other side, and names Catch2 as the framework for the unit-sized pieces.
#
# WHY IT DOES NOT INVOKE CMAKE
#
# Same reason tests/dsp/run_bench.sh does not: the suite is meant to be seconds of
# work on any machine with a C++17 compiler, and a CMake configure would make
# these tests un-runnable on a machine that cannot configure the plugin at all.
# Catch2 is fetched by CPM under -DJ37_BUILD_TESTS=ON, so this script looks in
# the usual CPM cache locations, accepts CATCH2_DIR as an override, and - when it
# cannot find the framework - says exactly what to run instead of passing
# silently. A test suite that skips itself when its dependencies are missing
# reports success for having done nothing.
#
# It compiles the AMALGAMATED Catch2 (extras/catch_amalgamated.{hpp,cpp}) that
# every v3 checkout and every v3 release carries: one translation unit, no build
# system, and the same assertions whether it came from CPM or from the release
# tarball.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "${HERE}/../.." && pwd)"
BUILD="${TMPDIR:-/tmp}/j37-unit-tests"
CXX="${CXX:-g++}"

mkdir -p "${BUILD}"

# One entry per layout that has been seen in the wild: the project's own CPM
# cache keyed by package name (cmake/J37Dependencies.cmake points CPM_SOURCE_CACHE
# at .cpm-cache in the source tree) with or without the tag appended, then a build
# tree's _deps, which is where a plain `cmake -S . -B build` leaves it. The pairs
# are the spellings CPM has used for the directory name.
CATCH2_SRC=""
for candidate in \
    "${CATCH2_DIR:-}" \
    "${REPO}/.cpm-cache/Catch2/"* \
    "${REPO}/.cpm-cache/Catch2" \
    "${REPO}/.cpm-cache/catch2/"* \
    "${REPO}/.cpm-cache/catch2" \
    "${REPO}/build/_deps/catch2-src" \
    "${REPO}/build/_deps/Catch2-src" \
    "${REPO}/build/deps/Catch2" \
    "${REPO}/build/deps/catch2"
do
    if [ -n "${candidate}" ] && [ -f "${candidate}/extras/catch_amalgamated.cpp" ]; then
        CATCH2_SRC="${candidate}"
        break
    fi

    # A directory that holds the two amalgamated files directly.
    if [ -n "${candidate}" ] && [ -f "${candidate}/catch_amalgamated.cpp" ]; then
        CATCH2_SRC="${candidate}"
        break
    fi
done

if [ -z "${CATCH2_SRC}" ]; then
    echo "Catch2 not found."
    echo
    echo "It is fetched by CPM only when the dev test libraries are enabled:"
    echo "    cmake -S . -B build -DJ37_BUILD_TESTS=ON"
    echo
    echo "Run that once (which populates the CPM cache), or point CATCH2_DIR at"
    echo "either a checkout of https://github.com/catchorg/Catch2 (v3) or a"
    echo "directory holding catch_amalgamated.hpp and catch_amalgamated.cpp."
    exit 1
fi

if [ -f "${CATCH2_SRC}/catch_amalgamated.cpp" ]; then
    CATCH2_INC="${CATCH2_SRC}"
else
    CATCH2_INC="${CATCH2_SRC}/extras"
fi

echo "catch2: ${CATCH2_SRC}"

# J37_SANITIZE=address,undefined, the same switch tests/dsp/run.sh honours: the
# unit tests are where a one-function numerical mistake is cheapest to see, and
# they run in the sanitizer pass for the same reason the harnesses do.
sanitize_flags=()
if [ -n "${J37_SANITIZE:-}" ]; then
    sanitize_flags=("-fsanitize=${J37_SANITIZE}" "-fno-sanitize-recover=all"
                    "-fno-omit-frame-pointer" "-g")
    echo "sanitizers: ${J37_SANITIZE}"
fi

# -I tests/dsp for the same shim.h the regression harness compiles against: these
# tests include the SHIPPING headers from Source/DSP/, and the shim is what lets
# them do that with no JUCE, no audio device and no host. -I Source is where
# those headers are found by the name the plugin uses for them.
"${CXX}" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
    "${sanitize_flags[@]}" \
    -I "${HERE}" -I "${REPO}/tests/dsp" -I "${REPO}/Source" -I "${CATCH2_INC}" \
    "${HERE}"/*_test.cpp "${CATCH2_INC}/catch_amalgamated.cpp" \
    -o "${BUILD}/unit_tests"

# The binary's exit status IS this script's: a failing assertion must fail the
# job, and nothing here filters its output in a way that could hide that.
"${BUILD}/unit_tests"
