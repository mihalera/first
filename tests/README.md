# The test suite

An index over `tests/`, not a replacement for the prose inside it: every harness
header explains *why* it exists and what it was written for, and the tables below
answer the question you actually arrive with - **if I change X, what will catch
me?** - in one screen.

Everything here runs on a machine with nothing but a C++17 compiler, Python 3 and
bash. No JUCE build, no audio device, no plugin host, no CMake configure. The
whole suite is seconds of work, which is why it runs on every pull request and
not only where the plugin is built. The two rows that need something *fetched*
first say so where they appear: Catch2 for the unit suite, nanobench for the
benchmarks.

## Run it

```bash
tests/dsp/run.sh          # subharmonics (SUBFUND), the loudness meter, and the shaper's tanh seam
tests/layout/run.sh       # knob geometry on every page, at four panel sizes
tests/state/run.sh        # session state round trip, and older state files
tests/transport/run.sh    # the transport rig, frame by frame
tests/pitch/run.sh        # the pitch tracker and the granular shifter
tests/neural/run.sh       # the rate bridge, and the neural stage with RTNeural stubbed
tests/spectrum/run.sh     # the spectrum analyser, with pocketfft in and out of the build
tests/unit/run.sh         # the equaliser, the compressor and the transient shaper, as units

tests/dsp/check_headers_juce.sh     # every Source/DSP/*.h against real JUCE
tests/editor/check_editor_juce.sh   # every translation unit and Source/GUI/*.h against real JUCE

python3 tests/presets/check_presets.py           # the factory preset table
python3 tests/theme/check_theme_wiring.py        # theme re-inking coverage
python3 tests/translations/check_translations.py # the shipped translation tables
python3 tests/parameters/check_parameters.py     # the table and the code that reads it
python3 tests/shaders/check_shaders.py           # the GLSL and the uniforms it is fed
python3 tests/definitions/check_definitions.py   # the macros the build defines, and who reads them
```

Two environment switches, both used by the workflows:

| Variable | Effect |
| --- | --- |
| `J37_SANITIZE=address,undefined` | builds every harness above, and the unit suite, with ASan + UBSan and turns them loose on the same assertions |
| `CXX` | the compiler to use; `g++` by default |

The micro-benchmarks are separate and **informational**: `tests/dsp/run_bench.sh`
(nanobench, needs `-DJ37_BUILD_TESTS=ON` to have fetched it) and the hot-path
probe step in `.github/workflows/build.yml`. Nothing fails a build because a
benchmark got slower. It builds `tests/dsp/bench.cpp` TWICE - once as the engine
ships, once with `-DJ37_USE_SIMD_TANH=1` - so the rows the two logs share
(`SaturationCore::process` above all) are the measurement of what that switch
buys, on the same signal through the same file, rather than two benchmarks
being compared from memory.

The unit suite needs a framework rather than a measurement, from the same
`-DJ37_BUILD_TESTS=ON` configure: `tests/unit/run.sh` compiles Catch2's own
amalgamated sources, looks for the checkout in the CPM cache and takes
`CATCH2_DIR` as an override. Unlike the benchmarks it IS a gate, because every
number in it is a property of a function rather than of the machine - there is
nothing in it that a busier runner could make flaky.

## What each harness compiles

This is the part that decides how much a green run is worth. A harness that cuts
a span of text out of a big file tests that span; a harness that *includes the
shipping header* tests the code the plugin compiles.

| Harness | What it compiles | How it gets it |
| --- | --- | --- |
| `dsp` | the subharmonic generator, the loudness meter, and a reduced replica of the signal path - plus `j37Tanh`, `SaturationCore` and the seven curves, built THREE ways | extracts spans through `tests/dsp/extract.py`, **and** `#include`s every stage that has moved to `Source/DSP/`; the tanh seam's three binaries are one file compiled with and without `J37_USE_SIMD_TANH` and with and without xsimd on the include path |
| `layout` | the knob strip arithmetic, the editor's constructor phase order, and the spectrum curve's level mapping | `tests/layout/extract.py` cuts the first two out of `Source/PluginEditor.cpp` + `Source/GUI/`, and `#include`s `Source/GUI/SpectrumMetrics.h` directly |
| `state` | the parameter table and the state functions | `tests/state/extract.py`, compiled against `tests/state/juce_stub.*` |
| `transport` | `TransportRig` and `RepitchStage` | `#include`s the shipping headers from `Source/DSP/` |
| `pitch` | `PitchTracker` and `RealtimePitchShifter` | `#include`s the shipping headers from `Source/DSP/` |
| `neural` | `RateBridge`, and `NeuralStage` with `J37_HAS_RTNEURAL=1` | `#include`s the shipping headers; RTNeural's two-method API is stood in for |
| `spectrum` | `SpectrumAnalyser` | `#include`s the shipping header from `Source/DSP/`, compiled TWICE: with `ThirdParty/pocketfft` on the include path, and without it |
| `unit` | `ThreeBandEq`, `StandardCompressor` and `TransientShaper` - one stage per file | `#include`s the shipping headers from `Source/DSP/` against the same shim the DSP harness uses, and links Catch2's amalgamated sources: no replica of the engine, because none of the three needs one |
| `dsp/check_headers_juce.sh` | every header in `Source/DSP/`, against real JUCE | compiles each header's public surface; catches a header that only works under a harness shim |
| `editor/check_editor_juce.sh` | every translation unit the plugin builds, and every `Source/GUI/*.h` | compiles them against real JUCE; catches a moved block with a missing include |
| the six Python checks | no C++ at all | read `Source/` and the build files as text and compare tables to the code |

## What each harness guarantees

| Harness | The claims it pins | Written for |
| --- | --- | --- |
| `dsp` (subfund) | the undertone series is generated at the octave below, does not run away, stays silent below its threshold, and the anti-phase guard engages | the subharmonic generator shipping silent and then shipping louder than the programme |
| `dsp` (tanh seam) | that the shaper's tanh is `std::tanh` bit for bit unless `J37_USE_SIMD_TANH` is thrown, that the flag WITHOUT xsimd on the include path renders the default curves byte for byte, and that the flag WITH xsimd really moves the rendered audio - by 1.19e-7 at worst, which is the accuracy the header quotes, and not by zero, which is what it used to do | a flag that switched a function nothing called, whose branch called `xsimd::tanh` on a float - and xsimd's scalar entry point is `using std::tanh;`, so the switch was a no-op that no build, test or CI job could notice. The three binaries are compared against each other because "the flag reaches the audio" is a claim about two builds rather than about one |
| `dsp` (loudness) | calibration, linearity, rate independence, the 3.01 LU channel sum, the floor, and that `prepare()` then `reset()` leaves the curve in place | the K-weighting filter running on one pair of state variables for two channels, and `reset()` zeroing the coefficients |
| `layout` | every knob's tile stands inside its page and above the member band; no two tiles overlap; the strip is centred; the disc is drawable at the 780 x 664 floor; the value text clears its field's wall; the painted disc is the solved disc; and the spectrum curve's level axis runs the right way up, is CLAMPED at both ends rather than rescaled, and keeps every point inside its cell at any box size | a layout drifting away from the paint routine, and a constructor phase that quietly never ran - plus a curve that draws smoothly, plausibly and upside down, which no screenshot review catches |
| `state` | a written state reads back identical, an older state loads, an unknown key is ignored, and a missing parameter keeps its default | a bad pointer or an out-of-bounds read in the state path, which only ever shows up in someone's session |
| `transport` | START starts, STOP arrives at rest, the power cut spins down, and `platter()` moves the way the engine's level and modulation assume | START doing nothing on a running machine, and STOP approaching rest without reaching it |
| `pitch` | the tracker finds notes, notes with harmonics, and notes in noise; silence yields no note; the shifter holds its ratios | an out-of-range lag or a read past the delay line |
| `neural` (rate) | the bridge carries signal between the engine's rate and a model's, at the rate pairs the engine can produce | the stage running a model at the engine's rate while the model carries its trained rate |
| `neural` (stage) | the guarded half of `NeuralStage` - everything inside `#if J37_HAS_RTNEURAL` - compiles and runs | a branch no harness had ever compiled |
| `unit` (`ThreeBandEq`) | at neutral settings the stage is an identity to within a float step and both filters are real bypasses at their off ends; the pole count is the slope divided by six, clamped to the eight-pole array; Q is held between 0.5 and 1.5; the high-pass removes DC while the low-pass keeps it; LOW and HIGH each move their own end of the spectrum and leave the other, and MID moves its own band without dragging the ends with it (measured and bounded: about 1.09x at 50 Hz and 1.15x at 10 kHz alongside 1.9x on the bell, for MID at +6 dB) | a "neutral" EQ that is actually a very gentle filter, an order control that quietly picks a different slope, and the wrong band taking a shelf's gain. It also records where the header's wording is stronger than the arithmetic: the three bands are a SUM of one-pole splits, so transparency is exact for a fresh instance on most values and one float step off on others (0.1 comes back 0.099999994) - a float step, never a filter, and the test says which is which |
| `unit` (`StandardCompressor`) | unity under the threshold; the ratio's slope above the knee, as 10 dB more in and 10/ratio dB more out; a knee that blends rather than steps, checked as three 4 dB steps up the curve; MAKEUP exactly on the label; a ratio below 1:1 behaving as 1:1 rather than as a gate; RELEASE recovering to unity instead of parking; and finite output at the parameter extremes, including a sample rate of zero. The slope is measured as a DIFFERENCE because the detector reads a sine about 1.6 dB high - a property of asymmetric ballistics that the suite pins rather than assumes | the two places this stage's maths divides by something - the ratio guard and the knee - and a gain that stays where a loud passage left it instead of recovering |
| `unit` (`TransientShaper`) | 0/0 returns the input bit for bit; ATTACK moves the leading edge and leaves the tail where it was; SUSTAIN lifts the tail at +1 and shortens it at -1 - the sign the header spends its longest paragraph justifying - and leaves the edge alone; the applied gain stays inside a quarter and four; and a signal arriving into an envelope that has only ever seen silence produces no NaN. The signal is a PLUCKED note rather than a step, because a step's tail is exactly zero and cannot show what SUSTAIN does | the inverted SUSTAIN sign, which sounds like a working control until it is measured, and a normalisation that divides by an empty envelope |
| `spectrum` | the transform is calibrated (a full-scale tone on a bin centre reads 0 dBFS), linear, low between bins only as far as a Hann window's own 1.42 dB scalloping loss allows, band-ordered, and floored on silence; and that a build WITHOUT pocketfft still compiles, stays unready and reports the floor | a curve that reads high or draws the wrong band, which looks entirely plausible on a panel - and the absent-library half of a header, which nothing else here compiles |
| `presets` | the preset names match the table, every sound-bearing parameter has a column, and the counts agree with `ParameterTable.cpp` | the preset box listing 20 names for a 26-row table, and 15 sound-bearing parameters silently keeping the session's value |
| `theme` | every widget the panel owns is re-inked when the theme changes | a control keeping the ivory panel's ink on charcoal, or falling back to the Look and Feel's own dark scheme |
| `translations` | no dead keys, every lookup present in every language, no ambiguous or empty entry | 17 Ukrainian entries keyed to sentences the plugin no longer said |
| `check_headers_juce` | each `Source/DSP/*.h` compiles against real JUCE, alone | a header that compiles under the harness shim and not in the plugin |
| `check_editor_juce` | each translation unit and each `Source/GUI/*.h` compiles against real JUCE | a moved block whose new file forgot an include; it caught three faults on its first run |
| `check_parameters` | that the table and the code that reads it agree, both ways: every registered id is fetched with `getRawParameterValue` or looked up with `getParameter`, every name read is registered, and every handle fetched is read again somewhere | a knob that changes nothing - a mistyped id reaches nothing silently (`getRawParameterValue` returns nullptr and every call site is `if (x != nullptr)`), and a registered parameter nothing reads is a row in the host's list that consumes nothing |
| `check_shaders` | that both GLSL sources are still runs of one literal per line, that `TAPE_SCENE_HIGHP` is still defined, and that the uniforms the shaders declare are exactly the ones `TapeScene.cpp` sets | the `"the lit arc"` line of `TapeSceneShaders.h`, where a paste left a bare quote behind - the literal closed early and the rest of the line became a C++ comment, which compiles; and a `setUniform` name the linked program does not declare, which is a silent no-op that leaves the visual that depends on it at zero |
| `check_definitions` | that every `J37_*` macro the build hands the compiler is read by a preprocessor branch under `Source/`; and that the IPP switch is the one JUCE's own FFT engine is gated on, is still thrown, and that the `JUCE_DSP_USE_INTEL_IPP` spelling which does not exist in JUCE 9.0.4 stays out of the build | `J37_HAVE_IPP` and `J37_HAVE_VDSP`, defined for the plugin by `cmake/J37VendorLibraries.cmake` and read by no file under `Source/` - two claims about the build that nothing checked, beside an IPP install that was discovered, linked and accelerated nothing. The JUCE half of the check needs the submodule and says so when it is not there |

## What the suite does **not** cover

Listed here rather than left to be discovered, because each one is a proposal in
[docs/plugin-improvement-plan.md](../docs/plugin-improvement-plan.md):

- **`processBlock`'s boundaries.** Zero frames, one frame, a prime block size, a
  rate change between blocks, a prepared-but-never-played plugin. The stages are
  measured; the edges between them are not.
- **The rendering backends.** `Source/GUI/NativeRenderer_{D3D11,D3D12,Vulkan,Metal}`
  are compiled by the platform builds and checked by nothing else. The Metal
  adapter's display-scale handling is the one part of it a Linux machine cannot
  even read: it is Objective-C against AppKit.
- **The GLSL as GLSL.** `check_shaders` proves the sources are intact and that the
  renderer and the shader agree on every uniform's NAME; it does not compile them.
  A dialect mistake - a construct a driver rejects - is still found by a driver and
  by nothing here. `glslangValidator` (glslang-tools) would close that, and it is
  the one addition worth making to this suite that needs a tool outside it.
- **What the plugin will load on.** The macOS workflow reads the built binaries'
  `minos` and the AU's own plist, and validates the AU with Apple's `auval` - but
  those run on GitHub's runner, not here, and none of them is a DAW.
- **Anything that needs a host.** Whether a VST3 loads, whether the editor opens,
  whether a real session recalls - that is what the platform build jobs and a
  human are for.
- **The sound.** No harness compares a rendered buffer to a reference: the claims
  are all local properties (a level, a ratio, a monotonicity), never "this is the
  tone we shipped in 1.2".
- **Translation coverage.** The check proves every existing key is live; it says
  nothing about how much of the panel is translated.

## How the extraction works

Four harnesses cannot include the plugin's own translation units, because those
need JUCE. They read `Source/` as **text** at build time instead:

| Extractor | Cuts | Its guard rails |
| --- | --- | --- |
| `tests/dsp/extract.py` | the inline shapers and the reduced signal path replica | reads `Source/` as one text across every file the processor is split into, so a fact that moves is found where it went, and a fact that lands in two files stops the harness |
| `tests/layout/extract.py` | the knob strip arithmetic and the constructor phases | writes its fragments into the harness's own build directory, and fails by name when an anchor disappears |
| `tests/state/extract.py` | the parameter table and the state format | looks each model list up in a short list of headers rather than assuming it lives in `PluginProcessor.h` |
| `tests/transport/extract.py` | the two flags the rig is driven by | names the file it searched in every failure message |

The rule they all follow: **a check that reads code as text must say which file
it read**, so that a move produces a failure that points at the move instead of a
mystery. When a stage moves into `Source/DSP/`, the extractor's entry for it is
deleted and the harness `#include`s the header - the direction of travel the
whole suite is built on, and the reason `Source/DSP/` is 26 headers today.
