---
name: mibox-validation
description: Select the smallest MiBoX check for a code change or an explicit validation request. Use when choosing tests for DSP, parameters/state, editor, rendering, assets, or build wiring.
---

# MiBoX validation

Use `tests/README.md` as the detailed source of truth for each harness and its limits. Choose checks that cover the changed contract; add a neighboring structural check when a move or wiring change could escape the focused harness.

On Windows, the shell harnesses need Git Bash or WSL and a C++ compiler on `PATH`. Python checks can run directly from PowerShell with `python` or `py`.

| Changed area | Relevant check |
| --- | --- |
| DSP, saturation, noise decode | `bash tests/dsp/run.sh`; moved DSP headers: `bash tests/dsp/check_headers_juce.sh` |
| Transport or repitch | `bash tests/transport/run.sh` |
| Pitch tracking or correction | `bash tests/pitch/run.sh` |
| Neural model path | `bash tests/neural/run.sh` |
| Phase guard, gain, limiter, crossfade, reverb | `bash tests/phase/run.sh`, `bash tests/gain/run.sh`, `bash tests/limiter/run.sh`, `bash tests/mix_crossfader/run.sh`, or `bash tests/reverb/run.sh` as applicable |
| Parameters, host state, or factory presets | `python tests/parameters/check_parameters.py`, `bash tests/state/run.sh`, or `python tests/presets/check_presets.py` |
| Editor layout or tooltip | `bash tests/layout/run.sh` or `bash tests/tooltip/run.sh`; moved GUI code: `bash tests/editor/check_editor_juce.sh` |
| Scene packing or renderer routing | `bash tests/scene_frame/run.sh`, `python tests/render_backend/check_render_backend.py`, and, if backend selection changed, `bash tests/render_backend/run.sh` |
| Shaders, assets, themes, translations | `python tests/shaders/check_shaders.py`, `python tests/assets/check_assets.py`, `python tests/theme/check_theme_wiring.py`, or `python tests/translations/check_translations.py` |
| CMake definitions, dependencies, source link line | `python tests/definitions/check_definitions.py`, `python tests/dependencies/check_unused_rack_fetch.py`, or `python tests/linkline/check_linkline.py` |

Benchmarks under `tests/` are informational, not correctness gates. The Catch2/nanobench CMake tests are opt-in via `-DJ37_BUILD_TESTS=ON` and may fetch dependencies. Report only checks actually run and distinguish harness coverage from a full plugin build.
