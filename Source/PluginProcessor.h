/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

// Which optional libraries this build actually has - chowdsp's math and DSP utils,
// RTNeural, xsimd and the four real-time-safe third-party utilities - is decided in
// ONE place now, Source/DSP/OptionalLibraries.h, and reported as the J37_HAS_*
// macros. It moved out of this file because the stages that read those macros live
// in headers of their own and are compiled by harnesses that have neither JUCE nor
// the libraries on their include path: a header that branches on a macro has to be
// able to find the file that decides it. What it defines is J37_HAS_CHOWDSP_MATH,
// J37_HAS_CHOWDSP_DSP, J37_HAS_RTNEURAL, J37_HAS_XSIMD, J37_HAS_RWQ, J37_HAS_FARBOT,
// J37_HAS_SIGNALSMITH and J37_HAS_SOUNDTOUCH.
//
// It is included at exactly this point because Source/DSP/J37Math.h below reads the
// first of them and Source/DSP/NeuralStage.h reads the third.
#include "DSP/OptionalLibraries.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// The scalar math bridge every DSP stage calls. Moved out of this file, so the
// harnesses can include it directly instead of having its text cut out of here;
// Source/DSP/J37Math.h states the conventions the rest of Source/DSP/ follows.
// It is included at exactly this point because it reads J37_HAS_CHOWDSP_MATH,
// which Source/DSP/OptionalLibraries.h above decides.
#include "DSP/J37Math.h"

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


// The saturation core and the four type lists that voice its non-tape principles.
// Moved out of this file, so the harnesses include it directly instead of having
// its text cut out of here.
#include "DSP/SaturationCore.h"

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

// The transient designer. Moved out of this file; measured on signal by
// tests/dsp/subfund_test.cpp, which includes the header directly.
#include "DSP/TransientShaper.h"


// The neural stage, and with it the rate bridge it HOLDS and the optional-library
// probes it is compiled under: the header includes both itself, so this file no
// longer has to put the bridge ahead of the stage that holds one. It is the last
// struct the DSP harness still had to cut by text, and it is compiled in two shapes
// - tests/dsp/ includes it with J37_HAS_RTNEURAL 0 (the pass-through) and
// tests/neural/ with it 1, against a stand-in for RTNeural's own API.
#include "DSP/NeuralStage.h"


// The IR cabinet stage: one convolution per channel against a user-loaded impulse
// response. Moved out of this file, prose and all: Source/DSP/ is where its code and
// the note explaining it now live together. It is also the header that says, in the
// directory's own terms, which JUCE modules its includer has to have - and the header
// that made tests/dsp/check_headers_juce.sh grow juce_dsp and juce_audio_formats.
#include "DSP/IrStage.h"

// The input stage: the preamp, the distortion and the DI box. Moved out of this
// file, prose and all: Source/DSP/ is where its code and the note explaining it
// now live together.
#include "DSP/InputStage.h"

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

// The plate reverb. Moved out of this file, prose and all: Source/DSP/ is where
// its code and the note explaining it now live together.
#include "DSP/PlateReverb.h"

// The vinyl stage: surface noise, rumble and the RIAA playback curve. Moved out
// of this file, so the harnesses can include it directly instead of having its
// text cut out of here.
#include "DSP/VinylStage.h"

// The equaliser: three tone bands, a high-pass and a low-pass. Moved out of this
// file, prose and all: Source/DSP/ is where its code and the note explaining it
// now live together.
#include "DSP/ThreeBandEq.h"

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

// The glue compressor. Moved out of this file; measured on signal by
// tests/dsp/subfund_test.cpp, which includes the header directly.
#include "DSP/GlueCompressor.h"


//==============================================================================
/** A conventional feed-forward compressor for the finished signal. It is
    deliberately separate from GlueCompressor, including its detector state. */
struct StandardCompressor
{
    float envelope = 0.0f;
    float gain = 1.0f;

    void reset() noexcept { envelope = 0.0f; gain = 1.0f; }

    float process (float input, float sampleRate, float thresholdDb, float ratio,
                   float attackMs, float releaseMs, float kneeDb, float makeupDb) noexcept
    {
        const auto detector = input * input;
        const auto coefficientForMs = [sampleRate] (float milliseconds)
        {
            const auto seconds = juce::jmax (0.001f, milliseconds * 0.001f);
            return 1.0f - std::exp (-1.0f / (juce::jmax (1.0f, sampleRate) * seconds));
        };
        const auto detectorCoefficient = detector > envelope
            ? coefficientForMs (attackMs) : coefficientForMs (releaseMs);
        envelope += (detector - envelope) * detectorCoefficient;
        const auto envelopeDb = juce::Decibels::gainToDecibels (std::sqrt (juce::jmax (0.0f, envelope)), -100.0f);
        const auto safeRatio = juce::jmax (1.0f, ratio);
        const auto halfKnee = juce::jmax (0.01f, kneeDb * 0.5f);
        const auto over = envelopeDb - thresholdDb;
        float reductionDb = 0.0f;
        if (over > halfKnee)
            reductionDb = -(1.0f - 1.0f / safeRatio) * over;
        else if (over > -halfKnee)
        {
            const auto progress = over + halfKnee;
            reductionDb = -(1.0f - 1.0f / safeRatio) * progress * progress
                        / (4.0f * halfKnee);
        }
        const auto targetGain = juce::Decibels::decibelsToGain (reductionDb + makeupDb);
        const auto gainCoefficient = targetGain < gain
            ? coefficientForMs (attackMs) : coefficientForMs (releaseMs);
        gain += (targetGain - gain) * gainCoefficient;
        return input * gain;
    }
};

// The K-weighted loudness meter. Moved out of this file; measured on signal by
// tests/dsp/loudness_test.cpp, which includes the header directly.
#include "DSP/LoudnessMeter.h"

// The subharmonic generator: the undertone series beneath the programme. Moved out
// of this file, so the harness can include it directly instead of having its text
// cut out of here.
#include "DSP/SubharmonicGenerator.h"

// The harmonic analyser. Moved out of this file, prose and all: Source/DSP/ is where
// its code and the note explaining it now live together.
#include "DSP/HarmonicAnalyser.h"

//==============================================================================
// The pitch tracker: the note the autotune stage corrects towards. Moved out of
// this file, so tests/pitch/ can include it directly instead of having its text
// cut out of here.
#include "DSP/PitchTracker.h"

// The realtime pitch shifter the autotune stage moves the audio with. Moved out
// for the same reason as the tracker above.
#include "DSP/RealtimePitchShifter.h"

// The transport rig: one capstan, three keys and a power cut. Moved out of this
// file, so tests/transport/ can include it directly instead of having its text cut
// out of here.
#include "DSP/TransportRig.h"

// The repitch stage: what the machine made, played back at the platter's speed.
// Moved out for the same reason as the rig above.
#include "DSP/RepitchStage.h"

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


    struct BlockContext
    {
        int samples = 0;
        int channels = 0;
        float sampleRate = 0.0f;
    };
    bool processBypassedBlock (juce::dsp::AudioBlock<float>&, const BlockContext&);
    void processPostMachineStages (juce::dsp::AudioBlock<float>&, const BlockContext&);
    void processAutoTuneSample (float* stereoSamples, int channels, float rate) noexcept;
    void publishBlockTelemetry (const BlockContext& context,
                                float inputPeak, float inputRms,
                                float outputPeak, float outputRms,
                                float inputPeakReductionDb, float peakReductionDb,
                                float inputEnvelopeActivity, float driftNow,
                                bool clipping, bool inputClipping,
                                float currentLufs, float currentInputLufs, float driveInto);
    static const std::array<int, 12>& scaleOffsetsForMode (int modeIndex) noexcept;

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

    /** Number of factory presets, including the Minimum and Maximum range endpoints.
        Derived from the resource list, which CMake builds from the JSON files - so
        it cannot be 26 while the files say something else. */
    static int numFactoryPresets();

    /** Display names of the factory presets, in order, read from the JSON files. */
    static juce::StringArray getPresetNames();

    /** Applies factory preset `index` (0..numFactoryPresets()-1) with one undo transaction. */
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
        here and the engine advances it to Play when the capstan arrives. */
    void setTransportState (int state);

    /** Flushes an engine-originated transport transition on a non-audio thread. */
    void flushPendingTransportHostSync();

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

    /** True while the START key is armed against the host: it was pressed on a DAW
        whose transport was not rolling, so the machine is at rest WAITING for it
        rather than spinning up. The parameter still reads START while this is
        true, which is what makes the flag necessary - without it the panel could
        not tell a waiting key from a machine that is simply stopped. */
    bool isTransportArmedForHost() const noexcept
    {
        return transportArmedPublished.load (std::memory_order_relaxed);
    }

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
    //  Neural stage, from the editor.
    //
    //  A model is DATA, not a parameter: it is a file the user picks, and the
    //  engine has no business knowing where it came from. These are the two
    //  entry points the editor uses - install a model from its JSON text, and ask
    //  whether one is currently loaded - and both are safe to call from the
    //  message thread. The audio thread only ever reads the resulting model, so
    //  a load is a pointer swap behind the stage's own guard rather than a lock
    //  on the audio thread.
    //==============================================================================

    /** Installs a model from RTNeural's JSON text into both channels. Returns
        false and changes nothing when the text is empty, malformed, or the
        library is not in the build. */
    bool loadNeuralModel (const juce::String& modelJson);

    /** Loads a NeuralAmpModeler (.nam) model from its raw JSON text. A .nam
        file is a DIFFERENT wire format from RTNeural's (one flat weight vector
        shaped by "config" rather than a stack of named layers), so it goes
        through its own converter in NeuralStage. Returns false - leaving any
        previously loaded model untouched - when the text does not parse as a
        .nam file this build can play. */
    bool loadNamModel (const juce::String& namJson);

    /** Installs the impulse response `file` into the IR cabinet stage.
        Returns false when the file is not audio JUCE can decode. */
    bool loadIrFile (const juce::File& irFile);

    /** Releases the loaded impulse response. */
    void clearIr();

    /** True while an impulse response is loaded. */
    bool hasIr() const noexcept { return irStage.hasIr; }

    /** Releases any installed model, returning the stage to the pass-through. */
    void clearNeuralModel();

    /** The rate the loaded model was trained at, in Hz - 0 when that is unknown,
        which is the case for every RTNeural JSON model (the format carries no
        rate) and for no model at all. A .nam file carries it, and the stage runs
        such a model at THIS rate whatever the engine is doing; see RateBridge. */
    double getNeuralModelRate() const noexcept
    {
        return neuralModelRate.load (std::memory_order_relaxed);
    }

    /** The rate the ENGINE is running at: the session's rate times the
        oversampling factor. Published because it is the rate a model would be run
        at if it were not being converted to its own, which is the comparison the
        panel makes when it says which rate a model is playing at. */
    double getEngineSampleRate() const noexcept
    {
        return neuralEngineRate.load (std::memory_order_relaxed);
    }

    /** True when a model is installed and the stage can run it. */
    bool hasNeuralModel() const noexcept { return neuralL.isActive(); }

    /** Why the last load FAILED, in the few words the panel's readout can put
        after "INVALID". Empty after a successful load and before the first
        attempt. Written and read on the MESSAGE thread - the picker's callback
        is the only caller of the three loaders - so a plain String is enough
        and nothing here is visible to the audio thread. */
    const juce::String& lastLoadError() const noexcept { return lastLoadErrorMessage; }

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
    // The anti-phase guard's switch. Read once per block beside the width it
    // protects, never per sample: the guard's own pole is what makes it slow, so
    // the switch has no need to be instantaneous.
    std::atomic<float>* antiPhaseGuardParam = nullptr;
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
    // The delay's LEVEL and its feedback are the same parameter by design, so
    // there is one member for both - see the read in processBlock.
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
    std::atomic<float>* tuningTonicParam = nullptr;
    std::atomic<float>* tuningModeParam = nullptr;
    std::atomic<float>* autoTuneParam = nullptr;
    std::atomic<float>* autoTuneAmountParam = nullptr;
    std::atomic<float>* compressorThresholdParam = nullptr;
    std::atomic<float>* compressorRatioParam = nullptr;
    std::atomic<float>* compressorAttackParam = nullptr;
    std::atomic<float>* compressorReleaseParam = nullptr;
    std::atomic<float>* compressorMakeupParam = nullptr;
    std::atomic<float>* compressorMixParam = nullptr;
    // The seven SOURCE knobs - the shares of the six saturation principles.
    // (The old blend/shape sweep pair was removed: the user asked for the
    // machines to be dialled by name.)
    std::atomic<float>* tapeSourceParam = nullptr;
    std::atomic<float>* vinylSourceParam = nullptr;
    std::atomic<float>* cassetteSourceParam = nullptr;
    std::atomic<float>* digitalSourceParam = nullptr;
    std::atomic<float>* ampSourceParam = nullptr;
    std::atomic<float>* valveSourceParam = nullptr;
    std::atomic<float>* transformerSourceParam = nullptr;
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
    std::atomic<float>* inputEqHpOnParam = nullptr;
    std::atomic<float>* inputEqLpOnParam = nullptr;
    std::atomic<float>* inputEqLpFreqParam = nullptr;
    std::atomic<float>* inputEqOrderParam = nullptr;
    std::atomic<float>* inputEqQParam = nullptr;
    std::atomic<float>* outputEqHpFreqParam = nullptr;
    std::atomic<float>* outputEqHpOnParam = nullptr;
    std::atomic<float>* outputEqLpOnParam = nullptr;
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

    // The transient shaper's three controls and the neural stage's wet/dry. Read
    // once per block like every other control so the per-sample loop branches on
    // plain floats rather than on atomics.
    std::atomic<float>* transientAttackParam = nullptr;
    std::atomic<float>* transientSustainParam = nullptr;
    std::atomic<float>* transientMixParam = nullptr;
    std::atomic<float>* neuralMixParam = nullptr;
    // The IR stage's wet/dry. Read once per block like every other control.
    std::atomic<float>* irMixParam = nullptr;

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
    std::array<StandardCompressor, 2> standardCompressors;
    std::array<RealtimePitchShifter, 2> autoTuneShifters;
    // One YIN tracker per channel: the estimate a corrector needs is the pitch of
    // the note in THIS channel, and a stereo mix's two notes are not the same
    // note. The tracker is only fed while the stage is switched on - see
    // processAutoTuneSample, which is the only caller.
    std::array<PitchTracker, 2> autoTuneTrackers;
    std::array<float, 2> autoTuneCorrectionSmoothed { 1.0f, 1.0f };
    juce::AudioBuffer<float> postFxScratch;

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
    SampleSmoother delayLevelSmoothed { sampleClock };

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
    //  The capstan itself is `transport` - a TransportRig, documented above this
    //  class - because it is one piece of state that the engine, the panel and a
    //  saved session all have to agree about, and because its gesture is worth
    //  being able to exercise on its own (tests/transport compiles this struct
    //  out of this header and drives it).
    //
    //  What stays here is exactly what the MESSAGE thread touches: the momentary
    //  hold, and the one transition the engine asks the host to follow (START
    //  settling into PLAY, or a spindown released on a stopped machine).
    // -----------------------------------------------------------------------
    TransportRig transport;

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
    //  long tail as it winds down. It multiplies the transport, so a hold on a
    //  running machine runs it down and a hold on a stopped one brings it up
    //  first - either way the platter is what the gesture moves.
    //
    //  `spindownHeld` is written by the editor (message thread) and read by the
    //  engine (audio thread) every block, so it is an atomic. The ramp it feeds
    //  lives in the rig, which is engine-only state.
    // -----------------------------------------------------------------------
    std::atomic<bool> spindownHeld { false };
    std::atomic<bool> transportHostSyncRequested { false };
    std::atomic<int> transportHostSyncState { 1 };

    // -----------------------------------------------------------------------
    //  Output anti-phase prevention.
    //
    //  `antiPhaseProduct` is a slow correlation envelope of the two output
    //  channels' sum and difference: positive when the sides agree, negative when
    //  they oppose. `antiPhaseCorrection` is the 0..1 amount the guard rotates the
    //  pair's mid/side plane by - it is the SINE of the rotation, so it reaches a
    //  quarter turn (full recovery of an inverted pair) at 1 - and it only ever
    //  rises while the correlation is negative. `antiPhaseCoefficient` is its
    //  per-sample pole (see resetSampleRateDependentState).
    //
    //  `antiPhaseAmount` is published for the panel so the user can SEE the guard
    //  working rather than only hearing its absence. It reads 0 whenever the guard
    //  is switched off, which is the one thing the readout cannot say on its own.
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
    // mutable: getTelemetry() is const and drains the queue to the newest
    // frame. Draining mutates the queue's internal read cursor but not the
    // machine's state, so the queue is mutable rather than the accessor.
    //
    // The queue's own alignas(8) is what trips C4324 ("structure was padded
    // due to alignment specifier") on every class that holds one. It is the
    // third-party header's choice, not ours - suppressed, not argued with.
    JUCE_BEGIN_IGNORE_WARNINGS_MSVC (4324)
    mutable moodycamel::ReaderWriterQueue<TelemetryFrame, 32> telemetryQueue;
    JUCE_END_IGNORE_WARNINGS_MSVC
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

    // True while the START key is armed against the host (see the arm in
    // TransportRig). Written once per block by the audio thread, beside the
    // request it publishes, and read with a relaxed load by the panel's deck
    // readout. While the engine is bypassed nothing writes it, so it holds the
    // last block's answer - which is the honest UI reading of a machine that is
    // not being run at all.
    std::atomic<bool> transportArmedPublished { false };

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
    // The SOURCE shares ride the same kind of smoother every other control
    // does, so a mix change cannot step the weights either. Two are paired on
    // one smoother only in the sense that setWeights consumes them together.
    SampleSmoother tapeSourceSmoothed { sampleClock, false, false, 1.0f };
    SampleSmoother vinylSourceSmoothed { sampleClock };
    SampleSmoother valveSourceSmoothed { sampleClock };
    SampleSmoother cassetteSourceSmoothed { sampleClock };
    SampleSmoother ampSourceSmoothed { sampleClock };
    SampleSmoother transformerSourceSmoothed { sampleClock };
    SampleSmoother digitalSourceSmoothed { sampleClock };

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

    // -----------------------------------------------------------------------
    //  Transient shaper - one per channel, because it carries envelope state.
    //
    //  It runs on the finished signal, after the tape nonlinearity, so what it
    //  shapes is the machine's output rather than something fed back into the
    //  curve. The three controls ramp like every other gain so dragging one
    //  cannot step the waveform, and the two detector poles are block-rate
    //  constants built from the engine rate.
    // -----------------------------------------------------------------------
    TransientShaper transientL;
    TransientShaper transientR;

    SampleSmoother transientAttackSmoothed { sampleClock };
    SampleSmoother transientSustainSmoothed { sampleClock };
    SampleSmoother transientMixSmoothed { sampleClock, false, false, 0.0f };

    // The two envelope-detector poles and the applied-gain smoother, rebuilt
    // per block from the engine rate. The fast pole is the transient window
    // (~2 ms) and the slow one is the reference (~120 ms): the gap between them
    // is what makes an edge read as an edge rather than as programme.
    float transientFastCoefficient = 0.0f;
    float transientSlowCoefficient = 0.0f;
    float transientGainCoefficient = 0.0f;

    // -----------------------------------------------------------------------
    //  Neural stage - one instance per channel, one model pointer shared because
    //  the MODEL is read-only at run time and the model object itself holds no
    //  per-channel state.
    //
    //  The per-channel state lives in NeuralStage itself (the input frame and the
    //  recurrent hidden state), so the two channels need two instances; but the
    //  loaded model is the same object, and sharing it is both correct and the
    //  point - it is data, not a voice.
    //
    //  `neuralModel` is owned here (public, so the editor can install one from a
    //  file) and the two stages below borrow it. `neuralMixSmoothed` is the
    //  wet/dry ramp.
    // -----------------------------------------------------------------------
    NeuralStage neuralL;
    NeuralStage neuralR;
    SampleSmoother neuralMixSmoothed { sampleClock, false, false, 0.0f };

    //  The rate the loaded model was trained at, and the rate the engine runs at.
    //  Atomics because either can be set from either thread - a model is loaded on
    //  the message thread, and the oversampling factor that moves the engine's
    //  rate is switched from the audio thread when its host parameter changes -
    //  and both are pushed to the stages by updateNeuralSampleRates(), which the
    //  stages then latch on the audio thread.
    std::atomic<double> neuralModelRate { 0.0 };
    // Seeded at the rate a session that has not been prepared yet is assumed to be
    // at, rather than at zero: the panel compares this against a loaded model's own
    // rate to decide whether to say anything, and a zero here would make every
    // model look like it was being converted until the first prepare.
    std::atomic<double> neuralEngineRate { 48000.0 };

    /** Hands the two rates to both neural stages. Called from the three places
        either number can have changed: a model load, prepareToPlay, and a change
        of oversampling factor. */
    void updateNeuralSampleRates();

    //  The IR cabinet stage: one convolution per channel against a user-loaded
    //  impulse response, blended by IR MIX (ir_mix) exactly the way NEURAL
    //  blends the model. The convolutions run on the FINISHED block after the
    //  per-sample loop, beside the reverb and the vinyl - the engine's own
    //  note there explains why a stage with a tail cannot live in that loop.
    IrStage irStage;
    SampleSmoother irMixSmoothed { sampleClock, false, false, 0.0f };

    //  The repitch stage: the one place the transport's speed changes the PITCH
    //  rather than the level. Prepared at the ENGINE rate, advanced once per
    //  frame; see the stage's own note above for what it does and why it is
    //  bounded the way it is.
    RepitchStage repitchStage;

    //  The reason the last file load failed, so the picker can say WHAT was
    //  wrong with the file instead of only that something was. Message thread
    //  only; see the accessor above for why no guard is needed.
    juce::String lastLoadErrorMessage;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FirstAudioProcessor)
};
