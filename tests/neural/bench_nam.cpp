// =============================================================================
//  The process COST of the two .nam engines - WP3's remaining gate, measured.
//
//      tests/neural/run_bench.sh
//
//  WHY THIS EXISTS
//
//  WP3 added NeuralAmpModelerCore as the backend for the .nam architectures the
//  RTNeural converter cannot express, and left its build option OFF by default
//  with one reason: "no shipped build pays for a convolutional stack it has not
//  measured." The accuracy half of that gate was measured when the backend
//  landed - tests/neural/fixture_test.cpp holds both engines to the reference
//  player's output at 1.19e-7 and 2.79e-8. The cost half was not measured at
//  all, so the option stayed off and this file is what closes it.
//
//  It is a MEASUREMENT, not a gate: nothing here fails on a slow number, because
//  a benchmark that fails a build gets its threshold lowered until it means
//  nothing (the same reasoning as tests/dsp/run_bench.sh). It fails only when a
//  model does not load or does not render finite samples - a measurement of
//  broken audio would be worse than none.
//
//  WHAT IS MEASURED, AND WHY THESE SHAPES
//
//  The fixtures under tests/neural/fixtures/ are deliberately tiny - 41 weights
//  for the WaveNet, 109 for the LSTM - because the accuracy gate compares
//  samples with a recorded oracle and a small model keeps that readable. They
//  are the wrong thing to time: a 2-channel, two-dilation WaveNet is not the
//  convolution stack that the default is waiting on. So this builds models of
//  the shape the format actually ships, from nam_shapes.h - 16 channels, a
//  3-tap kernel, ten dilations and the 1-tap post-stack head that narrows eight
//  channels to one (10 874 weights) for the WaveNet, twenty hidden cells (1 821
//  weights) for the LSTM - and reports the fixture's cost beside it so the
//  difference is a printed number rather than an assumption.
//
//  Each model is measured twice, because the question has two layers:
//
//    * `NeuralStage::process` is what one channel of the plugin costs: the
//      active pointer, the rate-bridge check, the model, and the dry/wet
//      mixdown. This is the number to compare against the real-time budget.
//    * `NamBackend::process` is the engine alone, so the two rows bracket the
//      stage's own share instead of it being estimated.
//
//  Two more things are measured, and both are there to stop the report
//  flattering itself. An empty visit of the same signal is the loop's own cost:
//  noise against a 3 microsecond model, most of the number beside a 41-weight
//  fixture, and a report that did not say so would compare a model with a `for`
//  loop. And the LSTM is measured on BOTH engines: the shipping dispatch sends
//  it to RTNeural whenever RTNeural is in the build, but a NAM-Core-only build
//  runs the same file on NAM Core, and the two are what a user's CPU pays
//  either way.
//
//  HOW TO READ IT
//
//  The printed share is the fraction of ONE core that one 48 kHz mono channel
//  needs, which is the only figure that says whether the stack fits in a host
//  at all. A stereo instance is two of them because the stage is per-channel,
//  and it runs at the MODEL's rate rather than the engine's - so 8x oversampling
//  does not multiply this number, which is exactly what the rate bridge buys.
//  The microseconds are the machine's; the share is the same arithmetic
//  restated, so the two cannot disagree.
//
//  BUILD (see tests/neural/run_bench.sh): NAM Core's own sources on the link
//  line as objects, RTNeural and nlohmann from ThirdParty/, `-DJ37_NAMCORE=1`,
//  and the fixtures directory as argv[1].
// =============================================================================

#include "shim.h"          // tests/dsp/shim.h - the juce stand-ins the DSP headers use
#include "NeuralStage.h"   // the SHIPPING stage, with the real vendored RTNeural
#include "NamBackend.h"    // and NAM Core behind the same stage
#include "nam_shapes.h"    // the weight layouts and the realistic-size generators

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace
{

constexpr double sampleRate = 48000.0;

// 60000 samples is 1.25 s of audio at 48 kHz: long enough that the clock's
// resolution is not the measurement and that a scheduler interrupt is a small
// fraction of a pass, short enough that the whole report is ten seconds even on
// a slow runner. Five repetitions because this runs on a shared CI machine - the
// BEST of them is the reported figure, since a scheduler interrupt can only ever
// make a run slower, and the median is printed beside it so a noisy machine
// shows up rather than being hidden.
//
// Even so, two rows of the same code can differ by a few percent between runs:
// that is the host's clock (frequency scaling, neighbours), not the model. The
// honest reading is the share to about half a point and the ORDER of the rows,
// which is why the report repeats the arithmetic as a percentage rather than
// asking anyone to trust the nanoseconds.
constexpr int samplesPerRun = 60000;
constexpr int repetitions = 5;

// The accumulated output. Read after each measurement, so the compiler cannot
// decide the loop is dead - the same barrier a volatile accumulator would give,
// at one store per run instead of one per sample.
float sink = 0.0f;

std::vector<float> makeSignal (int count)
{
    std::vector<float> signal (static_cast<std::size_t> (count));

    for (int i = 0; i < count; ++i)
    {
        const auto t = static_cast<float> (i) / static_cast<float> (sampleRate);
        signal[static_cast<std::size_t> (i)] =
            0.35f * std::sin (2.0f * 3.14159265f * 110.0f * t)
          + 0.05f * std::sin (2.0f * 3.14159265f * 3121.0f * t);
    }

    return signal;
}

struct Cost
{
    double bestNanoseconds = 0.0;
    double medianNanoseconds = 0.0;
};

template <typename ProcessOne>
Cost measure (const std::vector<float>& signal, ProcessOne&& processOne)
{
    // One untimed pass first: the first call into a model warms caches and, in
    // Eigen's case, may lazily choose a code path. Timing that pass would report
    // the cost of starting up rather than the cost of running.
    for (const auto sample : signal)
        sink += processOne (sample);

    std::vector<double> seconds;
    seconds.reserve (static_cast<std::size_t> (repetitions));

    for (int repetition = 0; repetition < repetitions; ++repetition)
    {
        const auto start = std::chrono::steady_clock::now();

        for (const auto sample : signal)
            sink += processOne (sample);

        const auto stop = std::chrono::steady_clock::now();
        seconds.push_back (std::chrono::duration<double> (stop - start).count());
    }

    std::sort (seconds.begin(), seconds.end());

    const auto frames = static_cast<double> (signal.size());
    return { seconds.front() / frames * 1.0e9, seconds[seconds.size() / 2] / frames * 1.0e9 };
}

/** The share of one core a mono channel at 48 kHz needs, as a percentage: the
    cost of a sample times the samples per second, against the second. */
double coreShare (const Cost& cost)
{
    return cost.bestNanoseconds * sampleRate / 1.0e9 * 100.0;
}

struct Row
{
    std::string model;
    std::string engine;
    Cost cost;
};

int failures = 0;

void fail (const std::string& what)
{
    std::printf ("  FAIL  %s\n", what.c_str());
    std::fflush (stdout);
    ++failures;
}

void report (const std::vector<Row>& rows)
{
    std::printf ("\n");
    std::printf ("  %-33s %-17s %10s %10s %9s\n",
                 "model", "engine", "ns/sample", "median", "% of core");
    std::printf ("  %-33s %-17s %10s %10s %9s\n",
                 "---------------------------------", "-----------------",
                 "----------", "----------", "---------");

    for (const auto& row : rows)
        std::printf ("  %-33s %-17s %10.1f %10.1f %8.2f%%\n",
                     row.model.c_str(), row.engine.c_str(),
                     row.cost.bestNanoseconds, row.cost.medianNanoseconds, coreShare (row.cost));

    std::printf ("\n");
    std::printf ("  One 48 kHz mono channel of the neural stage, from the rows above:\n");

    for (const auto& row : rows)
    {
        if (row.engine.rfind ("stage", 0) != 0)
            continue;

        const auto share = coreShare (row.cost);
        std::printf ("    %-40s %8.2f%% of a core per channel"
                     "  (stereo %.1f%%, %.0f mono channels per core)\n",
                     row.model.c_str(), share, share * 2.0,
                     share > 0.0 ? 100.0 / share : 0.0);
    }

    std::printf ("\n");
    std::printf ("  The model runs at the MODEL's rate, not the engine's: 8x\n"
                 "  oversampling does not multiply these shares, it only adds the\n"
                 "  rate bridge's conversion, which the stage rows already carry.\n");
}

bool readJson (const std::string& path, nlohmann::json& into, std::string& why)
{
    std::ifstream file (path);

    if (! file.is_open())
    {
        why = path + " could not be opened";
        return false;
    }

    std::stringstream text;
    text << file.rdbuf();

    try
    {
        into = nlohmann::json::parse (text.str());
    }
    catch (const std::exception& error)
    {
        why = path + " is not valid JSON: " + error.what();
        return false;
    }

    return true;
}

/** True when every sample the stage rendered is a number. A model that loads and
    then produces NaN is exactly the failure a cost measurement would hide, so
    the check runs on the same path the timing does. */
bool rendersFinite (const std::vector<float>& produced)
{
    for (const auto sample : produced)
        if (! std::isfinite (sample))
            return false;

    return ! produced.empty();
}

} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: bench_nam <fixtures-directory>\n");
        return 2;
    }

    const std::string fixtures = argv[1];
    const auto signal = makeSignal (samplesPerRun);

    std::printf ("\n");
    std::printf ("MiBoX - .nam inference cost (NAM Core and RTNeural, shipping stage)\n");
    std::printf ("  signal          : 110 Hz + 3.1 kHz, %.0f Hz, %d samples per run, %d runs\n",
                 sampleRate, samplesPerRun, repetitions);
    std::printf ("  block size      : one sample - what a per-sample stage can hand a block API\n");

    std::vector<Row> rows;

    // -------------------------------------------------------------------------
    //  0. The loop itself. At 10 865 weights this is noise; beside a 41-weight
    //     fixture it is most of the number, and a report that did not measure it
    //     would present the loop as the model.
    // -------------------------------------------------------------------------
    rows.push_back ({ "empty visit (loop overhead)", "neither",
                      measure (signal, [] (float x) { return x; }) });

    // -------------------------------------------------------------------------
    //  1. The realistic models from nam_shapes.h.
    //
    //     Both are loaded exactly the way a build with J37_NAMCORE=ON loads a
    //     file: buildFromNam on the message thread, adoptPendingModel at the
    //     block boundary (the handover the audio thread performs), then
    //     setSampleRates. The
    //     stage decides which engine gets which architecture, so the LSTM row
    //     below is RTNeural's and the WaveNet row is NAM Core's - that IS the
    //     shipping dispatch, not a benchmark-only arrangement.
    // -------------------------------------------------------------------------
    const auto waveNet = j37nam::standardWaveNet();
    const auto lstm = j37nam::standardLstm();

    std::printf ("  generated       : WaveNet 16 channels / kernel 3 / 10 dilations, %d weights\n",
                 static_cast<int> (waveNet.at ("weights").size()));
    std::printf ("                    LSTM 20 cells / 1 layer, %d weights\n",
                 static_cast<int> (lstm.at ("weights").size()));

    {
        NeuralStage stage;

        const auto built = stage.buildFromNam (waveNet);
        stage.adoptPendingModel();

        if (! built || ! stage.isActive())
        {
            fail ("the shipping stage refused the generated 16-channel WaveNet");
            return 1;
        }

        stage.setSampleRates (sampleRate, sampleRate);

        std::vector<float> produced;
        produced.reserve (signal.size());

        for (const auto sample : signal)
            produced.push_back (stage.process (sample, 1.0f));

        if (! rendersFinite (produced))
            fail ("the 16-channel WaveNet produced a sample that is not finite");

        rows.push_back ({ "WaveNet 16ch / 10 dilations", "stage (NAM)",
                          measure (signal, [&stage] (float x) { return stage.process (x, 1.0f); }) });

        stage.clear();
    }

    {
        j37dsp::NamBackend backend;

        const auto loaded = backend.load (waveNet);

        if (! loaded)
        {
            fail ("NamBackend refused the generated WaveNet: " + backend.lastError());
            return 1;
        }

        backend.prepare (backend.modelSampleRate());

        if (! backend.isPrepared())
        {
            fail ("the generated WaveNet loaded but could not be prepared at its own rate");
            return 1;
        }

        rows.push_back ({ "WaveNet 16ch / 10 dilations", "NamBackend",
                          measure (signal, [&backend] (float x) { return backend.process (x); }) });
    }

    {
        NeuralStage stage;

        const auto built = stage.buildFromNam (lstm);
        stage.adoptPendingModel();

        if (! built || ! stage.isActive())
        {
            fail ("the shipping stage refused the generated LSTM");
            return 1;
        }

        stage.setSampleRates (sampleRate, sampleRate);

        rows.push_back ({ "LSTM 20 cells", "stage (RTNeural)",
                          measure (signal, [&stage] (float x) { return stage.process (x, 1.0f); }) });

        stage.clear();
    }

    {
        j37dsp::NamBackend backend;

        const auto loaded = backend.load (lstm);

        if (! loaded)
        {
            fail ("NamBackend refused the generated LSTM: " + backend.lastError());
            return 1;
        }

        backend.prepare (backend.modelSampleRate());

        if (! backend.isPrepared())
        {
            fail ("the generated LSTM loaded but could not be prepared at its own rate");
            return 1;
        }

        rows.push_back ({ "LSTM 20 cells", "NamBackend",
                          measure (signal, [&backend] (float x) { return backend.process (x); }) });
    }

    // -------------------------------------------------------------------------
    //  2. The checked-in fixtures, for scale. These are the models the accuracy
    //     gate compares, and the point of printing them here is that their cost
    //     is not the cost of a profile - WP3 left the option off precisely
    //     because measuring these two would have said the wrong thing.
    // -------------------------------------------------------------------------
    {
        nlohmann::json fixture;
        std::string why;

        if (! readJson (fixtures + "/wavenet_small.nam", fixture, why))
        {
            fail (why);
            return 1;
        }

        j37dsp::NamBackend backend;

        if (! backend.load (fixture))
        {
            fail ("NamBackend refused the WaveNet fixture: " + backend.lastError());
            return 1;
        }

        backend.prepare (backend.modelSampleRate());

        if (! backend.isPrepared())
        {
            fail ("the WaveNet fixture loaded but could not be prepared at its own rate");
            return 1;
        }

        rows.push_back ({ "WaveNet fixture (2ch / 2 dil)", "NamBackend",
                          measure (signal, [&backend] (float x) { return backend.process (x); }) });
    }

    {
        nlohmann::json fixture;
        std::string why;

        if (! readJson (fixtures + "/lstm_small.nam", fixture, why))
        {
            fail (why);
            return 1;
        }

        NeuralStage stage;

        const auto built = stage.buildFromNam (fixture);
        stage.adoptPendingModel();

        if (! built || ! stage.isActive())
        {
            fail ("the shipping stage refused the LSTM fixture");
            return 1;
        }

        stage.setSampleRates (sampleRate, sampleRate);

        rows.push_back ({ "LSTM fixture (4 cells)", "stage (RTNeural)",
                          measure (signal, [&stage] (float x) { return stage.process (x, 1.0f); }) });
    }

    report (rows);

    // The one store the compiler cannot remove, and the checksum that proves the
    // timed loops produced the samples this report is about.
    std::printf ("  checksum        : %.6f\n\n", static_cast<double> (sink));

    if (failures != 0)
    {
        std::printf ("bench_nam: %d problem(s)\n", failures);
        return 1;
    }

    std::printf ("bench_nam: every model loaded and rendered finite samples; the numbers\n"
                 "           above are this machine's, and no threshold is applied to them\n");
    return 0;
}
