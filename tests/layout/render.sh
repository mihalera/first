#!/usr/bin/env bash
#
# Builds and runs the header render harness.
#
#   tests/layout/render.sh
#
# The knob harness next door (run.sh) has no JUCE and never will: it cuts the
# layout arithmetic out of Source/ and measures numbers. This one asks the other
# question - what the header's machine-state badge actually puts on the pixels -
# so it links the shipping plugin and paints the REAL editor into a juce::Image
# through an ordinary Graphics: no window, no display, no host, no new framework.
#
# It therefore needs the developer test build (the same -DJ37_BUILD_TESTS=ON tree
# tests/unit and the CMake harnesses use), which is a build of its own:
#
#   cmake -S . -B build-tests -DCMAKE_BUILD_TYPE=Release -DJ37_BUILD_TESTS=ON
#
# and that is what this script configures when the tree is not there yet. The
# plugin has to compile, so this is minutes rather than seconds: CI runs it in the
# job that already builds the plugin for the DSP harnesses.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
BUILD="${J37_TESTS_BUILD_DIR:-${ROOT}/build-tests}"

if [ ! -f "${BUILD}/CMakeCache.txt" ]; then
    cmake -S "${ROOT}" -B "${BUILD}" -DCMAKE_BUILD_TYPE=Release -DJ37_BUILD_TESTS=ON
fi

cmake --build "${BUILD}" --target j37_header_render_test --parallel "${J37_TEST_JOBS:-2}"

# -V, so the harness's own claim lines and its negative controls are in the log: a
# green run is only worth reading if the numbers it refused are in it too.
ctest --test-dir "${BUILD}" -C Release -V -R '^header_render$' --timeout 600
