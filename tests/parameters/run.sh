#!/usr/bin/env bash
#
# Builds and runs the per-parameter contract sweep and the unique per-parameter
# tests against the shipping table.
#
#   bash tests/parameters/run.sh
#
# extract.py cuts createParameterLayout() and every parameter group verbatim from
# Source/, then this compiles them against the parameter stand-in used by the
# saved-state harness. sweep_test asks every registered parameter the same
# generic questions; unique_test pins each parameter's OWN answer - its exact
# default, range, step, taper, unit and choices - one unique test per parameter.
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/../lib/harness.sh"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$(j37_build j37-parameter-test)"

mkdir -p "${BUILD}"
python3 "${HERE}/../state/extract.py" "${BUILD}"

"${CXX}" "${J37_CXX_STD}" -O2 "${J37_WARNINGS[@]}" \
    "${sanitize_flags[@]}" \
    -I "${HERE}/../state" -I "${BUILD}" -I "$(j37_path source)" \
    "${HERE}/../state/juce_stub.cpp" "${HERE}/sweep_test.cpp" \
    -o "${BUILD}/parameter_sweep"

"${BUILD}/parameter_sweep"

"${CXX}" "${J37_CXX_STD}" -O2 "${J37_WARNINGS[@]}" \
    "${sanitize_flags[@]}" \
    -I "${HERE}/../state" -I "${BUILD}" -I "$(j37_path source)" \
    "${HERE}/../state/juce_stub.cpp" "${HERE}/unique_test.cpp" \
    -o "${BUILD}/parameter_unique"

"${BUILD}/parameter_unique"
