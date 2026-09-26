/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

// chowdsp_utils (fetched by CPM in CMakeLists.txt). The approximation helpers
// the engine uses live in the header-only chowdsp_math module; the include is
// conditional so the DSP regression harness - which cuts structs out of this
// header verbatim and compiles them against a shim, without chowdsp on its
// include path - is never asked for it.
//
// chowdsp_dsp_utils is pulled in alongside it. It is a full JUCE module rather
// than a header, so it needs the plugin target's include path, which the harness
// does not provide; the same guard covers both. What the engine uses from it is
// named where it is used.
#if ! defined (J37_DSP_HARNESS) && __has_include (<chowdsp_math/chowdsp_math.h>)
 #include <chowdsp_math/chowdsp_math.h>
 #define J37_HAS_CHOWDSP_MATH 1
#else
 #define J37_HAS_CHOWDSP_MATH 0
#endif
#if ! defined (J37_DSP_HARNESS) && __has_include (<chowdsp_dsp_utils/chowdsp_dsp_utils.h>)
 #include <chowdsp_dsp_utils/chowdsp_dsp_utils.h>
 #define J37_HAS_CHOWDSP_DSP 1
#else
 #define J37_HAS_CHOWDSP_DSP 0
#endif

// xsimd (fetched by CPM in CMakeLists.txt). Portable SIMD wrappers, and the
// vehicle for the one approximation the review flagged as worth having: a
// vectorised tanh. The scalar shaper calls std::tanh four times per sample per
// channel (two branches, each also evaluated at the bias-only point so the DC
// pedestal can be subtracted), and that is the hot loop at 8x oversampling.
//
// It is wired in as an OPT-IN, not a default: an approximation only earns its
// place once a benchmark shows the shaper dominates, and a measured difference
// in the rendered audio is a change to the sound. See the use site in
// PluginProcessor.cpp for what the flag actually switches.
#if ! defined (J37_DSP_HARNESS) && defined (J37_USE_SIMD_TANH) && __has_include (<xsimd/xsimd.hpp>)
 #include <xsimd/xsimd.hpp>
 #define J37_HAS_XSIMD 1
#else
 #define J37_HAS_XSIMD 0
#endif

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>

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

//==============================================================================
/**
    The four saturation principles, and the blend that combines them.

    A tape machine is ONE of the ways analogue electronics bend a signal, and the
    plugin was built around that one curve. The four below are the distinct
    mechanisms a real signal chain uses, and they are genuinely different shapes
    rather than four settings of one:

      TAPE     - magnetic hysteresis: a MEMORY term, because the medium's state
                 depends on where it has been. Gentle at low level, and the
                 asymmetry is what makes the even harmonics.
      VALVE    - thermionic: a soft, strongly ASYMMETRIC knee with a wide
                 transition. Even-dominant, and it compresses rather than clips,
                 so it thickens before it distorts.
      CASSETTE - narrow-gauge, low-bias ferric: a HARD, early knee with a very
                 limited headroom and a pronounced low-frequency bump. The
                 "everything is louder and smaller" character.
      AMP      - a guitar amplifier's input stage: a high-gain, nearly symmetric
                 cascade that clips HARD and generates strong odd harmonics. It
                 is the one that bites.

    Blending them is not a gimmick: a real chain is exactly this. A guitar goes
    into an amp (AMP), the amp into a desk and a tape machine (TAPE), a valve
    compressor or a valve preamp somewhere in the path (VALVE), and the whole
    thing may end up on a cassette (CASSETTE). The BLEND control moves the
    weighting across those four, and the default sits on tape because that is
    what the plugin is calibrated around.

    Every curve is normalised to unity slope at the origin, exactly like
    magneticHysteresis, so the blend cannot change the level - only the shape.
    That property is what makes the control usable: moving it changes the
    harmonics, not the gain, and the DRIVE control keeps its own meaning.
*/
struct SaturationCore
{
    // -----------------------------------------------------------------------
    //  Weights. These are set once per block from the BLEND and SHAPE controls;
    //  they are kept normalised so the four always sum to 1 and the blend is a
    //  true crossfade rather than a stack.
    // -----------------------------------------------------------------------
    float tapeWeight = 1.0f;
    float valveWeight = 0.0f;
    float cassetteWeight = 0.0f;
    float ampWeight = 0.0f;

    // -----------------------------------------------------------------------
    //  Per-principle state.
    //
    //  Tape needs the hysteresis memory (three slots, as before). The valve and
    //  amp stages carry a bias-shift state, because a real stage's operating
    //  point drifts with the signal - that drift is a slow envelope, not a
    //  sample-by-sample term, so it is a one-pole per channel.
    // -----------------------------------------------------------------------
    float tapeMemory = 0.0f;
    float valveBiasState = 0.0f;
    float ampBiasState = 0.0f;
    float cassetteBiasState = 0.0f;

    void reset() noexcept
    {
        tapeMemory = 0.0f;
        valveBiasState = 0.0f;
        ampBiasState = 0.0f;
        cassetteBiasState = 0.0f;
    }

    /** Sets the four weights from two controls. Both are 0..1. */
    void setBlend (float blend, float shape) noexcept
    {
        // BLEND sweeps the weighting across the four principles in a fixed order,
        // tape -> valve -> cassette -> amp, so the control has one direction and
        // the ear can learn it. SHAPE skews the distribution: low concentrates on
        // a single principle (a focused, obvious character), high spreads it
        // evenly (a blend that reads as one compound machine).
        const auto b = juce::jlimit (0.0f, 1.0f, blend);
        const auto s = juce::jlimit (0.0f, 1.0f, shape);

        // Each principle gets a triangular response centred on its own position
        // along the sweep, so neighbouring ones overlap and the blend is smooth.
        const auto triangle = [] (float x, float centre, float width)
        {
            return juce::jmax (0.0f, 1.0f - std::abs (x - centre) / width);
        };

        // Width grows with SHAPE: at 0 the triangles are narrow and the sweep
        // snaps from one principle to the next; at 1 they are wide enough that all
        // four contribute at every position.
        const auto width = 0.25f + 0.75f * s;

        tapeWeight     = triangle (b, 0.00f, width);
        valveWeight    = triangle (b, 0.33f, width);
        cassetteWeight = triangle (b, 0.66f, width);
        ampWeight      = triangle (b, 1.00f, width);

        // The ends of the sweep must not fall off the edge: at b = 0 only tape is
        // in range of its own triangle, and the others are zero, which would leave
        // the sum short and the normalisation below would divide by a small number.
        // Seeding each end with its own principle keeps the extremes solid.
        if (b < 0.02f) tapeWeight = 1.0f;
        if (b > 0.98f) ampWeight = 1.0f;

        const auto sum = tapeWeight + valveWeight + cassetteWeight + ampWeight;
        if (sum > 1.0e-6f)
        {
            tapeWeight /= sum;
            valveWeight /= sum;
            cassetteWeight /= sum;
            ampWeight /= sum;
        }
        else
        {
            tapeWeight = 1.0f;
            valveWeight = cassetteWeight = ampWeight = 0.0f;
        }
    }

    /** True when nothing is contributing, so the caller can skip the whole core. */
    bool isIdle() const noexcept
    {
        return tapeWeight + valveWeight + cassetteWeight + ampWeight <= 1.0e-6f;
    }

    // -----------------------------------------------------------------------
    //  The four curves. Each takes the driven input and returns a value with the
    //  SAME unity slope at the origin, so they are interchangeable in the blend.
    //  `biasState` is the principle's own slow operating-point memory.
    // -----------------------------------------------------------------------

    /**
        TAPE - magnetic hysteresis with memory.

        The reference curve: two tanh branches at different slopes, blended, plus
        the asymmetry term that produces the even harmonics. `memory` is the
        previous shaped output, which is what gives tape its "sticky" transient
        behaviour - the curve knows where it has been, not just where it is.
    */
    float shapeTape (float x, float drive, float asymmetry) noexcept
    {
        const float biased = x + asymmetry;

        const float slope = 1.0f + drive * 2.6f;
        const float hard = std::tanh (biased * slope) / slope;

        const float lagged = tapeMemory * 0.62f;
        const float delayedSlope = 0.55f + drive * 1.0f;
        const float delayed = std::tanh ((biased * delayedSlope) + lagged) / delayedSlope;

        constexpr float hardWeight = 0.70f;
        constexpr float delayedWeight = 0.30f;
        const float blended = hard * hardWeight + delayed * delayedWeight;

        const float asymmetryTerm = asymmetry * 0.45f * hard * hard;

        // The zero-point correction, exactly as in magneticHysteresis: subtracting
        // the curve's own value at zero input removes the DC pedestal the bias
        // offset would otherwise leave, so zero in still means zero out.
        const float hardAtZero = std::tanh (asymmetry * slope) / slope;
        const float delayedAtZero = std::tanh ((asymmetry * delayedSlope) + lagged) / delayedSlope;
        const float blendedAtZero = hardAtZero * hardWeight + delayedAtZero * delayedWeight;
        const float asymmetryAtZero = asymmetry * 0.45f * hardAtZero * hardAtZero;

        const float out = (blended - blendedAtZero) - (asymmetryTerm - asymmetryAtZero);
        tapeMemory = out;
        return out;
    }

    /**
        VALVE - thermionic soft asymmetry.

        A valve stage has a wide, gradual transition and a strong asymmetry: the
        grid conducts on one half of the waveform long before the other half
        compresses. That is why valve gear is described as warm rather than edgy -
        the even harmonics dominate.

        The curve is a single tanh with a bias offset, but with a much gentler
        slope than tape and a bias that FOLLOWS the signal: a real stage's
        operating point moves with the average level, which is what makes the
        character level-dependent rather than static.
    */
    float shapeValve (float x, float drive, float asymmetry) noexcept
    {
        // The operating point drifts toward the signal's own average. A slow
        // one-pole rather than the instantaneous value, because the drift is a
        // thermal/electrical time constant in the real thing, not a waveform term.
        valveBiasState += (x - valveBiasState) * 0.0008f;
        const float driftingBias = asymmetry + valveBiasState * 0.25f;

        const float biased = x + driftingBias;

        // A gentler slope than tape's: the valve compresses across a wider range
        // instead of bending early. The squared term is the valve's own soft knee.
        const float slope = 1.0f + drive * 1.5f;
        const float soft = std::tanh (biased * slope) / slope;

        // The asymmetry here is stronger and applied to the whole curve rather
        // than only to the hard branch, which is what makes the even content
        // dominate instead of sitting under an odd-heavy fundamental.
        const float asymmetryTerm = driftingBias * 0.65f * soft * soft;

        const float softAtZero = std::tanh (driftingBias * slope) / slope;
        const float asymmetryAtZero = driftingBias * 0.65f * softAtZero * softAtZero;

        return (soft - softAtZero) - (asymmetryTerm - asymmetryAtZero);
    }

    /**
        CASSETTE - narrow gauge, low bias, hard early knee.

        A cassette is not "tape but worse": it is a different mechanism. The
        narrow track and low bias current mean the medium saturates far earlier
        and much more abruptly, so the curve has a tight linear region and then a
        hard corner. That is the "everything is louder and smaller" character, and
        it is what this models.

        The corner is a smoothstep rather than a tanh: a polynomial transition
        that reaches its asymptote quickly instead of approaching it
        exponentially, which is exactly the difference in feel between the two.
    */
    float shapeCassette (float x, float drive, float asymmetry) noexcept
    {
        // A small bias drift again, but much faster than the valve's - a cassette's
        // low bias means the operating point moves with the programme far more.
        cassetteBiasState += (x - cassetteBiasState) * 0.004f;
        const float biased = x + asymmetry * 0.5f + cassetteBiasState * 0.15f;

        // The knee: at drive 0 it is at 0.9 (a wide, fairly clean region), and it
        // closes fast as drive rises, which is the cassette's defining behaviour.
        const float knee = juce::jlimit (0.12f, 0.95f, 0.9f - drive * 0.75f);

        const float magnitude = std::abs (biased);
        const float sign = biased < 0.0f ? -1.0f : 1.0f;

        float shaped;
        if (magnitude <= knee)
        {
            // Linear region, scaled so the slope is unity: y = x.
            shaped = biased;
        }
        else
        {
            // Above the knee the curve walks to the asymptote over a short span.
            const float overshoot = juce::jmin (1.0f, (magnitude - knee) / juce::jmax (0.05f, knee));
            const float eased = overshoot * overshoot * (3.0f - 2.0f * overshoot); // smoothstep
            shaped = sign * (knee + eased * (1.0f - knee));
        }

        // The cassette's low-frequency bump: the head bump and the narrow track
        // both lift the bottom end, and it is part of the character rather than an
        // artefact. Applied as a level-dependent term so it only shows under drive.
        const float bump = 1.0f + drive * 0.12f * (1.0f - juce::jmin (1.0f, std::abs (shaped)));

        const float out = shaped * bump;

        // Zero-point correction, same rule as the others.
        const float atZero = [&]
        {
            const float z = asymmetry * 0.5f;
            const float m = std::abs (z);
            const float s = z < 0.0f ? -1.0f : 1.0f;
            if (m <= knee) return z;
            const float o = juce::jmin (1.0f, (m - knee) / juce::jmax (0.05f, knee));
            const float e = o * o * (3.0f - 2.0f * o);
            return s * (knee + e * (1.0f - knee));
        }();
        const float atZeroBumped = atZero * (1.0f + drive * 0.12f
                                             * (1.0f - juce::jmin (1.0f, std::abs (atZero))));

        return out - atZeroBumped;
    }

    /**
        AMP - a guitar amplifier's input stage.

        The one that bites. A high-gain valve input clips HARD and nearly
        symmetrically, which is why a distorted guitar is odd-harmonic dominant
        and reads as aggressive rather than warm. The asymmetry here is small on
        purpose: that is the difference between an amp and a valve preamp.

        The second stage is what gives it the "cascade" character - two gain
        stages in series compress twice, so the curve is flatter in the middle
        than a single stage of the same total gain.
    */
    float shapeAmp (float x, float drive, float asymmetry) noexcept
    {
        // A small, fast bias drift: a high-gain stage's operating point moves
        // quickly with the signal because the gain makes even a small shift
        // audible.
        ampBiasState += (x - ampBiasState) * 0.002f;
        const float biased = x + asymmetry * 0.18f + ampBiasState * 0.10f;

        // Stage one: high gain, hard clip. The slope is much steeper than tape's
        // so the knee arrives early.
        const float slope1 = 1.0f + drive * 4.5f;
        const float stage1 = std::tanh (biased * slope1) / slope1;

        // Stage two: the cascade. A second, gentler stage applied to the first
        // stage's output, which is what flattens the middle of the curve.
        const float slope2 = 1.0f + drive * 1.2f;
        const float stage2 = std::tanh (stage1 * slope2) / slope2;

        // Hard-clipped stages are nearly symmetric, so the asymmetry term is
        // small - just enough to keep some even content rather than a pure odd
        // series, which would read as a fuzz rather than an amp.
        const float asymmetryTerm = asymmetry * 0.20f * stage2 * stage2;

        const float s1Zero = std::tanh ((asymmetry * 0.18f) * slope1) / slope1;
        const float s2Zero = std::tanh (s1Zero * slope2) / slope2;
        const float asymmetryAtZero = asymmetry * 0.20f * s2Zero * s2Zero;

        return (stage2 - s2Zero) - (asymmetryTerm - asymmetryAtZero);
    }

    /**
        Runs the blend. `drive` and `asymmetry` are the same arguments the tape
        shaper has always taken, so the existing controls keep their meaning; the
        weighting is the only thing BLEND and SHAPE change.
    */
    float process (float x, float drive, float asymmetry) noexcept
    {
        if (isIdle())
            return x;

        float out = 0.0f;

        if (tapeWeight > 1.0e-4f)
            out += shapeTape (x, drive, asymmetry) * tapeWeight;

        if (valveWeight > 1.0e-4f)
            out += shapeValve (x, drive, asymmetry) * valveWeight;

        if (cassetteWeight > 1.0e-4f)
            out += shapeCassette (x, drive, asymmetry) * cassetteWeight;

        if (ampWeight > 1.0e-4f)
            out += shapeAmp (x, drive, asymmetry) * ampWeight;

        return out;
    }
};

//==============================================================================
/**
    Guitar-amplifier features: the parts of a real amp's behaviour that are not
    the clipping curve.

    A saturation curve alone does not sound like an amplifier. What does is the
    behaviour AROUND the curve, and these are the four that matter most:

      SAG         - the supply droops under sustained demand, so the gain falls
                    a little and then recovers. It is why an amp "gives" under a
                    held chord and why the attack feels spongy rather than
                    immediate. Modelled as a slow envelope that pulls the drive
                    down, with a recovery time long enough to hear.
      PRESENCE    - the negative-feedback network's top-end lift. A bright,
                    narrow boost in the upper mids that sits AFTER the clipping,
                    so it sharpens what is already there rather than adding new
                    harmonics.
      CABINET     - the speaker and its box. A resonant low-pass, not a plain
                    one: a peak around 80-120 Hz from the cabinet tuning and a
                    roll-off above 4-6 kHz from the cone's mass. Without it a
                    clipped signal is fizzy; with it, it reads as a speaker.
      BIAS SHIFT  - the DC operating point of the input stage, which is the single
                    most effective control on a real amp's character: cold is
                    tight and crossover-distorted, hot is fat and compressed. It
                    is the amp's own BIAS, separate from the tape BIAS.
*/
struct AmpVoicing
{
    // Sag: the supply envelope. `sagEnvelope` follows the signal's demand, and
    // `sagGain` is the gain reduction it produces.
    float sagEnvelope = 0.0f;
    float sagGain = 1.0f;

    // Cabinet: a two-pole resonant low-pass, per channel, plus its own state.
    float cabinetLow1 = 0.0f;
    float cabinetLow2 = 0.0f;
    float cabinetPeak = 0.0f;

    // Presence: a one-pole high-pass taken out of the signal and added back with
    // gain, which is what the feedback network's lift actually does.
    float presenceHighState = 0.0f;

    void reset() noexcept
    {
        sagEnvelope = 0.0f;
        sagGain = 1.0f;
        cabinetLow1 = cabinetLow2 = cabinetPeak = 0.0f;
        presenceHighState = 0.0f;
    }

    /**
        Runs the amplifier behaviour around one sample.

        `x` is the already-shaped signal; the return value is what leaves the amp.
        All three coefficients are block-rate constants, built by the caller.

        Order matters and follows the hardware: SAG acts on the DRIVE before the
        curve (it is the supply), so it is applied by the caller to the drive
        amount; here we run the POST-curve stages - cabinet first (the speaker is
        the last thing in the amp), then presence (the feedback network taps the
        output).
    */
    float process (float x,
                   float cabinetLowCoefficient,
                   float cabinetPeakCoefficient,
                   float presenceCoefficient,
                   float presenceAmount) noexcept
    {
        // -- Cabinet: a resonant low-pass ------------------------------------
        // Two cascaded one-poles give the roll-off; the peak term adds the
        // cabinet's own resonance on top by feeding back a little of the
        // difference between the two poles, which is where the low-mid thump
        // comes from.
        cabinetLow1 += (x - cabinetLow1) * cabinetLowCoefficient;
        cabinetLow2 += (cabinetLow1 - cabinetLow2) * cabinetLowCoefficient;

        // The resonance is the difference between the two pole outputs: it is
        // near zero for slow signals and rises where the cabinet's tuning sits.
        const float resonance = (cabinetLow1 - cabinetLow2);
        cabinetPeak += (resonance - cabinetPeak) * cabinetPeakCoefficient;

        const float cabinetOut = cabinetLow2 + cabinetPeak * 0.85f;

        // -- Presence: the feedback network's top-end lift ---------------------
        // A one-pole high-pass: the difference between the signal and its own
        // low-passed copy. Adding it back with gain is exactly what a presence
        // control does, and because it sits after the clipping it sharpens the
        // harmonics already present rather than generating new ones.
        presenceHighState += (cabinetOut - presenceHighState) * presenceCoefficient;
        const float highBand = cabinetOut - presenceHighState;

        return cabinetOut + highBand * presenceAmount;
    }
};

//==============================================================================
/**
    A sample-clock adapter for JUCE's SmoothedValue.

    The engine uses one coefficient pair for both channels, so every ramp must advance
    once per SAMPLE, not once per channel and not never. JUCE's getCurrentValue() is a
    peek; it does not advance the ramp. The first smoother in each frame advances the
    shared clock, and the other smoothers advance lazily on their first read in that
    frame. This preserves the existing call sites while making their old
    getCurrentValue() intent explicit and sample-accurate.
*/
struct SampleClock
{
    std::uint64_t sample = 0;
};

class SampleSmoother
{
public:
    explicit SampleSmoother (SampleClock& clockToUse, bool startsSample = false,
                             bool invertOutput = false, float initialValue = 0.0f)
        : clock (clockToUse), startsSample (startsSample), invertOutput (invertOutput),
          smoother (initialValue)
    {
    }

    void reset (double sampleRateToUse, double smoothingSeconds) noexcept
    {
        smoother.reset (sampleRateToUse, smoothingSeconds);
        lastSample = clock.sample;
        snapNextTarget = true;
    }

    void setCurrentAndTargetValue (float value) noexcept
    {
        smoother.setCurrentAndTargetValue (value);
        lastSample = clock.sample;
        snapNextTarget = false;
    }

    void setTargetValue (float value) noexcept
    {
        // The first target after a rate reset is seeded at its exact value. Without
        // this, a coefficient that starts at zero (notably the playback poles) can
        // leave the first wet frame silent while its 20 ms ramp is still in progress.
        if (snapNextTarget)
        {
            smoother.setCurrentAndTargetValue (value);
            snapNextTarget = false;
        }
        else
        {
            smoother.setTargetValue (value);
        }
    }

    float getNextValue() noexcept
    {
        if (startsSample)
        {
            ++clock.sample;
            lastSample = clock.sample;
        }
        else
        {
            lastSample = clock.sample;
        }

        const auto value = smoother.getNextValue();
        return invertOutput ? 1.0f - value : value;
    }

    float getCurrentValue() noexcept
    {
        if (lastSample != clock.sample)
        {
            lastSample = clock.sample;
            const auto value = smoother.getNextValue();
            return invertOutput ? 1.0f - value : value;
        }

        const auto value = smoother.getCurrentValue();
        return invertOutput ? 1.0f - value : value;
    }

    bool isSmoothing() const noexcept
    {
        return smoother.isSmoothing();
    }

    // The processing loop already consumed every sample. Advancing here would apply
    // the ramp a second time and turn it into a block-rate zipper.
    void skip (int) noexcept
    {
    }

private:
    using Smoother = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;

    SampleClock& clock;
    bool startsSample = false;
    bool invertOutput = false;
    Smoother smoother;
    std::uint64_t lastSample = 0;
    bool snapNextTarget = true;
};

//==============================================================================
/**
    One glue compressor stage. The two stages in this plugin are completely
    independent: each keeps its own detector envelope and its own gain computer,
    and neither reads the other's state. They are driven purely by the signal
    that reaches them and by the Input / Output parameters.

    Time constants are SEMI-AUTOMATIC - there are no attack or release controls.
    The stage listens to how the signal itself behaves and adapts both constants
    as it goes, along three independent axes:

      1. Programme level - the harder the stage is being driven, the slower it moves,
         which is what makes it read as machine headroom rather than a limiter.
      2. Transient vs sustained material - it tracks a fast and a slow view of the
         signal; when the fast view runs ahead of the slow one we are on a transient,
         so attack is quickened to catch it. When the two agree we are in sustained
         programme, so attack is relaxed and release is stretched, letting the stage
         breathe with the music instead of pumping.
      3. Envelope fill - as the detector fills up, release lengthens further, so a
         dense passage is held together and a sparse one recovers quickly.
*/
struct GlueCompressor
{
    float envelope = 0.0f;

    // A second, much slower follower used only to tell transients apart from sustained
    // programme. Keeping it separate from `envelope` means the adaptation never feeds
    // back into the gain computation itself.
    float slowEnvelope = 0.0f;

    void reset() noexcept
    {
        envelope = 0.0f;
        slowEnvelope = 0.0f;
    }

    /** Envelope level of the most recent block, as 0..1 linear activity. */
    float getEnvelopeActivity() const noexcept
    {
        return juce::jlimit (0.0f, 1.0f, std::sqrt (juce::jmax (0.0f, envelope)));
    }

    /** 0 = sustained programme, 1 = sharp transient. Used by the UI to show activity. */
    float getTransientAmount() const noexcept
    {
        const auto fast = std::sqrt (juce::jmax (0.0f, envelope));
        const auto slow = std::sqrt (juce::jmax (0.0f, slowEnvelope));
        if (slow <= 1.0e-6f)
            return 0.0f;

        return juce::jlimit (0.0f, 1.0f, (fast - slow) / slow * 1.6f);
    }

    /**
        Pushes signal power through the detector; call once per sample.

        attackBaseSeconds and releaseBaseSeconds are the nominal constants. They are
        then adapted by the three axes described above, so the caller never has to
        schedule attack or release by hand - that is the semi-automatic behaviour.

        The two coefficients that do NOT depend on the signal are passed in by the
        caller instead of being recomputed here. Both are functions of the sample
        rate, the release base and the load factor - none of which move within a
        block - so evaluating them per sample was two std::exp calls per sample per
        channel for a value that changes once per block. See the call site in
        processTapeEngine for where they are built.
    */
    struct Coefficients
    {
        float slow = 0.0f;
        float attack = 0.0f;
        float release = 0.0f;
    };

    /** Builds the block-rate coefficients. Called once per block, not per sample. */
    static Coefficients makeCoefficients (float sampleRate,
                                          float attackBaseSeconds,
                                          float releaseBaseSeconds,
                                          float loadFactor) noexcept
    {
        const float safeRate = juce::jmax (1.0f, sampleRate);

        // The slow follower tracks the running programme level, roughly a hundred
        // times slower than the detector itself. It depends only on the release base
        // and the rate, so it is a block-rate constant like the other two.
        const float slowTimeConstant = juce::jmax (0.05f, releaseBaseSeconds * 3.5f);

        // Load lengthens both constants, so a stage being leaned on turns slow and
        // dense while an idle one stays quick and transparent. The load factor is
        // the INPUT / OUTPUT trim, which is also read once per block.
        const float loadStretch = 1.0f + loadFactor * 1.8f;
        const float releaseStretch = 1.0f + loadFactor * 2.2f;

        Coefficients coefficients;
        coefficients.slow = j37math::exp (-1.0f / (safeRate * slowTimeConstant));
        coefficients.attack = attackBaseSeconds * loadStretch;
        coefficients.release = releaseBaseSeconds * releaseStretch;
        return coefficients;
    }

    float processDetection (float detectorPower,
                            float sampleRate,
                            const Coefficients& blockCoefficients) noexcept
    {
        const float safeRate = juce::jmax (1.0f, sampleRate);

        // -- Axis 3: how full the detector already is --------------------------
        const float envelopeLevel = getEnvelopeActivity();

        // -- Axis 2: transient or sustained? ----------------------------------
        // Comparing the fast detector against the slow programme follower is enough
        // to tell a drum hit (fast spikes above the slow average) from a sustained
        // pad or vocal line.
        const float slowCoefficient = blockCoefficients.slow;
        slowEnvelope = slowCoefficient * slowEnvelope
                     + (1.0f - slowCoefficient) * detectorPower;

        const float transientAmount = getTransientAmount();

        // Attack: quick on transients so nothing is missed, relaxed on sustained
        // material so the stage does not clamp the body of the sound. The transient
        // term dominates the level term, because catching a peak matters more.
        //
        // Only the two signal-dependent factors are evaluated here; the load stretch
        // and the base constant are already folded into blockCoefficients.attack.
        const float transientSpeedUp = 1.0f - transientAmount * 0.72f;
        const float attackSeconds = blockCoefficients.attack
                                  * juce::jlimit (0.25f, 1.6f, transientSpeedUp)
                                  * (1.0f + envelopeLevel * 0.9f);

        // Release: long on sustained programme and when the detector is full, short on
        // isolated transients so the stage reopens before the next event. This is what
        // gives the classic auto-release feel - dense passages stay together, sparse
        // ones breathe.
        const float releaseStretch = 1.0f
                                   + (1.0f - transientAmount) * 1.15f
                                   + envelopeLevel * 2.4f;
        const float releaseSeconds = blockCoefficients.release * releaseStretch;

        const float timeConstant = detectorPower > envelope ? attackSeconds : releaseSeconds;
        const float coefficient = j37math::exp (-1.0f / (safeRate * timeConstant));
        envelope = coefficient * envelope + (1.0f - coefficient) * detectorPower;

        return juce::Decibels::gainToDecibels (j37math::sqrt (juce::jmax (0.0f, envelope)),
                                               -100.0f);
    }
};

//==============================================================================
/**
    Together-loudness (LUFS) measurement, following the ITU-R BS.1770 / EBU R128
    weightings: a high-shelf and a high-pass, then mean-square over the measurement
    window. Only the K-weighting is implemented - this is a live meter, not an
    offline loudness normaliser, so gating and true-peak are deliberately left out.

    The filters are biquads in direct form I, rebuilt from the sample rate in
    prepare(), so the reading is correct at every supported rate: 44.1, 48, 88.2,
    96, 176.4 and 192 kHz. The coefficients come from tan(pi * f0 / rate), which is
    exact at any rate, so no additional scaling is needed for a 192 kHz session.
*/
struct LoudnessMeter
{
    void prepare (double sampleRate) noexcept
    {
        const auto rate = juce::jmax (8000.0, sampleRate);

        // Stage 1: high-shelf, roughly +4 dB above 1.5 kHz, as specified for K-weighting.
        {
            const auto f0 = 1681.974450955533;
            const auto gainDb = 3.999843853973347;
            const auto q = 0.7071752369554196;

            const auto k = std::tan (juce::MathConstants<double>::pi * f0 / rate);
            const auto vh = std::pow (10.0, gainDb / 20.0);
            const auto vb = std::pow (vh, 0.4996667741545416);
            const auto denominator = 1.0 + k / q + k * k;

            shelf.b0 = static_cast<float> ((vh + vb * k / q + k * k) / denominator);
            shelf.b1 = static_cast<float> (2.0 * (k * k - vh) / denominator);
            shelf.b2 = static_cast<float> ((vh - vb * k / q + k * k) / denominator);
            shelf.a1 = static_cast<float> (2.0 * (k * k - 1.0) / denominator);
            shelf.a2 = static_cast<float> ((1.0 - k / q + k * k) / denominator);
        }

        // Stage 2: high-pass at 38 Hz, the second half of the K-weighting curve.
        {
            const auto f0 = 38.13547087602444;
            const auto q = 0.5003270373238773;

            const auto k = std::tan (juce::MathConstants<double>::pi * f0 / rate);
            const auto denominator = 1.0 + k / q + k * k;

            highPass.b0 = static_cast<float> (1.0 / denominator);
            highPass.b1 = static_cast<float> (-2.0 / denominator);
            highPass.b2 = static_cast<float> (1.0 / denominator);
            highPass.a1 = static_cast<float> (2.0 * (k * k - 1.0) / denominator);
            highPass.a2 = static_cast<float> ((1.0 - k / q + k * k) / denominator);
        }
    }

    void reset() noexcept
    {
        shelf = {};
        highPass = {};
        meanSquare = 0.0f;
    }

    /**
        The window coefficient for a given rate, built once per block.

        This is a function of the sample rate alone - the 400 ms window is fixed -
        so recomputing it inside the per-frame loop was an std::exp per stereo frame
        per meter, two meters deep, for a constant. The caller builds it in
        prepare() / on a rate change and hands it in.
    */
    static float makeWindowCoefficient (float sampleRate) noexcept
    {
        return j37math::exp (-1.0f / (juce::jmax (1.0f, sampleRate) * 0.4f));
    }

    /** Feeds one stereo frame and returns the current loudness in LUFS. */
    float processFrame (float left, float right, float windowCoefficient) noexcept
    {
        const auto weightedLeft = highPass.process (shelf.process (left));
        const auto weightedRight = highPass.process (shelf.process (right));

        // BS.1770 sums the per-channel mean squares; the channels here are already
        // gain-weighted equally, so it is a plain sum.
        const auto frameMeanSquare = weightedLeft * weightedLeft
                                   + weightedRight * weightedRight;

        // A 400 ms sliding window, implemented as a one-pole that is close enough for
        // a live display while staying cheap and block-size independent.
        const auto coefficient = windowCoefficient;
        meanSquare = coefficient * meanSquare + (1.0f - coefficient) * frameMeanSquare;

        const auto loudness = -0.691f + 10.0f * j37math::log10 (juce::jmax (1.0e-12f, meanSquare));
        return juce::jmax (-70.0f, loudness);
    }

private:
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;

        float process (float x) noexcept
        {
            const auto y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;
            return y;
        }
    };

    Biquad shelf;
    Biquad highPass;
    float meanSquare = 0.0f;
};

//==============================================================================
/**
    Subharmonic generator - the descendant of the fundamental.

    Everything else in this plugin produces OVERtones: harmonics at integer multiples
    of the input frequency. A 100 Hz tone gets 200, 300, 400 Hz and so on. This stage
    is the opposite - it produces the subharmonic series (undertones at 1/2, 1/3, 1/4,
    1/5 of the fundamental), so a 100 Hz note gains weight at 50, 33.3, 25 and 20 Hz,
    with warm analog saturation running in the opposite direction.

    This cannot come out of the saturating curve, and it is worth being clear why,
    because it looks like it should. `tanh (sin (wt))` is a curve applied to a value,
    with no notion of time of its own, so its output is a function of the instantaneous
    input phase - and any such function has period 2pi/w, which means its Fourier
    series contains only multiples of w. Subharmonics need a process with its OWN
    timescale that can fall out of step with the signal.

    On a real machine there are three such processes, and this models all three:

      - Bias leakage. The ultrasonic bias oscillator is not perfectly suppressed on
        playback; the residue weakly modulates the operating point.
      - Domain-wall motion. Magnetic domains flip in groups, and the boundaries
        between them move at a rate that is not locked to the signal. This is the
        best-documented source of subharmonic content in magnetic recording.
      - Scrape flutter. Tape-to-head friction excites the tape's own mechanical
        resonances, modulating the effective head-to-tape speed.

    All three are the same shape mathematically: a slow, signal-dependent modulation
    of the transfer curve. The implementation below uses a phase-locked cascade of
    undertones (1/2, 1/3, 1/4, 1/5) excited by the fundamental bass envelope.
    Drive excites both the depth and the saturation of the subharmonics, producing
    warm inter-harmonic body and massive low-end weight while staying strictly
    locked to what is playing.

    This lives in the header rather than in an anonymous namespace in the .cpp because
    the processor holds two of them by value as members, and a member's type has to be
    visible where the class is declared.
*/
struct SubharmonicGenerator
{
    // An eight-stage undertone cascade. The stages produce the subharmonic series:
    // fundamental / 2, / 3, / 4, / 5, / 6, / 7, / 8, and / 9. Every stage is a pure
    // sinusoid: this block is a generator of partials, and nothing in it is allowed
    // to make a harmonic of one.
    //
    // For a fundamental note that allows them (e.g. >= 140 Hz):
    //   - Stage 0 (/2): -1 octave (foundational sub-bass weight)
    //   - Stage 1 (/3): -1 octave + 5th down (low-mid harmonic thickness)
    //   - Stage 2 (/4): -2 octaves down (deep sub rumble)
    //   - Stage 3 (/5): -2 octaves + major 3rd down (warm undertone)
    //   - Stage 4 (/6): -2 octaves + 5th down (sub-bass density)
    //   - Stage 5 (/7): -2 octaves + harmonic 7th down (extended sub warmth)
    //   - Stage 6 (/8): -3 octaves down (extreme low weight)
    //   - Stage 7 (/9): -3 octaves + major 2nd down (sub-boundary reinforcement)
    //
    // The relative level of the eight is fixed by `baseWeights` below: exactly -6 dB
    // per step of division, so the series always reads as a descending staircase with
    // 1/2 on top. See the weighting block in process() for why that is spelled out in
    // the dividers rather than in the stage index.
    //
    // Frequency-aware audibility:
    // When the input fundamental is very low (e.g. 20 - 40 Hz), dividing by 4, 5, 6, 7, 8, 9
    // would produce inaudible subsonic DC (< 14 Hz) that strains speakers and ruins headroom.
    // Each stage checks its actual synthesized frequency and smoothly rolls off between
    // 22 Hz and 14 Hz. Thus, when the signal allows (upper bass / low mids), all 8 stages
    // are fully active; when the note is already deep sub-bass, stages below the audible
    // limit fade out gracefully.
    static constexpr int numStages = 8;
    static constexpr int dividers[numStages] { 2, 3, 4, 5, 6, 7, 8, 9 };

    // 2^-(divider - 1): 1/2 at 0 dB, 1/3 at -6, 1/4 at -12 ... 1/9 at -42.
    static constexpr float baseWeights[numStages] { 0.5f, 0.25f, 0.125f, 0.0625f,
                                                     0.03125f, 0.015625f, 0.0078125f,
                                                     0.00390625f };

    // AC coupling in front of the detector. The magnetic shaper that excites this
    // stage is asymmetric on purpose, and an asymmetric transfer curve carries an
    // offset. An envelope follower cannot tell an offset from a quiet note, so
    // without this the undertones hold their level forever - a permanent drone at
    // the last tracked pitch with nothing playing. The corner sits below the
    // lowest fundamental the detector accepts, so no undertone this stage can
    // produce is touched by it.
    float detectorDcX = 0.0f;
    float detectorDcY = 0.0f;
    float detectorDcR = 0.9993f;

    // Detector filter states (2-pole Butterworth low-pass at ~260 Hz).
    // Filters out upper harmonics, cymbals, guitars and noise so that cycle
    // detection locks cleanly onto the true fundamental bass note.
    float lp1 = 0.0f;
    float lp2 = 0.0f;

    // Follower tracking the dynamic envelope of the RAW input signal.
    // Scales the generated subharmonics so they breathe with the music and
    // decay smoothly to silence.
    float detPeak = 0.0f;
    float peakTrack = 0.0f;  // tracks raw input envelope (not filtered)

    // Presence follower on the same band, with a deliberately much faster release
    // than detPeak above. The two do different jobs: detPeak sets how loud the
    // undertones are under a note that is playing, and it may take its time doing
    // that. `presence` answers a different question - is there still anything in
    // the fundamental band for the undertones to be derived from at all - and when
    // the answer turns out to be no, the answer has to be delivered at once.
    //
    // Without it the undertones outlived the note by a fixed musical release time
    // (30 ms of envelope plus the time the DC blocker and the tape filters need to
    // settle), which reads on an analyser as the stage still generating with
    // nothing playing - and the deeper the tracked pitch, the more audible the
    // rumble. It is a follower rather than a threshold, so it follows a note
    // decaying into its own tail instead of cutting at a fixed level.
    float presence = 0.0f;

    // Period tracker and Schmitt-trigger zero crossing detector
    float period = 441.0f;
    float samplesSinceCrossing = 0.0f;
    float prevDet = 0.0f;
    bool armed = true;
    int crossingCount = 0;

    // Running phase per subharmonic stage in [0, 1)
    float phases[numStages] {};

    // Cached sample-rate-dependent filter coefficients
    float cachedSampleRate = 0.0f;
    float lpCoeff = 0.0f;
    float attackCoeff = 0.0f;
    float releaseCoeff = 0.0f;
    float presenceAttackCoeff = 0.0f;
    float presenceReleaseCoeff = 0.0f;

    void reset() noexcept
    {
        lp1 = 0.0f;
        lp2 = 0.0f;
        detPeak = 0.0f;
        presence = 0.0f;
        period = 441.0f;
        samplesSinceCrossing = 0.0f;
        prevDet = 0.0f;
        armed = true;
        crossingCount = 0;
        cachedSampleRate = 0.0f;
        detectorDcX = 0.0f;
        detectorDcY = 0.0f;

        for (int s = 0; s < numStages; ++s)
            phases[s] = 0.0f;
    }

    void updateSampleRate (float currentSampleRate) noexcept
    {
        if (std::abs (currentSampleRate - cachedSampleRate) < 0.1f)
            return;

        cachedSampleRate = currentSampleRate;
        const float safeRate = juce::jmax (1.0f, currentSampleRate);
        lpCoeff = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 260.0f / safeRate);
        attackCoeff = 1.0f - std::exp (-1.0f / (safeRate * 0.003f));

        // 30 ms. The follower's job is to keep the undertones in proportion with the
        // note that is playing, and this is a musical release: a note that stops
        // cleanly should not have its undertones cut off under it.
        releaseCoeff = 1.0f - std::exp (-1.0f / (safeRate * 0.030f));

        // Presence, by contrast, is 3 ms up and 6 ms down. Slow enough that the
        // undertones fade in with a note instead of arriving ahead of it, and fast
        // enough that they are gone within a frame of the band going quiet.
        presenceAttackCoeff = 1.0f - std::exp (-1.0f / (safeRate * 0.003f));
        presenceReleaseCoeff = 1.0f - std::exp (-1.0f / (safeRate * 0.006f));

        // 5 Hz one-pole AC coupling, in the same form the playback DC blocker uses.
        // The detector accepts fundamentals from 15 Hz up, so this removes any
        // offset without touching anything the stage is trying to track.
        detectorDcR = juce::jlimit (0.5f, 0.9999f,
                                    1.0f - (juce::MathConstants<float>::twoPi * 5.0f) / safeRate);
    }

    /**
        Feeds one sample and returns the multi-frequency subharmonic component: a
        phase-locked series of pure sinusoids at f0/2, /3, /4 ... /9.

        `driveAmount` tilts the series towards its deep end, opening the lower
        dividers without ever reordering the staircase.
        `depth` scales the overall injected subharmonic level.
    */
    float process (float x, float driveAmount, float depth, float sampleRate) noexcept
    {
        if (depth <= 0.0f || ! std::isfinite (x))
            return 0.0f;

        updateSampleRate (sampleRate);
        const float safeRate = juce::jmax (1.0f, sampleRate);

        // ----------------------------------------------------------------------
        //  1. AC coupling, before anything reads the signal.
        //
        //  The shaper that excites this stage is asymmetric by design, and an
        //  asymmetric transfer curve carries an offset. A constant is not silence
        //  to an envelope follower: with one still in the detector path the
        //  undertones held their level indefinitely, droning at the last tracked
        //  pitch with nothing playing at all. The shaper no longer emits an
        //  offset at silence - its curve is pinned through the origin - but the
        //  follower must never be given an offset by anything, and this is the
        //  place that guarantees it. The corner sits below the lowest fundamental
        //  the detector accepts, so no undertone this stage can produce is
        //  touched by it.
        // ----------------------------------------------------------------------
        const float acCoupled = x - detectorDcX + detectorDcR * detectorDcY;
        detectorDcX = x;
        detectorDcY = acCoupled;

        // ----------------------------------------------------------------------
        //  2. Two-pole low-pass filter (~260 Hz) on detector path.
        //  Isolates the fundamental bass note from highs and overtones so the
        //  cycle detector never mistriggers on treble content.
        // ----------------------------------------------------------------------
        lp1 += (acCoupled - lp1) * lpCoeff;
        lp2 += (lp1 - lp2) * lpCoeff;
        const float det = lp2;

        // ----------------------------------------------------------------------
        //  3. Dynamic envelope follower of the fundamental bass band.
        //  Ensures level proportionality: quiet notes get quiet subharmonics,
        //  and silence decays cleanly without droning.
        //
        //  The presence follower below runs first and is not subject to the early
        //  exit, because it has to keep seeing the band in order to know the band
        //  has gone. Freezing it while the stage is silent would leave it holding
        //  whatever it had reached before the note stopped, and the next note
        //  would fade in from a stale level.
        // ----------------------------------------------------------------------
        const float absDet = std::abs (det);

        presence += (absDet - presence) * (absDet > presence ? presenceAttackCoeff
                                                             : presenceReleaseCoeff);

        detPeak += (absDet - detPeak) * (absDet > detPeak ? attackCoeff : releaseCoeff);

        // The floor is the same one detPeak is measured against, so the gate opens
        // and closes exactly where the follower does rather than at a level of its
        // own: below it there is no note, so there is nothing to double.
        constexpr float silenceFloor = 1.0e-4f;
        const float presenceGain = juce::jlimit (0.0f, 1.0f, presence / silenceFloor);

        if (detPeak < silenceFloor || presenceGain <= 0.0f)
            return 0.0f;

        // ----------------------------------------------------------------------
        //  4. Schmitt-trigger crossing detector with hysteresis.
        //  Measures fundamental period and triggers Phase-Locked Loop (PLL).
        // ----------------------------------------------------------------------
        samplesSinceCrossing += 1.0f;
        const float triggerThreshold = detPeak * 0.10f;

        if (det > triggerThreshold && prevDet <= triggerThreshold && armed)
        {
            armed = false;
            // Valid fundamental range: 15 Hz up to 500 Hz
            const float minPeriod = safeRate / 500.0f;
            const float maxPeriod = safeRate / 15.0f;

            if (samplesSinceCrossing >= minPeriod && samplesSinceCrossing <= maxPeriod)
                period += (samplesSinceCrossing - period) * 0.25f;

            samplesSinceCrossing = 0.0f;
            ++crossingCount;

            // Phase-Locked Loop (PLL): gently align phase to the crossing boundary
            // to ensure zero phase drift over extended playback.
            for (int s = 0; s < numStages; ++s)
            {
                const int d = dividers[s];
                const float targetPhase = static_cast<float> (crossingCount % d) / static_cast<float> (d);
                float phaseError = targetPhase - phases[s];
                if (phaseError > 0.5f)  phaseError -= 1.0f;
                if (phaseError < -0.5f) phaseError += 1.0f;
                phases[s] += phaseError * 0.10f;
                if (phases[s] >= 1.0f) phases[s] -= 1.0f;
                if (phases[s] < 0.0f)  phases[s] += 1.0f;
            }
        }
        else if (det < -triggerThreshold)
        {
            armed = true;
        }
        prevDet = det;

        // ----------------------------------------------------------------------
        //  5. Continuous phase advancement and subharmonic synthesis.
        // ----------------------------------------------------------------------
        const float safePeriod = juce::jlimit (safeRate / 500.0f, safeRate / 15.0f, period);
        const float baseStep = 1.0f / safePeriod;
        const float trackedFundamentalHz = safeRate / safePeriod;

        const float clampedDrive = juce::jlimit (0.0f, 1.0f, driveAmount);

        float sum = 0.0f;
        float weightSum = 0.0f;

        for (int s = 0; s < numStages; ++s)
        {
            phases[s] += baseStep / static_cast<float> (dividers[s]);
            if (phases[s] >= 1.0f)
                phases[s] -= 1.0f;

            const float subFreq = trackedFundamentalHz / static_cast<float> (dividers[s]);

            // Low-frequency audibility window:
            // Stages that fall below ~14 - 22 Hz are smoothly attenuated so that
            // inaudible, speaker-straining infrasonic DC is never generated.
            // When f0 is high enough (e.g. > 140 Hz), all 8 subharmonics are fully active.
            // When f0 is very low (e.g. 20 Hz), inaudible stages fade out naturally.
            float audibility = 0.0f;
            if (subFreq >= 22.0f)
            {
                audibility = 1.0f;
            }
            else if (subFreq > 14.0f)
            {
                const float t = (subFreq - 14.0f) / (22.0f - 14.0f);
                audibility = t * t * (3.0f - 2.0f * t); // smoothstep
            }

            if (audibility <= 0.0f)
                continue;

            // Stage weighting. The series is a STAIRCASE, and the law is written in
            // terms of the DIVIDER rather than the stage index so that it cannot
            // invert: a partial at f0/d is fed at 2^-(d-1), which is exactly -6 dB for
            // every step of division - 1/2 on top, 1/3 at -6 dB, 1/4 at -12 dB, down
            // to 1/9 some 42 dB under the octave.
            //
            // The previous law pinned stage 0 at 1.0 and weighted the rest by
            // 1/(stage + 1), which is the same as 1/(d - 1): it dropped 10 dB from 1/2
            // to 1/3 and then left 1/4 ... 1/9 inside an 8 dB band. On an analyser
            // that is a flat shelf of undertones with one partial perched on top of
            // it rather than a staircase, and it is what the "the deep undertones are
            // the loud ones" report was measuring.
            //
            // DRIVE opens the deep end without ever breaking that order. The tilt
            // grows with the divider, but by at most 1.875x across the whole 2 ... 9
            // range, so no two adjacent stages can swap places at any drive setting.
            const float divider = static_cast<float> (dividers[s]);
            const float w = baseWeights[s] * (1.0f + clampedDrive * (divider - 2.0f) * 0.125f);
            const float effectiveWeight = w * audibility;
            weightSum += effectiveWeight;

            // A pure sinusoid, and deliberately the only waveform this stage ever
            // produces.
            //
            // Each partial used to be run through `tanh` first - "downward
            // saturation", the stage's own description of it. A memoryless
            // waveshaper on a sinusoid is a harmonic generator, and a loud one: at
            // the default DRIVE the 1/2 undertone was returning its own third
            // harmonic only 16 dB down, and DRIVE pushed that to 13 dB, because
            // DRIVE is what opened the waveshaper. That is exactly the reported
            // fault - "the regular harmonics are generated from the subharmonics" -
            // and it was being manufactured here, inside the generator, before
            // anything downstream could be blamed for it.
            //
            // A sine is also the only waveform a phase-locked cascade can use
            // without the stages interfering: any harmonics it adds are at
            // frequencies that belong to no stage's own period, so the series
            // stops being the clean staircase it is supposed to be. DRIVE keeps
            // its musical job - it tilts the weights towards the deep end above -
            // but it no longer shapes the waveform.
            const float osc = std::cos (juce::MathConstants<float>::twoPi * phases[s]);
            sum += osc * effectiveWeight;
        }

        if (weightSum <= 1.0e-4f)
            return 0.0f;

        const float fade = juce::jmin (1.0f, weightSum);
        return (sum / weightSum) * detPeak * depth * fade * presenceGain;
    }
};

//==============================================================================
/**
    Live harmonic analysis of a nonlinear stage.

    The analogue character of this plugin lives in the harmonics its shaper adds, so rather
    than trusting the curve on paper, the stage is measured: Goertzel filters run at the 2nd
    and 3rd harmonic of a tracked fundamental and report how much energy is even (2nd)
    against odd (3rd) relative to it.

    Those two bins are enough to characterise the stage because the split they show is the
    one that matters: even content is what the bias asymmetry contributes and reads as
    warmth, odd content is what the symmetric tanh contributes and reads as edge. Measuring
    more bins would cost more for no extra insight into that balance.

    Two things make this usable as a live meter: it runs only every N samples so the cost is
    negligible, and it uses the shaper's own input and output, so what it reports is the
    distortion that was actually produced, not a prediction.

    This lives in the header rather than in an anonymous namespace in the .cpp because the
    processor holds one by value as a member, and a member's type has to be visible where
    the class is declared.
*/
struct HarmonicAnalyser
{
    void reset() noexcept
    {
        evenRatio = 0.0f;
        oddRatio = 0.0f;
        fundamentalLevel = 0.0f;
        trackedFrequency = 220.0f;
        samplesSinceCrossing = 1;
        sampleCounter = 0;
        previousPositive = true;
        previous1 = 0.0f;
        previous2 = 0.0f;
        lastMagnitude = 0.0f;
        windowCounter = 0;
    }

    /**
        Feeds one sample of the shaper's input and output.

        Return value: true once a fresh harmonic reading has been produced this call, false
        while the measurement is still accumulating or while there is too little signal to
        measure. Callers that only want the running values can use the getters instead and
        ignore the return entirely.
    */
    bool analyse (float shaperInput, float shaperOutput, float sampleRate)
    {
        // The fundamental is taken from the zero-crossing rate of the input, which is cheap
        // and needs no FFT. The estimate is heavily smoothed because it only has to be in
        // the right region for the harmonic bins to line up.
        const auto absInput = std::abs (shaperInput);
        if ((shaperInput >= 0.0f) != previousPositive && absInput > 1.0e-4f)
        {
            // samplesSinceCrossing is a period in SAMPLES, so it is converted to a frequency
            // by dividing the rate. It used to be passed through a jlimit with frequency
            // bounds, which was dimensionally wrong: clamping a sample count against a Hz
            // range silently picked the wrong branch and the estimate only worked because
            // the bounds happened to be wide. The period itself is what needs guarding, so
            // it is clamped to a sane sample range and the resulting frequency is bounded
            // separately below.
            const auto periodSamples = static_cast<float> (
                juce::jlimit (2, juce::jmax (2, static_cast<int> (sampleRate)), samplesSinceCrossing));
            const auto instantFrequency = sampleRate / periodSamples;
            trackedFrequency += (instantFrequency - trackedFrequency) * 0.05f;
            samplesSinceCrossing = 0;
        }

        previousPositive = shaperInput >= 0.0f;
        ++samplesSinceCrossing;

        // Only measure while there is real signal, and only occasionally.
        if (absInput < 1.0e-3f)
        {
            fundamentalLevel += (0.0f - fundamentalLevel) * 0.05f;
            return false;
        }

        // Measure at a fixed RATE rather than every fixed number of samples, so the update
        // frequency of the readout is the same at 44.1 kHz and 192 kHz. At a fixed stride
        // the analyser would run four times more often per second on a 192 kHz session,
        // costing four times as much for a display that updates at 30 Hz regardless.
        const auto stride = juce::jmax (16, juce::roundToInt (sampleRate / 700.0f));

        if (++sampleCounter < stride)
            return true;

        sampleCounter = 0;

        // Bound the tracked frequency to a range that is valid at any sample rate. The lower
        // edge is a musical floor and the upper edge is kept clear of Nyquist, and the
        // maximum is taken with jmax so the two can never cross over - passing inverted
        // bounds to jlimit would return an undefined value rather than the nearest limit.
        const auto maxFrequency = juce::jmax (60.0f, sampleRate * 0.45f);
        const auto frequency = juce::jlimit (30.0f, maxFrequency, trackedFrequency);

        // The output is captured explicitly: a lambda has no access to the enclosing
        // function's parameters unless they are named in the capture list.
        const auto ratioAt = [this, shaperOutput, frequency, sampleRate, maxFrequency] (float bin)
        {
            if (bin * frequency >= maxFrequency)
                return 0.0f;

            return std::abs (goertzel (shaperOutput, bin * frequency, sampleRate));
        };

        const auto fundamental = juce::jmax (1.0e-6f, std::abs (goertzel (shaperInput,
                                                                         frequency, sampleRate)));
        const auto second = ratioAt (2.0f);
        const auto third = ratioAt (3.0f);

        // Relative to the fundamental, so the reading is meaningful at any level: this is a
        // distortion ratio, not an absolute power.
        const auto even = second / fundamental;
        const auto odd = third / fundamental;

        const auto smoothing = 0.15f;
        evenRatio += (even - evenRatio) * smoothing;
        oddRatio += (odd - oddRatio) * smoothing;
        fundamentalLevel += (fundamental - fundamentalLevel) * smoothing;

        return true;
    }

    /** Second-harmonic content relative to the fundamental - warmth and body. */
    float getEvenRatio() const noexcept { return evenRatio; }

    /** Third-harmonic content relative to the fundamental - edge and density. */
    float getOddRatio() const noexcept { return oddRatio; }

    /** How much level the shaper saw, so the display can dim when there is no signal. */
    float getFundamentalLevel() const noexcept { return fundamentalLevel; }

private:
    /**
        Single-bin magnitude estimate, evaluated over the most recent window so it is
        independent of the block size. Used instead of an FFT because only a couple of bins
        are needed and this costs a fraction of a full transform.
    */
    float goertzel (float sample, float frequency, float sampleRate) noexcept
    {
        const auto omega = juce::MathConstants<float>::twoPi * frequency / sampleRate;
        const auto coefficient = 2.0f * std::cos (omega);

        // The window length is a fixed TIME, not a fixed number of samples, so the bin width
        // stays the same at every supported rate. A fixed sample count would make the window
        // shrink with the sample rate - at 192 kHz 512 samples is only 2.7 ms and the bin
        // width balloons to 375 Hz, which is wider than the 1 kHz gap between harmonics and
        // makes the even/odd split meaningless.
        //
        // 11.6 ms is the window that 512 samples gives at 44.1 kHz, so the behaviour at the
        // lower rates is unchanged and the higher ones now match it.
        const auto samplesPerWindow = juce::jmax (64, juce::roundToInt (0.0116 * sampleRate));

        const auto current = sample + coefficient * previous1 - previous2;
        previous2 = previous1;
        previous1 = current;

        // The window is reset periodically rather than run forever, which keeps the
        // recurrence from accumulating numerical error over a long session.
        if (++windowCounter >= samplesPerWindow)
        {
            // Power at the bin, from the final two states of the recurrence.
            const auto power = previous1 * previous1 + previous2 * previous2
                             - coefficient * previous1 * previous2;

            previous1 = 0.0f;
            previous2 = 0.0f;
            windowCounter = 0;

            lastMagnitude = std::sqrt (juce::jmax (0.0f, power))
                          / static_cast<float> (samplesPerWindow);
            lastMagnitude = juce::jlimit (0.0f, 4.0f, lastMagnitude);
        }

        return lastMagnitude;
    }

    float trackedFrequency = 220.0f;
    float evenRatio = 0.0f;
    float oddRatio = 0.0f;
    float fundamentalLevel = 0.0f;
    int samplesSinceCrossing = 1;
    int sampleCounter = 0;
    bool previousPositive = true;

    // Goertzel recurrence state and its measurement window.
    float previous1 = 0.0f;
    float previous2 = 0.0f;
    float lastMagnitude = 0.0f;
    int windowCounter = 0;
};

//==============================================================================
/**
    The tape machine. Signal flow, in order:

      input trim -> input glue compressor -> record head (bias + magnetic hysteresis)
      -> tape low-pass and head-gap loss -> tape noise floor and wow/flutter
      -> playback EQ tilt -> output glue compressor -> final gain compensation
      -> output trim -> stereo width
*/
class FirstAudioProcessor  : public juce::AudioProcessor,
                             private juce::AudioProcessorValueTreeState::Listener
{
public:
    //==============================================================================
    FirstAudioProcessor();
    ~FirstAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRateToUse, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    /** The tape engine proper; processBlock routes into this, oversampled or not. */
    void processTapeEngine (juce::dsp::AudioBlock<float>, juce::MidiBuffer&);

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState parameters;

    //==============================================================================
    //  Premium workflow: oversampling, factory presets, A/B compare, undo/redo.

    /** Oversampling quality selector, shown to the host as a parameter too. */
    enum class OversamplingFactor : int
    {
        off = 0,
        x2  = 1,
        x4  = 2,
        x8  = 3
    };

    OversamplingFactor getOversamplingFactor() const noexcept
    {
        return currentOversampling;
    }

    /** Undo manager shared with the editor (wired to Ctrl+Z / Ctrl+Y there). */
    juce::UndoManager& getUndoManager() noexcept { return undoManager; }

    /** Number of factory presets. */
    static constexpr int numFactoryPresets = 18;

    /** Display names of the factory presets, in order. */
    static juce::StringArray getPresetNames();

    /** Applies factory preset `index` (0..numFactoryPresets-1) with one undo transaction. */
    void applyFactoryPreset (int index);

    /** The index of the last factory preset the user (or a session load) selected. */
    int getLastPresetIndex() const noexcept { return lastPresetIndex.load (std::memory_order_relaxed); }

    //==============================================================================
    //  User presets - stored on disk next to the factory list.
    //
    //  A factory preset covers the machine's range; a USER preset freezes the whole
    //  machine exactly as it stands, including anything the factory list has no row
    //  for. Files live in <user app data>/Nonlin Analog Saturator/Presets with a
    //  .nonlinpreset extension, so they survive plugin updates and are shared by
    //  every instance.
    //==============================================================================

    /** Names of the user presets found on disk, sorted alphabetically. */
    juce::StringArray getUserPresetNames() const;

    /** Saves the current full machine state as a user preset. Returns true on success. */
    bool saveUserPreset (const juce::String& name);

    /** Recalls a user preset by name as one undoable transaction. Returns true on success. */
    bool applyUserPreset (const juce::String& name);

    /** Deletes a user preset file from disk. Returns true if the file existed. */
    bool deleteUserPreset (const juce::String& name);

    /** The user preset currently loaded, or an empty string when none is active. */
    juce::String getCurrentPresetName() const { return currentPresetName; }

    /** True when the live state has drifted from the loaded (factory or user) preset. */
    bool isPresetDirty() const noexcept { return presetDirty.load (std::memory_order_relaxed); }

    /** Stores the current settings into slot A or B (0 = A, 1 = B). */
    void copyToCompareSlot (int slot);

    /** Recalls slot A or B (0 = A, 1 = B) and swaps the active side. */
    void toggleCompare();

    /** The A/B side that is currently live (0 = A, 1 = B). */
    int getActiveCompareSlot() const noexcept { return activeSlot.load (std::memory_order_relaxed); }

    /** True when the settings in the two slots differ (drives the edited dot). */
    bool isCompareDirty() const noexcept { return compareDirty.load (std::memory_order_relaxed); }

    /** Stores the current settings into whichever slot is currently live. */
    void updateActiveCompareSlot();

    /** Swaps the whole APVTS state. Public because the undoable StateSwapAction
        (an anonymous-namespace type in the .cpp, so it cannot be befriended) calls
        it from perform() / undo(); every other caller should go through the
        UndoManager instead. */
    void replaceParameterState (const juce::ValueTree& newState);

    //==============================================================================
    //  Live telemetry published by the audio thread and consumed by the editor.
    //  Peaks are exchanged (consumed) by the meter; the rest are plain readings.
    float getInputPeakLevel() noexcept { return inputPeakLevel.exchange (0.0f, std::memory_order_relaxed); }
    float getInputRmsLevel() const noexcept { return inputRmsLevel.load (std::memory_order_relaxed); }
    float getOutputPeakLevel() noexcept { return outputPeakLevel.exchange (0.0f, std::memory_order_relaxed); }
    float getOutputRmsLevel() const noexcept { return outputRmsLevel.load (std::memory_order_relaxed); }

    // Input-side loudness, measured on the same four views as the output so the two
    // meters can be read against each other.
    float getInputPeakDb() const noexcept { return inputPeakDb.load (std::memory_order_relaxed); }
    float getInputRmsDb() const noexcept { return inputRmsDb.load (std::memory_order_relaxed); }
    float getInputLufs() const noexcept { return inputLufs.load (std::memory_order_relaxed); }
    float getInputVuDb() const noexcept { return inputVuDb.load (std::memory_order_relaxed); }
    float getInputCombinedDb() const noexcept { return inputCombinedDb.load (std::memory_order_relaxed); }
    bool isInputClipping() const noexcept { return inputClipping.load (std::memory_order_relaxed); }

    //==============================================================================
    //  Four-way loudness metering.
    //
    //  The meters panel shows the same signal four different ways, because each one
    //  answers a different question and no single scale is right for all of them:
    //
    //    RMS   - the honest electrical average, the engineer's baseline
    //    LUFS  - K-weighted, so it reflects perceived loudness rather than volts
    //    VU    - the classic 300 ms ballistic average, deliberately slower and forgiving
    //    dB    - peak dBFS, the only one that tells you about clipping
    //
    //  The combined reading weights each contribution equally (25 % each), which makes
    //  it deliberately blind to the weaknesses of any one scale: peak alone would jump
    //  on transients, LUFS alone would ignore them, VU alone would smooth too much.
    //==============================================================================
    float getOutputPeakDb() const noexcept { return outputPeakDb.load (std::memory_order_relaxed); }
    float getOutputRmsDb() const noexcept { return outputRmsDb.load (std::memory_order_relaxed); }
    float getOutputLufs() const noexcept { return outputLufs.load (std::memory_order_relaxed); }
    float getOutputVuDb() const noexcept { return outputVuDb.load (std::memory_order_relaxed); }

    /** Equal-weighted (25 % each) blend of the four loudness views, in dB. */
    float getOutputCombinedDb() const noexcept { return outputCombinedDb.load (std::memory_order_relaxed); }

    /** True while the output is clipping, for the meter's peak lamp. */
    bool isOutputClipping() const noexcept { return outputClipping.load (std::memory_order_relaxed); }

    //==============================================================================
    //  Harmonic character telemetry.
    //
    //  These report what the tape shaper is measurably producing, as ratios against the
    //  fundamental. They are the honest answer to "is this adding the right kind of
    //  distortion": even harmonics are warmth and body, odd harmonics are edge and
    //  density, and looking like analogue tape means having both with even content
    //  present rather than pure odd-order harshness.
    //==============================================================================
    float getEvenHarmonicRatio() const noexcept { return evenHarmonicRatio.load (std::memory_order_relaxed); }
    float getOddHarmonicRatio() const noexcept { return oddHarmonicRatio.load (std::memory_order_relaxed); }

    /** Gain reduction of the input stage compressor in dB (always <= 0). */
    float getInputGainReductionDb() const noexcept { return inputGainReductionDb.load (std::memory_order_relaxed); }

    /** Gain reduction of the output stage compressor in dB (always <= 0). */
    float getOutputGainReductionDb() const noexcept { return outputGainReductionDb.load (std::memory_order_relaxed); }

    /** Detector activity of the input stage compressor, 0..1, for its own meter. */
    float getInputCompressorActivity() const noexcept { return inputCompressorActivity.load (std::memory_order_relaxed); }

    /** Detector activity of the output stage compressor, 0..1, for its own meter. */
    float getOutputCompressorActivity() const noexcept { return outputCompressorActivity.load (std::memory_order_relaxed); }

    /** Total gain reduction of both glue stages in dB (always <= 0). */
    float getGainReductionDb() const noexcept
    {
        return juce::jlimit (-24.0f, 0.0f,
                             inputGainReductionDb.load (std::memory_order_relaxed)
                             + outputGainReductionDb.load (std::memory_order_relaxed));
    }

    /** Envelope of the tape glue compressors as a 0..1 linear activity value. */
    float getCompressorActivity() const noexcept { return compressorActivity.load (std::memory_order_relaxed); }

    /** Instantaneous transport drift (wow/flutter), normalised to 0..1 around 0.5. */
    float getTransportDrift() const noexcept { return transportDrift.load (std::memory_order_relaxed); }

    /** Tone macro (tape/speed character crossfade), 0..1, for the editor. */
    float getToneMacro() const noexcept { return characterParam != nullptr ? characterParam->load() : 0.0f; }

    /** Harmonic weight of the last block, 0..1, used for UI colour animation. */
    float getHarmonicCharacter() const noexcept { return harmonicCharacter.load (std::memory_order_relaxed); }

    /** True when the last processed block was fully bypassed. */
    bool isBypassed() const noexcept { return bypassActive.load (std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** Rebuilds every time-domain constant from the current sample rate. */
    void resetSampleRateDependentState();

    /** Recomputes the cached tone filter coefficients for the current rate. */
    void updateToneCoefficients (float toneValue, float engineSampleRate);

    /** (Re)creates the oversampling engine for the requested factor and reports latency. */
    void setOversamplingFactor (OversamplingFactor factor, int samplesPerBlock);

    /** Pure function of the parameter snapshot - the factory preset table. */
    static std::map<juce::String, float> factoryPresetValues (int index);

    /** Applies a raw value map through the APVTS, wrapped in one undo transaction. */
    void applyParameterValues (const std::map<juce::String, float>& values,
                               const juce::String& undoTransactionName);

    /** Applies a state tree as a single undoable transaction. */
    void applyStateWithUndo (const juce::ValueTree& targetState, const juce::String& transactionName);

    /** The directory user presets are read from and written to (created on demand). */
    static juce::File getUserPresetDirectory();

    /** Restores the badge to a clean, named preset state (used after a preset load). */
    void markPresetClean (const juce::String& name)
    {
        currentPresetName = name;
        presetNameNonEmpty.store (! name.isEmpty(), std::memory_order_relaxed);
        presetDirty.store (false, std::memory_order_relaxed);
    }

    /** AudioProcessorValueTreeState::Listener: any parameter change dirties the badge. */
    void parameterChanged (const juce::String&, float) override
    {
        // Reads and writes only atomics: APVTS forwards host automation here from the
        // audio thread, and the badge is advisory state, never control state.
        if (presetNameNonEmpty.load (std::memory_order_relaxed))
            presetDirty.store (true, std::memory_order_relaxed);
    }

    /** Rebuilds the A/B dirty flag from the two stored slot states. */
    void updateCompareDirty();

    // Cached parameter pointers: avoids repeated string lookups on the audio thread.
    std::atomic<float>* inputDbParam = nullptr;
    std::atomic<float>* driveParam = nullptr;
    std::atomic<float>* biasParam = nullptr;
    std::atomic<float>* toneParam = nullptr;
    std::atomic<float>* characterParam = nullptr;
    std::atomic<float>* wowParam = nullptr;
    std::atomic<float>* flutterParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* outputDbParam = nullptr;
    std::atomic<float>* widthParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* deltaParam = nullptr;
    std::atomic<float>* oversamplingParam = nullptr;
    std::atomic<float>* tapeTypeParam = nullptr;
    std::atomic<float>* speedParam = nullptr;
    std::atomic<float>* instrumentParam = nullptr;
    std::atomic<float>* polarityParam = nullptr;
    std::atomic<float>* autoGainParam = nullptr;
    std::atomic<float>* subFundamentalParam = nullptr;
    std::atomic<float>* delayTimeParam = nullptr;
    std::atomic<float>* delayFeedbackParam = nullptr;
    std::atomic<float>* stOffsetParam = nullptr;
    std::atomic<float>* noiseParam = nullptr;
    std::atomic<float>* transportParam = nullptr;
    std::atomic<float>* blendParam = nullptr;
    std::atomic<float>* shapeParam = nullptr;
    std::atomic<float>* sagParam = nullptr;
    std::atomic<float>* presenceParam = nullptr;
    std::atomic<float>* cabinetParam = nullptr;
    std::atomic<float>* ampBiasParam = nullptr;

    float sampleRate = 44100.0f;
    // Every smoother below is advanced exactly once at the top of each sample frame.
    // The values are then reused for both channels, so a stereo block cannot advance
    // a control ramp twice or leave it frozen at its initial coefficient.
    SampleClock sampleClock;
    SampleSmoother inputGainSmoothed { sampleClock, true };
    SampleSmoother outputGainSmoothed { sampleClock };
    // MIX is the control itself, not its inverse. What this returns is the MIX
    // position, and the crossfade in processTapeEngine is built from it directly -
    // dry = cos (angle), wet = sin (angle) - so 0 % is dry at unity and 100 % is
    // wet at unity. The 0.5 initial value is the neutral centre of the 0..1 space
    // the ramp is fed in after the percentage is scaled down.
    //
    // There is deliberately no invertOutput flag here, and there must not be one.
    // There used to be, and it was not a design decision: it was a patch over a
    // crossfade that had sin() on the dry side and cos() on the wet one, and the
    // two cancelled so the control happened to read correctly. Correcting the
    // expression to dry = cos, wet = sin - which is what an equal-power MIX is -
    // turned that patch into a second inversion and swapped the two ends of the
    // control: MIX 0 became fully wet and MIX 100 fully dry. The formula and the
    // flag were a pair that had to move together with nothing tying them together.
    // The formula is right on its own now, so there is no flag left to keep.
    SampleSmoother mixSmoothed { sampleClock, false, false, 0.5f };
    SampleSmoother widthSmoothed { sampleClock };
    SampleSmoother bypassSmoothed { sampleClock };

    std::atomic<float> inputPeakLevel { 0.0f };
    std::atomic<float> inputRmsLevel { 0.0f };
    std::atomic<float> inputPeakDb { -70.0f };
    std::atomic<float> inputRmsDb { -70.0f };
    std::atomic<float> inputLufs { -70.0f };
    std::atomic<float> inputVuDb { -70.0f };
    std::atomic<float> inputCombinedDb { -70.0f };
    std::atomic<bool> inputClipping { false };
    std::atomic<float> outputPeakLevel { 0.0f };
    std::atomic<float> outputRmsLevel { 0.0f };
    std::atomic<float> outputPeakDb { -70.0f };
    std::atomic<float> outputRmsDb { -70.0f };
    std::atomic<float> outputLufs { -70.0f };
    std::atomic<float> outputVuDb { -70.0f };
    std::atomic<float> outputCombinedDb { -70.0f };
    std::atomic<bool> outputClipping { false };
    std::atomic<float> evenHarmonicRatio { 0.0f };
    std::atomic<float> oddHarmonicRatio { 0.0f };
    std::atomic<float> inputGainReductionDb { 0.0f };
    std::atomic<float> outputGainReductionDb { 0.0f };
    std::atomic<float> inputCompressorActivity { 0.0f };
    std::atomic<float> outputCompressorActivity { 0.0f };
    std::atomic<float> compressorActivity { 0.0f };
    std::atomic<float> transportDrift { 0.5f };
    std::atomic<float> harmonicCharacter { 0.0f };
    std::atomic<bool> bypassActive { false };

    // -----------------------------------------------------------------------
    //  Premium workflow state.
    // -----------------------------------------------------------------------
    // Oversampling engines, one per factor. The OFF entry exists so the editor
    // combo can always call setOversamplingFactor with an OwnedArray index without
    // special-casing; the dummy stage passes audio through bit-for-bit (and its
    // latency is zero, so no host compensation is needed).
    juce::OwnedArray<juce::dsp::Oversampling<float>> oversamplers;
    OversamplingFactor currentOversampling = OversamplingFactor::off;

    // The block size the engines were last built for, and the rate multiplier of the
    // ACTIVE engine. processBlock picks a rebuild size from lastBlockSize rather than
    // buffer.getNumSamples(), which on the oversampled path returns the wrong rate's
    // sample count and would oscillate between rebuilds every block.
    int lastBlockSize = 512;
    float oversamplingRateFactor = 1.0f;

    // The two A/B slots hold full parameter states; the live side is the one the
    // engine is currently rendering. Slot recall swaps state, never sample data.
    juce::ValueTree compareSlots[2];
    std::atomic<int> activeSlot { 0 };
    std::atomic<bool> compareDirty { false };

    // Undo history for every parameter change the editor makes. Parameter gestures
    // from hosts are NOT recorded, so automation stays authoritative.
    juce::UndoManager undoManager;

    // The last factory preset selection, published so the editor combo can restore
    // its display after a preset or session change.
    std::atomic<int> lastPresetIndex { -1 };

    // User-preset bookkeeping: the name of the preset currently loaded (empty when
    // none is), and whether the live state has drifted away from it since. The flag
    // is re-armable cheaply because it is only advisory (a UI badge) - the true state
    // is the parameters themselves.
    juce::String currentPresetName;
    std::atomic<bool> presetDirty { false };
    std::atomic<bool> presetNameNonEmpty { false };

    // Per-channel tape state: 3-element hysteresis memory (current, previous, older)
    // plus a 3-element high-frequency memory holding the tape-medium pole, the
    // per-model head-damping pole and the TONE-macro head-gap pole, in chain order.
    std::array<float, 3> hystL {};
    std::array<float, 3> hystR {};
    std::array<float, 3> highFreqL {};
    std::array<float, 3> highFreqR {};

    // One-pole state for the hiss band-limit, per channel. Kept separate from the tape
    // filters so the noise colour cannot drift when the tone control moves.
    float hissLowPassL = 0.0f;
    float hissLowPassR = 0.0f;

    float wowPhaseL = 0.0f;
    float wowPhaseR = 0.0f;
    float flutterPhaseL = 0.0f;
    float flutterPhaseR = 0.0f;

    float previousTone = -1.0f;
    float toneLpAc = 0.0f;

    // TONE macro machine state, cached by updateToneCoefficients(). These were used by
    // the .cpp without being declared here, which is what broke the build (C2065).
    //   previousCharacter - last seen TONE value, so the cache is rebuilt only on change
    //   headGapHz         - playback head-gap corner the macro fades between 24 kHz and 4.3 kHz
    //   preDriveGain      - slow-machine pre-bias lift folded into the record head drive
    //   flutterScale      - fast-machine shimmer multiplier on the flutter depth
    float previousCharacter = -1.0f;
    float headGapHz = 24000.0f;
    float preDriveGain = 1.0f;
    float flutterScale = 1.0f;

    // BRIGHTNESS playback TILT, cached with the other coefficients: a fixed 1.6 kHz
    // pivot low-passes the wet signal itself, and a matched gain PAIR follows the
    // Brightness control - the band above the pivot and the band below move in
    // opposite directions - removal is capped at a gentle ~4 dB while the
    // opposite band opens up to +15 dB - with BOTH gains exactly
    // unity at the 50 percent pivot. Both gains are per-sample scalars carried by
    // smoothers, so the tilt can never step the waveform.
    float toneShelfCoefficient = 0.5f;
    float toneShelfGain = 1.0f;   // gain applied to the band BELOW the pivot
    float toneShelfBoost = 1.0f;  // gain applied to the band ABOVE the pivot
    // One state per channel, like every other filter in the engine. As a single
    // float it was shared: the left channel filtered into it, and the right
    // channel then carried on from where the left had left off. That is
    // crosstalk rather than a stereo shelf - a signal on one side reappears on
    // the other 8 kHz up, half a frame late - and the shelf's attack and release
    // run at twice the rate in stereo that they run in mono.
    std::array<float, 2> toneShelfSplit {};  // pivot low-pass state, per channel

    // Smoothed copies of the two coefficients that MULTIPLY the signal from a control:
    // the BRIGHTNESS shelf gain and the TONE macro's record-head pre-bias. Their raw
    // values are rebuilt the instant either knob moves, and feeding a stepped gain
    // straight into the per-sample loop put a discontinuity into the waveform on every
    // block boundary while the knob was dragged - that was the crackle. These ramp over
    // 20 ms instead, so the control still feels immediate but never steps the signal.
    SampleSmoother toneShelfGainSmoothed { sampleClock, false, false, 1.0f };
    SampleSmoother toneShelfBoostSmoothed { sampleClock, false, false, 1.0f };
    SampleSmoother preDriveGainSmoothed { sampleClock, false, false, 1.0f };

    // The two arguments that shape the magnetic curve itself. BIAS was the loudest
    // control to move because its asymmetry term is a DC OFFSET added straight into the
    // tanh and then subtracted again at the output - stepping it does not just change
    // gain, it shifts the whole transfer curve, and the playback DC blocker downstream
    // then has to swallow the resulting step. That is why BIAS thumped far harder than
    // any linear control. Both arguments ramp over 20 ms like the other gains, so the
    // curve morphs continuously instead of jumping.
    SampleSmoother shaperDriveSmoothed { sampleClock };
    SampleSmoother shaperAsymmetrySmoothed { sampleClock };

    // The remaining control-derived coefficients of the tape path, ramped for the same
    // reason. Smoothing only the two shaper arguments was not enough: DRIVE still
    // multiplied the signal through driveAmount raw, BRIGHT still stepped the record
    // pole (toneLpAc), TONE still stepped both playback poles and the flutter depth,
    // and the hiss level still jumped. Each of those is a step in a multiplier or a
    // filter pole on every block boundary, which is the crackle that survived.
    SampleSmoother driveAmountSmoothed { sampleClock };
    SampleSmoother toneLpSmoothed { sampleClock };
    // A neutral mid-range initial value keeps the first wet block audible while the
    // rate- and parameter-dependent poles settle to their exact targets.
    SampleSmoother hfPostSmoothed { sampleClock, false, false, 0.5f };
    SampleSmoother headGapSmoothed { sampleClock, false, false, 0.5f };
    SampleSmoother flutterScaleSmoothed { sampleClock, false, false, 1.0f };
    SampleSmoother hissGainSmoothed { sampleClock };

    // The head-damping pole gets a SECOND, much slower ramp that is only used when the
    // tape formula changes. A control move wants the 20 ms feel; a formula switch moves
    // this pole by up to 6 kHz, and 20 ms of that is a fast sweep rather than a
    // crossfade. Both ramps track the same target, and the tape loop reads the switch
    // ramp only on the block where a change was detected, so ordinary knob movement
    // keeps its original responsiveness.
    SampleSmoother headDampingSwitchSmoothed { sampleClock, false, false, 0.5f };

    // -------------------------------------------------------------------------
    //  Tape-type change handling.
    //
    //  Switching tape formula is a MUCH bigger step than moving a control: the
    //  model offset moves the saturation curve, the bias asymmetry, the head-damping
    //  pole by up to 6 kHz, the hysteresis thickness and the noise floor all at once.
    //
    //  Two things are needed for that to crossfade instead of cracking:
    //
    //   1. A slower ramp than the 20 ms used for controls, so a 6 kHz pole move reads
    //      as a morph rather than a fast sweep. That is headDampingSwitchSmoothed above.
    //   2. The shaper's own memory cleared at the moment of the change. The hysteresis
    //      term feeds the previous shaped output back into a NON-LINEAR function, so
    //      even with every coefficient ramping perfectly, old memory inside a new
    //      curve is an instantaneous discontinuity. Ramping the coefficients cannot
    //      fix that; the state has to be let go of.
    //
    //  `activeTapeType` caches the last seen index so the change is detected exactly
    //  once, and negative means "nothing seen yet", so the very first block seeds
    //  itself instead of being treated as a switch. `tapeTypeChangeCountdown` holds the
    //  number of samples the slow ramp is allowed to run for, which is what keeps the
    //  switch ramp from affecting ordinary knob movement.
    // -------------------------------------------------------------------------
    int activeTapeType = -1;
    int tapeTypeChangeCountdown = 0;

    // One subharmonic generator per channel. They are NOT shared, because each one
    // commits to a flip from its own channel's waveform: running a single generator on
    // the mono sum would collapse the stereo image at exactly the octave the effect is
    // meant to add weight to.
    SubharmonicGenerator subharmonicL;
    SubharmonicGenerator subharmonicR;

    // The depth control is read per sample, so it ramps like every other gain. A raw
    // step here would put a discontinuity into an already phase-locked oscillator.
    SampleSmoother subFundamentalSmoothed { sampleClock };

    // Per-instance tape noise generator. Kept as an object member rather than a
    // thread_local static so that instances never share one stream and the output
    // is reproducible for a given instance.
    std::uint32_t noiseState = 0x1b873593u;

    // There is deliberately no noise-floor levelling state here. The hiss is a
    // constant band-limited floor; an earlier programme-tracking leveller made the
    // floor loudest in a pause, which is the opposite of what a noise floor should do.

    // Per-channel DC-blocker state for the wet path. The asymmetric shaper and its
    // bias offset leave a small DC component on the tape signal; on a real machine
    // the playback electronics are AC-coupled, so the model is too. Without this,
    // MIX at 100 % hands the limiter and soft clipper an off-centre waveform, which
    // clips asymmetrically and reads as harsh garbage instead of a warm signal.
    std::array<float, 2> dcBlockXState {};
    std::array<float, 2> dcBlockYState {};

    // Compressor-coupled saturation. The smoothed (0..1) amount the two glue stages
    // are currently squeezing drives extra drive into the magnetic shaper, so the
    // harder the compressors work, the harder the tape saturates - the way pushing
    // a hot, compressed signal into a real record head does. Audio-thread only.
    float squeezeSaturationDrive = 0.0f;

    // Two independent glue stages, each with its own detector envelope. The input
    // stage runs straight after the input trim, the output stage straight before
    // the output trim; neither reads the other's state.
    GlueCompressor inputCompressor;
    GlueCompressor outputCompressor;

    // Measures the harmonics the tape shaper is actually producing, separating even from
    // odd. This is the observable signature of the analogue character and drives the
    // HARMONICS display.
    HarmonicAnalyser harmonicAnalyser;

    // Output safety limiter, the stage that keeps the signal below the soft clipper so it
    // almost never has to act. `preLimiterDetector` is a fast peak follower and
    // `limiterGain` is the smoothed gain it applies; keeping them separate gives the
    // classic brick-wall shape - instant catch, musical release.
    float preLimiterDetector = 0.0f;
    float limiterGain = 1.0f;

    // Final gain compensation. `smoothedCompensationDb` is the slow, programme-level
    // correction applied after the last compressor. It is driven by comparing the
    // reference power taken straight after the input trim (before the first compressor)
    // against the power the chain actually produced. Audio-thread only, so a plain float.
    float smoothedCompensationDb = 0.0f;

    // K-weighted loudness, run on the plugin output and on the reference point so the
    // panel can show both ends of the chain on the same scale.
    LoudnessMeter outputLoudness;
    LoudnessMeter inputLoudness;

    // Classic 300 ms VU ballistic average, kept separate from the RMS so the VU meter
    // has the slow, forgiving movement that makes it useful for programme level.
    float vuAverage = 0.0f;
    float inputVuAverage = 0.0f;

    // -----------------------------------------------------------------------
    //  Playback head delay.
    //
    //  A fixed-size circular buffer per channel, sized in prepareToPlay for the
    //  longest time the control can ask for at the highest rate the engine runs
    //  at (250 ms at 8x oversampling of 192 kHz). Allocating it once and never
    //  resizing is what keeps the audio thread free of allocation: the delay time
    //  is a read offset into this buffer, not a change to its size.
    //
    //  The read position is smoothed, so sweeping the DELAY control glides like a
    //  tape head being moved rather than stepping the waveform.
    // -----------------------------------------------------------------------
    juce::AudioBuffer<float> delayBuffer;
    int delayWritePosition = 0;
    int delayBufferLength = 0;
    SampleSmoother delaySamplesSmoothed { sampleClock };
    SampleSmoother delayFeedbackSmoothed { sampleClock };

    // The delay's own feedback path is damped: each repeat loses top end, the way
    // a real second head loses it through the same tape losses the main path has.
    // Without this the repeats stack into a bright metallic ring.
    std::array<float, 2> delayDampState {};
    float delayDampCoefficient = 0.35f;

    // -----------------------------------------------------------------------
    //  Stereo tape offset.
    //
    //  A one-sample-capable fractional delay on the right channel only, driven by
    //  ST OFFSET. It is deliberately tiny - a few tens of microseconds - and it is
    //  what makes a tape bounce sit wide instead of merely being equalised wide.
    //
    //  A short linear-interpolating buffer rather than an all-pass: an all-pass
    //  would give the same group delay with less memory but would colour the
    //  phase differently across the band, and the whole point here is that the two
    //  channels differ by TIME, not by filter shape.
    // -----------------------------------------------------------------------
    static constexpr int stOffsetBufferLength = 64;
    std::array<float, stOffsetBufferLength> stOffsetBuffer {};
    int stOffsetWritePosition = 0;
    SampleSmoother stOffsetSamplesSmoothed { sampleClock };

    // -----------------------------------------------------------------------
    //  Transport state (STOP / PLAY / START).
    //
    //  `transportRamp` is 0 when the machine is at rest and 1 when it is running
    //  at speed. STOP drives it to 0, PLAY holds it at 1, and START drives it
    //  toward 1 from wherever it was, so hitting START from STOP is a genuine
    //  spin-up and hitting it from PLAY is a brief re-lock rather than a jump.
    //
    //  It scales the whole wet path and the transport modulation together, which
    //  is what makes STOP silent and START a pitch ramp instead of a gate.
    // -----------------------------------------------------------------------
    float transportRamp = 1.0f;
    float transportRampCoefficient = 0.0f;
    int lastTransportState = -1;

    // Noise floor trim, smoothed like every other control-derived gain so moving
    // the NOISE knob cannot step the hiss level.
    SampleSmoother noiseTrimSmoothed { sampleClock, false, false, 1.0f };

    // -----------------------------------------------------------------------
    //  Saturation blend.
    //
    //  One core and one amp voicing per channel. They are NOT shared: both hold
    //  signal-dependent state (the hysteresis memory and the three bias-drift
    //  followers), and running one instance across a stereo pair would make the
    //  right channel's character depend on the left's - crosstalk in the
    //  nonlinearity itself, which is the one place it cannot be tolerated.
    // -----------------------------------------------------------------------
    SaturationCore saturationL;
    SaturationCore saturationR;
    AmpVoicing ampL;
    AmpVoicing ampR;

    // The two blend controls, ramped so moving either one morphs the harmonics
    // continuously instead of stepping the curve on a block boundary.
    SampleSmoother blendSmoothed { sampleClock };
    SampleSmoother shapeSmoothed { sampleClock };

    // The amp controls. PRESENCE and CABINET are coefficients rather than gains,
    // so they are smoothed for the same reason every other pole is: a stepped
    // pole is a discontinuity in the waveform.
    SampleSmoother sagSmoothed { sampleClock };
    SampleSmoother presenceSmoothed { sampleClock, false, false, 0.5f };
    SampleSmoother cabinetSmoothed { sampleClock };
    SampleSmoother ampBiasSmoothed { sampleClock, false, false, 0.5f };

    // Cabinet coefficients, built once per block from the rate. Two poles for the
    // roll-off and one for the resonance, so three numbers.
    float cabinetLowCoefficient = 0.5f;
    float cabinetPeakCoefficient = 0.02f;
    float presenceCoefficient = 0.5f;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FirstAudioProcessor)
};
