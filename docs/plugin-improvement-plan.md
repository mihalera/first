# Plugin improvement plan

> A working list, not a roadmap: each item says what to change, what it costs, and
> what it pays for. It is grounded in what the CI logs and the test suite actually
> say, so the evidence for every claim is a run or a file in this repository rather
> than an opinion about plugin development.

## What this plan is based on

- **The CI logs.** Twelve runs on `fix/processor-compile` and ten on `main` were
  read job by job. Three findings came out of them and are already fixed on this
  branch: the five-way build failure on `main` (`field 'neuralL' has incomplete
  type 'NeuralStage'`, run `38033815073`, which is what this branch is named for),
  the DSP benchmark job's missing `<array>` (`bench.cpp`, found by the benchmark
  job itself the moment `RateBridge` joined the extraction), and the two loudness
  harness failures the branch's own CI is still red on (runs `38293117361` and the
  `build-linux-debug` job beside it), which turned out to be two real defects in
  the shipping meter - see below.
- **The test suite**, harness by harness, because what it does not reach is what
  this plan is mostly about: `tests/{dsp,layout,state,transport,pitch,neural}`
  plus the text checks in `tests/dsp/extract.py`, `tests/presets`,
  `tests/theme` and the new `tests/translations`.
- **Two defects this session's own work turned up**, because they say something
  about which parts of this codebase are load-bearing and least watched:
  - the K-weighted loudness meter ran its two channels through one pair of filter
    state variables, and `reset()` - which the engine calls *after* `prepare()` -
    zeroed the coefficients as well as the history, so the meter ran unweighted
    for the whole session. Both were invisible: the readout was simply 2.4 dB and
    0.69 dB wrong.
  - the Ukrainian translation table had seventeen entries whose key no longer
    existed anywhere in the plugin, so those tooltips had silently fallen back to
    English.

## Priority 1 - things that cost on every change

### 1. Split the three big files

`Source/PluginProcessor.h` is 7,605 lines, `PluginProcessor.cpp` 7,220 and
`PluginEditor.cpp` 9,441. Everything the DSP harnesses cut out of the header - the
saturation core, the subharmonic generator, the vinyl stage, the loudness meter,
the rate bridge, the neural stage, the transient shaper - is a self-contained
struct that could live in its own header under `Source/DSP/`, and the repository
already carries `SPLITTING-BIG-FILES.md` for exactly this.

What it pays for, in order of size:

- **`tests/*/extract.py` stops cutting code by text span.** `extract_struct` finds
  a declaration by string match and reads to the next `\n};` - which is why the
  extraction order in `HEADER_PIECES` has to mirror the header's own order, and
  why adding a stage means editing a regex table. If each struct is a real header,
  the harness `#include`s it and the whole extraction layer shrinks to a build
  script. That is the single largest simplification available in the test suite.
- **Build time.** Each CI run on this branch took 15-16 minutes of wall clock
  (`38275422445`: 16m32s, `38129740123`: 15m12s) across five build jobs, and every
  one of them recompiles the editor translation unit, which includes the whole
  7,605-line header for the sake of a handful of types.
- **Review.** A 7,000-line header cannot be read; a change to `LoudnessMeter` is
  currently a diff inside `PluginProcessor.h`.

Suggested order: the DSP structs first (they are already used by the harnesses, so
the split is verifiable by the suite itself), then the editor's panel/theme types,
then `processBlock`'s stages if it is still unmanageable.

**Progress, and a correction to the paragraph above.** Nineteen headers have moved - the
scalar math bridge, then `LoudnessMeter`, `TransientShaper`, `GlueCompressor`,
`RateBridge`, `SaturationCore` (the largest, 1,000 lines, including the four voice
lists that have to travel with the struct), `VinylStage`, `SubharmonicGenerator`, the
pitch pair `PitchTracker` / `RealtimePitchShifter`, the transport pair `TransportRig`
/ `RepitchStage`, the four stages nothing measures (`InputStage`, `PlateReverb`,
`ThreeBandEq`, `HarmonicAnalyser`), then `OptionalLibraries.h`, `NeuralStage.h` and
`IrStage.h` - one header each, with their prose, and **three**
extractors
(`tests/dsp/`, `tests/pitch/`, `tests/transport/`) now *include* those headers instead
of cutting their text out of `PluginProcessor.h`. That is the payoff delivered three
times over: the pitch and transport harnesses used to cut the structs they drive by text,
and now they compile the shipping code. It is also where the split's one silent-failure
mode showed up for real: the transport extractor reads `armedForHost` out of
`PluginProcessor.h` as text, the flag moved with the rig, and the check failed with the
file it had searched named in the message - the right outcome, and the reason each of
those checks names its file. `PluginProcessor.h` itself is 1,787 lines, down from 7,605,
and the DSP extractor's header list is EMPTY: its last entry, `NeuralStage`, moved as
soon as the optional-library probe it is compiled under had a header of its own,
`Source/DSP/OptionalLibraries.h` - which is the rule this plan's own paragraph above
missed, that a header which branches on a macro has to be able to read the file that
decides it. The JUCE
header check compiles **every** file in `Source/DSP/` rather than the harnesses'
source lists, which is what gave the four stages nothing includes a compile against real
JUCE at all - coverage by convention rather than by registry. What is left in that
extractor is the .cpp half alone: the five inline shapers. The engine's own plumbing - the
amp's behaviour around its curve, the tape condition, the sample clock and the smoother
that reads it, and the conventional compressor on the finished signal - has followed it,
four more headers that no harness renders and that the JUCE header check instantiates
instead (23 headers in that directory now).

**And the editor has been split, which is the part this plan had not asked for.**
`PluginEditor.cpp` is 6,078 lines, down from 9,441: the paint kit and the page table
(`Source/GUI/PanelPaint.h`, `PanelTabs.h`), the knob table and every tooltip sentence
(`PanelText.h`), both look-and-feels (`J37LookAndFeel.cpp`), the meters
(`EditorMeters.cpp`), the interface-sound engine (`UiSoundEngine.cpp`) and the knob's
radial budget (`KnobMetrics.h`), which two files now call and which the layout harness
compiles as a header rather than cutting as text. Each move is a pure move of bytes,
with the three adaptations a header requires named in `SPLITTING-BIG-FILES.md` section
7 - `inline`, the two counts and the theme choice, and the radial budget that had to
stop being file-local. That section also records the check the split needed and did not
have: `tests/editor/check_editor_juce.sh` compiles the whole translation units and every
`Source/GUI/*.h` on its own against real JUCE, because every other harness in this
repository compiles an extracted span and would not see a moved block with a missing
include. It caught three real faults on its first run. The state harness, whose parameter table reads the same six model lists,
now looks each list up in a short list of header files rather than assuming it is in
`PluginProcessor.h` - the two-file version of the same rule. That is the first
payoff above, delivered; `SPLITTING-BIG-FILES.md` section 7 records the recipe and
what is left. The second payoff - build time - is **not** delivered by moving a header
on its own, and this plan claimed it was: `PluginProcessor.cpp` and `PluginEditor.cpp`
both include `PluginProcessor.h`, so both still parse every stage wherever it lives.
The moves are what makes the fix possible (the editor is the translation unit with the
least to do with any of these stages), but the build-time win needs the stage
implementations in per-stage `.cpp` files, or the editor's DSP access behind an opaque
aggregate, and that is a separate and much riskier step.

**The processor has been split too, and along its own seams rather than by size.**
`PluginProcessor.cpp` is 1,325 lines, down from 7,220, and what is left in it is the
processor's LIFECYCLE. Three things left it, and each is a file a reader has a reason
to open on its own: the parameter table (1,357 lines, `Source/ParameterTable.cpp`),
the preset/compare code with the saved-state format it writes
(`Source/ProcessorPresets.cpp` and `.h`, `Source/ProcessorStateFormat.h`), and the
whole signal path (3,916 lines, `Source/ProcessorSignalPath.cpp`) - including
`processTapeEngine()`, which is still one 3,366-line function and is now at least in a
file of its own. Two headers came out of the same move because the signal path and the
preparation code both call them: the five inline shapers (`Source/DSP/HotPathMath.h`,
which retired the LAST text `tests/dsp/extract.py` used to cut - that extractor now
includes every line it compiles) and the hot-path probe
(`Source/HotPathProbe.h`).

Two facts from that move are worth carrying to the next one. First, **the text checks
failed by name and the fix rode along with the move**: four extractors read this file by
anchor, and all four now read the processor's own source as one text across every file
it is split into, so a fact that moves is found where it went and a fact that ends up in
two files stops the harness. Second, **compiling the flag nobody compiles found a real
bug**: `J37_ENABLE_HOTPATH_PROBE`'s branch, turned on for the first time during the move,
held a copy assignment between two `std::atomic<bool>` - a deleted function - so the
probe the counters were declared for still did not build. The move fixed it, and
`tests/editor/check_editor_juce.sh` now compiles that branch on every run.

### 2. A fast compile gate, and ccache

The one failure class that has cost the most on this repository is the cheapest to
catch: an incomplete type, a missing include, a name that does not resolve. Run
`38033815073` shows five jobs red for one such error, each of them 8-15 minutes
into a full JUCE build. Two changes make that a minutes-long problem:

- **ccache** in the workflow. These builds are dominated by JUCE, glm and the
  rendering backends, which never change.
- **A "compile only" job** that builds the plugin's translation units with
  warnings as errors and no linking, before the platform jobs fan out. It is not a
  substitute for the cross-platform builds; it is what makes the common break
  visible in the first two minutes instead of the fifteenth.

### 3. Decide what the loudness meter is for, and write it down

The meter is a display readout today: `currentLufs` reaches `outputLufs`, and the
editor draws it. AUTO GAIN does **not** read it - the compensator measures block
power itself, a few hundred lines further down `processBlock`. Anyone reading
"K-weighted loudness" and "AUTO GAIN" in the same file will assume otherwise (this
plan's first draft did). Two options, either is fine as long as it is chosen:

- keep it a meter, and say so in the struct's doc comment; or
- make it the thing AUTO GAIN uses, at which point the harness's claims become the
  level contract instead of the display contract, and the compensator's 450 ms
  programme smoother has to be reconciled with a 400 ms loudness window.

The harness already pins what the reading is (calibration, linearity, rate
independence, the 3.01 LU channel sum, the floor, and that `prepare()` then
`reset()` leaves the curve in place), so either decision is testable from today.

## Priority 2 - the parts with no eyes on them

### 4. Extract the AUTO GAIN compensator so it can be measured on signal

It is the one gain stage in the chain that nothing measures, and it sits inline in
`processTapeEngine`'s per-sample loop (Source/ProcessorSignalPath.cpp). What could be measured if it were a struct, the
way `RateBridge` and `LoudnessMeter` are:

- that it only ever restores and never boosts (the `jlimit (0.0f, 12.0f, …)` clamp
  on a real signal, not as text);
- that a 12 dB loss is paid back within the smoother's time and without
  overshooting;
- that the correction it applies to the wet leg and to the MIX 0 leg is the same,
  so DELTA stays silent - the claim two separate comments above those lines make;
- that a stopped machine holds its last correction instead of ramping to +12 dB.

Until then, `tests/dsp/extract.py` pins all four as text, which catches an edit
that drops one and cannot catch an edit that gets the arithmetic wrong.

### 5. Give the rendering backends a compile check

`Source/GUI/NativeRenderer_{D3D11,D3D12,Vulkan,Metal}` have no automated coverage.
The failure they produce when an include goes wrong is the ugliest in the repo's
history - `expected unqualified-id before 'namespace'` cascading through fifty
lines of glm - and it is only visible in a full platform build. A job that
includes each backend's translation unit in an offscreen build (no window, no
swapchain, just the type checks) would catch it in seconds, on Linux, for all
backends, including the ones whose hardware the runner does not have.

### 6. Drive `processBlock` with the buffer shapes hosts actually hand over

The harnesses exercise the stages; none of them exercises the *boundaries*: zero
frames, one frame, a prime block size, a rate change between blocks, a plugin that
is prepared and never played, a stop followed by a start at a different rate. A
harness in the shape of `tests/state` that drives the whole chain unconditionally
and asserts "no NaN, no denormal, no output above the ceiling, DELTA silent at MIX
0" would cover the class of bug that costs a user their session and that neither
the sanitizers nor the per-stage harnesses can see.

## Priority 3 - product and prose

### 7. Say what a mono instance reads, or change it

For a mono instance the engine feeds the meter the same sample twice
(`processFrame (channelData[0][sample], channelData[0][sample], …)`), so a mono
instance reads the stereo sum - the same number a stereo instance reads for
centre-panned material. That is self-consistent, and it is nowhere stated. Either
write it into the meter's doc comment (it is a decision, and it is defensible) or
change it; what is not sustainable is that the next reader rediscovers it as a bug
and "fixes" it, which would move every mono mix by 3 LU.

### 8. Make translation coverage a number

`uk.txt` is 149 entries and `en.txt` is four keyed lookups; the new drift check
guards the keys that exist but cannot see the sentences that do not. Printing the
untranslated count per language on every run (as this plan's session shows: the
seven SOURCE knob tooltips and the new transport text are not in Ukrainian) turns
"how translated is the panel" from a feeling into a number that can be moved. A
hard gate on it would be wrong - a half-translated panel showing English is the
deliberate design - but a count nobody has to guess at is not.

### 9. An index of what the suite guarantees

Each harness header explains what it pins and why, in prose, which is the right
place for the reasoning and the wrong place to look up coverage. `tests/README.md`
with one table - harness, claims, the defect it was written for - would answer "if
I change X, what will catch me" in one screen. The prose stays where it is; this
is an index over it, not a replacement.

## If you do three things

1. **[done] Move the DSP structs out of `PluginProcessor.h` into `Source/DSP/*.h`** and let
   the harnesses `#include` them. It deletes the fragile part of the test suite,
   speeds every build, and unblocks items 4 and 6.
2. Add ccache and a compile-only gate so a missing include is a two-minute failure
   instead of a fifteen-minute one, five times over.
3. Turn the AUTO GAIN compensator into a struct and drive it on signal. It is the
   only gain stage in this plugin that nothing measures.

## Limits of this plan

- The plugin itself cannot be built or run in the sandbox this was written in (no
  CMake, no JUCE dependencies, no audio device), so every claim about runtime
  behaviour above comes either from a harness that renders the shipping code, or
  from the CI jobs. Items 5 and 6 are proposals, not measurements.
- The Ukrainian entries re-keyed in this session are written by the author of this
  plan, not by a native reviewer, and should be read by one before release. What
  the new check guarantees is that they will fire; it says nothing about how they
  read.
