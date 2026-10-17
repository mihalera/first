// =============================================================================
//  The machine's block-rate coefficients: the cabinet's two poles, the SAG
//  envelope's pair, the safety limiter's four and its ceiling, and the anti-phase
//  guard's own pole.
//
//  This is the first stage taken out of processTapeEngine() - a function of 3,367
//  lines, see the note on step F in SPLITTING-BIG-FILES.md - and it is the stage that
//  function's own comments point at twice: rebuilding the SAG pair inside the loop was
//  "two std::exp per sample per channel" and rebuilding the limiter's four was "four
//  std::exp per sample per channel". Every number here is a function of the sample
//  rate and of CABINET alone, so it is rebuilt once per BLOCK, and the header exists
//  to make that a property of the code rather than a fact about where the lines
//  happened to sit in a loop's preamble.
//
//  The conventions of the directory apply, and they are stated in J37Math.h: one
//  concern per header, the header includes what it needs, and it does not pull JUCE
//  in. What this names is the two coefficient builders in HotPathMath.h, so tests/dsp
//  compiles the shipping stage against its shim rather than a copy of it - and, since
//  the relationships between these numbers ARE the behaviour (a supply that droops
//  faster than it recovers, a limiter that catches faster than it releases, a guard
//  slower than either), the harness checks them here instead of leaving them to the
//  comments below to promise.
//
//  The prose came with the code, out of PluginProcessor.cpp and then out of
//  Source/ProcessorSignalPath.cpp by matching its exact text: what changed is the name
//  each number is written into (a field of the struct rather than one of the
//  processor's own members), and nothing else.
// =============================================================================
#pragma once

#include "DSP/HotPathMath.h"
#include "DSP/SafetyLimiter.h"

namespace j37rate
{

/** The time constants and the one level the machine's own dynamics stages run on,
    rebuilt once per block. */
struct BlockRateCoefficients
{
    // The cabinet's poles. The roll-off sits where a 12-inch speaker's cone mass
    // puts it (about 5 kHz at the default cabinet setting, opening toward 9 kHz as
    // CABINET is turned down) and the resonance tracks the cabinet's own tuning,
    // which is the 80-120 Hz thump a closed box adds. The resonance pole is
    // deliberately much slower than the roll-off, so the peak it adds is a broad
    // lift rather than a narrow ring. Presence is a high-pass split at 2.2 kHz:
    // everything above it is the band the feedback network lifts.
    float cabinetLow = 0.0f;
    float cabinetPeak = 0.0f;
    float presence = 0.0f;

    // The SAG envelope's two coefficients. Functions of the rate alone, so they are
    // block-rate constants like every other coefficient here - see the per-sample
    // loop, where rebuilding them was two std::exp per sample per channel.
    // 60 ms attack (the supply takes time to droop) and 240 ms release (it takes
    // longer to recover), which is the asymmetry that makes sag audible as "give"
    // rather than as a slow compressor.
    float sagAttack = 0.0f;
    float sagRelease = 0.0f;

    // The safety limiter's four coefficients and its ceiling. All functions of the
    // rate alone - see the per-sample loop, where rebuilding them was four std::exp
    // per sample per channel - and the four durations they are built from are named
    // in Source/DSP/SafetyLimiter.h, which is the stage that reads them.
    float limiterDetectorAttack = 0.0f;
    float limiterDetectorRelease = 0.0f;
    float limiterCatch = 0.0f;
    float limiterRecovery = 0.0f;

    // The ceiling is a level, not a time constant, so it does not belong with the
    // four one-pole coefficients above - but it is set here so the hand-off point
    // and the coefficients that act on it are read from one place. It mirrors
    // softClip's knee, which is the only value that makes this stage's own comment
    // true, so that knee and this number have to move together: softClip declares
    // its knee as a function-local constexpr, so a shared constant would have to
    // be lifted out of a function the DSP harness cuts up on its own.
    float limiterCeiling = 0.70f;

    // -------------------------------------------------------------------------
    //  Anti-phase guard coefficients.
    //
    //  Two windows, and the asymmetry between them is the whole behaviour:
    //
    //    - `antiPhase` is DELIBERATELY SLOW (about 400 ms). The guard
    //      must never react to a moment of genuine stereo - a hard-panned
    //      transient, a wide reverb tail - because those are real programme, not
    //      a fault. Only a SUSTAINED opposition is a fault, and 400 ms is long
    //      enough that a transient cannot trigger it and short enough that a
    //      genuinely inverted channel is corrected within a bar.
    //
    //    - the correction itself is applied through the same pole, so the rotation
    //      glides in and out rather than switching. A polarity flip that snapped
    //      would be a click, which is exactly the fault the guard exists to
    //      prevent.
    //
    // 400 ms, stated as a duration rather than as a reciprocal.
    // -------------------------------------------------------------------------
    float antiPhase = 0.0f;
};

/** Builds the block's coefficients from the rate and the two settings they are
    functions of: the engine's rate and CABINET, which is the only control that moves
    a pole rather than a gain. */
inline BlockRateCoefficients makeBlockRateCoefficients (float engineSampleRate,
                                                       float cabinetAmount)
{
    BlockRateCoefficients coefficients;

    const float cabinetHz = 9000.0f - cabinetAmount * 4000.0f;
    coefficients.cabinetLow = onePoleCoefficientHz (cabinetHz, engineSampleRate);
    coefficients.cabinetPeak = onePoleCoefficientHz (110.0f, engineSampleRate);
    coefficients.presence = onePoleCoefficientHz (2200.0f, engineSampleRate);

    coefficients.sagAttack = onePoleCoefficient (60.0f, engineSampleRate);
    coefficients.sagRelease = onePoleCoefficient (240.0f, engineSampleRate);

    // From the stage's own names, so the four durations have one definition and
    // tests/limiter can describe the shape from the same numbers.
    using Limiter = j37dsp::SafetyLimiter;
    coefficients.limiterDetectorAttack = onePoleCoefficient (Limiter::detectorAttackMs,
                                                            engineSampleRate);
    coefficients.limiterDetectorRelease = onePoleCoefficient (Limiter::detectorReleaseMs,
                                                             engineSampleRate);
    coefficients.limiterCatch = onePoleCoefficient (Limiter::catchMs, engineSampleRate);
    coefficients.limiterRecovery = onePoleCoefficient (Limiter::recoveryMs, engineSampleRate);

    coefficients.antiPhase = onePoleCoefficient (400.0f, engineSampleRate);

    return coefficients;
}

} // namespace j37rate
