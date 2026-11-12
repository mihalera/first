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
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/../lib/harness.sh"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$(j37_build j37-scene-frame-pack-contract)"

mkdir -p "${BUILD}"

# Sanitizers mirror the other scene_frame harness so the shared-mesh and
# pointer-to-shared-data invariants are still under the same tooling.

"${CXX}" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
    "${sanitize_flags[@]}" \
    -I "$(j37_path source)" \
    "${HERE}/pack_contract.cpp" \
    "${REPO}/Source/GUI/SceneFrame.cpp" \
    -o "${BUILD}/pack_contract"

"${BUILD}/pack_contract"

echo "scene_frame packing contract checks passed"
