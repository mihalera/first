# first

A JUCE-based VST3 audio plugin prototype inspired by classic analog tape saturation and J37-style coloration.

## Overview

This project is a focused audio effect plugin built for Windows with JUCE and Visual Studio.
It explores a tape-saturation workflow with:

- drive and harmonic character controls
- tape-type selection
- speed influences and modulation
- wow / flutter behavior
- bias, tone, mix, and calibrated output staging in dB
- an always-on tape glue compressor with a live reduction meter
- a vintage analog-inspired UI with animated knobs, reel and level meters

## Signal path

Input trim (dB) -> input glue compressor (always on) ->
record head (pre-emphasis, bias, magnetic hysteresis with memory) ->
tape low-pass and head-gap loss -> tape noise floor and wow/flutter modulation ->
playback EQ tilt -> output glue compressor (always on) -> output trim (dB) -> stereo width.

## Controls

| Control | Range | Notes |
| --- | --- | --- |
| Input | -32 to +32 dB | Drives the tape machine harder |
| Drive | 0 to 100 % | Saturation amount |
| Bias | 0 to 100 % | Tape bias offset and asymmetry |
| Tone | 0 to 100 % | Warm/soft to open/bright playback EQ |
| Wow | 0 to 100 % | Slow transport pitch wander |
| Flutter | 0 to 100 % | Fast transport shimmer |
| Mix | 0 to 100 % | Dry to fully processed |
| Output | -32 to +32 dB | Calibrated output trim in dB |
| Width | 0 to 100 % | Mono through natural to extra wide |
| Bypass | on/off | Ramps the whole tape engine out without clicking |
| Tape Type | J37 / Ampex 456 / Studer A800 / Chrome | Model character |
| Speed | 7.5 / 15 / 30 ips | Transport speed, affects modulation and top end |

The tape glue compressor is compressor-coupled in two places, and the two stages are
completely independent processors: each has its own detector envelope and its own gain
computer, and neither reads the other's state. Both are driven purely by the signal
that reaches them plus the Input / Output parameters.

The **input stage** sits immediately after the input trim, so INPUT pushes this signal
into a real recorder-input stage and the tape always hears a controlled level. The
**output stage** sits immediately before the output trim, so the headroom it creates is
spent directly on OUTPUT.

Turning INPUT or OUTPUT up moves that stage's threshold down (roughly -17 dB to -3 dB),
steepens its ratio and allows more reduction; turning it below the middle backs the
stage off completely, at which point it is genuinely transparent. Each stage pays back
roughly 30-65 % of the reduction it applies as makeup, weighted by how hard its trim
control is driving it, so neither stage quietly undoes the level the user dialled in.
The COMP meter shows the combined reduction (0 to -12 dB) plus the IN/OUT split.

## Project type

- Audio plugin: VST3
- Framework: JUCE 9.0.2
- Target platform: Windows
- Build system: Visual Studio/MSBuild

## Build

1. Open the solution in `Builds/VisualStudio2026/first.sln`.
2. Build the project in `Release` configuration for `x64`.
3. Load the generated VST3 plugin in your DAW.

The Visual Studio projects use a shared precompiled header (`Builds/VisualStudio2026/pch.h`),
which is the single biggest win for build time because every JUCE translation unit includes
`JuceHeader.h`. Release builds also use `Optimization: MaxSpeed` with `FavorSizeOrSpeed: Speed`
and multi-processor compilation.

Every plugin source file (`Source/*.cpp`) must include `pch.h` as its **first** include -
MSVC compiles those translation units with `/Yu pch.h` and fails with `C1010: unexpected end
of file while looking for precompiled header` when it does not find it. The JUCE module
wrappers (`JuceLibraryCode/include_juce_*.cpp`) deliberately stay out of the PCH scheme.
`Builds/VisualStudio2026/stdafx.h` is a thin alias to `pch.h`: other JUCE exporters ask the
compiler to force-include a file literally named `stdafx.h`, and a forced include that
cannot be found is only a warning, which would silently break the PCH setup.

## Repository notes

The project is structured as a standard JUCE plugin repository with generated Visual Studio files and custom DSP in `Source/`.

## License

This repository is for project and development use.
