# The test suite

An index over `tests/`, not a replacement for the prose inside it: every harness
header explains *why* it exists and what it was written for, and the tables below
answer the question you actually arrive with - **if I change X, what will catch
me?** - in one screen.

Everything here runs on a machine with nothing but a C++17 compiler, Python 3 and
bash. No JUCE build, no audio device, no plugin host, no CMake configure. The
whole suite is seconds of work, which is why it runs on every pull request and
not only where the plugin is built.

## Run it

```bash
tests/dsp/run.sh          # subharmonics (SUBFUND) + the loudness meter
tests/layout/run.sh       # knob geometry on every page, at four panel sizes
tests/state/run.sh        # session state round trip, and older state files
tests/transport/run.sh    # the transport rig, frame by frame
tests/pitch/run.sh        # the pitch tracker and the granular shifter
tests/neural/run.sh       # the rate bridge, and the neural stage with RTNeural stubbed

tests/dsp/check_headers_juce.sh     # every Source/DSP/*.h against real JUCE
tests/editor/check_editor_juce.sh   # every translation unit and Source/GUI/*.h against real JUCE

python3 tests/presets/check_presets.py           # the factory preset table
python3 tests/theme/check_theme_wiring.py        # theme re-inking coverage
python3 tests/translations/check_translations.py # the shipped translation tables
```

Two environment switches, both used by `build-linux-debug`:

| Variable | Effect |
| --- | --- |
| `J37_SANITIZE=address,undefined` | builds the harnesses in the first six rows with ASan + UBSan and turns them loose on the same assertions |
| `CXX` | the compiler to use; `g++` by default |

The micro-benchmarks are separate and informational: `tests/dsp/run_bench.sh`
(nanobench, needs `-DJ37_BUILD_TESTS=ON` to have fetched it) and the hot-path
probe step in `.github/workflows/build.yml`. Nothing fails a build because a
benchmark got slower.

## What each harness compiles

This is the part that decides how much a green run is worth. A harness that cuts
a span of text out of a big file tests that span; a harness that *includes the
shipping header* tests the code the plugin compiles.

| Harness | What it compiles | How it gets it |
| --- | --- | --- |
| `dsp` | the subharmonic generator, the loudness meter, and a reduced replica of the signal path | extracts spans through `tests/dsp/extract.py`, **and** `#include`s every stage that has moved to `Source/DSP/` |
| `layout` | the knob strip arithmetic and the editor's constructor phase order | `tests/layout/extract.py` cuts it out of `Source/PluginEditor.cpp` + `Source/GUI/` |
| `state` | the parameter table and the state functions | `tests/state/extract.py`, compiled against `tests/state/juce_stub.*` |
| `transport` | `TransportRig` and `RepitchStage` | `#include`s the shipping headers from `Source/DSP/` |
| `pitch` | `PitchTracker` and `RealtimePitchShifter` | `#include`s the shipping headers from `Source/DSP/` |
| `neural` | `RateBridge`, and `NeuralStage` with `J37_HAS_RTNEURAL=1` | `#include`s the shipping headers; RTNeural's two-method API is stood in for |
| `dsp/check_headers_juce.sh` | every header in `Source/DSP/`, against real JUCE | compiles each header's public surface; catches a header that only works under a harness shim |
| `editor/check_editor_juce.sh` | every translation unit the plugin builds, and every `Source/GUI/*.h` | compiles them against real JUCE; catches a moved block with a missing include |
| the three Python checks | no C++ at all | read `Source/` as text and compare tables to the code |

## What each harness guarantees

| Harness | The claims it pins | Written for |
| --- | --- | --- |
| `dsp` (subfund) | the undertone series is generated at the octave below, does not run away, stays silent below its threshold, and the anti-phase guard engages | the subharmonic generator shipping silent and then shipping louder than the programme |
| `dsp` (loudness) | calibration, linearity, rate independence, the 3.01 LU channel sum, the floor, and that `prepare()` then `reset()` leaves the curve in place | the K-weighting filter running on one pair of state variables for two channels, and `reset()` zeroing the coefficients |
| `layout` | every knob's tile stands inside its page and above the member band; no two tiles overlap; the strip is centred; the disc is drawable at the 780 x 664 floor; the value text clears its field's wall; the painted disc is the solved disc | a layout drifting away from the paint routine, and a constructor phase that quietly never ran |
| `state` | a written state reads back identical, an older state loads, an unknown key is ignored, and a missing parameter keeps its default | a bad pointer or an out-of-bounds read in the state path, which only ever shows up in someone's session |
| `transport` | START starts, STOP arrives at rest, the power cut spins down, and `platter()` moves the way the engine's level and modulation assume | START doing nothing on a running machine, and STOP approaching rest without reaching it |
| `pitch` | the tracker finds notes, notes with harmonics, and notes in noise; silence yields no note; the shifter holds its ratios | an out-of-range lag or a read past the delay line |
| `neural` (rate) | the bridge carries signal between the engine's rate and a model's, at the rate pairs the engine can produce | the stage running a model at the engine's rate while the model carries its trained rate |
| `neural` (stage) | the guarded half of `NeuralStage` - everything inside `#if J37_HAS_RTNEURAL` - compiles and runs | a branch no harness had ever compiled |
| `presets` | the preset names match the table, every sound-bearing parameter has a column, and the counts agree with `ParameterTable.cpp` | the preset box listing 20 names for a 26-row table, and 15 sound-bearing parameters silently keeping the session's value |
| `theme` | every widget the panel owns is re-inked when the theme changes | a control keeping the ivory panel's ink on charcoal, or falling back to the Look and Feel's own dark scheme |
| `translations` | no dead keys, every lookup present in every language, no ambiguous or empty entry | 17 Ukrainian entries keyed to sentences the plugin no longer said |
| `check_headers_juce` | each `Source/DSP/*.h` compiles against real JUCE, alone | a header that compiles under the harness shim and not in the plugin |
| `check_editor_juce` | each translation unit and each `Source/GUI/*.h` compiles against real JUCE | a moved block whose new file forgot an include; it caught three faults on its first run |

## What the suite does **not** cover

Listed here rather than left to be discovered, because each one is a proposal in
[docs/plugin-improvement-plan.md](../docs/plugin-improvement-plan.md):

- **`processBlock`'s boundaries.** Zero frames, one frame, a prime block size, a
  rate change between blocks, a prepared-but-never-played plugin. The stages are
  measured; the edges between them are not.
- **The rendering backends.** `Source/GUI/NativeRenderer_{D3D11,D3D12,Vulkan,Metal}`
  are compiled by the platform builds and checked by nothing else.
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
