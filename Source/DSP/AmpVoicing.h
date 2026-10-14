// =============================================================================
//  The amplifier's behaviour around the clipping curve.
//
//  Sag, presence, the cabinet resonance and the bias shift: the four parts of a
//  real amp that are not the curve itself. Moved out of PluginProcessor.h whole,
//  prose and all, by matching its exact text - so a typo would have failed the
//  move rather than changed the amplifier. The conventions of Source/DSP/ are
//  stated in J37Math.h.
// =============================================================================
#pragma once

#include <cmath>

//==============================================================================
/**
    Guitar-amplifier features: the parts of a real amp's behaviour that are not
    the clipping curve.

    A saturation curve alone does not sound like an amplifier. What does is the
    behaviour AROUND the curve, and these are the four that matter most:

      SAG         - the supply droops under sustained demand, so the gain falls
                    a little and then recovers. It is why an amp "gives" under a
                    held chord and why the attack feels spongy rather than
                    immediate. Modelled as a slow envelope that pulls the drive
                    down, with a recovery time long enough to hear.
      PRESENCE    - the negative-feedback network's top-end lift. A bright,
                    narrow boost in the upper mids that sits AFTER the clipping,
                    so it sharpens what is already there rather than adding new
                    harmonics.
      CABINET     - the speaker and its box. A resonant low-pass, not a plain
                    one: a peak around 80-120 Hz from the cabinet tuning and a
                    roll-off above 4-6 kHz from the cone's mass. Without it a
                    clipped signal is fizzy; with it, it reads as a speaker.
      BIAS SHIFT  - the DC operating point of the input stage, which is the single
                    most effective control on a real amp's character: cold is
                    tight and crossover-distorted, hot is fat and compressed. It
                    is the amp's own BIAS, separate from the tape BIAS.
*/
struct AmpVoicing
{
    // Sag: the supply envelope. `sagEnvelope` follows the signal's demand, and
    // `sagGain` is the gain reduction it produces.
    float sagEnvelope = 0.0f;
    float sagGain = 1.0f;

    // Cabinet: a two-pole resonant low-pass, per channel, plus its own state.
    float cabinetLow1 = 0.0f;
    float cabinetLow2 = 0.0f;
    float cabinetPeak = 0.0f;

    // Presence: a one-pole high-pass taken out of the signal and added back with
    // gain, which is what the feedback network's lift actually does.
    float presenceHighState = 0.0f;

    void reset() noexcept
    {
        sagEnvelope = 0.0f;
        sagGain = 1.0f;
        cabinetLow1 = cabinetLow2 = cabinetPeak = 0.0f;
        presenceHighState = 0.0f;
    }

    /**
        Runs the amplifier behaviour around one sample.

        `x` is the already-shaped signal; the return value is what leaves the amp.
        All three coefficients are block-rate constants, built by the caller.

        Order matters and follows the hardware: SAG acts on the DRIVE before the
        curve (it is the supply), so it is applied by the caller to the drive
        amount; here we run the POST-curve stages - cabinet first (the speaker is
        the last thing in the amp), then presence (the feedback network taps the
        output).
    */
    float process (float x,
                   float cabinetLowCoefficient,
                   float cabinetPeakCoefficient,
                   float presenceCoefficient,
                   float presenceAmount) noexcept
    {
        // -- Cabinet: a resonant low-pass ------------------------------------
        // Two cascaded one-poles give the roll-off; the peak term adds the
        // cabinet's own resonance on top by feeding back a little of the
        // difference between the two poles, which is where the low-mid thump
        // comes from.
        cabinetLow1 += (x - cabinetLow1) * cabinetLowCoefficient;
        cabinetLow2 += (cabinetLow1 - cabinetLow2) * cabinetLowCoefficient;

        // The resonance is the difference between the two pole outputs: it is
        // near zero for slow signals and rises where the cabinet's tuning sits.
        const float resonance = (cabinetLow1 - cabinetLow2);
        cabinetPeak += (resonance - cabinetPeak) * cabinetPeakCoefficient;

        const float cabinetOut = cabinetLow2 + cabinetPeak * 0.85f;

        // -- Presence: the feedback network's top-end lift ---------------------
        // A one-pole high-pass: the difference between the signal and its own
        // low-passed copy. Adding it back with gain is exactly what a presence
        // control does, and because it sits after the clipping it sharpens the
        // harmonics already present rather than generating new ones.
        presenceHighState += (cabinetOut - presenceHighState) * presenceCoefficient;
        const float highBand = cabinetOut - presenceHighState;

        return cabinetOut + highBand * presenceAmount;
    }
};
