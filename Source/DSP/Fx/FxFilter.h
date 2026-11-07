// =============================================================================
//  The filter family: the envelope wah, the comb, the formant bank and the
//  spectral gate.
//
//  These four are band-shapers rather than tone controls: each one changes where
//  the energy IS instead of how much of it there is, and each one is a
//  deliberate special case rather than a general-purpose EQ. The wah and the
//  formant bank are sweeps; the comb is a single resonance chain; the spectral
//  gate is a filterbank standing in for the STFT that the engine's per-sample
//  contract cannot host (see the note on that class).
// =============================================================================
#pragma once

#include "FxPrimitives.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace j37dsp::fx
{

// =============================================================================
//  Envelope wah.
//
//  A band-pass whose centre is pushed up by the signal's own envelope: quiet
//  playing leaves the peak low and dark, a harder note opens it. That coupling
//  is what makes it an envelope filter rather than a fixed resonant peak, and it
//  is why the stage reads the input BEFORE its own filter - reading its output
//  would make the sweep depend on the sweep.
// =============================================================================
class EnvelopeWah
{
public:
    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        detector.setTimes (5.0f, 90.0f, rate);
        leftFilter.setCutoff (400.0f, 2.5f, rate);
        rightFilter.setCutoff (400.0f, 2.5f, rate);
        reset();
    }

    void reset() noexcept
    {
        detector.reset();
        leftFilter.reset();
        rightFilter.reset();
    }

    void process (float& left, float& right, float amount) noexcept
    {
        const auto depth = clamp01 (amount);
        const auto envelope = detector.process (std::max (std::abs (left), std::abs (right)));
        const auto opening = std::min (1.0f, envelope * 4.0f);

        const auto hertz = 350.0f + 2600.0f * depth * opening;

        leftFilter.setCutoff (hertz, 2.5f, rate);
        rightFilter.setCutoff (hertz, 2.5f, rate);

        const auto wetLeft = leftFilter.process (left, StateVariableFilter::Output::band);
        const auto wetRight = rightFilter.process (right, StateVariableFilter::Output::band);

        left += (wetLeft * 1.8f - left) * depth;
        right += (wetRight * 1.8f - right) * depth;
    }

private:
    EnvelopeFollower detector;
    StateVariableFilter leftFilter, rightFilter;
    double rate = 48000.0;
};

// =============================================================================
//  Comb filter: one delay line, fed back through a damping pole, summed with
//  the dry signal. The delay is fixed at about 4 ms (a 250 Hz fundamental), so
//  the knob is the RESONANCE, not the pitch - a comb whose tuning follows the
//  knob is a flanger, and this rack already has one.
// =============================================================================
class CombFilter
{
public:
    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        const auto maximum = static_cast<int> (rate * 0.02) + 8;

        leftDelay.prepare (maximum);
        rightDelay.prepare (maximum);

        delaySamples = static_cast<float> (rate) * 0.004f;
        leftDamping.setCutoff (6000.0f, rate);
        rightDamping.setCutoff (6000.0f, rate);
        reset();
    }

    void reset() noexcept
    {
        leftDelay.reset();
        rightDelay.reset();
        leftDamping.reset();
        rightDamping.reset();
        feedbackLeft = feedbackRight = 0.0f;
    }

    void process (float& left, float& right, float amount) noexcept
    {
        const auto resonance = clamp01 (amount) * 0.92f;

        leftDelay.push (left + flushDenormal (feedbackLeft * resonance));
        rightDelay.push (right + flushDenormal (feedbackRight * resonance));

        const auto delayedLeft = leftDelay.read (delaySamples);
        const auto delayedRight = rightDelay.read (delaySamples);

        feedbackLeft = leftDamping.lowPass (delayedLeft);
        feedbackRight = rightDamping.lowPass (delayedRight);

        left += delayedLeft * resonance;
        right += delayedRight * resonance;
    }

private:
    DelayLine leftDelay, rightDelay;
    OnePole leftDamping, rightDamping;
    double rate = 48000.0;
    float delaySamples = 192.0f;
    float feedbackLeft = 0.0f, feedbackRight = 0.0f;
};

// =============================================================================
//  Formant filter: three resonant peaks walked through the five cardinal vowels.
//
//  The frequencies below are the classic male-voice formants. Their RELATIVE
//  gains matter as much as their positions - F2 carries the vowel's identity and
//  F3 is a whisper of it - which is why the bank is not three equal peaks.
// =============================================================================
class FormantFilter
{
public:
    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;

        for (auto& filter : leftBands)
            filter.setCutoff (500.0f, 4.0f, rate);

        for (auto& filter : rightBands)
            filter.setCutoff (500.0f, 4.0f, rate);

        reset();
    }

    void reset() noexcept
    {
        for (auto& filter : leftBands)
            filter.reset();

        for (auto& filter : rightBands)
            filter.reset();
    }

    void process (float& left, float& right, float amount) noexcept
    {
        const auto mix = clamp01 (amount);
        const auto position = mix * (vowels - 1);

        const auto index = std::min (static_cast<int> (position), vowels - 2);
        const auto fraction = position - static_cast<float> (index);

        float frequencies[3];
        float gains[3];

        for (int band = 0; band < 3; ++band)
        {
            const auto a = vowelFormants[static_cast<std::size_t> (index)][static_cast<std::size_t> (band)];
            const auto b = vowelFormants[static_cast<std::size_t> (index + 1)][static_cast<std::size_t> (band)];
            frequencies[band] = a + (b - a) * fraction;

            const auto ga = vowelGains[static_cast<std::size_t> (index)][static_cast<std::size_t> (band)];
            const auto gb = vowelGains[static_cast<std::size_t> (index + 1)][static_cast<std::size_t> (band)];
            gains[band] = ga + (gb - ga) * fraction;
        }

        float wetLeft = 0.0f, wetRight = 0.0f;

        for (std::size_t band = 0; band < 3; ++band)
        {
            leftBands[band].setCutoff (frequencies[band], 6.0f, rate);
            rightBands[band].setCutoff (frequencies[band], 6.0f, rate);

            wetLeft += leftBands[band].process (left, StateVariableFilter::Output::band) * gains[band];
            wetRight += rightBands[band].process (right, StateVariableFilter::Output::band) * gains[band];
        }

        left += (wetLeft * 3.0f - left) * mix;
        right += (wetRight * 3.0f - right) * mix;
    }

private:
    static constexpr int vowels = 5;

    std::array<StateVariableFilter, 3> leftBands, rightBands;
    double rate = 48000.0;

    // a, e, i, o, u - F1, F2, F3 in Hz.
    inline static constexpr float vowelFormants[vowels][3] = {
        { 800.0f, 1150.0f, 2900.0f },
        { 400.0f, 1600.0f, 2700.0f },
        { 350.0f, 1700.0f, 2700.0f },
        { 450.0f, 800.0f, 2830.0f },
        { 325.0f, 700.0f, 2530.0f }
    };

    inline static constexpr float vowelGains[vowels][3] = {
        { 1.0f, 0.63f, 0.10f },
        { 1.0f, 0.50f, 0.17f },
        { 1.0f, 0.35f, 0.10f },
        { 1.0f, 0.50f, 0.14f },
        { 1.0f, 0.25f, 0.10f }
    };
};

// =============================================================================
//  Spectral gate / blur.
//
//  The engine drives every stage one sample at a time, so an STFT - which needs
//  a window of samples before it can decide anything - cannot live here without
//  a wrapper pretending a block is a frame. What CAN live here is the same
//  decision made by a filterbank: six log-spaced bands, each band's envelope
//  compared against the programme's own, and each band faded out when it sits
//  below that reference. That is a spectral gate in the sense the effect means -
//  quiet partials are removed, loud ones survive - and it keeps the stage's cost
//  and latency stated (none on either count) rather than hiding a block.
//
//  The BLUR half is the gate's release: at the top of the knob a band's gain
//  falls slowly, so what is left smears across the silence instead of being
//  chopped, which is the difference between a gate and a blur.
// =============================================================================
class SpectralGate
{
public:
    static constexpr int bands = 6;

    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;

        for (int band = 0; band < bands; ++band)
        {
            const auto hertz = bandFrequency (band);

            leftFilters[static_cast<std::size_t> (band)].setCutoff (hertz, 3.0f, rate);
            rightFilters[static_cast<std::size_t> (band)].setCutoff (hertz, 3.0f, rate);
            leftEnvelopes[static_cast<std::size_t> (band)].setTimes (2.0f, 120.0f, rate);
            rightEnvelopes[static_cast<std::size_t> (band)].setTimes (2.0f, 120.0f, rate);
        }

        totalEnvelope.setTimes (2.0f, 120.0f, rate);
        reset();
    }

    void reset() noexcept
    {
        for (int band = 0; band < bands; ++band)
        {
            leftFilters[static_cast<std::size_t> (band)].reset();
            rightFilters[static_cast<std::size_t> (band)].reset();
            leftEnvelopes[static_cast<std::size_t> (band)].reset();
            rightEnvelopes[static_cast<std::size_t> (band)].reset();
            gains[static_cast<std::size_t> (band)] = 1.0f;
        }

        totalEnvelope.reset();
    }

    void process (float& left, float& right, float amount) noexcept
    {
        const auto depth = clamp01 (amount);
        const auto threshold = 0.5f - 0.45f * depth;
        const auto blurCoefficient = 0.002f - 0.0019f * depth;

        totalEnvelope.process (std::max (std::abs (left), std::abs (right)));
        const auto reference = std::max (totalEnvelope.value() * 0.02f, 1.0e-6f);

        float wetLeft = 0.0f, wetRight = 0.0f;

        for (int band = 0; band < bands; ++band)
        {
            const auto index = static_cast<std::size_t> (band);
            const auto bandLeft = leftFilters[index].process (left, StateVariableFilter::Output::band);
            const auto bandRight = rightFilters[index].process (right, StateVariableFilter::Output::band);

            const auto energy = std::max (leftEnvelopes[index].process (bandLeft),
                                          rightEnvelopes[index].process (bandRight));

            const auto target = energy > reference * threshold ? 1.0f : 0.0f;
            auto& gain = gains[index];
            gain += (target - gain) * (target > gain ? 0.05f : blurCoefficient);
            gain = flushDenormal (gain);

            wetLeft += bandLeft * gain;
            wetRight += bandRight * gain;
        }

        left += (wetLeft * 2.5f - left) * depth;
        right += (wetRight * 2.5f - right) * depth;
    }

private:
    static float bandFrequency (int band) noexcept
    {
        // 200 Hz to 6 kHz, log-spaced across the six bands.
        return 200.0f * std::pow (30.0f, static_cast<float> (band) / 5.0f);
    }

    std::array<StateVariableFilter, bands> leftFilters, rightFilters;
    std::array<EnvelopeFollower, bands> leftEnvelopes, rightEnvelopes;
    std::array<float, bands> gains {};
    EnvelopeFollower totalEnvelope;
    double rate = 48000.0;
};

} // namespace j37dsp::fx
