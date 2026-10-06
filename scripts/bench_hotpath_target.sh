#!/usr/bin/env bash
#
# Builds ONLY the hot-path benchmark target from the repo's dev-test tree.
#
#   scripts/bench_hotpath_target.sh [build dir]
#
# This is the narrow helper the wrapper script calls. It does not reconfigure the
# whole plugin; it assumes the source tree is already configured with
#   -DJ37_BUILD_TESTS=ON -DJ37_BUILD_HOTPATH_BENCH=ON
# and it only builds the bench target, which is what you want when you have
# already configured once and you are iterating on the benchmark itself.
#
# The target lives under tests/dsp/ and is wired by cmake/J37Tests.cmake, which
# is included by the top-level CMakeLists.txt. That file is intentionally OFF by
# default and only pulled in when J37_BUILD_TESTS=ON, so a normal plugin build
# never pays for Catch2 or nanobench or this target.
#
# nanobench is header-only plus one implementation TU. The implementation is
# provided by the bench.cpp translation unit itself through
#   #define ANKERL_NANOBENCH_IMPLEMENT
# so there is nothing extra to build; the target only needs the nanobench headers
# on its include path, which J37Tests.cmake adds when the dev tests are on.
#
# Usage from the wrapper:
#   scripts/bench_hotpath_target.sh build
#   build/bin/nonlin-hotpath-bench
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"

BUILD="${1:-build}"
if [ ! -d "$BUILD" ]; then
  echo "build directory missing: $BUILD"
  echo "configure first with:"
  echo "  cmake -S . -B build -DJ37_BUILD_TESTS=ON -DJ37_BUILD_HOTPATH_BENCH=ON"
  exit 1
fi

cd "$BUILD"

if ! cmake --build . --target nonlin-hotpath-bench -j 0 > /tmp/nonlin-bench-build.log 2>&1; then
  echo "build failed. tail of log:"
  echo "-----------------------------------------"
  tail -n 80 /tmp/nonlin-bench-build.log
  echo "-----------------------------------------"
  exit 2
fi

echo "built: $BUILD/bin/nonlin-hotpath-bench"
