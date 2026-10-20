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
| Signalsmith phase vocoder for autotune | **built, OFF by default** | `Source/DSP/CorrectorStage.h` is the stage: the granular shifter by default, signalsmith-stretch behind `-DJ37_CORRECTOR_USE_STRETCH=ON`. The restructure the row above predicted is done - the stage runs on blocks, at the top of `processPostMachineStages()`, and its lookahead is reported to the host. It is OFF because `tests/pitch` measured what it costs: 1216 frames of lookahead at 48 kHz, and a transposition 56 cents wide of the ratio it was given at 0.94x (a 60 ms window brings that to a cent, at three times the latency) against the granular engine's sub-cent reading. Both shapes are built and measured on every run |
| **±10 cent dead zone** | **done** | `Source/DSP/PitchCorrectionRule.h`, used by `processAutoTuneSample`, pinned at exact cents by `tests/pitch` |
| Formant preservation | **open, and tried** | `setFormantFactor (1, true)` was measured on a synthetic vowel at 1.06x and 1.5x, with the formant base given and autodetected, and the envelope's centroid moved with the pitch in both cases and identically with the compensation on and off - so the stage does not claim it and the row stays open |
| cycfi/q true-RMS envelopes for the transient shaper | **open** | `Source/DSP/TransientShaper.h` has its own fast/slow envelopes, pinned by `tests/unit` |

## Stage 4 - NAM, IR, micro-models, noise loader

| Item | Status | Where it is |
| --- | --- | --- |
| NeuralAmpModelerCore + Eigen | **done** | vendored (`ThirdParty/namcore` @ `0b3d3c97` v0.6.0, `ThirdParty/eigen` @ `bc3b3987`) and linked by default (`J37_NAMCORE`, ON; `-DJ37_NAMCORE=OFF` drops it): `Source/DSP/NamBackend.h` is the backend, `Source/DSP/NeuralStage.h` dispatches LSTM to RTNeural and everything else to NAM Core, and `tests/neural` measures both engines against the reference player (1.19e-7 LSTM, 5.96e-8 converter-vs-NAM, 2.79e-8 WaveNet through the stage). The process cost that its default waited for is printed by `tests/neural/run_bench.sh`: 14.5 % of one 48 kHz core per mono channel for a 16-channel WaveNet, 2.8 % for a 20-cell LSTM on NAM Core - see WP3 |
| `mNAMEngine->process()` in `processBlock` | **not as written** | model inference on the audio thread is only safe if it allocates and locks nothing; the current stage runs a fixed graph with per-channel state, and the async machinery here is the IR loader's |
| `juce::dsp::Convolution` IR loader | **done** | `Source/DSP/IrStage.h`: one Convolution per channel, `loadImpulseResponse` for the wait-free background decode, a stereo/mono rule, and a "loading" flag the panel reads |
| RTNeural micro-models for valve/cabinet | **done (as the neural stage)** | `Source/DSP/NeuralStage.h` + `tests/neural`, which compiles the guarded half with RTNeural stubbed |
| dr_libs noise loader through a lock-free queue | **done, without the queue** | `dr_libs` is vendored (`ThirdParty/dr_libs`), linked and called: `Source/DSP/NoiseDecoder.*` decodes a WAV and `Source/DSP/NoiseLoader.h` hands the loop to the vinyl stage on its own thread, measured by `tests/dsp`'s two noise binaries. There is no queue, and the header says why: what crosses the thread boundary is ONE pointer swapped atomically, so a queue would be a second mechanism for a hand-off that is already a single word - `readerwriterqueue` keeps its one caller (the telemetry), and a retired print is kept alive rather than freed, which is the part a queue would not have solved |

## Stage 5 - 3D, model import, anti-phase

| Item | Status | Where it is |
| --- | --- | --- |
| 3D renderer with shaders and matrices | **done** | `Source/GUI/NativeRenderer_{OpenGL,Vulkan,Metal,D3D11,D3D12}.cpp`, `TapeSceneShaders.h` (GLSL, checked by `tests/shaders`), `glm` for the matrices |
| BGFX or WebGPU/Dawn | **not as written** | `bgfx` is fetched and was on the link line with no caller until it was removed; the four native backends above are shipping and tested. Adopting BGFX is a renderer rewrite, not an addition |
| Assimp with only the OBJ importer | **open** | not fetched; no mesh assets exist in the tree, so there is nothing to import yet |
| Sokol GFX as a light pipeline wrapper | **not as written** | `sokol_gfx` is fetched and has no caller; the panel's 2D is `juce::Graphics`, and the 3D backends already own their pipelines |
| GLM for camera and Lambert/Phong lighting | **done** | `Source/GUI/TapeScene.*` |
| Procedural tape-grain / metal-roughness shader driven by DRIVE and the compressor | **partial** | the shaders generate grain and shading from the engine's telemetry; DRIVE and gain reduction are among the values the scene reads |
| Multiband Linkwitz-Riley crossover + correlation-driven M/S squeeze | **done** | `Source/DSP/MultibandPhaseGuard.h`: the frame is split by Linkwitz-Riley 4th-order crossovers at 200 Hz and 2 kHz, each band's own correlation decides that band's rotation, and the rotation is orthogonal (side folds into mid, so the fix costs no level). A frame with no opposition comes back bit for bit, and `tests/phase` measures the per-band side energy that proves the fault's band is the band that moves. Separate from - and never mixed with - stage 18's undertone phase-lock in `processOneSample` |

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

1. **The link line and the libraries waiting for a caller** - see
   [library-integration-plan.md](library-integration-plan.md): WP1 drops the GPLv3 module
   nothing calls; WP2 gave `dr_libs` a caller as the vinyl stage's user noise print,
   which is the shape that argument wanted - a library adopted by USING it.
   The multiband anti-phase guard this list used to open with is done (Stage 5).
2. **Formant preservation and the block-based corrector** - the restructure is done
   (`Source/DSP/CorrectorStage.h`, measured by `tests/pitch`), so what is left is the
   engine itself: a phase-locking configuration or a longer analysis window that
   makes the vocoder's transposition as accurate as the granular shifter's, and a
   formant path that survives the vowel measurement the roadmap row describes.
3. **~~NAM Core + Eigen~~**, done behind the existing `NeuralStage` seam: LSTM still
   goes through RTNeural and everything else through NAM Core, behind `J37_NAMCORE`
   (now ON by default). `tests/neural` builds both shapes and compares both engines
   with the reference player on `tests/neural/fixtures/` - the LSTM through the shipping
   stage (1.19e-7), the converter against NAM Core's own LSTM (5.96e-8), and WaveNet
   through the stage (2.79e-8) - and `tests/neural/run_bench.sh` prints the process cost
   the default waited for (14.5 % of one core per mono channel for a 16-channel WaveNet).
   See WP3 in
   [library-integration-plan.md](library-integration-plan.md).
