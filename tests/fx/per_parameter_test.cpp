// =============================================================================
//  Every rack control, one at a time, on rendered signal.
//
//    tests/fx/run.sh          (this file is one of the two it builds)
//
//  fx_test.cpp asks whether each STAGE does the thing its name promises. This
//  file asks the question the panel's own report asked instead - "the output
//  sounds wrong" - and asks it once per CONTROL:
//
//    1. NEUTRAL. Every amount at its neutral value, through the shipping rack,
//       must give back the samples it was handed, bit for bit. A "zero is out of
//       the path" contract that is off by one stage state is inaudible in a diff
//       and obvious here.
//    2. ALONE. With one control moved, the output must CHANGE - a stage that is
//       wired to its parameter but does nothing is silent in a review and
//       audible as "the knob does nothing".
//    3. BOUNDED. At every step of the sweep the output must stay finite and
//       inside a ceiling. This is the one that catches the "very strange sound"
//       class of defect: a stage whose gain runs away at an intermediate amount,
//       or one that emits a NaN and silences the host's whole bus.
//    4. AUDIBLE. At full amount the stage must not drop the signal into
//       inaudibility either. A control that mutes is as wrong as one that
//       explodes, and both read as the same complaint.
//
//  No JUCE, no extractor, no shim: the headers this includes name nothing of the
//  toolkit, so this is the same code the plugin links.
// =============================================================================
#include "DSP/FxRack.h"
#include "DSP/Fx/FxDynamics.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int frames = 9600;              // 200 ms: long enough for the 15 ms
                                              // smoothing to arrive and for the
                                              // comb and the pitch stages to fill
    int failures = 0;

    void check (bool condition, const std::string& what)
    {
        if (! condition)
        {
            std::printf ("FAIL  %s\n", what.c_str());
            ++failures;
        }
    }

    std::vector<float> tone (float hertz, float amplitude, int length = frames)
    {
        std::vector<float> signal (static_cast<std::size_t> (length));

        for (int i = 0; i < length; ++i)
            signal[static_cast<std::size_t> (i)] =
                amplitude * std::sin (6.28318530718f * hertz * static_cast<float> (i)
                                      / static_cast<float> (sampleRate));

        return signal;
    }

    struct Result
    {
        bool finite = true;
        float peak = 0.0f;
        double rms = 0.0;
        double worstDifference = 0.0;
    };

    /** One pass of the shipping rack over one channel pair. */
    Result runRack (const j37dsp::FxTargets& targets, const std::vector<float>& input)
    {
        j37dsp::FxRack rack;
        rack.prepare (sampleRate);
        rack.setTargets (targets);

        Result result;
        double sum = 0.0;

        for (std::size_t i = 0; i < input.size(); ++i)
        {
            float left = input[i];
            float right = input[i];

            // isActive() is the engine's own gate: the signal path does not enter
            // the rack at all when it says no. Asking it here keeps the harness on
            // the same side of that decision the plugin is on.
            if (rack.isActive())
                rack.processSample (left, right);

            if (! std::isfinite (left) || ! std::isfinite (right))
                result.finite = false;

            result.peak = std::max (result.peak, std::abs (left));
            result.worstDifference = std::max (result.worstDifference,
                                               static_cast<double> (std::abs (left - input[i])));
            sum += static_cast<double> (left) * static_cast<double> (left);
        }

        result.rms = std::sqrt (sum / static_cast<double> (input.size()));
        return result;
    }

    double rmsOf (const std::vector<float>& signal)
    {
        double sum = 0.0;

        for (const auto sample : signal)
            sum += static_cast<double> (sample) * static_cast<double> (sample);

        return std::sqrt (sum / static_cast<double> (signal.size()));
    }

    struct Control
    {
        const char* name;
        float j37dsp::FxTargets::* member;
        float neutral;
    };

    // The twenty-five amounts and SPEED, in the order the rack runs them. The
    // neutral column is the contract from Source/DSP/FxRack.h: every amount is out
    // of the path at zero, and SPEED is out of the path at its middle.
    const Control controls[] = {
        { "fx_wah", &j37dsp::FxTargets::wah, 0.0f },
        { "fx_anti_spike", &j37dsp::FxTargets::antiSpike, 0.0f },
        { "fx_gate", &j37dsp::FxTargets::gate, 0.0f },
        { "fx_overdrive", &j37dsp::FxTargets::overdrive, 0.0f },
        { "fx_fuzz", &j37dsp::FxTargets::fuzz, 0.0f },
        { "fx_wavefolder", &j37dsp::FxTargets::wavefolder, 0.0f },
        { "fx_bitcrusher", &j37dsp::FxTargets::bitcrusher, 0.0f },
        { "fx_rate_crusher", &j37dsp::FxTargets::rateCrusher, 0.0f },
        { "fx_ring_mod", &j37dsp::FxTargets::ringMod, 0.0f },
        { "fx_octaver", &j37dsp::FxTargets::octaver, 0.0f },
        { "fx_pitch", &j37dsp::FxTargets::pitch, 0.0f },
        { "fx_comb", &j37dsp::FxTargets::comb, 0.0f },
        { "fx_formant", &j37dsp::FxTargets::formant, 0.0f },
        { "fx_dynamic_eq", &j37dsp::FxTargets::dynamicEq, 0.0f },
        { "fx_de_esser", &j37dsp::FxTargets::deEsser, 0.0f },
        { "fx_chorus", &j37dsp::FxTargets::chorus, 0.0f },
        { "fx_flanger", &j37dsp::FxTargets::flanger, 0.0f },
        { "fx_phaser", &j37dsp::FxTargets::phaser, 0.0f },
        { "fx_tremolo", &j37dsp::FxTargets::tremolo, 0.0f },
        { "fx_vibrato", &j37dsp::FxTargets::vibrato, 0.0f },
        { "fx_rotary", &j37dsp::FxTargets::rotary, 0.0f },
        { "fx_vocoder", &j37dsp::FxTargets::vocoder, 0.0f },
        { "fx_granular", &j37dsp::FxTargets::granular, 0.0f },
        { "fx_spectral", &j37dsp::FxTargets::spectral, 0.0f },
        { "fx_speed", &j37dsp::FxTargets::speed, 0.5f },
    };

    constexpr float peakCeiling = 8.0f;        // +18 dBFS: nothing here is a gain
                                              // stage that may exceed it, and a
                                              // runaway is far past it
    constexpr double audibleFloor = 0.02;      // -34 dB of the input: a stage that
                                              // drops the signal below this is
                                              // muting it, not processing it
    constexpr double changeFloor = 1.0e-4;     // one part in ten thousand: below it
                                              // the control made no audible change
} // namespace

int main()
{
    const auto input = tone (440.0f, 0.5f);
    const auto inputRms = rmsOf (input);

    // ------------------------------------------------------------------
    //  1. The neutral rack is a pass-through.
    // ------------------------------------------------------------------
    {
        j37dsp::FxTargets neutral;
        const auto result = runRack (neutral, input);

        check (result.worstDifference == 0.0,
               "a fully neutral rack must be bit-exact, worst difference was "
                   + std::to_string (result.worstDifference));
    }

    // ------------------------------------------------------------------
    //  2-4. Each control, alone, at 0.05 steps.
    // ------------------------------------------------------------------
    for (const auto& control : controls)
    {
        const auto label = std::string (control.name);

        // SPEED is a RATE rather than an amount: at twice the tape's speed the
        // read head sits 0.75 s ahead of the write head, so the buffer has to be
        // fed that long before there is anything to read. Its own audition
        // therefore runs on a second of signal; the rest of the rack is measured
        // on the 200 ms tone.
        const auto isSpeed = std::string (control.name) == "fx_speed";
        const auto fullInput = isSpeed ? tone (440.0f, 0.5f, 72000) : input;

        // ALONE: full amount, everything else neutral.
        j37dsp::FxTargets full;
        full.*control.member = 1.0f;
        const auto atFull = runRack (full, fullInput);

        check (atFull.worstDifference > changeFloor,
               label + " at full amount did not change the signal at all");
        check (atFull.finite, label + " at full amount produced a non-finite sample");
        check (atFull.peak <= peakCeiling,
               label + " at full amount peaked at " + std::to_string (atFull.peak)
                   + ", above the " + std::to_string (peakCeiling) + " ceiling");
        check (atFull.rms >= audibleFloor * rmsOf (fullInput),
               label + " at full amount dropped the signal below the audible floor ("
                   + std::to_string (atFull.rms) + " RMS)");

        // BOUNDED: the same claims at every step of the control's travel. An
        // intermediate amount is where a gain law, a feedback coefficient or a
        // filter's Q is most likely to be out of its own range.
        for (int step = 0; step <= 10; ++step)
        {
            j37dsp::FxTargets swept;
            swept.*control.member = static_cast<float> (step) / 10.0f;
            const auto result = runRack (swept, input);

            check (result.finite, label + " at " + std::to_string (swept.*control.member)
                                      + " produced a non-finite sample");
            check (result.peak <= peakCeiling,
                   label + " at " + std::to_string (swept.*control.member) + " peaked at "
                       + std::to_string (result.peak) + ", above the ceiling");

            // NEUTRAL: the neutral value of every control is the pass-through one.
            if (std::abs (swept.*control.member - control.neutral) < 1.0e-6f)
                check (result.worstDifference == 0.0,
                       label + " at its neutral " + std::to_string (control.neutral)
                           + " is not bit-exact (worst difference "
                           + std::to_string (result.worstDifference) + ")");
        }

        std::printf ("%-18s full: peak %.3f  rms %.4f (input %.4f)\n",
                     control.name, atFull.peak, atFull.rms, inputRms);
    }

    // ------------------------------------------------------------------
    //  5. The compressor's upward stage, which is not in the rack.
    // ------------------------------------------------------------------
    {
        j37dsp::fx::MultibandCompressor compressor;
        compressor.prepare (sampleRate);

        const auto quiet = tone (440.0f, 0.01f);
        j37dsp::fx::MultibandCompressor::Settings settings;
        settings.mix = 1.0f;
        settings.upward = 0.0f;

        auto withoutUpward = quiet;
        auto withUpward = quiet;

        for (std::size_t i = 0; i < quiet.size(); ++i)
        {
            float left = quiet[i];
            float right = quiet[i];
            compressor.process (left, right, settings);
            withoutUpward[i] = left;
        }

        compressor.reset();
        settings.upward = 1.0f;

        for (std::size_t i = 0; i < quiet.size(); ++i)
        {
            float left = quiet[i];
            float right = quiet[i];
            compressor.process (left, right, settings);
            withUpward[i] = left;
        }

        const auto before = rmsOf (withoutUpward);
        const auto after = rmsOf (withUpward);

        check (std::isfinite (after), "comp_upward produced a non-finite sample");
        check (after > before * 1.05,
               "comp_upward at full did not lift the quiet end ("
                   + std::to_string (before) + " -> " + std::to_string (after) + ")");
        std::printf ("comp_upward        rms %.5f -> %.5f\n", before, after);
    }

    if (failures != 0)
    {
        std::printf ("per-parameter: %d failure(s)\n", failures);
        return 1;
    }

    std::printf ("per-parameter: every rack control neutral-silent, audible, bounded\n");
    return 0;
}
