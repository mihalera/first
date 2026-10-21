# Integrating and implementing the external libraries

Every library below JUCE is already *present*: `cmake/J37Dependencies.cmake` fetches it
at configure time or `ThirdParty/` vendors it. What is not present for most of them is a
**caller**. This file is the plan that turns "fetched" into "used", in the order that
costs least and proves the most, with the gate each one has to pass before its name goes
on `target_link_libraries`.

It is deliberately not a wish list. Three of the sections below end in "do not do this",
and they say why.

Read alongside:

- [requested-roadmap-status.md](requested-roadmap-status.md) - the audit of a proposed
  roadmap against what the tree already does;
- [plugin-improvement-plan.md](plugin-improvement-plan.md) - the review's priorities,
  including the licence decision this file depends on;
- [architecture.md](architecture.md) and [tests/README.md](../tests/README.md) - how a
  stage is written and how a stage is measured.

## 1. The gate

Five rules, each with the file that enforces it. A library that breaks one of them is
either not linked or turned off by default, and either way the reason is written down
next to the thing.

| # | Rule | Enforced by |
| --- | --- | --- |
| 1 | **A caller before a link line.** Nothing is linked that no file under `Source/` includes. Intent is not a caller; a dependency's own `dependencies:` line is. | `tests/linkline/check_linkline.py` fails a linked target with no include. Its allowance dictionaries are empty |
| 2 | **A probe, so the build survives without it.** The header decides a `J37_HAS_*` macro; the including file pulls the library in itself. A checkout with `ThirdParty/` pruned still builds and still passes the harness. | `Source/DSP/OptionalLibraries.h`; `tests/spectrum/run.sh` compiles the analyser **both ways** so the absent branch is compiled, not assumed |
| 3 | **A harness that renders and prints a number.** "It compiles" is not evidence. The gate for each item below is a measurement of audio, not of linkage. | `tests/<area>/run.sh` + `run.sh`'s exit status in CI `dsp-tests` |
| 4 | **A licence entry and, for copyleft, a decision.** | `THIRD_PARTY_NOTICES.md`, the README third-party table, and `check_linkline.py`'s ownership notes |
| 5 | **Nothing on the audio thread that allocates or locks.** Load in the background, swap in with an atomic, retire the old object rather than freeing it under the audio thread. | The pattern in `Source/DSP/IrStage.h` (JUCE's wait-free `loadImpulseResponse`) and `Source/DSP/NeuralStage.h` (`activeModel` + `retiredModels`) |

## 2. Where every library stands

`cmake/J37Dependencies.cmake` names 43 packages - declarations, whether the vendored
tree or the CPM fallback ends up serving them. They are grouped below by what they
contribute, not by where they are declared. In every group the question is the same one:
**what would have to become true for this to earn a place in the binary?** For most of
them the honest answer is "nothing I can name", and that is a finding, not an omission.

### 2a. Called - linked and used today

| Library | Pin | Licence | Caller |
| --- | --- | --- | --- |
| `RTNeural` | vendored `95c3c0f9` | BSD-3 | `Source/DSP/NeuralStage.h`, the LSTM `.nam` path |
| `NeuralAmpModelerCore` | vendored `0b3d3c97` (v0.6.0) | MIT | `Source/DSP/NamBackend.h` - **on by default** (`J37_NAMCORE`), the WaveNet/ConvNet `.nam` path, measured at 14.5 % of one 48 kHz core per channel |
| `Eigen` | vendored `bc3b3987` (3.5.0) | MPL-2.0 | NAM Core's linear algebra, and nothing else |
| `dattorro-verb` | vendored `41e976a0` | MIT | `Source/DSP/DattorroRoom.h` |
| `nlohmann/json` | vendored `v3.12.0` | MIT | preset + model parsing |
| `pocketfft` | vendored `c90e55b3` | BSD-3 | `Source/DSP/SpectrumAnalyser.h` |
| `glm` | vendored `1.0.3` | MIT / Happy Bunny | `Source/GUI/TapeScene.*` |
| `melatonin_blur` | `v1.4` | MIT | panel shadows and halos |
| `foleys_gui_magic` | `d53d998e` | BSD-3 | `Source/PluginEditor.h` |
| `readerwriterqueue` | `v1.0.7` | BSD-2 | telemetry (WP2 deliberately did not add a second caller - see the WP2 note) |
| `dr_libs` (`dr_wav.h`) | vendored `wav-0.14.5` | Unlicense / MIT-0 | `Source/DSP/NoiseDecoder.*`, the vinyl stage's noise print |
| `chowdsp_math` (+ `chowdsp_simd`, which it includes) | `v2.4.0` | BSD-3 | metering and auto-gain dB/exp paths |
| `xsimd` | vendored `14.3.0` | BSD-3 | **opt-in** via `J37_USE_SIMD_TANH` |
| `signalsmith-stretch` (+ `signalsmith-linear`) | vendored `1.4.0` / `0.6.4` | MIT | `Source/DSP/CorrectorStage.h` engine 2, **off by default** |
| `melatonin_inspector` | `9c483f86` | MIT | `JUCE_DEBUG` builds only |

The rows marked **opt-in** are the model for how an audio-changing library arrives: an
`option()`, a probe, and a harness that prints what each shape renders. NAM Core followed
that model one step further: it arrived opt-in, printed its numbers, and became the
default when they said it could - see WP3.

### 2b. The defect: linked, never called - now gone

| Library | Licence | State |
| --- | --- | --- |
| `chowdsp_dsp_utils` | **GPLv3** | **Removed** (WP1). It was the only allowance `check_linkline.py` carried, and both allowance dictionaries are empty now. `chowdsp_simd` stayed: it is not an exception, it is a dependency of `chowdsp_math`, which declares it and includes it |

### 2c. Vendored, unlinked

| Library | Licence | State |
| --- | --- | --- |
| `farbot` | MIT | Real-time-safe utilities fetched for a caller that never arrived. Nothing needs it; `IrStage` and `NeuralStage` solve the same problem with JUCE and atomics. `ThirdParty/farbot/` is small, header-only and MIT, so it stays as the one-line-away option §2d keeps for a stage that wants it |

(SoundTouch left this table in WP4: its target, fetch and vendored tree are gone, and
with them the last LGPL text in the repository.)

### 2d. Fetched, unlinked - and what would change that

| Library | Licence | Verdict |
| --- | --- | --- |
| `kissfft`, `pffft`, `AudioFFT`, `audio_fft`, `fftconvolver` | BSD-3, BSD-like, MIT, MIT, MIT | §4. Five FFTs for one caller that already exists (`pocketfft`). Adopt only on a benchmark that names the FFT as the bottleneck |

(One row left this table in WP2: `dr_libs` had a caller added rather than a reason found, and it now sits in 2a.)
| `dywapitchtrack`, `yin_pitch`, `pitch_detector` | MIT / see upstream | §4. Three rival detectors against a measured YIN. A replacement has to beat the printed error, not the include graph |
| `sndfile` | **LGPL-2.1** | Not planned while `dr_wav` decodes the format the feature needs. An LGPL decoder is the same obligation WP4 is trying to shed |
| `q_dsp` / `cycfi_infra` | MIT | §4. The transient shaper's envelopes are its own and pinned by `tests/unit` |
| `rack`, `yasio`, `imgui`, `sokol_gfx` | GPL-3 + plugin exception, MIT + Anti-996, MIT, zlib | §4. A host runtime, a socket library, and two UI/graphics toolkits this product has its own answers for |
| `transdes`, `whetstone`, `multiband_transient_shaper` | MIT, BSD-3, MIT | Research copies of shaper ideas. The shaper they informed is `Source/DSP/TransientShaper.h`; none of the three is a dependency of it |
| `bgfx`, `wgpu-native`, `dawn` | CC0/BSD-2, Apache-2.0, BSD-3 | Renderer **options** (`J37_RENDER_BACKEND`), not in the default build. Adopting one is a renderer rewrite; the four native backends already ship |

### 2e. Fetched to read, never built

`iPlug2` (custom permissive), `chowcentaur` (BSD-3), `vcv_prototype` (GPLv3),
`soundpipe` (MIT), `aubio` (GPL-3). Each is either another plugin framework or needs
generated files a bare checkout does not have. They are pinned so the code to lift from
is on disk; none of them is a candidate for the link line, and none of them is in the
repository's licence obligations for a binary.

### 2f. Development only

`Catch2` (`v3.9.1`, BSL-1.0) and `nanobench` (`v4.6.0`, MIT), under
`-DJ37_BUILD_TESTS=ON`. No shipped build sets it.

### 2g. Not fetched yet

| Library | Licence | When |
| --- | --- | --- |
| `Assimp` (OBJ importer only) | BSD-3 | **WP5** - blocked on a licence-clean mesh existing, not on time |

`NeuralAmpModelerCore` and `Eigen` left this table in WP3: both are vendored under
`ThirdParty/`, pinned by commit, and wired behind `J37_NAMCORE` - see 2a.

## 3. The plan

### WP1 - Remove `chowdsp_dsp_utils` from the link line (licence debt) - **done**

**Why.** It was the one library the link line claimed and no file called, and unlike the
others it is **GPLv3**: a distributed binary that links it takes on an obligation it gets
nothing for. This is the mechanical half of
[plugin-improvement-plan.md](plugin-improvement-plan.md) item 1.1: taking the third of
the three options it lists - ship without the GPLv3 module - rather than shipping under
GPLv3 or buying the commercial one.

**What was done.** `chowdsp::chowdsp_dsp_utils` left the link line, and the
`J37_HAS_CHOWDSP_DSP` probe left `Source/DSP/OptionalLibraries.h` with it - it was a
macro no file read, kept alive only by the `DEFERRED_PROBES` allowance.
`tests/linkline/check_linkline.py` lost the `SPELLINGS` row and the deferred entry, and
**both allowance dictionaries are now empty**. README, `THIRD_PARTY_NOTICES.md` and
`tests/README.md` say the same thing the build does.

**One prediction the plan got wrong, and the evidence that corrected it.** The first draft
of this item also proposed dropping `chowdsp::chowdsp_simd`, on the strength of the
`EXEMPT` row that said the module "arrives through `chowdsp_dsp_utils`". It does not.
Upstream v2.4.0's own module declaration is the answer:

```
ID:            chowdsp_math
version:       2.4.0
dependencies:  chowdsp_data_structures, chowdsp_simd
```

and `chowdsp_math.h` includes `<chowdsp_simd/chowdsp_simd.h>` directly. So `chowdsp_simd`
is a **real dependency of the module the hot paths call**, not a leftover: dropping it
would have removed the SIMD module `chowdsp_math` compiles against. It stays, and its
`EXEMPT` row now names the right parent. That is the whole argument for reading the
dependency's own file instead of trusting a comment about it - including this one.

**Gate.** `tests/linkline/check_linkline.py`: 30 targets, 6 probes, every linked library
has a caller, every probe is read, no deferrals. A configure and a CI build are the
remaining half of the gate and run on the next push.

**Cost.** Under an hour, plus one CI run.

### WP2 - `dr_libs` `dr_wav`: a user noise file for the vinyl stage - **done**

**Why.** `Source/DSP/VinylStage.h` synthesises its surface noise, and the plugin had no
way to use a real noise print - the click, the rumble and the hiss a record collector
means when they say "that pressing". `dr_wav` is a single header, decodes every PCM and
float WAV including 24-bit, and the licence asks for nothing (Unlicense / MIT-0).

**What was built.**

1. **Vendored**, not fetched-only: `ThirdParty/dr_libs/dr_wav.h` at `wav-0.14.5` with its
   `LICENSE`, and `cmake/J37Dependencies.cmake` prefers it over the CPM fallback. This is
   the one decision the plan got wrong and the harness forced: a test that has to build the
   decoder BOTH ways cannot wait for a configure, so a checkout has to be enough. It is
   the same reason `pocketfft` and `dattorro-verb` are vendored.
2. `J37_HAS_DRWAV` in `OptionalLibraries.h`, in the **unguarded** family - unguarded is
   what lets the harness opt in by adding an include path rather than by undefining a
   macro nobody can see.
3. `Source/DSP/NoiseDecoder.h` - the JUCE-free half: `NoisePrint`, `decodeWavMemory`,
   `resamplePrint`, and the refusals with their reasons. `Source/DSP/NoiseDecoder.cpp` is
   the one translation unit that defines `DR_WAV_IMPLEMENTATION`.
4. `Source/DSP/NoiseLoader.h` - the JUCE half: the file on a worker thread, one atomic
   pointer swap per load, a retirement list so a print the audio thread is inside is never
   freed under it, and a rate conversion in `prepare`. Exactly the shape `IrStage` and
   `NeuralStage` use.
5. `VinylStage` gained a print pointer and a position. A loaded print **replaces** the
   synthesised groove hiss rather than adding to it, and CRACKLE, DUST, CLICKS, SCRATCH,
   WARP and ELECTRICAL keep working over it - the print is the surface, the rest is the
   damage and the deck.
6. `tests/dsp/noise_loader_test.cpp`, built **twice** by `tests/dsp/run.sh` (with
   `ThirdParty/dr_libs` and without) and asserting its own shape either way.

**Gate, and what it measured.** Sixteen checks in each binary, all passing. The three
formats decode to the samples that were written (within half an LSB); a mono file is the
same surface on both sides; a float file outside `[-1, 1)` is left where it is; a 44.1 kHz
print converts to 48 kHz with the loop's seam **wrapped**, so there is no click per
repeat; an empty buffer, a truncated file, a NaN sample and a print over the minute cap
are each refused with a reason; and with the generators switched off the stage's output
IS the print's sample times the gain, sample for sample over two passes. The absent binary
asserts the other branch says which library is missing rather than loading nothing
quietly.

The gate as written asked for one thing differently: it said a file over `[-1, 1)` should
be "clipped once, at load". That is wrong for a noise print and the code says so - a print
is a recording of a surface at its own level, and normalising or clipping it would make
two prints of the same surface at different levels sound identical, which is the opposite
of what the control is for. A FLOAT file is therefore left alone; integer formats are
exact by construction.

**The picker, landed after that gate.** The engine alone was not the feature: a print
that can only be loaded through `FirstAudioProcessor::loadNoiseFile` is not a control a
user has. So the VINYL page - which until then carried nothing but three knobs, and
reserved no band under them at all - now owns the third file-fed picker, built beside
the model and IR ones in `Source/GUI/EditorSetup.cpp` and filtered to `*.wav;*`: dr_wav
is a WAV decoder, and `*` keeps an AIFF selectable so the readout can say what it was
rather than hiding it from the dialogue. The band under the three knobs carries the
print's name across two cells and LOAD PRINT / CLEAR PRINT under it, which is the same
arrangement the MODELS and FRONT END pages use for their own member controls.

The readout has FOUR states rather than the other pickers' two, because this is the
only load that is not finished when the chooser's callback returns: the decode runs on
`NoiseLoader`'s own thread, so `LOADING PRINT` covers the interval where `NO PRINT`
would be a lie about a file that is being read, and `INVALID PRINT (reason)` adopts the
loader's refusal from `getNoiseError()` on the next tick - the reason arrives after the
callback, which is why this readout polls where the model and IR ones read
synchronously. The reason outranks a print that is still playing, exactly as `INVALID
IR` does. A print is not saved with the session, and the picker reopens in the folder
the last one came from.

**Cost so far.** The engine half and the panel half, both done. The gates that measure
the panel - `tests/layout`, `tests/theme`, `tests/translations` and the JUCE editor
harness - were the reason the picker was a separate change, and all four pass with the
page as built.

### WP3 - `NeuralAmpModelerCore` + Eigen: the rest of the `.nam` format - **done**

**Why.** `Source/DSP/NeuralStage.h` already *reads* `.nam` files, but only the `LSTM`
architecture - it slices the flat weight vector into RTNeural layers and refuses anything
else, loudly, because "a model that loads and sounds wrong" is worse than one that fails.
WaveNet is most of the profiles in circulation, and the convolutional nets the stage's
comment calls "Feather" are the legacy half of the same refusal. NAM Core is MIT and is
the reference implementation of them.

**Where.** The seam already exists and does not change: `NeuralStage::loadFromJson`, the
`buildFromNam` converter, and the `activeModel` / `retiredModels` hand-off. The work is a
second backend behind that same seam, not a second stage.

**Steps.**

1. Fetch `NeuralAmpModelerCore` (pin the newest release tag, currently `v0.5.x`, by
   commit SHA - see the pinning note at the top of `J37Dependencies.cmake`) and `Eigen`
   as its backend, MPL-2.0. **Landed as vendored trees with CPM fallbacks at the same
   pins**, not as configured packages: that is what every other library here does, and
   NAM Core's own CMake is a standalone project whose flags, version floor and `tools/`
   executables a plugin build should not inherit.
2. `J37_HAS_NAMCORE` in `OptionalLibraries.h`, guarded, default **off** at that point
   (opt in with `-DJ37_NAMCORE=ON`). Two inference engines in one binary is a size and
   build-time cost, so it was an option until the measurement below said it should be
   the default - which it now is, and the opt-out is `-DJ37_NAMCORE=OFF`.
3. New `Source/DSP/NamBackend.h`: `bool load(const nlohmann::json&)`, `void prepare(double
   rate)`, `float process(float)`, `reset()`. **Landed exactly that way.** RTNeural stays
   the LSTM path and NAM Core takes WaveNet/ConvNet, and it takes LSTM too in a build
   with NAM but no RTNeural; deleting the converter outright is a follow-up commit, and
   deliberately not this one - it would delete the differential below.
4. Model loading keeps the IR stage's shape: parse and build on the background thread,
   publish with `activeModel`, retire the old object. NAM Core's `Model` allocates on
   construction and that is exactly why it never happens on the audio thread.
5. `tests/neural` grows the differential case: render one second of the same DI through
   (a) the existing `buildFromNam` converter and (b) NAM Core, for a small LSTM `.nam`
   checked into `tests/neural/fixtures/`, and report max absolute difference. Then a
   WaveNet fixture, for which the gate is only "finite, bounded, and the right length" -
   it has no counterpart in the old path to be equal to. **The fixtures and the yardstick
   half of this landed first**, because a backend written before its yardstick has
   nothing to be compared against: see the section below and
   `tests/neural/fixture_test.cpp`.
6. Bypass stays bit-exact and the model rate still goes through `RateBridge`, which
   is what makes a 48 kHz profile usable at 96 kHz.

**What the fixtures pin, measured rather than assumed.** The first pass of this item did
what the plan asked and stopped there - it wrote the yardstick, not the backend. The
second pass built the backend behind it:

- `tests/neural/fixtures/lstm_small.nam` (LSTM: 1 layer, 1 input, 4 hidden, 109 weights)
  and `tests/neural/fixtures/wavenet_small.nam` (WaveNet: one layer array, 2 channels,
  kernel 2, dilations 1 and 2, 41 weights) exist, are checked into the tree, and are
  authored for this gate rather than copied from a capture - their `metadata` says so.
  `tests/neural/fixture_test.cpp` parses them, computes the weight count each config's
  layout implies and requires the file to match it, and drives them; `tests/neural/run.sh`
  builds and runs it, and CI's `dsp-tests` job runs that script already, with and without
  `J37_SANITIZE`. The pair is LSTM + WaveNet because those are the two the stage's
  refusal named: the converter covers the LSTM and nothing else, and the `"Feather"` the
  stage's comment calls out is legacy naming for a convolutional net the current
  reference player has no such architecture for. The backend is asked for the WaveNet,
  and the file says so.
- The LSTM fixture goes through the SHIPPING stage - the real
  `Source/DSP/NeuralStage.h`, against the REAL vendored RTNeural, via
  `buildFromNam` - and reproduces NAM Core's own output for a fixed 64-frame input to
  **1.19e-7** at frames [32, 48). NAM Core's numbers were produced from a clone of the
  upstream project (its `get_dsp` path, 64 frames at 48 kHz, mono float) while the
  fixtures were authored, and are recorded in the test as the oracle. The gate's -100
  dBFS is therefore already met at about -138 dBFS, and the day RTNeural's conversion
  drifts the fixture test says so.
- The WaveNet fixture is rendered to **9.3e-10** by a reference evaluator that lives in
  the test and covers exactly the shape the fixture declares (one layer array, no
  post-stack head, ungated, Tanh, no FiLM, 1-in/1-out). It is a YARDSTICK, not a backend:
  it refuses any other shape rather than approximating it, so it cannot grow into a
  second implementation of what this item is meant to add.
- Until the backend landed, the shipping stage REFUSED the WaveNet fixture and that
  refusal was asserted as the known gap rather than as a failure - so it flipped on the
  day there was an engine to accept the file.
- The WaveNet refusal is GONE where there is a backend. `NeuralStage::buildFromNam`
  sends an LSTM to the converter (when RTNeural is in the build) and everything else to
  NAM Core; a build without NAM Core still refuses it, and the harness compiles that
  build too rather than assuming it.

**What the second pass landed, and why each piece is shaped the way it is.**

- **Vendored, because that is what this tree does.** `ThirdParty/namcore/` is NAM Core's
  `NAM/` tree with its `LICENSE` and `README.md` at commit `0b3d3c97` (the `v0.6.0`
  tag); `ThirdParty/eigen/` is Eigen at `bc3b3987`, the commit NAM Core's own submodule
  pins (3.5.0), with all four `COPYING.*` files beside it. Both blocks in
  `cmake/J37Dependencies.cmake` fall back to CPM at the same pins when the trees are
  absent, exactly like every other vendored library here. NAM Core's own CMake is
  deliberately never configured: it is a standalone project with its own flags, a 3.10
  floor CMake 4 rejects and a `tools/` directory of executables - the sources are
  declared here instead.
- **An OBJECT library, not a static one, and that is a real defect this pass found.**
  NAM registers each architecture with a file-scope `static ConfigParserHelper` in its
  own `.cpp`, and `get_dsp()` reaches them only through the registry - no architecture
  object is referenced by name. A static archive is therefore pruned and every model
  fails at RUNTIME with "No config parser registered for architecture: LSTM" after
  linking cleanly. Objects on the link line cannot be pruned, which is also how
  upstream's own tools build it.
- **`J37_HAS_NAMCORE` is a two-header probe** (NAM's `get_dsp.h` and `Eigen/Dense`),
  because NAM cannot be used without Eigen and a tree with one of them is not a tree
  this backend can compile against. The option `J37_NAMCORE` is ON by default (it
  arrived OFF, and the process-cost gate below is what moved it); the probe reads the
  option, as xsimd's and signalsmith's do.
- **`Source/DSP/NamBackend.h` is NAM's own loader and per-sample loop**, with three
  details that cost real behaviour:
    - the sample buffers are `NAM_SAMPLE`, not `float`. The library defaults to double
      and switches to float only when its build defines `NAM_SAMPLE_FLOAT`, so a
      hard-coded float would either fail to compile or disagree with the library's ABI;
    - prewarm is off TWICE. `DspLoadOptions{prewarm=false}` only covers the load, and
      NAM restores the model to the caller's thread-local default (on) when it returns,
      so the instance's own switch is turned off too. Prewarm runs the model over
      silence, and the first samples of a take are what the player heard;
    - `prepare` runs on the message thread, pushes one zero frame through to make NAM
      grow its ring buffers while allocating is allowed, then resets. `reset` re-runs
      NAM's `Reset` at the exact arguments `prepare` established, which is what makes
      it allocation-free on the audio thread: NAM only resizes when a dimension
      CHANGES, and those dimensions cannot.
- **One seam in the stage.** `NeuralStage` now holds an `InferenceModel` - `forward` and
  `reset` - with an RTNeural adapter and a NAM Core adapter behind it, so the rate
  bridge, the active pointer, the retirement list and the deferred reset are still
  written once. LSTM stays on RTNeural even when both are present, because that is what
  keeps the two engines comparable; NAM Core takes the LSTM too in a build with NAM but
  without RTNeural.
- **One JSON version in the binary.** RTNeural bundles nlohmann 3.11.1 and includes it
  by relative path; NAM Core's compiler objects take `const nlohmann::json&`. Two copies
  in one translation unit are two TYPES with two mangled names and a linker error, so
  `OptionalLibraries.h` now includes the vendored 3.12.0 ahead of RTNeural's
  `model_loader.h` - the guard makes RTNeural's own include a no-op, and both libraries
  share the one copy. This was found by the harness, not by reading the code.
- **`Source/PluginProcessor.cpp`** compiles its "NEEDS LSTM" refusal only when
  `J37_HAS_NAMCORE` is 0. With the backend in the build that check would be a lie about
  what the stage can load, so the decision belongs to `buildFromNam`.
- **`tests/neural/run.sh` now builds NAM Core** from its own sources (the same glob
  upstream uses) and puts the objects on the link line for the reason above. It then
  compiles the stage in every shape the dispatch has: `fixture_test.cpp` with both
  engines (the differential build), the same file without NAM Core (the default build,
  where WaveNet is still refused), and `nam_only_test.cpp` with NAM Core and
  `J37_HAS_RTNEURAL=0` (where the LSTM has to go to NAM too, because there is no
  converter). The library compiles in parallel, one job per core, because sixteen Eigen
  translation units are about a minute serially and the same work is paid again under
  ASan in CI.

**Gate, and what it measured.** LSTM: max abs difference against the reference player
under -100 dBFS - **1.19e-7** through the shipping converter, and **5.96e-8** between
that converter and NAM Core's own LSTM on the same fixture (the fourth check in
`fixture_test.cpp`, printed by every run). WaveNet: the same comparison through NAM Core - **9.31e-10** from the
test's own evaluator, **2.79e-8** through the shipping stage, the larger number because
that path also goes through `RateBridge` and the stage's crossfade. Both are three
orders under the gate. Every sample is also checked finite and bounded, and clearing the
model returns the stage to the bit-exact pass-through in both the LSTM and WaveNet
cases.

**The second gate - process cost - and how it turned the default ON.** The first pass of
this item ended with the option OFF and the reason stated: no shipped build pays for a
convolutional stack it has not measured. It also said where that measurement belonged -
`tests/dsp/run_bench.sh` - which turned out to be the one part of the plan that was
wrong: that harness extracts headers and renders the engine's own DSP blocks, and it has
no model, no NAM Core and no fixtures in it. The measurement needs both engines and a
model of a realistic SHAPE, so it lives where those are, in `tests/neural`:

- `tests/neural/bench_nam.cpp`, built and run by `tests/neural/run_bench.sh`. A
  measurement, not a gate, for the same reason `tests/dsp/run_bench.sh` is one: it
  fails only when a model does not load or renders a non-finite sample, never because a
  number is slow.
- `tests/neural/nam_shapes.h` holds the weight-layout rules the fixture gate already
  checked against - now the single copy, used in both directions: `fixture_test.cpp`
  checks a fixture's length against it, and the benchmark GENERATES its models with it.
  A generator that disagreed with the loader cannot stay silent: NAM Core refuses a
  weights array of the wrong length, and that is how the first run of the benchmark
  found that a WaveNet with no post-stack head has eight output channels rather than
  one - a document NAM's own loader accepts and this plugin's stage, correctly,
  refuses.
- The models are the shape the FORMAT ships rather than the shape the fixtures use: a
  one-layer-array WaveNet with 16 channels, a 3-tap kernel, ten dilations from 1 to 512,
  an eight-channel head and the standard 1-tap post-stack head that reduces it to one
  output (10 874 weights), and a 20-cell LSTM (1 821 weights). Weights come from a
  deterministic generator, small enough to keep every activation in its responsive
  range - a profile with the real shape's arithmetic, which is all a cost measurement
  needs and is labelled as such in the file.
- Every row is measured twice: through `NeuralStage::process` (what one channel of the
  plugin costs, rate bridge and mixdown included) and through `NamBackend::process`
  alone, so the two bracket the stage's own share rather than it being estimated. The
  two fixtures are measured beside them, and an empty visit of the same signal as the
  loop's own cost. One sample per NAM call, because that is what a per-sample stage can
  hand a block API.

**What it measured**, on the machine that ran it (GCC, `-O2`, one core; `run_bench.sh`
prints the same table for any host, and the share is the machine-independent column):

| model | engine | ns/sample | share of one 48 kHz mono channel |
| --- | --- | ---: | ---: |
| WaveNet 16ch / 10 dilations | `NeuralStage` (NAM Core) | 3017.7 | **14.49 %** |
| WaveNet 16ch / 10 dilations | `NamBackend` alone | 3188.0 | 15.30 % |
| LSTM 20 cells | `NeuralStage` (RTNeural) | 1954.7 | **9.38 %** |
| LSTM 20 cells | `NamBackend` (NAM Core) | 589.1 | **2.83 %** |
| WaveNet fixture (2ch / 2 dilations) | `NamBackend` | 403.2 | 1.94 % |
| LSTM fixture (4 cells) | `NeuralStage` (RTNeural) | 258.7 | 1.24 % |
| empty visit (loop overhead) | neither | 0.6 | 0.00 % |

Run-to-run spread is a few percent on a shared host - three runs of the same binary put
the WaveNet row at 14.40, 14.49 and 14.66 %, and the RTNeural LSTM at 9.44, 9.38 and
8.70 % - so the honest reading is the half-point and the order of the rows, not the
digits. The benchmark prints all of it rather than a threshold for that reason.

Three findings, in the order they matter:

1. **The option is now ON.** A mono channel of the format's most common architecture
   costs about 14.5 % of one core at 48 kHz - about seven mono channels per core, and the
   stereo instance a master bus gets is 29 %. That is a cost the user asks for: with no
   model loaded the stage is a bit-exact pass-through and the number is zero, the model
   runs at its own rate so 8x oversampling does not multiply it, and what a build pays
   unconditionally is the object code (about 0.95 MB of `.text` across NAM Core's
   sixteen objects before the linker's dead-strip) and the compile time. A build that
   would rather not pay either passes `-DJ37_NAMCORE=OFF`, which restores the LSTM-only
   stage and its WaveNet refusal. The gate said "measured before it is the default";
   this is the measurement, and the default follows it rather than staying off for a
   number nobody had.
2. **The fixtures would have said the wrong thing**, which is why the first pass refused
   to measure them: 1.94 % for the WaveNet fixture against 14.49 % for the real shape,
   and 1.24 % against 9.38 % for the LSTM. A gate measured on the fixture would have
   concluded the stack was nearly free.
3. **NAM Core's LSTM is more than three times cheaper than RTNeural's** on the same model
   (2.83 % against 9.38 %). That is a real argument for the follow-up the plan names -
   deleting the converter - and it does not change the order: the converter is what the
   differential comparison runs against, and it stays until that comparison has been
   green for a release. It does mean the deletion, when it comes, is a CPU win as well
   as a deletion.

The stage's own share is below what this harness can resolve: the two WaveNet rows land
within a few percent of each other, and which one is nominally lower moves between runs
(the backend reading slightly HIGHER is cache and clock placement - the two models are
separate objects measured one after the other - not the stage being cheaper than the
engine it calls). The honest reading is that the bookkeeping and the dry/wet mixdown
are lost in the noise against a 3 microsecond model, and that with the two rates equal
`RateBridge` is absent rather than busy, exactly as its header promises. At a session
rate that does not match the model, that row would carry the conversion instead.

**What the gate still does not include.** `NAM_ENABLE_A2_FAST` stays undefined: it
routes one hand-optimised shape to a faster WaveNet and every other model to the generic
path, so enabling it would make the measured number the cost of one branch rather than
of the backend. The benchmark measures the generic path every model takes, and there is
no second number to compare it against until that flag is on.

**Licence.** MIT (NAM Core, © 2023 Steven Atkinson) and MPL-2.0 (Eigen). Neither is
copyleft in the plugin-linking sense; both need a `THIRD_PARTY_NOTICES.md` row and a
README table row, and Eigen's MPL means its own files stay under MPL while the binary
as a whole does not.

**Risk.** Binary size and CPU: WaveNet is a convolution stack and costs far more per
sample than an LSTM (measured above: 14.5 % against 2.8 % of a core). That is a
scheduling decision (run the model at 48 kHz and bridge, never per-oversampled-sample),
not a reason to refuse, and it is why the cost is paid only while such a model is
loaded. The other risk is that NAM Core's object model is heavier than the RTNeural seam
expects - the `retiredModels` list is what keeps that safe.

**Cost, in the end.** One pass for the fixtures and the oracle, one for the fetch, the
backend, the stage seam, the harness split and the documentation. The fixtures were the
right thing to build first: they are what caught the JSON-version collision, the
pruned-registration failure and the Reset-on-the-audio-thread question, each of which
compiles and looks right on its own. A third pass closed the last gate: the process-cost
benchmark, the realistic model shapes it needed, and the default they justify. One
follow-up the plan already names is still open - deleting the LSTM converter once the
differential has been green for a release, which turns LSTM into NAM Core's too and
leaves one engine, and which the benchmark now says is a CPU win as well. That deletion
is deliberately not made here: it would take away the comparison the backend was
accepted on.

### WP4 - `SoundTouch`: deleted - **done**

**Why.** SoundTouch 2.4.1 was vendored under `ThirdParty/soundtouch`, buildable, and
linked by nothing. It is **LGPL-2.1** - a static link into a distributed plugin is an
obligation the notices had to flag - and it bought nothing.

**The decision.** The corrector already has two engines, and the one that ships is the
one that measured best (`tests/pitch` prints both: the granular shifter is sub-cent and
one period of delay; the vocoder's transposition is 6-10 cents wide at three times the
latency). A third stretcher has to beat *both* on that harness to be worth an LGPL
obligation, and nothing in the code suggests it will: SoundTouch is a WSOLA shifter for
time-stretching, which is the same family as the granular engine already shipping.

**What was removed.** The target block in `cmake/J37Dependencies.cmake` (and with it
the CPM fetch, so a configure no longer downloads an LGPL library), the vendored tree
at `ThirdParty/soundtouch/` with its `COPYING.TXT`, the include path the editor harness
carried for it, and its rows in `THIRD_PARTY_NOTICES.md`, `README.md` and `LICENSE`.
That last one mattered: `LICENSE` still said SoundTouch was "linked statically" and
that libsndfile was "on the link line", both false since the link line was trimmed -
the file now states what ships. `Source/DSP/OptionalLibraries.h` keeps the history in
its entry rather than erasing it, and the CMake file keeps the reason where the block
was.

**Why now, when the order said after WP3.** The wait was for the corrector's engine
set to settle before pruning a stretcher. It is settled - two engines, both measured
by `tests/pitch` - and WP3 adds an inference engine for `.nam` models, not a third
pitch engine, so it cannot unsettle it. The order below is corrected rather than left
to imply a dependency that does not exist.

**Gate.** `tests/linkline/check_linkline.py` still passes with no allowance: a target
that no longer exists cannot be linked by accident. `tests/definitions`, `tests/theme`,
`tests/translations` and `tests/layout` are unaffected, and the editor harness compiles
the TUs with the include path gone. `grep -ri soundtouch` over everything but git
history now finds prose only: the sentences that record the removal, and the
snapshot in [plugin-analysis.md](plugin-analysis.md), which is explicitly labelled
history and carries the correction where it matters.

**If it is ever used.** It re-enters `CorrectorStage` as engine 3 behind its own
`option()`, and `tests/pitch` has to print its error/latency next to the other two
before anything links it; the vendored pin is still in this file's history.

**Cost.** An hour, most of it checking that the licence text and the tables say what the
tree now does.

### WP5 - `Assimp`, OBJ only: gated on there being a mesh

**Why.** The tape scene's hardware is generated in `Source/GUI/TapeScene.cpp` from
primitives. Adopting Assimp with only its OBJ importer was proposed so that a modelled
part could be loaded from a file.

**The gate is content, not code.** There is no `.obj` in the tree and no licence-clean
asset to load. Fetching a loader before an asset exists is the exact pattern this
repository already paid for once: eighteen entries came off the link line because no file
under `Source/` named them.
**Do not fetch it until a mesh with a licence arrives.** When one does: Assimp
configured with every importer off except OBJ, `J37_HAS_ASSIMP` guarded, `TapeScene`
loading it at startup off the message thread, and `tests/shaders` unaffected because the
shaders are generated, not imported.

**Cost.** Two days *after* the asset exists.

### WP6 - Make the inventory true (documentation, no code)

The status docs drifted from the tree in one place worth fixing: the multiband
correlation-driven anti-phase guard is **done** (`Source/DSP/MultibandPhaseGuard.h`,
Linkwitz-Riley 4th-order at 200 Hz and 2 kHz, per-band rotation, idle bit-exact, measured
by `tests/phase`), while [requested-roadmap-status.md](requested-roadmap-status.md) still
lists it as open and names it as the next thing to build. Fixed in the same pass as this
file.

## 4. Deliberately not planned

- **The duplicate FFTs** (`kissfft`, `pffft`, `fftconvolver`). `pocketfft` is the one the
  analyser calls, and it is already fast enough that `tests/dsp/run_bench.sh` does not
  list the FFT as a cost. A second FFT needs a measurement that says the first is the
  bottleneck.
- **A second pitch detector** (`dywapitchtrack`, `yin_pitch`, `pitch_detector`, `aubio`). The detector is
  YIN with a confidence gate and its error is printed by `tests/pitch` on every run. A
  replacement has to beat *that number* on the same signal set before it is linked, and
  `aubio` additionally does not build from a bare checkout.
- **`cycfi/q` RMS envelopes.** The transient shaper's fast/slow envelopes are its own,
  pinned by `tests/unit`. Adopting a library here would replace ten lines of arithmetic
  with a dependency and change the rendered audio for no measured gain.
- **`bgfx` / `sokol_gfx` / `ImGui` / `yasio` / `rack`.** Four of them are other people's
  plugin frameworks, or a renderer rewrite, or a socket library a plugin does not need.
  The four native renderer backends already ship and are tested; `ImGui` would be a
  second, immediate-mode UI inside a JUCE component that already has one.
- **Preset XML blobs.** Not a library, but the same shape of mistake: see
  [requested-roadmap-status.md](requested-roadmap-status.md#items-that-should-not-be-applied-as-written).

## 5. Definition of done, for every library

A library is finished - not merely added - when all seven are true:

1. pinned by commit SHA in `cmake/J37Dependencies.cmake` (or vendored with its licence
   file next to it);
2. `J37_HAS_*` in `Source/DSP/OptionalLibraries.h`, with the reason for guarded vs
   unguarded written next to it;
3. an option, if the answer could be "no", defaulting to the shape the measurement chose;
4. a caller under `Source/`, so `check_linkline.py` passes with no new allowance;
5. a harness that renders and prints the number the gate is stated in, and that also
   builds and checks the **absent** branch;
6. a row in `THIRD_PARTY_NOTICES.md` and one in the README table, naming the licence and
   what the binary takes on by linking it;
7. a CI job that runs the harness on every push (`dsp-tests` or a new job), not a manual
   instruction.

## 6. The order

| Order | Item | Why here |
| --- | --- | --- |
| 1 | WP1 - drop `chowdsp_dsp_utils` | **Done.** Minutes of work, removed a GPLv3 obligation from the binary, needed no new measurement |
| 2 | WP2 - `dr_wav` noise loader | **Done** - engine, VINYL page picker and docs. It exercised the loader pattern WP3 reuses |
| 3 | WP3 - NAM Core + Eigen | **Done.** The backend, the stage seam, the differential harness and the process-cost benchmark are in the tree; WaveNet and ConvNet `.nam` files load in the default build, because the measurement that the option was waiting for says they should (`tests/neural/run_bench.sh`) |
| 4 | WP4 - delete `SoundTouch` | **Done.** The corrector's engine set is settled (two engines, measured by `tests/pitch`), and WP3 does not touch it |
| 5 | WP5 - `Assimp` | Blocked on an asset, not on time |
| 6 | WP6 - inventory truth | Done with this file |

WP1 and WP4 are the two that make the binary *smaller* and the licence story simpler.
They are not glamorous, and they are the ones worth doing early for that reason: every
later item inherits the link line they leave behind. WP1, WP2, WP3, WP4 and WP6 are
done. WP3 is the only one that ADDS a library to the binary: it arrived opt-in, the
harness measured both shapes on every push, and the default moved when the process-cost
number arrived - 14.5 % of one core per channel for the format's most common
architecture, paid only while such a model is loaded.

### What this leaves open

Nothing else on the list is an integration to make. WP5 is blocked on an asset rather
than on code, §4 records why the duplicate FFTs, the rival pitch detectors, the Q
envelopes and the other UI/runtime toolkits are deliberately not adopted, and the two
libraries a user can see (the IR and the noise print) already decode what their
features need. The one common-audio-library question left - reading compressed or
container formats - has no use case: a noise print is a recording the user makes, and
the IR picker has JUCE's decoders behind it. So WP3 is the end of this plan, and after
it the honest answer is that the tree's own stages are the answer.
