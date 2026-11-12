#pragma once

/*
    The tape stage's drive arithmetic, as one named unit.

    The saturation CURVE itself is Source/DSP/SaturationCore.h - seven weighted
    shapes with their own voices, sag envelope and amp voicing. What stayed inline
    in the engine was everything that decides how HARD the curve is asked to work:
    the drive the record head sees, the magnetic curve's own two arguments, the
    static trim that balances the curve's gain, and the supply droop.

    Those were six floating-point literals spread across a 1,400-line function,
    each with a paragraph of reasoning above it and none of them reachable from a
    test. They are here now, named, with the reasoning, and the engine calls them.
    The arithmetic is the shipping arithmetic, term for term and in the same order:
    this is an audio path, so the numbers are moved, not retuned.

    Everything is a pure function of the block's smoothed control values. No state,
    no rate, no allocation - which is what makes it callable both from the hot loop
    and from tests/transport.
*/

#include <cmath>

namespace j37dsp
{

/** DRIVE's own gain into the record head, per unit of the DRIVE control.

    DRIVE has no floor: at zero the machine is unity gain into the head, so it is
    clean until the control asks it not to be. The old 0.28 floor pushed the signal
    38 % harder at every setting, and was a large part of why the plugin sounded
    overdriven with the control down.
*/
inline constexpr float kTapeDrivePerCurve = 1.9f;

/** How much the compressors' own squeeze adds to the drive.

    The two glue stages are part of the machine, so how hard THEY work changes how
    hard the tape saturates: pushing an already-compressed signal into a real record
    head makes it saturate sooner and bloom harder. 1.15 is the weight that makes a
    slammed programme audibly hotter without turning the stage into a distortion
    pedal on a quiet one.
*/
inline constexpr float kTapeDrivePerSqueeze = 1.15f;

/** Pre-emphasis ahead of the head: how much of the drive reaches the input. */
inline constexpr float kTapePreEmphasis = 1.2f;

/** The magnetic curve's share of the hysteresis term.

    BIAS shifts the whole transfer curve, so it has to reach the shaper's drive as
    well as its asymmetry; 0.25 is the weight at which moving BIAS thickens the
    curve without narrowing its headroom to nothing.
*/
inline constexpr float kTapeHysteresisDrive = 0.25f;

/** The magnetic curve's share of the compressor coupling.

    Smaller than the head's own share (kTapeDrivePerSqueeze) on purpose: dense
    programme thickens the curve, but the curve is a SHAPE and the level is the
    head's business - pushing both by the same amount doubled the effect into a
    pumping artefact.
*/
inline constexpr float kTapeShaperSqueezeDrive = 0.30f;

/** The static trim that removes the gain the curve adds at a nominal level.

    The shaper is near-unity at zero drive and only compresses when pushed, so the
    trim has to be close to unity too or the plugin ends up quiet instead of clean.
    The level actually lost inside the shaper is tracked per sample by the engine's
    driveCompensation; this is only the fixed calibration, and the two do not fight.
*/
inline constexpr float kTapeDriveCompensation = 0.16f;

/** How far the supply droops at full SAG, as a fraction of the drive. */
inline constexpr float kTapeSagDepth = 0.35f;

/** How much envelope counts as full demand, so sag saturates rather than ramps. */
inline constexpr float kTapeSagReach = 3.0f;

/** The squeeze the compressors are applying, in dB, floored at zero and capped.

    The cap is 12 dB because past that the curve is already fully engaged and more
    drive only costs headroom; the sum is what both glue stages took off, which is
    the programme's density rather than any single transient.
*/
inline constexpr float kTapeSqueezeCeilingDb = 12.0f;

/** How fast the squeeze term turns gain reduction into drive: 0.20 per dB, so the
    mapping is smooth and monotone instead of a threshold that switches on. */
inline constexpr float kTapeSqueezePerDb = 0.20f;

/** The drive the record head is run at, from the DRIVE control and the squeeze.

    At DRIVE 0 and no squeeze this is exactly 0, which is what makes the machine
    clean at rest.
*/
inline float tapeDriveAmount (float driveCurve, float squeezeDrive) noexcept
{
    return driveCurve * kTapeDrivePerCurve + squeezeDrive * kTapeDrivePerSqueeze;
}

/** The magnetic curve's drive argument: tape character, hysteresis and squeeze. */
inline float tapeShaperDrive (float driveCurve, float tapeCurve, float hysteresis,
                              float squeezeDrive) noexcept
{
    return driveCurve * tapeCurve + hysteresis * kTapeHysteresisDrive
           + squeezeDrive * kTapeShaperSqueezeDrive;
}

/** The static output trim that compensates the curve's nominal gain. */
inline float tapeDriveGainCompensation (float driveCurve) noexcept
{
    return 1.0f - driveCurve * kTapeDriveCompensation;
}

/** The signal arriving at the curve: pre-emphasis scaled by the machine's bias. */
inline float tapePreDrive (float sample, float driveAmount, float speedBias,
                           float preDriveGain) noexcept
{
    return sample * (1.0f + driveAmount * kTapePreEmphasis * speedBias * preDriveGain);
}

/** The supply's droop, from the sag envelope the engine keeps per channel.

    SAG acts on the DRIVE rather than on the output, because that is what a supply
    does: it is the gain that droops under sustained demand. The envelope is the
    signal's own power, so a held chord gives and then recovers while a transient
    never moves it.
*/
inline float tapeSagGain (float sagAmount, float sagEnvelope) noexcept
{
    const float reach = sagEnvelope * kTapeSagReach;
    const float demand = reach < 0.0f ? 0.0f : (reach > 1.0f ? 1.0f : reach);

    return 1.0f - sagAmount * kTapeSagDepth * demand;
}

/** The gain reduction the two glue stages took off this block, as a positive dB
    count the squeeze term reads. A block that gained instead of reducing reads 0. */
inline float tapeSqueezeDb (float inputReductionDb, float reductionDb) noexcept
{
    const float db = -(inputReductionDb + reductionDb);

    return db < 0.0f ? 0.0f : (db > kTapeSqueezeCeilingDb ? kTapeSqueezeCeilingDb : db);
}

/** The squeeze drive a given gain reduction asks for, 0 ... 1. */
inline float tapeSqueezeTarget (float squeezeDb) noexcept
{
    return 1.0f - std::exp (-squeezeDb * kTapeSqueezePerDb);
}

} // namespace j37dsp
