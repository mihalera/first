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

# Source/ on the include path for the same reason run.sh puts it there: the
# extractor emits `#include "DSP/..."` for the stages that have moved into
# Source/DSP/, so the benchmark measures the plugin's own headers rather than a
# copy of them. Without it the benchmark fails to build with a missing
# DSP/J37Math.h - which is exactly what the DSP micro-benchmarks job did until
# this line was here.
"${CXX}" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
    -I "${HERE}" -I "${BUILD}" -I "${NANOBENCH_INC}" -I "${REPO}/Source" \
    "${HERE}/bench.cpp" -o "${BUILD}/bench"

"${BUILD}/bench"

# -----------------------------------------------------------------------------
#  The same translation unit a second time, with the shaper's tanh seam switched
#  the other way.
#
#  J37_USE_SIMD_TANH is a CMake option (ON by default) that routes every tanh
#  the saturation core runs through xsimd's batch kernel instead of libm's. The
#  run above measures the `-DJ37_USE_SIMD_TANH=OFF` shape, because that build
#  passes no define; this one measures the branch the plugin actually ships.
#
#  The comparison is a subtraction rather than an inference: the SaturationCore
#  and reference rows appear in BOTH logs, from the same file and the same
#  signal, and section 6's j37Tanh row exists only here. It is informational in
#  exactly the same way as the run above - a slower number fails nothing - and
#  tests/dsp/simd_tanh_test.cpp is what pins the curve the switch changes.
# -----------------------------------------------------------------------------
echo
echo "J37_USE_SIMD_TANH=ON (the default) - the shaper's tanh through xsimd's kernel"
echo "(compare the SaturationCore::process and std::tanh rows with the log above)"
echo

"${CXX}" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
    -I "${HERE}" -I "${BUILD}" -I "${NANOBENCH_INC}" -I "${REPO}/Source" \
    -I "${REPO}/ThirdParty/xsimd/include" -DJ37_USE_SIMD_TANH=1 \
    "${HERE}/bench.cpp" -o "${BUILD}/bench_simd"

"${BUILD}/bench_simd"
