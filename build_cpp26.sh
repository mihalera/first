#!/usr/bin/env bash
# Build MiBoX with C++26 in an isolated build directory.
# Usage: bash build_cpp26.sh
# This is an experimental compatibility check: the build may fail because
# JUCE 9.0.5 and some dependencies are not validated for C++26.
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build-cpp26}"
cmake -S . -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_STANDARD=26 \
  -DCMAKE_CXX_STANDARD_REQUIRED=ON \
  -DCMAKE_CXX_EXTENSIONS=OFF
cmake --build "$BUILD_DIR" --config Release --parallel
