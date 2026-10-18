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

`cmake/J37Dependencies.cmake` names 42 packages. They are grouped below by what they
contribute, not by where they are declared. In every group the question is the same one:
**what would have to become true for this to earn a place in the binary?** For most of
them the honest answer is "nothing I can name", and that is a finding, not an omission.

### 2a. Called - linked and used today

| Library | Pin | Licence | Caller |
| --- | --- | --- | --- |
| `RTNeural` | vendored `95c3c0f9` | BSD-3 | `Source/DSP/NeuralStage.h` |
| `dattorro-verb` | vendored `41e976a0` | MIT | `Source/DSP/DattorroRoom.h` |
| `nlohmann/json` | vendored `v3.12.0` | MIT | preset + model parsing |
| `pocketfft` | vendored `c90e55b3` | BSD-3 | `Source/DSP/SpectrumAnalyser.h` |
| `glm` | vendored `1.0.3` | MIT / Happy Bunny | `Source/GUI/TapeScene.*` |
| `melatonin_blur` | `v1.4` | MIT | panel shadows and halos |
| `foleys_gui_magic` | `d53d998e` | BSD-3 | `Source/PluginEditor.h` |
| `readerwriterqueue` | `v1.0.7` | BSD-2 | telemetry (WP2 adds a second caller) |
| `chowdsp_math` (+ `chowdsp_simd`, which it includes) | `v2.4.0` | BSD-3 | metering and auto-gain dB/exp paths |
| `xsimd` | vendored `14.3.0` | BSD-3 | **opt-in** via `J37_USE_SIMD_TANH` |
| `signalsmith-stretch` (+ `signalsmith-linear`) | vendored `1.4.0` / `0.6.4` | MIT | `Source/DSP/CorrectorStage.h` engine 2, **off by default** |
| `melatonin_inspector` | `9c483f86` | MIT | `JUCE_DEBUG` builds only |

The two rows marked **opt-in** are the model for how an audio-changing library arrives:
an `option()`, a probe, and a harness that prints what each shape renders.

### 2b. The defect: linked, never called - now gone

| Library | Licence | State |
| --- | --- | --- |
| `chowdsp_dsp_utils` | **GPLv3** | **Removed** (WP1). It was the only allowance `check_linkline.py` carried, and both allowance dictionaries are empty now. `chowdsp_simd` stayed: it is not an exception, it is a dependency of `chowdsp_math`, which declares it and includes it |

### 2c. Vendored, unlinked

| Library | Licence | State |
| --- | --- | --- |
| `SoundTouch` | **LGPL-2.1** | Declared as a target, built by nothing. WP4 decides: delete, or measure it into the corrector as a third engine |
| `farbot` | MIT | Real-time-safe utilities fetched for a caller that never arrived. Nothing needs it; `IrStage` and `NeuralStage` solve the same problem with JUCE and atomics |

### 2d. Fetched, unlinked - and what would change that

| Library | Licence | Verdict |
| --- | --- | --- |
| `dr_libs` | Unlicense / MIT-0 | **WP2.** A real feature (user noise print), no licence cost, one header |
| `kissfft`, `pffft`, `AudioFFT`, `audio_fft`, `fftconvolver` | BSD-3, BSD-like, MIT, MIT, MIT | §4. Five FFTs for one caller that already exists (`pocketfft`). Adopt only on a benchmark that names the FFT as the bottleneck |
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
| `NeuralAmpModelerCore` | MIT | **WP3** - the rest of the `.nam` format |
| `Eigen` | MPL-2.0 | **WP3**, as NAM Core's backend |
| `Assimp` (OBJ importer only) | BSD-3 | **WP5** - blocked on a licence-clean mesh existing, not on time |

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

### WP2 - `dr_libs` `dr_wav`: a user noise file for the vinyl stage

**Why.** `Source/DSP/VinylStage.h` synthesises its surface noise, and the plugin has no
way to use a real noise print. `dr_wav` is a single header, decodes every PCM/float WAV
including 24-bit, and the licence asks for nothing (Unlicense / MIT-0).

**Where.** A new `Source/DSP/NoiseLoader.h`, modelled one-for-one on the IR stage: a
background decode into a `juce::AudioBuffer`, a `loading` flag the panel reads, and a
hand-off to the audio thread with no allocation and no lock. `readerwriterqueue` (already
linked and called) carries the hand-off; the queue is bounded and a second request while
one is in flight replaces it rather than racing it.

**Steps.**

1. Fetch `dr_libs` (already pinned at `wav-0.14.5` in `J37Dependencies.cmake`) and add a
   `j37_declare_header_only_library(dr_libs ...)` target - `dr_wav.h` is a single header
   and needs no translation unit.
2. Add `J37_HAS_DRWAV` to `OptionalLibraries.h`, in the *guarded* family (the harness
   shape), because the file that uses it should compile with it absent.
3. `NoiseLoader`: `prepare(sampleRate, maxSeconds)`, `requestLoad(File)`,
   `takePending(AudioBuffer&)`; `/W4`, `-Wall` clean.
4. `VinylStage` gains a `noiseLoop` the synth noise is blended against; NONE means the
   synthesised print, which is what every build did before.
5. Panel: the file chooser the IR loader already has, pointed at `.wav/.wave/.aif` - the
   AIFF case is JUCE's reader, not dr_wav, and is worth having because the decode is
   already async.
6. `tests/dsp/noise_loader_test.cpp`: write three fixture WAVs (16-bit mono, 24-bit
   stereo, 32-bit float) with a known ramp, decode them through the loader, assert
   sample count and every sample within half an LSB. Then the headless path: no file,
   `takePending` returns false, the stage renders exactly what it did before.

**Gate.** The three fixtures decode to the values that were written; a 24-bit file whose
samples exceed `[-1, 1)` is clipped once, at load; the render with no file loaded is
bit-identical to the pre-change render (the harness prints the difference and fails on
non-zero).

**Risk.** Loop points and sample-rate mismatch: a 48 kHz print used at 96 kHz needs
either a resample at load (cheap, off the audio thread) or an honest refusal. Choose
resample-at-load and state it in the panel.

**Cost.** A day including the harness.

### WP3 - `NeuralAmpModelerCore` + Eigen: the rest of the `.nam` format

**Why.** `Source/DSP/NeuralStage.h` already *reads* `.nam` files, but only the `LSTM`
architecture - it slices the flat weight vector into RTNeural layers and refuses anything
else, loudly, because "a model that loads and sounds wrong" is worse than one that fails.
WaveNet and Feather are most of the profiles in circulation. NAM Core is MIT and is the
reference implementation of all of them.

**Where.** The seam already exists and does not change: `NeuralStage::loadFromJson`, the
`buildFromNam` converter, and the `activeModel` / `retiredModels` hand-off. The work is a
second backend behind that same seam, not a second stage.

**Steps.**

1. Fetch `NeuralAmpModelerCore` (pin the newest release tag, currently `v0.5.x`, by
   commit SHA - see the pinning note at the top of `J37Dependencies.cmake`) and `Eigen`
   as its backend, MPL-2.0. Both are configured, not vendored: NAM Core's own CMake is
   a real project.
2. `J37_HAS_NAMCORE` in `OptionalLibraries.h`, guarded, default **off**
   (`-DJ37_NAMCORE=ON`). Two inference engines in one binary is a size and build-time
   cost, so it is an option until the measurement below says it is the default.
3. New `Source/DSP/NamBackend.h`: `bool load(const nlohmann::json&)`, `void prepare(double
   rate)`, `float process(float)`, `reset()`. RTNeural stays the LSTM path; NAM Core
   takes WaveNet/Feather **and** LSTM, so the converter can then be deleted in one
   follow-up commit once the differential test below passes.
4. Model loading keeps the IR stage's shape: parse and build on the background thread,
   publish with `activeModel`, retire the old object. NAM Core's `Model` allocates on
   construction and that is exactly why it never happens on the audio thread.
5. `tests/neural` grows a differential case: render one second of the same DI through
   (a) the existing `buildFromNam` converter and (b) NAM Core, for a small LSTM `.nam`
   checked into `tests/neural/fixtures/`, and report max absolute difference. Then a
   WaveNet fixture, for which the gate is only "finite, bounded, and the right length" -
   it has no counterpart in the old path to be equal to.
6. Bypass stays bit-exact and the model rate still goes through `RateBridge`, which is
   what makes a 48 kHz profile usable at 96 kHz.

**Gate.** LSTM: max abs difference against the converter under -100 dBFS (they are the
same arithmetic in a different order, so this is a formatting question, not a modelling
one). WaveNet: no NaN/Inf, output peak within the fixture's expected range, and the
process cost measured by `tests/dsp/run_bench.sh` and printed, not hidden. Bypass and
"no model" remain bit-identical to a build without NAM Core.

**Licence.** MIT (NAM Core, © 2023 Steven Atkinson) and MPL-2.0 (Eigen). Neither is
copyleft in the plugin-linking sense; both need a `THIRD_PARTY_NOTICES.md` row and a
README table row, and Eigen's MPL means its own files stay under MPL while the binary
as a whole does not.

**Risk.** Binary size and CPU: WaveNet is a convolution stack and costs far more per
sample than an LSTM. That is a scheduling decision (run the model at 48 kHz and bridge,
never per-oversampled-sample), not a reason to refuse, but it has to be measured before
the option becomes a default. The other risk is that NAM Core's object model is heavier
than the RTNeural seam expects - the `retiredModels` list is what keeps that safe.

**Cost.** Two to three days, most of it the fixtures and the size/CPU measurement.

### WP4 - `SoundTouch`: decide, then delete or use

**Why.** SoundTouch 2.4.1 is vendored under `ThirdParty/soundtouch`, buildable, and
linked by nothing. It is **LGPL-2.1** - statically linked into a distributed plugin, that
is the obligation the notices already flag, and it buys nothing today.

**The decision.** The corrector already has two engines, and the one that ships is the
one that measured best (`tests/pitch` prints both: the granular shifter is sub-cent and
one period of delay; the vocoder's transposition is 6-10 cents wide at three times the
latency). A third stretcher has to beat *both* on that harness to be worth an LGPL
obligation, and nothing in the code suggests it will: SoundTouch is a WSOLA shifter for
time-stretching, which is the same family as the granular engine already shipping.

**Recommendation.** Remove the `SoundTouch` target block from
`cmake/J37Dependencies.cmake` and the vendored tree from `ThirdParty/`, together with its
`THIRD_PARTY_NOTICES.md` section. If a future pass wants to test the hypothesis, it can
be re-vendored cheaply - the target declaration is already written and the branches
(`feature/soundtouch-vendored`) keep the work.

**Gate (if it is ever used).** It enters `CorrectorStage` as engine 3 behind its own
`option()`, and `tests/pitch` has to print its error/latency next to the other two before
anything links it.

**Cost.** Deletion: minutes, once the licence text's removal is reviewed.

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
| 2 | WP2 - `dr_wav` noise loader | A real user feature with a small, self-contained gate, and it exercises the loader pattern that WP3 then reuses |
| 3 | WP3 - NAM Core + Eigen | The largest user-visible gain (most `.nam` profiles load) and the largest measurement burden; start it only with the fixtures written first |
| 4 | WP4 - delete `SoundTouch` | Follows WP3 so the corrector's engine set is settled before it is pruned |
| 5 | WP5 - `Assimp` | Blocked on an asset, not on time |
| 6 | WP6 - inventory truth | Done with this file |

WP1 and WP4 are the two that make the binary *smaller* and the licence story simpler.
They are not glamorous, and they are the ones worth doing early for that reason: every
later item inherits the link line they leave behind. WP1 is done; WP4 waits for WP3 so
the corrector's engine set is settled before it is pruned.
