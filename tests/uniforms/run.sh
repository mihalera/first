#!/usr/bin/env bash
#
# Builds and runs the scene-uniform harness.
#
#   tests/uniforms/run.sh
#
# Source/GUI/SceneUniforms.h is what the sokol adapter uploads: it turns one draw
# of a frame into the two uniform blocks the shaders declare, and remembers which
# of them the GPU already holds so a draw that repeats a block costs no upload.
# None of that names JUCE, sokol or a device - it is arithmetic on plain floats -
# so this compiles the shipping header as it ships and checks the two things the
# adapter cannot show on its own: that a draw becomes the block the shader reads,
# and that a block is uploaded exactly when it differs, after a scene mark, or
# after a new program.
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/../lib/harness.sh"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$(j37_build j37-uniforms-test)"

mkdir -p "${BUILD}"

"${CXX}" "${J37_CXX_STD}" -O2 "${J37_WARNINGS[@]}" \
    -I "$(j37_path source)" \
    "${HERE}/uniform_cache_test.cpp" -o "${BUILD}/uniform_cache_test"

"${BUILD}/uniform_cache_test"
