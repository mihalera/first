# Nonlin Analog Saturator

https://github.com/mihalera/first

**Nonlin Analog Saturator** - a JUCE-based **mastering-grade analog saturation plugin** built around a nonlinear magnetic tape model, with switchable tape stock, transport speed and tape-style coloration.

This is, first and foremost, a **bus / mastering tool**: two independent glue compressors wrap the tape stage, the output level is calibrated in dB, the loudness metering is four-way (peak / RMS / LUFS / VU), and the output protection chain guarantees that what leaves the plugin is clean and controlled. Use it on the master bus, a drum bus or any programme material where you want the density and warmth of tape without losing control of the level.

## Overview

This project is a mastering-oriented audio effect plugin built for Windows with JUCE and Visual Studio.
It provides a tape-saturation mastering workflow with:

- drive and harmonic character controls
- a **TONE macro** that crossfades the whole machine state (tape stock, head gap, pre-bias, flutter) between the classic slow machine and the fast/hot machine
- **oversampling** (Off / 2x / 4x / 8x) around the nonlinear engine, with the filter delay reported to the host
- a **transient shaper** (ATTACK / SUSTAIN), which changes the signal's *envelope* rather than its waveform - punch or softness without adding a harmonic
- an optional **neural stage** (RTNeural): a learned model loaded from a file, run per channel as a nonlinearity, blendable with a wet/dry control
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

## Saturation: six principles, not one curve

A tape machine is **one** of the ways analogue electronics bend a signal, and for a
long time this plugin was built around that one curve. The shaper is now a blend of
the six mechanisms a real chain uses, and they are genuinely different shapes:

| Principle | Mechanism | Character |
| --- | --- | --- |
| **TAPE** | Magnetic hysteresis, with a **memory** term - the medium's state depends on where it has been | Gentle at low level; the asymmetry is what makes the even harmonics |
| **VALVE** | Thermionic: a soft, strongly asymmetric knee with a wide transition | Even-dominant, and it compresses rather than clips - it thickens before it distorts |
| **CASSETTE** | Narrow gauge, low bias: a **hard, early knee** with very limited headroom and a low-frequency bump | "Everything is louder and smaller" |
| **AMP** | A guitar amplifier's input stage: a high-gain, nearly symmetric **cascade** | Clips hard, strong odd harmonics - the one that bites |
| **TRANSFORMER** | An iron core's flux lagging whatever drives it, with saturation on the peaks | A gentle, level-dependent compression with a soft top-end loss - the "iron" in a signal path |
| **DIGITAL** | A converter's quantisation: a held code between sample instants and a hard level ceiling | The one solid-state mechanism: bit depth (16 / 12 / 8-bit), bit crush or sample-and-hold |

Blending them is not a gimmick: a real chain **is** this. A guitar goes into an amp,
the amp into a desk and a tape machine, a valve preamp sits somewhere in the path, a
transformer couples the output, and the whole thing may end up on a cassette - or be
the last thing a converter sees before it is digitised.

**BLEND** sweeps the weighting across the six in a fixed order (tape → valve →
cassette → amp → transformer → digital) - the order is the signal path rather than a
ranking, five machines you overload by pushing level into them and then the converter
that replaces all of them - so the control has one direction the ear can learn.
**SHAPE** decides how concentrated it is: low picks one principle at a time, high
spreads the weighting so all six contribute and the result reads as one compound
machine.

Every curve is normalised to **unity slope at the origin**, exactly like the original
tape shaper, so the blend cannot change the level - only the shape. That property is
what keeps DRIVE meaning what it says.

Default BLEND is 0 % - pure tape, which is exactly what every earlier build did.

Each principle also has its own **model list** (VALVE TYPE, AMP TYPE, TRANSFORMER
TYPE, DIGITAL TYPE, VINYL TYPE), and each list has an **OFF** entry that removes that
principle outright. When one is off its share of the blend is **redistributed** among
the principles still present rather than simply dropped, so switching one off changes
the character without changing the amount of saturation.

## Guitar-amplifier features

A saturation curve alone does not sound like an amplifier. What does is the behaviour
**around** the curve:

- **SAG** - the supply droops under sustained demand, so the gain falls a little and
  then recovers. It is why an amp "gives" under a held chord and why the attack feels
  spongy rather than immediate. Short transients never move it; only sustained
  programme does.
- **PRESENCE** - the negative-feedback network's top-end lift, the upper-mid bite that
  makes an amp cut through. It sits **after** the clipping, so it sharpens harmonics
  already present rather than generating new ones. 50 % is flat and neutral.
- **CABINET** - the speaker and its box: a **resonant** low-pass, with a peak around
  110 Hz from the cabinet's tuning and a roll-off from the cone's mass. Without it a
  clipped signal is fizzy; with it, it reads as a speaker. The voicing fades in with
  however much AMP is in the blend.
- **AMP BIAS** - the input valve's DC operating point, the single most effective
  control on a real amp's character. Cold is tight and slightly crossover-distorted,
  hot is fat and compressed.

All four are off or neutral by default, so a pure tape setting is untouched.

## Preamp and distortion

Two gain stages **in front of** the machine, and they do different jobs:

- **PREAMP** is a valve-ish input stage: a gentle soft clip, a transformer's low-cut
  and a slight top-end lift. It is a **stage**, not a gain - driving it changes the
  colour as much as the level, and it is level-matched internally so INPUT stays the
  control that sets the operating level.
- **DISTORTION** is a diode clipper: a hard knee with a pre-gain, deliberately abrupt.
  Where the saturation core bends, this **breaks**.

Both sit ahead of the tape so the machine hears their output - which is the whole
point: a distorted guitar recorded to tape sounds like a record rather than a pedal
precisely because the tape smooths what the pedal produced.

## Dynamics: the transient shaper

The DYN tab carries two stages that act on the **finished** signal rather than on the
waveform, and they are the plugin's answer to two questions a saturator cannot answer
by itself.

A saturation curve is memoryless and monotone: it looks at the instant's level and
bends it, so every harmonic it adds is a deliberate change to the sound. A **transient
shaper** is a different mechanism entirely - it looks at the signal's **envelope**, the
fast and slow views of its own level, and changes how that envelope moves over time. It
can make a drum hit sharper without adding a single harmonic, and soften a pick without
removing one. Nothing else in the chain can do that, because everything else operates
on the waveform.

The mechanism is two envelope followers on the same signal:

- a **fast** one (about a 2 ms window) that tracks edges, and
- a **slow** one (about 120 ms) that is the average programme level.

On a rising edge the fast follower runs ahead of the slow one - a positive difference -
and on a decaying tail it falls behind, a negative one. Two controls scale those two
halves independently:

| Control | Positive | Negative |
| --- | --- | --- |
| **ATTACK** | Sharpens the leading edge: punch, snap, click | Softens it: rounder, less percussive |
| **SUSTAIN** | Lifts what follows the edge: body, ring, room | Shortens it: tighter, more staccato |

Both are **bipolar and centred on zero**, which is why their readouts are signed
percentages rather than 0-100. At 0 % each is neutral and the stage is transparent.

**TR MIX** is how much of the shaped signal reaches the output, so the stage can be
blended rather than switched, and at 0 % it is absent entirely.

The applied gain is derived from the envelope difference and then smoothed, so the
effect is a slow, gain-like ride on the envelope rather than a waveshaper. That is what
makes it safe to run **after** the tape stage: a shaper there would re-distort the
signal the machine just coloured, whereas an envelope shaper only moves its level, so
the tape's character survives intact.

## Neural stage (RTNeural)

The other stage on the DYN tab is optional and inert by default. Where every other
curve in this plugin is hand-written from physics, a **neural model** is *learned* from
measurements of a real device, and RTNeural runs it fast enough to sit inside the
per-sample loop.

- The model is a **file**, not a parameter: it is loaded with **LOAD MODEL** on the DYN
  tab, and the network it describes (a Dense net, an LSTM, a GRU) is whatever the file
  says. It is **not** saved with the session or with a preset, because it is data
  rather than a setting.
- It runs **per channel**, after the tape stage and before the transient shaper, so it
acts as one more nonlinearity alongside the hand-written ones - and the shaper can still
sharpen whatever it produces.
- **NEURAL** is the wet/dry position. At 0 %, or with no model loaded, the stage is a
bit-for-bit pass-through, so a build with no model is exactly the plugin it was before
the stage existed.
- With RTNeural not fetched (or compiling the DSP harness), every member of the stage
reduces to that same pass-through: the guard is honest, and the plugin never depends on
a model being present to sound right.

## Tape condition: flux, wear, mechanics

Three separate physical facts about the machine, and they are genuinely separate
rather than three amounts of the same thing:

| Control | What it is | What it does |
| --- | --- | --- |
| **FLUX** | How deep into the oxide the record head magnetises | More flux is more low end and a stronger hysteresis memory; less is thin and bright. It is a different axis from DRIVE - DRIVE is how hard the signal is pushed into the curve, FLUX is how much of the medium's depth is used |
| **WEAR** | The state of the heads and the tape | A rounded gap and patchy oxide lose top end and add contact noise. It reads as "an old machine", not as a fault |
| **MECHANICS** | How well the transport holds its speed | Good order means smooth, periodic wow and flutter; dry bearings and a slack belt mean irregular drift and the occasional slip |

MECHANICS and WEAR are folded into the modulation that already exists rather than
being separate oscillators, because that is what they do - they make it less **even**,
not more. Their slow random sources are updated **once per block**, which is not an
approximation: both are sub-audio, so a per-sample update would compute the same
number a thousand times and use it once.

## Reverb and delay

The two time-based stages live on their own tab because they are the same kind of
decision.

**Reverb** is a plate/room built from four Schroeder all-pass sections into two comb
banks. The topology is the classic one because it is the one that sounds like a room
for the least code: all-passes diffuse without colouring the spectrum, and the comb
banks that follow set the decay. Two banks with different comb lengths are what gives
the image its width - a single bank would collapse to mono. It sits **after** the
machine, so it reverberates the processed signal rather than feeding back into the
saturation; a reverb inside the nonlinearity would be pitch-shifted by the wow and
would smear the harmonics the plugin exists to produce.

**Delay** is the second playback head, and **DELAY TYPE** sets what its repeats sound
like. The three are genuinely different machines:

| Type | Character |
| --- | --- |
| **Tape** | Each pass round the loop loses top end, because the repeat is recorded onto the tape and played back through the same losses the main path has |
| **BBD** | A bucket-brigade chip: darker still, with clock noise on the repeats and a bandwidth that **narrows as the delay lengthens** - which is what a BBD physically does, because the same number of buckets is being clocked more slowly |
| **Modern** | A clean digital delay: full bandwidth, no loss, repeats that stack without getting dull |

## Vinyl

The record-playing end of the chain. A turntable adds three things nothing else here
does, and they are what "vinyl" means as a sound:

- **Crackle** - impulse noise, not hiss. A record surface is **ticks**, caused by dust
  and by the stylus crossing the groove's imperfections, so the generator produces
  sparse impulses with a fast decay rather than continuous noise. That is the
  difference between a record and a noisy tape.
- **Rumble** - a low-frequency thump from the bearing and the motor, which is why
  vinyl has a bottom-end floor that a CD does not.
- **Warmth** - the RIAA playback curve. A playback stage that is not perfectly
  complementary to the cutting curve leaves the characteristic low-end lift and
  top-end softness. It is a filter, not a colour.

Each channel has its own noise generator, so the crackle and the rumble are
uncorrelated between the sides - sharing one would put every tick in the centre of the
image instead of on the surface.

## Modes

Two switches that re-voice the **whole machine**:

- **MODERN** - a well-maintained 1990s deck. The head losses move further out of the
  audio band, the floor drops, and the magnetic memory thins out. It changes the
  machine's **calibration** rather than its level, so nothing else needs
  recalibrating around it.
- **LO-FI** - the deliberate degradation: a hard 3.2 kHz bandwidth limit and a
  sample-and-hold quantisation. It runs on the finished sample, **after** the
  protection chain, so the quantisation cannot be smoothed away by the limiter - the
  point of the mode is that it is a fault, and a fault should survive to the output.

They are mutually exclusive by design, because a machine cannot be both. MODERN wins
when both are set, which is the safer of the two to be wrong about.

## Subharmonics

Everything else in this plugin produces **overtones** - harmonics at integer multiples
of the input frequency. This stage is the opposite: it produces the **subharmonic
series**, so a 100 Hz note gains weight at 50, 33.3, 25, 20 Hz and so on down to 1/10.

That cannot come out of a saturating curve, and it is worth being clear why, because it
looks like it should. `tanh(sin(wt))` is a curve applied to a *value*, with no notion of
time of its own, so its output is a function of the instantaneous input phase - and any
such function has period `2pi/w`, which means its Fourier series contains only multiples
of `w`. Subharmonics need a process with its **own** timescale that can fall out of step
with the signal.

On a real machine there are three such processes, and this models all three: bias
leakage (the ultrasonic bias oscillator is imperfectly suppressed on playback), domain-
wall motion (magnetic domains flip in groups, at a rate not locked to the signal), and
scrape flutter (tape-to-head friction modulates the effective head speed). All three are
the same shape mathematically - a slow, signal-dependent modulation of the transfer
curve.

### The cascade

Nine stages produce `f0/2` through `f0/10`. The level of each is fixed by a **staircase**
law: a partial at `f0/d` is fed at `2^-(d-1)`, which is exactly **-6 dB per step of
division** - 1/2 on top, 1/3 at -6 dB, 1/4 at -12 dB, down to 1/10 at -48 dB. The law is
written in terms of the **divider** rather than the stage index so that it cannot invert.

DRIVE tilts the series toward its deep end without ever breaking that order: the tilt
grows with the divider but by at most 1.875x across the whole 2...10 range, so no two
adjacent stages can swap places at any drive setting.

Every stage is a **pure sinusoid**, and deliberately the only waveform the stage ever
produces. An earlier version ran each partial through `tanh` first - "downward
saturation" - but a memoryless waveshaper on a sinusoid is a harmonic generator, and a
loud one: at the default DRIVE the 1/2 undertone was returning its own third harmonic
only 16 dB down. That is a harmonic OF a subharmonic, which is exactly the fault it
looks like. DRIVE keeps its musical job of tilting the weights, but it no longer shapes
the waveform.

### Tracking: a phase-locked subdivision

The cascade has to know what note is playing, and it has to stay locked to it over a
long take. The reference is a **fundamental phase accumulator** in turns, advanced
continuously from the tracked period and pulled toward each detected zero-crossing by a
gentle 8 % of the error. Every stage's phase is then derived from that one master by
division - which is what a subdivision *is*.

This replaced an earlier scheme that aligned each stage against
`crossingCount % divider` - a running **count** of detected crossings rather than a
phase. That had two faults, and both are why the rewrite was worth doing:

- **It drifted permanently.** The count only ever increases, so a single missed crossing
  (a quiet passage, a transient the detector skips) shifted every stage's reference for
  the rest of the session. Nothing brought it back in step with the signal.
- **It was quantised wrongly.** For any stage whose divider did not divide the count
  evenly, the target phase was simply the wrong value, and the stage was pulled
  off-frequency every time a crossing arrived.

Deriving every stage from one master phase fixes both: the reference is always current,
and a missed crossing costs one correction rather than a permanent offset.

### The gate

Two independent statements decide whether there is a note to double, and they fail
**differently** - which is why both are required:

| Term | Question |
| --- | --- |
| **Presence** | Is there still *level* in the fundamental band? |
| **Cycle confidence** | Is the detector still getting *cycles* from it? |

A sustained note whose level drops keeps producing crossings long after it is quiet, and
a noisy signal can hold the level up while producing no usable cycles at all. Requiring
both means the cascade releases when **either** stops being true, so it cannot drone at
the last tracked pitch with nothing playing - which is the fault the presence gate was
originally added to fix, now closed from both sides.

### Anti-phase protection

The 1/2 stage carries the most energy in the cascade, so it is also the one that does the
most damage if the two channels disagree: two octaves in **opposite phase** cancel when
the mix is summed to mono, and the low end the control was asked for simply disappears.
That is not a fault in either generator - each is correctly locked to its own channel's
waveform - but it is a fault in the result.

The protection runs once per **frame**, after both channels have generated their
undertones, because it is a statement about the *pair*. It compares the two octave phases
and, only when they genuinely disagree, nudges one master phase back toward the other by
a clamped amount. An already-aligned pair is untouched, and the nudge is small enough
that it can never break the lock the PLL is holding.

### Tracking readout

The panel reports the note the cascade is locked to and how solidly, because a depth knob
at 60 % looks identical whether the generator is producing a clean undertone series or
sitting idle because the detector never found a note. The confidence percentage
distinguishes those two states, and the tracked frequency says **which** note it locked
to - which is how a user finds out that a mix is being read an octave low.

## Signal path



Input trim (dB) -> input glue compressor (always on) ->
record head (pre-emphasis, bias, magnetic hysteresis with memory) ->
tape low-pass and head-gap loss -> tape noise floor and wow/flutter modulation ->
playback EQ tilt -> playback DC blocker -> second playback head (DELAY) ->
stereo tape offset (ST OFFSET) -> output glue compressor (always on) ->
output EQ -> neural stage (optional, after the tape) -> transient shaper (DYN) ->
output trim (dB) -> stereo width -> anti-phase guard -> safety limiter -> soft clipper ->
reverb (SPACE) -> vinyl (VINYL).

The transport state (STOP / PLAY / START) scales the whole wet side of that path and
the transport modulation together, so STOP is the machine coming to rest rather than a
mute on the output.

The two DYN stages sit at the **end of the wet path** deliberately. The neural stage is
one more nonlinearity and belongs with the machine; the transient shaper moves the
envelope rather than the waveform, so it must come after everything that bends the
signal - a shaper before the tape would be re-distorted by it. Both are transparent at
their default settings (NEURAL 0, TR MIX 0), so an untouched instance is exactly the
machine it always was.

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
| Sub-Fundamental (SUBFUND) | 0 to 100 % | Subharmonic saturation: a nine-stage undertone cascade (1/2 ... 1/10 of the tracked bass fundamental) summed under the tape signal, per channel. Default OFF |
| Delay | 0 to 250 ms | The spacing of a second playback head. The tape takes time to travel between the record and playback gaps, so the signal returns as a slap rather than a dub echo. The repeat is taken after the tape, so it inherits the machine's own bandwidth and saturation. Default 0 ms (no second head) |
| Delay Level | 0 to 100 % | How loud the second head's output is. Each pass round the tape loses top end, the way a real repeat does. Default 0 % |
| ST Offset | -500 to +500 us | Inter-channel time offset. Real stereo decks record the two tracks with separate head gaps a fraction of a millimetre apart and the tape skews across them, so the channels are never perfectly aligned. Positive lags the right channel. Default 0 us |
| Noise | 0 to 100 % | A trim on top of whatever floor the loaded tape formula sets. The formula's own character is untouched, so the floor can be lifted for a dirty bounce or pulled to a clinical black without changing stock. Gated by the transport. Default 50 % (neutral) |
| Transport | Stop / Play / Start | The machine's three states. STOP lets the capstan coast to rest - no hiss, no wow, no delay tail: true silence, not a mute. START spins up from rest, running flat and climbing into tune over about a second |
| Output | -32 to +32 dB | Calibrated output trim in dB |
| Width | 0 to 100 % | Mono through natural to extra wide |
| Bypass | on/off | Ramps the whole tape engine out without clicking |
| Tape Type | J37 / Ampex 456 / Studer A800 / Chrome / Type 111 / GP9 / Quantegy 499 / RTM SM911 / SM 468 / 888 / 815 / 811 | Model character (also shapes the glue time constants) |
| Speed | 7.5 / 15 / 30 ips | Transport speed, affects modulation, top end and glue timing |
| Oversampling | Off / 2x / 4x / 8x | Runs the tape engine at a higher internal rate to reduce aliasing; the added latency is reported to the host |
| Polarity | on/off | Inverts the output polarity (180-degree flip), after the protection chain and the meters' magnitude path |
| Auto Gain | on/off | Lets the slow programme compensator restore the level the INPUT trim dialled in; off leaves the output exactly at the level the chain produced |
| Transient Attack (ATK) | -100 to +100 % | The leading edge of each event: positive sharpens it (punch, snap), negative softens it (rounder). It moves the **envelope**, not the waveform, so it adds no harmonics. Neutral at 0 % |
| Transient Sustain (SUS) | -100 to +100 % | What follows the attack - body, ring, room: positive lengthens it (fuller), negative shortens it (tighter, more staccato). Neutral at 0 % |
| Transient Mix (TR MIX) | 0 to 100 % | How much of the transient-shaped signal reaches the output. At 0 % the shaper is absent. Default 0 % |
| Neural Mix (NEURAL) | 0 to 100 % | Wet/dry position of the optional learned model. At 0 %, or with no model loaded, the stage is transparent. Default 0 % |
| Load Model | file | Reads an RTNeural model (its JSON description) and installs it as an optional per-channel nonlinearity, after the tape. Not saved with the session |
| Clear | - | Releases the loaded model, returning the NEURAL stage to its pass-through |
| Presets | 29 factory | Loaded from the preset box; each application is one undoable step |
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

Twenty-nine factory presets cover the machine's range from gentle bus warmth to slammed drum
tape, including vocal, bass and mastering-safety starting points. Three of them - **Transient
Punch**, **Soft Touch** and **Tight Bus** - are built around the transient shaper on the DYN
tab, so the two directions of both ATK and SUS are demonstrated by a preset rather than only
by a tooltip. A preset replaces the whole machine state in a single undoable transaction -
Ctrl+Z brings back exactly what was on screen before.

A preset **does not** carry a neural model. A model is a file the user loads, not a setting,
so NEURAL is stated by every preset as 0 % and a preset load never touches whatever model is
installed - the model survives a preset change untouched, and no preset can leave the stage
blending toward a model that is not there.

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
- Target platforms: Windows (x64), macOS (universal: arm64 + x86_64), Linux (x64, VST3), Android (arm64-v8a, Standalone)
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

## Інструкція для користувачів macOS 🍏

Оскільки плагін є безкоштовним та open-source, він не має платного підпису Apple
Developer. Якщо ваша система macOS або DAW видає помилку при спробі його запустити,
виконайте дві прості команди в Терміналі.

1. Перейдіть до папки з вашими VST3 плагінами:

```bash
cd ~/Library/Audio/Plug-Ins/VST3/
```

2. Зніміть мітку карантину Apple Gatekeeper з файлу плагіна:

```bash
xattr -cr "Nonlin Analog Saturator.vst3"
```

Будьте обачні, використовуючи код.

(Аналогічно для папки `Components` та розширення `.component`, якщо ви використовуєте
формат Audio Unit.)

Артефакти CI мають ad-hoc підпис: цього достатньо, щоб збірка запускалась на
Apple Silicon, але цього недостатньо, щоб минати перевірку Gatekeeper, тому
команда вище потрібна для завантажених копій.

## Android 🤖

JUCE's CMake layer reduces to the **Standalone** format on Android: the plugin
kinds VST3 / AU / AUv3 are desktop-host formats and are excluded there
(`_juce_get_platform_plugin_kinds`), and there is no Gradle exporter in the CMake
layer. So "Android support" here means the plugin's whole engine - DSP, editor
and every third-party library in the build - compiling for the Android NDK as
the Standalone wrapper, a shared library a Gradle project loads.

### Building for Android

The CI job `build-android` proves the cross-build on every push (arm64-v8a,
min API 28, NDK from the runner). To build by hand you need the Android NDK
(r23+) - via Android Studio's SDK Manager or `commandlinetools` - and then:

```sh
cmake -S . -B build-android \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=Android \
    -DCMAKE_ANDROID_API=29 \
    -DCMAKE_ANDROID_NDK=$ANDROID_NDK_LATEST_HOME \
    -DCMAKE_ANDROID_ARCH_ABI=arm64-v8a
cmake --build build-android --parallel
```

The output is the Standalone wrapper `.so` plus the static libraries it links.

### Turning the .so into an app

CMake cannot host an Android app - the APK, its manifest, the activity classes,
the resources and the signing all belong to a Gradle project. The skeleton:

```gradle
// app/build.gradle.kts
android {
    namespace = "com.MCsmes.first"
    compileSdk = 34
    defaultConfig { minSdk = 28 }
    externalNativeBuild { cmake {
        path = file("../../CMakeLists.txt")   // this repository's build
        version = "3.22.1"
    } }
}
```

The CMake configure must then receive the same Android variables as above
(Gradle sets `CMAKE_SYSTEM_NAME`, the API level and the ABI itself; the STL
defaults to `c++_static`, which is what this project's single-`.so` Standalone
wants). Point the manifest's activity at a JUCE standalone activity subclass or
at a thin launcher that opens the plugin window - that part is app code, and
this repository deliberately does not ship an app shell.

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
commit, so a fresh clone resolves the same versions forever. Most are linked into the
plugin target so the pieces are *available* and the engine uses its own code, with
adopting a piece a per-piece decision made against a profile and a listening test. The
exceptions are named in the table: those the engine actually runs today.

| Library | Pinned at | Why it is in the build |
| --- | --- | --- |
| `RTNeural` | commit `95c3c0f9` | **Used.** The real-time neural inference engine behind the DYN tab's optional model stage. `NeuralStage` runs a 1-in/1-out model per channel. Configured with `RTNEURAL_STL ON` and no `-mavx2`, so the binary still runs on machines without AVX |
| `nlohmann/json` | `v3.12.0` | **Used.** The JSON DOM `RTNeural::json_parser::parseJson` parses a model file into. Header-only |
| `chowdsp_utils` | `v2.4.0` | Chowdhury DSP's toolbox. Provides the polynomial `exp`/`log`/dB approximations on the metering and auto-gain hot paths; `chowdsp_dsp_utils` is linked so the rest is available |
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
