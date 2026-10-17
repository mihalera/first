// =============================================================================
//  The panel's text: the knob table, and the sentence each parameter shows.
//
//  ControlSpec is the checksum of the whole panel - an id that is not the
//  parameter's, a caption that disagrees with the host's, a default that is not
//  the parameter's default - and parameterTooltipText() is the sentence pair
//  behind every knob, switch and list on it. Both are data, so both are a
//  header: the editor reads them and nothing in them reads the editor.
//
//  Moved out of PluginEditor.cpp whole, prose and all, by matching its exact
//  text. `controlSpecs` and `parameterTooltipText` are `inline` for the reason
//  stated in PanelPaint.h: here they were one copy per translation unit with
//  internal linkage, and a table read by the panel has to be one table.
// =============================================================================
#pragma once

#include <JuceHeader.h>
#include <array>

// The panel's knob strip as ONE number: sixty-four controls. Declared here,
// above BOTH of its users, because they no longer live in the same scope: the
// class below sizes its sliders, labels and attachments from it, and the control
// table that NAMES those knobs is at file scope in this header (THE CONTROL
// TABLE, below) - a constant inside the class is not visible there,
// which is a build error rather than a warning, and was one.
//
// 64: the fifty-nine the panel carried, minus the BLEND/SHAPE pair the user asked
// to remove, plus the seven SOURCE knobs that replaced them.
//
// The count, the controlIds / controlNames lists, the defaultValues array and the
// tabSpecs table are all views of ONE list and must agree. Sizing the arrays from
// this name rather than from a literal is what turns a mismatch into a compile
// error (C2078) instead of a silent out-of-bounds read at run time, and the tab
// table is checked against this count by a static_assert in setCurrentTab, so a
// knob that no tab lists is a build error too.
inline constexpr std::size_t controlCount = 64;

namespace j37panel
{
    // These two lists are the grid's vocabulary, not its layout: their order no longer
    // decides which row anything sits on, or even whether it is on screen. tabSpecs
    // above says which tab each control belongs to and in which order it is drawn, and
    // resized() lays out the active tab alone. What the list order still has to agree
    // with is the defaultValues array below and controlCount in the header.
    //
    // BRIGHT and TONE are deliberately not swapped to line up with their ids: the
    // parameter called "tone" is registered as Brightness and the one called
    // "character" as Tone (see createParameterLayout), so each cell carries the name
    // the HOST shows for that parameter.
    // -----------------------------------------------------------------------
    //  THE CONTROL TABLE - one row per knob.
    //
    //  This used to be three parallel arrays (controlIds, controlNames,
    //  defaultValues) that had to be kept in step by hand, plus a tabSpecs table
    //  below that referred to them by INDEX. Adding a single knob meant editing
    //  four places and renumbering every index after the insertion point in
    //  tabSpecs - and because an index cannot be checked by the compiler against
    //  a name, the failure mode was a knob that silently showed another knob's
    //  value, or a range applied to the wrong control.
    //
    //  Now there is one row per control: its parameter id, its panel caption, its
    //  double-click reset value and the tab it belongs to. The arrays the rest of
    //  the constructor uses are BUILT from this table, so they cannot disagree
    //  with it, and the tab table below refers to controls by ID rather than by
    //  index. Adding a knob is one row here (plus its parameter and its tooltip);
    //  nothing needs renumbering.
    //
    //  `defaultValue` is the double-click reset, and it is the SAME number
    //  createParameterLayout() registers as the parameter's default - the two are
    //  two views of one fact and the row is where they meet.
    // -----------------------------------------------------------------------
    struct ControlSpec
    {
        const char* id;            // the parameter id
        const char* caption;       // the panel's short label
        double defaultValue;       // double-click reset (matches the parameter)
        const char* tab;           // the tab this knob belongs to, by NAME
    };

    // BRIGHT and TONE are deliberately not swapped to line up with their ids: the
    // parameter called "tone" is registered as Brightness and the one called
    // "character" as Tone (see createParameterLayout), so each row carries the
    // name the HOST shows for that parameter.
    //
    // Order here is the vocabulary, not the layout: tabSpecs decides which tab a
    // control sits on and in which order it is drawn. What this order still has to
    // agree with is the parameter defaults, which each row states directly.
    inline const std::array<ControlSpec, controlCount> controlSpecs { {
        { "input",           "INPUT",      0.0,   "MACHINE"  },
        { "drive",           "DRIVE",      0.30,  "SATURATION" },
        { "bias",            "BIAS",       0.42,  "SATURATION" },
        { "tone",            "BRIGHT",     0.50,  "MACHINE"  },
        { "character",       "TONE",       0.5,   "MACHINE"  },
        { "wow",             "WOW",        0.14,  "NOISE"    },
        { "flutter",         "FLUTTER",    0.18,  "NOISE"    },
        // MIX's default is 0.5, NOT 50.0: this column is stated in the units the
        // SLIDER works in (see the id-based range block below), every control on
        // this row is a 0..1 proportion whose readout is a percentage ("50 %"),
        // and 50.0 is not a value in that range at all - juce::Slider::setValue
        // clamps it, so a double-click on MIX behaved like a double-click on a
        // different knob: it jumped to 100 %, not to the parameter's own 50 %
        // default, which is the one thing a double-click is supposed to restore.
        { "mix",             "MIX",        0.5,   "MACHINE"  },
        { "output",          "OUTPUT",     0.0,   "MACHINE"  },
        { "stereo_width",    "WIDTH",      0.5,   "MACHINE"  },
        // The seven SOURCE knobs, where the old BLEND/SHAPE pair sat: each is
        // its own machine's share of the saturation mix, dialled by name.
        { "tape_source",     "TAPE",       1.0,   "SUBFUND"   },
        { "valve_source",    "VALVE",      0.0,   "SUBFUND"   },
        { "cassette_source", "CASSETTE",   0.0,   "SUBFUND"   },
        { "vinyl_source",    "VINYL",      0.0,   "SUBFUND"   },
        { "amp_source",      "AMP",        0.0,   "SUBFUND"   },
        { "transformer_source","TRANSFORMER",0.0,  "SUBFUND"   },
        { "digital_source",  "DIGITAL",    0.0,   "SUBFUND"   },
        { "amp_bias",        "AMP BIAS",   0.50,  "SUBFUND"   },
        { "sag",             "SAG",        0.0,   "SUBFUND"   },
        { "presence",        "PRESENCE",   0.50,  "CHARACTER"},
        { "cabinet",         "CABINET",    0.0,   "CHARACTER"},
        { "delay_time",      "DELAY",      0.0,   "DELAY"     },
        { "delay_feedback",  "DLY LVL",    0.0,   "DELAY"     },
        { "st_offset",       "ST OFFSET",  0.0,   "DELAY"     },
        { "noise",           "NOISE",      0.50,  "NOISE"    },
        { "subfund",         "SUBFUND",    0.0,   "SUBFUND"   },
        { "preamp",          "PREAMP",     0.0,   "FRONT END" },
        { "distortion",      "DISTORT",    0.0,   "FRONT END" },
        { "flux",            "FLUX",       0.50,  "CHARACTER"},
        { "wear",            "WEAR",       0.0,   "NOISE"    },
        { "mechanics",       "MECHANICS",  0.0,   "NOISE"    },
        { "reverb",          "REVERB",     0.0,   "REVERB"    },
        { "reverb_size",     "RVB SIZE",   0.40,  "REVERB"    },
        { "vinyl",           "VINYL",      0.0,   "VINYL"     },
        { "vinyl_crackle",   "CRACKLE",    0.50,  "VINYL"     },
        { "vinyl_rumble",    "RUMBLE",     0.35,  "VINYL"     },
        { "noise_lvl",       "NOISE LVL",  1.0,   "NOISE"    },
        { "st_link",         "ST LINK",    1.0,   "MACHINE"  },
        { "vinyl_dust",      "DUST",       0.0,   "RECORD"   },
        { "vinyl_scratch",   "SCRATCH",    0.0,   "RECORD"   },
        { "vinyl_warp",      "WARP",       0.0,   "RECORD"   },
        { "vinyl_electrical","ELECTRICAL", 0.0,   "RECORD"   },
        { "vinyl_clicks",    "CLICKS",     0.0,   "RECORD"   },
        { "in_low",          "IN LO",      0.0,   "IN EQ"    },
        { "in_mid",          "IN MID",     0.0,   "IN EQ"    },
        { "in_high",         "IN HI",      0.0,   "IN EQ"    },
        { "out_low",         "OUT LO",     0.0,   "OUT EQ"   },
        { "out_mid",         "OUT MID",    0.0,   "OUT EQ"   },
        { "out_high",        "OUT HI",     0.0,   "OUT EQ"   },
        { "di",              "DI",         0.0,   "FRONT END" },
        { "di_load",         "DI LOAD",    0.0,   "FRONT END" },
        { "di_transformer",  "DI XFMR",    0.0,   "FRONT END" },
        { "in_hp_freq",      "IN HP",      20.0,  "IN EQ"    },
        { "in_lp_freq",      "IN LP",      20000.0, "IN EQ"  },
        { "in_eq_q",         "IN Q",       0.7,   "IN EQ"    },
        { "out_hp_freq",     "OUT HP",     20.0,  "OUT EQ"   },
        { "out_lp_freq",     "OUT LP",     20000.0, "OUT EQ"  },
        { "out_eq_q",        "OUT Q",      0.7,   "OUT EQ"   },
        { "delay_pingpong",  "PING-PONG",  0.0,   "DELAY"     },
        { "transient_attack","ATK",        0.0,   "DYN"      },
        { "transient_sustain","SUS",       0.0,   "DYN"      },
        { "transient_mix",   "TR MIX",     0.0,   "DYN"      },
        { "neural_mix",      "NEURAL",     0.0,   "MODELS"    },
        { "ir_mix",          "IR MIX",     0.0,   "MODELS"    }
    } };

    // A tooltip for EVERY parameter - this is what shows when the user hovers.
    // The per-name text explains what the control does and its default, and the
    // generic interaction hints ride along on every knob.
    //
    // The function returns the ENGLISH sentence, untranslated on purpose:
    // setTip stores this exact string for the language switch and shows the
    // translation. It used to return xlat (...) + translated hints, so the
    // stored English was already Ukrainian on a Ukrainian panel - and the
    // replay's translate(already-translated) could never hit the table,
    // which is why the knob tooltips stayed English (or half-switched) after
    // a language change.
    inline juce::String parameterTooltipText (const juce::String& id)
    {
        if (id == "input")
            return "INPUT - output-stages the signal into the machine before the "
                   "tape. Positive pushes the tape harder for more saturation, "
                   "negative cleans up. Range -32 to +32 dB, default 0 dB.";
        if (id == "drive")
            return "DRIVE - the amount of magnetic saturation. At 0 percent the "
                   "machine is clean; higher settings bend the signal like tape "
                   "and add harmonics. Default 30 percent.";
        if (id == "bias")
            return "BIAS - the record head's ultra-sonic offset. It shapes the "
                   "even harmonics: low bias is edgy and thin, higher bias is "
                   "warmer and fuller. Default 42 percent.";
        if (id == "tone")
            return "BRIGHTNESS - a true tilt around the 1.6 kHz pivot: "
                   "low settings darken the machine (highs dip, lows lift), high "
                   "settings open it up (highs lift, lows pull back), +/-12 dB at "
                   "the extremes. 50 percent is the neutral pivot, so the spectral "
                   "balance passes through untouched. Default 50 percent.";
        if (id == "character")
            return "TONE - the machine-state macro. It crossfades the whole deck "
                   "between the classic slow machine (soft head gap, relaxed "
                   "flutter) and the fast hot machine (open top end, tight "
                   "flutter). Default 50 percent.";
        if (id == "wow")
            return "WOW - slow pitch wander of the transport, like a slightly "
                   "loose capstan. 0 percent is a perfectly steady machine. "
                   "Default 14 percent.";
        if (id == "flutter")
            return "FLUTTER - fast shimmer of the transport, like the tape "
                   "brushing the heads. 0 percent is perfectly steady. "
                   "Default 18 percent.";
        if (id == "mix")
            return "MIX - dry/wet crossfade. 0 percent is the untouched signal, "
                   "100 percent is fully through the tape. Default 50 percent.";
        if (id == "output")
            return "OUTPUT - calibrated output trim after the whole chain. "
                   "Range -32 to +32 dB, default 0 dB.";
        if (id == "stereo_width")
            return "WIDTH - stereo image after the tape. 0 percent is mono, "
                   "50 percent is the natural stereo width, 100 percent is extra "
                   "wide. Default 50 percent.";
        if (id == "subfund")
            return "SUBFUND - subharmonics. "
                   "Generates up to 8 undertones (1/2 through 1/9) "
                   "below the note when signal frequency permits, adding deep multi-layered "
                   "weight and warmth that regular saturation cannot reach. They fall away in a "
                   "staircase as they divide further, so the octave below is the strongest and "
                   "1/9 is 42 dB under it. Stages falling "
                   "below audible sub-bass (< 14-22 Hz) are smoothly attenuated to prevent DC "
                   "rumble, and the whole series stops with the note rather than continuing to "
                   "sound under it. They are added to the signal after the tape's own "
                   "saturation, so they stay clean partials instead of feeding it and coming "
                   "back out as a harmonic series. Default 0 percent - "
                   "it is a colour, not a correction.";
        if (id == "delay_time")
            return "DELAY - the spacing of a second playback head, in "
                   "milliseconds. The tape takes time to travel between the record and "
                   "playback gaps, so the signal returns as a slap rather than a dub "
                   "echo: real head spacings give tens of milliseconds, and 250 ms is "
                   "already the far end of the travel. The repeat is taken AFTER the "
                   "tape, so it inherits the machine's own bandwidth and saturation. "
                   "Default 0 ms - no second head engaged.";
        if (id == "delay_feedback")
            return "DLY LVL - how loud the second head's output is. At 0 "
                   "percent the delay is inaudible even with a time set, so TIME says "
                   "where the head is and DLY LVL says how much of it you hear. Each "
                   "pass round the tape loses top end, the way a real repeat does. "
                   "Default 0 percent.";
        if (id == "st_offset")
            return "ST OFFSET - the time offset between the two channels, "
                   "in microseconds. On a real stereo deck the two tracks are recorded "
                   "by separate head gaps a fraction of a millimetre apart and the tape "
                   "skews slightly across them, so the channels are never perfectly "
                   "aligned. Positive lags the right channel, negative the left. This "
                   "is a large part of why a tape bounce sounds wide rather than "
                   "merely equalised wide. Default 0 us.";
        if (id == "noise")
            return "NOISE MIX - how much of the noise SECTION is in the "
                   "output, exactly as MIX is how much of the tape section is. The "
                   "section it governs is every noise source at once: the tape "
                   "hiss floor, the vinyl crackle, the rumble and the groove "
                   "surface. The loaded tape formula's own character is "
                   "untouched - the formula sets the floor's shape, this sets how "
                   "much of it you hear. It is gated by the transport, so a "
                   "machine at rest is silent. Default 50 percent - the neutral "
                   "position, not a change.";
        if (id == "noise_lvl")
            return "NOISE LVL - the level of the noise SOURCES "
                   "themselves, tape and vinyl together. NOISE MIX is how much of "
                   "the noise section sits in the output; this is how loud what is "
                   "in that section actually runs, so it answers for every noise "
                   "source the machine makes, including the vinyl ones the old "
                   "NOISE control never reached. 100 percent is the calibrated "
                   "level the formulas and the record types were voiced at. "
                   "Default 100 percent.";
        if (id == "tape_source")
            return "TAPE - how much of the mix is the magnetic tape "
                   "machine: hysteresis with memory, gentle and warm, the "
                   "principle this plugin was calibrated as. Default 100 "
                   "percent.";
        if (id == "valve_source")
            return "VALVE - how much of the mix is the thermionic stage: "
                   "soft asymmetric compression, even-harmonic warmth. Shares "
                   "the mix with the other sources - the shares are "
                   "normalised, so the character changes without the level "
                   "moving. Default 0 percent.";
        if (id == "cassette_source")
            return "CASSETTE - how much of the mix is the compact cassette: "
                   "a hard early knee on narrow tape, small and loud. Default "
                   "0 percent.";
        if (id == "vinyl_source")
            return "VINYL - how much of the mix is the cutting lathe: the "
                   "gentlest tracing distortion of the seven, a soft "
                   "level-dependent knee like the groove rounding under the "
                   "stylus. Default 0 percent.";
        if (id == "amp_source")
            return "AMP - how much of the mix is the guitar amplifier's "
                   "input stage: hard, odd-harmonic, the one that bites. "
                   "Default 0 percent.";
        if (id == "transformer_source")
            return "TRANSFORMER - how much of the mix is the passive input "
                   "transformer: pure analogue but no machine, the only "
                   "principle that bends the low end first. Default 0 "
                   "percent.";
        if (id == "digital_source")
            return "DIGITAL - how much of the mix is the converter: a hard "
                   "ceiling with a held code, where the machines stop and the "
                   "conversion begins. Default 0 percent.";
        if (id == "amp_bias")
            return "AMP BIAS - the input valve's DC operating point, "
                   "which is the single most effective control on a real amp's "
                   "character. Cold (low) is tight and slightly crossover-distorted; "
                   "hot (high) is fat, compressed and soft. 50 percent is the "
                   "neutral centre, so it is a character sweep rather than a "
                   "one-way effect. Only audible in proportion to how much AMP is "
                   "in the blend.";
        if (id == "sag")
            return "SAG - how much the amplifier's power supply droops "
                   "under sustained demand. This is why a real amp 'gives' under a "
                   "held chord and why the attack feels spongy: the supply sags, "
                   "the gain falls a little, and then it recovers. 0 percent is a "
                   "stiff, regulated supply with no give at all; high settings are "
                   "a small amp being leaned on hard. Short transients never move "
                   "it - only sustained programme does. Default 0 percent.";
        if (id == "presence")
            return "PRESENCE - the negative-feedback network's top-end "
                   "lift, the upper-mid bite that makes an amp cut through. It sits "
                   "AFTER the clipping, so it sharpens harmonics already present "
                   "rather than generating new ones. 50 percent is the flat, "
                   "neutral position; above it sharpens, below it darkens the way "
                   "a low presence setting does. Default 50 percent.";
        if (id == "cabinet")
            return "CABINET - the speaker and its box. A resonant "
                   "low-pass, not a plain one: a peak around 110 Hz from the "
                   "cabinet's tuning and a roll-off from the cone's mass. Without "
                   "it a clipped signal is fizzy; with it, it reads as a speaker "
                   "rather than a circuit. The voicing fades in with however much "
                   "AMP is in the blend, so a pure tape setting is untouched. "
                   "Default 0 percent - a DI, the raw amp output.";
        if (id == "preamp")
            return "PREAMP - a valve input stage in FRONT of the "
                   "machine, the way a real chain has a microphone preamp before "
                   "the recorder. It is a STAGE, not a gain: driving it adds a "
                   "gentle soft clip, a transformer's low-cut and a slight top-end "
                   "lift, so it changes the colour as much as the level. It is "
                   "level-matched internally, so INPUT remains the control that "
                   "sets the operating level. Default 0 percent - no preamp.";
        if (id == "distortion")
            return "DISTORT - a diode clipper in front of the tape. "
                   "Where the saturation core BENDS, this BREAKS: a hard knee with "
                   "a pre-gain, deliberately abrupt. It sits ahead of the machine "
                   "on purpose - a distorted signal recorded to tape sounds like a "
                   "record rather than a pedal precisely because the tape smooths "
                   "what the pedal produced. Default 0 percent - off.";
        if (id == "flux")
            return "FLUX - how deep into the oxide the record head "
                   "magnetises. More flux is more low end, a stronger hysteresis "
                   "memory and a quieter floor; less is thin and bright. It is a "
                   "different axis from DRIVE: DRIVE is how hard the signal is "
                   "pushed into the curve, FLUX is how much of the medium's depth "
                   "is used. 50 percent is the calibrated, neutral flux.";
        if (id == "wear")
            return "WEAR - the state of the heads and the tape. A "
                   "used machine is not a broken one: the head gap has rounded "
                   "slightly and the oxide has lost some of its edge, so the top "
                   "end softens and the contact adds a little noise. It should "
                   "read as an old machine, not as a fault. Default 0 percent - "
                   "a fresh head and new tape.";
        if (id == "mechanics")
            return "MECHANICS - the state of the transport's moving "
                   "parts. WOW and FLUTTER set how much pitch modulation there is; "
                   "this sets how WELL the mechanism holds it. At 0 the capstan, "
                   "pinch roller and reel motors are in good order, so the "
                   "modulation is smooth and periodic. Higher settings are dry "
                   "bearings and a slack belt: irregular drift and the occasional "
                   "slip. Default 0 percent.";
        if (id == "reverb")
            return "REVERB - the room the machine is in. Tape "
                   "machines lived in rooms, and the room is part of the sound of "
                   "a recording made on one. This is a plate/room hybrid placed "
                   "AFTER the machine, so the reverb is of the processed signal "
                   "rather than feeding back into the saturation - which keeps it "
                   "clean and predictable. Default 0 percent - dry.";
        if (id == "reverb_size")
            return "RVB SIZE - how long the reverb's tail runs, and "
                   "how dark it is: a bigger room absorbs more top end per pass, "
                   "so a long tail is a darker one. That is what stops a large "
                   "setting from sounding like a metal tank. Decay and level are "
                   "separate decisions on a real reverb, which is why this is not "
                   "folded into REVERB. Default 40 percent.";
        if (id == "vinyl")
            return "VINYL - the record-playing end of the chain. A "
                   "turntable adds three things nothing else here does: surface "
                   "crackle, bearing rumble and the RIAA playback curve's low-end "
                   "lift and top-end softness. This is the overall amount; at 0 "
                   "the whole stage is bypassed. Default 0 percent.";
        if (id == "vinyl_crackle")
            return "CRACKLE - surface noise, and specifically "
                   "IMPULSES rather than hiss. A record surface is ticks, caused "
                   "by dust and by the stylus crossing the groove's "
                   "imperfections, so the generator produces sparse impulses with "
                   "a fast decay instead of continuous noise. That is the "
                   "difference between a record and a noisy tape. Default 50 "
                   "percent.";
        if (id == "vinyl_rumble")
            return "RUMBLE - the turntable's low-frequency thump, "
                   "from the bearing and the motor. It is why vinyl has a "
                   "bottom-end floor that a CD does not. It is a different noise "
                   "from the crackle and is scaled separately, because a worn "
                   "bearing and a dusty record are independent faults. Default "
                   "35 percent.";
        if (id == "vinyl_speed")
            return "VINYL SPEED - the speed of the turntable's motor, "
                   "not of the record: the VINYL TYPE describes the disc, this "
                   "describes what drives it. Each speed carries its own wow rate "
                   "and depth, so the same record wanders differently at 33 and "
                   "45, and a 78's wind-up motor wobbles hardest. 33 RPM is the "
                   "default.";
        if (id == "vinyl_dust")
            return "DUST - fine particulate in the groove. Unlike "
                   "CRACKLE's random ticks, dust is a CONTINUOUS granular "
                   "texture, band-limited high so it sits on top of the "
                   "music as grit. It follows the programme: a loud passage "
                   "sounds dirtier than a quiet one, because dust only makes "
                   "a sound when there is modulation in the groove to "
                   "disturb it. A little dust is a worn record; a lot is a "
                   "record that has been left out of its sleeve. "
                   "Default 0 percent.";
        if (id == "vinyl_scratch")
            return "SCRATCH - a deep groove wound, not dust. Where "
                   "a dust tick is random, a scratch is PERIODIC: the stylus "
                   "crosses the same damage every turn, so it arrives at the "
                   "platter rate and is heard as a repeating thud rather "
                   "than as a hiss. The rate follows VINYL SPEED, so the same "
                   "scratch repeats faster on a 45 than on a 33. "
                   "Default 0 percent.";
        if (id == "vinyl_warp")
            return "WARP - the record is not flat. A warped disc "
                   "makes the stylus ride up and down once per revolution, "
                   "so the tracking force - and therefore the output level - "
                   "breathes at the platter rate. It is a slow, cyclic "
                   "throb, which is what makes a warped record sound like it "
                   "is struggling rather than merely noisy. It modulates "
                   "what the stage passes rather than what it adds, so at 0 "
                   "percent it is exactly unity. Default 0 percent.";
        if (id == "vinyl_electrical")
            return "ELECTRICAL - the cartridge, the cable and the "
                   "earth loop. Two faults at once: MAINS HUM at the supply "
                   "frequency plus its second harmonic, which is what an "
                   "unearthed cartridge picks up from the motor and the "
                   "transformer; and EARTH STATIC, the broadband crackle of "
                   "a bad ground. The hum's two sides are deliberately not "
                   "identical - the second harmonic is in anti-phase across "
                   "the pair - which is exactly how a real earth loop "
                   "behaves. Default 0 percent.";
        if (id == "in_low")
            return "IN LO - the input equaliser's low shelf, a "
                   "low-pass split at 200 Hz with the bottom band gained. "
                   "Because it sits BEFORE the tape, lifting it drives the "
                   "saturation curve and the glue compressors harder, so it "
                   "changes what the machine DOES rather than merely the "
                   "balance. Range -12 to +12 dB, flat at 0 dB.";
        if (id == "in_mid")
            return "IN MID - the input equaliser's bell, centred at "
                   "1 kHz inside the 200 Hz - 4 kHz band. A bell rather than a "
                   "shelf, so it acts on its own pass band and leaves the two "
                   "ends where they were. Range -12 to +12 dB, flat at 0 dB.";
        if (id == "in_high")
            return "IN HI - the input equaliser's high shelf, "
                   "everything above 4 kHz. Feed the machine the top end you "
                   "want it to saturate on, rather than fixing it afterwards. "
                   "Range -12 to +12 dB, flat at 0 dB.";
        if (id == "out_low")
            return "OUT LO - the output equaliser's low shelf at "
                   "200 Hz. It sits AFTER everything the machine does and "
                   "before the output trim, so nothing downstream responds to "
                   "it: this is the neutral EQ you use to place the finished "
                   "sound. Range -12 to +12 dB, flat at 0 dB.";
        if (id == "out_mid")
            return "OUT MID - the output equaliser's bell at "
                   "1 kHz. Presence or hollow, depending which way you go, "
                   "with both ends left alone. Range -12 to +12 dB, flat at "
                   "0 dB.";
        if (id == "out_high")
            return "OUT HI - the output equaliser's high shelf "
                   "above 4 kHz. Air, or the lack of it, applied to the "
                   "finished machine. Range -12 to +12 dB, flat at 0 dB.";
        if (id == "vinyl_clicks")
            return "CLICKS - the sharp, discrete groove faults. "
                   "CRACKLE is fine surface texture and DUST is grit in the "
                   "groove; a CLICK is an actual ridge or pit that the stylus "
                   "hits as a single hard transient - a fast bipolar impact "
                   "with almost no ring, which is why it reads as a click "
                   "rather than as more crackle. Above half the travel a "
                   "fraction of the clicks becomes PERIODIC, locked to the "
                   "platter, so a badly pressed record ticks in time rather "
                   "than at random. Default 0 percent.";
        if (id == "transient_attack")
            return "ATK - the leading edge of each event. The transient "
                   "shaper looks at the signal's ENVELOPE rather than its "
                   "waveform, so it can make a hit sharper without adding a "
                   "harmonic: positive sharpens the attack (punch, snap, "
                   "click), negative softens it (rounder, less percussive). "
                   "Range -100 to +100 percent, neutral at 0.";
        if (id == "transient_sustain")
            return "SUS - what follows the attack: the body, the ring, "
                   "the room. Positive lengthens it (fuller, more sustain), "
                   "negative shortens it (tighter, more staccato). Like ATK "
                   "it moves the envelope's level rather than the waveform, "
                   "so the harmonics the machine produced are untouched. "
                   "Range -100 to +100 percent, neutral at 0.";
        if (id == "transient_mix")
            return "TR MIX - how much of the transient-shaped signal "
                   "reaches the output. At 0 the stage is absent and the "
                   "signal passes untouched; at 100 it is the fully shaped "
                   "one. In between the two are crossfaded, so the amount of "
                   "shaping can be dialled in rather than switched. "
                   "Default 0 percent.";
        if (id == "neural_mix")
            return "NEURAL - the wet/dry position of the optional "
                   "learned model. The model is not a knob: it is a file "
                   "loaded with LOAD MODEL on the MODELS tab, and the "
                   "network itself (a Dense net, an LSTM, a GRU) is whatever "
                   "the file describes. This control blends the model's "
                   "output with the untouched signal, so it is always a "
                   "crossfade. At 0, or with no model loaded, the stage is "
                   "transparent. Default 0 percent.";
        if (id == "ir_mix")
            return "IR MIX - how much of the convolved signal is blended "
                   "into the finished one. The impulse response itself is "
                   "not a knob: it is a file loaded with LOAD IR on the "
                   "MODELS tab, and it can be a cabinet, a room or any "
                   "other space. At 0, or with no IR loaded, the stage is "
                   "transparent. Default 0 percent.";
        // The eleven knobs that were falling through to an empty sentence:
        // they showed the shared interaction hint only, which the user read
        // as "tooltip is missing". One branch each, same shape as the rest.
        if (id == "di")
            return "DI - the instrument input's direct-injection stage in "
                   "FRONT of the machine: a high-impedance buffer that keeps "
                   "a guitar or synth pickup from loading down, plus the pad, "
                   "load and transformer that follow it. Default 0 percent - "
                   "the plain input path.";
        if (id == "di_load")
            return "DI LOAD - the input impedance the instrument sees in "
                   "the DI stage. A pickup is a reactive source: 1 M keeps "
                   "a guitar bright and open, 220 k tames a hot active bass, "
                   "and lower still tightens a synth's output stage. Default "
                   "0 percent.";
        if (id == "di_transformer")
            return "DI XFMR - the DI's output transformer. OFF is a clean "
                   "electronic path; the transformer adds its iron colour, "
                   "a gentle low cut and the slight phase smear a real direct "
                   "box gives. Default 0 percent - off.";
        if (id == "st_link")
            return "ST LINK - gangs the stereo pair's shared controls so one "
                   "knob sets both channels. Off splits the machine into two "
                   "independent sides for separate left/right settings. "
                   "Default linked.";
        if (id == "delay_pingpong")
            return "PING-PONG - bounces the second head's repeats between "
                   "the left and right output instead of doubling them in "
                   "both channels. Off, every repeat lands centred. Default "
                   "0 percent - off.";
        if (id == "in_eq_q")
            return "IN Q - the input equaliser's corner resonance, shared "
                   "by its bell and the shoulders of its shelves. 0.7 is the "
                   "flat Butterworth corner; higher sharpens the bell's "
                   "shoulder and rings slightly at the corner frequency. "
                   "Default 0.7.";
        if (id == "out_eq_q")
            return "OUT Q - the output equaliser's corner resonance, shared "
                   "by its bell and the shoulders of its shelves. Same curve "
                   "as the input Q, applied AFTER the machine, where it shapes "
                   "the result rather than the drive. Default 0.7.";
        if (id == "in_hp_freq")
            return "IN HP - the input equaliser's high-pass corner. "
                   "Everything below it is removed BEFORE the machine, so a "
                   "bass-heavy source can be kept from flapping the tape's "
                   "low end. 20 Hz is effectively out of the way.";
        if (id == "in_lp_freq")
            return "IN LP - the input equaliser's low-pass corner. "
                   "Everything above it is removed BEFORE the machine, the "
                   "way a deck's record electronics roll the top off. "
                   "20 kHz is effectively out of the way.";
        if (id == "out_hp_freq")
            return "OUT HP - the output equaliser's high-pass corner. "
                   "Everything below it is removed AFTER the machine, on the "
                   "finished sound, where nothing downstream responds to it. "
                   "20 Hz is effectively out of the way.";
        if (id == "out_lp_freq")
            return "OUT LP - the output equaliser's low-pass corner. "
                   "Everything above it is removed AFTER the machine, on the "
                   "finished sound, where nothing downstream responds to it. "
                   "20 kHz is effectively out of the way.";
        return {};
    }
}
