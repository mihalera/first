// =============================================================================
//  Micro-benchmarks for the DSP hot paths.
//
//  WHY THIS EXISTS
//
//  The library review flagged two things as "the hot loop": the magnetic shaper
//  (four std::tanh calls per sample per channel) and the two glue detectors
//  (two std::exp calls per sample per channel). Both claims are plausible from
//  reading the code and neither was measured. Optimising on a plausible reading
//  is how a codebase acquires an approximation that costs accuracy, complexity
//  and a listening test - and does not actually move the CPU number.
//
//  So this measures first. It reports:
//
//    1. The SHAPER alone - magneticHysteresis with a realistic drive/bias and a
//       live memory term, so the compiler cannot hoist the call out of the loop.
//
//    2. The DETECTOR alone - GlueCompressor::processDetection with the block
//       coefficients built the way processTapeEngine builds them.
//
//    3. A FULL FRAME - the shaper, the detector and the K-weighted meter run
//       together, which is what one sample of the engine actually costs. This is
//       the number to compare against the real-time budget.
//
//    4. The REFERENCE MATH - std::exp / std::tanh measured directly, so the cost
//       of the two hot calls can be read against the cost of everything around
//       them.
//
//  HOW TO READ IT
//
//  The interesting output is not the nanoseconds; it is the SHARE. If the shaper
//  is 15 % of a frame, replacing its tanh with a polynomial cannot win more than
//  15 % and is not worth the change. If it is 70 %, it is. The budget line at the
//  bottom prints how many frames fit in one second at 48 kHz, which is the only
//  figure that says whether there is headroom to spend in the first place.
//
//  nanobench is used rather than hand-rolled timing because it handles warm-up,
//  outlier rejection and the DoNotOptimize barrier - all three of which are easy
//  to get wrong in a way that produces a flattering number.
//
//  BUILD (see tests/dsp/run_bench.sh):
//     python3 tests/dsp/extract.py <out.inc>
//     g++ -std=c++17 -O2 -I tests/dsp -I <build> tests/dsp/bench.cpp \
//         -I <nanobench>/src -o bench && ./bench
// =============================================================================

#include "shim.h"

#include <cmath>
#include <cstdio>
#include <ctime>
#include <vector>

// nanobench is header-only. It is fetched by CPM only when J37_BUILD_TESTS=ON,
// so its include path is supplied by the build command rather than assumed here.
#include <nanobench.h>

#include "extracted_dsp.inc"

namespace
{
constexpr float kSampleRate = 48000.0f;
constexpr int kBlockSize = 512;

// A signal buffer rather than a constant: with a constant input the compiler can
// fold parts of the shaper, and a benchmark that measures a folded loop measures
// nothing. This is a 220 Hz sine plus a little noise, which is representative of
// the bass-band material the SUBFUND detector actually tracks.
std::vector<float> makeSignal (std::size_t count)
{
    std::vector<float> signal (count);
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto t = static_cast<float> (i) / kSampleRate;
        signal[i] = 0.35f * std::sin (2.0f * 3.14159265f * 220.0f * t)
                  + 0.02f * std::sin (2.0f * 3.14159265f * 3121.0f * t);
    }
    return signal;
}

// The detector's block coefficients, built exactly the way processTapeEngine
// builds them. Rebuilding them inside the timed loop would measure the wrong
// thing - the whole point of the refactor is that they are built once per block.
GlueCompressor::Coefficients makeDetectorCoefficients()
{
    return GlueCompressor::makeCoefficients (kSampleRate,
                                             0.16f,   // inputAttackSeconds
                                             0.85f,   // inputReleaseSeconds
                                             0.5f);   // inputDriveLoad
}

void reportBudget (double nanosecondsPerFrame)
{
    const auto framesPerSecond = 1.0e9 / nanosecondsPerFrame;
    const auto realtimeBudget = static_cast<double> (kSampleRate);
    const auto headroom = framesPerSecond / realtimeBudget;

    std::printf ("\n");
    std::printf ("  frame cost      : %.1f ns\n", nanosecondsPerFrame);
    std::printf ("  frames / second : %.0f\n", framesPerSecond);
    std::printf ("  realtime budget : %.0f frames/s at %.0f Hz\n",
                 realtimeBudget, kSampleRate);
    std::printf ("  headroom        : %.0fx one core\n", headroom);
    std::printf ("\n");
}
} // namespace

int main()
{
    ankerl::nanobench::Bench bench;
    bench.title ("Nonlin Analog Saturator - DSP hot paths")
         .unit ("frame")
         .warmup (200)
         .minEpochIterations (2000);

    const auto signal = makeSignal (kBlockSize);

    // -------------------------------------------------------------------------
    //  1. The magnetic shaper, in isolation.
    //
    //  `memory` is fed back from the previous output, so each call depends on the
    //  last and cannot be vectorised away. drive 0.30 and asymmetry 0.42 are the
    //  factory defaults, so this is the cost at the settings a user actually runs.
    // -------------------------------------------------------------------------
    bench.run ("magneticHysteresis (default drive/bias)", [&]
    {
        float memory = 0.0f;
        float sink = 0.0f;
        for (const auto sample : signal)
        {
            const auto shaped = magneticHysteresis (sample, 0.30f, 0.42f, memory);
            memory = shaped;
            sink += shaped;
        }
        ankerl::nanobench::doNotOptimizeAway (sink);
    });

    // -------------------------------------------------------------------------
    //  2. One glue detector, in isolation.
    //
    //  Same block coefficients the engine builds. The detector is stateful, so
    //  consecutive calls are genuinely dependent.
    // -------------------------------------------------------------------------
    bench.run ("GlueCompressor::processDetection", [&]
    {
        GlueCompressor detector;
        const auto coefficients = makeDetectorCoefficients();
        float sink = 0.0f;
        for (const auto sample : signal)
        {
            const auto power = sample * sample;
            sink += detector.processDetection (power, kSampleRate, coefficients);
        }
        ankerl::nanobench::doNotOptimizeAway (sink);
    });

    // -------------------------------------------------------------------------
    //  3. One full frame of the engine's nonlinear core.
    //
    //  Shaper + detector + K-weighted meter, which is the per-sample work that is
    //  not already a one-pole. This is the number to read against the budget: if
    //  a frame here is a small fraction of 1/48000 s, the engine has headroom and
    //  the approximations are not needed.
    // -------------------------------------------------------------------------
    bench.run ("full frame (shaper + detector + LUFS meter)", [&]
    {
        float memory = 0.0f;
        GlueCompressor detector;
        LoudnessMeter meter;
        const auto coefficients = makeDetectorCoefficients();
        const auto windowCoefficient = LoudnessMeter::makeWindowCoefficient (kSampleRate);
        float sink = 0.0f;

        for (const auto sample : signal)
        {
            const auto shaped = magneticHysteresis (sample, 0.30f, 0.42f, memory);
            memory = shaped;

            sink += detector.processDetection (shaped * shaped, kSampleRate, coefficients);
            sink += meter.processFrame (shaped, shaped, windowCoefficient);
        }
        ankerl::nanobench::doNotOptimizeAway (sink);
    });

    // -------------------------------------------------------------------------
    //  4. The reference math, measured directly.
    //
    //  These are the two libm calls the approximations would replace. Having them
    //  side by side with the loops above is what turns "the shaper looks
    //  expensive" into "the shaper is N % of a frame, and its tanh calls are M %
    //  of the shaper".
    // -------------------------------------------------------------------------
    bench.run ("std::tanh (reference)", [&]
    {
        float sink = 0.0f;
        for (const auto sample : signal)
            sink += std::tanh (sample * 1.78f);
        ankerl::nanobench::doNotOptimizeAway (sink);
    });

    bench.run ("std::exp (reference)", [&]
    {
        float sink = 0.0f;
        for (const auto sample : signal)
            sink += std::exp (-sample);
        ankerl::nanobench::doNotOptimizeAway (sink);
    });

    // -------------------------------------------------------------------------
    //  5. The approximations the review proposed, measured on the same input.
    //
    //  Only the std:: fallback exists in this translation unit (the harness never
    //  sees chowdsp), so these rows currently measure std:: against std:: - they
    //  are the placeholders that make the comparison a one-line change once the
    //  chowdsp path is wired into the harness build. Read them as "what the
    //  reference costs", not as "what the approximation costs".
    // -------------------------------------------------------------------------
    bench.run ("j37math::exp (shipping scalar path)", [&]
    {
        float sink = 0.0f;
        for (const auto sample : signal)
            sink += j37math::exp (-sample);
        ankerl::nanobench::doNotOptimizeAway (sink);
    });

    bench.run ("j37math::log10 (shipping scalar path)", [&]
    {
        float sink = 0.0f;
        for (const auto sample : signal)
            sink += j37math::log10 (std::fabs (sample) + 1.0e-6f);
        ankerl::nanobench::doNotOptimizeAway (sink);
    });

    // A single representative frame cost for the budget summary. This is the
    // full-frame loop measured above, re-run once outside the harness so the
    // printed headroom figure is a plain number rather than a parsed result.
    {
        float memory = 0.0f;
        GlueCompressor detector;
        LoudnessMeter meter;
        const auto coefficients = makeDetectorCoefficients();
        const auto windowCoefficient = LoudnessMeter::makeWindowCoefficient (kSampleRate);
        float sink = 0.0f;

        const auto start = std::clock();
        constexpr int reps = 200;
        for (int r = 0; r < reps; ++r)
        {
            for (const auto sample : signal)
            {
                const auto shaped = magneticHysteresis (sample, 0.30f, 0.42f, memory);
                memory = shaped;
                sink += detector.processDetection (shaped * shaped, kSampleRate, coefficients);
                sink += meter.processFrame (shaped, shaped, windowCoefficient);
            }
        }
        const auto elapsed = static_cast<double> (std::clock() - start) / CLOCKS_PER_SEC;
        ankerl::nanobench::doNotOptimizeAway (sink);

        const auto frames = static_cast<double> (reps) * static_cast<double> (signal.size());
        reportBudget (elapsed / frames * 1.0e9);
    }

    return 0;
}