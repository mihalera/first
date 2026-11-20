/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

// The two pieces of the processor that are no longer in this file and that this
// one still needs to see: the saved-state marker its own save/load stamps and
// migrates, and the factory preset entry points the constructor's debug audit
// calls. Both say in their own banners why they are headers.
#include "ProcessorStateFormat.h"
#include "ProcessorPresets.h"

// The five inline shapers the preparation code below builds its coefficients with.
// They are called from the signal path as well, which is why they are a header
// rather than a file-local block in either - see its own banner.
#include "DSP/HotPathMath.h"

// The ring's register, shared with the editor's widget: the note the panel shows
// and the note the engine plays must come from one table, and they do even while
// the window is closed and only the engine is left. The physics itself is
// Source/OscBall.h, included by the processor header.
#include "ScaleDegrees.h"

#if JUCE_ANDROID
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

// -----------------------------------------------------------------------------
//  OSC, STAGED: compiled here and STARTED NOWHERE.
//
//  Source/Net/Osc.h is the wire format and Source/Net/OscSurface.h is the address
//  map onto the parameter tree. Neither is constructed by this file: there is no
//  socket, no receiver thread, no port, and nothing in the plugin's state - so the
//  include below changes no behaviour at all. It is here because the alternative is
//  code that no build compiles until the day it is switched on, and that is the
//  shape of defect this repository's harnesses exist to prevent.
//
//  The day it IS switched on, what it needs is: a juce::DatagramSocket on its own
//  thread (never the audio thread), a Surface built from the parameter tree's ids,
//  ranges and groups, and a listener for the port the UI would show. tests/osc
//  already pins the half below the socket.
// -----------------------------------------------------------------------------
#include "Net/OscSurface.h"

// The switch, the trace and the shutdown watchdog. Everything they do is behind
// one check of one flag, so a build nobody turned the switch on for behaves and
// costs what it did before - the flag is read once, and every call site is a
// no-op after that. See docs/diagnostics.md for where the files land.
#include "Diagnostics/Platform.h"
#include "Diagnostics/ShutdownWatchdog.h"

#include <array>
#include <cstdint>
#include <limits>
#include <set>
#include <vector>

namespace
{
    // The one hand-written id-to-member table: the single place a parameter id and
    // the raw-pointer member that caches it meet. The dirty-badge id list below is
    // DERIVED from it, so the badge list and the cached pointers can never drift
    // apart the way two parallel lists could.
    struct ParameterCacheEntry { const char* id; std::atomic<float>* FirstAudioProcessor::* member; };

    static const ParameterCacheEntry parameterCacheEntries[];

    // Every parameter whose change should light the editor's "edited" badge. The
    // list is DERIVED from the parameter cache table (parameterCacheEntries,
    // defined beside cacheRawParameterPointers() further down this file), so the
    // badge list, the cached raw-parameter pointers and the ids the presets
    // mention can never disagree: there is one hand-written list of id-to-member
    // pairs and everything else reads it.
    // The editor's own knobs are listed too: a host automation pass and a user
    // drag arrive here by the same route, and both should mark the preset dirty.
    const std::vector<const char*>& parametersTrackedForDirtyBadge()
    {
        static const auto ids = []
        {
            std::vector<const char*> built;
            built.reserve (sizeof (parameterCacheEntries) / sizeof (parameterCacheEntries[0]));
            for (const auto& entry : parameterCacheEntries)
                built.push_back (entry.id);
            return built;
        }();

        return ids;
    }

    /**
        Subharmonic generator - the descendant of the fundamental.

        Everything else in this plugin produces OVERtones: harmonics at integer multiples
        of the input frequency. A 100 Hz tone gets 200, 300, 400 Hz and so on. This stage
        is the opposite - it produces the component at HALF the input frequency, so a
        100 Hz note also gains weight at 50 Hz.

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
        of the transfer curve. The implementation below is a bi-stable follower - a
        phase-locked relaxation oscillator - which is how a domain-wall group behaves: it
        holds one state through a half cycle, then flips under sufficient drive, giving an
        output that completes one cycle for every TWO input cycles and is therefore an
        octave below the note.

        Phase-locking is the point. A free-running oscillator would drone at a fixed pitch
        under everything, which is an artefact rather than a tape; this only flips when the
        input actually drives it, so it follows what is playing and stays silent when
        nothing is.
    */

    // -------------------------------------------------------------------------
    //  OSC, the note generator on the deck: where its packets go, and which of
    //  the map's slots says what.
    //
    //  LOOPBACK, and that is a decision rather than a default. The module
    //  PUBLISHES; it does not listen, so nothing here ever binds a port, opens a
    //  thread or takes a socket another piece of software could hold - the day
    //  the panel is driven from outside is the day Source/Net's receive half
    //  lands, and that is when a port number becomes a setting the user owns.
    //  Until then 127.0.0.1:9000 is the address an OSC client is pointed at by
    //  convention, and a machine with nothing listening sees no difference at
    //  all: UDP to a closed port fails quietly and costs one small write.
    //
    //  The slots are named because the publish path is written in terms of them:
    //  the map has two entries, built once in the constructor, and an index that
    //  had to be remembered as "the second one" is exactly the kind of thing that
    //  silently swaps a note for a switch one refactor later.
    // -------------------------------------------------------------------------
    constexpr int oscSendPort = 9000;
    constexpr std::size_t oscSlotOn = 0;
    constexpr std::size_t oscSlotNote = 1;

}


//==============================================================================
FirstAudioProcessor::FirstAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
       parameters (*this, nullptr, "TAPE_NONLIN", createParameterLayout())
#endif
{
    startTimer (50);

    // Every cached raw-parameter pointer, filled by one loop through a static
    // id-to-member table (see cacheRawParameterPointers below) instead of ~130
    // hand-written getRawParameterValue calls. Each miss is reported in debug
    // builds; every member stays nullptr-initialised so a miss is a visible
    // meter read of zero, never a dereference of a bad pointer.
    cacheRawParameterPointers();

    spindownHeld.store (spindownParam != nullptr && spindownParam->load() >= 0.5f,
                        std::memory_order_relaxed);

    // The module's address map, built once here from the two things it publishes.
    // It is a Surface rather than two string literals because the address is the
    // part of OSC that has to be right: the map is what a client would be told to
    // send to, and building it here means the on-wire address and the documented
    // one are the same expression. The full parameter map arrives with the receive
    // half - see the banner at the top of this file for what that still needs.
    oscSurface = std::make_unique<j37::osc::Surface> (
        std::vector<j37::osc::ParameterSlot> {
            { "osc_on",   "osc/on",   0.0f, 1.0f },
            // The note slot is in MIDI numbers, not in "0..1 over one octave":
            // the ring spans three octaves of the selected scale now, so a
            // client that wants the pitch reads the slot's own range (0..127)
            // and multiplies the normalised float by it.
            { "osc_note", "osc/note", 0.0f, 127.0f },
        },
        "/mibox");

    // Four fixed oversampling engines (off / 2x / 4x / 8x), inserted with
    // make_unique so the array's ownership and its count are type-checked:
    // OwnedArray::add takes a unique_ptr, so a raw `new` cannot quietly become
    // a leak the way an OwnedArray::add(T*) accepted it, and the size the
    // setOversamplingFactor/prepareToPlay loops index by is asserted below
    // against the enum the rest of the engine switches on. Each owns its own
    // filter state, so switching between them is glitch-free even mid-render,
    // and the host is told the latency of whichever one is active.
    oversamplers.add (std::make_unique<juce::dsp::Oversampling<float>> (2)); // dummy, factor 1
    oversamplers.add (std::make_unique<juce::dsp::Oversampling<float>> (2, 1,
                         juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true));
    oversamplers.add (std::make_unique<juce::dsp::Oversampling<float>> (2, 2,
                         juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true));
    oversamplers.add (std::make_unique<juce::dsp::Oversampling<float>> (2, 3,
                         juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true));
    jassert (oversamplers.size() == static_cast<int> (oversamplingFactorCount));

    // The A/B slots start as copies of the default state so that toggling compare
    // before anything is stored recalls the same settings rather than an empty tree.
    compareSlots[0] = parameters.copyState().createCopy();
    compareSlots[1] = compareSlots[0].createCopy();

    // Cache the raw atomics used by the message-thread dirty-badge poll. APVTS
    // listeners may execute under its parameter lock, so host automation must not
    // invoke processor callbacks from the audio thread.
    const auto& dirtyParameterIDs = parametersTrackedForDirtyBadge();
    dirtyBadgeParameters.reserve (dirtyParameterIDs.size());
    presetParameterSnapshot.reserve (dirtyParameterIDs.size());
    for (const auto* parameterID : dirtyParameterIDs)
    {
        if (auto* value = parameters.getRawParameterValue (parameterID))
        {
            dirtyBadgeParameters.push_back (value);
            presetParameterSnapshot.push_back (value->load (std::memory_order_relaxed));
        }
    }

#if DEBUG
    auditFactoryPresets();
#endif
}

// -----------------------------------------------------------------------------
//  cacheRawParameterPointers: one loop over a static id-to-member table.
//
//  Every cached `*Param` pointer in the header used to be assigned by its own
//  hand-written `parameters.getRawParameterValue ("...")` line in the
//  constructor - ~130 of them, each an opportunity to misspell an id and cache
//  a null that only a dead control would ever reveal. The table is the one place
//  the id and the member meet; the loop assigns them all, and a miss
//  is named in debug builds.
// -----------------------------------------------------------------------------
void FirstAudioProcessor::resetTelemetryAtomics()
{
    // Every meter/telemetry reading starts at its idle value, so the editor's
    // first frame shows silence rather than whatever the previous session left
    // behind. All relaxed: the audio thread overwrites them every block.
    inputPeakLevel.store (0.0f, std::memory_order_relaxed);
    inputRmsLevel.store (0.0f, std::memory_order_relaxed);
    inputPeakDb.store (-70.0f, std::memory_order_relaxed);
    inputRmsDb.store (-70.0f, std::memory_order_relaxed);
    inputLufs.store (-70.0f, std::memory_order_relaxed);
    inputVuDb.store (-70.0f, std::memory_order_relaxed);
    inputCombinedDb.store (-70.0f, std::memory_order_relaxed);
    inputClipping.store (false, std::memory_order_relaxed);
    outputPeakLevel.store (0.0f, std::memory_order_relaxed);
    outputRmsLevel.store (0.0f, std::memory_order_relaxed);
    outputPeakDb.store (-70.0f, std::memory_order_relaxed);
    outputRmsDb.store (-70.0f, std::memory_order_relaxed);
    outputLufs.store (-70.0f, std::memory_order_relaxed);
    outputVuDb.store (-70.0f, std::memory_order_relaxed);
    outputCombinedDb.store (-70.0f, std::memory_order_relaxed);
    outputClipping.store (false, std::memory_order_relaxed);
    inputGainReductionDb.store (0.0f, std::memory_order_relaxed);
    outputGainReductionDb.store (0.0f, std::memory_order_relaxed);
    inputCompressorActivity.store (0.0f, std::memory_order_relaxed);
    outputCompressorActivity.store (0.0f, std::memory_order_relaxed);
    compressorActivity.store (0.0f, std::memory_order_relaxed);
    transportDrift.store (0.5f, std::memory_order_relaxed);
    harmonicCharacter.store (0.0f, std::memory_order_relaxed);
    bypassActive.store (false, std::memory_order_relaxed);
}

void FirstAudioProcessor::resetDelayLine (int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);

    delayBuffer.setSize (juce::jmax (1, juce::jmin (2, getTotalNumOutputChannels())),
                         delayBufferLength, false, true, true);
    delayBuffer.clear();
    delayWritePosition = 0;
    delayToneLpState.fill (0.0f);
    delayHpXState.fill (0.0f);
    delayHpYState.fill (0.0f);
    delayFeedbackDampState.fill (0.0f);
    delayToneCurrent = 0.5f;
    delayFeedbackHpRCurrent = 0.99f;

    // The offset buffer is a fixed short window - a few hundred microseconds is far
    // more than the tens the control asks for - so it is a plain array, not an
    // allocation.
    stOffsetBuffer.reset();
}

void FirstAudioProcessor::resetInputChainState()
{
    // The DI's own smoother starts from the value the parameter restores, so a
    // session saved with the box engaged does not spend its first 20 ms sliding
    // into it.
    diSmoothed.setCurrentAndTargetValue (diParam != nullptr ? diParam->load() : 0.0f);
    diLoadSmoothed.setCurrentAndTargetValue (diLoadParam != nullptr ? diLoadParam->load() : 0.0f);
    diTransformerSmoothed.setCurrentAndTargetValue (diTransformerParam != nullptr
                                                        ? diTransformerParam->load() : 0.0f);

    // Reseeded rather than zeroed: zero is a degenerate LCG state, and the two
    // channels get different seeds so their clock noise is uncorrelated - sharing
    // one would put the BBD buzz in the centre of the image.
    bbdNoiseStateL = 0x85ebca6bu;
    bbdNoiseStateR = 0xc2b2ae35u;
    bbdLowL = bbdLowR = 0.0f;
    lofiLowL = lofiLowR = 0.0f;
    lofiHoldL = lofiHoldR = 0.0f;
    loFiHoldClock.reset();
    wearModulation = 0.0f;
}

// The table itself, written out once at file scope so the dirty-badge id list
// above is derived from the SAME rows (see parameterCacheEntries there).
const ParameterCacheEntry parameterCacheEntries[] =
{
        { "input", &FirstAudioProcessor::inputDbParam },
        { "output", &FirstAudioProcessor::outputDbParam },
        { "bypass", &FirstAudioProcessor::bypassParam },
        { "polarity", &FirstAudioProcessor::polarityParam },
        { "auto_gain", &FirstAudioProcessor::autoGainParam },
        { "subfund", &FirstAudioProcessor::subFundamentalParam },
        { "stereo_width", &FirstAudioProcessor::widthParam },
        { "tape_type", &FirstAudioProcessor::tapeTypeParam },
        { "speed", &FirstAudioProcessor::speedParam },
        { "instrument", &FirstAudioProcessor::instrumentParam },
        { "drive", &FirstAudioProcessor::driveParam },
        { "bias", &FirstAudioProcessor::biasParam },
        { "oversampling", &FirstAudioProcessor::oversamplingParam },
        { "tone", &FirstAudioProcessor::toneParam },
        { "wow", &FirstAudioProcessor::wowParam },
        { "flutter", &FirstAudioProcessor::flutterParam },
        { "mix", &FirstAudioProcessor::mixParam },
        { "tracks", &FirstAudioProcessor::tracksParam },
        { "character", &FirstAudioProcessor::characterParam },
        { "delta", &FirstAudioProcessor::deltaParam },
        { "delay_time", &FirstAudioProcessor::delayTimeParam },
        { "delay_feedback", &FirstAudioProcessor::delayFeedbackParam },
        { "delay_pingpong", &FirstAudioProcessor::delayPingPongParam },
        { "delay_tone", &FirstAudioProcessor::delayToneParam },
        { "delay_lowcut", &FirstAudioProcessor::delayLowCutParam },
        { "delay_damp", &FirstAudioProcessor::delayDampParam },
        { "st_offset", &FirstAudioProcessor::stOffsetParam },
        { "noise", &FirstAudioProcessor::noiseParam },
        { "noise_lvl", &FirstAudioProcessor::noiseLvlParam },
        { "transport", &FirstAudioProcessor::transportParam },
        { "spindown", &FirstAudioProcessor::spindownParam },
        { "ui_sounds", &FirstAudioProcessor::uiSoundsParam },
        { "language", &FirstAudioProcessor::languageParam },
        { "anti_phase_guard", &FirstAudioProcessor::antiPhaseGuardParam },
        { "osc_on", &FirstAudioProcessor::oscOnParam },
        { "tape_source", &FirstAudioProcessor::tapeSourceParam },
        { "valve_source", &FirstAudioProcessor::valveSourceParam },
        { "pentode_source", &FirstAudioProcessor::pentodeSourceParam },
        { "triode_source", &FirstAudioProcessor::triodeSourceParam },
        { "transistor_source", &FirstAudioProcessor::transistorSourceParam },
        { "fold_source", &FirstAudioProcessor::foldSourceParam },
        { "glass_source", &FirstAudioProcessor::glassSourceParam },
        { "transformer_source", &FirstAudioProcessor::transformerSourceParam },
        { "digital_source", &FirstAudioProcessor::digitalSourceParam },
        { "sag", &FirstAudioProcessor::sagParam },
        { "presence", &FirstAudioProcessor::presenceParam },
        { "cabinet", &FirstAudioProcessor::cabinetParam },
        { "amp_bias", &FirstAudioProcessor::ampBiasParam },
        { "preamp", &FirstAudioProcessor::preampParam },
        { "flux", &FirstAudioProcessor::fluxParam },
        { "wear", &FirstAudioProcessor::wearParam },
        { "mechanics", &FirstAudioProcessor::mechanicsParam },
        { "reverb", &FirstAudioProcessor::reverbParam },
        { "reverb_size", &FirstAudioProcessor::reverbSizeParam },
        { "reverb_predelay", &FirstAudioProcessor::reverbPreDelayParam },
        { "reverb_damp", &FirstAudioProcessor::reverbDampParam },
        { "reverb_width", &FirstAudioProcessor::reverbWidthParam },
        { "reverb_lowcut", &FirstAudioProcessor::reverbLowCutParam },
        { "di", &FirstAudioProcessor::diParam },
        { "di_load", &FirstAudioProcessor::diLoadParam },
        { "di_transformer", &FirstAudioProcessor::diTransformerParam },
        { "di_pad", &FirstAudioProcessor::diPadParam },
        { "delay_type", &FirstAudioProcessor::delayTypeParam },
        { "distortion", &FirstAudioProcessor::distortionParam },
        { "modern_mode", &FirstAudioProcessor::modernModeParam },
        { "lofi_mode", &FirstAudioProcessor::lofiModeParam },
        { "vinyl", &FirstAudioProcessor::vinylParam },
        { "vinyl_crackle", &FirstAudioProcessor::vinylCrackleParam },
        { "vinyl_rumble", &FirstAudioProcessor::vinylRumbleParam },
        { "vinyl_speed", &FirstAudioProcessor::vinylSpeedParam },
        { "vinyl_dust", &FirstAudioProcessor::vinylDustParam },
        { "vinyl_scratch", &FirstAudioProcessor::vinylScratchParam },
        { "vinyl_warp", &FirstAudioProcessor::vinylWarpParam },
        { "vinyl_electrical", &FirstAudioProcessor::vinylElectricalParam },
        { "vinyl_clicks", &FirstAudioProcessor::vinylClicksParam },
        { "vinyl_generation", &FirstAudioProcessor::vinylGenerationParam },
        { "vinyl_turntable", &FirstAudioProcessor::vinylTurntableParam },
        { "vinyl_cartridge", &FirstAudioProcessor::vinylCartridgeParam },
        { "in_low", &FirstAudioProcessor::inputEqLowParam },
        { "in_mid", &FirstAudioProcessor::inputEqMidParam },
        { "in_high", &FirstAudioProcessor::inputEqHighParam },
        { "in_hp_freq", &FirstAudioProcessor::inputEqHpFreqParam },
        { "in_lp_freq", &FirstAudioProcessor::inputEqLpFreqParam },
        { "in_eq_order", &FirstAudioProcessor::inputEqOrderParam },
        { "in_eq_q", &FirstAudioProcessor::inputEqQParam },
        { "in_hp_on", &FirstAudioProcessor::inputEqHpOnParam },
        { "in_lp_on", &FirstAudioProcessor::inputEqLpOnParam },
        { "out_low", &FirstAudioProcessor::outputEqLowParam },
        { "out_mid", &FirstAudioProcessor::outputEqMidParam },
        { "out_high", &FirstAudioProcessor::outputEqHighParam },
        { "out_hp_freq", &FirstAudioProcessor::outputEqHpFreqParam },
        { "out_lp_freq", &FirstAudioProcessor::outputEqLpFreqParam },
        { "out_eq_order", &FirstAudioProcessor::outputEqOrderParam },
        { "out_eq_q", &FirstAudioProcessor::outputEqQParam },
        { "out_hp_on", &FirstAudioProcessor::outputEqHpOnParam },
        { "out_lp_on", &FirstAudioProcessor::outputEqLpOnParam },
        { "st_link", &FirstAudioProcessor::stLinkParam },
        { "valve_type", &FirstAudioProcessor::valveTypeParam },
        { "pentode_type", &FirstAudioProcessor::pentodeTypeParam },
        { "triode_type", &FirstAudioProcessor::triodeTypeParam },
        { "transistor_type", &FirstAudioProcessor::transistorTypeParam },
        { "transformer_type", &FirstAudioProcessor::transformerTypeParam },
        { "digital_type", &FirstAudioProcessor::digitalTypeParam },
        { "vinyl_type", &FirstAudioProcessor::vinylTypeParam },
        { "delay_sync", &FirstAudioProcessor::delaySyncParam },
        { "delay_rate", &FirstAudioProcessor::delayRateParam },
        { "tuning_tonic", &FirstAudioProcessor::tuningTonicParam },
        { "tuning_mode", &FirstAudioProcessor::tuningModeParam },
        { "autotune_amount", &FirstAudioProcessor::autoTuneAmountParam },
        { "autotune_speed", &FirstAudioProcessor::autoTuneSpeedParam },
        { "autotune_range", &FirstAudioProcessor::autoTuneRangeParam },
        { "autotune_octave", &FirstAudioProcessor::autoTuneOctaveParam },
        { "autotune_tolerance", &FirstAudioProcessor::autoTuneToleranceParam },
        { "autotune_gate", &FirstAudioProcessor::autoTuneGateParam },
        { "comp_threshold", &FirstAudioProcessor::compressorThresholdParam },
        { "comp_ratio", &FirstAudioProcessor::compressorRatioParam },
        { "comp_attack", &FirstAudioProcessor::compressorAttackParam },
        { "comp_release", &FirstAudioProcessor::compressorReleaseParam },
        { "comp_makeup", &FirstAudioProcessor::compressorMakeupParam },
        { "comp_mix", &FirstAudioProcessor::compressorMixParam },
        { "transient_attack", &FirstAudioProcessor::transientAttackParam },
        { "transient_sustain", &FirstAudioProcessor::transientSustainParam },
        { "transient_mix", &FirstAudioProcessor::transientMixParam },
        { "neural_mix", &FirstAudioProcessor::neuralMixParam },
        { "ir_mix", &FirstAudioProcessor::irMixParam },
        { "glue_on", &FirstAudioProcessor::glueOnParam },
        { "fx_wah", &FirstAudioProcessor::fxWahParam },
        { "fx_anti_spike", &FirstAudioProcessor::fxAntiSpikeParam },
        { "fx_gate", &FirstAudioProcessor::fxGateParam },
        { "fx_overdrive", &FirstAudioProcessor::fxOverdriveParam },
        { "fx_fuzz", &FirstAudioProcessor::fxFuzzParam },
        { "fx_wavefolder", &FirstAudioProcessor::fxWavefolderParam },
        { "fx_bitcrusher", &FirstAudioProcessor::fxBitcrusherParam },
        { "fx_rate_crusher", &FirstAudioProcessor::fxRateCrusherParam },
        { "fx_ring_mod", &FirstAudioProcessor::fxRingModParam },
        { "fx_octaver", &FirstAudioProcessor::fxOctaverParam },
        { "fx_pitch", &FirstAudioProcessor::fxPitchParam },
        { "fx_comb", &FirstAudioProcessor::fxCombParam },
        { "fx_formant", &FirstAudioProcessor::fxFormantParam },
        { "fx_dynamic_eq", &FirstAudioProcessor::fxDynamicEqParam },
        { "fx_de_esser", &FirstAudioProcessor::fxDeEsserParam },
        { "fx_chorus", &FirstAudioProcessor::fxChorusParam },
        { "fx_flanger", &FirstAudioProcessor::fxFlangerParam },
        { "fx_phaser", &FirstAudioProcessor::fxPhaserParam },
        { "fx_tremolo", &FirstAudioProcessor::fxTremoloParam },
        { "fx_vibrato", &FirstAudioProcessor::fxVibratoParam },
        { "fx_rotary", &FirstAudioProcessor::fxRotaryParam },
        { "fx_vocoder", &FirstAudioProcessor::fxVocoderParam },
        { "fx_granular", &FirstAudioProcessor::fxGranularParam },
        { "fx_spectral", &FirstAudioProcessor::fxSpectralParam },
        { "fx_stutter", &FirstAudioProcessor::fxStutterParam },
        { "fx_speed", &FirstAudioProcessor::fxSpeedParam },
        { "comp_upward", &FirstAudioProcessor::compUpwardParam },
    };

void FirstAudioProcessor::cacheRawParameterPointers()
{
    for (const auto& entry : parameterCacheEntries)
    {
        auto* value = parameters.getRawParameterValue (entry.id);
        this->*entry.member = value;

        jassert (value != nullptr); // cache table names a parameter that is not registered
    }
}

// ----------------------------------------------------------------------------
//  The factory-preset audit, SHARED between the DEBUG constructor call and the
//  tests/presets/preset_audit.cpp harness (which links the shipping plugin and
//  runs in every CI build, not only debug ones).
//
//  The factory presets went wrong twice, in two different ways, and both
//  produced the same user-visible report ("the presets do not work") with
//  nothing in a release build to say why:
//
//    1. THE NAMES AND THE TABLE DISAGREED. getPresetNames() returned twenty
//       names for a twenty-six row table, so six rows had no name and - because
//       the box, the undo label and the badge all index by POSITION - every
//       name below the gap was attached to the wrong sound. Nothing crashed;
//       the list simply lied.
//
//    2. THE TABLE AND THE PARAMETERS DISAGREED. Fifteen sound-bearing
//       parameters had no column, so they kept the session's value and every
//       preset loaded "mostly".
//
//  Both are compile-time-shaped mistakes - a list length, a set of names, a
//  set of ids - and both are invisible at runtime by construction. So they are
//  checked from the three sources themselves rather than from a count written
//  down beside them: the names, the table, and the parameters the processor
//  actually registered. A fourth source of the same class of bug - a map key
//  naming a parameter that does not exist - is checked too, as the keys are
//  collected.
//
//  This instance-free variant costs O(presets x parameters) string-set work,
//  which is nothing for a test harness and is skipped entirely in a shipping
//  build (the DEBUG constructor call compiles to the same checks via jassert).
// ----------------------------------------------------------------------------
bool FirstAudioProcessor::verifyFactoryPresetStructure (juce::String& firstProblem)
{
    auto fail = [&firstProblem] (const juce::String& what)
    {
        if (firstProblem.isEmpty())
            firstProblem = what;
        return false;
    };

    const auto names = getPresetNames();

    if (names.size() != FactoryPresets::count())
        return fail ("preset name count " + juce::String (names.size())
                     + " disagrees with the table's " + juce::String (FactoryPresets::count()));

    // A name that is empty, or the same as another, is the same class of bug
    // as a name list that is the wrong length: the box would show two blank
    // entries and the user would have no way to tell which is which.
    for (int i = 0; i < names.size(); ++i)
    {
        if (names[i].isEmpty())
            return fail ("preset " + juce::String (i) + " has an empty name");

        for (int j = i + 1; j < names.size(); ++j)
            if (names[i] == names[j])
                return fail ("presets " + juce::String (i) + " and " + juce::String (j)
                             + " are both called \"" + names[i] + "\"");
    }

    // The REAL parameter set, from the table the plugin registered with the
    // host - the same reasoning the DEBUG audit's comment records above: the
    // layout object cannot be enumerated, so getParameters() is the source.
    std::set<juce::String> registered;
    for (const auto* parameter : getParameters())
        if (const auto* ranged = dynamic_cast<const juce::RangedAudioParameter*> (parameter))
            registered.insert (ranged->paramID);

    if (registered.empty())
        return fail ("no parameters are registered - every check below would pass for the wrong reason");

    // Every id any preset mentions, checked against the real set as it is
    // collected: a key naming a parameter that does not exist is dropped
    // SILENTLY by applyFactoryPreset()'s getParameter() guard, so the preset
    // loads with that one control left wherever the session had it.
    std::set<juce::String> covered;
    for (int i = 0; i < FactoryPresets::count(); ++i)
        for (const auto& entry : factoryPresetValues (i))
        {
            if (registered.count (entry.first) == 0)
                return fail ("preset " + juce::String (i) + " names \"" + entry.first
                             + "\", which is not a registered parameter");

            if (FactoryPresets::isSentinelValue (entry.second))
                return fail ("preset " + juce::String (i) + " holds a sentinel for \""
                             + entry.first + "\"");

            covered.insert (entry.first);
        }

    // The parameters a preset deliberately does NOT state. The exclusions and
    // their per-id justifications live in auditFactoryPresets (DEBUG) so the
    // reasoning stays beside the checks that consume them; only the SET is
    // repeated here, and the DEBUG audit's own jasserts keep the two honest.
    static const std::set<juce::String> intentionallyNotPresettable {
        "bypass", "delta", "ui_sounds", "language", "anti_phase_guard",
        "transport", "spindown",
        "transient_attack", "transient_sustain", "transient_mix", "neural_mix",
        "ir_mix", "osc_on", "osc_note", "glue_on",
    };

    for (const auto& id : registered)
        if (covered.count (id) == 0 && intentionallyNotPresettable.count (id) == 0)
            return fail ("parameter \"" + id + "\" is in no preset and is not excluded");

    return true;
}

#if DEBUG
void FirstAudioProcessor::auditFactoryPresets()
{
    // The DEBUG constructor keeps the structural audit above PLUS the instance
    // checks a static cannot make: dirty-badge completeness against this tree,
    // and the live apply-and-read-back of every preset. Both halves stay
    // aligned because the structural half is the SAME function the test
    // harness runs - it is not a second copy that can drift.
    juce::String problem;
    jassert (verifyFactoryPresetStructure (problem));

        const auto names = getPresetNames();

        // The REAL parameter set (see verifyFactoryPresetStructure above).
        std::set<juce::String> registered;
        for (const auto* parameter : getParameters())
            if (const auto* ranged = dynamic_cast<const juce::RangedAudioParameter*> (parameter))
                registered.insert (ranged->paramID);

        // Keep the dirty-badge cache complete as well as valid: every registered
        // parameter must be tracked, and every tracked id must resolve in the host
        // parameter set.
        std::set<juce::String> dirtyTracked;
        for (const auto* id : parametersTrackedForDirtyBadge())
        {
            dirtyTracked.insert (id);
            jassert (registered.count (id) != 0);
        }
        jassert (dirtyTracked.size() == parametersTrackedForDirtyBadge().size());
        for (const auto& id : registered)
            jassert (dirtyTracked.count (id) != 0);

        // THE LOAD ITSELF, exercised: apply each preset and re-read what landed in
        // the tree, then put back the untouched "no preset loaded" state.
        {
            const auto idProperty = juce::Identifier ("id");
            const auto valueProperty = juce::Identifier ("value");
            auto readBack = [&] (const juce::String& key) -> float
            {
                const auto state = parameters.copyState();

                for (int i = 0; i < state.getNumChildren(); ++i)
                {
                    const auto child = state.getChild (i);
                    if (child.hasProperty (idProperty)
                        && child.getProperty (idProperty).toString() == key)
                        return static_cast<float> (child.getProperty (valueProperty));
                }

                return std::numeric_limits<float>::quiet_NaN();
            };

            for (int i = 0; i < FactoryPresets::count(); ++i)
            {
                applyFactoryPreset (i);

                for (const auto& entry : factoryPresetValues (i))
                {
                    const auto landed = readBack (entry.first);

                    if (! juce::approximatelyEqual (landed, entry.second))
                        DBG ("[presets] preset " << i << " key " << entry.first
                             << ": wanted " << entry.second << ", tree holds " << landed);
                }
            }

            lastPresetIndex.store (-1, std::memory_order_relaxed);
            markPresetClean ({});
            undoManager.clearUndoHistory();
        }
}
#endif
FirstAudioProcessor::~FirstAudioProcessor()
{
    // The second half of the teardown window: a host that closes a project
    // destroys the editor and then this object, and either one can be where it
    // stops. The watchdog is shared, so one already armed by the editor is not
    // restarted here - it just keeps its clock.
    auto& guard = j37diag::shutdownWatchdog();

    if (j37diag::enabled())
        j37diag::recordThread (j37diag::ThreadSlot::message);

    guard.arm();
    guard.phase ("processor dtor: stop the timer");
    stopTimer();

    // What the callback did while it ran. Written here, once, on the message
    // thread, from counters the audio thread only ever incremented: a session
    // whose sound was wrong can now say whether the engine was overrunning its
    // own blocks or handing the host non-finite samples, and neither answer was
    // available anywhere before.
    if (j37diag::enabled())
    {
        const auto audio = j37diag::audioReportText();

        if (! audio.empty())
            j37diag::note (audio);
    }

    guard.phase ("processor dtor: done");
    guard.disarm();
}

//==============================================================================
//  OSC: the ring's note, and what goes on the wire with it.
//
//  Both are MESSAGE-thread calls - the editor's timer is the only caller - and
//  neither can touch the audio thread with anything but an atomic. That is the
//  whole reason these two are here rather than in the editor: the note is the
//  engine's state and the packet is the module's, so both belong with the object
//  that outlives the window that asked for them.
//
//  The engine steps the same ball itself when no editor exists (advanceOscBall in
//  ProcessorSignalPath.cpp), which is why the sector is kept here beside the
//  note: it is the handoff between the two clocks, and it has to survive the
//  window that wrote it.
//==============================================================================
void FirstAudioProcessor::setOscNote (int midiNote, bool retrigger, float impactGain) noexcept
{
    const auto note = juce::jlimit (0, 127, midiNote);
    oscNoteMidi.store (note, std::memory_order_relaxed);

    // The wall hit's speed becomes the note's level here rather than in the audio
    // callback: the mapping is OscBall's, so the editor and the engine cannot use
    // two different curves for the same impact.
    oscImpactGain.store (juce::jlimit (0.05f, 2.0f, impactGain), std::memory_order_relaxed);

    // Keep the ball's place with the note. While the panel is open this is how a
    // hit becomes the engine's starting sector when the window closes; a note that
    // is not in the current key's register leaves the sector alone.
    if (const auto sector = j37::scale::ringSectorOfNote (getTuningTonic(), getTuningMode(), note);
        sector >= 0)
        oscSector.store (sector, std::memory_order_relaxed);

    // A counter, not a flag: the audio thread compares it with what it saw at the
    // last block, so two hits inside one block both count and neither can be lost
    // to a read that happens between the write and the flag's reset.
    if (retrigger)
        oscPulse.fetch_add (1, std::memory_order_relaxed);
}

void FirstAudioProcessor::publishOscState (bool noteChanged)
{
    if (oscSurface == nullptr || oscOnParam == nullptr)
        return;

    const auto on = oscOnParam->load() >= 0.5f;

    // A wall hit only matters while the module is running - with the switch off the
    // ball freezes and nothing is generated, so publishing a note would be a message
    // about a sound nobody is making. The switch itself is the exception in the
    // other direction: the OFF packet is the one a client most needs to receive, so
    // it is sent whatever the state is.
    if (noteChanged && ! on)
        return;

    const auto slot = noteChanged ? oscSlotNote : oscSlotOn;
    const auto normalised = noteChanged
                              ? static_cast<float> (oscNoteMidi.load (std::memory_order_relaxed)) / 127.0f
                              : (on ? 1.0f : 0.0f);

    const auto message = oscSurface->feedback (slot, normalised);

    // feedback() returns an empty message for a slot it does not hold or a value
    // it cannot state. Sending nothing is the right answer to both, and an empty
    // address here would encode as a packet no client could route.
    if (message.address.empty())
        return;

    const auto packet = j37::osc::encode (message);

    if (packet.empty())
        return;

    // Fire-and-forget, from a socket that was never bound: write() caches the
    // address lookup after the first call, so the steady state is one sendto().
    juce::ignoreUnused (oscSocket.write ("127.0.0.1", oscSendPort,
                                         packet.data(), static_cast<int> (packet.size())));
}


//==============================================================================
const juce::String FirstAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool FirstAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool FirstAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool FirstAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double FirstAudioProcessor::getTailLengthSeconds() const
{
    // The longest measured reverb T60 is 4.9 s, with up to 100 ms of pre-delay.
    // The tape head can also feed its delayed signal back at unity, so use a
    // conservative finite host-render allowance rather than claiming no tail.
    // Keep this finite: JUCE's AUv2 silence timeout converts the duration to frames.
    return 30.0;
}

int FirstAudioProcessor::getNumPrograms()
{
    return 1;
}

int FirstAudioProcessor::getCurrentProgram()
{
    return 0;
}

void FirstAudioProcessor::setCurrentProgram (int)
{
}

const juce::String FirstAudioProcessor::getProgramName (int)
{
    return {};
}

void FirstAudioProcessor::changeProgramName (int, const juce::String&)
{
}

//==============================================================================
void FirstAudioProcessor::prepareToPlay (double sampleRateToUse, int samplesPerBlock)
{
    sampleRate = static_cast<float> (sampleRateToUse);

    // The OSC voice is tuned at the engine rate. Preparing it here also clears
    // any tail from the previous session, so a host that changes rate does not
    // carry a resonator tuned to the old one into the first block. The pulse
    // counter is marked as seen: nothing that happened before this point is a
    // strike the new session should sound.
    oscVoice.prepare (sampleRateToUse);
    oscGate = 0.0f;
    oscSeenPulse = oscPulse.load (std::memory_order_relaxed);

    // The oversampling engines only depend on rate and block size, so they are
    // (re)built here. The parameter (if restored by a session) picks which one runs.
    const auto requestedOversampling = oversamplingParam != nullptr
        ? static_cast<OversamplingFactor> (static_cast<int> (oversamplingParam->load()))
        : currentOversampling;
    const auto preparedBlockSize = juce::jmax (1, samplesPerBlock);

    // Prepare every oversampler AND every rate-dependent stage once, here, for
    // every factor. This is the allocation point for the whole oversampling
    // switch: processBlock later only selects a slot, so pressing OVERSAMPLING
    // no longer calls CorrectorStage::prepare, IrStage::prepare or
    // RepitchStage::prepare on the audio thread.
    for (int slot = 0; slot < static_cast<int> (oversamplingFactorCount); ++slot)
    {
        const auto factor = 1 << slot; // 1, 2, 4, 8 - the order of `oversamplers`
        const auto engineRate = sampleRateToUse * static_cast<double> (factor);
        const auto engineBlock = preparedBlockSize * factor;

        if (auto* oversampler = oversamplers[slot])
        {
            oversampler->initProcessing (static_cast<size_t> (preparedBlockSize));
            oversampler->reset();
        }

        auto& corrector = correctors[static_cast<std::size_t> (slot)];
        corrector.prepare (engineRate, 2);

        irStages[static_cast<std::size_t> (slot)].prepare (
            { engineRate, static_cast<juce::uint32> (engineBlock), 2 });

        repitchStages[static_cast<std::size_t> (slot)].prepare (engineRate);

        for (auto& tracker : autoTuneTrackers[static_cast<std::size_t> (slot)])
            tracker.prepare (engineRate);
    }

    setOversamplingFactor (requestedOversampling, preparedBlockSize);
    // Stated as a double on purpose: SmoothedValue::reset takes the ramp length in seconds
    // as a double, and `auto` here would have deduced the same type silently. Naming it
    // makes the intent explicit and keeps the literal from looking like a float that lost
    // its suffix.
    const double smoothingSeconds = 0.02;
    inputGainSmoothed.reset (sampleRateToUse, smoothingSeconds);
    inputGainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (
        inputDbParam != nullptr ? inputDbParam->load() : 0.0f));
    outputGainSmoothed.reset (sampleRateToUse, smoothingSeconds);
    outputGainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (
        outputDbParam != nullptr ? outputDbParam->load() : 0.0f));
    mixSmoothed.reset (sampleRateToUse, smoothingSeconds);
    const float initialMix = (mixParam != nullptr ? mixParam->load() : 50.0f) * 0.01f;
    mixSmoothed.setCurrentAndTargetValue (juce::jlimit (0.0f, 1.0f, initialMix));
    widthSmoothed.reset (sampleRateToUse, smoothingSeconds);
    widthSmoothed.setCurrentAndTargetValue (widthParam != nullptr
        ? juce::jlimit (0.0f, 2.0f, widthParam->load() * 2.0f) : 1.0f);
    bypassSmoothed.reset (sampleRateToUse, 0.01);
    // Start from the state the parameter restores: a session saved with BYPASS on
    // must not spend its first 10 ms ramping from the dry position.
    bypassSmoothed.setCurrentAndTargetValue (bypassParam != nullptr && bypassParam->load() >= 0.5f ? 0.0f : 1.0f);
    deltaListenSmoothed.reset (sampleRateToUse, smoothingSeconds);
    // Same reasoning as BYPASS directly above: start from the state the parameter
    // restores rather than ramping into it.
    deltaListenSmoothed.setCurrentAndTargetValue (deltaParam != nullptr && deltaParam->load() >= 0.5f ? 1.0f : 0.0f);

    resetTelemetryAtomics();

    hystL.fill (0.0f);
    hystR.fill (0.0f);
    highFreqL.fill (0.0f);
    highFreqR.fill (0.0f);

    // The hiss band-limit state is per channel and must start empty, or a rate switch would
    // carry a stale filter state into the first block and produce a click.
    hissLowPassL = 0.0f;
    hissLowPassR = 0.0f;

    wowPhaseL = 0.0f;
    wowPhaseR = 0.0f;
    flutterPhaseL = 0.0f;
    flutterPhaseR = 0.0f;
    previousTone = -1.0f;
    previousCharacter = -1.0f;
    toneLpAc = 0.0f;
    toneShelfSplit.fill (0.0f);
    headGapHz = 24000.0f;
    preDriveGain = 1.0f;
    flutterScale = 1.0f;

    autoTuneCorrectionSmoothed.fill (1.0f);
    autoTuneGateOpen.fill (false);
    autoTuneLastPeriodHint.fill (0);
    // AUTOTUNE's two rate-dependent halves - the trackers' decimation and the
    // corrector's analysis window - were prepared for every factor above, where
    // the ENGINE's rate is known. They are fed the engine's frames: the stage runs
    // at the end of processTapeEngine, on the block the oversampler expanded. A
    // prepare from `sampleRate` here would be off by one factor of two - the
    // tracker would read a 440 Hz note as 880 - so this method only starts every
    // slot clean.
    for (auto& corrector : correctors)
        corrector.reset();

    // The sample rate changed, so every time-domain constant has to be rebuilt.
    // The tone filters are cached rather than recomputed per block, and their
    // coefficients depend on the rate, so there is exactly one place that is
    // allowed to own them: this method. Phase accumulators are reset as well,
    // otherwise a rate switch would leave wow/flutter at a stale phase and click.
    resetSampleRateDependentState();

    //  The IR convolvers are NOT prepared here: they run inside the oversampled
    //  engine, and the per-factor prepare loop above already handed every slot its
    //  own rate and block. Preparing them again from the session's numbers here
    //  was the defect - see the call site for the whole of it.
    //
    //  What is left for this method is the part of a prepare that is not about
    //  sizing: the convolution's own tail. The old prepare used to clear it as a
    //  side effect of rebuilding the engine, and the stage's prepare now returns
    //  early on an unchanged spec (it is reached from a parameter change too), so
    //  the clear has to be asked for. A new session must not start with the
    //  previous one's reverb in the buffer.

    // The noise print, if one is loaded, is converted to this rate HERE - a
    // prepare may allocate, an audio callback may not - and the print stays in
    // its decoded form so a later rate switch costs arithmetic rather than
    // another read of the file.
    for (auto& stage : irStages)
        stage.reset();

    noiseLoader.prepare (sampleRate);
    postFxScratch.setSize (2, juce::jmax (1, samplesPerBlock * 8), false, true, true);
    postFxScratch.clear();

    // Per-channel DC-blocker state: AC coupling restarts from zero after a rate
    // change, exactly like the analogue coupling capacitors do on power-up.
    dcBlockXState.fill (0.0f);
    dcBlockYState.fill (0.0f);

    // -----------------------------------------------------------------------
    //  Playback head delay line.
    //
    //  Sized once, here, for the longest delay the control can ask for at the
    //  highest rate the engine can run at: 250 ms at 8x oversampling of 192 kHz is
    //  384000 samples. Allocating it in prepareToPlay and never resizing it is
    //  what keeps the audio thread free of allocation - the DELAY control only
    //  moves a read offset inside this buffer, it never changes its size.
    //
    //  Two channels at 4 bytes each is about 3 MB, which is the cost of having a
    //  head spacing that can be swept to a quarter of a second.
    // -----------------------------------------------------------------------
    const double maxEngineRate = juce::jmax (44100.0, sampleRateToUse) * 8.0;
    const int maxDelaySamples = static_cast<int> (std::ceil (0.25 * maxEngineRate)) + 4;
    delayBufferLength = juce::jmax (4, maxDelaySamples);
    resetDelayLine (samplesPerBlock);

    // The offset buffer is a fixed short window - a few hundred microseconds is far
    // more than the tens the control asks for - so it is a plain array, not an
    // allocation.
    stOffsetBuffer.reset();

    // The transport starts at speed and lets the first block decide what it should
    // actually be doing: the rig re-arms on its first beginBlock() (it keeps
    // `lastState` at -1), so a session saved at STOP or mid-hold comes back the
    // way it was saved rather than silent until the user touches the switch. The
    // rate is the ENGINE's, because the ramps advance per engine frame and the
    // gesture durations are wall-clock seconds.
    transport.prepare (static_cast<double> (sampleRate) * oversamplingRateFactor);

    // The repitch stage is prepared at each ENGINE rate for the same reason the
    // rig is: it advances once per engine frame, so its seconds and its samples
    // belong to the oversampled rate, not the host's. All four slots were
    // prepared above; the active one was reset when the factor was selected.

    // The delay's damping is tied to the machine's own low-pass so a dark tape
    // gives dark repeats without a second control to keep in sync.
    delayDampCoefficient = juce::jlimit (0.02f, 0.9f, toneLpAc + 0.08f);

    // Compressor-coupled saturation state restarts neutral, so the first block
    // after a rate switch is not coloured by a stale squeeze from the old rate.
    squeezeSaturationDrive = 0.0f;

    // The new stages' state. Everything that carries signal history has to start
    // clean on a rate change or the first block carries a stale tail - which for a
    // reverb means an audible burst of the previous session's room.
    inputStageL.reset();
    inputStageR.reset();
    tapeConditionL.reset();
    tapeConditionR.reset();
    vinylL.reset();
    vinylR.reset();
    resetInputChainState();

    // The room starts empty. Its tank runs at the rate its own delay lengths were
    // chosen at and the wrapper carries the signal across, so a new session rate
    // re-times that conversion rather than resizing the room - which is also why
    // the tail lasts the same number of SECONDS at any rate. prepare() clears the
    // tank itself, so there is nothing to flush afterwards.
    reverb.prepare (sampleRateToUse);

    // The vinyl stage's generator gets its own seed, separate from the tape hiss,
    // so the two noise sources are uncorrelated.
    vinylNoiseState = 0x9e3779b9u;

    // The tape noise generator is a per-instance LCG so that every plugin instance
    // and every render pass is deterministic, rather than sharing one thread_local
    // stream whose content would depend on how many instances happen to exist.
    noiseState = 0x1b873593u;

    // The safety limiter must start open, otherwise a stale gain from the previous
    // session would duck the first block audibly. It owns that state now -
    // Source/DSP/SafetyLimiter.h - so this is its own reset.
    limiter.reset();

    // The anti-phase guard starts neutral: no correction, and an energy floor that
    // keeps a silent opening passage from dividing by nothing.
    antiPhaseProduct = 0.0f;
    antiPhaseCorrection = 0.0f;
    antiPhaseProductMagnitude = 1.0e-3f;
    antiPhaseAmount.store (0.0f, std::memory_order_relaxed);

    // The per-band guard is prepared at the engine's rate here - the crossovers'
    // coefficients and the envelope's pole are both functions of it - and reset
    // so the first block after a rate change does not start with a stale filter.
    antiPhaseGuard.prepare (sampleRateToUse);
    antiPhaseGuard.reset();

    // The two equalisers start clean, so the first block after a rate change is
    // not coloured by a stale filter state from the previous rate.
    inputEq.reset();
    outputEq.reset();

    // And their six band gains start at the values the parameters restore, so a
    // session saved with a boost does not spend its first 20 ms gliding into it.
    inputEqLowSmoothed.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (inputEqLowParam != nullptr ? inputEqLowParam->load() : 0.0f));
    inputEqMidSmoothed.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (inputEqMidParam != nullptr ? inputEqMidParam->load() : 0.0f));
    inputEqHighSmoothed.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (inputEqHighParam != nullptr ? inputEqHighParam->load() : 0.0f));
    outputEqLowSmoothed.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (outputEqLowParam != nullptr ? outputEqLowParam->load() : 0.0f));
    outputEqMidSmoothed.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (outputEqMidParam != nullptr ? outputEqMidParam->load() : 0.0f));
    outputEqHighSmoothed.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (outputEqHighParam != nullptr ? outputEqHighParam->load() : 0.0f));

    harmonicAnalyser.reset();
    evenHarmonicRatio.store (0.0f, std::memory_order_relaxed);
    oddHarmonicRatio.store (0.0f, std::memory_order_relaxed);

    // Loudness metering. The K-weighting filters are built from the sample rate here,
    // so the LUFS reading is correct at every supported rate rather than being tuned
    // for one of them.
    outputLoudness.prepare (sampleRateToUse);
    outputLoudness.reset();
    inputLoudness.prepare (sampleRateToUse);
    inputLoudness.reset();
    vuAverage = 0.0f;
    inputVuAverage = 0.0f;

    inputCompressor.reset();
    outputCompressor.reset();

    // The final compensation starts from unity so the first block is not nudged by a
    // stale correction from a previous session or sample rate. The compensator owns
    // that correction now - Source/DSP/AutoGainCompensator.h - so this is its own
    // reset rather than a float written from here.
    autoGainCompensator.reset();
}

void FirstAudioProcessor::resetSampleRateDependentState()
{
    // Wow and flutter are very low frequency modulators, but they are advanced as
    // phase increments per sample, so a stale phase after a rate change is audible
    // as a click. Starting them from zero makes the transport restart cleanly.
    wowPhaseL = 0.0f;
    wowPhaseR = 0.0f;
    flutterPhaseL = 0.0f;
    flutterPhaseR = 0.0f;

    // Force the tone and TONE caches to rebuild against the new rate.
    previousTone = -1.0f;
    previousCharacter = -1.0f;
    if (toneParam != nullptr)
        updateToneCoefficients (toneParam->load(), sampleRate);

    // The control ramp: 20 ms. Every control-derived coefficient in the wet path
    // uses this one window, because it is the shortest that is reliably inaudible
    // as a step while still feeling immediate on a knob. Naming it means the
    // three other, deliberately DIFFERENT windows in this function (the 750 ms
    // hiss gate and the 150 ms formula switch) cannot be mistaken for it.
    constexpr double controlRampSeconds = 0.02;

    // Re-time the coefficient ramps for the new rate. The playback poles start from
    // neutral non-zero values, so the first wet block is audible while the exact
    // per-block targets are reached through the normal 20 ms ramps.
    toneShelfGainSmoothed.reset (sampleRate, controlRampSeconds);
    toneShelfGainSmoothed.setCurrentAndTargetValue (toneShelfGain);
    toneShelfBoostSmoothed.reset (sampleRate, controlRampSeconds);
    toneShelfBoostSmoothed.setCurrentAndTargetValue (toneShelfBoost);
    preDriveGainSmoothed.reset (sampleRate, controlRampSeconds);
    preDriveGainSmoothed.setCurrentAndTargetValue (preDriveGain);
    toneLpSmoothed.reset (sampleRate, controlRampSeconds);
    toneLpSmoothed.setCurrentAndTargetValue (toneLpAc);
    flutterScaleSmoothed.reset (sampleRate, controlRampSeconds);
    flutterScaleSmoothed.setCurrentAndTargetValue (flutterScale);

    driveAmountSmoothed.reset (sampleRate, controlRampSeconds);
    hfPostSmoothed.reset (sampleRate, controlRampSeconds);
    headGapSmoothed.reset (sampleRate, controlRampSeconds);
    // The noise floor ramps far slower than the controls: its gain is also the
    // transport gate (see processTapeEngine), so this window is how long the floor
    // takes to coast down when the machine comes to rest and back up when it spins
    // again - a fade, never a mute-switch drop.
    // The transport gate's own window: 750 ms. Deliberately far slower than the
    // control ramp - it is the time the noise floor takes to coast down when the
    // machine comes to rest, which should read as a fade rather than a mute.
    constexpr double transportGateSeconds = 0.75;
    hissGainSmoothed.reset (sampleRate, transportGateSeconds);
    shaperDriveSmoothed.reset (sampleRate, controlRampSeconds);
    shaperAsymmetrySmoothed.reset (sampleRate, controlRampSeconds);

    // The formula-switch ramp is deliberately slower than the control ramps: it has to
    // move the head-damping pole by up to 8 kHz without that travel being an audible
    // sweep. 150 ms is long enough to read as a morph and short enough that switching
    // formula still feels immediate.
    // The formula switch's own window: 150 ms. Slower than the control ramp because
    // a formula change moves the head-damping pole by up to 8 kHz, and 20 ms of
    // that travel is an audible sweep rather than a morph.
    constexpr double formulaSwitchSeconds = 0.15;
    headDampingSwitchSmoothed.reset (sampleRate, formulaSwitchSeconds);

    // The subharmonic generators hold a bi-stable state and a follower, so they carry
    // across blocks and have to start clean; the depth control ramps like every other
    // gain so that moving it cannot step the phase-locked oscillator.
    subharmonicL.reset();
    subharmonicR.reset();
    subFundamentalSmoothed.reset (sampleRate, controlRampSeconds);
    subFundamentalSmoothed.setCurrentAndTargetValue (
        subFundamentalParam != nullptr ? subFundamentalParam->load() : 0.0f);

    // A rate change invalidates any switch in progress, so the countdown is cleared and
    // the next block re-seeds activeTapeType instead of treating the new rate as a
    // formula change.
    activeTapeType = -1;
    tapeTypeChangeCountdown = 0;

    // The transient shaper's state is envelope-based, so a rate change invalidates it:
    // resuming from a stale envelope would read as a false transient on the first
    // block. The stage is reset and its three controls ramped like every other gain.
    transientL.reset();
    transientR.reset();
    transientAttackSmoothed.reset (sampleRate, controlRampSeconds);
    transientAttackSmoothed.setCurrentAndTargetValue (
        transientAttackParam != nullptr ? transientAttackParam->load() : 0.0f);
    transientSustainSmoothed.reset (sampleRate, controlRampSeconds);
    transientSustainSmoothed.setCurrentAndTargetValue (
        transientSustainParam != nullptr ? transientSustainParam->load() : 0.0f);
    transientMixSmoothed.reset (sampleRate, controlRampSeconds);
    transientMixSmoothed.setCurrentAndTargetValue (
        transientMixParam != nullptr ? transientMixParam->load() : 0.0f);

    // ~2 ms transient window, ~120 ms programme reference: the gap between the two
    // is what the detector reads as an edge. The gain smoother is slower than the
    // control ramp so the ride stays a ride rather than following the waveform.
    transientFastCoefficient = onePoleCoefficient (2.0f, sampleRate);
    transientSlowCoefficient = onePoleCoefficient (120.0f, sampleRate);
    transientGainCoefficient = onePoleCoefficient (30.0f, sampleRate);

    // The effects rack and the multiband compressor are prepared at the ENGINE
    // rate, which is the rate the per-sample loop runs at and therefore the rate
    // every delay length and filter corner in them is stated in. This is the only
    // place their memory is allocated: prepare() runs from prepareToPlay (and on a
    // rate change), never from the audio callback.
    fxRack.prepare (sampleRate);
    multibandCompressor.prepare (sampleRate);

    // The neural stage's recurrent state is let go of on a rate change for the same
    // reason the shaper's is: resuming a hidden state from a different rate would
    // produce a step. The model itself is untouched.
    // The model's wet/dry ramp. IR MIX rides its own smoother beside it.
    neuralL.reset();
    neuralR.reset();
    neuralMixSmoothed.reset (sampleRate, controlRampSeconds);
    neuralMixSmoothed.setCurrentAndTargetValue (
        neuralMixParam != nullptr ? neuralMixParam->load() : 0.0f);
    irMixSmoothed.reset (sampleRate, controlRampSeconds);
    irMixSmoothed.setCurrentAndTargetValue (
        irMixParam != nullptr ? irMixParam->load() : 0.0f);

    // The oversampling filters hold per-rate state (their half-band coefficients are
    // tuned to the incoming rate), so they must be flushed on a rate change or the
    // first block after the switch carries a stale pipeline and clicks.
    for (auto* oversampler : oversamplers)
        if (oversampler != nullptr)
            oversampler->reset();
}

void FirstAudioProcessor::publishReportedLatency()
{
    const auto total = oversamplerLatencySamples + publishedCorrectorLatencySamples;
    requestedLatencySamples.store (total, std::memory_order_release);
}

void FirstAudioProcessor::timerCallback()
{
    const auto total = requestedLatencySamples.load (std::memory_order_acquire);
    if (getLatencySamples() != total)
        AudioProcessor::setLatencySamples (total);

    updatePresetDirty();
}

void FirstAudioProcessor::updatePresetDirty() noexcept
{
    if (currentPresetName.isEmpty())
        return;

    for (std::size_t i = 0; i < dirtyBadgeParameters.size(); ++i)
    {
        const auto current = dirtyBadgeParameters[i]->load (std::memory_order_relaxed);
        if (std::abs (current - presetParameterSnapshot[i]) > 1.0e-6f)
        {
            presetDirty.store (true, std::memory_order_relaxed);
            return;
        }
    }
}

void FirstAudioProcessor::setOversamplingFactor (OversamplingFactor factor, int samplesPerBlock)
{
    // The rate each engine slot runs at, indexed by the enum's own order (off, x2,
    // x4, x8 - the same order the oversamplers array was built in, where
    // prepareToPlay derives the factor as 1 << slot). One table instead of a
    // branch per factor: adding a factor extends the table rather than leaving a
    // new else-if to forget.
    static constexpr float rateFactors[] { 1.0f, 2.0f, 4.0f, 8.0f };
    static_assert (sizeof (rateFactors) / sizeof (rateFactors[0])
                       == static_cast<int> (oversamplingFactorCount),
                   "one rate factor per oversampling slot");

    const auto clamped = (factor == OversamplingFactor::x2
                          || factor == OversamplingFactor::x4
                          || factor == OversamplingFactor::x8)
                       ? factor : OversamplingFactor::off;
    const auto clampedIndex = static_cast<int> (clamped);

    lastBlockSize = juce::jmax (1, samplesPerBlock);
    oversamplingRateFactor = rateFactors[static_cast<std::size_t> (clampedIndex)];

    // Select the PREPARED slot. prepareToPlay has already called prepare() on the
    // corrector, IR, trackers and repitch stage for every factor, so nothing here
    // allocates and nothing here may call prepare(): this function also runs from
    // processBlock when the host automation moves OVERSAMPLING.
    currentOversampling = clamped;

    if (auto* oversampler = oversamplers.getUnchecked (clampedIndex))
    {
        oversampler->reset();

        // The dummy stage adds no delay; the real ones report their filter latency
        // so every DAW can compensate sample-accurately. The message-thread timer
        // publishes the combined latency to the host.
        oversamplerLatencySamples = (clamped == OversamplingFactor::off)
                                        ? 0
                                        : juce::roundToInt (oversampler->getLatencyInSamples());

        // A factor change lands in a different engine, so the slot it left behind
        // must not hand its old tail to the new rate. These resets only clear
        // state - every buffer was sized in prepareToPlay - and resetting the
        // selected slot also makes a return to a factor start clean rather than
        // resume a stale ring.
        activeCorrector().reset();
        activeIrStage().reset();
        activeRepitchStage().reset();

        for (auto& tracker : activeAutoTuneTrackers())
            tracker.reset();

        autoTuneCorrectionSmoothed.fill (1.0f);
        autoTuneGateOpen.fill (false);
        autoTuneLastPeriodHint.fill (0);
        autoTuneRatiosForFrame = { 1.0f, 1.0f };
        autoTunePeriodHintsFor = { 0, 0 };

        publishReportedLatency();
    }

    // The engine's rate just moved, so a loaded model's conversion has to move
    // with it: a model trained at 48 kHz is run at 48 kHz from a 96 kHz engine
    // and from a 384 kHz one alike (see RateBridge). Cheap - it publishes two
    // numbers and the stages latch them - and safe here because the spectrum
    // analyser's prepare() is allocation-free.
    updateNeuralSampleRates();
}

void FirstAudioProcessor::updateNeuralSampleRates()
{
    // The rate the engine is running at: the session's rate times the oversampling
    // factor, which is the rate every stage inside the engine sees and therefore
    // the rate a model would be run at if it were not carried to its own.
    //
    // The model's rate may be 0, which means unknown - the RTNeural JSON format
    // carries no rate, and no model at all is the same case. Unknowable is not a
    // reason to guess: 0 leaves the model on the engine's samples, which is the
    // behaviour every build before the bridge had.
    const auto engineRate = static_cast<double> (
        juce::jmax (1.0f, sampleRate * oversamplingRateFactor));
    neuralEngineRate.store (engineRate, std::memory_order_relaxed);

    // The spectrum analyser is rebuilt from the same number, here, for the same
    // reason and in the same place. Its window and its band ladder are both
    // functions of the rate, so an OVERSAMPLING change moves every band edge: a
    // ladder built for a 48 kHz engine would label a 384 kHz transform with the
    // wrong frequencies and the curve would be a picture of nothing.
    //
    // This function is the engine-rate hook - reached from prepareToPlay and from
    // a factor change, which arrives on the audio thread - so preparing here is
    // what keeps a second copy of `sampleRate * oversamplingRateFactor` from being
    // written somewhere else and drifting. prepare() allocates nothing and takes
    // no lock (every buffer it fills is a member), so it is safe on that thread.
    spectrumAnalyser.prepare (engineRate);

    const auto modelRate = neuralModelRate.load (std::memory_order_relaxed);
    neuralL.setSampleRates (modelRate, engineRate);
    neuralR.setSampleRates (modelRate, engineRate);
}

void FirstAudioProcessor::updateToneCoefficients (float toneValue, float engineSampleRate)
{
    // Tone tilt: 0 = warm/soft, 1 = open/bright. Every corner is a real frequency in
    // Hz converted with onePoleCoefficientHz, so it means the same thing at every
    // sample rate. (The previous version passed millisecond values that were written
    // as if they were kilohertz - a 2*pi unit error - which put both corners around
    // 5-16 Hz and made the whole wet path sub-audio.)
    const auto toneCurve = std::pow (toneValue, 0.92f);

    // BRIGHTNESS is a true TILT: ONE control sweeping the machine's whole spectral
    // balance around a fixed pivot, not a shelf bolted on top of a fixed roll-off.
    // Both halves move together - the record roll-off pole AND the playback tilt
    // gains - from the same curve:
    //
    //   - record side: the magnetic medium's roll-off travels 3 kHz -> 30 kHz, so
    //     warm genuinely darkens the source and bright opens the record path wide.
    //
    //   - playback side: a fixed-PIVOT tilt stage low-passes the WET SIGNAL
    //     itself at 1.6 kHz and applies a matched gain PAIR - the band above the
    //     pivot and the band below move in opposite directions from the same
    //     Brightness value - removal is capped at a gentle ~4 dB while the
    //     opposite band opens up to +15 dB. 50 percent is exactly
    //     neutral (both gains unity). The pivot split is taken from the signal
    //     itself, NOT from a filtered copy: the previous "shelf" split the signal
    //     against its own already-low-passed output, so the "high band" it
    //     boosted was mostly hiss residue - which is why the knob never showed up
    //     on an analyser no matter how far it travelled.
    toneLpAc = onePoleCoefficientHz (3000.0f + 27000.0f * toneCurve, engineSampleRate);

    toneShelfCoefficient = onePoleCoefficientHz (1600.0f, engineSampleRate);

    // Matched tilt gains around the 1.6 kHz pivot, deliberately ASYMMETRIC:
    // a band never loses more than about 3.75 dB, while the opposite band opens
    // up to +15 dB - the knob should take away a little and give a lot. u sweeps
    // -1..+1 as Brightness sweeps 0..1 and is built from the raw control (not
    // the record-side curve), so at the 50 percent pivot u is exactly 0, both
    // gains are unity and the playback passes through untouched.
    const auto tiltU = 2.0f * toneValue - 1.0f;
    const auto tiltGive = 0.75f * juce::jmax (0.0f, tiltU);   // the band that opens: up to +15 dB
    const auto tiltTake = 0.1875f * juce::jmin (0.0f, tiltU); // the band that yields: max ~-3.75 dB
    toneShelfGain  = std::pow (10.0f, -tiltGive - tiltTake);  // low:  +15 dB warm .. -3.75 dB bright
    toneShelfBoost = std::pow (10.0f,  tiltGive + tiltTake);  // high: -3.75 dB warm .. +15 dB bright
    previousTone = toneValue;

    // TONE macro crossfade, between machine states rather than dry/wet:
    //   head gap  - the dominant top-end damping of the playback head
    //   pre-bias  - how hard the record head is driven for a given input
    //   flutter   - the fast transport shimmer every speed sets
    // 0 % is the classic slow machine (soft, dark, wide wow), 100 % the fast one
    // (open, tight, present). Cached here so the per-sample loop only ever reads
    // ready-made coefficients.
    const auto character = (characterParam != nullptr
                               ? juce::jlimit (0.0f, 1.0f, characterParam->load())
                               : 0.5f);
    const auto characterCurve = std::pow (character, 1.20f);

    headGapHz = 24000.0f * std::pow (0.28f, characterCurve); // 24 kHz -> 4.3 kHz
    preDriveGain = 1.0f + 0.55f * (1.0f - characterCurve);
    flutterScale = 0.75f + 0.55f * characterCurve;
    previousCharacter = character;
}

void FirstAudioProcessor::releaseResources()
{
    // The host calls this when it stops using the plugin: on a device change, on a
    // project close, and before it destroys it. It does nothing to the engine - the
    // stages are prepared again on the next prepareToPlay - but it is the last
    // message-thread moment before the teardown in some hosts, so it is where the
    // shutdown trace says which of the two it was.
    j37diag::note ("releaseResources: the host stopped the stream");
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool FirstAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif


//==============================================================================
FirstAudioProcessor::TelemetryFrame FirstAudioProcessor::getTelemetry() const
{
#if J37_HAS_RWQ
    // Drain to the NEWEST frame rather than taking the first: the editor polls at
    // 30 Hz while the audio thread may push a frame per block (which can be several
    // hundred a second), so reading the oldest would show the panel a picture from
    // well behind the audio. Popping until the ring is empty leaves `frame` holding
    // the most recent one - which is the only one worth drawing.
    TelemetryFrame frame;
    bool got = false;

    TelemetryFrame next;
    while (telemetryQueue.try_dequeue (next))
    {
        frame = next;
        got = true;
    }

    if (got)
        return frame;
#endif

    // Either the library is absent or nothing has been pushed yet (the very first
    // UI frame, before the first block). Fall back to the atomics, which processBlock
    // keeps current on every path including the bypass return.
    TelemetryFrame fallback;
    fallback.inputPeakDb = telemetryInputPeakDb.load (std::memory_order_relaxed);
    fallback.outputPeakDb = telemetryOutputPeakDb.load (std::memory_order_relaxed);
    fallback.inputRmsDb = telemetryInputRmsDb.load (std::memory_order_relaxed);
    fallback.outputRmsDb = telemetryOutputRmsDb.load (std::memory_order_relaxed);
    fallback.antiPhaseAmount = telemetryAntiPhase.load (std::memory_order_relaxed);
    fallback.transportRamp = telemetryPlatter.load (std::memory_order_relaxed);
    return fallback;
}

//==============================================================================
bool FirstAudioProcessor::loadNeuralModel (const juce::String& modelJson)
{
    // The new model is staged for adoption at the next audio block, so loading
    // never replaces the instance currently in inference.
    lastLoadErrorMessage = {};
    lastLoadDetailMessage = {};

    if (modelJson.isEmpty())
    {
        lastLoadErrorMessage = "EMPTY FILE";
        return false;
    }

    const bool loadedLeft = neuralL.loadFromJson (modelJson);
    const bool loadedRight = neuralR.loadFromJson (modelJson);
    if (loadedLeft != loadedRight)
    {
        neuralL.clear();
        neuralR.clear();
    }

    if (! (loadedLeft && loadedRight))
    {
        lastLoadErrorMessage = "NOT A MODEL FILE";
        lastLoadDetailMessage = "The file parsed as JSON but not as an RTNeural "
                                "network: the layer stack it describes is not one "
                                "this build can build.";
    }

    // RTNeural's own JSON carries no rate - the model's topology is all it
    // describes - so this model has no rate to be run at and keeps the engine's,
    // which is what every build before the bridge did. A rate left over from a
    // .nam model must NOT survive here: it would be that file's rate applied to
    // this one.
    if (loadedLeft && loadedRight)
    {
        neuralModelRate.store (0.0, std::memory_order_relaxed);
        updateNeuralSampleRates();
    }

    return loadedLeft && loadedRight;
}

bool FirstAudioProcessor::loadNamModel (const juce::String& namJson)
{
    // Same two-stages-at-once discipline as loadNeuralModel above, through the
    // .nam converter: the shared model pointer must not end up describing two
    // different networks on the two sides of the image.
#if J37_HAS_RTNEURAL || J37_HAS_NAMCORE
    lastLoadErrorMessage = {};
    lastLoadDetailMessage = {};

    if (namJson.isEmpty())
    {
        lastLoadErrorMessage = "EMPTY FILE";
        return false;
    }

    bool loadedLeft = false;
    bool loadedRight = false;

    try
    {
        //  A .nam file written on Windows sometimes carries a UTF-8 byte-order
        //  mark. nlohmann's parser refuses it - correctly, it is not JSON - and
        //  the rejection reads as "NOT JSON" for a file that is exactly JSON.
        //  JUCE's loadFileAsString() drops the mark for us; this guard keeps the
        //  processor honest for any other caller that hands it raw bytes.
        auto text = namJson;
        if (text.startsWithChar (static_cast<juce::juce_wchar> (0xfeff)))
            text = text.substring (1);

        const auto parsed = nlohmann::json::parse (text.toStdString());

        // The architecture is checked HERE only when this build has NO engine for
        // the other ones, so the panel can say which kind of file arrived. WaveNet
        // and the convolutional nets are NAM's other architectures and between
        // them account for most models on the model-sharing sites, so "NEEDS AN
        // LSTM MODEL" is the one failure a user is most likely to meet in a build
        // without NAM Core - and a bare "INVALID MODEL" would send them looking
        // for a corrupt file rather than a different download.
        //
        // With NAM Core in the build that check would be a lie: WaveNet is exactly
        // what the second engine is for, so the decision is left to the stage,
        // which accepts what either engine can build and refuses the rest.
    #if ! J37_HAS_NAMCORE
        if (! parsed.contains ("architecture") || ! parsed["architecture"].is_string()
            || parsed["architecture"].get<std::string>() != "LSTM")
        {
            lastLoadErrorMessage = "NEEDS LSTM";
            return false;
        }
    #endif

        // The rate the model was TRAINED at. It is the one thing in a .nam file
        // the converter has no use for and the ENGINE does: a neural net's memory
        // is counted in samples rather than seconds, so a 48 kHz model run on a
        // 192 kHz engine is a different network rather than a brighter one (see
        // RateBridge).
        //
        // `sample_rate` is OPTIONAL in the format, and the format's own
        // documentation says to assume 48 kHz when it is absent - which is also
        // the rate every .nam model this build is likely to meet was trained at,
        // so that is the assumption, and it only ever changes the model's sound
        // where the engine is NOT already running at 48 kHz.
        auto modelRate = 48000.0;
        if (const auto rate = parsed.find ("sample_rate");
            rate != parsed.end() && rate->is_number())
            modelRate = rate->get<double>();

        loadedLeft = neuralL.buildFromNam (parsed);
        loadedRight = neuralR.buildFromNam (parsed);

        if (loadedLeft && loadedRight)
            neuralModelRate.store (modelRate, std::memory_order_relaxed);
    }
    catch (const std::exception& error)
    {
        lastLoadErrorMessage = "NOT JSON";
        // The parser's own "[json.exception.parse_error.101] ..." is too long
        // for the readout and just right for its tooltip - cut to one line's
        // worth, because the parser quotes the document it choked on and a .nam
        // document is mostly a weights array.
        lastLoadDetailMessage = juce::String (error.what()).trim().substring (0, 240);
        loadedLeft = false;
        loadedRight = false;
    }
    catch (...)
    {
        lastLoadErrorMessage = "NOT JSON";
        loadedLeft = false;
        loadedRight = false;
    }

    if (loadedLeft != loadedRight)
    {
        neuralL.clear();
        neuralR.clear();
    }

    if (! (loadedLeft && loadedRight) && lastLoadErrorMessage.isEmpty())
    {
        lastLoadErrorMessage = "BAD SHAPE";

        //  Neither engine produced a model. NAM Core's own sentence is the one
        //  thing that can tell a user whether the file is corrupt, from an
        //  unsupported version, or simply an architecture this build lacks.
        const auto engineSaid = juce::String (neuralL.lastBuildErrorText()).trim().substring (0, 240);
        if (engineSaid.isNotEmpty())
        {
            lastLoadDetailMessage = engineSaid;

            //  A version refusal is the one failure with a one-line answer, so
            //  it keeps its own short label rather than "BAD SHAPE".
            if (engineSaid.containsIgnoreCase ("unsupported version"))
                lastLoadErrorMessage = "NAM VERSION";
            else if (engineSaid.containsIgnoreCase ("missing required key"))
                lastLoadErrorMessage = "NOT A NAM FILE";
        }
        else
        {
            lastLoadDetailMessage = "The file was read as a .nam document, but "
                                    "neither the LSTM converter nor NAM Core could "
                                    "build a model from it.";
        }
    }

    // The model's own rate reaches the stages here, after the load rather than
    // before it: the stages latch the pair on their next inference, so the bridge
    // is prepared for the model that is actually installed.
    if (loadedLeft && loadedRight)
        updateNeuralSampleRates();

    return loadedLeft && loadedRight;
#else
    //  A pruned build has neither engine. Said out loud rather than returned as
    //  a bare false: a silent refusal is what made "NAM does not load" look like
    //  a button that does nothing, because the readout never left "NO MODEL".
    juce::ignoreUnused (namJson);
    lastLoadErrorMessage = "NO NAM ENGINE";
    lastLoadDetailMessage = "This build has neither RTNeural nor NAM Core, so no "
                            ".nam file can be played - rebuild with the vendored "
                            "libraries, or with -DJ37_NAMCORE=ON.";
    return false;
#endif
}

bool FirstAudioProcessor::loadIrFile (const juce::File& irFile)
{
    lastLoadErrorMessage = {};
    lastLoadDetailMessage = {};

    if (! irFile.existsAsFile())
    {
        lastLoadErrorMessage = "NO SUCH FILE";
        return false;
    }

    // Load the file into every factor's slot. Each slot's convolvers were
    // prepared for a different engine rate in prepareToPlay, and the active one
    // can change while this IR is loaded; loading only the slot that happens to
    // be active would leave the next factor silent. The decode itself is JUCE's
    // background job, so this remains a message-thread call.
    bool anyLoaded = false;
    bool allLoaded = true;
    for (auto& stage : irStages)
    {
        const auto loaded = stage.loadFromFile (irFile);
        anyLoaded = anyLoaded || loaded;
        allLoaded = allLoaded && loaded;
    }

    if (allLoaded)
        return true;

    // A partial load is not a usable IR: keep every slot in the same state rather
    // than leaving one factor with a file and the others without one. If NO slot
    // loaded, every stage still holds its previous IR and nothing changes - the
    // same "a failed load does not replace what was playing" rule as before.
    if (anyLoaded)
        for (auto& stage : irStages)
            stage.clear();

    // The one way a real WAV gets here is a header JUCE will not decode (an
    // unsupported codec, or a file that is not audio at all). The reason is
    // about THIS file, not about the stage.
    lastLoadErrorMessage = "NOT AUDIO";
    lastLoadDetailMessage = "JUCE's basic formats read this file and found no "
                            "audio frames: it is not a WAV or AIFF this build can "
                            "decode, or it is empty.";
    return false;
}

void FirstAudioProcessor::clearIr()
{
    for (auto& stage : irStages)
        stage.clear();
    loadedIrName = {};
}

bool FirstAudioProcessor::loadNoiseFile (const juce::File& noiseFile)
{
    if (! noiseFile.existsAsFile())
        return false;

    // Everything else about this load happens on the loader's own thread: the
    // read, the decode and the rate conversion all allocate, and none of them may
    // happen anywhere near the audio callback. The audio thread sees the result
    // as one pointer swap, or never sees it at all if the file turns out not to
    // be a WAV this build can read - in which case getNoiseError() says why and
    // the previous print, if there was one, is still playing.
    noiseLoader.requestLoad (noiseFile);
    return true;
}

void FirstAudioProcessor::clearNoiseFile()
{
    noiseLoader.clear();
}

void FirstAudioProcessor::clearNeuralModel()
{
    neuralL.clear();
    neuralR.clear();
    loadedNeuralName = {};
}

//==============================================================================
bool FirstAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* FirstAudioProcessor::createEditor()
{
    return new FirstAudioProcessorEditor (*this);
}

//==============================================================================
void FirstAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Stamped with the current format, so the session this saves never needs
    // migrating when it is opened again.
    auto state = captureState (parameters);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void FirstAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // sizeInBytes is already an int, and so is getXmlFromBinary()'s second parameter, so
    // casting it to size_t only bought a size_t -> int narrowing warning.
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState != nullptr)
    {
        // This is the path every existing DAW project arrives on, and the one the
        // MIX rescale hits hardest. The tree holds RAW parameter values, so a
        // session saved at MIX 0.5 would be read as 0.5 PERCENT by the new 0..100
        // range and the machine would come back all but silent. Migrated here,
        // before the state reaches the parameters, and only for states written
        // before the format marker existed.
        auto tree = juce::ValueTree::fromXml (*xmlState);
        migrateStateFormat (tree);
        parameters.replaceState (tree);
    }

    // A freshly loaded session defines both A/B slots: the loaded state becomes
    // the active side and both slots are seeded with it, so compare starts clean.
    copyToCompareSlot (0);
    copyToCompareSlot (1);
    activeSlot.store (0, std::memory_order_relaxed);

    // The audio block reads the momentary spindown parameter directly, so no
    // mirrored state needs synchronising after the session restore.

    // A session load restores the parameters, not the preset that produced them:
    // the badge starts clean and unnamed, exactly like a freshly opened plugin.
    markPresetClean ({});
    lastPresetIndex.store (-1, std::memory_order_relaxed);
}

//==============================================================================
#if JUCE_ANDROID
namespace
{
    //  The standalone holder asks for zero inputs when the microphone permission
    //  is denied, but a saved DEVICESETUP can still carry `audioDeviceInChans`.
    //  AudioDeviceManager restores that mask before its output-only retry, so the
    //  retry asks for the microphone again and the app stays silent. The holder's
    //  settings are already loaded when createPluginFilter() runs, and this call
    //  happens before the holder reloads the device, so drop the mask here when the
    //  permission is not granted. If it is granted later, the default channel count
    //  rebuilds the mask; nothing is lost but the stale disabled-input setup.
    void dropSavedInputChannelsWithoutPermission()
    {
        if (juce::RuntimePermissions::isGranted (juce::RuntimePermissions::recordAudio))
        {
            j37diag::note ("android: microphone permission granted before the device opened");
            return;
        }

        j37diag::note ("android: microphone permission not granted, dropping saved input channels");

        if (auto* holder = juce::StandalonePluginHolder::getInstance())
        {
            if (auto* settings = holder->settings.get())
            {
                if (auto savedState = settings->getXmlValue ("audioSetup"))
                {
                    savedState->removeAttribute ("audioDeviceInChans");
                    settings->setValue ("audioSetup", savedState.get());
                }
                // Also set a flag to indicate permission was denied, so the UI can
                // show an appropriate message if needed
                settings->setValue ("microphonePermissionDenied", true);
            }
        }
    }
}
#endif

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    // The earliest point the native half is reached, and on Android the earliest
    // the diagnostic file can be named: the Java half has already written its
    // lifecycle lines by the time this runs, so the two halves share one file.
    j37diag::beginRunForThisPlatform ("createPluginFilter");

   #if JUCE_ANDROID
    dropSavedInputChannelsWithoutPermission();
   #endif

    auto* processor = new FirstAudioProcessor();
    j37diag::note ("native: plugin instance created");
    return processor;
}
