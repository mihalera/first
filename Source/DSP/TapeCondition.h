// =============================================================================
//  The tape condition: FLUX, WEAR and MECHANICS.
//
//  Three separate physical facts about the machine, moved out of
//  PluginProcessor.h whole, prose and all. It reaches for juce::jmax and
//  juce::roundToInt, which its includer has already declared (the plugin's
//  JuceHeader.h, or tests/dsp/check_headers_juce.sh's own module includes) -
//  the same arrangement every other header in this directory works under. The
//  conventions of Source/DSP/ are stated in J37Math.h.
// =============================================================================
#pragma once

#include <cmath>
#include <cstdint>

//==============================================================================
/**
    Tape condition: FLUX, WEAR and MECHANICS.

    Three separate physical facts about the machine, and they are genuinely
    separate rather than three amounts of the same thing:

      FLUX      - how deep into the oxide the record head magnetises. More flux is
                  more low end, more hysteresis memory and a quieter floor; less is
                  thin and bright. It is the RECORD head's depth.
      WEAR      - the state of the heads and the tape. A rounded gap and patchy
                  oxide lose top end and add contact noise. It is the MEDIUM's
                  condition.
      MECHANICS - how well the transport holds its speed. Good order means smooth,
                  periodic wow and flutter; worn bearings and a slack belt mean
                  irregular drift and the occasional slip.

    All three are slow, continuous controls with a neutral position, so they read
    as calibration rather than as effects.
*/
struct TapeCondition
{
    // WEAR's contact noise: a slow random modulation of the high-frequency loss.
    float wearNoiseState = 0.0f;
    float wearNoisePhase = 0.0f;

    // MECHANICS' irregular drift: a slow random walk added to the wow phase.
    float driftState = 0.0f;
    float driftTarget = 0.0f;
    int driftCountdown = 0;

    // The FLUX low shelf, per channel.
    float fluxLowL = 0.0f;
    float fluxLowR = 0.0f;

    // The WEAR high-frequency loss, per channel.
    float wearLowL = 0.0f;
    float wearLowR = 0.0f;

    void reset() noexcept
    {
        wearNoiseState = wearNoisePhase = 0.0f;
        driftState = driftTarget = 0.0f;
        driftCountdown = 0;
        fluxLowL = fluxLowR = 0.0f;
        wearLowL = wearLowR = 0.0f;
    }

    /**
        Advances the slow random sources. Called once per block, not per sample:
        the drift and the contact noise are both sub-audio, so a block-rate update
        is not merely adequate, it is the correct rate - a per-sample update would
        just be a slower way to compute the same number.

        `random` is the caller's noise source, so this stage does not carry a
        second generator.
    */
    void updateSlowSources (float mechanicsAmount, float wearAmount, int numSamples,
                            std::uint32_t& randomState) noexcept
    {
        // -- MECHANICS: the drift target changes every few hundred milliseconds ---
        driftCountdown -= numSamples;
        if (driftCountdown <= 0)
        {
            // 120 ms between targets. Slow enough to read as a wandering transport
            // rather than as noise, fast enough that a held note hears it move.
            driftCountdown = juce::jmax (1, juce::roundToInt (0.12f * 48000.0f));

            randomState = randomState * 1664525u + 1013904223u;
            const float unit = static_cast<float> ((randomState >> 8) & 0x00ffffffu)
                             * (1.0f / 8388608.0f) - 1.0f;

            // A worn machine's drift is larger; in good order it is nearly zero.
            driftTarget = unit * mechanicsAmount;
        }

        // The drift itself is a one-pole toward the target, so the walk is smooth.
        driftState += (driftTarget - driftState) * 0.05f;

        // -- WEAR: the contact noise is a slow random modulation -----------------
        randomState = randomState * 1664525u + 1013904223u;
        const float wearUnit = static_cast<float> ((randomState >> 8) & 0x00ffffffu)
                             * (1.0f / 8388608.0f) - 1.0f;
        wearNoiseState += (wearUnit * wearAmount - wearNoiseState) * 0.02f;
    }

    /**
        The FLUX low shelf and the WEAR top-end loss, per sample.

        Both are one-pole splits: FLUX lifts the low band, WEAR takes the high band
        away. They are separate filters rather than one tilt because they are
        separate mechanisms - one is the record head, the other is the medium.
    */
    float processTone (float x, int channel,
                       float fluxAmount, float wearAmount,
                       float fluxCoefficient, float wearCoefficient) noexcept
    {
        auto& fluxLow = channel == 0 ? fluxLowL : fluxLowR;
        auto& wearLow = channel == 0 ? wearLowL : wearLowR;

        // FLUX: the low band is the one-pole's output, the rest is everything above.
        // Lifting the low band by up to +3 dB is what "more flux" sounds like.
        fluxLow += (x - fluxLow) * fluxCoefficient;
        const float fluxHigh = x - fluxLow;
        const float fluxGain = 1.0f + (fluxAmount - 0.5f) * 0.6f;
        const float fluxOut = fluxLow * fluxGain + fluxHigh;

        // WEAR: a one-pole low-pass on the whole signal, crossfaded in by the wear
        // amount. A worn head loses top end, so the more worn it is the more of the
        // filtered copy is used.
        wearLow += (fluxOut - wearLow) * wearCoefficient;
        return fluxOut + (wearLow - fluxOut) * wearAmount;
    }
};
