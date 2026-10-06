#!/usr/bin/env bash
#
# One-shot hot-path benchmark: configure once, build the bench target, run it.
#
#   scripts/run_hotpath_bench.sh [build dir] [bench args...]
#
# This is the human-facing entry point for the hot-path micro-benchmarks in
#   tests/dsp/bench.cpp
#
# It is intentionally separate from tests/dsp/run_bench.sh. That script is the
# standalone harness helper that assumes you already have a configured tree and a
# local nanobench checkout or cache. This wrapper is the repo-native path: it
# configures the plugin source tree with the dev-test libraries enabled, builds
# only the bench target, and runs it.
#
# First run downloads a small set of pinned dev-only dependencies through CPM
# (Catch2 and nanobench). That is why the cached package dir is pinned to the
# repository tree: it survives a build-dir wipe and makes subsequent runs offline.
#
# The benchmark is a MEASUREMENT, not a gate. A slow machine, a noisy CI runner,
# or a different compiler should never fail this script. Only a build failure or
# a missing dependency is fatal.
#
# You can pass extra arguments straight to the bench binary:
#   scripts/run_hotpath_bench.sh build --benchmark_min_time=0.5
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"

BUILD="${1:-build}"
shift || true

export CPM_SOURCE_CACHE="${REPO}/.cpm-cache"
mkdir -p "$CPM_SOURCE_CACHE"

cd "$REPO"

if [ ! -d "$BUILD" ] || [ ! -f "$BUILD/CMakeCache.txt" ]; then
  echo "--- configure (dev tests + hot-path bench, pinned CPM cache) ---"
  cmake -S . -B "$BUILD" \
    -DJ37_BUILD_TESTS=ON \
    -DJ37_BUILD_HOTPATH_BENCH=ON \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER="${CXX:-g++}" \
    -DCMAKE_C_COMPILER="${CC:-gcc}" \
    -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$BUILD/bin" \
    -DCMAKE_LIBRARY_OUTPUT_DIRECTORY="$BUILD/lib" \
    -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY="$BUILD/lib" \
    -DJUCE_USE_CURL=0 \
    -DJUCE_WEB_BROWSER=0 \
    -DCMAKE_DISABLE_FIND_PACKAGE_Jack=ON \
    -DCMAKE_DISABLE_FIND_PACKAGE_Alsa=ON \
    -DCMAKE_DISABLE_FIND_PACKAGE_PulseAudio=ON \
    -DCMAKE_DISABLE_FIND_PACKAGE_CoreAudio=ON \
    -DCMAKE_DISABLE_FIND_PACKAGE_WASAPI=ON \
    -DCMAKE_DISABLE_FIND_PACKAGE_DirectX=ON \
    -DCMAKE_DISABLE_FIND_PACKAGE_X11=ON \
    -DCMAKE_DISABLE_FIND_PACKAGE_GTK3=ON \
    -DCMAKE_DISABLE_FIND_PACKAGE_Qt5=ON \
    -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON \
    -DCMAKE_DISABLE_FIND_PACKAGE_ImGui=ON \
    -DCMAKE_DISABLE_FIND_PACKAGE_Vulkan=ON \
    -DCMAKE_DISABLE_FIND_PACKAGE_IPP=ON \
    -DCMAKE_FIND_PACKAGE_PREFER_CONFIG=ON \
    > /tmp/nonlin-bench-configure.log 2>&1

  ec=$?
  if [ "$ec" -ne 0 ]; then
    echo "configure failed. tail of log:"
    echo "-----------------------------------------"
    tail -n 80 /tmp/nonlin-bench-configure.log
    echo "-----------------------------------------"
    exit 1
  fi
fi

echo "--- build bench target ---"
"$HERE/bench_hotpath_target.sh" "$BUILD"
ec=$?
if [ "$ec" -ne 0 ]; then
  exit "$ec"
fi

echo "--- run bench ---"
exec "$BUILD/bin/nonlin-hotpath-bench" "$@"
