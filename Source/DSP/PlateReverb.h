// =============================================================================
//  The plate reverb: four all-pass sections into two combs.
//
//  Moved out of PluginProcessor.h, where it was the record's own room, fed from the machine and mixed back into it. The conventions of
//  Source/DSP/ are stated in J37Math.h, and tests/dsp/check_headers_juce.sh
//  compiles this header against real JUCE along with the rest of the directory.
// =============================================================================
#pragma once

#include <array>
#include <cstddef>

//==============================================================================
/**
    A plate/room reverb, built from four Schroeder all-pass sections into two
    comb banks.

    The topology is the classic one because it is the one that sounds like a room
    for the least code: all-passes diffuse the signal without colouring its
    spectrum, and the comb banks that follow set the decay. Two banks, one per
    channel, with different comb lengths, is what gives the stereo image its
    width - a single bank would collapse to mono.

    It sits AFTER the machine in the chain, so it reverberates the processed
    signal rather than feeding back into the saturation. That keeps it clean and
    predictable: a reverb inside the nonlinearity would be modulated by the wow
    and would smear the harmonics the plugin exists to produce.

    The buffers are fixed and allocated once; the controls only change gains and
    feedback, so the audio thread never allocates.
*/
struct PlateReverb
{
    // Four all-passes per channel, then four combs per channel. The lengths are the
    // classic Schroeder numbers scaled for a small room; they are prime-ish so the
    // combs do not reinforce each other into a ringing pitch.
    static constexpr int numAllPasses = 4;
    static constexpr int numCombs = 4;
    static constexpr int maxDelayLength = 4096;

    struct AllPass
    {
        juce::AudioBuffer<float> buffer;
        int writePosition = 0;
        int length = 0;

        void prepare (int len)
        {
            length = juce::jlimit (1, maxDelayLength, len);
            buffer.setSize (1, maxDelayLength, false, true, true);
            buffer.clear();
            writePosition = 0;
        }

        float process (float x, float feedback)
        {
            if (length <= 0)
                return x;

            auto* data = buffer.getWritePointer (0);
            const int readPosition = (writePosition - length + maxDelayLength) % maxDelayLength;
            const float delayed = data[readPosition];

            // The all-pass recurrence: a diffuser, not a delay. It passes every
            // frequency at the same level and only spreads the phase, which is what
            // turns a sparse early reflection into a smooth tail.
            const float out = -x + delayed;
            data[writePosition] = x + delayed * feedback;

            writePosition = (writePosition + 1) % maxDelayLength;
            return out;
        }
    };

    struct Comb
    {
        juce::AudioBuffer<float> buffer;
        int writePosition = 0;
        int length = 0;
        float lowState = 0.0f;

        void prepare (int len)
        {
            length = juce::jlimit (1, maxDelayLength, len);
            buffer.setSize (1, maxDelayLength, false, true, true);
            buffer.clear();
            writePosition = 0;
            lowState = 0.0f;
        }

        float process (float x, float feedback, float damping)
        {
            if (length <= 0)
                return x;

            auto* data = buffer.getWritePointer (0);
            const int readPosition = (writePosition - length + maxDelayLength) % maxDelayLength;
            float delayed = data[readPosition];

            // Damping: the tail loses top end as it decays, which is what a real
            // room does and what stops a comb bank from sounding metallic. A
            // one-pole on the feedback path is the standard way to do it.
            lowState += (delayed - lowState) * damping;
            delayed = lowState;

            data[writePosition] = x + delayed * feedback;
            writePosition = (writePosition + 1) % maxDelayLength;
            return delayed;
        }
    };

    std::array<std::array<AllPass, numAllPasses>, 2> allPasses;
    std::array<std::array<Comb, numCombs>, 2> combs;
    bool prepared = false;

    // The comb and all-pass lengths, per channel. The two channels differ by a few
    // samples, which is the whole of the stereo width.
    static constexpr int allPassLengthsL[numAllPasses] { 225, 341, 441, 556 };
    static constexpr int allPassLengthsR[numAllPasses] { 231, 348, 449, 563 };
    static constexpr int combLengthsL[numCombs] { 1557, 1617, 1491, 1422 };
    static constexpr int combLengthsR[numCombs] { 1580, 1642, 1511, 1447 };

    void prepare (double sampleRate)
    {
        // The lengths are given for 44.1 kHz, so they are scaled for any other rate
        // to keep the room the same physical size.
        const auto scale = static_cast<double> (sampleRate) / 44100.0;
        const auto scaled = [scale] (int base)
        {
            return juce::jlimit (1, maxDelayLength - 1, juce::roundToInt (base * scale));
        };

        for (int channel = 0; channel < 2; ++channel)
        {
            for (int i = 0; i < numAllPasses; ++i)
                allPasses[static_cast<std::size_t> (channel)][static_cast<std::size_t> (i)]
                    .prepare (scaled (channel == 0 ? allPassLengthsL[i] : allPassLengthsR[i]));

            for (int i = 0; i < numCombs; ++i)
                combs[static_cast<std::size_t> (channel)][static_cast<std::size_t> (i)]
                    .prepare (scaled (channel == 0 ? combLengthsL[i] : combLengthsR[i]));
        }

        prepared = true;
    }

    void reset() noexcept
    {
        for (int channel = 0; channel < 2; ++channel)
        {
            for (auto& allPass : allPasses[static_cast<std::size_t> (channel)])
            {
                allPass.buffer.clear();
                allPass.writePosition = 0;
            }

            for (auto& comb : combs[static_cast<std::size_t> (channel)])
            {
                comb.buffer.clear();
                comb.writePosition = 0;
                comb.lowState = 0.0f;
            }
        }
    }

    /**
        One sample through the reverb.

        `size` is 0..1 and sets the tail length; `mix` is 0..1 and is how much of
        the reverberated signal is returned. At mix 0 the stage is bypassed
        entirely, so a machine with no reverb is unchanged.
    */
    float process (float x, int channel, float size, float mix) noexcept
    {
        if (! prepared || mix <= 1.0e-5f)
            return x;

        const auto index = static_cast<std::size_t> (juce::jlimit (0, 1, channel));

        // Decay rises with size. The ceiling is deliberately below 1.0: a comb bank
        // at unity feedback never decays, and the difference between a long reverb
        // and an infinite one is not a useful control.
        const float feedback = 0.70f + size * 0.28f;

        // Damping rises with size as well: a bigger room absorbs more top end per
        // pass, so a long tail is a darker one. That is what keeps a large setting
        // from sounding like a metal tank.
        const float damping = 0.25f + size * 0.35f;

        float diffused = x;
        for (auto& allPass : allPasses[index])
            diffused = allPass.process (diffused, 0.5f);

        float out = 0.0f;
        for (auto& comb : combs[index])
            out += comb.process (diffused, feedback, damping);

        // The comb bank sums four outputs, so it is scaled back to keep the reverb
        // at a comparable level to the dry signal rather than four times it.
        out *= 0.25f;

        return x + out * mix;
    }
};
