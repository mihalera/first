// =============================================================================
//  A build with NAM Core and WITHOUT RTNeural - the dispatch's third branch.
//
//  NeuralStage has three shapes, and tests/neural compiles all of them:
//
//    * neither engine (tests/dsp): the bit-for-bit pass-through;
//    * RTNeural only (stage_test.cpp, and the shipping default): the converter;
//    * BOTH (fixture_test.cpp): the converter for LSTM, NAM Core for everything
//      else, and the differential between them.
//
//  The fourth combination - NAM Core without RTNeural - is a real configuration:
//  `J37_NAMCORE` is independent of whether ThirdParty/RTNeural is on the include
//  path, and a tree with one vendored and not the other is a checkout, not an
//  error. In that build there is no converter, so `buildFromNam` has to send the
//  LSTM to NAM Core as well; if it did not, the one architecture the stage has
//  always supported would stop loading the day RTNeural was pruned.
//
//  So this file asks the only question that configuration raises: does an LSTM
//  .nam file still load, run and clear - through NAM Core - with J37_HAS_RTNEURAL
//  0? The OUTPUT is not compared with an oracle here; that is fixture_test's job
//  (it drives NAM Core's LSTM directly and against the converter). What is
//  measured here is the dispatch: built, active, not a pass-through, and back to
//  one after clear().
// =============================================================================

#include "shim.h"          // tests/dsp/shim.h - the juce stand-ins the DSP headers use
#include "NeuralStage.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

// The two macros this file exists to pin. Refusing to default them is deliberate:
// a build that quietly had RTNeural on would compile the converter branch and
// report success for the branch it was asked to check.
#if ! defined (J37_HAS_RTNEURAL) || J37_HAS_RTNEURAL
 #error "nam_only_test.cpp is the build WITHOUT RTNeural: pass -DJ37_HAS_RTNEURAL=0"
#endif
#if ! defined (J37_HAS_NAMCORE) || ! J37_HAS_NAMCORE
 #error "nam_only_test.cpp needs NAM Core: pass -DJ37_NAMCORE=1 and the vendored include paths"
#endif

namespace
{
constexpr int totalFrames = 64;
constexpr double fixtureSampleRate = 48000.0;

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

    std::fflush (stdout);
}

float inputSample (int frame) noexcept
{
    return static_cast<float> (0.5 * std::sin (0.37 * frame) + 0.25 * std::cos (0.11 * frame));
}

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

/** Loads one fixture through the shipping stage and drives it. */
void drive (const std::string& path, const char* architecture)
{
    nlohmann::json file;

    if (! readFixture (path, file))
        return;

    std::printf ("%s with NAM Core and no RTNeural\n", architecture);

    NeuralStage stage;
    const auto built = stage.buildFromNam (file);

    // `buildFromNam` publishes the model; the audio thread's block boundary is
    // what adopts it, makes `isActive()` true and gives `process()` something to
    // run. The shipping path calls this at the start of every block.
    stage.adoptPendingModel();

    check (built, std::string ("the stage BUILDS the ") + architecture + " fixture");
    check (stage.isActive(), "and reports the model as active");

    if (! built)
        return;

    stage.setSampleRates (fixtureSampleRate, fixtureSampleRate);

    std::vector<float> produced;
    produced.reserve (totalFrames);
    auto finite = true;
    auto moved = false;

    for (int frame = 0; frame < totalFrames; ++frame)
    {
        const auto x = inputSample (frame);
        const auto y = stage.process (x, 1.0f);

        produced.push_back (y);
        finite = finite && std::isfinite (y) && std::abs (y) < 10.0f;
        moved = moved || std::abs (y - x) > 1.0e-6f;
    }

    check (finite, "every sample is finite and bounded");
    check (moved, "the output is the model's, not the pass-through");

    stage.clear();
    check (! stage.isActive()
               && stage.process (inputSample (3), 1.0f) == inputSample (3),
           "clearing it returns the stage to the bit-exact pass-through");
}

} // namespace

int main (int argc, char** argv)
{
    const std::string directory = argc > 1 ? argv[1] : "tests/neural/fixtures";

    std::printf ("nam-only check: %s at %.0f Hz - J37_HAS_RTNEURAL 0, J37_HAS_NAMCORE 1\n",
                 directory.c_str(), fixtureSampleRate);

    drive (directory + "/lstm_small.nam", "LSTM");
    drive (directory + "/wavenet_small.nam", "WaveNet");

    std::printf ("nam-only check: %s (%d failure(s))\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
