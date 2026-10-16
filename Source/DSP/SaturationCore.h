// =============================================================================
//  The saturation core: the seven principles, and the four type lists that voice
//  the non-tape ones.
//
//  Compiled and driven by tests/dsp/subfund_test.cpp, which includes this header
//  rather than a copy cut out of PluginProcessor.h. The conventions of
//  Source/DSP/ are stated in J37Math.h.
//
//  Every tanh below goes through j37Tanh (Source/DSP/HotPathMath.h) rather than
//  straight to std::tanh, and that is the whole of the seam: the shaper is the
//  hot path, so the ONE place its cost can be traded - xsimd's batch kernel, an
//  opt-in build option - is the one call this file makes eleven times. With the
//  option off, j37Tanh IS std::tanh, so the curves and every render are what
//  they were before this include existed. tests/dsp/simd_tanh_test.cpp drives
//  the curves below both ways and asserts exactly that.
// =============================================================================
#pragma once

#include <array>
#include <cmath>

#include "HotPathMath.h"

//==============================================================================
/*
    The four non-tape saturation principles each carry their own type list, on
    exactly the pattern tapeStock established: one count, one name array, one
    completeness check and one StringArray conversion. The same three-way rule
    applies - the parameter reads the list, the panel reads the list, and the
    engine's switch is the one the compiler cannot count, so it carries a jassert.

    The names are types rather than brands: unlike tape formulas, which really are
    twelve different oxides, a valve or a transformer is best offered as a
    recognisable kind of the thing. Each entry changes how its core's curve is
    biased - a colder valve is tighter and thinner, a 6L6 is fatter than an EL34 -
    so the switch is a voice selector, not a rename.
*/

// -- VALVE ------------------------------------------------------------------
inline constexpr int valveTypeCount = 7;

inline constexpr std::array<const char*, valveTypeCount> valveTypeNames
{
    "12AX7", "ECC82", "EL34", "6L6", "300B", "KT88",
    "Off"   // see the note on tapeStockNames: OFF is appended, never inserted
};

inline constexpr bool valveTypeNamesAreComplete()
{
    for (const auto* name : valveTypeNames)
        if (name == nullptr)
            return false;
    return true;
}

static_assert (valveTypeNamesAreComplete(),
               "valveTypeCount and valveTypeNames must describe the same number of valve types");

inline juce::StringArray valveTypeNameList()
{
    juce::StringArray names;
    for (const auto* name : valveTypeNames)
        names.add (juce::String (name));
    return names;
}

// -- AMP ---------------------------------------------------------------------
inline constexpr int ampTypeCount = 7;

inline constexpr std::array<const char*, ampTypeCount> ampTypeNames
{
    "Fender Blackface", "Marshall Plexi", "Vox AC30", "Hiwatt DR103", "Mesa Recto", "Matchless HC30",
    "Off"   // see the note on tapeStockNames: OFF is appended, never inserted
};

inline constexpr bool ampTypeNamesAreComplete()
{
    for (const auto* name : ampTypeNames)
        if (name == nullptr)
            return false;
    return true;
}

static_assert (ampTypeNamesAreComplete(),
               "ampTypeCount and ampTypeNames must describe the same number of amp types");

inline juce::StringArray ampTypeNameList()
{
    juce::StringArray names;
    for (const auto* name : ampTypeNames)
        names.add (juce::String (name));
    return names;
}

// -- TRANSFORMER ---------------------------------------------------------------
inline constexpr int transformerTypeCount = 6;

inline constexpr std::array<const char*, transformerTypeCount> transformerTypeNames
{
    "Jensen JT-11P", "Cinemag CM-7510", "Lundahl LL1544", "OEP A262", "Sowter 4381",
    "Off"   // see the note on tapeStockNames: OFF is appended, never inserted
};

inline constexpr bool transformerTypeNamesAreComplete()
{
    for (const auto* name : transformerTypeNames)
        if (name == nullptr)
            return false;
    return true;
}

static_assert (transformerTypeNamesAreComplete(),
               "transformerTypeCount and transformerTypeNames must describe the same number of transformer types");

inline juce::StringArray transformerTypeNameList()
{
    juce::StringArray names;
    for (const auto* name : transformerTypeNames)
        names.add (juce::String (name));
    return names;
}

// -- DIGITAL -------------------------------------------------------------------
inline constexpr int digitalTypeCount = 6;

inline constexpr std::array<const char*, digitalTypeCount> digitalTypeNames
{
    "16-bit", "12-bit", "8-bit", "Bit Crush", "Sample Hold",
    "Off"   // see the note on tapeStockNames: OFF is appended, never inserted
};

inline constexpr bool digitalTypeNamesAreComplete()
{
    for (const auto* name : digitalTypeNames)
        if (name == nullptr)
            return false;
    return true;
}

static_assert (digitalTypeNamesAreComplete(),
               "digitalTypeCount and digitalTypeNames must describe the same number of digital types");

inline juce::StringArray digitalTypeNameList()
{
    juce::StringArray names;
    for (const auto* name : digitalTypeNames)
        names.add (juce::String (name));
    return names;
}

//==============================================================================
/**
    The seven saturation principles, and the mix that combines them.

    A tape machine is ONE of the ways analogue electronics bend a signal, and the
    plugin was built around that one curve. The seven below are the distinct
    mechanisms a real signal chain uses, and they are genuinely different shapes
    rather than seven settings of one:

      TAPE       - magnetic hysteresis: a MEMORY term, because the medium's state
                   depends on where it has been. Gentle at low level, and the
                   asymmetry is what makes the even harmonics.
      VALVE      - thermionic: a soft, strongly ASYMMETRIC knee with a wide
                   transition. Even-dominant, and it compresses rather than clips,
                   so it thickens before it distorts.
      CASSETTE   - narrow-gauge, low-bias ferric: a HARD, early knee with a very
                   limited headroom and a pronounced low-frequency bump. The
                   "everything is louder and smaller" character.
      VINYL      - a cutting lathe's tracing distortion: a soft, second-order
                   power-law knee plus a small drift of the groove's own
                   operating point. The gentlest of the seven.
      AMP        - a guitar amplifier's input stage: a high-gain, nearly symmetric
                   cascade that clips HARD and generates strong odd harmonics. It
                   is the one that bites.
      TRANSFORMER - core saturation with a flux memory: the core lags whatever
                   drives it, and its asymmetry arrives only once the core is
                   properly loaded.
      DIGITAL    - the converter's stop: transparent and bit-exact below full
                   scale, hard above it, with no memory and no bias to drift.

    Mixing them is not a gimmick: a real chain is exactly this. A guitar goes
    into an amp (AMP), the amp into a desk and a tape machine (TAPE), a valve
    compressor or a valve preamp somewhere in the path (VALVE), and the whole
    thing may end up on a cassette (CASSETTE) or a cut record (VINYL). The
    seven SOURCE knobs set each mechanism's share of the mix, and the default
    sits on tape because that is what the plugin is calibrated around.

    Every curve is normalised to unity slope at the origin, exactly like
    magneticHysteresis, so the mix cannot change the level - only the shape.
    That property is what makes the controls usable: moving them changes the
    harmonics, not the gain, and the DRIVE control keeps its own meaning.
*/
struct SaturationCore
{
    // -----------------------------------------------------------------------
    //  Weights. These are set once per block from the seven SOURCE controls:
    //  each is its own amount knob, so a user dials the machines they want by
    //  name rather than steering a sweep. They are kept normalised so the seven
    //  always sum to 1 and the mix is a true crossfade rather than a stack.
    // -----------------------------------------------------------------------
    float tapeWeight = 1.0f;
    float valveWeight = 0.0f;
    float cassetteWeight = 0.0f;
    float vinylWeight = 0.0f;
    float ampWeight = 0.0f;
    float transformerWeight = 0.0f;
    float digitalWeight = 0.0f;

    //  ...and the AMOUNT they asked for before they were normalised. The seven
    //  above answer WHICH machines; this answers HOW MUCH, and it is the
    //  crossfade depth process() applies to the whole blend. See setWeights for
    //  why it cannot be recovered from the shares.
    float saturationAmount = 1.0f;

    /** Sets all seven weights DIRECTLY from the seven SOURCE knobs: a knob is
        its principle's share, so TAPE 60 / VALVE 40 reads on the panel exactly
        as it sounds. Shares are normalised to sum to 1; a principle whose own
        model list is OFF has its share redistributed by the normalisation
        rather than dropped, so switching a machine off re-weights the rest
        instead of leaving a hole. (The old blend/shape pair hid all of this
        behind one sweep position and one spread; the user asked for names.)
        The amounts arrive already smoothed - every SOURCE knob is read once
        per block and fed to its own smoother - so the SET is what makes all
        seven live at once, and the normalisation below is what keeps the mix
        level-stable while amounts move.
    */
    void setWeights (float tapeAmount, float valveAmount, float cassetteAmount,
                     float vinylAmount, float ampAmount, float transformerAmount,
                     float digitalAmount) noexcept
    {
        tapeWeight        = juce::jlimit (0.0f, 1.0f, tapeAmount);
        valveWeight       = juce::jlimit (0.0f, 1.0f, valveAmount);
        cassetteWeight    = juce::jlimit (0.0f, 1.0f, cassetteAmount);
        vinylWeight       = juce::jlimit (0.0f, 1.0f, vinylAmount);
        ampWeight         = juce::jlimit (0.0f, 1.0f, ampAmount);
        transformerWeight = juce::jlimit (0.0f, 1.0f, transformerAmount);
        digitalWeight     = juce::jlimit (0.0f, 1.0f, digitalAmount);

        // A principle whose own model list is OFF contributes nothing, and its
        // share is REDISTRIBUTED by the normalisation below rather than dropped:
        // switching a machine off re-weights the rest to cover it, so the mix
        // holds its degree of saturation and only the character changes.
        if (valveOff)       valveWeight = 0.0f;
        if (ampOff)         ampWeight = 0.0f;
        if (transformerOff) transformerWeight = 0.0f;
        if (digitalOff)     digitalWeight = 0.0f;
        if (tapeOff)        tapeWeight = 0.0f;

        const auto sum = tapeWeight + valveWeight + cassetteWeight + vinylWeight
                               + ampWeight + transformerWeight + digitalWeight;

        // HOW MUCH saturation, kept separate from WHICH machines.
        //
        // The normalisation below answers the second question - the seven
        // numbers always become shares that sum to one, so the MIX page's bar
        // always reads 100 % whatever the knobs say. That was the whole point
        // of shares, and it had a consequence nobody intended: the seven SOURCE
        // knobs said NOTHING about how hard the stage worked. Turn all seven
        // down to nothing and the shares still summed to one, so the core ran
        // at full strength on whichever machine was nominally leading - the
        // panel's own minimum setting saturated exactly as hard as its maximum,
        // and the smallest move of any one knob re-weighted all seven at once.
        // That is what "the sound changes far too much, even at the smallest
        // settings" was measuring.
        //
        // So the sum is kept, and it drives the drive: one machine at half is
        // now half the saturation with a pure-tape character, rather than a
        // full-strength stage wearing a tape label. Saturation reaches its
        // ceiling once the seven together add up to one, which is where the
        // Default preset already lives (TAPE alone at 1.0), so every existing
        // session and every factory preset that uses one machine sounds exactly
        // as it did - only the region BELOW full strength, which nothing
        // produced before, now means what it says. process() applies it as a
        // crossfade against the input rather than as a scale on the drive,
        // because the curves are tanh and tanh saturates at any drive.
        saturationAmount = juce::jlimit (0.0f, 1.0f, sum);

        if (sum > 1.0e-6f)
        {
            tapeWeight /= sum;
            valveWeight /= sum;
            cassetteWeight /= sum;
            vinylWeight /= sum;
            ampWeight /= sum;
            transformerWeight /= sum;
            digitalWeight /= sum;
        }
        else
        {
            // All sources at zero. The character still has to resolve to
            // something, and pure tape is what the machine was calibrated as -
            // but saturationAmount above is zero, so process() hands every
            // curve a drive of nothing and the core returns its input
            // bit-for-bit. The shares here describe a mix that is not playing.
            tapeWeight = 1.0f;
            valveWeight = cassetteWeight = vinylWeight = ampWeight = transformerWeight
                        = digitalWeight = 0.0f;
        }
    }

    // -----------------------------------------------------------------------
    //  Per-principle state.
    //
    //  Tape needs the hysteresis memory (three slots, as before). The valve and
    //  amp stages carry a bias-shift state, because a real stage's operating
    //  point drifts with the signal - that drift is a slow envelope, not a
    //  sample-by-sample term, so it is a one-pole per channel. The transformer
    //  carries the core's own flux and the digital stage a held code: the first
    //  because a core lags whatever is driving it, the second because a converter
    //  keeps the value it picked for a whole sample period.
    // -----------------------------------------------------------------------
    float tapeMemory = 0.0f;
    float valveBiasState = 0.0f;
    float ampBiasState = 0.0f;
    float cassetteBiasState = 0.0f;
    float vinylBiasState = 0.0f;
    float transformerFlux = 0.0f;
    float digitalHold = 0.0f;

    void reset() noexcept
    {
        tapeMemory = 0.0f;
        valveBiasState = 0.0f;
        ampBiasState = 0.0f;
        cassetteBiasState = 0.0f;
        vinylBiasState = 0.0f;
        transformerFlux = 0.0f;
        digitalHold = 0.0f;
    }

    // (The old blend/shape sweep body was removed with the controls that drove
    //  it: the seven SOURCE knobs now speak to setWeights above directly.)

    /** True when nothing is contributing, so the caller can skip the whole core.

        The weights alone can no longer answer this. They are shares - they
        always sum to one, even for a mix that was asked for at zero - so the
        test is the AMOUNT as well: a blend with nothing in it is idle, and a
        blend at a quarter is not idle, it is quiet, which is a different thing
        and belongs to process(), not here. */
    bool isIdle() const noexcept
    {
        return saturationAmount <= 1.0e-6f;
    }

    // -----------------------------------------------------------------------
    //  The range the engine's DRIVE argument actually runs to. It is not 0..1: it
    //  is the drive curve times the tape formula (capped at 1.8) plus the
    //  hysteresis term plus the compressor squeeze, so by the time the record
    //  head is fully driven it is past 2.2. Two of the curves below have to map
    //  their own threshold onto it, and a threshold that ran negative would not
    //  bend a curve - it would fold it back on itself - so both of them take
    //  their range from here rather than from an assumed 0..1.
    // -----------------------------------------------------------------------
    static constexpr float shaperDriveRange = 2.25f;

    //  The six curves. Each takes the driven input and returns a value with the
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
        const float hard = j37Tanh (biased * slope) / slope;

        const float lagged = tapeMemory * 0.62f;
        const float delayedSlope = 0.55f + drive * 1.0f;
        const float delayed = j37Tanh ((biased * delayedSlope) + lagged) / delayedSlope;

        constexpr float hardWeight = 0.70f;
        constexpr float delayedWeight = 0.30f;
        const float blended = hard * hardWeight + delayed * delayedWeight;

        const float asymmetryTerm = asymmetry * 0.45f * hard * hard;

        // The zero-point correction, exactly as in magneticHysteresis: subtracting
        // the curve's own value at zero input removes the DC pedestal the bias
        // offset would otherwise leave, so zero in still means zero out.
        const float hardAtZero = j37Tanh (asymmetry * slope) / slope;
        const float delayedAtZero = j37Tanh ((asymmetry * delayedSlope) + lagged) / delayedSlope;
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
    // The valve family's voice, set once per block from VALVE TYPE. Defaults are
    // the ECC82-ish centre the engine shipped with, so type 0..5 map across it:
    // lower types run colder and tighter, higher ones fatter and softer.
    float valveColdness = 0.5f;      // how cold the stage runs (bias point)
    float valveSag      = 0.5f;      // how much the operating point drifts

    void setValveVoice (int type) noexcept
    {
        // OFF: the valve principle is not present, so its curve becomes an exact
        // pass-through. This is what OFF means for every one of these voice
        // selectors - the PRINCIPLE is removed, and the blend then distributes
        // whatever it would have contributed to the remaining principles.
        // Returning the input unchanged is what makes that true rather than
        // merely quiet: a scaled-down curve would still add its own harmonics.
        valveOff = (type >= valveTypeCount - 1);
        if (valveOff)
            return;

        static constexpr float cold[6] = { 0.30f, 0.40f, 0.55f, 0.65f, 0.80f, 0.45f };
        static constexpr float sag [6] = { 0.35f, 0.50f, 0.45f, 0.65f, 0.30f, 0.75f };
        valveColdness = cold[type];
        valveSag      = sag [type];
    }

    bool valveOff = false;
    /** True when the TAPE principle's own model list is OFF. Set from the TAPE
        TYPE switch by the engine; see the note in setBlend for what an OFF
        principle does and, more importantly, what it does NOT do. */
    bool tapeOff = false;

    float shapeValve (float x, float drive, float asymmetry) noexcept
    {
        // The operating point drifts toward the signal's own average. A slow
        // one-pole rather than the instantaneous value, because the drift is a
        // thermal/electrical time constant in the real thing, not a waveform term.
        // The type sets how much of that drift there is.
        valveBiasState += (x - valveBiasState) * (0.0004f + 0.0008f * valveSag);
        const float driftingBias = asymmetry
                                 + valveBiasState * 0.25f
                                 - valveColdness * 0.030f;

        const float biased = x + driftingBias;

        // A gentler slope than tape's: the valve compresses across a wider range
        // instead of bending early. The squared term is the valve's own soft knee.
        const float slope = 1.0f + drive * 1.5f;
        const float soft = j37Tanh (biased * slope) / slope;

        // The asymmetry here is stronger and applied to the whole curve rather
        // than only to the hard branch, which is what makes the even content
        // dominate instead of sitting under an odd-heavy fundamental.
        const float asymmetryTerm = driftingBias * 0.65f * soft * soft;

        const float softAtZero = j37Tanh (driftingBias * slope) / slope;
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
        float engaged = 0.0f;
        if (magnitude <= knee)
        {
            // Linear region, scaled so the slope is unity: y = x.
            shaped = biased;
        }
        else
        {
            // Above the knee the curve walks to the asymptote over a short span.
            const float overshoot = juce::jmin (1.0f, (magnitude - knee) / juce::jmax (0.05f, knee));
            const auto eased = overshoot * overshoot * (3.0f - 2.0f * overshoot); // smoothstep
            shaped = sign * (knee + eased * (1.0f - knee));
            engaged = overshoot;
        }

        // The cassette's low-frequency bump: the head bump and the narrow track
        // both lift the bottom end, and it is part of the character rather than an
        // artefact. Applied as a level-dependent term so it only shows under drive.
        //
        // Gated on `engaged`, so that it only shows under drive IN FACT. Ungated
        // the bump multiplied the linear region as well, and since it is a gain on
        // the curve that made this the one principle whose slope at the origin was
        // not 1 - 1.27 at full drive, measured - so crossfading the cassette in
        // changed the level as well as the character, which is the one thing this
        // blend is not allowed to do. Below the knee the term is now exactly zero.
        const float bump = 1.0f + drive * 0.12f * engaged
                             * (1.0f - juce::jmin (1.0f, std::abs (shaped)));

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
        // The signal path above, walked again for the correction, bump and its gate
        // included. Correcting for a bump the curve no longer applies would leave
        // the pedestal in instead of removing it.
        const auto atZeroEngaged = [&]
        {
            const float z = asymmetry * 0.5f;
            if (std::abs (z) <= knee)
                return 0.0f;
            return juce::jmin (1.0f, (std::abs (z) - knee) / juce::jmax (0.05f, knee));
        }();
        const float atZeroBumped = atZero * (1.0f + drive * 0.12f * atZeroEngaged
                                             * (1.0f - juce::jmin (1.0f, std::abs (atZero))));

        return out - atZeroBumped;
    }

    /**
        VINYL - a cutting lathe's tracing distortion.

        The seventh SOURCE. A lathe does not cut what it is fed: the stylus has
        a finite radius, so on tight curvature (high frequencies at high level)
        the groove's radius is larger than the signal asks and the replay stylus
        traces a rounded-off path - second-order, level-dependent, the gentlest
        of the seven. That is why vinyl at moderate levels reads as smooth
        rather than bent. The curve: a soft power-law knee (softer than the
        cassette's corner, gentler than the amp's clip) plus a small bias drift
        - the groove's own operating point moves with the programme, like the
        valve's but faster.
    */
    float shapeVinyl (float x, float drive, float asymmetry) noexcept
    {
        // The groove's operating point drifts with the programme - between the
        // valve's slow thermal drift and the cassette's fast one, which is the
        // order a real cutter head's feedback loop settles in.
        vinylBiasState += (x - vinylBiasState) * 0.0015f;
        const float biased = x + asymmetry * 0.35f + vinylBiasState * 0.12f;

        // The tracing knee: at drive 0 it sits at 1.05 (above full scale - the
        // lathe passes the programme untouched) and closes as drive rises, but
        // never as far as the cassette's 0.12 floor: a cutter is a precision
        // device, and its knee is soft.
        const float knee = juce::jlimit (0.25f, 1.05f, 1.05f - drive * 0.55f);

        const float magnitude = std::abs (biased);
        const float sign = biased < 0.0f ? -1.0f : 1.0f;

        float shaped;
        if (magnitude <= knee)
        {
            shaped = biased;   // linear region, unity slope: y = x.
        }
        else
        {
            // Above the knee the curve walks to its asymptote on a POWER law
            // (n = 2.5) rather than the cassette's smoothstep: a tracing radius
            // rounds an overshoot progressively, not on a polynomial schedule.
            const float overshoot = juce::jmin (1.0f,
                                                (magnitude - knee) / juce::jmax (0.08f, knee));
            const float eased = std::pow (overshoot, 2.5f);
            shaped = sign * (knee + eased * (1.0f - knee));
        }

        // Zero-point correction, the same rule every curve here follows: the
        // signal path walked again at zero input, so zero in means zero out.
        const float atZero = [&]
        {
            const float z = asymmetry * 0.35f;
            const float m = std::abs (z);
            if (m <= knee) return z;
            const float o = juce::jmin (1.0f, (m - knee) / juce::jmax (0.08f, knee));
            const float e = std::pow (o, 2.5f);
            return (z < 0.0f ? -1.0f : 1.0f) * (knee + e * (1.0f - knee));
        }();

        return shaped - atZero;
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
    // The amp family's voice, from AMP TYPE. gain1/gain2 move the two cascade
    // gains apart - a tweed is loose in stage one, a recto slams in stage two.
    float ampGain1 = 4.5f;
    float ampGain2 = 1.2f;

    void setAmpVoice (int type) noexcept
    {
        // OFF: see setValveVoice for what OFF means on a principle's own model
        // list - the principle is removed, not merely turned down.
        ampOff = (type >= ampTypeCount - 1);
        if (ampOff)
            return;

        static constexpr float g1[6] = { 3.2f, 4.8f, 3.8f, 4.0f, 5.6f, 4.2f };
        static constexpr float g2[6] = { 1.0f, 1.4f, 1.1f, 1.3f, 1.6f, 1.2f };
        ampGain1 = g1[type];
        ampGain2 = g2[type];
    }

    bool ampOff = false;

    float shapeAmp (float x, float drive, float asymmetry) noexcept
    {
        // A small, fast bias drift: a high-gain stage's operating point moves
        // quickly with the signal because the gain makes even a small shift
        // audible.
        ampBiasState += (x - ampBiasState) * 0.002f;
        const float biased = x + asymmetry * 0.18f + ampBiasState * 0.10f;

        // Stage one: high gain, hard clip. The slope is much steeper than tape's
        // so the knee arrives early, and the type sets how much steeper.
        const float slope1 = 1.0f + drive * ampGain1;
        const float stage1 = j37Tanh (biased * slope1) / slope1;

        // Stage two: the cascade. A second, gentler stage applied to the first
        // stage's output, which is what flattens the middle of the curve.
        const float slope2 = 1.0f + drive * ampGain2;
        const float stage2 = j37Tanh (stage1 * slope2) / slope2;

        // Hard-clipped stages are nearly symmetric, so the asymmetry term is
        // small - just enough to keep some even content rather than a pure odd
        // series, which would read as a fuzz rather than an amp.
        const float asymmetryTerm = asymmetry * 0.20f * stage2 * stage2;

        const float s1Zero = j37Tanh ((asymmetry * 0.18f) * slope1) / slope1;
        const float s2Zero = j37Tanh (s1Zero * slope2) / slope2;
        const float asymmetryAtZero = asymmetry * 0.20f * s2Zero * s2Zero;

        return (stage2 - s2Zero) - (asymmetryTerm - asymmetryAtZero);
    }

    // The core's time constant, as a per-sample one-pole coefficient. It is a
    // fixed number rather than a rate-derived one for the same reason the valve's
    // and the amp's drifts are: the curve is given drive and asymmetry and
    // nothing else, so it has no rate to derive a time constant from. The value
    // is a couple of milliseconds, which is the order of a small iron core's
    // flux following the drive - long enough that the memory is felt, short
    // enough that it does not smear the note.
    static constexpr float transformerCore = 0.02f;

    // The transformer family's voice, from TRANSFORMER TYPE: how fast the core
    // tracks (a function of the core material and the winding's own resistance)
    // and how hard it is to push into saturation.
    float transformerSpeed    = 0.02f;
    float transformerSoftness = 1.0f;   // slope multiplier on the core's bend

    void setTransformerVoice (int type) noexcept
    {
        // OFF: see setValveVoice for what OFF means on a principle's own model
        // list - the principle is removed, not merely turned down.
        transformerOff = (type >= transformerTypeCount - 1);
        if (transformerOff)
            return;

        static constexpr float spd[5] = { 0.016f, 0.020f, 0.024f, 0.014f, 0.022f };
        static constexpr float sft[5] = { 1.15f, 1.05f, 0.95f, 1.25f, 0.90f };
        transformerSpeed    = spd[type];
        transformerSoftness = sft[type];
    }

    bool transformerOff = false;

    /**
        TRANSFORMER - a passive input transformer, driven into its core.

        The other odd one out, and for the opposite reason to DIGITAL: this is
        pure analogue with no conversion anywhere in it, but it is not a machine
        either. A Variac, a console line input, the iron ahead of a 1176 - there is
        no valve, no bias supply and no transport. You overload it by turning the
        level up and nothing else.

        Three things separate it from the four beside it, and the first is the
        signature:

        1. It bends LOW FIRST. The primary's inductance is a series impedance, and
           a series impedance takes its bite out of the bottom of the band, so
           when the core starts to saturate it is the low end that is squeezed
           while the top stays comparatively open. That is the opposite of the
           amp, where the stage's gain makes the treble go first, and it is why
           this curve is built from a split signal rather than from one tanh.

        2. The memory is the core, not the signal. A core's flux lags whatever is
           driving it, and it lags the low end hardest - so the state here is a
           one-pole on the low-passed signal, not a per-sample lag the way the
           tape shaper's is. It is the same one-pole that does the splitting, so
           the stage costs one coefficient of state rather than two.

        3. The asymmetry arrives late. A shorted secondary rectifies, and that is
           where a transformer's even harmonics come from, but it produces
           nothing until the core is properly loaded. So the second-harmonic term
           is gated on level, which is the actual difference from the valve: an
           unloaded transformer is close to perfectly symmetrical, and a curve
           that leaked even content at low level would not sound like copper.
    */
    float shapeTransformer (float x, float drive, float asymmetry) noexcept
    {
        // The split, and the core's memory in one coefficient. The low-pass is not
        // decoration: a saturating primary's added impedance is itself a
        // low-frequency effect, so low-passing the drive is the cheapest honest
        // model of the mechanism rather than a band-splitting shortcut around it.
        transformerFlux += (x - transformerFlux) * transformerSpeed;

        const float core = transformerFlux;
        const float air  = x - core;      // the part the winding never carried

        // The core's own bend, and the one place this curve deliberately is NOT
        // like its neighbours. A cassette has a hard corner, a converter has a
        // hard stop; a transformer has neither. Iron this size goes into
        // saturation gradually and keeps compressing, so the curve is a soft
        // compressor with no knee at all - which is exactly why so much
        // material mixed on an old console has no audible edge on it, and why a
        // transformer overloads into glue rather than into distortion.
        //
        // One tanh, with the slope rising as the drive does, and no asymptote
        // below full scale. A knee-and-clamp shape here was measurably wrong: a
        // low knee and a ceiling at 1.0 is a curve that EXPANDS everything
        // between them, and this stage reached 2.8x gain on a 0.4 input before
        // it started to give any level back. This one is monotonically
        // compressive at every level, which is what a saturating core is.
        //
        // The drive is mapped from shaperDriveRange for the same reason the
        // digital ceiling is: this core is handed the engine's drive, not a 0..1
        // control, and the slope has to be built from the range it actually runs
        // to rather than from an assumed one.
        const auto load = juce::jlimit (0.0f, shaperDriveRange, drive)
                            / shaperDriveRange;
        const float slope = 1.0f + load * 1.1f * transformerSoftness;
        const float bent = j37Tanh (core * slope) / slope;

        // How loaded the core is, which gates the two effects below. This is a
        // guide rather than a corner in the curve - the bend above has no knee,
        // so there is no threshold in the signal for this to be measured
        // against. It is the level at which a core is meaningfully into
        // saturation, and it falls as the drive rises, so the copper and the
        // bloom arrive earlier and earlier the harder the transformer is worked.
        const float onset = 0.35f - load * 0.25f;
        const auto engaged = juce::jlimit (0.0f, 1.0f, (std::abs (core) - onset)
                                                  / juce::jmax (0.05f, onset));

        // Copper rectification, gated on how loaded the core is. See note 3 above:
        // this term is zero until the signal is into the bend, which is what makes
        // the stage's symmetry at low level honest rather than a convenient
        // constant. Below the onset it is exactly zero, so an unloaded transformer
        // adds no even content at all - measured at -140 dB and falling.
        const float rectification = asymmetry * 0.30f * engaged * bent * bent;

        // The bloom. A core that is saturating is a smaller impedance, and a
        // smaller winding impedance lets more low end through, so the bend is not
        // only compression but a gain. This is the thump every overloaded
        // transformer has, and it is applied to the core path only, which is what
        // keeps it a low-end event instead of turning into brightness.
        //
        // It is gated on `engaged` for the same reason the rectification is, and
        // that gate is not cosmetic: ungated, the bloom multiplies the core at
        // every level, so the curve's slope at the origin would be 1.15 rather
        // than 1 and this stage would quietly be the one that changes the level
        // when it is crossfaded in. An unloaded core is a linear inductor and
        // cannot bloom at all, so gating it is also the truer model.
        //
        // The two together - compression here, bloom there - are what give the
        // stage the shape a transformer actually has: the level still comes up
        // when you push into one, but the INCREMENTAL gain collapses, and that
        // is the part you hear.
        const float bloom = 1.0f + load * 0.15f * engaged
                              * (1.0f - juce::jmin (1.0f, std::abs (bent)));

        const float out = air + bent * bloom + rectification;

        // No zero-point correction, and that is not an oversight. Every other
        // curve applies its BIAS to the input and has to take the pedestal back
        // off the curve's own value at zero; here BIAS only modulates the
        // rectification term, and that term is gated on `engaged`, which is
        // gated on the core being past the onset. Zero in is therefore zero
        // out by construction - the state is the low-pass of the input, so it is
        // zero too - and a correction would only be the same number subtracted
        // from itself.
        return out;
    }

    // The corner the ceiling resolves over, in units of full scale. A mathematical
    // clamp has a slope discontinuity at its threshold, and that is not a sound a
    // converter can make: its output moves in whole code steps, so the transition
    // is resolved over a sample or two rather than instantly. Resolving it over
    // this corner is also what keeps the curve from aliasing as violently as a raw
    // clamp would.
    static constexpr float digitalCorner = 0.06f;

    // The converter family's voice, from DIGITAL TYPE: how deep the word length
    // is cut (which is where the ceiling falls) and how long the output holds
    // (a sample-and-hold at depth 0 is the identity).
    float digitalDepth = 0.0f;    // 0 = full code, 1 = 8-bit grind
    float digitalHoldAmount = 0.0f;

    void setDigitalVoice (int type) noexcept
    {
        // OFF: see setValveVoice for what OFF means on a principle's own model
        // list - the principle is removed, not merely turned down.
        digitalOff = (type >= digitalTypeCount - 1);
        if (digitalOff)
            return;

        static constexpr float dep[5] = { 0.00f, 0.35f, 0.80f, 1.00f, 0.60f };
        static constexpr float hld[5] = { 0.00f, 0.10f, 0.30f, 0.20f, 1.00f };
        digitalDepth      = dep[type];
        digitalHoldAmount = hld[type];
    }

    bool digitalOff = false;

    /**
        DIGITAL - a converter's ceiling.

        The odd one out, and deliberately the far end of the BLEND sweep: every
        other principle is a machine that bends an analogue signal, and this one
        does not have a medium, a head or an operating point at all. It has the two
        things none of the others have - a ceiling that is a hard stop rather than a
        knee, and a value the output HOLDS rather than a curve it draws. Both
        matter, and for different reasons: the ceiling is where the odd harmonics
        come from, and the hold is what puts them at the very top of the band
        instead of on the tape's low orders.
    */
    float shapeDigital (float x, float drive, float asymmetry) noexcept
    {
        // A converter is symmetric by construction: there is no domain to remember
        // and no bias that drifts with the programme, so BIAS is carried here as
        // a small static trim and nothing more. That absence is most of the
        // character - the valve's warmth comes from exactly the term this curve
        // does not have.
        const float biased = x + asymmetry * 0.08f;

        // Full scale, and it falls as the gain rises: DRIVE's only job on this
        // curve is to push more of the signal into the stop. Below the stop the
        // path is the identity - bit-exact, unity slope - which is what makes the
        // curve interchangeable in the blend and keeps the BLEND from changing
        // level as it moves along the sweep.
        const float threshold = 1.0f - (juce::jlimit (0.0f, shaperDriveRange, drive)
                                             / shaperDriveRange) * 0.55f
                                - digitalCorner
                                - digitalDepth * 0.12f;

        const float sign = biased < 0.0f ? -1.0f : 1.0f;
        const auto over = std::abs (biased) - threshold;

        float clipped = biased;
        float engaged = 0.0f;

        if (over > 0.0f)
        {
            const auto t = juce::jlimit (0.0f, 1.0f, over / (2.0f * digitalCorner));
            const auto eased = t * t * (3.0f - 2.0f * t);   // smoothstep
            clipped = sign * (threshold + eased * (2.0f * digitalCorner));
            engaged = t;
        }

        // The hold. A digital stage does not draw a curve, it picks a value and
        // keeps it for the sample period, so a digital clip's flat top is genuinely
        // held rather than merely compressed. Fading that hold in by `engaged` is
        // what leaves the linear part of the transfer untouched, and it is this
        // memory - not the ceiling - that is the digital counterpart of the tape
        // shaper's hysteresis: it is what puts these orders at the very top of the
        // band instead of on the tape's low ones. The mix is a convex one, so the
        // output can never leave the range the two inputs already span.
        const float held = digitalHold;
        const float out = clipped + (held - clipped) * engaged
                                        * (drive * 0.35f + digitalHoldAmount * 1.6f);
        digitalHold = clipped;

        // The zero-point correction, the same rule as the other four, walked along
        // the same path. Only the hold is absent from it - it is state, not a
        // function of the input - exactly as the lagged branch is absent from
        // shapeTape's correction.
        const auto atZero = [&]
        {
            const float z = asymmetry * 0.08f;
            if (std::abs (z) <= threshold)
                return z;

            const auto t = juce::jlimit (0.0f, 1.0f, (std::abs (z) - threshold)
                                                    / (2.0f * digitalCorner));
            const auto e = t * t * (3.0f - 2.0f * t);
            return (z < 0.0f ? -1.0f : 1.0f) * (threshold + e * (2.0f * digitalCorner));
        }();

        return out - atZero;
    }

    /**
        Runs the mix. `drive` and `asymmetry` are the same arguments the tape
        shaper has always taken, so the existing controls keep their meaning;
        the weighting is only what the seven SOURCE knobs set.
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

        if (vinylWeight > 1.0e-4f)
            out += shapeVinyl (x, drive, asymmetry) * vinylWeight;

        if (ampWeight > 1.0e-4f)
            out += shapeAmp (x, drive, asymmetry) * ampWeight;

        if (transformerWeight > 1.0e-4f)
        {
            out += shapeTransformer (x, drive, asymmetry) * transformerWeight;
        }
        else if (std::abs (transformerFlux) > 1.0e-9f)
        {
            // The same reason as the digital hold below: this curve is not called
            // while it is out of the blend, so without this the core's one-pole
            // would keep whatever it last saw and the first sample back on the
            // transformer side of the sweep would be shaped around a stale flux.
            transformerFlux = 0.0f;
        }

        if (digitalWeight > 1.0e-4f)
        {
            out += shapeDigital (x, drive, asymmetry) * digitalWeight;
        }
        else if (std::abs (digitalHold) > 1.0e-9f)
        {
            // Release the converter's held code when BLEND has left the digital
            // end. shapeDigital is not called there, so without this the hold
            // would keep whatever it last saw and the first sample back on the
            // digital side of the sweep would be mixed toward a stale value from
            // whenever the control was last there.
            digitalHold = 0.0f;
        }

        // The AMOUNT, as a crossfade from the input to the shaped blend.
        //
        // Not as a scale on the drive, which was the first thing tried and is
        // wrong for a specific reason: every curve below is a tanh of some
        // kind, and tanh saturates at any drive at all - the DRIVE argument
        // controls how HARD the knee is, not whether there is one. Multiplying
        // it down cannot reach "straight", so a part at a twentieth of the
        // amount would still be a saturator, just a gentle one, and the bottom
        // of the seven SOURCE knobs would keep colouring the signal no matter
        // how far down it was turned.
        //
        // Fading the blend against the input does reach it: at zero it is the
        // input sample-for-sample, and every value between is that stage's own
        // curve heard at that depth - which is what turning a saturation knob
        // down does on the machine. The per-principle states above still run
        // the full curve while the amount is low, so the hysteresis, the bias
        // drift and the converter's held code stay warm and come back at the
        // right phase instead of starting cold.
        return x + (out - x) * saturationAmount;
    }
};
