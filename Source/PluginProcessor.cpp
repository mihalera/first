/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

// The two pieces of the processor that are no longer in this file and that this
// one still needs to see: the saved-state marker its own save/load stamps and
// migrates, and the factory preset entry points the constructor's debug audit
// calls. Both say in their own banners why they are headers.
#include "ProcessorStateFormat.h"
#include "ProcessorPresets.h"

// The five inline shapers the preparation code below builds its coefficients with.
// They are called from the signal path as well, which is why they are a header
// rather than a file-local block in either - see its own banner.
#include "DSP/HotPathMath.h"

// -----------------------------------------------------------------------------
//  OSC, STAGED: compiled here and STARTED NOWHERE.
//
//  Source/Net/Osc.h is the wire format and Source/Net/OscSurface.h is the address
//  map onto the parameter tree. Neither is constructed by this file: there is no
//  socket, no receiver thread, no port, and nothing in the plugin's state - so the
//  include below changes no behaviour at all. It is here because the alternative is
//  code that no build compiles until the day it is switched on, and that is the
//  shape of defect this repository's harnesses exist to prevent.
//
//  The day it IS switched on, what it needs is: a juce::DatagramSocket on its own
//  thread (never the audio thread), a Surface built from the parameter tree's ids,
//  ranges and groups, and a listener for the port the UI would show. tests/osc
//  already pins the half below the socket.
// -----------------------------------------------------------------------------
#include "Net/OscSurface.h"

#include <array>
#include <cstdint>
#include <limits>
#include <set>
#include <vector>

namespace
{

    // Every parameter whose change should light the editor's "edited" badge. One
    // list, read by both the constructor and the destructor, so the listener and
    // the removal can never disagree about which ids they cover. The editor's own
    // knobs are listed too: a host automation pass and a user drag arrive here by
    // the same route, and both should mark the preset dirty.
    //
    // Returned by reference from a function-local static, so the single list is
    // also the single definition - no header, no duplicated initialiser, and the
    // range-for in both callers reads it without copying.
    // The element count is DEDUCED, not written down. It was a literal 83, and the
    // four EQ on/off parameters added after it made the list 87 - which is a
    // hard compile error ("too many initializers"), so the next parameter added
    // would have been a build break rather than a line to update. Class template
    // argument deduction reads the initialiser and sizes the array to it, which
    // is the whole point of returning the list by reference from one definition.
    const auto& parametersTrackedForDirtyBadge()
    {
        static const auto ids = std::array
        {
            "input", "output", "bypass", "polarity", "auto_gain",
            "subfund",
            "stereo_width", "tape_type", "speed", "instrument", "drive", "bias",
            "oversampling", "tone", "wow", "flutter", "mix", "tracks",
            "character", "delta", "delay_time", "delay_feedback",
            "delay_pingpong",
            "st_offset", "noise", "noise_lvl", "transport", "spindown",
            "ui_sounds", "language", "anti_phase_guard",
            "tape_source", "vinyl_source", "cassette_source", "digital_source",
            "amp_source", "valve_source", "transformer_source",
            "sag", "presence", "cabinet", "amp_bias",
            "preamp", "flux", "wear", "mechanics", "reverb", "reverb_size",
            "di", "di_load", "di_transformer", "di_pad",
            "delay_type", "distortion", "modern_mode", "lofi_mode",
            "vinyl", "vinyl_crackle", "vinyl_rumble", "vinyl_speed",
            "vinyl_dust", "vinyl_scratch", "vinyl_warp", "vinyl_electrical",
            "vinyl_clicks", "vinyl_generation", "vinyl_turntable", "vinyl_cartridge",
            "in_low", "in_mid", "in_high",
            "in_hp_freq", "in_lp_freq", "in_eq_order", "in_eq_q",
            "in_hp_on", "in_lp_on",
            "out_low", "out_mid", "out_high",
            "out_hp_freq", "out_lp_freq", "out_eq_order", "out_eq_q",
            "out_hp_on", "out_lp_on",
            "st_link",
            "valve_type", "amp_type", "transformer_type",
            "digital_type", "vinyl_type",
            "delay_sync", "delay_rate", "tuning_tonic", "tuning_mode", "autotune", "autotune_amount",
            "comp_threshold", "comp_ratio", "comp_attack", "comp_release", "comp_makeup", "comp_mix"
        };

        return ids;
    }

    /**
        Subharmonic generator - the descendant of the fundamental.

        Everything else in this plugin produces OVERtones: harmonics at integer multiples
        of the input frequency. A 100 Hz tone gets 200, 300, 400 Hz and so on. This stage
        is the opposite - it produces the component at HALF the input frequency, so a
        100 Hz note also gains weight at 50 Hz.

        This cannot come out of the saturating curve, and it is worth being clear why,
        because it looks like it should. `tanh (sin (wt))` is a curve applied to a value,
        with no notion of time of its own, so its output is a function of the instantaneous
        input phase - and any such function has period 2pi/w, which means its Fourier
        series contains only multiples of w. Subharmonics need a process with its OWN
        timescale that can fall out of step with the signal.

        On a real machine there are three such processes, and this models all three:

          - Bias leakage. The ultrasonic bias oscillator is not perfectly suppressed on
            playback; the residue weakly modulates the operating point.
          - Domain-wall motion. Magnetic domains flip in groups, and the boundaries
            between them move at a rate that is not locked to the signal. This is the
            best-documented source of subharmonic content in magnetic recording.
          - Scrape flutter. Tape-to-head friction excites the tape's own mechanical
            resonances, modulating the effective head-to-tape speed.

        All three are the same shape mathematically: a slow, signal-dependent modulation
        of the transfer curve. The implementation below is a bi-stable follower - a
        phase-locked relaxation oscillator - which is how a domain-wall group behaves: it
        holds one state through a half cycle, then flips under sufficient drive, giving an
        output that completes one cycle for every TWO input cycles and is therefore an
        octave below the note.

        Phase-locking is the point. A free-running oscillator would drone at a fixed pitch
        under everything, which is an artefact rather than a tape; this only flips when the
        input actually drives it, so it follows what is playing and stays silent when
        nothing is.
    */

    // -------------------------------------------------------------------------
    //  OSC, the note generator on the deck: where its packets go, and which of
    //  the map's slots says what.
    //
    //  LOOPBACK, and that is a decision rather than a default. The module
    //  PUBLISHES; it does not listen, so nothing here ever binds a port, opens a
    //  thread or takes a socket another piece of software could hold - the day
    //  the panel is driven from outside is the day Source/Net's receive half
    //  lands, and that is when a port number becomes a setting the user owns.
    //  Until then 127.0.0.1:9000 is the address an OSC client is pointed at by
    //  convention, and a machine with nothing listening sees no difference at
    //  all: UDP to a closed port fails quietly and costs one small write.
    //
    //  The slots are named because the publish path is written in terms of them:
    //  the map has two entries, built once in the constructor, and an index that
    //  had to be remembered as "the second one" is exactly the kind of thing that
    //  silently swaps a note for a switch one refactor later.
    // -------------------------------------------------------------------------
    constexpr int oscSendPort = 9000;
    constexpr std::size_t oscSlotOn = 0;
    constexpr std::size_t oscSlotNote = 1;

}


//==============================================================================
FirstAudioProcessor::FirstAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
       parameters (*this, nullptr, "TAPE_NONLIN", createParameterLayout())
#endif
{
    inputDbParam  = parameters.getRawParameterValue ("input");
    driveParam    = parameters.getRawParameterValue ("drive");
    biasParam     = parameters.getRawParameterValue ("bias");
    toneParam     = parameters.getRawParameterValue ("tone");
    characterParam = parameters.getRawParameterValue ("character");
    wowParam      = parameters.getRawParameterValue ("wow");
    flutterParam  = parameters.getRawParameterValue ("flutter");
    mixParam      = parameters.getRawParameterValue ("mix");
    tracksParam   = parameters.getRawParameterValue ("tracks");
    outputDbParam = parameters.getRawParameterValue ("output");
    widthParam    = parameters.getRawParameterValue ("stereo_width");
    tapeTypeParam  = parameters.getRawParameterValue ("tape_type");
    speedParam     = parameters.getRawParameterValue ("speed");
    instrumentParam = parameters.getRawParameterValue ("instrument");
    bypassParam   = parameters.getRawParameterValue ("bypass");
    deltaParam    = parameters.getRawParameterValue ("delta");
    oversamplingParam = parameters.getRawParameterValue ("oversampling");
    polarityParam  = parameters.getRawParameterValue ("polarity");
    autoGainParam  = parameters.getRawParameterValue ("auto_gain");
    // OSC, the note generator on the deck: the header's own switch, read by the
    // engine once per block (to gate the oscillator) and by the panel every frame.
    oscOnParam     = parameters.getRawParameterValue ("osc_on");

    // The module's address map, built once here from the two things it publishes.
    // It is a Surface rather than two string literals because the address is the
    // part of OSC that has to be right: the map is what a client would be told to
    // send to, and building it here means the on-wire address and the documented
    // one are the same expression. The full parameter map arrives with the receive
    // half - see the banner at the top of this file for what that still needs.
    oscSurface = std::make_unique<j37::osc::Surface> (
        std::vector<j37::osc::ParameterSlot> {
            { "osc_on",   "osc/on",   0.0f, 1.0f },
            { "osc_note", "osc/note", 0.0f, 1.0f },
        },
        "/mibox");
    // SUBFUND's depth parameter. Without this the pointer stayed null and the engine
    // read depth 0 through the nullptr guard - the generator was built, fed and
    // scaled correctly but multiplied by zero forever, so no subharmonic ever left it
    // no matter where the control was set (the "SUBFUND does nothing" report).
    subFundamentalParam = parameters.getRawParameterValue ("subfund");
    delayTimeParam = parameters.getRawParameterValue ("delay_time");
    delayFeedbackParam = parameters.getRawParameterValue ("delay_feedback");
    delayPingPongParam = parameters.getRawParameterValue ("delay_pingpong");
    stOffsetParam = parameters.getRawParameterValue ("st_offset");
    noiseParam = parameters.getRawParameterValue ("noise");
    noiseLvlParam = parameters.getRawParameterValue ("noise_lvl");
    transportParam = parameters.getRawParameterValue ("transport");
    spindownParam = parameters.getRawParameterValue ("spindown");
    uiSoundsParam = parameters.getRawParameterValue ("ui_sounds");
    antiPhaseGuardParam = parameters.getRawParameterValue ("anti_phase_guard");
    tuningTonicParam = parameters.getRawParameterValue ("tuning_tonic");
    tuningModeParam = parameters.getRawParameterValue ("tuning_mode");
    autoTuneParam = parameters.getRawParameterValue ("autotune");
    autoTuneAmountParam = parameters.getRawParameterValue ("autotune_amount");
    compressorThresholdParam = parameters.getRawParameterValue ("comp_threshold");
    compressorRatioParam = parameters.getRawParameterValue ("comp_ratio");
    compressorAttackParam = parameters.getRawParameterValue ("comp_attack");
    compressorReleaseParam = parameters.getRawParameterValue ("comp_release");
    compressorMakeupParam = parameters.getRawParameterValue ("comp_makeup");
    compressorMixParam = parameters.getRawParameterValue ("comp_mix");
    tapeSourceParam = parameters.getRawParameterValue ("tape_source");
    vinylSourceParam = parameters.getRawParameterValue ("vinyl_source");
    cassetteSourceParam = parameters.getRawParameterValue ("cassette_source");
    digitalSourceParam = parameters.getRawParameterValue ("digital_source");
    ampSourceParam = parameters.getRawParameterValue ("amp_source");
    valveSourceParam = parameters.getRawParameterValue ("valve_source");
    transformerSourceParam = parameters.getRawParameterValue ("transformer_source");
    sagParam = parameters.getRawParameterValue ("sag");
    presenceParam = parameters.getRawParameterValue ("presence");
    cabinetParam = parameters.getRawParameterValue ("cabinet");
    ampBiasParam = parameters.getRawParameterValue ("amp_bias");
    preampParam = parameters.getRawParameterValue ("preamp");
    diParam = parameters.getRawParameterValue ("di");
    diLoadParam = parameters.getRawParameterValue ("di_load");
    diTransformerParam = parameters.getRawParameterValue ("di_transformer");
    diPadParam = parameters.getRawParameterValue ("di_pad");
    fluxParam = parameters.getRawParameterValue ("flux");
    wearParam = parameters.getRawParameterValue ("wear");
    mechanicsParam = parameters.getRawParameterValue ("mechanics");
    reverbParam = parameters.getRawParameterValue ("reverb");
    reverbSizeParam = parameters.getRawParameterValue ("reverb_size");
    delayTypeParam = parameters.getRawParameterValue ("delay_type");
    distortionParam = parameters.getRawParameterValue ("distortion");
    modernModeParam = parameters.getRawParameterValue ("modern_mode");
    lofiModeParam = parameters.getRawParameterValue ("lofi_mode");
    vinylParam = parameters.getRawParameterValue ("vinyl");
    vinylCrackleParam = parameters.getRawParameterValue ("vinyl_crackle");
    vinylRumbleParam = parameters.getRawParameterValue ("vinyl_rumble");
    vinylSpeedParam = parameters.getRawParameterValue ("vinyl_speed");
    vinylDustParam = parameters.getRawParameterValue ("vinyl_dust");
    vinylScratchParam = parameters.getRawParameterValue ("vinyl_scratch");
    vinylWarpParam = parameters.getRawParameterValue ("vinyl_warp");
    vinylElectricalParam = parameters.getRawParameterValue ("vinyl_electrical");
    vinylClicksParam = parameters.getRawParameterValue ("vinyl_clicks");
    vinylGenerationParam = parameters.getRawParameterValue ("vinyl_generation");
    vinylTurntableParam = parameters.getRawParameterValue ("vinyl_turntable");
    vinylCartridgeParam = parameters.getRawParameterValue ("vinyl_cartridge");
    inputEqLowParam = parameters.getRawParameterValue ("in_low");
    inputEqMidParam = parameters.getRawParameterValue ("in_mid");
    inputEqHighParam = parameters.getRawParameterValue ("in_high");
    outputEqLowParam = parameters.getRawParameterValue ("out_low");
    outputEqMidParam = parameters.getRawParameterValue ("out_mid");
    outputEqHighParam = parameters.getRawParameterValue ("out_high");
    inputEqHpFreqParam = parameters.getRawParameterValue ("in_hp_freq");
    inputEqLpFreqParam = parameters.getRawParameterValue ("in_lp_freq");
    inputEqHpOnParam = parameters.getRawParameterValue ("in_hp_on");
    inputEqLpOnParam = parameters.getRawParameterValue ("in_lp_on");
    inputEqOrderParam = parameters.getRawParameterValue ("in_eq_order");
    inputEqQParam = parameters.getRawParameterValue ("in_eq_q");
    outputEqHpFreqParam = parameters.getRawParameterValue ("out_hp_freq");
    outputEqLpFreqParam = parameters.getRawParameterValue ("out_lp_freq");
    outputEqHpOnParam = parameters.getRawParameterValue ("out_hp_on");
    outputEqLpOnParam = parameters.getRawParameterValue ("out_lp_on");
    outputEqOrderParam = parameters.getRawParameterValue ("out_eq_order");
    outputEqQParam = parameters.getRawParameterValue ("out_eq_q");
    valveTypeParam = parameters.getRawParameterValue ("valve_type");
    ampTypeParam = parameters.getRawParameterValue ("amp_type");
    transformerTypeParam = parameters.getRawParameterValue ("transformer_type");
    digitalTypeParam = parameters.getRawParameterValue ("digital_type");
    vinylTypeParam = parameters.getRawParameterValue ("vinyl_type");
    stLinkParam = parameters.getRawParameterValue ("st_link");
    delaySyncParam = parameters.getRawParameterValue ("delay_sync");
    delayRateParam = parameters.getRawParameterValue ("delay_rate");
    transientAttackParam = parameters.getRawParameterValue ("transient_attack");
    transientSustainParam = parameters.getRawParameterValue ("transient_sustain");
    transientMixParam = parameters.getRawParameterValue ("transient_mix");
    neuralMixParam = parameters.getRawParameterValue ("neural_mix");
    irMixParam = parameters.getRawParameterValue ("ir_mix");

    for (auto& compressor : standardCompressors)
        compressor.reset();

    // Four fixed oversampling engines (off / 2x / 4x / 8x). Each owns its own filter
    // state, so switching between them is glitch-free even mid-render, and the
    // host is told the latency of whichever one is active.
    oversamplers.add (new juce::dsp::Oversampling<float> (2)); // dummy, factor 1
    oversamplers.add (new juce::dsp::Oversampling<float> (2, 1,
                        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true));
    oversamplers.add (new juce::dsp::Oversampling<float> (2, 2,
                        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true));
    oversamplers.add (new juce::dsp::Oversampling<float> (2, 3,
                        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true));

    // The A/B slots start as copies of the default state so that toggling compare
    // before anything is stored recalls the same settings rather than an empty tree.
    compareSlots[0] = parameters.copyState().createCopy();
    compareSlots[1] = compareSlots[0].createCopy();

    // Preset dirty tracking: any parameter change while a preset is loaded lights the
    // edited badge in the editor. Host automation ALSO flows through here, which is
    // fine - the badge is advisory and matches what the DAW's own "plugin modified"
    // indication would say.
    //
    // One list, used by both the constructor and the destructor. It used to be two
    // hand-copied spellings of the same 45 ids, so a parameter added to one and not
    // the other left a listener that was never removed - harmless while the object
    // lives, a dangling callback the moment it does not. parametersTrackedForDirtyBadge()
    // below is the single source of truth for both.
    for (const auto* parameterID : parametersTrackedForDirtyBadge())
        parameters.addParameterListener (parameterID, this);

#if DEBUG
    // =======================================================================
    //  The preset audit, run once at construction in debug builds.
    //
    //  The factory presets went wrong twice, in two different ways, and both
    //  produced the same user-visible report ("the presets do not work") with
    //  nothing in a release build to say why:
    //
    //    1. THE NAMES AND THE TABLE DISAGREED. getPresetNames() returned
    //       twenty names for a twenty-six row table, so six rows had no name
    //       and - because the box, the undo label and the badge all index by
    //       POSITION - every name below the gap was attached to the wrong
    //       sound. Nothing crashed; the list simply lied.
    //
    //    2. THE TABLE AND THE PARAMETERS DISAGREED. Fifteen sound-bearing
    //       parameters had no column, so they kept the session's value and
    //       every preset loaded "mostly". See the field block in the table.
    //
    //  Both are compile-time-shaped mistakes - a list length, a set of names,
    //  a set of ids - and both are invisible at runtime by construction. So
    //  they are checked here instead, from the three sources themselves rather
    //  than from a count written down beside them: the names, the table, and
    //  the parameters the processor actually registered. A fourth source of the
    //  same class of bug - a map key naming a parameter that does not exist - is
    //  checked here too, as the keys are collected.
    //
    //  DEBUG only. It is O(presets x parameters) with a ValueTree read per key,
    //  which is nothing to pay for while developing and nothing to pay for at
    //  all in a shipping build.
    // =======================================================================
    {
        const auto names = getPresetNames();
        jassert (names.size() == FactoryPresets::count());

        // A name that is empty, or the same as another, is the same class of bug
        // as a name list that is the wrong length: the box would show two blank
        // entries and the user would have no way to tell which is which. Cheap to
        // check here, invisible from the outside until it happens.
        for (int i = 0; i < names.size(); ++i)
        {
            jassert (names[i].isNotEmpty());

            for (int j = i + 1; j < names.size(); ++j)
                jassert (names[i] != names[j]);
        }

        // The REAL parameter set, from the processor rather than from a list
        // written out beside it: getParameters() is what was registered with the
        // host, and the APVTS member has already built and installed it by the
        // time this constructor body runs.
        //
        //  The layout object is not the source, and asking it is not an option:
        //  JUCE 9's ParameterLayout exposes only add() - its storage vector and
        //  anything that would enumerate it are private - so getParameterIds()
        //  and getParameter() do not exist on it. The cast is to
        //  RangedAudioParameter because that is where the id lives; a parameter
        //  that is not one (a group) has no single id and is skipped, which is
        //  right - the layout registers no groups.
        std::set<juce::String> registered;
        for (const auto* parameter : getParameters())
            if (const auto* ranged = dynamic_cast<const juce::RangedAudioParameter*> (parameter))
                registered.insert (ranged->paramID);

        // On the list itself: an empty one would make every check below pass for
        //  the wrong reason, which is the one way a check written this way fails
        //  to fail.
        jassert (! registered.empty());

        // Every id any preset mentions, so the "is every parameter in a preset"
        // pass below can be the complement of this set rather than a second
        // hand-written list that drifts from the first. And every key is
        // checked against the real set as it is collected: a key naming a
        // parameter that does not exist is dropped SILENTLY by
        // applyFactoryPreset()'s getParameter() guard, so the preset loads with
        // that one control left wherever the session had it.
        std::set<juce::String> covered;
        for (int i = 0; i < FactoryPresets::count(); ++i)
            for (const auto& entry : factoryPresetValues (i))
            {
                jassert (registered.count (entry.first) != 0);
                jassert (! FactoryPresets::isSentinelValue (entry.second));
                covered.insert (entry.first);
            }

        // The five parameters a preset deliberately does NOT state, and the
        // reason for each. This list is the point: the check is only useful if
        // it can be satisfied, so the exclusions are named and justified here
        // rather than the check being weakened until it stops failing.
        //
        //   bypass       a preset that silently bypasses the plugin is a trap;
        //               the user asks for that by pressing BYPASS.
        //   delta        the A/B reference tool, a comparison aid and not a
        //               sound the author would want to hand back on a load.
        //   ui_sounds    a panel preference. It is not audio, it never reaches
        //               an output, and a preset is the wrong place to change
        //               how someone's interface feels.
        //   transport    whether the machine is running is the session's
        //               state, not a property of a sound.
        //   spindown     the same argument: it is how the machine STOPS, so
        //               loading a preset with it on would stop the machine for
        //               a user who never asked for that.
        //   transient_*  and neural_mix. All four default to their NEUTRAL value -
        //               both transient amounts are 0, both mixes are 0 - so a
        //               preset that does not state them loads to the stage being
        //               absent, which is exactly what every preset written before
        //               these controls existed intends. Stating them would also
        //               mean every factory preset had to be rewritten, which is
        //               the churn the single-source registries below exist to
        //               avoid.
        //
        // Anything else missing from every preset is a bug, and the jassert
        // says which id by name rather than just failing.
        const std::set<juce::String> intentionallyNotPresettable {
            "bypass", "delta", "ui_sounds", "language", "anti_phase_guard",
            "transport", "spindown",
            "transient_attack", "transient_sustain", "transient_mix", "neural_mix",
            "ir_mix",
            // OSC is the same KIND of entry as bypass and delta: a listening state
            // rather than a property of a sound. A factory preset that switched the
            // note generator on would surprise every load, and no preset should have
            // to be rewritten to say "the oscillator stays off".
            "osc_on"
        };

        for (const auto& id : registered)
            jassert (covered.count (id) != 0
                  || intentionallyNotPresettable.count (id) != 0);

        // THE LOAD ITSELF, exercised: apply each preset and re-read what landed in
        // the tree. Every listed key must come back equal to the file's value and
        // nothing may throw. This is the objective half of "presets don't work":
        // the apply path (values -> tree -> replaceState) runs here against the
        // real engine, so a broken round-trip is a named key on the console rather
        // than a user's impression. It runs in the constructor, before any editor
        // exists; afterwards the "no preset loaded" state is put back, so the host
        // state lands on top of a clean tree, not on the last audited preset.
        {
            // A FRESH copy per read: replaceState installs a new tree object, so a
            // tree captured once would go stale after the very first apply.
            const auto idProperty = juce::Identifier ("id");
            const auto valueProperty = juce::Identifier ("value");
            auto readBack = [&] (const juce::String& key) -> float
            {
                const auto state = parameters.copyState();

                for (int i = 0; i < state.getNumChildren(); ++i)
                {
                    const auto child = state.getChild (i);
                    if (child.hasProperty (idProperty)
                        && child.getProperty (idProperty).toString() == key)
                        return static_cast<float> (child.getProperty (valueProperty));
                }

                return std::numeric_limits<float>::quiet_NaN();
            };

            for (int i = 0; i < FactoryPresets::count(); ++i)
            {
                applyFactoryPreset (i);

                // A key whose value did not land exactly (a choice parameter
                // clamped to a legal step, or a key the engine rounds) is PRINTED,
                // not asserted: the constructor cannot know what state the host
                // has already pushed, so equality here is a report rather than a
                // contract. What makes it objective is that it names the key and
                // the two numbers, from the real apply path.
                for (const auto& entry : factoryPresetValues (i))
                {
                    const auto landed = readBack (entry.first);

                    // approximatelyEqual also catches a NaN read-back: a missing
                    // key prints with "nan" as the value instead of silently
                    // passing, and the float-equal warning flag stays happy.
                    if (! juce::approximatelyEqual (landed, entry.second))
                        DBG ("[presets] preset " << i << " key " << entry.first
                             << ": wanted " << entry.second << ", tree holds " << landed);
                }
            }

            // Back to the untouched "no preset loaded" state the session opens with,
            // and no factory load left in the undo history: the audit's 29 swaps are
            // bookkeeping, not something Ctrl+Z should walk a debug session through.
            lastPresetIndex.store (-1, std::memory_order_relaxed);
            markPresetClean ({});
            undoManager.clearUndoHistory();
        }
    }
#endif
}

FirstAudioProcessor::~FirstAudioProcessor()
{
    cancelPendingUpdate();
    for (const auto* parameterID : parametersTrackedForDirtyBadge())
        parameters.removeParameterListener (parameterID, this);
}

//==============================================================================
//  OSC: the ring's note, and what goes on the wire with it.
//
//  Both are MESSAGE-thread calls - the editor's timer is the only caller - and
//  neither can touch the audio thread with anything but an atomic. That is the
//  whole reason these two are here rather than in the editor: the note is the
//  engine's state and the packet is the module's, so both belong with the object
//  that outlives the window that asked for them.
//==============================================================================
void FirstAudioProcessor::setOscNote (int noteIndex, bool retrigger) noexcept
{
    oscNoteIndex.store (juce::jlimit (0, 11, noteIndex), std::memory_order_relaxed);

    // A counter, not a flag: the audio thread compares it with what it saw at the
    // last block, so two landings inside one block both count and neither can be
    // lost to a read that happens between the write and the flag's reset.
    if (retrigger)
        oscPulse.fetch_add (1, std::memory_order_relaxed);
}

void FirstAudioProcessor::publishOscState (bool noteChanged)
{
    if (oscSurface == nullptr || oscOnParam == nullptr)
        return;

    const auto on = oscOnParam->load() >= 0.5f;

    // A landing only matters while the module is running - with the switch off the
    // ring parks and nothing is generated, so publishing a note would be a message
    // about a sound nobody is making. The switch itself is the exception in the
    // other direction: the OFF packet is the one a client most needs to receive, so
    // it is sent whatever the state is.
    if (noteChanged && ! on)
        return;

    const auto slot = noteChanged ? oscSlotNote : oscSlotOn;
    const auto normalised = noteChanged
                              ? static_cast<float> (oscNoteIndex.load (std::memory_order_relaxed)) / 11.0f
                              : (on ? 1.0f : 0.0f);

    const auto message = oscSurface->feedback (slot, normalised);

    // feedback() returns an empty message for a slot it does not hold or a value
    // it cannot state. Sending nothing is the right answer to both, and an empty
    // address here would encode as a packet no client could route.
    if (message.address.empty())
        return;

    const auto packet = j37::osc::encode (message);

    if (packet.empty())
        return;

    // Fire-and-forget, from a socket that was never bound: write() caches the
    // address lookup after the first call, so the steady state is one sendto().
    juce::ignoreUnused (oscSocket.write ("127.0.0.1", oscSendPort,
                                         packet.data(), static_cast<int> (packet.size())));
}


//==============================================================================
const juce::String FirstAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool FirstAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool FirstAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool FirstAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double FirstAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int FirstAudioProcessor::getNumPrograms()
{
    return 1;
}

int FirstAudioProcessor::getCurrentProgram()
{
    return 0;
}

void FirstAudioProcessor::setCurrentProgram (int)
{
}

const juce::String FirstAudioProcessor::getProgramName (int)
{
    return {};
}

void FirstAudioProcessor::changeProgramName (int, const juce::String&)
{
}

//==============================================================================
void FirstAudioProcessor::prepareToPlay (double sampleRateToUse, int samplesPerBlock)
{
    sampleRate = static_cast<float> (sampleRateToUse);

    // The oversampling engines only depend on rate and block size, so they are
    // (re)built here. The parameter (if restored by a session) picks which one runs.
    const auto requestedOversampling = oversamplingParam != nullptr
        ? static_cast<OversamplingFactor> (static_cast<int> (oversamplingParam->load()))
        : currentOversampling;
    // Pre-initialize all oversampler engines so processBlock never needs to reallocate
    for (auto* oversampler : oversamplers)
    {
        if (oversampler != nullptr)
        {
            oversampler->initProcessing (static_cast<size_t> (juce::jmax (1, samplesPerBlock)));
            oversampler->reset();
        }
    }

    setOversamplingFactor (requestedOversampling, juce::jmax (1, samplesPerBlock));
    // Stated as a double on purpose: SmoothedValue::reset takes the ramp length in seconds
    // as a double, and `auto` here would have deduced the same type silently. Naming it
    // makes the intent explicit and keeps the literal from looking like a float that lost
    // its suffix.
    const double smoothingSeconds = 0.02;
    inputGainSmoothed.reset (sampleRateToUse, smoothingSeconds);
    inputGainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (
        inputDbParam != nullptr ? inputDbParam->load() : 0.0f));
    outputGainSmoothed.reset (sampleRateToUse, smoothingSeconds);
    outputGainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (
        outputDbParam != nullptr ? outputDbParam->load() : 0.0f));
    mixSmoothed.reset (sampleRateToUse, smoothingSeconds);
    const float initialMix = (mixParam != nullptr ? mixParam->load() : 50.0f) * 0.01f;
    mixSmoothed.setCurrentAndTargetValue (juce::jlimit (0.0f, 1.0f, initialMix));
    widthSmoothed.reset (sampleRateToUse, smoothingSeconds);
    widthSmoothed.setCurrentAndTargetValue (widthParam != nullptr
        ? juce::jlimit (0.0f, 2.0f, widthParam->load() * 2.0f) : 1.0f);
    bypassSmoothed.reset (sampleRateToUse, 0.01);
    // Start from the state the parameter restores: a session saved with BYPASS on
    // must not spend its first 10 ms ramping from the dry position.
    bypassSmoothed.setCurrentAndTargetValue (bypassParam != nullptr && bypassParam->load() >= 0.5f ? 0.0f : 1.0f);
    deltaListenSmoothed.reset (sampleRateToUse, smoothingSeconds);
    // Same reasoning as BYPASS directly above: start from the state the parameter
    // restores rather than ramping into it.
    deltaListenSmoothed.setCurrentAndTargetValue (deltaParam != nullptr && deltaParam->load() >= 0.5f ? 1.0f : 0.0f);

    inputPeakLevel.store (0.0f, std::memory_order_relaxed);
    inputRmsLevel.store (0.0f, std::memory_order_relaxed);
    inputPeakDb.store (-70.0f, std::memory_order_relaxed);
    inputRmsDb.store (-70.0f, std::memory_order_relaxed);
    inputLufs.store (-70.0f, std::memory_order_relaxed);
    inputVuDb.store (-70.0f, std::memory_order_relaxed);
    inputCombinedDb.store (-70.0f, std::memory_order_relaxed);
    inputClipping.store (false, std::memory_order_relaxed);
    outputPeakLevel.store (0.0f, std::memory_order_relaxed);
    outputRmsLevel.store (0.0f, std::memory_order_relaxed);
    outputPeakDb.store (-70.0f, std::memory_order_relaxed);
    outputRmsDb.store (-70.0f, std::memory_order_relaxed);
    outputLufs.store (-70.0f, std::memory_order_relaxed);
    outputVuDb.store (-70.0f, std::memory_order_relaxed);
    outputCombinedDb.store (-70.0f, std::memory_order_relaxed);
    outputClipping.store (false, std::memory_order_relaxed);
    inputGainReductionDb.store (0.0f, std::memory_order_relaxed);
    outputGainReductionDb.store (0.0f, std::memory_order_relaxed);
    inputCompressorActivity.store (0.0f, std::memory_order_relaxed);
    outputCompressorActivity.store (0.0f, std::memory_order_relaxed);
    compressorActivity.store (0.0f, std::memory_order_relaxed);
    transportDrift.store (0.5f, std::memory_order_relaxed);
    harmonicCharacter.store (0.0f, std::memory_order_relaxed);
    bypassActive.store (false, std::memory_order_relaxed);

    hystL.fill (0.0f);
    hystR.fill (0.0f);
    highFreqL.fill (0.0f);
    highFreqR.fill (0.0f);

    // The hiss band-limit state is per channel and must start empty, or a rate switch would
    // carry a stale filter state into the first block and produce a click.
    hissLowPassL = 0.0f;
    hissLowPassR = 0.0f;

    wowPhaseL = 0.0f;
    wowPhaseR = 0.0f;
    flutterPhaseL = 0.0f;
    flutterPhaseR = 0.0f;
    previousTone = -1.0f;
    previousCharacter = -1.0f;
    toneLpAc = 0.0f;
    toneShelfSplit.fill (0.0f);
    headGapHz = 24000.0f;
    preDriveGain = 1.0f;
    flutterScale = 1.0f;

    autoTuneCorrectionSmoothed.fill (1.0f);
    // AUTOTUNE's two rate-dependent halves - the trackers' decimation and the
    // corrector's analysis window - are TOLD the rate rather than left at their
    // defaults, and that happens inside setOversamplingFactor above rather than
    // here, because the rate they must be told is the ENGINE's and this method
    // reads the session's. They are fed the engine's frames: the stage runs at the
    // end of processTapeEngine, on the block the oversampler expanded. Preparing
    // them here from `sampleRate` was a real defect over one factor of two - the
    // tracker read a 440 Hz note as 880 and the corrector would have carried the
    // wrong window - so both are prepared where the engine's rate is known and all
    // that is left here is to start them clean.
    corrector.reset();

    // The sample rate changed, so every time-domain constant has to be rebuilt.
    // The tone filters are cached rather than recomputed per block, and their
    // coefficients depend on the rate, so there is exactly one place that is
    // allowed to own them: this method. Phase accumulators are reset as well,
    // otherwise a rate switch would leave wow/flutter at a stale phase and click.
    resetSampleRateDependentState();

    //  The IR convolvers are NOT prepared here: they run inside the oversampled
    //  engine, and setOversamplingFactor above has already handed them that rate
    //  and block. Preparing them again from the session's numbers here was the
    //  defect - see the call site for the whole of it.
    //
    //  What is left for this method is the part of a prepare that is not about
    //  sizing: the convolution's own tail. The old prepare used to clear it as a
    //  side effect of rebuilding the engine, and the stage's prepare now returns
    //  early on an unchanged spec (it is reached from a parameter change too), so
    //  the clear has to be asked for. A new session must not start with the
    //  previous one's reverb in the buffer.

    // The noise print, if one is loaded, is converted to this rate HERE - a
    // prepare may allocate, an audio callback may not - and the print stays in
    // its decoded form so a later rate switch costs arithmetic rather than
    // another read of the file.
    irStage.reset();

    noiseLoader.prepare (sampleRate);
    postFxScratch.setSize (2, juce::jmax (1, samplesPerBlock * 8), false, true, true);
    postFxScratch.clear();

    // Per-channel DC-blocker state: AC coupling restarts from zero after a rate
    // change, exactly like the analogue coupling capacitors do on power-up.
    dcBlockXState.fill (0.0f);
    dcBlockYState.fill (0.0f);

    // -----------------------------------------------------------------------
    //  Playback head delay line.
    //
    //  Sized once, here, for the longest delay the control can ask for at the
    //  highest rate the engine can run at: 250 ms at 8x oversampling of 192 kHz is
    //  384000 samples. Allocating it in prepareToPlay and never resizing it is
    //  what keeps the audio thread free of allocation - the DELAY control only
    //  moves a read offset inside this buffer, it never changes its size.
    //
    //  Two channels at 4 bytes each is about 3 MB, which is the cost of having a
    //  head spacing that can be swept to a quarter of a second.
    // -----------------------------------------------------------------------
    const double maxEngineRate = juce::jmax (44100.0, sampleRateToUse) * 8.0;
    const int maxDelaySamples = static_cast<int> (std::ceil (0.25 * maxEngineRate)) + 4;
    delayBufferLength = juce::jmax (4, maxDelaySamples);
    delayBuffer.setSize (juce::jmax (1, juce::jmin (2, getTotalNumOutputChannels())),
                         delayBufferLength, false, true, true);
    delayBuffer.clear();
    delayWritePosition = 0;

    // The offset buffer is a fixed short window - a few hundred microseconds is far
    // more than the tens the control asks for - so it is a plain array, not an
    // allocation.
    stOffsetBuffer.fill (0.0f);
    stOffsetWritePosition = 0;

    // The transport starts at speed and lets the first block decide what it should
    // actually be doing: the rig re-arms on its first beginBlock() (it keeps
    // `lastState` at -1), so a session saved at STOP or mid-hold comes back the
    // way it was saved rather than silent until the user touches the switch. The
    // rate is the ENGINE's, because the ramps advance per engine frame and the
    // gesture durations are wall-clock seconds.
    transport.prepare (static_cast<double> (sampleRate) * oversamplingRateFactor);

    // The repitch stage is prepared at the ENGINE's rate for the same reason the
    // rig is: it advances once per engine frame, so its seconds and its samples
    // belong to the oversampled rate, not the host's.
    repitchStage.prepare (static_cast<double> (sampleRate) * oversamplingRateFactor);
    if (spindownParam != nullptr)
        spindownHeld.store (spindownParam->load() >= 0.5f, std::memory_order_relaxed);

    // The delay's damping is tied to the machine's own low-pass so a dark tape
    // gives dark repeats without a second control to keep in sync.
    delayDampCoefficient = juce::jlimit (0.02f, 0.9f, toneLpAc + 0.08f);

    // Compressor-coupled saturation state restarts neutral, so the first block
    // after a rate switch is not coloured by a stale squeeze from the old rate.
    squeezeSaturationDrive = 0.0f;

    // The new stages' state. Everything that carries signal history has to start
    // clean on a rate change or the first block carries a stale tail - which for a
    // reverb means an audible burst of the previous session's room.
    inputStageL.reset();
    inputStageR.reset();
    tapeConditionL.reset();
    tapeConditionR.reset();
    vinylL.reset();
    vinylR.reset();

    // The DI's own smoother starts from the value the parameter restores, so a
    // session saved with the box engaged does not spend its first 20 ms sliding
    // into it.
    diSmoothed.setCurrentAndTargetValue (diParam != nullptr ? diParam->load() : 0.0f);
    diLoadSmoothed.setCurrentAndTargetValue (diLoadParam != nullptr ? diLoadParam->load() : 0.0f);
    diTransformerSmoothed.setCurrentAndTargetValue (diTransformerParam != nullptr
                                                        ? diTransformerParam->load() : 0.0f);

    // Reseeded rather than zeroed: zero is a degenerate LCG state, and the two
    // channels get different seeds so their clock noise is uncorrelated - sharing
    // one would put the BBD buzz in the centre of the image.
    bbdNoiseStateL = 0x85ebca6bu;
    bbdNoiseStateR = 0xc2b2ae35u;
    bbdLowL = bbdLowR = 0.0f;
    lofiLowL = lofiLowR = 0.0f;
    lofiHoldL = lofiHoldR = 0.0f;
    lofiCounter = 0;
    wearModulation = 0.0f;

    // The room starts empty. Its tank runs at the rate its own delay lengths were
    // chosen at and the wrapper carries the signal across, so a new session rate
    // re-times that conversion rather than resizing the room - which is also why
    // the tail lasts the same number of SECONDS at any rate. prepare() clears the
    // tank itself, so there is nothing to flush afterwards.
    reverb.prepare (sampleRateToUse);

    // The vinyl stage's generator gets its own seed, separate from the tape hiss,
    // so the two noise sources are uncorrelated.
    vinylNoiseState = 0x9e3779b9u;

    // The tape noise generator is a per-instance LCG so that every plugin instance
    // and every render pass is deterministic, rather than sharing one thread_local
    // stream whose content would depend on how many instances happen to exist.
    noiseState = 0x1b873593u;

    // The safety limiter must start open, otherwise a stale gain from the previous
    // session would duck the first block audibly. It owns that state now -
    // Source/DSP/SafetyLimiter.h - so this is its own reset.
    limiter.reset();

    // The anti-phase guard starts neutral: no correction, and an energy floor that
    // keeps a silent opening passage from dividing by nothing.
    antiPhaseProduct = 0.0f;
    antiPhaseCorrection = 0.0f;
    antiPhaseProductMagnitude = 1.0e-3f;
    antiPhaseAmount.store (0.0f, std::memory_order_relaxed);

    // The per-band guard is prepared at the engine's rate here - the crossovers'
    // coefficients and the envelope's pole are both functions of it - and reset
    // so the first block after a rate change does not start with a stale filter.
    antiPhaseGuard.prepare (sampleRateToUse);
    antiPhaseGuard.reset();

    // The two equalisers start clean, so the first block after a rate change is
    // not coloured by a stale filter state from the previous rate.
    inputEq.reset();
    outputEq.reset();

    // And their six band gains start at the values the parameters restore, so a
    // session saved with a boost does not spend its first 20 ms gliding into it.
    inputEqLowSmoothed.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (inputEqLowParam != nullptr ? inputEqLowParam->load() : 0.0f));
    inputEqMidSmoothed.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (inputEqMidParam != nullptr ? inputEqMidParam->load() : 0.0f));
    inputEqHighSmoothed.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (inputEqHighParam != nullptr ? inputEqHighParam->load() : 0.0f));
    outputEqLowSmoothed.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (outputEqLowParam != nullptr ? outputEqLowParam->load() : 0.0f));
    outputEqMidSmoothed.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (outputEqMidParam != nullptr ? outputEqMidParam->load() : 0.0f));
    outputEqHighSmoothed.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (outputEqHighParam != nullptr ? outputEqHighParam->load() : 0.0f));

    harmonicAnalyser.reset();
    evenHarmonicRatio.store (0.0f, std::memory_order_relaxed);
    oddHarmonicRatio.store (0.0f, std::memory_order_relaxed);

    // Loudness metering. The K-weighting filters are built from the sample rate here,
    // so the LUFS reading is correct at every supported rate rather than being tuned
    // for one of them.
    outputLoudness.prepare (sampleRateToUse);
    outputLoudness.reset();
    inputLoudness.prepare (sampleRateToUse);
    inputLoudness.reset();
    vuAverage = 0.0f;
    inputVuAverage = 0.0f;

    inputCompressor.reset();
    outputCompressor.reset();

    // The final compensation starts from unity so the first block is not nudged by a
    // stale correction from a previous session or sample rate. The compensator owns
    // that correction now - Source/DSP/AutoGainCompensator.h - so this is its own
    // reset rather than a float written from here.
    autoGainCompensator.reset();
}

void FirstAudioProcessor::resetSampleRateDependentState()
{
    // Wow and flutter are very low frequency modulators, but they are advanced as
    // phase increments per sample, so a stale phase after a rate change is audible
    // as a click. Starting them from zero makes the transport restart cleanly.
    wowPhaseL = 0.0f;
    wowPhaseR = 0.0f;
    flutterPhaseL = 0.0f;
    flutterPhaseR = 0.0f;

    // Force the tone and TONE caches to rebuild against the new rate.
    previousTone = -1.0f;
    previousCharacter = -1.0f;
    if (toneParam != nullptr)
        updateToneCoefficients (toneParam->load(), sampleRate);

    // The control ramp: 20 ms. Every control-derived coefficient in the wet path
    // uses this one window, because it is the shortest that is reliably inaudible
    // as a step while still feeling immediate on a knob. Naming it means the
    // three other, deliberately DIFFERENT windows in this function (the 750 ms
    // hiss gate and the 150 ms formula switch) cannot be mistaken for it.
    constexpr double controlRampSeconds = 0.02;

    // Re-time the coefficient ramps for the new rate. The playback poles start from
    // neutral non-zero values, so the first wet block is audible while the exact
    // per-block targets are reached through the normal 20 ms ramps.
    toneShelfGainSmoothed.reset (sampleRate, controlRampSeconds);
    toneShelfGainSmoothed.setCurrentAndTargetValue (toneShelfGain);
    toneShelfBoostSmoothed.reset (sampleRate, controlRampSeconds);
    toneShelfBoostSmoothed.setCurrentAndTargetValue (toneShelfBoost);
    preDriveGainSmoothed.reset (sampleRate, controlRampSeconds);
    preDriveGainSmoothed.setCurrentAndTargetValue (preDriveGain);
    toneLpSmoothed.reset (sampleRate, controlRampSeconds);
    toneLpSmoothed.setCurrentAndTargetValue (toneLpAc);
    flutterScaleSmoothed.reset (sampleRate, controlRampSeconds);
    flutterScaleSmoothed.setCurrentAndTargetValue (flutterScale);

    driveAmountSmoothed.reset (sampleRate, controlRampSeconds);
    hfPostSmoothed.reset (sampleRate, controlRampSeconds);
    headGapSmoothed.reset (sampleRate, controlRampSeconds);
    // The noise floor ramps far slower than the controls: its gain is also the
    // transport gate (see processTapeEngine), so this window is how long the floor
    // takes to coast down when the machine comes to rest and back up when it spins
    // again - a fade, never a mute-switch drop.
    // The transport gate's own window: 750 ms. Deliberately far slower than the
    // control ramp - it is the time the noise floor takes to coast down when the
    // machine comes to rest, which should read as a fade rather than a mute.
    constexpr double transportGateSeconds = 0.75;
    hissGainSmoothed.reset (sampleRate, transportGateSeconds);
    shaperDriveSmoothed.reset (sampleRate, controlRampSeconds);
    shaperAsymmetrySmoothed.reset (sampleRate, controlRampSeconds);

    // The formula-switch ramp is deliberately slower than the control ramps: it has to
    // move the head-damping pole by up to 8 kHz without that travel being an audible
    // sweep. 150 ms is long enough to read as a morph and short enough that switching
    // formula still feels immediate.
    // The formula switch's own window: 150 ms. Slower than the control ramp because
    // a formula change moves the head-damping pole by up to 8 kHz, and 20 ms of
    // that travel is an audible sweep rather than a morph.
    constexpr double formulaSwitchSeconds = 0.15;
    headDampingSwitchSmoothed.reset (sampleRate, formulaSwitchSeconds);

    // The subharmonic generators hold a bi-stable state and a follower, so they carry
    // across blocks and have to start clean; the depth control ramps like every other
    // gain so that moving it cannot step the phase-locked oscillator.
    subharmonicL.reset();
    subharmonicR.reset();
    subFundamentalSmoothed.reset (sampleRate, controlRampSeconds);
    subFundamentalSmoothed.setCurrentAndTargetValue (
        subFundamentalParam != nullptr ? subFundamentalParam->load() : 0.0f);

    // A rate change invalidates any switch in progress, so the countdown is cleared and
    // the next block re-seeds activeTapeType instead of treating the new rate as a
    // formula change.
    activeTapeType = -1;
    tapeTypeChangeCountdown = 0;

    // The transient shaper's state is envelope-based, so a rate change invalidates it:
    // resuming from a stale envelope would read as a false transient on the first
    // block. The stage is reset and its three controls ramped like every other gain.
    transientL.reset();
    transientR.reset();
    transientAttackSmoothed.reset (sampleRate, controlRampSeconds);
    transientAttackSmoothed.setCurrentAndTargetValue (
        transientAttackParam != nullptr ? transientAttackParam->load() : 0.0f);
    transientSustainSmoothed.reset (sampleRate, controlRampSeconds);
    transientSustainSmoothed.setCurrentAndTargetValue (
        transientSustainParam != nullptr ? transientSustainParam->load() : 0.0f);
    transientMixSmoothed.reset (sampleRate, controlRampSeconds);
    transientMixSmoothed.setCurrentAndTargetValue (
        transientMixParam != nullptr ? transientMixParam->load() : 0.0f);

    // ~2 ms transient window, ~120 ms programme reference: the gap between the two
    // is what the detector reads as an edge. The gain smoother is slower than the
    // control ramp so the ride stays a ride rather than following the waveform.
    transientFastCoefficient = onePoleCoefficient (2.0f, sampleRate);
    transientSlowCoefficient = onePoleCoefficient (120.0f, sampleRate);
    transientGainCoefficient = onePoleCoefficient (30.0f, sampleRate);

    // The neural stage's recurrent state is let go of on a rate change for the same
    // reason the shaper's is: resuming a hidden state from a different rate would
    // produce a step. The model itself is untouched.
    // The model's wet/dry ramp. IR MIX rides its own smoother beside it.
    neuralL.reset();
    neuralR.reset();
    neuralMixSmoothed.reset (sampleRate, controlRampSeconds);
    neuralMixSmoothed.setCurrentAndTargetValue (
        neuralMixParam != nullptr ? neuralMixParam->load() : 0.0f);
    irMixSmoothed.reset (sampleRate, controlRampSeconds);
    irMixSmoothed.setCurrentAndTargetValue (
        irMixParam != nullptr ? irMixParam->load() : 0.0f);

    // The oversampling filters hold per-rate state (their half-band coefficients are
    // tuned to the incoming rate), so they must be flushed on a rate change or the
    // first block after the switch carries a stale pipeline and clicks.
    for (auto* oversampler : oversamplers)
        if (oversampler != nullptr)
            oversampler->reset();
}

void FirstAudioProcessor::publishReportedLatency()
{
    const auto total = oversamplerLatencySamples + publishedCorrectorLatencySamples;
    requestedLatencySamples.store (total, std::memory_order_release);
    triggerAsyncUpdate();
}

void FirstAudioProcessor::handleAsyncUpdate()
{
    const auto total = requestedLatencySamples.load (std::memory_order_acquire);
    if (getLatencySamples() != total)
        AudioProcessor::setLatencySamples (total);
}

void FirstAudioProcessor::setOversamplingFactor (OversamplingFactor factor, int samplesPerBlock)
{
    const auto clamped = (factor == OversamplingFactor::x2
                          || factor == OversamplingFactor::x4
                          || factor == OversamplingFactor::x8)
                       ? factor : OversamplingFactor::off;
    const auto clampedIndex = static_cast<int> (clamped);

    lastBlockSize = juce::jmax (1, samplesPerBlock);
    if (clamped == OversamplingFactor::x2)
        oversamplingRateFactor = 2.0f;
    else if (clamped == OversamplingFactor::x4)
        oversamplingRateFactor = 4.0f;
    else if (clamped == OversamplingFactor::x8)
        oversamplingRateFactor = 8.0f;
    else
        oversamplingRateFactor = 1.0f;

    if (auto* oversampler = oversamplers.getUnchecked (clampedIndex))
    {
        oversampler->initProcessing (static_cast<size_t> (lastBlockSize));
        oversampler->reset();

        // The dummy stage adds no delay; the real ones report their filter latency
        // so every DAW can compensate sample-accurately. setLatencySamples is the
        // inherited non-virtual host-notification setter.
        oversamplerLatencySamples = (clamped == OversamplingFactor::off)
                                        ? 0
                                        : juce::roundToInt (oversampler->getLatencyInSamples());
        publishReportedLatency();

        // The corrector runs INSIDE the engine, so its own lookahead - the
        // vocoder's analysis window - has to be rebuilt for the rate the engine is
        // about to run at. Allocating here is the same trade the oversamplers' own
        // initProcessing makes above: a rate change is a parameter change, and the
        // alternative is an engineering change on the audio thread.
        const auto engineRate = static_cast<double> (juce::jmax (1.0f, sampleRate * oversamplingRateFactor));
        corrector.prepare (engineRate, 2);

        //  The IR convolvers run inside the engine too - Source/ProcessorSignalPath.cpp
        //  hands them the block the upsampler produced, at the rate it produced it.
        //  They were prepared from prepareToPlay's SESSION rate and block, so the
        //  stage was prepared for a rate and a maximum block it never saw: the file
        //  was resampled for the wrong rate, and at 2x and above every block it
        //  received was longer than the maximum it had been prepared for. Both
        //  numbers are the engine's here, beside the corrector's, and the stage
        //  ignores a spec that has not moved.
        irStage.prepare ({ engineRate,
                           static_cast<juce::uint32> (juce::jmax (1, lastBlockSize)
                                                      * juce::jmax (1, static_cast<int> (oversamplingRateFactor))),
                           2 });

        // The tracker rides the same rate, for the same reason and with the same
        // absence of allocation: its decimation is what makes a lag mean the same
        // frequency at every rate, and it is fed the engine's frames. A separate
        // prepare() here is not redundancy - the two are handed the same number
        // because they must be, so the number is written once above.
        for (auto& tracker : autoTuneTrackers)
            tracker.prepare (engineRate);
    }

    currentOversampling = clamped;

    // The engine's rate just moved, so a loaded model's conversion has to move
    // with it: a model trained at 48 kHz is run at 48 kHz from a 96 kHz engine
    // and from a 384 kHz one alike (see RateBridge). Cheap - it publishes two
    // numbers and the stages latch them - which matters, because this is also
    // reached from the audio thread when the OVERSAMPLING parameter changes.
    updateNeuralSampleRates();
}

void FirstAudioProcessor::updateNeuralSampleRates()
{
    // The rate the engine is running at: the session's rate times the oversampling
    // factor, which is the rate every stage inside the engine sees and therefore
    // the rate a model would be run at if it were not carried to its own.
    //
    // The model's rate may be 0, which means unknown - the RTNeural JSON format
    // carries no rate, and no model at all is the same case. Unknowable is not a
    // reason to guess: 0 leaves the model on the engine's samples, which is the
    // behaviour every build before the bridge had.
    const auto engineRate = static_cast<double> (
        juce::jmax (1.0f, sampleRate * oversamplingRateFactor));
    neuralEngineRate.store (engineRate, std::memory_order_relaxed);

    // The spectrum analyser is rebuilt from the same number, here, for the same
    // reason and in the same place. Its window and its band ladder are both
    // functions of the rate, so an OVERSAMPLING change moves every band edge: a
    // ladder built for a 48 kHz engine would label a 384 kHz transform with the
    // wrong frequencies and the curve would be a picture of nothing.
    //
    // This function is the engine-rate hook - reached from prepareToPlay and from
    // a factor change, which arrives on the audio thread - so preparing here is
    // what keeps a second copy of `sampleRate * oversamplingRateFactor` from being
    // written somewhere else and drifting. prepare() allocates nothing and takes
    // no lock (every buffer it fills is a member), so it is safe on that thread.
    spectrumAnalyser.prepare (engineRate);

    const auto modelRate = neuralModelRate.load (std::memory_order_relaxed);
    neuralL.setSampleRates (modelRate, engineRate);
    neuralR.setSampleRates (modelRate, engineRate);
}

void FirstAudioProcessor::updateToneCoefficients (float toneValue, float engineSampleRate)
{
    // Tone tilt: 0 = warm/soft, 1 = open/bright. Every corner is a real frequency in
    // Hz converted with onePoleCoefficientHz, so it means the same thing at every
    // sample rate. (The previous version passed millisecond values that were written
    // as if they were kilohertz - a 2*pi unit error - which put both corners around
    // 5-16 Hz and made the whole wet path sub-audio.)
    const auto toneCurve = std::pow (toneValue, 0.92f);

    // BRIGHTNESS is a true TILT: ONE control sweeping the machine's whole spectral
    // balance around a fixed pivot, not a shelf bolted on top of a fixed roll-off.
    // Both halves move together - the record roll-off pole AND the playback tilt
    // gains - from the same curve:
    //
    //   - record side: the magnetic medium's roll-off travels 3 kHz -> 30 kHz, so
    //     warm genuinely darkens the source and bright opens the record path wide.
    //
    //   - playback side: a fixed-PIVOT tilt stage low-passes the WET SIGNAL
    //     itself at 1.6 kHz and applies a matched gain PAIR - the band above the
    //     pivot and the band below move in opposite directions from the same
    //     Brightness value - removal is capped at a gentle ~4 dB while the
    //     opposite band opens up to +15 dB. 50 percent is exactly
    //     neutral (both gains unity). The pivot split is taken from the signal
    //     itself, NOT from a filtered copy: the previous "shelf" split the signal
    //     against its own already-low-passed output, so the "high band" it
    //     boosted was mostly hiss residue - which is why the knob never showed up
    //     on an analyser no matter how far it travelled.
    toneLpAc = onePoleCoefficientHz (3000.0f + 27000.0f * toneCurve, engineSampleRate);

    toneShelfCoefficient = onePoleCoefficientHz (1600.0f, engineSampleRate);

    // Matched tilt gains around the 1.6 kHz pivot, deliberately ASYMMETRIC:
    // a band never loses more than about 3.75 dB, while the opposite band opens
    // up to +15 dB - the knob should take away a little and give a lot. u sweeps
    // -1..+1 as Brightness sweeps 0..1 and is built from the raw control (not
    // the record-side curve), so at the 50 percent pivot u is exactly 0, both
    // gains are unity and the playback passes through untouched.
    const auto tiltU = 2.0f * toneValue - 1.0f;
    const auto tiltGive = 0.75f * juce::jmax (0.0f, tiltU);   // the band that opens: up to +15 dB
    const auto tiltTake = 0.1875f * juce::jmin (0.0f, tiltU); // the band that yields: max ~-3.75 dB
    toneShelfGain  = std::pow (10.0f, -tiltGive - tiltTake);  // low:  +15 dB warm .. -3.75 dB bright
    toneShelfBoost = std::pow (10.0f,  tiltGive + tiltTake);  // high: -3.75 dB warm .. +15 dB bright
    previousTone = toneValue;

    // TONE macro crossfade, between machine states rather than dry/wet:
    //   head gap  - the dominant top-end damping of the playback head
    //   pre-bias  - how hard the record head is driven for a given input
    //   flutter   - the fast transport shimmer every speed sets
    // 0 % is the classic slow machine (soft, dark, wide wow), 100 % the fast one
    // (open, tight, present). Cached here so the per-sample loop only ever reads
    // ready-made coefficients.
    const auto character = (characterParam != nullptr
                               ? juce::jlimit (0.0f, 1.0f, characterParam->load())
                               : 0.5f);
    const auto characterCurve = std::pow (character, 1.20f);

    headGapHz = 24000.0f * std::pow (0.28f, characterCurve); // 24 kHz -> 4.3 kHz
    preDriveGain = 1.0f + 0.55f * (1.0f - characterCurve);
    flutterScale = 0.75f + 0.55f * characterCurve;
    previousCharacter = character;
}

void FirstAudioProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool FirstAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif


//==============================================================================
FirstAudioProcessor::TelemetryFrame FirstAudioProcessor::getTelemetry() const
{
#if J37_HAS_RWQ
    // Drain to the NEWEST frame rather than taking the first: the editor polls at
    // 30 Hz while the audio thread may push a frame per block (which can be several
    // hundred a second), so reading the oldest would show the panel a picture from
    // well behind the audio. Popping until the ring is empty leaves `frame` holding
    // the most recent one - which is the only one worth drawing.
    TelemetryFrame frame;
    bool got = false;

    TelemetryFrame next;
    while (telemetryQueue.try_dequeue (next))
    {
        frame = next;
        got = true;
    }

    if (got)
        return frame;
#endif

    // Either the library is absent or nothing has been pushed yet (the very first
    // UI frame, before the first block). Fall back to the atomics, which processBlock
    // keeps current on every path including the bypass return.
    TelemetryFrame fallback;
    fallback.inputPeakDb = telemetryInputPeakDb.load (std::memory_order_relaxed);
    fallback.outputPeakDb = telemetryOutputPeakDb.load (std::memory_order_relaxed);
    fallback.inputRmsDb = telemetryInputRmsDb.load (std::memory_order_relaxed);
    fallback.outputRmsDb = telemetryOutputRmsDb.load (std::memory_order_relaxed);
    fallback.antiPhaseAmount = telemetryAntiPhase.load (std::memory_order_relaxed);
    fallback.transportRamp = telemetryPlatter.load (std::memory_order_relaxed);
    return fallback;
}

//==============================================================================
bool FirstAudioProcessor::loadNeuralModel (const juce::String& modelJson)
{
    // The new model is staged for adoption at the next audio block, so loading
    // never replaces the instance currently in inference.
    lastLoadErrorMessage = {};
    lastLoadDetailMessage = {};

    if (modelJson.isEmpty())
    {
        lastLoadErrorMessage = "EMPTY FILE";
        return false;
    }

    const bool loadedLeft = neuralL.loadFromJson (modelJson);
    const bool loadedRight = neuralR.loadFromJson (modelJson);
    if (loadedLeft != loadedRight)
    {
        neuralL.clear();
        neuralR.clear();
    }

    if (! (loadedLeft && loadedRight))
    {
        lastLoadErrorMessage = "NOT A MODEL FILE";
        lastLoadDetailMessage = "The file parsed as JSON but not as an RTNeural "
                                "network: the layer stack it describes is not one "
                                "this build can build.";
    }

    // RTNeural's own JSON carries no rate - the model's topology is all it
    // describes - so this model has no rate to be run at and keeps the engine's,
    // which is what every build before the bridge did. A rate left over from a
    // .nam model must NOT survive here: it would be that file's rate applied to
    // this one.
    if (loadedLeft && loadedRight)
    {
        neuralModelRate.store (0.0, std::memory_order_relaxed);
        updateNeuralSampleRates();
    }

    return loadedLeft && loadedRight;
}

bool FirstAudioProcessor::loadNamModel (const juce::String& namJson)
{
    // Same two-stages-at-once discipline as loadNeuralModel above, through the
    // .nam converter: the shared model pointer must not end up describing two
    // different networks on the two sides of the image.
#if J37_HAS_RTNEURAL || J37_HAS_NAMCORE
    lastLoadErrorMessage = {};
    lastLoadDetailMessage = {};

    if (namJson.isEmpty())
    {
        lastLoadErrorMessage = "EMPTY FILE";
        return false;
    }

    bool loadedLeft = false;
    bool loadedRight = false;

    try
    {
        //  A .nam file written on Windows sometimes carries a UTF-8 byte-order
        //  mark. nlohmann's parser refuses it - correctly, it is not JSON - and
        //  the rejection reads as "NOT JSON" for a file that is exactly JSON.
        //  JUCE's loadFileAsString() drops the mark for us; this guard keeps the
        //  processor honest for any other caller that hands it raw bytes.
        auto text = namJson;
        if (text.startsWithChar (static_cast<juce::juce_wchar> (0xfeff)))
            text = text.substring (1);

        const auto parsed = nlohmann::json::parse (text.toStdString());

        // The architecture is checked HERE only when this build has NO engine for
        // the other ones, so the panel can say which kind of file arrived. WaveNet
        // and the convolutional nets are NAM's other architectures and between
        // them account for most models on the model-sharing sites, so "NEEDS AN
        // LSTM MODEL" is the one failure a user is most likely to meet in a build
        // without NAM Core - and a bare "INVALID MODEL" would send them looking
        // for a corrupt file rather than a different download.
        //
        // With NAM Core in the build that check would be a lie: WaveNet is exactly
        // what the second engine is for, so the decision is left to the stage,
        // which accepts what either engine can build and refuses the rest.
    #if ! J37_HAS_NAMCORE
        if (! parsed.contains ("architecture") || ! parsed["architecture"].is_string()
            || parsed["architecture"].get<std::string>() != "LSTM")
        {
            lastLoadErrorMessage = "NEEDS LSTM";
            return false;
        }
    #endif

        // The rate the model was TRAINED at. It is the one thing in a .nam file
        // the converter has no use for and the ENGINE does: a neural net's memory
        // is counted in samples rather than seconds, so a 48 kHz model run on a
        // 192 kHz engine is a different network rather than a brighter one (see
        // RateBridge).
        //
        // `sample_rate` is OPTIONAL in the format, and the format's own
        // documentation says to assume 48 kHz when it is absent - which is also
        // the rate every .nam model this build is likely to meet was trained at,
        // so that is the assumption, and it only ever changes the model's sound
        // where the engine is NOT already running at 48 kHz.
        auto modelRate = 48000.0;
        if (const auto rate = parsed.find ("sample_rate");
            rate != parsed.end() && rate->is_number())
            modelRate = rate->get<double>();

        loadedLeft = neuralL.buildFromNam (parsed);
        loadedRight = neuralR.buildFromNam (parsed);

        if (loadedLeft && loadedRight)
            neuralModelRate.store (modelRate, std::memory_order_relaxed);
    }
    catch (const std::exception& error)
    {
        lastLoadErrorMessage = "NOT JSON";
        // The parser's own "[json.exception.parse_error.101] ..." is too long
        // for the readout and just right for its tooltip - cut to one line's
        // worth, because the parser quotes the document it choked on and a .nam
        // document is mostly a weights array.
        lastLoadDetailMessage = juce::String (error.what()).trim().substring (0, 240);
        loadedLeft = false;
        loadedRight = false;
    }
    catch (...)
    {
        lastLoadErrorMessage = "NOT JSON";
        loadedLeft = false;
        loadedRight = false;
    }

    if (loadedLeft != loadedRight)
    {
        neuralL.clear();
        neuralR.clear();
    }

    if (! (loadedLeft && loadedRight) && lastLoadErrorMessage.isEmpty())
    {
        lastLoadErrorMessage = "BAD SHAPE";

        //  Neither engine produced a model. NAM Core's own sentence is the one
        //  thing that can tell a user whether the file is corrupt, from an
        //  unsupported version, or simply an architecture this build lacks.
        const auto engineSaid = juce::String (neuralL.lastBuildErrorText()).trim().substring (0, 240);
        if (engineSaid.isNotEmpty())
        {
            lastLoadDetailMessage = engineSaid;

            //  A version refusal is the one failure with a one-line answer, so
            //  it keeps its own short label rather than "BAD SHAPE".
            if (engineSaid.containsIgnoreCase ("unsupported version"))
                lastLoadErrorMessage = "NAM VERSION";
            else if (engineSaid.containsIgnoreCase ("missing required key"))
                lastLoadErrorMessage = "NOT A NAM FILE";
        }
        else
        {
            lastLoadDetailMessage = "The file was read as a .nam document, but "
                                    "neither the LSTM converter nor NAM Core could "
                                    "build a model from it.";
        }
    }

    // The model's own rate reaches the stages here, after the load rather than
    // before it: the stages latch the pair on their next inference, so the bridge
    // is prepared for the model that is actually installed.
    if (loadedLeft && loadedRight)
        updateNeuralSampleRates();

    return loadedLeft && loadedRight;
#else
    //  A pruned build has neither engine. Said out loud rather than returned as
    //  a bare false: a silent refusal is what made "NAM does not load" look like
    //  a button that does nothing, because the readout never left "NO MODEL".
    juce::ignoreUnused (namJson);
    lastLoadErrorMessage = "NO NAM ENGINE";
    lastLoadDetailMessage = "This build has neither RTNeural nor NAM Core, so no "
                            ".nam file can be played - rebuild with the vendored "
                            "libraries, or with -DJ37_NAMCORE=ON.";
    return false;
#endif
}

bool FirstAudioProcessor::loadIrFile (const juce::File& irFile)
{
    lastLoadErrorMessage = {};
    lastLoadDetailMessage = {};

    if (! irFile.existsAsFile())
    {
        lastLoadErrorMessage = "NO SUCH FILE";
        return false;
    }

    if (irStage.loadFromFile (irFile))
        return true;

    // The one way a real WAV gets here is a header JUCE will not decode (an
    // unsupported codec, or a file that is not audio at all). The reason is
    // about THIS file, not about the stage.
    lastLoadErrorMessage = "NOT AUDIO";
    lastLoadDetailMessage = "JUCE's basic formats read this file and found no "
                            "audio frames: it is not a WAV or AIFF this build can "
                            "decode, or it is empty.";
    return false;
}

void FirstAudioProcessor::clearIr()
{
    irStage.clear();
    loadedIrName = {};
}

bool FirstAudioProcessor::loadNoiseFile (const juce::File& noiseFile)
{
    if (! noiseFile.existsAsFile())
        return false;

    // Everything else about this load happens on the loader's own thread: the
    // read, the decode and the rate conversion all allocate, and none of them may
    // happen anywhere near the audio callback. The audio thread sees the result
    // as one pointer swap, or never sees it at all if the file turns out not to
    // be a WAV this build can read - in which case getNoiseError() says why and
    // the previous print, if there was one, is still playing.
    noiseLoader.requestLoad (noiseFile);
    return true;
}

void FirstAudioProcessor::clearNoiseFile()
{
    noiseLoader.clear();
}

void FirstAudioProcessor::clearNeuralModel()
{
    neuralL.clear();
    neuralR.clear();
    loadedNeuralName = {};
}

//==============================================================================
bool FirstAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* FirstAudioProcessor::createEditor()
{
    return new FirstAudioProcessorEditor (*this);
}

//==============================================================================
void FirstAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Stamped with the current format, so the session this saves never needs
    // migrating when it is opened again.
    auto state = captureState (parameters);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void FirstAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // sizeInBytes is already an int, and so is getXmlFromBinary()'s second parameter, so
    // casting it to size_t only bought a size_t -> int narrowing warning.
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState != nullptr)
    {
        // This is the path every existing DAW project arrives on, and the one the
        // MIX rescale hits hardest. The tree holds RAW parameter values, so a
        // session saved at MIX 0.5 would be read as 0.5 PERCENT by the new 0..100
        // range and the machine would come back all but silent. Migrated here,
        // before the state reaches the parameters, and only for states written
        // before the format marker existed.
        auto tree = juce::ValueTree::fromXml (*xmlState);
        migrateStateFormat (tree);
        parameters.replaceState (tree);
    }

    // A freshly loaded session defines both A/B slots: the loaded state becomes
    // the active side and both slots are seeded with it, so compare starts clean.
    copyToCompareSlot (0);
    copyToCompareSlot (1);
    activeSlot.store (0, std::memory_order_relaxed);

    // The engine's own copy of the momentary spindown is synced by
    // parameterChanged(), which replaceState() above drives for every restored
    // parameter - so there is nothing to do here for it.

    // A session load restores the parameters, not the preset that produced them:
    // the badge starts clean and unnamed, exactly like a freshly opened plugin.
    markPresetClean ({});
    lastPresetIndex.store (-1, std::memory_order_relaxed);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FirstAudioProcessor();
}
