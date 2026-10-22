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
| `foleys_gui_magic` in the build | **done (used)** | `CMakeLists.txt` links it, and `Source/GUI/ParameterSheet.h` builds the PARAMETERS sheet from a ValueTree the processor's own parameter tree generates; the panel itself is still the hand-written one |
| Whole panel authored in foleys XML | **open, and no longer the only use** | the editor is 2,600 lines of computed geometry with a harness that measures it (`tests/layout`); an XML rewrite would have to move that geometry and its checks with it. The sheet is a real foleys surface without that rewrite |
| Melatonin vector graphics for GR / spectrum | **partial** | `melatonin_blur` is used for the panel's shadows and halos (`Source/GUI/PanelPaint.h`, `J37LookAndFeel.cpp`); the GR readout and spectrum are drawn with `juce::Path` and the analyser's own curve |
| Localised tooltips through `LocalisedStrings` and `TRANS()` | **done, differently** | `Source/GUI/PanelTranslations.h` uses `LocalisedStrings::translateWithCurrentMappings`; `en.txt` and `uk.txt` ship inside the binary, and `tests/translations/check_translations.py` fails on a dead key, a missing language or an empty entry |

## Stage 3 - pitch, sub generator, autotune, transient

| Item | Status | Where it is |
| --- | --- | --- |
| DywaPitchTrack for the detector | **open** | `Source/DSP/PitchTracker.h` is YIN, with its confidence gate measured by `tests/pitch`. A second detector would need a reason better than "faster", and a harness comparison |
| Sub generator from phase accumulators | **done** | `Source/DSP/SubharmonicGenerator.h` (the undertone series with downward saturation), stage 9 and 21 of [signal-path.md](signal-path.md) |
| Signalsmith phase vocoder for autotune | **built, OFF by default** | `Source/DSP/CorrectorStage.h` is the stage: the granular shifter by default, signalsmith-stretch behind `-DJ37_CORRECTOR_USE_STRETCH=ON`. The restructure the row above predicted is done - the stage runs on blocks, at the top of `processPostMachineStages()`, and its lookahead is reported to the host. It is OFF because `tests/pitch` measured what it costs: 1216 frames of lookahead at 48 kHz, and a transposition 8 cents wide of the ratio it was given at 1.06x and 32 at 0.94x against the granular engine's sub-cent reading. That 32 is already the GOOD configuration: the error follows the analysis geometry, so eight analyses per window halve what the usual four cost (16 and 57 cents) for no extra latency, and a sixteenth halves it again for four times the analysis work (6.7 % of a core per channel) and still reads 17 cents at 0.94x. A longer window does not close it either - 60 ms reads 13 cents at 1.06x at three times the lookahead. Both shapes are built and measured on every run |
| **±10 cent dead zone** | **done** | `Source/DSP/PitchCorrectionRule.h`, used by `processAutoTuneSample`, pinned at exact cents by `tests/pitch` |
| Formant preservation | **not applicable to this stage** | The library does compensate: on a synthetic vowel with formants at 660/1211/2642 Hz shifted 1.5x, `setFormantFactor (1, true)` keeps them at 798/1149/2604 Hz where the uncompensated render puts them at 896/1007/1928. What rules it out is the RANGE, not the capability: the ratio this stage is handed is clamped to a semitone either way (`PitchCorrectionRule`), so the envelope moves by at most 6 % - inside the measurement's own band-tracking error, and what a correcting shifter has always done. The switch belongs to the commit that first asks the stage for an octave, and `tests/pitch` prints the pitch it does move |
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
| BGFX or WebGPU/Dawn | **built as an option, not as a second renderer** | `bgfx` is now a `J37_RENDER_BACKEND` value with an adapter (`Source/GUI/NativeRenderer_Bgfx.cpp`) that creates a child surface inside the host's peer, initialises bgfx on it and presents a cleared frame. It is staged, not advertised: `hasNativeImplementation()` still returns false, because bgfx draws nothing without shader binaries and this build turns its shaderc tool off. Dawn/wgpu-native remain plumbing in `j37_setup_graphics_backend` that the option does not accept |
| Assimp with only the OBJ importer | **open** | not fetched; no mesh assets exist in the tree, so there is nothing to import yet |
| Sokol GFX as a light pipeline wrapper | **built as an option** | `sokol_gfx` is a `J37_RENDER_BACKEND` value with a real adapter (`Source/GUI/NativeRenderer_Sokol.cpp`): it attaches a JUCE `OpenGLContext`, compiles the library once in a JUCE-free TU, clears and presents through JUCE's render callback, and reports the backend it landed on. It is staged for the same reason as the others - the tape scene is not ported to it - so the editor still paints with JUCE |
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
- **Re-adding a removed library without a caller.** `bgfx` and `sokol_gfx` came back
  WITH callers this time - a backend option, an adapter from `createNativeRenderer`
  down to a cleared frame, and a check in `tests/render_backend` that keeps the two
  in step - which is the difference between a selection and a dependency. The ones
  that are still off the link line (`PFFFT`, `kissfft`, the pitch trackers,
  `yasio`, `imgui`) left it because no file under `Source/` called them, and
  `tests/linkline/check_linkline.py` fails when a linked target has no caller. Each
  one has to arrive with a caller and a harness, which is exactly the order this
  document lists them in.
- **`mNAMEngine->process()` on the audio thread.** Loading a model is file I/O and
  allocation; running it is only safe once it is resident and lock-free. The pattern
  to copy is `Source/DSP/IrStage.h`'s: load in the background, report "loading", swap
  in on the audio thread.

## What changed in this pass

The corrector's vocoder engine was given the configuration its own measurement asked
for, and both open questions in this document about it were answered rather than left
standing.

`Source/DSP/CorrectorStage.h` now runs **eight analyses per 20 ms window** instead of
four. On a 220 Hz tone the vocoder's transposition reads 15.8 cents wide at 1.0595x and
56.9 at 0.9439x with the old geometry and **8.4 and 32.4** with the new one, and the
lookahead is identical (1216 frames at 48 kHz, measured and reported to the host by the
harness) because the lookahead is the window, not the interval. The cost is analysis
work: 0.8 % of a core per channel became 1.6 %. A sixteenth analysis halves the error
again and costs 6.7 %, and still loses to the granular shifter's sub-cent reading - so
the interval stops at eight and `J37_CORRECTOR_USE_STRETCH` stays OFF.

Two things that had been assumed were measured while doing it, and one assumption was
wrong:

- **The error does not follow the note.** The same cents appear at 110 Hz with twice
the window, so what a corrector pays is set by the analysis geometry, not by how low
the singer goes.
- **A longer window is not the fix.** 60 ms brings 0.9439x to within a cent and pushes
1.0595x to 13, at three times the lookahead - better than nothing and worse than eight
analyses at 20 ms, which costs no latency at all.
- **Formant compensation works, and was wrongly written off.** The header said
`setFormantFactor (1, true)` changed nothing; on a vowel with formants at 660, 1211 and
2642 Hz shifted 1.5x it holds them at 798, 1149 and 2604 Hz where the uncompensated
render puts them at 896, 1007 and 1928. It still is not switched on, for a reason the
range gives: this stage's ratio is clamped to a semitone, where the envelope moves
6 % and the compensated and uncompensated renders agree to a few Hz.

The tracker's own reading at the bottom of its range is still a few cents low (the
harness's diagnostic prints -8.6 cents at 55 Hz), so a very low note is inside the
±10 cent dead zone and will not be corrected - the trade the zone makes, printed on
every run so it stays visible.

## The next three, in order

1. **The link line and the libraries waiting for a caller** - see
   [library-integration-plan.md](library-integration-plan.md): WP1 drops the GPLv3 module
   nothing calls; WP2 gave `dr_libs` a caller as the vinyl stage's user noise print,
   which is the shape that argument wanted - a library adopted by USING it.
   The multiband anti-phase guard this list used to open with is done (Stage 5).
2. **~~Formant preservation and the block-based corrector~~**, answered by measurement
   and closed - and the answer is that neither half is left to build. Formant
   preservation exists in the library and works at 1.5x (the envelope holds to within
   ~140 Hz where the uncompensated render moves it by 240), and the stage's ratio is
   clamped to a semitone, where it changes nothing: the row says so now instead of
   calling it open. The accuracy half turned out to be the analysis GEOMETRY, not the
   window: going from four analyses per window to eight halves the vocoder's error
   (16/57 cents to 8/32) at exactly the same lookahead, which is what
   `Source/DSP/CorrectorStage.h` now runs, and it still leaves the vocoder three times
   worse than the shifter it would replace - so the flag stays OFF with a number
   behind it rather than a hope. The remaining next step is the one this list has not
   had: **a second detector behind `PitchTracker`** would need the harness comparison
   the Stage 3 row asks for, and nothing else on the list is waiting on code.
3. **~~NAM Core + Eigen~~**, done behind the existing `NeuralStage` seam: LSTM still
   goes through RTNeural and everything else through NAM Core, behind `J37_NAMCORE`
   (now ON by default). `tests/neural` builds both shapes and compares both engines
   with the reference player on `tests/neural/fixtures/` - the LSTM through the shipping
   stage (1.19e-7), the converter against NAM Core's own LSTM (5.96e-8), and WaveNet
   through the stage (2.79e-8) - and `tests/neural/run_bench.sh` prints the process cost
   the default waited for (14.5 % of one core per mono channel for a 16-channel WaveNet).
   See WP3 in
   [library-integration-plan.md](library-integration-plan.md).
