// =============================================================================
//  Three stages, measured against the way they were reported.
//
//    tests/fx/run.sh          (this file is one of the three it builds)
//
//  fx_test.cpp asks whether a stage is wired and per_parameter_test.cpp asks
//  whether its knob can be turned anywhere safely. Neither could see the three
//  defects this file exists for, because all three pass "audible at full amount"
//  on a -6 dBFS sine and still sound wrong in a session:
//
//    1. FUZZ at half travel and above was a HALF-WAVE RECTIFIER. The bias was
//       scaled by the pre-gain, so 0.12 * 61 = 7.3 sat under every sample, the
//       positive half arrived already past the tanh knee, and the stage returned
//       a thin octave-up buzz 6 dB below its input. What is checked here is that
//       both halves of the waveform survive, that the stage does not put DC on the
//       bus, and that full amount is not a level drop.
//    2. BITS at full amount MUTED everything quieter than -12 dBFS: a full-scale
//       grid at 2 bits is a step of 0.5, so a -20 dBFS programme rounded to zero.
//       The grid is normalised to the programme's own peak now, and what is
//       checked here is exactly the case that failed - a quiet signal - plus the
//       fact that the grid is still coarse in one block, because a smoother
//       quantiser that follows the signal quantises nothing.
//    3. VOCODER was "just noise, unrelated to the sound": eight bands at a Q of 4
//       left gaps between them, the makeup gain lifted what leaked through, and
//       the output's brightness barely moved with the input's. What is checked
//       here is that a 2 kHz input comes out brighter than a 200 Hz one, that the
//       stage does not raise the level, and that it goes quiet when the input does.
//
//  No JUCE: the headers this includes name nothing of the toolkit.
// =============================================================================
#include "DSP/Fx/FxDrive.h"
#include "DSP/Fx/FxPitch.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
    constexpr double sampleRate = 48000.0;
    int failures = 0;

    void check (bool condition, const std::string& what)
    {
        if (! condition)
        {
            std::printf ("FAIL  %s\n", what.c_str());
            ++failures;
        }
    }

    std::vector<float> tone (float hertz, float amplitude, int length)
    {
        std::vector<float> signal (static_cast<std::size_t> (length));

        for (int i = 0; i < length; ++i)
            signal[static_cast<std::size_t> (i)] =
                amplitude * std::sin (6.28318530718f * hertz * static_cast<float> (i)
                                      / static_cast<float> (sampleRate));

        return signal;
    }

    double rmsOf (const std::vector<float>& signal)
    {
        double sum = 0.0;

        for (const auto sample : signal)
            sum += static_cast<double> (sample) * static_cast<double> (sample);

        return std::sqrt (sum / static_cast<double> (signal.size()));
    }

    /** One-pole high-pass, used only to ask "is there treble in this". */
    double brightnessOf (const std::vector<float>& signal)
    {
        constexpr float coefficient = 0.18f;
        float state = 0.0f, previous = 0.0f;
        double sum = 0.0;

        for (const auto sample : signal)
        {
            state = coefficient * (state + sample - previous);
            previous = sample;
            sum += static_cast<double> (state) * static_cast<double> (state);
        }

        return std::sqrt (sum / static_cast<double> (signal.size()));
    }

    struct FuzzResult
    {
        double rms = 0.0;
        float positivePeak = 0.0f;
        float negativePeak = 0.0f;
        double dc = 0.0;
        bool finite = true;
    };

    FuzzResult runFuzz (float hertz, float amplitude, float amount, int length)
    {
        j37dsp::fx::Fuzz fuzz;
        fuzz.prepare (sampleRate);

        const auto input = tone (hertz, amplitude, length);
        FuzzResult result;
        std::vector<float> output (input.size());
        double sum = 0.0;

        for (std::size_t i = 0; i < input.size(); ++i)
        {
            float left = input[i], right = input[i];
            fuzz.process (left, right, amount);

            if (! std::isfinite (left))
                result.finite = false;

            output[i] = left;
            result.positivePeak = std::max (result.positivePeak, left);
            result.negativePeak = std::min (result.negativePeak, left);
            sum += static_cast<double> (left);
        }

        result.rms = rmsOf (output);
        result.dc = sum / static_cast<double> (input.size());
        return result;
    }

    std::vector<float> runBitcrusher (const std::vector<float>& input, float amount)
    {
        j37dsp::fx::Bitcrusher crusher;
        crusher.prepare (sampleRate);

        std::vector<float> output (input.size());

        for (std::size_t i = 0; i < input.size(); ++i)
        {
            float left = input[i], right = input[i];
            crusher.process (left, right, amount);
            output[i] = left;
        }

        return output;
    }

    std::vector<float> runVocoder (const std::vector<float>& input, float amount)
    {
        j37dsp::fx::Vocoder vocoder;
        vocoder.prepare (sampleRate);

        std::vector<float> output (input.size());

        for (std::size_t i = 0; i < input.size(); ++i)
        {
            float left = input[i], right = input[i];
            vocoder.process (left, right, amount);
            output[i] = left;
        }

        return output;
    }

    // ------------------------------------------------------------------
    //  1. FUZZ
    // ------------------------------------------------------------------
    void checkFuzz()
    {
        const auto inputRms = rmsOf (tone (440.0f, 0.5f, 9600));

        for (const auto amount : { 0.25f, 0.5f, 0.75f, 1.0f })
        {
            const auto label = "fuzz at " + std::to_string (amount);
            const auto result = runFuzz (440.0f, 0.5f, amount, 9600);

            check (result.finite, label + " produced a non-finite sample");

            // The defect: at half travel and above the positive peak was 0.000 -
            // the stage was rectifying. A fuzz is asymmetric, so the peaks will
            // never match, but the positive half has to be more than a rounding
            // error of the negative one.
            check (result.positivePeak >= 0.5f * std::abs (result.negativePeak),
                   label + " rectified the waveform: positive peak "
                       + std::to_string (result.positivePeak) + " against a negative peak of "
                       + std::to_string (result.negativePeak));

            check (std::abs (result.dc) <= 0.02,
                   label + " put " + std::to_string (result.dc) + " of DC on the bus");

            // "Поганий/не чути": it also came back 6 dB down. A drive stage may
            // compress and may lift; it may not hand back a third of the level.
            check (result.rms >= 0.6 * inputRms && result.rms <= 1.6 * inputRms,
                   label + " returned " + std::to_string (result.rms)
                       + " RMS for an input of " + std::to_string (inputRms));

            std::printf ("fuzz %.2f: rms %.4f  pos %.3f  neg %.3f  dc %+.4f\n",
                         static_cast<double> (amount), result.rms,
                         static_cast<double> (result.positivePeak),
                         static_cast<double> (result.negativePeak), result.dc);
        }

        // The register the report named: "the high octaves are the bad ones".
        const auto highInput = tone (4000.0f, 0.5f, 9600);
        const auto high = runFuzz (4000.0f, 0.5f, 1.0f, 9600);
        check (high.finite, "fuzz at 4 kHz produced a non-finite sample");
        check (high.rms >= 0.6 * rmsOf (highInput),
               "fuzz at 4 kHz returned " + std::to_string (high.rms)
                   + " RMS for an input of " + std::to_string (rmsOf (highInput)));
        check (high.positivePeak >= 0.5f * std::abs (high.negativePeak),
               "fuzz at 4 kHz rectified the waveform");
    }

    // ------------------------------------------------------------------
    //  2. BITS
    // ------------------------------------------------------------------
    void checkBits()
    {
        // Four levels, the quietest of them the one that was silent before:
        // -34 dBFS. Every one must come back at its own level.
        for (const auto level : { 0.5f, 0.25f, 0.1f, 0.02f })
        {
            const auto input = tone (440.0f, level, 9600);
            const auto output = runBitcrusher (input, 1.0f);
            const auto ratio = rmsOf (output) / std::max (1.0e-9, rmsOf (input));

            check (ratio >= 0.6 && ratio <= 1.6,
                   "bits at full amount returned " + std::to_string (100.0 * ratio)
                       + "% of a " + std::to_string (level) + "-peak input");
        }

        // ... and it must still be a quantiser. One block of a full-level tone has
        // to hold a handful of magnitudes, not every sample's own value: a grid
        // that chases the signal rounds each sample against a different scale and
        // is indistinguishable from a bypass.
        {
            const auto input = tone (440.0f, 0.5f, 4800);
            j37dsp::fx::Bitcrusher crusher;
            crusher.prepare (sampleRate);
            std::vector<float> block;

            for (std::size_t i = 0; i < input.size(); ++i)
            {
                float left = input[i], right = input[i];
                crusher.process (left, right, 1.0f);

                // After the held peak has settled, one block's worth of magnitudes.
                if (i >= 2048 && i < 2048 + 129)
                    block.push_back (std::abs (left));
            }

            std::sort (block.begin(), block.end());
            block.erase (std::unique (block.begin(), block.end(),
                                      [] (float a, float b)
                                      { return std::abs (a - b) < 1.0e-6f; }),
                         block.end());

            check (block.size() >= 3 && block.size() <= 24,
                   "bits at full amount put " + std::to_string (block.size())
                       + " levels in one block of a sine, so the grid is "
                         + (block.size() < 3 ? "not crushing" : "not coarser than 4 bits"));
        }
    }

    // ------------------------------------------------------------------
    //  3. VOCODER
    // ------------------------------------------------------------------
    void checkVocoder()
    {
        const auto lowInput = tone (200.0f, 0.3f, 12000);
        const auto highInput = tone (2000.0f, 0.3f, 12000);
        const auto low = runVocoder (lowInput, 1.0f);
        const auto high = runVocoder (highInput, 1.0f);

        // "No relation to the sound": the output's brightness has to move with the
        // input's. Before the bands overlapped the two were within a few dB.
        const auto lowBrightness = brightnessOf (low);
        const auto highBrightness = brightnessOf (high);

        check (highBrightness >= 4.0 * std::max (1.0e-9, lowBrightness),
               "the vocoder's output barely follows the input's brightness: "
                   + std::to_string (highBrightness) + " at 2 kHz against "
                   + std::to_string (lowBrightness) + " at 200 Hz");

        // It is not a gain stage either: what went in must come back in the same
        // range, or the knob reads as "more noise".
        for (const auto* pair : { &low, &high })
        {
            const auto inputRms = rmsOf (*pair == low ? lowInput : highInput);
            const auto ratio = rmsOf (*pair) / std::max (1.0e-9, inputRms);
            check (ratio >= 0.3 && ratio <= 3.0,
                   "the vocoder returned " + std::to_string (100.0 * ratio)
                       + "% of its input's level");
        }

        check (rmsOf (high) <= 3.0 * rmsOf (highInput),
               "the vocoder lifted a 2 kHz input to " + std::to_string (rmsOf (high))
                   + " RMS");

        // Silence in, silence out: the carrier must be shaped by the input's
        // envelopes and not heard on its own.
        {
            const auto input = tone (440.0f, 0.5f, 24000);
            j37dsp::fx::Vocoder vocoder;
            vocoder.prepare (sampleRate);
            double tail = 0.0;

            for (int i = 0; i < 33600; ++i)
            {
                float left = i < 24000 ? input[static_cast<std::size_t> (i)] : 0.0f;
                float right = left;
                vocoder.process (left, right, 1.0f);

                if (i >= 32640)         // the last 20 ms of 200 ms of silence
                    tail += static_cast<double> (left) * static_cast<double> (left);
            }

            check (std::sqrt (tail / 960.0) <= 0.01,
                   "the vocoder's carrier is audible with no input: "
                       + std::to_string (std::sqrt (tail / 960.0)) + " RMS after 200 ms of silence");
        }
    }
} // namespace

int main()
{
    checkFuzz();
    checkBits();
    checkVocoder();

    if (failures == 0)
        std::printf ("character: fuzz keeps both halves, bits keeps quiet material, "
                     "the vocoder follows the input\n");

    std::printf ("%s (%d failure%s)\n", failures == 0 ? "CHARACTER PASSED" : "CHARACTER FAILED",
                 failures, failures == 1 ? "" : "s");

    return failures == 0 ? 0 : 1;
}
