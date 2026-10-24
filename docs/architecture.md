# Architecture: how the plugin is put together

A map of the source tree for someone opening it for the first time, or for
someone who has been away. It says what each file is for and how the parts talk
to each other; it deliberately does **not** re-describe the sound - that is
[README.md](../README.md), which is the product document. What this file is for
is the question "where does this live, and who calls it?".

Everything here is checkable from the tree itself. Where a number is quoted it
was measured, and the command that measures it is in
[plugin-analysis.md](plugin-analysis.md#how-this-was-measured).

## The whole thing in one paragraph

`first` is a VST3 / AU / AUv3 / Standalone audio effect: **one** `AudioProcessor`
(`FirstAudioProcessor`) and **one** editor (`FirstAudioProcessorEditor`). The
processor owns a parameter tree of **109** parameters, a tape-machine signal path
that runs entirely on the audio thread, and a saved state of its own format with
a migration path. The editor owns a panel of **109** widgets, three themes, two
renderers (OpenGL through JUCE, or a software path), a decorative 3D tape deck in
box2d, and meters. Nothing in the audio path allocates, locks or touches the
editor; the editor reads published numbers.

## Entry points

| File | What it is |
| --- | --- |
| `Source/PluginProcessor.h/.cpp` | the processor's lifecycle: the constructor, `prepareToPlay`, `processBlock`, the state save/load, and the declarations everything else in the tree needs |
| `Source/ProcessorSignalPath.cpp` | the signal path itself: `processBlock`'s body, `processTapeEngine`, `processOneSample`, and the stages that run before and after the machine |
| `Source/ParameterTable.cpp` | `createParameterLayout()` - 23 groups, 109 parameters, and the four range constants the table is built from |
| `Source/ProcessorPresets.h/.cpp` | the factory preset table, the user preset directory, A/B compare, whole-state undo/redo |
| `Source/ProcessorStateFormat.h` | the saved-state format: its marker, its version, and the migration that reads an older file |
| `Source/PluginEditor.h/.cpp` | the editor: the widget declarations, the constructor entry point, and the resource accessors |
| `Source/GUI/*.cpp` | the editor split by concern - setup, theme, paint, timer, meters, look-and-feels, interface sounds, the deck scene, the native renderers |

`Source/ProcessorSignalPath.cpp` is where a change to the sound is made. It is
3,900 lines and one of its functions, `processTapeEngine`, spans lines 576-2229.

## The signal path

The order, in the code's own terms, is [signal-path.md](signal-path.md) - stage
by stage, with the rate domain of each - and the short version is
[README.md § Signal path](../README.md#signal-path). Where each step lives:

| Stage | File |
| --- | --- |
| input trim, input glue, record head, tape EQ, noise, playback EQ | `Source/ProcessorSignalPath.cpp` (inside `processTapeEngine`) |
| the saturation curve and its six principles | `Source/DSP/SaturationCore.h` |
| the amplifier's sag / presence / cabinet / bias | `Source/DSP/AmpVoicing.h` |
| the two pre-machine gain stages (preamp, distortion, DI) | `Source/DSP/InputStage.h` |
| the block-rate coefficients the machine rebuilds once a block | `Source/DSP/BlockRateCoefficients.h` |
| tape condition (FLUX / WEAR / MECHANICS) | `Source/DSP/TapeCondition.h` |
| the machine's own rate and its four shaping curves | `Source/DSP/TapeModulation.h` |
| the sample clock and its smoother | `Source/DSP/SampleSmoother.h` |
| glue compressor (one stage each side, always on) | `Source/DSP/GlueCompressor.h` |
| conventional compressor on the finished signal | `Source/DSP/StandardCompressor.h` |
| transient shaper (attack / sustain) | `Source/DSP/TransientShaper.h` |
| three-band EQ, high-pass, low-pass | `Source/DSP/ThreeBandEq.h` |
| the loudness meter the readout reads | `Source/DSP/LoudnessMeter.h` |
| neural stage (RTNeural, and NAM Core behind `J37_NAMCORE`, on by default) | `Source/DSP/NeuralStage.h` |
| NAM Core's loader and per-sample loop, for the architectures RTNeural does not convert | `Source/DSP/NamBackend.h` |
| IR cabinet (convolution, optional) | `Source/DSP/IrStage.h` |
| the rate bridge between engine rate and a model's rate | `Source/DSP/RateBridge.h` |
| subharmonic generator | `Source/DSP/SubharmonicGenerator.h` |
| room reverb (SPACE), Dattorro's plate against `ThirdParty/dattorro-verb` | `Source/DSP/DattorroRoom.h` |
| vinyl stage (surface, rumble, RIAA) | `Source/DSP/VinylStage.h` |
| pitch tracking for autotune | `Source/DSP/PitchTracker.h` |
| granular pitch shifting for repitch and autotune | `Source/DSP/RealtimePitchShifter.h` |
| transport (stop / play / start, the power cut) | `Source/DSP/TransportRig.h` |
| repitch (what the platter's speed does to the audio) | `Source/DSP/RepitchStage.h` |
| harmonic readout for the panel | `Source/DSP/HarmonicAnalyser.h` |
| spectrum analyser, and the only caller pocketfft has | `Source/DSP/SpectrumAnalyser.h` |
| the per-sample scalar helpers | `Source/DSP/HotPathMath.h` |
| which optional libraries this build has | `Source/DSP/OptionalLibraries.h` |
| the scalar math, routed to chowdsp or `std::` | `Source/DSP/J37Math.h` |

The `Source/DSP/` convention is stated once, at the top of `J37Math.h`: one
concern per header, the header includes what it needs, and a header that
branches on a macro reads the header that decides the macro
(`OptionalLibraries.h`). It is what lets a harness `#include` a stage instead of
cutting its text out of a 7,000-line file - see
[tests/README.md](../tests/README.md).

## Threading and data flow

| Direction | Mechanism | Where |
| --- | --- | --- |
| host -> audio thread | `processBlock` on the host's callback | `PluginProcessor.cpp`, `ProcessorSignalPath.cpp` |
| audio thread -> audio thread | `TapeBlockState`, coefficients rebuilt once per block | `BlockRateCoefficients.h` |
| audio thread -> editor | atomics: meter values, transport state, harmonic readout, build identity | `PluginProcessor.h`, read in `Source/GUI/EditorTimer.cpp` |
| editor -> audio thread | the APVTS parameter values (lock-free), plus atomics for the transport keys | `ParameterTable.cpp`, `TransportRig.h` |
| editor -> editor | `timerCallback()`: meters, deck animation, bounded repaints | `Source/GUI/EditorTimer.cpp` |
| any -> preset / undo | whole-state strings through `ProcessorPresets.cpp` | `Source/ProcessorPresets.cpp` |

`farbot`, `readerwriterqueue` and the atomics around them are the only
cross-thread machinery: no mutex is taken on the audio thread, and the editor
never calls into the engine to *ask* for a number - it reads one that was
published.

The editor's construction is its own pipeline: **23 phases**, each declared in
`PluginEditor.h`, defined once and called once in the order declared. The layout
harness checks exactly that, because a phase that is never called builds a panel
that is quietly missing a control.

## State

| Kind | Where it lives | Format |
| --- | --- | --- |
| parameters | the APVTS | host session |
| the machine's extra state | `getStateInformation` / `setStateInformation` | this plugin's own marker + version, migrated in `ProcessorStateFormat.h` |
| factory presets | `Source/Presets/Factory/*.json`, 29 of them | JSON, embedded as a binary resource |
| user presets | `<user app data>/MiBoX/Presets` | `.miboxpreset` |
| A/B slots and undo history | in memory, per instance | whole-state strings |

The identity a host keys on - manufacturer code `Manu`, plugin code `Zy59`,
bundle ID `com.MCsmes.first` - is deliberately frozen across renames: the product
name has moved twice, to MiBoX, and these three have not. `CMakeLists.txt` says
why beside it.

## Rendering

The editor draws through one boundary, `Source/GUI/RenderBackend.h`, selected at
configure time with `-DJ37_RENDER_BACKEND=`. OpenGL is JUCE's own context; CPU is
a JUCE `Graphics` renderer for the animated deck; Vulkan, Metal, D3D11, D3D12,
SOKOL and BGFX have adapters under `Source/GUI/NativeRenderer*`. Of those, only
OpenGL draws the tape scene: every other selection creates its device, presents a
cleared surface and then resolves to the CPU paint path rather than to a blank
editor. The build's selection is the DEFAULT: SETTINGS also carries a BACKEND
selector that forces OpenGL or CPU while the plugin runs, with a staged adapter
listed as PENDING and disabled. That runtime choice is a property of the machine
and lives in the editor's preferences file
(`Source/GUI/EditorPreferences.h`), not in the session. WGPU and DAWN are wired
inside `j37_setup_graphics_backend` but are not values `J37_RENDER_BACKEND`
accepts, because neither has an adapter. `README.md § Rendering backends` has the
detail, and
[plugin-analysis.md](plugin-analysis.md#7-the-renderers-have-no-coverage) has the
gap: those files are compiled by the platform builds and checked by nothing else.

## Build layers

```
CMakeLists.txt              the target, its sources, its definitions, its formats
cmake/J37Dependencies.cmake everything fetched, in the order it must be configured
cmake/J37VendorLibraries.cmake  IPP / Accelerate discovery, and the switch that makes JUCE's own FFT use IPP
cmake/J37Platform.cmake     the Apple, Windows and Android specifics
cmake/J37ThirdPartyWarnings.cmake  warning suppression for vendored code
cmake/J37Tests.cmake        Catch2 + nanobench, off by default
cmake/J37BuildIdentity.cmake  the git hash the editor prints
ThirdParty/                 the vendored libraries, at the versions CPM would fetch
JUCE/                       the JUCE 9.0.4 submodule
patches/juce-9.0.4/         the one JUCE header this repository supplies itself
tests/                      the harnesses; no CMake configure
```

Two things about this layout are worth knowing before changing it:

- **The vendored copies win over CPM.** `J37Dependencies.cmake` prefers
  `ThirdParty/` when it exists, so a fresh clone configures and builds offline
  and CI downloads nothing but JUCE. Re-pinning means changing *both* the
  vendored tree and the CPM block in one commit.
- **`patches/juce-9.0.4/` is per-version on purpose.** It carries the JUCE 9.0.4
  opus config header plus two typedefs that make `juce_audio_formats` compile.
  The version is part of the directory name and of the guard in `CMakeLists.txt`,
  so bumping the submodule stops the copy instead of writing an old header over a
  newer tree. `plugin-analysis.md` records when that patch can go away.

## The checks

The suite is described in [tests/README.md](../tests/README.md): what each
harness compiles, what it guarantees, and - the honest half - what it does not
cover. Two of them are structural rather than functional and belong in this map:
`tests/editor/check_editor_juce.sh` compiles every translation unit and every
`Source/GUI/*.h` against real JUCE, and `tests/dsp/check_headers_juce.sh` does
the same for every header in `Source/DSP/`. Between them they are what makes the
split of these files safe: every other harness compiles an extracted span and
would not see a moved block with a missing include.

One of them is wired the opposite way round, from a library to the audio: every tanh
the saturation core runs goes through `j37Tanh`, the one seam a library switch can be
thrown at, and the switch is `J37_USE_SIMD_TANH` (on by default: xsimd's kernel costs
~2.4 ns against `std::tanh`'s ~6.7 ns, for a ~1e-7 curve difference;
`-DJ37_USE_SIMD_TANH=OFF` restores `std::tanh`). `tests/dsp/simd_tanh_test.cpp` is built
in all three shapes - flag with xsimd, flag without it, and neither - and the three binaries'
curve digests are compared against each other, because "the switch reaches the audio"
is a claim about two builds rather than about one.

Three of the stages in the table above are unit-sized rather than engine-sized -
they hold no state of their own, take their coefficients as arguments, and state
their contract in the header comment - so they are measured directly as well as
through a harness: `tests/unit/run.sh` drives `ThreeBandEq`,
`StandardCompressor` and `TransientShaper` one call at a time under Catch2 (a
dev-only dependency, fetched by `-DJ37_BUILD_TESTS=ON`). A stage that is
engine-sized - one that reads the processor's state or the block-rate
coefficients - stays with the harnesses, which is the split `cmake/J37Tests.cmake`
describes from the other side.
