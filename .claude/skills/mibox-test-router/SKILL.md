---
name: mibox-test-router
description: Choose and run the narrowest MiBoX validation for a source change. Use when a task changes DSP, parameters/state, layout/rendering, assets, translations, or build definitions, or when the user asks to verify a change.
---

# MiBoX validation router

Read `tests/README.md` for each harness's actual coverage and limitations. Do not run the whole suite by default; choose the smallest check that exercises the changed contract and add a neighboring structural check when a move or wiring change could escape the focused harness.

The shell harnesses are Bash scripts. On Windows, run them from Git Bash or WSL with the required C++ compiler on `PATH`. Python checks can run from PowerShell with `python` or `py`.

| Change area | Focused validation |
| --- | --- |
| DSP stage, saturation, noise decode | `bash tests/dsp/run.sh`; for a moved/standalone DSP header, also `bash tests/dsp/check_headers_juce.sh` |
| Transport or repitch | `bash tests/transport/run.sh` |
| Pitch tracking/correction | `bash tests/pitch/run.sh` |
| Output phase guard, gain, limiter, crossfade, reverb | `bash tests/phase/run.sh`, `bash tests/gain/run.sh`, `bash tests/limiter/run.sh`, `bash tests/mix_crossfader/run.sh`, or `bash tests/reverb/run.sh` as applicable |
| Neural model path | `bash tests/neural/run.sh` |
| Parameters or host state | `python tests/parameters/check_parameters.py` and/or `bash tests/state/run.sh`; use `python tests/presets/check_presets.py` for factory presets |
| Editor layout or tooltip timing | `bash tests/layout/run.sh` or `bash tests/tooltip/run.sh`; after moving GUI code, use `bash tests/editor/check_editor_juce.sh` |
| Scene packing/render selection | `bash tests/scene_frame/run.sh` and `python tests/render_backend/check_render_backend.py`; use `bash tests/render_backend/run.sh` for backend-choice routing |
| Shaders/assets/theme/translations | `python tests/shaders/check_shaders.py`, `python tests/assets/check_assets.py`, `python tests/theme/check_theme_wiring.py`, or `python tests/translations/check_translations.py` |
| CMake definitions, dependencies, source link line | `python tests/definitions/check_definitions.py`, `python tests/dependencies/check_unused_rack_fetch.py`, or `python tests/linkline/check_linkline.py` |

Do not describe a check as passed unless it was actually run. Benchmarks in `tests/` are informational; do not treat them as correctness gates. The CMake unit suite is opt-in via `-DJ37_BUILD_TESTS=ON` and may fetch Catch2/nanobench.

