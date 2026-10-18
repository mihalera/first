# Plugin improvement plan

> **Code analysis - plan how to improve the code, plus new ideas.**
>
> This plan is not a wish list. Every item below comes from a finding in
> [plugin-analysis.md](plugin-analysis.md), says what it costs, what it pays for,
> and how you will know it worked. If an item has no evidence behind it, it is in
> [New ideas](#new-ideas) instead, where it belongs.

## How to read this

- **Priority 1** is what a *distributed binary* owes, and what makes every future
  change cheaper. It is not about the sound.
- **Priority 2** is the parts of the plugin nobody can currently see: the long
  function, the renderers, the block boundaries, the sound itself.
- **Priority 3** is product and prose.
- **[New ideas](#new-ideas)** are features that follow from what the engine
  already is, each with the reason it fits.
- **[What the last plan delivered](#what-the-last-plan-delivered)** closes the
  loop on the previous version of this document, so nothing is quietly dropped.

Each item carries: **what**, **why**, **cost**, **payoff**, **evidence**, and -
where it applies - **how to verify**.

## Priority 1 - what a distributed binary owes

### 1. Decide the licence question, then make the link line match the code

The analysis found that the plugin links **30 third-party libraries** and its own
code includes **13** of them, and that one of the thirteen is
**`chowdsp_dsp_utils`, which is GPLv3**. Two LGPL-2.1 libraries (`SoundTouch`,
`libsndfile`) are linked statically, and `rack_dsp` (VCVRack/Rack,
GPL-3.0-or-later with a free-of-charge plugin exception) is linked while being
included by nothing.

*What to do, in this order:*

1. **Decide the intended licence of a shipped binary.** The honest options are:
   ship under GPLv3/AGPL and stop worrying about `chowdsp_dsp_utils`, or buy the
   commercial licence for the modules used, or stop using `chowdsp_dsp_utils`
   and take only the BSD modules (`chowdsp_math`, `chowdsp_simd`) plus the
   handful of helpers that would have to be replaced. Option three is a real
   amount of work; option one costs nothing but the terms of the product.
2. **Delete the fifteen link-line entries nothing includes** - `dr_libs`,
   `dywapitchtrack`, `fftconvolver`, `imgui`, `kissfft`, `PFFFT`,
   `rack_dsp`, `sndfile`, `sokol_gfx`, `yasio`, `AudioFFT`, `audio_fft`,
   `dattorro_verb`, `pitch_detector`, `yin_pitch` - and the eight packages that
   are fetched but neither linked nor included (`aubio`, `iPlug2`, `ChowCentaur`,
   `VCV-Prototype`, `soundpipe`, `transdes`, `whetstone`,
   `multiband-transient-shaper`). `libsndfile` and `rack_dsp` are on both lists;
   deleting `sndfile` removes the last LGPL question that has nothing to do with
   the audio.

   **The link half of this is done.** Eighteen entries came off
   `target_link_libraries(first ...)` - the fifteen above, plus
   `signalsmith-stretch`, `SoundTouch`, `farbot` and `q_dsp`, with
   `pocketfft`/`xsimd` already having left by the other route. What is left on the
   line is what `Source/` includes, and the PGPL/LGPL picture improved with it:
   SoundTouch (LGPL-2.1) and libsndfile are no longer statically linked, because
   nothing called either, and `signalsmith-stretch` came off with them after the
   entry above was corrected - a library the code cannot call yet is not a use,
   and the vendored source plus the CPM fetch stay for the corrector work. The
   packages are still FETCHED; deleting those is the configure-time half of the
   item and is still open, because a fetch is also what makes adopting one of
   them a one-line change. `chowdsp_dsp_utils` **is off the line now**: the code
   calls only `chowdsp_math`'s approximators, so the GPLv3 module was dropped and
   with it the binary's GPL obligation - the third option in 1.1 above, taken
   under WP1 of [library-integration-plan.md](library-integration-plan.md). The
   package is still fetched for the BSD modules beside it.

   `tests/linkline/check_linkline.py` is the check item 3 asks for, and it is in
   the `dsp-tests` job: it fails on a linked target no include names, on an
   included library that is not linked, and on a `J37_HAS_*` probe nothing reads -
   the last of which is how xsimd's dead branch was found.

   **`pocketfft` has left this list since it was written, and by the other
   route.** `Source/DSP/SpectrumAnalyser.h` includes it, so it now has a caller to
   break before it can be deleted, and `tests/spectrum/run.sh` builds that stage
   both with the library and without it. That is the shape this item argues for -
   a linked library the code actually calls - reached by USING the library instead
   of by deleting it from the link line, and it is the cheaper of the two whenever
   the library is the right tool. The same audit found two entries whose prose
   claimed a use that did not exist: `OptionalLibraries.h` said
   `signalsmith-stretch` stretched time in the transport (it does not, and a tape
   slowing down is supposed to drop the pitch), and `J37_HAS_SIGNALSMITH` was
   never probed at all, so the macro read 0 in every build. Both libraries are
   still linked and still read by no code; the corrected entries say which stage
   should adopt `signalsmith-stretch` (the corrector, which shifts pitch at
   constant duration) and leave the deletion decision where it was.

   **`xsimd` has left the pile by the same route as `pocketfft`.** It appeared in
   the analysis's "actually included by Source/" list while no line of `Source/`
   included it: the only mention was behind a `J37_USE_SIMD_TANH` flag no build
   threw, guarding a function (`j37Tanh`) that nothing called, whose body called
   `xsimd::tanh` on a float - and xsimd's scalar entry point is `using std::tanh;`
   (`xsimd/arch/xsimd_scalar.hpp`), so the branch would have switched nothing even
   with the flag thrown. All three are fixed: the shaper's eleven tanh calls go
   through the seam, the flag is a real CMake option that is OFF by default
   because it changes the rendered audio by 1.19e-7, `tests/dsp/simd_tanh_test.cpp`
   builds the three shapes and compares their curves against each other, and
   `tests/dsp/run_bench.sh` measures what it buys - ~2.4 ns per call against
   `std::tanh`'s ~6.7 ns, and about 1.5x off the saturation core itself.

   **The two vendor libraries have left the "linked and inert" pile, by the same
   route.** `cmake/J37VendorLibraries.cmake` used to discover Intel IPP and Apple
   vDSP, link them, and define `J37_HAVE_IPP` / `J37_HAVE_VDSP` for a plugin that
   read neither. It now throws the IPP **linking-model** macro JUCE's own FFT
   engine is compiled under - there is no `JUCE_DSP_USE_INTEL_IPP` in JUCE 9.0.4,
   whatever the build guides say - and throws it only for the single-threaded
   static layout whose whole link line was measured against Intel's own wheel.
   That measurement also fixed two things the switch alone would not have
   survived: the include directory JUCE's engine needs is `include/ipp` rather
   than `include` (the wheels keep `ipp.h` alone at the top and the real headers
   one level down), and the engine's FFT init links through `ippvm`, which was
   not on the line. On a machine with that layout, `juce::dsp::FFT` - and so the
   Convolution behind the IR cabinet - is Intel's rather than JUCE's, and the
   licence consequence is real: that binary carries Intel's IPP object code, so
   the terms to read before distributing it are Intel's and not this
   repository's. On Apple nothing is thrown, because the vDSP that runs there is
   JUCE's own, from the Accelerate framework `juce_dsp` declares itself.

   This does not change the deletion decision above: `dr_libs`, `dywapitchtrack`,
   `fftconvolver`, `imgui`, `kissfft`, `PFFFT`, `rack_dsp`, `sndfile`,
   `sokol_gfx`, `yasio` and the rest are still on the link line with no caller,
   and the check item 3 below asks for - the one that compares the link line to
   the includes - is still open. `tests/definitions/check_definitions.py` is its
   sibling: it compares the *macros* the build hands the compiler against the
   preprocessor branches that read them, which is the same question one level
   down, and it is what caught this pair.
3. **Add a check that keeps it that way.** A five-line script comparing the
   names in `target_link_libraries(first` to the `include <...>` set under
   `Source/` would have caught this on the day it was written, and will catch it
   the next time somebody links a library "just in case". It belongs in the
   `dsp-tests` job beside the other text checks.

   **Done** - `tests/linkline/check_linkline.py`, wired into that job. It asks
   both directions of the link question (linked but not included, included but
   not linked) and a third the original sketch did not have: every `J37_HAS_*`
   macro in `OptionalLibraries.h` must be READ by something outside it, which is
   the defect that let xsimd's branch reach two builds without being compiled.
   The one exception the script used to carry - `chowdsp_dsp_utils`, the GPLv3
   module and the licence decision above - is gone, because the module is gone:
   the probe left `OptionalLibraries.h` with it, and the allowance dictionaries
   are empty on purpose.

*Cost:* an afternoon for the deletion and the check, plus whatever the licence
decision costs. *Payoff:* a shorter configure, a shorter build on five platforms,
and a binary whose terms are known rather than inherited. *Evidence*:
[analysis §5](plugin-analysis.md#5-the-link-line-is-larger-than-the-code).
*Verify:* CI green on all seven jobs, and the check fails when a dependency is
added to one list and not the other.

### 2. Make the common failure a two-minute failure

One missing include currently costs five platform builds, twelve minutes of wall
clock and a red run per platform (analysis §6: `build-windows` 12 m 25 s,
`build-linux` 10 m 59 s, `build-macos` 8 m 25 s).

- **A compile-only job** that builds the plugin's translation units with
  warnings as errors and no linking, before the platform jobs fan out. It is not
  a substitute for them: it is what makes the class of failure that has cost the
  most on this repository visible in the first two minutes instead of the
  fifteenth.
- **ccache** in the workflow. These builds are dominated by JUCE, glm, xsimd and
  the rendering backends, which never change between runs.
- **`J37_BUILD_JOBS`** already bounds the parallel compile on the Linux runners
  after the OOM kills; that bound should be in the compile-only job from the
  start rather than discovered again.

*Cost:* half a day. *Payoff:* the most common failure mode gets its own fast
lane. *Evidence:* [analysis §6](plugin-analysis.md#6-the-build-is-the-slowest-thing-in-the-project).
*Verify:* the new job's duration is under three minutes on a cold cache and
under one on a warm one.

### 3. Stop pulling `<JuceHeader.h>` into every translation unit

`CMakeLists.txt` already names this as the follow-up to the Projucer migration:
the sources include `<JuceHeader.h>`, which is how the old project exposed JUCE,
and the modern idiom is the individual `juce_*` module headers. A file that needs
one type currently parses every module.

*Cost:* mechanical, file by file, and the two JUCE-compiling harnesses
(`tests/editor/check_editor_juce.sh`, `tests/dsp/check_headers_juce.sh`) are
exactly the checks that make it safe. *Payoff:* the largest single build-time
lever in the repository, paid on every platform and every run. *Evidence*:
[analysis §6](plugin-analysis.md#6-the-build-is-the-slowest-thing-in-the-project).
*Verify:* the compile-only job's time before and after, on the same runner.

## Priority 2 - the parts nobody can see

### 4. Keep taking `processTapeEngine` apart at its own seams - **done**

It was **1,654 lines in one function** when this plan was written (analysis §2).
`BlockRateCoefficients.h` proved the recipe: name the state, move the coefficients
beside it, let the per-sample loop call one thing. Four stages were named, in the
order they pay off - the ones that were still inline in the loop and easiest to
reason about separately - and all four are out of the function now:

- the **AUTO GAIN compensator** - `Source/DSP/AutoGainCompensator.h`, pinned by
  `tests/gain`: it only restores, stops at 12 dB, holds through silence and does
  not chase a transient;
- the **safety limiter** - `Source/DSP/SafetyLimiter.h`, pinned by `tests/limiter`:
  bit-for-bit transparency below the ceiling, a catch measured in milliseconds, a
  release slow enough not to pump between notes, and the same shape at every
  engine rate;
- the **anti-phase guard** - `Source/DSP/MultibandPhaseGuard.h` (three bands rather
  than one), pinned by `tests/phase`: the crossover flat, the per-band rotation and
  the unwind after the fault ends;
- the **MIX crossfade** - `Source/DSP/MixCrossfader.h`, pinned by
  `tests/mix_crossfader`: 0 % is the dry input bit for bit, 100 % is the tape path,
  the law is equal power with its +3 dB correlated midpoint measured rather than
  hidden, and BOTH legs carry the transport gate - which is the claim the transport
  check can only see as text and a harness can now drive.

What is left of the signal path's own length (see SPLITTING-BIG-FILES.md, step F)
is the ~1,570 lines of per-block derivations, which is a move rather than a rewrite.

*Cost:* one stage per sitting, each one a move plus a harness section.
*Payoff:* the function is shorter, and four audible behaviours are measurable
rather than described. *Evidence:* [analysis §2](plugin-analysis.md#2-the-processor-is-one-file-and-one-very-long-function).
*Verify:* each extracted stage has a harness section that renders it and asserts
on the property its comment claims - all four do today.

### 5. Drive `processBlock` with the buffer shapes hosts actually hand over

Zero frames, one frame, a prime block size, a rate change between blocks, a
plugin that is prepared and never played, a stop followed by a start at a
different rate. A harness in the shape of `tests/state` that drives the whole
chain unconditionally and asserts "no NaN, no denormal, no output above the
ceiling, DELTA silent at MIX 0" covers the class of bug that costs a user their
session and that neither the sanitizers nor the per-stage harnesses can see.
*Evidence:* [analysis §4](plugin-analysis.md#4-what-is-measured-and-what-is-not).

### 6. Give the rendering backends a compile check

`Source/GUI/NativeRenderer_{D3D11,D3D12,Vulkan,Metal}.cpp` and
`TapeSceneShaders.h` are compiled only by the platform jobs, one OS each. A job
that includes each translation unit in an offscreen build - no window, no
swapchain, just the type checks - catches the failure that has already cost this
repository a fifty-line cascade of glm errors, in seconds, on Linux, for all four.
*Evidence:* [analysis §7](plugin-analysis.md#7-the-renderers-have-no-coverage).

### 7. Lock the tone

Nothing in the suite compares a rendered buffer to a reference: every claim is a
local property (a level, a ratio, a monotonicity). For a saturation plugin the
cheapest useful addition is a **reference render**: a short signal through the
whole chain at a fixed set of parameters, with a checksum or an RMS/peak
tolerance pinned in the harness. It will not catch a deliberate voicing change -
it is not meant to - it catches the refactor that moved the sound by 0.2 dB.
*Evidence:* [analysis §4](plugin-analysis.md#4-what-is-measured-and-what-is-not).

### 8. Close the parameter-versus-badge gap

Of the 109 parameters, 12 are deliberately outside every preset, 101 are set by
the presets, and **104** are watched by the dirty badge - so five parameters can
change without the panel offering to save them (analysis §3). Either those five
mark the preset dirty, or they are named as session-level like the twelve.

### 9. Make translation coverage a number

`uk.txt` is 153 live entries against 1,014 translatable strings in the panel
(analysis §3, §9). Printing the untranslated count per language on every run - as
a report line, not a gate, because a half-translated panel showing English is the
deliberate design - turns "how translated is the panel" from a feeling into a
number that can be moved.

### 10. Write down the two decisions a reader will otherwise re-litigate

- **A mono instance feeds the loudness meter the same sample twice**, so it reads
  the stereo sum. That is self-consistent and it is nowhere stated; the next
  reader will "fix" it and move every mono mix by 3 LU. Put it in the meter's doc
  comment (`Source/DSP/LoudnessMeter.h`).
- **The JUCE opus patch** (`patches/juce-9.0.4/`): record in `CMakeLists.txt`
  what upstream would have to change for it to go away, so the next JUCE bump
  starts by asking whether it is still needed. *Evidence*:
  [analysis §8](plugin-analysis.md#8-the-one-juce-patch-this-repository-carries),
  [§9](plugin-analysis.md#9-details-that-will-bite-the-next-reader).

### 11. Put the numbers where they can be re-measured

Twice now this document has quoted a line count that was true when written and
false a month later (analysis §9). The fix is a rule rather than a rewrite:
**a number in `docs/` is either printed by a check or given with its command.**
Appendix A of the analysis is the pattern - every figure there has the command
that produces it next to it.

## Priority 3 - product and prose

### 12. A panel that says what it is doing

The SETTINGS control already reports the active renderer fallback and explains
it. The same treatment is missing for the two states a user actually asks about:
**why is the output quieter** (AUTO GAIN is restoring, the limiter is working,
the machine is stopped) and **why is nothing happening** (the transport is armed
waiting for the host, a model failed to load, the IR file is missing). Those are
three numbers the engine already publishes and three sentences the panel does not
yet say.

### 13. Keep the documentation honest about `J37`

`J37` survives as a build-flag prefix and a header name after the product rename,
which is deliberate for the tape formula and accidental for the rest. Either the
flags get renamed in one mechanical commit, or the README's renaming note gets
one line naming them as build configuration - whichever, on purpose.

## New ideas

Features that follow from what the engine already is. None of them is scheduled;
each one says what it would be built out of.

| Idea | What it is | Why it fits this engine |
| --- | --- | --- |
| **LUFS-target AUTO GAIN** | let the compensator aim at a loudness target instead of unity, once the meter and the compensator read the same signal | the meter is already the panel's ground truth and the compensator is the one unmeasured gain stage (item 4) - doing them together is one piece of work |
| **Glue sidechain input** | let the two glue compressors be driven by an external bus | they are already signal-driven and their timing already follows the tape type and speed; a sidechain makes the "compressor-coupled saturation" claim user-controllable |
| **Mid/side drive and glue** | process the side channel with its own DRIVE and glue amount | the engine already has a stereo width stage and a stereo tape offset; M/S is the natural next stereo control and it is what the bus use case asks for |
| **Print / commit to the DAW** | render the machine's output into the host's audio in one action | every parameter is automatable and every stage is deterministic; a commit function is mostly bookkeeping, and it is what mastering users actually reach for |
| **Loudness-matched A/B** | match the level between the A and B slots before comparing | the plugin has A/B slots and a real loudness meter; unmatched A/B is the oldest mistake in audio |
| **Preset browser** | tag, search and preview the 29 factory presets and the user's own | the preset table is JSON and already consistent with the parameter table, so this is UI over data that exists |
| **Per-band drive** | three-band saturation on the existing three-band EQ split | `ThreeBandEq.h` already splits the signal; the shaper is per-sample and stateless |
| **Spectrum / harmonic view** | draw the harmonic analyser as a curve instead of a number | `HarmonicAnalyser.h` measures the shaper's own input and output today |
| **Tape-stop as automation** | make the transport keys automatable parameters | `TransportRig` is deterministic and driven frame by frame; the host-facing half is exposing the keys |
| **Session-aware wow/flutter** | offer a flutter rate that locks to the host's tempo | the modulation is already a set of shaping curves on the machine's rate |
| **Import / export a preset as a file** | a `.miboxpreset` the user can send someone | the format already exists as the user-preset file; this is a menu item and a dialog |
| **Accessibility pass** | real names and values on every control for screen readers | 109 widgets with panel-drawn text are the exact case where a screen reader needs help, and the widget table is centralised in `PanelText.h` |
| **Coverage in the panel** | a build/health line that prints which optional libraries this build has | `OptionalLibraries.h` already knows, and it is the first question a bug report raises |

## If you do three things

1. **Settle the licence question and trim the link line to what the code
   includes.** Every other plan in this document is cheaper once the dependency
   graph means something (Priority 1.1).
2. **Add the compile-only gate and ccache.** It converts the most expensive
   failure class on this repository into the cheapest (Priority 1.2).
3. **Take the compensator out of `processTapeEngine` and measure it.** It is the
   only gain stage in the chain that nothing measures, and it is the first step
   towards a function a reviewer can read (Priority 2.4).

## New ideas: how to choose one

If a feature has to be picked tomorrow: **loudness-matched A/B**, then
**LUFS-target AUTO GAIN**. Both are small, both use machinery that already
exists and is already tested, and both are the kind of thing a mastering user
notices immediately. The expensive ones on the list - mid/side drive, per-band
drive, print-to-DAW - are only worth starting after the link line and the
compile gate, because they are the changes that will be built, rebuilt and
reviewed the most.

## What the last plan delivered

The previous revision of this document was a working list written from the CI
logs of the day. Its items, and where they stand:

| Item | Status |
| --- | --- |
| Split the three long files (`PluginProcessor.h` 7,605 / `.cpp` 7,220 / `PluginEditor.cpp` 9,441 lines) | **done** - 1,904 / 1,325 / 2,615 today, with the DSP in 26 headers, the editor in 26 files and the signal path in a file of its own |
| Let the harnesses `#include` the moved stages instead of cutting text | **done** - the DSP extractor's header list is empty; only the inline shapers are still cut |
| Add the check that makes a split safe | **done** - `tests/editor/check_editor_juce.sh` compiles every translation unit and every `Source/GUI/*.h` against real JUCE, and caught three faults on its first run |
| Decide what the loudness meter is for, and write it down | **partly** - the meter's contracts are pinned by `tests/dsp/loudness_test.cpp`; the mono-instance question is still open (item 10) |
| Extract the AUTO GAIN compensator and measure it on signal | **done** - `Source/DSP/AutoGainCompensator.h`, pinned by `tests/gain` |
| Keep taking `processTapeEngine` apart at its own seams (the four stages of item 4) | **done** - the guard, the compensator, the limiter and the MIX crossfade are headers of their own, each with a harness that renders it |
| Give the rendering backends a compile check | **open** - item 6 |
| Drive `processBlock` with real buffer shapes | **open** - item 5 |
| Say what a mono instance reads, or change it | **open** - item 10 |
| Make translation coverage a number | **open** - item 9 |
| An index of what the suite guarantees | **done** - [tests/README.md](../tests/README.md) |
| A fast compile gate, and ccache | **open** - item 2 |

## Limits of this plan

- The plugin cannot be built or run in the environment this was written in (one
  CPU, 2 GB, no CMake, no ALSA headers), so every claim about runtime behaviour
  comes from a harness that renders the shipping code or from the CI logs.
  Items 5, 6 and 7 are proposals, not measurements.
- The licence section states what the components' own repositories publish and
  what the build currently links. It is not legal advice, and the `chowdsp_utils`
  commercial option is a conversation with that library's author, not a
  checkbox.
- The new ideas are reasoned from what the engine already contains; none has been
  prototyped, and the ones that touch the audio path (mid/side, per-band drive)
  would each need their own harness section before they could be trusted.
- This plan is derived from [plugin-analysis.md](plugin-analysis.md), which dates
  from the same day. Re-measure before re-planning: Appendix A of that document
  has the commands.
