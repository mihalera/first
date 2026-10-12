// =============================================================================
//  The neural stage itself, compiled WITH a model standing behind it.
//
//  tests/dsp/ compiles the same extracted source with J37_HAS_RTNEURAL 0, which
//  proves the guard is honest - with the library absent every member of the stage
//  reduces to the pass-through - but it also means that every line INSIDE the guard
//  is compiled by nothing that runs on this machine. The rate conversion the stage
//  now does lives inside that guard, so without this file the integration would
//  only ever be looked at.
//
//  So RTNeural's own API is stood in for here - it is a header-only template and
//  its loader is one function, so a stand-in is a dozen lines - and the shipping
//  NeuralStage is driven end to end on generated signal. What that buys is
//  measured rather than described: a model trained at 48 kHz in a 192 kHz engine is
//  HANDED the programme at 48 kHz, which is the defect the bridge exists for, seen
//  this time through the stage a user actually loads a file into; NEURAL at 0 stays
//  bit-for-bit; and a model whose rate matches the engine's is not delayed at all.
//
//  Two things this file is NOT: it is not a model (the stand-in answers `input` and
//  ignores its weights, because what is under test is the plumbing around a model,
//  not a network), and it is not a substitute for the build - the real RTNeural
//  still has to compile against the same source in CI. What it does is keep the
//  guarded half of the stage from going unchecked until someone builds the plugin.
// =============================================================================

#include "shim.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// nlohmann/json is the REAL one, out of ThirdParty/ - it is header-only and it is
// what the stage parses a .nam file with - so only RTNeural's API is stood in for.
#include <nlohmann/json.hpp>

// The loader the shipping stage calls, and the model type it holds. RTNeural's
// real model is a template over its own layer list; the stage only ever calls
// `forward` and `reset` on it, so that is the whole of what has to exist here.
namespace RTNeural
{
/** Every sample the stand-in model was handed, in order. The stage owns its model
    behind a unique_ptr, so the log lives where the test can read it. */
inline std::vector<float>& loggedInputs()
{
    static std::vector<float> log;
    return log;
}

inline int& resetCount()
{
    static int count = 0;
    return count;
}

template <typename T>
struct Model
{
    T forward (const T* input)
    {
        loggedInputs().push_back (input[0]);
        return input[0];
    }

    void reset() { ++resetCount(); }
};

namespace json_parser
{
template <typename T>
std::unique_ptr<Model<T>> parseJson (const nlohmann::json&, bool)
{
    return std::make_unique<Model<T>>();
}
} // namespace json_parser
} // namespace RTNeural

// The macro the plugin's own header sets for a build with the library present, and
// which run.sh passes on the command line. Refusing to default it is deliberate:
// without it this file would compile the pass-through instead and cheerfully report
// that the guarded half of the stage is fine, which is the one thing it exists to
// check.
#if ! defined (J37_HAS_RTNEURAL)
 #error "stage_test.cpp needs J37_HAS_RTNEURAL defined - see tests/neural/run.sh"
#elif ! J37_HAS_RTNEURAL
 #error "stage_test.cpp is the build WITH a model: J37_HAS_RTNEURAL must be 1"
#endif

#include "extracted_dsp.inc"

namespace
{
constexpr double kTwoPi = 6.283185307179586;
constexpr double kEngineRate = 192000.0;
constexpr double kModelRate = 48000.0;
constexpr double kFrequency = 220.0;

int failures = 0;

void check (bool condition, const std::string& what)
{
    if (! condition)
    {
        std::printf ("  FAIL  %s\n", what.c_str());
        ++failures;
    }
}

/** A minimal LSTM .nam document - one layer, one input, one hidden unit - sized
    exactly as the converter's own arithmetic expects, so the file takes the
    shipping path (`architecture`, `config`, the flat `weights`) rather than a
    shortcut. What is IN the weights does not matter: the stand-in model answers
    its input. */
nlohmann::json minimalNamFile()
{
    const auto numLayers = 1;
    const auto inputSize = 1;
    const auto hiddenSize = 1;

    const auto biasElements = 4 * hiddenSize;
    auto kernel = [&] (int inputRows) { return 4 * hiddenSize * (inputRows + hiddenSize); };
    const auto expected = hiddenSize + 1 + kernel (inputSize) + biasElements + 2 * hiddenSize;

    nlohmann::json weights = nlohmann::json::array();
    for (int i = 0; i < expected; ++i)
        weights.push_back (0.01 * i);

    (void) numLayers;

    return { { "architecture", "LSTM" },
             { "config", { { "num_layers", numLayers },
                           { "input_size", inputSize },
                           { "hidden_size", hiddenSize } } },
             { "weights", weights } };
}

/** Installs the stand-in model and hands the stage the two rates. `buildFromNam`
    rather than `loadFromJson`, because the harness's juce::String stand-in is
    deliberately not a string (see tests/dsp/shim.h) and reports itself empty. */
void install (NeuralStage& stage, double modelRate, double engineRate)
{
    RTNeural::loggedInputs().clear();
    stage.setSampleRates (modelRate, engineRate);
    check (stage.buildFromNam (minimalNamFile()), "the stage installs a model");
    check (stage.isActive(), "and reports itself active");
}

/** One sample of a sine at the ENGINE's rate, which is what the stage is handed. */
float sineSample (int index)
{
    return static_cast<float> (0.5 * std::sin (kTwoPi * kFrequency * index / kEngineRate));
}

// -----------------------------------------------------------------------------
//  NEURAL at 0 with a model installed: still a bit-for-bit pass-through.
// -----------------------------------------------------------------------------
void testMutedStageIsUntouched()
{
    std::printf ("NEURAL at 0 is a bit-for-bit pass-through, model installed or not\n");

    NeuralStage stage;
    install (stage, kModelRate, kEngineRate);

    bool exact = true;

    for (int i = 0; i < 2000; ++i)
    {
        const auto x = sineSample (i);
        exact = exact && stage.process (x, 0.0f) == x;
    }

    check (exact, "every sample comes out bit-for-bit while NEURAL is at 0");
    check (RTNeural::loggedInputs().empty(),
           "and the model is not asked for anything at all: the stage is not paying for it");
}

// -----------------------------------------------------------------------------
//  The report: the model has to be handed the programme at ITS rate.
// -----------------------------------------------------------------------------
void testModelIsDrivenAtItsOwnRate()
{
    std::printf ("the stage hands its model the programme at the model's own rate\n");

    NeuralStage matching;
    install (matching, kModelRate, kModelRate);

    for (int i = 0; i < 8000; ++i)
        matching.process (sineSample (i), 1.0f);

    check (RTNeural::loggedInputs().size() == 8000,
           "a model at the engine's own rate is handed every sample: "
               + std::to_string (RTNeural::loggedInputs().size()) + " of 8000");

    NeuralStage converted;
    install (converted, kModelRate, kEngineRate);

    for (int i = 0; i < 8000; ++i)
        converted.process (sineSample (i), 1.0f);

    const auto calls = static_cast<double> (RTNeural::loggedInputs().size());
    const auto expected = 8000.0 * kModelRate / kEngineRate;

    check (std::abs (calls - expected) <= 6.0,
           "a model at a quarter of the engine's rate is handed a quarter as many samples: "
               + std::to_string (static_cast<int> (calls)) + " against "
               + std::to_string (static_cast<int> (expected)));

    // And those samples ARE the programme at that rate - the same pitch read at the
    // rate the model was trained at, which is the whole point. Read at the engine's
    // rate instead they would be a quarter of the pitch, which is what the stage
    // used to hand over.
    double worstAtModelRate = 0.0;
    double worstAtEngineRate = 0.0;
    const auto& log = RTNeural::loggedInputs();

    for (std::size_t m = 10; m + 5 < log.size(); ++m)
    {
        const auto atModelRate =
            0.5 * std::sin (kTwoPi * kFrequency * static_cast<double> (m) / kModelRate);
        const auto atEngineRate =
            0.5 * std::sin (kTwoPi * kFrequency * static_cast<double> (m) / kEngineRate);

        worstAtModelRate = std::max (worstAtModelRate,
                                     std::abs (static_cast<double> (log[m]) - atModelRate));
        worstAtEngineRate = std::max (worstAtEngineRate,
                                      std::abs (static_cast<double> (log[m]) - atEngineRate));
    }

    check (worstAtModelRate <= 1.0e-3,
           "the model is handed the programme at its own rate (worst sample "
               + std::to_string (worstAtModelRate) + ")");
    check (worstAtEngineRate >= 1.0e-2,
           "and not at the engine's (worst sample " + std::to_string (worstAtEngineRate) + ")");
}

// -----------------------------------------------------------------------------
//  The stage's own contract with the rates: matching means no delay, and a
//  cleared model is the pass-through it always was.
// -----------------------------------------------------------------------------
void testTheStageIsUnchangedAtMatchingRates()
{
    std::printf ("a model at the engine's rate leaves the stage exactly as it was\n");

    NeuralStage stage;
    install (stage, kModelRate, kModelRate);

    // The stand-in model answers its input, so at full NEURAL the stage's output
    // IS its input - and with the two rates equal that has to hold from the very
    // first sample, which is what says the conversion is absent rather than
    // delaying by zero for a while and then starting.
    bool exact = true;

    for (int i = 0; i < 2000; ++i)
    {
        const auto x = sineSample (i);
        exact = exact && stage.process (x, 1.0f) == x;
    }

    check (exact, "the stage is the identity, frame for frame, with no delay of its own");

    stage.clear();
    check (! stage.isActive(), "clearing the model takes the stage out of circuit");

    bool cleared = true;

    for (int i = 0; i < 500; ++i)
    {
        const auto x = sineSample (i);
        cleared = cleared && stage.process (x, 1.0f) == x;
    }

    check (cleared, "and the pass-through is bit-for-bit, with no model and no conversion");
}

int run()
{
    testMutedStageIsUntouched();
    testModelIsDrivenAtItsOwnRate();
    testTheStageIsUnchangedAtMatchingRates();

    if (failures == 0)
        std::printf ("NEURAL STAGE CHECK PASSED (0 failures)\n");
    else
        std::printf ("NEURAL STAGE CHECK FAILED (%d)\n", failures);

    return failures == 0 ? 0 : 1;
}
} // namespace

int main() { return run(); }
