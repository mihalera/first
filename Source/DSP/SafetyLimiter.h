// =============================================================================
//  The program-dependent safety limiter at the end of the output chain.
//
//  This exists so the soft clipper downstream stays idle. It watches the loudest
//  channel of the frame and pulls the gain back whenever the signal is heading for
//  the ceiling, then releases slowly enough to stay musical.
//
//  It is a GAIN stage rather than a shaper, so it adds no harmonics of its own:
//  the only distortion in the output is the tape stage's, which is intentional.
//  Normal material is limited transparently - at unity, to the bit - and only
//  genuinely excessive level ever reaches the clipper.
//
//  Its shape is four poles and a ceiling, and the asymmetry between them is the
//  whole behaviour (tests/limiter pins each one):
//
//    * the DETECTOR is a fast peak follower - 0.5 ms up, 80 ms down - so it sees a
//      transient arrive almost at once and lets go only after the programme has;
//    * the GAIN it asks for is smoothed separately, 0.4 ms to catch and 120 ms to
//      recover, so the correction is continuous and cannot click. Catch faster
//      than release is what makes this a limiter rather than a compressor that
//      pumps;
//    * the CEILING sits exactly on softClip's knee (0.70). That is the only value
//      that makes the hand-off claim true: below the knee the clipper is
//      bit-for-bit transparent, so anything the limiter lets through is distortion
//      the clipper would not have caused yet.
//
//  The two asymmetries are also why the coefficients are built once per BLOCK, in
//  Source/DSP/BlockRateCoefficients.h, from the durations named here: rebuilding
//  them inside the per-sample loop cost four std::exp per sample per channel, and
//  the gain smoothing's ternary evaluated both std::exp branches and threw one
//  away. This header is JUCE-free and includes only <algorithm>/<cmath>, so
//  tests/limiter compiles it as it ships.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>

namespace j37dsp
{

/**
    The output safety limiter: one peak detector, one smoothed gain, one ceiling.

    Driven once per FRAME with the frame's loudest absolute sample. The caller
    takes the maximum across the channels so both sides get the same gain - a
    limiter that limited each channel on its own would shift the stereo image
    towards whichever side was quieter at that instant.
*/
class SafetyLimiter
{
public:
    // The four durations, in milliseconds. They live here because they ARE the
    // behaviour, and Source/DSP/BlockRateCoefficients.h builds its four
    // coefficients from these names rather than repeating the numbers - so a
    // change to one cannot leave the other behind.
    static constexpr float detectorAttackMs = 0.5f;
    static constexpr float detectorReleaseMs = 80.0f;
    static constexpr float catchMs = 0.4f;
    static constexpr float recoveryMs = 120.0f;

    /** The level the limiter pulls back to, which is softClip's knee. */
    static constexpr float ceiling = 0.70f;

    /** The block-rate coefficients, built by the caller for the engine's rate. */
    struct Coefficients
    {
        float detectorAttack = 0.0f;
        float detectorRelease = 0.0f;
        float catchCoefficient = 0.0f;
        float recoveryCoefficient = 0.0f;
        float ceiling = SafetyLimiter::ceiling;
    };

    void prepare (const Coefficients& toUse) noexcept { coefficients = toUse; }

    /** Open, with no detector history: a stale gain from the previous session must
        not duck the first block audibly. */
    void reset() noexcept
    {
        detector = 0.0f;
        gain = 1.0f;
    }

    float getGain() const noexcept { return gain; }
    float getDetector() const noexcept { return detector; }
    float getCeiling() const noexcept { return coefficients.ceiling; }

    /**
        Advances one frame and returns the gain to apply to it.

        `framePeak` is the loudest absolute sample of the frame, across channels.
        Below the ceiling the detector still runs - it is the stage's memory, not
        a gate - but the gain it asks for is exactly 1, so a signal under the
        ceiling is multiplied by 1.0f and comes back bit for bit.
    */
    float process (float framePeak) noexcept
    {
        // A very fast attack catches transients before they overshoot; the release
        // is short enough to recover between events without pumping on sustained
        // material.
        const auto detectorCoefficient = framePeak > detector ? coefficients.detectorAttack
                                                               : coefficients.detectorRelease;
        detector += (framePeak - detector) * detectorCoefficient;

        // Gain required to bring the peak back to the ceiling. It is smoothed
        // separately from the detector so the correction is continuous and cannot
        // click.
        const auto requiredGain = detector > coefficients.ceiling
                                    ? coefficients.ceiling / detector
                                    : 1.0f;

        const auto gainSmoothing = requiredGain < gain ? coefficients.catchCoefficient
                                                       : coefficients.recoveryCoefficient;
        gain += (requiredGain - gain) * gainSmoothing;

        return gain;
    }

private:
    Coefficients coefficients;
    float detector = 0.0f;
    float gain = 1.0f;
};

} // namespace j37dsp
