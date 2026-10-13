// =============================================================================
//  The input stage: the preamp, the distortion and the DI box.
//
//  Moved out of PluginProcessor.h, where it was the two gain stages in front of the machine, their low cut, and the DI box. The conventions of
//  Source/DSP/ are stated in J37Math.h, and tests/dsp/check_headers_juce.sh
//  compiles this header against real JUCE along with the rest of the directory.
// =============================================================================
#pragma once

#include <cmath>

//==============================================================================
/**
    The preamp and the distortion stage - the two gain stages in FRONT of the
    machine, and they do different jobs.

    A real chain has a preamp before the recorder and, if the source wants it, a
    distortion pedal before that. Both are placed ahead of the tape so the machine
    hears their output, which is the whole point: a distorted guitar recorded to
    tape sounds like a record rather than a pedal precisely because the tape
    smooths what the pedal produced.

    PREAMP is a valve-ish input stage: a gentle soft clip, a transformer's
    low-cut, and a slight high-frequency lift. It is a STAGE, not a gain - driving
    it changes the colour as much as the level.

    DISTORTION is a diode clipper: a hard knee with a pre-gain, deliberately
    abrupt. Where the saturation core bends, this breaks.

    The DI BOX is a different kind of thing again, and it is first in the chain
    for a physical reason: a DI box is what a guitar or a synth is plugged into
    BEFORE anything else. It takes an unbalanced, high-impedance, instrument-level
    signal and hands a balanced, low-impedance, mic-level one to the desk. Three
    things in it are audible, and all three are modelled here:

      TRANSFORMER  a real DI has one, and it is why a DI'd guitar sounds
                   slightly soft on top and slightly fatter in the low-middle
                   than the same guitar plugged straight in. It is a behaviour,
                   not a fault.

      IMPEDANCE    the box's input impedance LOADS the source. A high-impedance
                   instrument (a passive guitar pickup, most obviously) has a
                   resonant peak in the top octave, and the load the DI presents
                   moves and damps that peak. This is the single largest reason
                   two DI boxes sound different on the same guitar.

      PAD          most DIs offer a -20 or -30 dB pad for a hot source. It is a
                   clean attenuation BEFORE the transformer, so an active synth
                   or a hot pedal can be plugged in without driving the box's
                   own core into saturation - which is a real and useful decision
                   rather than a level control.

    Being FIRST also means the DI changes what everything after it hears: the
    preamp, the saturation curve and the glue stages all respond to a padded or
    loaded signal differently, which is exactly how the hardware behaves.
*/
struct InputStage
{
    // The preamp's transformer low-cut and its own soft-clip bias drift.
    float preampLowState = 0.0f;
    float preampBiasState = 0.0f;

    // The distortion's own coupling state, so its hard clip does not leave a DC
    // pedestal behind it for the tape stage to swallow.
    float distortionLowState = 0.0f;

    // ------------------------------------------------------------------
    //  The DI box's state.
    //
    //    diLoadState      the input load's own split, which is where the
    //                     impedance damping happens
    //    diTransformerState  the DI transformer's low-end and its own slight
    //                     hysteresis thickness
    //    diGroundHumPhase the ground loop's mains phase, per channel
    // ------------------------------------------------------------------
    float diLoadState = 0.0f;
    float diTransformerState = 0.0f;
    float diGroundHumPhase = 0.0f;

    void reset() noexcept
    {
        preampLowState = 0.0f;
        preampBiasState = 0.0f;
        distortionLowState = 0.0f;
        diLoadState = 0.0f;
        diTransformerState = 0.0f;
        diGroundHumPhase = 0.0f;
    }

    /**
        Runs the preamp. `amount` is 0..1; at 0 the stage is a straight wire, so a
        machine with no preamp engaged is bit-for-bit what it always was.

        `lowCutCoefficient` is the transformer's low-cut, built by the caller.
    */
    float processPreamp (float x, float amount, float lowCutCoefficient) noexcept
    {
        if (amount <= 1.0e-5f)
            return x;

        // The transformer's low-cut: a one-pole high-pass taken out of the signal.
        // It is what stops a driven preamp from stacking low end into the recorder.
        preampLowState += (x - preampLowState) * lowCutCoefficient;
        const float cut = x - preampLowState;

        // The valve stage: a soft asymmetric clip whose operating point drifts a
        // little with the signal, the same mechanism as the saturation core's valve
        // but at a much lower gain - this is an input stage, not a fuzz.
        preampBiasState += (cut - preampBiasState) * 0.0012f;
        const float biased = cut + preampBiasState * 0.12f;

        // Gain rises with the control, and the curve is normalised so the stage
        // stays level-matched: PREAMP changes the character, and the INPUT control
        // remains the thing that sets the level.
        const float gain = 1.0f + amount * 3.2f;
        const float driven = biased * gain;
        const float clipped = std::tanh (driven) / std::tanh (gain);

        // The slight top-end lift a good input transformer has. A one-pole
        // difference against the cut signal, added back gently.
        const float lift = (cut - preampLowState) * amount * 0.18f;

        return clipped + lift;
    }

    /**
        Runs the distortion. `amount` is 0..1; at 0 it is a straight wire.

        A diode clipper is a hard knee, and the abruptness is the point: it is what
        makes this read as a pedal rather than as more saturation.
    */
    float processDistortion (float x, float amount) noexcept
    {
        if (amount <= 1.0e-5f)
            return x;

        // Pre-gain, then the hard knee. The knee is fixed and the gain moves, which
        // is how a real pedal's gain control works - the clipping threshold is set
        // by the diodes, not by the knob.
        const float gain = 1.0f + amount * 9.0f;
        const float driven = x * gain;

        // A hard clip with a very small soft shoulder. Perfectly square would alias
        // badly; a shoulder of a few percent keeps it a diode clipper while staying
        // band-limited enough for the oversampler to work with.
        constexpr float knee = 0.92f;
        const float magnitude = std::abs (driven);
        float clipped;
        if (magnitude <= knee)
        {
            clipped = driven;
        }
        else
        {
            // The shoulder: a short exponential walk to the asymptote.
            const float excess = magnitude - knee;
            const float compressed = knee + (1.0f - j37math::exp (-excess * 3.0f)) * (1.0f - knee);
            clipped = driven < 0.0f ? -compressed : compressed;
        }

        // Undo the pre-gain so the stage stays level-matched, then AC-couple: a hard
        // clipper leaves an offset, and the tape stage downstream must not be handed
        // one.
        const float scaled = clipped / gain;
        distortionLowState += (scaled - distortionLowState) * 0.0006f;
        return scaled - distortionLowState;
    }

    /**
        Runs the DI box. All three of its controls arrive together because they
        are one decision - a DI is a box you plug into, and the box is the sum of
        its transformer, its load and its pad.

        `amount`     0..1 - how much the box is engaged at all. At 0 it is a
                     straight wire, bit-for-bit, so a session with no DI is
                     exactly the signal it always was.

        `load`       0..1 - how heavily the input loads the source. 0 is a very
                     high-impedance (transparent) input and 1 is a heavy load
                     that damps the source's own top-end resonance.

        `padDb`      the pad in dB (0 or negative). Applied BEFORE the
                     transformer, which is the whole point of a pad: it stops a
                     hot source saturating the box rather than merely turning it
                     down.

        `humAmount`  0..1 - the ground loop. A DI with a lifted ground is
                     silent; a DI with a bad earth hums, and the hum is at the
                     mains frequency. It is a fault the box either has or does
                     not, which is why it is part of the DI rather than a
                     separate control.

        The remaining three arguments are coefficient/phase values the caller
        builds per block from the engine rate, so this stage stays rate-agnostic
        like every other one here.
    */
    float processDiBox (float x, float amount, float load, float padDb, float humAmount,
                        float loadCoefficient, float transformerCoefficient,
                        float humIncrement) noexcept
    {
        if (amount <= 1.0e-5f)
            return x;

        // The PAD, first, before the transformer sees anything. A pad after the
        // transformer would be a level control; a pad before it is what lets a
        // hot source into a DI without driving the core, which is the decision
        // the switch exists for.
        float padded = x * juce::Decibels::decibelsToGain (padDb);

        // The input LOAD. A one-pole low-pass whose corner moves with the
        // control: a heavy load damps the source's top octave and its resonant
        // peak, which is what makes a passive guitar pickup sound fatter and
        // softer through one DI than another. It is taken as a SPLIT so the
        // removed top can be folded back at low level rather than simply lost -
        // a real load damps the peak, it does not silence the octave.
        const float corner = loadCoefficient * (1.0f - load * 0.85f);
        diLoadState += (padded - diLoadState) * corner;
        const float loadedLow = diLoadState;
        const float loadedHigh = padded - diLoadState;
        const float loaded = loadedLow + loadedHigh * (1.0f - load * 0.55f);

        // The DI TRANSFORMER. A real one is a small, often cheap transformer,
        // and its signature is a gentle low-end bloom and a slight thickness in
        // the low middle. A one-pole low split with the bottom band lifted is
        // the same mechanism the tape condition stage and the vinyl degree use,
        // which is why it is one line here rather than a model of its own.
        diTransformerState += (loaded - diTransformerState) * transformerCoefficient;
        const float transformerLow = diTransformerState;
        const float transformerHigh = loaded - diTransformerState;
        const float transformerOut = transformerLow * 1.12f + transformerHigh * 0.97f;

        // The GROUND LOOP. A 50 Hz hum, added rather than folded in, because it
        // is an interference the box picks up rather than something it does to
        // the signal.
        float groundHum = 0.0f;
        if (humAmount > 0.0f)
        {
            diGroundHumPhase += humIncrement;
            if (diGroundHumPhase >= 1.0f)
                diGroundHumPhase -= 1.0f;

            // The fundamental and its second harmonic - a mains loop is rarely
            // a pure sine, and the harmonic is what makes it read as a hum
            // rather than as a low tone.
            groundHum = (std::sin (diGroundHumPhase * 6.2831853f)
                           + 0.30f * std::sin (diGroundHumPhase * 12.5663706f))
                      * humAmount * humAmount * 0.010f;
        }

        // The whole box is mixed in by `amount`, so the control is a real
        // engagement rather than a set of simultaneous switches.
        return x + (transformerOut + groundHum - x) * amount;
    }
};
