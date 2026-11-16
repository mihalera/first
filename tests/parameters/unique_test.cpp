// =============================================================================
//  One UNIQUE test per parameter, built against the shipping table.
//
//    tests/parameters/run.sh   (second binary; the first is sweep_test.cpp)
//
//  sweep_test.cpp asks every parameter the same five GENERIC questions. This
//  harness asks each parameter its OWN question instead: a row of expectations
//  hand-pinned from the table in Source/ParameterTable.cpp, one row per
//  parameter, written the way a reviewer would state them -
//
//    - the KIND the host automates (float, bool, choice)
//    - the NAME the host and the panel display
//    - the DEFAULT a fresh instance loads, the exact number the table states
//    - a float's RANGE and STEP, and its KNOB TAPER (the skew centre the table
//      gives percentageRange - the taper is a knob-travel contract, not a sound)
//    - the UNIT LABEL the control carries (dB, %, Hz, us, ct, st, oct, x, ms, Q)
//    - a choice's COUNT, DEFAULT INDEX and the option NAMES the table spells out
//
//  So a change to any ONE parameter - a default nudged, a range widened, a step
//  coarsened, a choice renamed, a knob's taper changed, a unit dropped - fails
//  THAT parameter's test and names it, rather than passing a generic sweep that
//  was never asked about the number.
//
//  Two meta rules keep the table complete, and they are the reason a new
//  parameter cannot slip past it:
//
//    1. every registered parameter has a row - a parameter added without its
//       own unique test fails the harness;
//    2. every row matches a registered parameter - a parameter deleted without
//       its row being removed fails the harness too.
//
//  The rows also pin the table's ORDER: row i must be parameter i, because the
//  groups are called in the order the parameters were declared in and the order
//  is part of what a host's parameter index addresses.
//
//  What is deliberately NOT restated: the six model-list choice names
//  (tape_type, valve_type, amp_type, transformer_type, digital_type,
//  vinyl_type). They are single-sourced in the headers and arrive here through
//  the extracted table; restating them would be the second copy that already
//  drifted once. Their rows pin the count and the default index, and the sweep
//  holds their Off-last convention.
//
//  The table is the plugin's own: no JUCE, no host, no audio - juce_stub.h is
//  the same stand-in the state harness uses, and the layout is cut verbatim out
//  of Source/ by tests/state/extract.py, not copied.
// =============================================================================
#include "juce_stub.h"

#include <cstddef>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "extracted_preamble.inc"
#include "expected_parameters.h"

/** The stand-in for the plugin processor: only the members the extracted
    functions touch, declared as the shipping class declares them. */
struct FirstAudioProcessor : public juce::AudioProcessor
{
    FirstAudioProcessor() : parameters ("TAPE_NONLIN", createParameterLayout()) {}

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState parameters;

    juce::ValueTree compareSlots[2];
    std::atomic<int> activeSlot { 0 };
    std::atomic<bool> compareDirty { false };
    std::atomic<int> lastPresetIndex { -1 };
    juce::String currentPresetName;
    std::atomic<bool> presetDirty { false };
    std::vector<std::atomic<float>*> dirtyBadgeParameters;
    std::vector<float> presetParameterSnapshot;

    void copyToCompareSlot (int slot);
    void updateCompareDirty();

#include "mark_preset_clean.inc"

    void getStateInformation (juce::MemoryBlock& destData);
    void setStateInformation (const void* data, int sizeInBytes);
};

#include "extracted_members.inc"

namespace
{
int failures = 0;

void check (bool condition, const std::string& what)
{
    if (! condition)
    {
        std::printf ("  FAIL  %s\n", what.c_str());
        ++failures;
    }
}

std::string idOf (const juce::RangedAudioParameter& parameter)
{
    return parameter.getParameterID().id.toStdString();
}

constexpr const char* const* noOptions = nullptr;

// The option lists the table spells out, pinned exactly as written there.
const char* const eqOrderOptions[] = { "6 dB/oct", "12 dB/oct", "18 dB/oct",
                                       "24 dB/oct", "36 dB/oct", "48 dB/oct" };
const char* const speedOptions[] = { "7.5 ips", "15 ips", "30 ips" };
const char* const instrumentOptions[] = { "Master Bus", "Vocal", "Bass", "Guitar",
                                          "Piano", "Drums" };
const char* const oversamplingOptions[] = { "Off", "2x", "4x", "8x" };
const char* const tracksOptions[] = { "2", "2+3", "3", "Off" };
const char* const transportOptions[] = { "Stop", "Play", "Start" };
const char* const tonicOptions[] = { "C", "C#", "D", "D#", "E", "F",
                                     "F#", "G", "G#", "A", "A#", "B" };
const char* const modeOptions[] = { "Chromatic", "Major", "Minor", "Dorian",
                                    "Pentatonic Major", "Pentatonic Minor" };
// The language list's second entry is written with the SAME hex-escape literal
// the table uses, so the harness's compiler builds the pinned expectation and
// the extracted table identically - whatever it does with the escapes, it does
// it to both.
const char* const languageOptions[] = { "English",
                                        "\xd0\xa3\xd0\xba\xd1\x80\xd0\xb0\xd1\x97\xd0\xbd"
                                        "\xd1\x81\xd1\x8c\xd0\xba\xd0\xb0" };
const char* const padOptions[] = { "0 dB", "-10 dB", "-20 dB", "-30 dB" };
const char* const delayTypeOptions[] = { "Tape", "BBD", "Modern" };
const char* const vinylSpeedOptions[] = { "33 RPM", "45 RPM", "78 RPM" };
const char* const generationOptions[] = { "Laquer", "Direct", "Printed", "Off" };
const char* const turntableOptions[] = { "Belt", "Direct", "Idler", "Off" };
const char* const cartridgeOptions[] = { "MM", "MC", "DJ", "Off" };
const char* const delayRateOptions[] = { "1/1", "1/2", "1/4", "1/8", "1/16",
                                         "1/4 T", "1/8 T", "1/4 D" };

/** One parameter's unique expectations, in the order the table builds them.

    `def` is the value the table STATES: the number in the layout call. The
    parameter stores it snapped to its own grid, so the check compares the
    stored default against the same snap of the stated number - the identical
    arithmetic the parameter's own constructor runs - rather than pretending
    every stated number is already a legal grid value.
*/
struct Expect
{
    const char* id;
    char kind;              // 'F' float, 'B' bool, 'C' choice
    const char* name;       // the display name the host and the panel read
    float def;              // the default as the table states it
    float start;            // floats: the range as stated
    float end;
    float step;
    float taper;            // floats: the knob taper's centre; 0 when none
    const char* unit;       // the attribute label; "" when there is none
    int options;            // choices: how many entries
    const char* const* list; // exact names when the table spells them; nullptr for a model list
};

// -- addMonitorControls: the panel-wide monitors ---------------------------------
constexpr Expect uniqueExpectations[] =
{
    { "input", 'F', "Input", 0.0f, -32.0f, 32.0f, 0.1f, 0.0f, "dB", 0, noOptions },
    { "output", 'F', "Output", 0.0f, -32.0f, 32.0f, 0.1f, 0.0f, "dB", 0, noOptions },
    { "bypass", 'B', "Bypass", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 0, noOptions },
    { "delta", 'B', "Delta Listen", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 0, noOptions },
    { "polarity", 'B', "Polarity Invert", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 0, noOptions },
    { "auto_gain", 'B', "Auto Gain", 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 0, noOptions },
    { "glue_on", 'B', "Glue Compressors", 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 0, noOptions },

    // -- addEqualiserControls: two EQs, three bands each, plus filters ----------
    { "in_low", 'F', "In EQ Low", 0.0f, -12.0f, 12.0f, 0.1f, 0.0f, "dB", 0, noOptions },
    { "in_mid", 'F', "In EQ Mid", 0.0f, -12.0f, 12.0f, 0.1f, 0.0f, "dB", 0, noOptions },
    { "in_high", 'F', "In EQ High", 0.0f, -12.0f, 12.0f, 0.1f, 0.0f, "dB", 0, noOptions },
    { "out_low", 'F', "Out EQ Low", 0.0f, -12.0f, 12.0f, 0.1f, 0.0f, "dB", 0, noOptions },
    { "out_mid", 'F', "Out EQ Mid", 0.0f, -12.0f, 12.0f, 0.1f, 0.0f, "dB", 0, noOptions },
    { "out_high", 'F', "Out EQ High", 0.0f, -12.0f, 12.0f, 0.1f, 0.0f, "dB", 0, noOptions },
    { "in_hp_freq", 'F', "In EQ HP Freq", 20.0f, 20.0f, 500.0f, 1.0f, 100.0f, "Hz", 0, noOptions },
    { "in_lp_freq", 'F', "In EQ LP Freq", 20000.0f, 2000.0f, 20000.0f, 10.0f, 8000.0f, "Hz", 0,
      noOptions },
    { "in_hp_on", 'B', "In EQ HP On", 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 0, noOptions },
    { "in_lp_on", 'B', "In EQ LP On", 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 0, noOptions },
    { "in_eq_order", 'C', "In EQ Order", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 6, eqOrderOptions },
    { "in_eq_q", 'F', "In EQ Q", 0.7f, 0.5f, 1.5f, 0.01f, 0.0f, "Q", 0, noOptions },
    { "out_hp_freq", 'F', "Out EQ HP Freq", 20.0f, 20.0f, 500.0f, 1.0f, 100.0f, "Hz", 0,
      noOptions },
    { "out_lp_freq", 'F', "Out EQ LP Freq", 20000.0f, 2000.0f, 20000.0f, 10.0f, 8000.0f, "Hz", 0,
      noOptions },
    { "out_hp_on", 'B', "Out EQ HP On", 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 0, noOptions },
    { "out_lp_on", 'B', "Out EQ LP On", 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 0, noOptions },
    { "out_eq_order", 'C', "Out EQ Order", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 6, eqOrderOptions },
    { "out_eq_q", 'F', "Out EQ Q", 0.7f, 0.5f, 1.5f, 0.01f, 0.0f, "Q", 0, noOptions },

    // -- addSubFundAndWidthControls --------------------------------------------
    { "subfund", 'F', "Sub-Fundamental", 0.0f, 0.0f, 1.0f, 0.001f, 0.0f, "%", 0, noOptions },
    { "stereo_width", 'F', "Stereo Width", 0.5f, 0.0f, 1.0f, 0.001f, 0.0f, "%", 0, noOptions },
    { "anti_phase_guard", 'B', "Anti-Phase Guard", 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 0, noOptions },

    // -- addDeckControls: the deck itself ---------------------------------------
    { "tape_type", 'C', "Tape Type", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 13, noOptions },
    { "speed", 'C', "Speed", 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 3, speedOptions },
    { "instrument", 'C', "Instrument", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 6, instrumentOptions },
    { "drive", 'F', "Drive", 0.30f, 0.0f, 1.0f, 0.001f, 0.45f, "%", 0, noOptions },
    { "bias", 'F', "Bias", 0.42f, 0.0f, 1.0f, 0.001f, 0.40f, "%", 0, noOptions },
    { "oversampling", 'C', "Oversampling", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 4,
      oversamplingOptions },
    { "tone", 'F', "Brightness", 0.50f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "wow", 'F', "Wow", 0.14f, 0.0f, 1.0f, 0.001f, 0.35f, "%", 0, noOptions },
    { "flutter", 'F', "Flutter", 0.18f, 0.0f, 1.0f, 0.001f, 0.35f, "%", 0, noOptions },
    { "mix", 'F', "Mix", 50.0f, 0.0f, 100.0f, 0.1f, 0.0f, "%", 0, noOptions },
    { "tracks", 'C', "Modeled Tracks", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 4, tracksOptions },
    { "character", 'F', "Tone", 0.50f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },

    // -- addTapeDelayControls: the second playback head -------------------------
    { "delay_time", 'F', "Delay", 0.0f, 0.0f, 250.0f, 0.1f, 0.0f, "ms", 0, noOptions },
    { "delay_feedback", 'F', "Delay Level", 0.0f, 0.0f, 1.0f, 0.001f, 0.40f, "%", 0, noOptions },
    { "delay_pingpong", 'F', "Ping-Pong", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "delay_tone", 'F', "Delay Tone", 0.50f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "delay_lowcut", 'F', "Delay Low Cut", 80.0f, 20.0f, 500.0f, 1.0f, 0.0f, "Hz", 0, noOptions },
    { "delay_damp", 'F', "Delay Damping", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },

    // -- addOffsetAndNoiseControls ----------------------------------------------
    { "st_offset", 'F', "ST Offset", 0.0f, -500.0f, 500.0f, 1.0f, 0.0f, "us", 0, noOptions },
    { "noise", 'F', "Noise Mix", 0.50f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "noise_lvl", 'F', "Noise Level", 1.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },

    // -- addTransportControls ---------------------------------------------------
    { "transport", 'C', "Transport", 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 3, transportOptions },
    { "spindown", 'B', "Spindown", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 0, noOptions },
    { "ui_sounds", 'B', "UI Sounds", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 0, noOptions },
    { "osc_on", 'B', "OSC", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 0, noOptions },

    // -- addTuningControls ------------------------------------------------------
    { "tuning_tonic", 'C', "Tonic", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 12, tonicOptions },
    { "tuning_mode", 'C', "Mode", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 6, modeOptions },
    { "autotune_amount", 'F', "Auto Tune Amount", 0.0f, 0.0f, 1.0f, 0.001f, 0.0f, "", 0,
      noOptions },
    { "autotune_speed", 'F', "Auto Tune Speed", 0.50f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0,
      noOptions },
    { "autotune_tolerance", 'F', "Auto Tune Tolerance", 10.0f, 0.0f, 50.0f, 0.5f, 0.0f, "ct", 0,
      noOptions },
    { "autotune_gate", 'F', "Auto Tune Gate", 0.50f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "autotune_range", 'F', "Auto Tune Range", 1.0f, 1.0f, 12.0f, 1.0f, 0.0f, "st", 0, noOptions },
    { "autotune_octave", 'F', "Auto Tune Octave", 0.0f, -1.0f, 1.0f, 1.0f, 0.0f, "oct", 0,
      noOptions },

    // -- addCompressorControls --------------------------------------------------
    { "comp_threshold", 'F', "Compressor Threshold", -18.0f, -60.0f, 0.0f, 0.1f, 0.0f, "", 0,
      noOptions },
    { "comp_ratio", 'F', "Compressor Ratio", 2.0f, 1.0f, 20.0f, 0.1f, 0.0f, "", 0, noOptions },
    { "comp_attack", 'F', "Compressor Attack", 10.0f, 0.1f, 100.0f, 0.1f, 0.0f, "", 0, noOptions },
    { "comp_release", 'F', "Compressor Release", 120.0f, 10.0f, 1000.0f, 1.0f, 0.0f, "", 0,
      noOptions },
    { "comp_makeup", 'F', "Compressor Makeup", 0.0f, 0.0f, 24.0f, 0.1f, 0.0f, "", 0, noOptions },
    { "comp_mix", 'F', "Compressor Mix", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },

    // -- addLanguageControl -----------------------------------------------------
    { "language", 'C', "Language", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 2, languageOptions },

    // -- addSaturationMixControls: the seven SOURCE knobs -----------------------
    { "tape_source", 'F', "Tape", 1.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "vinyl_source", 'F', "Vinyl", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "cassette_source", 'F', "Cassette", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "digital_source", 'F', "Digital", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "amp_source", 'F', "Amp", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "valve_source", 'F', "Valve", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "transformer_source", 'F', "Transformer", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0,
      noOptions },

    // -- addAmpVoicingControls --------------------------------------------------
    { "sag", 'F', "Sag", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "presence", 'F', "Presence", 0.50f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "cabinet", 'F', "Cabinet", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "amp_bias", 'F', "Amp Bias", 0.50f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },

    // -- addPreampControl -------------------------------------------------------
    { "preamp", 'F', "Preamp", 0.0f, 0.0f, 1.0f, 0.001f, 0.45f, "%", 0, noOptions },

    // -- addDiBoxControls -------------------------------------------------------
    { "di", 'F', "DI", 0.0f, 0.0f, 1.0f, 0.001f, 0.45f, "%", 0, noOptions },
    { "di_load", 'F', "DI Load", 0.0f, 0.0f, 1.0f, 0.001f, 0.45f, "%", 0, noOptions },
    { "di_transformer", 'F', "DI XFMR", 0.0f, 0.0f, 1.0f, 0.001f, 0.45f, "%", 0, noOptions },
    { "di_pad", 'C', "DI Pad", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 4, padOptions },

    // -- addMediaWearControls: FLUX, WEAR, MECHANICS ----------------------------
    { "flux", 'F', "Flux", 0.50f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "wear", 'F', "Wear", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "mechanics", 'F', "Mechanics", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },

    // -- addReverbControls: the room --------------------------------------------
    { "reverb", 'F', "Reverb", 0.0f, 0.0f, 1.0f, 0.001f, 0.45f, "%", 0, noOptions },
    { "reverb_size", 'F', "Reverb Size", 0.40f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "reverb_predelay", 'F', "Reverb Pre-Delay", 20.0f, 0.0f, 100.0f, 0.1f, 0.0f, "ms", 0,
      noOptions },
    { "reverb_damp", 'F', "Reverb Damping", 0.45f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "reverb_width", 'F', "Reverb Width", 1.0f, 0.0f, 2.0f, 0.001f, 0.0f, "x", 0, noOptions },
    { "reverb_lowcut", 'F', "Reverb Low Cut", 120.0f, 20.0f, 500.0f, 1.0f, 0.0f, "Hz", 0,
      noOptions },

    // -- addColourControls ------------------------------------------------------
    { "delay_type", 'C', "Delay Type", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 3, delayTypeOptions },
    { "distortion", 'F', "Distortion", 0.0f, 0.0f, 1.0f, 0.001f, 0.45f, "%", 0, noOptions },
    { "modern_mode", 'B', "Modern", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 0, noOptions },
    { "lofi_mode", 'B', "Lo-Fi", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 0, noOptions },

    // -- addVinylControls: the record-playing end -------------------------------
    { "vinyl", 'F', "Vinyl", 0.0f, 0.0f, 1.0f, 0.001f, 0.45f, "%", 0, noOptions },
    { "vinyl_crackle", 'F', "Crackle", 0.5f, 0.0f, 1.0f, 0.001f, 0.45f, "%", 0, noOptions },
    { "vinyl_rumble", 'F', "Rumble", 0.35f, 0.0f, 1.0f, 0.001f, 0.45f, "%", 0, noOptions },
    { "vinyl_speed", 'C', "Vinyl Speed", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 3, vinylSpeedOptions },
    { "vinyl_dust", 'F', "Dust", 0.0f, 0.0f, 1.0f, 0.001f, 0.45f, "%", 0, noOptions },
    { "vinyl_scratch", 'F', "Scratch", 0.0f, 0.0f, 1.0f, 0.001f, 0.45f, "%", 0, noOptions },
    { "vinyl_warp", 'F', "Warp", 0.0f, 0.0f, 1.0f, 0.001f, 0.45f, "%", 0, noOptions },
    { "vinyl_electrical", 'F', "Electrical", 0.0f, 0.0f, 1.0f, 0.001f, 0.45f, "%", 0, noOptions },
    { "vinyl_clicks", 'F', "Clicks", 0.0f, 0.0f, 1.0f, 0.001f, 0.45f, "%", 0, noOptions },
    { "vinyl_generation", 'C', "Generation", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 4,
      generationOptions },
    { "vinyl_turntable", 'C', "Turntable", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 4,
      turntableOptions },
    { "vinyl_cartridge", 'C', "Cartridge", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 4,
      cartridgeOptions },

    // -- addTypeSwitchControls: the five model lists ----------------------------
    { "valve_type", 'C', "Valve Type", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 7, noOptions },
    { "amp_type", 'C', "Amp Type", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 7, noOptions },
    { "transformer_type", 'C', "Transformer Type", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 6, noOptions },
    { "digital_type", 'C', "Digital Type", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 6, noOptions },
    { "vinyl_type", 'C', "Vinyl Type", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 7, noOptions },

    // -- addStereoLinkControl ---------------------------------------------------
    { "st_link", 'F', "ST Link", 1.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },

    // -- addDelaySyncControls ---------------------------------------------------
    { "delay_sync", 'B', "Delay Sync", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 0, noOptions },
    { "delay_rate", 'C', "Delay Rate", 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, "", 8, delayRateOptions },

    // -- addTransientControls ---------------------------------------------------
    { "transient_attack", 'F', "Transient Attack", 0.0f, -1.0f, 1.0f, 0.001f, 0.0f, "", 0,
      noOptions },
    { "transient_sustain", 'F', "Transient Sustain", 0.0f, -1.0f, 1.0f, 0.001f, 0.0f, "", 0,
      noOptions },
    { "transient_mix", 'F', "Transient Mix", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },

    // -- addModelMixControls ----------------------------------------------------
    { "neural_mix", 'F', "Neural Mix", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "ir_mix", 'F', "IR Mix", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },

    // -- addEffectsRackControls: one control per stage --------------------------
    { "fx_wah", 'F', "Wah", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "fx_anti_spike", 'F', "Anti-Spike", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "fx_gate", 'F', "Gate", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "fx_overdrive", 'F', "Overdrive", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "fx_fuzz", 'F', "Fuzz", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "fx_wavefolder", 'F', "Wavefolder", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "fx_bitcrusher", 'F', "Bitcrusher", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "fx_rate_crusher", 'F', "Rate Crusher", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "fx_ring_mod", 'F', "Ring Mod", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "fx_octaver", 'F', "Octaver", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "fx_pitch", 'F', "Pitch Shift", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "fx_comb", 'F', "Comb", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "fx_formant", 'F', "Formant", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "fx_dynamic_eq", 'F', "Dynamic EQ", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "fx_de_esser", 'F', "De-Esser", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "fx_chorus", 'F', "Chorus", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "fx_flanger", 'F', "Flanger", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "fx_phaser", 'F', "Phaser", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "fx_tremolo", 'F', "Tremolo", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "fx_vibrato", 'F', "Vibrato", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "fx_rotary", 'F', "Rotary", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "fx_vocoder", 'F', "Vocoder", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "fx_granular", 'F', "Granular", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "fx_spectral", 'F', "Spectral Gate", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "fx_stutter", 'F', "Stutter", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "fx_speed", 'F', "Speed", 0.5f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
    { "comp_upward", 'F', "Upward", 0.0f, 0.0f, 1.0f, 0.001f, 0.50f, "%", 0, noOptions },
};

constexpr int uniqueExpectationCount =
    static_cast<int> (sizeof (uniqueExpectations) / sizeof (uniqueExpectations[0]));

/** What one parameter's PASS line summarises. */
std::string summaryOf (const Expect& row)
{
    std::string summary;

    if (row.kind == 'F')
    {
        summary = "float  default " + std::to_string (row.def)
                  + "  [" + std::to_string (row.start) + ".." + std::to_string (row.end) + "]"
                  + " step " + std::to_string (row.step)
                  + "  taper " + std::to_string (row.taper)
                  + "  \"" + row.unit + "\"";
    }
    else if (row.kind == 'B')
    {
        summary = std::string ("bool   default ") + (row.def > 0.5f ? "on" : "off");
    }
    else
    {
        summary = "choice default " + std::to_string (static_cast<int> (row.def))
                  + "/" + std::to_string (row.options);

        if (row.list == nullptr)
        {
            summary += "  (model list in the header)";
        }
        else
        {
            summary += "  {";
            for (int index = 0; index < row.options; ++index)
                summary += (index == 0 ? "" : ", ") + std::string (row.list[index]);
            summary += "}";
        }
    }

    return summary;
}

/** The row's own question, asked of the parameter the row names. */
void checkOne (const Expect& row, const juce::RangedAudioParameter& parameter)
{
    const std::string id = row.id;

    check (parameter.getName().toStdString() == row.name,
           id + ": name is \"" + parameter.getName().toStdString()
               + "\", the unique test pins \"" + row.name + "\"");

    if (row.kind == 'F')
    {
        const auto* floating = dynamic_cast<const juce::AudioParameterFloat*> (&parameter);

        if (floating == nullptr)
        {
            check (false, id + ": the table no longer builds a float for this parameter");
            return;
        }

        const auto& range = floating->getRange();

        check (range.getStart() == row.start,
               id + ": range start is " + std::to_string (range.getStart())
                   + ", the unique test pins " + std::to_string (row.start));

        check (range.getEnd() == row.end,
               id + ": range end is " + std::to_string (range.getEnd())
                   + ", the unique test pins " + std::to_string (row.end));

        check (range.interval == row.step,
               id + ": step is " + std::to_string (range.interval)
                   + ", the unique test pins " + std::to_string (row.step));

        check (range.centrePoint == row.taper,
               id + ": knob taper centre is " + std::to_string (range.centrePoint)
                   + ", the unique test pins " + std::to_string (row.taper));

        check (floating->getAttributes().label().toStdString() == row.unit,
               id + ": unit label is \"" + floating->getAttributes().label().toStdString()
                   + "\", the unique test pins \"" + row.unit + "\"");

        // The stored default against the same snap the parameter's constructor
        // runs on the stated number - exact equality, same code path.
        const float expectedDefault = range.snapToLegalValue (row.def);

        check (floating->getDefaultValue() == expectedDefault,
               id + ": default is " + std::to_string (floating->getDefaultValue())
                   + ", the unique test pins " + std::to_string (row.def)
                   + " (snaps to " + std::to_string (expectedDefault) + ")");
    }
    else if (row.kind == 'B')
    {
        if (dynamic_cast<const juce::AudioParameterBool*> (&parameter) == nullptr)
        {
            check (false, id + ": the table no longer builds a bool for this parameter");
            return;
        }

        check (parameter.getDefaultValue() == row.def,
               id + ": default is " + std::to_string (parameter.getDefaultValue())
                   + ", the unique test pins " + std::to_string (row.def));

        auto* mutableParameter = const_cast<juce::RangedAudioParameter*> (&parameter);
        mutableParameter->setValue (1.0f);
        check (mutableParameter->getValue() == 1.0f, id + ": on did not read back as 1");
        mutableParameter->setValue (0.0f);
        check (mutableParameter->getValue() == 0.0f, id + ": off did not read back as 0");
        mutableParameter->setValue (row.def);
    }
    else if (row.kind == 'C')
    {
        const auto* choice = dynamic_cast<const juce::AudioParameterChoice*> (&parameter);

        if (choice == nullptr)
        {
            check (false, id + ": the table no longer builds a choice for this parameter");
            return;
        }

        check (choice->getNumChoices() == row.options,
               id + ": has " + std::to_string (choice->getNumChoices())
                   + " options, the unique test pins " + std::to_string (row.options));

        check (parameter.getDefaultValue() == row.def,
               id + ": default index is "
                   + std::to_string (static_cast<int> (parameter.getDefaultValue()))
                   + ", the unique test pins " + std::to_string (static_cast<int> (row.def)));

        if (row.list == nullptr)
        {
            // A model list arrives through the extracted table; its names are
            // single-sourced elsewhere, so only their presence is asked here.
            for (int index = 0; index < row.options; ++index)
                check (! choice->getChoiceName (index).isEmpty(),
                       id + ": option " + std::to_string (index) + " has no name");
        }
        else
        {
            for (int index = 0; index < row.options; ++index)
                check (choice->getChoiceName (index).toStdString() == row.list[index],
                       id + ": option " + std::to_string (index) + " is \""
                           + choice->getChoiceName (index).toStdString()
                           + "\", the unique test pins \"" + row.list[index] + "\"");
        }

        auto* mutableParameter = const_cast<juce::RangedAudioParameter*> (&parameter);
        for (int index = 0; index < row.options; ++index)
        {
            mutableParameter->setValue (static_cast<float> (index));
            check (mutableParameter->getValue() == static_cast<float> (index),
                   id + ": selecting option " + std::to_string (index) + " read back as "
                       + std::to_string (mutableParameter->getValue()));
        }
        mutableParameter->setValue (row.def);
    }
}

} // namespace

int main()
{
    FirstAudioProcessor processor;
    const auto count = processor.parameters.getNumParameters();

    std::printf ("unique parameter tests: %d rows against the shipping table\n",
                 uniqueExpectationCount);

    check (count == expectedParameterCount,
           "the shipping table builds " + std::to_string (count)
               + " parameters, but the source counts " + std::to_string (expectedParameterCount));

    check (uniqueExpectationCount == count,
           "there are " + std::to_string (uniqueExpectationCount) + " unique test rows for "
               + std::to_string (count) + " registered parameters - every parameter needs "
               "exactly one, and no row may outlive its parameter");

    // The order IS the table: row i must be parameter i. A group whose call
    // moved is a different parameter index space for the host.
    for (int index = 0; index < uniqueExpectationCount && index < count; ++index)
    {
        const auto* parameter = processor.parameters.getParameterAt (index);

        if (parameter == nullptr)
        {
            check (false, "parameter " + std::to_string (index) + " is null");
            continue;
        }

        check (idOf (*parameter) == uniqueExpectations[index].id,
               "table position " + std::to_string (index) + " is \"" + idOf (*parameter)
                   + "\", the unique test pins \"" + uniqueExpectations[index].id + "\"");
    }

    // Every row against its parameter, by id - which also refuses a row whose
    // parameter no longer exists.
    std::map<std::string, int> rowsPerParameter;
    std::map<std::string, const juce::RangedAudioParameter*> byId;

    for (int index = 0; index < count; ++index)
    {
        const auto* parameter = processor.parameters.getParameterAt (index);

        if (parameter != nullptr)
            byId[idOf (*parameter)] = parameter;
    }

    int passed = 0;

    for (const auto& row : uniqueExpectations)
    {
        const std::string id = row.id;
        ++rowsPerParameter[id];

        const auto found = byId.find (id);

        if (found == byId.end())
        {
            check (false, "unique test row \"" + id + "\" matches no registered parameter");
            std::printf ("FAIL       %-18s (no such parameter)\n", id.c_str());
            continue;
        }

        const int before = failures;
        checkOne (row, *found->second);

        if (failures == before)
        {
            ++passed;
            std::printf ("PASS  %3d  %-18s  %s\n", passed, id.c_str(),
                         summaryOf (row).c_str());
        }
        else
        {
            std::printf ("FAIL       %-18s  (see the failures above)\n", id.c_str());
        }
    }

    // And every parameter has a row - a parameter added without its own unique
    // test stops the harness here, by name.
    for (int index = 0; index < count; ++index)
    {
        const auto* parameter = processor.parameters.getParameterAt (index);

        if (parameter == nullptr)
            continue;

        const auto id = idOf (*parameter);
        const auto rows = rowsPerParameter.find (id);

        if (rows == rowsPerParameter.end())
            check (false, "parameter \"" + id + "\" has no unique test");

        if (rows != rowsPerParameter.end() && rows->second > 1)
            check (false, "parameter \"" + id + "\" has " + std::to_string (rows->second)
                              + " unique test rows");
    }

    if (failures == 0)
        std::printf ("unique parameter tests: every one of the %d parameters has its own "
                     "unique, passing test\n", count);

    std::printf ("%s (%d failure%s across %d tests)\n",
                 failures == 0 ? "UNIQUE PARAMETER TESTS PASSED" : "UNIQUE PARAMETER TESTS FAILED",
                 failures, failures == 1 ? "" : "s", uniqueExpectationCount);

    return failures == 0 ? 0 : 1;
}
