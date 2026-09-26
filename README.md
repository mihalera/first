# Nonlin Analog Saturator

**Nonlin Analog Saturator** - a JUCE-based **mastering-grade analog saturation plugin** built around a nonlinear magnetic tape model, with switchable tape stock, transport speed and tape-style coloration.

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
playback EQ tilt -> playback DC blocker -> second playback head (DELAY) ->
stereo tape offset (ST OFFSET) -> output glue compressor (always on) ->
output trim (dB) -> stereo width.

The transport state (STOP / PLAY / START) scales the whole wet side of that path and
the transport modulation together, so STOP is the machine coming to rest rather than a
mute on the output.

## Tape delay

A spare playback head on a real deck is spaced away from the record head, so the same
signal comes back a fixed interval later - the interval being set by the gap and the
tape speed. DELAY is that spacing in milliseconds, and the range is short on purpose:
at 15 ips a real head spacing gives tens of milliseconds, so the point is the slap and
the comb colour a second head adds to a tape sound, not an echo unit.

The repeat is taken **after** the tape, from the DC-blocked wet signal, which is what
makes it a head on the machine rather than a parallel effect: the echo inherits the
machine's own bandwidth and saturation, and each pass round the tape loses top end the
way a real repeat does. It is a single interpolation-read circular buffer, sized once
in `prepareToPlay` for the longest time the control can ask for at the highest rate the
engine can run at, so the DELAY knob moves a read offset and never resizes a buffer on
the audio thread.

## Stereo tape offset

On a real stereo deck the two tracks are recorded by separate head gaps a fraction of a
millimetre apart, and the tape skews slightly across them. The result is that the
channels are not perfectly time-aligned: one lags the other by a few tens of
microseconds. It is a small effect and a large part of why a tape bounce sounds wide
rather than merely being equalised wide.

ST OFFSET sets that inter-channel delay directly in microseconds, positive meaning the
right channel lags. It is a linear-interpolating buffer rather than an all-pass: an
all-pass gives the same group delay for less memory but colours the phase differently
across the band, and the point here is that the two channels differ by **time**, not by
filter shape.

## Transport: start, play, stop

A tape machine has three states and the middle one is not "stopped":

| State | What the machine does |
| --- | --- |
| **Stop** | The capstan is at rest. The tape is not moving, so there is no hiss, no modulation and no delay tail. The wet path coasts down and the machine reaches true silence - not a mute on the output, because the machine's own noise goes with it |
| **Play** | Normal running. Everything the panel describes is active. This is the default and what every earlier build did |
| **Start** | The moment of engagement. The capstan comes up to speed, so the transport runs flat, the modulation deepens and the pitch rides up into tune over about a second - the sound a deck makes when you hit play on a take |

The ramp is advanced once per **frame**, not per channel: advancing it per channel would
put the two sides a sample apart, which is a channel skew rather than a transport. A
START from STOP gets a full one-second spin-up; a START from PLAY is a re-engagement and
gets a much shorter re-lock, because a button press that changed nothing should not
slide the pitch for a second.

## Noise floor level

TAPE TYPE sets each formula's own hiss floor as part of its character and that stays
untouched. NOISE is a trim **on top of** it, so the floor can be lifted for a
deliberately dirty bounce or pulled to a clinical black without changing which stock is
loaded. 50 % is exactly the formula's own figure - the neutral position, not a change -
and the trim is carried by a smoother, so dragging the knob glides the hiss instead of
stepping it. Like the formula's own floor it is gated by the transport, so a machine at
rest stays silent.

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
| Sub-Fundamental (SUBFUND) | 0 to 100 % | Subharmonic saturation: an eight-stage undertone cascade (1/2 ... 1/9 of the tracked bass fundamental) summed under the tape signal, per channel. Default OFF |
| Output | -32 to +32 dB | Calibrated output trim in dB |
| Width | 0 to 100 % | Mono through natural to extra wide |
| Bypass | on/off | Ramps the whole tape engine out without clicking |
| Tape Type | J37 / Ampex 456 / Studer A800 / Chrome / Type 111 / GP9 / Quantegy 499 / RTM SM911 | Model character (also shapes the glue time constants) |
| Speed | 7.5 / 15 / 30 ips | Transport speed, affects modulation, top end and glue timing |
| Oversampling | Off / 2x / 4x / 8x | Runs the tape engine at a higher internal rate to reduce aliasing; the added latency is reported to the host |
| Polarity | on/off | Inverts the output polarity (180-degree flip), after the protection chain and the meters' magnitude path |
| Auto Gain | on/off | Lets the slow programme compensator restore the level the INPUT trim dialled in; off leaves the output exactly at the level the chain produced |
| Presets | 18 factory | Loaded from the preset box; each application is one undoable step |
| User presets | unlimited | SAVE stores the whole machine state as a `.nonlinpreset` file in the user's application-data directory; DEL removes the selected file; recall is one undoable step |
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

### Tape formulas

Twelve stocks, each biasing the blended machine state toward its own character
rather than replacing it - so the TONE macro keeps its meaning on every one:

| Formula | Character |
| --- | --- |
| **J37** | The classic EMI reference: soft, gentle bend, the most neutral starting point |
| **Ampex 456** | Hotter, more low-order colour, an open head |
| **Studer A800** | The darkest and densest saturation of the set |
| **Chrome** | Clean and bright with a low noise floor |
| **Type 111** | Gentle low-noise mastering stock: very quiet, soft top end |
| **GP9** | Hot modern mastering formula: dense low end, a higher floor |
| **Quantegy 499** | High-output studio workhorse: open top, firm glue |
| **RTM SM911** | Broadcast reference: balanced, smooth, low noise |
| **SM 468** | High-output low-noise studio stock: a firm bend with a notably quiet floor and an open head, so it reads as clean density rather than colour |
| **888** | The hot, thick vintage stock: strong bias asymmetry and the thickest magnetic memory here, so it bends early and blooms hard. The formula to reach for when the saturation *is* the effect |
| **815** | Dark, dense and quiet at the top: a soft head and a heavy low-mid bias make it the warmest of the new stocks without the extra hiss 888 brings. It thickens rather than drives |
| **811** | The clean, open, low-noise mastering stock: the gentlest bend of the set with the most open head, so it stays transparent under level and keeps the top octave. A bus stock, not a colour |

Every formula's figures are clamped with the INSTRUMENT voicing on top, so any
combination of the two selectors stays inside the machine's designed operating range.

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

The tape hiss is a continuous band-limited noise floor whose level under signal can only
ever go **down**, never up: once per block, the mean gain reduction the output glue stage
and the safety limiter apply is measured, smoothed over about 250 ms, and the floor is
ducked by that amount (with a hard cap so the leveller can never lift the hiss back above
its resting level). While material plays the hiss sits hidden under the programme. The
noise never swells and never outweighs the signal - the floor is a property of the
machine, not of the momentary programme.

**The floor is gated by the transport.** Tape hiss is the sound of oxide moving across
the playback gap, so a machine at rest is silent: with both Wow and Flutter closed the
floor coasts down to true silence (a slow 750 ms fade, never a mute-switch drop), and
opening either control spins the machine up and the hiss back. Earlier builds left the
floor always-on, which was audible as noise during a pause even with the transport
controls at zero - fixed.

The fine tape-surface **grain modulation is gated by the same transport gate**: with both
Wow and Flutter closed, nothing at all is generated - no modulation, no floor, no hidden
"noise generator". Both fade in only as the transport controls are opened, like real tape
mechanics.

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
`.nonlinpreset` file (an XML state tree, the same format the session stores) under
`<user app data>/Nonlin Analog Saturator/Presets`. Files survive plugin updates, are shared by
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
- **No clicks when a knob moves** - the two coefficients that *multiply* the signal from
  a control, the BRIGHTNESS shelf gain and the TONE macro's record-head pre-bias, ramp
  over 20 ms (`SmoothedValue`) instead of being applied raw. They are recomputed the
  instant either knob moves, and a stepped gain dropped straight into the per-sample loop
  put a discontinuity in the waveform on every block boundary - heard as crackle while
  the knob was dragged. INPUT / OUTPUT / MIX / WIDTH / BYPASS were already safe (smoothed
  values) and WOW / FLUTTER are oscillators, so neither can step the waveform.
- **BIAS and DRIVE morph the curve, they do not step it** - the magnetic shaper takes
  two arguments that shape the transfer curve itself, and BIAS's is a DC offset added
  straight into the `tanh` and subtracted again at the output. That made it far louder
  than any linear control when it moved: stepping it shifts the whole curve, and the
  playback DC blocker downstream then has to swallow the resulting step. Both arguments
  now ramp through `SmoothedValue` over 20 ms, so the curve morphs continuously.
  Smoothing only the shaper arguments turned out not to be enough, so the rest of the
  tape path is ramped too: DRIVE's pre-drive gain, BRIGHT's record pole, TONE's two
  playback poles, the flutter depth and the hiss level. Every control-derived multiplier
  and pole in the wet path is now continuous - a step in any of them is a discontinuity
  in the waveform, and that was the crackle.
- **Level meters no longer overbook their cell** - the readout rows used to be sized from
  a fixed share of the meter body and only then clamped to fit, which pushed the first
  row back on top of the dial and left the last row one pixel from the rounded border.
  The rows now take the space that genuinely remains below the dial (floored, never
  rounded up, so they can never creep back into it) and a real bottom margin is reserved.
  Measured on the 980 x 690 default, the first row overlapped the dial box by 4 px, the
  needle pivot by 2 px and the end tick label by 3 px; all three are now 0, with 4-7 px
  of clear air underneath.
- **The meters' gauge arc is on the same half as its own ticks** - JUCE's arc angles run
  0 = 12 o'clock and increase *clockwise*, while the ticks and the needle are placed with
  `cos`/`sin` where 0 = 3 o'clock. Handing `addCentredArc` the tick loop's `pi..2*pi` so
  swept the **wrong half of the circle**: the arc started at the bottom of the dial,
  bulged out to the left and finished at the top - the black half-circle that hung below
  the INPUT / OUTPUT meters and crossed the readout rows. The arc now runs
  `1.5*pi .. 2.5*pi`, which is exactly the left-over-the-top-to-the-right path the ticks
  and needle describe, and the whole dial is clipped to its own face so no gauge geometry
  can reach the readout rows whatever happens.
- **The switch captions are centred under their own switch** - the rocker caption was
  centred in a band with the lamp's footprint sliced off its right edge, which parked the
  text about 6 px left of the switch's true centre and made the lamp look crookedly
  placed. The caption is now centred in the full label band (at the three real captions -
  BYPASS, POLARITY, AUTO GAIN - the centred text still clears the lamp by at least
  15 px), and the lamp sits in a recessed bezel 8 px in from the border.
- **Editor thread stays out of the audio thread's way** - the panel used to re-mirror the
  live machine into the active A/B slot on *every* 30 Hz timer frame, and each call made
  two full deep copies of the parameter tree: sixty whole-tree copies a second, landing
  on the message thread in the middle of a knob drag, on top of the repaint work. The
  dropouts that follow are heard as clicks. Mirroring now runs at ~5 Hz, only while the
  editor is on screen, and skips the second copy when the live state already matches the
  stored slot.
- **Tooltips** - every knob and switch carries a tooltip explaining what it does and its
  default; the tips appear quickly on hover (about a third of a second).
- **Fonts** - the title uses a fallback chain rather than a Windows-only family. Text
  buttons are drawn through the panel's own Look and Feel, which measures each caption
  against that button's width and scales the font to fit - `juce::TextButton` has no
  per-button font, and without this the width-constrained A/B row ellipsised a caption
  that grows (`A (LIVE) *`) into unreadable text. The fit keeps a proportional safety
  margin (3 px floor) rather than a flat pixel: on the 38-44 px SAVE / DEL / UNDO / REDO
  buttons a caption measured as "just fitting" and still came out as `SA...` / `D...`.
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
- **Text keeps clear of the panel edges** - the level meters inset their readout rows by a
  share of the cell's own width instead of a hardcoded 2 px, so the left-aligned label
  and the right-aligned value stop looking pasted onto the rounded border. The preset
  badge band sits 4 px clear of the controls above it and 3 px clear of the divider below
  it (it used to hug the deck's bottom border with the divider right under it, reading as
  a caption that had fallen off the panel), is inset further than the other deck text, and
  fits its caption to the available width - so neither `FACTORY STATE` nor a long
  user-preset name can run into the edges or be clipped.
- **Build identity** - the deck's heading strip prints `BUILD <commit>`, so a bug report
  can name the exact build instead of "the latest one". CMake reads it once with
  `git rev-parse --short=8 HEAD` and passes it to the sources as `J37_BUILD_COMMIT`.
  Two rules keep it from ever breaking a build or lying about one: outside a git
  checkout (a tarball, a vendored copy) the build still succeeds and prints `BUILD
  unknown`, and a dirty working tree gets a trailing `+` so local edits can never be
  mistaken for a clean build. The label sits on the deck heading strip because that is
  the only full-width band near the top that stays empty at the 780 px minimum - the
  header's equivalent gap shrinks to about 58 px there.
- **OpenGL** - treated as a best-effort accelerator; if a context cannot be created the
  panel falls back to the normal component renderer.

## Project type

- Audio plugin: **VST3**, **Audio Unit** and **AUv3**
- Framework: JUCE 9.0.2 (pinned as a git submodule)
- Target platforms: Windows (x64), macOS (universal: arm64 + x86_64), Linux (x64, VST3)
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
build/first_artefacts/Release/VST3/Nonlin Analog Saturator.vst3/Contents/x86_64-win/...
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
| `VST3/Nonlin Analog Saturator.vst3` | VST3 | Reaper, Cubase, Studio One, Bitwig |
| `AU/Nonlin Analog Saturator.component` | Audio Unit | Logic Pro, GarageBand, Final Cut |
| `AUv3/Nonlin Analog Saturator.appex` | AUv3 | Logic Pro, GarageBand, iOS hosts |

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
macOS   : ~/Library/Audio/Plug-Ins/VST3/Nonlin Analog Saturator.vst3
          ~/Library/Audio/Plug-Ins/Components/Nonlin Analog Saturator.component
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

### Third-party libraries

Everything below JUCE is fetched by **CPM** at configure time and pinned by tag or
commit, so a fresh clone resolves the same versions forever. They are linked into the
plugin target so the pieces are *available*; the engine still uses its own code, and
adopting any piece is a per-piece decision that has to be made against a profile and
a listening test.

| Library | Pinned at | Why it is in the build |
| --- | --- | --- |
| `chowdsp_utils` | `v2.4.0` | Chowdhury DSP's toolbox. Provides `SmoothedBufferValue`, `LoudnessMeter`, `PitchDetector`, `Compressor`/`LevelDetector`, `Noise`, `Upsampler`, `SineWave` - a generation ahead of the hand-rolled equivalents in `PluginProcessor.h` |
| `melatonin_inspector` | commit `9c483f86` | JUCE component inspector, compiled under `JUCE_DEBUG` only. Passive in the shipped build |
| `melatonin_blur` | `v1.4` | Fast shadow/gradient blurring for JUCE Components - the editor draws a lot of soft analog shading by hand today |
| `xsimd` | `13.0.0` | Portable SIMD wrappers (SSE/AVX/NEON). `chowdsp::chowdsp_simd` already wraps xsimd, so this is the same code path rather than a second SIMD layer |

Two dev-only libraries are **off by default**, so a plugin build never drags a test
framework into its dependency graph. Enable with `-DJ37_BUILD_TESTS=ON`:

| Library | Pinned at | Why |
| --- | --- | --- |
| `Catch2` | `v3.7.1` | Unit tests for the pieces underneath the DSP harness - a single shaper, a single loudness filter, a single compressor detector |
| `nanobench` | `v4.3.11` | Micro-benchmarks. The shaper runs two `std::tanh` calls per sample per channel, so approximating or vectorising it is a real complexity cost that should only be paid once a benchmark shows the shaper actually dominates |

What is deliberately **not** here: an FFT library (the harmonic analyser needs three
bins, and a full transform would be slower than the Goertzel it uses) and a pitch
detection library (the sub-bass detector's zero-crossing tracker already works, and a
heavier tracker would add latency to a stage that is supposed to be tight).

### Precompiled headers

Not used, and never were in the final setup. Every translation unit compiles its own copy
of the JUCE headers. A PCH only works when the header name, the `/Yc`/`/Yu` options and a
matching top-of-file `#include` in every participating source all agree; when they drift,
MSVC reports it as `C2857` plus `C1010` rather than as one readable error. `CMakeLists.txt`
passes no PCH options at all, so that failure mode cannot occur.

### Why the Windows build is slower than the macOS one

Three separate costs, and only one of them is about compiling your code:

1. **`juceaide` runs during configure.** Windows needs `juceaide.exe` while *configuring*,
to generate the `.rc` resource script and the icon. macOS produces its plists with plain
`configure_file()` and never invokes juceaide at configure time. This is why `cmake -S . -B build`
on its own can cost as much as a whole Mac build - the time is spent before any source file is
touched, not inside it. It cannot be avoided.

2. **`/GL` + `/LTCG`.** Whole-program optimisation makes MSVC write an intermediate
representation for every object and read it back at link time, roughly doubling the work.
Clang on macOS does LTO with the same flag and markedly less overhead. This is the largest
*avoidable* item, and it is only worth its cost for a release binary.

   `CMakeLists.txt` exposes `J37_FAST_WINDOWS_BUILD` (default `ON`) which clears the
   `juce_recommended_lto_flags` interface, dropping both flags. Configure with
   `-DJ37_FAST_WINDOWS_BUILD=OFF` for a distributable build.

   Note for anyone editing that block: the flags arrive through a linked **interface**
target as generator expressions (`$<$<CONFIG:Release>:-GL>` for compile, and the link flag
is attached with `target_link_libraries`, not `target_link_options`). Filtering this
target's own `COMPILE_OPTIONS` or `LINK_OPTIONS`, or matching the literal `-GL`, finds
nothing. Clearing the interface is the only thing that takes effect.

3. **Parallelism.** MSBuild parallelises across *projects* by default, and this solution has
only a handful, so most cores sit idle. `/MP` parallelises within a project and
`CMAKE_BUILD_PARALLEL_LEVEL` controls the build-level parallelism. Both are set.

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

### Linux

Linux builds the VST3 through the same CMake script. JUCE's Linux deps are detected
through pkg-config, so the development packages must be installed first - on current
Ubuntu/Debian (22.04+) that is:

```sh
sudo apt install build-essential cmake git \
  libasound2-dev libjack-jackd2-dev \
  libfreetype6-dev libfontconfig1-dev libcurl4-openssl-dev \
  libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
  libxinerama-dev libxrandr-dev libxrender-dev \
  libgl1-mesa-dev libglu1-mesa-dev \
  libgtk-3-dev libwebkit2gtk-4.1-dev
```

Two of these are worth calling out because their absence fails in confusing places:
`libfontconfig1-dev` (without it juceaide itself fails to compile with
`ft2build.h: No such file or directory`, which looks like a freetype problem but is
fontconfig's pkg-config file missing) and `libwebkit2gtk-4.1-dev` (the `webkit2gtk-4.0`
package from older Ubuntu releases no longer exists on 24.04 and JUCE 9 probes the 4.1
module; the same applies to `libgtk-3-dev` for `gtk+-x11-3.0`). The CI Linux job
installs exactly this set.

## Repository notes

```
CMakeLists.txt   the build, and the only place build settings live
Source/          the plugin: PluginProcessor, PluginEditor, and DSP
JUCE/            JUCE 9.0.2, pinned as a git submodule
.github/         CI: builds VST3 on Windows, VST3 + AU + AUv3 on macOS
```

### Renaming note

The plugin was renamed from *Analog Saturator* (repo `first`) to **Nonlin Analog
Saturator**. Only `PRODUCT_NAME` and `DESCRIPTION` changed. The manufacturer code
(`Manu`), the plugin code (`Zy59`) and the bundle ID (`com.MCsmes.first`) are the
plugin's identity to hosts and to saved sessions and were deliberately left alone, so
an existing session still finds the plugin after the rename instead of ending up with
two entries side by side.

Two user-visible names did change with it, both because they are the plugin's own
namespace on disk:

- the user-preset directory is now `<user app data>/Nonlin Analog Saturator/Presets`
- the preset extension is now `.nonlinpreset`

Presets saved under the old `J37 Tape Mastering` directory and `.j37tape` extension are
not read by this build. Move the files and rename the extension to keep them.

`J37` survives where it is a tape formula rather than a product name - the first entry
in the TAPE TYPE list - and in the internal build-flag names (`J37_BUILD_TESTS`,
`J37_BUILD_COMMIT`, `J37_FAST_WINDOWS_BUILD`), which are build configuration, not
user-facing product identity.

There are no generated project files in the repository. Open the folder directly in CLion,
Visual Studio or VS Code with the CMake extension, and the IDE will configure itself from
`CMakeLists.txt`.

## License

This repository is for project and development use.
  libxinerama-dev libxrandr-dev libxrender-dev \
  libgl1-mesa-dev libglu1-mesa-dev \
  libgtk-3-dev libwebkit2gtk-4.1-dev
```

Two of these are worth calling out because their absence fails in confusing places:
`libfontconfig1-dev` (without it juceaide itself fails to compile with
`ft2build.h: No such file or directory`, which looks like a freetype problem but is
fontconfig's pkg-config file missing) and `libwebkit2gtk-4.1-dev` (the `webkit2gtk-4.0`
package from older Ubuntu releases no longer exists on 24.04 and JUCE 9 probes the 4.1
module; the same applies to `libgtk-3-dev` for `gtk+-x11-3.0`). The CI Linux job
installs exactly this set.

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

## Repository notes

```
CMakeLists.txt   the build, and the only place build settings live
Source/          the plugin: PluginProcessor, PluginEditor, and DSP
JUCE/            JUCE 9.0.2, pinned as a git submodule
.github/         CI: builds VST3 on Windows, VST3 + AU + AUv3 on macOS
```

### Renaming note

The plugin was renamed from *Analog Saturator* (repo `first`) to **Nonlin Analog
Saturator**. Only `PRODUCT_NAME` and `DESCRIPTION` changed. The manufacturer code
(`Manu`), the plugin code (`Zy59`) and the bundle ID (`com.MCsmes.first`) are the
plugin's identity to hosts and to saved sessions and were deliberately left alone, so
an existing session still finds the plugin after the rename instead of ending up with
two entries side by side.

Two user-visible names did change with it, both because they are the plugin's own
namespace on disk:

- the user-preset directory is now `<user app data>/Nonlin Analog Saturator/Presets`
- the preset extension is now `.nonlinpreset`

Presets saved under the old `J37 Tape Mastering` directory and `.j37tape` extension are
not read by this build. Move the files and rename the extension to keep them.

`J37` survives where it is a tape formula rather than a product name - the first entry
in the TAPE TYPE list - and in the internal build-flag names (`J37_BUILD_TESTS`,
`J37_BUILD_COMMIT`, `J37_FAST_WINDOWS_BUILD`), which are build configuration, not
user-facing product identity.

There are no generated project files in the repository. Open the folder directly in CLion,
Visual Studio or VS Code with the CMake extension, and the IDE will configure itself from
`CMakeLists.txt`.

## License

This repository is for project and development use.
  libxinerama-dev libxrandr-dev libxrender-dev \
  libgl1-mesa-dev libglu1-mesa-dev \
  libgtk-3-dev libwebkit2gtk-4.1-dev
```

Two of these are worth calling out because their absence fails in confusing places:
`libfontconfig1-dev` (without it juceaide itself fails to compile with
`ft2build.h: No such file or directory`, which looks like a freetype problem but is
fontconfig's pkg-config file missing) and `libwebkit2gtk-4.1-dev` (the `webkit2gtk-4.0`
package from older Ubuntu releases no longer exists on 24.04 and JUCE 9 probes the 4.1
module; the same applies to `libgtk-3-dev` for `gtk+-x11-3.0`). The CI Linux job
installs exactly this set.

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
`CMakeLists.txt`.

## License

This repository is for project and development use.
