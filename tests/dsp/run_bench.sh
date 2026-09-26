#!/usr/bin/env bash
#
# Builds and runs the DSP micro-benchmarks.
#
#   tests/dsp/run_bench.sh
#
# Separate from run.sh on purpose. run.sh is the REGRESSION gate: it renders
# audio and asserts on it, so it must pass on every commit and must stay fast.
# This is a MEASUREMENT tool: it takes tens of seconds, its numbers depend on the
# machine, and nothing should ever fail a build because a benchmark got slower.
# Keeping them in one script would mean either a flaky gate or a benchmark nobody
# runs.
#
# nanobench is fetched by CPM under -DJ37_BUILD_TESTS=ON. This script does not
# invoke CMake, so it looks for the header in the usual CPM cache locations and
# says plainly what to do when it cannot find it.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "${HERE}/../.." && pwd)"
BUILD="${TMPDIR:-/tmp}/nonlin-dsp-bench"
CXX="${CXX:-g++}"

mkdir -p "${BUILD}"

python3 "${HERE}/extract.py" "${BUILD}/extracted_dsp.inc"

# nanobench is header-only and lives at src/include/nanobench.h upstream. CPM
# checks it out under one of these, depending on how the cache is configured.
NANOBENCH_INC=""
for candidate in \
    "${REPO}/.cpm-cache/nanobench/"*/src/include \
    "${REPO}/.cpm-cache/nanobench/src/include" \
    "${REPO}/build/_deps/nanobench-src/src/include" \
    "${REPO}/build/deps/nanobench/src/include" \
    "${NANOBENCH_SRC:-}/src/include"
do
    if [ -n "${candidate}" ] && [ -f "${candidate}/nanobench.h" ]; then
        NANOBENCH_INC="${candidate}"
        break
    fi
done

if [ -z "${NANOBENCH_INC}" ]; then
    echo "nanobench.h not found."
    echo
    echo "It is fetched by CPM only when the dev tests are enabled:"
    echo "    cmake -S . -B build -DJ37_BUILD_TESTS=ON"
    echo
    echo "Run that once (which populates the CPM cache), or point NANOBENCH_SRC at"
    echo "a checkout of https://github.com/martinus/nanobench."
    exit 1
fi

echo "nanobench: ${NANOBENCH_INC}"
echo

"${CXX}" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
    -I "${HERE}" -I "${BUILD}" -I "${NANOBENCH_INC}" \
    "${HERE}/bench.cpp" -o "${BUILD}/bench"

"${BUILD}/bench"