# Libraries: what is in the build, and what could join it

[library-integration-plan.md](library-integration-plan.md) answers *how* a dependency gets
into this tree: a caller before a link line, a probe so the build survives without it, a
harness that renders and prints a number, a licence entry, and nothing that allocates or
locks on the audio thread. This file answers the question that comes before it: **which
libraries are already here, which ones are outside the tree and worth the gate, and which
ones are a bad idea for this plugin in particular.**

Three readings matter before a name becomes a task:

- **A candidate is not an approval.** "Adopt" below means "worth the next free hour", not
  "on the link line". Every runtime entry still has to pass the five rules above.
- **Dev and CI tools are not runtime dependencies.** A profiler, a validator or a fuzzer
  never appears in `target_link_libraries(first ...)`, so it does not carry the licence of
  a distributed binary. That distinction is what lets `pluginval`, which is GPLv3, be a
  useful CI step while the plugin itself stays clear of GPL code.
- **The cheapest library is the one already in the tree.** Four of the strongest items
  below - CLAP, LV2, `juce_animation`, `juce::dsp::Oversampling` - either are already in
  `JUCE/` or a CMake switch away, and cost no new dependency at all.

Versions and licences were checked against upstream releases in October 2026; the pins in
this repository are the ones in `cmake/J37Dependencies.cmake`, not the newest upstream.

## 1. In the binary today

Fourteen library entries on the link line, eight named JUCE modules (the rest arrive as
their dependencies), and two more behind `J37_NAMCORE`. The right-hand column is the
verdict, not a summary: all of these are earning their place, with the exceptions stated
in the verdict itself and in §4. Catch2, nanobench and CPM.cmake are dev-only or
build-time and reach no shipped binary.

| Component | Pin / version | Licence | Called from | Verdict |
| --- | --- | --- | --- | --- |
| **JUCE** | `9.0.4` submodule | AGPLv3 or commercial | the whole plugin | **Keep.** `audio_processors`, `audio_utils`, `audio_devices`, `cryptography`, `dsp`, `gui_extra`, `opengl` and `box2d` are linked; `juce_dsp` carries the oversamplers and the IR convolver, `box2d` the deck's drifting particles |
| **RTNeural** | `95c3c0f9` | BSD-3 | `Source/DSP/NeuralStage.h` | **Keep.** The LSTM `.nam` engine; compiled with `RTNEURAL_STL` and without `-mavx2`, so the binary still runs on pre-AVX2 machines |
| **NeuralAmpModelerCore** | `v0.6.0` (`0b3d3c97`) | MIT | `Source/DSP/NamBackend.h` | **Keep, behind `J37_NAMCORE` (OFF today).** WaveNet/ConvNet/Linear/Sequential `.nam`; the fixtures compare it to the reference player. The open question is whether it becomes the default - measured in `tests/neural`, not assumed |
| **Eigen** | `3.5.0` (`bc3b3987`) | MPL-2.0 | NAM Core's linear algebra | **Keep.** Header-only, vendored at the commit NAM Core's own submodule pins; file-level copyleft only |
| **nlohmann/json** | `v3.12.0` | MIT | `.nam` model parsing (`Source/PluginProcessor.cpp`, `NamBackend`, RTNeural's parser) | **Keep.** One copy for the whole build on purpose: RTNeural bundles 3.11.1, and two copies would be two C++ types with two mangled names. Presets are a different parser - `juce::JSON` |
| **chowdsp_math + chowdsp_simd** | `v2.4.0` | BSD-3 | metering and auto-gain dB/exp paths | **Keep.** `chowdsp_dsp_utils`, the GPLv3 sibling, is off the link line (WP1) |
| **xsimd** | `14.3.0`, vendored | BSD-3 | `Source/DSP/HotPathMath.h` | **Keep, and it is the default.** `J37_USE_SIMD_TANH` is ON: 2.4 ns against `std::tanh`'s 6.7 ns for a 1.19e-7 curve difference, which is below audibility and worth 1.5x on the shaper. `-DOFF` restores `std::tanh` bit for bit |
| **pocketfft** | `c90e55b3` | BSD-3 | `Source/DSP/SpectrumAnalyser.h` | **Keep.** One FFT, one caller, and the reason four other FFTs are rejected |
| **dattorro-verb** | `41e976a0` | MIT | `Source/DSP/DattorroRoom.h` | **Keep.** 400 lines of C, run at 48 kHz with the stage carrying the session's rate |
| **dr_libs** (`dr_wav`) | `wav-0.14.5`, vendored | Unlicense / MIT-0 | `Source/DSP/NoiseDecoder.*` | **Keep.** The vinyl stage's user noise print; the reason libsndfile is not needed |
| **glm** | `1.0.3` | MIT / Happy Bunny | `Source/GUI/TapeScene.*` | **Keep.** Header-only maths for the tape scene |
| **melatonin_blur** | `v1.4` | MIT | panel shadow, knob halo and cap shadow | **Keep.** Three cached `DropShadow`s; repaint is a blit rather than a blur |
| **melatonin_inspector** | `9c483f86` | MIT | `createDebugInspector()` | **Keep.** Debug builds only; passive in a shipped binary |
| **readerwriterqueue** | `v1.0.7` | BSD-2 | the telemetry queue, SPSC | **Keep.** One producer, one consumer, fixed capacity - exactly what it is for |
| **signalsmith-stretch** | vendored `1.4.0` | MIT | `Source/DSP/CorrectorStage.h`, engine 2 | **Keep, OFF by default.** 1216 frames of latency at 48 kHz and up to 32 cents of its own error at the stage's 20 ms window and eight analyses per window (a longer window does not close it: 60 ms reads 13 cents at 1.06x for three times the lookahead, and sixteen analyses still read 17 at 0.94x for four times the analysis work); the shifter stays the default until a configuration earns the swap |
| **Catch2** | `v3.9.1` | BSL-1.0 | `tests/unit/` | **Keep, dev only.** The suite also runs under ASan + UBSan |
| **nanobench** | `v4.6.0` | MIT | `tests/dsp/bench.cpp` | **Keep, dev only.** The shaper's micro-benchmark |
| **CPM.cmake** | pinned in `cmake/J37Dependencies.cmake` | MIT | configure time | **Keep.** Every dependency pinned by tag or commit; Renovate watches the tags |
| **foleys_gui_magic** | `d53d998e` | BSD-3 | `Source/GUI/ParameterSheet.h` | **Keep.** The PARAMETERS key on SETTINGS opens a sheet of every parameter, generated from the processor's own tree: one `MagicProcessorState`, one `MagicGUIBuilder`, a ValueTree per group and a live attachment per control. It was linked with only an `#include` and no caller until this shipped - see §4, item 1 |

Deliberately absent, and staying absent: a general FFT library (the analyser needs three
bins, and a Goertzel is cheaper than a transform), a pitch-detection library (the tracker
is YIN and `tests/pitch` measures its error), and any LGPL decoder or time stretcher
(WP4 removed the last one). A C++20 build with `std::format`, `std::span` and
`std::bit_cast` also removes most of the reason to reach for a utility library at all.

## 2. Fetched, not in the binary

`cmake/J37Dependencies.cmake` names 43 packages; most of them exist so that adopting one
is a probe and a line rather than a new dependency.
[library-integration-plan.md §2d](library-integration-plan.md) has the per-item reasoning;
the verdicts in one place:

| Group | Libraries | Verdict |
| --- | --- | --- |
| Alternative FFTs and a convolver | `kissfft`, `pffft`, `AudioFFT`, `audio_fft`, `fftconvolver` | **Reject.** `pocketfft` has the only caller and `juce::dsp::Convolution` has the only IR; adopt one only on a benchmark that names the FFT as the bottleneck |
| Three rival pitch detectors | `dywapitchtrack`, `yin_pitch`, `pitch_detector` | **Reject.** A replacement has to beat the YIN error `tests/pitch` prints, not the include graph |
| Declarative DSP toolkit | `q_dsp` + `cycfi_infra` | **Reject.** The transient shaper's envelopes are hand-written and pinned by `tests/unit` |
| Renderers | `bgfx`, `wgpu-native`, `dawn` | **Reject for now.** Five native backends already compile (OpenGL, Vulkan, Metal, D3D11, D3D12); adopting one of these is a renderer rewrite, not a link line |
| UI / runtime / sockets | `imgui`, `sokol_gfx`, `rack`, `yasio` | **Reject.** A second GUI toolkit, a socket library, and a host runtime this product has its own answers for |
| Research copies | `transdes`, `whetstone`, `multiband_transient_shaper` | **Reject.** They informed `TransientShaper.h`; none is a dependency of it |
| Read-only sources | `iPlug2`, `chowcentaur`, `vcv_prototype`, `soundpipe`, `aubio` | **Reject.** Another framework, or code lifted for reference; `aubio` and `vcv_prototype` are GPLv3, which is a licence this binary does not take on |
| Decoder | `sndfile` | **Reject while LGPL.** `dr_wav` decodes the format the feature needs |
| Real-time utilities | `farbot` | **No caller.** `IrStage` and `NeuralStage` solve the same problem with JUCE and atomics. Small, header-only, MIT - kept as a one-line-away option rather than linked |

## 3. Candidates from outside the tree

### 3a. Adopt: dev and CI only

These improve the *evidence* the repository produces, not the audio. None appears on the
link line and none changes a rendered sample; each is gated by a CI job or a macro that
is off in a release build.

| Candidate | Version | Licence | Why this project | Cost |
| --- | --- | --- | --- | --- |
| **RealtimeSanitizer (RTSan)** | LLVM 20+ (`-fsanitize=realtime`) | Apache-2.0 WITH LLVM-exception | The repository's rule 5 - "nothing on the audio thread that allocates or locks" - is enforced by review and by the shape of `IrStage`/`NeuralStage`, not by a tool. RTSan is that tool: mark `processBlock` `[[clang::nonblocking]]` and a `malloc`, a `free` or a mutex lock inside it fails the run with a stack trace. This is the highest-value item in this file | One CI job, a handful of annotations; Linux and macOS only (no Windows runtime yet) |
| **pluginval** | `1.0.4` stable (`2.0.0-beta3` exists) | **GPLv3**, as a separate executable | Hosts are where a plugin's state, threading and parameter edge cases actually break; `pluginval` drives them at increasing strictness. Used as a binary, not linked, so no copyleft spillover - the same boundary WP1 used to remove `chowdsp_dsp_utils` | Download per OS in CI; run level 5 and above |
| **libFuzzer** | ships with Clang (LLVM 23.x current) | Apache-2.0 WITH LLVM-exception | Two parsers accept untrusted files: `NoiseDecoder` (WAV) and `NamBackend` (`.nam` JSON). A malformed model or print is the realistic crash path, and the decoders already return reasons instead of throwing - fuzzing proves the reasons cover the inputs | Two small fuzz targets plus a seeded corpus; a nightly or PR job |
| **Tracy Profiler** | `0.14.1` | BSD-3 | A frame graph for the editor timer and a zone timeline for `processBlock`, with the locks and allocations that go with them. The repository's performance claims are already measured - the shaper's 42 us to 28 us per block, the neural stage's process cost - and Tracy is what turns the next "it feels slow" into a number | Instrumentation behind a `J37_TRACY` macro, a client library in debug/CI builds only, zero cost in a release build |
| **libebur128** | `1.2.6` | MIT | An independent oracle for `Source/DSP/LoudnessMeter.h`: feed the same fixtures to both and compare LUFS. A meter that is confidently 0.4 LU out is the kind of bug no listening test catches | One test binary, dev only; never linked into the plugin |
| **clang-tidy** | LLVM 23.x | Apache-2.0 WITH LLVM-exception | `.clang-format` exists and is followed, but nothing checks the classes of bug a compiler is told to ignore - use-after-move, missing `override`, narrowing in a callback, `noexcept` on a hot path | A `.clang-tidy` file and a job on changed files; the existing code will produce a first-run backlog |
| **sccache** | `0.18.0` | Apache-2.0 | Every CI job compiles JUCE and the vendored trees from scratch; the macOS job alone spends most of its fourteen minutes there. A compiler cache is the only change on this list that saves wall-clock time on every push | `CMAKE_CXX_COMPILER_LAUNCHER` plus a cache step; `ccache` works too but is GPL-3.0-or-later (still fine as a tool, not linked) |

### 3b. Adopt when the trigger fires

Each of these has a real use, and none has one today. The trigger is written down so the
answer is not re-derived next time.

| Candidate | Version | Licence | Trigger, and why not yet |
| --- | --- | --- | --- |
| **clap-juce-extensions** | no releases; pin a commit | MIT | **Trigger: a CLAP build is wanted.** JUCE 9.0.4 in this checkout has no CLAP wrapper (`JUCE/modules/juce_audio_plugin_client` has AU, AAX, LV2, Standalone, VST3 and no CLAP), and `J37_PLUGIN_FORMATS` is `AU AUv3 VST3`. CLAP is the native format in Bitwig and Reaper, though both still load VST3. Upstream added JUCE 9 support; the README's "JUCE 6, 7 and 8" heading lags it. Cost is a new format target plus the state/parameter mapping a second wrapper always needs - not a drop-in |
| **SLEEF** | `3.9.0` | BSL-1.0 | **Trigger: the audio-thread profile still shows `j37Tanh` (or the metering `exp`) as hot after the xsimd path.** SLEEF is a vectorised libm with a `tanh_u10` that can beat xsimd's; the existing 2.4 ns measurement is the bar it has to clear, and `tests/dsp/run_bench.sh` is where it would be measured. Do not add a second maths library on the strength of its benchmark page |
| **r8brain-free-src** | `7.6` | MIT | **Trigger: a measured conversion artefact.** The oversampled stages use `juce::dsp::Oversampling`, `IrStage` resamples impulses through JUCE, and `NoiseLoader` converts a print in `prepare`. A high-quality SRC would earn its place only if one of those is shown to be the cause of an audible problem |
| **chowdsp_plugin_state / presets_v2** | `v2.4.0` | BSD | **Trigger: the state schema needs a migration the hand-written code cannot express.** Today `tests/state` and `check_presets.py` pin the format and the 29 factory presets; replacing that with a framework trades tested code for a dependency. `chowdsp_wdf` (BSD-3) is the other side of the same coin: it is the right tool only if a circuit-level model replaces a curve, which is an audio decision, not a library one |
| **moodycamel::ConcurrentQueue** | no release tags; pin a commit | BSD-2 (simplified) | **Trigger: telemetry gains a second producer.** `readerwriterqueue` is SPSC by design and is the correct shape for one audio thread feeding one editor |
| **magic_enum** | `0.9.8` | MIT | **Trigger: none foreseen.** Enum-to-string is already product copy in translation tables, checked by `check_translations.py` and `check_parameters.py`; reflection would compile slowly to produce words this project wants to author by hand |
| **simdjson** | `5.0.5` | Apache-2.0 OR MIT | **Trigger: model load time becomes a complaint.** Parsing happens once, off the audio thread, in a loader that already hands the stage an immutable model; `nlohmann/json` is shared with RTNeural and NAM Core, and a second parser would mean a second JSON type at that ABI boundary |

### 3c. Reject, and why

| Candidate | Version | Licence | Why not |
| --- | --- | --- | --- |
| **ONNX Runtime** | `1.31.0` | MIT | A second inference engine for a model format this product does not ship. Upstream's mobile documentation for ORT 1.18 puts an uncompressed arm64 `libonnxruntime.so` at 16.3 MB before any execution provider; NAM Core is the reference implementation of the format the plugin actually loads, and it is already vendored |
| **XNNPACK** | no releases; pin a commit | BSD-3 | Built for convolutional networks over images, and it fetches `cpuinfo` and `pthreadpool` to do it. The neural stage runs a 1-in/1-out model per channel with tiny tensors; the gate would be `tests/neural`, and nothing there suggests the GEMM is the bottleneck |
| **ARM Compute Library** | `v53.3.1` | MIT | ARM-only, no Windows support, and several to tens of MB for a broad build. The Android build would be the only beneficiary, and it already runs the same Eigen code |
| **Google Highway** | `1.4.0` | Apache-2.0 OR BSD-3 | A second portable-SIMD layer beside xsimd and `chowdsp_simd`. The repository's rule is one layer, not two: `j37Tanh` goes through the xsimd path that `chowdsp_simd` already wraps |
| **Rubber Band** | `4.0.0` | GPL-2.0+ or commercial | `signalsmith-stretch` (MIT) already carries the vocoder engine, and it is off by default because the measurements did not earn it. Taking on GPL for a second stretcher would undo WP4's licence cleanup for no new capability |
| **soxr** | `0.1.3` | LGPL-2.1+ | LGPL in a statically linked plugin is the obligation this repository has twice decided not to take (SoundTouch, libsndfile), and upstream has not cut a release since 2018. `juce::dsp::Oversampling` covers the need |
| **libsndfile** | `1.2.x` | LGPL-2.1 | Already fetched and deliberately unlinked: `dr_wav` decodes the WAV print, and the plugin never opens an arbitrary audio file |
| **FFTW** | `3.3.x` | GPL-2.0+ or commercial | GPL for a three-bin analyser that `pocketfft` already serves |
| **FFTS** | unversioned snapshot | BSD-3 | Unmaintained - the author points users at forks. `pocketfft` is pinned and maintained |
| **Intel IPP** | - | Proprietary | A per-platform proprietary runtime for maths that is not the bottleneck; it would also break the one-link-line-for-five-platforms property |
| **spdlog / fmt** | latest | MIT | `juce::Logger` exists, and `std::format` is in the C++20 toolchains the build already requires. Logging is not a hot path and does not justify a dependency |
| **Sentry / Crashpad** | - | MIT / Apache-2.0 | A plugin does not own its process: a crash handler inside a host can interfere with the host's own, and the privacy statement would have to grow a network endpoint. A DAW's crash report is the right report |
| **Google Benchmark / doctest / GoogleTest** | - | Apache-2.0 / MIT / BSD-3 | `Catch2` and `nanobench` are already in the build and in CI; a second framework is pure churn |
| **Boost / Abseil / range-v3** | - | BSL-1.0 / Apache-2.0 / BSL-1.0 | The standard library at C++20 covers what is used, and these are compile-time costs in a build whose slowest job is already the compiler |
| **Dear ImGui** | - | MIT | `melatonin_inspector` already covers the debug UI, and the panel is deliberately hand-painted: its look is the product |
| **mimalloc / jemalloc / tcmalloc** | latest | MIT / BSD-2 / Apache-2.0 | Replacing the allocator inside a host process is a footgun, and rule 5 already forbids allocating on the audio thread - fixing the call sites is cheaper than swapping the allocator under the host |
| **chowdsp_dsp_utils** | `v2.4.0` | GPLv3 | Removed in WP1 and not coming back: the only GPLv3 entry that ever sat on the link line, with no caller |

### 3d. Already in the tree, and free

Five options that need no new dependency. Two of them (`Oversampling` and `Convolution`)
are part of why several candidates above are rejections rather than backlog.

| Option | Where it is | What it would cost | Verdict |
| --- | --- | --- | --- |
| **LV2 format** | `JUCE/modules/juce_audio_plugin_client/LV2` is already in the submodule; `J37_PLUGIN_FORMATS` is `AU AUv3 VST3` | One word in the format list plus a bundle to test | **Consider for Linux.** VST3 covers most Linux hosts; LV2 is the Ardour/Mixbus path. Gate: a bundle that loads in Ardour and passes the same state round-trip |
| **`juce_animation`** | in `JUCE/modules`, not linked | One module on the link line, no new dependency | **Not now.** The editor already interpolates against its own clock in `EditorTimer.cpp`; `AnimatedAppComponent` would be a second timing model for the same ticks. Revisit only if hand-driven animation becomes a maintenance problem |
| **`juce::dsp::Oversampling`** | `juce_dsp`, already linked | Nothing | **In use.** The oversampled stages run through it, which is why r8brain and soxr are not needed |
| **`juce::dsp::Convolution`** | `juce_dsp`, already linked | Nothing | **In use.** `IrStage` holds two, one per channel, and relies on JUCE's wait-free `loadImpulseResponse`; this is why `fftconvolver` is not needed |
| **CLAP** | not in this JUCE checkout | See §3b: a wrapper, a format target and mapping tests | **The one real gap.** Every other format here is either built or a CMake word away |

## 4. What the audit turned up

1. **`foleys_gui_magic` was linked but unused - now it is used.** It sat on the link line
   with a single `#include` in `Source/PluginEditor.h`, and nothing named a `foleys::`
   symbol: `check_linkline.py` counts an include as a caller, which is the right rule for
   proving the link line honest, and exactly why a seam can sit there indefinitely. The
   answer was to build the surface the module exists for rather than drop it:
   `Source/GUI/ParameterSheet.h` now owns a `MagicProcessorState` and a
   `MagicGUIBuilder`, generates the parameter tree from the processor's own groups, and
   binds every row with a real attachment. The SETTINGS page's PARAMETERS key opens it.
   The module still carries `juce_audio_devices` and `juce_cryptography` as declared
   dependencies; that cost now buys a feature rather than a seam.

2. **`J37_USE_SIMD_TANH` is ON, and the docs now say so.** The option was documented as
   opt-in while the `option()` had been flipped to `ON` in `d8116be`, a CMake-minimum
   chore. That was resolved the other way from "restore OFF": ON is the intent, because
   1.19e-7 is below audibility for a saturation stage and the kernel is ~1.5x cheaper, and
   the three-way `simd_tanh_test` is what makes the branch a measured claim rather than a
   comment. The option's long comment, README, `docs/architecture.md`,
   `library-integration-plan.md` and the two `tests/dsp` scripts now all say ON, and
   `-DJ37_USE_SIMD_TANH=OFF` is documented as the way back to `std::tanh` bit for bit.

3. **README's licence row still says the binary links "a GPLv3 module".** It did until WP1;
   the only GPLv3 module, `chowdsp_dsp_utils`, came off the link line and `LICENSE` says so.
   The row is corrected here.

## 5. Order of work

| # | Item | Why first | Effort | Gate |
| --- | --- | --- | --- | --- |
| 1 | RealtimeSanitizer job | Turns rule 5 from a review convention into a check, on the thread that matters | Low | A new CI job; `processBlock` annotated; a known allocation injected and caught once as proof |
| 2 | pluginval in CI | Finds host-facing bugs the unit harness cannot reach | Low | A level-5 run per format on Linux and macOS |
| 3 | `clang-tidy` on changed files | Catches the bug classes warnings are told to ignore | Medium | A clean baseline on `Source/DSP`, then widen |
| 4 | libFuzzer on `NoiseDecoder` and `NamBackend` | The parsers take untrusted files; the refusals already exist, this proves them | Medium | Two targets, a corpus, and `-max_total_time` in a nightly job |
| 5 | libebur128 oracle | An independent meter is the only cross-check the loudness code will ever have | Low | LUFS agreement within a stated tolerance on the existing fixtures |
| 6 | sccache | Saves minutes on every CI run, on every platform | Low | Cache hit rate reported, then the macOS job's time compared |
| 7 | Tracy, once a performance question exists | Profiling without a question is a hobby; the shaper's numbers came from `nanobench` and that was the right tool | Medium | A capture of `processBlock` and of one editor frame before/after a change |
| 8 | CLAP, when a user asks | A real format gap, but a wrapper plus mapping tests | High | A CLAP build that passes `pluginval` and loads in Bitwig and Reaper |

Steps 1, 2, 4, 5 and 6 add **no runtime dependency at all**. That is the finding this file
started from: the plugin's dependency graph is not under-served by libraries it lacks; it
is under-served by evidence, and the cheapest evidence comes from tools that never ship.
