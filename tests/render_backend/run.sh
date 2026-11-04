#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CXX="${CXX:-g++}"
TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TEMP_DIR"' EXIT

build_and_run() {
    local name="$1"
    shift
    "$CXX" -std=c++17 -Wall -Wextra -Werror \
        -I "$ROOT/tests/render_backend/juce_stub" \
        -I "$ROOT/Source" \
        "$ROOT/tests/render_backend/backend_choice_test.cpp" \
        "$@" -o "$TEMP_DIR/$name"
    "$TEMP_DIR/$name"
}

build_and_run auto
build_and_run opengl -DJ37_RENDER_BACKEND_OPENGL=1
build_and_run sokol -DJ37_RENDER_BACKEND_SOKOL=1 -DJ37_NATIVE_SOKOL=1

echo "backend choice tests passed (AUTO, OPENGL, SOKOL)"
