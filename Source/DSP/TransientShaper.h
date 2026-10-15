// =============================================================================
//  The transient designer: attack and sustain shaping on the finished signal.
//
//  Measured on signal by tests/dsp/subfund_test.cpp, which includes this header
//  rather than a copy cut out of PluginProcessor.h. The conventions of
//  Source/DSP/ are stated in J37Math.h.
// =============================================================================
#pragma once

#include <cmath>

//==============================================================================
/**
    TransientShaper - attack and sustain shaping, the studio's transient designer.

    This is a DIFFERENT mechanism from every other stage in the plugin, and the
    distinction is the whole reason it earns a place beside the saturators. A
    saturation curve is memoryless-and-monotone: it looks at the instant's level
    and bends it. A transient shaper looks at the signal's ENVELOPE - the fast
    and slow views of its level - and changes how the signal's own amplitude
    moves over time. It can make a drum hit sharper without adding a harmonic,
    and soften a pick without removing one; nothing else in the chain can do
    that, because everything else operates on the waveform rather than on its
    envelope.

    The mechanism is two envelope followers on the same signal, one fast and one
    slow, and the difference between them is the transient: on a rising edge the
    fast follower runs ahead of the slow one (a positive difference), and on a
    decaying tail it falls behind (a negative one). Two independent controls
    scale those two halves of the difference:

      ATTACK  > 0 boosts the leading edge of each event (punch, snap, click);
              < 0 softens it (rounder, less percussive).
      SUSTAIN > 0 lifts what follows the edge (body, ring, room);
              < 0 shortens it (tighter, more staccato).

    The applied gain is DETECTOR-DRIVEN, not signal-multiplied: it is derived
    from the envelope difference and then smoothed, so the effect is a slow, gain
    like ride on the envelope rather than a waveshaper, and it adds no harmonics
    of its own. That is what makes it usable AFTER the tape stage - a shaper here
    would re-distort the signal the machine just coloured, whereas an envelope
    shaper only moves its level, so the tape's character survives intact.

    Single-instance-per-channel: it carries signal-dependent envelope state, and
    a shared instance would make the right channel's punch depend on the left's.
    Discrete single-sample state only - no allocation, no lookahead - so it stays
    realtime-safe on the audio thread.
*/
struct TransientShaper
{
    // -----------------------------------------------------------------------
    //  Envelope state. `fastEnvelope` tracks the waveform's own instantaneous
    //  magnitude; `slowEnvelope` averages it over the detector's slow window.
    //  The slow one is the reference the fast one is compared against, so the
    //  DIFFERENCE between them is what the two controls act on.
    // -----------------------------------------------------------------------
    float fastEnvelope = 0.0f;
    float slowEnvelope = 0.0f;

    // The smoothed gain the stage applied last sample. It is smoothed so a step
    // in the detector cannot become a step in the waveform - the same discipline
    // every other gain in the chain follows, and the reason this stage does not
    // click when TWO controls are swept at once.
    float appliedGain = 1.0f;

    /** The per-channel state a rate change or a bypass has to let go of. */
    void reset() noexcept
    {
        fastEnvelope = 0.0f;
        slowEnvelope = 0.0f;
        appliedGain = 1.0f;
    }

    /**
        One sample through the shaper.

        `attackAmount` and `sustainAmount` are -1..1: negative softens or shortens,
        positive sharpens or lifts. `fastCoefficient` and `slowCoefficient` are
        the two detector poles, built per block from the engine rate; `gainCoefficient`
        smooths the applied gain. All three are block-rate constants so the loop
        does no exp() work of its own.

        At both amounts zero this returns the input unchanged, bit for bit - the
        envelopes still run, so turning the control back up does not restart them
        from rest, but the gain stays exactly 1 and the stage is transparent.
    */
    float process (float x, float attackAmount, float sustainAmount,
                   float fastCoefficient, float slowCoefficient,
                   float gainCoefficient) noexcept
    {
        const float magnitude = std::abs (x);

        // The two followers. The fast one uses an attack/release asymmetry -
        // it climbs quickly and falls slowly - because that is what a transient
        // detector is: it must not miss an edge, and it must not chatter on the
        // way down. The slow one is symmetric and is simply the average level.
        const float fastPole = magnitude > fastEnvelope ? fastCoefficient
                                                        : fastCoefficient * 0.25f;
        fastEnvelope += (magnitude - fastEnvelope) * fastPole;
        slowEnvelope += (magnitude - slowEnvelope) * slowCoefficient;

        // The transient: positive while an edge is rising, negative on the tail.
        // Normalised by the slow envelope (floored so a silent passage does not
        // divide by nothing) so the reading is a RATIO of the event to the
        // programme, not an absolute level - a quiet hi-hat and a loud snare
        // produce the same number, which is what makes the control usable.
        const float reference = juce::jmax (slowEnvelope, 1.0e-6f);
        const float transient = (fastEnvelope - slowEnvelope) / reference;

        // The two halves are shaped independently. `jmax`/`jmin` split the
        // signed transient into its positive (attack) and negative (sustain)
        // parts, so ATTACK never touches a decay and SUSTAIN never touches an
        // edge - which is what makes the two controls mean what their names say.
        const float attackPart = juce::jmax (0.0f, transient);
        const float sustainPart = juce::jmin (0.0f, transient);

        // A bounded gain: +/-1 on either control is at most about 3x/+10 dB of
        // lift and about a quarter of the original on a full cut, so the stage
        // can be dramatic without ever being able to invert or blow up the
        // signal. The clamp is the safety net the whole chain relies on.
        //
        // The two halves are ADDED and SUBTRACTED, and the asymmetry is the whole
        // of it. `attackPart` is the positive half of the transient and
        // `sustainPart` the negative one, so adding the second with its own sign
        // means SUSTAIN +1 shrinks the tail - the control named for the thing it
        // does, doing its exact opposite. Subtracting a term that is already
        // negative is what makes the name true: +1 lifts what follows the edge,
        // -1 shortens it. ATTACK needs no correction, because its part is the
        // positive one and positive is the direction +1 is meant to push.
        const float shaping = 1.0f
                            + attackPart * attackAmount * 2.0f
                            - sustainPart * sustainAmount * 2.0f;
        const float target = juce::jlimit (0.25f, 4.0f, shaping);

        appliedGain += (target - appliedGain) * gainCoefficient;
        return x * appliedGain;
    }
};
