# The signal path: what happens to a sample, in order

This is the processing sequence the plugin runs, stage by stage, in the order the
code runs it. It is written from `Source/ProcessorSignalPath.cpp` - the order below
is the order of the branches in `processTapeEngine()` and `processOneSample()` -
rather than from the panel, because the two disagree in exactly the places that
matter (the reverb is not in the tape loop, and the limiter is not the last thing
that touches the sample).

Two companion documents: [README.md § Signal path](../README.md#signal-path) is the
short version, and [architecture.md](architecture.md#the-signal-path) is the map of
which header owns which stage.

## The wrapper: rate, blocks and where the stages sit

```
host block (session rate)
  |
  +-- oversampler up (x2 / x4 / x8, or OFF)
  |     |
  |     +-- processTapeEngine()
  |           per sample:  processOneSample()        <- stages 1..24 below
  |           per block:   processPostMachineStages() <- stages 25..27 below
  |     |
  |     +-- oversampler down
  |
host block (session rate)
```

Consequences worth knowing before reading the table:

- **Everything from the input trim to the limiter runs at the ENGINE rate**, which
  is the session rate multiplied by the OVERSAMPLING factor (48 kHz at x4 is
  192 kHz). The two glue compressors, the EQs, the compressor and the limiter all
  recompute their coefficients from that rate, so their times and corner
  frequencies do not move when the switch does.
- **The corrector's audio path left the per-frame loop and sits with the block stages.**
  `Source/DSP/CorrectorStage.h` has two engines behind one interface and one of them - the
  signalsmith-stretch vocoder, which `-DJ37_CORRECTOR_USE_STRETCH=ON` selects - is handed whole
  blocks, so the stage is driven where the engine can afford to wait for them: at the top of
  `processPostMachineStages()`, before the IR cabinet, the reverb and the vinyl stage. What
  that changes is the order of the PROTECTION stages (the compressor, width, guard, output
  trim, limiter and clipper no longer see a corrected signal) and not of the effects: the
  corrector is still fed the tape machine's output and the reverb is still fed the corrected
  signal. What it buys is that the stage's lookahead is one number, reported to the host by
  `publishReportedLatency()` while AUTO TUNE is engaged and zero while it is not - instead of
  a delay buried in the middle of the chain, where every stage after it would sit against the
  host's timeline. That latency is engine-specific and measured: 1216 frames at 48 kHz (25 ms)
  with the vocoder, and nothing reported for the granular shifter, whose one period of delay
  the plugin has never reported.
- **The IR cabinet, SPACE reverb and VINYL stage are per BLOCK, not per sample**,
  and they run *after* the limiter and the soft clipper - so they are the only
  stages that can add level the protection chain never saw. They also run at the
  engine rate, inside the oversampled region, which is why their state is prepared
  at that rate rather than at the session's.
- **Autotune runs inside the per-sample loop**, at the engine rate. The header
  comment at its call site carries a review note about that placement (engine rate
  versus session rate); it is preserved as written and is not changed here.
- **BYPASS and DELTA are crossfades at the very end**, applied to the finished
  sample against the machine's own dry reference, not switches that skip code.

## The stages, in order

Numbers are the order; "per" is the rate domain.

| # | Stage | per | What it does | Panel controls |
| --- | --- | --- | --- | --- |
| 1 | Input trim | sample | The gain the host signal enters the machine at. Also owns the shared sample clock every smoother below reads. | INPUT |
| 2 | Input glue compressor | frame (detector), sample (gain) | Always on. The detector runs once per frame - linked (channel average) or per channel - so both sides of a stereo pair cannot fight over the same gain. Its makeup is driven by INPUT, so a boosted trim pays its own reduction back. | INPUT, ST LINK |
| 3 | Preamp and distortion | sample | Two level-matched gain stages in front of the machine, so the tape sees the level the user dialled in rather than a pre-boosted version of it. | PREAMP, DIST |
| 4 | DI box | sample | Loading, transformer and pad of a DI in front of the machine; the impedance interaction is what a DI actually contributes. | DI, DI LOAD, DI TRAFO, DI PAD |
| 5 | IN EQ | sample | Three-band split plus optional high-pass and low-pass, with order and Q. Placed after the input stages and before the record head, which is where an input equaliser belongs. | IN LOW/MID/HIGH, IN HP/LP, ORDER, Q |
| 6 | Transport modulation | sample | WOW, FLUTTER, MECHANICS and WEAR modulate the record head's rate. Every term is scaled by the transport's speed error, so START is a pitch ramp rather than a level fade. | WOW, FLUTTER, MECHANICS, WEAR, TONE |
| 7 | Record head: pre-emphasis, bias, drive | sample | Pre-emphasis and the bias offset set the operating point; DRIVE scales the level into the curve; SAG acts on the DRIVE, not on the output. | DRIVE, BIAS, SAG, TONE |
| 8 | Saturation core | sample | The seven machine curves (tape, valve, cassette, vinyl, amp, transformer, digital), crossfaded by their SOURCE amounts and driven by the sum of those amounts, so a mix at half strength is half the saturation and not just a different flavour. | the seven SOURCE knobs, TAPE TYPE, VALVE/AMP/TRAFO/DIGITAL/CASSETTE/VINYL TYPE |
| 9 | Sub-fundamental generation | sample | The undertone series, generated from the SHAPER's output with downward saturation. The result is parked per channel, not summed here. | SUB FUND |
| 10 | Track width, as a low-end tilt | sample | A narrow track reads less low end per channel; this is the layout's consequence, not the user's width control. | TRACKS |
| 11 | Playback EQ (brightness tilt) | sample | The one-pole tilt that makes the machine sound duller or brighter. | TONE |
| 12 | Tape condition: FLUX and WEAR | sample | Record-head depth into the oxide and the state of head and tape: a property of the recorded signal, not of the electronics. | FLUX, WEAR |
| 13 | Playback DC blocker | sample | About an 8 Hz one-pole high-pass, removing the offset the deliberately asymmetric curve creates. Without it a hard-driven mix walks off centre. | - |
| 14 | Playback head delay | sample | The spare head: a single interpolated read of one buffer per channel, with its own repeat character. The repeat is taken from the DC-blocked wet signal, so it inherits the machine's bandwidth and saturation. | DELAY, DLY LVL, DELAY TYPE, RATE/SYNC/PINGPONG |
| 15 | Stereo tape offset | sample | A fractional-sample delay on ONE channel - the two tracks' heads are not in the same place. | ST OFFSET |
| 16 | Platter gate and MIX crossfade | sample | The dry and wet legs are crossfaded by MIX, and BOTH legs carry the platter gate: a stopped machine passes nothing, whatever MIX says. | MIX, transport |
| 17 | Repitch | sample | The finished frame is read back at the platter's speed, which is what turns the transport into a pitch bend. It sits after the crossfade and before every gain that follows. | transport, SPEED |
| 18 | Sub-fund anti-phase guard | frame | Pulls the 1/2 and 1/4 undertone stages back into phase when their energy would otherwise cancel or reinforce the programme. | - |
| 19 | Output glue compressor | frame (detector), sample (gain) | The second always-on compressor, immediately before the output trim; its makeup is driven by OUTPUT. | OUTPUT, ST LINK |
| 20 | AUTO GAIN compensation | sample | Measures the machine's output against its input and restores the level the machine's own drive and glue changed, with a slow one-pole so it tracks programme rather than notes. | AUTO GAIN |
| 21 | Sub-fund joins | sample | The undertones generated at stage 9 are added back here - the one point in the chain where they meet a signal that is linear or gain-only. | SUB FUND |
| 22 | OUT EQ | sample | The output equaliser, at the other end of the machine and deliberately before the limiter: a boost here is caught by protection further down. | OUT LOW/MID/HIGH, OUT HP/LP, ORDER, Q |
| 23 | Neural stage | sample | A learned model as a per-channel nonlinearity, after the tape and before the transient shaper: a model trained on hardware is another saturation curve, and the shaper can still sharpen what it produced. Bit-for-bit pass-through at NEURAL 0 or with no model. | NEURAL |
| 24 | Transient shaper | sample | Attack and sustain, blended by its own MIX. The only stage that changes the ENVELOPE rather than the waveform, which is why it comes after everything that bends the signal. | TR ATTACK, TR SUSTAIN, TR MIX |
| 25 | Autotune | frame (the detector and the rules), block (the audio path - see below) | Pitch correction: a YIN period detector gates a phase-locked shifter, pulling the reading onto the nearest degree of the chosen scale - but only once the reading is more than **±10 cents** off it (`Source/DSP/PitchCorrectionRule.h`), so a note that is nearly in tune is left alone. At 0 the stage and the detector are out of the path entirely, and nothing of it is reported either. | AUTOTUNE, AMOUNT, TONIC, MODE |
| 26 | Conventional compressor | sample | An ordinary feed-forward compressor on the finished signal, independent of the two glue stages, with its own mix. | COMP THRESH/RATIO/ATTACK/RELEASE/MAKEUP/MIX |
| 27 | Track bleed | sample | What one head reads from the other track's recent history - a small, symmetric leak that is part of what the machine put on the tape. | TRACKS |
| 28 | Stereo width | sample | Mid/side width on the finished image. | WIDTH |
| 29 | Anti-phase guard | frame | Splits the frame into three bands (200 Hz and 2 kHz Linkwitz-Riley crossovers), measures each band's own correlation and rotates only the bands whose sides oppose the mid, so a cancelling low end is recovered without flattening the bands that were fine. A frame with no opposition is blended by zero and comes back bit for bit; the GUARD switch unwinds the rotation rather than switching it. | GUARD |
| 30 | Output trim | sample | OUTPUT in dB, applied before the limiter so boosts stay protected. | OUTPUT |
| 31 | Safety limiter | sample | Program-dependent gain reduction with a fast catch and a slow release; a gain stage, so it adds no harmonics of its own. | - |
| 32 | Soft clipper | sample | The last resort for what still overshoots after the limiter; its flag means "the clipper had to act", which is why the meter warns about real distortion. | - |
| 33 | Polarity | sample | Flips the finished sample. | POLARITY |
| 34 | LO-FI mode | sample | Bandwidth restriction, sample-and-hold quantisation and a raised floor, applied after protection so the quantisation cannot be smoothed away. | LO-FI |
| 35 | BYPASS / DELTA crossfades | sample | BYPASS crossfades from the untouched host sample to the finished one; DELTA crossfades to the difference between the finished signal and the machine's dry reference, computed from the same frame so the subtraction is phase-perfect at any oversampling factor. | BYPASS, DELTA |
| 36 | IR cabinet | block | Convolution against a user impulse response, applied to a copy and blended by its amount. | CABINET |
| 37 | SPACE reverb | frame, per block | Dattorro's plate on the protected signal: one tank fed the frame, its width taken from two taps of it. It runs at 48 kHz, the rate its delay lengths were chosen at, and the signal is carried to and from the session's rate - so the same SIZE decays for the same number of SECONDS at 44.1, 48 or 96 kHz rather than being a different room at each. | SPACE, SPACE SIZE |
| 38 | VINYL | block | Surface crackle, rumble, dust, scratches, warp, electrical noise and the RIAA-style warmth, all scaled by the stage's own level. A user NOISE PRINT, if one is loaded, **replaces** the synthesised groove hiss - the surface becomes the file, while the damage model above keeps working over it - and its samples are read through one atomic pointer the loader swaps once per load (`Source/DSP/NoiseLoader.h`), never allocated or locked on the audio thread. | VINYL, and its six character controls |

## What the panel meters read

The meters are not a stage: `publishBlockTelemetry()` reads the block after the
machine and publishes one complete frame per block through the lock-free queue
(plain atomics when the queue library is absent). INPUT and OUTPUT loudness are
K-weighted with the same meter, the gain-reduction readouts come from the two glue
detectors, and the harmonic and spectrum readouts are computed from what the
machine produced.

## Where the behaviour is pinned

| Claim | Harness |
| --- | --- |
| the subharmonic series, its threshold and its anti-phase guard | `tests/dsp` (SUBFUND sections) |
| the output anti-phase guard's three bands, per band rotation, and its unwind | `tests/phase` |
| the AUTO GAIN compensator's pole, ceiling, no-boost rule and silence handling | `tests/gain` |
| the safety limiter's transparency, catch, release and rate independence | `tests/limiter` |
| the MIX crossfade's endpoints, equal-power law and the transport gate on both legs | `tests/mix_crossfader` |
| the loudness meter's calibration and channel sum | `tests/dsp` (loudness sections) |
| the shaper's curves when the tanh seam is switched | `tests/dsp/simd_tanh_test.cpp` |
| the three-band EQ's neutral point, slopes and band independence | `tests/unit` (`ThreeBandEq`) |
| the conventional compressor's ratio, knee and makeup | `tests/unit` (`StandardCompressor`) |
| the transient shaper's ATTACK and SUSTAIN signs | `tests/unit` (`TransientShaper`) |
| the transport rig and the repitch stage | `tests/transport` |
| the pitch tracker, the granular shifter, and the corrector stage in BOTH of its engines | `tests/pitch` |
| the rate bridge and the guarded neural stage | `tests/neural` |
| the room's tail length in seconds at three rates, its stereo width, and its silence at MIX 0 | `tests/reverb` |
| the spectrum analyser, with and without its FFT | `tests/spectrum` |
| session state round trip | `tests/state` |

Every stage on the list [plugin-improvement-plan.md](plugin-improvement-plan.md) item 4
named now has a harness section of its own: the anti-phase guard (`tests/phase`), the
AUTO GAIN compensator (`tests/gain`), the safety limiter (`tests/limiter`) and the MIX
crossfade (`tests/mix_crossfader`). What is still described here and measured nowhere is
the sequencing of the loop itself - item 5 drives `processBlock` with the buffer shapes a
host hands over.

## Short version

```
INPUT trim -> input glue -> preamp/distortion -> DI -> IN EQ -> wow/flutter ->
record head (pre-emphasis, bias, drive) -> saturation -> sub-fund generated ->
track-width tilt -> brightness tilt -> FLUX/WEAR -> DC blocker -> DELAY head ->
ST OFFSET -> platter gate x MIX -> repitch -> anti-phase -> output glue ->
AUTO GAIN -> sub-fund joins -> OUT EQ -> neural -> transient -> autotune ->
compressor -> track bleed -> width -> OUTPUT trim -> limiter -> soft clip ->
polarity -> LO-FI -> BYPASS/DELTA || then per block: cabinet -> SPACE -> VINYL
```
