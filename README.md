# first

A JUCE-based VST3 audio plugin prototype inspired by classic analog tape saturation and J37-style coloration.

## Overview

This project is a focused audio effect plugin built for Windows with JUCE and Visual Studio.
It explores a tape-saturation workflow with:

- drive and harmonic character controls
- tape-type selection
- speed influences and modulation
- wow / flutter behavior
- bias, tone, mix, and output staging
- a vintage analog-inspired UI

## Project type

- Audio plugin: VST3
- Framework: JUCE
- Target platform: Windows
- Build system: Visual Studio/MSBuild

## Build

1. Open the solution in `Builds/VisualStudio2026/first.sln`.
2. Build the project in `Release` configuration for `x64`.
3. Load the generated VST3 plugin in your DAW.

## Repository notes

The project is structured as a standard JUCE plugin repository with generated Visual Studio files and custom DSP in `Source/`.

## License

This repository is for project and development use.
