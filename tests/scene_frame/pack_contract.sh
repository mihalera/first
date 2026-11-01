#!/usr/bin/env bash
#
# Verifies the Phase 1 uniform packing contract in `Source/GUI/SceneUniforms.h`
# against a frame built per material with no JUCE, no OpenGL and no GPU.
#
#   tests/scene_frame/pack_contract.sh
#
# This is the harness the plan named for the Phase 1 verify step: the contract
# file `SceneUniforms.h` is written first, and this section exercises it with a
# minimal frame builder that reproduces the contract-relevant fields of
# `TapeScene::buildSceneFrame` for each `SceneMaterial`.
#
# The contract file and the frame builder must agree on the per-material
# meaning of `draw.parameters[0..2]`. When they disagree, this harness fails.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="${HERE}/../.."
BUILD="${TMPDIR:-/tmp}/j37-scene-frame-pack-contract"
CXX="${CXX:-g++}"

mkdir -p "${BUILD}"

# Sanitizers mirror the other scene_frame harness so the shared-mesh and
# pointer-to-shared-data invariants are still under the same tooling.
sanitize_flags=()
if [ -n "${J37_SANITIZE:-}" ]; then
    sanitize_flags=("-fsanitize=${J37_SANITIZE}" "-fno-sanitize-recover=all"
                    "-fno-omit-frame-pointer" "-g")
    echo "sanitizers: ${J37_SANITIZE}"
fi

"${CXX}" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
    "${sanitize_flags[@]}" \
    -I "${REPO}/Source" \
    "${HERE}/pack_contract.cpp" \
    "${REPO}/Source/GUI/SceneFrame.cpp" \
    -o "${BUILD}/pack_contract"

"${BUILD}/pack_contract"

echo "scene_frame packing contract checks passed"
