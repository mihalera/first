// =============================================================================
//  The .nam FIXTURES, loaded and processed for real - WP3's gate.
//
//  tests/neural has always compiled the neural stage twice: with the library
//  absent (tests/dsp) and with RTNeural's API STOOD IN (stage_test.cpp). Neither
//  of those ever reads a model file, so neither answered the question WP3 starts
//  with - "does an actual .nam document load, and does the code that consumes it
//  produce the samples the reference player produces?" This file answers it, and
//  it is the reason there are fixtures in tests/neural/fixtures/ at all.
//
//  Five things are checked, and the last two are the ones WP3 was gated on:
//
//   1. Both fixtures parse as JSON and match the schema the architecture
//      declares: the key names, the config fields, and - the part a hand-written
//      fixture gets wrong silently - the LENGTH of the flat weights array. The
//      length is not asserted against a magic number: it is COMPUTED from the
//      config by the layout rules the reference player uses (one function per
//      architecture below), so a fixture edited into an inconsistent shape fails
//      here rather than inside a model that loads and sounds wrong.
//
//   2. The LSTM fixture is loaded by the SHIPPING stage - the real
//      Source/DSP/NeuralStage.h, against the real vendored RTNeural, through
//      NeuralStage::buildFromNam - and driven sample by sample at 48 kHz.
//
//   3. Both paths are compared against NAM Core's OWN output for the same file
//      and the same input, recorded in the tables below. That is the differential
//      oracle WP3 is gated on: the numbers were produced by
//      NeuralAmpModelerCore's reference player (its `get_dsp` path, 64 frames at
//      48 kHz, mono), and the harness requires the shipping path for LSTM, and
//      the reference evaluator below for WaveNet, to reproduce them.
//
//   4. The LSTM fixture is ALSO driven through NAM Core's own LSTM directly
//      (Source/DSP/NamBackend.h, no stage and no rate bridge in the way) and the
//      result is compared with what the shipping converter produced. They are
//      the same arithmetic in a different order, so the two have to agree to
//      well under -100 dBFS; this is the differential comparison between the two
//      engines, and it is why LSTM still goes through RTNeural even in a build
//      that has NAM Core.
//
//   5. The WaveNet fixture is loaded by the SHIPPING stage, through NAM Core, and
//      its output is compared with the same recorded window plus a finite/bounded
//      check. This is what the refusal assertion used to occupy: a build WITHOUT
//      NAM Core still refuses WaveNet - it has no engine for it - and still says
//      so in a printed line rather than as a failure, because that build is a
//      legitimate configuration rather than a regression.
//
//  The WaveNet evaluator below stays TEST INFRASTRUCTURE, not a backend: it
//  implements exactly the shape the fixture declares and refuses every other
//  shape, so it cannot quietly grow into a second implementation of the thing NAM
//  Core is here to provide.
//
//  The input signal and the compared window are part of the contract with the
//  recorded numbers: 64 frames of 0.5*sin(0.37 t) + 0.25*cos(0.11 t), compared at
//  frames 32..47. Frame 32 is past every receptive field in these fixtures (the
//  WaveNet's is 1 + 2 + 1 = 4 samples), so the comparison is of steady state
//  rather than of the startup transient.
// =============================================================================

#include "shim.h"          // tests/dsp/shim.h - the juce stand-ins the DSP headers use
#include "NeuralStage.h"   // the SHIPPING stage, with the real vendored RTNeural
#include "NamBackend.h"    // and, when the build has it, NAM Core behind the same stage
#include "nam_shapes.h"    // the two weight-layout rules, shared with bench_nam.cpp

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace
{

//==============================================================================
//  The run, spelled once: the fixtures, the oracle and this harness all agree on
//  these three numbers or the comparison means nothing.
constexpr int totalFrames = 64;
constexpr int windowStart = 32;
constexpr int windowLength = 16;
constexpr double fixtureSampleRate = 48000.0;

// The stage's gate: -100 dBFS against the reference player. The oracle tables
// are printed to nine significant digits, so anything this small is arithmetic
// order rather than a modelling difference.
constexpr float tolerance = 1.0e-5f;

float inputSample (int frame) noexcept
{
    return static_cast<float> (0.5 * std::sin (0.37 * frame) + 0.25 * std::cos (0.11 * frame));
}

//==============================================================================
//  The reference player's numbers, for the input above. Each table is one
//  fixture's frames [32, 48), printed by NAM Core and rounded to float here. They
//  are the WP3 gate: every path that claims to load these fixtures has to
//  reproduce them, whether it is the RTNeural converter, NAM Core itself, or the
//  test's own evaluator.
constexpr float lstmOracle[windowLength] = {
    0.341895193f, 0.343455046f, 0.344966620f, 0.346291155f, 0.347378731f, 0.348216712f,
    0.348767251f, 0.348940343f, 0.348624289f, 0.347762078f, 0.346428454f, 0.344852626f,
    0.343360245f, 0.342266411f, 0.341779798f, 0.341957152f
};

constexpr float wavenetOracle[windowLength] = {
    -0.00588050671f, -0.00523831649f, -0.00442188373f, -0.00352767133f, -0.00268073287f,
    -0.00200717035f, -0.00160355598f, -0.00151853811f, -0.00174854777f, -0.00224101217f,
    -0.00290287333f, -0.00361756817f, -0.00426904205f, -0.00476263370f, -0.00503343809f,
    -0.00504340744f
};

//==============================================================================
//  The weights array each architecture's layout implies is computed by
//  nam_shapes.h, which states the layout (and why) once and is also what
//  bench_nam.cpp generates its realistic-size models with. The check below is
//  unchanged: a fixture whose flat array is not exactly as long as its own
//  config implies is the classic silent fixture bug.
//==============================================================================

//==============================================================================
//  A reference evaluator for the WaveNet fixture's own shape.
//
//  It exists to turn "loads without throwing" into "renders the number the
//  reference player renders", and it is deliberately narrow: one layer array, no
//  post-stack head, gated off, no FiLM, Tanh activation, Conv1x1 layer1x1. A
//  fixture outside that shape is REFUSED here (see fixtureShapeSupported below)
//  rather than evaluated approximately - the point is a differential oracle for
//  the fixture, not a WaveNet implementation living in a test.
//
//  Validated against NAM Core itself while it was written: for this fixture the
//  evaluator reproduces NAM Core's frames [32, 48) to 1.3e-9, and the isolated
//  cases (only the conv, only the mixin enabled) to the same order.
//==============================================================================
bool fixtureShapeSupported (const nlohmann::json& config, std::string& why)
{
    const auto& arrays = config.at ("layers");

    if (arrays.size() != 1)
    {
        why = "the evaluator covers one layer array";
        return false;
    }

    const auto& array = arrays.at (0);

    if (array.value ("gated", false))
    {
        why = "gating is not part of the fixture";
        return false;
    }

    if (array.contains ("head1x1") && array["head1x1"].value ("active", false))
    {
        why = "head1x1 is not part of the fixture";
        return false;
    }

    for (const auto* film : { "conv_pre_film", "conv_post_film", "input_mixin_pre_film",
                              "input_mixin_post_film", "activation_pre_film",
                              "activation_post_film", "layer1x1_post_film" })
    {
        if (array.contains (film) && ! array[film].is_boolean())
        {
            why = "FiLM is not part of the fixture";
            return false;
        }
    }

    if (! config.at ("head").is_null())
    {
        why = "the post-stack head is not part of the fixture";
        return false;
    }

    if (array.value ("activation", std::string ("Tanh")) != "Tanh")
    {
        why = "only the Tanh activation is implemented";
        return false;
    }

    // The evaluator is MONO and conditionless by construction: the stage it
    // stands in for is a mono per-channel nonlinearity, and the fixture's
    // condition IS the dry input. A fixture that wanted a channel matrix or a
    // real condition DSP would need more code here than a test should hold.
    if (array.at ("input_size").get<int>() != 1 || array.at ("condition_size").get<int>() != 1)
    {
        why = "the evaluator is mono in and mono conditioned";
        return false;
    }

    if (array.value ("head_kernel_size", 1) != 1 || array.value ("head_dilation", 1) != 1)
    {
        why = "the head rechannel is a 1x1 conv in the fixture";
        return false;
    }

    if (array.value ("head_bias", false))
    {
        why = "the head rechannel carries no bias in the fixture";
        return false;
    }

    return true;
}

/** The fixture's own maths, sample by sample, with the state the reference player
    keeps: each layer's INPUT history (the residual chain means layer i's input is
    layer i-1's output), its dilated conv read at lookback d*(K-1-k), the mixin
    added to the current condition - which is the dry input, there being no
    condition DSP - the activation, the head accumulation over layers, the
    layer1x1 residual, and finally the head rechannel scaled by head_scale. */
std::vector<float> referenceWaveNet (const nlohmann::json& file, int frames)
{
    const auto& config = file.at ("config");
    const auto& weights = file.at ("weights");
    const auto& array = config.at ("layers").at (0);

    const auto channels = array.at ("channels").get<int>();
    const auto condition = array.at ("condition_size").get<int>();
    const auto inputSize = array.at ("input_size").get<int>();
    const auto headSize = array.at ("head_size").get<int>();
    const auto kernel = array.at ("kernel_size").get<int>();
    const auto& dilations = array.at ("dilations");

    auto next = [&weights] (long long& cursor, long long count)
    {
        std::vector<float> slice;
        slice.reserve (static_cast<std::size_t> (count));
        for (long long i = 0; i < count; ++i)
            slice.push_back (weights.at (cursor++).get<float>());
        return slice;
    };

    long long cursor = 0;
    const auto rechannel = next (cursor, static_cast<long long> (inputSize) * channels);

    struct Layer
    {
        int dilation = 1;
        std::vector<float> conv, convBias, mixin, layer1x1, layer1x1Bias;
        std::vector<std::vector<float>> history;   // per input channel, per frame
    };

    std::vector<Layer> layers;

    for (const auto& dilation : dilations)
    {
        Layer layer;
        layer.dilation = dilation.get<int>();
        layer.conv = next (cursor, static_cast<long long> (channels) * channels * kernel);
        layer.convBias = next (cursor, channels);
        layer.mixin = next (cursor, static_cast<long long> (condition) * channels);
        layer.layer1x1 = next (cursor, static_cast<long long> (channels) * channels);
        layer.layer1x1Bias = next (cursor, channels);
        layer.history.resize (static_cast<std::size_t> (channels));
        layers.push_back (std::move (layer));
    }

    const auto head = next (cursor, static_cast<long long> (headSize) * channels);
    const auto headScale = next (cursor, 1).at (0);

    std::vector<float> output;
    output.reserve (static_cast<std::size_t> (frames));

    for (int frame = 0; frame < frames; ++frame)
    {
        const auto dry = inputSample (frame);
        std::vector<float> current (static_cast<std::size_t> (channels), 0.0f);

        for (int i = 0; i < channels; ++i)
            current[static_cast<std::size_t> (i)] = rechannel[static_cast<std::size_t> (i)] * dry;

        std::vector<float> headAccumulator (static_cast<std::size_t> (channels), 0.0f);

        for (auto& layer : layers)
        {
            for (int c = 0; c < channels; ++c)
                layer.history[static_cast<std::size_t> (c)].push_back (current[static_cast<std::size_t> (c)]);

            std::vector<float> activated (static_cast<std::size_t> (channels), 0.0f);

            for (int i = 0; i < channels; ++i)
            {
                auto sum = layer.convBias[static_cast<std::size_t> (i)];

                for (int j = 0; j < channels; ++j)
                    for (int k = 0; k < kernel; ++k)
                    {
                        const auto index = frame - layer.dilation * (kernel - 1 - k);
                        if (index < 0)
                            continue;

                        sum += layer.conv[static_cast<std::size_t> ((i * channels + j) * kernel + k)]
                             * layer.history[static_cast<std::size_t> (j)][static_cast<std::size_t> (index)];
                    }

                sum += layer.mixin[static_cast<std::size_t> (i)] * dry;   // condition_size is 1 in the fixture

                activated[static_cast<std::size_t> (i)] = std::tanh (sum);
                headAccumulator[static_cast<std::size_t> (i)] += activated[static_cast<std::size_t> (i)];
            }

            std::vector<float> residual (static_cast<std::size_t> (channels), 0.0f);

            for (int i = 0; i < channels; ++i)
            {
                auto sum = layer.layer1x1Bias[static_cast<std::size_t> (i)];

                for (int j = 0; j < channels; ++j)
                    sum += layer.layer1x1[static_cast<std::size_t> (i * channels + j)]
                         * activated[static_cast<std::size_t> (j)];

                residual[static_cast<std::size_t> (i)] = current[static_cast<std::size_t> (i)] + sum;
            }

            current = std::move (residual);
        }

        auto sum = 0.0f;

        for (int c = 0; c < channels; ++c)
        {
            const auto headIndex = static_cast<std::size_t> (c);
            const auto weight = headIndex < head.size() ? head[headIndex] : 0.0f;
            sum += weight * headAccumulator[static_cast<std::size_t> (c)];
        }

        output.push_back (headScale * sum);
    }

    juce::ignoreUnused (condition, inputSize);
    return output;
}

//==============================================================================

int failures = 0;

void check (bool condition, const std::string& what)
{
    if (! condition)
    {
        ++failures;
        std::printf ("  FAIL  %s\n", what.c_str());
    }
    else
    {
        std::printf ("  ok    %s\n", what.c_str());
    }

    // Flushed per line: a fixture that is broken badly enough to abort somewhere
    // below must still leave the checks that already ran in the log, rather than
    // losing them with the buffer.
    std::fflush (stdout);
}

//==============================================================================
//  Installs a fixture the way the ENGINE does, not just the message-thread half.
//
//  `buildFromNam` only PUBLISHES the model: the object is built on the control
//  thread and handed over at a block boundary, so that the audio thread never
//  destroys or allocates one. `adoptPendingModel` is that boundary - the shipping
//  path calls it at the start of every block (Source/ProcessorSignalPath.cpp).
//  It is also the call that makes `isActive()` true and gives `process()` a model
//  to run: a harness that skipped it would report "built" and then measure the
//  bit-exact pass-through, which is exactly what this file did when the deferred
//  handover landed.
bool buildAndAdopt (NeuralStage& stage, const nlohmann::json& nam)
{
    const auto built = stage.buildFromNam (nam);
    stage.adoptPendingModel();
    return built;
}

float worstDifference (const std::vector<float>& produced, const float* expected, int count)
{
    auto worst = 0.0f;

    for (int i = 0; i < count; ++i)
        worst = juce::jmax (worst, std::abs (produced[static_cast<std::size_t> (windowStart + i)] - expected[i]));

    return worst;
}

#if J37_HAS_NAMCORE
/** The same comparison for two RENDERS of the same input: the window is the same
    one the oracle tables cover, so both halves are measured where they are steady
    state. Only the two-engine build has two renders to compare. */
float worstDifference (const std::vector<float>& produced, const std::vector<float>& expected)
{
    auto worst = 0.0f;
    const auto end = juce::jmin (produced.size(), expected.size());

    for (std::size_t i = static_cast<std::size_t> (windowStart); i < end; ++i)
        worst = juce::jmax (worst, std::abs (produced[i] - expected[i]));

    return worst;
}
#endif

bool readFixture (const std::string& path, nlohmann::json& into)
{
    std::ifstream file (path);

    if (! file.is_open())
    {
        std::printf ("  FAIL  %s could not be opened\n", path.c_str());
        ++failures;
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
        std::printf ("  FAIL  %s is not valid JSON: %s\n", path.c_str(), error.what());
        ++failures;
        return false;
    }

    return true;
}

#if J37_HAS_NAMCORE
/** Drives NAM Core's own loader and per-sample loop over the standard input, with
    no stage and no rate bridge in the way - the reference half of the LSTM
    differential. Empty on failure, which the caller reports. */
std::vector<float> renderThroughNamCore (const nlohmann::json& file)
{
    std::vector<float> output;

    j37dsp::NamBackend backend;

    if (! backend.load (file))
    {
        check (false, "NAM Core loads the fixture: " + backend.lastError());
        return output;
    }

    backend.prepare (backend.modelSampleRate());

    for (int frame = 0; frame < totalFrames; ++frame)
        output.push_back (backend.process (inputSample (frame)));

    return output;
}

/** True when every sample is a finite number inside a range a small model can
    legitimately produce. A NaN is the failure mode a wrong weight layout tends
    to have, and it is invisible to a difference against an oracle because every
    comparison with a NaN is false. */
bool isFiniteAndBounded (const std::vector<float>& samples)
{
    for (const auto sample : samples)
        if (! std::isfinite (sample) || std::abs (sample) > 10.0f)
            return false;

    return true;
}
#endif

} // namespace

//==============================================================================
int main (int argc, char** argv)
{
    const std::string directory = argc > 1 ? argv[1] : "tests/neural/fixtures";

#if J37_HAS_NAMCORE
    const char* backend = "NAM Core + RTNeural";
#else
    const char* backend = "RTNeural only (no NAM Core in this build)";
#endif

    std::printf ("fixture check: %s at %.0f Hz, frames [%d, %d), backend: %s\n",
                 directory.c_str(), fixtureSampleRate, windowStart, windowStart + windowLength,
                 backend);

    // -------------------------------------------------------------------------
    //  The LSTM fixture, through the shipping stage.
    // -------------------------------------------------------------------------
    nlohmann::json lstmFile;

    if (! readFixture (directory + "/lstm_small.nam", lstmFile))
        return 1;

    check (lstmFile.value ("architecture", std::string()) == "LSTM",
           "lstm_small.nam declares the LSTM architecture");
    check (lstmFile.value ("sample_rate", 0.0) == fixtureSampleRate,
           "lstm_small.nam declares 48 kHz");
    const auto lstmCount = lstmWeightCount (lstmFile.at ("config"));
    const auto lstmConsistent = lstmFile.at ("weights").is_array()
                                && static_cast<long long> (lstmFile.at ("weights").size()) == lstmCount;
    check (lstmConsistent,
           "lstm_small.nam's weights array matches the config's layout ("
               + std::to_string (lstmCount) + " values)");

    std::vector<float> converterOutput;

    if (! lstmConsistent)
    {
        // A fixture whose shape disagrees with its own config is not driven: the
        // checks below would be measuring the failure rather than the fixture.
        check (false, "lstm_small.nam is driven (skipped: the weights array is the wrong length)");
    }
    else
    {
        NeuralStage stage;
        const auto built = stage.buildFromNam (lstmFile);
        check (built, "the shipping stage BUILDS lstm_small.nam");

        //  The window between the publish and the handover is the PANEL's window,
        //  and it is the one the readout has to answer for: a host with the
        //  transport stopped may not run a block for a while, so a picker that
        //  asked `isActive()` printed "NO MODEL" over a model that was built and
        //  waiting. `hasModel()` is the control thread's answer, and it is true
        //  from the publish; `isActive()` stays the audio thread's, and it is
        //  false until this block boundary.
        check (built && stage.hasModel() && ! stage.isActive(),
               "a BUILT model is installed before the audio thread adopts it");

        stage.adoptPendingModel();
        check (stage.hasModel(), "the installed model survives the handover");
        check (stage.isActive(), "the shipping stage reports the LSTM model as active");
        stage.setSampleRates (fixtureSampleRate, fixtureSampleRate);

        if (built)
        {
            converterOutput.reserve (totalFrames);

            for (int frame = 0; frame < totalFrames; ++frame)
                converterOutput.push_back (stage.process (inputSample (frame), 1.0f));

            const auto worst = worstDifference (converterOutput, lstmOracle, windowLength);
            std::printf ("  ..    LSTM: worst difference from the reference player %.3e\n", worst);
            check (worst < tolerance, "LSTM output matches the reference player at frames [32, 48)");

            stage.clear();
            check (! stage.isActive()
                       && stage.process (inputSample (3), 1.0f) == inputSample (3),
                   "clearing the stage returns it to the bit-exact pass-through");
            check (! stage.hasModel(), "and the stage no longer reports an installed model");
        }
    }

#if J37_HAS_NAMCORE
    // -------------------------------------------------------------------------
    //  The LSTM differential: the shipping converter against NAM Core's own LSTM,
    //  same fixture, same input, sample for sample.
    // -------------------------------------------------------------------------
    if (lstmConsistent && ! converterOutput.empty())
    {
        std::printf ("LSTM: the RTNeural converter against NAM Core's own LSTM\n");

        const auto referenceOutput = renderThroughNamCore (lstmFile);

        if (referenceOutput.size() == converterOutput.size())
        {
            const auto worst = worstDifference (converterOutput, referenceOutput);
            std::printf ("  ..    LSTM: worst difference between the two engines %.3e\n", worst);
            check (worst < tolerance,
                   "the RTNeural converter and NAM Core render the same LSTM samples");
        }
        else
        {
            check (false, "NAM Core rendered the same number of frames as the stage ("
                              + std::to_string (referenceOutput.size()) + " of "
                              + std::to_string (converterOutput.size()) + ")");
        }
    }
    else
    {
        check (false, "the LSTM differential runs (skipped: the converter produced no output)");
    }
#endif

    // -------------------------------------------------------------------------
    //  The WaveNet fixture: the reference evaluator measures it, and the shipping
    //  stage is required to load it - through NAM Core, or to refuse it in a build
    //  without the library.
    // -------------------------------------------------------------------------
    nlohmann::json wavenetFile;

    if (! readFixture (directory + "/wavenet_small.nam", wavenetFile))
        return 1;

    check (wavenetFile.value ("architecture", std::string()) == "WaveNet",
           "wavenet_small.nam declares the WaveNet architecture");
    check (wavenetFile.value ("sample_rate", 0.0) == fixtureSampleRate,
           "wavenet_small.nam declares 48 kHz");
    const auto wavenetCount = wavenetWeightCount (wavenetFile.at ("config"));
    const auto wavenetConsistent =
        wavenetFile.at ("weights").is_array()
        && static_cast<long long> (wavenetFile.at ("weights").size()) == wavenetCount;
    check (wavenetConsistent,
           "wavenet_small.nam's weights array matches the config's layout ("
               + std::to_string (wavenetCount) + " values)");
    check (wavenetConsistent
               && std::abs (wavenetFile.at ("weights").back().get<float>()
                            - wavenetFile.at ("config").at ("head_scale").get<float>()) < 1.0e-6f,
           "wavenet_small.nam's trailing weight IS the config's head_scale");

    std::string unsupported;

    if (! wavenetConsistent)
    {
        // Same rule as above: the evaluator indexes the weights stream, so a
        // stream of the wrong length cannot be evaluated - and must not be, or
        // the report would be about the shape rather than about the fixture.
        check (false, "wavenet_small.nam is evaluated (skipped: the weights array is the wrong length)");
    }
    else if (fixtureShapeSupported (wavenetFile.at ("config"), unsupported))
    {
        const auto produced = referenceWaveNet (wavenetFile, totalFrames);
        const auto worst = worstDifference (produced, wavenetOracle, windowLength);
        std::printf ("  ..    WaveNet: worst difference from the reference player %.3e\n", worst);
        check (worst < tolerance, "WaveNet output matches the reference player at frames [32, 48)");
    }
    else
    {
        check (false, "wavenet_small.nam is inside the reference evaluator's shape: " + unsupported);
    }

#if J37_HAS_NAMCORE
    if (wavenetConsistent)
    {
        // The stage's own path: NAM Core builds the model, the rate bridge runs it
        // at 48 kHz, and the panel's NEURAL control is at full wet. This is the
        // assertion that used to be "the shipping stage refuses WaveNet".
        NeuralStage stage;
        const auto built = buildAndAdopt (stage, wavenetFile);
        check (built, "the shipping stage BUILDS wavenet_small.nam through NAM Core");
        check (stage.isActive(), "the shipping stage reports the WaveNet model as active");

        if (built)
        {
            stage.setSampleRates (fixtureSampleRate, fixtureSampleRate);

            std::vector<float> produced;
            produced.reserve (totalFrames);

            for (int frame = 0; frame < totalFrames; ++frame)
                produced.push_back (stage.process (inputSample (frame), 1.0f));

            const auto worst = worstDifference (produced, wavenetOracle, windowLength);
            std::printf ("  ..    WaveNet through the stage: worst difference %.3e\n", worst);
            check (worst < tolerance,
                   "the stage's WaveNet output matches the reference player at frames [32, 48)");
            check (isFiniteAndBounded (produced),
                   "the stage's WaveNet output is finite and bounded");

            stage.clear();
            check (! stage.isActive()
                       && stage.process (inputSample (3), 1.0f) == inputSample (3),
                   "clearing the WaveNet model returns the stage to the bit-exact pass-through");
        }
    }
    else
    {
        check (false, "wavenet_small.nam is driven through the stage "
                      "(skipped: the weights array is the wrong length)");
    }
#else
    {
        // A build without NAM Core has no engine for WaveNet, so the refusal is
        // still correct - but it is a configuration, not the WP3 gap, and it is
        // reported as such rather than as a failure.
        NeuralStage stage;
        const auto built = buildAndAdopt (stage, wavenetFile);
        std::printf ("  ..    WaveNet is %s in this build - J37_NAMCORE is off, so only the\n"
                     "        LSTM converter exists; configure -DJ37_NAMCORE=ON to load it\n",
                     built ? "ACCEPTED" : "refused");
        check (! built && ! stage.isActive(),
               "WaveNet is refused without NAM Core, and the stage stays inactive");
    }
#endif

    std::printf ("fixture check: %s (%d failure(s))\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
