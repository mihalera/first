/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <array>
#include <cstdint>

namespace
{
    constexpr float minTrack = 0.0f;
    constexpr float maxTrack = 1.0f;

    // Symmetric decibel range shared by the input and output stage controls.
    constexpr float minInputDb = -32.0f;
    constexpr float maxInputDb = 32.0f;

    /**
        Soft magnetic hysteresis: a memory-dependent shaping that produces the
        asymmetric, mostly-odd/even blend of analogue tape rather than a plain
        symmetric tanh curve. `memory` is the previous shaped output.

        Harmonics are the whole point of this function, so the structure is deliberate:

          - A symmetric tanh generates only ODD harmonics (3rd, 5th...), which read as
            "harder" or "edgier".
          - The asymmetry offset biases the curve so it is no longer symmetric about
            zero. That is what generates EVEN harmonics (2nd, 4th...), and even
            harmonics are the ones heard as warmth, body and "bigger than the source".
          - Two tanh stages with different slopes mean the harmonic content grows
            gradually with level instead of switching on at a threshold, so quiet
            passages stay clean and loud ones bloom - the behaviour of real tape.
    */
    inline float magneticHysteresis (float x, float drive, float asymmetry, float memory)
    {
        const float biased = x + asymmetry;

        // ------------------------------------------------------------------
        //  One saturating branch, normalised so it is unity-slope at the origin.
        //
        //  The previous version summed three tanh terms whose weights added to 1.0. Each
        //  term saturated independently, so the curves stacked and the transfer compressed
        //  a full-scale input to 0.67 even with DRIVE at zero - the plugin saturated at
        //  every setting, which is what made it sound overdriven no matter what.
        //
        //  `tanh (s*x) / s` is the right shape: its slope at zero is exactly 1, so quiet
        //  signals pass through untouched, and its asymptote is 1/s, so the amount of
        //  compression at the top is set purely by s - which is what DRIVE controls. With
        //  DRIVE at zero, s is 1 and the curve is a gentle, single-tanh tape bend rather
        //  than three stacked ones.
        // ------------------------------------------------------------------
        const float slope = 1.0f + drive * 2.6f;
        const float hard = std::tanh (biased * slope) / slope;

        // The delayed image gives tape its "sticky" transient behaviour. It shares the
        // same unity-slope normalisation so it contributes character without costing
        // level, and it is scaled by the drive amount so it cannot bend at zero drive.
        const float lagged = memory * 0.62f;
        const float delayedSlope = 0.55f + drive * 1.0f;
        const float delayed = std::tanh ((biased * delayedSlope) + lagged) / delayedSlope;

        // A blend, not a sum: the weights add to one so the two branches average rather
        // than stacking their compression.
        constexpr float hardWeight = 0.70f;
        constexpr float delayedWeight = 0.30f;

        const float blended = hard * hardWeight + delayed * delayedWeight;

        // Remove the bias offset asymmetrically so the effect adds even harmonics instead
        // of merely shifting the signal. The squared term makes the asymmetry
        // level-dependent, mirroring how real bias interacts with signal amplitude.
        const float asymmetryTerm = asymmetry * 0.45f * hard * hard;

        // The bias is an OFFSET on this transfer curve, and an offset on a curve is
        // also a DC pedestal coming out of it: at digital silence this function
        // returned a constant of roughly -30 dBFS rather than zero. That pedestal is
        // the last always-on source in the engine - the hiss is gated by the
        // transport, but this was not - and it is a rumble, not a click: it arrives
        // the instant the input goes quiet, sits there, and is then taken away
        // again by the playback DC blocker some 20 ms later. On a pause that reads
        // as the machine still making a sound with nothing playing.
        //
        // Subtracting the curve's own value at zero input removes the pedestal and
        // nothing else. The even-harmonic asymmetry survives intact because only its
        // AC part is kept - the difference between the asymmetry term now and the
        // same term evaluated at zero - and the lagged branch is evaluated at the
        // same memory it was given, so the subtraction is exact rather than an
        // approximation that drifts. Zero in, zero out, at every drive and bias, on
        // every sample.
        const float hardAtZero = std::tanh (asymmetry * slope) / slope;
        const float delayedAtZero = std::tanh ((asymmetry * delayedSlope) + lagged) / delayedSlope;
        const float blendedAtZero = hardAtZero * hardWeight + delayedAtZero * delayedWeight;
        const float asymmetryAtZero = asymmetry * 0.45f * hardAtZero * hardAtZero;

        return (blended - blendedAtZero) - (asymmetryTerm - asymmetryAtZero);
    }

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
    constexpr const char* stateFormatProperty = "j37StateFormat";
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
       parameters (*this, nullptr, "TAPE_J37", createParameterLayout())
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
    outputDbParam = parameters.getRawParameterValue ("output");
    widthParam    = parameters.getRawParameterValue ("stereo_width");
    tapeTypeParam  = parameters.getRawParameterValue ("tape_type");
    speedParam     = parameters.getRawParameterValue ("speed");
    instrumentParam = parameters.getRawParameterValue ("instrument");
    bypassParam   = parameters.getRawParameterValue ("bypass");
    oversamplingParam = parameters.getRawParameterValue ("oversampling");
    polarityParam  = parameters.getRawParameterValue ("polarity");
    autoGainParam  = parameters.getRawParameterValue ("auto_gain");
    // SUBFUND's depth parameter. Without this the pointer stayed null and the engine
    // read depth 0 through the nullptr guard - the generator was built, fed and
    // scaled correctly but multiplied by zero forever, so no subharmonic ever left it
    // no matter where the control was set (the "SUBFUND does nothing" report).
    subFundamentalParam = parameters.getRawParameterValue ("subfund");

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
    for (const auto* parameterID : { "input", "output", "bypass", "polarity", "auto_gain",
                                     "subfund",
                                     "stereo_width", "tape_type", "speed", "instrument", "drive", "bias",
                                     "oversampling", "tone", "wow", "flutter", "mix",
                                     "character" })
        parameters.addParameterListener (parameterID, this);
}

FirstAudioProcessor::~FirstAudioProcessor()
{
    for (const auto* parameterID : { "input", "output", "bypass", "polarity", "auto_gain",
                                     "subfund",
                                     "stereo_width", "tape_type", "speed", "instrument", "drive", "bias",
                                     "oversampling", "tone", "wow", "flutter", "mix",
                                     "character" })
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
//  Eighteen starting points covering the machine's real range. Each returns the full
//  parameter map it represents - nothing is patched onto the user's current state
//  beyond the listed values, so a preset changes the machine, not the session.
//==============================================================================
juce::StringArray FirstAudioProcessor::getPresetNames()
{
    return { "Default Tape", "Gentle Warmth", "Bus Glue Tape", "Drum Slam",
             "Vintage Lo-Fi", "Wide Master", "Clean Glue", "Saturated Crunch",
             "Wobbly Cassette", "Bright Air Tape", "Mix Saturation", "Master Bounce",
             "Vocal Rail", "Drum Room Warm", "Bass Weight", "Master Safety",
             "Lo-Fi Radio", "Ferric Master" };
}

std::map<juce::String, float> FirstAudioProcessor::factoryPresetValues (int index)
{
    // Local helper so every row below reads like the sound it names.
    auto row = [] (float subfund, float inputDb, float drive, float bias, float tone,
                   float character, float wow, float flutter, float mix, float outputDb,
                   float width, int tapeType, int speed, int instrument, int oversampling)
    {
        return std::map<juce::String, float> {
            { "subfund",       subfund },
            { "input",         inputDb },
            { "drive",         drive },
            { "bias",          bias },
            { "tone",          tone },
            { "character",     character },
            { "wow",           wow },
            { "flutter",       flutter },
            // Raw parameter values, not normalised ones - see the saved-state format
            // note above. The MIX values in the table below are already percentages,
            // so nothing scales them here; a value test would only ever misfire on a
            // preset that genuinely asks for less than 1% wet.
            { "mix",           mix },
            { "output",        outputDb },
            { "stereo_width",  width },
            { "tape_type",     static_cast<float> (tapeType) },
            { "speed",         static_cast<float> (speed) },
            { "instrument",    static_cast<float> (instrument) },
            { "oversampling",  static_cast<float> (oversampling) }
        };
    };

    switch (index)
    {
        case 0:  return row (0.00f,  0.0f, 0.28f, 0.42f, 0.50f, 0.50f, 0.14f, 0.18f, 100.0f,  0.0f, 0.50f, 0, 1, 0, 1); // Default Tape
        case 1:  return row (0.15f, -3.0f, 0.28f, 0.30f, 0.48f, 0.30f, 0.10f, 0.12f, 65.0f, -1.0f, 0.50f, 0, 1, 0, 1); // Gentle Warmth
        case 2:  return row (0.20f, +1.5f, 0.62f, 0.48f, 0.66f, 0.62f, 0.16f, 0.22f, 100.0f, -0.5f, 0.55f, 1, 1, 0, 1); // Bus Glue Tape
        case 3:  return row (0.35f,  0.0f, 0.75f, 0.42f, 0.74f, 0.70f, 0.12f, 0.20f, 100.0f, -1.0f, 0.50f, 2, 2, 5, 2); // Drum Slam
        case 4:  return row (0.00f,  0.0f, 0.35f, 0.34f, 0.55f, 0.45f, 0.22f, 0.28f, 70.0f,  0.0f, 0.50f, 0, 0, 3, 1); // Vintage Lo-Fi
        case 5:  return row (0.12f,  0.0f, 0.32f, 0.44f, 0.62f, 0.55f, 0.18f, 0.24f, 55.0f,  0.0f, 0.62f, 0, 1, 0, 1); // Wide Master
        case 6:  return row (0.00f, -6.0f, 0.22f, 0.30f, 0.50f, 0.35f, 0.10f, 0.14f, 45.0f,  0.0f, 0.50f, 3, 2, 0, 1); // Clean Glue
        case 7:  return row (0.25f, +3.0f, 0.85f, 0.52f, 0.70f, 0.78f, 0.14f, 0.26f, 100.0f, -1.5f, 0.45f, 1, 2, 3, 2); // Saturated Crunch
        case 8:  return row (0.10f,  0.0f, 0.36f, 0.45f, 0.60f, 0.50f, 0.30f, 0.34f, 100.0f,  0.0f, 0.50f, 0, 0, 3, 1); // Wobbly Cassette
        case 9:  return row (0.00f, -1.0f, 0.55f, 0.44f, 0.68f, 0.58f, 0.12f, 0.16f, 100.0f, -0.5f, 0.58f, 2, 2, 4, 1); // Bright Air Tape
        case 10: return row (0.20f, +1.0f, 0.68f, 0.46f, 0.64f, 0.66f, 0.16f, 0.20f, 100.0f, -1.0f, 0.40f, 1, 1, 0, 2); // Mix Saturation
        case 11: return row (0.10f,  0.0f, 0.40f, 0.45f, 0.62f, 0.52f, 0.15f, 0.19f, 100.0f,  0.0f, 0.50f, 0, 1, 0, 2); // Master Bounce
        case 12: return row (0.00f, +1.0f, 0.38f, 0.42f, 0.60f, 0.42f, 0.08f, 0.10f, 70.0f, -1.0f, 0.50f, 0, 1, 1, 1); // Vocal Rail
        case 13: return row (0.18f, +2.0f, 0.55f, 0.50f, 0.45f, 0.38f, 0.18f, 0.22f, 100.0f, -1.0f, 0.50f, 1, 0, 5, 1); // Drum Room Warm
        case 14: return row (0.65f, +2.5f, 0.48f, 0.36f, 0.42f, 0.35f, 0.06f, 0.08f, 100.0f, -2.0f, 0.50f, 2, 2, 2, 2); // Bass Weight
        case 15: return row (0.00f,  0.0f, 0.25f, 0.40f, 0.66f, 0.62f, 0.05f, 0.07f, 100.0f,  0.0f, 0.55f, 3, 2, 0, 2); // Master Safety
        case 16: return row (0.00f, +4.0f, 0.62f, 0.30f, 0.28f, 0.30f, 0.26f, 0.32f, 65.0f, -4.0f, 0.35f, 3, 0, 3, 1); // Lo-Fi Radio
        case 17: return row (0.12f,  0.0f, 0.34f, 0.44f, 0.55f, 0.50f, 0.10f, 0.13f, 100.0f,  0.0f, 0.52f, 7, 1, 0, 1); // Ferric Master
        default: break;
    }
    return {};
}

void FirstAudioProcessor::applyFactoryPreset (int index)
{
    const auto clampedIndex = juce::jlimit (0, numFactoryPresets - 1, index);
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
//  getStateInformation writes into a session) with a .j37tape extension, stored in
//  the per-user application data directory, so they survive plugin updates, are
//  shared by every instance and never depend on the session.
//==============================================================================
juce::File FirstAudioProcessor::getUserPresetDirectory()
{
    auto directory = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile ("J37 Tape Mastering")
                       .getChildFile ("Presets");
    if (! directory.isDirectory())
        directory.createDirectory();
    return directory;
}

juce::StringArray FirstAudioProcessor::getUserPresetNames() const
{
    juce::StringArray names;
    for (const auto& entry : getUserPresetDirectory().findChildFiles (juce::File::findFiles,
                                                                      false, "*.j37tape"))
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

    const auto file = getUserPresetDirectory().getChildFile (safe + ".j37tape");
    const auto saved = xml->writeTo (file);
    if (saved)
        markPresetClean (safe);
    return saved;
}

bool FirstAudioProcessor::applyUserPreset (const juce::String& name)
{
    const auto file = getUserPresetDirectory().getChildFile (name.trim() + ".j37tape");
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
    const auto file = getUserPresetDirectory().getChildFile (name.trim() + ".j37tape");
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
    // POLARITY INVERT: a mastering staple. A full polarity flip on the output, so a
    // 180-degree mis-wiring between two sources can be corrected without re-patching.
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "polarity", 1 },
                                                            "Polarity Invert", false));
    // AUTO GAIN: when on, the slow programme compensator (see the final gain
    // compensation section) is allowed to act; when off the output level is exactly
    // what the chain produced. Default ON, matching what earlier builds always did.
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "auto_gain", 1 },
                                                            "Auto Gain", true));

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
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "tape_type", 1 }, "Tape Type",
                                                            juce::StringArray { "J37", "Ampex 456", "Studer A800",
                                                                                 "Chrome", "Type 111", "GP9",
                                                                                 "Quantegy 499", "RTM SM911" },
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

    // TONE is the added macro: a crossfade BETWEEN TAPE SETTINGS rather than between
    // dry and wet. At 0 % the transport behaves like the classic slow machine - soft
    // head damping, gentle roll-off, warmer wow. At 100 % it behaves like the fast
    // machine - open top end, wider head-gap pole, tighter flutter. Everything the
    // SPEED switch and the head electronics set is blended between those two states,
    // which is exactly how the machine's own speed/eq macro behaves on the hardware.
    // The ID is "character" because "tone" is already taken by Brightness above.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "character", 1 }, "Tone", percentageRange (0.50f), 0.50f,
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

    // Compressor-coupled saturation state restarts neutral, so the first block
    // after a rate switch is not coloured by a stale squeeze from the old rate.
    squeezeSaturationDrive = 0.0f;

    // The tape noise generator is a per-instance LCG so that every plugin instance
    // and every render pass is deterministic, rather than sharing one thread_local
    // stream whose content would depend on how many instances happen to exist.
    noiseState = 0x1b873593u;

    // The safety limiter must start open, otherwise a stale gain from the previous
    // session would duck the first block audibly.
    preLimiterDetector = 0.0f;
    limiterGain = 1.0f;

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

    // Re-time the coefficient ramps for the new rate. The playback poles start from
    // neutral non-zero values, so the first wet block is audible while the exact
    // per-block targets are reached through the normal 20 ms ramps.
    toneShelfGainSmoothed.reset (sampleRate, 0.02);
    toneShelfGainSmoothed.setCurrentAndTargetValue (toneShelfGain);
    toneShelfBoostSmoothed.reset (sampleRate, 0.02);
    toneShelfBoostSmoothed.setCurrentAndTargetValue (toneShelfBoost);
    preDriveGainSmoothed.reset (sampleRate, 0.02);
    preDriveGainSmoothed.setCurrentAndTargetValue (preDriveGain);
    toneLpSmoothed.reset (sampleRate, 0.02);
    toneLpSmoothed.setCurrentAndTargetValue (toneLpAc);
    flutterScaleSmoothed.reset (sampleRate, 0.02);
    flutterScaleSmoothed.setCurrentAndTargetValue (flutterScale);

    driveAmountSmoothed.reset (sampleRate, 0.02);
    hfPostSmoothed.reset (sampleRate, 0.02);
    headGapSmoothed.reset (sampleRate, 0.02);
    // The noise floor ramps far slower than the controls: its gain is also the
    // transport gate (see processTapeEngine), so this window is how long the floor
    // takes to coast down when the machine comes to rest and back up when it spins
    // again - a fade, never a mute-switch drop.
    hissGainSmoothed.reset (sampleRate, 0.75);
    shaperDriveSmoothed.reset (sampleRate, 0.02);
    shaperAsymmetrySmoothed.reset (sampleRate, 0.02);

    // The formula-switch ramp is deliberately slower than the control ramps: it has to
    // move the head-damping pole by up to 8 kHz without that travel being an audible
    // sweep. 150 ms is long enough to read as a morph and short enough that switching
    // formula still feels immediate.
    headDampingSwitchSmoothed.reset (sampleRate, 0.15);

    // The subharmonic generators hold a bi-stable state and a follower, so they carry
    // across blocks and have to start clean; the depth control ramps like every other
    // gain so that moving it cannot step the phase-locked oscillator.
    subharmonicL.reset();
    subharmonicR.reset();
    subFundamentalSmoothed.reset (sampleRate, 0.02);
    subFundamentalSmoothed.setCurrentAndTargetValue (
        subFundamentalParam != nullptr ? subFundamentalParam->load() : 0.0f);

    // A rate change invalidates any switch in progress, so the countdown is cleared and
    // the next block re-seeds activeTapeType instead of treating the new rate as a
    // formula change.
    activeTapeType = -1;
    tapeTypeChangeCountdown = 0;

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
    //     Brightness value, up to +/-12 dB at the extremes. 50 percent is exactly
    //     neutral (both gains unity). The pivot split is taken from the signal
    //     itself, NOT from a filtered copy: the previous "shelf" split the signal
    //     against its own already-low-passed output, so the "high band" it
    //     boosted was mostly hiss residue - which is why the knob never showed up
    //     on an analyser no matter how far it travelled.
    toneLpAc = onePoleCoefficientHz (3000.0f + 27000.0f * toneCurve, engineSampleRate);

    toneShelfCoefficient = onePoleCoefficientHz (1600.0f, engineSampleRate);

    // Matched tilt gains around the 1.6 kHz pivot: u sweeps -1..+1 as Brightness
    // sweeps 0..1, and each band moves 12 dB in the opposite direction of the
    // other. u is built from the raw control (not the record-side curve), so at
    // the 50 percent pivot u is exactly 0, both gains are unity and the playback
    // passes through untouched - the neutral default.
    const auto tiltU = 2.0f * toneValue - 1.0f;
    toneShelfGain  = std::pow (10.0f, -0.6f * tiltU); // low band:  +12 dB warm .. -12 dB bright
    toneShelfBoost = std::pow (10.0f,  0.6f * tiltU); // high band: -12 dB warm .. +12 dB bright
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

    if (bypassRequested && ! bypassSmoothed.isSmoothing() && bypassSmoothed.getCurrentValue() <= 0.0f)
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
    const auto wowFreq = (0.15f + wowCurve * 1.36f) * speedScale;
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
        default:
            tapeCurve += 0.05f;
            tapeAsymmetry += 0.02f;
            tapeHiss -= 0.01f;
            headDampingHz += 3500.0f;
            hysteresis += 0.03f;
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
    const float headGapCoefficient = onePoleCoefficientHz (headGapHz * speedScale, engineSampleRate);
    const float hfPostCoefficient = onePoleCoefficientHz (headDampingHz * speedScale, engineSampleRate);

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
    const float hissGain = tapeHiss * 0.00042f;

    // The floor is gated by the transport. Tape hiss exists only while the tape is
    // actually MOVING across the head: a machine at rest is silent, because the
    // oxide never passes the playback gap. When both Wow and Flutter are closed the
    // machine is at rest and the floor coasts down to silence; opening either
    // control spins it back up. The fade itself is the hiss ramp's own slow window
    // (750 ms, see resetSampleRateDependentState), so a stop between takes reads as
    // the machine coasting to rest rather than a mute-switch drop. This is the fix
    // for the "noise while paused with Wow and Flutter at zero" report: the floor
    // was the only ungated always-on source left in the engine.
    const float gatedHissGain = hissGain * transportActivityGate;

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
    const float squeezeDriveSmoothing = 1.0f - std::exp (
        -1.0f / (engineSampleRate * 0.2f));

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
    const float compensationCoefficient = 1.0f - std::exp (
        -1.0f / (engineSampleRate * 0.45f));

    // VU ballistics (300 ms) for the input and output meters. The coefficient is a
    // constant for the whole block - it depends only on the sample rate - so it is
    // computed once here instead of re-evaluating an exp() twice per sample inside
    // the loop below.
    const float vuBallisticCoefficient = 1.0f - std::exp (
        -1.0f / (engineSampleRate * 0.3f));

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

        std::array<float, 2> tapeOutput {};

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

        const float inputEnvelopeDb = inputCompressor.processDetection (
            inputDetectorPower, engineSampleRate, inputAttackSeconds, inputReleaseSeconds,
            inputDriveLoad);
        const float inputReductionDb = juce::jmax (inputReductionLimitDb,
                                                   softKneeReductionDb (inputEnvelopeDb,
                                                                        inputThresholdDb,
                                                                        inputKneeDb,
                                                                        inputCompressorRatio));
        const float inputCompressionGain = juce::Decibels::decibelsToGain (inputReductionDb);

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
            const float x = inputTrimmed * inputCompressionGain;

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
            const float wowMod = 1.0f + wowLfo * wowDepth;
            const float flutterMod = 1.0f + flutterLfo * flutterDepth
                                       * flutterScaleSmoothed.getCurrentValue();
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
            const float shapedCore = magneticHysteresis (preDrive,
                                                         shaperDriveSmoothed.getCurrentValue(),
                                                         shaperAsymmetrySmoothed.getCurrentValue(),
                                                         hysteresisMemory[0]);
            hysteresisMemory[2] = hysteresisMemory[1];
            hysteresisMemory[1] = hysteresisMemory[0];
            hysteresisMemory[0] = shapedCore;

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
                shapedCore, driveAmountSmoothed.getCurrentValue(),
                subFundamentalSmoothed.getCurrentValue(), engineSampleRate);

            // A parallel addition, not a blend: the control reads as adding weight
            // below the note instead of crossfading the tape away. The depth ramp keeps
            // the level change from stepping the phase-locked oscillator.
            const float withSubharmonic = shapedCore;

            // Measure what the shaper actually produced, comparing its input against its
            // output. Harmonics are the reason this plugin exists, so the character is
            // observed rather than assumed: the analyser separates the even content
            // (warmth, from the bias asymmetry) from the odd content (edge, from the
            // symmetric tanh). Only the left channel is measured, since the two are driven
            // identically and doubling the analyser would cost twice as much for the same
            // reading.
            if (channel == 0)
                harmonicAnalyser.analyse (preDrive, shapedCore, engineSampleRate);

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

            // Scale compensation, gentle level-dependent bias compression and the
            // tape noise floor ride on the modulated signal.
            const float compensation = tapeCurve / 1.30f;

            // Nonlinearity costs level, but only when the shaper is actually working. The
            // corrected shaper is near-unity at low drive, so this correction scales with
            // the drive amount rather than applying a large standing boost - the old 2.2x
            // ceiling was compensating for a stage that saturated at every setting and is
            // no longer appropriate, which is part of why the plugin came out harsh and
            // loud. It now recovers a modest amount of the level the clamps removed and
            // stays close to unity when the machine is running clean.
            const float shapedLevel = std::abs (shapedCore);
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
            const float noiseFloor = hissLowPass * bandLimitCompensation;

            const float motioned = (compressedBias + noiseFloor) * wowMod * flutterMod * grainMod;

            // Playback EQ: the BRIGHTNESS tilt. A one-pole low-pass at the fixed
            // 1.6 kHz pivot splits the wet signal ITSELF into low and high bands
            // (the old code split it against its own filtered copy, so the "high
            // band" was hiss residue and the control was inaudible on an
            // analyser). The matched gain pair then moves the two bands in
            // opposite directions - +/-12 dB at the extremes, exactly unity at
            // the 50 percent pivot. Both smoothers advance every sample, so the
            // tilt can never step the waveform.
            auto& pivotLow = toneShelfSplit[static_cast<std::size_t> (channel)];
            pivotLow += (motioned - pivotLow) * toneShelfCoefficient;
            const float highBand = motioned - pivotLow;
            const float deEmphasised = pivotLow * toneShelfGainSmoothed.getCurrentValue()
                                     + highBand * toneShelfBoostSmoothed.getCurrentValue();

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
            const float dcBlocked = deEmphasised - dcX + dcBlockR * dcY;
            dcX = deEmphasised;
            dcY = dcBlocked;

            // Raised-cosine crossfade between the dry input and the fully processed
            // tape signal, driven by the smoothed MIX. 0 % is a transparent dry signal
            // and 100 % is all tape, both at unity, with the level held across the
            // middle of the travel.
            const float wetMix = dcBlocked * wetGain;
            const float dryMix = x * dryGain;
            tapeOutput[static_cast<std::size_t> (channel)] = dryMix + wetMix;
        }

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
                                                                    outputAttackSeconds,
                                                                    outputReleaseSeconds,
                                                                    outputDriveLoad);
        const float reductionDb = juce::jmax (outputReductionLimitDb,
                                              softKneeReductionDb (envelopeDb,
                                                                   outputThresholdDb,
                                                                   outputKneeDb,
                                                                   outputCompressorRatio));
        const float compressionGain = juce::Decibels::decibelsToGain (reductionDb);
        peakReductionDb = juce::jmin (peakReductionDb, reductionDb);

        // Each stage pays back part of the reduction it applied, weighted by how hard
        // its trim control is driving it, so both together cannot leave the machine
        // quieter than it arrived.
        const float inputMakeup = juce::Decibels::decibelsToGain (-inputPeakReductionDb
                                                                  * inputMakeupFraction);
        const float outputMakeup = juce::Decibels::decibelsToGain (-reductionDb
                                                                   * outputMakeupFraction);
        const float stageGain = compressionGain * outputMakeup * finalOutputGain * inputMakeup;

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
                const auto signal = tapeOutput[static_cast<std::size_t> (channel)] * stageGain;
                postCompressorPower += signal * signal;
            }

            if (postCompressorPower > 1.0e-12f)
            {
                // How far the finished signal has fallen below the reference taken after
                // the input trim. Positive means level was lost and needs paying back.
                const auto levelRatioDb = juce::Decibels::gainToDecibels (
                    std::sqrt (referenceBlockPower / postCompressorPower), 0.0f);

                // Only ever restore, never exaggerate: the correction may recover a loss
                // but must not become an extra boost stage of its own.
                const auto targetCompensationDb = juce::jlimit (0.0f, 12.0f, levelRatioDb);
                smoothedCompensationDb += (targetCompensationDb - smoothedCompensationDb)
                                        * compensationCoefficient;
                compensationGain = juce::Decibels::decibelsToGain (smoothedCompensationDb);
            }
        }

        std::array<float, 2> outputSignal {};
        for (int channel = 0; channel < activeChannels; ++channel)
            outputSignal[static_cast<std::size_t> (channel)] =
                tapeOutput[static_cast<std::size_t> (channel)] * stageGain * compensationGain;

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

        if (activeChannels == 2)
        {
            const float mid = 0.5f * (outputSignal[0] + outputSignal[1]);
            const float side = 0.5f * (outputSignal[0] - outputSignal[1]) * currentWidth;
            outputSignal[0] = mid + side;
            outputSignal[1] = mid - side;
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
        const auto limiterCeiling = 0.94f;
        const auto detectorAttack = 1.0f - std::exp (-1.0f / (engineSampleRate * 0.0005f));
        const auto detectorRelease = 1.0f - std::exp (-1.0f / (engineSampleRate * 0.080f));
        const auto detectorCoefficient = framePeak > preLimiterDetector ? detectorAttack
                                                                        : detectorRelease;
        preLimiterDetector += (framePeak - preLimiterDetector) * detectorCoefficient;

        // Gain required to bring the peak back to the ceiling. It is smoothed separately
        // from the detector so the correction is continuous and cannot click.
        const auto requiredGain = preLimiterDetector > limiterCeiling
                                    ? limiterCeiling / preLimiterDetector
                                    : 1.0f;
        const auto gainSmoothing = requiredGain < limiterGain
                                     ? 1.0f - std::exp (-1.0f / (engineSampleRate * 0.0004f))
                                     : 1.0f - std::exp (-1.0f / (engineSampleRate * 0.120f));
        limiterGain += (requiredGain - limiterGain) * gainSmoothing;

        for (int channel = 0; channel < activeChannels; ++channel)
            outputSignal[static_cast<std::size_t> (channel)] *= limiterGain;

        for (int channel = 0; channel < activeChannels; ++channel)
        {
            auto& destination = channelData[static_cast<std::size_t> (channel)][sample];
            const auto blended = destination + (outputSignal[static_cast<std::size_t> (channel)]
                                                 - destination) * bypassMix;

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

            // The polarity switch flips the finished sample after the clipper, so a
            // 180-degree source mis-wiring is corrected at the very last stage.
            const auto limitedOut = softClip (blended) * polaritySign;
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
                                                       channelData[1][sample], engineSampleRate);
        else if (activeChannels == 1)
            currentLufs = outputLoudness.processFrame (channelData[0][sample],
                                                       channelData[0][sample], engineSampleRate);

        // The INPUT meter runs the same K-weighting on the raw signal at the plugin's
        // own input, so the two meters can be compared directly. The channels are read
        // back from the buffer because the dry input was overwritten in place.
        if (activeChannels == 2)
            currentInputLufs = inputLoudness.processFrame (inputChainHistory[0], inputChainHistory[1],
                                                           engineSampleRate);
        else if (activeChannels == 1)
            currentInputLufs = inputLoudness.processFrame (inputChainHistory[0], inputChainHistory[0],
                                                           engineSampleRate);
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
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, static_cast<size_t> (sizeInBytes)));

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
