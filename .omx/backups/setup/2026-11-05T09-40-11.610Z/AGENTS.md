# AI contributor guide

## Project

MiBoX is a JUCE 9.0.5 audio effect plugin built with CMake and C++20. It ships VST3, AU/AUv3, CLAP, and standalone targets across desktop platforms and Android. JUCE is a pinned git submodule; the remaining dependencies are vendored under `ThirdParty/`.

Start with the narrowest useful reference:

- `docs/architecture.md` for ownership, threading, and source locations.
- `docs/signal-path.md` for audio-stage order and rate domains.
- `tests/README.md` for the harnesses, what they compile, and their limits.
- `README.md` for product behavior and build instructions.
- `docs/release-checklist.md` before any binary distribution.

Avoid reading the whole `README.md` or the very large processor/editor files when a focused `rg` search and a small line range will answer the question.

## Source map

- `Source/PluginProcessor.*`: processor lifecycle, host state, and shared declarations.
- `Source/ProcessorSignalPath.cpp`: the audio signal path.
- `Source/ParameterTable.cpp`: host-visible parameter IDs, ranges, defaults, and grouping.
- `Source/ProcessorPresets.*` and `Source/ProcessorStateFormat.h`: presets and saved-state compatibility.
- `Source/DSP/`: focused DSP stages and helpers.
- `Source/PluginEditor.*` and `Source/GUI/`: editor, layout, drawing, and renderer adapters.
- `CMakeLists.txt` and `cmake/`: the source of truth for targets, dependencies, and platform builds.
- `tests/`: direct-source harnesses and structural checks; see its README before choosing one. Use `$mibox-validation` to route a change to the narrowest useful check.

## Optional agent tools

- Use Context7 for current external-library documentation when the pinned source in `JUCE/`, `ThirdParty/`, or the project docs do not answer the question. Verify behavior against the checked-in version before changing code.
- Use Graphify for broad cross-file navigation and dependency tracing. Its project configuration excludes the JUCE submodule and vendored dependencies; verify graph results against source.
- Use Impeccable only when a task changes visual design. Its automatic detector targets web UI source and does not validate JUCE C++ drawing code.
- Use `grill-me` when the user asks to pressure-test a design or plan. Keep ordinary implementation requests moving without turning them into interviews.
- Superpowers workflows may help with planning or debugging, but this file remains the source of truth for MiBoX contracts and validation scope.

## Contracts to preserve

- Keep the audio callback real-time safe: do not add heap allocation, blocking locks, file/network I/O, waits, or editor/UI calls to audio-thread code. Prepare state outside the callback and use the existing atomics/queues for cross-thread communication.
- Keep the editor from reaching into live DSP state. The editor reads published values and writes through the existing parameter/atomic pathways.
- Treat parameter IDs, defaults, ranges, units, and scaling as host automation and preset compatibility contracts. Change them only when the task requires it, and trace the effect through presets and tests.
- Keep saved plugin state backward compatible. If its shape changes, inspect `ProcessorStateFormat.h` and add or update the migration path and state harness.
- DSP headers should own one concern, include what they use, and remain directly testable where practical. Tests that cover a moved stage should include the shipping header rather than a copied implementation.
- Do not edit `JUCE/` or vendored `ThirdParty/` code for an MiBoX change unless the task is specifically about maintaining that dependency or patch.
- Treat `build/`, CMake output, generated project files, plugin bundles, and downloaded dependency caches as artifacts, not source.
- Keep release/license constraints in mind: a local build is not permission to distribute a binary. Follow `docs/release-checklist.md` first.

## Style and build

- Follow `.clang-format`: LLVM style, four spaces, 100-column limit. The project build uses C++20; some standalone test harnesses intentionally need only C++17.
- The documented Windows release build is:

  ```powershell
  cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_BUILD_TYPE=Release
  cmake --build build --config Release --parallel
  ```

- Build output is under `build/first_artefacts/Release/`. Do not add `COPY_PLUGIN_AFTER_BUILD=TRUE` unless local installation was requested.
- The developer-only CMake tests are off by default and fetched with `-DJ37_BUILD_TESTS=ON`. The many fast harnesses under `tests/` are run directly; they are not all CTest targets.
- On Windows, the shell harnesses require Bash and a C++ compiler. Use a Bash environment where `g++` or `clang++` is available. Use Python checks directly with `python` or `py`.
- Run the smallest relevant check for a source change and report exactly what ran. Use `tests/README.md` to select it; do not imply a full build or suite passed if it was not run.

