// =============================================================================
//  The saturation core: the nine principles, and the five type lists that voice
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
#include "ModelTypeList.h"

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

static_assert (j37model::namesAreComplete (valveTypeNames),
               "valveTypeCount and valveTypeNames must describe the same number of valve types");

inline juce::StringArray valveTypeNameList() { return j37model::toStringArray (valveTypeNames); }

// -- TRANSFORMER ---------------------------------------------------------------
inline constexpr int transformerTypeCount = 6;

inline constexpr std::array<const char*, transformerTypeCount> transformerTypeNames
{
    "Jensen JT-11P", "Cinemag CM-7510", "Lundahl LL1544", "OEP A262", "Sowter 4381",
    "Off"   // see the note on tapeStockNames: OFF is appended, never inserted
};

static_assert (j37model::namesAreComplete (transformerTypeNames),
               "transformerTypeCount and transformerTypeNames must describe the same number of transformer types");

inline juce::StringArray transformerTypeNameList() { return j37model::toStringArray (transformerTypeNames); }

// -- DIGITAL -------------------------------------------------------------------
inline constexpr int digitalTypeCount = 6;

inline constexpr std::array<const char*, digitalTypeCount> digitalTypeNames
{
    "16-bit", "12-bit", "8-bit", "Bit Crush", "Sample Hold",
    "Off"   // see the note on tapeStockNames: OFF is appended, never inserted
};

static_assert (j37model::namesAreComplete (digitalTypeNames),
               "digitalTypeCount and digitalTypeNames must describe the same number of digital types");

inline juce::StringArray digitalTypeNameList() { return j37model::toStringArray (digitalTypeNames); }

// -- PENTODE -------------------------------------------------------------------
//  The small-signal pentodes a guitar stage is built around. Each one sets how
//  early its knee arrives (the slope) and how much even content it leaves: an
//  EF86 is the tightest and most odd-dominant, a 6SJ7 is the softest.
inline constexpr int pentodeTypeCount = 5;

inline constexpr std::array<const char*, pentodeTypeCount> pentodeTypeNames
{
    "EF86", "6SJ7", "12BY7", "6AU6",
    "Off"   // see the note on tapeStockNames: OFF is appended, never inserted
};

static_assert (j37model::namesAreComplete (pentodeTypeNames),
               "pentodeTypeCount and pentodeTypeNames must describe the same number of pentode types");

inline juce::StringArray pentodeTypeNameList() { return j37model::toStringArray (pentodeTypeNames); }

// -- TRIODE --------------------------------------------------------------------
//  The low-mu triodes and driver valves. Each sets its operating point's drift
//  (coldness and sag), how late it bends (the slope) and how much even content it
//  carries.
inline constexpr int triodeTypeCount = 5;

inline constexpr std::array<const char*, triodeTypeCount> triodeTypeNames
{
    "12AT7", "12AU7", "6SN7", "2A3",
    "Off"   // see the note on tapeStockNames: OFF is appended, never inserted
};

static_assert (j37model::namesAreComplete (triodeTypeNames),
               "triodeTypeCount and triodeTypeNames must describe the same number of triode types");

inline juce::StringArray triodeTypeNameList() { return j37model::toStringArray (triodeTypeNames); }

// -- TRANSISTOR ----------------------------------------------------------------
//  Germanium and silicon stages. The type sets the crossover notch's width, the
//  two halves' slopes and the even content - the three things that separate one
//  transistor stage's grain from another's.
inline constexpr int transistorTypeCount = 5;

inline constexpr std::array<const char*, transistorTypeCount> transistorTypeNames
{
    "AC128", "OC71", "AF126", "2N3906",
    "Off"   // see the note on tapeStockNames: OFF is appended, never inserted
};

static_assert (j37model::namesAreComplete (transistorTypeNames),
               "transistorTypeCount and transistorTypeNames must describe the same number of transistor types");

inline juce::StringArray transistorTypeNameList() { return j37model::toStringArray (transistorTypeNames); }

//==============================================================================
/**
    The nine saturation principles, and the mix that combines them.

    A tape machine is ONE of the ways electronics bend a signal, and the plugin
    was built around that one curve. The nine below are the distinct mechanisms a
    real signal chain uses, and they are genuinely different shapes rather than
    nine settings of one:

      TAPE        - magnetic hysteresis: a MEMORY term, because the medium's state
                    depends on where it has been. Gentle at low level, and the
                    asymmetry is what makes the even harmonics.
      VALVE       - thermionic: a soft, strongly ASYMMETRIC knee with a wide
                    transition. Even-dominant, and it compresses rather than
                    clips, so it thickens before it distorts.
      PENTODE     - a sharp-cutoff pentode: the knee arrives early and the curve
                    is close to symmetric, so it is odd-dominant and hard-edged
                    where the valve is soft. The "tight" end of a valve amp.
      TRIODE     - a low-mu triode: the softest knee here and the most even
                    content, because the stage's operating point moves with the
                    programme. Warm at any level, and never sharp.
      TRANSISTOR  - a germanium stage: asymmetric conduction with a crossover
                    notch around zero, so it adds even harmonics and a grain the
                    valve curves do not have.
      FOLD        - a wavefolder: the signal is driven into a sine-shaped
                    transfer curve and folds back on itself. Odd-dominant and
                    level-dependent in the way only a folder is.
      GLASS       - a pure second-order term, and that is the whole curve:
                    y = x + a*x^2 with the offset the term creates removed.
                    It adds the SECOND harmonic and nothing else - no third, no
                    fifth, no edge.
      TRANSFORMER - core saturation with a flux memory: the core lags whatever
                    drives it, and its asymmetry arrives only once the core is
                    properly loaded.
      DIGITAL     - the converter's stop: transparent and bit-exact below full
                    scale, hard above it, with no memory and no bias to drift.

    Mixing them is not a gimmick: a real chain is exactly this. A guitar goes
    into an amp (PENTODE or TRIODE), through a pedal's folder (FOLD) or a
    germanium stage (TRANSISTOR), into a desk and a tape machine (TAPE), a valve
    compressor somewhere in the path (VALVE), and the whole thing may end up on
    a converter (DIGITAL). The nine SOURCE knobs set each mechanism's share of
    the mix, and the default sits on tape because that is what the plugin is
    calibrated around.

    Every curve is normalised to unity slope at the origin, exactly like
    magneticHysteresis, so the mix cannot change the level - only the shape.
    That property is what makes the controls usable: moving them changes the
    harmonics, not the gain, and the DRIVE control keeps its own meaning.
    GLASS is the one curve whose unity slope is only approached above its DC
    blocker's corner - the blocker is what removes the offset a squared term
    leaves behind, and it is documented at shapeGlass.

    (CASSETTE, VINYL and AMP were retired here: the cassette was a narrow-gauge
    TAPE, the vinyl a gentler TAPE with a lathe's knee, and the amp's cascade is
    what PENTODE and TRIODE now describe as two separate stages. Saved states
    and presets that named them are folded onto TAPE and VALVE by
    migrateStateFormat, which documents the mapping.)
*/
struct SaturationCore
{
    // -----------------------------------------------------------------------
    //  Weights. These are set once per block from the nine SOURCE controls:
    //  each is its own amount knob, so a user dials the machines they want by
    //  name rather than steering a sweep. They are kept normalised so the nine
    //  always sum to 1 and the mix is a true crossfade rather than a stack.
    // -----------------------------------------------------------------------
    float tapeWeight = 1.0f;
    float valveWeight = 0.0f;
    float pentodeWeight = 0.0f;
    float triodeWeight = 0.0f;
    float transistorWeight = 0.0f;
    float foldWeight = 0.0f;
    float glassWeight = 0.0f;
    float transformerWeight = 0.0f;
    float digitalWeight = 0.0f;

    //  ...and the AMOUNT they asked for before they were normalised. The nine
    //  above answer WHICH machines; this answers HOW MUCH, and it is the
    //  crossfade depth process() applies to the whole blend. See setWeights for
    //  why it cannot be recovered from the shares.
    float saturationAmount = 1.0f;

    /** Sets all nine weights DIRECTLY from the nine SOURCE knobs: a knob is
        its principle's share, so TAPE 60 / VALVE 40 reads on the panel exactly
        as it sounds. Shares are normalised to sum to 1; a principle whose own
        model list is OFF has its share redistributed by the normalisation
        rather than dropped, so switching a machine off re-weights the rest
        instead of leaving a hole. (The old blend/shape pair hid all of this
        behind one sweep position and one spread; the user asked for names.)
        The amounts arrive already smoothed - every SOURCE knob is read once
        per block and fed to its own smoother - so the SET is what makes all
        nine live at once, and the normalisation below is what keeps the mix
        level-stable while amounts move.
    */
    void setWeights (float tapeAmount, float valveAmount, float pentodeAmount,
                     float triodeAmount, float transistorAmount, float foldAmount,
                     float glassAmount, float transformerAmount,
                     float digitalAmount) noexcept
    {
        tapeWeight        = juce::jlimit (0.0f, 1.0f, tapeAmount);
        valveWeight       = juce::jlimit (0.0f, 1.0f, valveAmount);
        pentodeWeight     = juce::jlimit (0.0f, 1.0f, pentodeAmount);
        triodeWeight      = juce::jlimit (0.0f, 1.0f, triodeAmount);
        transistorWeight  = juce::jlimit (0.0f, 1.0f, transistorAmount);
        foldWeight        = juce::jlimit (0.0f, 1.0f, foldAmount);
        glassWeight       = juce::jlimit (0.0f, 1.0f, glassAmount);
        transformerWeight = juce::jlimit (0.0f, 1.0f, transformerAmount);
        digitalWeight     = juce::jlimit (0.0f, 1.0f, digitalAmount);

        // A principle whose own model list is OFF contributes nothing, and its
        // share is REDISTRIBUTED by the normalisation below rather than dropped:
        // switching a machine off re-weights the rest to cover it, so the mix
        // holds its degree of saturation and only the character changes.
        if (valveOff)       valveWeight = 0.0f;
        if (pentodeOff)     pentodeWeight = 0.0f;
        if (triodeOff)      triodeWeight = 0.0f;
        if (transistorOff)  transistorWeight = 0.0f;
        if (transformerOff) transformerWeight = 0.0f;
        if (digitalOff)     digitalWeight = 0.0f;
        if (tapeOff)        tapeWeight = 0.0f;

        const auto sum = tapeWeight + valveWeight + pentodeWeight + triodeWeight
                               + transistorWeight + foldWeight + glassWeight
                               + transformerWeight + digitalWeight;

        // HOW MUCH saturation, kept separate from WHICH machines.
        //
        // The normalisation below answers the second question - the nine
        // numbers always become shares that sum to one, so the MIX page's bar
        // always reads 100 % whatever the knobs say. That was the whole point
        // of shares, and it had a consequence nobody intended: the SOURCE knobs
        // said NOTHING about how hard the stage worked. Turn all of them down to
        // nothing and the shares still summed to one, so the core ran at full
        // strength on whichever machine was nominally leading - the panel's own
        // minimum setting saturated exactly as hard as its maximum.
        //
        // So the sum is kept, and it drives the drive: one machine at half is
        // now half the saturation with a pure-tape character, rather than a
        // full-strength stage wearing a tape label. Saturation reaches its
        // ceiling once the nine together add up to one, which is where the
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
            pentodeWeight /= sum;
            triodeWeight /= sum;
            transistorWeight /= sum;
            foldWeight /= sum;
            glassWeight /= sum;
            transformerWeight /= sum;
            digitalWeight /= sum;
        }
        else
        {
            // All sources at zero. The character still has to resolve to
            // SOMETHING, and it used to resolve to pure tape - which put the
            // panel's mix bar at 100 % TAPE the moment the user turned the last
            // knob down, as if the plugin had chosen a machine for them. An
            // empty mix has no leading principle, so the shares are divided
            // EQUALLY: the bar reads nine equal ninths, which is the honest
            // picture of "nothing dialled". saturationAmount above is zero, so
            // process() hands every curve a drive of nothing and the core
            // returns its input bit-for-bit either way - these shares describe
            // a mix that is not playing, and they should look like one.
            static constexpr float emptyMixShare = 1.0f / 9.0f;
            tapeWeight = valveWeight = pentodeWeight = triodeWeight = transistorWeight
                       = foldWeight = glassWeight = transformerWeight = digitalWeight
                       = emptyMixShare;
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
    float pentodeBiasState = 0.0f;
    float triodeBiasState = 0.0f;
    float transistorBiasState = 0.0f;
    float glassDrift = 0.0f;
    float transformerFlux = 0.0f;
    float digitalHold = 0.0f;

    void reset() noexcept
    {
        tapeMemory = 0.0f;
        valveBiasState = 0.0f;
        pentodeBiasState = 0.0f;
        triodeBiasState = 0.0f;
        transistorBiasState = 0.0f;
        glassDrift = 0.0f;
        transformerFlux = 0.0f;
        digitalHold = 0.0f;
    }

    // (The old blend/shape sweep body was removed with the controls that drove
    //  it: the nine SOURCE knobs now speak to setWeights above directly.)

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

    // The PENTODE family's voice, from PENTODE TYPE: how early the knee arrives
    // (the slope) and how much even content the stage leaves (the asymmetry
    // weight). Defaults are the 12BY7-ish centre the engine shipped with.
    float pentodeSlope = 3.2f;
    float pentodeEven  = 0.10f;

    void setPentodeVoice (int type) noexcept
    {
        // OFF: see setValveVoice for what OFF means on a principle's own model
        // list - the principle is removed, not merely turned down.
        pentodeOff = (type >= pentodeTypeCount - 1);
        if (pentodeOff)
            return;

        static constexpr float slope[4] = { 3.60f, 2.80f, 3.20f, 4.00f };
        static constexpr float even [4] = { 0.06f, 0.14f, 0.10f, 0.04f };
        pentodeSlope = slope[type];
        pentodeEven  = even [type];
    }

    bool pentodeOff = false;
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
        PENTODE - a sharp-cutoff small-signal pentode.

        The tight end of a valve amp: the knee arrives early because the stage
        has gain to spare, and the curve is close to symmetric, so the odd
        orders dominate and the sound is hard-edged where shapeValve is soft.
        A small bias term keeps a trace of even content rather than a pure odd
        series, which would read as a fuzz.
    */
    // AMP BIAS: the valve-amp stages' DC operating point, 0..1 with 0.5 neutral.
    //
    // It used to belong to shapeAmp alone. That curve is gone - the amp's
    // cascade is what PENTODE and TRIODE now describe - but the CONTROL is still
    // on the panel and still means what its name says, so it biases the two
    // curves that replaced it. Nothing else in the plugin gates it: it works
    // whatever NAM model may or may not be loaded, because it is a property of
    // the saturation stage rather than of a loaded file.
    float ampBiasAmount = 0.5f;

    void setAmpBiasAmount (float amount) noexcept
    {
        ampBiasAmount = juce::jlimit (0.0f, 1.0f, amount);
    }

    /** AMP BIAS as a bias offset, in signal units: zero at the 0.5 centre. */
    float ampBiasOffset() const noexcept { return (ampBiasAmount - 0.5f) * 0.10f; }

    float shapePentode (float x, float drive, float asymmetry) noexcept
    {
        const float biased = x + asymmetry * pentodeEven + ampBiasOffset();

        // The steepest slope of any analogue curve here: a pentode clips early,
        // and PENTODE TYPE moves the corner - an EF86 arrives sooner than a 6SJ7.
        const float slope = 1.0f + drive * pentodeSlope;
        const float shaped = j37Tanh (biased * slope) / slope;

        // Zero-point correction, the rule every curve here follows.
        const float atZero = j37Tanh ((asymmetry * pentodeEven + ampBiasOffset()) * slope)
                             / slope;
        return shaped - atZero;
    }

    /**
        TRIODE - a low-mu triode, the softest knee here.

        Where the pentode is tight, the triode is loose: its curve compresses
        across the widest range of any of these, and its operating point drifts
        with the programme, which is where the even content comes from. Warm at
        any level and never sharp - the opposite pairing to PENTODE, and the
        reason both exist.
    */
    float triodeColdness = 0.40f;
    float triodeSag      = 0.55f;
    float triodeSlope    = 0.90f;   // how late the knee arrives
    float triodeEven     = 0.85f;   // the squared term's weight

    void setTriodeVoice (int type) noexcept
    {
        // OFF: see setValveVoice for what OFF means on a principle's own model
        // list - the principle is removed, not merely turned down.
        triodeOff = (type >= triodeTypeCount - 1);
        if (triodeOff)
            return;

        static constexpr float cold [4] = { 0.45f, 0.55f, 0.60f, 0.70f };
        static constexpr float sag  [4] = { 0.45f, 0.60f, 0.70f, 0.35f };
        static constexpr float slope[4] = { 1.10f, 0.85f, 0.75f, 0.65f };
        static constexpr float even [4] = { 0.75f, 0.95f, 1.00f, 1.10f };
        triodeColdness = cold [type];
        triodeSag      = sag  [type];
        triodeSlope    = slope[type];
        triodeEven     = even [type];
    }

    bool triodeOff = false;

    float shapeTriode (float x, float drive, float asymmetry) noexcept
    {
        // A slower drift than the valve's: a low-mu stage's bias is set by a
        // large cathode resistor, so the operating point moves gently.
        triodeBiasState += (x - triodeBiasState) * (0.00025f + 0.0005f * triodeSag);
        const float driftingBias = asymmetry
                                 + triodeBiasState * 0.20f
                                 - triodeColdness * 0.020f
                                 + ampBiasOffset() * 1.5f;

        const float biased = x + driftingBias;

        // The gentlest slope of the three valve-family curves: it bends late and
        // keeps bending - and TRIODE TYPE moves how late (a 2A3 is the softest).
        const float slope = 1.0f + drive * triodeSlope;
        const float soft = j37Tanh (biased * slope) / slope;

        // The even content is carried by the squared term, as on the valve, but
        // stronger: this is the warm curve.
        const float asymmetryTerm = driftingBias * triodeEven * soft * soft;

        const float softAtZero = j37Tanh (driftingBias * slope) / slope;
        const float asymmetryAtZero = driftingBias * triodeEven * softAtZero * softAtZero;

        return (soft - softAtZero) - (asymmetryTerm - asymmetryAtZero);
    }

    /**
        TRANSISTOR - a germanium stage.

        Asymmetric conduction plus a crossover notch around zero: the two halves
        of the waveform are not handed over evenly, which puts even content into
        the output and a grain on the low orders that the valve curves do not
        have. The notch is what separates it from shapeValve - a valve has a
        bias point, a germanium stage has a dead band around it.
    */
    // The TRANSISTOR family's voice, from TRANSISTOR TYPE: the notch's width, the
    // two halves' slopes and the even weight. Defaults are the AC128-ish stage
    // the engine shipped with; a silicon BC109 crosses far more cleanly than a
    // germanium OC71, which is the difference the type list exists to offer.
    float transistorNotch   = 0.060f;
    float transistorSlopePos = 2.0f;
    float transistorSlopeNeg = 2.6f;
    float transistorEven    = 0.30f;

    void setTransistorVoice (int type) noexcept
    {
        // OFF: see setValveVoice for what OFF means on a principle's own model
        // list - the principle is removed, not merely turned down.
        transistorOff = (type >= transistorTypeCount - 1);
        if (transistorOff)
            return;

        static constexpr float notch[4] = { 0.060f, 0.085f, 0.070f, 0.030f };
        static constexpr float pos  [4] = { 2.00f, 1.90f, 2.10f, 2.40f };
        static constexpr float neg  [4] = { 2.60f, 2.90f, 2.70f, 2.40f };
        static constexpr float even [4] = { 0.30f, 0.38f, 0.34f, 0.18f };
        transistorNotch    = notch[type];
        transistorSlopePos = pos  [type];
        transistorSlopeNeg = neg  [type];
        transistorEven     = even [type];
    }

    bool transistorOff = false;

    float shapeTransistor (float x, float drive, float asymmetry) noexcept
    {
        transistorBiasState += (x - transistorBiasState) * 0.002f;
        const float biased = x + asymmetry * 0.22f + transistorBiasState * 0.10f;

        // The crossover notch: a soft dead band that closes as the drive rises,
        // because a harder-driven stage crosses it faster. Written as a
        // smoothstep so the slope at the origin stays 1 - a hard subtraction
        // would give the curve a kink that no amplifier has.
        const float notch = transistorNotch / (1.0f + drive * 3.0f);
        const float magnitude = std::abs (biased);
        const float eased = magnitude / (magnitude + notch);
        const float shifted = (biased < 0.0f ? -1.0f : 1.0f) * magnitude * eased;

        // The conduction asymmetry: the slope differs between the halves, which
        // is the even content. TRANSISTOR TYPE sets both halves.
        const float slopePos = 1.0f + drive * transistorSlopePos;
        const float slopeNeg = 1.0f + drive * transistorSlopeNeg;
        const float slope = shifted < 0.0f ? slopeNeg : slopePos;
        const float shaped = j37Tanh (shifted * slope) / slope;

        // The squared term carries the rest of the even series, and the zero
        // correction takes its pedestal back off.
        const float asymmetryTerm = asymmetry * transistorEven * shaped * shaped;
        const float z = asymmetry * 0.22f;
        const float zEased = std::abs (z) / (std::abs (z) + notch);
        const float zShifted = (z < 0.0f ? -1.0f : 1.0f) * std::abs (z) * zEased;
        const float zSlope = zShifted < 0.0f ? slopeNeg : slopePos;
        const float zShaped = j37Tanh (zShifted * zSlope) / zSlope;
        const float asymmetryAtZero = asymmetry * transistorEven * zShaped * zShaped;

        return (shaped - zShaped) - (asymmetryTerm - asymmetryAtZero);
    }

    /**
        FOLD - a wavefolder.

        The one curve here that is not a saturation in the strict sense: it does
        not approach a ceiling, it turns back. The transfer function is a sine,
        so once the driven signal passes a quarter of the sine's period it folds
        instead of flattening, and the odd series that produces is much richer
        than any tanh's. Normalised by its own slope so the origin still reads 1.
    */
    float shapeFold (float x, float drive, float asymmetry) noexcept
    {
        const auto load = juce::jlimit (0.0f, shaperDriveRange, drive) / shaperDriveRange;

        // The fold gain. At zero drive it is 1 and the curve is close to the
        // identity over the signal's own range; the more drive, the more times
        // the signal wraps.
        const float foldGain = 1.0f + load * 3.6f;

        // The asymmetry walks the fold's own centre, so the two halves wrap at
        // different points - which is where the even content comes from. It is
        // applied BEFORE the fold, because a folder's bias is part of its input.
        const float biased = x + asymmetry * 0.25f;

        const float folded = std::sin (biased * foldGain) / foldGain;
        const float atZero = std::sin ((asymmetry * 0.25f) * foldGain) / foldGain;

        return folded - atZero;
    }

    /**
        GLASS - second harmonic only.

        The whole curve is y = x + a*x^2, and that is deliberate: a square term
        produces the second harmonic and NOTHING else, so this is the one model
        here with no odd content at all - no third, no fifth, no edge. It is the
        curve for "add body without adding dirt".

        A squared term also leaves a level-dependent OFFSET (the mean of x^2 is
        not zero), which would shift the mix's operating point as the source
        knob moves. The one-pole below is a DC blocker at a few hertz: it removes
        that offset and nothing else. It is state, like the tape's memory, so
        process() resets it when the glass share leaves the mix - otherwise the
        first sample back would be corrected against a stale mean.

        Slope at the origin is 1 above the blocker's corner, which is the band
        the mix is auditioned in.
    */
    static constexpr float glassDcPole = 0.0004f;   // ~7 Hz at 48 kHz

    float shapeGlass (float x, float drive, float asymmetry) noexcept
    {
        (void) asymmetry;   // a square term has no bias to apply: it is symmetric

        const auto load = juce::jlimit (0.0f, shaperDriveRange, drive) / shaperDriveRange;
        const float quadratic = 0.08f + load * 0.60f;

        const float shaped = x + quadratic * x * x;

        // The DC blocker, and the whole of this curve's state.
        glassDrift += (shaped - glassDrift) * glassDcPole;
        return shaped - glassDrift;
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
        // like its neighbours. A pentode has a hard corner, a converter has a
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
        the weighting is only what the nine SOURCE knobs set.
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

        if (pentodeWeight > 1.0e-4f)
            out += shapePentode (x, drive, asymmetry) * pentodeWeight;

        if (triodeWeight > 1.0e-4f)
            out += shapeTriode (x, drive, asymmetry) * triodeWeight;

        if (transistorWeight > 1.0e-4f)
            out += shapeTransistor (x, drive, asymmetry) * transistorWeight;

        if (foldWeight > 1.0e-4f)
            out += shapeFold (x, drive, asymmetry) * foldWeight;

        if (glassWeight > 1.0e-4f)
        {
            out += shapeGlass (x, drive, asymmetry) * glassWeight;
        }
        else if (std::abs (glassDrift) > 1.0e-9f)
        {
            // The blocker's mean is stale once glass leaves the blend: without
            // this the first sample back would be corrected against it.
            glassDrift = 0.0f;
        }

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
            // would keep whatever it last saw.
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
        // of the nine SOURCE knobs would keep colouring the signal no matter
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
