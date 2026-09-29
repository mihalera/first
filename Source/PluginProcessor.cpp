/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

// The two generated headers from the build: the resource accessors, and the
// preset list in build order. Both come from CMakeLists.txt rather than from a
// list written out in this file - see the note above FactoryPresets below.
#include <BinaryData.h>
#include <FactoryPresetIndex.h>

#include <array>
#include <cstdint>
#include <limits>
#include <set>
#include <vector>

namespace
{
    constexpr float minTrack = 0.0f;
    constexpr float maxTrack = 1.0f;

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
            "ui_sounds", "language",
            "blend", "shape", "sag", "presence", "cabinet", "amp_bias",
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
            "delay_sync", "delay_rate"
        };

        return ids;
    }

    // Symmetric decibel range shared by the input and output stage controls.
    constexpr float minInputDb = -32.0f;
    constexpr float maxInputDb = 32.0f;

    /**
        The tanh the magnetic shaper calls.

        This exists as a named function rather than a direct std::tanh call so the
        one place the shaper's cost can be traded for a different one is explicit
        and greppable.

        Default: std::tanh. Exact, and what every existing render was produced
        with.

        With J37_USE_SIMD_TANH defined AND xsimd available: a vectorised rational
        approximation. It is a DIFFERENT function - accurate to roughly 1e-4 over
        the range the shaper uses, which is well below audibility for a curve that
        is already generating harmonics on purpose, but it is not the same curve
        and it will not null against the default. That is exactly why it is behind
        a flag: the choice belongs to a listening test, not to a build default.

        xsimd::tanh is used in its scalar form here. The vector form pays off once
        the tape loop processes a batch of samples at a time; the scalar entry
        point lets that change be made in one place without the shaper having to
        know about it yet.
    */
    // maybe_unused, and not deleted: this is the seam the opt-in flag switches
    // at, and until J37_USE_SIMD_TANH is turned on nothing calls it, which
    // Clang reports as an unused function on every build. The spelling is kept
    // exactly as it is because tests/dsp/extract.py matches this line by name.
    [[maybe_unused]] inline float j37Tanh (float x) noexcept
    {
#if J37_HAS_XSIMD
        return xsimd::tanh (x);
#else
        return std::tanh (x);
#endif
    }

        /**
        NOTE: the single magneticHysteresis curve that used to live here has been
        REMOVED, not merely superseded.

        The engine now runs SaturationCore, whose `shapeTape` branch is this curve
        bit-for-bit - same slope, same memory term, same zero-point correction - so
        keeping a second copy here would be dead code that a reader could easily
        mistake for the live path. That is not a hypothetical risk: the DSP harness
        kept calling this function for a while after the engine stopped, which made
        it report a large regression that was only the test and the plugin
        disagreeing about which curve they were measuring.

        Anything that needs the tape curve now reaches it through SaturationCore,
        which is the one place it exists.
    */

    /** One-pole low-pass coefficient for a given time constant in milliseconds. */
    inline float onePoleCoefficient (float milliseconds, float sampleRate)
    {
        const float seconds = juce::jmax (0.01f, milliseconds) * 0.001f;
        return 1.0f - std::exp (-1.0f / (seconds * juce::jmax (1.0f, sampleRate)));
    }

    /**
        One-pole low-pass coefficient for a given -3 dB CORNER FREQUENCY in Hz.

        This is the correct unit for musical filters. `onePoleCoefficient()` takes a time
        constant, and those two are easy to confuse: tau = 1/(2*pi*f0), so passing a
        frequency where a time constant is expected (or vice versa) puts the corner off by
        a factor of 2*pi or more. The previous tape low-passes passed millisecond values
        that were written as if they were kilohertz corners - "26.0f" meant 26 kHz but
        landed at a 26 ms tau, i.e. a 6 Hz corner - and the whole wet path came out
        sub-audio, which is what made MIX at 100 % sound like mush instead of tape.
    */
    inline float onePoleCoefficientHz (float cornerHz, float sampleRate)
    {
        const float omega = juce::MathConstants<float>::twoPi
                          * juce::jmax (1.0f, cornerHz)
                          / juce::jmax (1.0f, sampleRate);
        return juce::jlimit (0.0f, 1.0f, 1.0f - std::exp (-omega));
    }

    /**
        Soft clipper for the very end of the chain.

        A hard `jlimit (-1, 1)` is the one thing this plugin must not do: it turns any
        overshoot into a flat-topped square edge, which is what "digital clipping" sounds
        like and it is not a tape behaviour. Tape saturates progressively and rolls off,
        so the same treatment is applied at the output.

        The curve is a linear region that blends smoothly into a saturating exponential,
        chosen because it is monotonic, has no jump in value at the knee, and is exactly
        linear for small inputs. That last property matters: below the knee the output is
        bit-for-bit the input, so normal material passes through completely untouched and
        the stage only engages when the signal actually approaches full scale.

        `ceiling` is deliberately just under 1.0 so the output cannot reach full scale even
        when driven hard. That keeps the plugin from hitting the host's own hard limit, and
        it means the resulting distortion is always the gentle kind rather than a wrap.

        Below the knee the response is 1:1. Above it, the output approaches the ceiling
        asymptotically, so no input, however large, can push the result past 0.985.
    */
    inline float softClip (float x) noexcept
    {
        constexpr float ceiling = 0.985f;

        // Below this the curve is 1:1, so quiet and normal-level material is transparent.
        constexpr float knee = 0.70f;

        const auto magnitude = std::abs (x);
        if (magnitude <= knee)
            return x;

        const auto sign = x < 0.0f ? -1.0f : 1.0f;
        const auto excess = magnitude - knee;

        // The value is continuous at the knee (it equals `knee` there) and the slope only
        // changes gradually, so there is no audible corner where saturation begins. The
        // remaining headroom above the knee is what the exponential curve gets to spend.
        const auto compressed = knee + (1.0f - std::exp (-excess)) * (ceiling - knee);
        return sign * juce::jmin (ceiling, compressed);
    }

    /**
        Soft-knee gain computer. `envelopeDb` is the detector level in dBFS.
    */
    inline float softKneeReductionDb (float envelopeDb, float thresholdDb, float kneeDb, float ratio) noexcept
    {
        const float halfKnee = kneeDb * 0.5f;
        const float overshootDb = envelopeDb - thresholdDb;

        if (overshootDb <= -halfKnee)
            return 0.0f;

        if (overshootDb < halfKnee)
        {
            const auto kneeProgress = overshootDb + halfKnee;
            return -(1.0f - 1.0f / ratio) * kneeProgress * kneeProgress / (2.0f * kneeDb);
        }

        return -(1.0f - 1.0f / ratio) * overshootDb;
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

    // ==========================================================================
    //  Saved-state format
    //
    //  MIX moved from 0..1 to 0..100, to match the percentage the panel shows. That
    //  rescale is visible in everything already saved, because
    //  AudioProcessorValueTreeState stores DENORMALISED values: ParameterAdapter::
    //  flushToTree writes `unnormalisedValue` into the tree, and setNewState reads
    //  the "value" property back through setDenormalisedValue. The tree is raw, not
    //  normalised, so a session saved at MIX 0.5 comes back as 0.5 PERCENT once the
    //  parameter's range is 0..100 - the machine all but disappears.
    //
    //  Old states are told apart by the absence of a marker property, never by the
    //  value. A value test cannot work here: 0.5 is a perfectly good 0.5% and was
    //  also a perfectly good 50%, so any threshold either rewrites a legitimate
    //  setting or misses a real one. The marker is stamped on the way out, so every
    //  state this build writes is already current and the migration only ever runs
    //  once per file.
    //
    //  Unknown properties on the tree root are ignored by AudioProcessorValueTree-
    //  State, so this rides along without touching the parameters or the editor.
    // ==========================================================================
    constexpr const char* stateFormatProperty = "nonlinStateFormat";
    constexpr int currentStateFormat = 2;   // 1 = MIX as 0..1, 2 = MIX as 0..100

    void migrateStateFormat (juce::ValueTree& tree)
    {
        // The cast is not decoration: getProperty returns a juce::var, and var against
        // an int has several viable implicit conversions, so `>=` is ambiguous
        // without it.
        if (static_cast<int> (tree.getProperty (stateFormatProperty, 1)) >= currentStateFormat)
            return;

        if (auto mixChild = tree.getChildWithProperty ("id", "mix"); mixChild.isValid())
        {
            const auto stored = static_cast<float> (mixChild.getProperty ("value", 50.0f));
            mixChild.setProperty ("value", stored <= 1.0f ? stored * 100.0f : stored, nullptr);
        }

        tree.setProperty (stateFormatProperty, currentStateFormat, nullptr);
    }

    /** copyState() with the format marker attached, for writing out. */
    juce::ValueTree captureState (juce::AudioProcessorValueTreeState& parameters)
    {
        auto tree = parameters.copyState();
        tree.setProperty (stateFormatProperty, currentStateFormat, nullptr);
        return tree;
    }
}

//==============================================================================
//  The factory presets are defined further down this file, in their own
//  namespace. The debug audit inside the constructor below is the only code
//  that reaches them from above that point, so the two entry points it uses
//  are declared here.
//
//  They were previously called with no declaration at all, which is a name
//  lookup failure - but only in a debug build, because the audit sits inside
//  #if DEBUG. Every release build compiles the mistake out of existence, so
//  nothing else in the build could have caught it.
//==============================================================================
namespace FactoryPresets
{
    inline int count();
    inline bool isSentinelValue (float value) noexcept;
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
    blendParam = parameters.getRawParameterValue ("blend");
    shapeParam = parameters.getRawParameterValue ("shape");
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
            "bypass", "delta", "ui_sounds", "language", "transport", "spindown",
            "transient_attack", "transient_sustain", "transient_mix", "neural_mix"
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
    for (const auto* parameterID : parametersTrackedForDirtyBadge())
        parameters.removeParameterListener (parameterID, this);
}


//==============================================================================
//  Full-state undo. Preset applications and A/B recalls are recorded as one
//  UndoableAction each that swaps the whole APVTS state tree, so a single Ctrl+Z
//  brings back exactly what was on screen before. Individual knob moves are not
//  booked into the history (the A/B slots and double-click reset cover those),
//  which keeps the undo stack meaningful instead of thousands of micro-steps.
//==============================================================================
namespace
{
    class StateSwapAction final : public juce::UndoableAction
    {
    public:
        StateSwapAction (FirstAudioProcessor& ownerIn,
                         const juce::ValueTree& stateBefore,
                         const juce::ValueTree& stateAfter)
            : owner (ownerIn), before (stateBefore), after (stateAfter) {}

        bool perform() override { owner.replaceParameterState (after); return true; }
        bool undo() override    { owner.replaceParameterState (before); return true; }

        int getSizeInUnits() override
        {
            return (int) (sizeof (*this) + (std::size_t) before.getNumChildren() * 128);
        }

    private:
        FirstAudioProcessor& owner;
        juce::ValueTree before, after;
    };
}

void FirstAudioProcessor::replaceParameterState (const juce::ValueTree& newState)
{
    if (! newState.isValid())
        return;

    // replaceState pushes every parameter change to the host and the editor, so the
    // UI, the DAW automation display and the engine all agree after a swap.
    parameters.replaceState (newState.createCopy());

    // A state swap can include the oversampling switch, so the active engine and the
    // reported latency must follow before the next block is rendered. The RAW value
    // is the choice index; getValue() returns the normalised 0..1 form, which for a
    // choice parameter would round the 4x entry down to 2x. The rebuild uses the
    // remembered host block size rather than a hardcoded one.
    if (auto* rawOversampling = parameters.getRawParameterValue ("oversampling"))
    {
        const auto requested = static_cast<int> (rawOversampling->load());
        if (requested != static_cast<int> (currentOversampling))
            setOversamplingFactor (static_cast<OversamplingFactor> (requested), lastBlockSize);
    }
}

void FirstAudioProcessor::applyStateWithUndo (const juce::ValueTree& targetState,
                                              const juce::String& transactionName)
{
    if (! targetState.isValid())
        return;

    const auto stateBefore = parameters.copyState().createCopy();
    const auto stateAfter = juce::ValueTree (targetState).createCopy();

    undoManager.beginNewTransaction (transactionName);
    undoManager.perform (new StateSwapAction (*this, stateBefore, stateAfter));
}

//==============================================================================
//  Factory presets.
//
//  Twenty starting points: the eighteen sounding machines, plus the two range
//  endpoints the machine is checked against - MINIMUM is the plugin doing the
//  least it can (MIX at zero, so the tape path is out of the signal entirely and the
//  output is the input), and MAXIMUM is every control at the top of its range, which
//  is the honest way to find out what the machine does when it is pushed hardest. Each
//  returns the full parameter map it represents - nothing is patched onto the user's
//  current state beyond the listed values, so a preset changes the machine, not the
//  session.
//
//  The two endpoints are listed SECOND, directly under DEFAULT TAPE, rather than
//  parked at the end of the list. They are reference points, not sounds anybody
//  reaches for while working, and the list is otherwise a set of things to listen
//  to; keeping the references next to the neutral machine they bracket means the
//  sounding presets read as one uninterrupted list with the calibration around it.
//  They are also the two presets a tester wants first, and at the end of twenty they
//  are the two easiest to forget exist.
//
//  The order here and the row order in the table below are ONE list written twice,
//  and they are index-aligned by hand. Changing one without the other hands the
//  panel a name for a different machine, which is the same class of bug as a
//  mismatched choice count: nothing fails, the list just quietly lies.
//==============================================================================
//  The factory presets.
//
//  These used to be a 160-field struct plus twenty-six rows of hand-written
//  C++ initialisers - about 590 lines of numbers, in the same file as the
//  processor, where a new preset meant retyping a row of positional floats and
//  recompiling. They are now twenty-six JSON files under Source/Presets/Factory,
//  compiled into the binary, and this is the whole of what knows how to read
//  them.
//
//  Three things fell out of the move, each of them a bug the old table could
//  have and these files cannot:
//
//    1. THE LIST IS WRITTEN ONCE. FactoryPresetIndex.h is generated by CMake
//       from the same list that feeds juce_add_binary_data, so the preset box,
//       the undo label, the dirty badge and the JSON files cannot disagree about
//       which file is preset 3. That disagreement was real: twenty names over a
//       twenty-six row table, and the box, the label and the badge all index by
//       POSITION, so six rows loaded as whichever preset the selection happened
//       to name.
//
//    2. THE NAME IS DATA TOO. It lives in the file under "name", so renaming a
//       preset is editing that file, not a list in a different one. There is no
//       second list of names left to fall out of step with the files.
//
//    3. A MALFORMED VALUE IS VISIBLE. A key that is not a number is the failure
//       this file used to be able to have quietly: a value silently dropped, so
//       the preset loaded "mostly". It cannot be silent here - see
//       isSentinelValue() below, which the debug audit turns into an assert that
//       names the key.
//==============================================================================
namespace FactoryPresets
{
    // One parse per process, not one per call.
    //
    // factoryPresetValues() is called in a loop by the debug audit and once per
    // preset click; parsing twenty-six JSON objects on each of those is work
    // done for nothing. The cache is a function-local static, so it is built on
    // first use and its initialisation is thread-safe without a mutex (C++11
    // guarantees this for function-local statics). The reference returned is to
    // storage that outlives every caller.
    struct Preset
    {
        juce::String name;
        std::map<juce::String, float> values;
    };

    // The value written in place of anything that was not a number. Chosen to be
    // outside every parameter's range, so even if the audit below is compiled out
    // the parameter rejects it and the control ends up at a legal value rather
    // than at a nonsense one.
    inline constexpr float notANumber = -1.0e9f;

    inline bool isSentinelValue (float value) noexcept
    {
        // Compared as int, not float: the sentinel is exactly representable, so
        // the comparison is exact, and -Wfloat-equal is right to object to a
        // float == in a codebase that has it on.
        return static_cast<int> (value) == static_cast<int> (notANumber);
    }

    const std::vector<Preset>& all()
    {
        static const std::vector<Preset> parsed = []
        {
            std::vector<Preset> result;
            result.reserve (juce::numElementsInArray (FactoryPresetIndex::entries));

            for (const auto& entry : FactoryPresetIndex::entries)
            {
                Preset preset;

                // The name is read separately because it is the one field with a
                // type of its own: everything else in the file is a number
                // destined for a parameter.
                const auto text = juce::String::createStringFromData (entry.data, entry.size);
                const auto parsedJson = juce::JSON::parse (text);

                if (const auto* object = parsedJson.getDynamicObject())
                {
                    preset.name = object->getProperty ("name").toString();

                    // getProperties(), not getPropertyNames(): a NamedValueSet is
                    // what a DynamicObject actually holds, and iterating it yields
                    // the name and the value together, so the two cannot be read
                    // out of step.
                    for (const auto& property : object->getProperties())
                    {
                        // toString(): a NamedValueSet names its entries with
                        // juce::Identifier, and comparing one of those to a string
                        // literal is ambiguous rather than merely wrong. The
                        // preset's keys are strings everywhere else, so this is
                        // where that conversion belongs.
                        const auto propertyName = property.name.toString();

                        if (propertyName == "name")
                            continue;

                        // A value that is not a number is a typo in a key, and a
                        // typo in a key is precisely the bug this file used to
                        // have. It is not skipped quietly; it is recorded, and the
                        // audit in the constructor fails on it by name.
                        const auto value = property.value;

                        if (value.isInt() || value.isInt64() || value.isDouble() || value.isBool())
                            preset.values.emplace (propertyName, static_cast<float> (value));
                        else
                            preset.values.emplace (propertyName, notANumber);
                    }
                }

                result.push_back (std::move (preset));
            }

            return result;
        }();

        return parsed;
    }

    inline int count()
    {
        return static_cast<int> (all().size());
    }
}

int FirstAudioProcessor::numFactoryPresets()
{
    return FactoryPresets::count();
}

juce::StringArray FirstAudioProcessor::getPresetNames()
{
    // Read from the data rather than written beside it. The names come back in
    // the ORDER of the files, because the preset box, the undo label and the
    // dirty badge all index by position - which is what used to break.
    juce::StringArray names;

    for (const auto& preset : FactoryPresets::all())
        names.add (preset.name);

    return names;
}

std::map<juce::String, float> FirstAudioProcessor::factoryPresetValues (int index)
{
    const auto& presets = FactoryPresets::all();
    // size() is unsigned and index is signed, and jmax/jlimit are templates that
    // will not mix the two - so the bound is converted once, here, rather than
    // at three call sites.
    const auto last = juce::jmax (1, static_cast<int> (presets.size())) - 1;
    const auto clamped = juce::jlimit (0, last, index);

    return presets[static_cast<std::size_t> (clamped)].values;
}


void FirstAudioProcessor::applyFactoryPreset (int index)
{
    const auto clampedIndex = juce::jlimit (0, FactoryPresets::count() - 1, index);
    const auto& values = factoryPresetValues (clampedIndex);
    if (values.empty())
        return;

    // Build the target tree from the CURRENT state so anything the map does not
    // list (nothing today, but future-proof) survives the preset change.
    auto target = parameters.copyState().createCopy();
    const auto idProperty = juce::Identifier ("id");
    const auto valueProperty = juce::Identifier ("value");

    for (const auto& valuePair : values)
    {
        // The lookup is only a guard against an id the plugin does not have; the
        // value itself goes in RAW. The tree stores denormalised values, so
        // normalising here and writing the result made every factory preset load
        // the wrong setting: a -3 dB INPUT came back as 0.45 dB, and once MIX became
        // a 0..100 percentage, 100 came back as 1.0 - i.e. 1%, which is why the
        // factory presets had all but gone dry.
        if (parameters.getParameter (valuePair.first) != nullptr)
        {
            for (int i = 0; i < target.getNumChildren(); ++i)
            {
                auto parameterChild = target.getChild (i);
                if (parameterChild.hasProperty (idProperty)
                    && parameterChild.getProperty (idProperty).toString() == valuePair.first)
                {
                    parameterChild.setProperty (valueProperty, valuePair.second, nullptr);
                    break;
                }
            }
        }
    }

    applyStateWithUndo (target, "Preset: " + getPresetNames()[clampedIndex]);
    lastPresetIndex.store (clampedIndex, std::memory_order_relaxed);
    markPresetClean (getPresetNames()[clampedIndex]);
}

void FirstAudioProcessor::copyToCompareSlot (int slot)
{
    const auto slotIndex = juce::jlimit (0, 1, slot);
    compareSlots[static_cast<std::size_t> (slotIndex)] = parameters.copyState().createCopy();
    updateCompareDirty();
}

void FirstAudioProcessor::toggleCompare()
{
    const auto nextSlot = 1 - activeSlot.load (std::memory_order_relaxed);
    applyStateWithUndo (compareSlots[static_cast<std::size_t> (nextSlot)],
                        nextSlot == 0 ? "Recall A" : "Recall B");
    activeSlot.store (nextSlot, std::memory_order_relaxed);
}

void FirstAudioProcessor::updateCompareDirty()
{
    const auto& slotA = compareSlots[0];
    const auto& slotB = compareSlots[1];
    const auto dirty = slotA.isValid() && slotB.isValid() && ! slotA.isEquivalentTo (slotB);
    compareDirty.store (dirty, std::memory_order_relaxed);
}

void FirstAudioProcessor::updateActiveCompareSlot()
{
    const auto slotIndex = static_cast<std::size_t> (
        juce::jlimit (0, 1, activeSlot.load (std::memory_order_relaxed)));

    // One deep copy of the live parameter tree, then compare before storing. The old
    // path called copyToCompareSlot, which made a SECOND full copy of the tree every
    // call - and the editor was calling this on every timer frame, so dragging a knob
    // piled two whole-tree copies per frame on top of the repaint. Skipping the second
    // copy when the live state already matches the stored slot keeps the A/B mirroring
    // cheap in the common case, where nothing has actually changed.
    const auto liveState = parameters.copyState();
    if (compareSlots[slotIndex].isEquivalentTo (liveState))
        return;

    compareSlots[slotIndex] = liveState.createCopy();
    updateCompareDirty();
}

//==============================================================================
//  User presets.
//
//  A factory preset covers the machine's designed range; a user preset freezes the
//  whole machine exactly as it stands. Files are XML state trees (the same format
//  getStateInformation writes into a session) with a .nonlin preset extension, stored in
//  the per-user application data directory, so they survive plugin updates, are
//  shared by every instance and never depend on the session.
//==============================================================================
juce::File FirstAudioProcessor::getUserPresetDirectory()
{
    auto directory = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile ("Nonlin Analog Saturator")
                       .getChildFile ("Presets");
    if (! directory.isDirectory())
        directory.createDirectory();
    return directory;
}

juce::StringArray FirstAudioProcessor::getUserPresetNames() const
{
    juce::StringArray names;
    for (const auto& entry : getUserPresetDirectory().findChildFiles (juce::File::findFiles,
                                                                      false, "*.nonlinpreset"))
        names.add (entry.getFileNameWithoutExtension());
    names.sort (true);
    return names;
}

bool FirstAudioProcessor::saveUserPreset (const juce::String& name)
{
    const auto sanitised = name.trim();
    if (sanitised.isEmpty())
        return false;

    // Strip characters the host file system cannot store, so a preset named with a
    // slash cannot escape the preset directory.
    juce::String safe;
    for (const auto character : sanitised)
        // The full set Windows itself refuses: / \\ : * ? " < > |. A name holding any
        // of the extra five would otherwise survive sanitisation on macOS and Linux
        // and then fail the write on Windows, where the save returns false with no
        // explanation.
        if (character != '/' && character != '\\' && character != ':' && character != '?'
            && character != '*' && character != '"' && character != '<'
            && character != '>' && character != '|')
            safe += character;
    safe = safe.trim();
    if (safe.isEmpty())
        return false;

    // captureState stamps the saved-state format marker, so a preset this build
    // writes never has to be migrated again.
    auto state = captureState (parameters);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (xml == nullptr)
        return false;

    const auto file = getUserPresetDirectory().getChildFile (safe + ".nonlinpreset");
    const auto saved = xml->writeTo (file);
    if (saved)
        markPresetClean (safe);
    return saved;
}

bool FirstAudioProcessor::applyUserPreset (const juce::String& name)
{
    const auto file = getUserPresetDirectory().getChildFile (name.trim() + ".nonlinpreset");
    const auto xml = juce::parseXML (file);
    if (xml == nullptr)
        return false;

    auto restored = juce::ValueTree::fromXml (*xml);
    if (! restored.isValid())
        return false;

    // Brought up to the current saved-state format before it is applied, so a preset
    // written by an older build loads the setting the user actually saved. See the
    // format note at the top of this file: the old code decided whether to rescale
    // from the stored value, and every value this plugin can store is <= 1.0 at MIX's
    // old scale, so it rewrote 50% presets as 100% and a preset saved at 1% as 50%.
    // The marker makes it a decision about the FILE rather than about the number.
    migrateStateFormat (restored);

    applyStateWithUndo (restored, "User preset: " + name);
    lastPresetIndex.store (-1, std::memory_order_relaxed);
    markPresetClean (name.trim());
    return true;
}

bool FirstAudioProcessor::deleteUserPreset (const juce::String& name)
{
    const auto file = getUserPresetDirectory().getChildFile (name.trim() + ".nonlinpreset");
    if (! file.existsAsFile())
        return false;

    const auto deleted = file.deleteFile();
    if (currentPresetName.equalsIgnoreCase (name.trim()))
        markPresetClean ({});
    return deleted;
}

juce::AudioProcessorValueTreeState::ParameterLayout FirstAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

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
    const auto eqOrderRange = juce::NormalisableRange<float> (6.0f, 48.0f, 6.0f);
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
    // WIDTH's 50 percent default is NOT a typo: the control multiplies by 2 in
    // the engine, so 0.5 maps to a 1.0 multiplier - the natural stereo image,
    // with mono at 0 and extra-wide at 100. The panel reading 50 percent is
    // exactly what a natural image should say.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "stereo_width", 1 },
                                                            "Stereo Width",
                                                            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
                                                            0.5f,
                                                            juce::AudioParameterFloatAttributes().withLabel ("%")));
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

    // Knob taper only. This skew shapes how knob travel maps onto the parameter value;
    // it has nothing to do with the sound. The analogue nonlinearity lives in the DSP
    // itself (the power curves and the stacked tanh stages in processBlock), which are
    // deliberately NOT linearised - see the note there.
    //
    // The skew is centred low so the gentle end of each control gets more travel, which
    // is where an analogue control is actually judged, while still reaching its maximum.
    const auto percentageRange = [] (float centre)
    {
        juce::NormalisableRange<float> range (minTrack, maxTrack, 0.001f);
        range.setSkewForCentre (centre);
        return range;
    };

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
    //  Transport state: STOP / PLAY / START.
    //
    //  Three states rather than a play/stop pair, because a tape machine has three
    //  and the middle one is not "stopped":
    //
    //    STOP  - the capstan is at rest. The tape is not moving, so there is no
    //            hiss, no modulation and no delay tail: the machine is silent.
    //    PLAY  - normal running, which is what every earlier build did.
    //    START - the moment of engagement: the capstan comes up to speed, so the
    //            transport runs flat, the modulation deepens and the pitch rides
    //            up into tune over about a second. This is the sound a tape machine
    //            makes when you hit play on a take.
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
                                                            "UI Sounds", true));

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
                                                              juce::StringArray { "English", "\u0423\u043a\u0440\u0430\u0457\u043d\u0441\u044c\u043a\u0430" },
                                                              0));

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
    //  BLEND sweeps the weighting across the six in a fixed order, tape -> valve
    //  -> cassette -> amp -> transformer -> digital, so the control has one
    //  direction: the order is the signal path, and the right end is where the
    //  machines stop and the conversion begins. Default 0 - pure tape, which is
    //  exactly what every earlier build did, so an existing session or preset
    //  loads the machine it was saved with.
    // -------------------------------------------------------------------------
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "blend", 1 }, "Blend",
                                                            percentageRange (0.50f), 0.0f,
                                                            juce::AudioParameterFloatAttributes().withLabel ("%")));
    // SHAPE decides how concentrated the blend is: low picks one principle at a
    // time (an obvious, focused character), high spreads the weighting so all six
    // contribute and the result reads as one compound machine. Default 50 - an
    // even spread, which is the useful starting point once BLEND is moved.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "shape", 1 }, "Shape",
                                                            percentageRange (0.50f), 0.50f,
                                                            juce::AudioParameterFloatAttributes().withLabel ("%")));

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

    return layout;
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
    widthSmoothed.setCurrentAndTargetValue (widthParam != nullptr ? widthParam->load() * 2.0f : 1.0f);
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

    // The sample rate changed, so every time-domain constant has to be rebuilt.
    // The tone filters are cached rather than recomputed per block, and their
    // coefficients depend on the rate, so there is exactly one place that is
    // allowed to own them: this method. Phase accumulators are reset as well,
    // otherwise a rate switch would leave wow/flutter at a stale phase and click.
    resetSampleRateDependentState();

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

    // A transport given no state yet starts at speed if the parameter says PLAY,
    // so a fresh instance is not silent until the user touches the switch. The
    // constructor leaves lastTransportState at -1, so seeding it here is what
    // makes the first block land on the right ramp. The spindown ramp starts at
    // speed too, and syncs from the parameter so a session that was saved
    // mid-hold comes back held.
    transportRamp = 1.0f;
    spindownRamp = 1.0f;
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

    // The reverb is prepared for the rate (its comb lengths scale with it, so the
    // room keeps the same physical size) and then flushed.
    reverb.prepare (sampleRateToUse);
    reverb.reset();

    // The vinyl stage's generator gets its own seed, separate from the tape hiss,
    // so the two noise sources are uncorrelated.
    vinylNoiseState = 0x9e3779b9u;

    // The tape noise generator is a per-instance LCG so that every plugin instance
    // and every render pass is deterministic, rather than sharing one thread_local
    // stream whose content would depend on how many instances happen to exist.
    noiseState = 0x1b873593u;

    // The safety limiter must start open, otherwise a stale gain from the previous
    // session would duck the first block audibly.
    preLimiterDetector = 0.0f;
    limiterGain = 1.0f;

    // The anti-phase guard starts neutral: no correction, and an energy floor that
    // keeps a silent opening passage from dividing by nothing.
    antiPhaseProduct = 0.0f;
    antiPhaseCorrection = 0.0f;
    antiPhaseProductMagnitude = 1.0e-3f;
    antiPhaseAmount.store (0.0f, std::memory_order_relaxed);

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
    // stale correction from a previous session or sample rate.
    smoothedCompensationDb = 0.0f;
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
    neuralL.reset();
    neuralR.reset();
    neuralMixSmoothed.reset (sampleRate, controlRampSeconds);
    neuralMixSmoothed.setCurrentAndTargetValue (
        neuralMixParam != nullptr ? neuralMixParam->load() : 0.0f);

    // The oversampling filters hold per-rate state (their half-band coefficients are
    // tuned to the incoming rate), so they must be flushed on a rate change or the
    // first block after the switch carries a stale pipeline and clicks.
    for (auto* oversampler : oversamplers)
        if (oversampler != nullptr)
            oversampler->reset();
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
        const int newLatency = (clamped == OversamplingFactor::off)
                                   ? 0
                                   : juce::roundToInt (oversampler->getLatencyInSamples());
        if (getLatencySamples() != newLatency)
            AudioProcessor::setLatencySamples (newLatency);
    }

    currentOversampling = clamped;
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

void FirstAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // The oversampling switch is a host parameter; a change requires rebuilding
    // nothing (three fixed engines exist side by side) - only the routing picks
    // which one runs, which is a cheap branch taken once per block.
    if (oversamplingParam != nullptr)
    {
        const auto requested = static_cast<int> (oversamplingParam->load());
        if (requested != static_cast<int> (currentOversampling))
            setOversamplingFactor (static_cast<OversamplingFactor> (requested),
                                   juce::jmax (1, lastBlockSize));
    }

    switch (currentOversampling)
    {
        case OversamplingFactor::x2:
        case OversamplingFactor::x4:
        case OversamplingFactor::x8:
        {
            if (auto* oversampler = oversamplers.getUnchecked (static_cast<int> (currentOversampling)))
            {
                // The block must be non-const: processSamplesDown writes the result
                // back into it, in place over the host's audio.
                juce::dsp::AudioBlock<float> block (buffer);
                processTapeEngine (oversampler->processSamplesUp (block), midiMessages);
                oversampler->processSamplesDown (block);
                return;
            }
            break;
        }
        case OversamplingFactor::off:
        default:
            break;
    }

    processTapeEngine (buffer, midiMessages);
}

//==============================================================================
void FirstAudioProcessor::setTransportState (int state)
{
    // The transport choice parameter is the single source of truth: the host, the
    // preset state and the panel's combo all read it. Writing it here - rather than
    // a private flag beside it - is what keeps those three in agreement, and it is
    // also why START can settle into PLAY on its own: the engine writes the same
    // parameter when the capstan arrives (see processTapeEngine).
    if (auto* parameter = parameters.getParameter ("transport"))
    {
        const auto target = static_cast<float> (juce::jlimit (0, 2, state));
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (target));
    }
}

void FirstAudioProcessor::setSpindownHeld (bool shouldHold)
{
    // Two things have to happen, and both are cheap:
    //
    //   1. The atomic the engine reads every block. That is what actually runs the
    //      platter down, and it is the only path that touches the audio thread -
    //      a relaxed store, no lock, no allocation.
    //   2. The `spindown` parameter, so a host that records automation sees the
    //      gesture and a saved session can hold it. It is written through the
    //      parameter rather than through the attachment in the editor so a button
    //      press and an automation lane are the same event.
    //
    // The engine treats a held spindown as a running transport, so a hold started
    // from STOP brings the platter up and then cuts it: the gesture always has
    // something to act on.
    spindownHeld.store (shouldHold, std::memory_order_relaxed);

    if (auto* parameter = parameters.getParameter ("spindown"))
        parameter->setValueNotifyingHost (shouldHold ? 1.0f : 0.0f);
}

void FirstAudioProcessor::processTapeEngine (juce::dsp::AudioBlock<float> block,
                                             juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);

    const int numSamples = static_cast<int> (block.getNumSamples());

    // Every time constant inside the engine is a duration converted from a sample
    // rate, so it has to see the rate the block actually runs at: the session rate in
    // the plain path, and the multiplied rate in the oversampled path (see
    // processBlock). Reading `sampleRate` here instead would silently turn every
    // filter, detector and modulator off-tune the moment oversampling is switched on.
    const auto engineSampleRate = juce::jmax (1.0f, sampleRate * oversamplingRateFactor);

    // The engine works directly on the block: in the oversampled path the block
    // references the oversampler's internal storage, in the plain path it wraps
    // the host buffer. Either way processing is in place, so what arrives also
    // leaves through the same block.

    // Channels the engine actually works with: up to two, bounded by both what the
    // host delivers and what the oversampler's internal storage provides. Clearing
    // output-only channels (a mono-in/stereo-out host) must not write past the block.
    const int activeInputChannels = juce::jmin (2, juce::jmin (getTotalNumInputChannels(),
                                                               (int) block.getNumChannels()));

    // Clear any output-only channels (mono->stereo hosts) the host expects filled.
    // AudioBlock::clear() in JUCE 9 takes no arguments and clears EVERY channel,
    // so the per-channel primitive is FloatVectorOperations::clear directly -
    // exactly what AudioBlock's own clearInternal() calls.
    for (int channel = activeInputChannels;
         channel < juce::jmin (2, (int) block.getNumChannels());
         ++channel)
        juce::FloatVectorOperations::clear (
            block.getChannelPointer (static_cast<std::size_t> (channel)), numSamples);

    // -------------------------------------------------------------------------
    //  Bypass: the parameter is ramped, so the plugin can be switched in and out
    //  without a click, and while fully bypassed we skip the tape engine entirely.
    //  Input metering stays alive so the user can still see what is arriving.
    // -------------------------------------------------------------------------
    // Getters (never raw fields) so the reads are seq_cst-per-call but always
    // thread-consistent: `bypassParam->load() >= 0.5f && bypassParam->load() < 0.5f`
    // against the same atomic could otherwise straddle a host-side value change.
    const auto bypassRequested = [&]
    {
        auto* parameter = bypassParam;
        return parameter != nullptr && parameter->load() >= 0.5f;
    }();

    // DELTA listen is read HERE, ahead of the early return below, because the two
    // interact. Returning the untouched host buffer out of a fully bypassed engine
    // snapped the monitor from "only what the machine adds" back to the full dry
    // signal in a single block: with the blend below ignoring the bypass crossfade,
    // the output was the difference on the last block through the engine and the
    // raw input on the next one. A monitor that switches position with a step in the
    // waveform clicks exactly like any other step.
    //
    // `deltaEngaged` is the OR of the parameter, the ramp and the current value, so
    // the return is held off until the monitor has actually finished travelling.
    // While DELTA is still in the block the engine keeps running and the crossfade
    // down to silence further down does the rest - a bypassed machine makes no
    // difference, which is what the delta monitor has to show.
    const auto deltaListen = [&]
    {
        auto* parameter = deltaParam;
        return parameter != nullptr && parameter->load() >= 0.5f;
    }();
    const bool deltaEngaged = deltaListen
                           || deltaListenSmoothed.isSmoothing()
                           || deltaListenSmoothed.getCurrentValue() > 0.0f;

    if (bypassRequested && ! bypassSmoothed.isSmoothing() && bypassSmoothed.getCurrentValue() <= 0.0f
        && ! deltaEngaged)
    {
        bypassActive.store (true, std::memory_order_relaxed);

        float bypassPeak = 0.0f;
        double bypassSquares = 0.0;
        for (int channel = 0; channel < activeInputChannels; ++channel)
        {
            for (int sample = 0; sample < numSamples; ++sample)
            {
                // getSample() reads through the block, so the same code serves the
                // host-buffer path and the oversampler's internal-buffer path.
                const auto value = block.getSample (channel, sample);
                bypassPeak = juce::jmax (bypassPeak, std::abs (value));
                bypassSquares += static_cast<double> (value) * value;
            }
        }

        const auto bypassSamples = static_cast<double> (numSamples)
                                 * static_cast<double> (juce::jmax (1, activeInputChannels));
        const auto bypassRms = bypassSamples > 0.0
            ? static_cast<float> (std::sqrt (bypassSquares / bypassSamples)) : 0.0f;

        inputPeakLevel.store (bypassPeak, std::memory_order_relaxed);
        inputRmsLevel.store (bypassRms, std::memory_order_relaxed);
        outputPeakLevel.store (bypassPeak, std::memory_order_relaxed);
        outputRmsLevel.store (bypassRms, std::memory_order_relaxed);

        // While fully bypassed the plugin is transparent, so every loudness view reads
        // the dry signal and the clipping lamp reflects what is actually passing through.
        const auto bypassPeakDb = juce::Decibels::gainToDecibels (bypassPeak, -70.0f);
        const auto bypassRmsDb = juce::Decibels::gainToDecibels (bypassRms, -70.0f);
        outputPeakDb.store (bypassPeakDb, std::memory_order_relaxed);
        outputRmsDb.store (bypassRmsDb, std::memory_order_relaxed);
        outputLufs.store (bypassRmsDb, std::memory_order_relaxed);
        outputVuDb.store (bypassRmsDb, std::memory_order_relaxed);
        outputCombinedDb.store (bypassPeakDb * 0.25f + bypassRmsDb * 0.75f,
                                std::memory_order_relaxed);
        outputClipping.store (bypassPeak > 1.0f, std::memory_order_relaxed);

        // Bypassed, the input and output are the same signal, so the input meter reports
        // the same four-way reading.
        inputPeakDb.store (bypassPeakDb, std::memory_order_relaxed);
        inputRmsDb.store (bypassRmsDb, std::memory_order_relaxed);
        inputLufs.store (bypassRmsDb, std::memory_order_relaxed);
        inputVuDb.store (bypassRmsDb, std::memory_order_relaxed);
        inputCombinedDb.store (bypassPeakDb * 0.25f + bypassRmsDb * 0.75f,
                               std::memory_order_relaxed);
        inputClipping.store (bypassPeak > 1.0f, std::memory_order_relaxed);
        inputGainReductionDb.store (0.0f, std::memory_order_relaxed);
        outputGainReductionDb.store (0.0f, std::memory_order_relaxed);
        inputCompressorActivity.store (0.0f, std::memory_order_relaxed);
        outputCompressorActivity.store (0.0f, std::memory_order_relaxed);
        compressorActivity.store (0.0f, std::memory_order_relaxed);
        return;
    }

    bypassActive.store (false, std::memory_order_relaxed);

    const auto tapeType = static_cast<int> (tapeTypeParam->load());
    const auto speed = static_cast<int> (speedParam->load());
    const auto instrument = (instrumentParam != nullptr)
                                ? static_cast<int> (instrumentParam->load()) : 0;
    const auto drive = driveParam->load();
    const auto bias = biasParam->load();
    const auto tone = toneParam->load();
    const auto character = characterParam != nullptr ? characterParam->load() : 0.5f;
    const auto wow = wowParam->load();
    const auto flutter = flutterParam->load();
    const auto mix = (mixParam != nullptr ? mixParam->load() : 50.0f) * 0.01f;
    const auto outputDb = outputDbParam->load();
    const auto inputDb = inputDbParam->load();
    const auto stereoWidth = widthParam->load() * 2.0f;

    // -------------------------------------------------------------------------
    //  MODELED TRACKS: the geometry of the tape, read once per block.
    //
    //  Three consequences of the layout, all of them geometry rather than taste:
    //
    //    widthScale   how WIDE each track is. A stereo deck's two tracks each
    //                 get half the tape; a three-track deck's get a third. A
    //                 narrower track reads less low end per channel and a
    //                 proportionally higher noise floor for the same tape, so
    //                 this is the same number driving both.
    //
    //    crosstalk    how much of the OTHER channel leaks into this one, from
    //                 the head's fringing field. The closer the tracks, the
    //                 more - so a three-track deck leaks most and the 2+3
    //                 layout, with a whole track between the two channels,
    //                 leaks least.
    //
    //    spacingDelay how long the leakage takes to arrive, in samples. The
    //                 fringing path is short but it is not instant, and on a
    //                 multitrack the spacing between the tracks is what sets
    //                 it - which is why the leak is a tiny DELAY rather than an
    //                 instantaneous blend. It is a fraction of a millisecond in
    //                 every real layout, so it reads as thickening rather than
    //                 as an echo, which is exactly what track-to-track bleed is.
    // -------------------------------------------------------------------------
    const auto tracksIndex = tracksParam != nullptr
                                 ? juce::jlimit (0, 2, static_cast<int> (tracksParam->load())) : 0;

    //       2-track        2+3 (4-track used as 2)     3-track
    //       widest/tightest spacing,       widest spacing,        narrowest tracks,
    //       least crosstalk                least crosstalk        most crosstalk
    static constexpr float tracksWidthScale[3]   { 1.00f, 0.94f, 0.82f };
    static constexpr float tracksCrosstalk[3]    { 0.020f, 0.010f, 0.055f };
    static constexpr float tracksSpacingMs[3]    { 0.018f, 0.042f, 0.009f };
    static constexpr float tracksNoiseScale[3]   { 1.00f, 1.06f, 1.22f };

    const float trackWidthScale = tracksWidthScale[tracksIndex];
    const float trackCrosstalk = tracksCrosstalk[tracksIndex];
    const float trackNoiseScale = tracksNoiseScale[tracksIndex];

    // The bleed's own delay, in samples. A one-sample floor keeps the line valid
    // at every rate, and at any normal rate the figure is tens of samples - far
    // below the ear's ability to hear it as a separate event, which is the
    // point: it is an infinitesimal smear, not an echo. It is a hard limit that
    // the maximum spacing is well under, so the wrap below never loops.
    const int trackBleedDelaySamples = juce::jlimit (
        1, tracksBleedBufferLength - 2,
        juce::roundToInt (tracksSpacingMs[tracksIndex] * 0.001f * engineSampleRate));

    // Output-stage switches, read once per block: polarity is a pure sign flip on
    // whatever leaves the machine, and auto gain gates the slow programme
    // compensator (see the final gain compensation section below).
    const float polaritySign = (polarityParam != nullptr && polarityParam->load() >= 0.5f) ? -1.0f : 1.0f;
    const bool autoGainEnabled = autoGainParam == nullptr || autoGainParam->load() >= 0.5f;

    inputGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (inputDb));
    outputGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (outputDb));
    mixSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, mix));
    widthSmoothed.setTargetValue (stereoWidth);
    bypassSmoothed.setTargetValue (bypassRequested ? 0.0f : 1.0f);
    deltaListenSmoothed.setTargetValue (deltaListen ? 1.0f : 0.0f);

    // Delay, stereo offset, noise trim and transport state. All read once per block
    // and fed to smoothers, so none of them can step the signal.
    const auto delayMs = delayTimeParam != nullptr ? delayTimeParam->load() : 0.0f;
    const auto delayFeedback = delayFeedbackParam != nullptr ? delayFeedbackParam->load() : 0.0f;
    const auto delayPingPong = delayPingPongParam != nullptr ? delayPingPongParam->load() : 0.0f;
    const auto stOffsetUs = stOffsetParam != nullptr ? stOffsetParam->load() : 0.0f;
    const auto noiseAmount = noiseParam != nullptr ? noiseParam->load() : 0.5f;
    const auto noiseLvlAmount = noiseLvlParam != nullptr ? noiseLvlParam->load() : 1.0f;
    // The transport state is advanced by the engine too (START settles into PLAY),
    // so it is not const here: it is a local mirror of the parameter, written back
    // through it below when the spin-up completes.
    auto transportState = transportParam != nullptr
                              ? static_cast<int> (transportParam->load()) : 1;

    // -----------------------------------------------------------------------
    //  Tempo, from the host.
    //
    //  Read once per block from the playhead. `getPlayHead()` may return null (a
    //  host that does not offer one, or an offline render), and the position may
    //  be absent even when the playhead exists - so both are checked, and the
    //  last known tempo is kept when either is missing. Resetting to a default
    //  instead would make the delay jump every time the transport stopped.
    //
    //  getPosition() is called ONCE here rather than per parameter read, because
    //  it is a virtual call into the host and hosts are entitled to make it
    //  expensive.
    // -----------------------------------------------------------------------
    if (auto* hostPlayHead = getPlayHead())
    {
        if (auto position = hostPlayHead->getPosition())
        {
            if (auto bpm = position->getBpm())
            {
                // Guard against a host reporting nonsense during a seek or a
                // tempo ramp: a zero or negative tempo would divide by zero below.
                if (*bpm > 1.0 && *bpm < 1000.0)
                {
                    hostTempoBpm = *bpm;
                    hostTempoValid = true;
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    //  The delay time, free-running or tempo-locked.
    //
    //  In free mode it is the DELAY control in milliseconds, converted to samples
    //  at the ENGINE rate, so a 100 ms head spacing is 100 ms of tape travel
    //  whether the engine runs at the session rate or at 8x it.
    //
    //  In sync mode the time is derived from the host's tempo and the note value
    //  RATE selects. A quarter note at 120 BPM is 500 ms, which is the whole
    //  calculation: one beat is 60000/tempo ms, and the ratio scales it.
    // -----------------------------------------------------------------------
    const bool delaySyncOn = delaySyncParam != nullptr && delaySyncParam->load() >= 0.5f;
    const auto delayRateIndex = delayRateParam != nullptr
                                    ? static_cast<int> (delayRateParam->load()) : 2;

    float effectiveDelayMs = delayMs;

    if (delaySyncOn)
    {
        // The note value as a fraction of a whole note. A quarter is 1/4, a
        // dotted quarter is 1/4 * 1.5, a triplet quarter is 1/4 * 2/3.
        //
        //  1/1 = 1.0      1/2 = 0.5      1/4 = 0.25     1/8 = 0.125   1/16 = 0.0625
        //  1/4 T (triplet) = 0.25 * 2/3  1/8 T = 0.125 * 2/3
        //  1/4 D (dotted)  = 0.25 * 1.5
        static constexpr float noteRatios[] { 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f,
                                              0.25f * 2.0f / 3.0f,
                                              0.125f * 2.0f / 3.0f,
                                              0.25f * 1.5f };
        constexpr int numNoteRatios = static_cast<int> (std::size (noteRatios));

        const auto ratio = noteRatios[juce::jlimit (0, numNoteRatios - 1, delayRateIndex)];

        // A whole note is four beats, so a whole note lasts 4 * 60000/tempo ms.
        const auto wholeNoteMs = 4.0 * 60000.0 / juce::jmax (1.0, hostTempoBpm);
        effectiveDelayMs = static_cast<float> (wholeNoteMs * static_cast<double> (ratio));
    }

    delaySamplesSmoothed.setTargetValue (
        juce::jlimit (0.0f, static_cast<float> (juce::jmax (0, delayBufferLength - 1)),
                      effectiveDelayMs * 0.001f * engineSampleRate));
    delayFeedbackSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, delayFeedback));
    pingPongSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, delayPingPong));

    // ST OFFSET is given in microseconds and converted to samples the same way. Only
    // the magnitude is the read offset; the sign decides WHICH channel is delayed, so
    // a negative value lags the left channel instead of the right.
    const float stOffsetSamples = std::abs (stOffsetUs) * 1.0e-6f * engineSampleRate;
    stOffsetSamplesSmoothed.setTargetValue (
        juce::jlimit (0.0f, static_cast<float> (stOffsetBufferLength - 2), stOffsetSamples));
    const bool offsetRightChannel = stOffsetUs >= 0.0f;

    // -----------------------------------------------------------------------
    //  Saturation blend and the amp voicing.
    //
    //  All six are read once per block and fed to smoothers, so none of them can
    //  step the curve. The blend weights themselves are applied inside the core
    //  from the SMOOTHED values, per sample, so sweeping BLEND morphs the
    //  harmonics continuously rather than switching between six curves.
    // -----------------------------------------------------------------------
    const auto blendAmount = blendParam != nullptr ? blendParam->load() : 0.0f;
    const auto shapeAmount = shapeParam != nullptr ? shapeParam->load() : 0.5f;
    const auto sagAmount = sagParam != nullptr ? sagParam->load() : 0.0f;
    const auto presenceAmount = presenceParam != nullptr ? presenceParam->load() : 0.5f;
    const auto cabinetAmount = cabinetParam != nullptr ? cabinetParam->load() : 0.0f;
    const auto ampBiasAmount = ampBiasParam != nullptr ? ampBiasParam->load() : 0.5f;

    blendSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, blendAmount));
    shapeSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, shapeAmount));
    sagSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, sagAmount));
    presenceSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, presenceAmount));
    cabinetSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, cabinetAmount));
    ampBiasSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, ampBiasAmount));

    // -----------------------------------------------------------------------
    //  Transient shaper and neural stage.
    //
    //  Four controls, read once per block and fed to smoothers like the rest.
    //  The transient stage's two DETECTOR poles are the only rate-dependent
    //  numbers here, and they are block-rate constants for the same reason every
    //  other coefficient is: they cannot change within a block. The stage's own
    //  gain smoother is one of them, so it costs no exp() in the loop.
    // -----------------------------------------------------------------------
    const auto transientAttackAmount = transientAttackParam != nullptr
                                           ? transientAttackParam->load() : 0.0f;
    const auto transientSustainAmount = transientSustainParam != nullptr
                                            ? transientSustainParam->load() : 0.0f;
    const auto transientMixAmount = transientMixParam != nullptr
                                        ? transientMixParam->load() : 0.0f;
    const auto neuralMixAmount = neuralMixParam != nullptr ? neuralMixParam->load() : 0.0f;

    transientAttackSmoothed.setTargetValue (juce::jlimit (-1.0f, 1.0f, transientAttackAmount));
    transientSustainSmoothed.setTargetValue (juce::jlimit (-1.0f, 1.0f, transientSustainAmount));
    transientMixSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, transientMixAmount));
    neuralMixSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, neuralMixAmount));

    // ~2 ms transient window against a ~120 ms programme reference: the ratio of
    // the two is what the detector reads as an edge, so the gap between them is
    // the effect's whole time constant. 30 ms on the applied gain keeps the ride
    // a ride rather than a per-sample multiply of the envelope's own wobble.
    transientFastCoefficient = onePoleCoefficient (2.0f, engineSampleRate);
    transientSlowCoefficient = onePoleCoefficient (120.0f, engineSampleRate);
    transientGainCoefficient = onePoleCoefficient (30.0f, engineSampleRate);

    // -----------------------------------------------------------------------
    //  Preamp, distortion, tape condition, reverb, vinyl and the two modes.
    //
    //  All read once per block and fed to smoothers, so none of them can step
    //  the signal. The modes are read as plain bools because they change the
    //  MACHINE's voicing rather than a per-sample coefficient, and a mode is not
    //  something a user sweeps.
    // -----------------------------------------------------------------------
    const auto preampAmount = preampParam != nullptr ? preampParam->load() : 0.0f;
    const auto distortionAmount = distortionParam != nullptr ? distortionParam->load() : 0.0f;
    const auto fluxAmount = fluxParam != nullptr ? fluxParam->load() : 0.5f;
    const auto wearAmount = wearParam != nullptr ? wearParam->load() : 0.0f;
    const auto mechanicsAmount = mechanicsParam != nullptr ? mechanicsParam->load() : 0.0f;
    const auto reverbMix = reverbParam != nullptr ? reverbParam->load() : 0.0f;
    const auto reverbSize = reverbSizeParam != nullptr ? reverbSizeParam->load() : 0.4f;
    const auto vinylAmount = vinylParam != nullptr ? vinylParam->load() : 0.0f;
    const auto vinylCrackle = vinylCrackleParam != nullptr ? vinylCrackleParam->load() : 0.5f;
    const auto vinylRumble = vinylRumbleParam != nullptr ? vinylRumbleParam->load() : 0.35f;

    preampSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, preampAmount));
    distortionSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, distortionAmount));

    // -----------------------------------------------------------------------
    //  The DI box.
    //
    //  The pad is a CHOICE parameter (0 / -10 / -20 / -30 dB), so its raw value
    //  is the index and the dB it means is looked up in ONE place here. A pad is
    //  a switch, not a sweep, which is why it goes through no smoother - a
    //  smoothed pad would glide between two pads and briefly sit at a level that
    //  is neither.
    //
    //  The load and transformer coefficients are built from the engine rate, so
    //  the box behaves identically at every rate and at every oversampling
    //  factor - they are rate-dependent time constants like every other one in
    //  this file. The hum increment is the mains frequency the ground loop picks
    //  up (50 Hz), converted the same way.
    // -----------------------------------------------------------------------
    diSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, diParam != nullptr ? diParam->load() : 0.0f));
    diLoadSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f,
                                                 diLoadParam != nullptr ? diLoadParam->load() : 0.0f));
    diTransformerSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f,
                                                        diTransformerParam != nullptr
                                                            ? diTransformerParam->load() : 0.0f));

    {
        static constexpr float padDbValues[4] { 0.0f, -10.0f, -20.0f, -30.0f };
        const auto padIndex = diPadParam != nullptr
                                  ? juce::jlimit (0, 3, static_cast<int> (diPadParam->load())) : 0;
        diPadDb = padDbValues[padIndex];
    }

    // The load's own bandwidth: a very high input impedance is effectively
    // transparent (12 kHz at the neutral end), and a heavy load damps the
    // source's top octave. The LOAD control then scales this further inside the
    // stage, so the two together give the full range from "no load" to "heavy".
    diLoadCoefficient = onePoleCoefficientHz (12000.0f, engineSampleRate);
    // The DI transformer's own low split: small transformers bloom the bottom of
    // their band, and 200 Hz is where that starts being audible as "thickness".
    diTransformerCoefficient = onePoleCoefficientHz (200.0f, engineSampleRate);
    diHumIncrement = 50.0f / engineSampleRate;
    fluxSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, fluxAmount));
    wearSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, wearAmount));
    mechanicsSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, mechanicsAmount));
    reverbMixSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, reverbMix));
    reverbSizeSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, reverbSize));
    vinylSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, vinylAmount));
    vinylCrackleSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, vinylCrackle));
    vinylRumbleSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, vinylRumble));

    // The two mode switches. They are mutually exclusive BY DESIGN - a machine
    // cannot be both a modern deck and a deliberately degraded one - so MODERN
    // wins when both are set, which is the safer of the two to be wrong about.
    // The raw value of a bool parameter is a float, so it is tested against 0.5 -
    // exactly how `bypassParam` and `polarityParam` are read. Using it as a bool
    // directly would compile only because of an implicit conversion and would
    // read as "not zero", which happens to work here but hides the type.
    const bool modernRequested = modernModeParam != nullptr && modernModeParam->load() >= 0.5f;
    const bool lofiRequested = lofiModeParam != nullptr && lofiModeParam->load() >= 0.5f;
    modernMode = modernRequested;
    lofiMode = ! modernRequested && lofiRequested;

    // The delay character, cached as an int so the per-sample loop branches on a
    // register rather than reading an atomic.
    delayTypeCached = delayTypeParam != nullptr
                          ? static_cast<int> (delayTypeParam->load()) : 0;

    // The five type switches, cached for the same reason. Each is clamped to its
    // own list before it reaches the engine: a choice parameter should never
    // exceed its list, but a restored state that predates the parameter could
    // hand back anything, and every one of these indexes a static table.
    valveTypeCached = juce::jlimit (0, valveTypeCount - 1,
                          valveTypeParam != nullptr
                              ? static_cast<int> (valveTypeParam->load()) : 0);
    ampTypeCached = juce::jlimit (0, ampTypeCount - 1,
                        ampTypeParam != nullptr
                            ? static_cast<int> (ampTypeParam->load()) : 0);
    transformerTypeCached = juce::jlimit (0, transformerTypeCount - 1,
                                transformerTypeParam != nullptr
                                    ? static_cast<int> (transformerTypeParam->load()) : 0);
    digitalTypeCached = juce::jlimit (0, digitalTypeCount - 1,
                            digitalTypeParam != nullptr
                                ? static_cast<int> (digitalTypeParam->load()) : 0);
    vinylTypeCached = juce::jlimit (0, vinylTypeCount - 1,
                          vinylTypeParam != nullptr
                              ? static_cast<int> (vinylTypeParam->load()) : 0);

    // The vinyl type re-voices the two VinylStages, once per block. RIAA depth,
    // crackle and rumble multipliers, tick decay and groove hiss all belong to
    // the disc rather than to the controls, so they are written here and only
    // read in the loop.
    {
        // riaa, rumble, crackle, decay, surface
        static constexpr float voices[6][5] =
        {
            // Standard LP: the reference pressing. Full RIAA, quiet surface.
            { 0.45f, 1.00f, 0.60f, 0.99850f, 0.00f },
            // Single: less play per side, wider grooves, a little more top. The
            // rumble is lower - a single spins faster and the bearing sees less
            // time under the stylus.
            { 0.42f, 0.80f, 0.70f, 0.99820f, 0.00f },
            // Shellac 78: pre-vinyl. Shellac is abrasive, so the surface is
            // LOUD between every note, the groove hiss is continuous, the
            // rumble is a wind-up motor's, and there is no RIAA to undo - 78s
            // predate the curve, so the tilt here is the rougher blunt one.
            { 0.18f, 1.60f, 1.35f, 0.99700f, 1.00f },
            // Worn Classic: a well-loved record. The ticks multiply and ring,
            // the surface hiss rises, the top end has been played off.
            { 0.40f, 1.15f, 1.60f, 0.99880f, 0.35f },
            // Dubplate: soft lacquer, loud cut, played minutes after the cut.
            // Crackle is nearly absent and the RIAA is hot - the lathe was
            // pushed for a sound system.
            { 0.55f, 0.90f, 0.25f, 0.99900f, 0.05f },
            // Half-Speed Master: the cleanest pressing. The lathe tracked at
            // half speed, so the RIAA is deep and the surface is almost silent.
            { 0.55f, 0.70f, 0.30f, 0.99920f, 0.02f },
        };

        // VINYL TYPE = OFF: no DISC at all.
        //
        // The record is removed from the turntable, so what is left is the
        // PLAYBACK CHAIN with nothing in the groove: no RIAA, no crackle, no
        // rumble, no groove hiss, no surface character. The stage's own controls
        // (DUST, SCRATCH, WARP, CLICKS, ELECTRICAL) still work, because they
        // describe the record and the deck modelling rather than the pressing -
        // but with the disc off they act on a silent groove, so the faults that
        // need programme (dust, which follows the level) fall silent with it.
        //
        // It is the reference state for hearing what the DISC contributes.
        const bool vinylTypeOff = (vinylTypeCached == modelOffIndex);
        const auto& v = voices[juce::jlimit (0, 5, vinylTypeCached)];
        for (auto* stage : { &vinylL, &vinylR })
        {
            stage->riaaAmount    = vinylTypeOff ? 0.0f : v[0];
            stage->rumbleAmount  = vinylTypeOff ? 0.0f : v[1];
            stage->crackleAmount = vinylTypeOff ? 0.0f : v[2];
            stage->crackleDecay  = v[3];
            stage->surfaceNoise  = v[4];
        }

        // VINYL SPEED sets how much the disc's own speed error modulates the
        // stage - a slow wow on a 33, a tighter shimmer on a 45, and the wind-up
        // motor's wobble on a 78. Zero is a perfectly steady turntable. The
        // wander the type voices apply is folded on top of it, so a shellac at
        // speed 0 is steady but still shellac.
        const auto vinylSpeedNow = vinylSpeedParam != nullptr
            ? static_cast<int> (vinylSpeedParam->load()) : 0;
        static constexpr float wanderRate[3] = { 0.62f, 1.10f, 3.30f };   // Hz
        static constexpr float wanderDepth[3] = { 0.010f, 0.016f, 0.028f };
        for (auto* stage : { &vinylL, &vinylR })
        {
            stage->vinylWowIncrement = wanderRate[vinylSpeedNow] / engineSampleRate;
            // The turntable scales the depth (see the voicing below, which runs
            // after this and overwrites it with the scale applied) - the deck's
            // stability is what decides how much of the disc's wander you hear.
            stage->speedModulation = wanderDepth[vinylSpeedNow];

            // Every one of the four faults' rates is derived from the SAME platter
            // speed the wander uses, because they are all consequences of how fast
            // the disc turns: a scratch closes on the stylus once per revolution,
            // a warp lifts it once per revolution, and a 78's motor hums over a
            // winding that is not synchronised to a 33's at all. Deriving them from
            // one number is what makes the faults belong to the SAME record instead
            // of being four unrelated generators.
            const float revolutionHz = wanderRate[vinylSpeedNow];

            stage->scratchIncrement = revolutionHz / engineSampleRate;
            stage->warpIncrement = revolutionHz / engineSampleRate;

            // The dust's bandwidth is a real corner frequency - the grit sits above
            // the programme's own top end - converted the same way every other
            // coefficient here is. It also opens slightly with the platter speed,
            // because a faster stylus excites finer grit.
            stage->dustCoefficient = onePoleCoefficientHz (
                6000.0f + 2000.0f * static_cast<float> (vinylSpeedNow), engineSampleRate);

            // Mains hum is the one rate that is NOT the disc's: it is the supply's.
            // 50 Hz is the standard outside North America and 60 Hz inside it; the
            // engine uses the host's declared sample rate to place it and defaults
            // to 50 Hz, which is the more common supply and the lower, more audible
            // hum. The second harmonic is set from the same base so the two can
            // never lose their 2:1 relationship.
            constexpr float humHz = 50.0f;
            stage->humIncrement = humHz / engineSampleRate;
            stage->humIncrement2 = humHz * 2.0f / engineSampleRate;

            // The four amounts, read once per block like every other control. They
            // are squared inside the stage, so the control's travel is gentle at the
            // bottom (a little dust is a little dust) and the top end is where the
            // damage actually sounds like damage.
            stage->dustAmount = vinylDustParam != nullptr ? vinylDustParam->load() : 0.0f;
            stage->scratchAmount = vinylScratchParam != nullptr ? vinylScratchParam->load() : 0.0f;
            stage->warpAmount = vinylWarpParam != nullptr ? vinylWarpParam->load() : 0.0f;
            stage->electricalAmount = vinylElectricalParam != nullptr ? vinylElectricalParam->load() : 0.0f;

            // CLICKS rides the same platter-locked rate as the scratch and the
            // warp, because the periodic half of it IS a once-per-revolution
            // event: a pressing fault crossed by the stylus every turn.
            stage->clickAmount = vinylClicksParam != nullptr ? vinylClicksParam->load() : 0.0f;
            stage->clickIncrement = revolutionHz / engineSampleRate;

            // The split the GENERATION and CARTRIDGE tilts both act around: a
            // real playback-band boundary at 1.2 kHz, converted the same way
            // every other coefficient here is. One coefficient, two users, so
            // the two controls cannot disagree about where the boundary is.
            stage->cartridgeCoefficient = onePoleCoefficientHz (1200.0f, engineSampleRate);
        }

        // ------------------------------------------------------------------
        //  GENERATION, TURNTABLE and CARTRIDGE - the voicing of the three
        //  selectors. They are read once per block here, and the stage applies
        //  them per sample, exactly like the type voices above.
        // ------------------------------------------------------------------
        const auto generationIndex = vinylGenerationParam != nullptr
            ? juce::jlimit (0, 2, static_cast<int> (vinylGenerationParam->load())) : 0;
        const auto turntableIndex = vinylTurntableParam != nullptr
            ? juce::jlimit (0, 2, static_cast<int> (vinylTurntableParam->load())) : 0;
        const auto cartridgeIndex = vinylCartridgeParam != nullptr
            ? juce::jlimit (0, 2, static_cast<int> (vinylCartridgeParam->load())) : 0;

        // GENERATION: how much top end the cut and the pressing took, how much
        // bottom was left, and how much noise the process added. The three are
        // ordered from the reference cut to the copy of a copy.
        struct GenerationVoice { float topLoss; float bottomLift; float noise; };
        static constexpr GenerationVoice generationVoices[3]
        {
            // Laquer: the reference cut. Almost no loss, no extra noise.
            { 0.94f, 1.02f, 1.00f },
            // Direct: cut straight to metal. The most open top and the
            // quietest surface of the three - cleaner than the lacquer itself,
            // because one whole generation is missing.
            { 1.03f, 1.00f, 0.90f },
            // Printed: the pressing you actually buy. Every generation's loss,
            // a noticeably darker top, a fuller bottom and a noisier surface.
            { 0.80f, 1.08f, 1.35f },
        };

        // TURNTABLE: how much the drive wanders and how steadily it holds. A
        // belt is smooth but elastic, a direct drive is locked solid, an idler
        // couples the motor's own rumble into the platter.
        struct TurntableVoice { float wowScale; float stability; };
        static constexpr TurntableVoice turntableVoices[3]
        {
            { 1.10f, 1.15f },   // Belt: a little more wander, less steady
            { 0.55f, 0.70f },   // Direct: locked, the steadiest of the three
            { 1.35f, 1.55f },   // Idler: the least stable, coupling the drive
        };

        // CARTRIDGE: a top/bottom gain pair plus its own noise multipliers. The
        // moving coil is the bright, detailed one and carries the most hiss
        // because of its lower output; the DJ cart is the hot, heavier one with
        // the most surface noise.
        struct CartridgeVoice { float top; float bottom; float noise; float hiss; };
        static constexpr CartridgeVoice cartridgeVoices[3]
        {
            { 0.94f, 1.04f, 1.00f, 1.00f },   // MM: soft top, broad bottom
            { 1.12f, 0.96f, 1.05f, 1.30f },   // MC: bright, tight, more hiss
            { 1.05f, 1.12f, 1.30f, 1.10f },   // DJ: hot and heavy, more surface
        };

        const auto& generation = generationVoices[generationIndex];
        const auto& turntable = turntableVoices[turntableIndex];
        const auto& cartridge = cartridgeVoices[cartridgeIndex];

        for (auto* stage : { &vinylL, &vinylR })
        {
            stage->generationTopLoss = generation.topLoss;
            stage->generationBottomLift = generation.bottomLift;
            stage->generationNoiseScale = generation.noise;

            // The turntable scales the stage's own wander depth, so the same
            // VINYL SPEED setting wanders differently on each deck - which is
            // exactly what the control is for.
            stage->turntableWowScale = turntable.wowScale;
            stage->turntableStability = turntable.stability;

            stage->cartridgeTopGain = cartridge.top;
            stage->cartridgeBottomGain = cartridge.bottom;
            stage->cartridgeNoiseGain = cartridge.noise * generation.noise;
            stage->cartridgeHissGain = cartridge.hiss * generation.noise;

            // The turntable scales the wander the VINYL SPEED switch set, so the
            // deck's stability is what decides how much of it you actually hear.
            // Applied here rather than at the speed block because the voice is
            // only known once the selector has been read.
            stage->speedModulation *= turntable.stability;
            // And the WANDER RATE separately: a belt-driven platter does not
            // merely wander further, it wanders SLOWER, because the elastic belt
            // filters the motor's own speed variations. An idler is the opposite
            // - the wheel couples them through. Scaling the increment as well as
            // the depth is what makes the three decks three decks rather than
            // three amounts of the same wobble.
            stage->vinylWowIncrement *= turntable.wowScale;
        }
    }

    // The stage coefficients. Every frequency is converted with
    // onePoleCoefficientHz, so they mean the same thing at every sample rate.
    //   preamp low-cut  - the input transformer's roll-off, 40 Hz
    //   flux shelf      - the split the FLUX control lifts, 220 Hz
    //   wear loss       - the top-end the WEAR control removes, 6 kHz
    //   vinyl rumble    - the turntable's low-frequency floor, 30 Hz
    //   vinyl warmth    - the RIAA playback tilt's pivot, 400 Hz
    preampLowCutCoefficient = onePoleCoefficientHz (40.0f, engineSampleRate);
    fluxShelfCoefficient = onePoleCoefficientHz (220.0f, engineSampleRate);
    wearLossCoefficient = onePoleCoefficientHz (6000.0f, engineSampleRate);
    vinylRumbleCoefficient = onePoleCoefficientHz (30.0f, engineSampleRate);
    vinylWarmthCoefficient = onePoleCoefficientHz (400.0f, engineSampleRate);

    // The LO-FI mode's two constants. 3.2 kHz is the band limit - roughly a cheap
    // radio, and the most recognisable part of the mode - and the hold is four
    // samples, which is a 4x sample-rate reduction at any rate (it is expressed in
    // samples rather than in Hz so it stays the same reduction as the rate moves).
    lofiLowCoefficient = onePoleCoefficientHz (3200.0f, engineSampleRate);
    lofiHoldSamples = 4;

    // -------------------------------------------------------------------------
    //  The two equalisers.
    //
    //  The split coefficients are rebuilt here per block rather than only in
    //  prepareToPlay, because the engine's rate changes with the oversampling
    //  switch WITHOUT prepareToPlay running again - so an EQ prepared only once
    //  would have its 200 Hz and 4 kHz corners at the wrong frequencies the
    //  moment 4x was selected. One std::exp each per block is nothing.
    //
    //  The band gains come from the parameters, converted from dB to linear,
    //  and are fed to the smoothies so a drag glides. 0 dB converts to exactly
    //  1.0, which is what keeps a neutral EQ bit-for-bit transparent.
    // -------------------------------------------------------------------------
    inputEq.prepare (engineSampleRate);
    outputEq.prepare (engineSampleRate);

    // The two corners, orders and Q values, read once per block.
    //
    // The ORDER is a choice parameter, so its raw value is the INDEX into the
    // dB/octave list (0 = 6, 5 = 48), and the conversion to a slope happens in
    // ONE place here rather than in the filter - the filter wants a slope, the
    // panel wants a name, and this is the join between them.
    const auto eqOrderSlope = [] (std::atomic<float>* parameter) -> float
    {
        // Six entries: 6, 12, 18, 24, 36, 48. The list is not an arithmetic
        // series because 6, 12, 18, 24 are the useful everyday slopes and 36 and
        // 48 are the "get it out of the way" end - a straight 6..48 line would
        // spend three of its six positions on 30, 36 and 42, which nobody asks
        // for.
        static constexpr float slopes[6] { 6.0f, 12.0f, 18.0f, 24.0f, 36.0f, 48.0f };
        const auto index = parameter != nullptr
                               ? juce::jlimit (0, 5, static_cast<int> (parameter->load())) : 0;
        return slopes[index];
    };

    inputEq.prepareFilters (engineSampleRate,
                            inputEqHpFreqParam != nullptr ? inputEqHpFreqParam->load() : 20.0f,
                            inputEqLpFreqParam != nullptr ? inputEqLpFreqParam->load() : 20000.0f,
                            eqOrderSlope (inputEqOrderParam),
                            inputEqQParam != nullptr ? inputEqQParam->load() : 0.7f);
    // An explicit OFF on the switch wins over the corner's own travel-bypass.
    if (inputEqHpOnParam != nullptr && inputEqHpOnParam->load() < 0.5f)
        inputEq.hpActive = false;
    if (inputEqLpOnParam != nullptr && inputEqLpOnParam->load() < 0.5f)
        inputEq.lpActive = false;

    outputEq.prepareFilters (engineSampleRate,
                             outputEqHpFreqParam != nullptr ? outputEqHpFreqParam->load() : 20.0f,
                             outputEqLpFreqParam != nullptr ? outputEqLpFreqParam->load() : 20000.0f,
                             eqOrderSlope (outputEqOrderParam),
                             outputEqQParam != nullptr ? outputEqQParam->load() : 0.7f);
    if (outputEqHpOnParam != nullptr && outputEqHpOnParam->load() < 0.5f)
        outputEq.hpActive = false;
    if (outputEqLpOnParam != nullptr && outputEqLpOnParam->load() < 0.5f)
        outputEq.lpActive = false;

    const float inputEqMidCoefficient = inputEq.midBandCoefficient (engineSampleRate);
    const float outputEqMidCoefficient = outputEq.midBandCoefficient (engineSampleRate);

    inputEqLowSmoothed.setTargetValue (
        juce::Decibels::decibelsToGain (inputEqLowParam != nullptr ? inputEqLowParam->load() : 0.0f));
    inputEqMidSmoothed.setTargetValue (
        juce::Decibels::decibelsToGain (inputEqMidParam != nullptr ? inputEqMidParam->load() : 0.0f));
    inputEqHighSmoothed.setTargetValue (
        juce::Decibels::decibelsToGain (inputEqHighParam != nullptr ? inputEqHighParam->load() : 0.0f));
    outputEqLowSmoothed.setTargetValue (
        juce::Decibels::decibelsToGain (outputEqLowParam != nullptr ? outputEqLowParam->load() : 0.0f));
    outputEqMidSmoothed.setTargetValue (
        juce::Decibels::decibelsToGain (outputEqMidParam != nullptr ? outputEqMidParam->load() : 0.0f));
    outputEqHighSmoothed.setTargetValue (
        juce::Decibels::decibelsToGain (outputEqHighParam != nullptr ? outputEqHighParam->load() : 0.0f));

    // -----------------------------------------------------------------------
    //  MODERN mode: the machine re-voiced for a well-maintained 1990s deck.
    //
    //  It moves the COEFFICIENTS rather than the signal, because that is what
    //  actually differs about a modern machine: its head losses sit further out of
    //  the audio band, its floor is lower, and its magnetic memory is thinner
    //  because the tape is better. Applying these as block-rate scales means the
    //  mode changes the machine's calibration, not its level, so nothing else has
    //  to be recalibrated around it.
    // -----------------------------------------------------------------------
    modernHeadGapScale = modernMode ? 1.35f : 1.0f;
    modernHissScale = modernMode ? 0.35f : 1.0f;
    modernHysteresisScale = modernMode ? 0.75f : 1.0f;

    // The cabinet's poles. The roll-off sits where a 12-inch speaker's cone mass
    // puts it (about 5 kHz at the default cabinet setting, opening toward 9 kHz as
    // CABINET is turned down) and the resonance tracks the cabinet's own tuning,
    // which is the 80-120 Hz thump a closed box adds.
    const float cabinetHz = 9000.0f - cabinetAmount * 4000.0f;
    cabinetLowCoefficient = onePoleCoefficientHz (cabinetHz, engineSampleRate);
    // The resonance pole is deliberately much slower than the roll-off, so the
    // peak it adds is a broad lift rather than a narrow ring.
    cabinetPeakCoefficient = onePoleCoefficientHz (110.0f, engineSampleRate);
    // Presence is a high-pass split at 2.2 kHz: everything above it is the band the
    // feedback network lifts.
    presenceCoefficient = onePoleCoefficientHz (2200.0f, engineSampleRate);

    // The SAG envelope's two coefficients. Functions of the rate alone, so they are
    // block-rate constants like every other coefficient here - see the per-sample
    // loop, where rebuilding them was two std::exp per sample per channel.
    // 60 ms attack (the supply takes time to droop) and 240 ms release (it takes
    // longer to recover), which is the asymmetry that makes sag audible as "give"
    // rather than as a slow compressor.
    sagAttackCoefficient = onePoleCoefficient (60.0f, engineSampleRate);
    sagReleaseCoefficient = onePoleCoefficient (240.0f, engineSampleRate);

    // The safety limiter's four coefficients and its ceiling. All functions of the
    // rate alone - see the per-sample loop, where rebuilding them was four std::exp
    // per sample per channel.
    limiterDetectorAttack = onePoleCoefficient (0.5f, engineSampleRate);
    limiterDetectorRelease = onePoleCoefficient (80.0f, engineSampleRate);
    limiterCatchCoefficient = onePoleCoefficient (0.4f, engineSampleRate);
    limiterRecoveryCoefficient = onePoleCoefficient (120.0f, engineSampleRate);
    // The ceiling is a level, not a time constant, so it does not belong with the
    // four one-pole coefficients above - but it is set here so the hand-off point
    // and the coefficients that act on it are read from one place. It mirrors
    // softClip's knee, which is the only value that makes this stage's own comment
    // true, so that knee and this number have to move together: softClip declares
    // its knee as a function-local constexpr, so a shared constant would have to
    // be lifted out of a function the DSP harness cuts up on its own.
    limiterCeiling = 0.70f;

    // -------------------------------------------------------------------------
    //  Anti-phase guard coefficients.
    //
    //  Two windows, and the asymmetry between them is the whole behaviour:
    //
    //    - `antiPhaseCoefficient` is DELIBERATELY SLOW (about 400 ms). The guard
    //      must never react to a moment of genuine stereo - a hard-panned
    //      transient, a wide reverb tail - because those are real programme, not
    //      a fault. Only a SUSTAINED opposition is a fault, and 400 ms is long
    //      enough that a transient cannot trigger it and short enough that a
    //      genuinely inverted channel is corrected within a bar.
    //
    //    - the correction itself is applied through the same pole, so the rotation
    //      glides in and out rather than switching. A polarity flip that snapped
    //      would be a click, which is exactly the fault the guard exists to
    //      prevent.
    //
    // 400 ms, stated as a duration rather than as a reciprocal.
    antiPhaseCoefficient = onePoleCoefficient (400.0f, engineSampleRate);

    // The normaliser: the correlation is divided by a slow measure of the frame's
    // own energy, so the guard reads the RATIO of opposition to total energy rather
    // than an absolute number that would depend on the programme's level. It is
    // seeded here and tracked per frame inside the loop; the floor keeps a silent
    // passage from dividing by nothing.
    antiPhaseProductMagnitude = juce::jmax (1.0e-3f, antiPhaseProductMagnitude);

    // -------------------------------------------------------------------------
    //  Transport, rebuilt so the three states are genuinely different and START
    //  is a real transient.
    //
    //  STOP drives the ramp to 0. PLAY holds it at 1. START drives it to 1 over
    //  about a second and is then DONE - the state advances to PLAY when the ramp
    //  arrives (see the auto-advance just below, which runs on the audio thread
    //  and writes the parameter so the host and the panel see the same change).
    //
    //  Each transition picks its own ramp length, which the previous version only
    //  did for the spin-up case: a STOP from PLAY reused the last coefficient and
    //  therefore coasted down on a spin-up's timing.
    //
    //  The spindown hold OVERRIDES this and is handled with it: a spindown is a
    //  running state, so while it is held the transport is treated as running
    //  (the platter must not also be stopped) and the platter speed comes from
    //  the spindown ramp below.
    // -------------------------------------------------------------------------
    const bool spindownNow = spindownHeld.load (std::memory_order_relaxed);

    // Detect the release edge of the spindown hold. A spindown pressed from STOP
    // forces the platter to run (below), so on release the machine must settle into
    // PLAY rather than snapping back to silence - the gesture put the platter in
    // motion and releasing the power leaves it running. That is written to the
    // parameter, so the panel's keys and the host's lane follow.
    if (lastSpindownHeld && ! spindownNow && transportState == 0)
    {
        transportState = static_cast<int> (TransportState::play);
        lastTransportState = -1;   // force the switch below to re-arm the ramp

        if (auto* parameter = parameters.getParameter ("transport"))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (1.0f));
    }

    lastSpindownHeld = spindownNow;

    // The transport the engine should DRIVE toward, before the spindown mask.
    // A spindown forces a running transport so a held platter that was stopped
    // does not stay silent; released, the machine settles into PLAY and the
    // transport state is written to match.
    if (transportState != lastTransportState)
    {
        switch (transportState)
        {
            case 0:   // STOP - settle to rest. Quicker than a spin-up: a stopped
                      // capstan is braked, not coasting.
                transportRampCoefficient = onePoleCoefficient (350.0f, engineSampleRate);
                transportSpinningUp = false;
                break;

            case 2:   // START - the full spin-up, from wherever the ramp is.
                transportRampCoefficient = onePoleCoefficient (1000.0f, engineSampleRate);
                transportSpinningUp = true;
                break;

            case 1:   // PLAY - if we arrived here from START the ramp is already
                      // running and must keep its spin-up timing; if the user
                      // selected PLAY directly it is a short re-lock.
            default:
                if (! transportSpinningUp)
                    transportRampCoefficient = onePoleCoefficient (350.0f, engineSampleRate);
                break;
        }

        lastTransportState = transportState;
    }

    // -------------------------------------------------------------------------
    //  SPINDOWN: the momentary hold.
    //
    //  While held, the machine runs down like a turntable whose power has been
    //  cut. The coefficient is chosen for the run-down (~1.2 s, so the pitch
    //  slides audibly rather than stopping) and for the spin-back-up when the
    //  button is released (~0.5 s, so releasing it feels like power returning).
    //
    //  It multiplies the transport ramp rather than replacing it, so a spindown
    //  from PLAY runs down and a spindown from STOP does nothing - which is
    //  correct, because a stopped machine cannot slow further.
    //
    //  The transport target is forced to `running` while the hold is engaged, so
    //  a spindown pressed from STOP brings the platter up instead of doing
    //  nothing. That is what a DJ pressing the stop button on a deck expects: the
    //  record is under the head, so the gesture always has something to act on.
    // -------------------------------------------------------------------------
    const bool transportRunning = spindownNow || transportState != 0;
    const float transportTarget = transportRunning ? 1.0f : 0.0f;
    const float spindownTarget = spindownNow ? 0.0f : 1.0f;
    spindownCoefficient = spindownNow ? onePoleCoefficient (1200.0f, engineSampleRate)
                                      : onePoleCoefficient (500.0f, engineSampleRate);

    // -------------------------------------------------------------------------
    //  Analogue transfer curves.
    //
    //  These are deliberately NONLINEAR and must stay that way: they are the model of
    //  the machine, not a control taper. A tape stage bends gently at low levels and
    //  enters saturation hard as it is pushed, and that progressive bending is what
    //  produces the harmonics. Linearising them (as they briefly were) removes the
    //  analogue character entirely.
    //
    //  The knob taper is handled by the skewed NormalisableRange in
    //  createParameterLayout, so these power curves are free to be the sound-shaping
    //  functions they should be.
    // -------------------------------------------------------------------------
    const auto driveCurve = std::pow (drive, 1.45f);
    const auto biasCurve = std::pow (bias, 1.30f);
    const auto wowCurve = std::pow (wow, 1.55f);
    const auto flutterCurve = std::pow (flutter, 1.45f);

    // MIX carries no shaping curve of its own: its smoothed value IS the blend
    // position, converted to the raised-cosine dry/wet gains per sample below. Any
    // extra curve on it would only make the blend disagree with its own readout.
    const auto twoPi = juce::MathConstants<float>::twoPi;
    const auto speedScale = (speed == 0) ? 0.76f : (speed == 1) ? 1.0f : 1.34f;

    // The wow rate is tempo-locked. A machine's speed irregularities read as
    // musical when they land on the track's own timing, so the LFO is anchored to
    // the host tempo - one wander cycle per BAR at WOW's centre - and the WOW
    // control sweeps around that anchor instead of over an absolute rate. At 120
    // BPM the centre is 0.5 Hz; double the tempo and the machine wanders twice as
    // fast, which is what playing along with a faster track means for a deck.
    // Without a host tempo the 120 BPM default keeps the machine where it was.
    const auto barsPerSecond = static_cast<float> (hostTempoBpm) / 240.0f;
    const auto tempoWowAnchor = juce::jlimit (0.10f, 3.0f, barsPerSecond);
    const auto wowFreq = tempoWowAnchor * (0.30f + wowCurve * 1.40f) * speedScale;
    const auto flutterFreq = (1.9f + flutterCurve * 5.4f) * (1.0f + speedScale * 0.22f);

    // The TONE macro's blend curve, computed once per block. Declared BEFORE the tape
    // character section because the machine-state crossfade below is built from it:
    // 0 % reads as the classic slow stock, 100 % as the hot fast stock.
    const auto characterCurve = std::pow (
        juce::jlimit (0.0f, 1.0f, character), 1.20f);

    // Per-model tape character: saturation curve, bias asymmetry, tape noise floor,
    // head-gap damping and the magnetic hysteresis thickness. The damping is a real
    // corner FREQUENCY in Hz (the previous ms values landed at 4-9 Hz through the
    // time-constant filter - the same 2*pi unit error that hid the whole wet path).
    //
    // TONE (the character macro) is a crossfade BETWEEN MACHINE STATES, and the tape
    // formula is part of that state: at 0 % the machine behaves like the classic slow
    // stock (gentle curve, strong asymmetry, warm hiss, dark damping, thick hysteresis
    // blend) and at 100 % like the fast/hot stock (harder curve, less asymmetry, less
    // hiss, more open damping, leaner hysteresis). The selected TAPE TYPE shifts the
    // centre of that fade, so the knob still has its own meaning on top of the macro.
    // The slow-machine end is represented by the J37 numbers and the fast end by a
    // hotter generic stock, blended by the TONE curve.
    const float slowMachineCurve = 1.18f;      // J37 - soft magnetic bend
    const float slowMachineAsymmetry = 0.16f;  // strong even-harmonic warmth
    const float slowMachineHiss = 0.10f;       // quiet oxide floor (below audibility)
    const float slowMachineDampingHz = 12000.0f;
    const float slowMachineHysteresis = 0.30f;

    const float fastMachineCurve = 1.44f;      // hot-stud style dense saturation
    const float fastMachineAsymmetry = 0.10f;  // leaner, more symmetric bend
    const float fastMachineHiss = 0.06f;       // quieter still, faster stock
    const float fastMachineDampingHz = 20000.0f;
    const float fastMachineHysteresis = 0.44f;

    const auto machineBlend = juce::jlimit (0.0f, 1.0f, characterCurve);
    const auto blend = [machineBlend] (float slow, float fast)
    {
        return slow + (fast - slow) * machineBlend;
    };

    float tapeCurve = blend (slowMachineCurve, fastMachineCurve);
    float tapeAsymmetry = blend (slowMachineAsymmetry, fastMachineAsymmetry);
    float tapeHiss = blend (slowMachineHiss, fastMachineHiss);
    float headDampingHz = blend (slowMachineDampingHz, fastMachineDampingHz);
    float hysteresis = blend (slowMachineHysteresis, fastMachineHysteresis);

    // The TAPE TYPE switch keeps its own voice on top of the TONE macro: it biases the
    // blended state toward that formula's character (hotter formulas bend harder and
    // hiss less, the classic J37 stays soft) rather than replacing it.
    //
    // This switch is the third copy of the stock list and the only one the compiler
    // cannot check: C++ has no way to count case labels, so nothing here ties the
    // cases to tapeStockNames. Without an assert, a stock added to the table and
    // forgotten here would fall through to default and sound exactly like stock 0 -
    // a wrong tape rather than an error, which is the worst kind. tapeStockCount is
    // the same constant the parameter and the panel read, so this fires in a debug
    // build on the first block whenever the three stop agreeing.
    jassert (tapeType >= 0 && tapeType < tapeStockCount);

    // -------------------------------------------------------------------------
    //  TAPE TYPE = OFF: the magnetic medium is removed.
    //
    //  What is left is a solid-state amplifier: no hysteresis, no oxide floor,
    //  no head damping, no bias asymmetry - the machine's own electronics with
    //  no tape in it. It is the reference state for hearing what the MEDIUM
    //  contributes, because everything else in the chain is untouched by it.
    //
    //  It is implemented by (a) telling the saturation core that the tape
    //  principle is absent, so the blend redistributes its share among the
    //  principles still present, and (b) skipping the stock voicing switch
    //  entirely, which is what the early flag below does. BOTH are needed:
    //  the core alone would still have the stock's hiss and head damping riding
    //  on top of a tape-less blend.
    // -------------------------------------------------------------------------
    const bool tapeModelOff = (tapeType == modelOffIndex);
    saturationL.tapeOff = tapeModelOff;
    saturationR.tapeOff = tapeModelOff;

    if (tapeModelOff)
    {
        // A solid-state machine: no medium, so no medium noise and no head-gap
        // damping. The transport still runs - the electronics are still moving
        // the tape past a head - so wow and flutter stay, which is exactly the
        // distinction that makes this a useful reference rather than a bypass.
        tapeHiss = 0.0f;
        headDampingHz = 26000.0f;
        hysteresis = 0.10f;
        tapeAsymmetry = 0.0f;
    }
    else
    switch (tapeType)
    {
        case 0: // J37 - the classic EMI reference sound
            break;
        case 1: // Ampex 456 - hotter, more low-order colour
            tapeCurve += 0.08f;
            tapeAsymmetry += 0.05f;
            tapeHiss += 0.03f;
            headDampingHz += 2500.0f;
            hysteresis += 0.06f;
            break;
        case 2: // Studer A800 - darkest, densest saturation
            tapeCurve += 0.16f;
            tapeAsymmetry += 0.08f;
            tapeHiss += 0.06f;
            headDampingHz += 5000.0f;
            hysteresis += 0.12f;
            break;
        case 3: // Chrome - clean and bright, low noise
            tapeCurve += 0.02f;
            tapeAsymmetry -= 0.03f;
            tapeHiss -= 0.02f;
            headDampingHz -= 2500.0f;
            hysteresis -= 0.04f;
            break;
        case 4: // Type 111 - gentle low-noise mastering stock, very quiet, soft top
            tapeCurve -= 0.05f;
            tapeAsymmetry -= 0.02f;
            tapeHiss -= 0.045f;
            headDampingHz -= 4000.0f;
            hysteresis -= 0.05f;
            break;
        case 5: // GP9 - hot modern mastering formula: dense low end, higher floor
            tapeCurve += 0.14f;
            tapeAsymmetry += 0.07f;
            tapeHiss += 0.075f;
            headDampingHz += 6000.0f;
            hysteresis += 0.11f;
            break;
        case 6: // Quantegy 499 - high-output studio workhorse: open top, firm glue
            tapeCurve += 0.10f;
            tapeAsymmetry += 0.04f;
            tapeHiss += 0.045f;
            headDampingHz += 8000.0f;
            hysteresis += 0.08f;
            break;
        case 7: // RTM SM911 - broadcast reference: balanced, smooth, low noise
            tapeCurve += 0.05f;
            tapeAsymmetry += 0.02f;
            tapeHiss -= 0.01f;
            headDampingHz += 3500.0f;
            hysteresis += 0.03f;
            break;
        case 8: // SM 468 - high-output low-noise studio stock. A firm bend with a
                // notably quiet floor and an open head, so it reads as clean density
                // rather than as colour.
            tapeCurve += 0.09f;
            tapeAsymmetry += 0.03f;
            tapeHiss -= 0.055f;
            headDampingHz += 7000.0f;
            hysteresis += 0.04f;
            break;
        case 9: // 888 - the hot, thick vintage stock. Strong bias asymmetry and the
                // thickest magnetic memory here, so it bends early and blooms hard:
                // the formula to reach for when the saturation IS the effect.
            tapeCurve += 0.19f;
            tapeAsymmetry += 0.10f;
            tapeHiss += 0.05f;
            headDampingHz -= 1500.0f;
            hysteresis += 0.14f;
            break;
        case 10: // 815 - dark, dense and quiet at the top. A soft head and a heavy
                 // low-mid bias make it the warmest of the new stocks without the
                 // extra hiss 888 brings: it thickens rather than drives.
            tapeCurve += 0.13f;
            tapeAsymmetry += 0.08f;
            tapeHiss -= 0.02f;
            headDampingHz -= 4500.0f;
            hysteresis += 0.12f;
            break;
        case 11: // 811 - the clean, open, low-noise mastering stock. The gentlest bend
                 // of the set with the most open head, so it stays transparent under
                 // level and keeps the top octave: a bus stock, not a colour.
            tapeCurve -= 0.02f;
            tapeAsymmetry += 0.01f;
            tapeHiss -= 0.05f;
            headDampingHz += 9000.0f;
            hysteresis -= 0.03f;
            break;
        default:
            // Unreachable while the assert above holds: the choice parameter is
            // clamped to tapeStockCount, and the switch covers every index below it.
            // Present so the switch is exhaustive and a future entry cannot silently
            // do nothing.
            jassertfalse;
            break;
    }

    // The INSTRUMENT selector's voicing rides on top of the TAPE TYPE bias, and the
    // clamps below bound every figure, so any combination of the two selectors stays
    // inside the machine's designed operating range.
    float instrumentTransportScale = 1.0f;   // how loose the transport runs

    switch (instrument)
    {
        case 1: // Vocal: gentler bend, more even warmth, quiet floor, steady transport
            tapeCurve -= 0.10f;
            tapeAsymmetry += 0.04f;
            tapeHiss -= 0.03f;
            headDampingHz -= 1000.0f;
            hysteresis += 0.05f;
            instrumentTransportScale = 0.80f;
            break;
        case 2: // Bass: the thickest bend and memory, the darkest head, the steadiest
                // transport - pitch wobble under a bass note is heard at once
            tapeCurve += 0.12f;
            tapeAsymmetry += 0.06f;
            tapeHiss += 0.02f;
            headDampingHz -= 2500.0f;
            hysteresis += 0.10f;
            instrumentTransportScale = 0.65f;
            break;
        case 3: // Guitar: cleaner bend, more bite, a slightly loose vintage transport
            tapeCurve -= 0.06f;
            tapeAsymmetry -= 0.02f;
            tapeHiss -= 0.01f;
            headDampingHz += 1500.0f;
            hysteresis -= 0.04f;
            instrumentTransportScale = 1.15f;
            break;
        case 4: // Piano: the cleanest bend so transients and decay stay honest, the
                // quietest floor, top just nudged open
            tapeCurve -= 0.12f;
            tapeAsymmetry += 0.02f;
            tapeHiss -= 0.04f;
            headDampingHz += 500.0f;
            hysteresis -= 0.06f;
            instrumentTransportScale = 0.85f;
            break;
        case 5: // Drums: the slam calibration - a harder bend and an open head for
                // the crack, tight magnetic memory so transients do not smear, and
                // a steady transport (wow under a kick drum reads as a fault)
            tapeCurve += 0.10f;
            tapeAsymmetry -= 0.04f;
            tapeHiss += 0.01f;
            headDampingHz += 3000.0f;
            hysteresis -= 0.08f;
            instrumentTransportScale = 0.90f;
            break;
        case 0: // Master Bus: the machine exactly as calibrated
        default:
            break;
    }

    tapeCurve = juce::jlimit (1.0f, 1.8f, tapeCurve);
    tapeAsymmetry = juce::jlimit (0.0f, 0.4f, tapeAsymmetry);
    tapeHiss = juce::jlimit (0.0f, 0.6f, tapeHiss);
    headDampingHz = juce::jlimit (4000.0f, 26000.0f, headDampingHz);
    hysteresis = juce::jlimit (0.1f, 0.7f, hysteresis);

    // -------------------------------------------------------------------------
    //  Tape-type change.
    //
    //  Detected once per switch so the shaper's memory can be released at exactly
    //  that moment, and so the head-damping pole can be moved on the slow ramp instead
    //  of the 20 ms control ramp. See the note on activeTapeType in the header for why
    //  ramping the coefficients alone cannot make this continuous: the hysteresis term
    //  feeds the previous output back through a non-linear function, so stale memory
    //  inside a new curve is a step no amount of coefficient smoothing can remove.
    //
    //  The memory is faded rather than zeroed. Writing zeros would itself be a step -
    //  the curve would be evaluated against silence for one frame. Halving the three
    //  history slots lets the state unwind into the new curve over a few samples
    //  instead of jumping, which is inaudible at any level.
    // -------------------------------------------------------------------------
    const bool tapeTypeChanged = (activeTapeType != tapeType);

    if (tapeTypeChanged)
    {
        activeTapeType = tapeType;

        for (auto& memory : hystL) memory *= 0.5f;
        for (auto& memory : hystR) memory *= 0.5f;

        // Hold the slow ramp open long enough to cover the whole travel. The countdown
        // is in samples, so it scales with the rate like every other time constant.
        tapeTypeChangeCountdown = juce::roundToInt (engineSampleRate * 0.15f);
    }

    // The switch ramp is only consulted while its countdown is running. Outside that
    // window the ordinary control ramp owns the pole, so moving a knob keeps its 20 ms
    // response and only a formula change pays for the slower one.
    const bool tapeTypeSwitching = tapeTypeChangeCountdown > 0;

    if (tapeTypeSwitching)
        tapeTypeChangeCountdown -= numSamples;

    // DRIVE has no floor. It used to start at 0.28, which meant the signal was pushed
    // 38 % harder into the saturator even with the control at zero - a large part of why
    //    the plugin sounded overdriven at every setting. Now zero drive means unity gain
    // into the record head, so the machine is clean until the control asks it not to be.
    //
    // Compressor-coupled saturation: the smoothed squeeze of both glue stages (updated
    // once per block, see the end of processBlock) adds drive on top of the DRIVE
    // control. The harder the compressors work, the hotter the record head is run and
    // the harder the tape saturates - how a compressed signal hits a real machine.
    const float driveAmount = driveCurve * 1.9f + squeezeSaturationDrive * 1.15f;
    const float biasAmount = 0.18f + biasCurve * 1.55f;

    // Dry/wet blend. MIX is a genuine crossfade: at 0 % the signal is untouched dry
    // and at 100 % it is fully through the tape path. Previously the wet side had a
    // 12 % floor and the dry side was never fully removed, so MIX could not reach a
    // clean bypass or a fully saturated signal and its travel felt dead at the ends.
    // The gains themselves are derived per sample further down, from the smoothed MIX.
    const float wowDepth = wowCurve * (0.05f + speedScale * 0.08f)
                           * instrumentTransportScale;
    const float flutterDepth = flutterCurve * (0.08f + speedScale * 0.09f)
                               * instrumentTransportScale;
    const float speedBias = 0.84f + speedScale * 0.30f;

    // -------------------------------------------------------------------------
    //  Device couplings: where one control changes what another one MEANS.
    //
    //  A machine is not a list of independent knobs. A worn capstan makes the wow
    //  deeper at the same WOW setting; a harder-driven head makes its own hiss
    //  louder; a dark head gap hides modulation; a spent platter loses speed before
    //  it loses signal. These are the interactions that make a tape model feel like
    //  one object instead of a chain of processors, and every one of them is a
    //  plain product of two already-computed signals - no extra oscillator, no
    //  extra state, nothing to keep in sync.
    //
    //  Each coupling is deliberately ASYMMETRIC and bounded: the couplings can only
    //  add, never invert, so a control at zero always means zero and the machine
    //  cannot be talked into a state its design does not have.
    // -------------------------------------------------------------------------
    const auto mechanismWear = juce::jlimit (0.0f, 1.0f,
                                             mechanicsSmoothed.getCurrentValue()
                                                 + wearSmoothed.getCurrentValue());

    // WEAR and MECHANICS deepen the transport's own wander. Both are already folded
    // into the per-sample modulation; this is the part they own on their own, so a
    // worn machine wanders even with WOW and FLUTTER closed - which is the one case
    // where the transport gate below is wrong to silence it, and is why this term is
    // added AFTER the gate is taken.
    const float wearWowCoupling = mechanismWear * 0.006f;

    // A hot record head is a noisier head: the hiss floor rises with the drive that
    // is actually reaching the tape, not with the DRIVE control alone, so it follows
    // the compressor-coupled driveAmount computed above.
    const float driveHissCoupling = juce::jlimit (0.0f, 1.0f, driveAmount * 0.35f);

    // A dark head gap hides the fine modulation before it hides anything else, so
    // the top of the TONE travel rolls the grain and the flutter off together. It
    // reads the TONE control directly rather than the cached toneLpAc coefficient:
    // that coefficient is only recomputed when tone or character actually move, so
    // on the first block it can still be 0 and would dampen the modulation before
    // the knob had been touched. The 0.55 floor means TONE can never silence the
    // transport's own movement, only soften it - which is what a biased head does
    // to pitch shimmer.
    const float toneNorm = juce::jlimit (0.0f, 1.0f, tone);
    const float headGapModulationMask = juce::jlimit (0.55f, 1.0f, 0.55f + toneNorm * 0.45f);

    // A plunger that is slowing loses speed before it loses signal. The whole point
    // of SPINDOWN is that the pitch goes first, so the platter speed is mapped
    // through a curve that stays near 1 until the platter is well below speed and
    // then falls away quickly - an exponent above 1 would do the opposite and make
    // the very start of the run-down audible as a fade. The value is applied in the
    // per-sample loop, where the platter speed actually lives.
    constexpr float platterPitchExponent = 1.35f;

    // A hard-driven machine also runs its transport more loosely: pushing a deck
    // that hard is a physical load, and the speed error is the same term the ramp
    // uses, read here as a small extra wander.
    const float loadInducedSpeedError = driveAmount * 0.004f;

    // The transport activity gate: 0 when both Wow and Flutter are closed, 1 from
    // roughly 50 % on either. The machine only makes a sound of any kind while its
    // transport moves, so EVERYTHING the transport does is gated by this one factor:
    // the tape-surface grain in the loop below (which used to run unconditionally
    // and read as a mystery noise generator whenever both controls were closed),
    // and - since the "hiss while paused" report - the noise floor as well, whose
    // gain is multiplied by it further down. Nothing in the engine is always-on.
    const float transportActivityGate = juce::jlimit (0.0f, 1.0f, (wowCurve + flutterCurve) * 2.0f);

    // Tone tilt: 0 = warm/soft, 1 = open/bright. The coefficients are cached by
    // updateToneCoefficients, which also derives the TONE-macro machine-state scalars
    // (head gap, pre-bias, flutter scale). They are rebuilt whenever either control
    // moves, and also whenever the sample rate changes (see
    // resetSampleRateDependentState).
    if (std::abs (tone - previousTone) > 1.0e-5f
        || std::abs (character - previousCharacter) > 1.0e-5f)
        updateToneCoefficients (tone, engineSampleRate);

    // Feed the ramps from the cached coefficients once per block; the per-sample loop
    // then reads getCurrentValue(), which advances the ramp one sample at a time. Only
    // these two gains are ramped - every other control either feeds a memoryless curve
    // (DRIVE, BIAS), an oscillator (WOW, FLUTTER) or an already-smoothed value
    // (INPUT, OUTPUT, MIX, WIDTH, BYPASS), none of which can step the waveform.
    toneShelfGainSmoothed.setTargetValue (toneShelfGain);
    toneShelfBoostSmoothed.setTargetValue (toneShelfBoost);
    preDriveGainSmoothed.setTargetValue (preDriveGain);

    // The magnetic curve's own two arguments ramp for the same reason: BIAS shifts the
    // whole transfer curve, so a per-block step is a hard discontinuity, not a zipper.
    const auto shaperDriveTarget = driveCurve * tapeCurve + hysteresis * 0.25f
                                     + squeezeSaturationDrive * 0.30f;
    const auto shaperAsymmetryTarget = tapeAsymmetry * (biasAmount * 0.42f);
    shaperDriveSmoothed.setTargetValue (shaperDriveTarget);
    shaperAsymmetrySmoothed.setTargetValue (shaperAsymmetryTarget);

    // The TONE curve used by the tilt stage below is the one computed above the tape
    // character section, so the head poles, the machine crossfade and the tilt all
    // read the same value.

    // The playback head poles are constants for the whole block (they only depend on
    // the controls and the rate), so they are built here once rather than per sample.
    // The head-gap pole is the TONE macro's own crossfade - slow/soft machine (4.3 kHz)
    // to fast/open machine (24 kHz) - divided by the selected speed's damping, so SPEED
    // and TONE keep their independent meaning on the same head.
    // MODERN mode opens the head losses up: the gap and damping corners move
    // further out of the audio band, which is what a better-maintained machine
    // actually does. The scale is 1.0 when the mode is off, so the tape machine is
    // bit-for-bit unchanged.
    const float headGapCoefficient = onePoleCoefficientHz (headGapHz * speedScale * modernHeadGapScale,
                                                           engineSampleRate);
    const float hfPostCoefficient = onePoleCoefficientHz (headDampingHz * speedScale * modernHeadGapScale,
                                                          engineSampleRate);

    // Tape hiss is a continuous noise floor, so its density is expressed per sample and
    // therefore scales with the sample rate.
    //
    // Two things are needed for this to stay correct up to 192 kHz:
    //
    //   1. The noise is BAND-LIMITED by the coefficient below rather than running white all
    //      the way to Nyquist. Real tape hiss comes from the medium and stops well short of
    //      the top octave; left white it would spread over 96 kHz at a 192 kHz rate, which
    //      sounds like a bright digital hiss instead of tape. Filtering it also means the
    //      noise occupies a fixed bandwidth at every rate, which is what makes point 2 work.
    //
    //   2. Because the bandwidth is now fixed, the gain no longer needs the sqrt(rate)
    //      compensation - that term existed only to keep total white-noise energy constant
    //      as more samples were added per second. Adding it on top of the band limit would
    //      double-compensate and the hiss would get louder as the rate went up, which is
    //      exactly the bug this replaces.
    // MODERN mode lowers the floor, which is the single most obvious difference
    // between an old machine and a well-kept one. 1.0 when the mode is off.
    // NOISE LVL is the trim that acts on the floors themselves. NOISE is the
    // mix of every noise source; this is the level both floors share. It rides
    // the same smoother family, so a drag glides rather than steps.
    const float hissGain = tapeHiss * 0.00042f * modernHissScale
                         * noiseLvlSmoothed.getCurrentValue()
                         // A narrower track has a proportionally higher noise
                         // floor for the same tape, because the signal it carries
                         // is smaller and the medium's noise is not. This is the
                         // same geometric fact that costs the low end below -
                         // one cause, two consequences.
                         * trackNoiseScale;

    // The floor is gated by the transport. Tape hiss exists only while the tape is
    // actually MOVING across the head: a machine at rest is silent, because the
    // oxide never passes the playback gap. When both Wow and Flutter are closed the
    // machine is at rest and the floor coasts down to silence; opening either
    // control spins it back up. The fade itself is the hiss ramp's own slow window
    // (750 ms, see resetSampleRateDependentState), so a stop between takes reads as
    // the machine coasting to rest rather than a mute-switch drop. This is the fix
    // for the "noise while paused with Wow and Flutter at zero" report: the floor
    // was the only ungated always-on source left in the engine.
    //
    // driveHissCoupling rides on top of that: a head driven harder is a noisier head,
    // so the floor rises with the drive actually reaching the tape. It is added to the
    // gate rather than to the gain, so a machine whose transport is at rest still
    // cannot hiss - the coupling only makes a moving machine louder, which is the
    // physical thing it models.
    const float gatedHissGain = hissGain * juce::jlimit (0.0f, 1.0f,
                                                        transportActivityGate + driveHissCoupling * 0.6f
                                                            * transportActivityGate);

    // The two slow random sources - the MECHANICS drift and the WEAR contact
    // noise - are advanced ONCE per block, not per sample. Both are sub-audio
    // (the drift targets change every 120 ms, the contact noise follows over about
    // half a second), so a block-rate update is not an approximation, it is the
    // correct rate: a per-sample update would compute the same number a thousand
    // times and then use it once.
    //
    // They share the tape noise generator rather than carrying their own, because
    // a second LCG would double the state for no benefit - the two sources are
    // already decorrelated by being sampled at different moments.
    const int blockSamples = juce::jmax (1, numSamples);
    tapeConditionL.updateSlowSources (mechanicsSmoothed.getCurrentValue(),
                                      wearSmoothed.getCurrentValue(),
                                      blockSamples, noiseState);
    tapeConditionR.updateSlowSources (mechanicsSmoothed.getCurrentValue(),
                                      wearSmoothed.getCurrentValue(),
                                      blockSamples, noiseState);

    // The NOISE control trims the floor on top of the formula's own figure: 0.5 is
    // unity, so the knob starts neutral and the stock's own character is unchanged
    // unless the user asks for a different floor.
    noiseTrimSmoothed.setTargetValue (juce::jlimit (0.0f, 2.0f, noiseAmount * 2.0f));
    noiseLvlSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, noiseLvlAmount));

    // ST LINK: 1.0 is fully linked (one shared detector per stage), 0.0 is fully
    // unlinked (two independent detectors). Ramped, because it scales the gain
    // difference between the two paths and a step there is a step in the waveform.
    const auto stLinkAmount = stLinkParam != nullptr ? stLinkParam->load() : 1.0f;
    stLinkSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, stLinkAmount));

    // Every control-derived coefficient the block needs is in scope by now, so the
    // ramps are fed once here and read per sample with getCurrentValue(): no
    // multiplier and no pole in the wet path can step from one block to the next.
    driveAmountSmoothed.setTargetValue (driveAmount);
    toneLpSmoothed.setTargetValue (toneLpAc);
    hfPostSmoothed.setTargetValue (hfPostCoefficient);
    headGapSmoothed.setTargetValue (headGapCoefficient);
    flutterScaleSmoothed.setTargetValue (flutterScale);

    // The switch ramp tracks the same pole but over a much longer window. It is only
    // read while tapeTypeSwitching is true, so feeding it every block costs nothing
    // when no formula change is happening.
    headDampingSwitchSmoothed.setTargetValue (hfPostCoefficient);
    hissGainSmoothed.setTargetValue (gatedHissGain);
    subFundamentalSmoothed.setTargetValue (subFundamentalParam != nullptr
                                               ? subFundamentalParam->load() : 0.0f);

    // -----------------------------------------------------------------------
    //  Noise floor.
    //
    //  The hiss is a constant, band-limited floor - the sound of the medium. It is
    //  deliberately NOT programme-dependent: tracking the signal would make the
    //  noise breathe with the music, and the hiss must sound the same in a pause as
    //  under the programme.
    // -----------------------------------------------------------------------

    // onePoleCoefficient takes MILLISECONDS, so the 16 kHz corner is converted to the
    // equivalent time constant first: 1 / (2*pi*f). Passing 16000 here would be read as a
    // 16-second time constant, which would all but remove the hiss instead of shaping it.
    const float hissBandLimit = onePoleCoefficient (1000.0f / (juce::MathConstants<float>::twoPi * 16000.0f),
                                                    engineSampleRate);

    // Playback AC coupling: an 8 Hz one-pole DC blocker, rebuilt per block from the
    // rate. The standard form is y = x - x1 + R*y; R = 1 - 2*pi*fc/rate puts the
    // corner exactly at fc Hz, and clamping keeps the arithmetic safe at any rate.
    const float dcBlockR = juce::jlimit (0.5f, 0.9999f,
                                         1.0f - (juce::MathConstants<float>::twoPi * 8.0f)
                                             / engineSampleRate);

    // Output staging: the loudness the model adds is balanced out here, so OUTPUT
    // is a clean, calibrated +/- dB trim rather than an extra hidden gain stage.
    //
    // This is the STATIC calibration only: it compensates for the fixed gain the
    // saturation curve adds at a nominal level. The level that is actually lost inside
    // the shaper as it clamps is tracked per sample in the tape loop (driveCompensation),
    // so the two do not fight each other - one sets the operating level, the other keeps
    // the stage gain-neutral as DRIVE and the signal level move.
    //
    // Recalibrated for the corrected shaper. The old constants assumed a stage that
    // saturated at every setting, so they were fighting a permanent loss; now that the
    // shaper is near-unity at zero drive and only compresses when pushed, the static trim
    // has to be close to unity as well or the plugin ends up quiet instead of clean.
    const float driveGainCompensation = 1.0f - driveCurve * 0.16f;

    // -----------------------------------------------------------------------
    //  Compressor-coupled saturation.
    //
    //  The two glue stages are part of the machine, so how hard THEY work changes
    //  how hard the tape saturates - the way pushing an already-compressed signal
    //  into a real record head makes it saturate sooner and bloom harder. The
    //  drive term below grows smoothly (200 ms smoothing, so it follows the
    //  programme's density rather than individual transients) with the total gain
    //  reduction both stages are currently applying.
    //
    //  The tape stage runs BEFORE the output compressor in the chain, so it cannot
    //  see that stage's current-block reduction; it uses the previous block's
    //  value instead, which at one-block latency is indistinguishable musically.
    // -----------------------------------------------------------------------
    // 200 ms, stated as a duration rather than as a reciprocal.
    const float squeezeDriveSmoothing = onePoleCoefficient (200.0f, engineSampleRate);

    // The static calibration stays as designed: the slow programme compensator after
    // the output stage already restores any residual broadband loss against the
    // post-INPUT reference, so no extra static boost is wanted here - with the tape
    // poles now in the audio range the passband is close to unity and the wet path
    // level-tracks the dry one at any MIX setting.
    const float finalOutputGain = 0.78f * driveGainCompensation * (0.94f + speedScale * 0.08f);

    // Continuous pseudo-random tape noise: a 32-bit LCG held per instance, so the
    // hiss is uncorrelated between instances and reproducible for a given one.
    // The floor itself is constant - see the note in the tape loop for why there is
    // no programme-dependent levelling.
    const auto nextNoise = [] (std::uint32_t& state) -> float
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<float> ((state >> 8) & 0x00ffffffu) * (1.0f / 8388608.0f) - 1.0f;
    };

    const int activeChannels = activeInputChannels;

    // Working pointers into the block's own storage. In the plain path these are
    // the host buffer's channels; in the oversampled path they are the oversampler's
    // internal buffer. The engine is written against these pointers only.
    std::array<float*, 2> channelData {};
    for (int channel = 0; channel < activeChannels; ++channel)
        channelData[static_cast<std::size_t> (channel)] = block.getChannelPointer (static_cast<std::size_t> (channel));

    float inputPeak = 0.0f;
    float outputPeak = 0.0f;
    double inputSquares = 0.0;
    double outputSquares = 0.0;
    float peakReductionDb = 0.0f;
    float inputPeakReductionDb = 0.0f;
    float inputEnvelopeActivity = 0.0f;
    float driftAccumulator = 0.0f;

    // Reference power for the final gain compensation: the power of the signal straight
    // after the INPUT trim, before the first glue compressor touches it. That is the
    // level the user dialled in with INPUT, so it is what the plugin's output should
    // still be tracking once the tape stage and both compressors have had their way
    // with the signal. Accumulated across this block only - the correction it drives is
    // itself smoothed, so no block-to-block history is needed here.
    float referenceBlockPower = 0.0f;

    // Loudness metering accumulators for this block. currentLufs is carried out of the
    // per-sample loop because the K-weighted follower is stateful across the whole block.
    float currentLufs = -70.0f;
    bool clippingThisBlock = false;
    float currentInputLufs = -70.0f;
    bool inputClippingThisBlock = false;

    // The input side of the K-weighted meter needs the raw signal, but the buffer is
    // processed in place, so the dry input of the current sample is stashed here before
    // the channel loop overwrites it. Two slots are enough for a stereo frame.
    std::array<float, 2> inputChainHistory {};

    // Slow one-pole coefficient for the compensation itself. Deliberately far slower
    // than either compressor, so the correction settles on the programme level instead
    // of pumping along with the transients.
    // 450 ms, stated as a duration rather than as a reciprocal.
    const float compensationCoefficient = onePoleCoefficient (450.0f, engineSampleRate);

    // VU ballistics (300 ms) for the input and output meters. The coefficient is a
    // constant for the whole block - it depends only on the sample rate - so it is
    // computed once here instead of re-evaluating an exp() twice per sample inside
    // the loop below.
    // 300 ms, stated as a duration rather than as a reciprocal.
    const float vuBallisticCoefficient = onePoleCoefficient (300.0f, engineSampleRate);

    // -------------------------------------------------------------------------
    //  Glue compressor operating points.
    //
    //  The two stages are independent processors, but each one is calibrated from
    //  the trim control that feeds it. Turning INPUT up pushes this scaled signal
    //  into the input stage, so that stage clamps down sooner and more firmly;
    //  OUTPUT works the same way on the output stage, which then spends the trimmed
    //  level on the output trim. A negative trim backs a stage off completely and a
    //  positive one leans on it hard, which is what makes the two controls feel
    //  like real gain staging rather than plain volume.
    // -------------------------------------------------------------------------
    const float inputDriveLoad = juce::jlimit (0.0f, 1.0f, (inputDb + 24.0f) / 48.0f);
    const float outputDriveLoad = juce::jlimit (0.0f, 1.0f, (outputDb + 24.0f) / 48.0f);

    const float inputThresholdDb = -17.0f + inputDriveLoad * 14.0f;    // -17 .. -3 dB
    const float inputKneeDb = 10.0f - inputDriveLoad * 3.0f;           // 10 .. 7 dB
    const float inputCompressorRatio = 1.15f + inputDriveLoad * 0.25f; // 1.15 .. 1.40 : 1
    const float inputReductionLimitDb = -(2.0f + inputDriveLoad * 5.0f);

    const float outputThresholdDb = -18.0f + outputDriveLoad * 15.0f;  // -18 .. -3 dB
    const float outputKneeDb = 7.0f - outputDriveLoad * 2.0f;          // 7 .. 5 dB
    const float outputCompressorRatio = 1.16f + outputDriveLoad * 0.18f;
    const float outputReductionLimitDb = -(2.5f + outputDriveLoad * 5.0f);

    // -----------------------------------------------------------------------
    //  Tape-stock and transport coupling into the glue time constants.
    //
    //  The compressors are part of the machine, so the FORMULA loaded on it and
    //  the SPEED it runs at change how the glue stages move - not just how the
    //  tape saturates:
    //
    //    Tape formula (0 = J37 ... 3 = Chrome). Oxide thickness and bias current
    //    set how quickly the detector can follow the programme: the soft, low-
    //    output formulas get slower constants (more head bump, more relaxed
    //    glue), the hot and chrome formulas get faster, tighter ones.
    //
    //    Transport speed. A slow 7.5 ips pass has more print-through and a
    //    lazier flux build-up, so the glue is stretched; 30 ips is tight and
    //    immediate, so the constants shorten. This rides the same speedScale
    //    used for the head damping, so SPEED keeps one coherent meaning across
    //    the whole machine.
    //
    //  Attack multipliers land roughly in 0.72..1.33, release in 0.72..1.40.
    //  Both stages share the multipliers, so the two stages still feel like one
    //  machine while remaining independent processors.
    // -----------------------------------------------------------------------
    // Eight formulas now index this scale: type 0 (J37, softest glue) through
    // type 7 (RTM SM911), linearly between. The jlimit keeps the ends exact.
    const float stockAttackScale = juce::jlimit (0.95f, 1.34f,
                                                 1.34f - 0.047f * static_cast<float> (tapeType));
    const float stockReleaseScale = juce::jlimit (0.95f, 1.40f,
                                                  1.40f - 0.064f * static_cast<float> (tapeType));
    const float transportAttackScale = 1.32f - 0.22f * speedScale;                  // 1.14 -> 1.03
    const float transportReleaseScale = 1.38f - 0.30f * speedScale;                 // 1.15 -> 0.98

    const float inputAttackSeconds = 0.16f * stockAttackScale * transportAttackScale;
    const float inputReleaseSeconds = 0.85f * stockReleaseScale * transportReleaseScale;
    const float outputAttackSeconds = 0.20f * stockAttackScale * transportAttackScale;
    const float outputReleaseSeconds = 1.00f * stockReleaseScale * transportReleaseScale;

    // The two glue detectors' block-rate coefficients. Everything they depend on -
    // the engine rate, the four base constants above and the trim-driven load - is
    // fixed for the whole block, so they are built once here rather than recomputed
    // per sample. Before this the detector cost two std::exp calls per sample per
    // channel for values that cannot change inside the block.
    const auto inputCompressorCoefficients = GlueCompressor::makeCoefficients (
        engineSampleRate, inputAttackSeconds, inputReleaseSeconds, inputDriveLoad);
    const auto outputCompressorCoefficients = GlueCompressor::makeCoefficients (
        engineSampleRate, outputAttackSeconds, outputReleaseSeconds, outputDriveLoad);

    // The K-weighted meters' 400 ms window coefficients, same reasoning: a function
    // of the rate alone, so two std::exp calls per frame per meter become two per
    // block.
    const auto outputLoudnessCoefficient = LoudnessMeter::makeWindowCoefficient (engineSampleRate);
    const auto inputLoudnessCoefficient = LoudnessMeter::makeWindowCoefficient (engineSampleRate);

    // Makeup is part of each stage, and it is driven by the same trim control: a
    // boosted trim pays its reduction back and a trimmed-down one simply backs off,
    // so neither stage can quietly undo the balance the user dialled in.
    const float inputDegree = juce::jlimit (0.0f, 1.0f, (inputDb + 16.0f) / 32.0f);
    const float outputDegree = juce::jlimit (0.0f, 1.0f, (outputDb + 16.0f) / 32.0f);
    const float inputMakeupFraction = 0.30f + inputDegree * 0.35f;
    const float outputMakeupFraction = 0.35f + outputDegree * 0.35f;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const float inputGain = inputGainSmoothed.getNextValue();
        const float outputGain = outputGainSmoothed.getNextValue();
        const float currentWidth = widthSmoothed.getNextValue();
        const float bypassMix = bypassSmoothed.getNextValue();
        const float deltaMix = deltaListenSmoothed.getNextValue();

        // Per-sample reference power for the final compensation, reset each iteration.
        referenceBlockPower = 0.0f;

        // The dry/wet blend is handled per channel below, so the smoothed MIX is read
        // once per SAMPLE here and the gains are derived from it.
        //
        // Two defects are fixed together. First, this line used to advance the smoother
        // and throw the value away while the crossfade used the raw per-block
        // parameter - so MIX itself was a hard step on every block boundary, the last
        // control that could still crackle. Second, the crossfade was linear
        // (dry + wet = 1) while the comment beside it promised an equal-gain fade with
        // no dip; for two mostly uncorrelated signals a linear fade loses about 3 dB in
        // the middle of the travel. A raised-cosine fade holds the pair's total power
        // constant, sits at exactly 0 dB on BOTH ends (MIX 0 is pure dry, MIX 1 is pure
        // wet) and has no step in its slope, so neither end of the control can collapse.
        const auto mixNow = juce::jlimit (0.0f, 1.0f, mixSmoothed.getNextValue());
        const auto mixAngle = mixNow * juce::MathConstants<float>::halfPi;

        // The taper itself was fine; the two gains were on the wrong sides. sin was
        // applied to the DRY path and cos to the WET one, so MIX 0 delivered the
        // fully processed tape and MIX 100 the untouched input - the exact reverse
        // of what the control, its parameter comment and the panel tooltip all
        // describe, and the reason MIX at 0 did not sound dry at all. Both ends of
        // an equal-power crossfade sit at unity either way, so this is a swap and
        // not a change of taper: MIX 0 is dry at unity, MIX 1 is wet at unity, and
        // the total power still holds across the travel.
        const auto dryGain = std::cos (mixAngle);
        const auto wetGain = std::sin (mixAngle);

        // ------------------------------------------------------------------
        //  Transport ramp and the spindown platter ramp.
        //
        //  Both are advanced once per FRAME and used for the whole frame, so both
        //  channels ride the same capstan - advancing a ramp per channel would put
        //  the sides a sample apart, which is a channel skew, not a transport.
        //
        //  STOP drives the transport to 0, PLAY holds it at 1, and START lets it
        //  climb back from wherever it was. The spindown ramp runs the platter
        //  speed the other way: 1 at speed, 0 fully stopped, and it MULTIPLIES the
        //  transport, so the two compose into one platter speed.
        // ------------------------------------------------------------------
        transportRamp += (transportTarget - transportRamp) * transportRampCoefficient;
        if (transportTarget <= 0.0f && transportRamp < 1.0e-5f)
            transportRamp = 0.0f;

        spindownRamp += (spindownTarget - spindownRamp) * spindownCoefficient;
        if (spindownTarget <= 0.0f && spindownRamp < 1.0e-5f)
            spindownRamp = 0.0f;

        // The platter's actual speed, which is what the machine's pitch, its
        // modulation depth and its reels all follow. This is the single value the
        // rest of the loop reads; it is the PRODUCT of the two ramps rather than
        // either one, which is what makes a spindown from a stopped transport and
        // a stop during a spindown both land at the same place.
        const float platterSpeed = transportRamp * spindownRamp;

        // The spin-up pitch error: while the platter is below speed the transport
        // runs flat, and that is what makes the ramp read as a machine engaging
        // rather than as a fade-in. A spindown drives the same term the other
        // way, so the pitch falls away as the platter coasts down.
        //
        // The curve is raised to platterPitchExponent so the pitch holds near speed
        // until the platter is genuinely slow, then falls away quickly. That is what
        // makes a spindown read as a turntable losing its drive rather than as a
        // fade-out, and it is why the exponent has to be ABOVE 1: below 1 the pitch
        // would droop the instant the button went down.
        const float pitchedPlatter = std::pow (platterSpeed, platterPitchExponent);
        const float speedError = (1.0f - (1.0f - pitchedPlatter) * 0.7f)
                               * (1.0f - loadInducedSpeedError);

        // ------------------------------------------------------------------
        //  START is transient: advance it to PLAY once the capstan arrives.
        //
        //  A tape deck has no "starting" position - you press play, it comes up to
        //  speed, and then it IS playing. That is what this does, and it is the
        //  fix for START never ending: the ramp reaches speed, the state is written
        //  back as PLAY, and the transport control settles on Play by itself.
        //
        //  The write goes through the parameter rather than straight into
        //  transportState, so the host's automation lane and the panel's combo box
        //  both show the same thing the engine is doing. It is a host-facing call
        //  from the audio thread, which JUCE permits (the parameter is a plain
        //  atomic set behind it) and which is exactly what a plugin does when it
        //  drives one of its own parameters.
        // ------------------------------------------------------------------
        if (transportState == static_cast<int> (TransportState::start)
              && transportSpinningUp
              && transportRamp >= 0.999f)
        {
            transportSpinningUp = false;
            transportState = static_cast<int> (TransportState::play);
            lastTransportState = transportState;

            if (auto* parameter = parameters.getParameter ("transport"))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (1.0f));
        }

        // Publish the platter speed and the spindown alone for the editor's reels
        // and momentary button. Relaxed stores: these are advisory UI values, and
        // the UI reads them with relaxed loads, so there is nothing to synchronise.
        transportRampPublished.store (platterSpeed, std::memory_order_relaxed);
        spindownRampPublished.store (spindownRamp, std::memory_order_relaxed);

        std::array<float, 2> tapeOutput {};

        // The machine's DRY leg, for DELTA listen: the signal after the input trim
        // and the input glue compressor and nothing else - the same `x` the raised-
        // cosine MIX crossfade feeds its dry side further down. Held per channel so
        // it can be scaled by stageGain once that gain is known, which it is not at
        // this point in the frame.
        std::array<float, 2> machineDryInput {};

        // The SUBFUND undertones, one per channel. They are generated from the
        // shaper's output down in the tape loop but deliberately NOT summed into
        // the tape path: everything between there and here is a nonlinearity that
        // would distort them into harmonics of themselves. They join the finished
        // signal further down instead, where the remaining stages are linear or
        // gain-only.
        std::array<float, 2> undertoneOutput {};

        // ------------------------------------------------------------------
        //  Input stage glue compressor, detected ONCE per frame.
        //
        //  It sits straight after the input trim, so the signal that reaches the
        //  tape is always the controlled one and the INPUT control is what drives
        //  this stage - exactly like hitting a recorder input harder. Fully
        //  independent of the output stage: this detector only ever sees the
        //  trimmed input.
        //
        //  It used to run inside the per-channel loop, once per channel, on that
        //  channel's own power. In stereo that is a detector advancing twice per
        //  frame, so every attack and release constant silently halved, and each
        //  channel taking its gain from an envelope the other channel had already
        //  moved - the two sides compressed at slightly different instants, which
        //  is where stereo image wander on a transient comes from. The gain is now
        //  computed once, from the power averaged over the channels, and both
        //  sides are given the same one. This is the arrangement the OUTPUT stage
        //  compressor below has always used.
        // ------------------------------------------------------------------
        std::array<float, 2> inputTrimmedByChannel {};
        float inputDetectorPower = 0.0f;
        for (int channel = 0; channel < activeChannels; ++channel)
        {
            const auto raw = channelData[static_cast<std::size_t> (channel)][sample];
            const auto trimmed = raw * inputGain;
            inputTrimmedByChannel[static_cast<std::size_t> (channel)] = trimmed;
            inputDetectorPower += trimmed * trimmed;
        }
        inputDetectorPower /= static_cast<float> (juce::jmax (1, activeChannels));

        // ------------------------------------------------------------------
        //  Input stage glue, LINKED or per-channel.
        //
        //  The shared detector always runs: it is the linked answer, and it is the
        //  path that has to stay warm so moving ST LINK cannot resume it from a
        //  stale envelope. When ST LINK is below 100 % each channel's own detector
        //  is advanced too, on that channel's own power, and the control blends
        //  the two gains.
        //
        //  Both paths use the same coefficients, so the only thing that differs is
        //  WHICH signal the detector sees: the channel average, or one side. That
        //  is exactly the difference between a bus compressor and a dual-mono one.
        //
        //  The blend happens per channel below, where each side's gain is applied,
        //  because the unlinked answer is different for L and R.
        // ------------------------------------------------------------------
        const float inputEnvelopeDb = inputCompressor.processDetection (
            inputDetectorPower, engineSampleRate, inputCompressorCoefficients);
        const float linkedReductionDb = juce::jmax (inputReductionLimitDb,
                                                    softKneeReductionDb (inputEnvelopeDb,
                                                                         inputThresholdDb,
                                                                         inputKneeDb,
                                                                         inputCompressorRatio));
        const float inputReductionDb = linkedReductionDb;
        const float linkNow = stLinkSmoothed.getCurrentValue();

        // The per-channel reductions, valid only when the control is actually
        // asking for them. At full link this array is unused and the detectors are
        // left alone, so the default path costs nothing extra.
        std::array<float, 2> inputPerChannelReductionDb {};
        if (linkNow < 0.999f)
        {
            for (int channel = 0; channel < activeChannels; ++channel)
            {
                const auto trimmed = inputTrimmedByChannel[static_cast<std::size_t> (channel)];
                const auto channelEnvelopeDb = inputCompressorChannels[static_cast<std::size_t> (channel)]
                    .processDetection (trimmed * trimmed, engineSampleRate,
                                       inputCompressorCoefficients);
                inputPerChannelReductionDb[static_cast<std::size_t> (channel)] =
                    juce::jmax (inputReductionLimitDb,
                                softKneeReductionDb (channelEnvelopeDb, inputThresholdDb,
                                                     inputKneeDb, inputCompressorRatio));
            }
        }

        const float inputCompressionGain = juce::Decibels::decibelsToGain (linkedReductionDb);

        inputPeakReductionDb = juce::jmin (inputPeakReductionDb, inputReductionDb);
        inputEnvelopeActivity = juce::jmax (inputEnvelopeActivity,
                                            inputCompressor.getEnvelopeActivity());

        for (int channel = 0; channel < activeChannels; ++channel)
        {
            auto& wowPhase = channel == 0 ? wowPhaseL : wowPhaseR;
            auto& flutterPhase = channel == 0 ? flutterPhaseL : flutterPhaseR;
            auto& hysteresisMemory = channel == 0 ? hystL : hystR;
            auto& highFreqMemory = channel == 0 ? highFreqL : highFreqR;

            const float rawInput = channelData[static_cast<std::size_t> (channel)][sample];
            inputPeak = juce::jmax (inputPeak, std::abs (rawInput));
            inputSquares += static_cast<double> (rawInput) * rawInput;
            inputChainHistory[static_cast<std::size_t> (channel)] = rawInput;

            // Input-side VU ballistic and clipping, measured on the raw signal arriving
            // at the plugin so the INPUT meter shows what the host is actually sending.
            inputVuAverage += (std::abs (rawInput) - inputVuAverage) * vuBallisticCoefficient;
            if (std::abs (rawInput) > 1.0f)
                inputClippingThisBlock = true;

            // The trimmed input for this channel, and this frame's compressor gain
            // for both of them, as computed once above the loop.
            const float inputTrimmed = inputTrimmedByChannel[static_cast<std::size_t> (channel)];

            // Track the power of the post-INPUT, pre-first-compressor signal, summed
            // over every active channel and averaged right after the loop. The old
            // first-channel-only reference was a different scale from the averaged
            // post-compressor power it is compared against, so a hard-panned right
            // channel could hide a genuine level loss and switch the compensation off.
            referenceBlockPower += inputTrimmed * inputTrimmed;

            // The glue gain for THIS channel. At full link it is the shared
            // reduction, as it always was; below that it is a blend toward this
            // channel's own reduction, so a loud left side ducks only the left.
            float inputCompressionGainThisChannel = inputCompressionGain;
            if (linkNow < 0.999f)
            {
                const auto unlinkedGain = juce::Decibels::decibelsToGain (
                    inputPerChannelReductionDb[static_cast<std::size_t> (channel)]);
                inputCompressionGainThisChannel = inputCompressionGain
                                                + (unlinkedGain - inputCompressionGain)
                                                      * (1.0f - linkNow);
            }

            float x = inputTrimmed * inputCompressionGainThisChannel;

            // ------------------------------------------------------------------
            //  Preamp and distortion - the two gain stages in FRONT of the machine.
            //
            //  They run here, on the trimmed and glue-compressed input, so the tape
            //  stage hears what they produced. That ordering is the whole point:
            //  a distorted signal recorded to tape sounds like a record rather than
            //  a pedal precisely because the machine smooths what the pedal did.
            //
            //  Both are level-matched internally (see InputStage), so neither
            //  changes the operating level - the INPUT control remains the thing
            //  that sets it, and these two only change the character.
            // ------------------------------------------------------------------
            auto& inputStage = channel == 0 ? inputStageL : inputStageR;

            // ------------------------------------------------------------------
            //  THE DI BOX, first in the chain.
            //
            //  First for a physical reason, not an arbitrary one: a DI box is
            //  what the instrument is plugged INTO, so everything after it - the
            //  distortion, the preamp, the saturation curve, the glue stages -
            //  hears a signal the box has already loaded, padded and coloured.
            //  That ordering is what makes the DI a real part of the chain
            //  rather than a tone control: a padded signal drives the tape curve
            //  differently from an unpadded one at the same level.
            //
            //  It runs before the IN EQ as well, because on the hardware the
            //  box is physically between the instrument and everything else.
            // ------------------------------------------------------------------
            const float diNow = diSmoothed.getCurrentValue();
            if (diNow > 1.0e-5f)
            {
                x = inputStage.processDiBox (x, diNow,
                                             diLoadSmoothed.getCurrentValue(),
                                             diPadDb, diTransformerSmoothed.getCurrentValue(),
                                             diLoadCoefficient, diTransformerCoefficient,
                                             diHumIncrement);
            }

            const float distortionNow = distortionSmoothed.getCurrentValue();
            if (distortionNow > 1.0e-5f)
                x = inputStage.processDistortion (x, distortionNow);

            const float preampNow = preampSmoothed.getCurrentValue();
            if (preampNow > 1.0e-5f)
                x = inputStage.processPreamp (x, preampNow, preampLowCutCoefficient);
            // ------------------------------------------------------------------
            //  IN EQ - the input equaliser, and the position is the point.
            //
            //  It sits here, after the trim and the two input stages and BEFORE
            //  the tape, so what it shapes is what the machine HEARS. That makes
            //  it a different control from the output EQ: lifting the low end
            //  here pushes more low end into the saturation curve and the glue
            //  compressors, so the result is not merely a bass boost - the machine
            //  processes that bass, saturates on it and compresses it, and the
            //  character of the whole chain changes with it. This is the EQ an
            //  engineer uses to feed the recorder what it wants.
            //
            //  `machineDryInput` is captured BEFORE the EQ so that DELTA still
            //  subtracts the machine's own dry reference and not the EQ'd version
            //  of it - otherwise a neutral EQ would be invisible in delta while a
            //  boosted one would read as "character", which it is not.
            // ------------------------------------------------------------------
            machineDryInput[static_cast<std::size_t> (channel)] = x;

            inputEq.lowGain = inputEqLowSmoothed.getCurrentValue();
            inputEq.midGain = inputEqMidSmoothed.getCurrentValue();
            inputEq.highGain = inputEqHighSmoothed.getCurrentValue();
            x = inputEq.process (x, channel, inputEqMidCoefficient);

            const float wowLfo = std::sin (wowPhase);
            const float flutterLfo = std::sin (flutterPhase);
            const float grainLfo = std::sin (wowPhase * 0.8f + flutterPhase * 1.3f
                                             + static_cast<float> (channel) * 2.4f);

            wowPhase += (twoPi * wowFreq) / engineSampleRate;
            flutterPhase += (twoPi * flutterFreq) / engineSampleRate;
            // Wrap rather than a single subtraction: at very low rates, or with a
            // high wow/flutter setting, one increment can exceed a full turn and a
            // lone `-= twoPi` would leave the phase running away unbounded.
            wowPhase = std::fmod (wowPhase, twoPi);
            flutterPhase = std::fmod (flutterPhase, twoPi);

            if (channel == 0)
                driftAccumulator = wowLfo * wowDepth + flutterLfo * flutterDepth;

            // Transport speed modulation: wow is a slow pitch wander, flutter a fast
            // shimmer, and the grain term adds the fine tape-surface texture. The grain
            // is gated by the transport's actual activity: with Wow AND Flutter both at
            // zero the machine is mathematically still, so no modulation of any kind
            // reaches the signal.
            // Every transport term is scaled by speedError, which is below 1 while
            // START spins the capstan up. That is what turns the transport ramp into a
            // PITCH ramp rather than a level fade: the modulation deepens and the whole
            // wet path runs flat until the machine reaches speed.
            // MECHANICS and WEAR both add irregularity on top of the periodic wow
            // and flutter, and they are different mechanisms: MECHANICS is the
            // transport (dry bearings, a slack belt), WEAR is the medium (patchy
            // contact against the head). Both are folded in as extra depth rather
            // than as separate oscillators, because that is what they do to the
            // modulation that already exists - they make it less even, not more.
            const float mechanicsNow = mechanicsSmoothed.getCurrentValue();
            const float wearNowMod = wearSmoothed.getCurrentValue();
            // Two couplings meet here:
            //   - wearWowCoupling, the part WEAR/MECHANICS own on their own, so a
            //     worn machine wanders even with WOW and FLUTTER closed;
            //   - headGapModulationMask, the dark-head damping that softens the
            //     pitch shimmer at the top of the TONE travel before it softens
            //     anything else. Both are block-rate constants.
            const float irregularDepth = wowDepth
                                       + mechanicsNow * 0.006f
                                       + wearNowMod * 0.003f
                                       + wearWowCoupling;
            const float irregularOffset = (channel == 0 ? tapeConditionL : tapeConditionR).driftState
                                        * mechanicsNow * 0.010f;

            const float wowMod = (1.0f + wowLfo * irregularDepth + irregularOffset)
                                 * headGapModulationMask * speedError;
            const float flutterMod = (1.0f + flutterLfo * flutterDepth
                                        * flutterScaleSmoothed.getCurrentValue())
                                     * headGapModulationMask * speedError;
            const float grainMod = 1.0f + tapeHiss * 0.10f * transportActivityGate * grainLfo;

            // Record head: pre-emphasis, tape bias offset and drive. With DRIVE at zero
            // this is exactly unity, so the saturator sees the signal at the level the
            // user dialled in rather than a pre-boosted version of it. The TONE macro
            // adds the slow-machine pre-bias on top, scaled by the speed's own bias so
            // the two controls multiply naturally instead of fighting.
            const float preDrive = x * (1.0f + driveAmountSmoothed.getCurrentValue() * 1.2f
                                          * speedBias * preDriveGainSmoothed.getCurrentValue());
            // Magnetic hysteresis with memory - the core of the tape sound. The
            // squeeze term in the slope is the compressor coupling: dense, compressed
            // programme literally thickens the magnetic curve, not just its level.
            // Both arguments come from the ramps, so DRIVE and BIAS morph the curve
            // continuously instead of stepping it on every block boundary - BIAS was
            // the loudest of all, because its asymmetry term shifts the whole curve
            // rather than merely scaling it.
            // ------------------------------------------------------------------
            //  The saturation core.
            //
            //  This replaces the single magneticHysteresis call. It runs the same
            //  DRIVE and BIAS arguments the old shaper took - so both controls keep
            //  their meaning and their existing calibration - but weights six
            //  distinct curve shapes together: tape, valve, cassette, amp,
            //  transformer and digital. See SaturationCore for what each one is
            //  and why they are genuinely different mechanisms rather than six
            //  settings of one.
            //
            //  The weights come from the SMOOTHED blend controls and are applied
            //  per sample, so sweeping BLEND morphs the harmonics continuously
            //  instead of stepping between six curves on a block boundary.
            //
            //  SAG acts on the DRIVE, not on the output, because that is what the
            //  supply does: it is the gain that droops under sustained demand. The
            //  sag envelope follows the signal's own power, so a held chord gives
            //  and then recovers while a short transient never moves it - which is
            //  exactly how a real amplifier behaves.
            // ------------------------------------------------------------------
            auto& saturation = channel == 0 ? saturationL : saturationR;
            auto& amp = channel == 0 ? ampL : ampR;

            saturation.setBlend (blendSmoothed.getCurrentValue(),
                                 shapeSmoothed.getCurrentValue());

            // The type switches re-voice their own curve. Four of the five live
            // here, because four of the five are voices on SaturationCore's
            // curves; the vinyl type re-voices the VinylStage at the end of the
            // chain instead and is applied once per block.
            //
            // Each switch is cheap - a table read into three floats - but it is
            // still per sample here rather than per block so that sweeping a type
            // control mid-note lands the same way the curve does: sample-locked
            // to the block the host handed us, with no extra state to unwind.
            saturation.setValveVoice (valveTypeCached);
            saturation.setAmpVoice (ampTypeCached);
            saturation.setTransformerVoice (transformerTypeCached);
            saturation.setDigitalVoice (digitalTypeCached);

            // Sag: the supply envelope. The two coefficients are BLOCK-RATE
            // constants (see where they are built, above the loop) - rebuilding
            // them here cost two std::exp per sample per channel for values that
            // cannot change within the block.
            const float sagAmountNow = sagSmoothed.getCurrentValue();
            // The demand the supply sees is the signal ARRIVING at the stage, not
            // what leaves it: a real supply droops in proportion to how hard it is
            // being asked to work, which is the input to the gain stage.
            const float demand = std::abs (preDrive);
            const float sagCoefficient = demand > amp.sagEnvelope ? sagAttackCoefficient
                                                                  : sagReleaseCoefficient;
            amp.sagEnvelope += (demand - amp.sagEnvelope) * sagCoefficient;
            amp.sagGain = 1.0f - sagAmountNow * 0.35f
                              * juce::jlimit (0.0f, 1.0f, amp.sagEnvelope * 3.0f);

            const float driveWithSag = shaperDriveSmoothed.getCurrentValue() * amp.sagGain;

            const float shapedCore = saturation.process (preDrive, driveWithSag,
                                                         shaperAsymmetrySmoothed.getCurrentValue());

            // The amp's post-curve voicing: cabinet, then presence. It is applied
            // only in proportion to how much amp is in the blend, so a pure tape
            // setting is bit-for-bit the machine it always was.
            const float ampShare = juce::jlimit (0.0f, 1.0f, saturation.ampWeight);
            float voicedCore = shapedCore;
            if (ampShare > 1.0e-4f)
            {
                const float voiced = amp.process (shapedCore,
                                                  cabinetLowCoefficient,
                                                  cabinetPeakCoefficient,
                                                  presenceCoefficient,
                                                  (presenceSmoothed.getCurrentValue() - 0.5f) * 2.0f
                                                      * 0.6f);
                // Crossfade rather than switch, so the voicing fades in with the
                // amp share and there is no step at any BLEND position.
                voicedCore = shapedCore + (voiced - shapedCore)
                                          * ampShare * cabinetSmoothed.getCurrentValue();
            }

            hysteresisMemory[2] = hysteresisMemory[1];
            hysteresisMemory[1] = hysteresisMemory[0];
            hysteresisMemory[0] = voicedCore;

            // ------------------------------------------------------------------
            //  Sub-Fundamental: the multi-stage undertone series with downward saturation.
            //
            //  Generated here, from the SHAPER's output rather than the raw input, so
            //  it is excited by the signal that actually reached the magnetic domain,
            //  and per channel: one generator on the mono sum would collapse the
            //  stereo image at precisely the octave the control is meant to thicken.
            //
            //  The result is parked in undertoneOutput[] rather than summed into the
            //  tape path. Everything between here and the output is a nonlinearity -
            //  the bias-compression curve, the wow and flutter modulation, the output
            //  glue compressor - and each of them will happily turn the undertones
            //  into harmonics of themselves. That is the "regular harmonics are
            //  generated from the subharmonic" report: the undertones went in as
            //  partials and came out as a full harmonic series built on each of them.
            //  They are added back where the rest of the chain is linear or gain-only.
            // ------------------------------------------------------------------
            auto& subharmonic = channel == 0 ? subharmonicL : subharmonicR;
            undertoneOutput[static_cast<std::size_t> (channel)] = subharmonic.process (
                voicedCore, driveAmountSmoothed.getCurrentValue(),
                subFundamentalSmoothed.getCurrentValue(), engineSampleRate);

            // A parallel addition, not a blend: the control reads as adding weight
            // below the note instead of crossfading the tape away. The depth ramp keeps
            // the level change from stepping the phase-locked oscillator.
            // The voiced core is what continues down the tape path: the cabinet and
            // presence stages are part of the amp, so discarding them here would
            // compute the whole voicing and then throw it away.
            const float withSubharmonic = voicedCore;

            // Measure what the shaper actually produced, comparing its input against its
            // output. Harmonics are the reason this plugin exists, so the character is
            // observed rather than assumed: the analyser separates the even content
            // (warmth, from the bias asymmetry) from the odd content (edge, from the
            // symmetric tanh). Only the left channel is measured, since the two are driven
            // identically and doubling the analyser would cost twice as much for the same
            // reading.
            if (channel == 0)
                harmonicAnalyser.analyse (preDrive, voicedCore, engineSampleRate);

            // Tape is a low-pass medium: the faster the tape and the brighter the
            // tone setting, the more top end survives. The undertones are NOT part of
            // this - they join below, after the last nonlinearity.
            highFreqMemory[1] += (withSubharmonic - highFreqMemory[1]) * toneLpSmoothed.getCurrentValue();
            const float afterTapeLoss = highFreqMemory[1];

            // Per-model head damping (the headDampingHz each TAPE TYPE sets, scaled
            // by the transport speed). This pole used to be computed and then never
            // applied - the formulas' damping figures were a no-op.
            //
            // Two ramps share this target: the ordinary 20 ms control ramp, and the
            // slow one used while a tape formula is being switched. A formula change
            // moves this pole by up to 8 kHz, and 20 ms of that travel is an audible
            // sweep rather than a crossfade, which is why the slow ramp exists.
            const float dampingCoefficient = tapeTypeSwitching
                ? headDampingSwitchSmoothed.getCurrentValue()
                : hfPostSmoothed.getCurrentValue();

            // Both ramps must advance every sample even when one is idle, or the unused
            // one would resume from a stale value on the next switch. Reading advances
            // it lazily through the shared sample clock, so touching the other one here
            // is enough.
            if (tapeTypeSwitching)
                hfPostSmoothed.getCurrentValue();
            else
                headDampingSwitchSmoothed.getCurrentValue();

            highFreqMemory[2] += (afterTapeLoss - highFreqMemory[2]) * dampingCoefficient;
            const float dampedLoss = highFreqMemory[2];

            // Playback head gap loss: the TONE macro's crossfade of the head itself.
            highFreqMemory[0] += (dampedLoss - highFreqMemory[0]) * headGapSmoothed.getCurrentValue();
            const float headLoss = highFreqMemory[0];
            highFreqMemory[1] = afterTapeLoss;

            // ------------------------------------------------------------------
            //  MODELED TRACKS: the track WIDTH's two consequences.
            //
            //  A narrower track reads less low end per channel, because the head
            //  gap sees less of the recorded wavelength at the bottom of the
            //  band. That is the same geometry that raised the noise floor above,
            //  and it is applied here as a gentle bottom-end tilt rather than as
            //  a level change: the machine's overall level is the OUTPUT
            //  control's job, and a layout should not be a volume knob.
            //
            // The tilt is taken from the same split the tape condition stage
            // uses, so a narrower track's difference lands in the same place a
            // FLUX change does - which is correct, because both are statements
            // about how much of the medium's depth is being used. At the
            // two-track default the scale is exactly 1.0, so the machine is
            // bit-for-bit what it always was.
            // ------------------------------------------------------------------
            const float widthTilt = juce::jlimit (0.75f, 1.15f,
                                                  1.0f + (trackWidthScale - 1.0f) * 0.65f);

            // Scale compensation, gentle level-dependent bias compression and the
            // tape noise floor ride on the modulated signal.
            const float compensation = tapeCurve / 1.30f * widthTilt;

            // Nonlinearity costs level, but only when the shaper is actually working. The
            // corrected shaper is near-unity at low drive, so this correction scales with
            // the drive amount rather than applying a large standing boost - the old 2.2x
            // ceiling was compensating for a stage that saturated at every setting and is
            // no longer appropriate, which is part of why the plugin came out harsh and
            // loud. It now recovers a modest amount of the level the clamps removed and
            // stays close to unity when the machine is running clean.
            const float shapedLevel = std::abs (voicedCore);
            const float shaperLoss = juce::jlimit (0.0f, 1.0f,
                                                   driveCurve * 0.22f * (1.0f - shapedLevel * 0.7f));
            const float driveCompensation = 1.0f + shaperLoss;

            const float compressedBias = headLoss * (1.0f - 0.18f * headLoss * headLoss)
                                       / juce::jmax (0.35f, compensation)
                                       * juce::jlimit (0.8f, 1.35f, driveCompensation);

            // The hiss is band-limited rather than white, so its spectrum is the same at
            // 44.1 kHz and 192 kHz and it reads as tape noise instead of digital hiss. The
            // filter state is per channel so the two sides stay uncorrelated.
            auto& hissLowPass = channel == 0 ? hissLowPassL : hissLowPassR;
            const float rawHiss = nextNoise (noiseState) * hissGainSmoothed.getCurrentValue();
            hissLowPass += (rawHiss - hissLowPass) * hissBandLimit;

            // The band limit costs most of the noise power, so the gain is compensated by
            // the inverse of the filter's RMS response. Deriving it from the coefficient
            // rather than a fixed number keeps the perceived level flat at every rate.
            //
            // NOTE: there is deliberately no programme-dependent levelling here. An
            // earlier version divided the floor back up by the amount the glue stages had
            // ducked, with the intent of holding the level constant between pause and
            // signal. The maths was inverted - the compensation ran from 1.0 in a pause up
            // to a 1.05 ceiling under signal - so the hiss was loudest when nothing was
            // playing and slightly louder still when the compressors clamped down. Tape
            // hiss is simply a constant floor; tracking the programme makes it breathe
            // with the music, which is the one thing a noise floor must not do.
            const float bandLimitCompensation = 1.0f / std::sqrt (juce::jmax (0.05f, hissBandLimit));
            // The NOISE control trims the floor on top of the formula's own figure:
            // 0.5 is unity, so the knob starts neutral and the stock's own character
            // is unchanged unless the user asks for a different floor. The multiplier
            // is carried by a smoother, so dragging the knob glides the hiss rather
            // than stepping it.
            const float noiseFloor = hissLowPass * bandLimitCompensation
                                   * noiseTrimSmoothed.getCurrentValue();

            const float motioned = (compressedBias + noiseFloor) * wowMod * flutterMod * grainMod;

            // Playback EQ: the BRIGHTNESS tilt. A one-pole low-pass at the fixed
            // 1.6 kHz pivot splits the wet signal ITSELF into low and high bands
            // (the old code split it against its own filtered copy, so the "high
            // band" was hiss residue and the control was inaudible on an
            // analyser). The matched gain pair then moves the two bands in
            // opposite directions - removals capped near 4 dB, boosts to +15 dB,
            // exactly unity at the 50 percent pivot. Both smoothers advance every sample, so the
            // tilt can never step the waveform.
            auto& pivotLow = toneShelfSplit[static_cast<std::size_t> (channel)];
            pivotLow += (motioned - pivotLow) * toneShelfCoefficient;
            const float highBand = motioned - pivotLow;
            const float deEmphasised = pivotLow * toneShelfGainSmoothed.getCurrentValue()
                                     + highBand * toneShelfBoostSmoothed.getCurrentValue();

            // ------------------------------------------------------------------
            //  Tape condition: FLUX and WEAR.
            //
            //  FLUX is the record head's depth into the oxide - more flux is more
            //  low end and a stronger hysteresis memory, less is thin and bright.
            //  WEAR is the state of the heads and the tape: a rounded gap and
            //  patchy oxide lose top end.
            //
            //  They run here, after the playback tilt, because both are properties
            //  of the RECORDED signal rather than of the electronics - the tilt is
            //  the playback EQ, and these two are what the medium did before it.
            // ------------------------------------------------------------------
            auto& condition = channel == 0 ? tapeConditionL : tapeConditionR;
            const float fluxNow = fluxSmoothed.getCurrentValue();
            const float wearNow = wearSmoothed.getCurrentValue();

            float conditioned = deEmphasised;
            if (std::abs (fluxNow - 0.5f) > 1.0e-4f || wearNow > 1.0e-5f)
                conditioned = condition.processTone (deEmphasised, channel,
                                                     fluxNow, wearNow,
                                                     fluxShelfCoefficient, wearLossCoefficient);

            // -------------------------------------------------------------------
            //  Playback AC coupling (DC blocker) - the fix for "MIX at maximum
            //  produces garbage instead of a warm signal".
            //
            //  The shaper is deliberately asymmetric (that asymmetry is what creates
            //  the even harmonics), which leaves a small DC component and a slightly
            //  lopsided envelope on the tape signal. On a real machine the playback
            //  electronics are AC-coupled, so that offset never reaches the output;
            //  here it used to ride all the way into the glue compressor, the safety
            //  limiter and the soft clipper. The clipper then worked on an off-centre
            //  waveform: one half clipped much earlier than the other, the safety
            //  limiter sat permanently pulled down by the DC, and the result was a
            //  harsh, congested mess exactly when MIX was at 100 %.
            //
            //  A gentle one-pole DC blocker (about 8 Hz corner) removes the offset
            //  without touching the bass the way a steep high-pass would, which is
            //  precisely what the coupling capacitors in the playback chain do.
            // -------------------------------------------------------------------
            auto& dcX = dcBlockXState[static_cast<std::size_t> (channel)];
            auto& dcY = dcBlockYState[static_cast<std::size_t> (channel)];
            const float dcBlocked = conditioned - dcX + dcBlockR * dcY;
            dcX = conditioned;
            dcY = dcBlocked;

            // -------------------------------------------------------------------
            //  Playback head delay.
            //
            //  The delayed signal is taken from the DC-blocked wet path, which is
            //  the signal as it actually leaves the playback head - so the repeats
            //  inherit the tape's own bandwidth and saturation rather than being a
            //  clean digital echo of the input. That is the whole difference between
            //  a second tape head and a delay unit.
            //
            //  The read offset is the smoothed delay in samples, read with linear
            //  interpolation: the smoothing means the offset is almost never whole,
            //  and a delay that snapped to whole samples would zipper as the knob
            //  moved.
            // -------------------------------------------------------------------
            float delayed = 0.0f;
            if (delayBufferLength > 0)
            {
                const float delaySamples = juce::jlimit (
                    0.0f, static_cast<float> (delayBufferLength - 1),
                    delaySamplesSmoothed.getCurrentValue());

                float readPosition = static_cast<float> (delayWritePosition) - delaySamples;
                if (readPosition < 0.0f)
                    readPosition += static_cast<float> (delayBufferLength);

                const int readIndex = static_cast<int> (readPosition);
                const float fraction = readPosition - static_cast<float> (readIndex);
                const int nextIndex = (readIndex + 1) % delayBufferLength;

                const auto* delayRead = delayBuffer.getReadPointer (channel);
                delayed = delayRead[readIndex]
                        + (delayRead[nextIndex] - delayRead[readIndex]) * fraction;
            }

            // ------------------------------------------------------------------
            //  The repeat's character: TAPE / BBD / MODERN.
            //
            //  DELAY and DLY LVL set the time and the level; this sets what the
            //  repeats SOUND like, and the three are genuinely different machines
            //  rather than three amounts of the same loss:
            //
            //    TAPE   - the original behaviour. Each pass round the loop loses
            //             top end, because the repeat is recorded onto the tape and
            //             played back through the same losses the main path has.
            //    BBD    - a bucket-brigade chip, the analogue delay of the era.
            //             Darker still, and its bandwidth narrows as the delay
            //             lengthens - which is what a BBD physically does, because
            //             the same number of buckets is being clocked more slowly.
            //    MODERN - a clean digital delay: full bandwidth, no loss.
            // ------------------------------------------------------------------
            auto& damp = delayDampState[static_cast<std::size_t> (channel)];
            float delayCharacter = delayed;

            if (delayTypeCached == 0)
            {
                // TAPE: the original damping, unchanged.
                damp += (delayed - damp) * delayDampCoefficient;
                delayCharacter = damp;
            }
            else if (delayTypeCached == 1)
            {
                // BBD: darker still, and the loss rises with the delay time - a
                // long setting on a bucket brigade is a dull one, because the same
                // buckets are being clocked more slowly and the anti-alias filter
                // tracks the clock.
                auto& bbdLow = channel == 0 ? bbdLowL : bbdLowR;
                auto& bbdNoise = channel == 0 ? bbdNoiseStateL : bbdNoiseStateR;

                // The BBD's bandwidth is a fraction of its clock, which is the
                // defining limitation of the technology - and the clock rate is
                // set by the delay time, so this coefficient DOES have to track a
                // smoothed value and cannot be hoisted to the block.
                //
                // It is refreshed on a stride instead. The delay time ramps over
                // 20 ms, so recomputing the coefficient every 32 samples still
                // follows that ramp with 750 points at 48 kHz - indistinguishable
                // - and it costs one std::exp per 32 samples rather than one per
                // sample. The counter is advanced once per frame, not per channel,
                // so both sides refresh on the same instants.
                if (channel == 0 && --bbdCoefficientCountdown <= 0)
                {
                    bbdCoefficientCountdown = bbdCoefficientStride;

                    const float delaySamplesNow = juce::jmax (1.0f, delaySamplesSmoothed.getCurrentValue());
                    const float clockRate = engineSampleRate / delaySamplesNow;
                    const float bbdBandwidthHz = juce::jlimit (800.0f, 6000.0f, clockRate * 0.22f);
                    bbdLowCoefficient = onePoleCoefficientHz (bbdBandwidthHz, engineSampleRate);
                }

                bbdLow += (delayed - bbdLow) * bbdLowCoefficient;

                // The clock's own noise: a small, high-frequency buzz riding on the
                // repeats, which is the audible signature of a BBD.
                bbdNoise = bbdNoise * 1664525u + 1013904223u;
                const float clockNoise = (static_cast<float> ((bbdNoise >> 8) & 0x00ffffffu)
                                            * (1.0f / 8388608.0f) - 1.0f) * 0.006f;

                delayCharacter = bbdLow + clockNoise;
                damp = bbdLow;
            }
            else
            {
                // MODERN: full bandwidth. The damping state is still advanced so
                // switching back to TAPE does not resume from a stale value.
                damp += (delayed - damp) * delayDampCoefficient;
                delayCharacter = delayed;
            }

            const float delayTap = delayCharacter * delayFeedbackSmoothed.getCurrentValue();

            // ------------------------------------------------------------------
            //  What is written back into the line.
            //
            //  Two things matter here, and both are what make this a head on the
            //  machine rather than a parallel effect:
            //
            //    - the wet signal is written, so the echo is recorded onto the
            //      tape and saturates on the way round;
            //
            //    - the FEEDBACK goes into the line as well, and which line it
            //      goes into is what PING-PONG changes. At 0 the repeat is written
            //      back into its own channel, so the echoes stay where they started
            //      - a normal tape slap. At 1 it is written into the OTHER channel,
            //      so each repeat arrives on the opposite side and the echoes
            //      alternate left, right, left - the classic ping-pong.
            //
            //  The delay line itself is one stereo buffer indexed by channel, so
            //  "the other channel" is a one-bit difference in this index. That is
            //  the whole of the implementation: ping-pong is not a second delay
            //  unit, it is the SAME head read on the other side of the machine,
            //  which is what it physically is when two heads are wired across a
            //  stereo pair.
            // ------------------------------------------------------------------
            if (delayBufferLength > 0)
            {
                // The channel the feedback is written into. At a full ping-pong
                // this is the other side; in between it is neither, and the two
                // writes below distribute the feedback across both lines so the
                // control sweeps continuously rather than switching.
                const int feedbackChannel = 1 - channel;
                const float feedbackAmount = delayFeedbackSmoothed.getCurrentValue()
                                           * damp;
                const float pingPongNow = pingPongSmoothed.getCurrentValue();

                auto* delayWrite = delayBuffer.getWritePointer (channel);

                // The dry-ish wet signal always goes into its OWN line: the head
                // is recording what it hears, and that does not move when the
                // feedback is re-routed.
                delayWrite[delayWritePosition] = dcBlocked;

                // The feedback: partly into this channel's line and partly into
                // the other's, split by the control. The sum of the two partial
                // writes is the same total energy at every position, so sweeping
                // PING-PONG moves the echoes across the image rather than fading
                // them.
                delayWrite[delayWritePosition] += feedbackAmount * (1.0f - pingPongNow);

                if (feedbackChannel != channel)
                    delayBuffer.getWritePointer (feedbackChannel)[delayWritePosition]
                        += feedbackAmount * pingPongNow;
            }

            const float withDelay = dcBlocked + delayTap;

            // -------------------------------------------------------------------
            //  Stereo tape offset.
            //
            //  A fractional-sample delay on ONE channel, set by ST OFFSET. It is
            //  deliberately tiny - a few tens of microseconds - and it is what makes
            //  a tape bounce sit wide instead of merely being equalised wide.
            //
            //  A linear-interpolating buffer rather than an all-pass: an all-pass
            //  gives the same group delay for less memory but colours the phase
            //  differently across the band, and the point here is that the two
            //  channels differ by TIME, not by filter shape.
            // -------------------------------------------------------------------
            float aligned = withDelay;
            const bool offsetThisChannel = (channel == 1) == offsetRightChannel;
            if (offsetThisChannel && activeChannels > 1)
            {
                const float offsetSamples = juce::jlimit (
                    0.0f, static_cast<float> (stOffsetBufferLength - 2),
                    stOffsetSamplesSmoothed.getCurrentValue());

                float readPosition = static_cast<float> (stOffsetWritePosition) - offsetSamples;
                while (readPosition < 0.0f)
                    readPosition += static_cast<float> (stOffsetBufferLength);

                const int readIndex = static_cast<int> (readPosition);
                const float fraction = readPosition - static_cast<float> (readIndex);
                const int nextIndex = (readIndex + 1) % stOffsetBufferLength;

                aligned = stOffsetBuffer[static_cast<std::size_t> (readIndex)]
                        + (stOffsetBuffer[static_cast<std::size_t> (nextIndex)]
                           - stOffsetBuffer[static_cast<std::size_t> (readIndex)]) * fraction;
            }

            // The offset buffer is written for the channel being delayed AND for the
            // other one, so whichever side the control points at has a current sample
            // to read. Both writes happen every frame, so the buffer never holds a
            // stale side.
            if (channel == 1)
                stOffsetBuffer[static_cast<std::size_t> (stOffsetWritePosition)] = withDelay;

            // Raised-cosine crossfade between the dry input and the fully processed
            // tape signal, driven by the smoothed MIX. 0 % is a transparent dry signal
            // and 100 % is all tape, both at unity, with the level held across the
            // middle of the travel.
            const float wetMix = aligned * wetGain * platterSpeed;
            const float dryMix = x * dryGain;
            tapeOutput[static_cast<std::size_t> (channel)] = dryMix + wetMix;
        }

        // ------------------------------------------------------------------
        //  Subharmonic anti-phase protection.
        //
        //  Runs once per FRAME, after both channels have generated their
        //  undertones, because it is a statement about the PAIR - the channel loop
        //  cannot see both sides at once.
        //
        //  The 1/2 stage carries the most energy of the whole cascade, so it is
        //  the one that matters if the two sides disagree: two octaves in opposite
        //  phase cancel when the mix is summed to mono, and the low end the
        //  control was asked for simply disappears. That is not a fault in either
        //  generator - each is correctly locked to its own channel's waveform -
        //  but it is a fault in the result.
        //
        //  The fix is to pull the two octaves back together. The correction is a
        //  small rotation of the master phase toward the other channel's, applied
        //  only while the two are actually disagreeing and only in proportion to
        //  how much they disagree, so an already-aligned pair is untouched.
        // ------------------------------------------------------------------
        if (activeChannels == 2)
        {
            const float errorL = subharmonicL.getOctavePhaseError();
            const float errorR = subharmonicR.getOctavePhaseError();

            // The signed difference, wrapped to [-0.5, 0.5): zero means the two
            // octaves are in phase, +/-0.5 means they are exactly opposite.
            float octaveError = errorL - errorR;
            if (octaveError > 0.5f)  octaveError -= 1.0f;
            if (octaveError < -0.5f) octaveError += 1.0f;

            // Only correct a genuine disagreement, and only gently. A 0.05
            // threshold means small tracking differences are left alone - they are
            // not audible and correcting them would fight the per-channel lock -
            // while a real opposition is pulled back over a few cycles.
            if (std::abs (octaveError) > 0.05f)
            {
                const float correction = -octaveError * 0.02f;
                subharmonicR.nudgeMasterPhase (correction);
            }
        }

        // One advance of the delay line's write head per FRAME, not per channel:
        // advancing it inside the channel loop would move the line twice per stereo
        // frame, halving every delay time and putting the two channels a sample
        // apart. The ST OFFSET buffer advances with it so the two stay aligned.
        if (delayBufferLength > 0)
            delayWritePosition = (delayWritePosition + 1) % delayBufferLength;
        stOffsetWritePosition = (stOffsetWritePosition + 1) % stOffsetBufferLength;

        // The track-bleed ring advances once per FRAME for the same reason the
        // other two do: advancing it per channel would move it twice per stereo
        // frame and halve the spacing between the two heads.
        tracksBleedWritePosition = (tracksBleedWritePosition + 1) % tracksBleedBufferLength;

        // The reference is a per-channel average now, the same scale the
        // post-compressor power below is measured on.
        referenceBlockPower /= static_cast<float> (juce::jmax (1, activeChannels));

        // ---------------------------------------------------------------------
        //  Output stage glue compressor. It is immediately before the output trim,
        //  so the headroom it creates is spent directly on the OUTPUT control and
        //  the level leaving the plugin stays calibrated. Independent of the input
        //  stage: this detector only sees the tape output, and its operating point
        //  follows the OUTPUT trim.
        // ---------------------------------------------------------------------
        float detectorPower = 0.0f;
        if (activeChannels > 0)
        {
            for (int channel = 0; channel < activeChannels; ++channel)
            {
                const auto signal = tapeOutput[static_cast<std::size_t> (channel)];
                detectorPower += signal * signal;
            }
            detectorPower /= static_cast<float> (activeChannels);
        }

        const float envelopeDb = outputCompressor.processDetection (detectorPower, engineSampleRate,
                                                                    outputCompressorCoefficients);
        const float reductionDb = juce::jmax (outputReductionLimitDb,
                                              softKneeReductionDb (envelopeDb,
                                                                   outputThresholdDb,
                                                                   outputKneeDb,
                                                                   outputCompressorRatio));

        // The output stage's per-channel detectors, on the same terms as the input
        // stage's: advanced only when ST LINK is actually asking for them, so the
        // default linked path costs nothing extra. They see the TAPE output, which
        // is what this stage has always detected.
        std::array<float, 2> outputPerChannelReductionDb {};
        if (linkNow < 0.999f)
        {
            for (int channel = 0; channel < activeChannels; ++channel)
            {
                const auto signal = tapeOutput[static_cast<std::size_t> (channel)];
                const auto channelEnvelopeDb = outputCompressorChannels[static_cast<std::size_t> (channel)]
                    .processDetection (signal * signal, engineSampleRate,
                                       outputCompressorCoefficients);
                outputPerChannelReductionDb[static_cast<std::size_t> (channel)] =
                    juce::jmax (outputReductionLimitDb,
                                softKneeReductionDb (channelEnvelopeDb, outputThresholdDb,
                                                     outputKneeDb, outputCompressorRatio));
            }
        }
        // The output stage's three dB-to-gain conversions are also per sample. They
        // are grouped behind one guard rather than three, because the branch is
        // compile-time and the three always travel together.
#if J37_HAS_CHOWDSP_MATH
        const float compressionGain = chowdsp::DecibelsApprox::decibelsToGain (reductionDb);
#else
        const float compressionGain = juce::Decibels::decibelsToGain (reductionDb);
#endif
        peakReductionDb = juce::jmin (peakReductionDb, reductionDb);

        // Each stage pays back part of the reduction it applied, weighted by how hard
        // its trim control is driving it, so both together cannot leave the machine
        // quieter than it arrived.
#if J37_HAS_CHOWDSP_MATH
        const float inputMakeup = chowdsp::DecibelsApprox::decibelsToGain (-inputPeakReductionDb
                                                                            * inputMakeupFraction);
        const float outputMakeup = chowdsp::DecibelsApprox::decibelsToGain (-reductionDb
                                                                             * outputMakeupFraction);
#else
        const float inputMakeup = juce::Decibels::decibelsToGain (-inputPeakReductionDb
                                                                  * inputMakeupFraction);
        const float outputMakeup = juce::Decibels::decibelsToGain (-reductionDb
                                                                   * outputMakeupFraction);
#endif
        const float stageGain = compressionGain * outputMakeup * finalOutputGain * inputMakeup;

        // The same blend for the output stage's gain, per channel. Held as an
        // array so the loop below applies each side's own answer.
        std::array<float, 2> stageGainPerChannel { stageGain, stageGain };
        if (linkNow < 0.999f)
        {
            for (int channel = 0; channel < activeChannels; ++channel)
            {
                const auto unlinkedCompressionGain = juce::Decibels::decibelsToGain (
                    outputPerChannelReductionDb[static_cast<std::size_t> (channel)]);
                const auto blended = compressionGain
                                   + (unlinkedCompressionGain - compressionGain) * (1.0f - linkNow);
                stageGainPerChannel[static_cast<std::size_t> (channel)] =
                    blended * outputMakeup * finalOutputGain * inputMakeup;
            }
        }

        // ---------------------------------------------------------------------
        //  Final gain compensation.
        //
        //  The tape stage, the two glue compressors and their makeup all change the
        //  level, so what leaves the plugin is not necessarily the level the user
        //  dialled in with INPUT. This compares the finished signal against the
        //  reference taken straight after the input trim and before the first
        //  compressor, and pays back whatever was lost or gained, so the plugin holds
        //  its output level as DRIVE, TAPE TYPE, SPEED and MIX are changed instead of
        //  drifting louder or quieter with every edit.
        //
        //  The correction is smoothed with a slow one-pole, so it tracks the overall
        //  programme level rather than fighting the moment-to-moment compression. That
        //  keeps the compressors' punch intact: it corrects the average, not transients.
        // ---------------------------------------------------------------------
        float compensationGain = 1.0f;
        if (autoGainEnabled && referenceBlockPower > 0.0f)
        {
            // Power of the signal as it leaves the compressors and tape stage, but
            // BEFORE this compensation is folded in - measuring the compensated output
            // would make the correction chase its own tail.
            float postCompressorPower = 0.0f;
            for (int channel = 0; channel < activeChannels; ++channel)
            {
                const auto signal = tapeOutput[static_cast<std::size_t> (channel)]
                                  * stageGainPerChannel[static_cast<std::size_t> (channel)];
                postCompressorPower += signal * signal;
            }

            if (postCompressorPower > 1.0e-12f)
            {
                // How far the finished signal has fallen below the reference taken after
                // the input trim. Positive means level was lost and needs paying back.
                // chowdsp's polynomial dB approximation replaces libm's exp2/log2
                // chain on the auto-gain hot path; the JUCE fallback below keeps any
                // build without the chowdsp header identical in behaviour.
#if J37_HAS_CHOWDSP_MATH
                const auto levelRatioDb = chowdsp::DecibelsApprox::gainToDecibels (
                    std::sqrt (referenceBlockPower / postCompressorPower), 0.0f);
#else
                const auto levelRatioDb = juce::Decibels::gainToDecibels (
                    std::sqrt (referenceBlockPower / postCompressorPower), 0.0f);
#endif

                // Only ever restore, never exaggerate: the correction may recover a loss
                // but must not become an extra boost stage of its own.
                const auto targetCompensationDb = juce::jlimit (0.0f, 12.0f, levelRatioDb);
                smoothedCompensationDb += (targetCompensationDb - smoothedCompensationDb)
                                        * compensationCoefficient;
                compensationGain = juce::Decibels::decibelsToGain (smoothedCompensationDb);
            }
        }

        std::array<float, 2> outputSignal {};
        std::array<float, 2> machineDry {};
        for (int channel = 0; channel < activeChannels; ++channel)
        {
            const auto index = static_cast<std::size_t> (channel);
            outputSignal[index] = tapeOutput[index]
                                * stageGainPerChannel[index] * compensationGain;

            // The same signal at MIX 0. Because it carries the identical
            // stageGain * compensationGain, everything that is gain rather than
            // character cancels in the subtraction below and DELTA shows only what
            // the machine ADDS. Subtracting the raw host sample instead left all of
            // it in the difference: the input trim, both compressors, both makeups,
            // the compensator and the output trim all read as "character", so a
            // perfectly dry MIX 0 still monitored as a loud level change rather than
            // silence.
            machineDry[index] = machineDryInput[index]
                              * stageGainPerChannel[index] * compensationGain;
        }

        // -------------------------------------------------------------------
        //  SUBFUND joins here: the one point in the chain where the undertones
        //  can enter without anything downstream turning them into harmonics of
        //  themselves.
        //
        //  Upstream of here sit the three stages that caused the fault - the
        //  bias-compression curve, the wow and flutter modulation and the output
        //  glue compressor - which is why the sum was held back rather than added
        //  at the shaper. Downstream of here every stage is linear or gain-only:
        //  the stereo width, the OUTPUT trim, the safety limiter and the bypass
        //  crossfade. The limiter still sees the undertones, so a genuine
        //  overshoot is caught by it and the soft clipper stays a safety net
        //  rather than a shaper of them.
        //
        //  Two scalings keep the control meaning what it says:
        //
        //    - `wetGain`, so MIX 0 is the untouched input and the undertones go
        //      with it, exactly like the rest of the tape path;
        //    - `finalOutputGain`, the machine's static output calibration, so the
        //      knob holds the same level in the mix that it held when the sum ran
        //      through the whole chain. The DYNAMIC part of the glue stage is
        //      deliberately left out: a per-sample gain riding on the undertones
        //      would pump them at the note rate, which is the same fault as letting
        //      the compressor distort them.
        // -------------------------------------------------------------------
        for (int channel = 0; channel < activeChannels; ++channel)
            outputSignal[static_cast<std::size_t> (channel)] +=
                undertoneOutput[static_cast<std::size_t> (channel)] * wetGain * finalOutputGain;

        // -------------------------------------------------------------------
        //  OUT EQ - the output equaliser, at the other end of the machine.
        //
        //  It sits after everything the machine does - the tape, both glue
        //  stages, the SUBFUND undertones - and before the width, the output
        //  trim, the anti-phase guard and the limiter. That means it corrects
        //  the RESULT rather than the input: nothing downstream responds to what
        //  it does, so it is the neutral, predictable EQ - the one you use to
        //  place the finished sound. The input EQ is the one that changes what
        //  the machine does; this is the one that changes what comes out.
        //
        //  It runs before the limiter deliberately: a boost here is caught by
        //  the protection chain like any other level, rather than being applied
        //  after the ceiling and forcing the clipper to work.
        // -------------------------------------------------------------------
        for (int channel = 0; channel < activeChannels; ++channel)
        {
            outputEq.lowGain = outputEqLowSmoothed.getCurrentValue();
            outputEq.midGain = outputEqMidSmoothed.getCurrentValue();
            outputEq.highGain = outputEqHighSmoothed.getCurrentValue();
            outputSignal[static_cast<std::size_t> (channel)] =
                outputEq.process (outputSignal[static_cast<std::size_t> (channel)],
                                  channel, outputEqMidCoefficient);
        }

        // -------------------------------------------------------------------
        //  NEURAL stage.
        //
        //  A learned model run as a per-channel nonlinearity, in the wet path
        //  and after the machine's own curve. It is deliberately placed AFTER
        //  the tape and BEFORE the transient shaper: a model that has been
        //  trained on a piece of hardware is itself a saturation curve, so it
        //  belongs beside the hand-written one rather than after the envelope
        //  stage - and running it before the shaper means the shaper can still
        //  sharpen whatever the model produced, which is the order an engineer
        //  would use the two.
        //
        //  At NEURAL MIX 0, or with no model loaded, this is a bit-for-bit
        //  pass-through, so a build without a model is exactly the plugin it was
        //  before this stage existed. The model is read-only at run time; the
        //  per-channel state lives in each stage.
        // -------------------------------------------------------------------
        const float neuralMixNow = neuralMixSmoothed.getCurrentValue();
        if (neuralMixNow > 1.0e-5f)
        {
            for (int channel = 0; channel < activeChannels; ++channel)
            {
                auto& neural = channel == 0 ? neuralL : neuralR;
                outputSignal[static_cast<std::size_t> (channel)] =
                    neural.process (outputSignal[static_cast<std::size_t> (channel)], neuralMixNow);
            }
        }

        // -------------------------------------------------------------------
        //  TRANSIENT SHAPER.
        //
        //  Attack and sustain, applied to the finished wet signal. It is the
        //  only stage in the chain that changes the ENVELOPE rather than the
        //  waveform, which is why it sits here: it moves the level of events
        //  without adding a harmonic, so the character the machine and the model
        //  just produced survives intact.
        //
        //  At both amounts zero the stage is transparent - the envelopes still
        //  run so the controls do not restart from rest, but the applied gain
        //  stays exactly 1. TRANSIENT MIX crossfades the shaped signal against
        //  the unshaped one, so the stage can be blended rather than switched.
        // -------------------------------------------------------------------
        const float transientMixNow = transientMixSmoothed.getCurrentValue();
        const float transientAttackNow = transientAttackSmoothed.getCurrentValue();
        const float transientSustainNow = transientSustainSmoothed.getCurrentValue();

        if (transientMixNow > 1.0e-5f
            && (std::abs (transientAttackNow) > 1.0e-5f || std::abs (transientSustainNow) > 1.0e-5f))
        {
            for (int channel = 0; channel < activeChannels; ++channel)
            {
                auto& shaper = channel == 0 ? transientL : transientR;
                const auto index = static_cast<std::size_t> (channel);

                const float shaped = shaper.process (outputSignal[index],
                                                     transientAttackNow, transientSustainNow,
                                                     transientFastCoefficient,
                                                     transientSlowCoefficient,
                                                     transientGainCoefficient);

                // The wet/dry crossfade is the MIX control: the shaper is the
                // wet leg, the untouched signal the dry one, so MIX 0 is exactly
                // the plugin without the stage.
                outputSignal[index] += (shaped - outputSignal[index]) * transientMixNow;
            }
        }

        if (activeChannels == 2)
        {
            // ------------------------------------------------------------------
            //  MODELED TRACKS: the bleed between the two heads.
            //
            //  Each channel reads what the OTHER wrote a few samples ago and
            //  adds a little of it to itself - the fringing field reaching the
            //  neighbouring track. It runs BEFORE the width stage because it is
            //  part of what the machine put on the tape, not part of how the
            //  image is presented: the width control is the user's choice, the
            //  bleed is the layout's consequence.
            //
            //  The writes happen FIRST, for both channels, so the read below
            //  always sees the other channel's CURRENT frame rather than a frame
            //  that has already been overwritten by this one. That is the same
            //  discipline the ST OFFSET buffer uses and for the same reason.
            //
            //  At a two-track layout with the crosstalk figures above the leak
            //  is 0.010 to 0.055 - about -40 to -25 dB, which is the range real
            //  track-to-track bleed sits in. It is a small number on purpose: it
            //  should be felt as the image sitting slightly differently, not
            //  heard as anything at all.
            // ------------------------------------------------------------------
            tracksBleedBuffer[0][static_cast<std::size_t> (tracksBleedWritePosition)]
                = outputSignal[0];
            tracksBleedBuffer[1][static_cast<std::size_t> (tracksBleedWritePosition)]
                = outputSignal[1];

            // The read position, wrapped rather than clamped: the ring is a
            // circle, and the delay is always well under its length.
            int bleedReadPosition = tracksBleedWritePosition - trackBleedDelaySamples;
            if (bleedReadPosition < 0)
                bleedReadPosition += tracksBleedBufferLength;

            // Each channel takes the OTHER channel's delayed sample. This is what
            // makes the two sides of a three-track deck sound slightly closer
            // together: the leak is symmetric, so the image narrows a touch
            // without either side gaining on the other.
            const float bleedToLeft  = tracksBleedBuffer[1][static_cast<std::size_t> (bleedReadPosition)];
            const float bleedToRight = tracksBleedBuffer[0][static_cast<std::size_t> (bleedReadPosition)];

            outputSignal[0] += (bleedToLeft  - outputSignal[0]) * trackCrosstalk;
            outputSignal[1] += (bleedToRight - outputSignal[1]) * trackCrosstalk;

            const float mid = 0.5f * (outputSignal[0] + outputSignal[1]);
            const float side = 0.5f * (outputSignal[0] - outputSignal[1]) * currentWidth;
            outputSignal[0] = mid + side;
            outputSignal[1] = mid - side;

            // ------------------------------------------------------------------
            //  Anti-phase prevention.
            //
            //  A stereo signal whose two sides are largely in OPPOSITE polarity
            //  cancels when the mix is folded to mono - the classic cause being an
            //  inverted source, a mis-wired cable, or the plugin's own modulation
            //  and undertone generators landing out of step across the pair. It is
            //  inaudible in stereo and then the low end and the centre of the image
            //  simply disappear the moment anything downstream sums the channels:
            //  a broadcast mono fold-down, a club PA, a phone speaker, a mono
            //  mastering check.
            //
            //  The guard measures how much of the frame's energy sits in the SIDE
            //  (the difference) relative to the MID (the sum), using the smoothed
            //  correlation of the two channels. When the two are genuinely in
            //  anti-phase the correlation goes negative, and the guard rotates the
            //  right channel's polarity back toward the left's - in proportion to
            //  how wrong it is, over a slow ramp, so a real stereo image is never
            //  touched and a genuinely inverted channel is pulled back over tens of
            //  milliseconds rather than switched.
            //
            //  It deliberately does NOT touch the width control: WIDTH can widen
            //  the image as far as the user likes, because a wide image is a
            //  legitimate stereo choice. What this prevents is the OTHER thing -
            //  the sides being in OPPOSITE polarity, which is not width at all.
            // ------------------------------------------------------------------
            const float frameMid = outputSignal[0] + outputSignal[1];
            const float frameSide = outputSignal[0] - outputSignal[1];

            // The correlation envelope: a slow one-pole on the difference between
            // the mid power and the side power. Positive means the frame's energy
            // sits in the SUM (the sides agree - normal stereo), negative means it
            // sits in the DIFFERENCE (the sides oppose - the fault).
            const float frameEnergy = frameMid * frameMid - frameSide * frameSide;
            antiPhaseProduct += (frameEnergy - antiPhaseProduct) * antiPhaseCoefficient;

            // The normaliser tracks the frame's total energy on the same pole, so
            // the guard measures a RATIO and cannot be fooled by the programme
            // simply being louder or quieter.
            const float frameMagnitude = frameMid * frameMid + frameSide * frameSide;
            antiPhaseProductMagnitude += (frameMagnitude - antiPhaseProductMagnitude)
                                       * antiPhaseCoefficient;
            antiPhaseProductMagnitude = juce::jmax (1.0e-9f, antiPhaseProductMagnitude);

            // A bounded correction: only an actual negative correlation pulls the
            // guard into action, and it can only ever REDUCE the opposition, never
            // invert a healthy image.
            const float opposition = antiPhaseProduct < 0.0f
                ? juce::jlimit (0.0f, 1.0f, -antiPhaseProduct / antiPhaseProductMagnitude)
                : 0.0f;
            antiPhaseCorrection += (opposition - antiPhaseCorrection) * antiPhaseCoefficient;

            antiPhaseAmount.store (antiPhaseCorrection, std::memory_order_relaxed);

            // The rotation: as the correction grows, the right channel is blended
            // toward its own inverted form, which pulls an opposing pair back into
            // agreement. At correction 0 this is bit-for-bit the untouched signal.
            if (antiPhaseCorrection > 1.0e-5f)
            {
                const float rotated = outputSignal[1] * (1.0f - 2.0f * antiPhaseCorrection);
                outputSignal[1] += (rotated - outputSignal[1]) * antiPhaseCorrection;
            }
        }
        else
        {
            // Mono: there is no pair to be out of phase, so the guard's state is
            // allowed to relax rather than freezing at whatever the last stereo
            // block left it at.
            antiPhaseCorrection += (0.0f - antiPhaseCorrection) * antiPhaseCoefficient;
            antiPhaseAmount.store (antiPhaseCorrection, std::memory_order_relaxed);
        }

        // Apply the output trim before limiting, not after. The limiter has to be the last
        // thing that touches the level, otherwise a boost on the OUTPUT control would push
        // the signal straight past the ceiling it just established and the clipper would
        // be doing the work instead.
        for (int channel = 0; channel < activeChannels; ++channel)
            outputSignal[static_cast<std::size_t> (channel)] *= outputGain;

        // ---------------------------------------------------------------------
        //  Program-dependent safety limiter.
        //
        //  This exists so the soft clipper downstream stays idle. It watches the loudest
        //  channel of the frame and pulls the gain back whenever the signal is heading for
        //  the ceiling, then releases slowly enough to stay musical.
        //
        //  It is a GAIN stage rather than a shaper, so it adds no harmonics of its own:
        //  the only distortion in the output is the tape stage's, which is intentional.
        //  Normal material is limited transparently and only genuinely excessive level
        //  ever reaches the clipper.
        // ---------------------------------------------------------------------
        float framePeak = 0.0f;
        for (int channel = 0; channel < activeChannels; ++channel)
            framePeak = juce::jmax (framePeak, std::abs (outputSignal[static_cast<std::size_t> (channel)]));

        // A very fast attack catches transients before they overshoot; the release is
        // short enough to recover between events without pumping on sustained material.
        //
        // All four coefficients are BLOCK-RATE constants, built above the loop. They
        // used to be rebuilt here, which cost four std::exp per sample per channel -
        // and because the gain smoothing is a ternary, BOTH of its branches were
        // evaluated every sample and one thrown away.
        const auto detectorCoefficient = framePeak > preLimiterDetector ? limiterDetectorAttack
                                                                        : limiterDetectorRelease;
        preLimiterDetector += (framePeak - preLimiterDetector) * detectorCoefficient;

        // Gain required to bring the peak back to the ceiling. It is smoothed separately
        // from the detector so the correction is continuous and cannot click.
        const auto requiredGain = preLimiterDetector > limiterCeiling
                                    ? limiterCeiling / preLimiterDetector
                                    : 1.0f;
        // 0.4 ms catch, 120 ms recovery. The asymmetry is the point: a limiter
        // must catch instantly and let go slowly, or it pumps.
        const auto gainSmoothing = requiredGain < limiterGain ? limiterCatchCoefficient
                                                              : limiterRecoveryCoefficient;
        limiterGain += (requiredGain - limiterGain) * gainSmoothing;

        for (int channel = 0; channel < activeChannels; ++channel)
            outputSignal[static_cast<std::size_t> (channel)] *= limiterGain;

        for (int channel = 0; channel < activeChannels; ++channel)
        {
            const auto index = static_cast<std::size_t> (channel);
            auto& destination = channelData[index][sample];

            // DELTA listen: the finished signal minus the machine's own dry
            // reference. Both legs are computed on this frame, in this loop, from
            // the same input sample, so the subtraction is phase-perfect at every
            // oversampling factor with no compensation delay of its own. The
            // reference is the signal the machine WOULD have produced with the tape
            // stage silent - same input trim, same input compressor, same static
            // output gain - so what is left is character, not level.
            const auto delta = outputSignal[index] - machineDry[index];

            // Normal monitoring: the bypass crossfade, from the untouched host sample
            // to the finished signal. DELTA applies the SAME crossfade to the
            // difference rather than replacing it, so engaging BYPASS while DELTA
            // listens ramps the difference to silence instead of snapping to the dry
            // signal - and the early return above is held off until that ramp has
            // finished, so there is no step at the end of it either.
            const auto monitored = destination + (outputSignal[index] - destination) * bypassMix;
            const auto deltaMonitored = delta * bypassMix;

            // The two monitor positions themselves are crossfaded by the smoothed
            // DELTA ramp, so switching the button in or out is a 20 ms fade between
            // two continuous signals rather than a step. The meters keep reading the
            // finished output path below, so in delta mode they show the level of what
            // the machine adds.
            const auto blended = monitored + (deltaMonitored - monitored) * deltaMix;

            // Protection for the output, in two stages:
            //
            //   1. The safety limiter above keeps normal programme below the ceiling.
            //   2. The soft clipper only bends what still overshoots - a peak faster than
            //      the limiter's attack - so it is the last resort, not the mechanism that
            //      keeps the level in range.
            //
            // The flag therefore means "the soft clipper had to act", not merely "the
            // signal was loud", so the meter warns about real distortion.
            if (std::abs (blended) > 0.985f)
                clippingThisBlock = true;

            // ------------------------------------------------------------------
            //  LO-FI mode: the deliberate degradation.
            //
            //  Where MODERN opens the machine up, LO-FI closes it down: a hard
            //  bandwidth limit, a sample-and-hold quantisation (the bit-crush that
            //  gives the mode its grit) and a raised noise floor. It runs on the
            //  finished sample, after the protection chain, so the quantisation
            //  cannot be smoothed away by the limiter - the point of the mode is
            //  that it is a fault, and a fault should survive to the output.
            // ------------------------------------------------------------------
            float modeProcessed = blended;

            if (lofiMode)
            {
                auto& lofiLow = channel == 0 ? lofiLowL : lofiLowR;
                auto& lofiHold = channel == 0 ? lofiHoldL : lofiHoldR;

                // The bandwidth limit: a 3.2 kHz one-pole, which is the telephone/
                // cheap-radio band and the single most recognisable part of the
                // mode.
                lofiLow += (modeProcessed - lofiLow) * lofiLowCoefficient;

                // The sample-and-hold: the signal is held for a few samples, which
                // is a sample-rate reduction rather than a bit-depth one - it is
                // the aliasing that gives the mode its edge. The hold counter is
                // advanced once per frame by the caller, so both channels hold the
                // same instants and the image does not smear.
                // The counter is shared so both channels hold on the SAME instants -
                // per-channel counters would sample the two sides at different times
                // and smear the image. Each channel keeps its own held VALUE.
                if (channel == 0 && --lofiCounter <= 0)
                    lofiCounter = juce::jmax (1, lofiHoldSamples);

                if (lofiCounter >= lofiHoldSamples || channel == 0)
                    lofiHold = lofiLow;

                modeProcessed = lofiHold;
            }

            // The polarity switch flips the finished sample after the clipper, so a
            // 180-degree source mis-wiring is corrected at the very last stage.
            const auto limitedOut = softClip (modeProcessed) * polaritySign;
            destination = limitedOut;
            outputPeak = juce::jmax (outputPeak, std::abs (limitedOut));
            outputSquares += static_cast<double> (limitedOut) * limitedOut;

            // VU ballistics read the magnitude, so a polarity flip cannot make the
            // meter lie about the programme level.
            vuAverage += (std::abs (limitedOut) - vuAverage) * vuBallisticCoefficient;
        }

        // K-weighted loudness runs on the final stereo frame, after the width stage, so
        // it reports what actually leaves the plugin.
        //
        // It reads the channels back out of the buffer rather than off outputSignal,
        // for two reasons. The OUTPUT trim was already applied to outputSignal above,
        // so metering `outputSignal * outputGain` - which is what this used to do -
        // applied that trim a second time: the meter read 6 dB high for every decibel
        // of boost on OUTPUT, on a control that has nothing to do with loudness. And
        // the buffer holds the finished sample - the limiter's gain, the soft clipper
        // and the bypass crossfade have all run by this point - so this is the only
        // one of the three reads that is the signal the host actually receives. The
        // INPUT meter below reads its channels the same way.
        if (activeChannels == 2)
            currentLufs = outputLoudness.processFrame (channelData[0][sample],
                                                       channelData[1][sample],
                                                       outputLoudnessCoefficient);
        else if (activeChannels == 1)
            currentLufs = outputLoudness.processFrame (channelData[0][sample],
                                                       channelData[0][sample],
                                                       outputLoudnessCoefficient);

        // The INPUT meter runs the same K-weighting on the raw signal at the plugin's
        // own input, so the two meters can be compared directly. The channels are read
        // back from the buffer because the dry input was overwritten in place.
        if (activeChannels == 2)
            currentInputLufs = inputLoudness.processFrame (inputChainHistory[0], inputChainHistory[1],
                                                           inputLoudnessCoefficient);
        else if (activeChannels == 1)
            currentInputLufs = inputLoudness.processFrame (inputChainHistory[0], inputChainHistory[0],
                                                           inputLoudnessCoefficient);
    }

    // -------------------------------------------------------------------------
    //  Post-machine stages: reverb, then vinyl.
    //
    //  Both run AFTER the per-sample loop, on the finished block, for one reason:
    //  neither belongs inside the tape nonlinearity. A reverb inside the wow
    //  modulation would be pitch-shifted with it and would smear the harmonics
    //  the plugin exists to produce; a turntable is the last thing in the signal
    //  path, so its surface noise must not be recorded onto the tape.
    //
    //  They are a second pass over the block rather than part of the main loop
    //  because the reverb is a STEREO processor - its two comb banks have to see
    //  both channels to produce the width - and the main loop is per-channel.
    // -------------------------------------------------------------------------
    const float reverbMixNow = reverbMixSmoothed.getCurrentValue();
    const float vinylNow = vinylSmoothed.getCurrentValue();

    if (activeChannels > 0
        && (reverbMixNow > 1.0e-5f || vinylNow > 1.0e-5f))
    {
        const float reverbSizeNow = reverbSizeSmoothed.getCurrentValue();
        const float crackleNow = vinylCrackleSmoothed.getCurrentValue() * vinylNow;
        const float rumbleNow = vinylRumbleSmoothed.getCurrentValue() * vinylNow;

        for (int sample = 0; sample < numSamples; ++sample)
        {
            for (int channel = 0; channel < activeChannels; ++channel)
            {
                float value = channelData[static_cast<std::size_t> (channel)][sample];

                // -- Reverb: the room the machine is in --------------------------
                // It is applied to the finished signal, so what reverberates is
                // the record rather than the performance.
                if (reverbMixNow > 1.0e-5f)
                    value = reverb.process (value, channel, reverbSizeNow, reverbMixNow);

                // -- Vinyl: surface, rumble and the RIAA playback character ------
                // Each channel has its own VinylStage, so the crackle and the
                // rumble are uncorrelated between the sides - sharing a generator
                // would put every tick in the centre of the image instead of on
                // the surface.
                if (vinylNow > 1.0e-5f)
                {
                    auto& vinyl = channel == 0 ? vinylL : vinylR;
                    // NOISE LVL scales the vinyl noise the same way it scales the
                    // tape floor: the crackle and rumble CONTROLS set how much of
                    // each source the record has, NOISE LVL sets how loud the
                    // sources themselves are. NOISE (the mix) then sets how much
                    // of that noise section reaches the output - the same
                    // proportion the tape floor rides, so the two noise
                    // departments answer to the same two controls. Applied here
                    // rather than inside the stage so the stage's own type
                    // voicing stays untouched by the shared trims.
                    const float vinylNoiseLevel = noiseLvlSmoothed.getCurrentValue()
                                                * noiseTrimSmoothed.getCurrentValue();
                    value = vinyl.process (value, channel,
                                           crackleNow * vinylNoiseLevel,
                                           rumbleNow * vinylNoiseLevel, vinylNow,
                                           vinylRumbleCoefficient, vinylWarmthCoefficient,
                                           vinylNoiseState);
                }

                channelData[static_cast<std::size_t> (channel)][sample] = value;
            }
        }
    }

    const auto measuredSamples = static_cast<double> (numSamples)
                               * static_cast<double> (juce::jmax (1, activeChannels));
    const float inputRms = measuredSamples > 0.0
        ? static_cast<float> (std::sqrt (inputSquares / measuredSamples)) : 0.0f;
    const float outputRms = measuredSamples > 0.0
        ? static_cast<float> (std::sqrt (outputSquares / measuredSamples)) : 0.0f;

    // Compressor-coupled saturation drive update (see the note at driveAmount): the
    // total gain reduction both stages applied this block is smoothed into
    // squeezeSaturationDrive over about 200 ms, so the tape stage reads programme
    // density rather than individual transients. Zero squeeze leaves the shaper
    // exactly as the DRIVE control set it; a slammed programme grows it smoothly.
    {
        const auto totalSqueezeDb = juce::jlimit (0.0f, 12.0f,
                                                  -(inputPeakReductionDb + peakReductionDb));
        const auto targetSqueeze = 1.0f - std::exp (-totalSqueezeDb * 0.20f);
        squeezeSaturationDrive += (targetSqueeze - squeezeSaturationDrive)
                                * squeezeDriveSmoothing;
    }

    // Noise-floor levelling is deliberately absent. See the note in the tape loop:
    // the hiss is a constant band-limited floor, not a programme-tracking one.

    const auto retainPeakUntilConsumed = [] (std::atomic<float>& publishedPeak, float blockPeak)
    {
        auto accumulatedPeak = publishedPeak.load (std::memory_order_relaxed);
        while (accumulatedPeak < blockPeak
               && ! publishedPeak.compare_exchange_weak (accumulatedPeak, blockPeak,
                                                          std::memory_order_relaxed,
                                                          std::memory_order_relaxed))
        {
        }
    };

    retainPeakUntilConsumed (inputPeakLevel, inputPeak);
    inputRmsLevel.store (inputRms, std::memory_order_relaxed);
    retainPeakUntilConsumed (outputPeakLevel, outputPeak);
    outputRmsLevel.store (outputRms, std::memory_order_relaxed);

    // -------------------------------------------------------------------------
    //  Four-way loudness meter.
    //
    //  Each view is converted to dB and then averaged with equal weight, so no single
    //  scale can dominate the combined reading:
    //
    //    dB   - true peak of the block, the only view that reports clipping
    //    RMS  - electrical average of the whole block
    //    LUFS - K-weighted, so it tracks perceived loudness rather than voltage
    //    VU   - 300 ms ballistic average, the classic programme-level display
    //
    //  Averaging in dB (rather than linear) keeps the four views comparable, because
    //  all four are already level scales; converting to linear first would let a single
    //  silent view drag the result toward -infinity.
    // -------------------------------------------------------------------------
    const auto outputPeakDbValue = juce::Decibels::gainToDecibels (outputPeak, -70.0f);
    const auto outputRmsDbValue = juce::Decibels::gainToDecibels (outputRms, -70.0f);
    const auto outputVuDbValue = juce::Decibels::gainToDecibels (vuAverage, -70.0f);

    constexpr float viewWeight = 0.25f;
    const auto combinedDb = outputPeakDbValue * viewWeight
                          + outputRmsDbValue * viewWeight
                          + currentLufs * viewWeight
                          + outputVuDbValue * viewWeight;

    outputPeakDb.store (outputPeakDbValue, std::memory_order_relaxed);
    outputRmsDb.store (outputRmsDbValue, std::memory_order_relaxed);
    outputLufs.store (currentLufs, std::memory_order_relaxed);
    outputVuDb.store (outputVuDbValue, std::memory_order_relaxed);
    outputCombinedDb.store (combinedDb, std::memory_order_relaxed);
    outputClipping.store (clippingThisBlock, std::memory_order_relaxed);

    // Same four-way treatment for the input side, so the two meters are directly
    // comparable: the difference between them is what the plugin did to the level.
    const auto inputPeakDbValue = juce::Decibels::gainToDecibels (inputPeak, -70.0f);
    const auto inputRmsDbValue = juce::Decibels::gainToDecibels (inputRms, -70.0f);
    const auto inputVuDbValue = juce::Decibels::gainToDecibels (inputVuAverage, -70.0f);

    const auto inputCombinedValue = inputPeakDbValue * viewWeight
                                  + inputRmsDbValue * viewWeight
                                  + currentInputLufs * viewWeight
                                  + inputVuDbValue * viewWeight;

    inputPeakDb.store (inputPeakDbValue, std::memory_order_relaxed);
    inputRmsDb.store (inputRmsDbValue, std::memory_order_relaxed);
    inputLufs.store (currentInputLufs, std::memory_order_relaxed);
    inputVuDb.store (inputVuDbValue, std::memory_order_relaxed);
    inputCombinedDb.store (inputCombinedValue, std::memory_order_relaxed);
    inputClipping.store (inputClippingThisBlock, std::memory_order_relaxed);

    // Subharmonic telemetry: what the undertone cascade is doing. Published once
    // per block from the LEFT generator, because the two are locked to the same
    // note by construction - the anti-phase protection keeps them together, and
    // reporting both would only invite the reader to compare two numbers that are
    // supposed to agree.
    subfundTrackedHz.store (subharmonicL.getTrackedFrequency (engineSampleRate),
                            std::memory_order_relaxed);
    subfundConfidence.store (subharmonicL.getCycleConfidence(), std::memory_order_relaxed);

    // Harmonic character, measured on the shaper earlier in the block. Published so the
    // panel can show the even/odd balance the tape stage is actually producing.
    evenHarmonicRatio.store (harmonicAnalyser.getEvenRatio(), std::memory_order_relaxed);
    oddHarmonicRatio.store (harmonicAnalyser.getOddRatio(), std::memory_order_relaxed);

    // Blocks of zero samples arrive during silence (and with some host buffer sizes),
    // and by then the per-sample smoothing would never have been advanced.
    inputGainSmoothed.skip (numSamples);
    outputGainSmoothed.skip (numSamples);
    mixSmoothed.skip (numSamples);
    widthSmoothed.skip (numSamples);
    bypassSmoothed.skip (numSamples);

    // Glue compressor telemetry: worst-case reduction this block plus an activity
    // envelope the UI can animate, both read without locking. Each stage publishes
    // its own reduction and activity so the editor can give it a dedicated meter.
    inputGainReductionDb.store (inputPeakReductionDb, std::memory_order_relaxed);
    outputGainReductionDb.store (peakReductionDb, std::memory_order_relaxed);
    inputCompressorActivity.store (juce::jlimit (0.0f, 1.0f, inputEnvelopeActivity),
                                   std::memory_order_relaxed);
    outputCompressorActivity.store (juce::jlimit (0.0f, 1.0f,
                                                  outputCompressor.getEnvelopeActivity()),
                                    std::memory_order_relaxed);
    compressorActivity.store (juce::jlimit (0.0f, 1.0f,
                                            juce::jmax (inputEnvelopeActivity,
                                                        outputCompressor.getEnvelopeActivity())),
                              std::memory_order_relaxed);

    // Transport drift mapped to 0..1 for the UI wobble, and a harmonic weight
    // derived from how hard the input is being driven into the tape curve.
    transportDrift.store (juce::jlimit (0.0f, 1.0f, 0.5f + driftAccumulator * 2.0f),
                          std::memory_order_relaxed);
    const float driveInto = juce::jlimit (0.0f, 1.0f, inputRms * inputGainSmoothed.getCurrentValue()
                                                          * (0.5f + driveCurve));
    harmonicCharacter.store (driveInto, std::memory_order_relaxed);

    // -------------------------------------------------------------------------
    //  Publish one complete telemetry frame.
    //
    //  This is the LAST thing the block does, so every value in the frame is the
    //  final value for this block - the meters, the drift, the harmonics, the
    //  subfund tracker and the anti-phase guard have all finished writing. With
    //  the lock-free queue present, this ONE push is what the editor reads: the
    //  fields cannot be read half-updated, because they are pushed together.
    //
    //  The individual atomics are still written, because the editor falls back to
    //  them when the queue is not compiled in and because they are what the DSP
    //  harness and any future non-UI consumer read. Keeping both costs a handful of
    //  relaxed stores per block and means neither path can be the stale one.
    // -------------------------------------------------------------------------
    {
        TelemetryFrame frame;
        frame.inputPeakDb = inputPeakDb.load (std::memory_order_relaxed);
        frame.inputRmsDb = inputRmsDb.load (std::memory_order_relaxed);
        frame.inputLufs = inputLufs.load (std::memory_order_relaxed);
        frame.inputVuDb = inputVuDb.load (std::memory_order_relaxed);
        frame.inputCombinedDb = inputCombinedDb.load (std::memory_order_relaxed);
        frame.inputClipping = inputClipping.load (std::memory_order_relaxed);

        frame.outputPeakDb = outputPeakDb.load (std::memory_order_relaxed);
        frame.outputRmsDb = outputRmsDb.load (std::memory_order_relaxed);
        frame.outputLufs = outputLufs.load (std::memory_order_relaxed);
        frame.outputVuDb = outputVuDb.load (std::memory_order_relaxed);
        frame.outputCombinedDb = outputCombinedDb.load (std::memory_order_relaxed);
        frame.outputClipping = outputClipping.load (std::memory_order_relaxed);

        frame.inputGainReductionDb = inputPeakReductionDb;
        frame.outputGainReductionDb = peakReductionDb;
        frame.inputCompressorActivity = inputEnvelopeActivity;
        frame.outputCompressorActivity = outputCompressor.getEnvelopeActivity();
        frame.compressorActivity = juce::jmax (inputEnvelopeActivity,
                                               outputCompressor.getEnvelopeActivity());

        frame.transportDrift = transportDrift.load (std::memory_order_relaxed);
        frame.harmonicCharacter = driveInto;
        frame.evenHarmonicRatio = evenHarmonicRatio.load (std::memory_order_relaxed);
        frame.oddHarmonicRatio = oddHarmonicRatio.load (std::memory_order_relaxed);
        frame.subfundTrackedHz = subfundTrackedHz.load (std::memory_order_relaxed);
        frame.subfundConfidence = subfundConfidence.load (std::memory_order_relaxed);
        frame.antiPhaseAmount = antiPhaseCorrection;
        frame.transportRamp = transportRampPublished.load (std::memory_order_relaxed);
        frame.spindownRamp = spindownRampPublished.load (std::memory_order_relaxed);
        frame.bypassActive = bypassActive.load (std::memory_order_relaxed);

#if J37_HAS_RWQ
        // Non-blocking: if the editor has not drained the ring, the OLDEST frame
        // is dropped rather than the audio thread waiting. An old meter reading is
        // worthless, so dropping is the right answer. pop() first is not needed -
        // ReaderWriterQueue's try_enqueue overwrites nothing and simply fails when
        // full, so a full ring stops being a problem the moment the editor reads.
        if (! telemetryQueue.try_enqueue (frame))
        {
            // Full ring: make room by discarding the oldest frame, then retry once.
            TelemetryFrame discarded;
            telemetryQueue.try_dequeue (discarded);
            telemetryQueue.try_enqueue (frame);
        }
#endif

        // The always-present mirror. These are the three the editor needs even when
        // the queue is absent, and the values a host-side meter would read.
        telemetryInputPeakDb.store (frame.inputPeakDb, std::memory_order_relaxed);
        telemetryOutputPeakDb.store (frame.outputPeakDb, std::memory_order_relaxed);
        telemetryInputRmsDb.store (frame.inputRmsDb, std::memory_order_relaxed);
        telemetryOutputRmsDb.store (frame.outputRmsDb, std::memory_order_relaxed);
        telemetryAntiPhase.store (frame.antiPhaseAmount, std::memory_order_relaxed);
        telemetryPlatter.store (frame.transportRamp, std::memory_order_relaxed);
    }
}

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
    // A model is installed on BOTH channel stages at once. They share the same
    // model object, but each holds its own recurrent state, and a load must not
    // leave one channel on the old model and the other on the new - which is a
    // stereo mismatch in the nonlinearity itself, the one place it cannot be
    // tolerated. The parse happens on the message thread; the audio thread only
    // ever sees the finished pointer.
    const bool loaded = neuralL.loadFromJson (modelJson);

    if (loaded)
        neuralR.loadFromJson (modelJson);
    else
        neuralR.clear();

    return loaded;
}

void FirstAudioProcessor::clearNeuralModel()
{
    neuralL.clear();
    neuralR.clear();
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
