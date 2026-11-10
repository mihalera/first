$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$build = Join-Path $env:TEMP 'mibox-stereo-offset-test'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$include = Join-Path $repo 'Source/DSP'
$cl = Get-Command cl.exe -ErrorAction SilentlyContinue
if ($null -eq $cl) { throw 'cl.exe not found. Run this script from a Visual Studio developer shell.' }
$tests = @(
    @{ Source = 'stereo_offset_test.cpp'; Name = 'stereo_offset_test' },
    @{ Source = 'lofi_stereo_test.cpp'; Name = 'lofi_stereo_test' },
    @{ Source = 'delay_feedback_test.cpp'; Name = 'delay_feedback_test' }
)
foreach ($test in $tests) {
    $source = Join-Path $PSScriptRoot $test.Source
    $exe = Join-Path $build ($test.Name + '.exe')
    & $cl.Source /nologo /std:c++20 /EHsc /W4 /WX "/I$include" $source "/Fe:$exe"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $exe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
exit 0
