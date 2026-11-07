// =============================================================================
//  The modulation family: chorus, flanger, phaser, tremolo, vibrato and the
//  rotary speaker.
//
//  All six are time-varying filters, so each one owns its delay memory and its
//  own oscillator and nothing else. They are driven one sample at a time from
//  the engine's per-sample loop (see FxRack.h), which is why they take a frame
//  of two samples rather than a block: the chorus and the rotary put the two
//  sides out of phase, and a per-channel interface would move that phase by a
//  buffer length.
//
//  The single control each stage exposes is its AMOUNT, 0..1. Depth, feedback
//  and wet mix are all driven from it and the rates are the classic ones, fixed
//  on purpose: a chorus that needs three knobs before it sounds like a chorus is
//  a chorus nobody turns on. Where a rate is musically load-bearing (the rotary)
//  it is mapped from the amount and the mapping is stated.
// =============================================================================
#pragma once

#include "FxPrimitives.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace j37dsp::fx
{

// =============================================================================
//  Chorus: two taps, one per side, a quarter cycle apart, around 12 ms.
// =============================================================================
class Chorus
{
public:
    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        const auto maximum = static_cast<int> (rate * 0.05) + 8;

        leftDelay.prepare (maximum);
        rightDelay.prepare (maximum);

        leftLfo.setRate (static_cast<float> (0.35), rate);
        rightLfo.setRate (static_cast<float> (0.35), rate);
        leftLfo.reset (0.0f);
        rightLfo.reset (0.25f);

        baseDelay = static_cast<float> (rate) * 0.012f;
        depth = static_cast<float> (rate) * 0.006f;
    }

    void reset() noexcept
    {
        leftDelay.reset();
        rightDelay.reset();
        leftLfo.reset (0.0f);
        rightLfo.reset (0.25f);
    }

    void process (float& left, float& right, float amount) noexcept
    {
        const auto depthNow = clamp01 (amount);
        const auto delayLeft = baseDelay + depth * leftLfo.next();
        const auto delayRight = baseDelay + depth * rightLfo.next();

        leftDelay.push (left);
        rightDelay.push (right);

        const auto wetLeft = leftDelay.read (delayLeft);
        const auto wetRight = rightDelay.read (delayRight);

        left += (wetLeft - left) * depthNow;
        right += (wetRight - right) * depthNow;
    }

private:
    DelayLine leftDelay, rightDelay;
    Lfo leftLfo, rightLfo;
    double rate = 48000.0;
    float baseDelay = 576.0f;
    float depth = 288.0f;
};

// =============================================================================
//  Flanger: the same idea at a tenth of the delay, with feedback, which is what
//  turns a comb into the jet.
// =============================================================================
class Flanger
{
public:
    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        const auto maximum = static_cast<int> (rate * 0.02) + 8;

        leftDelay.prepare (maximum);
        rightDelay.prepare (maximum);

        leftLfo.setRate (static_cast<float> (0.25), rate);
        rightLfo.setRate (static_cast<float> (0.25), rate);
        leftLfo.reset (0.0f);
        rightLfo.reset (0.5f);

        baseDelay = static_cast<float> (rate) * 0.0012f;
        depth = static_cast<float> (rate) * 0.0010f;
    }

    void reset() noexcept
    {
        leftDelay.reset();
        rightDelay.reset();
        leftLfo.reset (0.0f);
        rightLfo.reset (0.5f);
        feedbackLeft = feedbackRight = 0.0f;
    }

    void process (float& left, float& right, float amount) noexcept
    {
        const auto depthNow = clamp01 (amount);
        const auto delayLeft = baseDelay + depth * leftLfo.next();
        const auto delayRight = baseDelay + depth * rightLfo.next();
        const auto feedbackGain = 0.7f * depthNow;

        leftDelay.push (left + flushDenormal (feedbackLeft * feedbackGain));
        rightDelay.push (right + flushDenormal (feedbackRight * feedbackGain));

        feedbackLeft = leftDelay.read (delayLeft);
        feedbackRight = rightDelay.read (delayRight);

        left += (feedbackLeft - left) * depthNow * 0.7f;
        right += (feedbackRight - right) * depthNow * 0.7f;
    }

private:
    DelayLine leftDelay, rightDelay;
    Lfo leftLfo, rightLfo;
    double rate = 48000.0;
    float baseDelay = 57.6f;
    float depth = 48.0f;
    float feedbackLeft = 0.0f, feedbackRight = 0.0f;
};

// =============================================================================
//  Phaser: six first-order allpass sections per side, swept across the vocal
//  presence band, with feedback around the chain.
// =============================================================================
class Phaser
{
public:
    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;

        leftLfo.setRate (static_cast<float> (0.4), rate);
        rightLfo.setRate (static_cast<float> (0.4), rate);
        leftLfo.reset (0.0f);
        rightLfo.reset (0.25f);

        minimumHz = 200.0f;
        maximumHz = 3000.0f;
        reset();
    }

    void reset() noexcept
    {
        for (int stage = 0; stage < stages; ++stage)
            leftState[static_cast<std::size_t> (stage)] =
                rightState[static_cast<std::size_t> (stage)] = 0.0f;

        feedbackLeft = feedbackRight = 0.0f;
        leftLfo.reset (0.0f);
        rightLfo.reset (0.25f);
    }

    void process (float& left, float& right, float amount) noexcept
    {
        const auto depthNow = clamp01 (amount);
        const auto sweepLeft = minimumHz * std::pow (maximumHz / minimumHz, leftLfo.next());
        const auto sweepRight = minimumHz * std::pow (maximumHz / minimumHz, rightLfo.next());

        const auto wetLeft = processChannel (left + feedbackLeft * 0.5f * depthNow,
                                             sweepLeft, 0);
        const auto wetRight = processChannel (right + feedbackRight * 0.5f * depthNow,
                                              sweepRight, 1);

        feedbackLeft = flushDenormal (wetLeft);
        feedbackRight = flushDenormal (wetRight);

        left += (wetLeft - left) * depthNow * 0.6f;
        right += (wetRight - right) * depthNow * 0.6f;
    }

private:
    static constexpr int stages = 6;

    float processChannel (float input, float cutoff, int side) noexcept
    {
        const auto tangent = std::tan (3.14159265359f
                                       * std::clamp (cutoff, 20.0f,
                                                     static_cast<float> (rate * 0.45))
                                       / static_cast<float> (rate));
        const auto coefficient = (tangent - 1.0f) / (tangent + 1.0f);
        auto& states = side == 0 ? leftState : rightState;

        auto signal = input;

        for (int stage = 0; stage < stages; ++stage)
        {
            auto& state = states[static_cast<std::size_t> (stage)];
            const auto output = coefficient * signal + state;
            state = flushDenormal (signal - coefficient * output);
            signal = output;
        }

        return signal;
    }

    std::array<float, static_cast<std::size_t> (stages)> leftState {}, rightState {};
    Lfo leftLfo, rightLfo;
    double rate = 48000.0;
    float minimumHz = 200.0f, maximumHz = 3000.0f;
    float feedbackLeft = 0.0f, feedbackRight = 0.0f;
};

// =============================================================================
//  Tremolo: amplitude, one oscillator, no delay memory.
// =============================================================================
class Tremolo
{
public:
    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        lfo.setRate (4.5f, rate);
        lfo.reset (0.0f);
    }

    void reset() noexcept { lfo.reset (0.0f); }

    void process (float& left, float& right, float amount) noexcept
    {
        const auto depth = clamp01 (amount);
        const auto gain = 1.0f - depth * lfo.next();

        left *= gain;
        right *= gain;
    }

private:
    Lfo lfo;
    double rate = 48000.0;
};

// =============================================================================
//  Vibrato: delay modulation with no dry leg. The AMOUNT crossfades the dry
//  signal in at the bottom of its travel, so switching the stage on is a shallow
//  vibrato rather than a delay jump.
// =============================================================================
class Vibrato
{
public:
    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        const auto maximum = static_cast<int> (rate * 0.02) + 8;

        leftDelay.prepare (maximum);
        rightDelay.prepare (maximum);

        leftLfo.setRate (5.0f, rate);
        rightLfo.setRate (5.0f, rate);

        centreDelay = static_cast<float> (rate) * 0.006f;
        depth = static_cast<float> (rate) * 0.003f;
        reset();
    }

    void reset() noexcept
    {
        leftDelay.reset();
        rightDelay.reset();
        leftLfo.reset (0.0f);
        rightLfo.reset (0.0f);
    }

    void process (float& left, float& right, float amount) noexcept
    {
        const auto mix = clamp01 (amount);
        const auto delayLeft = centreDelay + depth * leftLfo.next();
        const auto delayRight = centreDelay + depth * rightLfo.next();
        const auto dryLeft = left;
        const auto dryRight = right;

        leftDelay.push (left);
        rightDelay.push (right);

        const auto wetLeft = leftDelay.read (delayLeft);
        const auto wetRight = rightDelay.read (delayRight);

        left = dryLeft + (wetLeft - dryLeft) * mix;
        right = dryRight + (wetRight - dryRight) * mix;
    }

private:
    DelayLine leftDelay, rightDelay;
    Lfo leftLfo, rightLfo;
    double rate = 48000.0;
    float centreDelay = 288.0f;
    float depth = 144.0f;
};

// =============================================================================
//  Rotary speaker.
//
//  A rotating horn does two things at once, and both are needed for the sound: a
//  Doppler shift from the horn moving toward and away from the listener, and an
//  amplitude modulation from its directivity. The Doppler is the delay
//  modulation and the amplitude is the gain, on the SAME oscillator - which is
//  why the two are not independent controls here. The sides are a quarter cycle
//  apart, as they are across a real cabinet, and the AMOUNT sets the rotation
//  rate (0.8 Hz to 3.3 Hz, slow rotor to fast) as well as the depth.
// =============================================================================
class Rotary
{
public:
    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        const auto maximum = static_cast<int> (rate * 0.03) + 8;

        leftDelay.prepare (maximum);
        rightDelay.prepare (maximum);

        centreDelay = static_cast<float> (rate) * 0.008f;
        depth = static_cast<float> (rate) * 0.0025f;
        reset();
    }

    void reset() noexcept
    {
        leftDelay.reset();
        rightDelay.reset();
        leftLfo.reset (0.0f);
        rightLfo.reset (0.25f);
        leftHornFilter.setCutoff (6000.0f, rate);
        rightHornFilter.setCutoff (6000.0f, rate);
        leftHornFilter.reset();
        rightHornFilter.reset();
    }

    void process (float& left, float& right, float amount) noexcept
    {
        const auto mix = clamp01 (amount);
        const auto hertz = 0.8f + 2.5f * mix;

        leftLfo.setRate (hertz, rate);
        rightLfo.setRate (hertz, rate);

        const auto modulationLeft = leftLfo.next();
        const auto modulationRight = rightLfo.next();

        const auto delayLeft = centreDelay + depth * modulationLeft;
        const auto delayRight = centreDelay + depth * modulationRight;

        leftDelay.push (left);
        rightDelay.push (right);

        auto wetLeft = leftDelay.read (delayLeft);
        auto wetRight = rightDelay.read (delayRight);

        // The horn's own brightness rides the same motion: the cabinet is
        // brighter when the horn faces the listener.
        wetLeft = leftHornFilter.lowPass (wetLeft) * (0.55f + 0.45f * modulationLeft);
        wetRight = rightHornFilter.lowPass (wetRight) * (0.55f + 0.45f * modulationRight);

        left += (wetLeft - left) * mix;
        right += (wetRight - right) * mix;
    }

private:
    DelayLine leftDelay, rightDelay;
    Lfo leftLfo, rightLfo;
    OnePole leftHornFilter, rightHornFilter;
    double rate = 48000.0;
    float centreDelay = 384.0f;
    float depth = 120.0f;
};

} // namespace j37dsp::fx
