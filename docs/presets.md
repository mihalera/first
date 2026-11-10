# Factory presets

Thirty-five presets ship in `Source/Presets/Factory/`, each one a complete machine state. A
preset replaces the whole panel in a single undoable step: every control it does not name stays
at its neutral value rather than at whatever the last session left there, because every file
states all 139 preset parameters.

They are intended as starting points, not as finished masters - each names the job it was
written for and the stages it leans on, so you can hear what a page does before you touch it.

| | What it is for |
|---|---|
| **Minimum** | A null test: the signal comes out as it went in. |
| **Maximum** | Every parameter at the top of its range - a demo of the machine's ceiling, not a mix. |
| **Default** | The starting state an empty instance exposes: J37 at 15 ips, modest drive, real hiss. |

**Minimum** is the one preset that must not process. Every machine list sits on `Off`, every
amount is at zero, the filters are open (20 Hz to 20 kHz) and switched off, the compressor is
1:1 at 0 dB, and only the controls whose *centre* means "no change" keep a centre - a stereo
width of 0 is mono, and a tone of 0 is a tilt. If it changes what you hear, it is a bug;
`tests/presets/check_presets.py` compares it against that transparent table so the mistake
cannot come back.

**Maximum** is the opposite contract: it may never sit below another factory preset on any
amount, so it is the ceiling of the bank rather than one more loud preset. It does leave
BYPASS, DELTA and POLARITY alone - a switch is not an amount.

## Bus, master and glue

| Preset | What it is for | What it sets |
|---|---|---|
| **Gentle Warmth** | The first tape pass on a mix that is already right. | J37 at 15 ips, DRIVE 28 %, MIX 65 %, SUB-FUND 15 %, input -3 dB; hiss 15 %. |
| **Bus Glue Tape** | Glue a drum bus or a full mix without hearing the tape. | Ampex 456, DRIVE 62 %, MIX 100 %, TONE 66 %, CHAR 62 %, 2x oversampling, a touch of WEAR and MECHANICS, transient ATK +0.1 at 35 %. |
| **Clean Glue** | Transparent glue where the low end must stay tight. | Chrome at 30 ips, DRIVE 22 %, MIX 45 %, input -6 dB, both filters off. |
| **Tight Bus** | Shorten a bus that smears; keep the front and lose the tail. | J37, MIX 55 %, transient SUS -0.45 with ATK +0.25 at 70 %, input +0.5 dB. |
| **Wide Master** | Open the sides of a finished master. | J37, MIX 55 %, WIDTH 62 %, out HIGH +1.5 dB, SUB-FUND 12 %. |
| **Master Bounce** | The sound of a mix bounced to tape and back. | J37 at 15 ips, 4x oversampling, DRIVE 40 %, DIST 30 %, ping-pong 30 %, transient 40 %. |
| **Master Safety** | A master-bus setting that cannot hurt the programme. | Chrome at 30 ips, 4x oversampling, DRIVE 25 %, DIST 5 %, wow 5 %/flutter 7 %, no SUB-FUND. |
| **Ferric Master** | The 456-era sheen on a finished master. | RTM SM911, DRIVE 34 %, out HIGH +0.5 dB, light WEAR. |
| **Bright Air Tape** | Tape character on a piano or a mix that must stay open. | Studer A800 at 30 ips, DRIVE 55 %, TONE 68 %, in HIGH +2 dB, WIDTH 58 %, output -0.5 dB. |

## Drums

| Preset | What it is for | What it sets |
|---|---|---|
| **Drum Slam** | Drums through a hot machine. | Studer A800 at 30 ips, 4x oversampling, DRIVE 75 %, DIST 18 %, transient ATK +0.55 / SUS -0.3 at 80 %, input MID -2 dB and HIGH +1.5 dB. |
| **Drum Room Warm** | Add size without reverb-mush. | Ampex 456 at 7.5 ips, DRIVE 55 %, REVERB 22 %, ping-pong 20 %, transient 70 % with SUS +0.25, out LOW +1.5 dB. |
| **Transient Punch** | Make a close drum sound hit harder. | J37, MIX 65 %, transient ATK +0.6 / SUS -0.2 at 85 %, input HIGH +0.5 dB, hiss 10 %. |
| **Soft Touch** | Take the click off a bright kit. | Transient ATK -0.55 / SUS +0.15 at 75 %, DRIVE 32 %, MIX 60 %, TONE 42 %, noise 2 %. |

## Vocals

| Preset | What it is for | What it sets |
|---|---|---|
| **Vocal Rail** | A vocal that sits forward without EQ surgery. | Vocal voicing, J37 at MIX 70 %, input EQ LOW -1.5 / MID +2 / HIGH +2.5 dB, transient ATK +0.35 / SUS -0.15 at 60 %. |
| **Vocal Presence** | De-ess and lift a vocal in one step. | Studer A800, PRESENCE 70 %, out HIGH +2 dB, compressor -20 dB at 3.5:1 with 70 % MIX, de-esser 50 %, dynamic EQ 40 %. |

## Bass

| Preset | What it is for | What it sets |
|---|---|---|
| **Bass Weight** | Add the octave-below weight a DI bass lacks. | Studer A800 at 30 ips, 4x oversampling, SUB-FUND 65 %, in LOW +4 dB, out LOW +1 dB, DRIVE 48 %, SUS +0.2 at 40 %. |
| **Bass Console** | A bass recorded through a console rather than a plugin chain. | Ampex 456, DRIVE 50 %, in HP 32 Hz, out LOW +2.5 dB, compressor -24 dB at 3:1 with 80 % MIX, SAG 30 %. |

## Guitar: valves, amps and transformers

These three blend saturation principles rather than running one machine: the SOURCE knobs
share the stage, so the amp, the transformer and the cassette each contribute their own
nonlinearity.

| Preset | What it is for | What it sets |
|---|---|---|
| **British Valve** | The EL34 midrange push on a DI or a reamp. | EL34 valve 77 % + Marshall Plexi + cassette 11 %, DRIVE 55 %, DIST 30 %, in LOW -1.5 / MID -1 dB. |
| **Plexi Bite** | A hard, bright rhythm guitar. | Marshall Plexi 93 %, 2x oversampling, DRIVE 72 %, DIST 38 %, CHAR 68 %, out HIGH +2 dB, filters off. |
| **Iron Bloom** | Iron and a Vox-style chime on a clean part. | Cinemag transformer 68 % + Vox AC30 16 %, DRIVE 48 %, in LOW +3 dB, WIDTH 60 %, DIST 34 %. |

## Worn media, radio and lo-fi

| Preset | What it is for | What it sets |
|---|---|---|
| **Vintage Lo-Fi** | An old record, not a broken one. | J37 at 7.5 ips, wow 22 %/flutter 28 %, VINYL 25 %, DIST 22 %, WEAR 20 %/MECHANICS 18 %, hiss 45 %. |
| **Wobbly Cassette** | A cassette that has been in a drawer. | Cassette voicing at 7.5 ips, wow 30 %/flutter 34 %, WEAR 28 %/MECHANICS 30 %, VINYL 30 %, hiss 50 %. |
| **Lo-Fi Radio** | A small speaker in the next room. | Chrome at 7.5 ips, TONE 28 %, WIDTH 35 %, hiss 55 %, VINYL 45 %, DIST 30 %, input +4 dB and output -4 dB. |
| **Shellac Radio** | A 78 through a valve set. | Shellac 78 print at VINYL 85 %, hiss 60 %, WIDTH 25 %, wow 30 %, EL34 drive 62 %, output -3 dB. |
| **Dubplate Dub** | A dub plate with the delay and the wear that come with it. | Dubplate print at VINYL 70 %, delay 220 ms at 42 % feedback, REVERB 22 %, WEAR 22 %/MECHANICS 20 %. |
| **12-bit Bounce** | The sampler that defined a decade. | 12-bit digital stage, no oversampling, DRIVE 52 %, DIST 15 %, delay 125 ms at 35 %, output -2 dB. |
| **Saturated Crunch** | Deliberate overload. | Ampex 456 at 30 ips, 4x oversampling, DRIVE 85 %, DIST 35 %, input +3 dB, out HIGH +2 dB. |
| **Mix Saturation** | Audible saturation that still holds a mix together. | Ampex 456, 4x oversampling, DRIVE 68 %, DIST 25 %, 2+3 track layout, WIDTH 40 %, out LOW -0.5 dB. |

## Effects-forward

| Preset | What it is for | What it sets |
|---|---|---|
| **Fuzz Stack** | A fuzz pedal in front of a driven machine. | 811 tape, DRIVE 85 %, CHAR 85 %, rack OVERDRIVE 50 % + FUZZ 90 % + WAVEFOLDER 35 %, SAG 25 %, compressor 4:1 at -22 dB with 55 % MIX. |
| **Broken Bits** | Digital destruction with no tape in the path. | Sample-hold machine (tape `Off`), BITCRUSHER 85 %, RATE CRUSHER 70 %, RING MOD 25 %, LO-FI mode, output -2 dB. |
| **Wide Chorus** | Width from modulation rather than from EQ. | CHORUS 75 % + PHASER 30 %, WIDTH 90 %, wow 40 %/flutter 35 %, MIX 85 %. |
| **Dub Echo** | A dub delay built from the machine's own echo. | BBD delay 240 ms at 65 % feedback, ping-pong 100 %, REVERB 30 %, MIX 90 %, output -2 dB. |

## What a preset does not carry

A preset is a setting, so it carries settings only:

- **BYPASS, DELTA, POLARITY and the transport** are listening states, not part of a sound, so
  no preset moves them.
- **A neural model, an impulse response and a vinyl print** are files the user loads. Presets
  state the MIX for those stages - 0 % everywhere except Maximum, which is the ceiling demo -
  but never the file itself, so loading a preset leaves whatever is loaded in place.
- **The interface language and the panel's own click sounds** are not part of a sound either.

## Adding one

1. Write `Source/Presets/Factory/<name>.json` with a unique `name` and a value for every
   preset parameter - copy `default.json` and change what the preset is about.
2. Append the file to `FACTORY_PRESET_FILES` in `CMakeLists.txt`. Append, do not insert: the
   preset index is what selects the artwork tile, so an insertion renames the picture on every
   row below it.
3. Regenerate the artwork and its catalog with `python Assets/generate_asset_pack.py`, then
   update the width, columns and `tile_order` in `Assets/manifest.json` and the width in
   `Source/GUI/PanelAssetSlots.h`.
4. Run `python tests/presets/check_presets.py`. It checks the file against the shipping
   parameter table, holds Minimum to the transparent table, holds Maximum to the ceiling, and
   fails on a key no parameter answers to.
