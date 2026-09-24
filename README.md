# first

**J37 Tape Mastering** - a JUCE-based VST3 **mastering-grade tape saturation plugin** inspired by classic analog tape machines and J37-style coloration.

This is, first and foremost, a **bus / mastering tool**: two independent glue compressors wrap the tape stage, the output level is calibrated in dB, the loudness metering is four-way (peak / RMS / LUFS / VU), and the output protection chain guarantees that what leaves the plugin is clean and controlled. Use it on the master bus, a drum bus or any programme material where you want the density and warmth of tape without losing control of the level.

## Overview

This project is a mastering-oriented audio effect plugin built for Windows with JUCE and Visual Studio.
It provides a tape-saturation mastering workflow with:

- drive and harmonic character controls
- a **TONE macro** that crossfades the whole machine state (tape stock, head gap, pre-bias, flutter) between the classic slow machine and the fast/hot machine
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
| Tape Type | J37 / Ampex 456 / Studer A800 / Chrome | Model character (also shapes the glue time constants) |
| Speed | 7.5 / 15 / 30 ips | Transport speed, affects modulation, top end and glue timing |

The percentage controls (Drive, Bias, Wow, Flutter) use a skewed knob taper so the
gentle end of each control gets more travel. This is purely ergonomic and does **not**
make the processing linear: the analogue nonlinearity lives in the DSP, not in the
control mapping.

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
- **Display scale** - the editor opens at a compact 960 x 600 and can be resized
  between 780 x 540 and 1400 x 900; both meter types lay themselves out proportionally
  to their own bounds. Text scales with the meter, so nothing is drawn at a hardcoded
  pixel size. The transport-deck controls chain off each other's edges rather than
  sitting at absolute offsets, so nothing collides at the minimum size, and the level
  meters keep clear air between the dial and the readout rows.
- **Fonts** - the title uses a fallback chain rather than a Windows-only family.
- **OpenGL** - treated as a best-effort accelerator; if a context cannot be created the
  panel falls back to the normal component renderer.

## Project type

- Audio plugin: VST3
- Framework: JUCE 9.0.2
- Target platform: Windows
- Build system: Visual Studio/MSBuild

## Build

1. Open the solution in `Builds/VisualStudio2026/first.sln`.
2. Build the project in `Release` configuration for `x64`.
3. Load the generated VST3 plugin in your DAW.

Precompiled headers are **not** used. Every translation unit compiles its own copy of
the JUCE headers, which costs some build time but removes a whole class of failures: a
PCH only works when the `PrecompiledHeader` mode, the `PrecompiledHeaderFile` name and a
matching top-of-file `#include` in every participating source all agree, and when they
drift MSVC reports it as `C2857` on `pch.cpp` plus `C1010` on the plugin sources rather
than as one readable error. `Builds/VisualStudio2026/pch.h` is kept as an ordinary
umbrella header and is not included by anything.

Release builds use `Optimization: MaxSpeed` with `FavorSizeOrSpeed: Speed` and
multi-processor compilation.

> Note: `first_SharedCode.vcxproj` is generated by the Projucer from `first.jucer`. If the
> project is re-saved from the Projucer, re-check that no `<PrecompiledHeader>` metadata
> has reappeared on the `ClCompile` items.

## Repository notes

The project is structured as a standard JUCE plugin repository with generated Visual Studio files and custom DSP in `Source/`.

## License

This repository is for project and development use.
