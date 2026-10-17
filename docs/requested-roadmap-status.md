# The requested roadmap, checked against the tree

A plan for five sprints arrived as a single list: CI/INFRA, a new UI, pitch and
modulation DSP, NAM and texture loading, and modern 3D plus an Ozone-style
anti-phase guard. This file answers, for every item, **what the repository already
does**, where that lives, and what the item would actually cost. It was written by
reading the code, not by trusting the plan's own status.

Status words: **done** (shipping and tested), **partial** (the mechanism exists but
not the breadth asked for), **open** (no code), **not as written** (the item names
something that does not exist or would make the build worse - the evidence is in the
section after the tables).

## Stage 1 - infrastructure, CI/CD, presets

| Item | Status | Where it is |
| --- | --- | --- |
| `pip install ipp-static` on Windows in CI | **done** | `.github/workflows/build.yml`, the "Install Intel IPP (PyPI wheels)" step - `ipp-static` plus `ipp-include`, with the absent-IPP job left in place as the control |
| `-framework Accelerate` on macOS | **done, by JUCE** | `JUCE/modules/juce_dsp/juce_dsp.h` declares `OSXFrameworks: Accelerate`; JUCE links it, and `cmake/J37VendorLibraries.cmake` adds nothing on Apple on purpose |
| `JUCE_DSP_USE_INTEL_IPP=1` | **not as written** | JUCE 9.0.4 contains no such macro. The switch that matters is the IPP *linking-model* macros JUCE's FFT is compiled under, and `cmake/J37VendorLibraries.cmake` throws them (with the include path and `ippvm` the wheels need). `tests/definitions/check_definitions.py` fails a macro with no reader, so adding the name would turn CI red |
| Factory presets out of `PluginProcessor.cpp` | **done** | `Source/Presets/Factory/*.json`, one file per preset, embedded with `juce_add_binary_data(PluginResources ...)`; the index header is generated from the same list, and `tests/presets/check_presets.py` compares the table, the names and the counts |
| Presets in the `"XMLELEMENT": "value_tree_state"` shape | **not as written** | see below - it would replace readable data with an opaque blob |
| Apply presets through `apvts.replaceState()` | **done** | `Source/ProcessorPresets.cpp` and `Source/PluginProcessor.cpp` (`setStateInformation` -> `replaceState`), with the tree copy that path needs |
| Catch2 / GTest target for DSP tests | **done, differently** | `tests/unit/` with Catch2, a `dsp-unit` job in CI, and the same suite under ASan+UBSan. It is a harness rather than a CMake test target: it compiles Catch2's amalgamated sources directly so it needs no configure |

## Stage 2 - declarative UI, 2D acceleration, localisation

| Item | Status | Where it is |
| --- | --- | --- |
| `foleys_gui_magic` in the build | **done (linked)** | `CMakeLists.txt` links it and `Source/PluginEditor.h` probes/embeds it; the panel itself is still the hand-written one |
| Whole panel authored in foleys XML | **open** | the editor is 2,600 lines of computed geometry with a harness that measures it (`tests/layout`); an XML rewrite would have to move that geometry and its checks with it |
| Melatonin vector graphics for GR / spectrum | **partial** | `melatonin_blur` is used for the panel's shadows and halos (`Source/GUI/PanelPaint.h`, `J37LookAndFeel.cpp`); the GR readout and spectrum are drawn with `juce::Path` and the analyser's own curve |
| Localised tooltips through `LocalisedStrings` and `TRANS()` | **done, differently** | `Source/GUI/PanelTranslations.h` uses `LocalisedStrings::translateWithCurrentMappings`; `en.txt` and `uk.txt` ship inside the binary, and `tests/translations/check_translations.py` fails on a dead key, a missing language or an empty entry |

## Stage 3 - pitch, sub generator, autotune, transient

| Item | Status | Where it is |
| --- | --- | --- |
| DywaPitchTrack for the detector | **open** | `Source/DSP/PitchTracker.h` is YIN, with its confidence gate measured by `tests/pitch`. A second detector would need a reason better than "faster", and a harness comparison |
| Sub generator from phase accumulators | **done** | `Source/DSP/SubharmonicGenerator.h` (the undertone series with downward saturation), stage 9 and 21 of [signal-path.md](signal-path.md) |
| Signalsmith phase vocoder for autotune | **open, blocked by shape** | the shifter is driven one frame at a time (`Source/DSP/RealtimePitchShifter.h`); the library works one block at a time, so adopting it restructures the frame loop before it can be swapped in |
| **±10 cent dead zone** | **done** | `Source/DSP/PitchCorrectionRule.h`, used by `processAutoTuneSample`, pinned at exact cents by `tests/pitch` |
| Formant preservation | **open** | it belongs with the same block-based restructure as the vocoder; there is no formant detector today |
| cycfi/q true-RMS envelopes for the transient shaper | **open** | `Source/DSP/TransientShaper.h` has its own fast/slow envelopes, pinned by `tests/unit` |

## Stage 4 - NAM, IR, micro-models, noise loader

| Item | Status | Where it is |
| --- | --- | --- |
| NeuralAmpModelerCore + Eigen | **open** | not fetched. `Source/DSP/NeuralStage.h` runs RTNeural models (a 1-in/1-out network per channel), not NAM weights |
| `mNAMEngine->process()` in `processBlock` | **not as written** | model inference on the audio thread is only safe if it allocates and locks nothing; the current stage runs a fixed graph with per-channel state, and the async machinery here is the IR loader's |
| `juce::dsp::Convolution` IR loader | **done** | `Source/DSP/IrStage.h`: one Convolution per channel, `loadImpulseResponse` for the wait-free background decode, a stereo/mono rule, and a "loading" flag the panel reads |
| RTNeural micro-models for valve/cabinet | **done (as the neural stage)** | `Source/DSP/NeuralStage.h` + `tests/neural`, which compiles the guarded half with RTNeural stubbed |
| dr_libs noise loader through a lock-free queue | **open** | `dr_libs` is fetched but not linked (it has no caller); the telemetry already uses `readerwriterqueue`, and the IR loader is the pattern to copy for a user noise file |

## Stage 5 - 3D, model import, anti-phase

| Item | Status | Where it is |
| --- | --- | --- |
| 3D renderer with shaders and matrices | **done** | `Source/GUI/NativeRenderer_{OpenGL,Vulkan,Metal,D3D11,D3D12}.cpp`, `TapeSceneShaders.h` (GLSL, checked by `tests/shaders`), `glm` for the matrices |
| BGFX or WebGPU/Dawn | **not as written** | `bgfx` is fetched and was on the link line with no caller until it was removed; the four native backends above are shipping and tested. Adopting BGFX is a renderer rewrite, not an addition |
| Assimp with only the OBJ importer | **open** | not fetched; no mesh assets exist in the tree, so there is nothing to import yet |
| Sokol GFX as a light pipeline wrapper | **not as written** | `sokol_gfx` is fetched and has no caller; the panel's 2D is `juce::Graphics`, and the 3D backends already own their pipelines |
| GLM for camera and Lambert/Phong lighting | **done** | `Source/GUI/TapeScene.*` |
| Procedural tape-grain / metal-roughness shader driven by DRIVE and the compressor | **partial** | the shaders generate grain and shading from the engine's telemetry; DRIVE and gain reduction are among the values the scene reads |
| Multiband Linkwitz-Riley crossover + correlation-driven M/S squeeze | **open** | the shipping guard is single-band: `processOneSample` pulls the 1/2 and 1/4 undertone stages back into phase (stage 18). An Ozone-style version is the biggest genuinely new DSP item in this plan |

## Items that should not be applied as written

- **`JUCE_DSP_USE_INTEL_IPP=1`.** `grep -r` over `JUCE/modules` finds nothing. The
  macro would be defined and read by no one, which is precisely the defect
  `tests/definitions/check_definitions.py` was written to catch, and that check runs
  in `dsp-tests` - so the build would go red on the next push. The working switch is
  already thrown (see the table).
- **Presets as `"XMLELEMENT": "value_tree_state"`.** The factory presets are flat
  JSON keyed by parameter id, which is what makes them reviewable in a diff, patchable
  by script, and checkable (`check_presets.py` compares them to `ParameterTable.cpp`).
  An APVTS state XML is a base64 blob of the whole tree: it would make a preset
  unreviewable, break the check, and couple the preset format to every parameter's
  layout. `replaceState` is the right call for a *session* (it is what
  `setStateInformation` uses); it is the wrong shape for a factory preset.
- **Re-adding the removed libraries.** `bgfx`, `sokol_gfx`, `dr_libs`, `PFFFT`,
  `kissfft`, `dattorro_verb`, the pitch trackers and the rest came off the link line
  because no file under `Source/` called them, and
  `tests/linkline/check_linkline.py` fails when a linked target has no caller. Each
  one has to arrive with a caller and a harness, which is exactly the order this
  document lists them in.
- **`mNAMEngine->process()` on the audio thread.** Loading a model is file I/O and
  allocation; running it is only safe once it is resident and lock-free. The pattern
  to copy is `Source/DSP/IrStage.h`'s: load in the background, report "loading", swap
  in on the audio thread.

## What changed in this pass

`Source/DSP/PitchCorrectionRule.h` (new) + the call site in
`processAutoTuneSample`: the corrector now has the **±10 cent dead zone** asked for
in Stage 3. Inside the zone the ratio is exactly 1, so the shifter reads at unity and
the note passes untouched; outside it the pull is the interval the reading asked for,
scaled by TUNE AMOUNT, still clamped to a semitone. `tests/pitch` drives the rule at
exact cent values (`0`, `±3`, `±9.9`, `±10`, `±10.5`, `±37`, `±50`, `±200`), including
the boundary itself and the float round trip around it.

One honest consequence worth knowing: the tracker's own reading at the bottom of its
range is a few cents low (the harness's diagnostic prints -8.6 cents at 55 Hz), so a
very low note is *inside* the dead zone and will not be corrected. That is the trade
the zone makes - it is a statement that a few cents is not an error - and the harness
prints the reading so the trade stays visible.

## The next three, in order

1. **Multiband anti-phase guard** (`juce::dsp::LinkwitzRileyFilter` crossover,
   correlation per band, M/S squeeze only where correlation < 0). It is the one item
   in the plan that is both genuinely missing and audible, and it has a natural
   harness: render a decorrelated band and assert the side channel shrinks while the
   correlated bands do not.
2. **Formant preservation and the block-based corrector** - together, because both
   need the same restructure of the frame loop, and `signalsmith-stretch` is only
   adoptable once it exists.
3. **NAM Core + Eigen**, behind the existing `NeuralStage` seam, with a model-format
   loader that follows the IR stage's background-load pattern.
