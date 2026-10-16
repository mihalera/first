# Plugin analysis: what the code is actually like

A code review of the whole repository, written to be **acted on**: every finding
below is something you can decide about, and the plan that follows from it is
[plugin-improvement-plan.md](plugin-improvement-plan.md). Where the two documents
disagree, this one is the evidence and the plan is the opinion.

The rule for everything here: **a claim without a command is an opinion.** Each
section ends with how it was measured, or says plainly that it could not be.

## How this was measured

| What | How |
| --- | --- |
| file and line counts | `find Source -type f \| xargs wc -l`, `git ls-files \| awk -F/ '{print $1}' \| sort \| uniq -c` |
| what the code links vs. what it includes | `grep -rho 'include <[a-zA-Z0-9_./-]*>' Source/`, compared against the `target_link_libraries` block in `CMakeLists.txt` |
| the shipped numbers | the three text checks and the preset check, run from a clean tree |
| build and job timings | `gh run view <id> --json jobs` on run `38723374163` (the `main` build of 2026-10-15) |
| the suite's coverage | reading every `tests/**/run.sh` and its assertions - see [tests/README.md](../tests/README.md) |

**What this analysis cannot see.** The plugin cannot be configured or built in
the environment it was written in: one CPU, 2 GB of RAM, no CMake, no
`pkg-config`, no ALSA headers. So there is no profiler output, no measurement of
the audio thread under load, and no verification of any runtime behaviour that a
harness does not already pin. Everything about the *build* below comes from the
CI logs; everything about the *code* comes from the code.

## 1. Size and shape

`Source/` is **93 files and 32,592 lines**. The repository is the plugin and
almost nothing else: `Source/`, `tests/`, `cmake/`, `ThirdParty/` (vendored
libraries), `JUCE` (a submodule), `docs/`, and the build files.

The largest files, and what they are:

| Lines | File | Why it is that long |
| --- | --- | --- |
| 3,900 | `Source/ProcessorSignalPath.cpp` | the whole signal path, including `processTapeEngine` (lines 576-2229) and `processOneSample` |
| 2,615 | `Source/PluginEditor.cpp` | the editor's construction helpers and the resource accessors |
| 2,494 | `Source/GUI/TapeScene.cpp` | the animated 3D deck, its physics and its geometry |
| 1,913 | `Source/GUI/EditorSetup.cpp` | the constructor's 23 phases and the controls they build |
| 1,904 | `Source/PluginProcessor.h` | the processor's declarations - what every other file includes |
| 1,357 | `Source/ParameterTable.cpp` | 109 parameters in 23 groups |
| 1,325 | `Source/PluginProcessor.cpp` | the lifecycle only, now |
| 1,303 | `Source/PluginEditor.h` | the editor's widget declarations |
| 1,213 | `Source/GUI/J37LookAndFeel.cpp` | two look-and-feels and the text arithmetic they share |
| 1,000 | `Source/DSP/SaturationCore.h` | six principles, four type lists, and the normalisation |

`Source/DSP/` holds **26 headers** and `Source/GUI/` **26 files**. Four years of
"the file is too long to read" work has already been done here (see
[SPLITTING-BIG-FILES.md](../SPLITTING-BIG-FILES.md)), and it shows: the DSP is
one concern per header, the editor is one concern per file, and the extraction
layer in the tests is shrinking rather than growing.

**The shape is good; two files are still outliers.**
`ProcessorSignalPath.cpp` and `PluginEditor.cpp` are where the remaining
reading cost lives, and one function inside the first one is most of it.

Measured with:

```bash
find Source -name '*.cpp' -o -name '*.h' -o -name '*.mm' | xargs wc -l | sort -rn | head -20
```

## 2. The processor is one file and one very long function

`processTapeEngine()` is **1,654 lines of one function** (576 → 2229 in
`Source/ProcessorSignalPath.cpp`). It is the inside of the tape machine: the
per-sample loop, the record head, the EQ, the noise, the modulation, the glue,
the mix, the compensator, the limiter. Everything it needs from the outside now
arrives in precomputed block-rate coefficients (`Source/DSP/BlockRateCoefficients.h`),
which is the first real dent in it - the SAG envelope and the limiter's four
poles used to be rebuilt inside the loop at two and four `std::exp` per sample
per channel.

What is still true:

- **It cannot be read.** A 1,654-line function is not a reviewable unit: a change
  to the compensator shows up as a diff in the middle of a function whose name
  says "the tape engine".
- **It is the place every sound regression would be fixed**, and the harnesses
  around it are per-stage. The glue compressor, the loudness meter, the shaper,
  the rate bridge and the transport are all measured on their own; the *loop that
  sequences them* is measured by nothing.
- **The next stages to lift out are known.** `BlockRateCoefficients.h` is the
  template: a set of coefficients and state that turns into a struct which
  `processOneSample` calls once. The candidates, in the order they would pay off:
  the AUTO GAIN compensator, the safety limiter, the anti-phase guard, and the
  MIX crossfade itself.

Measured with:

```bash
awk '/^void FirstAudioProcessor::processTapeEngine/{print NR}' Source/ProcessorSignalPath.cpp
awk '/^void FirstAudioProcessor::processOneSample/{print NR}' Source/ProcessorSignalPath.cpp
```

## 3. The parameters, presets and state agree - today

The numbers the shipped checks report on a clean tree:

```
check_presets: 29 presets consistent, 101 of 109 parameters set, 12 excluded
check_presets: 109 registered in ParameterTable.cpp, 104 watched by the dirty badge
check_theme_wiring: 109 widgets, 94 bodies, 2 look-and-feel(s)
check_translations: 2 table(s) [en.txt, uk.txt], 153 entries, 1014 distinct strings in 62 files
```

Read that as a picture of the parameter surface: **109 parameters**, of which
**12 are deliberately outside every preset** (they are controls a preset should
not move: the compare slots, the bypass, things the user sets per session),
**8 more are settable but not stored** by the current preset table, and **104**
are watched by the dirty badge - so five parameters can change without the panel
offering to save them.

That last gap is worth a decision rather than a bug report: either those five are
meant to be part of the sound (and should mark the preset dirty), or they are
meant to be session-level (and should be named as excluded, like the twelve).

The state format has its own guard: the state harness round-trips a state, loads
an older one, and checks that a missing parameter keeps its default. That is the
right shape - the format is a compatibility surface and it has both a test and a
migration file (`ProcessorStateFormat.h`).

Measured with the three checks in [tests/README.md](../tests/README.md#run-it).

## 4. What is measured, and what is not

**Six harnesses and five checks run on every pull request.** They compile the
shipping code (increasingly by `#include`, not by text extraction), they render
audio, and they run under ASan + UBSan in the debug job. This is a strong suite
for a plugin of this size, and it is the reason the file splits in this
repository have been safe.

The holes, in the order they cost:

1. **`processBlock`'s boundaries.** Zero frames, one frame, a prime block size, a
   rate change between blocks, `prepareToPlay` without ever playing, a stop
   followed by a start at a different rate. The stages are measured; the seams
   between them are not, and a host will hand over every one of those shapes.
2. **The renderers.** Four native backends with no automated coverage at all -
   see [§7](#7-the-renderers-have-no-coverage).
3. **The AUTO GAIN compensator.** The one gain stage in the chain that nothing
   measures on signal. Four of its properties are pinned as text today, which
   catches an edit that drops one and cannot catch an edit that gets the
   arithmetic wrong.
4. **Nothing compares a rendered buffer to a reference.** Every claim in the
   suite is a local property: a level, a ratio, a monotonicity, a silence. There
   is no "this is the tone we shipped" test - which for a saturation plugin is
   the cheapest way to notice that a refactor changed the sound by 0.2 dB.

## 5. The link line is larger than the code

`CMakeLists.txt` links **30 third-party libraries** into the plugin, and
`Source/` includes **13** of them. Sixteen are on the link line and are never
named by a single line of the plugin's own code:

```
linked, never included (16):
  dr_libs  dywapitchtrack  fftconvolver  imgui  kissfft  PFFFT  pocketfft
  rack_dsp  sndfile  sokol_gfx  yasio  AudioFFT  audio_fft  dattorro_verb
  pitch_detector  yin_pitch

actually included by Source/ (13):
  chowdsp_dsp_utils  chowdsp_math  melatonin_inspector  melatonin_blur
  foleys_gui_magic  xsimd  signalsmith-stretch  SoundTouch  farbot
  readerwriterqueue  RTNeural  nlohmann_json  glm

required only transitively (1):
  chowdsp_simd   (pulled in by chowdsp_dsp_utils' own headers)
```

> **Two corrections, applied after this count was taken.** `pocketfft` is
> INCLUDED now: `Source/DSP/SpectrumAnalyser.h` is its caller and
> `tests/spectrum/run.sh` measures that stage with the library and without it, so
> the "never included" list is fifteen rather than sixteen. And
> `signalsmith-stretch` was never included at all - the probe that would decide
> `J37_HAS_SIGNALSMITH` did not exist, so the macro read 0 in every build and
> nothing could have included it. `signalsmith-stretch` and `SoundTouch` are two
> of the names below that "look like the pitch engines and are probed for": they
> are probed and then read by no code, which is the point that paragraph makes,
> arrived at from the other side. Re-run the count rather than editing these
> numbers.
>
> **A third correction, about the two libraries on neither list because they are
> not repositories.** `cmake/J37VendorLibraries.cmake` discovered Intel IPP and
> Apple vDSP, linked them, and defined `J37_HAVE_IPP` and `J37_HAVE_VDSP` for the
> plugin - two macros that no file under `Source/` has ever read. The file's
> prose said the DSP took "its vDSP path" under one of them and the portable path
> otherwise; neither path existed, and the IPP install on the CI runners
> accelerated nothing at all. The switch that library's own FFT engine is gated
> on is JUCE's, and it is not the flag the internet names: JUCE 9.0.4 has no
> `JUCE_DSP_USE_INTEL_IPP` anywhere in its tree, and `juce_dsp.cpp` /
> `juce_FFT.cpp` compile their Intel IPP engine under the IPP **linking-model**
> macros instead. Both halves are fixed and both are now checked:
> `tests/definitions/check_definitions.py` compares every macro the build hands the
> compiler against the preprocessor branches that read them, and asserts the IPP
> switch is the one JUCE gates on and is still thrown. What that changes about
> the link line - and what it does not - is item 1 of
> [plugin-improvement-plan.md](plugin-improvement-plan.md).

That is the link line. The dependency *menu* is larger still:
`cmake/J37Dependencies.cmake` declares **42 CPM packages** over 1,222 lines -
soundtouch, chowdsp_utils, melatonin_inspector, melatonin_blur,
foleys_gui_magic, xsimd, signalsmith-stretch, signalsmith-linear, pocketfft,
nlohmann_json, farbot, Rack, dywapitchtrack, fftconvolver, imgui, iPlug2,
ChowCentaur, VCV-Prototype, soundpipe, aubio, kissfft, pffft,
readerwriterqueue, yasio, RTNeural, dr_libs, libsndfile, Yin-Pitch-Tracking,
audio_fft, AudioFFT, pitch_detector, dattorro-verb, q, cycfi/infra, sokol,
glm, transdes, whetstone, multiband-transient-shaper, bgfx, wgpu-native and
Dawn - each with a pin, a probe and a paragraph of reasoning. Twenty-five of
them are `DOWNLOAD_ONLY` (fetched, never configured) and eight of those are not
linked either, so a package like `aubio` is a fetch, a version pin and sixty
lines of comment that no translation unit, no target and no binary of this
plugin ever reaches.

Three consequences, only the first of which is about build time:

- **It costs configure time and build time** on every platform, five times over:
  vendored copies keep it offline, but every configured package still gets a
  target, an include path and a version pin to maintain.
- **It hides which libraries are load-bearing.** `signalsmith-stretch` and
  `SoundTouch` look like the pitch engines and are probed for by
  `OptionalLibraries.h`, but the autotune path is `PitchTracker` +
  `RealtimePitchShifter`, which are this repository's own headers. A reader
  cannot tell the fallback from the thing in use without doing this count.
- **It changes the licence of a distributed binary.** The one that binds today
  is **`chowdsp_dsp_utils`**, which is **GPLv3** and is both included and linked
  (`chowdsp_math` and `chowdsp_simd` from the same repository are BSD, and
  `foleys_gui_magic` is BSD 3-Clause despite its reputation). Beyond that,
  **SoundTouch** and **libsndfile** are LGPL-2.1 and linked statically, and
  `rack_dsp` is GPL-3.0-or-later with a plugin exception. `aubio` is a fetched
dependency that is neither linked nor included, so it is dead weight rather than
  an exposure. `THIRD_PARTY_NOTICES.md` lists each component's terms.

The reason it survived is worth naming: the link line is written the way a
"kitchen sink" dependency list is written, and nothing in the repository ever
asks the question "does the code call this?". A check that compares the link list
to the includes under `Source/` would answer it on every PR, and deleting the
sixteen entries is a link-line edit rather than a rewrite.

Measured with:

```bash
grep -rho 'include <[a-zA-Z0-9_./-]*>' Source/ | sort -u
sed -n '/^target_link_libraries(first/,/^        juce::juce_recommended_warning_flags/p' CMakeLists.txt
```

## 6. The build is the slowest thing in the project

From the `main` build of 2026-10-15 (run `38723374163`), job durations:

| Job | Time |
| --- | --- |
| DSP regression harness | 1 m 25 s |
| DSP micro-benchmarks | 3 m 01 s |
| build-linux-debug | 6 m 14 s |
| build-android | 7 m 26 s |
| build-macos | 8 m 25 s |
| build-linux | 10 m 59 s |
| build-windows | 12 m 25 s |

Wall clock for the run: **12 m 32 s**, and almost all of it is JUCE. Two things
make that worse than it needs to be:

- **Every translation unit pulls `<JuceHeader.h>`.** `CMakeLists.txt` names the
  alternative and leaves it as a follow-up: including the individual
  `juce_*` module headers instead stops a file that needs one type from parsing
  every module. With 26 editor files and 22 plugin files this is the largest
  single build-time lever in the repository, and it is a mechanical change.
- **No ccache.** These builds are dominated by code that never changes between
  runs; the workflow caches nothing across runs but the CPM checkouts.

The cheap half of the fix is already in place: the text checks and the harnesses
are ~1.5 minutes and are the jobs that fail first when something is wrong with
the *code* rather than with the *build*. What is missing is a compile-only gate
that turns "one missing include" from a five-job, 12-minute failure into a
two-minute one.

Measured with:

```bash
gh run view 38723374163 --json jobs \
  --jq '.jobs[] | [.name, .conclusion, ((.completedAt|fromdateiso8601)-(.startedAt|fromdateiso8601))] | @tsv'
```

## 7. The renderers have no coverage

`Source/GUI/NativeRenderer_D3D11.cpp`, `_D3D12.cpp`, `_Vulkan.cpp` and
`_Metal.mm` (1,466 lines together) plus `TapeSceneShaders.h` are compiled only by
the platform build jobs, each on its own OS, and by nothing else. There is no
harness that includes them, because they need a graphics device, a swapchain and
a window - all three of which the CPU fallback exists to avoid needing.

The failure this leaves open is not hypothetical: a wrong include in one backend
produced a cascade of `expected unqualified-id before 'namespace'` through fifty
lines of glm on a previous change, and it was visible only in a full platform
build. A job that compiles each backend's translation unit and its shader header
*without* linking, and without creating a device, would catch that class in
seconds on Linux for all four.

## 8. The one JUCE patch this repository carries

`patches/juce-9.0.4/juce_opus_config.h` is JUCE's own 9.0.4 opus config header
with two `typedef`s inserted (`kiss_twiddle_scalar`, `kiss_fft_scalar`).
`CMakeLists.txt` copies it over the submodule's copy before JUCE is configured,
and the copy is keyed to the submodule's version on purpose: bumping JUCE stops
the copy rather than writing a stale header into a newer tree.

The cause is upstream: `mdct.c` includes `mdct.h` before `kiss_fft.h`, and
`mdct.h` declares its trig table in terms of a type that is defined in the second
one. It is a real defect in the JUCE 9.0.4 tree, it is not the only way to fix it
(patching `mdct.h`'s include order would also do), and it is tracked here because
it cannot be pushed upstream from this repository. **The thing to check on every
JUCE bump is whether the typedefs are still needed** - if a future 9.0.x adds
them, the patch directory and its guard go away in one commit.

## 9. Details that will bite the next reader

Smaller, but each one is a real trap:

- **A mono instance reads the stereo loudness sum.** For a mono instance the meter
  is fed the same sample twice, so a mono instance reads what a stereo instance
  reads for centre-panned material - and that is stated nowhere in the code. The
  next reader who spots it will "fix" it and move every mono mix by 3 LU.
- **`J37` survives as a name** in build flags (`J37_BUILD_TESTS`, `J37_BUILD_COMMIT`,
  `J37_FAST_WINDOWS_BUILD`, the `J37_*` CMake options and the `Source/DSP/J37Math.h`
  header) after the product was renamed. That is deliberate for the tape formula
  named J37 and accidental for the rest: a reader looking for "J37" finds fourteen
  unrelated things.
- **Translation coverage is not a number.** 153 entries exist and all are live;
  how much of the 1,014-string panel surface is translated is not printed
  anywhere, and it is the half that actually describes the experience of a
  Ukrainian user.
- **Documentation drifts.** `docs/plugin-improvement-plan.md` quoted a
  `processTapeEngine` of 3,367 lines, a `PluginProcessor.h` of 7,605 and a
  `PluginEditor.cpp` of 9,441; the real numbers today are 3,900 / 1,904 / 2,615
  for the files and 1,654 for the function. Every one of those was true when it
  was written and false within a month, which is the argument for putting numbers
  in a document only when the document also says how to re-measure them.
- **`.vscode/` is committed** while `.gitignore` ignores `.vscode/`. Three files
  are tracked by explicit force-add; changing one of them is a change to
  everybody's editor.

## 10. What is genuinely healthy

Not everything is a finding. This repository does three things that most plugin
codebases of this size do not:

- **It explains its decisions where they are made.** The `CMakeLists.txt` comment
  on the manufacturer code, the one on the opus patch, the one in
  `Source/DSP/J37Math.h` stating the conventions of the directory: a reader
  arriving at any of them knows why, not just what.
- **It tests what is hard to test.** The loudness meter, the transport's
  gestures, the layout arithmetic, the constructor's phase order, the theme
  re-inking, the preset table's consistency with the parameter table. Every one
  of those is a class of bug that is invisible until a user hits it, and each was
  written after a real defect.
- **It has no TODO markers at all** (`grep -rn "TODO\|FIXME\|HACK" Source/` → 0
  matches). The work that is not done lives in `docs/`, not in scattered
  comments - which is why the numbers in this file are checkable rather than
  folklore.

## Appendix A: the numbers

Everything quoted above, in one place, with the command that produces it.

| Number | Value | Command |
| --- | --- | --- |
| files in `Source/` | 93 | `find Source -type f \| wc -l` |
| lines in `Source/` | 32,592 | `find Source -type f \| xargs wc -l \| tail -1` |
| headers in `Source/DSP/` | 26 | `ls Source/DSP \| wc -l` |
| files in `Source/GUI/` | 26 | `ls Source/GUI \| wc -l` |
| parameters | 109 | `python3 tests/presets/check_presets.py` |
| parameters outside every preset | 12 | `python3 tests/presets/check_presets.py` |
| parameters set by the presets | 101 | `python3 tests/presets/check_presets.py` |
| parameters watched by the dirty badge | 104 | `python3 tests/presets/check_presets.py` |
| factory presets | 29 | `ls Source/Presets/Factory/*.json \| wc -l` |
| panel widgets | 109 | `python3 tests/theme/check_theme_wiring.py` |
| look-and-feels | 2 | `python3 tests/theme/check_theme_wiring.py` |
| translation entries | 153 | `python3 tests/translations/check_translations.py` |
| translatable strings | 1,014 in 62 files | `python3 tests/translations/check_translations.py` |
| `processTapeEngine` | 1,654 lines | `awk` on `Source/ProcessorSignalPath.cpp` |
| third-party libraries linked | 30 | `sed -n '/^target_link_libraries(first/,/recommended_warning_flags/p' CMakeLists.txt` |
| libraries the source includes | 13 | `grep -rho 'include <...>' Source/ \| sort -u` |
| CPM packages declared | 42 | `grep -c 'CPMAddPackage(' cmake/J37Dependencies.cmake` |
| TODO / FIXME / HACK in `Source/` | 0 | `grep -rn "TODO\|FIXME\|HACK" Source/` |
| CI wall clock | 12 m 32 s | `gh run view 38723374163 --json jobs` |

## Appendix B: reproduce this analysis

```bash
git clone --recurse-submodules <repo> && cd first

# the shipped numbers
python3 tests/presets/check_presets.py
python3 tests/theme/check_theme_wiring.py
python3 tests/translations/check_translations.py

# size and shape
find Source -type f | xargs wc -l | sort -rn | head -20

# the link line vs. what the code includes
sed -n '/^target_link_libraries(first/,/recommended_warning_flags/p' CMakeLists.txt
grep -rho 'include <[a-zA-Z0-9_./-]*>' Source/ | sort -u

# the whole suite, which is what CI runs first
for t in dsp layout state transport pitch neural; do "tests/$t/run.sh"; done
```
