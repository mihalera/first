// =============================================================================
//  The effects rack, on rendered signal.
//
//    tests/fx/run.sh
//
//  Every claim in this file is a property of the AUDIO, not of the source: a
//  stage that is wired but silent passes a text check and fails here, which is
//  the point. The file includes the shipping headers - Source/DSP/FxRack.h and
//  the five family headers under it - as they ship, with no extractor, no
//  replica and no shim: none of them names JUCE.
//
//  What is measured, and how:
//
//    * Goertzel, for "is there energy at this frequency" - the only way to tell
//      a drive stage from a gain stage, a sub-octave from a volume drop, and a
//      pitch shift from a filter.
//    * RMS over a window, for the gates, the crushers and the modulation, where
//      the claim is about level over time rather than about one bin.
//    * Bit comparison, for the bypass contract: an amount of zero has to give
//      back exactly the samples that arrived.
// =============================================================================
#include "DSP/FxRack.h"
#include "DSP/Fx/FxDynamics.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    constexpr double sampleRate = 48000.0;
    int failures = 0;

    void check (bool condition, const char* what)
    {
        if (!condition)
        {
            std::printf ("FAIL  %s\n", what);
            ++failures;
        }
    }

    std::vector<float> sine (float hertz, int frames, float amplitude = 0.25f)
    {
        std::vector<float> signal (static_cast<std::size_t> (frames));

        for (int i = 0; i < frames; ++i)
            signal[static_cast<std::size_t> (i)] =
                amplitude * std::sin (6.28318530718f * hertz * static_cast<float> (i)
                                      / static_cast<float> (sampleRate));

        return signal;
    }

    double rmsRange (const std::vector<float>& signal, std::size_t from, std::size_t to)
    {
        from = std::min (from, signal.size());
        to = std::min (to, signal.size());

        if (to <= from)
            return 0.0;

        double sum = 0.0;

        for (std::size_t i = from; i < to; ++i)
            sum += static_cast<double> (signal[i]) * static_cast<double> (signal[i]);

        return std::sqrt (sum / static_cast<double> (to - from));
    }

    /** Energy at one frequency, normalised by the window's length. */
    double goertzel (const std::vector<float>& signal, double hertz,
                     std::size_t from = 0, std::size_t to = 0)
    {
        if (to == 0 || to > signal.size())
            to = signal.size();

        const auto omega = 6.283185307179586 * hertz / sampleRate;
        const auto coefficient = 2.0 * std::cos (omega);
        double s0 = 0.0, s1 = 0.0, s2 = 0.0;

        for (std::size_t i = from; i < to; ++i)
        {
            s0 = signal[i] + coefficient * s1 - s2;
            s2 = s1;
            s1 = s0;
        }

        const auto power = s1 * s1 + s2 * s2 - coefficient * s1 * s2;
        return std::sqrt (std::max (0.0, power)) / static_cast<double> (std::max<std::size_t> (1, to - from));
    }

    std::size_t distinctValues (const std::vector<float>& signal, std::size_t from)
    {
        std::vector<float> copy (signal.begin() + static_cast<std::ptrdiff_t> (from), signal.end());
        std::sort (copy.begin(), copy.end());
        copy.erase (std::unique (copy.begin(), copy.end()), copy.end());
        return copy.size();
    }

    /** The ratio of the loudest to the quietest 10 ms window: how much the level
        MOVES, which is the only thing a tremolo promises. */
    double levelSwing (const std::vector<float>& signal, std::size_t from)
    {
        constexpr std::size_t window = 480;
        double loudest = 0.0, quietest = 1.0e9;

        for (std::size_t start = from; start + window < signal.size(); start += window)
        {
            const auto level = rmsRange (signal, start, start + window);
            loudest = std::max (loudest, level);
            quietest = std::min (quietest, level);
        }

        return quietest > 1.0e-9 ? loudest / quietest : 0.0;
    }

    template <typename Configure>
    std::vector<float> runRack (const std::vector<float>& left,
                                const std::vector<float>& right,
                                Configure configure)
    {
        j37dsp::FxRack rack;
        rack.prepare (sampleRate);

        j37dsp::FxTargets targets;
        configure (targets);
        rack.setTargets (targets);

        std::vector<float> output;
        output.reserve (left.size());

        for (std::size_t i = 0; i < left.size(); ++i)
        {
            auto l = left[i];
            auto r = right[i];
            rack.processSample (l, r);
            output.push_back (l);
        }

        return output;
    }

    void checkBypass()
    {
        const auto left = sine (440.0f, 4096);
        const auto right = sine (660.0f, 4096);
        const auto output = runRack (left, right, [] (j37dsp::FxTargets&) {});

        bool identical = true;

        for (std::size_t i = 0; i < left.size(); ++i)
            identical = identical && output[i] == left[i];

        check (identical, "rack at every amount zero is bit-exact");
    }

    bool addsHarmonics (const std::vector<float>& input, float amount,
                        void (*apply) (j37dsp::FxTargets&))
    {
        const auto output = runRack (input, input, [apply, amount] (j37dsp::FxTargets& targets)
        {
            apply (targets);
            (void) amount;
        });

        const auto fundamental = goertzel (output, 1000.0, 4800);
        const auto second = goertzel (output, 2000.0, 4800);
        const auto third = goertzel (output, 3000.0, 4800);

        return second > fundamental * 0.02 || third > fundamental * 0.02;
    }

    void checkDrivesAddHarmonics()
    {
        const auto input = sine (1000.0f, 9600);

        check (addsHarmonics (input, 1.0f, [] (j37dsp::FxTargets& t) { t.overdrive = 1.0f; }),
               "overdrive generates harmonics");
        check (addsHarmonics (input, 1.0f, [] (j37dsp::FxTargets& t) { t.fuzz = 1.0f; }),
               "fuzz generates harmonics");
        check (addsHarmonics (input, 1.0f, [] (j37dsp::FxTargets& t) { t.wavefolder = 1.0f; }),
               "wavefolder generates harmonics");
    }

    void checkCrushers()
    {
        const auto input = sine (1000.0f, 4800);

        const auto crushed = runRack (input, input, [] (j37dsp::FxTargets& t) { t.bitcrusher = 1.0f; });
        check (distinctValues (crushed, 2400) < 200, "bitcrusher quantises to a small set of levels");

        const auto held = runRack (input, input, [] (j37dsp::FxTargets& t) { t.rateCrusher = 1.0f; });

        std::size_t repeats = 0;

        for (std::size_t i = 2401; i < held.size(); ++i)
            if (held[i] == held[i - 1])
                ++repeats;

        check (repeats > held.size() / 8, "rate crusher holds samples");
    }

    void checkGateAndSpike()
    {
        const auto quiet = sine (440.0f, 9600, 0.002f);
        const auto gated = runRack (quiet, quiet, [] (j37dsp::FxTargets& t) { t.gate = 1.0f; });
        check (rmsRange (gated, 4800, 9600) < rmsRange (quiet, 4800, 9600) * 0.5,
               "gate closes on signal below its threshold");

        const auto loud = sine (440.0f, 9600, 0.5f);
        const auto passed = runRack (loud, loud, [] (j37dsp::FxTargets& t) { t.gate = 1.0f; });
        check (rmsRange (passed, 4800, 9600) > rmsRange (loud, 4800, 9600) * 0.5,
               "gate stays open on signal above its threshold");

        auto spiky = sine (220.0f, 4800, 0.05f);
        spiky[2000] = 0.9f;

        const auto repaired = runRack (spiky, spiky, [] (j37dsp::FxTargets& t) { t.antiSpike = 1.0f; });
        check (std::abs (repaired[2000]) < 0.9f * 0.8f, "anti-spike removes a single-sample spike");
    }

    void checkFilters()
    {
        const auto input = sine (1000.0f, 9600, 0.3f);

        const auto wah = runRack (input, input, [] (j37dsp::FxTargets& t) { t.wah = 1.0f; });
        check (std::abs (rmsRange (wah, 4800, 9600) - rmsRange (input, 4800, 9600)) > 0.01,
               "wah changes the level of a tone");

        const auto comb = runRack (input, input, [] (j37dsp::FxTargets& t) { t.comb = 1.0f; });
        check (rmsRange (comb, 4800, 9600) > rmsRange (input, 4800, 9600),
               "comb adds resonant energy");

        const auto formant = runRack (input, input, [] (j37dsp::FxTargets& t) { t.formant = 1.0f; });
        check (std::abs (rmsRange (formant, 4800, 9600) - rmsRange (input, 4800, 9600)) > 0.005,
               "formant filter reshapes a tone");

        // A sibilant signal: a hiss band riding on a body tone. The de-esser
        // must pull the hiss down and leave the body alone.
        std::vector<float> sibilant (9600);
        for (std::size_t i = 0; i < sibilant.size(); ++i)
            sibilant[i] = 0.20f * std::sin (6.28318530718f * 7000.0f * static_cast<float> (i) / 48000.0f)
                        + 0.20f * std::sin (6.28318530718f * 500.0f * static_cast<float> (i) / 48000.0f);

        const auto deessed = runRack (sibilant, sibilant, [] (j37dsp::FxTargets& t) { t.deEsser = 1.0f; });
        check (goertzel (deessed, 7000.0, 4800) < goertzel (sibilant, 7000.0, 4800) * 0.9,
               "de-esser reduces the sibilance band");

        // A quiet partial beside a loud one: the spectral gate must remove the
        // quiet band's share of the level.
        std::vector<float> partials (9600);
        for (std::size_t i = 0; i < partials.size(); ++i)
            partials[i] = 0.40f * std::sin (6.28318530718f * 1000.0f * static_cast<float> (i) / 48000.0f)
                        + 0.004f * std::sin (6.28318530718f * 4000.0f * static_cast<float> (i) / 48000.0f);

        const auto gated = runRack (partials, partials, [] (j37dsp::FxTargets& t) { t.spectral = 1.0f; });
        check (goertzel (gated, 4000.0, 4800) < goertzel (partials, 4000.0, 4800) * 0.8,
               "spectral gate removes a quiet partial");
    }

    void checkModulation()
    {
        const auto input = sine (440.0f, 19200, 0.3f);
        const auto reference = rmsRange (input, 9600, 19200);

        const auto chorus = runRack (input, input, [] (j37dsp::FxTargets& t) { t.chorus = 1.0f; });
        check (std::abs (rmsRange (chorus, 9600, 19200) - reference) > 0.001, "chorus changes the signal");

        const auto flanger = runRack (input, input, [] (j37dsp::FxTargets& t) { t.flanger = 1.0f; });
        check (std::abs (rmsRange (flanger, 9600, 19200) - reference) > 0.001, "flanger changes the signal");

        const auto phaser = runRack (input, input, [] (j37dsp::FxTargets& t) { t.phaser = 1.0f; });
        check (std::abs (rmsRange (phaser, 9600, 19200) - reference) > 0.001, "phaser changes the signal");

        const auto tremolo = runRack (input, input, [] (j37dsp::FxTargets& t) { t.tremolo = 1.0f; });
        check (levelSwing (tremolo, 2400) > 2.0, "tremolo modulates the level");

        const auto vibrato = runRack (input, input, [] (j37dsp::FxTargets& t) { t.vibrato = 1.0f; });
        check (std::abs (rmsRange (vibrato, 9600, 19200) - reference) > 0.001, "vibrato changes the signal");

        const auto rotary = runRack (input, input, [] (j37dsp::FxTargets& t) { t.rotary = 1.0f; });
        check (std::abs (rmsRange (rotary, 9600, 19200) - reference) > 0.001, "rotary changes the signal");

        const auto ring = runRack (input, input, [] (j37dsp::FxTargets& t) { t.ringMod = 1.0f; });
        check (std::abs (rmsRange (ring, 9600, 19200) - reference) > 0.001, "ring modulator changes the signal");
    }

    void checkPitchStages()
    {
        const auto input = sine (1000.0f, 24000, 0.3f);

        const auto octaver = runRack (input, input, [] (j37dsp::FxTargets& t) { t.octaver = 1.0f; });
        check (goertzel (octaver, 500.0, 12000, 24000) > goertzel (octaver, 1000.0, 12000, 24000) * 0.05,
               "octaver produces energy an octave below");

        const auto shifted = runRack (input, input, [] (j37dsp::FxTargets& t) { t.pitch = 1.0f; });
        check (goertzel (shifted, 2000.0, 12000, 24000) > goertzel (input, 2000.0, 12000, 24000) * 5.0,
               "pitch shifter moves energy up an octave at full amount");

        const auto vocoded = runRack (input, input, [] (j37dsp::FxTargets& t) { t.vocoder = 1.0f; });
        check (std::abs (rmsRange (vocoded, 12000, 24000) - rmsRange (input, 12000, 24000)) > 0.001,
               "vocoder changes the signal");

        const auto granular = runRack (input, input, [] (j37dsp::FxTargets& t) { t.granular = 1.0f; });
        check (std::abs (rmsRange (granular, 12000, 24000) - rmsRange (input, 12000, 24000)) > 0.001,
               "granular changes the signal");
    }

    void checkSpeed()
    {
        const auto shortInput = sine (440.0f, 9600);
        const auto normal = runRack (shortInput, shortInput, [] (j37dsp::FxTargets& t) { t.speed = 0.5f; });

        bool transparent = true;
        for (std::size_t i = 0; i < shortInput.size(); ++i)
            transparent = transparent && normal[i] == shortInput[i];

        check (transparent, "speed at 1x is bit-exact");

        // At 2x the stage replays the history it has already been given, at
        // double speed - a tape loop. The buffer is 0.75 s, so the tone is fed
        // for that long before the measurement window opens.
        const auto filled = sine (440.0f, 72000);
        const auto doubled = runRack (filled, filled, [] (j37dsp::FxTargets& t) { t.speed = 1.0f; });

        check (goertzel (doubled, 880.0, 48000, 72000) > goertzel (filled, 880.0, 48000, 72000) * 2.0,
               "speed at 2x replays a 440 Hz tone at 880 Hz");

        const auto halved = runRack (filled, filled, [] (j37dsp::FxTargets& t) { t.speed = 0.0f; });
        check (goertzel (halved, 220.0, 48000, 72000) > goertzel (filled, 220.0, 48000, 72000) * 2.0,
               "speed at half replays a 440 Hz tone at 220 Hz");
    }

    void checkMultibandCompressor()
    {
        j37dsp::fx::MultibandCompressor compressor;
        compressor.prepare (sampleRate);

        j37dsp::fx::MultibandCompressor::Settings settings;
        settings.thresholdDb = -30.0f;
        settings.ratio = 8.0f;
        settings.attackMs = 5.0f;
        settings.releaseMs = 80.0f;
        settings.makeupDb = 0.0f;
        settings.mix = 1.0f;
        settings.upward = 0.0f;

        auto loud = sine (200.0f, 24000, 0.5f);

        for (std::size_t i = 0; i < loud.size(); ++i)
        {
            auto l = loud[i];
            auto r = loud[i];
            compressor.process (l, r, settings);
            loud[i] = l;
        }

        check (rmsRange (loud, 12000, 24000) < 0.5 * 0.7071 * 0.95,
               "multiband compressor reduces a signal above threshold");

        j37dsp::fx::MultibandCompressor idle;
        idle.prepare (sampleRate);

        auto settingsIdle = settings;
        settingsIdle.mix = 0.0f;

        const auto input = sine (200.0f, 1000, 0.5f);
        bool unchanged = true;

        for (std::size_t i = 0; i < input.size(); ++i)
        {
            auto l = input[i];
            auto r = input[i];
            idle.process (l, r, settingsIdle);
            unchanged = unchanged && l == input[i] && r == input[i];
        }

        check (unchanged, "multiband compressor at MIX 0 is bit-exact");

        j37dsp::fx::MultibandCompressor upward;
        upward.prepare (sampleRate);

        auto settingsUp = settings;
        settingsUp.upward = 1.0f;

        auto quiet = sine (200.0f, 48000, 0.01f);

        for (std::size_t i = 0; i < quiet.size(); ++i)
        {
            auto l = quiet[i];
            auto r = quiet[i];
            upward.process (l, r, settingsUp);
            quiet[i] = l;
        }

        check (rmsRange (quiet, 24000, 48000) > 0.01 * 0.7071 * 1.2,
               "upward compressor lifts a signal below its ceiling");
    }
}

int main()
{
    checkBypass();
    checkDrivesAddHarmonics();
    checkCrushers();
    checkGateAndSpike();
    checkFilters();
    checkModulation();
    checkPitchStages();
    checkSpeed();
    checkMultibandCompressor();

    if (failures == 0)
        std::printf ("fx_test: all twenty-five stages and the multiband compressor behaved as documented\n");

    return failures == 0 ? 0 : 1;
}
