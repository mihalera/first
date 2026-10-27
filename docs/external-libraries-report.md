# External libraries: integration audit and deeper-integration assessment

This report answers three questions for every external component the build knows
about:

1. **What is it, and how is it integrated today?**
2. **Would deeper integration help** - vendoring it, linking it, or coupling the
   engine to it more closely?
3. **Why is the current arrangement the right one** - or, where it is not, what
   would have to change first?

It is an audit of the tree as it stands on 2026-10-26, not a wish list. The
short answer is in §1; the evidence is in §4-§9.

## 1. Verdict, up front

**The current integration split is correct.** Of the 44 CPM declarations in
`cmake/J37Dependencies.cmake`, 12 vendored trees, one JUCE submodule and three
discovered maths SDKs, every component that has a caller is already integrated,
and the ones that do not have
a caller are either alternatives to something already in the binary or
references to another framework/host - not capabilities the plugin lacks.

There is exactly **one place where deeper integration is a real, beneficial
piece of work**: the staged Sokol and bgfx renderers. Both libraries are already
integrated as far as a library can be - target, surface ownership, a compiled
implementation - and what remains is porting the tape scene to their shaders,
which is feature work rather than dependency work. It should happen only when
that renderer is the goal, not because the library is present.

For everything else, deeper integration would be one of four things:

- **redundant** - a second FFT, pitch detector, decoder, UI toolkit or
  convolution engine for a job the binary already does;
- **architecturally hostile** - a second window owner (ImGui, `sokol_app`,
  iPlug2), a second JSON type across an ABI boundary, or a second inference
  engine;
- **a licence regression** - LGPL, GPL or an unlicensed dependency that WP1 and
  WP4 removed on purpose;
- **dead weight** - a link line entry no file under `Source/` includes, which is
  exactly the defect `tests/linkline/check_linkline.py` now prevents.

The rest of this file records that judgement library by library.

## 2. Scope, sources and terms

**In scope:** every third-party repository, vendored tree, platform library and
test-only dependency the CMake build resolves. The standard library, the
compiler runtime and operating-system frameworks the build never names are out
of scope.

**Sources of truth**, in order:

| Question | File |
| --- | --- |
| What is fetched, at which pin, and how? | [`cmake/J37Dependencies.cmake`](../cmake/J37Dependencies.cmake) |
| What is linked into the plugin? | [`CMakeLists.txt`](../CMakeLists.txt), `target_link_libraries(first ...)` |
| What is checked in instead of fetched? | `ThirdParty/`, `.gitmodules` |
| What is discovered on the machine? | [`cmake/J37VendorLibraries.cmake`](../cmake/J37VendorLibraries.cmake) |
| What is test-only? | [`cmake/J37Tests.cmake`](../cmake/J37Tests.cmake), `tests/` |
| What licence obligations follow? | [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md) |

CMake's `NAME` is not always the published name - `signalsmith-dsp` is the
signalsmith-stretch repository, `nlohmann_json` is nlohmann/json,
`audio_fft_module` is audio_fft, and `dattorro_verb` is dattorro-verb. This
report uses the published names and the target names where they differ.

**Terms used below:**

- **vendored** - the source is checked into `ThirdParty/`; CPM is only a
  fallback when the tree is pruned.
- **CPM-configured** - CPM downloads it and runs its own `CMakeLists.txt` as a
  subproject.
- **CPM download-only** - CPM fetches the sources, and this repository declares
  the target (or exposes headers) itself.
- **source-only** - fetched to read or lift from, never configured, compiled or
  linked.
- **shipped** - reaches a default release build. An `option()` that defaults ON
  counts as shipped; one that defaults OFF is stated explicitly.
- **deeper integration** - anything beyond the current boundary: vendoring a
  fetched package, linking a fetched-but-unlinked one, replacing a stage with
  the library, or exposing its types through more of the engine.

## 3. The integration patterns in use

Seven patterns, each with a job. The pattern is chosen per library; there is no
house rule that "vendored is better" or "CPM is better".

| Pattern | Used for | What it buys | What it costs |
| --- | --- | --- | --- |
| **Git submodule** | JUCE | One framework, pinned at the tag every clone can fetch; `add_subdirectory` is the supported integration | A submodule update is a repository change; a local patch needs a version-keyed copy |
| **Vendored source + CPM fallback** | RTNeural, NAM Core, Eigen, dattorro-verb, `dr_wav`, glm, nlohmann/json, pocketfft, signalsmith-stretch (+ `signalsmith-linear`), xsimd, farbot | Header-only or ABI-sensitive code is on disk; harnesses can compile the "library absent" branch without a configure; offline builds of the engine's own dependencies | Re-pinning is two edits (vendored tree and CPM fallback); the tree carries the source |
| **Vendored test-only** | libebur128 | An independent oracle that a fresh checkout can run with no network | 72 KB and one test binary |
| **CPM-configured subproject** | chowdsp_utils, melatonin_inspector, melatonin_blur, foleys_gui_magic, clap-juce-extensions, `readerwriterqueue`, kissfft, pffft, yasio, libsndfile, bgfx, Dawn | Upstream's own target and JUCE-module registration are used unchanged; pins are visible in one file | Configure-time network and a cache; upstream's CMake becomes part of the configure |
| **CPM download-only + local target** | NAM Core's sources, dattorro-verb's `verb.c`, FFTConvolver, ImGui, YIN, audio_fft/pitch_detector, q_dsp/cycfi_infra, sokol_gfx, Rack headers, dywapitchtrack, `dr_wav`, farbot | The build decides flags, warnings and what is compiled; a package whose own CMake is unusable (too-old minimum, standalone project, no CMake at all) still arrives pinned | This repository owns the target definition and must keep it correct |
| **Discovered platform SDK** | Intel IPP, Arm Performance Libraries, Apple vDSP, Vulkan, Metal, D3D11/D3D12, OpenGL | No vendoring is possible (proprietary, OS-owned, or architecture-specific); the build uses what the machine has and stays portable without it | Builds differ by machine; CI has to install or require the SDK to test the path |
| **Dev-only** | Catch2, nanobench, libebur128 | Test evidence without adding anything to a distributed binary | A second configure shape, off by default |
| **Build-time tool** | CPM.cmake | Pins and caching for everything above | One downloaded script, not a linked library |

## 4. Inventory: what the plugin links

The default configuration is `J37_NAMCORE=ON`, `J37_USE_SIMD_TANH=ON`,
`J37_CORRECTOR_USE_STRETCH=OFF`, `J37_RENDER_BACKEND=AUTO`. The plugin's
direct list is 8 JUCE modules, 14 third-party targets and `PluginResources`
(plus `namcore` when NAM Core is on); `check_linkline.py` follows their
dependencies to a closure of 33 targets and verifies each linked target has a
caller in `Source/`. Rows marked *machine SDK* are linked only when the build
machine has them; §6 covers those paths.

| Library | Pin | Licence | Integration | Role / caller | Deeper integration? |
| --- | --- | --- | --- | --- | --- |
| **JUCE** | `9.0.4` submodule | AGPLv3 or commercial | `add_subdirectory(JUCE)`, 8 modules linked, one version-keyed opus-header patch | The framework: processor, editor, formats, DSP, `box2d` | **No.** Keep the submodule and the narrow patch; vendoring or forking the framework would be a maintenance and licence liability |
| **chowdsp_math + chowdsp_simd** | `v2.4.0` | BSD-3 | CPM subproject, JUCE modules, `EXCLUDE_FROM_ALL`/`SYSTEM` | `J37Math.h` (`PowApprox`, `LogApprox`), metering and auto-gain (`DecibelsApprox`) | **No.** The per-module split is what keeps GPLv3 `chowdsp_dsp_utils` off the link line; taking the umbrella would undo WP1 |
| **xsimd** | `14.3.0`, vendored | BSD-3 | Vendored header-only; CPM fallback | `J37Tanh` in `HotPathMath.h`, default via `J37_USE_SIMD_TANH` | **No.** One SIMD layer, one wrapper call site; a second SIMD abstraction (Highway) is a rejected duplicate |
| **RTNeural** | `95c3c0f9`, vendored | BSD-3 | Vendored header-only; CPM fallback, `RTNEURAL_STL` | `NeuralStage.h`, the LSTM `.nam` path | **No.** Vendored is right for an ABI-adjacent model format; keep it behind the stage seam |
| **NeuralAmpModelerCore (NAM Core)** | `0b3d3c97` (`v0.6.0`), vendored | MIT | Vendored sources, locally declared `OBJECT` library (PIC, warnings silenced on GCC/Clang), CPM fallback | `NamBackend.h`, WaveNet/ConvNet/Linear `.nam`; ON by default | **No.** The `OBJECT` form is required for NAM's static parser registry; using upstream's standalone CMake or a static archive would break or prune it |
| **Eigen** | `3.5.0` (`bc3b3987`), vendored | MPL-2.0 | Vendored header-only `INTERFACE` target, reached through `namcore` | NAM Core's linear algebra only | **No.** Direct Eigen use would expand the MPL-covered surface for no capability; keep it an implementation detail of NAM |
| **nlohmann/json** | `v3.12.0`, vendored | MIT | Vendored single header; one copy forced ahead of RTNeural's bundled 3.11.1 | `.nam` model parsing (`NamBackend`, `NeuralStage`, `OptionalLibraries`) | **No.** One JSON type across the NAM ABI is load-bearing; a second parser (simdjson) would break it |
| **pocketfft** | `c90e55b3`, vendored | BSD-3 | Vendored header-only; CPM fallback | `SpectrumAnalyser.h`, the analyser transform | **No.** One FFT caller; the other four FFTs are rejected on that basis |
| **dattorro-verb** | `41e976a0`, vendored | MIT | Vendored C source, locally declared static library; CPM fallback | `DattorroRoom.h`, the plate reverb | **No.** 341 lines of C with an opaque API; a deeper wrapper would add nothing |
| **dr_libs (`dr_wav`)** | `wav-0.14.5`, vendored | Unlicense / MIT-0 | Vendored header; exactly one implementation TU (`NoiseDecoder.cpp`) | `NoiseDecoder`, the vinyl noise print | **No.** One decoder, one format; libsndfile is LGPL and unnecessary |
| **glm** | `1.0.3`, vendored | MIT / Happy Bunny | Vendored header-only; CPM fallback | `TapeScene.*`, `TapeSceneFrame.cpp` maths | **No.** Header-only maths for one scene; nothing to couple |
| **signalsmith-stretch (+ `signalsmith-linear`)** | `1.4.0` / `0.6.6`, vendored | MIT | Vendored header-only, `INTERFACE` target; CPM fallback; opt-in engine | `CorrectorStage.h` engine 2, OFF by default | **No, until measurements earn it.** The stage's own harness gives it 1216 frames of latency at 48 kHz and up to tens of cents of error against the shifter; the default stays the shifter |
| **readerwriterqueue** | `v1.0.7` | BSD-2 | CPM `INTERFACE` target; probe with atomics fallback | Telemetry, SPSC audio -> editor | **No.** One producer, one consumer, fixed capacity; a concurrent queue would be the wrong shape |
| **melatonin_blur** | `v1.4` | MIT | CPM JUCE module, `SOURCE_DIR` pinned for module-name resolution | Panel shadow, knob halo, cap shadow | **No.** Three cached shadows; vendoring a JUCE module would add update work for no gain |
| **melatonin_inspector** | `9c483f86` | MIT | CPM JUCE module, debug-only use | `createDebugInspector()` under `JUCE_DEBUG` | **No.** Debug-only; there is no shipped coupling to deepen |
| **foleys_gui_magic** | `d53d998e` | BSD-3 | CPM from `modules/` (not its root, which would fetch a second JUCE); pinned commit | `ParameterSheet.h`, the SETTINGS -> PARAMETERS sheet | **No.** It is already at the depth its design implies; using it for the whole UI would fight the hand-painted panel that is the product |
| **clap-juce-extensions (+ CLAP + clap-helpers)** | `7adee3a1` | MIT | CPM subproject with its own submodules; adds `first_CLAP` module over the same shared code | The CLAP wrapper JUCE does not ship | **No.** The wrapper has to track JUCE and CLAP ABI changes; vendoring would freeze it mid-API |
| **Intel IPP** | machine SDK | Proprietary | Discovered by `find_package(IPP)`; optional; only JUCE's FFT reads it | JUCE `dsp::FFT` acceleration, not this repository's analyser | **No.** Proprietary and per-architecture; discovery is the only licence-clean option. `J37_ENABLE_IPP=OFF` opts out |
| **Arm Performance Libraries** | machine SDK | Proprietary | Discovered by header+library; `J37_HAS_ARMPL`; required in ARM CI | `SpectrumAnalyser.h` through the FFTW3-compatible interface | **No.** Same reasoning as IPP, plus it is the one vendor library with a direct caller; vendoring is not permitted and would not help non-ARM builds |
| **Apple vDSP / Accelerate** | OS framework | OS SDK | Linked by JUCE's `juce_dsp`; `J37_HAVE_VDSP` is a configure-time check only | JUCE's FFT path on Apple | **No.** It is an OS framework behind JUCE; the plugin has no direct vDSP code to deepen |

**Two notes on the table.** `signalsmith-stretch` is header-only, so "on the
link line" means an include path, not object code; with the option OFF the
binary carries none of it. `chowdsp_simd` is not included directly - it is a
declared and included dependency of `chowdsp_math`, which is why both targets
appear even though `Source/` names only `chowdsp_math`.

## 5. Inventory: fetched but not in the default binary

These are present for one of three reasons: an alternative that a benchmark
might one day choose, a source to read or lift from, or a backend that a
`J37_RENDER_BACKEND` value selects. None is a missing capability.

### 5a. Built or registered, but unlinked

| Library | Licence | Integration | Why it is not linked | Deeper integration? |
| --- | --- | --- | --- | --- |
| **FFTConvolver** | MIT | CPM download-only; four TUs compiled into a static target | `juce::dsp::Convolution` is the IR engine (`IrStage`), and its wait-free load is the reason | **No.** A second convolution engine for the same IR stage; adopt only on a benchmark that names it |
| **kissfft** | BSD-3 | CPM-configured static target | `pocketfft` is the one analyser caller; JUCE has its own FFT too | **No.** A fourth FFT for one existing caller |
| **pffft** | BSD-like | CPM-configured static target | Same | **No.** Same |
| **AudioFFT** | MIT | CPM download-only, pinned into `audio_fft`'s submodule slot | Only the MPM wrapper uses it, and the wrapper is not linked | **No.** Reached only through a rejected pitch detector |
| **audio_fft** | MIT | CPM download-only, registered with `juce_add_module` | Part of `pitch_detector`'s graph | **No.** A JUCE module dependency of a rejected detector |
| **pitch_detector** | see upstream | CPM download-only, registered with `juce_add_module` | The plugin's tracker is its own YIN, measured by `tests/pitch` | **No.** A replacement must beat the printed error, not the include graph |
| **yin_pitch** | see upstream | CPM download-only; `Yin.c` compiled into a static target | Same: the shipping tracker is already YIN | **No.** Same |
| **dywapitchtrack** | MIT | CPM download-only; header-only target | Alternative pitch tracker, no caller | **No.** Same |
| **libsndfile** | **LGPL-2.1** | CPM-configured static target, codecs/programs/tests off | `dr_wav` decodes the one format the feature needs; LGPL is the obligation WP4 removed | **No, and actively harmful.** Linking it would reintroduce the LGPL decision for a decoder the plugin does not need |
| **q_dsp** | MIT | CPM download-only; header-only target | No caller; the transient shaper's envelopes are hand-written and pinned by `tests/unit` | **No.** A second DSP toolkit for maths already solved and tested |
| **cycfi_infra** | **none upstream** | CPM download-only; four include paths exposed | Required only if Q is used; no caller | **No, and blocked.** No licence file means there is no grant of rights to rely on; it must not ship until that is settled |
| **Rack (`rack_dsp`)** | GPL-3 with plugin exception | CPM download-only; headers exposed | The Rack engine is a different host runtime; only its DSP headers could be borrowed, and none are called | **No.** Linking the engine is architecturally impossible inside a plugin; the licence is another reason not to |
| **yasio** | MIT + Anti-996 | CPM-configured static target, TLS backend 0 | A socket library in an audio plugin has no caller | **No.** Networking is not a plugin capability; keeping TLS off also keeps a configure-time OpenSSL download out |
| **ImGui** | MIT | CPM download-only; four core TUs compiled into a static target | A second immediate-mode UI would fight JUCE's component/window ownership | **No, detrimental.** The panel is deliberately hand-painted; `melatonin_inspector` already covers debug UI |
| **farbot** | MIT | Vendored header-only; CPM fallback | No caller; `IrStage`/`NeuralStage` use JUCE and atomics for the same job | **No.** Small and MIT, so it stays one line away; linking it without a caller is exactly the defect the audit removed |

### 5b. Source-only references

| Library | Licence | Why it is only source | Deeper integration? |
| --- | --- | --- | --- |
| **iPlug2** | custom permissive | A complete plugin framework with its own window and wrapper layers; two frameworks cannot own one plugin | **No, detrimental.** It is a reference for wrapper and UI ideas, not a dependency |
| **ChowCentaur** | BSD-3 | A finished JUCE plugin project, not a library; its stages would have to be copied file by file | **No.** Lifting a stage is a code decision, not a dependency |
| **VCV-Prototype** | GPLv3 | A module for a different host (Rack), with a Rack-plugin Makefile; GPL | **No, licence and architecture both refuse it** |
| **Soundpipe** | MIT | Upstream builds by generating `soundpipe.h`/`config.h` from its Makefile; the bare checkout does not compile | **No.** Generated-file toolchain for DSP the engine already has |
| **aubio** | **GPL-3.0** | Needs generated `config.h` from waf/autoconf; GPL | **No, licence.** The shipping tracker is the measured YIN |
| **TransDes** | MIT | A finished JUCE plugin; its detector informed `TransientShaper.h` | **No.** Reading the algorithm was the integration |
| **Whetstone** | BSD-3 | FAUST source; consuming it means adding the Faust compiler and checking in generated C++ | **No.** A toolchain decision for one band split, not a library |
| **multiband-transient-shaper** | MIT | MATLAB/Python research, last commit 2020; no C++ to compile | **No.** The Bark filterbank design is the useful part, and it was read |

### 5c. Optional graphics backends

| Library | Licence | Integration | State | Deeper integration? |
| --- | --- | --- | --- | --- |
| **sokol_gfx** | zlib | CPM download-only; header-only target; implementation compiled once in `SokolGfxImpl.cpp` | `J37_RENDER_BACKEND=SOKOL` owns a JUCE GL context, clears and presents; the tape scene is not ported | **Conditional yes - and this is the one real item.** Port the scene into the backend; do not move sokol types into the engine |
| **bgfx** | CC0/BSD-2 | CPM-configured only when `J37_RENDER_BACKEND=BGFX`; `bgfx.cmake`, tools and examples off | Creates a child surface in the host peer, initialises bgfx, presents a cleared frame; shader tooling is off | **Conditional yes.** Same boundary; the open work is shader binaries and the scene port, not the link line |
| **wgpu-native** | Apache-2.0 | CPM download-only plus a Cargo `ExternalProject` | `j37_setup_graphics_backend` plumbing; `WGPU` is not in `J37_RENDER_BACKEND`'s `STRINGS` list | **No.** Needs Cargo and a native single-architecture build; the cost is out of proportion to a staged renderer |
| **Dawn** | BSD-3 | CPM-configured, samples/tests/GLFW off | Plumbing; `DAWN` is not in `J37_RENDER_BACKEND`'s `STRINGS` list | **No.** A large dependency tree for a backend nothing selects |

## 6. Platform SDKs and OS frameworks

| Component | How it is integrated | Why no vendoring | Deeper integration? |
| --- | --- | --- | --- |
| **Intel IPP** | `find_package(IPP CONFIG QUIET)` plus a wheel-layout fallback; `J37_ENABLE_IPP`; `J37_IPP_LINKING_MODEL` makes JUCE's FFT engine Intel's | Proprietary licence, per-architecture binaries, tens of MB; a source build is not something this repository can redistribute | **No.** Discovery is the maximum this project should own. The analyser deliberately stays on pocketfft, so IPP only accelerates JUCE's FFT |
| **Arm Performance Libraries** | Header + library discovery (`ARMPL_DIR`/standard install roots); `J37_ARMPL_REQUIRED` in CI; `J37_HAS_ARMPL` defines the one direct caller | Same: proprietary binary distribution, AArch64-specific | **No.** The ARM CI jobs make it testable; vendoring would be a licence violation and useless on x86 |
| **Apple vDSP / Accelerate** | `find_library` check; the actual path is JUCE's `juce_dsp` declaring `OSXFrameworks Accelerate` | OS framework; already on every Apple SDK | **No.** There is no plugin-side vDSP code to deepen |
| **Vulkan** | `find_package(Vulkan QUIET)`; linked only if found and selected | OS/SDK-owned | **No.** Platform SDK; the renderer abstraction is the right boundary |
| **Metal / QuartzCore** | Framework link in the Metal branch | OS-owned | **No.** Same |
| **D3D11 / D3D12 / DXGI / DXGUID** | `target_link_libraries` in the Windows branches | OS-owned | **No.** Same |
| **OpenGL** | Through `juce::juce_opengl` and JUCE's GL context | OS/driver-owned | **No.** Already one layer down |

JUCE's own tree also bundles third-party code - Oboe for Android audio, the
FLAC/Ogg/Opus codecs, `box2d` - inside the submodule. Those are covered by
JUCE's pin and licence rather than by separate rows here; they are not fetched
or vendored by this repository and have no independent upgrade path.

## 7. Development, test and build-time components

| Component | Pin | Licence | Integration | Deeper integration? |
| --- | --- | --- | --- | --- |
| **Catch2** | `v3.16.1` in CMake | BSL-1.0 | CPM only when `J37_BUILD_TESTS=ON`; linked into test binaries, never the plugin | **No.** A second test framework would be pure churn |
| **nanobench** | `v4.6.0` | MIT | Same switch; header + one implementation TU in `tests/dsp/bench.cpp` | **No.** Dev-only measurement |
| **libebur128** | `v1.2.6`, vendored | MIT | Vendored source compiled into `loudness_oracle_test` only | **No.** It is the independent oracle for `LoudnessMeter.h`; shipping it would create a second loudness implementation and a runtime dependency |
| **CPM.cmake** | `0.43.2` | MIT | Downloaded into `build/cmake/CPM.cmake` at configure time | **No.** Build tooling, not a library in the binary |

**Tools are not runtime libraries.** `pluginval` (GPLv3, run as a separate
executable), libFuzzer (Clang runtime), clang-tidy, sccache, RTSan and Tracy
belong to the evidence layer described in
[`library-candidates.md`](library-candidates.md#3a-adopt-dev-and-ci-only).
None is on `target_link_libraries`, so none changes the licence of a
distributed binary. They are mentioned here only so the inventory is not
mistaken for a list of everything in CI.

## 8. Removed and deferred components

| Component | State | Why it stays that way |
| --- | --- | --- |
| **SoundTouch** | **Removed** (WP4). No target, no fetch, no `ThirdParty/soundtouch` | It was WSOLA - the same family as the granular shifter already shipping - and it carried LGPL-2.1 while no stage called it. Re-adding it would restore a copyleft obligation for a third stretcher the harness has no reason to prefer. If a future stage wants a stretcher, it starts by beating the printed numbers in `tests/pitch`, not by restoring the dependency |
| **Assimp** | **Not fetched** | The planned OBJ importer is gated on a licence-clean mesh existing. A loader before an asset is the pattern that left eighteen dead entries on the link line; WP5's steps are the plan, not a new dependency |
| **`juce_opengl_3d`** | **Does not exist** | There is no such JUCE module; 3D support is shader-level through `juce::OpenGLShaderProgram`, which is already linked. No pin is possible |

## 9. Per-library assessment: the reasoning in one place

This section is the "why" behind the verdicts in §4-§7, grouped by the
decision each library needs. It is deliberately repetitive with the tables: a
table says *what*, this says *why*.

### 9a. Already at the right depth - keep exactly as is

- **JUCE** is the framework, not a dependency beside the framework. A
  submodule at `9.0.4`, `add_subdirectory`, and one patched opus header keyed
  to that version is the minimum integration that works. Vendoring JUCE into
  the repository would add 109 MB, a second source of updates, and an AGPL /
  commercial licensing surface that the submodule already states. Forking it
  to carry the opus fix would be worse: the fix is two typedefs, and the
  content-hash guard means the patch applies once and can never silently
  overwrite a newer tree.
- **chowdsp_math** and **chowdsp_simd** are used as JUCE modules through CPM.
  This is the correct granularity: `chowdsp_dsp_utils` is GPLv3 and is not
  linked, and the package's own CMake lets the build select modules. Vendoring
  the package would not change the code; it would only move the update and the
  licence bookkeeping into this repository. Deeper engine coupling is
  unnecessary: three `DecibelsApprox` calls and the `J37Math` wrappers are the
  whole surface.
- **xsimd** is vendored and reached through one function, `j37Tanh`. That is
  the right shape for a SIMD primitive: one call site, one measured curve
  difference, one flag to turn it off. Using xsimd directly in more DSP would
  spread a vector-width assumption the engine does not need and duplicate
  `chowdsp_simd`, which already wraps it.
- **RTNeural** is vendored because the `.nam` loader and the plugin's own
  model path share a header-only API and an ABI-adjacent JSON type. It sits
  behind `NeuralStage`; the processor does not name RTNeural types. That seam
  is the integration. Moving RTNeural types into `PluginProcessor` would make a
  future engine swap a rewrite.
- **NAM Core** is the one library whose integration *must* be exactly this
  shape. It is compiled as an `OBJECT` library, not a static archive, because
  its architecture parsers register themselves through file-scope statics and
  a static archive prunes the objects that contain them. It also needs `-fPIC`
  on Linux because a shared plugin cannot take the non-PIC `thread_local`
  relocation. Both details are in the dependency file; neither is optional.
  Deeper integration would mean adopting NAM's standalone CMake, which owns
  compiler flags and tools a plugin should not inherit.
- **Eigen** is an implementation detail of NAM Core. Using it directly would
  put MPL-2.0 files on more of the engine and duplicate maths that is not the
  bottleneck. The current indirect inclusion is the strongest possible argument
  that the plugin does not depend on Eigen.
- **nlohmann/json** is vendored and pinned so that one copy serves both NAM
  Core and RTNeural. RTNeural bundles 3.11.1 and includes it by relative path;
  if its copy were read first, the build would have two `nlohmann::json` types
  with different mangled names on either side of NAM's ABI. The include order
  in `OptionalLibraries.h` is load-bearing. A second parser or a "faster JSON"
  swap would reopen exactly that failure for a loader that runs off the audio
  thread once.
- **pocketfft** is one header for one analyser. There is no deeper integration
  to make: the transform is a pure function, and the analyser already falls
  back cleanly when the header is absent. The four alternative FFTs are
  rejected because they would each serve the same caller.
- **dattorro-verb** is 341 lines of C with an opaque struct API. The C++
  wrapper `DattorroRoom.h` is the natural boundary; compiling `verb.c` as C
  preserves the library rather than re-spelling it. Nothing about the reverb
  would improve by exposing its state.
- **dr_libs (`dr_wav`)** is a single header whose implementation is compiled
  exactly once, in `NoiseDecoder.cpp`. That is the pattern the library
  documents. Adding libsndfile would add LGPL and a second decoder stack for a
  format the feature already reads.
- **glm** is header-only maths used by `TapeScene`. It needs no target beyond
  an include path. A deeper role (for example using GLM throughout the DSP)
  would mix graphics semantics into audio code for no benefit.
- **readerwriterqueue** is a single-producer/single-consumer queue used for
  telemetry. The one producer is the audio thread and the one consumer is the
  editor; that is exactly its contract. A multi-producer queue would be the
  wrong tool, and a lock would violate rule 5.
- **melatonin_blur** and **melatonin_inspector** are JUCE modules. The blur is
  used in three cached shadows; the inspector exists only under `JUCE_DEBUG`.
  Vendoring a JUCE module would break the module-name resolution the project
  already has to work around with `SOURCE_DIR`, and there is no architectural
  depth to add.
- **foleys_gui_magic** is used for the one surface it is designed for: the
  generated parameter sheet. It is already coupled at the ProcessorState /
  MagicGUIBuilder level, which is as deep as it should go. Using it to host the
  whole editor would replace a deliberate, hand-painted design with a generic
  builder and add a second UI paradigm.
- **clap-juce-extensions** is a wrapper, not a format. It has to move with
  JUCE's wrapper API and CLAP's ABI. Vendoring it would freeze it at a
  revision and make every JUCE bump a manual merge; CPM at a commit is the
  right cost. The `first_CLAP` target links the same shared code as VST3/AU,
  so there is no second processor to drift.
- **Intel IPP** and **ArmPL** are machine SDKs. The only sane integration is
  discovery, an opt-out, and a CI job that installs them when the path must be
  tested. Vendoring is legally impossible for IPP and ArmPL; bundling a
  proprietary binary would also break the one-link-line-for-five-platforms
  property. ArmPL is the exception that proves the rule: it has a direct
  caller, so the build defines `J37_HAS_ARMPL` from the link result rather
  than from `__has_include`, which could accept an FFTW that is not linked.
- **vDSP/Accelerate** is already integrated by JUCE. The repository checks
  that the framework exists so an Apple build without it is loud; the plugin
  itself has no vDSP code to deepen.
- **Catch2**, **nanobench** and **libebur128** improve evidence, not audio.
  Catch2 and nanobench are behind `J37_BUILD_TESTS`; libebur128 is compiled
  only into the loudness oracle. Promoting the oracle into the shipped binary
  would make the plugin depend on a second meter and would not improve the
  meter it already has.

### 9b. Should stay unlinked - linking would be redundant or harmful

- **FFTConvolver**: `IrStage` uses `juce::dsp::Convolution`, which already
  provides wait-free impulse loading and two channels. A second convolver
  would either replace it (a rewrite with no measured defect) or sit unused (a
  dead link-line entry). The library is compiled into a target only so a
  future benchmark can compare; it is not called.
- **kissfft, pffft, AudioFFT, audio_fft**: four more transforms against the one
  caller `pocketfft` has. JUCE also has its own FFT. Adopting one is a
  benchmark decision; until a profile names the transform, linking any of them
  is pure binary and build-time cost.
- **yin_pitch, dywapitchtrack, pitch_detector**: the shipping tracker is YIN,
  and `tests/pitch` prints its error. A replacement has to beat that number,
  not add a dependency. `pitch_detector` also drags in a JUCE module graph
  (`audio_fft` -> `AudioFFT`) for a capability already measured.
- **libsndfile**: the decisive fact is the licence, not the code. It is
  LGPL-2.1, it is configured as a static library, and `dr_wav` decodes the
  WAV print the vinyl stage needs. Linking it would be the exact obligation
  WP4 removed SoundTouch to escape, for a feature that does not exist.
- **q_dsp / cycfi_infra**: no caller, and `cycfi_infra` ships no licence file
  at all. That is not a technical preference; it is a legal blocker. The
  transient shaper's envelopes are hand-written and pinned by unit tests, so
  Q would be a second, untested implementation of the same maths.
- **Rack (`rack_dsp`)**: its headers could be borrowed, but the engine is a
  host runtime and the licence is GPL-3 with a plugin exception. Linking it
  into a JUCE plugin is neither technically nor legally sensible. Keeping the
  headers readable is the integration.
- **yasio**: a socket library has no role in an offline audio plugin. Its
  Anti-996 licence and its optional TLS download are two more reasons to keep
  it at zero.
- **ImGui**: it owns an event loop and a draw list. A JUCE plugin already has
  both. The panel's look is the product, so a second immediate-mode UI would
  be a regression, not a feature. `melatonin_inspector` covers the debug case.
- **farbot**: it is a realtime-safety toolbox, and the two places that needed
  one (`IrStage`, `NeuralStage`) solved it with JUCE and atomics. It stays
  vendored because it is 57 KB, MIT and header-only - one line away if a stage
  needs an `AsyncCaller`. Linking it now would be a dependency without a
  caller.
- **Source-only references (iPlug2, ChowCentaur, VCV-Prototype, Soundpipe,
  aubio, TransDes, Whetstone, multiband-transient-shaper)**: each fails a
  different gate. iPlug2 and VCV-Prototype are other frameworks or hosts;
  ChowCentaur and TransDes are finished plugins whose stages would have to be
  copied; Soundpipe and aubio need generated files; Whetstone needs the Faust
  compiler; the multiband shaper is MATLAB/Python research. All of them are
  useful to read. None is a link line.

### 9c. Conditional - the one area where more integration is real work

- **sokol_gfx** and **bgfx** are integrated as libraries: target, include
  path, surface creation, device lifetime and a present call. What is missing
  is the tape scene itself. Porting it means writing the scene's draw calls and
  shaders for the selected API, not changing how the dependency is fetched or
  linked. That work is worth doing only when a renderer backend is a product
  goal; the current `AUTO` path uses the CPU renderer, so the staged backends
  are not on the default binary's critical path.
- **wgpu-native** and **Dawn** are the same idea with a much larger cost:
  Cargo and a native single-architecture build for wgpu; a large dependency
  tree for Dawn. Neither is listed as a selectable value in
  `J37_RENDER_BACKEND`'s `STRINGS` list today (the branches exist in
  `j37_setup_graphics_backend`). There is no case for deeper integration until
  one of them is the only way to reach a platform the product needs.

### 9d. Removed or deferred - deeper integration is the wrong direction

- **SoundTouch** was removed in WP4. It was LGPL-2.1, it was WSOLA (the same
  family as the shipping granular shifter), and nothing called it. Re-adding
  it would not add a capability; it would add a licence and a third engine to
  measure. The correct next step for a stretcher is the existing
  `tests/pitch` comparison, not a new dependency.
- **Assimp** is gated on content. A loader added before a licence-clean mesh
  exists is the same mistake as the eighteen dead link-line entries; WP5's
  steps are in [`library-integration-plan.md`](library-integration-plan.md).

## 10. Cross-cutting findings

1. **The gate is doing real work.** `check_linkline.py` reports 33 linked
   targets, 393 include lines and 9 `J37_HAS_*` probes in
   `Source/DSP/OptionalLibraries.h` (the checker's scope), with every linked
   target and every probe accounted for. The old failure mode - linking a
   library because it might be useful - cannot return without failing this
   check.
2. **Vendoring is selective on purpose, not a purity test.** The vendored set
   is the code the engine includes and the harnesses must compile both ways:
   header-only DSP/maths, the two neural engines, the plate reverb, the WAV
   header, and libebur128's oracle. The JUCE-module dependencies (chowdsp,
   melatonin, foleys, clap) stay on CPM because upstream's module registration
   and API churn make a fork worse than a fetch. A truly network-free first
   configure still needs a populated CPM cache; that is the accepted cost of
   tracking those modules.
3. **Licence boundaries are load-bearing.** Apart from JUCE itself (AGPLv3 or
   commercial), the link line carries no GPL and no LGPL component.
   `chowdsp_dsp_utils` (GPLv3), SoundTouch (LGPL-2.1), libsndfile (LGPL-2.1),
   aubio (GPL-3), Rack (GPL-3) and cycfi_infra (no licence) are either off the
   link line or absent. Any proposal to "just link" one of them is a licence
   decision before it is a technical one.
4. **One copy at every ABI boundary.** nlohmann/json is deliberately pinned
   and included ahead of RTNeural's bundled copy; NAM Core, RTNeural and the
   plugin share one `nlohmann::json` type. A second JSON library, a second
   inference engine, or a second FFT would each break a boundary the build
   currently keeps clean.
5. **The only open integration work is renderer feature work.** Sokol and bgfx
   are staged; wgpu-native and Dawn are plumbing. The report does not
   recommend promoting either beyond what their `J37_RENDER_BACKEND` branch
   already does.
6. **Documentation drift found during the audit.** The counts and pins in the
   older docs have moved: `docs/library-integration-plan.md` says 43 packages
   and 8 probes (the tree now has 44 `CPMAddPackage` call sites and 9 probes),
   and `docs/library-candidates.md` / `THIRD_PARTY_NOTICES.md` say Catch2
   `v3.9.1` while `cmake/J37Tests.cmake` pins `v3.16.1`. These are accuracy
   issues in the records, not integration defects; the licence is unchanged
   (BSL-1.0). They should be corrected the next time those files are edited.

## 11. How this was verified

- `python3 tests/linkline/check_linkline.py` - exit 0, reporting:
  `33 target(s) on the plugin's link line, 393 include line(s) under Source/`,
  `9 J37_HAS_* probe(s), 9 read by real code`, and
  `every linked library has a caller, every probe is read`.
- The package inventory was read from `cmake/J37Dependencies.cmake` (44
  `CPMAddPackage` call sites), `cmake/J37VendorLibraries.cmake`,
  `cmake/J37Tests.cmake`, `.gitmodules`, `ThirdParty/` and the
  `target_link_libraries(first ...)` block in `CMakeLists.txt`.
- Call sites were cross-checked with `grep` over `Source/` for each linked
  library's published include spelling; the report does not claim a caller
  that the link-line checker does not also count.
- **Not run:** a full multi-platform build, the audio harnesses, and the CI
  matrix. This is a static integration audit; the existing harnesses remain
  the place where rendered behaviour is proved. No build file, source file or
  third-party tree was changed by this report.

## 12. Bottom line

No library in the tree needs deeper integration to do its current job. The
vendored set is at the strongest practical depth (source on disk, local target,
probe, fallback), the CPM set is at the right distance (upstream target, pinned
pin, no fork), and the unlinked set is deliberately unlinked because a second
copy of an existing capability would cost binary size, build time or a licence
for no new sound. The only forward work that is genuinely integration-shaped is
the staged renderer scene port; everything else is either already done or
rejected for a reason recorded next to it.
