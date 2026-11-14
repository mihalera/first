// =============================================================================
//  The ARCHIVE layout: the one build where NAM Core's registry can come up empty.
//
//  Every other binary under tests/ links NAM Core's objects on the command line,
//  and a whole-program link prunes nothing: an object that only carries a
//  file-scope `ConfigParserHelper` is in the program whether or not a symbol
//  names it, so its initialiser runs and the registry is populated. That is why
//  the fixtures kept passing while the shipped plugin refused every .nam file.
//
//  The plugin is not linked that way. `juce_add_plugin` makes its target a STATIC
//  library, so NAM Core's objects are archived into libMiBoX_SharedCode.a, and
//  the VST3/CLAP/AU wrapper then scans that archive. An archive member is pulled
//  only when something references a symbol it defines - and nothing references
//  `nam::wavenet::create_config`, because `get_dsp()` reaches the parsers through
//  a run-time registry. The registration objects are pruned, their initialisers
//  never run, and every valid WaveNet or ConvNet file is refused with
//  "No config parser registered for architecture: WaveNet" - which the panel
//  shows as INVALID MODEL (BAD SHAPE) for a file that is perfectly good.
//
//  So this file reproduces the shipped link: the NAM Core objects AND
//  Source/DSP/NamCoreRegistration.cpp go into a static archive, this translation
//  unit goes into the executable, and the only thing it asks of the archive is
//  the loader path it would use in the plugin. The test is what makes the anchor
//  in NamBackend.h load-bearing rather than decorative: delete that one call and
//  this binary is the only thing in the tree that notices.
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

// The two macros this file exists to pin, refused rather than defaulted: a build
// that quietly had the objects on its command line instead of in the archive
// would pass for exactly the reason the plugin failed.
#if defined (J37_HAS_RTNEURAL) && J37_HAS_RTNEURAL
 #error "archive_test.cpp is the NAM-Core-only archive build: pass -DJ37_HAS_RTNEURAL=0"
#endif
#if ! defined (J37_HAS_NAMCORE) || ! J37_HAS_NAMCORE
 #error "archive_test.cpp needs NAM Core: pass -DJ37_NAMCORE=1 and the vendored include paths"
#endif

namespace
{
constexpr int totalFrames = 32;
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

/** Loads one fixture through the stage, out of the archive rather than off the
    command line, and drives it enough to prove the model is really in use. */
void drive (const std::string& path, const char* architecture)
{
    nlohmann::json file;

    if (! readFixture (path, file))
        return;

    std::printf ("%s loaded out of a static archive\n", architecture);

    NeuralStage stage;
    const auto built = stage.buildFromNam (file);

    // The engine's own sentence, printed before the check so a failure names the
    // cause rather than only the symptom: with the anchor missing this reads
    // "No config parser registered for architecture: ...".
    if (! built)
        std::printf ("        refused: %s\n", stage.lastBuildErrorText().c_str());

    check (built, std::string ("the registry is populated for ") + architecture);

    if (! built)
        return;

    stage.adoptPendingModel();
    stage.setSampleRates (fixtureSampleRate, fixtureSampleRate);

    auto produced = false;

    for (int frame = 0; frame < totalFrames; ++frame)
    {
        const auto x = static_cast<float> (0.5 * std::sin (0.37 * frame));
        const auto y = stage.process (x, 1.0f);

        const auto usable = std::isfinite (y) && std::abs (y) < 10.0f;
        produced = produced || std::abs (y - x) > 1.0e-6f;

        if (! usable)
        {
            check (false, "every sample the archived model produces is finite and bounded");
            return;
        }
    }

    check (produced, "and the archived model is running rather than passing the sample through");
}

} // namespace

int main (int argc, char** argv)
{
    const std::string directory = argc > 1 ? argv[1] : "tests/neural/fixtures";

    std::printf ("archive check: %s - NAM Core inside libnamcore.a, -DJ37_HAS_RTNEURAL 0\n",
                 directory.c_str());

    // Both architectures, because the anchor is what pulls each one's object:
    // the WaveNet path is the one users meet, and the LSTM is the one that also
    // has a converter in the other build, so a test that only ran the WaveNet
    // could be satisfied by a converter that is not there.
    drive (directory + "/wavenet_small.nam", "WaveNet");
    drive (directory + "/lstm_small.nam", "LSTM");

    std::printf ("archive check: %s (%d failure(s))\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
