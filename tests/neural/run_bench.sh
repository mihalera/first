#!/usr/bin/env bash
#
# Builds and runs the .nam process-cost benchmark.
#
#   tests/neural/run_bench.sh
#
# A MEASUREMENT, not a gate - the same split tests/dsp keeps between run.sh and
# run_bench.sh. tests/neural/run.sh is the regression harness and has to stay
# fast and deterministic on every push; this takes a minute of compiling and
# prints numbers that belong to the machine it ran on, so nothing here fails a
# build for being slow. It fails when a model does not load or renders a sample
# that is not a number, because a measurement of broken audio is worse than none.
#
# Why it lives here rather than beside the DSP micro-benchmarks: the measurement
# needs the two inference engines, the NAM Core objects, and the .nam fixtures -
# none of which tests/dsp has. That harness extracts headers out of Source/ and
# compiles them against a shim; this one loads models through the shipping stage.
#
# NAM Core's sources are compiled the way tests/neural/run.sh compiles them -
# its own `NAM/*.cpp` and `NAM/*/*.cpp` globs, objects on the LINK LINE rather
# than in an archive, because the architectures register themselves in file-scope
# statics that an archive would prune (see cmake/J37Dependencies.cmake for the
# long version). Keep the two scripts' build blocks in step.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "${HERE}/../.." && pwd)"
SOURCE="${REPO}/Source"
BUILD="${TMPDIR:-/tmp}/j37-neural-bench"
CXX="${CXX:-g++}"

RTNEURAL_ROOT="${REPO}/ThirdParty/RTNeural"
NAM_ROOT="${REPO}/ThirdParty/namcore"
EIGEN_ROOT="${REPO}/ThirdParty/eigen"
NLOHMANN_ROOT="${REPO}/ThirdParty/nlohmann_json/single_include"
NAM_JSON_INCLUDE="${NLOHMANN_ROOT}/nlohmann"

for required in "${RTNEURAL_ROOT}" "${NAM_ROOT}/NAM/get_dsp.h" \
                "${EIGEN_ROOT}/Eigen/Dense" "${NLOHMANN_ROOT}/nlohmann/json.hpp"; do
    if [ ! -e "${required}" ]; then
        echo "run_bench: ${required} is missing." >&2
        echo "It is vendored under ThirdParty/; a checkout without it cannot build" >&2
        echo "the NAM Core backend this benchmark measures." >&2
        exit 1
    fi
done

mkdir -p "${BUILD}/namcore"

# One job per core, in waves - bash 3.2 compatible on purpose (`wait -n` is 4.3+
# and macOS's /bin/bash is 3.2). A failing compile stops the script through
# `wait`'s exit status rather than being swallowed.
nam_jobs=1
if command -v nproc >/dev/null 2>&1; then
    nam_jobs="$(nproc)"
elif command -v sysctl >/dev/null 2>&1; then
    nam_jobs="$(sysctl -n hw.ncpu 2>/dev/null || echo 1)"
fi
case "${nam_jobs}" in
    ''|*[!0-9]*) nam_jobs=1 ;;
esac
[ "${nam_jobs}" -ge 1 ] || nam_jobs=1

nam_objects=()
nam_wave=()

wait_for_wave() {
    for pid in "${nam_wave[@]}"; do
        wait "${pid}"
    done
    nam_wave=()
}

for source in "${NAM_ROOT}"/NAM/*.cpp "${NAM_ROOT}"/NAM/*/*.cpp; do
    object="${BUILD}/namcore/$(basename "${source}" .cpp).o"
    nam_objects+=("${object}")

    "${CXX}" -std=c++20 -O2 -w \
        -isystem "${NAM_ROOT}" -isystem "${EIGEN_ROOT}" -isystem "${NAM_JSON_INCLUDE}" \
        -c "${source}" -o "${object}" &
    nam_wave+=("$!")

    if [ "${#nam_wave[@]}" -ge "${nam_jobs}" ]; then
        wait_for_wave
    fi
done

wait_for_wave

# The benchmark itself. -Wno-cpp for the same reason run.sh passes it: the
# probes in OptionalLibraries.h use #warning-style diagnostics the plugin build
# silences too. RTNeural's own modules/json is deliberately NOT on the include
# path - two copies of nlohmann in one translation unit are two types, and NAM
# Core's `const nlohmann::json&` parameters would mangle differently.
"${CXX}" -std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-cpp \
    -DJ37_NAMCORE=1 \
    -I "${HERE}/../dsp" -I "${SOURCE}" -I "${SOURCE}/DSP" \
    -isystem "${RTNEURAL_ROOT}" \
    -isystem "${NLOHMANN_ROOT}" \
    -isystem "${NAM_ROOT}" -isystem "${EIGEN_ROOT}" -isystem "${NAM_JSON_INCLUDE}" \
    "${HERE}/bench_nam.cpp" "${nam_objects[@]}" -o "${BUILD}/bench_nam"

"${BUILD}/bench_nam" "${HERE}/fixtures"
