// =============================================================================
//  The glue compressor: one stage of the tape machine's own headroom.
//
//  Measured on signal by tests/dsp/subfund_test.cpp, which includes this header
//  rather than a copy cut out of PluginProcessor.h. The conventions of
//  Source/DSP/ are stated in J37Math.h.
// =============================================================================
#pragma once

#include "J37Math.h"

#include <cmath>

//==============================================================================
/**
    One glue compressor stage. The two stages in this plugin are completely
    independent: each keeps its own detector envelope and its own gain computer,
    and neither reads the other's state. They are driven purely by the signal
    that reaches them and by the Input / Output parameters.

    Time constants are SEMI-AUTOMATIC - there are no attack or release controls.
    The stage listens to how the signal itself behaves and adapts both constants
    as it goes, along three independent axes:

      1. Programme level - the harder the stage is being driven, the slower it moves,
         which is what makes it read as machine headroom rather than a limiter.
      2. Transient vs sustained material - it tracks a fast and a slow view of the
         signal; when the fast view runs ahead of the slow one we are on a transient,
         so attack is quickened to catch it. When the two agree we are in sustained
         programme, so attack is relaxed and release is stretched, letting the stage
         breathe with the music instead of pumping.
      3. Envelope fill - as the detector fills up, release lengthens further, so a
         dense passage is held together and a sparse one recovers quickly.
*/
struct GlueCompressor
{
    float envelope = 0.0f;

    // A second, much slower follower used only to tell transients apart from sustained
    // programme. Keeping it separate from `envelope` means the adaptation never feeds
    // back into the gain computation itself.
    float slowEnvelope = 0.0f;

    void reset() noexcept
    {
        envelope = 0.0f;
        slowEnvelope = 0.0f;
    }

    /** Envelope level of the most recent block, as 0..1 linear activity. */
    float getEnvelopeActivity() const noexcept
    {
        return juce::jlimit (0.0f, 1.0f, std::sqrt (juce::jmax (0.0f, envelope)));
    }

    /** 0 = sustained programme, 1 = sharp transient. Used by the UI to show activity. */
    float getTransientAmount() const noexcept
    {
        const auto fast = std::sqrt (juce::jmax (0.0f, envelope));
        const auto slow = std::sqrt (juce::jmax (0.0f, slowEnvelope));
        if (slow <= 1.0e-6f)
            return 0.0f;

        return juce::jlimit (0.0f, 1.0f, (fast - slow) / slow * 1.6f);
    }

    /**
        Pushes signal power through the detector; call once per sample.

        attackBaseSeconds and releaseBaseSeconds are the nominal constants. They are
        then adapted by the three axes described above, so the caller never has to
        schedule attack or release by hand - that is the semi-automatic behaviour.

        The two coefficients that do NOT depend on the signal are passed in by the
        caller instead of being recomputed here. Both are functions of the sample
        rate, the release base and the load factor - none of which move within a
        block - so evaluating them per sample was two std::exp calls per sample per
        channel for a value that changes once per block. See the call site in
        processTapeEngine for where they are built.
    */
    struct Coefficients
    {
        float slow = 0.0f;
        float attack = 0.0f;
        float release = 0.0f;
    };

    /** Builds the block-rate coefficients. Called once per block, not per sample. */
    static Coefficients makeCoefficients (float sampleRate,
                                          float attackBaseSeconds,
                                          float releaseBaseSeconds,
                                          float loadFactor) noexcept
    {
        const float safeRate = juce::jmax (1.0f, sampleRate);

        // The slow follower tracks the running programme level, roughly a hundred
        // times slower than the detector itself. It depends only on the release base
        // and the rate, so it is a block-rate constant like the other two.
        const float slowTimeConstant = juce::jmax (0.05f, releaseBaseSeconds * 3.5f);

        // Load lengthens both constants, so a stage being leaned on turns slow and
        // dense while an idle one stays quick and transparent. The load factor is
        // the INPUT / OUTPUT trim, which is also read once per block.
        const float loadStretch = 1.0f + loadFactor * 1.8f;
        const float releaseStretch = 1.0f + loadFactor * 2.2f;

        Coefficients coefficients;
        coefficients.slow = j37math::exp (-1.0f / (safeRate * slowTimeConstant));
        coefficients.attack = attackBaseSeconds * loadStretch;
        coefficients.release = releaseBaseSeconds * releaseStretch;
        return coefficients;
    }

    float processDetection (float detectorPower,
                            float sampleRate,
                            const Coefficients& blockCoefficients) noexcept
    {
        const float safeRate = juce::jmax (1.0f, sampleRate);

        // -- Axis 3: how full the detector already is --------------------------
        const float envelopeLevel = getEnvelopeActivity();

        // -- Axis 2: transient or sustained? ----------------------------------
        // Comparing the fast detector against the slow programme follower is enough
        // to tell a drum hit (fast spikes above the slow average) from a sustained
        // pad or vocal line.
        const float slowCoefficient = blockCoefficients.slow;
        slowEnvelope = slowCoefficient * slowEnvelope
                     + (1.0f - slowCoefficient) * detectorPower;

        const float transientAmount = getTransientAmount();

        // Attack: quick on transients so nothing is missed, relaxed on sustained
        // material so the stage does not clamp the body of the sound. The transient
        // term dominates the level term, because catching a peak matters more.
        //
        // Only the two signal-dependent factors are evaluated here; the load stretch
        // and the base constant are already folded into blockCoefficients.attack.
        const float transientSpeedUp = 1.0f - transientAmount * 0.72f;
        const float attackSeconds = blockCoefficients.attack
                                  * juce::jlimit (0.25f, 1.6f, transientSpeedUp)
                                  * (1.0f + envelopeLevel * 0.9f);

        // Release: long on sustained programme and when the detector is full, short on
        // isolated transients so the stage reopens before the next event. This is what
        // gives the classic auto-release feel - dense passages stay together, sparse
        // ones breathe.
        const float releaseStretch = 1.0f
                                   + (1.0f - transientAmount) * 1.15f
                                   + envelopeLevel * 2.4f;
        const float releaseSeconds = blockCoefficients.release * releaseStretch;

        const float timeConstant = detectorPower > envelope ? attackSeconds : releaseSeconds;
        const float coefficient = j37math::exp (-1.0f / (safeRate * timeConstant));
        envelope = coefficient * envelope + (1.0f - coefficient) * detectorPower;

        return juce::Decibels::gainToDecibels (j37math::sqrt (juce::jmax (0.0f, envelope)),
                                               -100.0f);
    }
};
