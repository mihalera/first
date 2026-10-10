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
   `resized()` is better off - the layout harness cuts four spans out of it and
   compiles them - but the rest of the function is checked by looking at the window.
   Any split of those two functions is therefore a change with no measurement behind
   it, and should be treated that way.

---

## 3. What can move

Ranked by benefit per unit of risk. Line ranges are from the current tree.

**A. The parameter table -> `Source/ParameterTable.cpp` (low risk, biggest win).**
`PluginProcessor.cpp:1151-2410` is the namespace holding the 23 group functions
(1,259 lines). The groups are already internal linkage, and `createParameterLayout()`
is declared in the class, so the whole namespace *plus* that function can move as
one unit with no new header and no change to `PluginProcessor.h`. Costs: point
`tests/state/extract.py` and `tests/presets/check_presets.py` at the new file (or at
a list of files - see step 1 of the plan); add one line to `target_sources`. Risk:
low. This is the split already half done: the groups exist, and the state harness
compiles them and counts the 109 parameters they build.

**B. The tooltip text -> `Source/PanelText.cpp` (low risk, moderate win).**
`PluginEditor.cpp:3696-4266`: the `ControlSpec` table (one row per knob) and
`parameterTooltipText()` (431 lines of sentences). It is read by two call sites and
by nothing else, it is pure data, and neither the layout nor the DSP harness touches
it. Costs: one line in `target_sources`; if the `ControlSpec` table moves too, the
editor's debug cross-check against `tabSpecs` has to see the declaration, so give
the file a small header or leave the table where it is.

**C. The paint kit -> `Source/GUI/PanelPaint.{h,cpp}` (low risk, moderate win).**
`PluginEditor.cpp:181-1045` is the 865-line anonymous namespace that draws the
vintage furniture: `UiPalette` and the palette tables, `drawPanel`,
`fillCylinderBarrel`, `fillLitDome`, `shrinkingFont`, `tabsCoverAllControls`. It is
referenced by `paint()`, by the two look-and-feels and by the meters, and - checked -
not by `PluginEditor.h`, so the header only needs to include whatever the new
header declares. The radial-budget namespace right after it (line 1046) is cut by
the layout harness and should stay put. Risk: low.

**D. The DSP structs -> `Source/DSP/*.h`, one per stage (low risk, large win).**
`SaturationCore` (861), `VinylStage` (567), `SubharmonicGenerator` (560),
`NeuralStage` (380), `ThreeBandEq` (292), `InputStage` (244), `HarmonicAnalyser`
(194), `PlateReverb` (193), `GlueCompressor` (148) - about 3,600 lines of header that
would take `PluginProcessor.h` from 6,177 to roughly 2,600. They are self-contained
header-only structs. Five of the nine (`SaturationCore`, `VinylStage`,
`SubharmonicGenerator`, `NeuralStage`, `GlueCompressor`) are compiled by the DSP
harness today, so a broken move there fails in CI rather than in a session; the other
four are covered only by the plugin build, which is a reason to start with the five.
Costs: constraint 3 above (the probe block must travel or be shared), and
`tests/dsp/extract.py`'s `HEADER_PIECES` must name the new files. Do them **one
struct per commit**, harness in hand. Risk: low mechanically, but this is the
most-edited header in the project, so keep the diffs pure moves.

**E. The shader sources -> `Source/GUI/TapeSceneShaders.h` (low risk, small win).**
335 lines of GLSL string literals inside `TapeScene.cpp`'s scene namespace. They are
data, nothing cuts them, and the file is the least comment-heavy one under `Source/`
outside the small backends (21 %). Risk: very low; win: the scene namespace drops to
~500 lines of geometry and constants. Not worth doing on its own, worth folding into
any other `TapeScene` work.

**F. `processTapeEngine()` -> named stage functions (high risk, the real prize).**
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

1. **Teach the extractors to search a list of files** (`tests/state/`, `tests/dsp/`,
   `tests/layout/`, `tests/presets/check_presets.py`). Mechanical, no moves; it makes
   every later step a one-line change and removes the "which file does the harness
   read" trap. Verify: the four checks below.
2. **A - the parameter table** to `Source/ParameterTable.cpp`. Verify: state harness
   (109 parameters, 39 checks), preset check.
3. **B and C - tooltip text and the paint kit** out of `PluginEditor.cpp`. Verify:
   layout harness, then the window itself.
4. **D - the DSP structs**, one per commit. Verify: DSP harness after each.
5. **E - the shaders**, if `TapeScene.cpp` is being touched anyway.
6. **F and G - the long functions**, last, one stage per commit, and only with the
   harnesses extended to compile the stage that moves.

After every step: `bash tests/dsp/run.sh`, `bash tests/layout/run.sh`,
`bash tests/state/run.sh`, `python3 tests/presets/check_presets.py`, and the three
harnesses once with `J37_SANITIZE=address,undefined`.
