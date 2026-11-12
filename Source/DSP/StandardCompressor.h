// =============================================================================
//  The conventional feed-forward compressor on the finished signal.
//
//  Deliberately separate from GlueCompressor, including its detector state -
//  which is why it is a header of its own rather than a mode of that one.
//  Moved out of PluginProcessor.h whole, prose and all. It reaches for
//  juce::Decibels and juce::jmax, which its includer has already declared. The
//  conventions of Source/DSP/ are stated in J37Math.h.
// =============================================================================
#pragma once

#include <cmath>

//==============================================================================
/** A conventional feed-forward compressor for the finished signal. It is
    deliberately separate from GlueCompressor, including its detector state. */
struct StandardCompressor
{
    float envelope = 0.0f;
    float gain = 1.0f;

    void reset() noexcept { envelope = 0.0f; gain = 1.0f; }

    /** Block-rate coefficients, so the per-sample loop does no std::exp work.
        The two coefficients are functions of the sample rate, the attack and the
        release durations - none of which move within a block - so evaluating them
        per sample was two std::exp calls per sample per channel for values that
        change once per block. The same discipline GlueCompressor and SafetyLimiter
        already follow. */
    struct Coefficients
    {
        float attack = 0.0f;
        float release = 0.0f;
    };

    static Coefficients makeCoefficients (float sampleRate, float attackMs,
                                          float releaseMs) noexcept
    {
        const auto coefficientForMs = [] (float sRate, float milliseconds)
        {
            const auto seconds = juce::jmax (0.001f, milliseconds * 0.001f);
            return 1.0f - std::exp (-1.0f / (juce::jmax (1.0f, sRate) * seconds));
        };

        return { coefficientForMs (sampleRate, attackMs),
                 coefficientForMs (sampleRate, releaseMs) };
    }

    float process (float input, float thresholdDb, float ratio,
                   float kneeDb, float makeupDb,
                   const Coefficients& coefficients) noexcept
    {
        const auto detector = input * input;
        const auto detectorCoefficient = detector > envelope
            ? coefficients.attack : coefficients.release;
        envelope += (detector - envelope) * detectorCoefficient;
        const auto envelopeDb = juce::Decibels::gainToDecibels (
            std::sqrt (std::max (0.0f, envelope)), -100.0f);
        const auto safeRatio = juce::jmax (1.0f, ratio);
        const auto halfKnee = juce::jmax (0.01f, kneeDb * 0.5f);
        const auto over = envelopeDb - thresholdDb;
        float reductionDb = 0.0f;
        if (over > halfKnee)
            reductionDb = -(1.0f - 1.0f / safeRatio) * over;
        else if (over > -halfKnee)
        {
            const auto progress = over + halfKnee;
            reductionDb = -(1.0f - 1.0f / safeRatio) * progress * progress
                        / (4.0f * halfKnee);
        }
        const auto targetGain = juce::Decibels::decibelsToGain (reductionDb + makeupDb);
        const auto gainCoefficient = targetGain < gain
            ? coefficients.attack : coefficients.release;
        gain += (targetGain - gain) * gainCoefficient;
        return input * gain;
    }
};
