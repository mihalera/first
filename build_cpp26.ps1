# Build MiBoX with C++26 in an isolated build directory.
# This is an experimental compatibility check; the build may fail because
# JUCE 9.0.5 and some dependencies are not validated for C++26.
# Usage: .\build_cpp26.ps1
$ErrorActionPreference = "Stop"
$buildDir = if ($env:BUILD_DIR) { $env:BUILD_DIR } else { "build-cpp26" }
cmake -S . -B $buildDir -G "Visual Studio 18 2026" -A x64 `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_CXX_STANDARD=26 `
  -DCMAKE_CXX_STANDARD_REQUIRED=ON `
  -DCMAKE_CXX_EXTENSIONS=OFF
cmake --build $buildDir --config Release --parallel
