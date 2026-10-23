// =============================================================================
//  The parameter table: the 23 groups createParameterLayout() is built from, and
//  the four range constants they are built with.
//
//  Moved out of PluginProcessor.cpp, prose and all, by matching its exact text. The
//  table is 1,300 lines of what is essentially DATA - one registration per
//  parameter, each with the note that says what it does - and it was sitting in the
//  middle of the signal path, which is the file a reader opens to find out what the
//  machine does to the audio.
//
//  Nothing about the table changed, and that is the whole safety argument: a saved
//  session, a factory preset and a host's automation all address parameters by name,
//  and the order the groups are called in IS the table's order. The groups keep
//  their bodies, their order and their call order.
//
//  Two things had to travel with it, both of them things only it used:
//
//    * createParameterLayout() itself. It is declared in PluginProcessor.h (the
//      constructor's initialiser list calls it), so it needs no header of its own -
//      just this file, where the groups it calls are.
//    * minTrack, maxTrack, minInputDb and maxInputDb, the four range constants. They
//      were in the processor's file-local namespace and read by nothing after the
//      table left, so they left with it - which is what keeps this a move rather
//      than a copy: a constant in two files is two constants.
//
//  tests/state/extract.py cuts the groups, the constants and createParameterLayout()
//  out of THIS file and compiles them, and counts the parameters they build (109) -
//  so a group that stops being extracted is a failing check rather than a table that
//  quietly holds fewer parameters than the plugin has.
// =============================================================================
#include "PluginProcessor.h"

namespace
{
    constexpr float minTrack = 0.0f;
    constexpr float maxTrack = 1.0f;

    // Symmetric decibel range shared by the input and output stage controls.
    constexpr float minInputDb = -32.0f;
    constexpr float maxInputDb = 32.0f;
}

//==============================================================================
//  The parameter table, built in groups.
//
//  createParameterLayout() used to be one 1,100-line function: 109 parameters,
//  the full comment for each, and nothing between them to say where the deck ended
//  and the vinyl stage began. It is now a short function that calls one function
//  per group, so a group can be read on its own and a new parameter has an obvious
//  place to go.
//
//  The split is a move, not a rewrite: every line of every group is the text that
//  was already here, at the same indentation, and the groups are called in the
//  order the parameters were declared in - so the parameter list, and with it every
//  saved session and preset, is unchanged.
//
//  tests/state/extract.py cuts these functions out by name and compiles them
//  verbatim against a stub of the toolkit, which is how the saved-state round trip
//  is checked without JUCE. Renaming one, or adding a group without registering it
//  there, fails that harness loudly rather than quietly leaving a parameter
//  unmeasured.
//==============================================================================
namespace
{
    using ParamLayout = juce::AudioProcessorValueTreeState::ParameterLayout;

    // Knob taper only. This skew shapes how knob travel maps onto the parameter value;
    // it has nothing to do with the sound. The analogue nonlinearity lives in the DSP
    // itself (the power curves and the stacked tanh stages in processBlock), which are
    // deliberately NOT linearised - see the note there.
    //
    // The skew is centred low so the gentle end of each control gets more travel, which
    // is where an analogue control is actually judged, while still reaching its maximum.
    juce::NormalisableRange<float> percentageRange (float centre)
    {
        juce::NormalisableRange<float> range (minTrack, maxTrack, 0.001f);
        range.setSkewForCentre (centre);
        return range;
    }

    /** The panel-wide monitors: the two trims, bypass, DELTA listen, polarity and
        the AUTO GAIN switch. Everything here acts on the finished signal rather than
        on one stage of the machine.
    */
    void addMonitorControls (ParamLayout& layout)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "input", 1 }, "Input",
                                                                juce::NormalisableRange<float> (minInputDb, maxInputDb, 0.1f),
                                                                0.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("dB")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "output", 1 }, "Output",
                                                                juce::NormalisableRange<float> (minInputDb, maxInputDb, 0.1f),
                                                                0.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("dB")));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "bypass", 1 },
                                                                "Bypass", false));
        // DELTA listen: when on, the output becomes wet minus the machine's own dry
        // signal - only what the machine itself adds (harmonics, glue, transport
        // wander) is heard. The reference is the input after the INPUT trim and the
        // input glue compressor, scaled by the same stage gain the wet leg gets, so
        // every gain in the machine cancels in the subtraction: MIX 0 monitors as
        // silence, which is its own sanity check. Both legs are formed on the SAME
        // frame further up in this very loop, so the difference is phase-perfect at
        // every oversampling factor with no compensation delay of its own. The
        // mode is ramped, so pressing DELTA is a fade between two monitor positions
        // rather than a step, and a bypassed machine fades the difference to silence
        // over the BYPASS ramp instead of snapping back to the dry signal.
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "delta", 1 },
                                                                "Delta Listen", false));
        // POLARITY INVERT: a mastering staple. A full polarity flip on the output, so a
        // 180-degree mis-wiring between two sources can be corrected without re-patching.
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "polarity", 1 },
                                                                "Polarity Invert", false));
        // AUTO GAIN: when on, the slow programme compensator (see the final gain
        // compensation section) is allowed to act; when off the output level is exactly
        // what the chain produced. Default ON, matching what earlier builds always did.
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "auto_gain", 1 },
                                                                "Auto Gain", true));
    }

    /** The two equalisers, IN and OUT, each with its three bands and the
        high-/low-pass filter pair that follows them.
    */
    void addEqualiserControls (ParamLayout& layout)
    {
        // =========================================================================
        //  THE TWO EQUALISERS.
        //
        //  One at each end of the machine, and the positions are the point:
        //
        //    IN EQ   sits immediately after the input trim and BEFORE everything
        //            else. What it shapes is what the tape HEARS, so lifting the low
        //            end here is not the same thing as lifting it at the output: the
        //            tape's own saturation, its glue compressors and its hysteresis
        //            all respond to what arrives, so an input EQ changes the
        //            CHARACTER of the processing rather than merely its balance.
        //            This is the EQ you use to feed the machine what it wants.
        //
        //    OUT EQ  sits after the machine and before the output trim. It shapes
        //            what leaves, so it corrects the RESULT rather than the input -
        //            the EQ you use to place the finished sound.
        //
        //  Both are the same three bands (low shelf at 200 Hz, bell at 1 kHz in a
        //  200 Hz - 4 kHz band, high shelf above 4 kHz) and both are bit-for-bit
        //  transparent at 0 dB on all three, so a fresh instance is untouched and
        //  the two controls cannot colour the signal by merely existing.
        //
        //  The band gains are in dB, -12 .. +12, because that is the unit an EQ is
        //  read in. The taper is deliberately linear rather than skewed: an EQ's
        //  travel should be a straight line between cut and boost.
        // =========================================================================
        const auto eqBandRange = juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f);
        const auto eqBandAttributes = juce::AudioParameterFloatAttributes().withLabel ("dB");

        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "in_low", 1 },
                                                                "In EQ Low", eqBandRange, 0.0f, eqBandAttributes));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "in_mid", 1 },
                                                                "In EQ Mid", eqBandRange, 0.0f, eqBandAttributes));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "in_high", 1 },
                                                                "In EQ High", eqBandRange, 0.0f, eqBandAttributes));

        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "out_low", 1 },
                                                                "Out EQ Low", eqBandRange, 0.0f, eqBandAttributes));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "out_mid", 1 },
                                                                "Out EQ Mid", eqBandRange, 0.0f, eqBandAttributes));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "out_high", 1 },
                                                                "Out EQ High", eqBandRange, 0.0f, eqBandAttributes));

        // -------------------------------------------------------------------------
        //  The EQ's two FILTERS, one pair per equaliser.
        //
        //  Separate from the three bands above because they are a different kind of
        //  control. A shelf shapes a band and leaves everything else at unity; a
        //  FILTER removes everything outside its passband. That is what you reach
        //  for to take the rumble off a turntable, the hum off a bad earth loop, or
        //  the hiss off before printing - none of which a shelf can do, because a
        //  shelf can never reach zero.
        //
        //  ORDER is in dB per octave, which is the unit a filter is specified in: 6
        //  is one pole, 12 is two, up to 48. The engine implements each pole as the
        //  one-pole section the rest of the plugin already uses, so the number on
        //  the panel is the slope you get.
        //
        //  FREQUENCY's floors and ceilings are chosen so that both ends of each
        //  control's travel are REAL bypasses: HP at 20 Hz and LP at 20 kHz are not
        //  filters, and the engine treats them as such, so a neutral EQ is bit-for-
        //  bit transparent rather than "transparent to within a gentle filter".
        //
        //  Q is shared between the two filters of one EQ, because a per-filter Q
        //  would be two more knobs for a parameter that matters far less than the
        //  corner and the slope. It is capped at 1.5 - a resonant filter ringing on
        //  a tape emulation is a fault, not a feature.
        // -------------------------------------------------------------------------
        auto hpFreqRange = juce::NormalisableRange<float> (20.0f, 500.0f, 1.0f);
        hpFreqRange.setSkewForCentre (100.0f);
        auto lpFreqRange = juce::NormalisableRange<float> (2000.0f, 20000.0f, 10.0f);
        lpFreqRange.setSkewForCentre (8000.0f);
        // The two ORDER parameters are choices, so they carry their own dB/octave
        // list rather than a numeric range - there is no eqOrderRange here, and the
        // one that used to sit here was never read.
        const auto eqQRange = juce::NormalisableRange<float> (0.5f, 1.5f, 0.01f);

        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "in_hp_freq", 1 },
                                                                "In EQ HP Freq", hpFreqRange, 20.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("Hz")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "in_lp_freq", 1 },
                                                                "In EQ LP Freq", lpFreqRange, 20000.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("Hz")));

        // The high-pass / low-pass switches. The corner knobs already bypass
        // themselves at their travel's ends, but an explicit switch is the control a
        // user means by "turn the EQ's HP off": the corner stays where it was and the
        // filter is removed outright. Default ON, and the default corners ARE
        // bypasses (20 Hz / 20 kHz), so the default sound does not move.
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "in_hp_on", 1 },
                                                                "In EQ HP On", true));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "in_lp_on", 1 },
                                                                "In EQ LP On", true));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "in_eq_order", 1 },
                                                                "In EQ Order",
                                                                juce::StringArray { "6 dB/oct", "12 dB/oct", "18 dB/oct",
                                                                                     "24 dB/oct", "36 dB/oct", "48 dB/oct" },
                                                                0));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "in_eq_q", 1 },
                                                                "In EQ Q", eqQRange, 0.7f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("Q")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "out_hp_freq", 1 },
                                                                "Out EQ HP Freq", hpFreqRange, 20.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("Hz")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "out_lp_freq", 1 },
                                                                "Out EQ LP Freq", lpFreqRange, 20000.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("Hz")));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "out_hp_on", 1 },
                                                                "Out EQ HP On", true));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "out_lp_on", 1 },
                                                                "Out EQ LP On", true));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "out_eq_order", 1 },
                                                                "Out EQ Order",
                                                                juce::StringArray { "6 dB/oct", "12 dB/oct", "18 dB/oct",
                                                                                     "24 dB/oct", "36 dB/oct", "48 dB/oct" },
                                                                0));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "out_eq_q", 1 },
                                                                "Out EQ Q", eqQRange, 0.7f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("Q")));
    }

    /** The subharmonic generator, the stereo width control and the anti-phase
        guard's switch.
    */
    void addSubFundAndWidthControls (ParamLayout& layout)
    {
        // SUBFUND: the subharmonic generator. Every other stage here makes overtones - a
        // 100 Hz note gains 200, 300, 400 Hz. This one produces the subharmonic series:
        // an 8-stage downward harmonic cascade (1/2, 1/3, 1/4, 1/5, 1/6, 1/7, 1/8, 1/9)
        // with analogue saturation in the other direction. When the fundamental frequency
        // permits, up to 8 subharmonics are synthesized; stages falling below the audible
        // threshold (< 14-22 Hz) are smoothly attenuated to prevent subsonic DC rumble.
        // See SubharmonicGenerator.
        //
        // Defaults to OFF: it is a colour, not a correction, and a plugin should not add
        // subharmonic weight to every session that has not asked for it.
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "subfund", 1 },
                                                                "Sub-Fundamental",
                                                                juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
                                                                0.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
        // WIDTH at 50 percent is natural stereo width. It scales SIDE from 0x
        // (dual mono) through 1x (natural) to 2x (extra-wide), preserving MID.
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "stereo_width", 1 },
                                                                "Stereo Width",
                                                                juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
                                                                0.5f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));

        // -------------------------------------------------------------------------
        //  The anti-phase guard's switch.
        //
        //  The guard catches the one fault that is INAUDIBLE where it is created and
        //  catastrophic where it lands: two sides in opposite polarity sound like a
        //  wide stereo image until something sums them, and then the centre and the
        //  low end disappear. It is on by default and should stay on - but "should"
        //  is not "cannot be turned off": a user with a genuinely phase-inverted
        //  source (a mis-wired cable they intend to keep, a creative mid/side rig, a
        //  deliberate out-of-phase effect chain) needs the plugin to pass it through
        //  untouched rather than quietly narrow the image over a second.
        //
        //  A parameter rather than a member of the editor for the same reason the
        //  language is one: it is saved with the session, restored with it and
        //  undone with it, and there is no second settings store to keep in step
        //  with the host's idea of the project. It is deliberately NOT presettable -
        //  it is a protection, not a sound, and a factory preset must not be able to
        //  switch a safety net off behind the user's back (see
        //  intentionallyNotPresettable in the constructor).
        // -------------------------------------------------------------------------
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "anti_phase_guard", 1 },
                                                                "Anti-Phase Guard", true));
    }

    /** The deck itself: the stock and the speed, the instrument voicing, the drive
        and bias of the record head, the oversampling switch, the brightness macro,
        the transport modulation, MIX, the track model and the speed/eq macro.
    */
    void addDeckControls (ParamLayout& layout)
    {
        // Every parameter carries a versioned ParameterID. The plain-String constructor
        // the controls below used before is deprecated in JUCE 9 and, more importantly,
        // it leaves the parameter unversioned, so a host has no way to tell a future
        // meaning change from the current one. The id strings are unchanged, so saved
        // sessions and presets resolve exactly as before.
        // The stocks come from tapeStockNames in PluginProcessor.h, which the panel's
        // combo box reads too. Spelling them out here separately is how the two drifted
        // apart once already - see the note on that list for what that looked like.
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "tape_type", 1 }, "Tape Type",
                                                                tapeStockNameList(),
                                                                0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "speed", 1 }, "Speed",
                                                                juce::StringArray { "7.5 ips", "15 ips", "30 ips" },
                                                                1));

        // INSTRUMENT re-voices the machine for the source in front of it, the way an
        // engineer would bias and level a real deck differently for a vocal, a bass or a
        // piano: how hard the record head is pushed, how thick the magnetic memory runs,
        // how much top end survives, how loud the floor sits and how steady the transport
        // runs (bass pitch wobble is audible immediately, guitar wobble is character).
        // MASTER BUS is the neutral calibration the presets and the panel assume; DRUMS
        // is the slam calibration - a harder bend, an open head and tight magnetic
        // memory so transients keep their crack.
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "instrument", 1 }, "Instrument",
                                                                juce::StringArray { "Master Bus", "Vocal", "Bass",
                                                                                     "Guitar", "Piano", "Drums" },
                                                                0));

        // DRIVE defaults to the studio-default 30 percent: an audible but polite
        // thickening that leaves a mastered mix believable. The old 42 read "hot out
        // of the box" and made every fresh instance fight the mix it was dropped on.
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "drive", 1 }, "Drive", percentageRange (0.45f), 0.30f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
        // BIAS defaults to 42 percent, close to the flattest, most transparent part
        // of the transfer curve: a fresh instance is audibly neutral until the user
        // asks for the edge (low) or the warmth (high). The old 36 sat on the edgy
        // slope, so "doing nothing" was never actually nothing.
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "bias", 1 }, "Bias", percentageRange (0.40f), 0.42f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
        // OVERSAMPLING: a host-visible quality switch. OFF keeps the latency at zero;
        // 2x/4x run the tape engine at a higher internal rate so the magnetic shaper
        // aliases far less, and the added filter delay is reported to the host.
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "oversampling", 1 },
                                                                "Oversampling",
                                                                juce::StringArray { "Off", "2x", "4x", "8x" },
                                                                0));

        // The parameter ID stays "tone" so existing saved sessions still resolve it; only the
        // name shown in the host and on the panel is BRIGHTNESS. Artists reach for brightness
        // first, and "tone" is vague enough that it reads as a different thing (tilt, midrange,
        // character) depending on who is looking at it.
        // BRIGHTNESS defaults to its neutral pivot. With the tilt design the middle
        // of the travel now leaves the spectral balance untouched, and 58 would print
        // a +4 dB smile on every fresh instance before the user touched anything.
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "tone", 1 }, "Brightness", percentageRange (0.50f), 0.50f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "wow", 1 }, "Wow", percentageRange (0.35f), 0.14f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "flutter", 1 }, "Flutter", percentageRange (0.35f), 0.18f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));

        // MIX is a true crossfade from 0 % (pure dry) to 100 % (pure wet), default 50 %.
        //
        // The range is 0..100 so the stored number is the percentage the panel shows.
        // Two consequences are handled elsewhere and one is not handled here at all:
        //
        //   - Saved states and presets are migrated on load, by migrateStateFormat().
        //   - Factory presets store raw values, which is what the tree expects.
        //   - AUTOMATION LANES ALREADY WRITTEN IN A SAVED PROJECT cannot be migrated
        //     from inside the plugin. The host owns those numbers and hands them over
        //     already scaled, so an old lane spanning 0..1 now sweeps 0%..1% and the
        //     effect all but vanishes. The ParameterID version below is the only
        //     signal a host gets that this parameter's meaning changed, and it is why
        //     it is spelled { "mix", 1 } rather than left as a bare id: hosts that
        //     support parameter mapping use it to offer a conversion. They are not
        //     obliged to, so opening an old project may need MIX re-recorded or the
        //     lane scaled by hand. That is a deliberate, documented limitation -
        //     there is no portable way for a plugin to rescale its own automation.
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "mix", 1 }, "Mix",
                                                                juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
                                                                50.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));

        // -------------------------------------------------------------------------
        //  MODELED TRACKS - the track layout of the machine.
        //
        //  A tape machine is not one wide track: it is a number of narrow parallel
        //  tracks on the same tape, recorded by a head with that many gaps and read
        //  by the same. Which layout the machine has changes the sound in ways that
        //  are not a trim, because they are geometric:
        //
        //    2      a stereo deck: two tracks, one per channel, each with the full
        //           width of its half of the tape. The most low end per channel and
        //           the least crosstalk - there is nothing adjacent to leak from.
        //
        //    2+3    a FOUR-track deck used as two, on tracks 2 and 3. There is a
        //           whole track's worth of tape between the two channels, so the
        //           spacing is the widest of the three and the channels are the most
        //           separated - but the two unused tracks (1 and 4) still carry the
        //           guard band and its own fringing, which is why this is not the
        //           same thing as "2 with more separation".
        //
        //    3      a three-track deck: three narrow tracks, so each one is NARROWER
        //           than either layout above. A narrower track has less low end and
        //           a noticeably higher noise floor for the same tape, and the two
        //           adjacent tracks are close enough that the head's fringing field
        //           reaches them - so the crosstalk is the highest of the three.
        //
        //  The model applies three things, all of them consequences of the geometry:
        //
        //    - the track WIDTH, which scales the low end and the noise floor
        //    - the CROSSTALK between the two channels, from the head's fringing
        //    - the SPACING, which is what the crosstalk's own delay/phase depends on
        //
        //  Default 2, which is the two-track stereo deck every earlier build assumed,
        //  so an existing session loads the machine it was saved with.
        // -------------------------------------------------------------------------
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "tracks", 1 },
                                                                "Modeled Tracks",
                                                                juce::StringArray { "2", "2+3", "3" },
                                                                0));

        // TONE is the added macro: a crossfade BETWEEN TAPE SETTINGS rather than between
        // dry and wet. At 0 % the transport behaves like the classic slow machine - soft
        // head damping, gentle roll-off, warmer wow. At 100 % it behaves like the fast
        // machine - open top end, wider head-gap pole, tighter flutter. Everything the
        // SPEED switch and the head electronics set is blended between those two states,
        // which is exactly how the machine's own speed/eq macro behaves on the hardware.
        // The ID is "character" because "tone" is already taken by Brightness above.
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "character", 1 }, "Tone", percentageRange (0.50f), 0.50f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
    }

    /** The second playback head: its spacing, how much of it is returned and where
        the feedback goes.
    */
    void addTapeDelayControls (ParamLayout& layout)
    {
        // -------------------------------------------------------------------------
        //  Tape delay.
        //
        //  A second playback head spaced away from the record head, which is what a
        //  spare head on a real deck IS: the tape takes time to travel between them,
        //  so the same signal comes back a fixed interval later. The interval is set
        //  by the gap and the speed, which is why the control is in milliseconds.
        //
        //  The range is short on purpose. This is not a dub delay: at 15 ips a real
        //  head spacing gives tens of milliseconds, and the point is the slap and the
        //  comb colour a second head adds to a tape sound, not an echo unit. Default
        //  0 - a fresh instance has no second head engaged.
        // -------------------------------------------------------------------------
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "delay_time", 1 }, "Delay",
                                                                juce::NormalisableRange<float> (0.0f, 250.0f, 0.1f),
                                                                0.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("ms")));
        // How much of the delayed signal is returned. 0 leaves the delay inaudible
        // even with a time set, so the two controls cannot fight: TIME says where the
        // head is, LEVEL says how loud its output is.
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "delay_feedback", 1 }, "Delay Level",
                                                                percentageRange (0.40f), 0.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));

        // -------------------------------------------------------------------------
        //  PING-PONG - where the second head's feedback goes.
        //
        //  At 0 the repeat is written back into its OWN channel, so the echoes stay
        //  where they started: a normal tape slap. At 100 the repeat is written into
        //  the OTHER channel, so each pass arrives on the opposite side and the
        //  echoes alternate left, right, left - the classic ping-pong.
        //
        //  In between it is a genuine crossfade rather than a switch: the feedback is
        //  split between the two lines in proportion to the control, so the echoes
        //  MOVE across the image as the knob turns instead of jumping. The two
        //  partial writes always sum to the same amount, which is why the total
        //  energy - and therefore the decay of the repeats - does not change as the
        //  control sweeps. Only their position does.
        //
        //  It is not a second delay unit. It is the SAME head, read on the other side
        //  of the machine, which is what ping-pong physically is when two heads are
        //  wired across a stereo pair.
        //
        //  Default 0 - every earlier build behaved this way, so an existing session
        //  loads the delay it was saved with.
        // -------------------------------------------------------------------------
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "delay_pingpong", 1 }, "Ping-Pong",
                                                                percentageRange (0.50f), 0.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
    }

    /** The inter-channel tape offset and the machine's noise floor.
    */
    void addOffsetAndNoiseControls (ParamLayout& layout)
    {
        // -------------------------------------------------------------------------
        //  Stereo tape offset (ST OFFSET).
        //
        //  On a real stereo deck the two tracks are recorded by separate head gaps a
        //  fraction of a millimetre apart, and the tape skews slightly across them.
        //  The result is that the two channels are not perfectly time-aligned: one
        //  lags the other by a few tens of microseconds. It is a small effect and it
        //  is a large part of why a tape bounce sounds wide rather than merely
        //  equalised.
        //
        //  The control sets that inter-channel delay directly in microseconds,
        //  positive meaning the right channel lags. It is kept well under a
        //  millisecond so it reads as width and never as an echo or a phase fault.
        // -------------------------------------------------------------------------
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "st_offset", 1 }, "ST Offset",
                                                                juce::NormalisableRange<float> (-500.0f, 500.0f, 1.0f),
                                                                0.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("us")));

        // -------------------------------------------------------------------------
        //  Noise floor level.
        //
        //  TAPE TYPE sets the machine's own hiss floor as part of its character and
        //  that is untouched. This is a trim ON TOP of it, so the floor can be lifted
        //  for effect (a deliberately dirty bounce) or pulled to a clinical black
        //  without changing which stock is loaded. 50 percent is exactly the
        //  formula's own floor - the neutral position, not a change.
        // -------------------------------------------------------------------------
        // NOISE is the MIX of every noise source the machine makes: how much of the
        // noise section sits in the final output, exactly as MIX is how much of the
        // tape section does. NOISE LVL is the level of the sources themselves - it
        // trims the tape floor and the vinyl noise together, because they are one
        // noise department rather than two.
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "noise", 1 }, "Noise Mix",
                                                                percentageRange (0.50f), 0.50f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "noise_lvl", 1 }, "Noise Level",
                                                                percentageRange (0.50f), 1.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
    }

    /** The transport itself - STOP / PLAY / START and the momentary spindown -
        plus the panel's own interface clicks.
    */
    void addTransportControls (ParamLayout& layout)
    {
        //  Transport state: STOP / PLAY / START.
        //
        //  Three states rather than a play/stop pair, because a tape deck has three:
        //
        //    STOP  - the capstan is braked to rest and stays there. A machine that
        //            is not moving passes nothing, so the plugin is SILENT: no tape,
        //            no dry input, no hiss, no delay tail. Pressing STOP again spins
        //            the machine back up.
        //    PLAY  - normal running, which is where START settles.
        //    START - the engagement gesture: the capstan comes up to speed over
        //            about 1.1 s and the state advances to PLAY by itself. It is a
        //            SPIN-UP and nothing else: pressed on a machine that is already
        //            running there is nothing left to drive, so the state settles
        //            straight into PLAY - no brake first, which would make the key
        //            a re-cue rather than a start. The durations live in
        //            TransportRig, with the reason they are durations rather than
        //            one-pole time constants, and with the TAPE SPEED / VINYL SPEED
        //            / tempo scales that move them with the deck's own settings.
        //            Pressed while the HOST's transport is parked it ARMS the key
        //            instead of running it - the machine waits at rest until the
        //            DAW rolls - because a take belongs to the DAW and a spin-up
        //            spent on a pause is a spin-up wasted. The state stays START
        //            while it waits, which is what keeps the key lit; the engine
        //            publishes the arm beside it so the panel can say so (see
        //            armedForHost in TransportRig).
        //
        //  What the transport moves is the machine's LEVEL, its modulation and its
        //  speed response (the same character mapping the SPEED control uses), all
        //  following one number - the platter's speed - and the programme's PITCH
        //  moves with it, because the repitch stage reads the finished frame back
        //  at that same number: a coast walks the material down through the
        //  octaves and a spin-up walks it back. STOP is still a coast to silence,
        //  since the gate is what stops the sound, but it is a DJ-style bend on
        //  the way there rather than the fade it used to be.
        // -------------------------------------------------------------------------
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "transport", 1 }, "Transport",
                                                                juce::StringArray { "Stop", "Play", "Start" },
                                                                1));

        // -------------------------------------------------------------------------
        //  SPINDOWN - the momentary hold, exposed as a host-visible parameter.
        //
        //  A bool rather than a fourth entry in the transport choice, because it
        //  ACTS on the transport rather than replacing it: held, the platter runs
        //  down under the current transport, and released, it spins back up and the
        //  transport settles into Play. As a parameter it can be automated (a
        //  spindown at the end of a section is a real production move /
        //  and it survives in the preset state, which is what makes the editor's
        //  momentary button and a saved automation lane the same thing.
        //
        //  The editor's button writes it through the attached ButtonAttachment, so
        //  the button and the host cannot disagree about whether it is held.
        // -------------------------------------------------------------------------
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "spindown", 1 },
                                                                "Spindown", false));

        // -------------------------------------------------------------------------
        //  UI SOUNDS - the panel's own interface clicks.
        //
        //  A host-visible parameter rather than a private editor flag, so the choice
        //  survives in the session and in a preset, and so it is automatable like
        //  every other setting. The editor reads it and enables its sound engine.
        //
        //  Default OFF. A plugin that starts ticking the moment a window opens is a
        //  plugin that gets uninstalled, and this is a studio tool - the sounds are
        //  there for the people who want a hardware feel, not imposed on the people
        //  who do not.
        //
        //  It lives on the SETTINGS tab beside GL and OVERSAMPLING, because it is
        //  the same KIND of switch: an engine-level preference rather than a control
        //  that shapes the sound.
        // -------------------------------------------------------------------------
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "ui_sounds", 1 },
                                                                "UI Sounds", false));
    }

    /** The musical reference and the optional pitch correction that quantises
        towards it.
    */
    void addTuningControls (ParamLayout& layout)
    {
        // Musical reference and optional pitch correction. The correction is deliberately
        // opt-in and uses the selected key as a quantisation grid; at zero correction the
        // detector remains idle and the signal is bit-for-bit unchanged by this stage.
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "tuning_tonic", 1 },
                                                                  "Tonic",
                                                                  juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" },
                                                                  0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "tuning_mode", 1 },
                                                                  "Mode",
                                                                  juce::StringArray { "Chromatic", "Major", "Minor", "Dorian", "Pentatonic Major", "Pentatonic Minor" },
                                                                  0));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "autotune", 1 },
                                                                "Auto Tune", false));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "autotune_amount", 1 },
                                                                  "Auto Tune Amount",
                                                                  juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
                                                                  0.0f));
    }

    /** The conventional compressor, which is independent of both glue stages.
    */
    void addCompressorControls (ParamLayout& layout)
    {
        // Conventional compressor, independent from both glue stages.
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "comp_threshold", 1 }, "Compressor Threshold",
                                                                  juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -18.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "comp_ratio", 1 }, "Compressor Ratio",
                                                                  juce::NormalisableRange<float> (1.0f, 20.0f, 0.1f), 2.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "comp_attack", 1 }, "Compressor Attack",
                                                                  juce::NormalisableRange<float> (0.1f, 100.0f, 0.1f), 10.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "comp_release", 1 }, "Compressor Release",
                                                                  juce::NormalisableRange<float> (10.0f, 1000.0f, 1.0f), 120.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "comp_makeup", 1 }, "Compressor Makeup",
                                                                  juce::NormalisableRange<float> (0.0f, 24.0f, 0.1f), 0.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "comp_mix", 1 }, "Compressor Mix",
                                                                  percentageRange (0.50f), 0.0f,
                                                                  juce::AudioParameterFloatAttributes().withLabel ("%")));
    }

    /** The interface language.
    */
    void addRenderSettingsControl (ParamLayout& layout)
    {
        // -------------------------------------------------------------------------
        //  Renderer backend selector.
        //
        //  A host-visible choice, so the user's pick survives in the session and in
        //  a preset and is exactly what a host automation lane addresses. This is the
        //  counterpart to the deck's oversampler switch: the engine-level preference
        //  that the panel exposes in the SETTINGS tab beside GL and oversampling.
        //
        //  It is a ComboBox bound to the "renderer" parameter, so the same name the
        //  panel's attachment reads is the name the parameter table registers - two
        //  different files, one id, no second store to keep in step.
        //
        //  The list mirrors the panel's combo, because the panel is built from the
        //  same enum the table names here. When either side changes the other must
        //  change with it, which is why the two live in the same review rather than
        //  one of them drifting out of date.
        // -------------------------------------------------------------------------
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "renderer", 1 },
                                                                  "Renderer",
                                                                  juce::StringArray {
                                                                      "Auto",
                                                                      "OpenGL",
                                                                      "Vulkan",
                                                                      "Metal",
                                                                      "DirectX 11",
                                                                      "DirectX 12",
                                                                      "CPU",
                                                                      "Sokol_gfx"
                                                                  },
                                                                  0));
    }

    void addLanguageControl (ParamLayout& layout)
    {
        // -------------------------------------------------------------------------
        //  Language.
        //
        //  A parameter rather than a member of the editor for one reason that
        //  matters: persistence. A language held in the editor is lost the moment the
        //  user closes the plugin, and a plugin that forgets how to speak Ukrainian
        //  every time it is reopened is not a translation. As a parameter it is saved
        //  with the session, restored with it, and carried by the host's own state
        //  handling - no ApplicationProperties, no separate settings file, and
        //  nothing to keep in step with the host's idea of the project.
        //
        //  It is deliberately NOT part of a preset: a factory preset is a sound, and
        //  the person who loads it may not read the language it was written in. It is
        //  listed as intentionally not presettable for the same reason ui_sounds is.
        // -------------------------------------------------------------------------
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "language", 1 },
                                                                  "Language",
                                                                  juce::StringArray { "English",
                                                                      juce::CharPointer_UTF8 ("\xd0\xa3\xd0\xba\xd1\x80\xd0\xb0\xd1\x97\xd0\xbd\xd1\x83\xd0\xb0\xd0\xb9\xd1\x81\xd1\x8c\xd0\xba\xd0\xb0") },
                                                                  0));
    }

    /** The seven SOURCE knobs: each saturation principle's share of the machine.
    */
    void addSaturationMixControls (ParamLayout& layout)
    {
        // -------------------------------------------------------------------------
        //  Saturation blend.
        //
        //  The plugin's shaper has always been ONE curve - magnetic hysteresis. These
        //  two controls turn it into a blend of six mechanisms: the five a real
        //  analogue chain runs through - tape, valve, cassette, amp, transformer - and
        //  the converter that stands in for all of them when the programme is going
        //  through a box instead of a machine. See SaturationCore for what each one is
        //  and why they are genuinely different shapes.
        //
        //  The seven SOURCE knobs. Each is its own principle's share of the
        //  saturation mix - TAPE, VALVE, CASSETTE, AMP, TRANSFORMER, DIGITAL - so a
        //  user dials the machines they want BY NAME instead of steering one sweep
        //  and one spread (the old BLEND/SHAPE pair, removed at the user's
        //  request). Shares are normalised to sum to 1, so the mix stays a true
        //  crossfade and level-stable; a machine whose own TYPE list is OFF has its
        //  share redistributed among the rest. Tape default 100, the rest 0 - the
        //  machine the plugin was calibrated as, exactly what every earlier build
        //  and every preset saved before this change meant.
        // -------------------------------------------------------------------------
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "tape_source", 1 }, "Tape",
                                                                percentageRange (0.50f), 1.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "vinyl_source", 1 }, "Vinyl",
                                                                percentageRange (0.50f), 0.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "cassette_source", 1 }, "Cassette",
                                                                percentageRange (0.50f), 0.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "digital_source", 1 }, "Digital",
                                                                percentageRange (0.50f), 0.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "amp_source", 1 }, "Amp",
                                                                percentageRange (0.50f), 0.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "valve_source", 1 }, "Valve",
                                                                percentageRange (0.50f), 0.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "transformer_source", 1 }, "Transformer",
                                                                percentageRange (0.50f), 0.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
    }

    /** The guitar-amplifier features - SAG, PRESENCE, CABINET and AMP BIAS.
    */
    void addAmpVoicingControls (ParamLayout& layout)
    {
        // -------------------------------------------------------------------------
        //  Guitar-amplifier features. See AmpVoicing for what each one is.
        //
        //  They are OFF by default: the plugin is calibrated as a tape machine, and an
        //  amp's cabinet and sag on a mastering bus would be a surprise rather than a
        //  feature. They are there for the sources that want them.
        // -------------------------------------------------------------------------
        // SAG: how much the supply droops under sustained demand. 0 is a stiff,
        // regulated supply (no give at all); 100 is a small amp being leaned on hard.
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "sag", 1 }, "Sag",
                                                                percentageRange (0.50f), 0.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
        // PRESENCE: the feedback network's top-end lift, applied after the clipping.
        // 50 percent is the flat, neutral position; above it sharpens, below it
        // darkens the way a lower presence setting does on a real amp.
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "presence", 1 }, "Presence",
                                                                percentageRange (0.50f), 0.50f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
        // CABINET: the speaker and its box. 0 is the raw amp output (a DI, essentially);
        // 100 is a closed 4x12. Default 0 so the tape machine is unchanged.
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "cabinet", 1 }, "Cabinet",
                                                                percentageRange (0.50f), 0.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
        // AMP BIAS: the input stage's DC operating point. Cold is tight and crossover-
        // distorted, hot is fat and compressed. 50 percent is the neutral centre, so
        // the control is a character sweep rather than a one-way effect.
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "amp_bias", 1 }, "Amp Bias",
                                                                percentageRange (0.50f), 0.50f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
    }

    /** The input stage in front of the machine.
    */
    void addPreampControl (ParamLayout& layout)
    {
        // =========================================================================
        //  PREAMP - the input stage in front of the machine.
        //
        //  A separate gain stage, not another drive: a real chain has a microphone
        //  preamp before the recorder, and its character is its own. It is placed
        //  BEFORE the tape so the machine hears the level the preamp delivers, which
        //  is exactly what the INPUT control already does - except this one has the
        //  preamp's own colour: a valve-ish soft clip and a low-cut from the input
        //  transformer, so driving it does not just add level, it adds a stage.
        //
        //  Default 0 - no preamp engaged, so the machine is unchanged.
        // =========================================================================
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "preamp", 1 }, "Preamp",
                                                                percentageRange (0.45f), 0.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
    }

    /** The DI box at the very front of the chain: how much of it is engaged, how
        hard it loads the source, its transformer and its pad.
    */
    void addDiBoxControls (ParamLayout& layout)
    {
        // =========================================================================
        //  THE DI BOX.
        //
        //  A DI box is not a preamp and not a gain stage: it is the box a guitar or
        //  a synth is plugged into BEFORE anything else. It takes an unbalanced,
        //  high-impedance, instrument-level signal and hands a balanced,
        //  low-impedance, mic-level one to the desk.
        //
        //  It is FIRST in this plugin's chain for exactly that reason - which also
        //  means it changes what everything after it hears, so the preamp, the
        //  saturation curve and the glue stages all respond to a loaded or padded
        //  signal differently. That is how the hardware behaves.
        //
        //  Four controls, and each is one of the four things a real DI actually does:
        //
        //    DI      how much of the box is engaged at all (0 = a straight wire)
        //    LOAD    how heavily it loads the source, which damps that source's own
        //            top-end resonance - the single largest reason two DI boxes sound
        //            different on the same guitar
        //    TRANS   its transformer's own colour: a small low-end bloom and a
        //            slight softness on top
        //    PAD     -0..-30 dB BEFORE the transformer, so a hot source can be plugged
        //            in without driving the box's core - a decision, not a level trim
        //
        //  There is deliberately no separate ground-lift switch: the ground loop is
        //  folded into the DI amount, because a DI with a bad earth hums and one with
        //  a lifted ground does not, and that is a property of the box rather than a
        //  separate control. The hum it can make is discussed with the stage itself.
        //
        //  All four default to OFF/neutral, so a fresh instance has no DI in the
        //  path and the machine is unchanged.
        // =========================================================================
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "di", 1 }, "DI",
                                                                percentageRange (0.45f), 0.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "di_load", 1 }, "DI Load",
                                                                percentageRange (0.45f), 0.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "di_transformer", 1 }, "DI XFMR",
                                                                percentageRange (0.45f), 0.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "di_pad", 1 }, "DI Pad",
                                                                juce::StringArray { "0 dB", "-10 dB", "-20 dB", "-30 dB" },
                                                                0));
    }

    /** FLUX, WEAR and MECHANICS: what the record head puts on the tape, the state
        of the tape and heads, and the state of the transport's moving parts.
    */
    void addMediaWearControls (ParamLayout& layout)
    {
        // =========================================================================
        //  FLUX - the magnetic flux the record head actually puts on the tape.
        //
        //  On a real deck the bias current and the record-head gap together decide how
        //  DEEP the magnetism goes into the oxide. More flux means the medium is
        //  driven further from its rest state, which raises the low-frequency output
        //  and lowers the noise floor, but also widens the hysteresis loop - so the
        //  same signal is remembered more strongly and comes back with more low end
        //  and a softer top. Less flux is thin, quiet and bright.
        //
        //  It is a different axis from DRIVE: DRIVE is how hard the signal is pushed
        //  into the curve, FLUX is how much of the medium's depth is used. On the
        //  hardware the two interact, and they do here too - FLUX scales the
        //  hysteresis memory and the low-frequency shelf, DRIVE scales the curve.
        //
        //  50 percent is the calibrated, neutral flux.
        // =========================================================================
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "flux", 1 }, "Flux",
                                                                percentageRange (0.50f), 0.50f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));

        // =========================================================================
        //  WEAR - how worn the tape and the heads are.
        //
        //  A used machine is not a broken one: the heads have a slightly rounded gap,
        //  the tape has lost some oxide at the edges, and the contact is less even.
        //  The audible result is a gentle loss of top end, a little extra modulation
        //  noise (the contact is no longer uniform), and a very slight compression of
        //  the high frequencies - not distortion, but DULLING.
        //
        //  It is deliberately a slow, subtle control: at 100 percent it should read as
        //  "an old machine" rather than "a fault". Default 0 - a fresh head and new
        //  tape.
        // =========================================================================
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "wear", 1 }, "Wear",
                                                                percentageRange (0.50f), 0.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));

        // =========================================================================
        //  MECHANICS - the state of the transport's moving parts.
        //
        //  WOW and FLUTTER set how much pitch modulation there is; MECHANICS decides
        //  how WELL the mechanism is holding it. At 0 the capstan, the pinch roller
        //  and the reel motors are all in good order, so the modulation is smooth and
        //  periodic. At 100 the bearings are dry, the belt is slack and the reel has a
        //  flat spot: the modulation becomes irregular, with a slow random drift on top
        //  of the periodic wow and an occasional slip.
        //
        //  In other words it is the difference between a studio deck's gentle flutter
        //  and a tired consumer machine's wobble. Default 0.
        // =========================================================================
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "mechanics", 1 }, "Mechanics",
                                                                percentageRange (0.50f), 0.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
    }

    /** The room the machine is in.
    */
    void addReverbControls (ParamLayout& layout)
    {
        // =========================================================================
        //  REVERB - the room the machine is in.
        //
        //  Tape machines lived in rooms, and the room is part of the sound of a
        //  recording made on one. This is a small-to-medium plate/room hybrid placed
        //  AFTER the machine, so the reverb is of the processed signal rather than
        //  feeding back into the saturation - which keeps it clean and predictable.
        //
        //  Default 0 - dry, no room.
        // =========================================================================
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "reverb", 1 }, "Reverb",
                                                                percentageRange (0.45f), 0.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
        // How long the reverb's tail runs. A separate control because decay and level
        // are genuinely independent decisions on a real reverb.
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "reverb_size", 1 }, "Reverb Size",
                                                                percentageRange (0.50f), 0.40f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
    }

    /** The delay's character, the hard-clipping stage in front of the machine and
        the two alternative-machine switches.
    */
    void addColourControls (ParamLayout& layout)
    {
        // =========================================================================
        //  DELAY TYPE - which kind of delay the second head behaves as.
        //
        //  DELAY and DLY LVL set the time and the level; this sets the CHARACTER of
        //  the repeats, and the three are genuinely different machines:
        //
        //    TAPE  - the original behaviour: each pass round the loop loses top end,
        //            because the repeat is recorded onto the tape and played back.
        //    BBD   - a bucket-brigade chip, the analogue delay of the era. Darker
        //            still, with a slight aliasing/clock noise on the repeats and a
        //            bandwidth that narrows as the delay lengthens.
        //    MODERN- a clean digital delay: full bandwidth, no loss, repeats that
        //            stack without getting dull.
        //
        //  Default TAPE, which is what every earlier build did.
        // =========================================================================
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "delay_type", 1 }, "Delay Type",
                                                                juce::StringArray { "Tape", "BBD", "Modern" },
                                                                0));

        // =========================================================================
        //  DISTORTION - a hard-clipping stage in front of the machine.
        //
        //  Where the saturation core bends, this breaks: a diode-clipper style hard
        //  knee with a pre-gain, which is the sound of a distortion pedal rather than
        //  an overdriven recorder. It is deliberately placed BEFORE the tape so the
        //  machine can smooth what it produces - which is what makes a distorted
        //  signal recorded to tape sound like a record rather than a pedal.
        //
        //  Default 0 - off.
        // =========================================================================
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "distortion", 1 }, "Distortion",
                                                                percentageRange (0.45f), 0.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));

        // =========================================================================
        //  MODES - the two "alternative machine" switches.
        //
        //  MODERN re-voices the whole machine for a modern, clean, wide-bandwidth
        //  sound: the head losses open up, the noise floor drops, the hysteresis
        //  memory thins out and the glue stages tighten. It is the difference between
        //  a 1970s deck and a well-maintained 1990s one.
        //
        //  LO-FI goes the other way and then further: it narrows the bandwidth hard,
        //  adds a bit-crush style quantisation, brings up the noise and the transport
        //  instability, and rolls off both ends. It is the deliberate degradation
        //  mode - an effect, not a calibration.
        //
        //  Both are off by default, and they are mutually exclusive by design: turning
        //  one on releases the other, because a machine cannot be both.
        // =========================================================================
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "modern_mode", 1 },
                                                                "Modern", false));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "lofi_mode", 1 },
                                                                "Lo-Fi", false));
    }

    /** The record-playing end of the chain: the surface noise, the four physical
        faults, the generation, the turntable and the cartridge.
    */
    void addVinylControls (ParamLayout& layout)
    {
        // =========================================================================
        //  VINYL - the record-playing end of the chain.
        //
        //  A turntable adds three things that nothing else in this plugin does, and
        //  they are what "vinyl" means as a sound:
        //
        //    SURFACE - the crackle and the rumble of a record surface. The crackle is
        //              impulse noise (ticks), not hiss; the rumble is a low-frequency
        //              thump from the bearing and the motor.
        //    RUMBLE  - how much of that low-frequency noise there is.
        //    WARMTH  - the RIAA playback curve's low-end lift and top-end roll-off,
        //              which is what makes vinyl read as warm rather than merely noisy.
        //
        //  SURFACE is the overall amount; at 0 the whole vinyl stage is bypassed.
        // =========================================================================
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "vinyl", 1 }, "Vinyl",
                                                                percentageRange (0.45f), 0.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "vinyl_crackle", 1 }, "Crackle",
                                                                percentageRange (0.45f), 0.5f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "vinyl_rumble", 1 }, "Rumble",
                                                                percentageRange (0.45f), 0.35f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
        // VINYL SPEED - the turntable's speed, which is a transport property rather
        // than a disc property: the type selectors describe the record, this describes
        // the motor driving it. Each speed carries its own wow rate and depth, so the
        // same record wanders differently at 33 and 45.
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "vinyl_speed", 1 }, "Vinyl Speed",
                                                                juce::StringArray { "33 RPM", "45 RPM", "78 RPM" },
                                                                0));

        // -------------------------------------------------------------------------
        //  The four physical faults of a record and a turntable.
        //
        //  VINYL is the overall amount of the record-playing end; CRACKLE and RUMBLE
        //  are its two classic noise sources. These four are the REST of what goes
        //  wrong, and each is a genuinely different mechanism rather than another
        //  amount of noise:
        //
        //    DUST       fine particulate in the groove - a continuous granular
        //               texture that follows the programme, so a loud passage
        //               sounds dirtier than a quiet one.
        //    SCRATCH    a deep groove wound crossed once per revolution - PERIODIC
        //               damage, heard as a repeating thud rather than as a hiss.
        //    WARP       the record is not flat - the level breathes at the platter
        //               rate as the stylus rides up and down.
        //    ELECTRICAL the cartridge, the cable and the earth loop - mains hum at
        //               the supply frequency plus its harmonic, and earth static.
        //
        //  All four default to 0: they are faults, not calibrations, and a fresh
        //  instance must play the record clean until the user asks for the damage.
        //  They live under the VINYL stage, so VINYL 0 bypasses them with it.
        // -------------------------------------------------------------------------
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "vinyl_dust", 1 }, "Dust",
                                                                percentageRange (0.45f), 0.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "vinyl_scratch", 1 }, "Scratch",
                                                                percentageRange (0.45f), 0.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "vinyl_warp", 1 }, "Warp",
                                                                percentageRange (0.45f), 0.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "vinyl_electrical", 1 }, "Electrical",
                                                                percentageRange (0.45f), 0.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));

        // -------------------------------------------------------------------------
        //  CLICKS - the sharp, discrete groove faults.
        //
        //  CRACKLE is the fine surface texture and DUST is the grit in the groove;
        //  CLICKS is the third and loudest class of record damage: an actual ridge
        //  or pit that the stylus hits as a single hard transient. Where a crackle
        //  tick is a few milliseconds of noise, a click is a fast bipolar IMPACT -
        //  a full-bandwidth spike with almost no ringing - which is why it reads as
        //  "a click" rather than as more crackle.
        //
        //  Above half the travel a fraction of the clicks becomes PERIODIC, locked to
        //  the platter, so a badly pressed record ticks in time rather than at
        //  random. That is the difference between a dirty record and a broken one.
        //
        //  Default 0 - a clean pressing has no clicks.
        // -------------------------------------------------------------------------
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "vinyl_clicks", 1 }, "Clicks",
                                                                percentageRange (0.45f), 0.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));

        // -------------------------------------------------------------------------
        //  GENERATION - how the record was cut and pressed.
        //
        //  Three stages of the same thing, in the order a record is actually made:
        //
        //    LAQUER   the reference cut. Almost none of the cutter head's own colour,
        //             loud and clean - what the mastering engineer actually heard.
        //    DIRECT   a direct-metal master: cut straight to a metal mother rather
        //             than a lacquer, so it is cleaner still, with the most top end
        //             and the quietest surface of the three.
        //    PRINTED  a stamper pressing - the record you buy. Every generation
        //             between the cut and this copy has taken something: the top end
        //             is duller, the surface is noisier, and the bass is a little
        //             fuller because that is what survives.
        //
        //  The choice sets the stage's own top-band loss and bottom-band lift, and
        //  scales the noise it makes - so a PRINTED record is not merely darker, it
        //  is noisier, which is what actually distinguishes the two.
        // -------------------------------------------------------------------------
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "vinyl_generation", 1 },
                                                                "Generation",
                                                                juce::StringArray { "Laquer", "Direct", "Printed" },
                                                                0));

        // -------------------------------------------------------------------------
        //  TURNTABLE - what drives the platter.
        //
        //  BELT   an audiophile belt-drive: the motor is isolated from the platter by
        //         an elastic belt, so the drive is smooth and quiet but marginally
        //         less steady, and the platter takes a moment to settle. The quieter,
        //         gentler answer.
        //    DIRECT a high-torque direct-drive DJ deck: the platter IS the motor, so
        //         the speed is rock-steady and the pitch is locked. What chasing and
        //         scratching needs.
        //    IDLER  a vintage idler-wheel deck: the motor bears on the inside of the
        //         platter through a rubber wheel, which couples the drive's own
        //         rumble straight into the groove. Warmer and noticeably less stable
        //         than either of the other two.
        //
        //  It scales the stage's speed wander and its stability, so the same record
        //  wanders differently on each deck - which is the point of the control.
        // -------------------------------------------------------------------------
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "vinyl_turntable", 1 },
                                                                "Turntable",
                                                                juce::StringArray { "Belt", "Direct", "Idler" },
                                                                0));

        // -------------------------------------------------------------------------
        //  CARTRIDGE - what reads the groove, and the largest single difference of
        //  the three selectors.
        //
        //    MM   moving magnet: warm, slightly soft on top, broad and gentle. The
        //         forgiving answer.
        //    MC   moving coil: more detail and a brighter, tighter top, with a lower
        //         output so it needs more gain - and carries more hiss with it. The
        //         revealing answer.
        //    DJ   a Concorde-style DJ cart: heavier, hotter, tracks harder, with a
        //         little more surface noise and a firm bottom. The loud answer.
        //
        //  It applies a gain PAIR (top and bottom) plus its own noise multipliers,
        //  which is why it sounds like a different cartridge rather than a tone knob.
        // -------------------------------------------------------------------------
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "vinyl_cartridge", 1 },
                                                                "Cartridge",
                                                                juce::StringArray { "MM", "MC", "DJ" },
                                                                0));
    }

    /** The five model switches, one per saturation principle plus vinyl. Each is
        built from its own single-sourced name list in PluginProcessor.h.
    */
    void addTypeSwitchControls (ParamLayout& layout)
    {
        // -------------------------------------------------------------------------
        //  The five type switches, one per saturation principle plus vinyl.
        //
        //  Each is built from its own single-sourced name list in PluginProcessor.h -
        //  the same list the panel's combo box reads - so the parameter and the panel
        //  cannot drift the way tape's two copies once did. The engine's switch is
        //  guarded by a jassert, because C++ cannot count case labels.
        //
        //  VALVE / AMP / TRANSFORMER / DIGITAL re-voice their core's existing curve,
        //  so they only mean anything when their principle is actually in the BLEND.
        //  VINYL re-voices the VinylStage at the end of the chain, so it is audible
        //  whenever VINYL is up, and says nothing about the blend at all.
        // -------------------------------------------------------------------------
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "valve_type", 1 }, "Valve Type",
                                                                valveTypeNameList(),
                                                                0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "amp_type", 1 }, "Amp Type",
                                                                ampTypeNameList(),
                                                                0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "transformer_type", 1 }, "Transformer Type",
                                                                transformerTypeNameList(),
                                                                0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "digital_type", 1 }, "Digital Type",
                                                                digitalTypeNameList(),
                                                                0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "vinyl_type", 1 }, "Vinyl Type",
                                                                vinylTypeNameList(),
                                                                0));
    }

    /** ST LINK: whether the two glue compressors share one gain or run two.
    */
    void addStereoLinkControl (ParamLayout& layout)
    {
        // =========================================================================
        //  ST LINK - whether the two glue compressors share one gain or run two.
        //
        //  Every other stage in this plugin is already per-channel: the saturation
        //  core, the head losses, the transport modulation, the delay, the vinyl
        //  noise and the reverb all keep independent state for the two sides. The
        //  two GLUE stages are the exception, and deliberately so - a single detector
        //  fed by the average of the channels is what stops a hard-panned transient
        //  from pulling the image sideways, which is the classic reason a bus
        //  compressor is stereo-linked.
        //
        //  But that is a CHOICE, not a law, and the two answers are genuinely
        //  different tools:
        //
        //    LINKED (default, 100)  one detector, one gain, both channels. The image
        //                           is rock steady and the compression is
        //                           programme-wide - a bus compressor.
        //    UNLINKED (0)           two detectors, two gains, independent. A loud
        //                           left channel ducks only the left, which is what
        //                           you want on a stereo source with wildly
        //                           different sides - and what a dual-mono
        //                           compressor does.
        //
        //  In between it crossfades, so the image can be tightened or loosened by
        //  degree rather than switched. 100 is the default because it is what every
        //  earlier build did, so an existing session loads unchanged.
        // =========================================================================
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "st_link", 1 }, "ST Link",
                                                                percentageRange (0.50f), 1.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
    }

    /** The tempo-locked delay: the sync switch and the note value it selects.
    */
    void addDelaySyncControls (ParamLayout& layout)
    {
        // =========================================================================
        //  DELAY RATE - the second head locked to the host's tempo.
        //
        //  DELAY is a free-running time in milliseconds, which is what a real head
        //  spacing gives you. A DELAY that follows the music is a different tool, and
        //  it needs the host's tempo rather than a number the user typed: the plugin
        //  reads it from the playhead every block.
        //
        //  SYNC switches between the two. When it is on, RATE selects a note value
        //  and the time is derived from the tempo - so the repeat lands on the beat
        //  whatever the session is at, and follows a tempo change without the user
        //  touching anything.
        //
        //  The note values are the ones a delay is actually used with: straight
        //  divisions from a whole note down to a sixteenth, the two common triplets,
        //  and the dotted eighth - which is the one that gives the classic
        //  off-beat repeat.
        // =========================================================================
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "delay_sync", 1 },
                                                                "Delay Sync", false));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "delay_rate", 1 }, "Delay Rate",
                                                                juce::StringArray { "1/1", "1/2", "1/4", "1/8", "1/16",
                                                                                     "1/4 T", "1/8 T", "1/4 D" },
                                                                2));
    }

    /** The transient shaper's attack, sustain and mix.
    */
    void addTransientControls (ParamLayout& layout)
    {
        // =========================================================================
        //  TRANSIENT SHAPER - attack and sustain, the studio's transient designer.
        //
        //  Every other stage in this plugin changes the WAVEFORM: the saturation core
        //  bends it, the head losses filter it, the delay repeats it. This one changes
        //  the signal's ENVELOPE - how its own amplitude moves over time - which is a
        //  different axis entirely, and the only way to get punch or tightness without
        //  touching the harmonics the machine just produced. It runs AFTER the tape
        //  stage for that reason, on the finished signal, so it shapes what the machine
        //  made rather than feeding a shaper into the nonlinearity.
        //
        //  ATTACK and SUSTAIN are deliberately centred on 0 (no effect) rather than on
        //  a 0..100 sweep, because both are two-sided: negative shortens, positive
        //  lengthens. 0 % is the neutral that leaves the stage transparent, so an
        //  existing session - which has neither control - loads unchanged.
        //
        //  TRANSIENT MIX is how much of the shaped signal is in the output, exactly
        //  like VINYL MIX or MIX itself: it crossfades to the unshaped signal, so the
        //  stage can be A/B'd and blended rather than switched.
        // =========================================================================
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "transient_attack", 1 },
                                                                "Transient Attack",
                                                                juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f),
                                                                0.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "transient_sustain", 1 },
                                                                "Transient Sustain",
                                                                juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f),
                                                                0.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "transient_mix", 1 },
                                                                "Transient Mix",
                                                                percentageRange (0.50f), 0.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
    }

    /** The wet/dry positions of the learned-model and impulse-response stages.
    */
    void addModelMixControls (ParamLayout& layout)
    {
        // =========================================================================
        //  NEURAL stage - a learned model run as a per-channel nonlinearity.
        //
        //  NEURAL MIX is the wet/dry position, and it defaults to 0: with no model
        //  loaded the stage is inert anyway, but defaulting the MIX to 0 as well means
        //  even a session that unexpectedly carries a model still loads to exactly the
        //  sound an earlier build made. The model itself is not a parameter - it is
        //  data, loaded from the editor - so the parameter set never has to change
        //  when a model is swapped.
        // =========================================================================
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "neural_mix", 1 },
                                                                "Neural Mix",
                                                                percentageRange (0.50f), 0.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));

        // IR MIX: the wet/dry position of the impulse-response stage, for the same
        // reasons NEURAL is one: the stage is installed from a file rather than
        // voiced by a knob, it defaults to ABSENT, and a preset that does not
        // state it must load to the stage being out of the path rather than to
        // whatever the session had blended in. Registered beside NEURAL; the
        // load-from-file bits (the IR itself) are editor business, not parameters.
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "ir_mix", 1 },
                                                                "IR Mix",
                                                                percentageRange (0.50f), 0.0f,
                                                                juce::AudioParameterFloatAttributes().withLabel ("%")));
    }

} // namespace

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout FirstAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // One call per group, in the order the parameters were declared in before the
    // split - the order IS the table, so it cannot be shuffled without changing what
    // a saved session and a preset mean.
    addMonitorControls (layout);
    addEqualiserControls (layout);
    addSubFundAndWidthControls (layout);
    addDeckControls (layout);
    addTapeDelayControls (layout);
    addOffsetAndNoiseControls (layout);
    addTransportControls (layout);
    addTuningControls (layout);
    addCompressorControls (layout);
    addLanguageControl (layout);
    addRenderSettingsControl (layout);
    addSaturationMixControls (layout);
    addAmpVoicingControls (layout);
    addPreampControl (layout);
    addDiBoxControls (layout);
    addMediaWearControls (layout);
    addReverbControls (layout);
    addColourControls (layout);
    addVinylControls (layout);
    addTypeSwitchControls (layout);
    addStereoLinkControl (layout);
    addDelaySyncControls (layout);
    addTransientControls (layout);
    addModelMixControls (layout);

    return layout;
}
