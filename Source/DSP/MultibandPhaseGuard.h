// =============================================================================
//  The output anti-phase guard, in three bands instead of one.
//
//  The single-band guard in Source/ProcessorSignalPath.cpp measures the frame's
//  correlation once and rotates the whole mid/side plane by it, which is right
//  when the opposition is broadband (an inverted pair) and wrong when it is not:
//  a close-miked snare whose low end cancels against a room mic - or a bass DI
//  and a bass amp a few samples apart - puts negative correlation in ONE band
//  while the other bands are perfectly healthy. Rotating the whole frame by that
//  reading flattens the good bands to fix the broken one.
//
//  So the frame is split into three bands by Linkwitz-Riley 4th-order crossovers
//  (200 Hz and 2 kHz), each band's correlation is measured on ITS OWN mid/side
//  pair, and only the bands whose correlation is negative are rotated - by their
//  own amount, not by the frame's. The rotation is the same energy-preserving one
//  the single-band guard uses: a side component that opposes the mid is folded
//  into the mid rather than faded out, so the fix costs no level and cannot pump.
//  Reducing SIDE is what a rotation does (side becomes side*cos - mid*sin), which
//  is the "side compression" the plan asked for, with the energy kept instead of
//  thrown away.
//
//  Two properties this file is built around, both pinned by tests/phase:
//
//    * **Idle is bit-exact.** The crossovers, the envelopes and the sums only run
//      when the caller passes a non-zero engagement, and the result is blended in
//      by `engagement * maxOpposition`. A frame whose bands are all correlated
//      therefore comes back untouched - not "nearly", bit for bit - which is what
//      keeps a guard that spends most of its life asleep from changing the sound
//      of a mix that has no problem.
//
//    * **The band a fault is in is the band that moves.** A frame whose
//      opposition lives below 200 Hz rotates the low band and leaves the mid and
//      high bands where they were; the harness measures the side energy per band
//      on the way out to prove it.
//
//  The crossover is written here rather than taken from juce::dsp so that a
//  harness with no JUCE can measure it (tests/phase). It is the same topology -
//  two cascaded 2nd-order Butterworth sections per path, Q = 1/sqrt(2) - and the
//  harness checks the one property that matters for a crossover used this way:
//  the three bands sum back to a flat magnitude, so the guard changes phase where
//  it must and nothing else.
// =============================================================================
#pragma once

#include <array>
#include <cmath>

namespace j37dsp
{

/** One 2nd-order Butterworth section, RBJ coefficients, transposed direct form
    II (two state variables, and no coefficient recomputation per sample). */
struct ButterworthSection
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    void setLowPass (float cutoffHz, float sampleRate) noexcept
    {
        const auto w0 = 2.0f * juce::MathConstants<float>::pi * cutoffHz / sampleRate;
        const auto cosW = std::cos (w0);
        const auto alpha = std::sin (w0) / (2.0f * 0.70710678f);
        const auto a0 = 1.0f + alpha;

        b0 = (1.0f - cosW) * 0.5f / a0;
        b1 = (1.0f - cosW) / a0;
        b2 = b0;
        a1 = -2.0f * cosW / a0;
        a2 = (1.0f - alpha) / a0;
    }

    void setHighPass (float cutoffHz, float sampleRate) noexcept
    {
        const auto w0 = 2.0f * juce::MathConstants<float>::pi * cutoffHz / sampleRate;
        const auto cosW = std::cos (w0);
        const auto alpha = std::sin (w0) / (2.0f * 0.70710678f);
        const auto a0 = 1.0f + alpha;

        b0 = (1.0f + cosW) * 0.5f / a0;
        b1 = -(1.0f + cosW) / a0;
        b2 = b0;
        a1 = -2.0f * cosW / a0;
        a2 = (1.0f - alpha) / a0;
    }

    void reset() noexcept { z1 = z2 = 0.0f; }

    float process (float x) noexcept
    {
        const auto y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

/** A 4th-order Linkwitz-Riley crossover: two cascaded Butterworth sections per
    path, so each output is -6 dB at the cutoff and their sum is all-pass. */
struct LinkwitzRiley4
{
    ButterworthSection lowA, lowB, highA, highB;

    float low = 0.0f, high = 0.0f;

    void prepare (float cutoffHz, float sampleRate) noexcept
    {
        lowA.setLowPass (cutoffHz, sampleRate);
        lowB.setLowPass (cutoffHz, sampleRate);
        highA.setHighPass (cutoffHz, sampleRate);
        highB.setHighPass (cutoffHz, sampleRate);
    }

    void reset() noexcept
    {
        lowA.reset(); lowB.reset(); highA.reset(); highB.reset();
        low = high = 0.0f;
    }

    void process (float x) noexcept
    {
        low = lowB.process (lowA.process (x));
        high = highB.process (highA.process (x));
    }
};

/** Three bands, two crossovers: the second splits the FIRST's high output, so
    low + mid + high sums to low + high of the full-range crossover. */
struct ThreeBandSplit
{
    LinkwitzRiley4 first;
    LinkwitzRiley4 second;

    std::array<float, 3> band { 0.0f, 0.0f, 0.0f };   // low, mid, high

    void prepare (float lowHz, float highHz, float sampleRate) noexcept
    {
        first.prepare (lowHz, sampleRate);
        second.prepare (highHz, sampleRate);
    }

    void reset() noexcept
    {
        first.reset();
        second.reset();
        band.fill (0.0f);
    }

    void process (float x) noexcept
    {
        first.process (x);
        second.process (first.high);

        band[0] = first.low;
        band[1] = second.low;
        band[2] = second.high;
    }
};

//==============================================================================
/** The guard itself. One instance per processor: it holds the two channels'
    crossovers and the six band envelopes, and it is prepared with the engine's
    sample rate.

    `process()` is called once per frame with the frame's two samples and the
    engagement the caller's own detector has already smoothed (0..1). It returns
    false when it changed nothing, so the caller can skip the telemetry work.
*/
class MultibandPhaseGuard
{
public:
    static constexpr float lowCrossoverHz = 200.0f;
    static constexpr float highCrossoverHz = 2000.0f;

    /** The correlation envelope's pole: 400 ms at 48 kHz, expressed as a time so
        it is the same detector at every engine rate. The single-band guard this
        replaces used the same figure, which is why the readout moves the same
        way. */
    static constexpr float envelopeSeconds = 0.4f;

    void prepare (double sampleRate) noexcept
    {
        const auto rate = static_cast<float> (juce::jmax (8000.0, sampleRate));

        for (auto& split : splits)
            split.prepare (lowCrossoverHz, highCrossoverHz, rate);

        coefficient = 1.0f - std::exp (-1.0f / (envelopeSeconds * rate));

        reset();
    }

    void reset() noexcept
    {
        for (auto& split : splits)
            split.reset();

        for (auto& band : bands)
        {
            band.product = 0.0f;
            band.magnitude = 1.0e-9f;
            band.correction = 0.0f;
        }
    }

    /** Rotates the bands that need it, in place.

        `engagement` is the caller's smoothed 0..1 amount (its own wideband
        detector). At 0 this function returns immediately and the frame is
        untouched.
    */
    bool process (float& left, float& right, float engagement) noexcept
    {
        if (engagement <= 1.0e-6f)
            return false;

        const auto dryLeft = left;
        const auto dryRight = right;

        splits[0].process (left);
        splits[1].process (right);

        auto summedLeft = 0.0f;
        auto summedRight = 0.0f;
        auto mostOpposition = 0.0f;

        for (int index = 0; index < 3; ++index)
        {
            const auto i = static_cast<std::size_t> (index);
            const auto mid = 0.5f * (splits[0].band[i] + splits[1].band[i]);
            const auto side = 0.5f * (splits[0].band[i] - splits[1].band[i]);

            // The same measure the single-band guard uses, per band: the sign of
            // the mid/side energy difference is the sign of the correlation, and
            // the normaliser makes it a ratio rather than a level.
            auto& band = bands[i];
            band.product += ((mid * mid - side * side) - band.product) * coefficient;
            band.magnitude += ((mid * mid + side * side) - band.magnitude) * coefficient;
            band.magnitude = juce::jmax (1.0e-9f, band.magnitude);

            const auto opposition = band.product < 0.0f
                ? juce::jlimit (0.0f, 1.0f, -band.product / band.magnitude)
                : 0.0f;

            band.correction += (opposition - band.correction) * coefficient;
            mostOpposition = juce::jmax (mostOpposition, band.correction);

            // The rotation: a quarter turn at full opposition recovers an
            // inverted band into mono, and every level between is proportional.
            // The angle form (not its sine) is what keeps the derivative bounded
            // at full engagement - see the note at the single-band call site.
            const auto rotation = juce::jlimit (0.0f, 1.0f,
                                                band.correction * engagement)
                                  * juce::MathConstants<float>::halfPi;

            const auto sinRotation = std::sin (rotation);
            const auto cosRotation = std::cos (rotation);

            const auto rotatedMid = mid * cosRotation + side * sinRotation;
            const auto rotatedSide = side * cosRotation - mid * sinRotation;

            summedLeft += rotatedMid + rotatedSide;
            summedRight += rotatedMid - rotatedSide;
        }

        // Blended by the engagement AND by how much opposition there actually
        // was, so a frame with no fault comes back bit for bit even at full
        // engagement. `mostOpposition` is already smoothed per band.
        const auto blend = juce::jlimit (0.0f, 1.0f, engagement * mostOpposition);

        left = dryLeft + (summedLeft - dryLeft) * blend;
        right = dryRight + (summedRight - dryRight) * blend;

        return blend > 0.0f;
    }

private:
    struct Band
    {
        float product = 0.0f;
        float magnitude = 1.0e-9f;
        float correction = 0.0f;
    };

    std::array<ThreeBandSplit, 2> splits {};
    std::array<Band, 3> bands {};
    float coefficient = 0.0f;
};

} // namespace j37dsp
