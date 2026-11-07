// =============================================================================
//  The small arithmetic the effects rack is built from.
//
//  Everything under Source/DSP/Fx/ is a per-sample stage with no JUCE in it, for
//  the same reason Source/DSP/AutoGainCompensator.h has none: it lets tests/fx
//  compile the headers exactly as they ship, on a machine with no plugin, no host
//  and no audio device. Nothing here allocates after prepare(), takes a lock,
//  logs, or throws - the rack runs inside the engine's per-sample loop, so those
//  are the rules it inherits from docs/architecture.md.
//
//  The primitives are deliberately few. Every effect below is a combination of a
//  one-pole, a state-variable filter, an LFO, an envelope follower, a delay line
//  and an antiderivatively-antialiased shaper; writing them once is what keeps
//  twenty-five stages reviewable instead of twenty-five private implementations
//  of the same four poles.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace j37dsp::fx
{

inline constexpr float twoPi = 6.28318530718f;

/** The rack's controls are 0..1 proportions, so this is the clamp every stage
    applies before it maps one onto a frequency, a depth or a drive. */
inline float clamp01 (float value) noexcept
{
    return std::clamp (value, 0.0f, 1.0f);
}

/** A denormal inside a feedback path is not an error, it is a CPU fire: the
    arithmetic stays correct and the processor drops into a slow path. Every
    state the rack feeds back through itself is passed through this. */
inline float flushDenormal (float value) noexcept
{
    return std::abs (value) < 1.0e-20f ? 0.0f : value;
}

/** Semitones to a frequency ratio. */
inline float semitonesToRatio (float semitones) noexcept
{
    return std::pow (2.0f, semitones / 12.0f);
}

// =============================================================================
//  One pole. The rack's DC blockers, damping filters and smoothing all use it.
// =============================================================================
class OnePole
{
public:
    void setCutoff (float hertz, double sampleRate) noexcept
    {
        const auto rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        const auto limited = std::clamp (hertz, 0.5f, static_cast<float> (rate * 0.49));
        coefficient = 1.0f - std::exp (-twoPi * limited / static_cast<float> (rate));
    }

    void reset() noexcept { state = 0.0f; }

    float lowPass (float input) noexcept
    {
        state += coefficient * (input - state);
        state = flushDenormal (state);
        return state;
    }

    float highPass (float input) noexcept { return input - lowPass (input); }

private:
    float coefficient = 1.0f;
    float state = 0.0f;
};

// =============================================================================
//  Topology-preserving state-variable filter (Zavalishin). Stable when the
//  cutoff is modulated faster than the coefficient is recomputed - which is what
//  every auto-wah, envelope filter and formant sweep below does.
// =============================================================================
class StateVariableFilter
{
public:
    enum class Output { low, band, high };

    void setCutoff (float hertz, float q, double sampleRate) noexcept
    {
        const auto rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        const auto limited = std::clamp (hertz, 20.0f, static_cast<float> (rate * 0.45));
        const auto g = std::tan (3.14159265359f * limited / static_cast<float> (rate));
        const auto safeQ = std::max (0.05f, q);

        k = 1.0f / safeQ;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void reset() noexcept { s1 = 0.0f; s2 = 0.0f; }

    float process (float input, Output output) noexcept
    {
        const auto v3 = input - s2;
        const auto v1 = a1 * s1 + a2 * v3;
        const auto v2 = s2 + a2 * s1 + a3 * v3;

        s1 = flushDenormal (2.0f * v1 - s1);
        s2 = flushDenormal (2.0f * v2 - s2);

        switch (output)
        {
            case Output::low:  return v2;
            case Output::band: return v1;
            case Output::high: return input - k * v1 - v2;
        }

        return v2;
    }

private:
    float k = 1.0f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    float s1 = 0.0f, s2 = 0.0f;
};

// =============================================================================
//  A sine LFO with an offset phase, which is how the chorus and the rotary
//  speaker put the two sides out of step without a second oscillator.
// =============================================================================
class Lfo
{
public:
    void setRate (float hertz, double sampleRate) noexcept
    {
        const auto rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        increment = std::max (0.0f, hertz) / static_cast<float> (rate);
    }

    void setPhase (float phase01) noexcept { phase = phase01 - std::floor (phase01); }

    void reset (float phase01 = 0.0f) noexcept { setPhase (phase01); }

    /** Unipolar, 0..1. */
    float next() noexcept
    {
        const auto value = 0.5f + 0.5f * std::sin (twoPi * phase);
        phase += increment;

        if (phase >= 1.0f)
            phase -= 1.0f;

        return value;
    }

    /** The value at the current phase without advancing, for the second channel. */
    float peek() const noexcept { return 0.5f + 0.5f * std::sin (twoPi * phase); }

private:
    float phase = 0.0f;
    float increment = 0.0f;
};

// =============================================================================
//  Peak envelope follower. `process` takes the absolute value of what it is
//  handed, so callers do not each have to remember to.
// =============================================================================
class EnvelopeFollower
{
public:
    void setTimes (float attackMs, float releaseMs, double sampleRate) noexcept
    {
        const auto rate = static_cast<float> (sampleRate > 0.0 ? sampleRate : 48000.0);
        attack = 1.0f - std::exp (-1.0f / (std::max (0.01f, attackMs) * 0.001f * rate));
        release = 1.0f - std::exp (-1.0f / (std::max (0.01f, releaseMs) * 0.001f * rate));
    }

    void reset (float value = 0.0f) noexcept { envelope = value; }

    float process (float input) noexcept
    {
        const auto rectified = std::abs (input);
        const auto coefficient = rectified > envelope ? attack : release;
        envelope += coefficient * (rectified - envelope);
        envelope = flushDenormal (envelope);
        return envelope;
    }

    float value() const noexcept { return envelope; }

private:
    float attack = 1.0f, release = 0.001f, envelope = 0.0f;
};

// =============================================================================
//  A fractional-delay ring. prepare() is the only place it allocates, and it is
//  called from prepareToPlay, never from the audio callback.
// =============================================================================
class DelayLine
{
public:
    void prepare (int maximumSamples)
    {
        const auto size = std::max (4, maximumSamples);
        buffer.assign (static_cast<std::size_t> (size), 0.0f);
        length = size;
        writeIndex = 0;
    }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;
    }

    bool isPrepared() const noexcept { return length > 0; }

    void push (float input) noexcept
    {
        buffer[static_cast<std::size_t> (writeIndex)] = input;

        if (++writeIndex >= length)
            writeIndex = 0;
    }

    /** Linear interpolation at `delaySamples` back from the newest sample. One
        sample back is the sample just pushed; the caller keeps the delay at or
        above 1 so a read never lands on the write head. */
    float read (float delaySamples) const noexcept
    {
        auto position = static_cast<float> (writeIndex) - delaySamples;

        while (position < 0.0f)
            position += static_cast<float> (length);

        auto index0 = static_cast<int> (position);
        const auto fraction = position - static_cast<float> (index0);

        if (index0 >= length)
            index0 -= length;

        auto index1 = index0 + 1;

        if (index1 >= length)
            index1 -= length;

        const auto a = buffer[static_cast<std::size_t> (index0)];
        const auto b = buffer[static_cast<std::size_t> (index1)];

        return a + (b - a) * fraction;
    }

    int maximumDelay() const noexcept { return length; }

private:
    std::vector<float> buffer;
    int length = 0;
    int writeIndex = 0;
};

// =============================================================================
//  Antiderivative antialiasing for the odd shaper.
//
//  A memoryless tanh at 48 kHz is a broadband distortion and its aliases fold
//  back into the audible band as inharmonic grit. The first-order ADAA form
//  averages the curve over the segment between the previous and current input,
//  which is a one-sample integral of the nonlinearity and removes most of that
//  fold without oversampling. F(x) = integral of tanh is log(cosh x).
// =============================================================================
class AdaaTanh
{
public:
    void reset() noexcept { previousInput = 0.0f; previousIntegral = 0.0f; }

    float process (float input) noexcept
    {
        const auto limited = std::clamp (input, -18.0f, 18.0f);
        const auto integral = std::log (std::cosh (limited));
        const auto delta = limited - previousInput;

        const auto output = std::abs (delta) > 1.0e-4f
                                ? (integral - previousIntegral) / delta
                                : std::tanh (0.5f * (limited + previousInput));

        previousInput = limited;
        previousIntegral = integral;

        return output;
    }

private:
    float previousInput = 0.0f;
    float previousIntegral = 0.0f;
};

/** Waveshaping without the antiderivative, for the fold: the sine folder is
    already band-limited by the fold itself at the depths this rack uses, and the
    ADAA integral of sin(x*drive) is a division by the drive that changes every
    block. */
inline float foldSample (float input) noexcept
{
    return std::sin (input);
}

/** Equal-power dry/wet pair, the same law MixCrossfader uses, so a stage's
    amount means the level an engineer expects rather than a linear fade. */
inline void crossfadeGains (float amount, float& dry, float& wet) noexcept
{
    const auto angle = clamp01 (amount) * 1.57079632679f;
    dry = std::cos (angle);
    wet = std::sin (angle);
}

} // namespace j37dsp::fx
