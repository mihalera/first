// =============================================================================
//  The hot path's scalar helpers: the shaper the magnetic stage calls, the two
//  one-pole coefficient builders, the output soft clipper and the soft-knee gain
//  computer the glue stages use.
//
//  Moved out of PluginProcessor.cpp, prose and all, when the signal path left for
//  Source/ProcessorSignalPath.cpp. They are a header because they are called from
//  BOTH halves of what that move separated - processTapeEngine runs them per sample,
//  and resetSampleRateDependentState() and updateToneCoefficients() build their
//  coefficients with the same two functions - so they could travel with neither.
//
//  This is the last of what tests/dsp/extract.py used to CUT out of
//  PluginProcessor.cpp. The harness includes this header now, which is the same
//  result the stage headers under Source/DSP/ already had, all 23 of them: what the harness
//  measures is the shipping text rather than a copy of it.
//
//  The conventions of the directory apply, and they are stated in J37Math.h: one
//  concern per header, the header includes what it needs, and it does not pull JUCE
//  in - what these name is `juce::jmax`, `juce::jlimit` and `juce::MathConstants`,
//  which the plugin's own JuceHeader.h declares and which the DSP harness's shim
//  declares in its own words. J37_HAS_XSIMD comes from the probe beside it, included
//  here for the reason OptionalLibraries.h gives: a header that reads a macro has to
//  be able to find the file that decides it.
//
//  The five bodies are unchanged, including their comments. The only edit the move
//  made is the four spaces of indentation the .cpp's anonymous namespace gave them,
//  removed because there is no enclosing namespace here.
// =============================================================================
#pragma once

#include <cmath>

#include "OptionalLibraries.h"

/**
    The tanh the magnetic shaper calls.

    This exists as a named function rather than a direct std::tanh call so the
    one place the shaper's cost can be traded for a different one is explicit
    and greppable.

    Default: std::tanh. Exact, and what every existing render was produced
    with.

    With J37_USE_SIMD_TANH defined AND xsimd available: a vectorised rational
    approximation. It is a DIFFERENT function - accurate to roughly 1e-4 over
    the range the shaper uses, which is well below audibility for a curve that
    is already generating harmonics on purpose, but it is not the same curve
    and it will not null against the default. That is exactly why it is behind
    a flag: the choice belongs to a listening test, not to a build default.

    xsimd::tanh is used in its scalar form here. The vector form pays off once
    the tape loop processes a batch of samples at a time; the scalar entry
    point lets that change be made in one place without the shaper having to
    know about it yet.
*/
// maybe_unused, and not deleted: this is the seam the opt-in flag switches
// at, and until J37_USE_SIMD_TANH is turned on nothing calls it, which
// Clang reports as an unused function on every build. The spelling is kept
// exactly as it is because tests/dsp/extract.py matches this line by name.
[[maybe_unused]] inline float j37Tanh (float x) noexcept
{
#if ! defined (J37_DSP_HARNESS) && defined (J37_USE_SIMD_TANH) && J37_HAS_XSIMD
    // OPT-IN only, not a build default. The SIMD form is a different curve from
    // std::tanh at roughly the 1e-4 level, so this is correct only after a listening
    // test / benchmark says the shaper dominates and the difference is acceptable.
    return xsimd::tanh (x);
#else
    return std::tanh (x);
#endif
}

/**
NOTE: the single magneticHysteresis curve that used to live here has been
REMOVED, not merely superseded.

    The engine now runs SaturationCore, whose `shapeTape` branch is this curve
    bit-for-bit - same slope, same memory term, same zero-point correction - so
    keeping a second copy here would be dead code that a reader could easily
    mistake for the live path. That is not a hypothetical risk: the DSP harness
    kept calling this function for a while after the engine stopped, which made
    it report a large regression that was only the test and the plugin
    disagreeing about which curve they were measuring.

    Anything that needs the tape curve now reaches it through SaturationCore,
    which is the one place it exists.
*/

/** One-pole low-pass coefficient for a given time constant in milliseconds. */
inline float onePoleCoefficient (float milliseconds, float sampleRate)
{
    const float seconds = juce::jmax (0.01f, milliseconds) * 0.001f;
    return 1.0f - std::exp (-1.0f / (seconds * juce::jmax (1.0f, sampleRate)));
}

/**
    One-pole low-pass coefficient for a given -3 dB CORNER FREQUENCY in Hz.

    This is the correct unit for musical filters. `onePoleCoefficient()` takes a time
    constant, and those two are easy to confuse: tau = 1/(2*pi*f0), so passing a
    frequency where a time constant is expected (or vice versa) puts the corner off by
    a factor of 2*pi or more. The previous tape low-passes passed millisecond values
    that were written as if they were kilohertz corners - "26.0f" meant 26 kHz but
    landed at a 26 ms tau, i.e. a 6 Hz corner - and the whole wet path came out
    sub-audio, which is what made MIX at 100 % sound like mush instead of tape.
*/
inline float onePoleCoefficientHz (float cornerHz, float sampleRate)
{
    const float omega = juce::MathConstants<float>::twoPi
                      * juce::jmax (1.0f, cornerHz)
                      / juce::jmax (1.0f, sampleRate);
    return juce::jlimit (0.0f, 1.0f, 1.0f - std::exp (-omega));
}

/**
    Soft clipper for the very end of the chain.

    A hard `jlimit (-1, 1)` is the one thing this plugin must not do: it turns any
    overshoot into a flat-topped square edge, which is what "digital clipping" sounds
    like and it is not a tape behaviour. Tape saturates progressively and rolls off,
    so the same treatment is applied at the output.

    The curve is a linear region that blends smoothly into a saturating exponential,
    chosen because it is monotonic, has no jump in value at the knee, and is exactly
    linear for small inputs. That last property matters: below the knee the output is
    bit-for-bit the input, so normal material passes through completely untouched and
    the stage only engages when the signal actually approaches full scale.

    `ceiling` is deliberately just under 1.0 so the output cannot reach full scale even
    when driven hard. That keeps the plugin from hitting the host's own hard limit, and
    it means the resulting distortion is always the gentle kind rather than a wrap.

    Below the knee the response is 1:1. Above it, the output approaches the ceiling
    asymptotically, so no input, however large, can push the result past 0.985.
*/
inline float softClip (float x) noexcept
{
    constexpr float ceiling = 0.985f;

    // Below this the curve is 1:1, so quiet and normal-level material is transparent.
    constexpr float knee = 0.70f;

    const auto magnitude = std::abs (x);
    if (magnitude <= knee)
        return x;

    const auto sign = x < 0.0f ? -1.0f : 1.0f;
    const auto excess = magnitude - knee;

    // The value is continuous at the knee (it equals `knee` there) and the slope only
    // changes gradually, so there is no audible corner where saturation begins. The
    // remaining headroom above the knee is what the exponential curve gets to spend.
    const auto compressed = knee + (1.0f - std::exp (-excess)) * (ceiling - knee);
    return sign * juce::jmin (ceiling, compressed);
}

/**
    Soft-knee gain computer. `envelopeDb` is the detector level in dBFS.
*/
inline float softKneeReductionDb (float envelopeDb, float thresholdDb, float kneeDb, float ratio) noexcept
{
    const float halfKnee = kneeDb * 0.5f;
    const float overshootDb = envelopeDb - thresholdDb;

    if (overshootDb <= -halfKnee)
        return 0.0f;

    if (overshootDb < halfKnee)
    {
        const auto kneeProgress = overshootDb + halfKnee;
        return -(1.0f - 1.0f / ratio) * kneeProgress * kneeProgress / (2.0f * kneeDb);
    }

    return -(1.0f - 1.0f / ratio) * overshootDb;
}
