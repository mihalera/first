// =============================================================================
//  The pitch and time family: the shifter, the octaver built on it, the vocoder,
//  the granular processor and the speed control.
//
//  Four of the five are delay-line instruments rather than filters - they read
//  the recent past at a speed other than one sample per sample, which is what
//  changes pitch, and the art is entirely in hiding where the read catches up
//  with the write. Each class below states the trick it uses.
//
//  None of these report latency. They cannot: a granular reader's delay moves,
//  which is exactly what the plugin already says about its own granular corrector
//  (docs/signal-path.md, on the corrector's unreported period of delay). A host
//  asked to compensate a moving delay would move the whole session against it.
// =============================================================================
#pragma once

#include "FxPrimitives.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace j37dsp::fx
{

// =============================================================================
//  Pitch shifter: two taps of one delay line, half a window apart, crossfaded by
//  equal-power windows as they travel.
//
//  A tap whose delay falls by (ratio - 1) samples per sample replays the signal
//  faster and therefore higher. The tap cannot fall forever, so it is wrapped
//  around a window and crossfaded with a second tap that is doing the same thing
//  half a window out of phase - the sum is continuous because one tap is always
//  in the middle of its travel while the other is at an edge.
// =============================================================================
class PitchShifter
{
public:
    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;

        const auto maximum = static_cast<int> (rate * 0.12) + 8;
        leftDelay.prepare (maximum);
        rightDelay.prepare (maximum);

        windowSamples = static_cast<float> (rate) * 0.045f;
        reset();
    }

    void reset() noexcept
    {
        leftDelay.reset();
        rightDelay.reset();
        fraction = 0.0f;
    }

    void setSemitones (float semitones) noexcept { ratio = semitonesToRatio (semitones); }

    void process (float& left, float& right) noexcept
    {
        const auto step = (1.0f - ratio) / windowSamples;

        fraction += step;

        while (fraction >= 1.0f)
            fraction -= 1.0f;

        while (fraction < 0.0f)
            fraction += 1.0f;

        const auto other = fraction >= 0.5f ? fraction - 0.5f : fraction + 0.5f;
        const auto windowLeft = equalPower (fraction);
        const auto windowRight = equalPower (other);

        leftDelay.push (left);
        rightDelay.push (right);

        const auto leftA = leftDelay.read (1.0f + fraction * windowSamples);
        const auto leftB = leftDelay.read (1.0f + other * windowSamples);
        const auto rightA = rightDelay.read (1.0f + fraction * windowSamples);
        const auto rightB = rightDelay.read (1.0f + other * windowSamples);

        left = leftA * windowLeft + leftB * windowRight;
        right = rightA * windowLeft + rightB * windowRight;
    }

    float currentRatio() const noexcept { return ratio; }

private:
    static float equalPower (float position) noexcept
    {
        const auto angle = position * 1.57079632679f;
        return std::cos (angle) * std::cos (angle);
    }

    DelayLine leftDelay, rightDelay;
    double rate = 48000.0;
    float windowSamples = 2160.0f;
    float fraction = 0.0f;
    float ratio = 1.0f;
};

// =============================================================================
//  Octaver.
//
//  The classic octaver is not a pitch shifter - it is a flip-flop that divides
//  the input's period by two, which tracks monophonic playing and produces a
//  clean square-ish sub. This one is the shifter at -12 semitones with the top
//  taken off, which works polyphonically (a divider does not) at the price of the
//  shifter's own artefacts on wide material. The low-pass is not decoration: the
//  sub is there to be felt, and the shifter's grain is not.
// =============================================================================
class Octaver
{
public:
    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        shifter.prepare (rate);
        shifter.setSemitones (-12.0f);
        subLeft.setCutoff (900.0f, rate);
        subRight.setCutoff (900.0f, rate);
        reset();
    }

    void reset() noexcept
    {
        shifter.reset();
        subLeft.reset();
        subRight.reset();
    }

    void process (float& left, float& right, float amount) noexcept
    {
        const auto mix = clamp01 (amount);
        auto subLeftSample = left;
        auto subRightSample = right;

        shifter.process (subLeftSample, subRightSample);

        subLeftSample = subLeft.lowPass (subLeftSample) * 1.4f;
        subRightSample = subRight.lowPass (subRightSample) * 1.4f;

        left += subLeftSample * mix;
        right += subRightSample * mix;
    }

private:
    PitchShifter shifter;
    OnePole subLeft, subRight;
    double rate = 48000.0;
};

// =============================================================================
//  Vocoder with an internal carrier.
//
//  A channel vocoder imposes the spectral envelope of one signal onto another.
//  An insert has no second input to take that carrier from, so this one
//  synthesises it: a sawtooth at 110 Hz, shaped by the same band envelopes the
//  input produced. The result is the robotic, formant-following sound the effect
//  is used for, and the AMOUNT is how much of the vocoded signal replaces the
//  direct one.
// =============================================================================
class Vocoder
{
public:
    static constexpr int bands = 8;

    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;

        for (int band = 0; band < bands; ++band)
        {
            const auto index = static_cast<std::size_t> (band);
            const auto hertz = bandFrequency (band);

            analysisLeft[index].setCutoff (hertz, 4.0f, rate);
            analysisRight[index].setCutoff (hertz, 4.0f, rate);
            carrierBand[index].setCutoff (hertz, 4.0f, rate);
            leftEnvelope[index].setTimes (3.0f, 60.0f, rate);
            rightEnvelope[index].setTimes (3.0f, 60.0f, rate);
        }

        reset();
    }

    void reset() noexcept
    {
        for (int band = 0; band < bands; ++band)
        {
            const auto index = static_cast<std::size_t> (band);
            analysisLeft[index].reset();
            analysisRight[index].reset();
            carrierBand[index].reset();
            leftEnvelope[index].reset();
            rightEnvelope[index].reset();
        }

        carrierPhase = 0.0f;
    }

    void process (float& left, float& right, float amount) noexcept
    {
        const auto mix = clamp01 (amount);

        // A naive saw: the band filters above remove most of the aliasing, and
        // what is left is part of the vocoder's own character.
        carrierPhase += 110.0f / static_cast<float> (rate);

        if (carrierPhase >= 1.0f)
            carrierPhase -= 1.0f;

        const auto carrier = 2.0f * carrierPhase - 1.0f;

        float wetLeft = 0.0f, wetRight = 0.0f;

        for (int band = 0; band < bands; ++band)
        {
            const auto index = static_cast<std::size_t> (band);

            const auto analysedLeft =
                analysisLeft[index].process (left, StateVariableFilter::Output::band);
            const auto analysedRight =
                analysisRight[index].process (right, StateVariableFilter::Output::band);

            const auto carrierBandSample =
                carrierBand[index].process (carrier, StateVariableFilter::Output::band);

            wetLeft += leftEnvelope[index].process (analysedLeft) * carrierBandSample;
            wetRight += rightEnvelope[index].process (analysedRight) * carrierBandSample;
        }

        left += (wetLeft * 14.0f - left) * mix;
        right += (wetRight * 14.0f - right) * mix;
    }

private:
    static float bandFrequency (int band) noexcept
    {
        return 180.0f * std::pow (22.0f, static_cast<float> (band) / 7.0f);
    }

    std::array<StateVariableFilter, bands> analysisLeft, analysisRight, carrierBand;
    std::array<EnvelopeFollower, bands> leftEnvelope, rightEnvelope;
    double rate = 48000.0;
    float carrierPhase = 0.0f;
};

// =============================================================================
//  Granular processor.
//
//  Four overlapping grains read from a shared history at a random offset, each
//  windowed by a raised cosine so it fades in and out rather than clicking. The
//  AMOUNT sets the grain rate: a sparse cloud at the bottom of the knob, a
//  continuous texture at the top. The offsets are drawn from a small LCG rather
//  than std::rand, which takes a global lock on some implementations and has no
//  business on this thread.
// =============================================================================
class Granular
{
public:
    static constexpr int voices = 4;

    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;

        const auto maximum = static_cast<int> (rate * 0.3) + 8;
        leftDelay.prepare (maximum);
        rightDelay.prepare (maximum);

        grainLength = static_cast<int> (rate * 0.07);
        reset();
    }

    void reset() noexcept
    {
        leftDelay.reset();
        rightDelay.reset();

        for (auto& voice : voicesLeft)
        {
            voice.position = 0.0f;
            voice.age = 0;
            voice.length = 0;
        }

        for (auto& voice : voicesRight)
        {
            voice.position = 0.0f;
            voice.age = 0;
            voice.length = 0;
        }

        untilNextGrain = 0.0f;
        randomState = 0x9e3779b9u;
    }

    void process (float& left, float& right, float amount) noexcept
    {
        const auto density = clamp01 (amount);
        const auto grainsPerSecond = 8.0f + 40.0f * density;

        leftDelay.push (left);
        rightDelay.push (right);

        float wetLeft = 0.0f, wetRight = 0.0f;

        for (auto& voice : voicesLeft)
            wetLeft += renderVoice (voice, leftDelay);

        for (auto& voice : voicesRight)
            wetRight += renderVoice (voice, rightDelay);

        untilNextGrain -= 1.0f;

        if (untilNextGrain <= 0.0f)
        {
            untilNextGrain = static_cast<float> (rate) / grainsPerSecond;
            spawnGrain();
        }

        left += (wetLeft * 0.7f - left) * density;
        right += (wetRight * 0.7f - right) * density;
    }

private:
    struct Voice
    {
        float position = 0.0f;
        int age = 0;
        int length = 0;
    };

    float renderVoice (Voice& voice, DelayLine& delay) noexcept
    {
        if (voice.length <= 0 || voice.age >= voice.length)
            return 0.0f;

        const auto window = 0.5f - 0.5f * std::cos (twoPi * static_cast<float> (voice.age)
                                                    / static_cast<float> (voice.length));

        const auto sample = delay.read (voice.position) * window;

        ++voice.age;

        // The grain plays FORWARD through the history, which in a ring buffer
        // measured back from the write head means its delay falls by one sample
        // per sample. The floor keeps the read from arriving at the write head
        // before the window has closed.
        voice.position -= 1.0f;

        if (voice.position < 1.0f)
            voice.position = 1.0f;

        return sample;
    }

    void spawnGrain() noexcept
    {
        const auto offset = randomUnit() * static_cast<float> (rate) * 0.2f + 2.0f;
        const auto length = static_cast<int> (grainLength * (0.6f + 0.8f * randomUnit()));

        for (auto& voice : voicesLeft)
        {
            if (voice.length <= 0 || voice.age >= voice.length)
            {
                voice.position = offset;
                voice.age = 0;
                voice.length = length;
                break;
            }
        }

        for (auto& voice : voicesRight)
        {
            if (voice.length <= 0 || voice.age >= voice.length)
            {
                voice.position = offset;
                voice.age = 0;
                voice.length = length;
                break;
            }
        }
    }

    float randomUnit() noexcept
    {
        randomState = randomState * 1664525u + 1013904223u;
        return static_cast<float> (randomState >> 8) / 16777216.0f;
    }

    std::array<Voice, voices> voicesLeft {}, voicesRight {};
    DelayLine leftDelay, rightDelay;
    double rate = 48000.0;
    int grainLength = 3360;
    float untilNextGrain = 0.0f;
    unsigned randomState = 0x9e3779b9u;
};

// =============================================================================
//  Speed: half, normal or double, with the pitch following the tape.
//
//  A read pointer that advances at 0.5 or 2.0 samples per sample changes both the
//  speed and the pitch at once - which is what a tape does when the capstan is
//  switched, and what this effect is. The knob is a three-position control
//  because that is what the effect is: below a quarter it is half speed, above
//  three quarters it is double, and anywhere else it is 1x, which is a bit-exact
//  pass-through.
//
//  At 1x the read pointer is not used at all: the stage returns its input, so an
//  unchanged setting costs nothing and cannot colour the signal.
// =============================================================================
class Speed
{
public:
    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;

        const auto maximum = static_cast<int> (rate * 0.75) + 8;
        leftDelay.prepare (maximum);
        rightDelay.prepare (maximum);

        reset();
    }

    void reset() noexcept
    {
        leftDelay.reset();
        rightDelay.reset();
        readPosition = 1.0f;
    }

    void process (float& left, float& right, float amount) noexcept
    {
        const auto control = clamp01 (amount);
        const auto selected = control < 0.25f ? 0.5f : (control > 0.75f ? 2.0f : 1.0f);

        if (selected == 1.0f)
        {
            readPosition = 1.0f;
            return;
        }

        leftDelay.push (left);
        rightDelay.push (right);

        // The read consumes history at the tape's own rate: the delay back from
        // the write head falls by `selected` every sample, which is forward
        // playback at that speed and pitch. When it reaches the head the buffer
        // has been played out, and it wraps to the far end - a tape loop's
        // splice, click and all.
        readPosition -= selected;

        const auto maximum = static_cast<float> (leftDelay.maximumDelay() - 4);

        if (readPosition < 1.0f)
            readPosition += maximum;

        left = leftDelay.read (readPosition);
        right = rightDelay.read (readPosition);
    }

private:
    DelayLine leftDelay, rightDelay;
    double rate = 48000.0;
    float readPosition = 0.0f;
};

} // namespace j37dsp::fx
