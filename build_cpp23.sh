#!/usr/bin/env bash
# Build MiBoX with C++23 in an isolated build directory.
# Usage: bash build_cpp23.sh
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build-cpp23}"
cmake -S . -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_STANDARD=23 \
  -DCMAKE_CXX_STANDARD_REQUIRED=ON \
  -DCMAKE_CXX_EXTENSIONS=OFF
cmake --build "$BUILD_DIR" --config Release --parallel
