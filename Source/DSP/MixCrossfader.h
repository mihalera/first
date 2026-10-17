// =============================================================================
//  The MIX crossfade: the dry/wet gain pair and the two legs it is applied to.
//
//  MIX is the one control that does not touch the machine at all - it decides how
//  much of the finished tape signal leaves the plugin and how much of the input
//  goes with it. 0 % is the input and 100 % is all tape, both at unity, and the
//  law between them is the equal-power one: a quarter turn of cos/sin,
//
//      dry = cos (mix * pi/2)        wet = sin (mix * pi/2)
//
//  so dry^2 + wet^2 is 1 for every position and the level is held across the
//  middle of the travel. What that costs is stated rather than hidden, because it
//  is a property of the law rather than a bug to be fixed: on CORRELATED material
//  - the input and the machine's version of it, most of the time - the midpoint
//  is +3 dB, cos(pi/4) + sin(pi/4) = 1.414. A linear law would instead dip about
//  -6 dB on uncorrelated material, which is the fault this shape was chosen to
//  avoid. tests/mix_crossfader pins both readings.
//
//  Both legs carry the TRANSPORT GATE, and that is the second half of the stage.
//  A stopped machine passes nothing, so STOP is silence rather than "the wet part
//  went away and the input is still there" - which is what a stop on a deck does,
//  and what the panel's STOP key promises. At MIX 0 the machine IS the input's
//  path, so STOP there has to be a stop too. The gate is a parameter of the leg
//  rather than something the caller may forget, which is why this is a type
//  instead of two expressions:
//
//      tapeOutput = dryLeg (dryInput, gains, platterGate)
//                 + wetLeg (wetInput, gains, platterGate)
//
//  and the same `gains.wet` scales the SUBFUND join, so MIX 0 takes the undertones
//  with it. The gate's own SHAPE - a curve over the platter rather than the speed
//  itself - is the transport's business and tests/transport reads it there.
//
//  The header is JUCE-free: std::cos, std::sin and one constant, so
//  tests/mix_crossfader compiles it as it ships.
// =============================================================================
#pragma once

#include <cmath>

namespace j37dsp
{

/**
    The equal-power MIX crossfade, and the two legs the engine sums.

    Everything is static: the stage holds no state of its own. The ramp the user
    hears is the block-rate smoother that feeds `gainsFor` one position per sample
    - this header is the LAW those positions are turned into gains by.
*/
class MixCrossfader
{
public:
    // A quarter turn, and the same float juce::MathConstants<float>::halfPi is -
    // tests/dsp/check_headers_juce.sh static_asserts that, because a constant that
    // is one ULP off would move every rendered sample of every existing session by
    // that much.
    static constexpr float halfPi = 3.14159265358979323846f * 0.5f;

    /** The dry/wet gain pair for one MIX position. */
    struct Gains
    {
        float dry = 1.0f;
        float wet = 0.0f;
    };

    /** `juce::jlimit (0.0f, 1.0f, mix)` exactly, NaN included: a NaN position is
        not a position, and jlimit's jmax(0, jmin(1, v)) answers 0 where
        std::clamp would pass the NaN through to cos and sin. */
    static float clampMix (float rawMix) noexcept
    {
        const auto capped = 1.0f < rawMix ? 1.0f : rawMix;
        return 0.0f < capped ? capped : 0.0f;
    }

    /** The equal-power gain pair. At 0 % this is exactly (1, 0); at 100 % the wet
        gain is exactly 1 and the dry gain is cos(pi/2) in float - about -4.4e-8,
        which is the one sample-level rounding the law has and not a leak to fix. */
    static Gains gainsFor (float mix) noexcept
    {
        const auto angle = clampMix (mix) * halfPi;
        return { std::cos (angle), std::sin (angle) };
    }

    /** The machine's leg of the crossfade, gated. */
    static float dryLeg (float dryInput, Gains gains, float platterGate) noexcept
    {
        return dryInput * gains.dry * platterGate;
    }

    /** The finished tape signal's leg of the crossfade, gated. */
    static float wetLeg (float wetInput, Gains gains, float platterGate) noexcept
    {
        return wetInput * gains.wet * platterGate;
    }
};

} // namespace j37dsp
