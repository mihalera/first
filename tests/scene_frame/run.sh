#!/usr/bin/env bash
#
# Builds and runs the scene-frame harness.
#
#   tests/scene_frame/run.sh
#
# Source/GUI/SceneFrame.h is the contract a native adapter receives, and
# Source/GUI/SceneLayout.h is the deck geometry the scene, the frame builder
# and this test all read. Neither needs JUCE, a GPU or a host: the frame is
# plain data and isValid() is arithmetic on it, so the boundary can be measured
# on every pull request instead of only where a native backend is built.
#
# The header is compiled exactly as it ships, and so is its implementation:
# there is no extractor and no replica here.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="${HERE}/../.."
BUILD="${TMPDIR:-/tmp}/j37-scene-frame-test"
CXX="${CXX:-g++}"

mkdir -p "${BUILD}"

# J37_SANITIZE=address,undefined puts the harness under the same sanitizers the
# other harnesses take. A frame is a pointer to shared mesh data plus a draw
# list, so a null or dangling share is exactly what they catch.
sanitize_flags=()
if [ -n "${J37_SANITIZE:-}" ]; then
    sanitize_flags=("-fsanitize=${J37_SANITIZE}" "-fno-sanitize-recover=all"
                    "-fno-omit-frame-pointer" "-g")
    echo "sanitizers: ${J37_SANITIZE}"
fi

"${CXX}" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
    "${sanitize_flags[@]}" \
    -I "${REPO}/Source" \
    "${HERE}/scene_frame_test.cpp" "${REPO}/Source/GUI/SceneFrame.cpp" \
    -o "${BUILD}/scene_frame_test"

"${BUILD}/scene_frame_test"

echo "scene_frame checks passed"
