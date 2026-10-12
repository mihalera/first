// =============================================================================
//  Regression harness for the RATE BRIDGE - the conversion between the rate the
//  engine is running at and the rate a model was trained at.
//
//  The defect this exists for is one no build can see and no panel can show: the
//  neural stage used to hand its model every engine sample, and the engine runs at
//  the session's rate times its oversampling factor. A .nam model carries its OWN
//  rate - the rate it was trained at, almost always 48 kHz - and a neural net's
//  memory is counted in SAMPLES rather than seconds, so a model fed four times too
//  fast is not a brighter version of itself, it is a different network. Both the
//  old code and the new code compile; neither says anything about the rate.
//
//  What can tell the difference is the sequence the model is HANDED, so that is
//  what is measured here: it has to be the programme at the model's own rate - a
//  220 Hz sine whose samples, read at the model's rate, are a 220 Hz sine. Read at
//  the engine's rate instead, they are 55 Hz, which is the old bug, and both
//  readings are measured so the harness knows which one it is looking at.
//
//  The rest is what a conversion has to do to be worth having: the model's answer
//  has to come back at the engine's rate at the right time and the right level, it
//  has to be late by exactly the delay the bridge declares, and the two legs of the
//  stage's wet/dry crossfade have to stay in phase - a conversion that is faithful
//  but late would turn the NEURAL blend into a comb filter rather than a blend.
//
//  RateBridge is cut verbatim out of Source/PluginProcessor.h by
//  tests/dsp/extract.py, so this harness cannot drift from what the plugin
//  compiles. What it canNOT see is the wiring around it - that a .nam file's
//  `sample_rate` is what reaches it, that the engine's rate follows the
//  oversampling factor, and that the stage asks for the pair at all - which is the
//  build's business and the stage's own text.
// =============================================================================

#include "shim.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "extracted_dsp.inc"

namespace
{
constexpr double kTwoPi = 6.283185307179586;
constexpr float kAmplitude = 0.5f;

/** Long enough that every rate pair below covers many periods of the test tone in
    the measurement window, and cheap enough that the widest ratio - where the
    engine is 34.8 times the model's rate - costs a few tens of thousands of model
    calls rather than a few million. */
constexpr int kSamples = 400000;

/** Where the measurements start and how long they run, as fractions of the run.
    Nothing near the beginning: the bridge says nothing about a model's first few
    answers (see `ready` in RateBridge), and a measurement that included them would
    be measuring the start-up rather than the conversion. */
constexpr double kWindowStart = 0.7;
constexpr double kWindowLength = 0.25;

int failures = 0;

void check (bool condition, const std::string& what)
{
    if (! condition)
    {
        std::printf ("  FAIL  %s\n", what.c_str());
        ++failures;
    }
}

/** A sine at the ENGINE's rate: the programme the bridge is handed. Its pitch is the
    one thing every conversion below has to preserve, and the one thing the bug
    changed. */
struct Sine
{
    Sine (double frequency, double rate) : step (kTwoPi * frequency / rate) {}

    float next()
    {
        const auto value = static_cast<float> (kAmplitude * std::sin (phase));
        phase += step;
        return value;
    }

    double step = 0.0;
    double phase = 0.0;
};

/** What one frequency of a signal comes to: its level, and its phase relative to
    the start of the window it was measured over. The phase is what turns "the
    output is late" into a number in SAMPLES, which is the unit the bridge states
    its own delay in. */
struct Partial
{
    double amplitude = 0.0;
    double phase = 0.0;
};

/** Hann-windowed single-bin DFT of `frequency` over a window of `signal`.

    Windowed because the tone does not land on a window boundary at any of these
    rates, and unwindowed leakage would read as a level error - which is one of the
    things being measured. */
Partial measure (const std::vector<float>& signal, std::size_t start, std::size_t length,
                 double frequency, double rate)
{
    if (length < 4 || start + length > signal.size())
        return {};

    double real = 0.0, imaginary = 0.0, windowSum = 0.0;
    const auto n = static_cast<double> (length - 1);

    for (std::size_t i = 0; i < length; ++i)
    {
        const auto w = 0.5 * (1.0 - std::cos (kTwoPi * static_cast<double> (i) / n));
        const auto phase = kTwoPi * frequency * static_cast<double> (i) / rate;
        const auto sample = static_cast<double> (signal[start + i]);
        windowSum += w;
        real += w * sample * std::cos (phase);
        imaginary -= w * sample * std::sin (phase);
    }

    if (windowSum <= 0.0)
        return {};

    const auto scale = 2.0 / windowSum;
    return { scale * std::sqrt (real * real + imaginary * imaginary),
             std::atan2 (imaginary, real) };
}

/** The model stand-in: one sample in, the same sample out, and a log of every
    sample it was handed.

    What a real model DOES with its sample is not this harness's business - the
    bridge's contract is about the samples it delivers and the timing of the
    answers, not about what a network does with them - so the stand-in is the
    identity and the harness measures where its calls land. */
struct RecordingModel
{
    std::vector<float> inputs;

    float operator() (float input)
    {
        inputs.push_back (input);
        return input;
    }
};

/** One run of the bridge: the programme in, and both legs of its output. */
struct Run
{
    std::vector<float> input;
    std::vector<float> wet;
    std::vector<float> dry;
    std::vector<float> modelInputs;
    double delay = 0.0;
    double step = 1.0;
    bool active = false;
};

Run drive (double engineRate, double modelRate, double frequency, int samples)
{
    RateBridge bridge;
    bridge.prepare (modelRate, engineRate);

    RecordingModel model;
    Sine sine (frequency, engineRate);

    Run run;
    run.delay = bridge.delay();
    run.step = bridge.step;
    run.active = bridge.active;
    run.input.reserve (static_cast<std::size_t> (samples));
    run.wet.reserve (static_cast<std::size_t> (samples));
    run.dry.reserve (static_cast<std::size_t> (samples));

    for (int i = 0; i < samples; ++i)
    {
        const auto x = sine.next();
        const auto converted = bridge.process (x, model);
        run.input.push_back (x);
        run.wet.push_back (converted.wet);
        run.dry.push_back (converted.dry);
    }

    run.modelInputs = std::move (model.inputs);
    return run;
}

/** The rate pairs the engine can actually put in front of a model: the session
    rates, every oversampling factor, and the two directions a model's own rate can
    differ in - a model coarser than the engine (the common case, the engine
    oversampled) and one finer than it. The last entry is the widest ratio this
    build can reach, which is where the bridge's own history is at its longest. */
struct Pair
{
    double engine;
    double model;
    const char* name;
};

const Pair kPairs[]
{
    { 192000.0,  48000.0, "4x oversampling of a 48 kHz session" },
    {  96000.0,  48000.0, "2x oversampling of a 48 kHz session" },
    { 384000.0,  48000.0, "8x oversampling of a 48 kHz session" },
    {  48000.0,  44100.0, "a 44.1 kHz model in a 48 kHz session" },
    { 176400.0,  48000.0, "4x oversampling of a 44.1 kHz session" },
    {  48000.0,  96000.0, "a 96 kHz model with no oversampling" },
    {  48000.0, 192000.0, "a 192 kHz model with no oversampling" },
    { 352800.0,  44100.0, "8x oversampling of a 44.1 kHz session" },
    { 384000.0,  22050.0, "8x oversampling against a 22.05 kHz model" },
    { 1536000.0, 44100.0, "the widest ratio this build can reach" },
};

// -----------------------------------------------------------------------------
//  The two rates agree: the bridge is not there at all.
// -----------------------------------------------------------------------------
void testMatchingRatesAreUntouched()
{
    std::printf ("a model at the engine's own rate is handed the sample itself\n");

    Run run = drive (48000.0, 48000.0, 220.0, 4000);

    check (! run.active, "the bridge reports itself inactive, so nothing is converted");
    check (run.delay == 0.0, "and it adds no delay of its own");
    check (run.modelInputs.size() == run.input.size(),
           "the model is called once per engine sample: "
               + std::to_string (run.modelInputs.size()) + " calls for "
               + std::to_string (run.input.size()) + " samples");

    // Bit-for-bit, not "close": this is the path a session takes whenever its model
    // was trained at the rate the engine runs at, and the stage has to be exactly
    // what it was before the bridge existed.
    bool exact = true;

    for (std::size_t i = 0; i < run.input.size(); ++i)
        exact = exact && run.wet[i] == run.input[i] && run.dry[i] == run.input[i];

    check (exact, "every sample comes out bit-for-bit as it went in, on both legs");
}

// -----------------------------------------------------------------------------
//  The report: the model has to be driven at ITS OWN rate.
// -----------------------------------------------------------------------------
void testTheModelIsDrivenAtItsOwnRate()
{
    std::printf ("the model is driven at its own rate, not the engine's\n");

    const double frequency = 220.0;

    for (const auto& pair : kPairs)
    {
        Run run = drive (pair.engine, pair.model, frequency, kSamples);

        check (run.active, std::string (pair.name) + ": the bridge converts at all");

        // The count is the ratio, less the bridge's own lookahead: the model is a
        // couple of engine samples behind the signal it is being handed (see
        // RateBridge::lookahead), which at an upsampling ratio is `lookahead`
        // MODEL samples rather than two - so the tolerance has to be in model
        // samples too, with the frame at each end of the run on top.
        const auto ratio = pair.model / pair.engine;
        const auto expectedCalls = static_cast<double> (kSamples) * ratio;
        const auto tolerance = 2.0 * ratio + 4.0;
        const auto calls = static_cast<double> (run.modelInputs.size());

        check (std::abs (calls - expectedCalls) <= tolerance,
               std::string (pair.name) + ": the model is called "
                   + std::to_string (run.modelInputs.size()) + " times against "
                   + std::to_string (static_cast<int> (expectedCalls))
                   + " expected, so it is being driven at its own rate (tolerance "
                   + std::to_string (static_cast<int> (tolerance)) + ")");

        // The samples the model was HANDED, read at its own rate, have to be the
        // programme: the same pitch and the same waveform. Read at the engine's rate
        // they are the old bug - the whole point of the conversion - and both
        // readings are measured so this cannot pass by accident.
        double worstAtModelRate = 0.0;
        double worstAtEngineRate = 0.0;

        for (std::size_t m = 10; m + 5 < run.modelInputs.size(); ++m)
        {
            const auto atModelRate = kAmplitude
                * std::sin (kTwoPi * frequency * static_cast<double> (m) / pair.model);
            const auto atEngineRate = kAmplitude
                * std::sin (kTwoPi * frequency * static_cast<double> (m) / pair.engine);

            worstAtModelRate = std::max (worstAtModelRate,
                                         std::abs (static_cast<double> (run.modelInputs[m])
                                                   - atModelRate));
            worstAtEngineRate = std::max (worstAtEngineRate,
                                          std::abs (static_cast<double> (run.modelInputs[m])
                                                    - atEngineRate));
        }

        check (worstAtModelRate <= 1.0e-3,
               std::string (pair.name) + ": the model is handed the programme at the model's own rate"
                   + " (worst sample " + std::to_string (worstAtModelRate) + ")");
        check (worstAtEngineRate >= 1.0e-2,
               std::string (pair.name) + ": and that is not what the engine's rate would have handed it"
                   + " (worst sample " + std::to_string (worstAtEngineRate) + ")");
    }
}

// -----------------------------------------------------------------------------
//  And it has to come back: at the engine's rate, at the same level, at the
//  declared delay, with the two legs of the crossfade in phase.
// -----------------------------------------------------------------------------
void testTheAnswerComesBackAtTheEngineRate()
{
    std::printf ("the model's answer comes back at the engine's rate, in phase and at level\n");

    const double frequency = 220.0;
    const auto start = static_cast<std::size_t> (kSamples * kWindowStart);
    const auto length = static_cast<std::size_t> (kSamples * kWindowLength);

    for (const auto& pair : kPairs)
    {
        Run run = drive (pair.engine, pair.model, frequency, kSamples);

        const auto input = measure (run.input, start, length, frequency, pair.engine);
        const auto wet = measure (run.wet, start, length, frequency, pair.engine);

        check (input.amplitude > 0.1,
               std::string (pair.name) + ": the harness is measuring a real tone");

        // 1. Level. The conversion has to be a rate change and nothing else: a
        //    cubic interpolation of a band-limited signal is not exactly unity, but
        //    a percent is two orders of magnitude more than its own error.
        check (std::abs (wet.amplitude - input.amplitude) <= 0.01 * input.amplitude,
               std::string (pair.name) + ": the model's answer comes back at the level it went in at ("
                   + std::to_string (wet.amplitude) + " against " + std::to_string (input.amplitude) + ")");

        // 2. Time. The phase difference between what went in and what came out, in
        //    seconds, is the delay - and the bridge declares its own, so the two
        //    together are a measurement rather than a restatement. The tone is low
        //    enough that the delay is a small fraction of a period at every rate
        //    here, so the comparison is unambiguous.
        const auto latency = -(wet.phase - input.phase) * pair.engine / (kTwoPi * frequency);

        check (std::abs (latency - run.delay) <= 0.05,
               std::string (pair.name) + ": and it is late by exactly the delay it declares (measured "
                   + std::to_string (latency) + " samples against " + std::to_string (run.delay) + ")");

        // 3. Phase between the legs. The stage crossfades the bridge's wet leg
        //    against its dry one, so those two have to be the same signal at the
        //    same time: a wet leg that arrived a few samples early or late would
        //    turn the NEURAL blend into a comb filter, which is a defect the level
        //    and the delay checks above could not see at all.
        double worst = 0.0;

        for (std::size_t i = start; i < start + length; ++i)
            worst = std::max (worst, std::abs (static_cast<double> (run.wet[i])
                                               - static_cast<double> (run.dry[i])));

        check (worst <= 2.0e-3,
               std::string (pair.name) + ": the wet and dry legs of the crossfade are in phase"
                   + " (worst sample " + std::to_string (worst) + ")");
    }
}

// -----------------------------------------------------------------------------
//  A conversion that is a rate change and nothing else, at the level of the
//  samples rather than of the tone.
// -----------------------------------------------------------------------------
void testTheConversionIsBoundedAndFaithful()
{
    std::printf ("the conversion neither gains a level nor drifts, and its delay is bounded\n");

    for (const auto& pair : kPairs)
    {
        Run run = drive (pair.engine, pair.model, 220.0, 20000);

        // Every leg the bridge emits is inside the range of what it was handed,
        // give or take the interpolator's own ringing: a four-tap cubic passing
        // through four samples of a sine can sit a fraction of a percent outside
        // them, and that is what a band-limited interpolation IS rather than a
        // fault. A percent is an order of magnitude above that error and orders
        // below anything that would matter in a signal path - what this check is
        // for is a conversion that gains level, which would show up as tens of
        // percent. Both legs are checked because the dry one is interpolated too:
        // that is the whole point of the two of them sharing a kernel.
        const auto ceiling = kAmplitude * 1.01f;
        double worst = 0.0;

        for (std::size_t i = 0; i < run.wet.size(); ++i)
            worst = std::max (worst, static_cast<double> (std::max (std::abs (run.wet[i]),
                                                                   std::abs (run.dry[i]))));

        check (worst <= ceiling,
               std::string (pair.name) + ": neither leg overshoots the signal that went in (worst "
                   + std::to_string (worst) + " against " + std::to_string (kAmplitude) + ")");

        // And the delay is stated in engine samples and has to stay inside the
        // history the bridge keeps for it - the failure mode being a conversion
        // that reads a sample it has already overwritten, which is what the widest
        // ratio in the table above exists to catch.
        check (run.delay <= static_cast<double> (RateBridge::xSize) - 1.0 - std::ceil (1.0 / run.step),
               std::string (pair.name) + ": its delay fits the engine-side history ("
                   + std::to_string (run.delay) + " against " + std::to_string (RateBridge::xSize) + ")");
    }
}

/** The stage's own guards, exercised without a model: with the two rates equal the
    bridge must not be a conversion with a pass-through branch but a pass-through,
    and `reset()` has to leave it as good as new. */
void testTheBridgeIsReusable()
{
    std::printf ("the bridge can be re-prepared and reset without carrying anything over\n");

    RateBridge bridge;
    RecordingModel model;

    bridge.prepare (48000.0, 192000.0);
    check (bridge.active, "a model at a quarter of the engine's rate is converted");

    for (int i = 0; i < 500; ++i)
        bridge.process (0.5f, model);

    bridge.reset();

    // A fresh bridge and a reset one have to behave identically: the history is
    // the conversion's memory, and a stale one would smear the last session's
    // samples into this one.
    RateBridge fresh;
    fresh.prepare (48000.0, 192000.0);
    RecordingModel other;

    bool identical = true;

    for (int i = 0; i < 500; ++i)
    {
        const auto x = static_cast<float> (std::sin (0.01 * i));
        const auto reset = bridge.process (x, model);
        const auto brandNew = fresh.process (x, other);

        identical = identical && reset.wet == brandNew.wet && reset.dry == brandNew.dry;
    }

    check (identical, "a reset bridge and a fresh one answer identically");

    bridge.prepare (48000.0, 48000.0);
    check (! bridge.active, "and re-preparing at a matching rate takes the conversion away");
    check (bridge.delay() == 0.0, "with no delay left behind");
}

int run()
{
    testMatchingRatesAreUntouched();
    testTheModelIsDrivenAtItsOwnRate();
    testTheAnswerComesBackAtTheEngineRate();
    testTheConversionIsBoundedAndFaithful();
    testTheBridgeIsReusable();

    if (failures == 0)
        std::printf ("NEURAL RATE CHECK PASSED (0 failures)\n");
    else
        std::printf ("NEURAL RATE CHECK FAILED (%d)\n", failures);

    return failures == 0 ? 0 : 1;
}
} // namespace

int main() { return run(); }
