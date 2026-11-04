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

// The panel's knob strip as ONE number: ninety-seven controls. Declared here,
// above BOTH of its users, because they no longer live in the same scope: the
// class below sizes its sliders, labels and attachments from it, and the control
// table that NAMES those knobs is at file scope in this header (THE CONTROL
// TABLE, below) - a constant inside the class is not visible there,
// which is a build error rather than a warning, and was one.
//
// The count grew from 64 when the room gained PRE-RVB, DAMP and WIDTH and the
// second head gained DLY TONE and LOW CUT, then from 69 when the second head
// gained DLY DAMP and the room gained RVB LOW. They are appended to the table so
// every existing index keeps its meaning.
//
// The count, the controlIds / controlNames lists, the defaultValues array and the
// tabSpecs table are all views of ONE list and must agree. Sizing the arrays from
// this name rather than from a literal is what turns a mismatch into a compile
// error (C2078) instead of a silent out-of-bounds read at run time, and the tab
// table is checked against this count by a static_assert in setCurrentTab, so a
// knob that no tab lists is a build error too.
inline constexpr std::size_t controlCount = 97;

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
        { "ir_mix",          "IR MIX",     0.0,   "MODELS"    },
        // The room and the second head grew independent voicing controls. They
        // are appended rather than inserted so every existing tab index and every
        // stored default keeps its meaning; tabSpecs names them by index below.
        { "reverb_predelay", "PRE-RVB",   20.0,  "REVERB"    },
        { "reverb_damp",     "DAMP",       0.45,  "REVERB"    },
        { "reverb_width",    "RVB WIDTH",  1.0,   "REVERB"    },
        { "delay_tone",      "DLY TONE",   0.5,   "DELAY"     },
        { "delay_lowcut",    "LOW CUT",    80.0,  "DELAY"     },
        // The second head's feedback damper and the room's low cut. Both are
        // appended, like the controls above, so no tab index moves.
        { "delay_damp",      "DLY DAMP",   0.0,   "DELAY"     },
        { "reverb_lowcut",   "RVB LOW",   120.0,  "REVERB"    },

        // -------------------------------------------------------------------
        //  THE EFFECTS RACK - one row per stage, appended so every existing
        //  index keeps its meaning (the tab table below refers to controls by
        //  index, and the array is sized from controlCount).
        //
        //  One knob per stage, with SPEED the only non-zero default: it is a
        //  three-position control (half / 1x / double) spread across a continuous
        //  knob, so 0.5 is its neutral. Every other row resets to 0, which is out
        //  of the path - see the note on addEffectsRackControls in
        //  Source/ParameterTable.cpp.
        // -------------------------------------------------------------------
        { "fx_wah",          "WAH",       0.0,   "SHAPE"     },
        { "fx_anti_spike",   "SPIKE",     0.0,   "DRIVE"     },
        { "fx_gate",         "GATE",      0.0,   "DRIVE"     },
        { "fx_overdrive",    "DRIVE",     0.0,   "DRIVE"     },
        { "fx_fuzz",         "FUZZ",      0.0,   "DRIVE"     },
        { "fx_wavefolder",   "FOLD",      0.0,   "DRIVE"     },
        { "fx_bitcrusher",   "BITS",      0.0,   "DRIVE"     },
        { "fx_rate_crusher", "RATE",      0.0,   "DRIVE"     },
        { "fx_ring_mod",     "RING",      0.0,   "SHAPE"     },
        { "fx_octaver",      "OCTAVE",    0.0,   "PITCH"     },
        { "fx_pitch",        "PITCH",     0.0,   "PITCH"     },
        { "fx_comb",         "COMB",      0.0,   "SHAPE"     },
        { "fx_formant",      "FORMNT",    0.0,   "SHAPE"     },
        { "fx_dynamic_eq",   "DYN EQ",    0.0,   "SHAPE"     },
        { "fx_de_esser",     "DE-ESS",    0.0,   "SHAPE"     },
        { "fx_chorus",       "CHORUS",    0.0,   "MOTION"    },
        { "fx_flanger",      "FLANGER",   0.0,   "MOTION"    },
        { "fx_phaser",       "PHASER",    0.0,   "MOTION"    },
        { "fx_tremolo",      "TREM",      0.0,   "MOTION"    },
        { "fx_vibrato",      "VIBRATO",   0.0,   "MOTION"    },
        { "fx_rotary",       "ROTARY",    0.0,   "MOTION"    },
        { "fx_vocoder",      "VOCODER",   0.0,   "MOTION"    },
        { "fx_granular",     "GRAIN",     0.0,   "PITCH"     },
        { "fx_spectral",     "SPECTRL",   0.0,   "SHAPE"     },
        { "fx_speed",        "SPEED",     0.5,   "PITCH"     },
        { "comp_upward",     "UPWARD",    0.0,   "COMP"      }
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
            return "RVB SIZE - how long the reverb's tail runs. Decay and "
                   "level are separate decisions on a real reverb, which is why "
                   "this is not folded into REVERB. DAMP is the separate colour "
                   "control, so a long tail does not have to be a dark one. "
                   "Default 40 percent.";
        if (id == "reverb_predelay")
            return "PRE-RVB - how long after the dry note the room starts, in "
                   "milliseconds. A pre-delay separates the source from its tail, "
                   "which is what makes a vocal sit in front of a plate instead of "
                   "inside it. It is applied at the session rate, so 20 ms is "
                   "20 ms at every sample rate. Default 20 ms.";
        if (id == "reverb_damp")
            return "DAMP - how dark the room is, independent of its size. At 0 "
                   "the tail keeps its top end; at 100 the high frequencies are "
                   "absorbed on every pass, which is a soft, dark plate. It is the "
                   "library's one-pole damping coefficient, inverted so that more "
                   "DAMP means less top end. Default 45 percent.";
        if (id == "reverb_width")
            return "RVB WIDTH - the stereo image of the wet pair. 0 collapses the "
                   "room to mono, 100 is the tank's own two taps, and 200 doubles "
                   "the side signal for a wider tail. It is a mid/side trim, not a "
                   "second room. Default 100 percent.";
        if (id == "reverb_lowcut")
            return "RVB LOW - the high-pass at the room's input, in Hz. Below "
                   "this the room hears nothing, which is what stops a full mix "
                   "turning into low-end cloud. 120 Hz is the default; lower it "
                   "for a bass or a cello you want placed in the space, raise it "
                   "for a thin, bright plate. Default 120 Hz.";
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
        if (id == "di_pad")
            return "DI PAD - a fixed attenuator in front of the DI stage, for a "
                   "hot instrument or a loud pickup that would otherwise clip "
                   "the input before the machine ever sees it. Default 0 percent "
                   "- no pad.";
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
        if (id == "delay_tone")
            return "DLY TONE - the colour of the repeats. It is a tilt around "
                   "2.5 kHz: 50 percent is neutral, above it the repeats darken, "
                   "below they brighten. Unlike the machine's damping it works on "
                   "TAPE, BBD and MODERN alike. Default 50 percent.";
        if (id == "delay_lowcut")
            return "LOW CUT - a high-pass at the head of the repeat path, in Hz. "
                   "Feedback collects low end, and a long delay turns a bass note "
                   "into a rising cloud; this removes it before the first repeat. "
                   "80 Hz is below most programme, 500 Hz is a deliberately thin "
                   "echo. Default 80 Hz.";
        if (id == "delay_damp")
            return "DLY DAMP - a low-pass in the feedback path only. Each pass "
                   "loses a little more top end, so a long MODERN or BBD repeat "
                   "settles instead of stacking into a metallic ring, while the "
                   "first repeat keeps the tone DLY TONE set. 0 is fully open; "
                   "100 is a dark, tape-like decay. Default 0 percent.";
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

        // The switches, the selectors and the engine settings. These are not
        // knobs on the grid, which is why they were missing while every knob had
        // a sentence - and they are exactly the rows the PARAMETERS sheet lists
        // with nothing but a name (BYPASS, DI XFMR, TRACKS). Same shape, same
        // table, so the sheet, the tooltip and the translation agree.
        if (id == "bypass")
            return "BYPASS - hard bypass: the tape engine and both glue "
                   "compressors are switched out of the path. The switch is "
                   "ramped, so toggling it never clicks.";
        if (id == "delta")
            return "DELTA - listen to the difference: the output becomes the "
                   "finished signal minus the machine's own dry signal, so you "
                   "hear only what the machine adds - harmonics, glue, transport "
                   "wander. Turn it off before you print.";
        if (id == "polarity")
            return "POLARITY - inverts the output polarity (a 180-degree phase "
                   "flip), after the protection chain and the meters' magnitude "
                   "path. Use it to correct an inverted source or to align two "
                   "machines feeding one bus.";
        if (id == "auto_gain")
            return "AUTO GAIN - lets the slow programme compensator restore the "
                   "level the INPUT trim dialled in. Off keeps the output "
                   "exactly at the level the chain produced.";
        if (id == "in_hp_on")
            return "IN HP ON - switches the input equaliser's high-pass in or "
                   "out without moving its corner: off passes everything below "
                   "it, and the frequency stays where you set it for next time.";
        if (id == "in_lp_on")
            return "IN LP ON - switches the input equaliser's low-pass in or "
                   "out without moving its corner. Off lets the full top end "
                   "reach the machine.";
        if (id == "out_hp_on")
            return "OUT HP ON - switches the output equaliser's high-pass in or "
                   "out without moving its corner. Off lets the machine's own "
                   "low end out untouched.";
        if (id == "out_lp_on")
            return "OUT LP ON - switches the output equaliser's low-pass in or "
                   "out without moving its corner. Off lets everything above "
                   "the corner through to the host.";
        if (id == "in_eq_order")
            return "IN EQ ORDER - the slope of the input equaliser's corners, "
                   "from 6 dB/oct (a soft shoulder) to 48 dB/oct (a hard fence). "
                   "The bell keeps the same shape at every order.";
        if (id == "out_eq_order")
            return "OUT EQ ORDER - the slope of the output equaliser's corners, "
                   "from 6 dB/oct (a soft shoulder) to 48 dB/oct (a hard fence). "
                   "Applied after the machine, so it fences off the result.";
        if (id == "anti_phase_guard")
            return "ANTI-PHASE GUARD - watches the stereo pair for content that "
                   "will cancel when the host folds the mix to mono, and corrects "
                   "it before the output. Off, the panel only reports the risk in "
                   "the ANTI-PHASE readout.";
        if (id == "tape_type")
            return "TAPE TYPE - the formula the machine's saturation is built "
                   "from: how hard the tape bends, how much top end survives and "
                   "how the glue compressors time. J37 is soft and classic, "
                   "Ampex and Studer are hotter, Chrome and Type 111 are cleaner, "
                   "GP9 and 499 are dense modern formulas.";
        if (id == "speed")
            return "SPEED - the transport speed: 7.5 ips is dark and loose, "
                   "15 ips is the classic studio speed, 30 ips keeps the most top "
                   "end and the tightest glue. It also shapes the glue timing and "
                   "how long the deck's own gestures take.";
        if (id == "instrument")
            return "INSTRUMENT - re-voices the whole machine for what is being "
                   "recorded: how hard the tape bends, how much top end survives, "
                   "how loud the noise floor sits and how steady the transport "
                   "runs. Master Bus is the neutral calibration, Drums the slam.";
        if (id == "oversampling")
            return "OVERSAMPLING - the engine's internal rate: 2x and 4x reduce "
                   "the aliasing of the magnetic shaper. The added filter delay is "
                   "reported to the host, so the DAW's delay compensation covers "
                   "it. Off runs at the session rate.";
        if (id == "tracks")
            return "TRACKS - how many tape tracks the deck is modelled as: two, "
                   "the 2+3 pair, or three. It sets how many heads' worth of "
                   "wander and gap loss the transport has, and the default is "
                   "two.";
        if (id == "transport")
            return "TRANSPORT - the deck's state: STOP parks the capstan, PLAY is "
                   "normal running and START is the engagement gesture that "
                   "settles into PLAY. It is written by the transport keys and by "
                   "the host's own transport.";
        if (id == "spindown")
            return "SPINDOWN - the momentary platter hold: while it is on, the "
                   "machine winds down as if the power were cut, and it comes back "
                   "up when it is released.";
        if (id == "ui_sounds")
            return "UI SOUNDS - the panel's own clicks and ticks. They play "
                   "through a separate audio device, so they can never reach the "
                   "plugin's output, be recorded or be bounced. Off is silent.";
        if (id == "tuning_tonic")
            return "TONIC - the root the pitch correction quantises to. With "
                   "TUNE AMOUNT above zero, the corrected pitch is pulled toward "
                   "the scale that starts on this note, up to RANGE semitones.";
        if (id == "tuning_mode")
            return "MODE - the scale the pitch correction quantises to: major or "
                   "minor on the TONIC, or chromatic, which snaps to semitones "
                   "only. RANGE caps how far a reading may travel to reach that "
                   "scale. It has no effect while TUNE AMOUNT is zero.";
        if (id == "autotune_amount")
            return "TUNE AMOUNT - the depth of the correction and the only on/off: "
                   "0 is the untouched signal and takes the whole stage, detector "
                   "included, out of the path; 100 is the full pull the current "
                   "RANGE allows. It does not change the retune speed - SPEED owns "
=======
                   "TUNE AMOUNT above zero, the corrected pitch is snapped to "
                   "the scale that starts on this note.";
        if (id == "tuning_mode")
            return "MODE - the scale the pitch correction quantises to: major or "
                   "minor on the TONIC, or chromatic, which snaps to semitones "
                   "only. It has no effect while TUNE AMOUNT is zero.";
        if (id == "autotune_amount")
            return "TUNE AMOUNT - the depth of the correction and the only on/off: "
                   "0 is the untouched signal and takes the whole stage, detector "
                   "included, out of the path; 100 is the full pull onto the "
                   "scale. It does not change the retune speed - SPEED owns "
                   "that - so a deep, natural correction is possible without "
                   "the snap. Default 0 percent (off).";
        if (id == "autotune_speed")
            return "SPEED - how quickly the correction glides to its new pitch, "
                   "independent of TUNE AMOUNT. 0 percent is a slow, transparent "
                   "100 ms glide; 50 percent is about 13 ms; 100 percent is a "
                   "1 ms snap. Slower settings are what keep consonants and "
                   "vibrato from being smeared. Default 50 percent.";
        if (id == "autotune_range")
            return "RANGE - how far, in semitones, the corrector may pull a "
                   "reading toward the note the key asks for. 1 is the classic "
                   "grip: a note is moved onto the grid, never transposed. "
                   "Larger values let a scale's nearest note (or a deliberately "
                   "heavy correction) be reached; 12 is the whole octave. "
                   "Default 1 semitone.";
        if (id == "autotune_octave")
            return "OCTAVE - transposes the corrected result by whole octaves, "
                   "after RANGE has taken its grip. 0 leaves the note where the "
                   "scale puts it; +1 and -1 move it one octave up or down. "
                   "TUNE AMOUNT scales the transposition like every other pull. "
                   "Default 0.";
        if (id == "autotune_tolerance")
            return "TOLERANCE - the dead zone around the target note, in cents. "
                   "A reading inside it is left bit-for-bit alone, which keeps "
                   "the human part of the performance. 0 corrects every reading, "
                   "10 is the default and 50 lets a wide vibrato through. "
                   "Default 10 cents.";
        if (id == "autotune_gate")
            return "GATE - how confident the pitch detector must be before the "
                   "corrector trusts a reading. Higher settings leave breath, "
                   "noise and ambiguous chords alone; lower settings follow "
                   "quiet or noisy notes but can chase a reading that is not a "
                   "pitch. Default 50 percent.";
        if (id == "comp_threshold")
            return "COMP THRESHOLD - the level above which the conventional "
                   "compressor starts working. Lower thresholds compress more of "
                   "the signal; it is independent of both glue stages.";
        if (id == "comp_ratio")
            return "COMP RATIO - how hard the compressor holds what crosses the "
                   "threshold: 4:1 is a firm general-purpose setting, higher "
                   "ratios approach limiting.";
        if (id == "comp_attack")
            return "COMP ATTACK - how quickly the compressor clamps down once the "
                   "signal crosses the threshold. Fast keeps transients in check, "
                   "slow lets the first hit through before it reacts.";
        if (id == "comp_release")
            return "COMP RELEASE - how long the compressor takes to let go after "
                   "the signal falls back under the threshold. Short is lively, "
                   "long is smooth and hides the gain movement.";
        if (id == "comp_makeup")
            return "COMP MAKEUP - the gain added back after compression, so the "
                   "level can be restored without changing the compression.";
        if (id == "comp_mix")
            return "COMP MIX - the compressor's dry/wet crossfade. At 0 the "
                   "compressor is absent, at 100 the signal is fully compressed; "
                   "in between it is blended, so parallel compression is one "
                   "control.";
        if (id == "language")
            return "LANGUAGE - the interface language of the panel and its "
                   "sheet. It is an editor setting, saved with the session so a "
                   "collaborator opens what the sender saw; it never changes the "
                   "sound.";
        if (id == "delay_type")
            return "DELAY TYPE - which machine the second head behaves as: a "
                   "tape echo with its own wander, a clean digital repeat, or the "
                   "short slap a deck's second head gives. It is a choice of "
                   "machines, not a continuous control.";
        if (id == "delay_sync")
            return "DELAY SYNC - locks the delay time to the host tempo instead "
                   "of to milliseconds, so the repeats stay in time when the "
                   "session's tempo changes. DELAY RATE picks the note value it "
                   "syncs to.";
        if (id == "delay_rate")
            return "DELAY RATE - the note value the delay syncs to when DELAY "
                   "SYNC is on, from a sixteenth to a whole bar. It has no effect "
                   "with sync off, where DELAY sets the time in milliseconds.";
        if (id == "modern_mode")
            return "MODERN - the modern-voicing switch: opens the top end and "
                   "tightens the glue, the way a freshly aligned deck with new "
                   "tape does. Off is the older, rounder calibration.";
        if (id == "lofi_mode")
            return "LO-FI - the deliberately compromised voicing: narrower "
                   "bandwidth, more noise and a looser transport, the sound of a "
                   "well-used machine rather than a serviced one.";
        if (id == "vinyl_generation")
            return "GENERATION - which pressing the record is: an original "
                   "pressing, a reissue or a later, worn copy. Each generation "
                   "carries its own surface noise and its own amount of wear.";
        if (id == "vinyl_turntable")
            return "TURNTABLE - the deck the record is played on. It sets the "
                   "mechanical character: rumble, platter stability and how much "
                   "the arm picks up from the motor.";
        if (id == "vinyl_cartridge")
            return "CARTRIDGE - the pickup fitted to the arm: how much surface "
                   "and how much of the groove's own body it hands to the "
                   "preamp.";
        if (id == "valve_type")
            return "VALVE TYPE - the voice of the VALVE principle. Only audible "
                   "in proportion to how much VALVE is in the BLEND: a 12AX7 "
                   "runs cold and tight, an EL34 or 6L6 is fatter and more "
                   "compressed, a 300B is the softest and most even, a KT88 has "
                   "the widest drift.";
        if (id == "amp_type")
            return "AMP TYPE - the voice of the AMP principle. Only audible in "
                   "proportion to how much AMP is in the BLEND: the Blackface is "
                   "clean and firm, the Plexi bites, the AC30 chimes, the Recto "
                   "slams hardest in the second stage.";
        if (id == "transformer_type")
            return "TRANSFORMER TYPE - the voice of the TRANSFORMER principle. "
                   "Only audible in proportion to how much TRANSFORMER is in the "
                   "BLEND: the types set how fast the core tracks and how early it "
                   "starts to saturate - a nickel core bends earlier, steel later "
                   "and harder.";
        if (id == "digital_type")
            return "DIGITAL TYPE - the voice of the DIGITAL principle. Only "
                   "audible in proportion to how much DIGITAL is in the BLEND: "
                   "the types shorten the word length and deepen the "
                   "sample-and-hold, from a 16-bit ceiling to full bit-crush.";
        if (id == "vinyl_type")
            return "VINYL TYPE - the record itself, not the machine: how it was "
                   "pressed and how worn it is. Audible whenever VINYL is up, "
                   "and independent of the BLEND. Shellac 78 plays with loud "
                   "surface between every note, a dubplate is a fresh loud "
                   "lacquer, a half-speed master is nearly silent between the "
                   "grooves. The speed of the motor is VINYL SPEED.";
        // -------------------------------------------------------------------
        //  THE EFFECTS RACK. One sentence pair per stage, in the same shape as
        //  the rest: what it does, then what the knob means. Every one of them
        //  ends the same way because it is true of every one of them - 0 is out
        //  of the path, not a bypass.
        // -------------------------------------------------------------------
        if (id == "fx_wah")
            return "WAH - an envelope filter: a resonant band whose centre is "
                   "pushed up by the signal's own level, so quiet playing stays "
                   "dark and a harder note opens it. 0 takes it out of the path.";
        if (id == "fx_anti_spike")
            return "SPIKE - the guitarist's spike suppressor, for the clicks a "
                   "pick, a jack or a knocked cable put into a take. It is a "
                   "median of the last three samples plus a slew limit, so a "
                   "one-sample burst cannot pass; it does not act on the envelope. "
                   "0 takes it out of the path.";
        if (id == "fx_gate")
            return "GATE - a noise gate: below its threshold the stage closes. "
                   "The threshold rises with the knob, from a gentle -60 dB floor "
                   "to a firm -6 dB, with hysteresis so a signal near the line "
                   "does not chatter. 0 takes it out of the path.";
        if (id == "fx_overdrive")
            return "DRIVE - an antialiased soft-clip stage, the symmetric one of "
                   "the pair. Drive and make-up are tied together, so the knob "
                   "changes how much break-up there is rather than the volume. "
                   "0 takes it out of the path.";
        if (id == "fx_fuzz")
            return "FUZZ - the same antialiased curve with far more gain and an "
                   "asymmetric bias, which is where its octave-ish edge comes "
                   "from. The bias is subtracted back out so it does not put DC on "
                   "the chain. 0 takes it out of the path.";
        if (id == "fx_wavefolder")
            return "FOLD - a wavefolder: sin(x) as a transfer curve, so past a "
                   "quarter turn the curve turns back on itself. The harmonics it "
                   "add are not the odd series a clip makes, which is why it is a "
                   "different sound and not a stronger drive. 0 takes it out of "
                   "the path.";
        if (id == "fx_bitcrusher")
            return "BITS - quantisation. Full travel is about 2 bits, the bottom "
                   "of the knob is 16 and transparent enough to leave in the "
                   "path. 0 takes it out of the path.";
        if (id == "fx_rate_crusher")
            return "RATE - sample-and-hold at a fraction of the engine rate: the "
                   "alias-generating half of a bit-crush, kept separate because "
                   "it is a different control. 0 takes it out of the path.";
        if (id == "fx_ring_mod")
            return "RING - a ring modulator. The carrier is mapped from 30 Hz to "
                   "just under a kilohertz by this knob, and the knob also sets "
                   "how much of the product is heard, so the stage fades in as a "
                   "tremolo and arrives as a ring mod. 0 takes it out of the "
                   "path.";
        if (id == "fx_octaver")
            return "OCTAVE - the octave below: the shifter at -12 semitones with "
                   "its top taken off, so the sub is felt rather than heard as "
                   "grain. It tracks polyphonically, which the classic divider "
                   "does not. 0 takes it out of the path.";
        if (id == "fx_pitch")
            return "PITCH - a two-tap granular shifter, from unison at the bottom "
                   "of the knob to an octave up at the top, crossfaded so the "
                   "taps' travel is hidden. 0 takes it out of the path.";
        if (id == "fx_comb")
            return "COMB - a resonant comb at a fixed 4 ms delay (a 250 Hz "
                   "fundamental) with a damping pole in its feedback. The knob is "
                   "the RESONANCE, not the tuning - a comb that follows the knob "
                   "is a flanger, and the rack has one. 0 takes it out of the "
                   "path.";
        if (id == "fx_formant")
            return "FORMNT - three resonant peaks walked through the five "
                   "cardinal vowels, a at the bottom of its travel and u at the "
                   "top, with F2 and F3 carrying the vowel's identity. 0 takes it "
                   "out of the path.";
        if (id == "fx_dynamic_eq")
            return "DYN EQ - one band at 150-600 Hz that is cut only while that "
                   "band is loud. A static cut hollows out a mix that did not need "
                   "it; this responds to the programme. 0 takes it out of the "
                   "path.";
        if (id == "fx_de_esser")
            return "DE-ESS - a split-band compressor that only ever acts on the "
                   "band above 5 kHz. Splitting first is what keeps the body of "
                   "the word intact, which is what a broadband de-esser ruins. 0 "
                   "takes it out of the path.";
        if (id == "fx_chorus")
            return "CHORUS - two taps around 12 ms, a quarter cycle apart across "
                   "the sides, at 0.35 Hz. Depth and wet mix are both this knob. "
                   "0 takes it out of the path.";
        if (id == "fx_flanger")
            return "FLANGER - the chorus at a tenth of the delay with feedback, "
                   "which is what turns a comb into the jet. 0 takes it out of "
                   "the path.";
        if (id == "fx_phaser")
            return "PHASER - six first-order allpass sections per side swept "
                   "across the presence band, with feedback around the chain. 0 "
                   "takes it out of the path.";
        if (id == "fx_tremolo")
            return "TREM - amplitude modulation at 4.5 Hz; the knob is the "
                   "depth, so full travel swings the gain from unity to silence. "
                   "0 takes it out of the path.";
        if (id == "fx_vibrato")
            return "VIBRATO - delay modulation at 5 Hz with no dry leg. The knob "
                   "blends the dry signal back in at the bottom of its travel, so "
                   "switching the stage on is a shallow vibrato rather than a "
                   "delay jump. 0 takes it out of the path.";
        if (id == "fx_rotary")
            return "ROTARY - a rotary speaker: Doppler from a moving horn and its "
                   "directivity on the same oscillator, the sides a quarter cycle "
                   "apart as they are across a real cabinet. The knob sets the "
                   "rotation from 0.8 Hz, slow rotor, to 3.3 Hz, fast, and the "
                   "depth with it. 0 takes it out of the path.";
        if (id == "fx_vocoder")
            return "VOCODER - eight bands of the input's own spectrum imposed on "
                   "a synthesised 110 Hz carrier, because an insert has no second "
                   "input to take a carrier from. The knob replaces the direct "
                   "signal with the vocoded one. 0 takes it out of the path.";
        if (id == "fx_granular")
            return "GRAIN - four overlapping grains of a shared history, each "
                   "windowed so it fades rather than clicks. The knob sets the "
                   "grain rate: a sparse cloud at the bottom, a continuous "
                   "texture at the top. 0 takes it out of the path.";
        if (id == "fx_spectral")
            return "SPECTRL - a spectral gate: six log-spaced bands, each removed "
                   "when it sits under the programme's own reference. The knob "
                   "deepens the gate and lengthens its release, which is the blur. "
                   "0 takes it out of the path.";
        if (id == "fx_speed")
            return "SPEED - the tape's own three-position switch, spread across a "
                   "continuous knob: below a quarter is half speed, the middle is "
                   "1x and above three quarters is double. Speed and pitch move "
                   "together, as they do on a deck, and 1x is a bit-exact "
                   "pass-through. Default 50 percent - 1x.";
        if (id == "comp_upward")
            return "UPWARD - the multiband compressor's upward stage. Where the "
                   "downward stages turn the loud end down, this lifts what sits "
                   "below -30 dBFS toward it, up to 18 dB. It is not a makeup "
                   "gain: it changes the dynamic range at the BOTTOM of the "
                   "programme. Default 0 percent.";

        return {};
    }
}
