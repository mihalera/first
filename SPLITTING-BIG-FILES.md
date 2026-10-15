# Splitting the big source files

An analysis of the five long files in `Source/`, what can be moved out of them, what
each move costs, and what it would have to be verified against. The two splits that
were asked for first - `createParameterLayout()` and the editor's constructor - are
already in, and their own evidence is at the end; this is the answer to "and how far
can this go?", written down so the next move does not have to rediscover the traps.

**Short version:** the three `PluginProcessor.cpp` structures that are *data* (the
parameter table, the tooltip text, the DSP stage structs) can move out with low risk
and a clear win. The two long *functions* (`processTapeEngine`, `resized()`) are
where the real reviewing pain is, and they are the ones with no CI coverage - so
they should be split last, and only after the harnesses are pointed at them.

---

## 1. What is actually in the long files

Measured, not eyeballed (`wc -l`, and a comment/code count that ignores blank lines):

| File | Lines | Comment | Code | The big items inside it |
| --- | ---: | ---: | ---: | --- |
| `Source/PluginEditor.cpp` | 9,199 | 3,209 (35 %) | 5,284 | `resized()` ~1,554 · paint-kit namespace 865 · tooltip text 571 · `timerCallback()` 541 · `drawRotarySlider()` 538 · `paint()` 366 |
| `Source/PluginProcessor.cpp` | 7,042 | 3,269 (46 %) | 3,167 | `processTapeEngine()` 3,390 · parameter-table namespace 1,259 · constructor 344 · `prepareToPlay()` 263 · `publishBlockTelemetry()` 184 |
| `Source/PluginProcessor.h` | 6,177 | 2,783 (45 %) | 2,625 | `FirstAudioProcessor` declaration 1,475 · `SaturationCore` 861 · `VinylStage` 567 · `SubharmonicGenerator` 560 · `NeuralStage` 380 · `ThreeBandEq` 292 · `InputStage` 244 · `HarmonicAnalyser` 194 · `PlateReverb` 193 · `GlueCompressor` 148 |
| `Source/GUI/TapeScene.cpp` | 2,860 | 604 (21 %) | 1,894 | scene namespace 844 (335 of it GLSL string literals) · `renderOpenGL()` 210 · `drawControlBank()` 166 · `controlIndexAt()` 110 · `paint()` 93 |
| `Source/PluginEditor.h` | 1,233 | 494 (40 %) | 595 | nothing but declarations and 24 bodies of under 20 lines each |

**Where the five stand now** - the same `wc -l`, after the splits section 7 records:

| File | Was | Now | What left it |
| --- | ---: | ---: | --- |
| `Source/PluginProcessor.h` | 6,177 | 1,787 | thirteen stages plus the engine's own plumbing - 23 headers |
| `Source/PluginEditor.cpp` | 9,441 | 2,615 | the paint kit, the page table, the knob table, the look-and-feels, the meters, the interface-sound engine, the build/theme/paint/tick, and `resized()`'s bands |
| `Source/PluginEditor.h` | 1,256 | 1,303 | the two counts and the theme choice, to the headers that use them - and back come the ten band declarations and the geometry they hand on |
| `Source/PluginProcessor.cpp` | 7,220 | 1,325 | the parameter table, the preset/state code, and the whole signal path (section 7) |
| `Source/GUI/TapeScene.cpp` | 2,860 | 2,494 | the 335 lines of GLSL string literals, to `Source/GUI/TapeSceneShaders.h` (405) |

Four files came out of `PluginProcessor.cpp` and none of them is a fragment of it: the
processor is now its lifecycle in one file, `Source/ParameterTable.cpp` (1,357), the
preset and A/B code with the state format it writes, `Source/ProcessorPresets.cpp`
(465) and `Source/ProcessorStateFormat.h`, and the signal path the file is actually
read for, `Source/ProcessorSignalPath.cpp` (3,916).

Two facts worth keeping in view. First, **35-46 % of the big files is prose** - the
notes that explain why the tape model, the layout and the preset format are shaped
the way they are. A split that keeps that prose with the code it explains does not
shrink the repository at all; it only decides which file a reader has to open.
Second, **there is no bulk data left in any of them**: the factory presets are JSON
under `Source/Presets/Factory/`, the translations are text under
`Source/Translations/`, and both reach the plugin as binary data. So nothing here is
long because it carries a table.

That leaves the real question: which of these chunks can live in another file
without breaking a promise the rest of the repository depends on.

---

## 2. What makes a move expensive in *this* repository

Four constraints, in the order they bite:

1. **The parameter table's order is state.** A saved session, a factory preset and a
   host's automation all address parameters by name, and the table builds them in a
   fixed order. Moving the group functions is safe *because nothing about the table
   changes* - which is exactly what the split that is already in was checked for
   (section 5). A move that also regroups or reorders the table is not a move.

2. **Three harnesses read these files by name and cut by anchor.** `tests/state/`,
   `tests/dsp/` and `tests/layout/` each read one source file (plus the processor
   header) and cut the shipping text out of it verbatim, asserting that each anchor
   appears **exactly once**. They compile what the *plugin* compiles, which is why
   they catch drift a copy would not - and why moving anchored text out of a file
   stops the harness with the anchor named. The same is true of
   `tests/presets/check_presets.py`, which reads the registered ids out of
   `Source/PluginProcessor.cpp`. None of these is a blocker; each is a one-line
   change that must ride along with the move.

3. **The DSP structs are compiled twice, under different macros.** The DSP harness
   compiles seven of them out of the header - `SaturationCore`, `VinylStage`,
   `SubharmonicGenerator`, `NeuralStage`, `GlueCompressor`, `LoudnessMeter`,
   `TransientShaper` - with `J37_DSP_HARNESS` defined and no `RTNeural`/`chowdsp`
   present, so several members reduce to pass-throughs. The `__has_include` probe
   block at the top of `PluginProcessor.h` is what makes that honest. Move one of
   those to a new header and the new header needs that probe (or an include that
   carries it), or the harness stops compiling - loudly, which is the good case.

4. **The audio path is not covered.** `processTapeEngine()` is not compiled by any
   harness; the DSP harness renders the *stages* it cuts and builds its detector
   coefficients "the way `processTapeEngine` builds them", i.e. a replica.
   `resized()` was better off - the layout harness cuts ten spans out of the editor's
   own text and compiles them, so its arithmetic is measured at four window sizes - but
   the rest of the function was checked by looking at the window. That asymmetry is
   what step G's split was built on (section 7): the spans it cuts are the reason the
   bands had to move as TEXT rather than be retyped, and the ten byte-identical `.inc`
   files are what says they did. `processTapeEngine()` has no such fragment to stand
   on, which is why it is still whole.

---

## 3. What can move

Ranked by benefit per unit of risk. Line ranges are from the tree the analysis was
written against; what has since been done is marked, and section 7 has the evidence.

**A. The parameter table -> `Source/ParameterTable.cpp` (DONE - low risk, biggest win).**
`PluginProcessor.cpp:1151-2410` is the namespace holding the 23 group functions
(1,259 lines). The groups are already internal linkage, and `createParameterLayout()`
is declared in the class, so the whole namespace *plus* that function can move as
one unit with no new header and no change to `PluginProcessor.h`. Costs: point
`tests/state/extract.py` and `tests/presets/check_presets.py` at the new file (or at
a list of files - see step 1 of the plan); add one line to `target_sources`. Risk:
low. This is the split already half done: the groups exist, and the state harness
compiles them and counts the 109 parameters they build.

**B. The tooltip text -> `Source/GUI/PanelText.h` (DONE - low risk, moderate win).**
`PluginEditor.cpp:3696-4266`: the `ControlSpec` table (one row per knob) and
`parameterTooltipText()` (431 lines of sentences). It is read by two call sites and
by nothing else, it is pure data, and neither the layout nor the DSP harness touches
it. Costs: one line in `target_sources`; if the `ControlSpec` table moves too, the
editor's debug cross-check against `tabSpecs` has to see the declaration, so give
the file a small header or leave the table where it is.

**C. The paint kit -> `Source/GUI/PanelPaint.h` + `PanelTabs.h` (DONE - low risk).**
`PluginEditor.cpp:181-1045` is the 865-line anonymous namespace that draws the
vintage furniture: `UiPalette` and the palette tables, `drawPanel`,
`fillCylinderBarrel`, `fillLitDome`, `shrinkingFont`, `tabsCoverAllControls`. It is
referenced by `paint()`, by the two look-and-feels and by the meters, and - checked -
not by `PluginEditor.h`, so the header only needs to include whatever the new
header declares. The radial-budget namespace right after it (line 1046) is cut by
the layout harness and should stay put. Risk: low.

**D. The DSP structs -> `Source/DSP/*.h`, one per stage (DONE - low risk, large win).**
`SaturationCore` (861), `VinylStage` (567), `SubharmonicGenerator` (560),
`NeuralStage` (380), `ThreeBandEq` (292), `InputStage` (244), `HarmonicAnalyser`
(194), `PlateReverb` (193), `GlueCompressor` (148) - about 3,600 lines of header that
would take `PluginProcessor.h` from 6,177 to roughly 2,600. They are self-contained
header-only structs. Five of the nine (`SaturationCore`, `VinylStage`,
`SubharmonicGenerator`, `NeuralStage`, `GlueCompressor`) are compiled by the DSP
harness today, so a broken move there fails in CI rather than in a session; the other
four are covered only by the plugin build, which is a reason to start with the five.
Costs: constraint 3 above (the probe block must travel or be shared - which is what
`Source/DSP/OptionalLibraries.h` ended up being for), and `tests/dsp/extract.py`'s
`SOURCE_HEADERS` must name the new file. Do them **one
struct per commit**, harness in hand. Risk: low mechanically, but this is the
most-edited header in the project, so keep the diffs pure moves.

**E. The shader sources -> `Source/GUI/TapeSceneShaders.h` (low risk, small win).**
335 lines of GLSL string literals inside `TapeScene.cpp`'s scene namespace. They are
data, nothing cuts them, and the file is the least comment-heavy one under `Source/`
outside the small backends (21 %). Risk: very low; win: the scene namespace drops to
~500 lines of geometry and constants. Not worth doing on its own, worth folding into
any other `TapeScene` work.

**F. `processTapeEngine()` -> named stage functions (high risk, the real prize).** It
has moved to a file of its own (`Source/ProcessorSignalPath.cpp`, section 7) and is
still ONE function there: that half is done, this is the half that is not.
3,390 lines (1,368 of them code) doing the whole signal path: input trim and glue,
the saturation blend, transport and wow/flutter, the EQ pairs, the second glue
stage, the output stage, telemetry. Splitting it does **not** need a new file: a
sequence of private member functions - or, better, of stage structs that the DSP
harness can compile - in the same file would do. This is the single change that
would most improve the project's readability, and the one no harness currently
covers (constraint 4). If it is done, do it one stage at a time, and put the stage
in the header as a struct so the harness can render it before and after.

**G. `resized()` -> one function per band (medium risk, large win).**
~1,554 lines (766 code) placing every control of a page-dependent panel. A
`placeDeck()`, `placeKnobStrip()`, `placeMemberRow()`, `placeLists()` split would
make it reviewable. Costs: all six of the layout harness's fragments are cut *from
inside this function* (the grid, the member band, the knob count, the solver, the
member row's top edge and the placement), so every anchor has to be re-pointed in the
same commit - and re-anchoring them is the point at which someone could quietly
weaken the harness, so move the anchors with the code and show the harness output
before and after.

**H. `PluginEditor.h` (1,233 lines) -> leave it.** It is declarations and small
inline bodies; splitting it would mean a `-inl.h` for no reader's benefit.

---

## 4. What not to do

- **Do not split to a line target.** A "no file over 1,000 lines" rule would push the
  rationale comments away from the code they explain, which is the one thing this
  codebase does better than most. The goal is a file a reader can hold in their head;
  that is a different number in every one of these files.
- **Do not move code the extractors cut without re-pointing them in the same
  commit.** They abort rather than measure something else, so CI will catch it - but
  the fix belongs in the change that caused it, not in a follow-up.
- **Do not reorder or regroup the parameter table** while moving it. Order is state
  (constraint 1). This is the rule the current split was verified against.
- **Do not copy a stage instead of moving it.** A duplicated struct or formula is
  exactly the drift the harnesses exist to catch, and they cannot catch it in code
  they do not cut.

---

## 5. The two splits that are already in, and their evidence

**`createParameterLayout()` -> 23 group functions** (`PluginProcessor.cpp`, with the
banner note at line 1151). Verified against the parent revision:

- all **109** `layout.add (...)` registrations are byte-identical and in the same
  order (comment- and whitespace-normalised text, `layout.add` scanned to its
  matching parenthesis);
- the constructor of the table now calls the 23 groups in the order the parameters
  were declared in; there are no groups defined and never called;
- the group bodies concatenated reproduce the old single body statement for
  statement, with exactly two differences: an unused local
  (`eqOrderRange`) that no longer had a reader, and the inline `percentageRange`
  lambda, which became a namespace-scope function with an identical body.
- `tests/state/extract.py` cuts those groups out by name and counts the parameters
  they build, so a group that stops being extracted fails the round-trip harness
  instead of quietly shrinking its table.

**The editor's constructor -> 23 phases** (`PluginEditor.cpp:4268`, declared under the
"constructor's phases" rule in `PluginEditor.h`). Verified against the parent
revision: the old constructor's 1,582 statements are accounted for - the phase bodies
carry them in the same order, 488 statements are the two data tables that moved to
file scope (`ControlSpec` and the palette/table rows), and 2 are the tooltip lambda
that became the free function `parameterTooltipText()` (its 413-statement body is
byte-identical to the lambda's, and it captured nothing).

This one has a guard in CI now, because its failure mode is silent - a phase that is
declared, defined and never called still compiles and simply leaves its controls
unbuilt. `tests/layout/extract.py` reads both ends (the header's phase block and the
constructor's calls, which it brace-matches through comments and string literals) and
fails on a phase that is missing from the header, called twice, never called, defined
twice, or called out of the declared order. `tests/layout/run.sh` prints the count it
found (`constructor phases: 23 declared, defined once and called once, in the
declared order`).

Both splits, and everything else in `Source/`, are covered by the harnesses as they
stand: DSP regression, knob layout, saved-state round trip (39 checks) and the preset
table (29 presets, 101 of 109 parameters set, 12 deliberately excluded) - each of the
three harnesses also run under `ASan` + `UBSan` in `build-linux-debug`, which is what
those runners' `J37_SANITIZE` switch is for.

**How the "pure move" claim was checked, so it can be checked again.** Both
verifications compare the working tree against the parent commit
(`git show <rev>:<path>`), which is why they are not in CI - a CI checkout has no
guarantee of the previous revision. Each is a short Python pass over the two texts
with comments and string literals blanked out: statement lines normalised and
compared as sequences (the parameter table and the constructor), or every
`layout.add (...)` scanned to its matching parenthesis and compared in order (the
table). Re-running them takes seconds and needs nothing but `git` and `python3`.

---

## 6. The order to do it in

Each step is independently revertable and ends with the same checks green.

1. ~~**Teach the extractors to search a list of files**~~ - done as the moves needed
   it (`tests/dsp/`, `tests/state/`, `tests/pitch/`, `tests/transport/`), and
   `tests/layout/extract.py` is the last one to have been given the same treatment:
   it reads the paint routine and the radial budget out of the files that have them.
2. ~~**A - the parameter table** to `Source/ParameterTable.cpp`~~ - done, with the
   preset and state code that indexes it and with the signal path on the back of the
   same move (section 7). Verify: the state harness (109 parameters, 39 checks), the
   preset check, the DSP harness, the transport harness and the two JUCE checks.
3. ~~**B and C - tooltip text and the paint kit** out of `PluginEditor.cpp`~~ - done,
   as `Source/GUI/PanelText.h`, `PanelPaint.h` and `PanelTabs.h`. Verify: layout
   harness, plus the new compile check (section 7).
4. ~~**D - the DSP structs**~~ - done: thirteen stages and the engine's own plumbing
   are headers under `Source/DSP/`, and nothing is left in `PluginProcessor.h` for a
   harness to cut. Verify: DSP harness and the JUCE header check after each.
5. ~~**E - the shaders**~~ - done, and it is the cheapest move in the document: the 335
   lines of GLSL string literals left `Source/GUI/TapeScene.cpp` for
   `Source/GUI/TapeSceneShaders.h`. They are data -
   nothing cuts them, nothing includes them but the scene, and no harness has to move.
   Verify: the editor JUCE check, which compiles `TapeScene.cpp` as a whole translation
   unit, which is the one check that sees the GLSL at all.
6. **F and G - the long functions**, last, one stage per commit, and only with the
   harnesses extended to compile the stage that moves. **G is done** (section 7):
   `resized()` is 22 lines and the bands it was made of are member functions. **F is
   HALF done and the easy half**: `processTapeEngine()` is in a file of its own (step 2),
   still one 3,367-line function, which is the part that needed no new measurement -
   splitting it into named stages is what is left, and it is the one change in this
   document that no harness compiles.

   **F's first two stages are in, and then its third** (section 7): the machine's
   block-rate coefficients and its wander (the four shaping curves, the SPEED/platter
   scale and the two LFO rates) left the function for
   `Source/DSP/BlockRateCoefficients.h` and `Source/DSP/TapeModulation.h`, both checked by
   the DSP harness rather than only compiled by it; and the map's parts 1-3 are one TYPE
   and one FUNCTION now - `TapeBlockState` holds the 68 names the sample loop read by
   capture, and that loop is `processOneSample (TapeBlockState&, int)`. What is left of F
   is the map's part 2: cutting the ~1,570 lines of derivations into named stages of
   their own, which is the move the two stages above already made and can now be done one
   group at a time.

   **F's map, measured off the function rather than guessed** - so the next pass starts
   from the numbers instead of from reading 3,307 lines. The map was taken before either
   stage left, so its ranges run high. The function has 1,050 lines at its own indent
   level, and it is four parts, not seven:

   | Lines | What it is | Why it is a stage |
   | ---: | --- | --- |
   | 551-623 | the per-block parameter reads: ~40 `const auto` scalars from the parameter tree, each with the default a missing parameter means | one block, one snapshot: nothing below changes them, and every stage reads them |
   | 624-2,200 | the derivations "read once per block": tape geometry, tempo, delay time, mix and voicing, transient/neural, preamp/condition/reverb/vinyl, both equalisers' coefficients, MODERN mode, the guard, the transfer curves, the couplings and the glue's operating points | ~1,570 lines of coefficients, all consumed by the sample loop and by nothing else |
   | 2,203-3,872 | `processOneSample`, a lambda 1,669 lines long | the machine itself: one sample in, one sample out, and the only code that touches the audio |
   | 3,874-3,916 | the block loop, then the measurement and telemetry tail | one loop over the lambda, and one publish |

   Two things about that shape matter more than the line counts. First, the file already
   has the recipe for the parameters: `BlockContext { numSamples, activeInputChannels,
   engineSampleRate }` is built at the top and handed to `processBypassedBlock`,
   `processPostMachineStages` and `publishBlockTelemetry` - so parts 1, 2 and 4 are a
   struct with more members, not a new invention, and `processPostMachineStages` is a
   stage that has already been cut out of this very function. Second, the lambda is the
   hard half and the reason F is last: it captures `[&]`, so its 1,669 lines read the
   ~1,600 lines of block constants above it by CAPTURE rather than by argument. Cutting
   the derivations out first is what turns that capture list into a type, and until that
   exists there is nothing for a harness to compile the lambda against.

   **The map, re-measured after the three moves** (every range above was taken before any
   of them): `processTapeEngine()` is 1,639 lines (was 3,367), `processOneSample` is a
   1,670-line member function of the processor (was the lambda at 2,203-3,872), and the
   ~1,570 lines of part 2 are still inside the function but now write into
   `TapeBlockState`'s 68 fields instead of into locals. Part 4 - the block loop and the
   measurement/telemetry tail - is unchanged.

After every step: `bash tests/dsp/run.sh`, `bash tests/layout/run.sh`,
`bash tests/state/run.sh`, `python3 tests/presets/check_presets.py`, and the three
harnesses once with `J37_SANITIZE=address,undefined`.

---

## 7. Progress

Sixteen headers are out, and what matters is the recipe rather than the pieces,
because the recipe is what the rest of them follow:

- `Source/DSP/J37Math.h` - the scalar math bridge every stage calls, and the file that
  states the conventions of the directory: one concern per header, the header includes
  what it needs so it can be compiled on its own, and it must not pull JUCE in. The
  harnesses compile these headers against their own stand-in for the `juce::` names a
  stage touches; a header that included `JuceHeader.h` could not be compiled there at
  all.
- `Source/DSP/LoudnessMeter.h`, `Source/DSP/TransientShaper.h`,
  `Source/DSP/GlueCompressor.h`, `Source/DSP/RateBridge.h` - one stage each, with
  their prose, moved whole. Not retyped: the block was deleted from
  `PluginProcessor.h` by matching its exact text and written to the new header by
  the same text, so a typo fails the move rather than changing the stage.
- `Source/DSP/InputStage.h`, `Source/DSP/PlateReverb.h`, `Source/DSP/ThreeBandEq.h` and
  `Source/DSP/HarmonicAnalyser.h` - 243, 192, 291 and 193 lines of the file into 254,
  204, 304 and 204 of header, all four self-contained (juce's scalar helpers, `Decibels`
  and `AudioBuffer`, plus the std:: math). These are the first moves that no harness
  pays for: nothing cuts them, nothing includes them, and nothing would have noticed
  them rotting in the monolith either. What they earn is the file and the convention -
  each header now holds the stage and the note explaining it - and, because of the
  change below, a compile against real JUCE that they did not have before.
- `Source/DSP/TransportRig.h` and `Source/DSP/RepitchStage.h` - 393 and 237 lines of
  the file into 403 and 252 of header, the transport harness's pair, and the second
  move that takes a harness with it: `tests/transport/extract.py` cut both structs by
  text and now includes them, with `-I Source` in its `run.sh` and the two headers
  added to `check_headers_juce.sh`'s list (which reads all three extractors now, twelve
  headers). This one has a wrinkle the others did not: the extractor also reads the
  rig's `armedForHost` flag and the rule that sets it as TEXT, from `PluginProcessor.h`,
  because they are half of a promise whose other half is in the engine - and they moved
  with the rig. The check said so ("PluginProcessor.h does not declare `armedForHost`")
  the moment the move landed, and it now reads them out of `Source/DSP/TransportRig.h`
  instead. A text check that silently passes because its subject moved away is the
  failure this is worth guarding against, which is exactly why it names the file it
  read.
- `Source/DSP/PitchTracker.h` and `Source/DSP/RealtimePitchShifter.h` - 285 and 189
  lines of the file into 301 and 205 of header, the pair the pitch harness drives. They
  are the first move to take a second harness with it: `tests/pitch/extract.py` cut
  both structs by text and now emits `#include "DSP/..."` instead (its own
  `SOURCE_HEADERS`, and `tests/pitch/run.sh` gained the `-I Source` that makes those
  includes resolve), so the harness that measures the detector and the shifter
  measures the shipping headers. `tests/dsp/check_headers_juce.sh` now reads both
  extractors' lists - which is why it went from eight headers to ten without anyone
  adding them twice.
- `Source/DSP/VinylStage.h` - 581 lines of header for 566 of the file. The stage
  holds nothing but its own state and reaches for `juce::jlimit` and the std:: math,
  so this is what a move costs when nothing has to travel with it: the include is the
  whole plugin-side change. It is also the one that says something about the harness
  rather than the plugin - nothing in `tests/dsp/subfund_test.cpp` names `VinylStage`
  any more (the extraction was kept for the noise tab's controls, and whatever check
  that was drives the chain rather than the struct), so the entry became a plain
  include. A check that wants the stage again finds the shipping text.
- `Source/DSP/SubharmonicGenerator.h` - 573 lines of header for 559 of the file, and
  the last stage the DSP harness drives to move. `tests/dsp/subfund_test.cpp` renders
  it (the divider series, the undertone ordering, the mix direction, and that
  silence stays silent), so this is the move that turns the harness's most-driven
  stage from a cut copy into the shipping header - with the same `SOURCE_HEADERS`
  include and one line out of `HEADER_PIECES`.
- `Source/DSP/SaturationCore.h` - the largest piece so far, 1,001 lines of header
  for an 860-line struct, and the first move that had to take something else with
  it: the four type lists (`valveTypeNames`, `ampTypeNames`, `transformerTypeNames`,
  `digitalTypeNames`) that voice its non-tape principles. They are not decoration,
  they are the reason the struct can compile at all - `setValveVoice` and its three
  siblings read the counts those lists are built from, and a header that left them
  behind would compile in the plugin (where they still live above it in
  `PluginProcessor.h`) and fail in every harness. The fifth list, `vinylTypeNames`,
  stayed behind: it voices `VinylStage` rather than the core.
- `Source/DSP/NeuralStage.h`, with `Source/DSP/OptionalLibraries.h` in front of it -
  595 lines of the file into headers of 480 and 183, and the move that finally emptied
  the extractor's header list. It needed the probe first, exactly as recorded above: the
  stage is compiled in two shapes under `J37_HAS_RTNEURAL`, and that macro was decided
  by a `__has_include` block at the top of `PluginProcessor.h` - so a header could not
  read it, because a header cannot depend on its includer having got there first. The
  whole optional-dependency block therefore left the plugin header: chowdsp's two
  probes, RTNeural's, xsimd's, and the four third-party utilities' (ReaderWriterQueue,
  farbot, signalsmith-stretch, SoundTouch), all of them now in one file whose concern is
  "which of these does this build have". It is included first by `PluginProcessor.h`, and
  directly by the stage that needs one of its answers - which is also the only way a
  harness with none of those libraries on its include path can compile that stage.

  One guard changed with the move, and the reason is worth keeping: the RTNeural probe
  now yields to a macro that is ALREADY defined. `tests/neural/run.sh` passes
  `-DJ37_HAS_RTNEURAL=1` and stands RTNeural's own API in itself, which is the only way
  the guarded half of the stage is compiled on a machine without the library. Before the
  move that define met no probe at all; after it, a probe that insisted would have
  switched off the very half that harness exists to compile, and the harness would have
  gone on passing while measuring the pass-through. The other three probes are
  unchanged - same `__has_include` chain, same macros, and under a harness with none of
  those libraries on the path, the same all-zero outcome as before.
- `Source/DSP/IrStage.h` - 138 lines of the file into 169 of header, and the first move
  where the cost landed on the JUCE check rather than on the plugin. This is the header
  that leans hardest on the directory's second convention (a header here does not
  include JUCE itself, because a harness compiles it against a stand-in with no JUCE on
  its include path): what it uses is `juce::dsp::Convolution`, `ProcessSpec`,
  `AudioBlock` and `ProcessContextReplacing`, plus `AudioFormatManager` /
  `AudioFormatReader`, `File` and `AudioBuffer` - so its includer has to provide
  juce_dsp and juce_audio_formats, which inside the plugin is `JuceHeader.h` and under
  the check is now the script's own module includes. No harness renders this stage, so
  the check is its only automated look, and the glob is what made the requirement show
  up as a compile error rather than as a surprise in a platform build.

`PluginProcessor.h` went from 7,605 lines to 6,932 in the first pass (`RateBridge`,
194 lines, closed it), to 5,951 with `SaturationCore`, to 4,835 with `VinylStage` and
`SubharmonicGenerator`, to 4,368 with the pitch pair, to 3,744 with the transport pair,
to 2,838 with the four stages nothing measures, to 2,262 with the neural stage and
the probe block that had to travel in front of it, and to 2,130 with the IR stage -
nine moves, 5,500 lines, in nineteen headers. The more useful result is in the harness:
`tests/dsp/extract.py` no longer cuts those out by string span. It emits
`#include "DSP/..."` lines (`SOURCE_HEADERS`), and the harness then compiles the
shipping header itself, so there is one fewer place for a test copy and the plugin to
drift apart. What is still cut below that list in the extractor is what has not moved.

The move also needed a new guard, because it changes what the harness's green light
means. The harnesses compile these headers against their own stand-in for `juce::`,
and until the stages moved, every `juce::` name in them had already been compiled by
the plugin. Now a header can satisfy the stand-in and fail the plugin: the stand-in
declares what a stage touches, which is not the same as real JUCE declaring it.
`tests/dsp/check_headers_juce.sh` (a step in the `dsp-tests` job) compiles the same
headers against real JUCE - `juce_audio_basics` (and `juce_core` through it) for the
scalar helpers, `jmin`, `jmax`, `jlimit`, `MathConstants`, `Decibels`, `SmoothedValue`
and `StringArray`; `juce_audio_formats` for the IR stage's `AudioFormatManager` and
`AudioFormatReader`; and `juce_dsp` for its `Convolution`, `ProcessSpec`, `AudioBlock`
and `ProcessContextReplacing` - instantiates each stage's public surface, and reads its
header list from the directory. It is `-fsyntax-only`: no window, no device, no JUCE
build, two seconds.

**Its list is the directory, not a list.** It began by reading the harnesses' own
`SOURCE_HEADERS` lists, which had one hole: a header that moved into `Source/DSP/` with
no harness that includes it - which is what `InputStage`, `PlateReverb`, `ThreeBandEq`
and `HarmonicAnalyser` were - would have been compiled against real JUCE by nobody. It
now compiles every `*.h` in the directory, so the check follows the convention (state
your concern in a header, include what you need) rather than a registry, and a header
that moves in is covered on the next run without being named anywhere. `IrStage` is
that rule paying off a second time: it moved in, its surface needed two modules the
check did not have, and the answer was the two modules (`juce_dsp`, `juce_audio_formats`)
rather than a shorter header list. The negative side is checked the same way the other
headers' is - with the `juce_dsp` include removed from the generated translation unit,
the compile fails on `Source/DSP/IrStage.h`'s own declarations (`'dsp' in namespace
'juce' does not name a type`), so the module is doing real work and the stage is
really being compiled.

Three more things the `SaturationCore` move changed, all of them in the direction of
the checks covering the shipping text rather than a restatement of it:

- **`CONSTANT_PIECES` is gone.** `tests/dsp/extract.py` used to paste the four type
  counts in ahead of the struct, because cutting the struct alone delivered a method
  that read a constant which had not arrived (that is how a new DIGITAL voice broke
  the harness once). The counts travel with the struct now, so a constant that goes
  back to being cut alone is a compile failure instead of a silent zero.
- **The state harness searches two files.** `tests/state/extract.py` cuts the six
  model lists, and four of them moved. Each is looked for in every file of a short
  list - `PluginProcessor.h` and `Source/DSP/SaturationCore.h` - and taken from the
  one that has it, with that file named in the generated preamble. A piece found in
  neither file, or in both, stops the build with the piece named.
- **The DSP shim grew a `StringArray`.** The four lists build one
  (`...TypeNameList()`), and an inline function's body is compiled whether or not
  anything calls it. It is a stand-in that cannot be called meaningfully, which is
  exactly why the JUCE check above now instantiates the four lists against the real
  `juce::StringArray` - the shim can no longer be the only thing that has seen them.

**The wiring cost something, and the first CI run said so.** Two failures, neither of
them in the moved code: `tests/dsp/run_bench.sh` compiled the extracted include without
`-I Source`, so the benchmark could not find `DSP/J37Math.h` (`7:10: fatal error` in job
`115341904730`), and the JUCE header check ran in the `dsp-tests` job, which checked out
without submodules and therefore had no `JUCE/modules` to compile against. Both are
fixed - the benchmark gets the include path the other harnesses already had, and that
job now takes the submodule (`submodules: recursive`, a depth-1 fetch of the one
submodule, seconds). Worth writing down because it is the class of cost this split
carries: every new include path is a new place for a build to look somewhere else.

**The DSP extractor cuts no header text at all.** `HEADER_PIECES` went with its last
entry, which is the goal its docstring stated when the first header moved; what it used
to do is one line per stage in `SOURCE_HEADERS` now. The check that reads the stage's own
state went with it, the way the transport's `armedForHost` check had to: the three
atomics the rate bridge is handed (`wantedModelRate`, `wantedEngineRate`, `ratesPending`)
live in `Source/DSP/NeuralStage.h`, so `tests/dsp/extract.py` reads that file now -
through a named constant, `STAGE_HEADER` - and says WHICH file it read when it finds
nothing. It failed the moment the stage moved and the check was still pointed at
`PluginProcessor.h`, which is the behaviour that makes it worth having: a text check
that passes because its subject moved away is the failure this guards against, and it is
the second time this branch has hit it.

What is left in the extractor is `IMPL_PIECES`: the five inline shapers (`j37Tanh`,
`softClip`, `softKneeReductionDb` and the two one-pole coefficient helpers) still cut out
of `PluginProcessor.cpp`, which is the same recipe applied to the .cpp side.

**The engine's own plumbing followed, and that finished the header.** Four more blocks
left `PluginProcessor.h`: `Source/DSP/AmpVoicing.h`, `TapeCondition.h`,
`SampleSmoother.h` (the sample clock and the smoother that reads it, one file because
they are one mechanism) and `StandardCompressor.h` - 94, 114, 109 and 42 lines of the
file into 107, 129, 125 and 55 of header. They are not stages, they are the engine's
furniture - the amp's behaviour around its curve, how worn the machine is, the clock a
coarse ramp is measured against, and the conventional compressor on the finished signal
- which is why they were last: none of them is rendered by a harness, so their only
automated look is the JUCE check, which now instantiates all four (that is what took it
from nineteen headers to 23). The one thing worth recording is a trap that did **not**
fire: `tests/dsp/extract.py` reads the MIX smoother's inversion flag by regex from
`PluginProcessor.h`, and it kept working, because what it reads is the *member's* own
declaration (`SampleSmoother mixSmoothed { ... }`) and a member of the processor stays
where the processor is. The flag travelling with the class would have needed the regex
re-pointed; it did not travel, so nothing was re-pointed, and the check is still reading
the shipping declaration rather than a restatement of it. `PluginProcessor.h` is **1,787
lines**, down from 7,605, and has nothing left in it that a stage owns.

**The editor was split next, into six files.** `PluginEditor.cpp` went from 9,441 lines
to 6,078, and what left it did so in five checked moves, each a pure move of bytes with
every replacement asserted to apply exactly once:

| New file | Lines | What it holds |
| --- | ---: | --- |
| `Source/GUI/PanelPaint.h` | 602 | the three palettes, the drawing helpers, the two formatters, and the melatonin_blur probe they use |
| `Source/GUI/PanelTabs.h` | 353 | the page table and its three lookups |
| `Source/GUI/PanelText.h` | 594 | the knob table (`ControlSpec`), the sentence every parameter shows, and `controlCount` |
| `Source/GUI/J37LookAndFeel.cpp` | 1,213 | both look-and-feels, and the combo inset they share |
| `Source/GUI/EditorMeters.cpp` | 596 | the two loudness meters, the compressor meter and the MIX bar |
| `Source/GUI/UiSoundEngine.cpp` | 178 | the interface-sound engine and the four constraints it is designed under |
| `Source/GUI/KnobMetrics.h` | 82 | the knob's radial budget, which two files now call |

Three kinds of cost showed up, and each is a fact about headers rather than about
tidiness:

- **`inline`.** Names in the editor's anonymous namespace were one copy per translation
  unit with internal linkage. In a header that is still legal and quietly wrong:
  `liveThemeChoice` would be a *different object* in each of them, so the theme a
  look-and-feel sets would not be the one the panel reads. Every definition in the
  moved spans gained `inline`, and the two `constexpr` tables that a `static_assert`
  reads stayed `constexpr` (`inline constexpr`) - a static_assert needs the definition,
  not a declaration in a .cpp.
- **Two nested names left the class they were nested in.** `ThemeChoice` moved out of
  `J37LookAndFeel` to sit beside the palettes that select on it (a palette cannot be
  nested inside the look-and-feel that paints with it), and `principleCount` and
  `controlCount` moved into the headers whose tables are sized by them. `ThemeChoice` is
  `j37panel::ThemeChoice` at every site now.
- **The radial budget had to stop being file-local,** because two files call it: the
  editor's own strip solver and `drawRotarySlider`. It is `Source/GUI/KnobMetrics.h`,
  and it is deliberately JUCE-free - it names `juce::jmin`, `jmax` and `jlimit` and
  relies on its includer declaring them - because `tests/layout` compiles it with no
  JUCE at all. That is the convention `Source/DSP/J37Math.h` states, applied to a GUI
  helper, and it is what let the harness stop cutting text: `metrics.inc` pasted into
  the harness is now `#include "GUI/KnobMetrics.h"`.

**A new check, because none of the existing ones could see any of this.** The harnesses
compile extracted *spans* - the knob arithmetic, the transport rig, a saved state -
never a whole file, so a block that moves to a new .cpp and forgets an include is
textually perfect and does not compile. `tests/editor/check_editor_juce.sh` compiles the
whole translation units instead: every `.cpp` under `Source/` that includes the
processor or the editor (five now, found by that rule rather than listed), plus every
`*.h` under `Source/GUI/` **on its own** - a header that only compiles when the editor's
was included first is a header with a hidden dependency, which is exactly what a split
produces. Real JUCE, `-fsyntax-only`, no CMake, no window, eighteen seconds. It found
three real faults the moment it ran: the new `.cpp` files were handed `principleCount`
inside the namespace when it is declared above it, `PanelText.h` needed `controlCount`
from the editor's header, and a header that names JUCE without including it needed the
module header in front of it in the generated TU.

**`tests/layout` was re-pointed in the same change, and the numbers are the proof.**
Everything it used to read out of `PluginEditor.cpp` that moved is read out of the file
that has it now, each through a named constant: the cap's divisions, the indicator's
length and paint order, and the lists' text inset from `J37LookAndFeel.cpp`, the grid's
divider from `PanelTabs.h`, and the radial budget compiled from `KnobMetrics.h`. The
harness measures the same panel: `COMP knobs 6 cols 3 rows 2 tile 329x322 | disc 151.9
(was 81.7) +86%` and `LISTS list inset 9 px (field 30, panel 1180) | 2 call site(s)`
before the move and after it, with the call-site count now asserted rather than only
printed - a re-point is the moment a harness can quietly start measuring something else,
so it is the moment to make it louder.

**The processor followed, and it is the move this document was written for.**
`PluginProcessor.cpp` went from 7,220 lines to 1,325 in seven moves, and the file it
became is the processor's lifecycle: what it is constructed with, what the host asks
it, what a session writes. What left it:

| New file | Lines | What it holds |
| --- | ---: | --- |
| `Source/ParameterTable.cpp` | 1,357 | the 23 group functions and `createParameterLayout()`, plus the four range constants they are built from |
| `Source/ProcessorSignalPath.cpp` | 3,916 | `processBlock`, the transport commands, bypass/post-machine, autotune, telemetry and `processTapeEngine()` |
| `Source/ProcessorPresets.cpp` | 465 | the full-state undo action and the two functions it swaps through, the factory preset table, and the preset/compare code |
| `Source/ProcessorStateFormat.h` | 87 | the saved-state marker, its migration and `captureState()` |
| `Source/ProcessorPresets.h` | 30 | the two `FactoryPresets` entry points the constructor's debug audit calls from above the table |
| `Source/DSP/HotPathMath.h` | 175 | the five inline shapers, which the signal path and the preparation code both call |
| `Source/HotPathProbe.h` | 113 | the four probe counters and the readout, behind the flag no build sets |

Three kinds of cost, and each is the same kind of fact the editor's split produced:

- **The text checks failed, loudly, and that is the point.** Four extractors read this
  file by anchor, and each named its own subject the moment the move landed: `tests/dsp`
  ("the AUTO GAIN compensator's `smoothedCompensationDb = 0.0f` appears 0 times"),
  `tests/transport` ("the wet leg of the MIX crossfade matches 0 places in
  PluginProcessor.cpp"), `tests/presets` (the id list) and `tests/state` (the range
  constants, the saved-state block and `createParameterLayout`). Every one of them
  aborts rather than measuring something else, so the fix could not be forgotten - and
  all four now read the processor's own source AS ONE TEXT, each file under a marker
  naming it, so the next fact that moves is found where it went and a fact that ends up
  in two files stops the harness instead of matching whichever came first.
- **The two shared helpers had to become headers, and they are the argument for the
  whole convention.** `Source/DSP/HotPathMath.h` is `inline` FUNCTIONS (pure, so two
  copies would be harmless) and `Source/HotPathProbe.h` is `inline` VARIABLES (not
  harmless at all: the signal path writes the counters and the processor's telemetry
  readout reads them, so a copy per translation unit would be a probe that reads zero).
  The shapers also retired the last thing `tests/dsp/extract.py` CUT rather than
  included - `IMPL_PIECES` is gone, the harness includes the header now, and the file it
  cuts no text from the plugin at all.
- **`Source/ProcessorStateFormat.h` is deliberately still an anonymous namespace**, and
  the reason is in its banner: that is the linkage the four names had in the .cpp, and
  all four are pure functions and constants, so two copies cannot disagree.
  `liveThemeChoice` in `Source/GUI/PanelPaint.h` is the counter-example that forced
  `inline` there - it was an OBJECT two files had to share.

**The move found a real bug in the flag nothing compiles.** `J37_ENABLE_HOTPATH_PROBE`
appears in no build and no CI job, and the comment above the counters says they were
declared because turning the documented flag on was "a guaranteed compile error". It
still was: `hotPathThisBlockActive = hotPathThisSampleActive;` is a copy assignment
between two `std::atomic<bool>`, which is a deleted function. It was found by compiling
the flag's own branch during the move, and fixed with the explicit load/store. Both
checks now cover it: `tests/editor/check_editor_juce.sh` compiles every TU that includes
`HotPathProbe.h` a second time with `-DJ37_ENABLE_HOTPATH_PROBE=1` (the list found by
that rule, not written down), and it was verified by putting the bug back - the check
fails on it with `use of deleted function ... operator=(const std::atomic<bool>&)`.

**The evidence.** Two things were checked, and neither is a green build:

- **Nothing was lost, which is what "pure move" has to mean.** The line multisets of
  the files before and after were compared, comment and code lines together with their
  counts. Across the seven moves the only lines that disappeared are the three that
  described an include list that no longer exists, one comment sentence ("at the bottom
  of this file" -> "header"), two redundant `#if`/`#endif` pairs and the two lines of the
  atomic bug above; everything added is a banner, an include, a `#pragma once` marker or
  an `inline`. No moved line was retyped, and no line of code was added except the fix.
- **Every harness and both JUCE checks, with the numbers.** `tests/dsp`, `tests/layout`,
  `tests/state` (109 parameters, 39 checks), `tests/pitch`, `tests/neural`,
  `tests/transport`, `tests/presets` (29 presets, 101 of 109 parameters set, 12
excluded), `tests/theme`, `tests/translations` - all passed, the three
  moving-text harnesses again under `J37_SANITIZE=address,undefined`. The two compile
  checks: `check_headers_juce.sh` 24 headers from `Source/DSP/`, and
  `check_editor_juce.sh` 8 translation units (every `.cpp` under `Source/` that includes
  the processor or the editor: 5 at the top level and 3 under `Source/GUI/`, with the 3
  new processor files among them), 1 of them again with the probe flag on, and 11 of the
  plugin's own headers compiled on their own - up from 6, because that pass now reads the
  top level of `Source/` as well as `Source/GUI/`, which is the half a split of the
  PROCESSOR produces and the editor's split never did.

Every move here was made by a script rather than by hand, for the reason the two that follow
set out in detail: a move is a claim that the bytes are the same bytes, and a script asserts
that claim at every anchor instead of trusting a reader to diff four thousand lines. They cut
the parameter table, the presets and the state format; then `HotPathMath.h`, `HotPathProbe.h`
and the signal path; then the editor's build, its theme, its paint and its tick; and finally
the machine's state and its loop. They were one-off tools: each asserted that every anchor
matched exactly once, and each was re-run against its parent revision to reproduce the
working tree, which is the check that makes the claim testable. None is kept in the
repository now that the moves have landed - the record is the tables below and the commits
they arrived in.

**The editor's build, theme, paint and tick left next, and `resized()` was then taken
apart in place.** `PluginEditor.cpp` went from 6,078 lines to 2,615 in two more moves,
each a pure move of bytes with every replacement asserted to apply exactly once:

| New file | Lines | What it holds |
| --- | ---: | --- |
| `Source/GUI/EditorSetup.cpp` | 1,913 | the constructor's 23 phases: the panel's whole build, one function per part of it |
| `Source/GUI/EditorTimer.cpp` | 831 | `timerCallback()` - the telemetry it publishes and the surfaces it invalidates |
| `Source/GUI/EditorTheme.cpp` | 535 | `applyTheme()`, the palettes' application and the panel's label styling |
| `Source/GUI/EditorPaint.cpp` | 498 | `paint()` and the furniture it draws |
| `Source/GUI/TapeSceneShaders.h` | 405 | the GLSL the tape scene compiles, as data - step E, moved out of `TapeScene.cpp` |

Two harnesses had to be re-pointed with these moves, and both were made to say which
file they read rather than to go quiet:

- **`tests/theme/check_theme_wiring.py`** started from `applyTheme()` in
  `PluginEditor.cpp` and walked its widgets from there. It now reads the editor's own
  source as a LIST of files - `PluginEditor.cpp` and the four that came out of it, each
  under a marker naming it - which is the same shape the four extractors under `tests/`
  had already been given for exactly this reason. The widget count did not move (109
  widgets, 2 look-and-feels, 109 of 109 following the theme), which is the point: a
  re-point that changed what it measured would show up here as a number that moved.
- **`tests/layout/extract.py`** is now the one that reads the editor across files: the
  constructor's phases are DECLARED in `PluginEditor.h` and DEFINED in
  `Source/GUI/EditorSetup.cpp`, so the guard - every phase declared, defined exactly
  once, called exactly once, in the declared order - reads all five files as one text
  and fails if a phase is defined twice rather than measuring whichever copy it found
  first. What it still cuts by span (the strip's arithmetic) is still in
  `PluginEditor.cpp`, which is why the two halves had to be stated separately.
- **`tests/transport/extract.py` failed, loudly, and was re-pointed.** It reads the
  panel for one thing - that it asks whether START is armed
  (`isTransportArmedForHost`) - and that read is in the tick now, so
  `Source/GUI/EditorTimer.cpp`. The check did not go quiet when the text left
  `PluginEditor.cpp`: it stops the harness with the sentence that says a waiting machine
  would be called a stopped one. It reads the editor's five files as one text now, names
  all five in the failure, and was verified by running a copy with `EditorTimer.cpp`
  removed from its list - it fails on the same sentence, naming the four files it did
  read.

**The split found a real defect, and it was one the checks could not see.** The four new
`.cpp` files were left out of `target_sources(first ...)` in `CMakeLists.txt`: the plugin
target still named `PluginEditor.cpp` alone, which defines none of the build, the theme,
the paint or the tick any more - so the plugin would have failed to LINK, on the symbols
that file no longer defines. Nothing in `tests/` could see it, because
`check_editor_juce.sh` finds its translation units by RULE (every `.cpp` under `Source/`
that includes the processor's, the editor's or the tape scene's header), and that rule
compiled all four happily. The list is written by hand and the check's is read from the
tree, so the two can disagree - silently, in the direction that matters. The fix is the
four names; the guard is that the check now READS `target_sources` and fails, before it
compiles anything, if a translation unit it is about to compile is not in it. Verified by
putting the defect back (removing `Source/GUI/EditorTheme.cpp` from the list): the check
stops with `these translation units compile here but are not in target_sources in
CMakeLists.txt, so the plugin never links them: Source/GUI/EditorTheme.cpp`, and nothing
else is compiled.

**`resized()` went from 1,561 lines to 22, and the bands it was made of are functions.**
It is one call per band now - `placeHeader`, `placeDeck`, `placeControls`, `placeTabBar`,
`placeControlStrip`, `updateTabVisibility`, `placeTabMembers`, `placeMeters` - in the
order the single body had, with `verifyPanelHasNoOverlaps()` (the DEBUG sweep that used
to be its tail) still last, on the bounds every band above has just written. Three
things about it are worth keeping:

- **The layout harness's ten fragments are byte-identical before and after.** Six of them
  are cut out of the text this moved, so the way to tell a pure move from a retyped one
  is to run the extractor on both revisions and diff: `band.inc`, `band_height.inc`,
  `combo_text.inc`, `constants.inc`, `editor_layout.inc`, `grid.inc`, `knob_count.inc`,
  `member_row.inc`, `placement.inc`, `solver.inc` - all ten identical, and the harness's
  own numbers come out unchanged (`COMP knobs 6 cols 3 rows 2 tile 329x322 | disc 151.9
  (was 81.7) +86%`, `LISTS list inset 9 px (field 30, panel 1180) | 2 call site(s)`, 0
  failures). That is the check this step needed: the fragments are the panel's own
  arithmetic, and a split that retyped them would keep compiling while measuring a copy.
- **One thing changed shape rather than moving, and it is the interesting one.**
  `placeDeckSwitch` - the member band's cell arithmetic (label band 17 px, a 30 px
  control band under it) - was a LAMBDA inside `resized()`, so if it had simply moved it
  would have taken the whole function with it: every caller had to be in that one body.
  It is a member function now, and what it measures in (the strip's grid, its cell
  width, the member band's top edge) plus the page flags travel to the three bands after
  it as a `ControlGeometry` - the same idiom `EditorLayout` already used. The point is
  not tidiness: the flags and the cell width used to be locals of the same body the
  solve ran in, so nothing stopped a reader from using them above the solve.
- **The comments travelled with the code they explain.** Twenty-odd of them name
  `resized()` or `placeDeckSwitch` from other files (`PluginEditor.h`, `PanelTabs.h`,
  `EditorSetup.cpp`, `EditorPaint.cpp`, `EditorTimer.cpp`), and every one of those is
  still true after the split: the phases are still phases, `resized()` still runs from
  `setSize()` before some widgets had a parent, and `placeDeckSwitch` is still what the
  member band's SETTINGS branch fills. A split that renamed its way out of the prose
  would have been a bigger edit than the one that moved the code.

**The evidence, for the two moves together.** `tests/layout` (0 failures, and the ten
fragments byte-identical), `tests/theme` (109 widgets, 94 bodies, 2 look-and-feels),
`tests/presets` (29 presets, 101 of 109 parameters set, 12 excluded), `tests/dsp`,
`tests/state` (39 checks), `tests/transport` (after the re-point), `tests/pitch`,
`tests/neural` and `tests/translations` (1012 strings in 60 source files) - all passed.
The two compile checks: `check_editor_juce.sh` 13 translation units, 1 of them again with
the probe flag on, and 13 of the plugin's own headers each compiled on its own - up from
8 and 11, because the four new files are translation units and their headers are on the
glob; and `check_headers_juce.sh` 24 headers from `Source/DSP/`. The editor check is also
where the CMake guard above lives, so the 13 it compiles are now the 13 the plugin builds.

**Step F's first two stages are in, and they are what turns a comment into a check.**
`processTapeEngine()` went from 3,367 lines to 3,307 in two moves, each of them a stage the
DSP harness now CALLS:

| New header | Lines | What left the function |
| --- | ---: | --- |
| `Source/DSP/BlockRateCoefficients.h` | 125 | the cabinet's two poles, the SAG envelope's pair, the safety limiter's four and its ceiling, and the anti-phase guard's pole - every number the function's own comments say was rebuilt per sample rather than per block |
| `Source/DSP/TapeModulation.h` | 114 | the drive, bias, wow and flutter shaping curves, the SPEED control's scale on the platter, and the two LFO rates - the tempo lock, the anchor's clamp and the speed mapping, which are the most-checkable prose in the engine |

Both are JUCE-free and neither needed a new CMake entry (they are headers), a harness
list beyond `SOURCE_HEADERS`, or a change to anything the plugin links. Four things make
them more than moves:

- **The function's own comments said why it had to be a stage.** Two of them say the
  same thing about different coefficients - rebuilding the SAG pair inside the loop was
  "two `std::exp` per sample per channel" and the limiter's four were another four - so
  the stage is a block-rate thing, and the header makes that a property of the code
  rather than a fact about where lines happen to sit in a loop's preamble.
- **The relationships between those numbers ARE the behaviour, so the harness checks
  them.** Eight checks in `tests/dsp/subfund_test.cpp` ("9. The block-rate coefficients
  the machine's dynamics stages run on"): a coefficient is `1 - exp(-1/(tau * rate))`, so
  the SAG envelope's release must be SMALLER than its attack (slower - that asymmetry is
  what makes sag read as *give* rather than as a pump), the limiter must catch faster
  than it recovers, the anti-phase guard must be the slowest pole in the machine (a
  guard that reacted as fast as the limiter would rotate on a hard-panned transient,
  which is the fault it exists to prevent), the cabinet's resonance pole must be slower
  than its roll-off, halving the rate must move the presence pole without halving it
  (the difference between a CORNER and a per-sample constant - the unit error this
  project has already paid for twice), and the limiter's ceiling must sit exactly on
  `softClip`'s knee, which is the one relationship that ties this stage to the stage
  after it and would catch either number moving without the other.
- **The wander's claims are checked one sentence at a time.** Eight more checks in the
  same harness ("10. The machine's shaping curves and its two wander rates"): the four
  curves bend BELOW their control (a curve, not a taper) and are monotonic in it; at
  speed the platter's scale is exactly the SPEED control's own number (0.76 / 1.00 /
  1.34), which is what makes a running machine unchanged by the mechanism that models it
  winding down; a platter off its stop lowers that scale; **twice the tempo is twice the
  wander** and half the tempo is half of it (the tempo lock - the claim that is easiest
  to break and hardest to hear, because a wow rate that stopped following the tempo
  sounds merely wrong on faster tracks); the flutter rate does NOT follow the tempo,
  because the capstan is mechanical; and the tempo anchor's clamp holds at both ends,
  where a host reporting 1 BPM or 5,000 still leaves the machine wandering instead of
  stopped or buzzing.
- **Both are covered by the compile checks by EXISTING, not by being listed.**
  `check_headers_juce.sh` reads the DIRECTORY, so it went from 24 headers to 26 and both
  are compiled against real JUCE with their surface USED (the probe builds a coefficient
  set and a modulation set and sums them) - which matters here because the harness's
  shim provides the `juce::` scalar helpers and the shim cannot prove that real JUCE
  does. `tests/dsp/extract.py` names both in `SOURCE_HEADERS`, directly under
  `HotPathMath.h`, which is what puts the shipping stages in the harness's include and
  lets the checks above be about the code the plugin runs.

**The third move is the one the first two were preparing: the machine's state and its loop
have names.** `processTapeEngine()` is 3,367 lines before it and 1,639 after, and what left
the function is not a stage but the boundary the function never had. Its middle was
`const auto processOneSample = [&] (int sample) noexcept` - 1,669 lines that read 68 of the
function's own locals by CAPTURE, so nothing distinguished "what this block is set up as"
from "what happens to one sample" but a capture list that meant all of it. Those 68 names
are the fields of `TapeBlockState` now (82 lines of declaration in `PluginProcessor.h`,
grouped by what they are), the declarations that created them are assignments into it, and
the loop is `FirstAudioProcessor::processOneSample (TapeBlockState& state, int sample)
noexcept`, a 1,670-line member function whose body reaches through `state.` 157 times.

Nothing was retyped, and that is the property to keep: every statement is the same
statement, in the same order, and the only text that changed is what had to - a name
becoming a member of the state, and a declaration becoming an assignment:

    const float tapeCurve = blend (slowMachineCurve, fastMachineCurve);
    state.tapeCurve = blend (slowMachineCurve, fastMachineCurve);

That is what the cut's script did, from a field list it asserted against the function itself
before it wrote a byte. Four of its refusals are the reason this was a script and not a long
afternoon:

- **The field list has to be EXACTLY the set of function-scope names the loop reads** (plus
  `nextNoise`, accounted for separately - see below). A name that is read but is not a
  field fails at link; a field the loop never reads would be filled in and never used.
- **A name used BEFORE its own declaration line is refused.** In C++ that use is a member of
  the same name rather than the local, so renaming it would point the code at a different
  object - and it would still compile.
- **A name declared AGAIN inside the function is refused** for that same reason: inside the
  inner scope the name is a different variable and a text rewrite cannot tell the two
  apart. (`index` is the only such name, and it is not a field.)
- **Only code is rewritten.** Comments and string literals are masked out first, so a name
  in prose - and this file is half prose - is left exactly as it is.

One thing moved rather than changed shape. `nextNoise` is a lambda that takes its own PRNG
state by reference and touches nothing else of the block's, and the loop calls it once per
sample, so it is a file-local function now: a field holding it as a `std::function` would
have turned the hiss into an indirect call per sample.

What the move bought is a boundary the COMPILER enforces rather than one a reader has to
remember. The loop cannot reach a local of the block's setup any more, so that setup is a
type and the loop is a function that takes it. That is what turns the rest of part 2 from
"rewrite a 1,669-line capture list" into "move a group of statements out of one function" -
the recipe `BlockRateCoefficients.h` and `TapeModulation.h` already follow, with the harness
checking the relationships between the numbers in each group.

Two things in `tests/` had to follow it, and both are the checks doing their job. The AUTO
GAIN compensation gate and its 450 ms coefficient are read as TEXT out of the signal path,
and both halves name the state now (`state.autoGainEnabled`,
`state.compensationCoefficient`, `onePoleCoefficient (450.0f, state.engineSampleRate)`) -
the extractor accepts the `state.` as optional on purpose, because the check is about the
gate's SHAPE and a gate that lost one of its three terms is the fault whether or not the
split is undone. And the block that check reads is found by COUNTING braces: its anchor used
to be the gate's own indentation, which was eight spaces inside the lambda and is four
inside a function, so the old `\n    }\n` would have cut the block short.

**What is left of F is part 2, and it is a move now rather than a rewrite.** The ~1,570
lines of derivations ("read once per block": tape geometry, tempo, delay time, mix and
voicing, transient/neural, preamp/condition/reverb/vinyl, both equalisers' coefficients,
MODERN mode, the guard, the transfer curves, the couplings and the glue's operating points)
are still inside `processTapeEngine()`, still in the order the single body had them - but
they are a function of the parameter snapshot and the rate, they write into
`TapeBlockState`, and each group of them can leave the function the way the two stages above
did, one at a time, with the DSP harness checking what it produces. That, and nothing else
in this document, is the remaining work.

The moves this section adds were made the same way as the three before them, by scripts that
asserted every anchor matched exactly once - and one of those refusals carried a rule rather
than a check: the script that took `resized()` apart refused to run at all until
`ControlGeometry` was declared in the editor's header, so the geometry the bands hand on could
not go missing quietly.

**The evidence, for the move together with the two stages before it.** All six harnesses
pass (`dsp`, `transport`, `pitch`, `state`, `layout`, `neural` - the last four under
`J37_SANITIZE=address,undefined` in CI's debug job as well), the three text checks pass
(`check_presets` 29 presets / 101 of 109 parameters set, `check_theme_wiring` 109 widgets /
94 bodies / 2 look-and-feels, `check_translations` 153 entries / 1,014 distinct strings in
62 source files), and both compile checks pass: `check_headers_juce.sh` 26 headers from
`Source/DSP/` against real JUCE, and `check_editor_juce.sh` 13 translation units plus the
13 plugin headers each on its own. CI, on the revision that carries the move: the five jobs
above green, and the two Linux jobs failing for a reason that took a second look to name.
`build-linux` died with `c++: fatal error: Killed signal terminated program cc1plus` while
compiling `Source/GUI/TapeScene.cpp`, and `build-linux-debug` lost the runner agent itself
(exit 143, with no compiler error anywhere above it). Both compiled with a bare `--parallel`,
so every core started the heaviest translation unit it had at the same moment - and this
branch is exactly what makes the heavy set bigger, because the editor is one file per concern
now and each of those files pulls in `<JuceHeader.h>`. Bounding the compile to two units in
flight (`J37_BUILD_JOBS` in the workflow) let the Debug job run to the end and say something
more useful: the link failed on `FactoryPresets::isSentinelValue(float)`, whose definition and
`count()`'s were declared `inline` in a file other than their only caller - which links in
Release, where every use sits under `jassert` and compiles away, and does not in Debug. Both
entry points have external linkage now, and with those two fixes in, all seven jobs are green
on `103aadb`, the revision that merged as `dee1b87`.
