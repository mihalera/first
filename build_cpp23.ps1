# Build MiBoX with C++23 in an isolated build directory.
# Usage: .\build_cpp23.ps1
$ErrorActionPreference = "Stop"
$buildDir = if ($env:BUILD_DIR) { $env:BUILD_DIR } else { "build-cpp23" }
cmake -S . -B $buildDir -G "Visual Studio 18 2026" -A x64 `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_CXX_STANDARD=23 `
  -DCMAKE_CXX_STANDARD_REQUIRED=ON `
  -DCMAKE_CXX_EXTENSIONS=OFF
cmake --build $buildDir --config Release --parallel
