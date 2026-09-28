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

// ------------------------------------------------------------------------------
//  Real-time-safe third-party utilities.
//
//  Each of these is included behind a __has_include guard AND a harness guard,
//  exactly like chowdsp above, because tests/dsp/extract.py cuts the DSP structs
//  out of this header and compiles them against a shim that has neither JUCE's
//  full include path nor any of these libraries. The engine must therefore stay
//  compilable with all four macros 0, and every use site must have a portable
//  substitution rather than assuming the library is present.
//
//    fatoml::ReaderWriterQueue  cameron314/readerwriterqueue - the lock-free
//                               single-producer/single-consumer queue. Used for
//                               the audio->UI telemetry stream, so the audio
//                               thread can publish a frame of metering and the
//                               editor can read it WITHOUT every field being its
//                               own atomic and without a lock on the audio thread.
//
//    farbot::RealtimeObject     hogliux/farbot - a realtime-safe object holder.
//                               Used for the machine-state hand-off, so the
//                               message thread can install a whole new engine
//                               configuration without the audio thread ever
//                               blocking on it.
//
//    signalsmith-stretch        Signalsmith Audio - a real-time phase-vocoder
//                               time stretcher. Used by the transport so a speed
//                               change is a genuine TIME change (the tape slows
//                               down and the music slows with it) rather than a
//                               resample that also drops the pitch of everything.
// ------------------------------------------------------------------------------
#if ! defined (J37_DSP_HARNESS)

 // readerwriterqueue: the header sits at the repository ROOT and the CMake target
 // is INTERFACE with that root as its include directory, so it is spelled
 // <readerwriterqueue.h>, not <readerwriterqueue/readerwriterqueue.h>. Both
 // spellings are probed, because some packaging layouts nest it under the project
 // name and the guard must not depend on which of them the fetch produced.
 #if __has_include (<readerwriterqueue.h>)
  #include <readerwriterqueue.h>
  #define J37_HAS_RWQ 1
 #elif __has_include (<readerwriterqueue/readerwriterqueue.h>)
  #include <readerwriterqueue/readerwriterqueue.h>
  #define J37_HAS_RWQ 1
 #else
  #define J37_HAS_RWQ 0
 #endif

 // farbot: its headers live under include/farbot, and the CMake target adds
 // include/ to the path, so the spelling is <farbot/...>. Two of its headers are
 // useful here - the realtime-safe FIFO and the RealtimeObject that guards it.
 #if __has_include (<farbot/fifo.hpp>)
  #include <farbot/fifo.hpp>
  #define J37_HAS_FARBOT 1
 #else
  #define J37_HAS_FARBOT 0
 #endif

 // signalsmith-stretch: one self-contained header under include/, which is what
 // the CMake target exposes.
 #if __has_include (<signalsmith-stretch/signalsmith-stretch.h>)
  #include <signalsmith-stretch/signalsmith-stretch.h>
  #define J37_HAS_SIGNALSMITH 1
 #else
  #define J37_HAS_SIGNALSMITH 0
 #endif

#else
 #define J37_HAS_RWQ 0
 #define J37_HAS_FARBOT 0
 #define J37_HAS_SIGNALSMITH 0
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
    TAPE TYPE's stock list, in one place, for the three places that need it.

    The control is described three times over: the choice parameter the host reads
    and the panel's attachment is bound to, the combo box that draws the names, and
    the engine's switch that implements them. Only the last two are on screen, so a
    disagreement between the parameter and the combo box hides from every angle that
    is easy to check - the panel draws all twelve, the engine implements all twelve,
    the DSP harness passes because it renders the engine directly rather than going
    through the parameter, and the build is clean because a short choice list is
    perfectly legal C++.

    That is not a hypothetical. The parameter declared eight while the panel and the
    engine had twelve, and because ComboBoxAttachment clamps the selection to the
    number of choices the parameter itself declares, the four newest stocks were
    listed on the panel, implemented in the engine, and impossible to select.

    So the names live here, once, and the parameter and the panel both build their
    lists from them; the two can no longer be written out of step. tapeStockCount is
    the count they agree on and the array below is sized to it, so adding a name
    without bumping the count - or bumping the count without adding a name - is a
    compile error rather than a stock that cannot be selected.

    The third list, the engine's switch, is checked at run time instead: C++ cannot
    count case labels. See the jassert that guards it.
*/
inline constexpr int tapeStockCount = 13;

inline constexpr std::array<const char*, tapeStockCount> tapeStockNames
{
    "J37", "Ampex 456", "Studer A800", "Chrome", "Type 111", "GP9",
    "Quantegy 499", "RTM SM911", "SM 468", "888", "815", "811",
    // The OFF entry, and it is deliberately LAST rather than first.
    //
    // A model list is indexed by NUMBER everywhere - the parameter, the
    // presets, the saved sessions, the engine's switch - so inserting an entry
    // at the front would renumber all twelve real stocks and silently load a
    // different tape into every existing session and factory preset. Appending
    // costs nothing but a scroll to the bottom of the list, and it is the only
    // ordering that keeps the existing twelve where they were.
    //
    // What OFF means is per-family, but it is always the same idea: the stage
    // does not exist. On a TAPE model list it makes the machine a pure
    // solid-state amplifier with no magnetic medium in it at all.
    "Off"
};

/** The index of the OFF entry in every model list. Named, because the engine
    tests for it in six different switches and a bare 12 would be a magic number
    that silently stops being right the moment a model is appended. */
inline constexpr int modelOffIndex = tapeStockCount - 1;

/**
    Whether every slot of tapeStockNames was actually given a name.

    std::array catches the count being too SMALL for the names - too many
    initialisers is a hard error. It does not catch it being too LARGE: elements
    past the initialiser list are value-initialised, so raising tapeStockCount
    without adding a name compiles clean and quietly hands the list a null pointer
    to hand to StringArray. This is that half, and it is the half that turns into a
    crash rather than a diagnostic.
*/
inline constexpr bool tapeStockNamesAreComplete()
{
    for (const auto* name : tapeStockNames)
        if (name == nullptr)
            return false;

    return true;
}

static_assert (tapeStockNamesAreComplete(),
               "tapeStockCount and tapeStockNames must describe the same number of tape stocks");

/**
    The same list as the juce::StringArray that both sides of the control need.

    A function rather than a constant because juce::StringArray is not a literal
    type, and because the constructor takes one; the names above remain the single
    source of truth and this is only the conversion. In the header rather than in
    either .cpp because the parameter is built in the processor and the combo box in
    the editor, and the whole point is that neither of them owns a copy.
*/
inline juce::StringArray tapeStockNameList()
{
    juce::StringArray names;

    for (const auto* name : tapeStockNames)
        names.add (name);

    return names;
}


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

// -- VINYL ---------------------------------------------------------------------
// Vinyl is the odd family: its "type" is not a voice on one curve but a record
// condition, and it does not bias a curve inside SaturationCore at all. The
// VinylStage the engine already runs carries the mechanism - crackle, rumble and
// the RIAA playback character - and the VINYL TYPE switch re-voices that stage:
// how a record is pressed, how worn it is and how the cutting lathe was set up
// are genuinely different sounds, which is exactly what a type selector means.
inline constexpr int vinylTypeCount = 7;

inline constexpr std::array<const char*, vinylTypeCount> vinylTypeNames
{
    "Standard LP", "Single", "Shellac 78", "Worn Classic", "Dubplate", "Half-Speed Master",
    "Off"   // see the note on tapeStockNames: OFF is appended, never inserted
};

inline constexpr bool vinylTypeNamesAreComplete()
{
    for (const auto* name : vinylTypeNames)
        if (name == nullptr)
            return false;
    return true;
}

static_assert (vinylTypeNamesAreComplete(),
               "vinylTypeCount and vinylTypeNames must describe the same number of vinyl types");

inline juce::StringArray vinylTypeNameList()
{
    juce::StringArray names;
    for (const auto* name : vinylTypeNames)
        names.add (juce::String (name));
    return names;
}

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
    //  they are kept normalised so the six always sum to 1 and the blend is a
    //  true crossfade rather than a stack.
    // -----------------------------------------------------------------------
    float tapeWeight = 1.0f;
    float valveWeight = 0.0f;
    float cassetteWeight = 0.0f;
    float ampWeight = 0.0f;
    float transformerWeight = 0.0f;
    float digitalWeight = 0.0f;

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
    float transformerFlux = 0.0f;
    float digitalHold = 0.0f;

    void reset() noexcept
    {
        tapeMemory = 0.0f;
        valveBiasState = 0.0f;
        ampBiasState = 0.0f;
        cassetteBiasState = 0.0f;
        transformerFlux = 0.0f;
        digitalHold = 0.0f;
    }

    /** Sets the six weights from two controls. Both are 0..1. */
    void setBlend (float blend, float shape) noexcept
    {
        // BLEND sweeps the weighting across the six principles in a fixed order,
        // tape -> valve -> cassette -> amp -> transformer -> digital, so the
        // control has one direction and the ear can learn it. The order is the
        // signal path rather than a ranking: five machines you overload by pushing
        // level into them, and then the converter that replaces all of them.
        // SHAPE skews the distribution: low concentrates on a single principle
        // (a focused, obvious character), high spreads it evenly (a blend that
        // reads as one compound machine).
        const auto b = juce::jlimit (0.0f, 1.0f, blend);
        const auto s = juce::jlimit (0.0f, 1.0f, shape);

        // Each principle gets a triangular response centred on its own position
        // along the sweep, so neighbouring ones overlap and the blend is smooth.
        const auto triangle = [] (float x, float centre, float width)
        {
            return juce::jmax (0.0f, 1.0f - std::abs (x - centre) / width);
        };

        // Six centres, so five intervals. Derived rather than typed in, because
        // the spacing is what the narrow end of the width below is measured
        // against, and a literal list of centres would let the two drift apart
        // the moment a principle was added.
        const auto step = 1.0f / 5.0f;

        // Width grows with SHAPE: at 0 the triangles are exactly disjoint - each
        // reaches half way to its neighbours - so the sweep snaps from one
        // principle to the next; at 1 they are wide enough that all six
        // contribute at every position. The narrow end is HALF THE SPACING rather
        // than a fixed number, so tightening the spacing cannot quietly turn the
        // "focused" end of the sweep into an overlapping one.
        const auto width = step * 0.5f + (1.0f - step * 0.5f) * s;

        tapeWeight        = triangle (b, 0 * step, width);
        valveWeight       = triangle (b, 1 * step, width);
        cassetteWeight    = triangle (b, 2 * step, width);
        ampWeight         = triangle (b, 3 * step, width);
        transformerWeight = triangle (b, 4 * step, width);
        digitalWeight     = triangle (b, 5 * step, width);

        // The ends of the sweep must not fall off the edge. At b = 0 only tape is
        // centred on the sweep, but SHAPE widens the triangles, so at the default
        // SHAPE the next principle is in range too - and leaving it in put roughly
        // a third of the valve curve into a setting documented as pure tape, so an
        // existing session did not load the machine it was saved with. Seeding an
        // end therefore claims its own principle AND clears the others, rather
        // than only overwriting its own. The sum is then exactly 1 at both ends
        // and the normalisation below has nothing to do.
        if (b < 0.02f) { tapeWeight = 1.0f; valveWeight = cassetteWeight = ampWeight
                                                   = transformerWeight = digitalWeight = 0.0f; }
        if (b > 0.98f) { digitalWeight = 1.0f; tapeWeight = valveWeight = cassetteWeight
                                                    = ampWeight   = transformerWeight = 0.0f; }

        // ------------------------------------------------------------------
        //  OFF principles.
        //
        //  A principle whose own model list is set to OFF contributes nothing,
        //  and its share is REDISTRIBUTED among the principles that are still
        //  present by the normalisation below - not simply dropped, which would
        //  make the machine quieter whenever a model was switched off.
        //
        //  The redistribution is the whole point of the feature: turning the
        //  TRANSFORMER off does not leave a hole in the blend, it re-weights the
        //  tape, the valve and the rest to cover it, so the machine still
        //  saturates to the same degree and only the CHARACTER changes. A user
        //  who wants a pure solid-state machine turns off the tape and gets
        //  whatever else is in the blend carrying the whole signal.
        //
        //  This runs after the two end-seeds above deliberately: an end-seed
        //  claims its principle outright, and if that principle is OFF the
        //  normalisation that follows is what keeps the output from vanishing.
        // ------------------------------------------------------------------
        if (valveOff)       valveWeight = 0.0f;
        if (ampOff)         ampWeight = 0.0f;
        if (transformerOff) transformerWeight = 0.0f;
        if (digitalOff)     digitalWeight = 0.0f;
        if (tapeOff)        tapeWeight = 0.0f;

        const auto sum = tapeWeight + valveWeight + cassetteWeight + ampWeight
                               + transformerWeight + digitalWeight;
        if (sum > 1.0e-6f)
        {
            tapeWeight /= sum;
            valveWeight /= sum;
            cassetteWeight /= sum;
            ampWeight /= sum;
            transformerWeight /= sum;
            digitalWeight /= sum;
        }
        else
        {
            tapeWeight = 1.0f;
            valveWeight = cassetteWeight = ampWeight = transformerWeight
                        = digitalWeight = 0.0f;
        }
    }

    /** True when nothing is contributing, so the caller can skip the whole core. */
    bool isIdle() const noexcept
    {
        return tapeWeight + valveWeight + cassetteWeight + ampWeight
                                    + transformerWeight + digitalWeight <= 1.0e-6f;
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
        const float stage1 = std::tanh (biased * slope1) / slope1;

        // Stage two: the cascade. A second, gentler stage applied to the first
        // stage's output, which is what flattens the middle of the curve.
        const float slope2 = 1.0f + drive * ampGain2;
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
        const float bent = std::tanh (core * slope) / slope;

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
    The preamp and the distortion stage - the two gain stages in FRONT of the
    machine, and they do different jobs.

    A real chain has a preamp before the recorder and, if the source wants it, a
    distortion pedal before that. Both are placed ahead of the tape so the machine
    hears their output, which is the whole point: a distorted guitar recorded to
    tape sounds like a record rather than a pedal precisely because the tape
    smooths what the pedal produced.

    PREAMP is a valve-ish input stage: a gentle soft clip, a transformer's
    low-cut, and a slight high-frequency lift. It is a STAGE, not a gain - driving
    it changes the colour as much as the level.

    DISTORTION is a diode clipper: a hard knee with a pre-gain, deliberately
    abrupt. Where the saturation core bends, this breaks.

    The DI BOX is a different kind of thing again, and it is first in the chain
    for a physical reason: a DI box is what a guitar or a synth is plugged into
    BEFORE anything else. It takes an unbalanced, high-impedance, instrument-level
    signal and hands a balanced, low-impedance, mic-level one to the desk. Three
    things in it are audible, and all three are modelled here:

      TRANSFORMER  a real DI has one, and it is why a DI'd guitar sounds
                   slightly soft on top and slightly fatter in the low-middle
                   than the same guitar plugged straight in. It is a behaviour,
                   not a fault.

      IMPEDANCE    the box's input impedance LOADS the source. A high-impedance
                   instrument (a passive guitar pickup, most obviously) has a
                   resonant peak in the top octave, and the load the DI presents
                   moves and damps that peak. This is the single largest reason
                   two DI boxes sound different on the same guitar.

      PAD          most DIs offer a -20 or -30 dB pad for a hot source. It is a
                   clean attenuation BEFORE the transformer, so an active synth
                   or a hot pedal can be plugged in without driving the box's
                   own core into saturation - which is a real and useful decision
                   rather than a level control.

    Being FIRST also means the DI changes what everything after it hears: the
    preamp, the saturation curve and the glue stages all respond to a padded or
    loaded signal differently, which is exactly how the hardware behaves.
*/
struct InputStage
{
    // The preamp's transformer low-cut and its own soft-clip bias drift.
    float preampLowState = 0.0f;
    float preampBiasState = 0.0f;

    // The distortion's own coupling state, so its hard clip does not leave a DC
    // pedestal behind it for the tape stage to swallow.
    float distortionLowState = 0.0f;

    // ------------------------------------------------------------------
    //  The DI box's state.
    //
    //    diLoadState      the input load's own split, which is where the
    //                     impedance damping happens
    //    diTransformerState  the DI transformer's low-end and its own slight
    //                     hysteresis thickness
    //    diGroundHumPhase the ground loop's mains phase, per channel
    // ------------------------------------------------------------------
    float diLoadState = 0.0f;
    float diTransformerState = 0.0f;
    float diGroundHumPhase = 0.0f;

    void reset() noexcept
    {
        preampLowState = 0.0f;
        preampBiasState = 0.0f;
        distortionLowState = 0.0f;
        diLoadState = 0.0f;
        diTransformerState = 0.0f;
        diGroundHumPhase = 0.0f;
    }

    /**
        Runs the preamp. `amount` is 0..1; at 0 the stage is a straight wire, so a
        machine with no preamp engaged is bit-for-bit what it always was.

        `lowCutCoefficient` is the transformer's low-cut, built by the caller.
    */
    float processPreamp (float x, float amount, float lowCutCoefficient) noexcept
    {
        if (amount <= 1.0e-5f)
            return x;

        // The transformer's low-cut: a one-pole high-pass taken out of the signal.
        // It is what stops a driven preamp from stacking low end into the recorder.
        preampLowState += (x - preampLowState) * lowCutCoefficient;
        const float cut = x - preampLowState;

        // The valve stage: a soft asymmetric clip whose operating point drifts a
        // little with the signal, the same mechanism as the saturation core's valve
        // but at a much lower gain - this is an input stage, not a fuzz.
        preampBiasState += (cut - preampBiasState) * 0.0012f;
        const float biased = cut + preampBiasState * 0.12f;

        // Gain rises with the control, and the curve is normalised so the stage
        // stays level-matched: PREAMP changes the character, and the INPUT control
        // remains the thing that sets the level.
        const float gain = 1.0f + amount * 3.2f;
        const float driven = biased * gain;
        const float clipped = std::tanh (driven) / std::tanh (gain);

        // The slight top-end lift a good input transformer has. A one-pole
        // difference against the cut signal, added back gently.
        const float lift = (cut - preampLowState) * amount * 0.18f;

        return clipped + lift;
    }

    /**
        Runs the distortion. `amount` is 0..1; at 0 it is a straight wire.

        A diode clipper is a hard knee, and the abruptness is the point: it is what
        makes this read as a pedal rather than as more saturation.
    */
    float processDistortion (float x, float amount) noexcept
    {
        if (amount <= 1.0e-5f)
            return x;

        // Pre-gain, then the hard knee. The knee is fixed and the gain moves, which
        // is how a real pedal's gain control works - the clipping threshold is set
        // by the diodes, not by the knob.
        const float gain = 1.0f + amount * 9.0f;
        const float driven = x * gain;

        // A hard clip with a very small soft shoulder. Perfectly square would alias
        // badly; a shoulder of a few percent keeps it a diode clipper while staying
        // band-limited enough for the oversampler to work with.
        constexpr float knee = 0.92f;
        const float magnitude = std::abs (driven);
        float clipped;
        if (magnitude <= knee)
        {
            clipped = driven;
        }
        else
        {
            // The shoulder: a short exponential walk to the asymptote.
            const float excess = magnitude - knee;
            const float compressed = knee + (1.0f - j37math::exp (-excess * 3.0f)) * (1.0f - knee);
            clipped = driven < 0.0f ? -compressed : compressed;
        }

        // Undo the pre-gain so the stage stays level-matched, then AC-couple: a hard
        // clipper leaves an offset, and the tape stage downstream must not be handed
        // one.
        const float scaled = clipped / gain;
        distortionLowState += (scaled - distortionLowState) * 0.0006f;
        return scaled - distortionLowState;
    }

    /**
        Runs the DI box. All three of its controls arrive together because they
        are one decision - a DI is a box you plug into, and the box is the sum of
        its transformer, its load and its pad.

        `amount`     0..1 - how much the box is engaged at all. At 0 it is a
                     straight wire, bit-for-bit, so a session with no DI is
                     exactly the signal it always was.

        `load`       0..1 - how heavily the input loads the source. 0 is a very
                     high-impedance (transparent) input and 1 is a heavy load
                     that damps the source's own top-end resonance.

        `padDb`      the pad in dB (0 or negative). Applied BEFORE the
                     transformer, which is the whole point of a pad: it stops a
                     hot source saturating the box rather than merely turning it
                     down.

        `humAmount`  0..1 - the ground loop. A DI with a lifted ground is
                     silent; a DI with a bad earth hums, and the hum is at the
                     mains frequency. It is a fault the box either has or does
                     not, which is why it is part of the DI rather than a
                     separate control.

        The remaining three arguments are coefficient/phase values the caller
        builds per block from the engine rate, so this stage stays rate-agnostic
        like every other one here.
    */
    float processDiBox (float x, float amount, float load, float padDb, float humAmount,
                        float loadCoefficient, float transformerCoefficient,
                        float humIncrement) noexcept
    {
        if (amount <= 1.0e-5f)
            return x;

        // The PAD, first, before the transformer sees anything. A pad after the
        // transformer would be a level control; a pad before it is what lets a
        // hot source into a DI without driving the core, which is the decision
        // the switch exists for.
        float padded = x * juce::Decibels::decibelsToGain (padDb);

        // The input LOAD. A one-pole low-pass whose corner moves with the
        // control: a heavy load damps the source's top octave and its resonant
        // peak, which is what makes a passive guitar pickup sound fatter and
        // softer through one DI than another. It is taken as a SPLIT so the
        // removed top can be folded back at low level rather than simply lost -
        // a real load damps the peak, it does not silence the octave.
        const float corner = loadCoefficient * (1.0f - load * 0.85f);
        diLoadState += (padded - diLoadState) * corner;
        const float loadedLow = diLoadState;
        const float loadedHigh = padded - diLoadState;
        const float loaded = loadedLow + loadedHigh * (1.0f - load * 0.55f);

        // The DI TRANSFORMER. A real one is a small, often cheap transformer,
        // and its signature is a gentle low-end bloom and a slight thickness in
        // the low middle. A one-pole low split with the bottom band lifted is
        // the same mechanism the tape condition stage and the vinyl degree use,
        // which is why it is one line here rather than a model of its own.
        diTransformerState += (loaded - diTransformerState) * transformerCoefficient;
        const float transformerLow = diTransformerState;
        const float transformerHigh = loaded - diTransformerState;
        const float transformerOut = transformerLow * 1.12f + transformerHigh * 0.97f;

        // The GROUND LOOP. A 50 Hz hum, added rather than folded in, because it
        // is an interference the box picks up rather than something it does to
        // the signal.
        float groundHum = 0.0f;
        if (humAmount > 0.0f)
        {
            diGroundHumPhase += humIncrement;
            if (diGroundHumPhase >= 1.0f)
                diGroundHumPhase -= 1.0f;

            // The fundamental and its second harmonic - a mains loop is rarely
            // a pure sine, and the harmonic is what makes it read as a hum
            // rather than as a low tone.
            groundHum = (std::sin (diGroundHumPhase * 6.2831853f)
                           + 0.30f * std::sin (diGroundHumPhase * 12.5663706f))
                      * humAmount * humAmount * 0.010f;
        }

        // The whole box is mixed in by `amount`, so the control is a real
        // engagement rather than a set of simultaneous switches.
        return x + (transformerOut + groundHum - x) * amount;
    }
};

//==============================================================================
/**
    Tape condition: FLUX, WEAR and MECHANICS.

    Three separate physical facts about the machine, and they are genuinely
    separate rather than three amounts of the same thing:

      FLUX      - how deep into the oxide the record head magnetises. More flux is
                  more low end, more hysteresis memory and a quieter floor; less is
                  thin and bright. It is the RECORD head's depth.
      WEAR      - the state of the heads and the tape. A rounded gap and patchy
                  oxide lose top end and add contact noise. It is the MEDIUM's
                  condition.
      MECHANICS - how well the transport holds its speed. Good order means smooth,
                  periodic wow and flutter; worn bearings and a slack belt mean
                  irregular drift and the occasional slip.

    All three are slow, continuous controls with a neutral position, so they read
    as calibration rather than as effects.
*/
struct TapeCondition
{
    // WEAR's contact noise: a slow random modulation of the high-frequency loss.
    float wearNoiseState = 0.0f;
    float wearNoisePhase = 0.0f;

    // MECHANICS' irregular drift: a slow random walk added to the wow phase.
    float driftState = 0.0f;
    float driftTarget = 0.0f;
    int driftCountdown = 0;

    // The FLUX low shelf, per channel.
    float fluxLowL = 0.0f;
    float fluxLowR = 0.0f;

    // The WEAR high-frequency loss, per channel.
    float wearLowL = 0.0f;
    float wearLowR = 0.0f;

    void reset() noexcept
    {
        wearNoiseState = wearNoisePhase = 0.0f;
        driftState = driftTarget = 0.0f;
        driftCountdown = 0;
        fluxLowL = fluxLowR = 0.0f;
        wearLowL = wearLowR = 0.0f;
    }

    /**
        Advances the slow random sources. Called once per block, not per sample:
        the drift and the contact noise are both sub-audio, so a block-rate update
        is not merely adequate, it is the correct rate - a per-sample update would
        just be a slower way to compute the same number.

        `random` is the caller's noise source, so this stage does not carry a
        second generator.
    */
    void updateSlowSources (float mechanicsAmount, float wearAmount, int numSamples,
                            std::uint32_t& randomState) noexcept
    {
        // -- MECHANICS: the drift target changes every few hundred milliseconds ---
        driftCountdown -= numSamples;
        if (driftCountdown <= 0)
        {
            // 120 ms between targets. Slow enough to read as a wandering transport
            // rather than as noise, fast enough that a held note hears it move.
            driftCountdown = juce::jmax (1, juce::roundToInt (0.12f * 48000.0f));

            randomState = randomState * 1664525u + 1013904223u;
            const float unit = static_cast<float> ((randomState >> 8) & 0x00ffffffu)
                             * (1.0f / 8388608.0f) - 1.0f;

            // A worn machine's drift is larger; in good order it is nearly zero.
            driftTarget = unit * mechanicsAmount;
        }

        // The drift itself is a one-pole toward the target, so the walk is smooth.
        driftState += (driftTarget - driftState) * 0.05f;

        // -- WEAR: the contact noise is a slow random modulation -----------------
        randomState = randomState * 1664525u + 1013904223u;
        const float wearUnit = static_cast<float> ((randomState >> 8) & 0x00ffffffu)
                             * (1.0f / 8388608.0f) - 1.0f;
        wearNoiseState += (wearUnit * wearAmount - wearNoiseState) * 0.02f;
    }

    /**
        The FLUX low shelf and the WEAR top-end loss, per sample.

        Both are one-pole splits: FLUX lifts the low band, WEAR takes the high band
        away. They are separate filters rather than one tilt because they are
        separate mechanisms - one is the record head, the other is the medium.
    */
    float processTone (float x, int channel,
                       float fluxAmount, float wearAmount,
                       float fluxCoefficient, float wearCoefficient) noexcept
    {
        auto& fluxLow = channel == 0 ? fluxLowL : fluxLowR;
        auto& wearLow = channel == 0 ? wearLowL : wearLowR;

        // FLUX: the low band is the one-pole's output, the rest is everything above.
        // Lifting the low band by up to +3 dB is what "more flux" sounds like.
        fluxLow += (x - fluxLow) * fluxCoefficient;
        const float fluxHigh = x - fluxLow;
        const float fluxGain = 1.0f + (fluxAmount - 0.5f) * 0.6f;
        const float fluxOut = fluxLow * fluxGain + fluxHigh;

        // WEAR: a one-pole low-pass on the whole signal, crossfaded in by the wear
        // amount. A worn head loses top end, so the more worn it is the more of the
        // filtered copy is used.
        wearLow += (fluxOut - wearLow) * wearCoefficient;
        return fluxOut + (wearLow - fluxOut) * wearAmount;
    }
};

//==============================================================================
/**
    A plate/room reverb, built from four Schroeder all-pass sections into two
    comb banks.

    The topology is the classic one because it is the one that sounds like a room
    for the least code: all-passes diffuse the signal without colouring its
    spectrum, and the comb banks that follow set the decay. Two banks, one per
    channel, with different comb lengths, is what gives the stereo image its
    width - a single bank would collapse to mono.

    It sits AFTER the machine in the chain, so it reverberates the processed
    signal rather than feeding back into the saturation. That keeps it clean and
    predictable: a reverb inside the nonlinearity would be modulated by the wow
    and would smear the harmonics the plugin exists to produce.

    The buffers are fixed and allocated once; the controls only change gains and
    feedback, so the audio thread never allocates.
*/
struct PlateReverb
{
    // Four all-passes per channel, then four combs per channel. The lengths are the
    // classic Schroeder numbers scaled for a small room; they are prime-ish so the
    // combs do not reinforce each other into a ringing pitch.
    static constexpr int numAllPasses = 4;
    static constexpr int numCombs = 4;
    static constexpr int maxDelayLength = 4096;

    struct AllPass
    {
        juce::AudioBuffer<float> buffer;
        int writePosition = 0;
        int length = 0;

        void prepare (int len)
        {
            length = juce::jlimit (1, maxDelayLength, len);
            buffer.setSize (1, maxDelayLength, false, true, true);
            buffer.clear();
            writePosition = 0;
        }

        float process (float x, float feedback)
        {
            if (length <= 0)
                return x;

            auto* data = buffer.getWritePointer (0);
            const int readPosition = (writePosition - length + maxDelayLength) % maxDelayLength;
            const float delayed = data[readPosition];

            // The all-pass recurrence: a diffuser, not a delay. It passes every
            // frequency at the same level and only spreads the phase, which is what
            // turns a sparse early reflection into a smooth tail.
            const float out = -x + delayed;
            data[writePosition] = x + delayed * feedback;

            writePosition = (writePosition + 1) % maxDelayLength;
            return out;
        }
    };

    struct Comb
    {
        juce::AudioBuffer<float> buffer;
        int writePosition = 0;
        int length = 0;
        float lowState = 0.0f;

        void prepare (int len)
        {
            length = juce::jlimit (1, maxDelayLength, len);
            buffer.setSize (1, maxDelayLength, false, true, true);
            buffer.clear();
            writePosition = 0;
            lowState = 0.0f;
        }

        float process (float x, float feedback, float damping)
        {
            if (length <= 0)
                return x;

            auto* data = buffer.getWritePointer (0);
            const int readPosition = (writePosition - length + maxDelayLength) % maxDelayLength;
            float delayed = data[readPosition];

            // Damping: the tail loses top end as it decays, which is what a real
            // room does and what stops a comb bank from sounding metallic. A
            // one-pole on the feedback path is the standard way to do it.
            lowState += (delayed - lowState) * damping;
            delayed = lowState;

            data[writePosition] = x + delayed * feedback;
            writePosition = (writePosition + 1) % maxDelayLength;
            return delayed;
        }
    };

    std::array<std::array<AllPass, numAllPasses>, 2> allPasses;
    std::array<std::array<Comb, numCombs>, 2> combs;
    bool prepared = false;

    // The comb and all-pass lengths, per channel. The two channels differ by a few
    // samples, which is the whole of the stereo width.
    static constexpr int allPassLengthsL[numAllPasses] { 225, 341, 441, 556 };
    static constexpr int allPassLengthsR[numAllPasses] { 231, 348, 449, 563 };
    static constexpr int combLengthsL[numCombs] { 1557, 1617, 1491, 1422 };
    static constexpr int combLengthsR[numCombs] { 1580, 1642, 1511, 1447 };

    void prepare (double sampleRate)
    {
        // The lengths are given for 44.1 kHz, so they are scaled for any other rate
        // to keep the room the same physical size.
        const auto scale = static_cast<double> (sampleRate) / 44100.0;
        const auto scaled = [scale] (int base)
        {
            return juce::jlimit (1, maxDelayLength - 1, juce::roundToInt (base * scale));
        };

        for (int channel = 0; channel < 2; ++channel)
        {
            for (int i = 0; i < numAllPasses; ++i)
                allPasses[static_cast<std::size_t> (channel)][static_cast<std::size_t> (i)]
                    .prepare (scaled (channel == 0 ? allPassLengthsL[i] : allPassLengthsR[i]));

            for (int i = 0; i < numCombs; ++i)
                combs[static_cast<std::size_t> (channel)][static_cast<std::size_t> (i)]
                    .prepare (scaled (channel == 0 ? combLengthsL[i] : combLengthsR[i]));
        }

        prepared = true;
    }

    void reset() noexcept
    {
        for (int channel = 0; channel < 2; ++channel)
        {
            for (auto& allPass : allPasses[static_cast<std::size_t> (channel)])
            {
                allPass.buffer.clear();
                allPass.writePosition = 0;
            }

            for (auto& comb : combs[static_cast<std::size_t> (channel)])
            {
                comb.buffer.clear();
                comb.writePosition = 0;
                comb.lowState = 0.0f;
            }
        }
    }

    /**
        One sample through the reverb.

        `size` is 0..1 and sets the tail length; `mix` is 0..1 and is how much of
        the reverberated signal is returned. At mix 0 the stage is bypassed
        entirely, so a machine with no reverb is unchanged.
    */
    float process (float x, int channel, float size, float mix) noexcept
    {
        if (! prepared || mix <= 1.0e-5f)
            return x;

        const auto index = static_cast<std::size_t> (juce::jlimit (0, 1, channel));

        // Decay rises with size. The ceiling is deliberately below 1.0: a comb bank
        // at unity feedback never decays, and the difference between a long reverb
        // and an infinite one is not a useful control.
        const float feedback = 0.70f + size * 0.28f;

        // Damping rises with size as well: a bigger room absorbs more top end per
        // pass, so a long tail is a darker one. That is what keeps a large setting
        // from sounding like a metal tank.
        const float damping = 0.25f + size * 0.35f;

        float diffused = x;
        for (auto& allPass : allPasses[index])
            diffused = allPass.process (diffused, 0.5f);

        float out = 0.0f;
        for (auto& comb : combs[index])
            out += comb.process (diffused, feedback, damping);

        // The comb bank sums four outputs, so it is scaled back to keep the reverb
        // at a comparable level to the dry signal rather than four times it.
        out *= 0.25f;

        return x + out * mix;
    }
};

//==============================================================================
/**
    The vinyl stage: surface noise, rumble and the RIAA playback curve.

    These three are what "vinyl" means as a sound, and none of them is a tape
    effect:

      CRACKLE - impulse noise. A record surface is not hiss: it is ticks, caused by
                dust and by the stylus crossing the groove's imperfections. So the
                generator produces SPARSE IMPULSES rather than continuous noise,
                which is the difference between a record and a noisy tape.
      RUMBLE  - a low-frequency thump from the turntable's bearing and motor,
                which is why vinyl has a bottom-end floor that a CD does not.
      WARMTH  - the RIAA playback curve. The curve itself is a strong low-frequency
                roll-off with a high-frequency boost (it is the inverse of the
                cutting curve), and a playback stage that is not perfectly
                complementary leaves the characteristic low-end lift and top-end
                softness. That is the "warm" part, and it is a filter, not a colour.

    Placed at the very end of the chain, after the protection stages, because a
    turntable is the last thing in the signal path.
*/
struct VinylStage
{
    // The voice the VINYL TYPE switch sets. RIAA is the cutting standard's own
    // tilt, so it belongs to the DISC, not to the playback chain's error - a 78
    // was not cut to the RIAA curve at all, and a half-speed master has more of
    // it because the lathe tracked properly.
    float riaaAmount = 0.45f;
    float rumbleAmount = 1.0f;       // multiplier on the caller's rumble level
    float crackleAmount = 1.0f;      // multiplier on the caller's crackle level
    float crackleDecay = 0.9985f;    // per-sample: how long a tick rings
    float surfaceNoise = 0.0f;       // groove hiss between the ticks
    float speedModulation = 0.0f;    // vinyl SPEED: wow/flutter depth on the stage

    // ------------------------------------------------------------------
    //  The four physical faults of a record and a turntable.
    //
    //  CRACKLE is the needle finding a scratch; these four are the rest of
    //  what goes wrong when a record is played. Each is a genuinely different
    //  mechanism rather than another amount of noise:
    //
    //    DUST       - fine particulate in the groove. A continuous, granular
    //                 high-frequency texture that follows the groove's own
    //                 modulation (a louder passage sounds dirtier) and lifts
    //                 off whenever the stylus has signal to grind through.
    //    SCRATCH    - a deep groove wound, not a dust tick. It comes back once
    //                 per revolution, so it is PERIODIC: a short burst of
    //                 noise at a fixed rate derived from the platter speed.
    //    WARP       - the record is not flat. The vertical warp makes the
    //                 stylus ride up and down once per turn, so the LEVEL (and
    //                 slightly the pitch) breathes at the platter rate. It is
    //                 the slow, cyclic throb a warped record has.
    //    ELECTRICAL - the cartridge, the cable and the earth loop. Mains hum
    //                 at the supply frequency plus its harmonics, and the
    //                 broadband static that comes with a bad earth.
    //
    //  All four are set per block from the controls and read only in process().
    // ------------------------------------------------------------------
    float dustAmount = 0.0f;         // 0..1 fine particulate in the groove
    float scratchAmount = 0.0f;      // 0..1 deep periodic groove damage
    float warpAmount = 0.0f;         // 0..1 vertical warp, level breathing
    float electricalAmount = 0.0f;   // 0..1 mains hum + earth static

    // ------------------------------------------------------------------
    //  CLICKS - the sharp, discrete groove faults: the cut, not the surface.
    //
    //  CRACKLE is the fine surface texture and DUST is the grit in the groove;
    //  CLICKS is the third and loudest class of record damage: an actual ridge
    //  or pit in the lacquer, or a stamping fault, that the stylus hits as a
    //  single hard transient. Where a crackle tick is a few milliseconds of
    //  noise, a click is a fast BIPOLAR impulse - a sharp full-bandwidth spike
    //  with almost no ringing - because it is a mechanical impact rather than
    //  a contact noise. That is why it reads as "a click" and not as "more
    //  crackle", and why it is a control of its own.
    //
    //  A second, lower-state effect rides with it and is what makes a badly
    //  pressed record sound BROKEN rather than merely dirty: the same fault
    //  repeats because the stylus crosses it once per revolution, so a fraction
    //  of the clicks line up with the platter instead of falling at random.
    // ------------------------------------------------------------------
    float clickAmount = 0.0f;        // 0..1 sharp groove faults
    float clickEnvelopeL = 0.0f;
    float clickEnvelopeR = 0.0f;
    float clickPhaseL = 0.0f;
    float clickPhaseR = 0.0f;
    float clickIncrement = 0.0f;     // one periodic click per revolution

    // ------------------------------------------------------------------
    //  GENERATION, TURNTABLE and CARTRIDGE - the three stages of getting a
    //  record to play, and each one changes the sound in its own way.
    //
    //  They are the same idea as TAPE TYPE and VINYL TYPE, one level up: the
    //  record TYPE says what was pressed, these say what was CUT, what it is
    //  played ON, and what is reading it.
    //
    //    GENERATION  how the lacquer was cut. A modern direct-metal master has
    //                almost none of the cutting-head's own colour and plays
    //                loud and clean; a vintage lacquer master is darker, its
    //                high end rolled off by the cutter head's own limits; a
    //                PRINTED pressing is a copy of a copy, so it carries the
    //                accumulated loss of every generation before it - duller
    //                still, noisier, and with a little extra surface character.
    //                ("PRINTED" is the pressing made from a stamper, as opposed
    //                to the reference lacquer the engineer hears.)
    //
    //    TURNTABLE   what drives the platter. A high-torque direct-drive DJ deck
    //                holds a rock-steady 33 and stops in a quarter turn; a
    //                belt-drive audiophile deck has a slow, elastic belt, so it
    //                is marginally less steady but smoother, and takes a moment
    //                to settle. The DJ deck is what chasing and scratching needs;
    //                the belt deck is what a quiet pressing wants.
    //
    //    CARTRIDGE   what reads the groove, and the largest single difference of
    //                the three. A moving-MAGNET cartridge has a warm, slightly
    //                soft top and a broad, gentle response; a moving-COIL has
    //                more detail and a brighter, tighter top with a lower output
    //                (so more gain, and more hiss); a DJ CONCORDE-style cart is
    //                heavier and hotter, tracks harder, and carries a bit more
    //                surface noise with it.
    //
    //  The three are INDEPENDENT: each sets coefficients of its own, and the
    //  engine applies whichever combination is selected. A funky modern cut on a
    //  belt-drive with a moving coil is a different machine from a vintage
    //  printed pressing on a DJ deck, and both are reachable.
    // ------------------------------------------------------------------
    float generationTiltL = 0.0f;   // per-channel cutting-generation tilt state
    float generationTiltR = 0.0f;
    float generationTopLoss = 1.0f; // >0, multiplier on the top band
    float generationBottomLift = 1.0f; // >0, multiplier on the bottom band
    float generationNoiseScale = 1.0f; // multiplier on this stage's own noise

    float turntableWowScale = 1.0f;    // multiplier on the speed wander
    float turntableStability = 1.0f;   // flutter irregularity

    float cartridgeTopGain = 1.0f;     // >0, top-band gain of the reading
    float cartridgeBottomGain = 1.0f;  // >0, bottom-band gain
    float cartridgeNoiseGain = 1.0f;   // multiplier on crackle/dust/click
    float cartridgeHissGain = 1.0f;    // multiplier on groove hiss
    float cartridgeCoefficient = 0.0f; // the split the two gains act around

    // Mains hum's frequency and the per-revolution rates are given as
    // INCREMENTS (fraction of a cycle per sample) so the stage is rate-agnostic
    // for the same reason every other increment here is: the caller converts a
    // frequency in Hz using the engine rate, and the phase maths below is then
    // one multiply and one wrap.
    float humIncrement = 0.0f;       // mains hum, usually 50 or 60 Hz
    float humIncrement2 = 0.0f;      // its second harmonic (100 / 120 Hz)
    float scratchIncrement = 0.0f;   // one scratch burst per platter revolution
    float warpIncrement = 0.0f;      // one warp cycle per platter revolution

    // Rumble: a low-frequency one-pole, per channel.
    float rumbleL = 0.0f;
    float rumbleR = 0.0f;

    // The RIAA-ish playback tilt, per channel.
    float warmthLowL = 0.0f;
    float warmthLowR = 0.0f;

    // Crackle: a short decaying envelope per impulse, per channel.
    float crackleEnvelopeL = 0.0f;
    float crackleEnvelopeR = 0.0f;

    // DUST: the granular high-frequency texture's own one-pole, per channel -
    // its bandwidth is set by the caller so it stays the same at every rate.
    float dustLowL = 0.0f;
    float dustLowR = 0.0f;
    float dustCoefficient = 0.0f;

    // SCRATCH and WARP phases, per channel. The scratch phase drives the burst
    // gate; the warp phase drives the level breath. They run independently of
    // the speed-wander phase above, because a warp and a speed error are two
    // different faults that happen to share a rotation rate.
    float scratchPhaseL = 0.0f;
    float scratchPhaseR = 0.0f;
    float warpPhaseL = 0.0f;
    float warpPhaseR = 0.0f;

    // ELECTRICAL: the hum's own two phases (fundamental and second harmonic),
    // per channel, so a cartridge wired with the two sides out of balance hums
    // differently on each - which is exactly how a real earth loop behaves.
    float humPhaseL = 0.0f;
    float humPhaseR = 0.0f;
    float humHarmonicPhaseL = 0.0f;
    float humHarmonicPhaseR = 0.0f;

    // The disc's own wow phase, per channel. The increment is set per block from
    // the speed the VINYL SPEED control selects, exactly as the transport's phases
    // are - a 33 RPM disc wanders at a different rate than a 45.
    float vinylWowPhaseL = 0.0f;
    float vinylWowPhaseR = 0.0f;
    float vinylWowIncrement = 0.02f;

    void reset() noexcept
    {
        rumbleL = rumbleR = 0.0f;
        warmthLowL = warmthLowR = 0.0f;
        crackleEnvelopeL = crackleEnvelopeR = 0.0f;
        vinylWowPhaseL = vinylWowPhaseR = 0.0f;
        dustLowL = dustLowR = 0.0f;
        scratchPhaseL = scratchPhaseR = 0.0f;
        warpPhaseL = warpPhaseR = 0.0f;
        humPhaseL = humPhaseR = 0.0f;
        humHarmonicPhaseL = humHarmonicPhaseR = 0.0f;
        clickEnvelopeL = clickEnvelopeR = 0.0f;
        clickPhaseL = clickPhaseR = 0.0f;
        generationTiltL = generationTiltR = 0.0f;
    }

    /**
        One sample of surface noise and colour.

        `crackleAmount` and `rumbleAmount` are 0..1 and scale the two noise
        sources independently; `warmth` is 0..1 and sets how much of the RIAA
        playback character is applied. `random` is the caller's noise source.
    */
    float process (float x, int channel,
                   float crackleControl, float rumbleControl, float warmth,
                   float rumbleCoefficient, float warmthCoefficient,
                   std::uint32_t& random) noexcept
    {
        // -- CRACKLE: sparse impulses, not continuous noise ---------------------
        // A tick happens when the stylus hits something, so it is an impulse with a
        // fast decay rather than a steady hiss. The probability per sample is low
        // and the amplitude is high, which is what makes it read as a record.
        auto& crackleEnvelope = channel == 0 ? crackleEnvelopeL : crackleEnvelopeR;

        random = random * 1664525u + 1013904223u;
        const float unit = static_cast<float> ((random >> 8) & 0x00ffffffu)
                         * (1.0f / 8388608.0f) - 1.0f;

        // Roughly one impulse every few thousand samples at full crackle, so the
        // ticks are distinct events rather than a buzz.
        const float probability = 0.00008f * crackleAmount;
        if (std::abs (unit) < probability)
        {
            // The impulse's own amplitude, also random, so the ticks are not all
            // the same size.
            random = random * 1664525u + 1013904223u;
            const float amplitude = static_cast<float> ((random >> 8) & 0x00ffffffu)
                                  * (1.0f / 8388608.0f) - 1.0f;
            crackleEnvelope += amplitude * 0.35f;
        }

        // A tick is over in a millisecond or two, unless the type says otherwise -
        // a worn shellac rings longer than a fresh half-speed master.
        crackleEnvelope *= crackleDecay;

        // -- RUMBLE: the turntable's low-frequency floor ------------------------
        auto& rumble = channel == 0 ? rumbleL : rumbleR;
        random = random * 1664525u + 1013904223u;
        const float rumbleNoise = static_cast<float> ((random >> 8) & 0x00ffffffu)
                                * (1.0f / 8388608.0f) - 1.0f;
        // A one-pole low-pass on white noise gives the thump; the coefficient is
        // built by the caller so it is the same at every rate. The type's own
        // rumble multiplier sits on top of the control.
        rumble += (rumbleNoise * rumbleControl * rumbleAmount * rumbleAmount * 0.020f - rumble) * rumbleCoefficient;

        // Groove surface hiss - continuous, unlike the ticks. A shellac 78 has
        // real surface between every transient; a half-speed master almost none.
        float groove = 0.0f;
        if (surfaceNoise > 0.0f)
        {
            random = random * 1664525u + 1013904223u;
            const float hiss = static_cast<float> ((random >> 8) & 0x00ffffffu)
                             * (1.0f / 8388608.0f) - 1.0f;
            groove = hiss * surfaceNoise * 0.0080f;
        }

        // ------------------------------------------------------------------
        //  DUST: fine particulate in the groove.
        //
        //  Unlike the ticks above, dust is a CONTINUOUS granular texture - the
        //  sound of the stylus grinding through fine grit. Two things make it
        //  read as dust rather than as another hiss:
        //
        //    - it is band-limited HIGH (a one-pole above the programme's own
        //      top end), so it sits on top of the music as grit;
        //    - it is scaled by the programme's own level, so a loud passage
        //      sounds dirtier than a quiet one. Real dust only makes a sound
        //      when there is modulation in the groove to disturb it.
        // ------------------------------------------------------------------
        float dust = 0.0f;
        if (dustAmount > 0.0f)
        {
            auto& dustLow = channel == 0 ? dustLowL : dustLowR;
            random = random * 1664525u + 1013904223u;
            const float grit = static_cast<float> ((random >> 8) & 0x00ffffffu)
                             * (1.0f / 8388608.0f) - 1.0f;

            // The high-pass is the complement of the one-pole: what the filter
            // removes is what is left.
            dustLow += (grit - dustLow) * dustCoefficient;
            const float highGrit = grit - dustLow;

            // The programme gating: `x` is the signal entering the stage, so a
            // quiet groove stays quiet even under a lot of dust.
            const float grooveEnergy = juce::jlimit (0.0f, 1.0f, std::abs (x) * 3.0f);
            dust = highGrit * dustAmount * dustAmount * 0.020f
                 * (0.25f + 0.75f * grooveEnergy);
        }

        // ------------------------------------------------------------------
        //  SCRATCH: a deep groove wound, once per revolution.
        //
        //  Where a dust tick is random, a SCRATCH is PERIODIC: the stylus crosses
        //  the same wound every turn, so the damage arrives at the rotation rate
        //  and is heard as a repeating thud rather than as a hiss. The phase
        //  wraps once per platter revolution, and the burst fires over a short
        //  window at the wrap, so the fault is a distinct event in time.
        // ------------------------------------------------------------------
        float scratch = 0.0f;
        if (scratchAmount > 0.0f && scratchIncrement > 0.0f)
        {
            float& phase = channel == 0 ? scratchPhaseL : scratchPhaseR;
            phase += scratchIncrement;
            if (phase >= 1.0f)
                phase -= 1.0f;

            // The burst window: the first few percent of the revolution. A short
            // raised-cosine so the event fades in and out instead of clicking -
            // a step at either end of the window would be a glitch of its own.
            constexpr float burstWindow = 0.04f;
            if (phase < burstWindow)
            {
                const float burstProgress = phase / burstWindow;
                const float window = 0.5f - 0.5f * std::cos (burstProgress * 6.2831853f);

                random = random * 1664525u + 1013904223u;
                const float wound = static_cast<float> ((random >> 8) & 0x00ffffffu)
                                  * (1.0f / 8388608.0f) - 1.0f;

                scratch = wound * scratchAmount * scratchAmount * 0.10f * window;
            }
        }

        // ------------------------------------------------------------------
        //  CLICKS: the sharp, discrete groove faults.
        //
        //  The third and loudest class of record damage. A crackle tick is a
        //  short burst of contact noise; a CLICK is a mechanical IMPACT - the
        //  stylus dropping off a ridge or hitting a pit - so it is a fast,
        //  bipolar, full-bandwidth spike with almost no ring. Two things make it
        //  read as a click rather than as more crackle:
        //
        //    - the impulse is shaped as an alternating pair of samples (a i, -i),
        //      which is the shortest transient a sampled signal can carry and
        //      therefore the brightest thing the stage can produce;
        //    - a fraction of the clicks is PERIODIC, locked to the platter, so a
        //      bad pressing ticks in time rather than at random - the same
        //      wound crossed once per revolution.
        // ------------------------------------------------------------------
        float click = 0.0f;
        if (clickAmount > 0.0f)
        {
            auto& clickEnvelope = channel == 0 ? clickEnvelopeL : clickEnvelopeR;

            random = random * 1664525u + 1013904223u;
            const float unit = static_cast<float> ((random >> 8) & 0x00ffffffu)
                             * (1.0f / 8388608.0f) - 1.0f;

            // The random half of the clicks: roughly one every few thousand
            // samples at full CLICKS, which is a stream of distinct events.
            const float randomProbability = 0.00012f * clickAmount;

            // The periodic half: one click per revolution, fired through a very
            // short window so it is a discrete impact and not a rumble. Only the
            // part of the CLICKS control ABOVE half the travel brings the
            // periodic behaviour in, so the low half of the knob is a clean
            // random-click stream and the top half is a record that ticks in time.
            bool periodicClick = false;
            if (clickIncrement > 0.0f && clickAmount > 0.5f)
            {
                float& phase = channel == 0 ? clickPhaseL : clickPhaseR;
                phase += clickIncrement;
                if (phase >= 1.0f)
                    phase -= 1.0f;

                constexpr float clickWindow = 0.004f;
                if (phase < clickWindow)
                    periodicClick = true;
            }

            if (std::abs (unit) < randomProbability || periodicClick)
            {
                random = random * 1664525u + 1013904223u;
                const float amplitude = static_cast<float> ((random >> 8) & 0x00ffffffu)
                                      * (1.0f / 8388608.0f) - 1.0f;

                // A click is an IMPACT, so it is much louder than a tick - and it
                // is bipolar, alternating sign between events, which is what a
                // mechanical impact produces when the stylus is knocked both ways.
                const float sign = unit < 0.0f ? -1.0f : 1.0f;
                clickEnvelope += amplitude * sign * 0.55f;
            }

            // The decay is FAST - a couple of samples - because a click has no
            // ring. This is the other half of what separates it from the crackle
            // envelope above, which is deliberately slow enough to ring.
            clickEnvelope *= 0.62f;

            click = clickEnvelope * clickAmount * clickAmount;
        }

        // ------------------------------------------------------------------
        //  WARP: the record is not flat.
        //
        //  A warped record makes the stylus ride up and down once per turn, so
        //  the tracking force - and therefore the output level - breathes at the
        //  platter rate. It is a slow, cyclic throb, and it is what makes a
        //  warped record sound like it is struggling rather than merely noisy.
        //
        //  It modulates what the stage PASSES rather than what it adds, so at
        //  WARP 0 it is exactly unity and the stage is untouched.
        // ------------------------------------------------------------------
        float warp = 1.0f;
        if (warpAmount > 0.0f && warpIncrement > 0.0f)
        {
            float& phase = channel == 0 ? warpPhaseL : warpPhaseR;
            phase += warpIncrement;
            if (phase >= 1.0f)
                phase -= 1.0f;

            // Depth is deliberately small: a warp is a wobble in level, not a
            // tremolo. At 100 % it is about +-1.5 dB, which is enough to hear
            // the record struggling without becoming an effect.
            warp = 1.0f + std::sin (phase * 6.2831853f) * warpAmount * 0.175f;
        }

        // ------------------------------------------------------------------
        //  ELECTRICAL: the cartridge, the cable and the earth loop.
        //
        //  Two separate faults, and both are what "electrical" means on a
        //  turntable:
        //
        //    - MAINS HUM at the supply frequency plus its second harmonic,
        //      which is what an unearthed cartridge picks up from the motor and
        //      the transformer;
        //    - EARTH STATIC, the broadband crackle of a bad ground, continuous
        //      and independent of the groove.
        //
        //  The hum's two components have independent phases per channel, so a
        //  cartridge whose two sides pick the field up differently hums
        //  asymmetrically - which is exactly how a real earth loop behaves.
        // ------------------------------------------------------------------
        float electrical = 0.0f;
        if (electricalAmount > 0.0f)
        {
            // Two phases per channel: the fundamental and the second harmonic.
            // Both are advanced by their own increment, set by the caller from the
            // ONE supply frequency, so the 2:1 relationship between them is
            // established in the voicing block and never re-derived here.
            float& humPhase = channel == 0 ? humPhaseL : humPhaseR;
            float& harmonicPhase = channel == 0 ? humHarmonicPhaseL : humHarmonicPhaseR;
            humPhase += humIncrement;
            harmonicPhase += humIncrement2;
            if (humPhase >= 1.0f) humPhase -= 1.0f;
            if (harmonicPhase >= 1.0f) harmonicPhase -= 1.0f;

            const float fundamental = std::sin (humPhase * 6.2831853f);
            const float second = std::sin (harmonicPhase * 6.2831853f);

            // The right channel's earthing is slightly different from the left's:
            // the second harmonic is in anti-phase across the pair while the
            // fundamental is not - the classic asymmetric hum of a cartridge whose
            // two sides pick the field up differently.
            const float harmonicSign = channel == 0 ? 1.0f : -1.0f;
            const float hum = (fundamental + 0.35f * harmonicSign * second)
                            * electricalAmount * electricalAmount * 0.006f;

            random = random * 1664525u + 1013904223u;
            const float staticNoise = static_cast<float> ((random >> 8) & 0x00ffffffu)
                                    * (1.0f / 8388608.0f) - 1.0f;
            const float earthStatic = staticNoise * electricalAmount * electricalAmount * 0.004f;

            electrical = hum + earthStatic;
        }

        // -- WARMTH: the RIAA playback character --------------------------------
        // A one-pole split: the low band is lifted and the high band is left, which
        // is the low-end lift and relative top-end softness of a playback stage
        // that is not perfectly complementary to the cutting curve.
        auto& warmthLow = channel == 0 ? warmthLowL : warmthLowR;
        warmthLow += (x - warmthLow) * warmthCoefficient;
        const float warmthHigh = x - warmthLow;
        const float warmed = warmthLow * (1.0f + warmth * riaaAmount)
                           + warmthHigh * (1.0f - warmth * riaaAmount * 0.4f);

        // The disc's own speed error, as a slow amplitude wander. A record that is
        // running off speed does not merely hiss - the whole groove's output rises
        // and falls with the stylus's tracking error, which is what this term is.
        // It is folded onto the stage's output rather than onto the dry path so
        // that at VINYL 0 none of it exists.
        float speedWander = 0.0f;
        if (speedModulation > 0.0f)
        {
            float& phase = channel == 0 ? vinylWowPhaseL : vinylWowPhaseR;
            phase += vinylWowIncrement;
            if (phase >= 1.0f)
                phase -= 1.0f;
            speedWander = std::sin (phase * 6.2831853f) * speedModulation;
        }

        // ------------------------------------------------------------------
        //  GENERATION and CARTRIDGE: the cut, and the thing reading it.
        //
        //  Both are a two-band tilt around one split, but for opposite reasons,
        //  and that is why they are two controls rather than one:
        //
        //    GENERATION is a LOSS. A cut master has the cutter head's own colour
        //    and a printed pressing has every generation's loss on top of it, so
        //    the top band is what suffers and the bottom is what remains. The
        //    tilt is applied to the stage's OWN output, before the cartridge gains,
        //    because it is part of what was pressed onto the disc rather than part
        //    of what reads it.
        //
        //    CARTRIDGE is a RESHAPE. A moving coil is bright and tight, a moving
        //    magnet is soft and broad, a DJ cart is hot and heavier. That is a gain
        //    PAIR on the two bands, applied after, because it is what the reading
        //    does to a record that is already the way it is.
        //
        //  Both already default to a flat pair (gains of 1.0), so a stage whose
        //  switches sit on their neutral entries is bit-for-bit untouched.
        // ------------------------------------------------------------------
        const float generationOut = [&]
        {
            if (std::abs (generationTopLoss - 1.0f) < 1.0e-5f
                && std::abs (generationBottomLift - 1.0f) < 1.0e-5f)
                return warmed;

            auto& tilt = channel == 0 ? generationTiltL : generationTiltR;
            tilt += (warmed - tilt) * cartridgeCoefficient;
            const float bottom = tilt;
            const float top = warmed - tilt;
            return bottom * generationBottomLift + top * generationTopLoss;
        }();

        const float cartridgeOut = [&]
        {
            if (std::abs (cartridgeTopGain - 1.0f) < 1.0e-5f
                && std::abs (cartridgeBottomGain - 1.0f) < 1.0e-5f)
                return generationOut;

            // The same split the generation tilt uses, taken from the same state -
            // one filter, two users, so the two controls cannot disagree about where
            // the band boundary is.
            auto& tilt = channel == 0 ? generationTiltL : generationTiltR;
            const float bottom = tilt;
            const float top = generationOut - tilt;
            return bottom * cartridgeBottomGain + top * cartridgeTopGain;
        }();

        const float grooveNoiseGain = cartridgeNoiseGain;
        const float hissGainNow = cartridgeHissGain;

        return (cartridgeOut
                     + groove * hissGainNow
                     + crackleEnvelope * crackleControl * crackleAmount * crackleAmount * 0.6f
                         * grooveNoiseGain
                     + rumble
                     + dust * grooveNoiseGain
                     + scratch * grooveNoiseGain
                     + click * grooveNoiseGain
                     + electrical) * (1.0f + speedWander) * warp;
    }
};

//==============================================================================
/**
    The plugin's equaliser: three tone bands plus a high-pass and a low-pass.

    THE TONE STACK. Three bands, each a genuinely different filter rather than
    three of the same:

      LOW   a low shelf. It lifts or cuts everything below the corner, which is
            where weight lives. A shelf rather than a peak, because "more bass"
            on a master means the whole bottom octave, not one frequency.

      MID   a bell around 1 kHz inside the 200 Hz - 4 kHz band, leaving both ends
            alone - the band that decides whether something sounds present or
            hollow.

      HIGH  a high shelf above 4 kHz, the mirror of LOW. Air, or the lack of it.

    THE FILTERS. A high-pass and a low-pass, each with a real ORDER (how steep it
    is) and a real Q (how much it rings at the corner). They are deliberately not
    the same control as the shelves above:

      - a shelf shapes a band and leaves everything else at unity, so it can
        never remove anything completely;
      - a FILTER removes everything outside its passband, which is what you need
        to get rid of rumble, a hum, a hiss, or the sub-audio content a tape
        machine's own transport can produce.

    ORDER is expressed in dB PER OCTAVE - 6, 12, 18, 24, 36, 48 - because that is
    the unit a filter is actually specified in. Six is one pole, twelve is two,
    and the engine implements each pole as a one-pole section exactly like every
    other filter in this plugin, so the number the user picks is the slope they
    get, to within the usual one-pole approximation.

    Q is the resonance at the corner. At 0.5 the filter is critically damped
    (the gentlest, no overshoot); at higher values there is a lift at the corner.
    It is capped well short of self-oscillation, because a resonant filter that
    rings on a tape emulation is a fault rather than a feature - the Q control
    exists to let a steep filter be usable, not to make it sing.

    At 0 dB on all three bands, HP at its floor, LP at its ceiling and Q at its
    default, the whole stage is bit-for-bit transparent.
*/
struct ThreeBandEq
{
    // Split frequencies. The low split is where a mix's weight sits (200 Hz),
    // the high split is where air begins (4 kHz), and the mid bell sits between
    // them at 1 kHz - the octave where presence and honk both live.
    float lowCoefficient = 0.0f;
    float highCoefficient = 0.0f;

    // Per-band gains, 0.25x .. 4x (i.e. about -12 dB .. +12 dB). Set per block.
    float lowGain = 1.0f;
    float midGain = 1.0f;
    float highGain = 1.0f;

    // The mid bell the fraction of the band that passes through the
    // -3 dB point. A narrower bell is useful for taming a resonance, a wider one
    // for shaping a whole region, so Q is a control rather than a constant.
    float midWidth = 0.6f;

    // ------------------------------------------------------------------
    //  The high-pass and the low-pass.
    //
    //  `hpCoefficient` and `lpCoefficient` are the ONE-POLE coefficients every
    //  pole of the filter reuses - the order control only changes how many times
    //  each pole is applied, which is what makes a one-pole section stack into
    //  a 24 dB/octave filter without any of the biquad coefficient maths.
    //
    //  `hpPoles` and `lpPoles` are the pole COUNT (1, 2, 3, 4, 6, 8 for 6, 12,
    //  18, 24, 36, 48 dB/octave). `hpQ` and `lpQ` trim the corner lift.
    // ------------------------------------------------------------------
    float hpCoefficient = 0.0f;
    float lpCoefficient = 0.0f;
    int   hpPoles = 1;
    int   lpPoles = 1;
    float hpQ = 0.7f;
    float lpQ = 0.7f;

    // Per-channel filter state. The tone stack needs three memories per channel
    // (the low/high split, the mid/high split and the bell), and the two filters
    // need one each per pole - eight poles deep, which is the worst case and
    // costs sixty-four floats for the pair. That is nothing, and a static array
    // is what keeps this stage allocation-free on the audio thread.
    float lowStateL = 0.0f, lowStateR = 0.0f;
    float midLowL = 0.0f, midLowR = 0.0f;
    float bellStateL = 0.0f, bellStateR = 0.0f;

    static constexpr int maxPoles = 8;
    std::array<float, maxPoles> hpStateL {}, hpStateR {};
    std::array<float, maxPoles> lpStateL {}, lpStateR {};

    // The filters are bypassed when the control is at its floor/ceiling, so a
    // neutral EQ costs one comparison rather than eight multiplies.
    bool hpActive = false;
    bool lpActive = false;

    /** Rebuilds every split coefficient for a rate. Called from prepareToPlay
        and from the per-block path, because the rate the engine runs at can
        change with oversampling without prepareToPlay running again. */
    void prepare (float sampleRate) noexcept
    {
        // 200 Hz and 4 kHz, converted with the same one-pole helper everything
        // else in the plugin uses, so they mean the same thing at every rate.
        const auto rate = juce::jmax (1.0f, sampleRate);
        const auto omegaLow = juce::MathConstants<float>::twoPi * 200.0f / rate;
        const auto omegaHigh = juce::MathConstants<float>::twoPi * 4000.0f / rate;
        lowCoefficient = juce::jlimit (0.0f, 1.0f, 1.0f - std::exp (-omegaLow));
        highCoefficient = juce::jlimit (0.0f, 1.0f, 1.0f - std::exp (-omegaHigh));
    }

    /** The bell's own coefficient, derived from the split and the width. The mid
        band is the difference between two low-passes - one at the bell's centre
        and one wider - which is a band-pass built the same way every other split
        in this plugin is built. */
    float midBandCoefficient (float sampleRate) const noexcept
    {
        // Width 0 gives a narrow bell (about 2 kHz of passband), width 1 a broad
        // one (about 8 kHz). It is deliberately never zero-width: a resonant
        // peak on a tape machine is a fault, not a control.
        const float widthHz = 2000.0f + midWidth * 6000.0f;
        const auto omega = juce::MathConstants<float>::twoPi * widthHz
                         / juce::jmax (1.0f, sampleRate);
        return juce::jlimit (0.0f, 1.0f, 1.0f - std::exp (-omega));
    }

    /** Rebuilds the two filter coefficients and the pole counts.

        `hpHz` is the high-pass corner, `lpHz` the low-pass corner, `order` the
        slope in dB/octave and `q` the corner resonance. This is called once per
        BLOCK, not per sample: all four are controls, so they change on block
        boundaries at the fastest, and the eight std::exp below would be eight
        per sample otherwise.
    */
    void prepareFilters (float sampleRate, float hpHz, float lpHz,
                         float orderDbPerOctave, float q) noexcept
    {
        const auto rate = juce::jmax (1.0f, sampleRate);

        // The pole count from the slope: one pole is 6 dB per octave, so the
        // count is the slope divided by six. Clamped to the array's own size,
        // which is what makes the array's size the highest order on offer.
        const auto poles = juce::jlimit (1, maxPoles,
                                         juce::roundToInt (orderDbPerOctave / 6.0f));
        hpPoles = poles;
        lpPoles = poles;

        // "Off" positions. A corner below audibility is not a filter, and a
        // corner at or above Nyquist cannot exist - so both ends of each control's
        // travel are REAL bypasses rather than a filter that happens to be very
        // gentle. That is what makes a neutral EQ bit-for-bit transparent.
        hpActive = hpHz > 22.0f;
        lpActive = lpHz < rate * 0.45f;

        // Q: the corner lift. One-pole sections in series multiply their own
        // gentle knee, so a stack of them gives a rounded corner rather than a
        // resonant one - which is why the Q control acts on the CORNER only, by
        // trimming the pole's own response rather than by adding a resonant
        // biquad on top. 0.5 is critically damped, 1.5 is the practical maximum
        // before a steep stack starts to ring audibly at the corner.
        hpQ = juce::jlimit (0.5f, 1.5f, q);
        lpQ = hpQ;

        const auto hpOmega = juce::MathConstants<float>::twoPi
                           * juce::jlimit (22.0f, rate * 0.45f, hpHz) / rate;
        const auto lpOmega = juce::MathConstants<float>::twoPi
                           * juce::jlimit (22.0f, rate * 0.45f, lpHz) / rate;

        // The Q trims the coefficient: a higher Q makes the pole respond further
        // per step, which is the corner lift, and 1.0 is the neutral multiplier
        // so a Q of 0.7 is deliberately slightly under-damped rather than a
        // no-op - that is what a real one-pole corner looks like.
        hpCoefficient = juce::jlimit (0.0f, 1.0f,
                                      (1.0f - std::exp (-hpOmega)) * hpQ);
        lpCoefficient = juce::jlimit (0.0f, 1.0f,
                                      (1.0f - std::exp (-lpOmega)) * lpQ);
    }

    void reset() noexcept
    {
        lowStateL = lowStateR = 0.0f;
        midLowL = midLowR = 0.0f;
        bellStateL = bellStateR = 0.0f;
        hpStateL.fill (0.0f); hpStateR.fill (0.0f);
        lpStateL.fill (0.0f); lpStateR.fill (0.0f);
    }

    /** One sample through the three bands.

        `lowState` and `midState` are the caller's per-channel filter memories -
        the caller owns them so that both instances of this stage (input and
        output) keep their state in the places the rest of the engine keeps its
        own, rather than this struct allocating.
    */
    float process (float x, int channel, float midCoefficient) noexcept
    {
        // ------------------------------------------------------------------
        //  The HIGH-PASS, first, so everything downstream sees a signal that is
        //  already free of whatever it removes.
        //
        //  Each pole is a one-pole high-pass built from a one-pole low-pass, the
        //  same split every other filter in this plugin uses: what the low-pass
        //  keeps is subtracted from the input, and what is left is the high side.
        //  Stacking the poles in series is what makes 6 dB/octave into 24 - the
        //  slope is the pole COUNT and nothing else, which is why the order
        //  control needs no coefficient maths of its own.
        //
        //  `hpActive` is false at the control's floor, in which case the whole
        //  loop is skipped and the sample passes untouched. That is what keeps a
        //  neutral EQ bit-for-bit transparent rather than "transparent to within
        //  a very gentle filter".
        // ------------------------------------------------------------------
        float filtered = x;
        if (hpActive)
        {
            auto& hpState = channel == 0 ? hpStateL : hpStateR;
            for (int pole = 0; pole < hpPoles; ++pole)
            {
                auto& state = hpState[static_cast<std::size_t> (pole)];
                state += (filtered - state) * hpCoefficient;
                filtered -= state;   // the low side removed, the high side left
            }
        }

        // The low/high split: what the low-pass keeps is the LOW band, what it
        // removes is everything above it.
        auto& lowState = channel == 0 ? lowStateL : lowStateR;
        lowState += (filtered - lowState) * lowCoefficient;
        const float lowBand = lowState;
        const float upperBand = filtered - lowState;

        // The upper band is split again at 4 kHz into the MID and HIGH bands.
        auto& midState = channel == 0 ? midLowL : midLowR;
        midState += (upperBand - midState) * highCoefficient;
        const float midHighBand = upperBand - midState;   // everything above 4 kHz
        const float midBand = midState;                   // 200 Hz .. 4 kHz
        // The mid BELL. MID is a resonant-style bell rather than a second
        // low/high pair, and that is a deliberate difference: a full-band mid
        // shelf would drag the low and high ends with it, so turning MID up
        // would sound like turning everything up. A bell acts on its own pass
        // band ONLY, leaving the two shelves where they were.
        //
        // It is built from two one-poles over the same mid band - one at the
        // bell's own (narrower) corner and one at the band's own top - and what
        // the narrow one passes is subtracted from what the broad one passes.
        // That difference is a band-pass, and it is the same technique every
        // other filter in this plugin uses: one-pole splits, never biquads.
        // The small addition at the end keeps the bell's peak from vanishing as
        // the band narrows, so 0 dB really is 0 dB at every width. */
        auto& bellState = channel == 0 ? bellStateL : bellStateR;
        bellState += (midBand - bellState) * midCoefficient;
        const float bellBand = bellState;
        const float bellRest = midBand - bellBand;

        // The mid output: the bell's pass band takes the gain, and everything
        // else in the mid region is left at unity, so MID is a bell and not a
        // level control on the whole 200 Hz - 4 kHz span.
        const float midOut = midBand + bellBand * (midGain - 1.0f)
                                    + bellRest * (midGain - 1.0f) * 0.15f;

        const float tonal = lowBand * lowGain
                          + midOut
                          + midHighBand * highGain;

        // ------------------------------------------------------------------
        //  The LOW-PASS, last: the mirror of the high-pass above and built the
        //  same way, one pole at a time, with the pole COUNT being what the
        //  order control changes. It is the control that takes a hiss off, or
        //  that rounds the top of the machine off before it leaves the plugin.
        //
        //  A low-pass cannot be "off" at the top of its range the way a
        //  high-pass is at the bottom: a corner above Nyquist is not a filter at
        //  all, so the control's own maximum IS the bypass and `lpActive` tests
        //  for exactly that. The two filters are therefore symmetric in the
        //  panel and symmetric here - each has one end that is genuinely no
        //  filter rather than a very gentle one.
        // ------------------------------------------------------------------
        float out = tonal;
        if (lpActive)
        {
            auto& lpState = channel == 0 ? lpStateL : lpStateR;
            for (int pole = 0; pole < lpPoles; ++pole)
            {
                auto& state = lpState[static_cast<std::size_t> (pole)];
                state += (out - state) * lpCoefficient;
                out = state;
            }
        }

        return out;
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
    // The two flag parameters are named `should...` rather than after the fields they
    // initialise: a constructor parameter with the same name as its field shadows it,
    // and Clang flags that on macOS (it is how a silent self-assignment hides here).
    explicit SampleSmoother (SampleClock& clockToUse, bool shouldStartAtSample = false,
                             bool shouldInvertOutput = false, float initialValue = 0.0f)
        : clock (clockToUse), startsSample (shouldStartAtSample),
          invertOutput (shouldInvertOutput), smoother (initialValue)
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
    static constexpr int numStages = 9;
    static constexpr int dividers[numStages] { 2, 3, 4, 5, 6, 7, 8, 9, 10 };

    // 2^-(divider - 1): 1/2 at 0 dB, 1/3 at -6, 1/4 at -12 ... 1/10 at -48.
    // The staircase law is unchanged - the ninth stage simply continues it, so
    // adding 1/10 does not alter the balance of the eight that were already there.
    static constexpr float baseWeights[numStages] { 0.5f, 0.25f, 0.125f, 0.0625f,
                                                     0.03125f, 0.015625f, 0.0078125f,
                                                     0.00390625f, 0.001953125f };

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

    // ------------------------------------------------------------------------
    //  Fundamental phase accumulator, in turns [0, 1).
    //
    //  This is the master clock the whole cascade is derived from, and it is the
    //  upgrade that matters most. The previous version aligned each stage against
    //  `crossingCount % divider` - a running COUNT of detected crossings rather
    //  than a phase. That has two faults:
    //
    //    - It drifts permanently. `crossingCount` only ever increases, so a
    //      single missed crossing (a quiet passage, a transient the detector
    //      skips) shifts every stage's reference for the rest of the session.
    //      The error never heals because nothing brings the count back in step
    //      with the signal.
    //
    //    - It is only correct when the divider happens to divide the count
    //      evenly. For the stages whose divider does not, the target phase is
    //      quantised to the wrong value and the stage is pulled off-frequency
    //      every time a crossing arrives.
    //
    //  A phase accumulator fixes both. It advances continuously from the tracked
    //  period, so the reference is always current, and a missed crossing costs
    //  one cycle of correction rather than a permanent offset - the PLL pulls
    //  the phase back toward the crossing each time one arrives.
    // ------------------------------------------------------------------------
    float fundamentalPhase = 0.0f;

    // Running phase per subharmonic stage in [0, 1)
    float phases[numStages] {};

    // ------------------------------------------------------------------------
    //  Cycle-confidence tracker.
    //
    //  The detector's trigger threshold is a fraction of the tracked peak, so a
    //  decaying note eventually stops producing crossings. When that happens the
    //  cascade must not simply coast: it would keep generating at the last
    //  tracked pitch, which is the drone the presence gate exists to prevent.
    //  Counting the samples since the last crossing and folding that into the
    //  presence gate makes the stage release as soon as the signal stops
    //  driving it, rather than after a fixed envelope time.
    // ------------------------------------------------------------------------
    float samplesSinceLastCrossing = 0.0f;
    float cycleConfidence = 0.0f;

    // The confidence coefficients, built with the rate.
    float confidenceAttackCoeff = 0.0f;
    float confidenceReleaseCoeff = 0.0f;

    // ------------------------------------------------------------------------
    //  Octave-lock memory.
    //
    //  A subharmonic an octave below the note is the one that carries the most
    //  energy, so it is also the one that can do the most damage: if the two
    //  channels happen to generate it in opposite phase, summing the mix to mono
    //  cancels it, and the low end disappears. That is not a fault in this stage,
    //  but it IS a fault in the result, and it is preventable.
    //
    //  `octavePhaseAligned` records whether the 1/2 stage has stayed close to the
    //  master's half-phase over the last few cycles. When the two channels agree,
    //  the octave is reinforced; when they disagree, the two octaves are opposite
    //  and the mono sum loses them. The caller can then bias the depth per channel
    //  so the pair stays constructive.
    // ------------------------------------------------------------------------
    float octavePhaseError = 0.0f;

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
        fundamentalPhase = 0.0f;
        samplesSinceLastCrossing = 0.0f;
        cycleConfidence = 0.0f;
        octavePhaseError = 0.0f;
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

        // Cycle confidence: rises over about 40 ms of steady crossings and falls
        // over about 25 ms without one. Both are slower than the presence gate,
        // because confidence is a statement about the TRACK, not about the level -
        // it should not flicker on a single missed crossing.
        confidenceAttackCoeff = 1.0f - std::exp (-1.0f / (safeRate * 0.040f));
        confidenceReleaseCoeff = 1.0f - std::exp (-1.0f / (safeRate * 0.025f));

        // 5 Hz one-pole AC coupling, in the same form the playback DC blocker uses.
        // The detector accepts fundamentals from 15 Hz up, so this removes any
        // offset without touching anything the stage is trying to track.
        detectorDcR = juce::jlimit (0.5f, 0.9999f,
                                    1.0f - (juce::MathConstants<float>::twoPi * 5.0f) / safeRate);
    }

    /**
        The 1/2 stage's phase relative to the master, in turns, signed to [-0.5, 0.5).
        The caller reads this on both channels to decide whether their octaves are
        in phase; see the field's own note for why that matters.
    */
    float getOctavePhaseError() const noexcept { return octavePhaseError; }

    /** How well the detector is currently tracking, 0..1. Drives the UI readout. */
    float getCycleConfidence() const noexcept { return cycleConfidence; }

    /**
        Rotates the master phase by a small amount, for the caller's anti-phase
        protection. Deliberately tiny in effect and clamped: this is a nudge, not a
        set, so it can never break the lock the PLL is holding - it only biases
        which side of the lock the cascade settles on.
    */
    void nudgeMasterPhase (float turns) noexcept
    {
        fundamentalPhase += juce::jlimit (-0.01f, 0.01f, turns);
        if (fundamentalPhase >= 1.0f) fundamentalPhase -= 1.0f;
        if (fundamentalPhase < 0.0f)  fundamentalPhase += 1.0f;
    }

    /** The fundamental the cascade is locked to, in Hz. Zero when unlocked. */
    float getTrackedFrequency (float sampleRate) const noexcept
    {
        if (cycleConfidence < 0.1f)
            return 0.0f;

        const float safeRate = juce::jmax (1.0f, sampleRate);
        const float safePeriod = juce::jlimit (safeRate / 500.0f, safeRate / 15.0f, period);
        return safeRate / safePeriod;
    }

    /**
        Feeds one sample and returns the multi-frequency subharmonic component: a
        phase-locked series of pure sinusoids at f0/2, /3, /4 ... /10.

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
        // The gate is now the product of TWO independent statements about whether
        // there is a note to double:
        //
        //   presenceGain     - is there still LEVEL in the fundamental band
        //   cycleConfidence  - is the detector still getting CYCLES from it
        //
        // The second is the new one, and it matters because they fail differently.
        // A sustained note whose level drops keeps producing crossings long after
        // it is quiet, and a noisy signal can hold the level up while producing no
        // usable cycles at all. Requiring both means the cascade releases when
        // EITHER stops being true, which is the honest answer to "is there a note
        // to derive undertones from".
        const float confidenceCoefficient = samplesSinceLastCrossing < safeRate * 0.25f
                                                ? confidenceAttackCoeff
                                                : confidenceReleaseCoeff;
        cycleConfidence += ((samplesSinceLastCrossing < safeRate * 0.25f ? 1.0f : 0.0f)
                                - cycleConfidence) * confidenceCoefficient;

        const float presenceGain = juce::jlimit (0.0f, 1.0f, presence / silenceFloor);
        const float gateGain = presenceGain * cycleConfidence;

        if (detPeak < silenceFloor || gateGain <= 0.0f)
            return 0.0f;

        // ----------------------------------------------------------------------
        //  4. Schmitt-trigger crossing detector with hysteresis.
        //  Measures fundamental period and triggers Phase-Locked Loop (PLL).
        // ----------------------------------------------------------------------
        samplesSinceCrossing += 1.0f;
        samplesSinceLastCrossing += 1.0f;
        const float triggerThreshold = detPeak * 0.10f;

        if (det > triggerThreshold && prevDet <= triggerThreshold && armed)
        {
            armed = false;
            // Valid fundamental range: 15 Hz up to 500 Hz
            const float minPeriod = safeRate / 500.0f;
            const float maxPeriod = safeRate / 15.0f;

            // The period estimate is averaged rather than jumped to. A quarter
            // weight per crossing is about four cycles of settling, which is fast
            // enough to follow a line and slow enough to reject a single bad
            // interval - and the interval is only accepted at all if it falls in
            // the musical range, so a transient cannot pull the track off.
            if (samplesSinceCrossing >= minPeriod && samplesSinceCrossing <= maxPeriod)
                period += (samplesSinceCrossing - period) * 0.25f;

            samplesSinceCrossing = 0.0f;
            samplesSinceLastCrossing = 0.0f;

            // ------------------------------------------------------------------
            //  Phase-Locked Loop, on the FUNDAMENTAL.
            //
            //  The crossing is a known point of the waveform: at an upward
            //  zero-crossing the fundamental's phase is exactly 0. So the
            //  correction is simply "pull the accumulator toward 0", and every
            //  stage's target follows from it by division - which is what a
            //  subdivision IS.
            //
            //  This replaces the previous `crossingCount % divider` scheme. That
            //  one used a running count rather than a phase, so it drifted
            //  permanently after a single missed crossing and was quantised to the
            //  wrong value for any divider that did not divide the count evenly.
            //  Deriving every stage from one phase accumulator fixes both: the
            //  reference is always current, and a missed crossing costs one
            //  correction rather than a permanent offset.
            // ------------------------------------------------------------------
            float phaseError = -fundamentalPhase;
            if (phaseError > 0.5f)  phaseError -= 1.0f;
            if (phaseError < -0.5f) phaseError += 1.0f;

            // A gentle pull: 8 % of the error per crossing. Tight enough to hold
            // lock over a long take, loose enough that it never fights the
            // continuous advancement and produces a jump.
            fundamentalPhase += phaseError * 0.08f;
            if (fundamentalPhase >= 1.0f) fundamentalPhase -= 1.0f;
            if (fundamentalPhase < 0.0f)  fundamentalPhase += 1.0f;
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

        // ----------------------------------------------------------------------
        //  Advance the fundamental phase, then re-derive every stage from it.
        //
        //  This is the second half of the PLL upgrade. The stages are no longer
        //  free-running oscillators that get nudged; they are DIVISIONS of one
        //  master phase. That is what keeps them locked to each other as well as
        //  to the signal - with independent accumulators, two stages can drift
        //  apart from each other even while both stay near the note, which shows
        //  up as the series beating against itself.
        // ----------------------------------------------------------------------
        fundamentalPhase += baseStep;
        if (fundamentalPhase >= 1.0f)
            fundamentalPhase -= 1.0f;

        const float clampedDrive = juce::jlimit (0.0f, 1.0f, driveAmount);

        float sum = 0.0f;
        float weightSum = 0.0f;

        for (int s = 0; s < numStages; ++s)
        {
            // The stage runs its OWN accumulator, advanced at baseStep/divider and
            // wrapped at 1.0 independently of the master.
            //
            // It cannot simply be sampled from the master, which is the obvious way
            // to express "locked" and was tried: fundamentalPhase / dividers[s] only
            // ever spans [0, 1/d) before the master wraps back to 0, so the cosine
            // traced a half-wave and returned instead of running continuously. Every
            // stage became a rectified pulse rather than a sinusoid, and the whole
            // staircase lost about 190 dB - the 1/2 stage measured -220 dB where it
            // should be -29. A divided oscillator needs a period of its own: it gets
            // 1/d of a cycle per master period, and those d pieces have to be joined
            // into one continuous cycle, which is exactly what the accumulator does.
            //
            // The master is still authoritative for LOCK - it is what the PLL
            // corrects against and what nudgeMasterPhase moves - it just cannot
            // supply the running phase.
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

            // The octave stage's phase error is recorded for the caller's
            // anti-phase bias. Stage 0 is the 1/2 divider by construction, and the
            // error is signed so the two channels can be compared.
            if (s == 0)
                octavePhaseError = phases[0] < 0.5f ? phases[0] : phases[0] - 1.0f;
        }

        if (weightSum <= 1.0e-4f)
            return 0.0f;

        const float fade = juce::jmin (1.0f, weightSum);
        return (sum / weightSum) * detPeak * depth * fade * gateGain;
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

    // AudioProcessor declares BOTH a float and a double processBlock in the headless
    // module. Overriding only the float one hid the double overload, which GCC reports
    // as -Woverloaded-virtual: the base virtual becomes unreachable by name. The using
    // declaration brings it back into scope; the double one is never called for a
    // float-only plugin, but hiding it is still a real interface change.
    using juce::AudioProcessor::processBlock;
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

    /** Number of factory presets, including the Minimum and Maximum range endpoints. */
    static constexpr int numFactoryPresets = 26;

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

    //==============================================================================
    //  Subharmonic tracking telemetry.
    //
    //  Reports what the undertone cascade is actually doing, which is the one
    //  thing about the stage a user cannot see from the panel: a depth knob at
    //  60 % tells you nothing if the detector has not locked onto a note, and
    //  until now there was no way to tell those two states apart.
    //==============================================================================

    /** The fundamental the cascade is locked to, in Hz. Zero when unlocked. */
    float getSubfundTrackedHz() const noexcept { return subfundTrackedHz.load (std::memory_order_relaxed); }

    /** The tempo the deck is running at, for the panel's BPM readout. Read from
        the message thread; the audio thread writes it as a plain double, which is
        the same discipline every other published value here follows, and a torn
        double read would only smear one displayed digit for one frame. */
    double getDeckTempoBpm() const noexcept { return hostTempoBpm; }
    bool   getDeckTempoValid() const noexcept { return hostTempoValid; }

    /** How solidly the detector is tracking, 0..1. Zero means nothing is being generated. */
    float getSubfundConfidence() const noexcept { return subfundConfidence.load (std::memory_order_relaxed); }

    //==============================================================================
    //  Transport control, from the editor.
    //
    //  The editor owns the momentary SPINDOWN button and must be able to drive the
    //  three-state transport without re-implementing the state machine. These are
    //  the two entry points it uses, and both are realtime-safe.
    //==============================================================================

    /** Sets the transport state. `state` is the transport AudioParameterChoice
        index (0 = Stop, 1 = Play, 2 = Start). Start is transient: it is armed
        here and the engine advances it to Play when the capstan arrives.

        Writes through to the parameter so the host (and the preset/session) sees
        the change, then forwards to the engine immediately so the audio thread
        does not have to wait for the next automation pass. */
    void setTransportState (int state);

    /** The momentary SPINDOWN hold. While held the machine runs down like a
        turntable whose power has been cut; on release it spins back up and the
        transport settles into Play. Both calls are safe to make from the message
        thread and are messages to the audio thread, never a lock on it. */
    void setSpindownHeld (bool shouldHold);

    /** True while spindown is held. For the panel's button lamp. */
    bool isSpindownHeld() const noexcept { return spindownHeld.load (std::memory_order_relaxed); }

    /** The platter speed the engine is actually running at, 0..1, as the product
        of the transport ramp and the spindown ramp. Published so the editor can
        spin its reels at the real speed rather than at a guess. */
    float getTransportRamp() const noexcept { return transportRampPublished.load (std::memory_order_relaxed); }

    /** The spindown run-down alone, 0..1, 1 = at speed. Lets the panel show the
        pitch falling away during a hold even while the transport itself is Play. */
    float getSpindownRamp() const noexcept { return spindownRampPublished.load (std::memory_order_relaxed); }

    /** How hard the output anti-phase guard is currently working, 0..1. Zero means
        the stereo pair is healthy; a rising value means the two sides were found
        to be in opposition and are being pulled back into agreement, which is the
        one fault that would otherwise be inaudible in stereo and then cancel in
        mono. Published so the panel can show the guard acting. */
    float getAntiPhaseAmount() const noexcept { return antiPhaseAmount.load (std::memory_order_relaxed); }

    /** True when the panel's interface clicks are switched on. The editor reads
        this every frame and enables its own sound engine from it, so the switch
        on the SETTINGS tab, a preset and an automation lane all reach the sounds
        by the one route. */
    bool getUiSoundsEnabled() const noexcept
    {
        return uiSoundsParam != nullptr && uiSoundsParam->load() >= 0.5f;
    }

    //==============================================================================
    //  Audio -> UI telemetry.
    //
    //  One complete frame of metering, filled by the audio thread once per block
    //  and read by the editor on its timer. With the readerwriterqueue library on
    //  the include path the frame travels through a lock-free SPSC queue, so every
    //  field the editor sees belongs to the SAME block of audio; without it the
    //  frame is assembled from the individual atomics, which is what earlier builds
    //  did. Either way the editor has exactly one call to make.
    //
    //  The struct is public because the accessor returns it and the editor has to
    //  name the type; the queue and the mirror atomics are private, below.
    //==============================================================================
    struct TelemetryFrame
    {
        float inputPeakDb = -70.0f;
        float inputRmsDb = -70.0f;
        float inputLufs = -70.0f;
        float inputVuDb = -70.0f;
        float inputCombinedDb = -70.0f;
        bool  inputClipping = false;

        float outputPeakDb = -70.0f;
        float outputRmsDb = -70.0f;
        float outputLufs = -70.0f;
        float outputVuDb = -70.0f;
        float outputCombinedDb = -70.0f;
        bool  outputClipping = false;

        float inputGainReductionDb = 0.0f;
        float outputGainReductionDb = 0.0f;
        float inputCompressorActivity = 0.0f;
        float outputCompressorActivity = 0.0f;
        float compressorActivity = 0.0f;

        float transportDrift = 0.5f;
        float harmonicCharacter = 0.0f;
        float evenHarmonicRatio = 0.0f;
        float oddHarmonicRatio = 0.0f;
        float subfundTrackedHz = 0.0f;
        float subfundConfidence = 0.0f;
        float antiPhaseAmount = 0.0f;
        float transportRamp = 1.0f;
        float spindownRamp = 1.0f;
        bool  bypassActive = false;
    };

    /** The newest telemetry frame. Realtime-safe to call from the message thread:
        it drains the queue when the library is present and never blocks. */
    TelemetryFrame getTelemetry() const;

    /** True when the telemetry is carried by the lock-free queue rather than by
        the plain atomics - so the editor can say which path it is reading. */
    static constexpr bool hasLockFreeTelemetry() noexcept
    {
#if J37_HAS_RWQ
        return true;
#else
        return false;
#endif
    }

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

    /** AudioProcessorValueTreeState::Listener: any parameter change dirties the
        badge, and a change to `spindown` syncs the engine's momentary flag. */
    void parameterChanged (const juce::String& parameterID, float newValue) override
    {
        // Reads and writes only atomics: APVTS forwards host automation here from the
        // audio thread, and the badge is advisory state, never control state.
        if (presetNameNonEmpty.load (std::memory_order_relaxed))
            presetDirty.store (true, std::memory_order_relaxed);

        // `spindown` is the one parameter whose value the engine keeps a second
        // copy of (the atomic it reads every block). Keeping it in step here means
        // a preset load, an undo/redo or an automation pass that moves the
        // parameter also moves the platter - without a second code path that could
        // drift from this one. The comparison first means the common case of the
        // button writing the parameter is a no-op rather than a redundant store.
        if (parameterID == "spindown")
            spindownHeld.store (newValue >= 0.5f, std::memory_order_relaxed);
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
    // The machine's track layout: 2 / 2+3 / 3. Read once per block like every
    // other choice, so the per-sample crosstalk branch is on a plain int.
    std::atomic<float>* tracksParam = nullptr;
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
    // PING-PONG's own control, read once per block like the rest of the delay.
    std::atomic<float>* delayPingPongParam = nullptr;
    std::atomic<float>* stOffsetParam = nullptr;
    std::atomic<float>* noiseParam = nullptr;
    std::atomic<float>* transportParam = nullptr;
    std::atomic<float>* spindownParam = nullptr;
    // The panel's interface-sound switch. Read on the message thread by the
    // editor, but a parameter like every other setting so it survives in a
    // session and can be automated.
    std::atomic<float>* uiSoundsParam = nullptr;
    std::atomic<float>* blendParam = nullptr;
    std::atomic<float>* shapeParam = nullptr;
    std::atomic<float>* sagParam = nullptr;
    std::atomic<float>* presenceParam = nullptr;
    std::atomic<float>* cabinetParam = nullptr;
    std::atomic<float>* ampBiasParam = nullptr;
    std::atomic<float>* preampParam = nullptr;

    // The DI box: engagement, input load, transformer colour and the pad choice.
    std::atomic<float>* diParam = nullptr;
    std::atomic<float>* diLoadParam = nullptr;
    std::atomic<float>* diTransformerParam = nullptr;
    std::atomic<float>* diPadParam = nullptr;
    std::atomic<float>* fluxParam = nullptr;
    std::atomic<float>* wearParam = nullptr;
    std::atomic<float>* mechanicsParam = nullptr;
    std::atomic<float>* reverbParam = nullptr;
    std::atomic<float>* reverbSizeParam = nullptr;
    std::atomic<float>* delayTypeParam = nullptr;
    std::atomic<float>* distortionParam = nullptr;
    // These are the RAW parameter values, which JUCE stores as floats for every
    // parameter kind - including AudioParameterBool. So the type is
    // atomic<float> and the value is read as `>= 0.5f`, exactly like the bypass
    // and polarity switches above. Declaring them as atomic<bool> was a
    // reasonable guess that does not match the API.
    std::atomic<float>* modernModeParam = nullptr;
    std::atomic<float>* lofiModeParam = nullptr;
    std::atomic<float>* noiseLvlParam = nullptr;
    std::atomic<float>* vinylParam = nullptr;
    std::atomic<float>* vinylCrackleParam = nullptr;
    std::atomic<float>* vinylRumbleParam = nullptr;
    std::atomic<float>* vinylSpeedParam = nullptr;

    // The four physical faults of a record and a turntable. Read once per block
    // like every other control, so the per-sample vinyl loop branches on plain
    // floats rather than on atomics.
    std::atomic<float>* vinylDustParam = nullptr;
    std::atomic<float>* vinylScratchParam = nullptr;
    std::atomic<float>* vinylWarpParam = nullptr;
    std::atomic<float>* vinylElectricalParam = nullptr;

    // CLICKS, and the three selectors that describe how the record was made
    // (GENERATION), what plays it (TURNTABLE) and what reads it (CARTRIDGE).
    std::atomic<float>* vinylClicksParam = nullptr;
    std::atomic<float>* vinylGenerationParam = nullptr;
    std::atomic<float>* vinylTurntableParam = nullptr;
    std::atomic<float>* vinylCartridgeParam = nullptr;

    // The two equalisers' six bands. Read once per block like every other
    // control, so the per-sample EQ loop branches on plain floats.
    std::atomic<float>* inputEqLowParam = nullptr;
    std::atomic<float>* inputEqMidParam = nullptr;
    std::atomic<float>* inputEqHighParam = nullptr;
    std::atomic<float>* outputEqLowParam = nullptr;
    std::atomic<float>* outputEqMidParam = nullptr;
    std::atomic<float>* outputEqHighParam = nullptr;

    // The EQ filters: corner, order (as a choice index into the dB/octave list)
    // and Q, per equaliser.
    std::atomic<float>* inputEqHpFreqParam = nullptr;
    std::atomic<float>* inputEqLpFreqParam = nullptr;
    std::atomic<float>* inputEqOrderParam = nullptr;
    std::atomic<float>* inputEqQParam = nullptr;
    std::atomic<float>* outputEqHpFreqParam = nullptr;
    std::atomic<float>* outputEqLpFreqParam = nullptr;
    std::atomic<float>* outputEqOrderParam = nullptr;
    std::atomic<float>* outputEqQParam = nullptr;

    // The five type switches. Read once per block like every other choice, so the
    // per-sample loops branch on plain ints rather than on atomics.
    std::atomic<float>* valveTypeParam = nullptr;
    std::atomic<float>* ampTypeParam = nullptr;
    std::atomic<float>* transformerTypeParam = nullptr;
    std::atomic<float>* digitalTypeParam = nullptr;
    std::atomic<float>* vinylTypeParam = nullptr;
    std::atomic<float>* stLinkParam = nullptr;
    std::atomic<float>* delaySyncParam = nullptr;
    std::atomic<float>* delayRateParam = nullptr;

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
    // DELTA listen is a monitor position rather than a level control, but it is
    // still a change of CONTENT, and a step change of content is a step in the
    // waveform - a click. Its target is the parameter, exactly like BYPASS's, and
    // its value is the crossfade between the two monitor positions further down.
    // Read once per SAMPLE next to bypassMix, for the same reason: one advance
    // per frame, shared by both channels.
    SampleSmoother deltaListenSmoothed { sampleClock };

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

    // Subharmonic telemetry, published so the panel can show whether the cascade
    // is actually locked and to what. Without it the only way to tell a stage that
    // is tracking from one that is merely idling is to listen - and "is the
    // subfund doing anything" is exactly the question the readout should answer.
    std::atomic<float> subfundTrackedHz { 0.0f };
    std::atomic<float> subfundConfidence { 0.0f };

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
    //
    // Each stage ALSO has a second, per-channel detector pair. ST LINK decides
    // which pair is used: at 100 % the shared detector above supplies one gain for
    // both sides (a stereo-linked bus compressor, and what every earlier build
    // did); below that the two per-channel detectors take over, crossfaded in, so
    // a loud left channel ducks only the left.
    //
    // The second pair is a separate object rather than two more fields on
    // GlueCompressor because the linked path must keep running even while the
    // unlinked one is in use - otherwise moving ST LINK would resume the shared
    // detector from a stale envelope and step the gain.
    GlueCompressor inputCompressor;
    GlueCompressor outputCompressor;
    std::array<GlueCompressor, 2> inputCompressorChannels;
    std::array<GlueCompressor, 2> outputCompressorChannels;

    // Measures the harmonics the tape shaper is actually producing, separating even from
    // odd. This is the observable signature of the analogue character and drives the
    // HARMONICS display.
    HarmonicAnalyser harmonicAnalyser;

    // Output safety limiter, the stage that keeps the signal below the soft clipper so it
    // almost never has to act. `preLimiterDetector` is a fast peak follower and
    // `limiterGain` is the smoothed gain it applies; keeping them separate gives the
    // classic brick-wall shape - instant catch, musical release.
    //
    // `limiterCeiling` is the level the limiter pulls back to. It sits exactly on
    // softClip's knee (0.70), which is the only value that makes the hand-off
    // claim true: below the knee the clipper is bit-for-bit transparent, so anything
    // the limiter lets through is distortion the clipper would not have caused yet.
    float preLimiterDetector = 0.0f;
    float limiterCeiling = 0.70f;
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

    // PING-PONG is smoothed for the same reason the feedback amount is: it
    // decides how much of the repeat is written into each of the two delay
    // lines, and a step there would be a step in the waveform. It is a genuine
    // crossfade between the two routings, so the echoes move across the image
    // as the control turns rather than jumping sides.
    SampleSmoother pingPongSmoothed { sampleClock };

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
    //  MODELED TRACKS: the head-to-head bleed.
    //
    //  One shared per-channel history per channel, because the bleed is what
    //  this channel's head picks up OUT OF THE OTHER ONE - so each channel needs
    //  to be able to read what the other channel wrote a few samples ago.
    //
    //  The buffer is deliberately tiny: the longest layout's spacing is a few
    //  hundredths of a millisecond, which is a handful of samples even at 8x
    //  oversampling of 192 kHz. A 256-sample ring is generous by two orders of
    //  magnitude, which is the point - it cannot overflow at any supported rate,
    //  so the index maths needs no bounds test beyond the modulo.
    //
    //  The two channels share ONE ring: they write into their own track's slice
    //  and read the other's, which is what makes the bleed symmetric. Two
    //  separate rings would let one side's leak arrive before the other's.
    // -----------------------------------------------------------------------
    static constexpr int tracksBleedBufferLength = 256;
    std::array<std::array<float, tracksBleedBufferLength>, 2> tracksBleedBuffer {};
    int tracksBleedWritePosition = 0;

    // -----------------------------------------------------------------------
    //  Transport state (STOP / PLAY / START), and SPINDOWN.
    //
    //  `transportRamp` is 0 when the capstan is at rest and 1 when it is running
    //  at speed. It scales the wet path AND the transport modulation together,
    //  which is what makes STOP true silence rather than a mute and START a
    //  pitch ramp rather than a fade.
    //
    //  The STATE.
    //
    //  0 STOP   - the capstan is at rest. True silence: no hiss, no modulation,
    //             no delay tail.
    //  1 PLAY   - normal running. This is the resting "engaged" state and the one
    //             START settles into.
    //  2 START  - a TRANSIENT, not a third resting position. The capstan spins up
    //             over about a second and START is immediately done: the state
    //             advances to PLAY by itself (see the auto-advance in
    //             processTapeEngine).
    //
    //  START must NOT loop or stay engaged: the previous implementation left the
    //  ramp climbing forever because START and PLAY shared a target and the state
    //  never advanced. The auto-advance is now the point: pressing START, the
    //  capstan arrives at speed and the control reads PLAY.
    // -----------------------------------------------------------------------
    float transportRamp = 1.0f;
    float transportRampCoefficient = 0.0f;
    int lastTransportState = -1;

    // True while START (or a spindown recovery) is spinning the capstan up. The
    // auto-advance to PLAY watches this, so the transition happens once per
    // spin-up rather than being re-armed on every block.
    bool transportSpinningUp = false;

    /** The transport states, named rather than as bare integers so a reader can
        tell 2 from a typo. Values match the `transport` AudioParameterChoice. */
    enum class TransportState : int
    {
        stop    = 0,
        play    = 1,
        start   = 2
    };

    // -----------------------------------------------------------------------
    //  SPINDOWN.
    //
    //  A momentary hold, not a state: while the button is held the machine runs
    //  down like a turntable whose power has been cut, and when it is released
    //  the machine is simply stopped. That is a performance control rather than a
    //  transport setting, which is why it is not a fourth entry in the transport
    //  combo - it acts ON the transport rather than replacing it.
    //
    //  The run-down is deliberately slower than STOP's settle: a platter with
    //  mass does not stop instantly, and the whole point of the effect is the
    //  long tail as the pitch falls away. It also runs the pitch DOWN rather
    //  than the level, because a slowing turntable loses speed before it loses
    //  signal.
    //
    //  `spindownHeld` is written by the editor (message thread) and read by the
    //  engine (audio thread) every block, so it is an atomic. The two ramps are
    //  engine-only state and stay plain floats.
    // -----------------------------------------------------------------------
    std::atomic<bool> spindownHeld { false };
    float spindownRamp = 1.0f;              // 1 = at speed, 0 = fully stopped
    float spindownCoefficient = 0.0f;       // built from the rate
    // The previous block's hold state, so the release edge can be seen once. Engine
    // only: it is not read by the editor and needs no atomic.
    bool lastSpindownHeld = false;

    // -----------------------------------------------------------------------
    //  Output anti-phase prevention.
    //
    //  `antiPhaseProduct` is a slow correlation envelope of the two output
    //  channels' sum and difference: positive when the sides agree, negative when
    //  they oppose. `antiPhaseCorrection` is the 0..1 amount the guard rotates the
    //  right channel by, and it only ever rises while the correlation is negative.
    //  `antiPhaseCoefficient` is its block-rate pole (see resetSampleRateDependentState).
    //
    //  `antiPhaseAmount` is published for the panel so the user can SEE the guard
    //  working rather than only hearing its absence.
    // -----------------------------------------------------------------------
    float antiPhaseProduct = 0.0f;
    float antiPhaseCorrection = 0.0f;
    float antiPhaseCoefficient = 0.0f;
    float antiPhaseProductMagnitude = 1.0e-3f;
    std::atomic<float> antiPhaseAmount { 0.0f };

    // -----------------------------------------------------------------------
    //  Audio -> UI telemetry stream.
    //
    //  Every meter and readout on the panel is a value the audio thread produces
    //  and the editor consumes. They used to be a dozen separate std::atomics,
    //  each published with its own relaxed store and each read with its own
    //  relaxed load - which works, but it means one frame of metering can be read
    //  while it is half updated (a new peak next to last block's RMS), and every
    //  new readout needs another atomic added to the class.
    //
    //  A lock-free SPSC QUEUE fixes both: the audio thread pushes ONE complete
    //  frame of telemetry per block, the editor pops the newest one, so the values
    //  on screen always belong to the same block of audio, and a new readout is a
    //  field on the struct rather than a new member. cameron314/readerwriterqueue
    //  is the library for exactly this; when it is not on the include path the
    //  engine falls back to the plain atomics that were already there, so a build
    //  without the library behaves exactly as before.
    //
    //  The struct and the accessor are PUBLIC (declared above, with the other
    //  getters) so the editor can name the type; the QUEUE and the mirror atomics
    //  are private here, because only this class publishes to them.
    // -----------------------------------------------------------------------
#if J37_HAS_RWQ
    /** The audio-to-UI queue. 32 frames is about a second of metering at 30 Hz
        of UI polling - far more than the editor ever needs, and small enough that
        the whole ring is cache-friendly. The producer never blocks: if the UI has
        not drained it the push simply fails and the frame is dropped, which is
        the correct behaviour for telemetry (an old meter reading is worthless). */
    moodycamel::ReaderWriterQueue<TelemetryFrame, 32> telemetryQueue;
#endif

    /** The most recent frame, published by the audio thread. The editor reads it
        through getTelemetry(), which drains the queue when the library is present
        and otherwise reads these fields directly. */
    std::atomic<float> telemetryInputPeakDb { -70.0f };
    std::atomic<float> telemetryOutputPeakDb { -70.0f };
    std::atomic<float> telemetryInputRmsDb { -70.0f };
    std::atomic<float> telemetryOutputRmsDb { -70.0f };
    std::atomic<float> telemetryAntiPhase { 0.0f };
    std::atomic<float> telemetryPlatter { 1.0f };

    // Published for the editor's reels and lamp. Written once per block by the
    // audio thread, read with a relaxed load by the UI, exactly like the other
    // telemetry on this class.
    std::atomic<float> transportRampPublished { 1.0f };
    std::atomic<float> spindownRampPublished { 1.0f };

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

    // ST LINK, ramped like every other control-derived coefficient: it scales the
    // gain difference between the linked and unlinked paths, and a step there is a
    // step in the waveform.
    SampleSmoother stLinkSmoothed { sampleClock, false, false, 1.0f };

    // -----------------------------------------------------------------------
    //  Tempo sync.
    //
    //  The host's tempo, read from the playhead once per block and cached here.
    //  It is NOT an atomic: it is written and read on the audio thread only, and
    //  the editor has no business seeing it - the panel shows the resulting note
    //  value, which is a parameter, not the tempo.
    //
    //  `hostTempoValid` records whether the host actually supplied a tempo this
    //  block. A host that offers no playhead, or one that is stopped and reports
    //  nothing, leaves the previous value in place rather than resetting to zero:
    //  falling back to 120 would make the delay jump every time the transport
    //  stopped, which is worse than holding the last known tempo.
    // -----------------------------------------------------------------------
    double hostTempoBpm = 120.0;
    bool hostTempoValid = false;

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

    // -----------------------------------------------------------------------
    //  Block-rate envelope coefficients.
    //
    //  These were rebuilt inside the per-sample loop, which cost six std::exp
    //  per sample per channel for values that cannot change within a block -
    //  every one of them is a function of the sample rate alone. They are built
    //  next to the other block-rate coefficients in processTapeEngine and read
    //  as plain floats in the loop.
    //
    //  The limiter's two gain-smoothing coefficients were worse than that: they
    //  sat in a ternary, so BOTH branches were evaluated on every sample and one
    //  was discarded.
    // -----------------------------------------------------------------------
    float sagAttackCoefficient = 0.0f;
    float sagReleaseCoefficient = 0.0f;
    float limiterDetectorAttack = 0.0f;
    float limiterDetectorRelease = 0.0f;
    float limiterCatchCoefficient = 0.0f;
    float limiterRecoveryCoefficient = 0.0f;

    // -----------------------------------------------------------------------
    //  Preamp and distortion - the two gain stages in front of the machine.
    //  One per channel, because both carry signal-dependent bias state.
    // -----------------------------------------------------------------------
    InputStage inputStageL;
    InputStage inputStageR;

    SampleSmoother preampSmoothed { sampleClock };
    SampleSmoother distortionSmoothed { sampleClock };

    // -----------------------------------------------------------------------
    //  The DI box.
    //
    //  Three smoothed controls (engagement, load and transformer) and one
    //  block-rate value (the pad, which is a choice rather than a sweep and so
    //  needs no ramp). The three coefficients and the hum increment are built
    //  per block from the engine rate, like every other rate-dependent value.
    // -----------------------------------------------------------------------
    SampleSmoother diSmoothed { sampleClock };
    SampleSmoother diLoadSmoothed { sampleClock };
    SampleSmoother diTransformerSmoothed { sampleClock };
    float diPadDb = 0.0f;
    float diLoadCoefficient = 0.5f;
    float diTransformerCoefficient = 0.1f;
    float diHumIncrement = 0.0f;

    // The preamp's input-transformer low-cut, built once per block from the rate.
    float preampLowCutCoefficient = 0.5f;

    // -----------------------------------------------------------------------
    //  Tape condition: FLUX, WEAR and MECHANICS.
    // -----------------------------------------------------------------------
    TapeCondition tapeConditionL;
    TapeCondition tapeConditionR;

    SampleSmoother fluxSmoothed { sampleClock, false, false, 0.5f };
    SampleSmoother wearSmoothed { sampleClock };
    SampleSmoother mechanicsSmoothed { sampleClock };

    // The FLUX shelf and the WEAR loss, built once per block. Two frequencies
    // because the two mechanisms sit in different places: flux is a low shelf,
    // wear is a top-end loss.
    float fluxShelfCoefficient = 0.5f;
    float wearLossCoefficient = 0.5f;

    // The WEAR contact noise, folded into the wow/flutter modulation as an extra
    // slow irregularity. Kept separate from the MECHANICS drift because they are
    // different mechanisms: one is the medium, the other is the transport.
    float wearModulation = 0.0f;

    // -----------------------------------------------------------------------
    //  Reverb - one instance, two channels inside it. It is placed after the
    //  machine, so it is a single stereo processor rather than two mono ones.
    // -----------------------------------------------------------------------
    PlateReverb reverb;
    SampleSmoother reverbMixSmoothed { sampleClock };
    SampleSmoother reverbSizeSmoothed { sampleClock, false, false, 0.4f };

    // -----------------------------------------------------------------------
    //  Vinyl stage. One per channel, because the crackle and the rumble are
    //  uncorrelated between the two sides - sharing a generator would make the
    //  ticks appear in the centre of the image instead of on the surface.
    // -----------------------------------------------------------------------
    VinylStage vinylL;
    VinylStage vinylR;

    // -----------------------------------------------------------------------
    //  The two equalisers.
    //
    //  One instance each, at the two ends of the chain - see the parameter
    //  layout for why the POSITION is the point. Each holds its own filter
    //  state, so the input EQ's splits and the output EQ's are independent and
    //  neither can be disturbed by the other.
    // -----------------------------------------------------------------------
    ThreeBandEq inputEq;
    ThreeBandEq outputEq;

    // The EQ band gains, smoothed so dragging one glides rather than steps. Six
    // smoothies, one per band per EQ, all on the shared sample clock so they
    // advance exactly once per frame. They start at 1.0 (unity), which is what
    // makes a fresh instance bit-for-bit transparent.
    SampleSmoother inputEqLowSmoothed { sampleClock, false, false, 1.0f };
    SampleSmoother inputEqMidSmoothed { sampleClock, false, false, 1.0f };
    SampleSmoother inputEqHighSmoothed { sampleClock, false, false, 1.0f };
    SampleSmoother outputEqLowSmoothed { sampleClock, false, false, 1.0f };
    SampleSmoother outputEqMidSmoothed { sampleClock, false, false, 1.0f };
    SampleSmoother outputEqHighSmoothed { sampleClock, false, false, 1.0f };

    SampleSmoother vinylSmoothed { sampleClock };
    SampleSmoother noiseLvlSmoothed { sampleClock, false, false, 1.0f };
    SampleSmoother vinylCrackleSmoothed { sampleClock, false, false, 0.5f };
    SampleSmoother vinylRumbleSmoothed { sampleClock, false, false, 0.35f };

    float vinylRumbleCoefficient = 0.5f;
    float vinylWarmthCoefficient = 0.5f;

    // -----------------------------------------------------------------------
    //  Delay character (TAPE / BBD / MODERN) and the mode switches.
    //
    //  `delayTypeCached` mirrors the choice parameter so the per-sample loop can
    //  branch on an int rather than reading an atomic; `modernMode` and
    //  `lofiMode` are the two machine-voicing switches, read once per block.
    // -----------------------------------------------------------------------
    int delayTypeCached = 0;

    // The five type switches, cached per block for the same reason the delay's is:
    // the per-sample work below must branch on a register, not on an atomic. The
    // vinyl type also drives the per-block voice fields on the VinylStages, which
    // are written once here and only read in the loop.
    int valveTypeCached = 0;
    int ampTypeCached = 0;
    int transformerTypeCached = 0;
    int digitalTypeCached = 0;
    int vinylTypeCached = 0;
    bool modernMode = false;
    bool lofiMode = false;

    // The BBD delay's clock noise and its bandwidth state, per channel.
    // The BBD's clock-noise generator, per channel. A uint32_t LCG state like
    // `noiseState` and `vinylNoiseState` - it is an integer recurrence, not a
    // level, so float was simply the wrong type for it.
    std::uint32_t bbdNoiseStateL = 0x85ebca6bu;
    std::uint32_t bbdNoiseStateR = 0xc2b2ae35u;
    float bbdLowL = 0.0f;
    float bbdLowR = 0.0f;

    // The BBD's bandwidth coefficient, refreshed on a stride rather than per sample.
    // It has to follow the smoothed delay time, so it cannot be a block-rate constant
    // - but it does not need an std::exp per sample either. See the use site.
    float bbdLowCoefficient = 0.5f;
    int bbdCoefficientCountdown = 1;
    static constexpr int bbdCoefficientStride = 32;

    // The LO-FI mode's quantisation and bandwidth state, per channel. The hold
    // counter is shared so both channels sample on the same instants; the held
    // VALUES are per channel so the image is preserved.
    float lofiLowL = 0.0f;
    float lofiLowR = 0.0f;
    float lofiHoldL = 0.0f;
    float lofiHoldR = 0.0f;
    int lofiCounter = 0;

    // The LO-FI band limit and its sample-and-hold length, built once per block
    // from the rate so the mode sounds the same at 44.1 and 192 kHz.
    float lofiLowCoefficient = 0.5f;
    int lofiHoldSamples = 4;

    // The MODERN mode's voicing offsets, applied to the block-rate coefficients
    // rather than to the signal: a modern deck's head losses are further out of
    // the audio band and its floor is lower, so it is the COEFFICIENTS that move.
    float modernHeadGapScale = 1.0f;
    float modernHissScale = 1.0f;
    float modernHysteresisScale = 1.0f;

    // A second, independent noise generator for the vinyl stage. It is separate
    // from `noiseState` so that turning VINYL on cannot change the tape hiss
    // stream - the two are different sources and must stay uncorrelated.
    std::uint32_t vinylNoiseState = 0x9e3779b9u;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FirstAudioProcessor)
};
