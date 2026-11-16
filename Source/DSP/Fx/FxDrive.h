// =============================================================================
//  The drive family: overdrive, fuzz, the wavefolder, the two crushers and the
//  ring modulator.
//
//  These are the stages that add harmonics, so the level they are driven at is
//  part of their definition: each one states its own gain law and its own
//  compensation, and the compensation is deliberately not "make it the same
//  level" - a fuzz that returns to unity is a fuzz that does nothing.
//
//  The antialiased tanh in FxPrimitives.h is what the two amplifier curves are
//  built on, and it runs at the ENGINE rate, which may be up to 8x the session's
//  (see docs/signal-path.md): the rack sits inside the oversampled region, so a
//  shaper here is already getting the bandwidth it needs before ADAA is asked to
//  do anything.
// =============================================================================
#pragma once

#include "FxPrimitives.h"

#include <algorithm>
#include <cmath>

namespace j37dsp::fx
{

// =============================================================================
//  Overdrive: a symmetric soft clip with the drive and the make-up tied
//  together, so the knob changes the amount of break-up rather than the volume.
// =============================================================================
class Overdrive
{
public:
    void prepare (double) noexcept { reset(); }

    void reset() noexcept
    {
        leftShaper.reset();
        rightShaper.reset();
    }

    void process (float& left, float& right, float amount) noexcept
    {
        const auto drive = clamp01 (amount);
        const auto preGain = 1.0f + 24.0f * drive;
        const auto postGain = 1.0f / std::sqrt (preGain);

        left = leftShaper.process (left * preGain) * postGain;
        right = rightShaper.process (right * preGain) * postGain;
    }

private:
    AdaaTanh leftShaper, rightShaper;
};

// =============================================================================
//  Fuzz: far more gain than the overdrive, and asymmetric, which is where the
//  octave-ish edge comes from. The bias is subtracted back out so the stage does
//  not put a DC offset on the rest of the chain.
// =============================================================================
class Fuzz
{
public:
    void prepare (double sampleRate) noexcept
    {
        dcLeft.setCutoff (18.0f, sampleRate);
        dcRight.setCutoff (18.0f, sampleRate);
        reset();
    }

    void reset() noexcept
    {
        leftShaper.reset();
        rightShaper.reset();
        dcLeft.reset();
        dcRight.reset();
    }

    void process (float& left, float& right, float amount) noexcept
    {
        const auto drive = clamp01 (amount);
        const auto preGain = 1.0f + 60.0f * drive;

        // The bias lives in the SHAPER's domain, a fraction of the tanh knee, not
        // in the input's. Scaling it by preGain - which is what this did - put
        // 0.12 * 61 = 7.3 under every sample at full amount, so the positive half
        // of the waveform arrived already past +1 and the stage output was a
        // half-wave rectifier: the positive peak measured 0.000, the RMS fell 6 dB
        // below the input, and what came out was a thin octave-up buzz rather than
        // a fuzz. Asymmetry is the point of this curve; losing half the signal is
        // not.
        const auto bias = 0.35f * drive;
        const auto offset = std::tanh (bias);

        // Saturation compresses, so it has to come back up: 1/sqrt(preGain) alone
        // left the stage 8 dB quieter than its input at full amount, which reads as
        // "the knob mutes it".
        const auto postGain = (1.0f + 2.0f * drive) / std::sqrt (preGain);

        // Subtracting the bias's REST value is not the same as removing the mean of
        // the shaped waveform: an asymmetric curve has a mean of its own, and the
        // difference is real DC on the bus - 0.126 at full amount, measured. The
        // high-pass is what takes it out, the same way the wavefolder does.
        left = dcLeft.highPass ((leftShaper.process (left * preGain + bias) - offset) * postGain);
        right = dcRight.highPass ((rightShaper.process (right * preGain + bias) - offset) * postGain);
    }

private:
    AdaaTanh leftShaper, rightShaper;
    OnePole dcLeft, dcRight;
};

// =============================================================================
//  Wavefolder: sin(x) as a transfer curve. Past a quarter turn the curve turns
//  back on itself, which is the fold: the harmonics it adds are not the odd
//  series a clip produces, which is why it is a different sound and not a
//  stronger overdrive.
// =============================================================================
class Wavefolder
{
public:
    void prepare (double sampleRate) noexcept
    {
        dcLeft.setCutoff (18.0f, sampleRate);
        dcRight.setCutoff (18.0f, sampleRate);
        reset();
    }

    void reset() noexcept { dcLeft.reset(); dcRight.reset(); }

    void process (float& left, float& right, float amount) noexcept
    {
        const auto drive = 1.0f + 9.0f * clamp01 (amount);
        const auto postGain = 1.0f / std::sqrt (drive);

        left = dcLeft.highPass (foldSample (left * drive) * postGain);
        right = dcRight.highPass (foldSample (right * drive) * postGain);
    }

private:
    OnePole dcLeft, dcRight;
};

// =============================================================================
//  Bitcrusher: quantisation to a coarser grid. 16 bits at the bottom of the
//  knob is transparent enough to leave in the path; 4 bits at the top is the
//  effect.
//
//  The grid is measured against the programme's own peak, not against full scale.
//  A full-scale grid is level dependent in the worst possible way: 2 bits there is
//  a step of 0.5, so every sample quieter than -12 dBFS rounded to zero and the
//  stage MUTED real material while a -6 dBFS test tone survived - which is exactly
//  how this knob was reported ("BITS 1, no sound"). Four bits of a peak-normalised
//  signal, by contrast, is the crunch the knob promises whatever the level the
//  machine hands it.
//
//  The normalisation is LATCHED per block, and that is the whole trick: a grid
//  that follows the signal smoothly quantises nothing at all, because every sample
//  is rounded against a slightly different scale and the steps never line up. Held
//  for a couple of milliseconds, the grid is real inside the block and moves only
//  as fast as the programme's own level.
// =============================================================================
class Bitcrusher
{
public:
    void prepare (double sampleRate) noexcept
    {
        const auto rate = sampleRate > 0.0 ? sampleRate : 48000.0;

        // ~2.7 ms at 48 kHz, and held in samples rather than milliseconds so the
        // grid is the same shape at every engine rate the path runs at.
        blockLength = std::max (16, static_cast<int> (std::lround (rate * 0.0027)));
        reset();
    }

    void reset() noexcept
    {
        peak = 0.0f;
        blockPeak = 0.0f;
        counter = 0;
    }

    void process (float& left, float& right, float amount) noexcept
    {
        const auto depth = clamp01 (amount);
        const auto bits = 16.0f - 12.0f * depth;
        const auto levels = std::pow (2.0f, bits - 1.0f);
        const auto inverse = 1.0f / levels;

        if (counter <= 0)
        {
            // A held peak that decays by 10% per block (~38 dB/s), so a passage
            // that gets quieter gets a finer grid, and one that stops leaves the
            // grid where it was instead of crawling down onto the noise floor.
            peak = std::max (blockPeak, peak * 0.9f);
            blockPeak = 0.0f;
            counter = blockLength;
        }

        --counter;
        blockPeak = std::max (blockPeak, std::max (std::abs (left), std::abs (right)));

        // The floor keeps a silent passage from dividing by its own zero; it is
        // -80 dBFS, so it quantises nothing anyone can hear.
        const auto scale = std::max (peak, 1.0e-4f);

        left = std::round (left / scale * levels) * inverse * scale;
        right = std::round (right / scale * levels) * inverse * scale;
    }

private:
    int blockLength = 128;
    int counter = 0;
    float peak = 0.0f;
    float blockPeak = 0.0f;
};

// =============================================================================
//  Rate crusher: sample-and-hold at a fraction of the engine rate. This is the
//  alias-generating half of a "bitcrush" and the two are separate stages on
//  purpose - a hold and a quantise together are one sound, but they are two
//  different controls and an engineer reaches for them separately.
// =============================================================================
class RateCrusher
{
public:
    void prepare (double) noexcept { reset(); }

    void reset() noexcept
    {
        counter = 0;
        heldLeft = heldRight = 0.0f;
    }

    void process (float& left, float& right, float amount) noexcept
    {
        const auto factor = 1.0f + 40.0f * clamp01 (amount);
        hold = std::max (1, static_cast<int> (std::lround (factor)));

        if (counter == 0)
        {
            heldLeft = left;
            heldRight = right;
        }

        left = heldLeft;
        right = heldRight;

        if (++counter >= hold)
            counter = 0;
    }

private:
    int hold = 1;
    int counter = 0;
    float heldLeft = 0.0f, heldRight = 0.0f;
};

// =============================================================================
//  Ring modulator: the signal times a carrier. The carrier's frequency is
//  mapped from the amount (30 Hz to a little under a kilohertz), and the amount
//  also sets how much of the product reaches the output, so the stage fades in
//  as a tremolo and arrives as a ring mod.
// =============================================================================
class RingModulator
{
public:
    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        reset();
    }

    void reset() noexcept { phase = 0.0f; }

    void process (float& left, float& right, float amount) noexcept
    {
        const auto mix = clamp01 (amount);
        const auto hertz = 30.0f * std::pow (2.0f, 5.0f * mix);
        const auto increment = hertz / static_cast<float> (rate);

        const auto carrier = std::sin (twoPi * phase);
        phase += increment;

        if (phase >= 1.0f)
            phase -= 1.0f;

        const auto dryLeft = left;
        const auto dryRight = right;

        left = dryLeft + (dryLeft * carrier - dryLeft) * mix;
        right = dryRight + (dryRight * carrier - dryRight) * mix;
    }

private:
    double rate = 48000.0;
    float phase = 0.0f;
};

} // namespace j37dsp::fx
