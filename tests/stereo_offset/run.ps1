$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$build = Join-Path $env:TEMP 'mibox-stereo-offset-test'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$source = Join-Path $PSScriptRoot 'stereo_offset_test.cpp'
$exe = Join-Path $build 'stereo_offset_test.exe'
$include = Join-Path $repo 'Source/DSP'
$llvm = Join-Path $repo 'build/tools/llvm-mingw-20261104-ucrt-x86_64'
$clang = Join-Path $llvm 'bin/clang++.exe'
if (-not (Test-Path $clang)) { throw "clang++ not found at $clang" }
$standardHeaders = Join-Path $llvm 'include/c++/v1'
$targetHeaders = Join-Path $llvm 'x86_64-w64-mingw32/include'
& $clang --target=x86_64-w64-windows-gnu -stdlib=libc++ -std=c++20 -Wall -Wextra -Werror `
    "-I$standardHeaders" "-I$targetHeaders" "-I$include" $source -o $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $exe
exit $LASTEXITCODE
