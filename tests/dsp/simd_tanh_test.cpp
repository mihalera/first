// =============================================================================
//  The shaper's tanh seam: the one library switch in this build that changes
//  the rendered audio, and until now the one that changed nothing.
//
//  Source/DSP/HotPathMath.h's j37Tanh is the single place the saturation core's
//  cost can be traded: with J37_USE_SIMD_TANH defined (the CMake option of the
//  same name, OFF by default) and xsimd on the include path, every tanh the
//  seven curves run goes through xsimd's batch kernel instead of std::tanh.
//
//  WHY THIS FILE EXISTS. All three parts of that sentence were false when this
//  was written, and no build, test or CI job could have told anyone:
//
//    - the flag was read by a header and thrown by nobody, so J37_HAS_XSIMD was
//      0 in every build this repository has ever produced;
//    - the function the flag switched had NO CALLER AT ALL: the shaper called
//      std::tanh directly in eleven places, and `[[maybe_unused]]` on the seam
//      is what kept the compiler quiet about it;
//    - and the call it made, xsimd::tanh (x) with a float, was std::tanh: the
//      scalar entry point of xsimd is `using std::tanh;`
//      (ThirdParty/xsimd/include/xsimd/arch/xsimd_scalar.hpp), so the branch
//      would still have been a no-op the day somebody threw the flag. The
//      approximation only exists in the batch kernel.
//
//  So this harness is built THREE times by tests/dsp/run.sh, one binary per
//  shape, each told which shape it is by -DJ37_EXPECT_XSIMD and each asserting
//  what its own build must do:
//
//    1. flag + xsimd   -> J37_HAS_XSIMD 1, the curves move by the measured
//                         ~1e-7 and no more, and the seam is the approximation;
//    2. flag, no xsimd -> J37_HAS_XSIMD 0 and bit-for-bit std::tanh: a build
//                         that asks for a library it does not have gets the
//                         default rather than a broken branch;
//    3. neither        -> the shipping default, bit-for-bit std::tanh.
//
//  A single binary can only ever be one of the three, so "the flag reaches the
//  audio" is checked ACROSS them: each prints a deterministic digest of the
//  seven curves (below), and the script compares 2 against 3 for equality and 1
//  against 3 for a bounded, non-zero difference. That is the claim that matters
//  - not that the flag compiles, but that a build with it ON renders different
//  audio from a build with it OFF, by the amount the header quotes.
//
//  The curves are driven through SaturationCore's public process(), exactly the
//  way the engine drives them, and the core is the shipping struct rather than a
//  copy: tests/dsp/extract.py includes Source/DSP/SaturationCore.h.
// =============================================================================
#include "shim.h"

#include "DSP/SaturationCore.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
int gFailures = 0;

void check (bool condition, const std::string& what)
{
    std::printf ("  %-4s %s\n", condition ? "PASS" : "FAIL", what.c_str());
    if (! condition)
        ++gFailures;
}

std::string format (const char* pattern, double a)
{
    std::array<char, 256> buffer {};
    std::snprintf (buffer.data(), buffer.size(), pattern, a);
    return std::string (buffer.data());
}

std::string format2 (const char* pattern, double a, double b)
{
    std::array<char, 256> buffer {};
    std::snprintf (buffer.data(), buffer.size(), pattern, a, b);
    return std::string (buffer.data());
}

#ifndef J37_EXPECT_XSIMD
 #define J37_EXPECT_XSIMD 0
#endif

// The seven principles, in the order setWeights takes their amounts, plus the
// name each one's digest line carries.
struct Principle
{
    const char* name;
    int which;   // index into setWeights' seven arguments
};

const std::array<Principle, 7> kPrinciples {{
    { "tape",        0 }, { "valve",       1 }, { "cassette",    2 },
    { "vinyl",       3 }, { "amp",         4 }, { "transformer", 5 },
    { "digital",     6 }
}};

// The drive argument as the engine runs it: 0 to SaturationCore::shaperDriveRange.
const std::array<float, 4> kDrives { 0.0f, 0.75f, 1.5f, 2.25f };

// A fixed input sweep per drive, including both ends of a full-scale signal and
// a silent sample, so the digest covers the curve's knee and its saturation.
std::vector<float> inputs()
{
    std::vector<float> values;

    for (int i = -8; i <= 8; ++i)
        values.push_back (static_cast<float> (i) * 0.1875f);   // -1.5 .. 1.5

    values.push_back (1.0f);
    values.push_back (0.0f);
    values.push_back (-1.0f);
    return values;
}

/** One principle alone, over every drive and every input, printed as the digest
    the script compares between the three builds. The core is rebuilt per drive
    so the sequencing is the same in every build - the tape curve's hysteresis
    memory and the transformer's flux are part of what is being compared. */
void printDigest()
{
    const auto values = inputs();

    for (const auto& principle : kPrinciples)
    {
        for (std::size_t d = 0; d < kDrives.size(); ++d)
        {
            SaturationCore core;

            std::array<float, 7> weights {};
            weights[static_cast<std::size_t> (principle.which)] = 1.0f;
            core.setWeights (weights[0], weights[1], weights[2], weights[3],
                             weights[4], weights[5], weights[6]);

            for (std::size_t i = 0; i < values.size(); ++i)
            {
                const auto shaped = core.process (values[i], kDrives[d], 0.35f);
                std::printf ("curve %s %zu %zu %.9g\n", principle.name, d, i, static_cast<double> (shaped));
            }
        }
    }
}

/** The seam against std::tanh, over the range the shaper's arguments actually
    reach. `slope` runs to about 1 + drive * 2.6 at the top of the DRIVE range,
    so |x| <= 40 covers a full-scale sample with the steepest slope the curves
    can build. */
void testSeam()
{
    float worst = 0.0f;
    int exact = 0, total = 0;
    bool monotone = true;
    float previous = -2.0f;

    for (int i = -40000; i <= 40000; ++i)
    {
        const auto x = static_cast<float> (i) * 0.001f;
        const auto mine = j37Tanh (x);
        const auto reference = std::tanh (x);
        const auto difference = std::abs (mine - reference);

        worst = difference > worst ? difference : worst;
        exact += difference == 0.0f ? 1 : 0;
        ++total;

        monotone = monotone && mine >= previous;
        previous = mine;
    }

    if (J37_HAS_XSIMD)
    {
        // The measured worst case is 1.19e-7 absolute; the bound is set at twice
        // that so a different compiler or architecture has room without the
        // claim becoming "any error is fine".
        check (worst > 0.0f,
               "the flag reaches the seam: the kernel is a different curve from std::tanh "
               + format ("(worst |difference| %.3g)", worst));
        check (worst <= 2.5e-7f,
               "and it is the accuracy the header quotes: worst |difference| under 2.5e-7 "
               + format ("(measured %.3g)", worst));
        check (exact > total / 2,
               format2 ("and most of the range agrees bit for bit (%g of %g samples)", exact, total));
    }
    else
    {
        check (worst == 0.0f,
               "without the flag the seam IS std::tanh, bit for bit "
               + format ("(worst |difference| %.3g)", worst));
    }

    check (monotone, "the seam is monotonic over |x| <= 40, as tanh is");

    // The saturation ends and the exact zero, which an approximation is most
    // likely to get wrong at the edges rather than in the middle.
    check (j37Tanh (0.0f) == 0.0f, "j37Tanh(0) is exactly zero");
    check (j37Tanh (1000.0f) == 1.0f && j37Tanh (-1000.0f) == -1.0f,
           "the seam saturates to exactly +/-1 by |x| = 1000");
    check (j37Tanh (1.0e-30f) == 1.0e-30f, "a denormal-sized input passes through untouched");
    check (std::isfinite (j37Tanh (1.0e30f)), "and a huge one is finite");
}
} // namespace

int main()
{
    std::printf ("SHAPER TANH SEAM (%s) - j37Tanh, xsimd, and the flag between them\n",
                 J37_HAS_XSIMD ? "xsimd kernel" : "std::tanh");

    check (J37_HAS_XSIMD == (J37_EXPECT_XSIMD != 0),
           std::string ("this build reports J37_HAS_XSIMD ")
           + (J37_EXPECT_XSIMD ? "1, as the flag with the library on the include path should"
                               : "0, as the default (and the flag without the library) should"));

    testSeam();

    std::printf ("\n");
    printDigest();

    std::printf ("\n%s (%d failure%s)\n", gFailures == 0 ? "ALL CHECKS PASSED" : "FAILURES PRESENT",
                 gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
