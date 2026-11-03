#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${HERE}/../.."
BUILD="${TMPDIR:-/tmp}/j37-scale-test"
CXX="${CXX:-g++}"

mkdir -p "${BUILD}"

sanitize_flags=()
if [ -n "${J37_SANITIZE:-}" ]; then
    sanitize_flags=("-fsanitize=${J37_SANITIZE}" "-fno-sanitize-recover=all"
                    "-fno-omit-frame-pointer" "-g")
    echo "sanitizers: ${J37_SANITIZE}"
fi

"${CXX}" -std=c++17 -O2 -Wall -Wextra -Werror \
    "${sanitize_flags[@]}" \
    -I "${ROOT}/Source" \
    "${HERE}/scale_test.cpp" -o "${BUILD}/scale_test"

"${BUILD}/scale_test"
