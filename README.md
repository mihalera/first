# first

**J37 Tape Mastering** - a JUCE-based VST3 **mastering-grade tape saturation plugin** inspired by classic analog tape machines and J37-style coloration.

This is, first and foremost, a **bus / mastering tool**: two independent glue compressors wrap the tape stage, the output level is calibrated in dB, the loudness metering is four-way (peak / RMS / LUFS / VU), and the output protection chain guarantees that what leaves the plugin is clean and controlled. Use it on the master bus, a drum bus or any programme material where you want the density and warmth of tape without losing control of the level.

## Overview

This project is a mastering-oriented audio effect plugin built for Windows with JUCE and Visual Studio.
It provides a tape-saturation mastering workflow with:

- drive and harmonic character controls
- a **TONE macro** that crossfades the whole machine state (tape stock, head gap, pre-bias, flutter) between the classic slow machine and the fast/hot machine
- **oversampling** (Off / 2x / 4x) around the nonlinear engine, with the filter delay reported to the host
- **factory presets** covering the machine's real range, plus **user presets** saved to disk, A/B compare and full-state undo/redo
- **polarity invert** and **auto gain** output switches, the two mastering staples
- tape-type selection
- speed influences and modulation
- wow / flutter behavior
- bias, brightness, mix, and calibrated output staging in dB
- two independent always-on tape glue compressors, one after the input trim and one
  before the output trim, driven by the signal and by the Input / Output controls
- **compressor-coupled saturation**: the harder the glue stages squeeze, the hotter the record head is driven, so dense programme saturates more than sparse programme - like real tape
- glue attack/release time constants that follow the loaded **tape type** and the selected **transport speed**
- four VU-style meters: input and output level, plus one reduction meter per compressor
- a vintage analog-inspired UI with animated knobs, reel and level meters

## Signal path

Input trim (dB) -> input glue compressor (always on) ->
record head (pre-emphasis, bias, magnetic hysteresis with memory) ->
tape low-pass and head-gap loss -> tape noise floor and wow/flutter modulation ->
playback EQ tilt -> output glue compressor (always on) -> output trim (dB) -> stereo width.

## Controls

| Control | Range | Notes |
| --- | --- | --- |
| Input | -32 to +32 dB | Drives the tape machine harder |
| Drive | 0 to 100 % | Saturation amount (grows with compressor squeeze) |
| Bias | 0 to 100 % | Tape bias offset and asymmetry |
| Brightness | 0 to 100 % | Tape roll-off and playback EQ: warm/soft to open/airy |
| Tone | 0 to 100 % | Machine-state macro: blends the tape stock, head gap, pre-bias and flutter between the classic slow machine (0 %) and the fast/hot machine (100 %) |
| Wow | 0 to 100 % | Slow transport pitch wander |
| Flutter | 0 to 100 % | Fast transport shimmer |
| Mix | 0 to 100 % | True dry/wet crossfade: 0 % is dry, 100 % is fully tape. Displayed as a percentage; default 50 % |
| Output | -32 to +32 dB | Calibrated output trim in dB |
| Width | 0 to 100 % | Mono through natural to extra wide |
| Bypass | on/off | Ramps the whole tape engine out without clicking |
| Tape Type | J37 / Ampex 456 / Studer A800 / Chrome / Type 111 / GP9 / Quantegy 499 / RTM SM911 | Model character (also shapes the glue time constants) |
| Speed | 7.5 / 15 / 30 ips | Transport speed, affects modulation, top end and glue timing |
| Oversampling | Off / 2x / 4x / 8x | Runs the tape engine at a higher internal rate to reduce aliasing; the added latency is reported to the host |
| Polarity | on/off | Inverts the output polarity (180-degree flip), after the protection chain and the meters' magnitude path |
| Auto Gain | on/off | Lets the slow programme compensator restore the level the INPUT trim dialled in; off leaves the output exactly at the level the chain produced |
| Presets | 18 factory | Loaded from the preset box; each application is one undoable step |
| User presets | unlimited | SAVE stores the whole machine state as a `.j37tape` file in the user's application-data directory; DEL removes the selected file; recall is one undoable step |
| A/B compare | two slots | COPY A / COPY B store states, A/B swaps them live; an EDITED badge shows when the sides differ |
| Undo / Redo | full state | Ctrl+Z / Ctrl+Y (or the panel buttons) step through preset and A/B history |

The percentage controls (Drive, Bias, Wow, Flutter) use a skewed knob taper so the
gentle end of each control gets more travel. This is purely ergonomic and does **not**
make the processing linear: the analogue nonlinearity lives in the DSP, not in the
control mapping. Knob drags are direct and fast - a full sweep takes about a third of the
panel width - and Shift engages fine control, so nothing feels sluggish at any setting.

### Analogue nonlinearity

This is a saturation model, so the transfer functions are deliberately nonlinear - that
is where the harmonics come from:

- The per-control `pow()` curves (Drive 1.45, Bias 1.30, Wow 1.55, Flutter 1.45) make each
  stage bend progressively harder as it is pushed rather than responding proportionally.
- `magneticHysteresis()` uses a single saturating branch, normalised so its slope at the
  origin is exactly 1. That is the property that makes DRIVE mean something: at zero drive
  the curve is a gentle tape bend, and the amount of compression at the top is set purely by
  the drive-scaled slope. A delayed memory term gives tape its "sticky" transient
  behaviour, and the level-dependent bias term is what produces the even harmonics that make
  tape sound warm rather than merely clipped.
- Because clamping costs level, the tape stage tracks how much amplitude the shaper removed
  and pays a little of it back per sample, so DRIVE changes the tone instead of doubling as
  a volume control. `finalOutputGain` handles only the static calibration; the two do not
  fight each other.
- **DRIVE at 0 % is unity gain into the record head and the shaper is near-transparent**,
  so the machine is clean until the control asks it not to be. This is worth stating because
  it was previously broken twice over: the shaper summed three saturating curves that
  stacked into permanent distortion, and DRIVE had a hard floor of 0.28 that pre-boosted the
  signal 38 % even at zero. Both are fixed.
- MIX is the one control that is a true linear crossfade, so the blend always agrees with
  its own readout.
- **BRIGHTNESS is a real playback high-shelf**: a fixed 8 kHz corner whose gain follows
  the control (0 dB at warm up to about +8.5 dB at bright). Earlier versions moved the
  corner frequency and folded it into a tilt stage shared with the TONE macro, which made
  the knob's action faint and unpredictable; the shelf is now fixed-corner, monotonic and
  independent of the macro.

Speed also changes head-gap damping, so a faster tape genuinely keeps more top end.

The tape glue compressor is compressor-coupled in two places, and the two stages are
completely independent processors: each has its own detector envelope and its own gain
computer, and neither reads the other's state. Both are driven purely by the signal
that reaches them plus the Input / Output parameters, while their attack/release
time constants follow the tape formula and transport speed (see above).

The **input stage** sits immediately after the input trim, so INPUT pushes this signal
into a real recorder-input stage and the tape always hears a controlled level. The
**output stage** sits immediately before the output trim, so the headroom it creates is
spent directly on OUTPUT.

Turning INPUT or OUTPUT up moves that stage's threshold down (roughly -17 dB to -3 dB),
steepens its ratio and allows more reduction; turning it below the middle backs the
stage off completely, at which point it is genuinely transparent. Each stage pays back
roughly 30-65 % of the reduction it applies as makeup, weighted by how hard its trim
control is driving it, so neither stage quietly undoes the level the user dialled in.

### Compressor-coupled saturation

The glue stages and the tape stage share one machine, so they are coupled both ways:
the total gain reduction both stages are applying (smoothed over about 200 ms, so it
follows programme density rather than individual hits) adds drive on top of the DRIVE
control and thickens the magnetic curve itself. The result is exactly what happens
when a compressed signal is pushed into a real record head: dense, slammed programme
saturates noticeably harder, sparse programme stays clean. With zero reduction the
shaper is exactly what the DRIVE control set - the coupling only ever adds on top.

### Tape formula and speed shape the glue timing

The attack and release time constants of both stages are derived from the loaded tape
formula and the selected transport speed, the way a real machine's glue behaves:

- **Tape type** - the soft classic stock moves slower and warmer (more head bump in the
  envelope); hotter and chrome stocks move faster and tighter.
- **Speed** - 7.5 ips stretches the constants (lazier flux build-up, print-through),
  30 ips shortens them (tight, immediate). The multipliers ride the same speed scale
  as the head damping, so SPEED keeps one coherent meaning across the whole machine.

The semi-automatic programme adaptation (transient vs sustained, envelope fill, load)
still runs on top of these bases, as described below in the header documentation.

## Noise floor

The tape hiss is a continuous band-limited noise floor whose **pause level is calibrated
below -32 dBFS** - inaudible by design - and whose level under signal can only ever go
**down**, never up: once per block, the mean gain reduction the output glue stage and the
safety limiter apply is measured, smoothed over about 250 ms, and the floor is ducked by
that amount (with a hard cap so the leveller can never lift the hiss back above its pause
level). So in a pause you hear the machine's quiet calibrated floor, and while material
plays the hiss sits even lower, hidden under the programme. The noise never swells and
never outweighs the signal - the floor is a property of the machine, not of the momentary
programme.

The fine tape-surface **grain modulation is gated by the transport**: with both Wow and
Flutter closed, no modulation of any kind reaches the wet signal - there is no hidden
"noise generator" running when the transport is switched off. It fades in only as the
transport controls are opened, like real tape mechanics.

The wet path is AC-coupled at the playback end (an 8 Hz DC blocker, exactly like the
coupling capacitors in real playback electronics), so the asymmetric shaper's DC
offset never reaches the compressor, limiter or clipper. This is what keeps MIX at
100 % sounding like a warm saturated signal instead of an off-centre, congested one.

## Metering

The meters panel shows four VU-style displays in a 2 x 2 grid:

| Position | Meter | Reads |
| --- | --- | --- |
| top left | INPUT | input level, four-way loudness |
| top right | OUTPUT | output level, four-way loudness |
| bottom left | COMP IN | reduction of the compressor after the input trim |
| bottom right | COMP OUT | reduction of the compressor before the output trim |

Each compressor stage publishes its own reduction and detector activity from the audio
thread, so the two glue meters are independent readings rather than one split value.

### Four-way loudness

Both level meters show the same signal four ways, because each scale answers a different
question and none of them alone is enough:

| View | What it is | Why it is there |
| --- | --- | --- |
| **PEAK dB** | block peak in dBFS | the only view that reports clipping |
| **RMS** | electrical average of the block | the honest baseline |
| **LUFS** | K-weighted, ITU-R BS.1770 | tracks perceived loudness rather than voltage |
| **VU** | 300 ms ballistic average | the classic programme-level display |

LUFS is computed with real K-weighting (high-shelf plus 38 Hz high-pass, rebuilt from the
sample rate), not an approximation, so the reading is correct at 44.1, 48, 88.2, 96, 176.4
and 192 kHz. The coefficients derive from `tan(pi * f0 / rate)`, which is exact at any rate.

**AVG** is the equal-weighted average of all four and is what the needle and the dial
ride. Both the needle and the dial show that one trustworthy value while the text block
shows exactly how it was arrived at. Averaging in dB rather than linear matters: a single
silent view would otherwise drag a linear average toward -infinity. (The row is captioned
"AVG", not "MIX": it has nothing to do with the MIX control.)

The spread between the rows is itself information: a large PEAK-to-LUFS gap means very
dynamic material, and a large VU-to-RMS gap means a lot of transient content.

## Oversampling

The magnetic hysteresis is a nonlinear process, so it aliases: harmonics generated above
Nyquist fold back down into the audible band. The OVERSAMPLING switch (Off / 2x / 4x) wraps
the whole tape engine in a polyphase half-band upsampler, runs the model at two, four or
eight times the session rate, and filters back down. The added filter delay (zero at Off,
a few samples at 2x/4x/8x) is reported to the host through `setLatencySamples`, so DAW PDC
compensates automatically. The four engines exist side by side with independent filter
state, so switching mid-playback is glitch-free.

## Presets, A/B compare and undo

Eighteen factory presets cover the machine's range from gentle bus warmth to slammed drum
tape, including vocal, bass and mastering-safety starting points. A preset replaces the whole machine state in a single undoable transaction - Ctrl+Z
brings back exactly what was on screen before.

**User presets** go further: SAVE freezes the whole machine exactly as it stands into a
`.j37tape` file (an XML state tree, the same format the session stores) under
`<user app data>/J37 Tape Mastering/Presets`. Files survive plugin updates, are shared by
every instance, and recall as one undoable step. A badge under the workflow row names the
loaded preset and lights while the live state has drifted away from it - including when
host automation moves a parameter.

The A/B section stores two complete states:
COPY A / COPY B capture the current settings into their slot (the live side mirrors your
edits continuously), the A/B button swaps the sides live, and an A/B EDITED badge lights
while the two stored sides differ. Undo history covers factory and user preset loads and
A/B recalls as full-state transactions, so one step moves the whole machine, never half of
it.

## Output-stage switches

Two switches sit at the very end of the chain:

- **POLARITY** flips the output sign (a 180-degree rotation, not a phase shift). It is
  applied after the limiter and clipper, and the VU/metering path reads magnitudes, so
  the displays stay honest either way.
- **AUTO GAIN** gates the slow programme compensator. On (default) the plugin keeps
  tracking the level the INPUT trim dialled in; off hands full level responsibility to
  the OUTPUT trim alone, which is how many engineers prefer to ride a mastering chain.

## Output protection

Two stages keep the output clean, in this order:

1. **Safety limiter.** A fast peak-follower pulls the gain back before the signal reaches
the ceiling. It is a *gain* stage, not a shaper, so it adds no harmonics of its own - the
only distortion in the output is the tape stage's. Normal material is limited
transparently.

2. **Soft clipper.** Only what still overshoots (a peak faster than the limiter's attack)
reaches this. It is a linear region up to 0.70, then blends smoothly into a saturating
exponential, so:
   - below the knee the output is bit-for-bit the input, i.e. transparent for normal level;
   - the value is continuous at the knee and the slope only changes gradually, so there is
     no audible corner;
   - it approaches a 0.985 ceiling asymptotically, so **no input, however large, can clip**.

A hard `jlimit (-1, 1)` is deliberately not used anywhere: it converts overshoot into a
flat-topped square edge, which is digital clipping, not tape behaviour. The meter's red
PEAK row means the *clipper* had to act, not merely that the signal was loud.

Order matters here: stereo width and the output trim are applied **before** the limiter, so
a boost on OUTPUT cannot push the signal past the ceiling the limiter just established.

## Final gain compensation

After the last compressor, and before the output trim, a slow compensator compares the
finished signal against a reference taken **straight after the input trim, before the
first compressor**. That reference is the level the user dialled in with INPUT, so it is
what the output should still track once the tape stage and both compressors have done
their work.

- It only ever **restores** a loss, never exaggerates: the correction is clamped to a
  positive range, so it cannot turn into a hidden boost stage.
- It is smoothed with a slow one-pole (about 450 ms), far slower than either compressor,
  so it settles on the programme level and does not pump with transients. The
  compressors keep their punch.
- The measurement is taken *before* the correction is folded in, so it cannot chase its
  own tail.

This is why the plugin keeps a steady output level as DRIVE, TAPE TYPE, SPEED and MIX are
changed, instead of drifting louder or quieter with every edit.

## Platform notes

The plugin is written to behave the same at any sample rate and on any display.
Supported rates are **44.1, 48, 88.2, 96, 176.4 and 192 kHz**.

- **Sample rate** - every time-domain constant is rebuilt in `prepareToPlay` through
  `resetSampleRateDependentState()`. That includes the cached brightness filter
  coefficients, which previously were only refreshed when the Brightness control moved and
  so stayed tuned to the old rate after a switch.
- **Time, not samples** - filters and detectors are specified as time constants and
  converted with `1 - exp(-1 / (rate * seconds))`, so a 400 ms LUFS window, a 300 ms VU
  ballistic and a 0.5 ms limiter attack all keep their meaning when the rate quadruples.
  Nothing is expressed as a fixed sample count where it should be a duration.
- **Hiss is band-limited** - the tape noise runs through a ~16 kHz one-pole rather than
  white to Nyquist. Without it, a 192 kHz session would spread hiss across 96 kHz and it
  would read as bright digital noise rather than tape. Because the bandwidth is fixed, its
  gain needs no rate compensation; the earlier `sqrt(rate)` term existed only to keep
  white-noise energy constant and would double-compensate on top of the band limit.
- **No fixed Nyquist fractions used as frequencies** - where a limit depends on Nyquist
  (the harmonic analyser's bins, its frequency tracker) the bound is written as a multiple
  of the rate with a `jmax` floor, so the two bounds in a `jlimit` can never cross over.
- **Harmonic analysis** - the Goertzel window is a fixed *duration* (11.6 ms, which is 512
  samples at 44.1 kHz), so its bin width is constant. A fixed 512-sample window would be
  only 2.7 ms at 192 kHz, giving a 375 Hz bin width that cannot separate a 1 kHz harmonic
  series. The analysis stride is likewise rate-scaled, so it runs at a constant rate per
  second instead of four times as often.
- **Buffer size** - the DSP is per sample and reads `getNumSamples()` each block, so
  there is no fixed block-size assumption; zero-length blocks are handled too.
- **Display scale** - the editor opens at 980 x 690 and can be resized between
  780 x 640 and 1400 x 960; both meter types lay themselves out proportionally to their
  own bounds. Text scales with the meter, so nothing is drawn at a hardcoded pixel size.
  The deck is three control lines - transport, switches / oversampling, then the preset /
  A/B / undo row - with a badge band of its own underneath, and every line's control
  chain is width-accounted to fit the minimum 780 px panel with no overlap. The level
  meters keep clear air between the dial and the readout rows, and each level
  meter's title and subtitle are carved out of one proportional top block as two
  non-overlapping bands (the old arithmetic started the subtitle *before* the title
  ended, so the two lines printed on top of each other).
- **Tooltips** - every knob and switch carries a tooltip explaining what it does and its
  default; the tips appear quickly on hover (about a third of a second).
- **Fonts** - the title uses a fallback chain rather than a Windows-only family, and the
  fixed-width A/B, compare and undo/redo captions are measured and scaled to their buttons
  so a caption that grows (`A (LIVE) *`) can never ellipsise into unreadable text.
- **Toggle switches** - BYPASS, POLARITY and AUTO GAIN are drawn as small hardware
  rockers by the panel's own Look and Feel: the function name engraved across a
  proportional top band, a recessed track below with a single sliding thumb that
  carries the only state word (`OFF` or `ON`), and a status LED inset on the right of
  the caption band that lights when engaged. The previous version stacked a hardcoded
  11 px label band over a 15 px track and printed `OFF`, `ON` *and* the thumb caption
  at once - that is what made the switches look crowded and strange. The redundant
  stop captions are gone, the band height is a proportion of the control, and the
  thumb is sized to exactly half the track so it can never overhang either end.
- **Decorative artwork stays out of the way** - the spinning reel sits in the deck's
  heading corner, the tape ribbon hangs inside the free corridor between the
  oversampling switch and the harmonics readout, and the drifting dust particles are
  confined to that corridor *on the switches row only*, so no decoration ever prints
  over a caption or a switch. The deck divider is drawn one pixel above the panel's
  bottom edge, below the preset badge, so it no longer cuts through the badge text.
- **OpenGL** - treated as a best-effort accelerator; if a context cannot be created the
  panel falls back to the normal component renderer.

## Project type

- Audio plugin: **VST3**, **Audio Unit** and **AUv3**
- Framework: JUCE 9.0.2 (pinned as a git submodule)
- Target platforms: Windows (x64), macOS (universal: arm64 + x86_64)
- Build system: **CMake** (3.22+), driving MSVC on Windows and Xcode on macOS

AU and AUv3 are macOS-only formats. JUCE compiles them to nothing on Windows, which is why
the Windows build produces VST3 only - the format simply does not exist there. The single
`FORMATS` list in `CMakeLists.txt` is shared by both platforms and JUCE selects what each
one can build.

## Getting the source

JUCE is a **git submodule pinned at 9.0.2**, so it must be checked out along with the
project. A plain clone leaves `JUCE/` empty and every build fails:

```sh
git clone --recursive <repo-url>
# or, if already cloned:
git submodule update --init --recursive
```

Pinning it this way removes a whole class of "works on my machine" failures. `CMakeLists.txt`
reaches JUCE with a single `add_subdirectory(JUCE)`: there are no module search paths to
configure and therefore none to get wrong per platform. Every contributor builds against
byte-identical JUCE sources and does not have to install JUCE at any particular location.

## Build

The build is driven by `CMakeLists.txt`, which is the single source of truth. There is no
`.jucer` file and no checked-in project files: CMake generates them from the script.

Builds are out-of-source, so nothing lands in the source tree.

### Windows

```sh
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

Artefacts land in `build/first_artefacts/Release/`, one subfolder per plugin format, with
each bundle named after `PRODUCT_NAME`:

```
build/first_artefacts/Release/VST3/Analog Saturator.vst3/Contents/x86_64-win/...
```

> This is a real VST3 **bundle**, not a bare DLL. The old Projucer workflow produced a
> flattened file and needed `flatten-vst3.cmd` to collapse the bundle; CMake produces the
> correct structure directly, so that script is gone. If a host or script you use expects
> the flattened layout, point it at the path above instead.

### macOS

```sh
cmake -S . -B build -G Xcode \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
  -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO
cmake --build build --config Release --parallel
```

`CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` produces a **universal binary** that runs natively
on both Apple Silicon and Intel Macs. Leaving one architecture out makes the plugin
invisible to half the machines it is installed on, so both are always listed.

`-G Xcode` is **required** for the AUv3 build. JUCE only adds the AUv3 target when the
generator is Xcode (see `_juce_get_platform_plugin_kinds` in
`JUCE/extras/Build/CMake/JUCEModuleSupport.cmake`); with the default Unix Makefiles
generator the AUv3 format is silently dropped and a "build all" run produces VST3 and AU
only. If you are unsure, run `cmake --build build --target help | grep AUv3` and switch
generators if the target is absent.

Three formats are produced, one per format subfolder under `build/first_artefacts/Release/`:

| Artefact | Format | Hosts that load it |
| --- | --- | --- |
| `VST3/Analog Saturator.vst3` | VST3 | Reaper, Cubase, Studio One, Bitwig |
| `AU/Analog Saturator.component` | Audio Unit | Logic Pro, GarageBand, Final Cut |
| `AUv3/Analog Saturator.appex` | AUv3 | Logic Pro, GarageBand, iOS hosts |

AU and AUv3 matter for Mac users because **Logic and GarageBand cannot load VST3 at all**.
Shipping only VST3 means the plugin does not exist for them.

### Installing locally

By default the build does **not** write into your plugin folders, so a CI run can never
silently overwrite installed plugins. To install as part of the build:

```sh
cmake -S . -B build -DCOPY_PLUGIN_AFTER_BUILD=TRUE
cmake --build build --config Release
```

Or copy by hand:

```
Windows : C:\Program Files\Common Files\VST3\
macOS   : ~/Library/Audio/Plug-Ins/VST3/Analog Saturator.vst3
          ~/Library/Audio/Plug-Ins/Components/Analog Saturator.component
```

AUv3 is discovered through its containing app rather than copied by hand; run the app that
wraps the extension once so the system registers it.

## Build system notes

### Why CMake, and what changed

The project previously used the Projucer, which generates project files (`.vcxproj`, `.pbxproj`)
from `first.jucer`. That generation step caused a repeating class of problems, all of them
present in this repository's history:

- **Two sets of module paths** (one for Visual Studio, one for Xcode), which drifted apart and
  ended up containing absolute paths like `D:\Desktop\Programs\JUCE\modules`.
- **Generated files being edited by hand**, then silently reverted the next time the project
  was re-saved. The precompiled-header configuration had to be patched in the `.vcxproj`
  more than once for exactly this reason.
- **No standard way to build from a terminal**, so CI had to drive MSBuild and `xcodebuild`
  directly against generated files.

CMake removes all three: the script is the source of truth, paths are relative by
construction, and one command builds on every platform.

### Module set

Only the modules the plugin actually uses are linked, instead of the twenty-four the
Projucer project enabled:

| Module | Used for |
| --- | --- |
| `juce_audio_processors` | `AudioProcessor`, parameter tree, format wrappers |
| `juce_audio_utils` | shared plugin UI helpers |
| `juce_dsp` | `Oversampling`, `AudioBlock` |
| `juce_gui_extra` | drawing extras used by the editor |
| `juce_opengl` | `OpenGLContext`, the best-effort panel accelerator |
| `juce_box2d` | decorative physics particles on the tape deck |

### Precompiled headers

Not used, and never were in the final setup. Every translation unit compiles its own copy
of the JUCE headers. A PCH only works when the header name, the `/Yc`/`/Yu` options and a
matching top-of-file `#include` in every participating source all agree; when they drift,
MSVC reports it as `C2857` plus `C1010` rather than as one readable error. `CMakeLists.txt`
passes no PCH options at all, so that failure mode cannot occur.

### Warning flags

`juce::juce_recommended_warning_flags` is enabled. One warning is deliberately kept
visible rather than suppressed: a `size_t` to `int` narrowing in `setStateInformation`,
where JUCE's `getXmlFromBinary` takes an `int`. It is harmless - a plugin state blob is
never close to 2 GB - but it is the only warning in the build and worth not losing.

### Signing and notarisation

CI builds are **unsigned**. A plugin that is not signed and notarised will be refused by
Gatekeeper on any modern macOS, and quarantine will block it on the user's machine even if
it launches locally:

```sh
codesign --deep --force --options runtime \
  --sign "Developer ID Application: <name> (<TEAMID>)" first.vst3

xcrun notarytool submit first.vst3.zip --keychain-profile <profile> --wait
xcrun stapler staple first.vst3
```

This needs a paid Apple Developer account and the certificate stored as a CI secret.

### macOS-specific notes

- **AUv3 is sandboxed.** The app extension needs `com.apple.security.app-sandbox` in its
  entitlements or the host refuses to load it. `CMakeLists.txt` writes
  `Builds/AUv3Support/AUv3_AppExtension.entitlements` if it is not present and attaches it
  to the AUv3 target.
- **Display scaling needs nothing from the plugin.** JUCE component coordinates are logical
  and the host multiplies them by the display scale, so `setSize()` must **not** be
  pre-multiplied by `Display::scale`. Doing so applies the scale twice and the window opens
  far too large - which is exactly the bug this project had.
- **OpenGL is best-effort.** It is only an optimisation for the animated panel and the least
  portable part of the UI. `attachTo()` returns `void`, so availability is checked with
  `isAttached()`; if no context can be created the panel falls back to the normal component
  renderer with no visual difference. This matters on macOS, where OpenGL is deprecated in
  favour of Metal but still functional.
- **No platform-specific code in `Source/`.** There are no `_WIN32` guards and no
  Windows-only APIs; the only platform branches are JUCE's own macros.

## Repository notes

```
CMakeLists.txt   the build, and the only place build settings live
Source/          the plugin: PluginProcessor, PluginEditor, and DSP
JUCE/            JUCE 9.0.2, pinned as a git submodule
.github/         CI: builds VST3 on Windows, VST3 + AU + AUv3 on macOS
```

There are no generated project files in the repository. Open the folder directly in CLion,
Visual Studio or VS Code with the CMake extension, and the IDE will configure itself from
`CMakeLists.txt`.

## License

This repository is for project and development use.
