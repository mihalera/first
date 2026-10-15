// =============================================================================
//  The scalar math the DSP blocks call.
//
//  Routed through chowdsp's polynomial approximations when the library is in the
//  build, and through std:: when it is not. This is the first piece of the engine
//  to move out of PluginProcessor.h, so the convention the rest of Source/DSP/
//  follows is stated once here:
//
//    - One concern per header, and the header includes what it needs. The stages
//      are measured by the harnesses under tests/, which compile the shipping text
//      against a small stand-in for JUCE - so a header must not, itself, pull in
//      JUCE. What it may rely on is the handful of juce:: helpers its includer has
//      already declared: inside the plugin that is PluginProcessor.h (JuceHeader.h),
//      and under tests/*/run.sh it is the harness's shim.h.
//    - The chowdsp availability macro is decided by PluginProcessor.h, which owns
//      the conditional include of the library itself. Undefined means 0, which is
//      what makes the harnesses measure the std:: branch below - the same numbers
//      the shipping build produced before the bridge existed.
// =============================================================================
#pragma once

#include <cmath>

#if ! defined (J37_HAS_CHOWDSP_MATH)
    #define J37_HAS_CHOWDSP_MATH 0
#endif

//==============================================================================
/**
    The scalar math the DSP blocks call, routed through chowdsp's polynomial
    approximations when they are available.

    These exist as named functions rather than direct std::exp / std::sqrt calls
    for one reason: the hot paths below are PER SAMPLE, and the two glue
    detectors plus the two loudness meters call them millions of times a second
    at 8x oversampling. Putting the swap in one place per operation keeps the
    decision greppable and keeps the fallback honest - every function here is
    the exact std:: implementation when chowdsp is not in the build, so the DSP
    regression harness (which never sees chowdsp) measures the same numbers the
    shipping build produced before this change.

    What the approximations cost: chowdsp's polynomial exp and log are accurate to
    roughly 1e-4 over the ranges a one-pole coefficient and a dB readout need, which
    is far below the threshold where a 20 ms ramp's shape or a meter digit is
    audible. Neither is a change to the sound; both are a change to how much of the
    CPU the meters and detectors consume.

    On sqrt: chowdsp has no scalar sqrt approximation - only Math::rsqrt, which is
    a reciprocal and would need a division to undo. There is no version of that
    trade worth making here, so sqrt stays std::sqrt. It is listed in the bridge
    anyway so every scalar op the hot paths use has one place to look.
*/
#if J37_HAS_CHOWDSP_MATH
namespace j37math
{
    inline float exp (float x) noexcept   { return chowdsp::PowApprox::exp (x); }
    inline float sqrt (float x) noexcept  { return std::sqrt (x); }
    inline float log10 (float x) noexcept { return chowdsp::LogApprox::log10 (x); }
}
#else
namespace j37math
{
    inline float exp (float x) noexcept   { return std::exp (x); }
    inline float sqrt (float x) noexcept  { return std::sqrt (x); }
    inline float log10 (float x) noexcept { return std::log10 (x); }
}
#endif
