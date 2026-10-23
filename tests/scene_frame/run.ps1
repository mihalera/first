$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$build = Join-Path $env:TEMP 'mibox-scene-frame-test'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$compiler = Get-Command clang++ -ErrorAction SilentlyContinue
if (-not $compiler) { $compiler = Get-Command g++ -ErrorAction SilentlyContinue }
if (-not $compiler) { throw 'scene_frame tests need clang++ or g++ available on PATH' }
& $compiler.Source -std=c++17 -Wall -Wextra -Werror `
    (Join-Path $PSScriptRoot 'scene_frame_test.cpp') `
    (Join-Path $root 'Source\GUI\SceneFrame.cpp') `
    -o (Join-Path $build 'scene_frame_test.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& (Join-Path $build 'scene_frame_test.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Output 'scene_frame checks passed'
