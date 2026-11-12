#!/usr/bin/env bash
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/../lib/harness.sh"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${HERE}/../.."
BUILD="$(j37_build j37-scale-test)"

mkdir -p "${BUILD}"

"${CXX}" -std=c++17 -O2 -Wall -Wextra -Werror \
    "${sanitize_flags[@]}" \
    -I "${ROOT}/Source" \
    "${HERE}/scale_test.cpp" -o "${BUILD}/scale_test"

"${BUILD}/scale_test"
