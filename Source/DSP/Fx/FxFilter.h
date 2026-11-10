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

        // A bandpass's peak gain IS its Q, and these three run at Q = 6 - so the
        // three bands summed to about +20 dB on a tone sitting at a vowel's first
        // formant, and the old 3x makeup on top of that made the stage a resonator
        // with a gain knob instead of a vowel. The measurement that found it is
        // tests/fx/per_parameter_test.cpp: a 440 Hz sine at -6 dBFS left this stage
        // at +3 dBFS peak and -1 dBFS RMS, which is the "very strange sound" from
        // the panel report. Dividing each band by its own Q puts the peak back at
        // unity, and the summed bands then sit at the level of the signal they are
        // imposing a shape on - which is what a formant filter is for.
        constexpr float bandQ = 6.0f;
        constexpr float bandNormalisation = 1.0f / bandQ;

        for (std::size_t band = 0; band < 3; ++band)
        {
            leftBands[band].setCutoff (frequencies[band], bandQ, rate);
            rightBands[band].setCutoff (frequencies[band], bandQ, rate);

            wetLeft += leftBands[band].process (left, StateVariableFilter::Output::band)
                       * gains[band] * bandNormalisation;
            wetRight += rightBands[band].process (right, StateVariableFilter::Output::band)
                        * gains[band] * bandNormalisation;
        }

        left += (wetLeft - left) * mix;
        right += (wetRight - right) * mix;
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
//  a wrapper pretending a block is a frame, and a spectral effect that delayed
//  the signal by a window would have to be reported to the host as latency the
//  rack does not have. What CAN live here is the same decision made by a
//  filterbank, and it can be made to recombine without changing the level:
//
//    * six log-spaced low passes, all fed the same sample, at 200 Hz to 6.2 kHz;
//    * seven bands, each the difference between two neighbouring low passes -
//      the sub band, five bands between them, and a top band that is the high
//      side of the last crossover. Every section is fourth order, two cascaded
//      second-order filters, and the top band is a high pass rather than what
//      the last low pass left behind: that remainder is only the low pass's
//      phase error, so it carries as much of a loud body as the top band holds
//      of a quiet partial, and a gate that closes it takes the body with it.
//      The low side and the high side of a crossover like this recombine flat in
//      magnitude, so with every band open the gate changes phase, not level.
//    * each band's envelope is compared against the loudest band of the same
//      sample, and a band below that share is faded out. Sharing against the
//      loudest band is what makes it spectral: the reference moves with the
//      programme instead of sitting at a level the user has to keep resetting.
//
//  The BLUR half is the fade's release: at the top of the knob a band closes over
//  20 ms instead of 2, so what is left smears across the silence instead of being
//  chopped, which is the difference between a gate and a blur.
// =============================================================================
class SpectralGate
{
public:
    /** Six crossovers, so seven bands: the sub, the five between them, and the
        top one, which is the high side of the last crossover. */
    static constexpr int crossovers = 6;
    static constexpr int bands = crossovers + 1;

    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;

        for (int band = 0; band < crossovers; ++band)
        {
            const auto index = static_cast<std::size_t> (band);
            const auto hertz = bandFrequency (band);

            for (std::size_t stage = 0; stage < stages; ++stage)
            {
                leftFilters[index][stage].setCutoff (hertz, 0.707f, rate);
                rightFilters[index][stage].setCutoff (hertz, 0.707f, rate);
            }
        }

        for (std::size_t stage = 0; stage < stages; ++stage)
        {
            const auto hertz = bandFrequency (crossovers - 1);

            leftTop[stage].setCutoff (hertz, 0.707f, rate);
            rightTop[stage].setCutoff (hertz, 0.707f, rate);
        }

        for (int band = 0; band < bands; ++band)
        {
            const auto index = static_cast<std::size_t> (band);

            leftEnvelopes[index].setTimes (2.0f, 120.0f, rate);
            rightEnvelopes[index].setTimes (2.0f, 120.0f, rate);
        }

        reset();
    }

    void reset() noexcept
    {
        for (int band = 0; band < crossovers; ++band)
        {
            const auto index = static_cast<std::size_t> (band);

            for (std::size_t stage = 0; stage < stages; ++stage)
            {
                leftFilters[index][stage].reset();
                rightFilters[index][stage].reset();
            }
        }

        for (std::size_t stage = 0; stage < stages; ++stage)
        {
            leftTop[stage].reset();
            rightTop[stage].reset();
        }

        for (int band = 0; band < bands; ++band)
        {
            const auto index = static_cast<std::size_t> (band);
            leftEnvelopes[index].reset();
            rightEnvelopes[index].reset();
            gains[index] = 1.0f;
        }
    }

    void process (float& left, float& right, float amount) noexcept
    {
        const auto depth = clamp01 (amount);

        // The share of the loudest band a band has to reach to stay open: half of
        // it at a touch of the control (-6 dB), a fiftieth at full (-34 dB), and
        // the fade's release lengthening with the same control - the blur.
        const auto share = 0.5f - 0.45f * depth;
        const auto close = 0.01f - 0.009f * depth;

        std::array<float, bands> bandLeft {};
        std::array<float, bands> bandRight {};

        float previousLeft = 0.0f;
        float previousRight = 0.0f;

        for (int band = 0; band < crossovers; ++band)
        {
            const auto index = static_cast<std::size_t> (band);

            const auto lowLeft = cascade (leftFilters[index], left, Output::low);
            const auto lowRight = cascade (rightFilters[index], right, Output::low);

            bandLeft[index] = lowLeft - previousLeft;
            bandRight[index] = lowRight - previousRight;
            previousLeft = lowLeft;
            previousRight = lowRight;
        }

        bandLeft[crossovers] = cascade (leftTop, left, Output::high);
        bandRight[crossovers] = cascade (rightTop, right, Output::high);

        std::array<float, bands> energy {};
        auto loudest = 0.0f;

        for (int band = 0; band < bands; ++band)
        {
            const auto index = static_cast<std::size_t> (band);

            energy[index] = std::max (leftEnvelopes[index].process (bandLeft[index]),
                                      rightEnvelopes[index].process (bandRight[index]));
            loudest = std::max (loudest, energy[index]);
        }

        const auto level = std::max (loudest, 1.0e-6f) * share;

        auto wetLeft = 0.0f;
        auto wetRight = 0.0f;

        for (int band = 0; band < bands; ++band)
        {
            const auto index = static_cast<std::size_t> (band);
            const auto target = energy[index] > level ? 1.0f : 0.0f;
            auto& gain = gains[index];

            gain += (target - gain) * (target > gain ? openingCoefficient : close);
            gain = flushDenormal (gain);

            wetLeft += bandLeft[index] * gain;
            wetRight += bandRight[index] * gain;
        }

        left += (wetLeft - left) * depth;
        right += (wetRight - right) * depth;
    }

private:
    /** Each crossover is fourth order - two cascaded second-order sections.
        That order is what makes the top band a real high pass: at the fourth
        order it holds under a ten-thousandth of a body four octaves below it,
        so closing it takes the sibilance and leaves the body alone. */
    static constexpr std::size_t stages = 2;

    using Output = StateVariableFilter::Output;
    using Chain = std::array<StateVariableFilter, stages>;

    static float cascade (Chain& chain, float sample, Output output) noexcept
    {
        for (std::size_t stage = 0; stage < stages; ++stage)
            sample = chain[stage].process (sample, output);

        return sample;
    }

    static float bandFrequency (int band) noexcept
    {
        // 200 Hz to 6.2 kHz, log-spaced across the six crossovers.
        return 200.0f * std::pow (30.0f, static_cast<float> (band)
                                             / static_cast<float> (crossovers - 1));
    }

    static constexpr float openingCoefficient = 0.05f;

    std::array<Chain, crossovers> leftFilters, rightFilters;
    Chain leftTop, rightTop;
    std::array<EnvelopeFollower, bands> leftEnvelopes, rightEnvelopes;
    std::array<float, bands> gains {};
    double rate = 48000.0;
};

} // namespace j37dsp::fx
