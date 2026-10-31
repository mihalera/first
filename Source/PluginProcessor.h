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
// J37_HAS_RTNEURAL, J37_HAS_XSIMD, J37_HAS_RWQ and J37_HAS_POCKETFFT - and every one
// of them is read by a stage, which tests/linkline/check_linkline.py checks. The last of those is
// read by Source/DSP/SpectrumAnalyser.h - the one stage that needs a library this
// repository does not vendor JUCE-side - and it is included below with the rest of
// the engine's stages, because its bands travel in the telemetry frame this file
// declares.
//
// It is included at exactly this point because Source/DSP/J37Math.h below reads the
// first of them and Source/DSP/NeuralStage.h reads the third.
#include "DSP/AutoGainCompensator.h"
#include "DSP/MultibandPhaseGuard.h"
#include "DSP/SafetyLimiter.h"
#include "DSP/OptionalLibraries.h"

// The OSC codec and its address map (Source/Net). They are JUCE-free headers, so
// including them here costs nothing to a harness and buys the processor one thing:
// the module's outgoing messages are built by the SAME Surface the receiving half
// will one day route with, so the addresses the panel publishes are not a second
// copy of the map. See the banner in PluginProcessor.cpp for what is still missing
// before the surface is switched on as a listener.
#include "Net/OscSurface.h"

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


// The amplifier's behaviour around the clipping curve - sag, presence, the
// cabinet and the bias shift. Moved out of this file, prose and all: Source/DSP/
// is where its code and the note explaining it now live together.
#include "DSP/AmpVoicing.h"

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

// The tape condition: FLUX, WEAR and MECHANICS, the three slow facts about the
// machine. Moved out of this file, prose and all: Source/DSP/ is where its code
// and the note explaining it now live together.
#include "DSP/TapeCondition.h"

// The room. Dattorro's plate against el-visio/dattorro-verb, wrapped in
// Source/DSP/: the tank runs at the rate its delay lengths were chosen at and the
// wrapper carries the signal across. Prose and all, it lives there now.
#include "DSP/DattorroRoom.h"

// The vinyl stage: surface noise, rumble and the RIAA playback curve. Moved out
// of this file, so the harnesses can include it directly instead of having its
// text cut out of here.
#include "DSP/VinylStage.h"

// The vinyl stage's user noise print: the WAV decoder (Source/DSP/NoiseDecoder.*)
// and the background loader that feeds the two stages above it. Moved out of this
// file for the same reason, and it is the header that names the ONE JUCE module
// it needs (juce_core) rather than inheriting it: everything else it uses is std.
#include "DSP/NoiseLoader.h"

// The equaliser: three tone bands, a high-pass and a low-pass. Moved out of this
// file, prose and all: Source/DSP/ is where its code and the note explaining it
// now live together.
#include "DSP/ThreeBandEq.h"

// The sample clock and the smoother that reads it: the one place where JUCE's
// SmoothedValue has to advance exactly once per SAMPLE rather than once per read.
// Moved out of this file, prose and all.
#include "DSP/SampleSmoother.h"

// The glue compressor. Moved out of this file; measured on signal by
// tests/dsp/subfund_test.cpp, which includes the header directly.
#include "DSP/GlueCompressor.h"


// The conventional feed-forward compressor on the finished signal, separate
// from GlueCompressor down to its detector state. Moved out of this file, prose
// and all.
#include "DSP/StandardCompressor.h"

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

// The spectrum analyser, which reads the same shaper output the harmonic balance
// above is measured on and reports it as a curve instead of two ratios. It pulls
// pocketfft in when the build has it (see the probe in Source/DSP/
// OptionalLibraries.h), which is one more header in every translation unit that
// includes this one - measured at about 0.6 s of -fsyntax-only on the machine this
// was written on, against the 2.0 s the three JUCE modules above already cost.
#include "DSP/SpectrumAnalyser.h"

//==============================================================================
// The pitch tracker: the note the autotune stage corrects towards. Moved out of
// this file, so tests/pitch/ can include it directly instead of having its text
// cut out of here.
#include "DSP/PitchTracker.h"

// The autotune stage's audio path: signalsmith-stretch's phase vocoder when the
// build has it, the granular shifter when it does not. Moved out for the same
// reason as the tracker above.
#include "DSP/CorrectorStage.h"

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
                             private juce::AudioProcessorValueTreeState::Listener,
                             private juce::AsyncUpdater
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

    /** The OSC module's oscillator: the note the deck's ring landed on, mixed into
        the machine's INPUT before any stage touches it - never onto the output, so
        it is the tape, the mix and the output trim the user dialled that shape it.

        Called from processTapeEngine after the BYPASS early-out, so a machine that
        is switched out generates nothing. All of its state is the three floats and
        the counter declared beside `oscOnParam`, and all of them belong to the
        audio thread. */
    void mixOscillatorIntoInput (juce::dsp::AudioBlock<float>& block,
                                 float engineSampleRate, int channels);

    /** The tape engine proper; processBlock routes into this, oversampled or not. */
    void processTapeEngine (juce::dsp::AudioBlock<float>, juce::MidiBuffer&);


    struct BlockContext
    {
        int samples = 0;
        int channels = 0;
        float sampleRate = 0.0f;
    };

    /** One block's worth of the tape machine: what processTapeEngine() works out before a
        sample is touched, and what the per-sample loop then reads and carries.

        This is the boundary that was a capture list. The loop was a 1,669-line lambda
        inside processTapeEngine() that captured 68 of that function's own locals by
        reference, so nothing - not a reader, not the compiler - could tell the block's
        setup from one sample's work; now the setup fills this in and the loop takes it as
        an argument, and `processOneSample` can be read on its own.

        Three things about it are deliberate:

          * **A field cannot be `const`.** The locals it replaces were mostly declared
            `const` (a block's coefficients do not change while it runs). What those
            promises are now is a rule of the split: the setup writes every field, once,
            before the loop starts, and the loop reads them - except the fifteen
            accumulators at the end of the struct, which are what the loop carries from
            sample to sample and are the only fields it writes.
          * **The fields are grouped by what they are**, not by the order the old
            declarations happened to be in: the block's frame, the controls the hot path
            reads, the machine's curves and levels, the two equalisers' mid-band
            coefficients, the glue pair's operating points, the meters' coefficients, and
            then the loop's own state.
          * **It holds values, not references**, so a block's state is one object with one
            lifetime: processTapeEngine()'s stack frame. Nothing here can outlive the block
            it describes, which is what makes passing it by reference into the machine
            safe. */
    struct TapeBlockState
    {
        // The block's own frame.
        float engineSampleRate = 1.0f;
        int activeChannels = 0;
        std::array<float*, 2> channelData {};

        // The controls the hot path reads.
        bool antiPhaseGuardOn = false;
        bool autoGainEnabled = false;
        float autoTuneStrength = 0.0f;
        float compressorAttack = 0.0f;
        float compressorMakeup = 0.0f;
        float compressorMixAmount = 0.0f;
        float compressorRatio = 0.0f;
        float compressorRelease = 0.0f;
        float compressorThreshold = 0.0f;
        bool offsetRightChannel = false;
        float polaritySign = 1.0f;
        int trackBleedDelaySamples = 0;
        float trackCrosstalk = 0.0f;
        float trackWidthScale = 1.0f;

        // The machine's curves, its poles and the levels it works between.
        float dcBlockR = 0.0f;
        float driveCurve = 0.0f;
        float finalOutputGain = 1.0f;
        float flutterDepth = 0.0f;
        float flutterFreq = 0.0f;
        float headGapModulationMask = 0.0f;
        float hissBandLimit = 0.0f;
        float loadInducedSpeedError = 0.0f;
        float platterGateExponent = 0.0f;
        float platterPitchExponent = 0.0f;
        float speedBias = 0.0f;
        float tapeCurve = 0.0f;
        float tapeHiss = 0.0f;
        bool tapeTypeSwitching = false;
        float transportActivityGate = 0.0f;
        float twoPi = 0.0f;
        float wearWowCoupling = 0.0f;
        float wowDepth = 0.0f;
        float wowFreq = 0.0f;

        // The two equalisers' mid-band coefficients.
        float inputEqMidCoefficient = 0.0f;
        float outputEqMidCoefficient = 0.0f;

        // The glue pair: their operating points, their coefficients and their makeup.
        float compensationCoefficient = 0.0f;
        GlueCompressor::Coefficients inputCompressorCoefficients {};
        float inputCompressorRatio = 0.0f;
        float inputKneeDb = 0.0f;
        float inputMakeupFraction = 0.0f;
        float inputReductionLimitDb = 0.0f;
        float inputThresholdDb = 0.0f;
        GlueCompressor::Coefficients outputCompressorCoefficients {};
        float outputCompressorRatio = 0.0f;
        float outputKneeDb = 0.0f;
        float outputMakeupFraction = 0.0f;
        float outputReductionLimitDb = 0.0f;
        float outputThresholdDb = 0.0f;

        // The meters' own coefficients.
        float inputLoudnessCoefficient = 0.0f;
        float outputLoudnessCoefficient = 0.0f;
        float vuBallisticCoefficient = 0.0f;

        // What the loop carries from sample to sample, and what the telemetry tail reads
        // back after it: the only fields the machine writes.
        bool clippingThisBlock = false;
        float currentInputLufs = -70.0f;
        float currentLufs = -70.0f;
        float driftAccumulator = 0.0f;
        std::array<float, 2> inputChainHistory {};
        bool inputClippingThisBlock = false;
        float inputEnvelopeActivity = 0.0f;
        float inputPeak = 0.0f;
        float inputPeakReductionDb = 0.0f;
        double inputSquares = 0.0;
        float outputPeak = 0.0f;
        double outputSquares = 0.0;
        float peakReductionDb = 0.0f;
        float referenceBlockPower = 0.0f;
    };

    /** Shared values produced by the input stage for one audio frame. */
    struct SampleInputContext
    {
        std::array<float, 2> trimmedByChannel {};
        float detectorPower = 0.0f;
    };

    /** The machine: one sample of the block, with everything the block was set up with.
        It was the 1,669-line lambda inside processTapeEngine() - see TapeBlockState. */
    void processOneSample (TapeBlockState& state, int sample) noexcept;
    SampleInputContext prepareSampleInput (const TapeBlockState& state, int sample,
                                           float inputGain) noexcept;
    void updateSampleLoudnessMeters (TapeBlockState& state, int sample) noexcept;

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
    //  for. Files live in <user app data>/MiBoX/Presets with a .miboxpreset
    //  extension, so they survive plugin updates and are shared by
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
    //  OSC, from the editor.
    //
    //  The deck's ring picks a note by where its ball lands. These are the two
    //  entry points that landing uses, and both run on the MESSAGE thread: one
    //  hands the note to the audio thread, the other puts it on the wire.
    //==============================================================================

    /** Publishes a new note for the oscillator: `midiNote` is an absolute MIDI
        note number, 0..127 (60 is C4). The ring no longer counts semitones inside
        one octave - it lays out the degrees of the selected TONIC and MODE across
        three octaves (see Source/GUI/OscRing.h), so the note it hands over is the
        note itself rather than a position in a twelve-slot wheel that would stop
        meaning anything above the first octave.

        The audio thread reads it as an atomic; `retrigger` is true when the ball
        has just LANDED, which restarts the note's envelope so each landing is
        heard as its own note rather than as a pitch bend on the previous one.

        Realtime-safe: two relaxed stores, no lock and no allocation. It is called
        from the ring's landing callback, which runs on the message thread, and
        never from the audio thread. */
    void setOscNote (int midiNote, bool retrigger) noexcept;

    /** Tells the module whether the panel that draws the ring is on screen.

        The ring's clock is the editor's timer, so with the window closed nothing
        lands and the note the ball was last on would keep sounding - one frozen
        note, the report that came back from a session. The generator therefore
        stops when the panel goes away (the gate fades it over the same 15 ms a
        switch uses) and starts again, on the ball's parked note, when the window
        comes back. Message thread only: the constructor and destructor of the
        editor are the two callers. */
    void setOscPanelOpen (bool isOpen) noexcept
    {
        oscPanelOpen.store (isOpen, std::memory_order_relaxed);
    }

    /** True while the OSC module is switched on. The panel reads this every frame
        to decide whether the ring is hopping, and the engine reads the parameter
        itself - this is the panel's view, not the machine's. */
    bool getOscEnabled() const noexcept
    {
        return oscOnParam != nullptr && oscOnParam->load() >= 0.5f;
    }

    /** The key the OSC ring lays its notes out in, 0..11 (C..B). The panel reads
        both this and the mode every frame so the ring follows the AUTOTUNE tab's
        TONIC and MODE selectors without a listener of its own. */
    int getTuningTonic() const noexcept
    {
        return tuningTonicParam != nullptr ? static_cast<int> (tuningTonicParam->load()) : 0;
    }

    /** The scale the OSC ring and the corrector share, 0..5: chromatic, major,
        minor, dorian, pentatonic major, pentatonic minor. */
    int getTuningMode() const noexcept
    {
        return tuningModeParam != nullptr ? static_cast<int> (tuningModeParam->load()) : 0;
    }

    /** Sends the current OSC module state as one OSC 1.0 packet - `/mibox/osc/on`
        or `/mibox/osc/note`, whichever changed - through the codec in Source/Net
        and out over UDP to the loopback address the processor names beside the
        socket. Fire-and-forget: nothing is
        listening here, no socket is ever bound, and a machine with no receiver
        sees no difference. Message thread only, like every other non-realtime
        entry point on this class. */
    void publishOscState (bool noteChanged);

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

    /** Loads a WAV file as the VINYL stage's surface noise - a noise print, in
        the sense a record collector means it: the sound of a real surface,
        replacing the one the stage synthesises. Returns immediately and decodes
        on a background thread; ask isNoiseLoading() for progress and
        getNoiseError() for why a file produced nothing. Returns false only when
        the file does not exist, which is the one failure that is known
        synchronously. */
    bool loadNoiseFile (const juce::File& noiseFile);

    /** Goes back to the synthesised surface. */
    void clearNoiseFile();

    /** True while a print is being decoded. */
    bool isNoiseLoading() const noexcept { return noiseLoader.isLoading(); }

    /** True while a decoded print is playing. */
    bool hasNoisePrint() const noexcept { return noiseLoader.active() != nullptr; }

    /** The loaded file's name, or an empty string. */
    juce::String getNoiseFileName() const { return noiseLoader.loadedFileName(); }

    /** Why the last noise load produced no print, or an empty string. */
    juce::String getNoiseError() const { return noiseLoader.lastError(); }

    /** Frames in the loaded print, and the rate of the file it came from - both
        zero when nothing is loaded. The panel's readout of what is playing. */
    std::size_t getNoisePrintFrames() const noexcept { return noiseLoader.loadedFrames(); }
    double getNoisePrintSourceRate() const noexcept { return noiseLoader.loadedSourceRate(); }

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

    /** The name of the model the NEURAL stage holds, for the panel's readout.

        Stored on the PROCESSOR rather than in the editor because it describes the
        stage, not the window: a second editor on the same instance, or a window
        closed and reopened, must read the same answer. It lives and dies with the
        load - clearNeuralModel() empties it - so it can never name a model the
        stage does not hold. Message thread only, like the loaders. */
    void setLoadedNeuralName (const juce::String& name) { loadedNeuralName = name; }
    const juce::String& getLoadedNeuralName() const noexcept { return loadedNeuralName; }

    /** The same for the IR stage. */
    void setLoadedIrName (const juce::String& name) { loadedIrName = name; }
    const juce::String& getLoadedIrName() const noexcept { return loadedIrName; }

    /** True when a model is INSTALLED and the stage can run it.

        Deliberately not isActive(): a host with the transport stopped may not run
        a block for a while, and the model a picker has just built is in the
        stage's pending slot until the next block adopts it. Reporting the audio
        thread's own answer here made a successful .nam load look like a refusal
        on the panel - "NO MODEL" while the model was already in hand. */
    bool hasNeuralModel() const noexcept { return neuralL.hasModel(); }

    /** Why the last load FAILED, in the few words the panel's readout can put
        after "INVALID". Empty after a successful load and before the first
        attempt. Written and read on the MESSAGE thread - the picker's callback
        is the only caller of the three loaders - so a plain String is enough
        and nothing here is visible to the audio thread. */
    const juce::String& lastLoadError() const noexcept { return lastLoadErrorMessage; }

    /** The full sentence behind lastLoadError(): what the engine itself said was
        wrong with the file. Shown as the readout's tooltip rather than in the
        label, because it is a sentence - "Model config is an unsupported version
        0.4.0." - and the band above the buttons is 17 px tall. Empty when there
        is nothing more to say than the short reason. */
    const juce::String& lastLoadDetail() const noexcept { return lastLoadDetailMessage; }

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

        /** The shaper's own output as a CURVE rather than a ratio: one entry per
            band, in dBFS, over the ladder SpectrumAnalyser::edgesHz describes.

            It is an array in the frame rather than one field per band for the
            reason the frame exists at all: the twenty-four readings belong to the
            same block of audio, and twenty-four separate atomics would be
            twenty-four chances to read the curve half updated. The floor value
            means "nothing there", so a build that carries no frames (no
            readerwriterqueue) shows a flat curve rather than no curve. */
        std::array<float, SpectrumAnalyser::bandCount> spectrumBandsDb {};
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

    /** Rebuilds rate-dependent block coefficients from the current engine rate. */
    void configureBlockRateCoefficients (float engineSampleRate, float cabinetAmount);

    /** (Re)creates the oversampling engine for the requested factor and reports latency. */
    void setOversamplingFactor (OversamplingFactor factor, int samplesPerBlock);

    /** Message-thread callback for realtime-safe latency changes. */
    void handleAsyncUpdate() override;

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

    // -----------------------------------------------------------------------
    //  OSC, the note generator on the deck.
    //
    //  The three fields below are the module's whole state and they live in two
    //  threads with a deliberate split: the two atomics are written by the editor's
    //  timer (the message thread) and read once per block or once per landing by
    //  the audio thread; the three floats are owned by the audio thread alone and
    //  are never touched from outside it. `oscPulse` is a COUNTER rather than a
    //  bool on purpose - a bool can be missed if a landing arrives while the last
    //  one is still being read, and a counter only has to be compared with what
    //  was seen before.
    // -----------------------------------------------------------------------
    std::atomic<float>* oscOnParam = nullptr;
    std::atomic<int> oscNoteMidi { 60 };   // absolute MIDI note, 60 = C4
    std::atomic<int> oscPulse { 0 };       // bumped on every landing
    // True while an editor is on screen. The ring's hops are the editor's timer,
    // so without a window there is nothing to land and the last note would sit
    // there droning. The generator is gated on this as well as on the switch.
    std::atomic<bool> oscPanelOpen { false };
    float oscPhase = 0.0f;                 // 0..1, the oscillator's own phase
    float oscEnvelope = 0.0f;              // per-landing decay, audio thread only
    float oscGate = 0.0f;                  // click-free fade to the switch's state
    int oscSeenPulse = 0;                  // the last landing this block acted on

    // The module's own wire: a datagram socket that is never bound (so it can never
    // be a listener and never takes a port from anyone) and is written to only from
    // the message thread, plus the address map those writes are built from. See
    // Source/Net/OscSurface.h for what the map is for and why it is float-only.
    juce::DatagramSocket oscSocket { false };
    std::unique_ptr<j37::osc::Surface> oscSurface;
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

    // Latency reported to the host, in two parts. The oversampler's is fixed for a
    // factor; the corrector's is zero while AUTOTUNE is off and one block of
    // lookahead while it is on, because the phase vocoder is handed whole blocks.
    // The two are published together by publishReportedLatency(), so the host is
    // only ever told a number that matches what the audio path is actually doing.
    int oversamplerLatencySamples = 0;
    int publishedCorrectorLatencySamples = 0;
    std::atomic<int> requestedLatencySamples { 0 };
    void publishReportedLatency();


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
    // The corrector's audio path. Two engines live behind it - see
    // Source/DSP/CorrectorStage.h - and which one this build has is decided by
    // OptionalLibraries.h, not here. It is prepared at the ENGINE's rate, since
    // that is the rate the stage is handed frames at.
    CorrectorStage corrector;
    // One YIN tracker per channel: the estimate a corrector needs is the pitch of
    // the note in THIS channel, and a stereo mix's two notes are not the same
    // note. The tracker is only fed while the stage is switched on - see
    // processAutoTuneSample, which is the only caller.
    std::array<PitchTracker, 2> autoTuneTrackers;
    std::array<float, 2> autoTuneCorrectionSmoothed { 1.0f, 1.0f };
    std::array<float, 2> autoTuneRatiosForFrame { 1.0f, 1.0f };
    std::array<int, 2> autoTunePeriodHintsFor { 0, 0 };
    juce::AudioBuffer<float> postFxScratch;

    // Measures the harmonics the tape shaper is actually producing, separating even from
    // odd. This is the observable signature of the analogue character and drives the
    // HARMONICS display.
    HarmonicAnalyser harmonicAnalyser;

    // Measures the same shaper output as a log-spaced spectrum, for the curve the
    // MODELS page draws. Prepared from the ENGINE's rate (see
    // updateNeuralSampleRates), so its band ladder follows the oversampling
    // switch, and fed one sample at a time from the channel-0 tap the harmonic
    // analyser is fed from - the transform only runs once per 512 frames.
    SpectrumAnalyser spectrumAnalyser;

    // Output safety limiter, the stage that keeps the signal below the soft clipper so it
    // almost never has to act. It is Source/DSP/SafetyLimiter.h - one peak detector, one
    // smoothed gain and one ceiling - and tests/limiter measures it on generated signal.
    // Keeping the detector and the gain separate is what gives the classic brick-wall
    // shape: instant catch, musical release.
    j37dsp::SafetyLimiter limiter;

    // Final gain compensation. The compensator holds the slow, programme-level
    // correction applied after the last compressor, driven by comparing the
    // reference power taken straight after the input trim (before the first
    // compressor) against the power the chain actually produced. It lives in
    // Source/DSP/AutoGainCompensator.h so tests/gain can measure it on signal;
    // its state is one float, so this costs the block state nothing.
    j37dsp::AutoGainCompensator autoGainCompensator;

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

    //  The rotation itself, per band. It holds both channels' Linkwitz-Riley
    //  crossovers and the six band envelopes, so it is prepared with the engine
    //  rate (see prepareToPlay) and reset with the rest of the per-rate state.
    //  Source/DSP/MultibandPhaseGuard.h has the rule; tests/phase measures it.
    j37dsp::MultibandPhaseGuard antiPhaseGuard;

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
    //  The SAG pair was rebuilt inside the per-sample loop, which cost two std::exp
    //  per sample per channel for values that cannot change within a block - both
    //  are functions of the sample rate alone. They are built next to the other
    //  block-rate coefficients in processTapeEngine and read as plain floats in the
    //  loop. The safety limiter's four moved with the stage: its header holds them
    //  now, and its own comment says why the catch/release asymmetry matters.
    // -----------------------------------------------------------------------
    float sagAttackCoefficient = 0.0f;
    float sagReleaseCoefficient = 0.0f;

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
    //  machine, so it is a single stereo processor rather than two mono ones,
    //  and it takes the frame rather than a channel: Dattorro's network is fed
    //  one signal and its width comes from the taps.
    // -----------------------------------------------------------------------
    DattorroRoom reverb;
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
    //  The user's noise print: the WAV file behind Source/DSP/NoiseLoader.h,
    //  decoded on its own thread and read by the two stages above through one
    //  atomic pointer per block. Nothing here is read or written on the audio
    //  thread except that pointer load.
    // -----------------------------------------------------------------------
    j37dsp::NoiseLoader noiseLoader;

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

    //  The engine's own explanation of that reason, for the readout's tooltip.
    juce::String lastLoadDetailMessage;

    //  The names the readouts print, owned here rather than by the editor - see
    //  the accessors above.
    juce::String loadedNeuralName;
    juce::String loadedIrName;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FirstAudioProcessor)
};
