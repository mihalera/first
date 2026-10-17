// =============================================================================
//  The final gain compensation: the one gain stage in the chain nothing measured.
//
//  The tape stage, the two glue compressors and their makeups all change the
//  level, so what leaves the plugin is not necessarily the level the user dialled
//  in with INPUT. This compares the finished signal against a reference taken
//  straight after the input trim and before the first compressor, and pays back
//  whatever was lost or gained - so the plugin holds its output level as DRIVE,
//  TAPE TYPE, SPEED and MIX are changed instead of drifting louder or quieter
//  with every edit.
//
//  The correction is smoothed with a slow one-pole (450 ms, chosen by the caller),
//  so it tracks the programme level rather than fighting the moment-to-moment
//  compression. What that means in practice, and what tests/gain pins:
//
//    * **It only ever restores.** A signal already at or above the reference
//      leaves the correction at unity - this is a compensator, not an extra
//      boost stage - and it never asks for more than 12 dB.
//    * **Silence is not a measurement.** A frame below the power floor returns
//      unity gain and leaves the smoothed state exactly where it was, so a pause
//      cannot drive the correction to its limit; the same rule gates a stopped
//      platter, which the caller applies by not calling this at all.
//    * **Idle is bit-exact.** With nothing lost the smoothing starts and stays at
//      0 dB, and `getGain()` is exactly 1.0f, so a mix with no level change is
//      not moved by a stage that spends its life asleep.
//
//  It is a header rather than a block inside processTapeEngine() for the reason
//  the plan gives for item 4: it is a gain applied to every sample and nothing
//  measured it. It includes nothing of JUCE, so tests/gain compiles this file as
//  it ships.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>

#include "OptionalLibraries.h"

// The chowdsp availability macro is decided by OptionalLibraries.h, which also
// includes the library itself when the build has it. Undefined means 0, which is
// what makes a harness measure the std:: branch.
#if ! defined (J37_HAS_CHOWDSP_MATH)
    #define J37_HAS_CHOWDSP_MATH 0
#endif

namespace j37dsp
{

/**
    The programme-level gain compensator that follows the output glue stage.

    One instance per machine, driven once per frame with two POWER values: the
    reference taken after the input trim, and the power the chain produced before
    this compensation is folded in. The second value must be measured BEFORE the
    gain is applied - measuring the compensated output would make the correction
    chase its own tail.
*/
class AutoGainCompensator
{
public:
    /** The most this stage will ever restore, in dB. A programme that lost more
        than this is a setting, not a level error, and should be heard. */
    static constexpr float maximumRestorationDb = 12.0f;

    /** Below this frame power there is no measurement to make. */
    static constexpr float minimumOutputPower = 1.0e-12f;

    /** Back to neutral, so a rate change or a fresh session does not start with a
        correction left over from the previous one. */
    void reset() noexcept { smoothedDb = 0.0f; }

    /** The smoothed correction, in dB. Zero means the stage is doing nothing. */
    float getSmoothedDb() const noexcept { return smoothedDb; }

    /** The gain that correction represents. Exactly 1.0f at rest. */
    float getGain() const noexcept { return decibelsToGain (smoothedDb); }

    /**
        Advances the compensator by one frame and returns the gain for it.

        `referencePower` is the post-input-trim power, `outputPower` the power the
        chain produced before this stage, and `coefficient` the one-pole
        coefficient the caller built for the engine's rate. A frame that cannot be
        measured - a silent output, or a block whose reference was never filled -
        returns unity and holds the state.
    */
    float process (float referencePower, float outputPower, float coefficient) noexcept
    {
        if (referencePower <= 0.0f || outputPower <= minimumOutputPower)
            return 1.0f;

        // How far the finished signal has fallen below the reference taken after
        // the input trim. Positive means level was lost and needs paying back.
        const auto levelRatioDb = gainToDecibels (std::sqrt (referencePower / outputPower));

        // Only ever restore, never exaggerate: the correction may recover a loss
        // but must not become an extra boost stage of its own.
        const auto targetDb = std::clamp (levelRatioDb, 0.0f, maximumRestorationDb);

        smoothedDb += (targetDb - smoothedDb) * coefficient;

        return decibelsToGain (smoothedDb);
    }

private:
    // The same pair the signal path used inline, so a build with chowdsp keeps the
    // polynomial readings it was producing and a build without it keeps the std::
    // ones. The ratio's floor of 0 dB is stated as part of the conversion because
    // it is what makes "never attenuate" true a step before the clamp above.
    static float gainToDecibels (float gain) noexcept
    {
#if J37_HAS_CHOWDSP_MATH
        return chowdsp::DecibelsApprox::gainToDecibels (gain, 0.0f);
#else
        return 20.0f * std::log10 (gain);
#endif
    }

    static float decibelsToGain (float decibels) noexcept
    {
        return std::pow (10.0f, decibels * 0.05f);
    }

    float smoothedDb = 0.0f;
};

} // namespace j37dsp
