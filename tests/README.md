# The test suite

An index over `tests/`, not a replacement for the prose inside it: every harness
header explains *why* it exists and what it was written for, and the tables below
answer the question you actually arrive with - **if I change X, what will catch
me?** - in one screen.

Everything here runs on a machine with nothing but a C++17 compiler, Python 3 and
bash. No JUCE build, no audio device, no plugin host, no CMake configure. The
whole suite is seconds of work, which is why it runs on every pull request and
not only where the plugin is built. The two rows that need something *fetched*
first say so where they appear: Catch2 for the unit suite, nanobench for the
benchmarks.

## Run it

```bash
tests/dsp/run.sh          # subharmonics (SUBFUND), the loudness meter, the shaper's tanh seam, and the noise decoder
tests/layout/run.sh       # knob geometry on every page, at four panel sizes
tests/tooltip/run.sh      # the tooltip dwell, at exact millisecond boundaries
tests/state/run.sh        # session state round trip, and older state files
tests/transport/run.sh    # the transport rig, frame by frame
tests/pitch/run.sh        # the pitch tracker, the corrector stage, and BOTH of its engines
tests/phase/run.sh        # the three-band output anti-phase guard, band by band
tests/gain/run.sh         # the AUTO GAIN compensator, on generated signal
tests/limiter/run.sh      # the output safety limiter, catch and release
tests/mix_crossfader/run.sh  # the MIX crossfade, its law and its two gated legs
tests/neural/run.sh       # the rate bridge, the stage with RTNeural stubbed, and the .nam fixtures through BOTH engines
tests/spectrum/run.sh     # the spectrum analyser, with pocketfft in and out of the build
tests/reverb/run.sh       # the room reverb: T60 in seconds, at 44.1, 48 and 96 kHz
tests/scene_frame/run.sh  # the frame a native backend receives: validity, shared meshes, the deck's layout
tests/osc/run.sh          # the staged OSC codec (bytes, padding, bundles) and its address surface
tests/fuzz/run.sh         # the WAV decoder fed mutated bytes, under ASan/UBSan and (where clang is) libFuzzer
tests/unit/run.sh         # the equaliser, the compressor and the transient shaper, as units

tests/dsp/check_headers_juce.sh     # every Source/DSP/*.h against real JUCE
tests/editor/check_editor_juce.sh   # every translation unit and Source/GUI/*.h against real JUCE

tests/dsp/run_bench.sh    # measurement: the DSP hot paths, with and without the xsimd tanh
tests/neural/run_bench.sh # measurement: .nam inference cost on NAM Core and RTNeural

python3 tests/presets/check_presets.py           # the factory preset table
python3 tests/theme/check_theme_wiring.py        # theme re-inking coverage
python3 tests/translations/check_translations.py # the shipped translation tables
python3 tests/parameters/check_parameters.py     # the table and the code that reads it
python3 tests/shaders/check_shaders.py           # the GLSL and the uniforms it is fed
python3 tests/definitions/check_definitions.py   # the macros the build defines, and who reads them
python3 tests/linkline/check_linkline.py         # the link line, against the includes under Source/
python3 tests/render_backend/check_render_backend.py  # the renderer options and the adapters behind them
```

Two environment switches, both used by the workflows:

| Variable | Effect |
| --- | --- |
| `J37_SANITIZE=address,undefined` | builds every harness above, and the unit suite, with ASan + UBSan and turns them loose on the same assertions |
| `CXX` | the compiler to use; `g++` by default |

The micro-benchmarks are separate and **informational**: `tests/dsp/run_bench.sh`
(nanobench, needs `-DJ37_BUILD_TESTS=ON` to have fetched it), `tests/neural/run_bench.sh`
(the .nam inference engines - see its row below), and the hot-path probe step in
`.github/workflows/build.yml`. Nothing fails a build because a benchmark got slower. It builds `tests/dsp/bench.cpp` TWICE - once as the engine
ships, once with `-DJ37_USE_SIMD_TANH=1` - so the rows the two logs share
(`SaturationCore::process` above all) are the measurement of what that switch
buys, on the same signal through the same file, rather than two benchmarks
being compared from memory.

`tests/neural/run_bench.sh` is the other measurement, and it is informational in
the same way. It compiles NAM Core from its own sources, generates the models the
`.nam` format actually ships from `tests/neural/nam_shapes.h` (a 16-channel,
ten-dilation WaveNet and a 20-cell LSTM, not the tiny fixtures), and prints
nanoseconds per sample and the share of one 48 kHz core for each engine - through
the shipping stage and through the backend alone, with the fixtures beside them
for scale. It fails only when a model does not load or renders a non-finite
sample; those numbers are why `J37_NAMCORE` is ON - see WP3 in
[docs/library-integration-plan.md](../docs/library-integration-plan.md).

The unit suite needs a framework rather than a measurement, from the same
`-DJ37_BUILD_TESTS=ON` configure: `tests/unit/run.sh` compiles Catch2's own
amalgamated sources, looks for the checkout in the CPM cache and takes
`CATCH2_DIR` as an override. Unlike the benchmarks it IS a gate, because every
number in it is a property of a function rather than of the machine - there is
nothing in it that a busier runner could make flaky.

## What each harness compiles

This is the part that decides how much a green run is worth. A harness that cuts
a span of text out of a big file tests that span; a harness that *includes the
shipping header* tests the code the plugin compiles.

| Harness | What it compiles | How it gets it |
| --- | --- | --- |
| `dsp` | the subharmonic generator, the loudness meter, a reduced replica of the signal path, the noise decoder behind the vinyl print - plus `j37Tanh`, `SaturationCore` and the seven curves, built THREE ways | extracts spans through `tests/dsp/extract.py`, **and** `#include`s every stage that has moved to `Source/DSP/`; the tanh seam's three binaries are one file compiled with and without `J37_USE_SIMD_TANH` and with and without xsimd on the include path, and the noise decoder is one file compiled with and without `ThirdParty/dr_libs` |
| `layout` | the knob strip arithmetic, the editor's constructor phase order, and the spectrum curve's level mapping | `tests/layout/extract.py` cuts the first two out of `Source/PluginEditor.cpp` + `Source/GUI/`, and `#include`s `Source/GUI/SpectrumMetrics.h` directly |
| `tooltip` | `j37panel::TipDwell` - the rule that decides when a hint may be shown | `#include`s `Source/GUI/TipDwell.h` as it ships: no extractor, no shim, because the header includes nothing (the JUCE half is `Source/GUI/DwellTooltipWindow.h`) |
| `state` | the parameter table and the state functions | `tests/state/extract.py`, compiled against `tests/state/juce_stub.*` |
| `transport` | `TransportRig` and `RepitchStage` | `#include`s the shipping headers from `Source/DSP/` |
| `pitch` | `PitchTracker`, `RealtimePitchShifter` and `CorrectorStage` | `#include`s the shipping headers from `Source/DSP/`, and builds `pitch_test.cpp` TWICE: once as the plugin ships it, and once with `-DJ37_CORRECTOR_USE_STRETCH=1` and `ThirdParty/signalsmith-stretch` on the include path, which is exactly the pair of switches CMake's option throws. The two binaries assert different promises, and the second one prints the numbers behind that option being OFF |
| `phase` | `MultibandPhaseGuard` - the crossovers, the six band envelopes and the rotation | `#include`s the shipping header from `Source/DSP/`: no extractor, no replica, because the guard is arithmetic on the two samples it is handed |
| `gain` | `AutoGainCompensator` - the programme-level correction, its pole and its ceiling | `#include`s the shipping header from `Source/DSP/`, with no shim at all: the stage names nothing of JUCE, which is what lets this compile with `-I Source` and nothing else |
| `limiter` | `SafetyLimiter` - the peak detector, the required gain, the catch/release pair and the ceiling | `#include`s the shipping header AND `Source/DSP/BlockRateCoefficients.h`, against the DSP harness's `shim.h`: the four coefficients come from the file the engine gets them from, so the harness measures the poles the plugin actually runs |
| `mix_crossfader` | `MixCrossfader` - the equal-power gain pair, the two gated legs and the clamp | `#include`s the shipping header from `Source/DSP/` and renders signal through the legs to measure the level across the travel: no extractor, no replica and no shim, because the stage names nothing of JUCE |
| `neural` | `RateBridge`, `NeuralStage` with `J37_HAS_RTNEURAL=1`, and the `.nam` fixtures in `tests/neural/fixtures/` through both engines | `#include`s the shipping headers. Five binaries: `rate_test.cpp` drives the bridge alone; `stage_test.cpp` stands RTNeural's two-method API in for (so the guarded half compiles with no library at all); `fixture_test.cpp` uses the REAL vendored `ThirdParty/RTNeural` AND `ThirdParty/namcore` off the probes in `OptionalLibraries.h` - which is why nothing there defines `J37_HAS_RTNEURAL` by hand - so a model FILE is loaded, driven through both engines and compared; the same file again WITHOUT `J37_NAMCORE`, which is the `-DJ37_NAMCORE=OFF` build and keeps the WaveNet-refusal branch compiled; and `nam_only_test.cpp`, NAM Core with `J37_HAS_RTNEURAL=0`, where the dispatch has to send the LSTM to NAM too. The NAM Core sources are compiled from their own tree and put on the link line as objects, not an archive: their architectures register themselves in file-scope statics that an archive would prune |
| `spectrum` | `SpectrumAnalyser` | `#include`s the shipping header from `Source/DSP/`, compiled TWICE: with `ThirdParty/pocketfft` on the include path, and without it |
| `fuzz` | `Source/DSP/NoiseDecoder.h`, compiled with the vendored `ThirdParty/dr_libs` | feeds the shipping decoder bytes no one wrote - truncations, bit flips across the header, zeroed and maximal channel/rate/size fields, appended garbage, random buffers - and asserts the header's own contract on every outcome. TWO builds of one source: the file's `main` is a fixed sweep that runs on any C++17 compiler, and the same file with `-fsanitize=fuzzer` is a coverage-guided run for `J37_FUZZ_SECONDS` (default 20) on a clang that has libFuzzer. Two mutators, one decoder: a machine without clang loses the search, not the gate |
| `osc` | `Source/Net/Osc.h` and `Source/Net/OscSurface.h`, as they ship | `#include`s the two headers with no JUCE on the include path at all - they name nothing of the toolkit, on purpose, so the codec can be checked against the wire format without a plugin, a window or a host |
| `scene_frame` | `Source/GUI/SceneFrame.h` and `Source/GUI/SceneLayout.h`, as they ship | `#include`s both headers with no JUCE, no GPU and no host: a frame is plain data and `isValid()` is arithmetic on it, so the boundary a native adapter receives is measured on every pull request rather than only where one is built |
| `reverb` | `DattorroRoom`, linked against the vendored `ThirdParty/dattorro-verb/verb.c` | `#include`s the shipping header against the DSP harness's `shim.h`, and compiles the library's own C source beside it: `verb.c` as C, the header as C++, which is also the only place the stage's `extern "C"` wrapping is measured |
| `unit` | `ThreeBandEq`, `StandardCompressor` and `TransientShaper` - one stage per file | `#include`s the shipping headers from `Source/DSP/` against the same shim the DSP harness uses, and links Catch2's amalgamated sources: no replica of the engine, because none of the three needs one |
| `linkline` | no C++ at all: `target_link_libraries(first ...)` in `CMakeLists.txt`, every `#include` under `Source/`, and the `J37_HAS_*` probes in `OptionalLibraries.h` | reads all three as text. A spelling table says how each target is named when the code uses it (`nlohmann_json` is `nlohmann/json.hpp`, `pocketfft` is `pocketfft_hdronly.h`, `chowdsp_simd` is included by chowdsp_math rather than by `Source/`), because a name match would call `rack_dsp` used on the strength of `PitchTracker.h` |
| `dsp/check_headers_juce.sh` | every header in `Source/DSP/`, against real JUCE | compiles each header's public surface; catches a header that only works under a harness shim |
| `editor/check_editor_juce.sh` | every translation unit the plugin builds, and every `Source/GUI/*.h` | compiles them against real JUCE; catches a moved block with a missing include |
| the seven Python checks | no C++ at all | read `Source/` and the build files as text and compare tables to the code |

## What each harness guarantees

| Harness | The claims it pins | Written for |
| --- | --- | --- |
| `dsp` (subfund) | the undertone series is generated at the octave below, does not run away, stays silent below its threshold, and the anti-phase guard engages | the subharmonic generator shipping silent and then shipping louder than the programme |
| `dsp` (tanh seam) | that the shaper's tanh is `std::tanh` bit for bit unless `J37_USE_SIMD_TANH` is thrown, that the flag WITHOUT xsimd on the include path renders the default curves byte for byte, and that the flag WITH xsimd really moves the rendered audio - by 1.19e-7 at worst, which is the accuracy the header quotes, and not by zero, which is what it used to do | a flag that switched a function nothing called, whose branch called `xsimd::tanh` on a float - and xsimd's scalar entry point is `using std::tanh;`, so the switch was a no-op that no build, test or CI job could notice. The three binaries are compared against each other because "the flag reaches the audio" is a claim about two builds rather than about one |
| `dsp` (noise print) | that 16-bit, 24-bit and 32-bit-float WAVs decode to the samples that were written; that a mono print is the same surface on both sides; that a float print outside [-1, 1) is left alone; that a 44.1 kHz print is converted to the engine's rate with the loop's seam wrapped; that an empty buffer, a truncated file, a NaN sample and a print over a minute are each REFUSED with a reason; and that a loaded print replaces the synthesised hiss rather than doubling it, while CLICKS keeps firing over it | a loop padded with zeros after a short read (a dropout once per repeat), a print silently clipped or normalised so two surfaces at different levels sound identical, and a rate conversion that clamps at the seam instead of wrapping - a click on every repeat. Built twice, because "this build has no decoder" is a branch a pruned `ThirdParty/` compiles and nothing else would read |
| `dsp` (loudness oracle) | the shipping `LoudnessMeter` and libebur128 - the reference implementation of BS.1770 - agree to within 0.25 LU on the SAME buffer, at twelve points: 1 kHz at -20, -3, -40 and -60 dBFS, 50 Hz, 100 Hz, 10 kHz and 15 kHz at -20, white noise at -20, and 1 kHz at 44.1, 96 and 192 kHz. The worst deviation is printed (%0.3f) so a drift toward the gate is visible before it fails | the only claim in this repository that no self-consistent implementation can make: that the K-weighting curve is BS.1770's rather than a plausible approximation of it. The two implementations differ by DESIGN on a signal that starts or stops - a one-pole decays where the reference's window ends - which is why every case is a three-second steady signal read at its last frame, and why the tolerance is a band rather than an equality |
| `dsp` (loudness) | calibration, linearity, rate independence, the 3.01 LU channel sum, the floor, that `prepare()` then `reset()` leaves the curve in place, and EBU Tech 3341's own three signals - 1 kHz stereo sine at -23, -33 and -36 dBFS reading -23.0, -33.0 and -36.0 LUFS within the standard's 0.1 LU. The curve behind those is cross-checked against libebur128 IN the tree, by `loudness_oracle_test.cpp` beside it: twelve steady signals from 50 Hz to 15 kHz, -3 to -60 dBFS and 44.1 to 192 kHz, requiring 0.25 LU and measuring 0.054 at worst (the meter sits 0.005 LU from the exact digital response where the reference sits 0.044 above it). That comparison used to be a sentence about a measurement made outside the repository; it is a binary in CI now | the K-weighting filter running on one pair of state variables for two channels, `reset()` zeroing the coefficients, and a meter that is self-consistent with a curve that is not BS.1770's - the standard's signals are the only numbers in the harness that were not derived from the code they check |
| `tooltip` | a hint is withheld for the full 350 ms after the pointer lands on a control, the clock restarts when it moves to the NEXT control (so the second hint waits exactly as long as the first), a repeated reading of the same control restarts nothing, and hovering off the panel counts as a change | a hint that appeared instantly when the pointer moved between two switches, because `juce::TooltipWindow` applies its delay only to the first tip and shows every later one as soon as the hovered component changes |
| `layout` | every knob's tile stands inside its page and above the member band; no two tiles overlap; the strip is centred; the disc is drawable at the 780 x 664 floor; the value text clears its field's wall; the painted disc is the solved disc; and the spectrum curve's level axis runs the right way up, is CLAMPED at both ends rather than rescaled, and keeps every point inside its cell at any box size | a layout drifting away from the paint routine, and a constructor phase that quietly never ran - plus a curve that draws smoothly, plausibly and upside down, which no screenshot review catches |
| `state` | a written state reads back identical, an older state loads, an unknown key is ignored, and a missing parameter keeps its default | a bad pointer or an out-of-bounds read in the state path, which only ever shows up in someone's session |
| `transport` | START starts, STOP arrives at rest, the power cut spins down, and `platter()` moves the way the engine's level and modulation assume | START doing nothing on a running machine, and STOP approaching rest without reaching it |
| `pitch` | the tracker finds notes, notes with harmonics, and notes in noise; silence yields no note; the shifter holds its ratios; and the corrector leaves a reading within ±10 cents exactly alone (ratio 1, bit for bit), pulls one outside it by the interval it is off, scales the pull with TUNE AMOUNT and never grips past a semitone | an out-of-range lag or a read past the delay line, and a corrector that "corrects" a performance that was already in tune - which is what removes the human character the control is meant to keep |
| `mix_crossfader` | MIX 0 % is exactly (dry 1, wet 0) and returns the dry input bit for bit; 100 % is the tape path (wet exactly 1, dry is cos(pi/2) in float, under 1e-7); dry^2 + wet^2 stays 1 across a 1000-step travel, one leg rising and the other falling with no step over 0.0016; an uncorrelated pair is held within 0.5 dB across the travel while a correlated one is +3 dB at the midpoint, which is measured rather than hidden; a gate at 0.5 halves BOTH legs and a closed one empties them, so STOP at MIX 0 % is silence and not the input; and a position outside 0 .. 1 - NaN included - clamps exactly as `juce::jlimit` answered | a crossfade whose ends are not the two signals, one that dips in the middle (the linear law this shape replaced), a stopped machine that still passes the dry leg - the reported STOP fault - and a NaN position reaching cos/sin instead of clamping |
| `limiter` | a programme below the ceiling - and one sitting exactly on it - is multiplied by exactly 1.0f for two seconds, bit for bit; a step 6 dB over the ceiling is caught within 6 ms and held there, with the settled gain equal to ceiling / peak; both sides of a frame get the one gain, so a loud left channel does not shift the image; 10 ms of quiet after a burst moves the gain by well under a percent, under a quarter of the correction is paid back after 60 ms, just under half after 120 ms and most of it after 240 ms; and the catch takes the same number of milliseconds at 44.1, 48, 96 and 192 kHz | a limiter that colours a signal that was already safe, one that catches too late and lets the clipper distort what it should have protected, one that pumps between notes, and a coefficient pair that quietly stops following the rate - none of them audible as anything but "the plugin sounds worse" |
| `gain` | a programme that lost nothing is multiplied by exactly 1.0f for two seconds; a -6 dB programme returns to its input level within 0.05 dB and the correction never overshoots it; a programme 6 dB above the reference is left exactly alone; a -30 dB loss is restored by 12 dB and not a decimal more; 450 ms is one time constant at both 48 kHz and 96 kHz, to within 0.01 dB of each other; a silent frame returns unity and holds the correction rather than driving it; and a 10 ms dip moves it by a seventh of what a 450 ms window would take | a compensator that is a boost stage when the programme is already loud, one that slowly rides to its +12 dB limit while STOP makes the output silent, and one that chases transients - three failures that are inaudible until the level has already drifted |
| `phase` | the three Linkwitz-Riley bands sum back to a flat magnitude (within 0.1 dB across 30 Hz to 15 kHz) and each path is -6 dB at its own crossover; a correlated frame at full engagement comes back bit for bit; an inverted LOW band loses its side by 40 dB while the high band, which was healthy but wide, keeps its own within 0.03 dB; the recovered low band is folded into MID rather than faded away; and when the fault ends the rotation unwinds until the pair is whole again | a single-band guard rotating the whole frame by the low band's reading, which flattens the bands that were fine to fix the one that was not - and the two things a guard must never do: change a mix with no problem, or leave a rotation latched after the fault is gone |
| `neural` (rate) | the bridge carries signal between the engine's rate and a model's, at the rate pairs the engine can produce | the stage running a model at the engine's rate while the model carries its trained rate |
| `neural` (stage) | the guarded half of `NeuralStage` - everything inside `#if J37_HAS_RTNEURAL` - compiles and runs | a branch no harness had ever compiled |
| `neural` (fixtures) | that `lstm_small.nam` and `wavenet_small.nam` parse as the architecture they declare, that their flat weights array matches the LENGTH the config's layout implies (computed, not remembered), that the trailing WaveNet weight IS the config's `head_scale`; that the shipping stage BUILDS the LSTM fixture and reproduces NAM Core's own output for a fixed 64-frame input to 1.19e-7 at frames [32, 48); that NAM Core's own LSTM, driven directly, agrees with the RTNeural converter to 5.96e-8 on the same fixture; that the WaveNet fixture renders the same window to 9.31e-10 through a reference evaluator validated against NAM Core AND to 2.79e-8 through the shipping stage, which is NAM Core behind the rate bridge; that every rendered sample is finite and bounded; that clearing either model returns the stage to the bit-exact pass-through; and that a build WITHOUT NAM Core still refuses WaveNet and says so | the WP3 gate: a hand-authored fixture whose weights array is the wrong length, an LSTM whose converter drifts from the reference player's arithmetic, two engines that disagree, and a WaveNet file nobody has ever rendered - each of which fails quietly inside a model that loads. The pair of binaries is what keeps both the with-backend and without-backend branches compiled |
| `pitch` (corrector) | the stage the two halves above drive, in whichever engine the build selected: with the granular shifter, no lookahead is reported and a tone is moved to within a couple of cents at every ratio; with the vocoder, the pitch moves within its own few percent (printed per ratio) and NEVER by the resampling the other engine implies - 100 ms of note comes back 103 ms at ratio 1.0293, where a resampler answers 97 - the level is kept, and an impulse arrives exactly `latencyFrames()` later, which is the number the plugin adds to its reported latency. That last one is what keeps a corrected session's other tracks in place | a corrector whose engine is swapped without anyone knowing what it costs: the two engines differ by 25 ms of lookahead and by up to 32 cents of pitch accuracy on a stationary tone (8 at the top of the range), and both numbers are printed by the run rather than described |
| `unit` (`ThreeBandEq`) | at neutral settings the stage is an identity to within a float step and both filters are real bypasses at their off ends; the pole count is the slope divided by six, clamped to the eight-pole array; Q is held between 0.5 and 1.5; the high-pass removes DC while the low-pass keeps it; LOW and HIGH each move their own end of the spectrum and leave the other, and MID moves its own band without dragging the ends with it (measured and bounded: about 1.09x at 50 Hz and 1.15x at 10 kHz alongside 1.9x on the bell, for MID at +6 dB) | a "neutral" EQ that is actually a very gentle filter, an order control that quietly picks a different slope, and the wrong band taking a shelf's gain. It also records where the header's wording is stronger than the arithmetic: the three bands are a SUM of one-pole splits, so transparency is exact for a fresh instance on most values and one float step off on others (0.1 comes back 0.099999994) - a float step, never a filter, and the test says which is which |
| `unit` (`StandardCompressor`) | unity under the threshold; the ratio's slope above the knee, as 10 dB more in and 10/ratio dB more out; a knee that blends rather than steps, checked as three 4 dB steps up the curve; MAKEUP exactly on the label; a ratio below 1:1 behaving as 1:1 rather than as a gate; RELEASE recovering to unity instead of parking; and finite output at the parameter extremes, including a sample rate of zero. The slope is measured as a DIFFERENCE because the detector reads a sine about 1.6 dB high - a property of asymmetric ballistics that the suite pins rather than assumes | the two places this stage's maths divides by something - the ratio guard and the knee - and a gain that stays where a loud passage left it instead of recovering |
| `unit` (`TransientShaper`) | 0/0 returns the input bit for bit; ATTACK moves the leading edge and leaves the tail where it was; SUSTAIN lifts the tail at +1 and shortens it at -1 - the sign the header spends its longest paragraph justifying - and leaves the edge alone; the applied gain stays inside a quarter and four; and a signal arriving into an envelope that has only ever seen silence produces no NaN. The signal is a PLUCKED note rather than a step, because a step's tail is exactly zero and cannot show what SUSTAIN does | the inverted SUSTAIN sign, which sounds like a working control until it is measured, and a normalisation that divides by an empty envelope |
| `spectrum` | the transform is calibrated (a full-scale tone on a bin centre reads 0 dBFS), linear, low between bins only as far as a Hann window's own 1.42 dB scalloping loss allows, band-ordered, and floored on silence; and that a build WITHOUT pocketfft still compiles, stays unready and reports the floor | a curve that reads high or draws the wrong band, which looks entirely plausible on a panel - and the absent-library half of a header, which nothing else here compiles |
| `scene_frame` | a valid frame - shared mesh array, identity matrices, finite colours - is accepted; a zero-sized frame, a null mesh array, an index past the mesh, a triangle with two indices, a NaN vertex normal, an infinite model entry, a NaN base colour or an infinite per-draw highlight is refused; and the layout constants the builder reads are internally consistent (`controlBankCount` matches the placements, the pack fractions increase) | the boundary is the one place a frame can be structurally wrong without any backend noticing: a null share segfaults inside an adapter, an index past the mesh reads whatever follows it, and a NaN reaches a shader uniform as undefined behaviour - none of them visible in a screenshot |
| `reverb` | at MIX 0 the frame comes back bit for bit, at the tank's own rate and at a converted one; SIZE lengthens the tail, and the same SIZE decays for the same number of SECONDS at 44.1, 48 and 96 kHz (1.21 / 1.18 / 1.20 s at 0.5); at 48 kHz the stage is the library's own path with one sample of latency, and off that rate it costs only the conversion's read-ahead; the two sides of the tail are decorrelated (correlation -0.01, side-to-mid energy 1.02) and a mono bus is fed the pair summed rather than one side; MIX 1 puts the wet pair within 6 dB of the dry programme; and the tail decays away (to -340 dB after 12 s) without a NaN or an infinity anywhere | a reverb is the one stage whose delay lengths are SAMPLE COUNTS, so run at 96 kHz its tail is half as long as at 48 - the property no listening test catches by ear and no source reading catches at all. Also the two things a room must never do: leave a trace at MIX 0, and ring on after its tank has lost its energy |
| `fuzz` | that every input the sweep can reach leaves the decoder inside its contract: a REFUSAL names its reason (an empty string is a panel printing `INVALID PRINT ()`), reports no frames and leaves the caller's print untouched, so a failed load cannot half-replace the loop that is playing; an ACCEPTANCE is playable - a non-zero length, both channels at least that long, a positive rate, and every sample finite; and the same bytes decode to the same print twice. 1532 inputs on the sweep: 870 decoded, 662 refused across 5 refusal branches, none of them a violation | a decoder that returns "ok" and then a NaN, a refusal that half-fills the print so the previous loop is destroyed by a file that did not load, an out-of-bounds read on a truncated header (ASan), an uninitialised or overflowing field (UBSan) - and the class of input nobody writes by hand, which is what a fuzzer is for |
| `osc` | the wire format itself, in both directions: an eight-byte message with no arguments is the bytes OSC fixes; the address, the `,f` type tag string and IEEE-754 big-endian `0.25f` land where a client puts them; a five-character string occupies eight padded bytes; a hand-built client packet (not one this encoder produced) decodes to `/gain` and 0.5f; int32, float and string survive a round trip including negatives and multi-word text; a bundle of three decodes in the sender's order; and the refusals - a packet that is not a whole number of four-byte words, an unterminated string, a type tag promising an argument the packet does not hold, a missing type tag, an unsupported tag, a bundle handed to `decode()`, a bundle element longer than the packet it sits in, an address with a reserved character. On the surface: an exact address routes to its slot, values above 1 and below 0 CLAMP rather than refuse, NaN and a second argument and an int32 and a string are refused rather than guessed, a pattern address is refused rather than expanded, feedback round-trips through the codec, and the engineering value maps the parameter's own range | a codec that only agrees with its own encoder - which round-trips perfectly and talks to nobody - and a decoder that trusts a length out of a packet. Both halves are STAGED: the plugin compiles the headers and constructs neither, so this harness is the only thing standing between the staged code and the day a socket is opened on it |
| `presets` | the preset names match the table, every sound-bearing parameter has a column, and the counts agree with `ParameterTable.cpp` | the preset box listing 20 names for a 26-row table, and 15 sound-bearing parameters silently keeping the session's value |
| `theme` | every widget the panel owns is re-inked when the theme changes | a control keeping the ivory panel's ink on charcoal, or falling back to the Look and Feel's own dark scheme |
| `translations` | no dead keys, every lookup present in every language, no ambiguous or empty entry | 17 Ukrainian entries keyed to sentences the plugin no longer said |
| `linkline` | every library on the plugin's link line is named by an include under `Source/` (or is a transitive dependency named as one), every library the code includes is on the link line, and every `J37_HAS_*` probe is read outside the header that decides it - with no exceptions left, since the GPLv3 module that owned the last one came off the link line | eighteen linked libraries the code never named: four alternative FFTs against the one pocketfft the analyser calls, a second decoder stack, a GUI toolkit, a socket library, two rival pitch detectors, a reverb and an LGPL time stretcher - plus xsimd's probe, read only by a branch no build compiled |
| `check_render_backend` | that the two whole-library graphics layers (`SOKOL`, `BGFX`) are values `J37_RENDER_BACKEND` accepts AND adapters `createNativeRenderer` reaches, that the sokol implementation stays in a JUCE-free translation unit, that bgfx presents into a child surface rather than the host's window, that the platform adapters keep their resource and lifecycle rules, and that no native selection is advertised as drawing the scene | a backend a build can select but nothing implements, a sokol implementation that includes JuceHeader.h and loses the GL names to `namespace juce::gl`, and a bgfx surface created on the host's own peer - which paints over the host's UI |
| `check_headers_juce` | each `Source/DSP/*.h` compiles against real JUCE, alone | a header that compiles under the harness shim and not in the plugin |
| `check_editor_juce` | each translation unit and each `Source/GUI/*.h` compiles against real JUCE | a moved block whose new file forgot an include; it caught three faults on its first run |
| `check_parameters` | that the table and the code that reads it agree, both ways: every registered id is fetched with `getRawParameterValue` or looked up with `getParameter`, every name read is registered, and every handle fetched is read again somewhere | a knob that changes nothing - a mistyped id reaches nothing silently (`getRawParameterValue` returns nullptr and every call site is `if (x != nullptr)`), and a registered parameter nothing reads is a row in the host's list that consumes nothing |
| `check_shaders` | that both GLSL sources are still runs of one literal per line, that `TAPE_SCENE_HIGHP` is still defined, and that the uniforms the shaders declare are exactly the ones `TapeScene.cpp` sets | the `"the lit arc"` line of `TapeSceneShaders.h`, where a paste left a bare quote behind - the literal closed early and the rest of the line became a C++ comment, which compiles; and a `setUniform` name the linked program does not declare, which is a silent no-op that leaves the visual that depends on it at zero |
| `check_definitions` | that every `J37_*` macro the build hands the compiler is read by a preprocessor branch under `Source/`; and that the IPP switch is the one JUCE's own FFT engine is gated on, is still thrown, and that the `JUCE_DSP_USE_INTEL_IPP` spelling which does not exist in JUCE 9.0.4 stays out of the build | `J37_HAVE_IPP` and `J37_HAVE_VDSP`, defined for the plugin by `cmake/J37VendorLibraries.cmake` and read by no file under `Source/` - two claims about the build that nothing checked, beside an IPP install that was discovered, linked and accelerated nothing. The JUCE half of the check needs the submodule and says so when it is not there |

## What the suite does **not** cover

Listed here rather than left to be discovered, because each one is a proposal in
[docs/plugin-improvement-plan.md](../docs/plugin-improvement-plan.md):

- **`processBlock`'s boundaries.** Zero frames, one frame, a prime block size, a
  rate change between blocks, a prepared-but-never-played plugin. The stages are
  measured; the edges between them are not.
- **The rendering backends.** `Source/GUI/NativeRenderer_{D3D11,D3D12,Vulkan,Metal}`
  are compiled by the platform builds and checked by nothing else. The Metal
  adapter's display-scale handling is the one part of it a Linux machine cannot
  even read: it is Objective-C against AppKit.
- **The GLSL as GLSL.** `check_shaders` proves the sources are intact and that the
  renderer and the shader agree on every uniform's NAME; it does not compile them.
  A dialect mistake - a construct a driver rejects - is still found by a driver and
  by nothing here. `glslangValidator` (glslang-tools) would close that, and it is
  the one addition worth making to this suite that needs a tool outside it.
- **What the plugin will load on.** The macOS workflow reads the built binaries'
  `minos` and the AU's own plist, and validates the AU with Apple's `auval` - but
  those run on GitHub's runner, not here, and none of them is a DAW.
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
