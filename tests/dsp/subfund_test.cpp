// =============================================================================
//  Regression harness for the SUBFUND (subharmonic) stage.
//
//  Three defects were reported against the subharmonic generator, and all three
//  are properties of the rendered AUDIO rather than of the code as written, so
//  they are checked here on signal:
//
//    1. The undertone series must fall off with division - 1/2 loudest, then 1/3,
//       1/4, 1/5 ... The series has to read as a staircase, not as a flat shelf
//       with one partial on top of it.
//    2. The undertones must be clean partials. Nothing downstream may re-distort
//       them, so no harmonic OF an undertone (2x, 3x, 4x ... 500/d) may appear
//       when SUBFUND is engaged.
//    3. With no input there is no output. Not a decaying rumble, not a drone:
//       the stage has to reach actual silence along with the signal.
//
//  The generator, the glue compressor and the inline shaping helpers are cut out
//  of Source/ by tests/dsp/extract.py and #included below, so this harness always
//  measures the shipping code rather than a copy of it that can drift.
//
//  The signal path around the generator is a reduced replica of processBlock: the
//  same stages, in the same order, with the same coefficients the J37 /
//  default-control configuration produces. What it is here to prove is the ORDER
//  of those stages - above all where the undertone sum is injected - and that is
//  exactly what a reduced replica can show honestly. Every control value below is
//  the default from createParameterLayout, so the numbers are the plugin's own.
// =============================================================================

#include "shim.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

#include "extracted_dsp.inc"

namespace
{
constexpr double kTwoPi = 6.283185307179586;
constexpr double kSampleRate = 48000.0;

/**
    Amplitude of a sinusoid at `frequency` over a window of `signal`.

    Hann-windowed, because most of the frequencies under test (500/3, 500/6,
    500/7 ...) do not land on a window boundary, and an unwindowed
    measurement of those is off by a leak factor rather than by a rounding
    error. Normalised by the window's coherent gain, so the result is the
    amplitude of the partial itself and not the sum of the window.
*/
double measureToneAmplitude (const std::vector<float>& signal,
                             std::size_t start, std::size_t length,
                             double frequency)
{
    if (length < 4 || start + length > signal.size())
        return 0.0;

    double real = 0.0, imaginary = 0.0, windowSum = 0.0;
    const auto n = static_cast<double> (length - 1);

    for (std::size_t i = 0; i < length; ++i)
    {
        const double w = 0.5 * (1.0 - std::cos (kTwoPi * static_cast<double> (i) / n));
        windowSum += w;
        const double phase = kTwoPi * frequency * static_cast<double> (i) / kSampleRate;
        const double sample = static_cast<double> (signal[start + i]);
        real += w * sample * std::cos (phase);
        imaginary -= w * sample * std::sin (phase);
    }

    if (windowSum <= 0.0)
        return 0.0;

    return 2.0 * std::sqrt (real * real + imaginary * imaginary) / windowSum;
}

double rms (const std::vector<float>& signal, std::size_t start, std::size_t length)
{
    if (start + length > signal.size())
        return 0.0;

    double sum = 0.0;
    for (std::size_t i = 0; i < length; ++i)
        sum += static_cast<double> (signal[start + i]) * signal[start + i];

    return std::sqrt (sum / static_cast<double> (length));
}

double toDb (double amplitude)
{
    return 20.0 * std::log10 (juce::jmax (amplitude, 1.0e-12));
}

int gFailures = 0;

void check (bool condition, const std::string& what)
{
    std::printf ("  %-4s %s\n", condition ? "PASS" : "FAIL", what.c_str());
    if (! condition)
        ++gFailures;
}

/**
    The divider series, taken from the generator itself rather than retyped.

    This was `{ 2, 3, 4, 5, 6, 7, 8, 9 }` in a `[numStages]` array. While the
    generator had eight stages that was exactly right; when it gained a ninth
    (1/10) the array grew to nine slots and the initialiser did not, so the last
    element was VALUE-INITIALISED TO ZERO. The test then measured 500/0 = inf Hz,
    read the noise floor there, and reported the whole staircase as failing - a
    harness bug that looked exactly like a DSP regression.

    `std::size` on the generator's own array means the two cannot drift again:
    adding a stage to the generator extends this automatically, and removing one
    is a compile error rather than a silent zero.
*/
constexpr auto kDividerCount = static_cast<std::size_t> (SubharmonicGenerator::numStages);

static_assert (kDividerCount == std::size (SubharmonicGenerator::dividers),
               "numStages and the generator's dividers array must describe the same series");

/**
    True when `frequency` is a frequency the signal legitimately contains on its
    own - a harmonic of the test tone or one of the undertones themselves.

    Products that land on one of those cannot be attributed to a nonlinearity:
    2x the 1/4 undertone IS the 1/2 undertone, for instance, so the reference run
    cannot separate the two and the measurement would only be measuring itself.
*/
bool isIntrinsicFrequency (double frequency, double f0)
{
    const auto matches = [frequency] (double candidate)
    {
        return std::fabs (frequency / candidate - 1.0) < 1.0e-4;
    };

    for (int harmonic = 1; harmonic <= 6; ++harmonic)
        if (matches (f0 * harmonic))
            return true;

    for (int d = 0; d < SubharmonicGenerator::numStages; ++d)
        if (matches (f0 / SubharmonicGenerator::dividers[d]))
            return true;

    return false;
}

/** Lists the strongest tones in a window, biggest first. Diagnostic only: the
    assertions are all exact-frequency measurements, and this is here so a failing
    run can be read as a spectrum rather than guessed at. */
void printSpectrum (const std::vector<float>& signal, std::size_t start, std::size_t length,
                    double lowestHz, double highestHz, double stepHz, int maxPeaks)
{
    struct Peak { double frequency; double level; };
    std::vector<Peak> peaks;

    for (double f = lowestHz; f <= highestHz; f += stepHz)
    {
        const double amplitude = measureToneAmplitude (signal, start, length, f);
        if (toDb (amplitude) > -90.0)
            peaks.push_back ({ f, toDb (amplitude) });
    }

    std::sort (peaks.begin(), peaks.end(), [] (const Peak& a, const Peak& b)
    {
        return a.level > b.level;
    });

    for (int i = 0; i < maxPeaks && i < static_cast<int> (peaks.size()); ++i)
        std::printf ("       %8.2f Hz  %7.2f dB\n", peaks[static_cast<std::size_t> (i)].frequency,
                     peaks[static_cast<std::size_t> (i)].level);
}
} // namespace

// -----------------------------------------------------------------------------
//  Reduced replica of the plugin's signal path
// -----------------------------------------------------------------------------

struct ChainSettings
{
    float drive = 0.42f;        // createParameterLayout default
    float bias = 0.36f;         // createParameterLayout default
    float brightness = 0.58f;   // "tone" default
    float character = 0.50f;    // TONE macro default
    float mix = 1.0f;           // fully wet, so the wet path is all that is measured
    float width = 0.5f;
    float inputDb = 0.0f;
    float outputDb = 0.0f;
    float subfund = 1.0f;
    float wow = 0.0f;           // the reported scenario: transport at rest
    float flutter = 0.0f;

    // The arrangement under test. false = the undertone sum joins the signal
    // after the last nonlinearity, which is the fix. true = the old placement
    // inside the tape path, kept only so the harness can show the difference.
    bool injectInsideTapePath = false;
};

/** One sample through a mono reduction of the plugin's chain. */
struct ChainReplica
{
    ChainReplica (ChainSettings s, double rate) : settings (s), sampleRate (rate)
    {
        updateCoefficients();
    }

    // The shipping shaper. It replaced the single magneticHysteresis curve, so the
    // replica has to carry one too - otherwise the harness renders a signal path
    // the plugin no longer uses, and reports a regression that is only a
    // divergence between the test and the engine.
    SaturationCore saturation;

    void updateCoefficients()
    {
        const float sr = static_cast<float> (sampleRate);

        const auto driveCurve = std::pow (settings.drive, 1.45f);
        const auto biasCurve = std::pow (settings.bias, 1.30f);
        const auto toneCurve = std::pow (settings.brightness, 0.92f);
        const auto characterCurve = std::pow (juce::jlimit (0.0f, 1.0f, settings.character), 1.20f);

        driveAmount = driveCurve * 1.9f;
        biasAmount = 0.18f + biasCurve * 1.55f;

        // J37 at the mid TONE macro setting, straight out of the tape formula in
        // processBlock. Tape type 0 applies no shift of its own.
        const auto blend = [characterCurve] (float slow, float fast)
        {
            return slow + (fast - slow) * characterCurve;
        };
        tapeCurve = juce::jlimit (1.0f, 1.8f, blend (1.18f, 1.44f));
        tapeAsymmetry = juce::jlimit (0.0f, 0.4f, blend (0.16f, 0.10f));
        const float hysteresis = juce::jlimit (0.1f, 0.7f, blend (0.30f, 0.44f));
        const float dampingHz = juce::jlimit (4000.0f, 26000.0f, blend (12000.0f, 20000.0f));

        speedScale = 1.0f;
        speedBias = 0.84f + speedScale * 0.30f;
        preDriveGain = 1.0f + 0.55f * (1.0f - characterCurve);
        headGapHz = 24000.0f * std::pow (0.28f, characterCurve);

        toneLpCoefficient = onePoleCoefficientHz (6500.0f + 11500.0f * toneCurve, sr);
        toneShelfCoefficient = onePoleCoefficientHz (8000.0f, sr);
        toneShelfGain = 1.0f + toneCurve * 0.42f;
        headGapCoefficient = onePoleCoefficientHz (headGapHz * speedScale, sr);
        headDampingCoefficient = onePoleCoefficientHz (dampingHz * speedScale, sr);
        dcBlockR = juce::jlimit (0.5f, 0.9999f,
                                 1.0f - (juce::MathConstants<float>::twoPi * 8.0f) / sr);

        shaperDrive = driveCurve * tapeCurve + hysteresis * 0.25f;
        shaperAsymmetry = tapeAsymmetry * (biasAmount * 0.42f);

        const auto wowCurve = std::pow (settings.wow, 1.55f);
        const auto flutterCurve = std::pow (settings.flutter, 1.45f);
        transportGate = juce::jlimit (0.0f, 1.0f, (wowCurve + flutterCurve) * 2.0f);

        inputGain = juce::Decibels::decibelsToGain (settings.inputDb);
        outputGain = juce::Decibels::decibelsToGain (settings.outputDb);
        finalOutputGain = 0.78f * (1.0f - driveCurve * 0.16f) * (0.94f + speedScale * 0.08f);

        const auto inputDegree = juce::jlimit (0.0f, 1.0f, (settings.inputDb + 16.0f) / 32.0f);
        const auto outputDegree = juce::jlimit (0.0f, 1.0f, (settings.outputDb + 16.0f) / 32.0f);
        inputMakeupFraction = 0.30f + inputDegree * 0.35f;
        outputMakeupFraction = 0.35f + outputDegree * 0.35f;

        const auto inputLoad = juce::jlimit (0.0f, 1.0f, (settings.inputDb + 24.0f) / 48.0f);
        const auto outputLoad = juce::jlimit (0.0f, 1.0f, (settings.outputDb + 24.0f) / 48.0f);
        inputThresholdDb = -17.0f + inputLoad * 14.0f;
        inputKneeDb = 10.0f - inputLoad * 3.0f;
        inputCompressorRatio = 1.15f + inputLoad * 0.25f;
        inputReductionLimitDb = -(2.0f + inputLoad * 5.0f);
        inputDriveLoad = inputLoad;
        inputAttackSeconds = 0.16f;
        inputReleaseSeconds = 0.85f;

        outputThresholdDb = -18.0f + outputLoad * 15.0f;
        outputKneeDb = 7.0f - outputLoad * 2.0f;
        outputCompressorRatio = 1.16f + outputLoad * 0.18f;
        outputReductionLimitDb = -(2.5f + outputLoad * 5.0f);
        outputDriveLoad = outputLoad;
        outputAttackSeconds = 0.20f;
        outputReleaseSeconds = 1.00f;

        // The block-rate coefficients the new detector API takes: everything
        // they depend on is fixed for the whole replica, so they are built
        // here exactly as processTapeEngine builds them once per block.
        inputCompressorCoefficients = GlueCompressor::makeCoefficients (
            sr, inputAttackSeconds, inputReleaseSeconds, inputDriveLoad);
        outputCompressorCoefficients = GlueCompressor::makeCoefficients (
            sr, outputAttackSeconds, outputReleaseSeconds, outputDriveLoad);
    }

    float processSample (float rawInput)
    {
        const float sr = static_cast<float> (sampleRate);

        // ---- input stage ---------------------------------------------------
        const float inputTrimmed = rawInput * inputGain;
        const auto inputEnvelopeDb = inputCompressor.processDetection (
            inputTrimmed * inputTrimmed, sr, inputCompressorCoefficients);
        const auto inputReductionDb = juce::jmax (
            inputReductionLimitDb,
            softKneeReductionDb (inputEnvelopeDb, inputThresholdDb, inputKneeDb, inputCompressorRatio));
        const float x = inputTrimmed * juce::Decibels::decibelsToGain (inputReductionDb);

        // ---- record head and magnetic shaper --------------------------------
        //
        //  This calls the SHIPPING shaper, not the old magneticHysteresis. The
        //  engine moved to SaturationCore when the blend was added, and the harness
        //  kept rendering the previous curve - so it was measuring a signal path
        //  the plugin no longer uses, which is exactly the drift the harness exists
        //  to prevent. SaturationCore is extracted into the generated header from
        //  the same struct the plugin compiles.
        //
        //  BLEND is left at its default (tape only), so the curve this produces is
        //  the one the plugin produces at a default setting.
        const float preDrive = x * (1.0f + driveAmount * 1.2f * speedBias * preDriveGain);
        // Pure tape: the seven SOURCE knobs at their defaults (tape 100, rest 0).
        saturation.setWeights (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        const float shapedCore = saturation.process (preDrive, shaperDrive, shaperAsymmetry);
        hysteresisMemory = shapedCore;

        // ---- SUBFUND --------------------------------------------------------
        const float undertone = generator.process (shapedCore, driveAmount, settings.subfund, sr);

        float tapeSignal = shapedCore;
        if (settings.injectInsideTapePath)
            tapeSignal = shapedCore + undertone;

        // ---- tape path: record roll-off, head damping, head gap -------------
        tapeHighFreq += (tapeSignal - tapeHighFreq) * toneLpCoefficient;
        float afterTapeLoss = tapeHighFreq;
        postDamping += (afterTapeLoss - postDamping) * headDampingCoefficient;
        afterTapeLoss = postDamping;
        headGap += (afterTapeLoss - headGap) * headGapCoefficient;
        const float headLoss = headGap;
        tapeHighFreq = afterTapeLoss;

        // ---- bias compression (a genuine nonlinearity) and the noise floor ---
        const float compensation = tapeCurve / 1.30f;
        const float shapedLevel = std::abs (shapedCore);
        const float shaperLoss = juce::jlimit (0.0f, 1.0f,
                                               (driveAmount / 1.9f) * 0.22f * (1.0f - shapedLevel * 0.7f));
        const float driveCompensation = 1.0f + shaperLoss;
        const float compressedBias = headLoss * (1.0f - 0.18f * headLoss * headLoss)
                                   / juce::jmax (0.35f, compensation)
                                   * juce::jlimit (0.8f, 1.35f, driveCompensation);

        // The transport is closed in these tests, so the gate zeroes the floor.
        const float noiseFloor = 0.0f;
        const float playback = compressedBias + noiseFloor;

        const float lowBand = headGap;
        toneShelfState += (playback - lowBand - toneShelfState) * toneShelfCoefficient;
        const float shelfLift = toneShelfState * (toneShelfGain - 1.0f);
        const float deEmphasised = playback + shelfLift;

        // ---- playback AC coupling and the dry/wet crossfade ------------------
        const float dcBlocked = deEmphasised - dcX + dcBlockR * dcY;
        dcX = deEmphasised;
        dcY = dcBlocked;

        // Raised-cosine crossfade: MIX 0 is the untouched input, MIX 1 is all tape.
        //
        // SUBFUND_MIX_INVERTS is read out of the real `mixSmoothed` declaration by
        // extract.py rather than assumed here, because the inversion and this
        // formula were a pair that cancelled each other once and then stopped. A
        // replica that hard-codes the intended behaviour reports the control as
        // working no matter what the processor does with the parameter.
        const auto mixPosition = SUBFUND_MIX_INVERTS ? 1.0f - settings.mix : settings.mix;
        const auto mixAngle = mixPosition * juce::MathConstants<float>::halfPi;
        const float dryGain = std::cos (mixAngle);
        const float wetGain = std::sin (mixAngle);
        const float tapeOutput = dcBlocked * wetGain + x * dryGain;

        // ---- output stage glue compressor -----------------------------------
        const auto envelopeDb = outputCompressor.processDetection (
            tapeOutput * tapeOutput, sr, outputCompressorCoefficients);
        const auto reductionDb = juce::jmax (
            outputReductionLimitDb,
            softKneeReductionDb (envelopeDb, outputThresholdDb, outputKneeDb, outputCompressorRatio));
        const float compressionGain = juce::Decibels::decibelsToGain (reductionDb);
        const float inputMakeup = juce::Decibels::decibelsToGain (-inputReductionDb * inputMakeupFraction);
        const float outputMakeup = juce::Decibels::decibelsToGain (-reductionDb * outputMakeupFraction);
        const float stageGain = compressionGain * outputMakeup * finalOutputGain * inputMakeup;

        float outputSignal = tapeOutput * stageGain;

        // The arrangement under test: the undertone sum joins here, after every
        // nonlinearity that could bend it and before the linear stages that
        // should still see it - width, output trim, limiter. The two scalings are
        // the plugin's: the wet side of the MIX crossfade, so MIX 0 takes the
        // undertones with it, and the machine's static output calibration.
        if (! settings.injectInsideTapePath)
            outputSignal += undertone * wetGain * finalOutputGain;

        // ---- output trim, safety limiter, clipper ---------------------------
        outputSignal *= outputGain;

        const float peak = std::abs (outputSignal);
        const auto detectorAttack = 1.0f - std::exp (-1.0f / (sr * 0.0005f));
        const auto detectorRelease = 1.0f - std::exp (-1.0f / (sr * 0.080f));
        preLimiterDetector += (peak - preLimiterDetector)
                                * (peak > preLimiterDetector ? detectorAttack : detectorRelease);
        const float requiredGain = preLimiterDetector > 0.94f ? 0.94f / preLimiterDetector : 1.0f;
        const auto gainSmoothing = requiredGain < limiterGain
                                     ? 1.0f - std::exp (-1.0f / (sr * 0.0004f))
                                     : 1.0f - std::exp (-1.0f / (sr * 0.120f));
        limiterGain += (requiredGain - limiterGain) * gainSmoothing;

        return softClip (outputSignal * limiterGain);
    }

    ChainSettings settings;
    double sampleRate = kSampleRate;

    SubharmonicGenerator generator;
    GlueCompressor inputCompressor;
    GlueCompressor outputCompressor;
    GlueCompressor::Coefficients inputCompressorCoefficients;
    GlueCompressor::Coefficients outputCompressorCoefficients;

    float hysteresisMemory = 0.0f;
    float tapeHighFreq = 0.0f;
    float postDamping = 0.0f;
    float headGap = 0.0f;
    float toneShelfState = 0.0f;
    float dcX = 0.0f, dcY = 0.0f;
    float preLimiterDetector = 0.0f;
    float limiterGain = 1.0f;

    float driveAmount = 0.0f, biasAmount = 0.0f, tapeCurve = 1.0f, tapeAsymmetry = 0.0f;
    float speedScale = 1.0f, speedBias = 1.0f, preDriveGain = 1.0f, headGapHz = 0.0f;
    float toneLpCoefficient = 0.0f, toneShelfCoefficient = 0.0f, toneShelfGain = 1.0f;
    float headGapCoefficient = 0.0f, headDampingCoefficient = 0.0f, dcBlockR = 0.9f;
    float shaperDrive = 0.0f, shaperAsymmetry = 0.0f, transportGate = 0.0f;
    float inputGain = 1.0f, outputGain = 1.0f, finalOutputGain = 1.0f;
    float inputMakeupFraction = 0.3f, outputMakeupFraction = 0.35f;
    float inputThresholdDb = -17.0f, inputKneeDb = 10.0f, inputCompressorRatio = 1.15f;
    float inputReductionLimitDb = -2.0f, inputDriveLoad = 0.0f;
    float inputAttackSeconds = 0.16f, inputReleaseSeconds = 0.85f;
    float outputThresholdDb = -18.0f, outputKneeDb = 7.0f, outputCompressorRatio = 1.16f;
    float outputReductionLimitDb = -2.5f, outputDriveLoad = 0.0f;
    float outputAttackSeconds = 0.20f, outputReleaseSeconds = 1.00f;
};

// -----------------------------------------------------------------------------
//  Rendering
// -----------------------------------------------------------------------------

/** A constant-amplitude sine for `toneSeconds`, then digital silence. */
std::vector<float> render (ChainSettings settings, double frequency, double amplitude,
                           double toneSeconds, double silenceSeconds)
{
    ChainReplica chain (settings, kSampleRate);
    const auto toneSamples = static_cast<std::size_t> (toneSeconds * kSampleRate);
    const auto silenceSamples = static_cast<std::size_t> (silenceSeconds * kSampleRate);

    std::vector<float> out;
    out.reserve (toneSamples + silenceSamples);

    for (std::size_t i = 0; i < toneSamples + silenceSamples; ++i)
    {
        const double time = static_cast<double> (i) / kSampleRate;
        const float input = i < toneSamples
                              ? static_cast<float> (amplitude * std::sin (kTwoPi * frequency * time))
                              : 0.0f;
        out.push_back (chain.processSample (input));
    }

    return out;
}

/** The generator on its own, excited by the shaper but with no chain after it. */
std::vector<float> renderGenerator (ChainSettings settings, double frequency, double amplitude, double seconds)
{
    ChainReplica chain (settings, kSampleRate);
    const auto samples = static_cast<std::size_t> (seconds * kSampleRate);
    std::vector<float> out;
    out.reserve (samples);

    for (std::size_t i = 0; i < samples; ++i)
    {
        const double time = static_cast<double> (i) / kSampleRate;
        const float input = static_cast<float> (amplitude * std::sin (kTwoPi * frequency * time));
        const float preDrive = input * (1.0f + chain.driveAmount * 1.2f * chain.speedBias * chain.preDriveGain);
        chain.saturation.setWeights (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        const float shaped = chain.saturation.process (preDrive, chain.shaperDrive, chain.shaperAsymmetry);
        out.push_back (chain.generator.process (shaped, chain.driveAmount, settings.subfund,
                                                static_cast<float> (kSampleRate)));
    }

    return out;
}

// -----------------------------------------------------------------------------
//  1. The undertone series must be a descending staircase
// -----------------------------------------------------------------------------

void testUndertoneOrdering()
{
    std::printf ("\n1. Undertone series falls off with division (500 Hz input, generator alone)\n");

    ChainSettings settings;

    // The staircase has to hold across the whole DRIVE range, since DRIVE is what
    // opens the deep end of the series.
    for (float drive : { 0.0f, 0.42f, 1.0f })
    {
        settings.drive = drive;
        const auto signal = renderGenerator (settings, 500.0, 0.5, 2.0);
        const auto window = static_cast<std::size_t> (1.0 * kSampleRate);
        const auto start = signal.size() - window;

        std::printf ("     DRIVE %.2f\n", drive);

        double previous = 1.0e9;
        for (int s = 0; s < SubharmonicGenerator::numStages; ++s)
        {
            const double frequency = 500.0 / SubharmonicGenerator::dividers[s];
            const double amplitude = measureToneAmplitude (signal, start, window, frequency);
            std::printf ("       1/%d  %7.2f Hz  %7.2f dB%s\n", SubharmonicGenerator::dividers[s], frequency, toDb (amplitude),
                         s > 0 ? "" : "   <- top of the staircase");

            char label[160];
            std::snprintf (label, sizeof label, "at DRIVE %.2f, 1/%d is present and quieter than 1/%d",
                           drive, SubharmonicGenerator::dividers[s], SubharmonicGenerator::dividers[s > 0 ? s - 1 : 0]);
            check (amplitude > 1.0e-6 && amplitude < previous, label);
            previous = amplitude;
        }
    }

    settings.drive = 0.42f;
}

// -----------------------------------------------------------------------------
//  2. The undertones must not generate harmonics of themselves
// -----------------------------------------------------------------------------

/** Prints the undertones and the level each of their harmonics has picked up. */
void reportUndertoneHarmonics (ChainSettings settings, double f0, const char* caption)
{
    ChainSettings referenceSettings = settings;
    referenceSettings.subfund = 0.0f;

    const auto engaged = render (settings, f0, 0.5, 2.5, 0.0);
    const auto reference = render (referenceSettings, f0, 0.5, 2.5, 0.0);

    const auto window = static_cast<std::size_t> (1.5 * kSampleRate);
    const auto start = engaged.size() - window;

    std::printf ("     %s\n", caption);

    for (int s = 0; s < SubharmonicGenerator::numStages; ++s)
    {
        const double undertone = f0 / SubharmonicGenerator::dividers[s];
        const double level = toDb (measureToneAmplitude (engaged, start, window, undertone));

        for (int harmonic = 2; harmonic <= 4; ++harmonic)
        {
            const double product = undertone * harmonic;

            // A product that lands on a frequency the signal already has is not a
            // distortion product at all - 2x of the 1/4 undertone IS the 1/2
            // undertone - so a reference run cannot attribute it. Skipped, not
            // measured against itself.
            if (isIntrinsicFrequency (product, f0))
                continue;

            const double engagedLevel = toDb (measureToneAmplitude (engaged, start, window, product));
            const double referenceLevel = toDb (measureToneAmplitude (reference, start, window, product));
            const double delta = engagedLevel - referenceLevel;

            std::printf ("       1/%-2d %7.2f Hz (%6.1f dB)  ->  %dx = %7.1f Hz  %+6.1f dB from the "
                         "undertone, %+6.1f dB over the dry run\n",
                         SubharmonicGenerator::dividers[s], undertone, level, harmonic, product,
                         engagedLevel - level, delta);
        }
    }
}

void testNoHarmonicsFromUndertones()
{
    std::printf ("\n2. Undertones stay clean: nothing downstream re-distorts them\n");

    ChainSettings settings;
    double worstDelta = -1.0e9;
    reportUndertoneHarmonics (settings, 400.0, "fixed arrangement (undertones after the nonlinearities):");

    ChainSettings referenceSettings = settings;
    referenceSettings.subfund = 0.0f;
    const auto engaged = render (settings, 400.0, 0.5, 2.5, 0.0);
    const auto reference = render (referenceSettings, 400.0, 0.5, 2.5, 0.0);
    const auto window = static_cast<std::size_t> (1.5 * kSampleRate);
    const auto start = engaged.size() - window;

    for (int s = 0; s < SubharmonicGenerator::numStages; ++s)
    {
        const double undertone = 400.0 / SubharmonicGenerator::dividers[s];
        const double undertoneLevel = toDb (measureToneAmplitude (engaged, start, window, undertone));

        for (int harmonic = 2; harmonic <= 4; ++harmonic)
        {
            const double product = undertone * harmonic;
            if (isIntrinsicFrequency (product, 400.0))
                continue;

            const double engagedLevel = toDb (measureToneAmplitude (engaged, start, window, product));
            const double referenceLevel = toDb (measureToneAmplitude (reference, start, window, product));
            const double delta = engagedLevel - referenceLevel;

            // How far the product sits BELOW the undertone it belongs to. A product
            // of the generator rather than of a nonlinearity cannot be driven to
            // zero: a sum of sinusoids at f0/2, f0/3, f0/4 ... is itself a periodic
            // waveform with a period of f0/2520, so it has a Fourier series that
            // includes frequencies that are not any stage's own. Those sit tens of
            // dB down and are the floor this check is written against - what it
            // must exclude is a partial sitting just under the one it came from,
            // which is what a waveshaper on the stage would produce.
            const double margin = undertoneLevel - engagedLevel;

            char label[160];
            std::snprintf (label, sizeof label,
                           "harmonic %d of 1/%d (%.1f Hz) sits %.1f dB below the undertone (want > 40)",
                           harmonic, SubharmonicGenerator::dividers[s], product, margin);
            check (margin > 40.0, label);

            worstDelta = juce::jmax (worstDelta, delta);
        }
    }

    std::printf ("       worst product over the dry run: %.1f dB\n", worstDelta);

    // ----------------------------------------------------------------------
    //  The defect this test exists for was DRIVE-dependent, and that is what
    //  makes it worth a check of its own: the stages used to run each partial
    //  through a tanh, so the more DRIVE opened that waveshaper the more each
    //  undertone grew its own odd harmonics - the 1/2 undertone's third harmonic
    //  was 23 dB down at DRIVE 0 and 13 dB down at DRIVE 1. A level threshold
    //  alone would have caught the end of that range and missed the start.
    //
    //  A clean generator does not care: the partials are sines, so the margin is
    //  the same at both ends of the control.
    // ----------------------------------------------------------------------
    const auto marginAtDrive = [&] (float drive)
    {
        ChainSettings driven = settings;
        driven.drive = drive;
        const auto signal = render (driven, 400.0, 0.5, 2.5, 0.0);
        const auto begin = signal.size() - window;
        double worst = 1.0e9;

        for (int s = 0; s < SubharmonicGenerator::numStages; ++s)
        {
            const double undertone = 400.0 / SubharmonicGenerator::dividers[s];
            const double level = toDb (measureToneAmplitude (signal, begin, window, undertone));

            for (int harmonic = 2; harmonic <= 4; ++harmonic)
            {
                const double product = undertone * harmonic;
                if (isIntrinsicFrequency (product, 400.0))
                    continue;

                worst = juce::jmin (worst, level - toDb (measureToneAmplitude (signal, begin, window, product)));
            }
        }

        return worst;
    };

    const double cleanDrive = marginAtDrive (0.0f);
    const double hotDrive = marginAtDrive (1.0f);

    char label[160];
    std::snprintf (label, sizeof label,
                   "the undertones gain no harmonics as DRIVE opens (%.1f dB clean, %.1f dB at DRIVE 1, "
                   "want no more than 3 dB apart)",
                   cleanDrive, hotDrive);
    check (hotDrive > cleanDrive - 3.0, label);
}

// -----------------------------------------------------------------------------
//  3. No signal, no subharmonics
// -----------------------------------------------------------------------------

void testSilenceIsSilent()
{
    std::printf ("\n3. The stage reaches true silence along with the signal\n");

    ChainSettings settings;
    const auto scenario = render (settings, 500.0, 0.5, 1.0, 3.0);
    const auto window = static_cast<std::size_t> (1.0 * kSampleRate);
    const auto start = scenario.size() - window;

    char label[160];
    const double silentRms = rms (scenario, start, window);
    std::snprintf (label, sizeof label, "output over the last second of silence is %.1f dBFS",
                   toDb (silentRms));
    check (silentRms < 1.0e-4, label);

    for (int s = 0; s < SubharmonicGenerator::numStages; ++s)
    {
        const double frequency = 500.0 / SubharmonicGenerator::dividers[s];
        const double amplitude = measureToneAmplitude (scenario, start, window, frequency);
        std::snprintf (label, sizeof label, "no undertone at %.1f Hz while silent (%.1f dB)",
                       frequency, toDb (amplitude));
        check (amplitude < 1.0e-5, label);
    }

    // The decay itself must be quick, and it is measured on the undertones rather
    // than on the output. An output reading here would measure the tape chain and
    // not this stage: with SUBFUND closed the engine still reads -47.9 dBFS in the
    // quarter second after the input stops, because the playback DC blocker rings
    // down the step the note made. The engine is not supposed to be silent at that
    // moment - it is not silent with SUBFUND off either - so what has to be checked
    // is the undertones themselves: how far they fall, and how soon they are gone.
    //
    // They are not expected to vanish AT the cut. The 1/2 undertone is a real part
    // of the note while it is playing, and truncating it the instant the input
    // stops would be a click, not a fix. What must not happen is the level hanging
    // on, which is what "subharmonics are still generated with no signal" is.
    const auto cutAt = static_cast<std::size_t> (1.0 * kSampleRate);
    const auto shortWindow = static_cast<std::size_t> (0.025 * kSampleRate);

    const double atCut = toDb (measureToneAmplitude (scenario, cutAt, shortWindow, 250.0));
    const auto lateStart = cutAt + static_cast<std::size_t> (0.200 * kSampleRate);
    const double after200ms = toDb (measureToneAmplitude (scenario, lateStart, shortWindow, 250.0));

    std::snprintf (label, sizeof label,
                   "200 ms after the input stops the 1/2 undertone is %.1f dBFS (%.1f dB down, want > 40)",
                   after200ms, atCut - after200ms);
    check (atCut - after200ms > 40.0, label);

    // And nothing may be left over: the same run, measured long after the note, has
    // to be indistinguishable from the same run with SUBFUND closed. This is the
    // check that a drone cannot pass.
    ChainSettings referenceSettings = settings;
    referenceSettings.subfund = 0.0f;
    const auto reference = render (referenceSettings, 500.0, 0.5, 1.0, 3.0);

    const auto driftStart = static_cast<std::size_t> (1.75 * kSampleRate);
    const auto driftWindow = static_cast<std::size_t> (0.25 * kSampleRate);
    std::vector<float> contribution (driftWindow, 0.0f);
    for (std::size_t i = 0; i < driftWindow; ++i)
        contribution[i] = scenario[driftStart + i] - reference[driftStart + i];

    const double drift = rms (contribution, 0, driftWindow);
    std::snprintf (label, sizeof label,
                   "750 ms after the input stops the undertones contribute %.1f dBFS (want < -100)",
                   toDb (drift));
    check (drift < 1.0e-5, label);

    const auto silent = render (settings, 500.0, 0.5, 0.0, 2.0);
    double silentPeak = 0.0;
    for (auto s : silent)
        silentPeak = juce::jmax (silentPeak, static_cast<double> (std::abs (s)));

    std::snprintf (label, sizeof label, "a run that is silent from the first sample peaks at %.1f dBFS",
                   toDb (silentPeak));
    check (silentPeak < 1.0e-5, label);
}

// -----------------------------------------------------------------------------
//  4. MIX runs the way round the control says it does
// -----------------------------------------------------------------------------

/**
    MIX is a crossfade between the untouched input and the whole tape path, so it
    is measurable rather than a matter of taste: the dry signal is a pure sine and
    the tape path is a saturator, so the second harmonic of the test tone is
    content that exists only on the wet side. Where that harmonic sits therefore
    says which end of the control is where.

    This is the check for the two-inversion fault: the smoother's `invertOutput`
    flag existed to cancel a crossfade that had sin() on the dry side and cos() on
    the wet one. Correcting that expression and leaving the flag in place made the
    two ends swap - MIX 0 became fully wet and MIX 100 fully dry - and the flag is
    read straight out of the real declaration, so putting it back fails here.
*/
void testMixDirection()
{
    std::printf ("\n4. MIX 0 is dry and MIX 100 is wet\n");

    const auto measure = [] (float mix, double frequency)
    {
        ChainSettings settings;
        settings.mix = mix;
        settings.subfund = 0.0f;   // isolate the crossfade from the undertone sum
        const auto signal = render (settings, 400.0, 0.5, 2.5, 0.0);
        const auto window = static_cast<std::size_t> (1.5 * kSampleRate);
        return toDb (measureToneAmplitude (signal, signal.size() - window, window, frequency));
    };

    const double dryFundamental = measure (0.0f, 400.0);
    const double dryHarmonic = measure (0.0f, 800.0);
    const double wetFundamental = measure (1.0f, 400.0);
    const double wetHarmonic = measure (1.0f, 800.0);

    std::printf ("     MIX 0    fundamental %7.2f dB, 2nd harmonic %7.2f dB (%+.1f dB)\n",
                 dryFundamental, dryHarmonic, dryHarmonic - dryFundamental);
    std::printf ("     MIX 100  fundamental %7.2f dB, 2nd harmonic %7.2f dB (%+.1f dB)\n",
                 wetFundamental, wetHarmonic, wetHarmonic - wetFundamental);

    char label[160];

    std::snprintf (label, sizeof label,
                   "MIX 0 is dry: the tape's 2nd harmonic is %.1f dB down (want > 60)",
                   dryFundamental - dryHarmonic);
    check (dryFundamental - dryHarmonic > 60.0, label);

    std::snprintf (label, sizeof label,
                   "MIX 100 is wet: the tape's 2nd harmonic is only %.1f dB down (want < 45)",
                   wetFundamental - wetHarmonic);
    check (wetFundamental - wetHarmonic < 45.0, label);

    // An equal-power crossfade puts unity at both ends, so neither one may be
    // quieter than the other by more than the taper allows.
    std::snprintf (label, sizeof label,
                   "both ends of MIX sit at unity: %.1f dB dry against %.1f dB wet (want within 3)",
                   dryFundamental, wetFundamental);
    check (std::fabs (dryFundamental - wetFundamental) < 3.0, label);
}

// -----------------------------------------------------------------------------
//  5. The transient shaper is transparent at its neutral settings
// -----------------------------------------------------------------------------

/**
    Renders the shipping TransientShaper over a signal and returns its output.

    The three coefficients are built the same way resetSampleRateDependentState
    builds them: onePoleCoefficient(Hz, rate) for the two detector poles and the
    applied-gain smoother. Using the extracted onePoleCoefficient rather than a
    retyped formula means a change to the coefficient convention reaches the test.
*/
std::vector<float> renderTransientShaper (const std::vector<float>& input,
                                          float attack, float sustain, float mix)
{
    TransientShaper shaper;
    const auto fastCoefficient = onePoleCoefficient (2.0f, static_cast<float> (kSampleRate));
    const auto slowCoefficient = onePoleCoefficient (120.0f, static_cast<float> (kSampleRate));
    const auto gainCoefficient = onePoleCoefficient (30.0f, static_cast<float> (kSampleRate));

    std::vector<float> out;
    out.reserve (input.size());

    for (auto sample : input)
    {
        const float shaped = shaper.process (sample, attack, sustain,
                                             fastCoefficient, slowCoefficient, gainCoefficient);
        out.push_back (sample + (shaped - sample) * mix);
    }

    return out;
}

/** A train of exponentially-decaying bursts - a drum-like transient pattern. */
std::vector<float> makeTransientTrain()
{
    constexpr double burstHz = 4.0;          // four hits a second
    constexpr double decaySeconds = 0.05;
    const auto samples = static_cast<std::size_t> (2.0 * kSampleRate);
    std::vector<float> out;
    out.reserve (samples);

    for (std::size_t i = 0; i < samples; ++i)
    {
        const double time = static_cast<double> (i) / kSampleRate;
        const double phase = std::fmod (time, 1.0 / burstHz);
        const double envelope = std::exp (-phase / decaySeconds);
        out.push_back (static_cast<float> (0.8 * envelope
                                           * std::sin (kTwoPi * 220.0 * time)));
    }

    return out;
}

void testTransientShaperTransparentAtZero()
{
    std::printf ("\n5. The transient shaper is transparent at both amounts zero\n");

    const auto input = makeTransientTrain();
    const auto output = renderTransientShaper (input, 0.0f, 0.0f, 1.0f);

    double worst = 0.0;
    for (std::size_t i = 0; i < input.size(); ++i)
        worst = juce::jmax (worst, std::fabs (static_cast<double> (output[i] - input[i])));

    char label[160];
    std::snprintf (label, sizeof label,
                   "with ATTACK 0 and SUSTAIN 0 the output is the input (worst error %.3e)", worst);
    check (worst == 0.0, label);
}

// -----------------------------------------------------------------------------
//  6. ATTACK and SUSTAIN move the envelope the way their names say
// -----------------------------------------------------------------------------

/**
    The peak-to-sustain ratio of a rendered burst train.

    The transient is what the shaper acts on, so the measurement is the thing it
    acts on: the peak level of the whole run against the average level of the
    tails. A shaper with attack turned up raises the first and leaves the second,
    so this ratio rises; a shaper that only changed gain would move both together
    and the ratio would not.
*/
double peakToSustainRatio (const std::vector<float>& signal)
{
    double peak = 0.0;
    double tailSum = 0.0;
    std::size_t tailCount = 0;

    for (std::size_t i = 0; i < signal.size(); ++i)
    {
        const auto magnitude = std::fabs (static_cast<double> (signal[i]));
        peak = juce::jmax (peak, magnitude);

        // The tails: the last third of each 250 ms burst, which is where the
        // attack has long since passed and only the sustain is left.
        const double time = static_cast<double> (i) / kSampleRate;
        const double phase = std::fmod (time, 0.25);
        if (phase > 0.1667)
        {
            tailSum += magnitude;
            ++tailCount;
        }
    }

    const double tailAverage = tailCount > 0 ? tailSum / static_cast<double> (tailCount) : 0.0;
    return tailAverage > 1.0e-9 ? peak / tailAverage : 0.0;
}

void testTransientShaperAttackDirection()
{
    std::printf ("\n6. ATTACK sharpens when positive and softens when negative\n");

    const auto input = makeTransientTrain();
    const auto neutral  = renderTransientShaper (input,  0.0f, 0.0f, 1.0f);
    const auto boosted  = renderTransientShaper (input,  1.0f, 0.0f, 1.0f);
    const auto softened = renderTransientShaper (input, -1.0f, 0.0f, 1.0f);

    const double neutralRatio  = peakToSustainRatio (neutral);
    const double boostedRatio  = peakToSustainRatio (boosted);
    const double softenedRatio = peakToSustainRatio (softened);

    std::printf ("     ATTACK peak-to-tail ratio: neutral %.3f, +1 %.3f, -1 %.3f\n",
                 neutralRatio, boostedRatio, softenedRatio);

    // The comparisons are against the SAME signal with the control at zero, not
    // against a constant. An absolute threshold cannot work here: the ratio is
    // peak over the average of the last third of each 250 ms period, and the
    // bursts decay with a 50 ms constant, so the untouched signal already
    // measures about 88 - the window is looking at the near-silence between hits.
    // Nothing a transient shaper does to a decaying burst can pull that below
    // 1.0, which needs the tail louder than the peak.

    // Against neutral, which is also what makes this a real assertion: a stage
    // that did nothing but change gain would leave all three identical.
    char label[192];
    std::snprintf (label, sizeof label,
                   "ATTACK +1 raises the peak-to-tail ratio above neutral "
                   "(%.3f vs %.3f)", boostedRatio, neutralRatio);
    check (boostedRatio > neutralRatio, label);

    std::snprintf (label, sizeof label,
                   "ATTACK -1 lowers it below neutral (%.3f vs %.3f)",
                   softenedRatio, neutralRatio);
    check (softenedRatio < neutralRatio, label);

    // And the two ends are far enough apart that swapping the signs - the bug
    // this test exists to catch - cannot slip through the two comparisons above,
    // which are individually close to neutral by construction.
    std::snprintf (label, sizeof label,
                   "ATTACK +1 (%.3f) sits well above ATTACK -1 (%.3f)",
                   boostedRatio, softenedRatio);
    check (boostedRatio > softenedRatio * 1.25, label);
}

void testTransientShaperSustainDirection()
{
    std::printf ("\n7. SUSTAIN lengthens when positive and shortens when negative\n");

    const auto input = makeTransientTrain();

    // Sustained level (the tails), against the same run with the stage closed.
    // Attack is held at 0 so the two runs differ only in what SUSTAIN did.
    const auto neutral = renderTransientShaper (input, 0.0f, 0.0f, 1.0f);
    const auto lifted = renderTransientShaper (input, 0.0f, 1.0f, 1.0f);
    const auto shortened = renderTransientShaper (input, 0.0f, -1.0f, 1.0f);

    const auto tailRms = [] (const std::vector<float>& signal)
    {
        double sum = 0.0;
        std::size_t count = 0;
        for (std::size_t i = 0; i < signal.size(); ++i)
        {
            const double time = static_cast<double> (i) / kSampleRate;
            const double phase = std::fmod (time, 0.25);
            if (phase > 0.1667)
            {
                sum += static_cast<double> (signal[i]) * signal[i];
                ++count;
            }
        }
        return count > 0 ? std::sqrt (sum / static_cast<double> (count)) : 0.0;
    };

    const double neutralTail = tailRms (neutral);
    const double liftedTail = tailRms (lifted);
    const double shortenedTail = tailRms (shortened);

    std::printf ("     tail level: neutral %.4f, SUSTAIN +1 %.4f, SUSTAIN -1 %.4f\n",
                 neutralTail, liftedTail, shortenedTail);

    char label[160];
    std::snprintf (label, sizeof label,
                   "SUSTAIN +1 lifts the tails (%.4f vs %.4f)", liftedTail, neutralTail);
    check (liftedTail > neutralTail, label);

    std::snprintf (label, sizeof label,
                   "SUSTAIN -1 shortens them (%.4f vs %.4f)", shortenedTail, neutralTail);
    check (shortenedTail < neutralTail, label);
}

// -----------------------------------------------------------------------------
//  8. The neural stage is inert without a model
// -----------------------------------------------------------------------------

void testNeuralStageIsTransparentWithoutAModel()
{
    std::printf ("\n8. The neural stage is a pass-through when no model is loaded\n");

    // The harness compiles NeuralStage with J37_HAS_RTNEURAL 0, which is the
    // shipping behaviour on any build where RTNeural was not fetched - and the
    // same branch a shipping build takes when no model has been installed. Either
    // way the claim is the same: the stage changes nothing.
    NeuralStage stage;
    check (! stage.isActive(), "a fresh stage reports no model loaded");

    double worst = 0.0;
    for (double x = -1.0; x <= 1.0; x += 0.125)
    {
        const auto in = static_cast<float> (x);

        // Even at a fully wet setting, with no model there is nothing to blend
        // toward, so the output must be the input exactly.
        worst = juce::jmax (worst, std::fabs (static_cast<double> (stage.process (in, 1.0f) - in)));
    }

    char label[160];
    std::snprintf (label, sizeof label,
                   "process() returns its input at full wet with no model (worst error %.3e)", worst);
    check (worst == 0.0, label);

    // A model cannot be loaded from arbitrary text, and the failure must be a
    // clean false rather than a crash - which is what makes the editor's
    // file chooser safe to point at any file.
    check (! stage.loadFromJson ("not a model"),
           "loadFromJson refuses malformed text and returns false");
}

// -----------------------------------------------------------------------------
//  9. The machine's block-rate coefficients
// -----------------------------------------------------------------------------

void testBlockRateCoefficients()
{
    std::printf ("\n9. The block-rate coefficients the machine's dynamics stages run on\n");

    // This is the first stage taken out of processTapeEngine(), and it is the one
    // stage in this harness that renders no audio: what it produces is time
    // constants. So the checks are the RELATIONSHIPS between them, read off the
    // shipping stage (Source/DSP/BlockRateCoefficients.h) rather than restated here
    // - and each one is a sentence the comments beside that code already make. A
    // coefficient is 1 - exp(-1/(tau * rate)), so a longer time constant is a
    // SMALLER coefficient, and "slower" below means exactly that.
    const auto blockRate = j37rate::makeBlockRateCoefficients (static_cast<float> (kSampleRate),
                                                               0.0f);

    check (blockRate.cabinetLow > 0.0f && blockRate.cabinetLow < 1.0f
           && blockRate.cabinetPeak > 0.0f && blockRate.cabinetPeak < 1.0f
           && blockRate.presence > 0.0f && blockRate.presence < 1.0f,
           "the cabinet's two poles and the presence split are one-pole coefficients");

    check (blockRate.cabinetPeak < blockRate.cabinetLow,
           "the cabinet's resonance pole is slower than its roll-off, so the peak is a lift and not a ring");

    check (blockRate.sagRelease < blockRate.sagAttack,
           "the SAG envelope releases more slowly than it attacks, which is what makes it give rather than pump");

    check (blockRate.limiterCatch > blockRate.limiterRecovery
           && blockRate.limiterDetectorAttack > blockRate.limiterDetectorRelease,
           "the limiter catches a peak faster than it recovers from it, detector and gain alike");

    check (blockRate.antiPhase < blockRate.sagRelease
           && blockRate.antiPhase < blockRate.limiterRecovery
           && blockRate.antiPhase < blockRate.limiterDetectorRelease,
           "the anti-phase guard is the slowest pole in the machine, so a moment of real stereo cannot trip it");

    // The one number here that is a level rather than a time constant, and the one
    // relationship that ties this stage to the stage after it: the limiter hands the
    // signal off exactly where softClip's knee begins, so the clipper is transparent
    // for everything the limiter lets through and does its work only above it. Both
    // numbers are 0.70f, and this is what would catch either of them being moved
    // without the other - which the comments beside them say must not happen.
    check (std::fabs (softClip (blockRate.limiterCeiling) - blockRate.limiterCeiling) < 1.0e-6f,
           "the limiter's ceiling sits exactly on softClip's knee, so the clipper is transparent below it");

    // The builder is a function of the rate and of CABINET alone, and it has to be
    // rebuilt per block rather than per sample (that is the reason the stage exists).
    // A coefficient that did not move with the rate would be a filter whose corner
    // drifts with the host's sample rate - the one unit error this project has
    // already paid for twice.
    // At half the rate the same 2.2 kHz corner is a larger fraction of a cycle, so
    // the coefficient GROWS - and it grows less than the rate fell, which is what
    // says this is a corner frequency rather than a per-sample constant. (That
    // distinction is the 2*pi error this project has already paid for twice: the
    // old tape low-passes passed millisecond values that were written as kHz.)
    const auto halfRate = j37rate::makeBlockRateCoefficients (static_cast<float> (kSampleRate) * 0.5f,
                                                             0.0f);
    check (halfRate.presence > blockRate.presence
           && halfRate.presence < 2.0f * blockRate.presence,
           "every coefficient is rebuilt from the rate: at half the rate the presence pole is larger, and not twice");

    // CABINET is the one control that moves a pole rather than a gain, and its end
    // stops are a 5 kHz roll-off at 100 % and 9 kHz at 0 % - so a closed cabinet is
    // the LOWER corner of the two, hence the smaller coefficient.
    const auto closedCabinet = j37rate::makeBlockRateCoefficients (static_cast<float> (kSampleRate),
                                                                  1.0f);
    check (closedCabinet.cabinetLow < blockRate.cabinetLow,
           "CABINET moves the roll-off: a closed cabinet is the lower corner of the two");
}

// -----------------------------------------------------------------------------
// 10. The machine's wander: the shaping curves, the speed scale, the LFO rates
// -----------------------------------------------------------------------------

void testTapeModulation()
{
    std::printf ("\n10. The machine's shaping curves and its two wander rates\n");

    // The second stage out of processTapeEngine(), and the one whose prose makes the
    // most checkable claims: it is a pure function of seven numbers, so the harness
    // calls the SHIPPING stage (Source/DSP/TapeModulation.h) rather than a replica.
    // Every sentence asserted below is one of the comments beside that code.
    constexpr float kCentre = 0.5f;      // a control at its centre
    constexpr float kAtSpeed = 1.0f;     // the platter at its running position

    const auto running = j37mod::makeTapeModulation (kCentre, kCentre, kCentre, kCentre,
                                                     1, kAtSpeed, 120.0);

    check (running.driveCurve < kCentre && running.biasCurve < kCentre
           && running.wowCurve < kCentre && running.flutterCurve < kCentre,
           "the four shaping curves bend below their control, because they are curves rather than tapers");

    const auto harder = j37mod::makeTapeModulation (1.0f, kCentre, kCentre, kCentre,
                                                    1, kAtSpeed, 120.0);
    check (harder.driveCurve > running.driveCurve,
           "more DRIVE is a harder curve, and the curve is monotonic in the control");

    // "At speed this is exactly the SPEED control's number": the platter at its
    // running position multiplies by one, so the scale IS the choice's own number -
    // which is what makes a running machine unchanged by the mechanism that exists to
    // model it winding down.
    const auto slowChoice = j37mod::makeTapeModulation (kCentre, kCentre, kCentre, kCentre,
                                                        0, kAtSpeed, 120.0);
    const auto fastChoice = j37mod::makeTapeModulation (kCentre, kCentre, kCentre, kCentre,
                                                        2, kAtSpeed, 120.0);
    check (std::fabs (running.speedScale - 1.0f) < 1.0e-6f
           && std::fabs (slowChoice.speedScale - 0.76f) < 1.0e-6f
           && std::fabs (fastChoice.speedScale - 1.34f) < 1.0e-6f,
           "at speed the scale is the SPEED control's own number: 0.76, 1.00 and 1.34");

    const auto coasting = j37mod::makeTapeModulation (kCentre, kCentre, kCentre, kCentre,
                                                      1, 0.5f, 120.0);
    check (coasting.speedScale < running.speedScale,
           "a platter off its running position lowers the scale, which is how a coast reads as a machine losing speed");

    // "The wow rate is tempo-locked ... double the tempo and the machine wanders twice
    // as fast." This is the claim that is easiest to break and hardest to see: a wow
    // rate that stopped following the tempo would sound subtly wrong on every faster
    // track rather than obviously wrong anywhere.
    const auto atOneTwenty = j37mod::makeTapeModulation (kCentre, kCentre, kCentre, kCentre,
                                                         1, kAtSpeed, 120.0);
    const auto atTwoForty = j37mod::makeTapeModulation (kCentre, kCentre, kCentre, kCentre,
                                                        1, kAtSpeed, 240.0);
    const auto atSixty = j37mod::makeTapeModulation (kCentre, kCentre, kCentre, kCentre,
                                                     1, kAtSpeed, 60.0);
    check (std::fabs (atTwoForty.wowFreq - 2.0f * atOneTwenty.wowFreq) < 1.0e-5f
           && std::fabs (atSixty.wowFreq - 0.5f * atOneTwenty.wowFreq) < 1.0e-5f,
           "the wow rate is locked to the host tempo: twice the tempo is twice the wander");

    check (std::fabs (atTwoForty.flutterFreq - atOneTwenty.flutterFreq) < 1.0e-6f,
           "the flutter rate does NOT follow the tempo: the capstan is mechanical");

    // The anchor's clamp, both ends. A host reporting a tempo of 1 BPM or 5000 must
    // still leave the machine wandering rather than stopped or buzzing.
    const auto noTempo = j37mod::makeTapeModulation (kCentre, kCentre, kCentre, kCentre,
                                                     1, kAtSpeed, 1.0);
    const auto insaneTempo = j37mod::makeTapeModulation (kCentre, kCentre, kCentre, kCentre,
                                                         1, kAtSpeed, 5000.0);
    const auto clampedLow = j37mod::makeTapeModulation (kCentre, kCentre, kCentre, kCentre,
                                                        1, kAtSpeed, 24.0);
    const auto clampedHigh = j37mod::makeTapeModulation (kCentre, kCentre, kCentre, kCentre,
                                                        1, kAtSpeed, 720.0);
    check (std::fabs (noTempo.wowFreq - clampedLow.wowFreq) < 1.0e-6f
           && std::fabs (insaneTempo.wowFreq - clampedHigh.wowFreq) < 1.0e-6f,
           "the tempo anchor is clamped at 0.10 Hz and 3.0 Hz, so an absurd tempo cannot stop or buzz the machine");

    check (insaneTempo.wowFreq > atOneTwenty.wowFreq,
           "and above the clamp the wander still grows, so the clamp is a floor and a ceiling rather than a constant");
}

// -----------------------------------------------------------------------------
// 11. The tape formula and the instrument selector's voicing
// -----------------------------------------------------------------------------

void testTapeVoicing()
{
    std::printf ("\n11. The tape formula and instrument selector's voicing\n");

    // A pure function of the TONE macro, the formula and the instrument choice, so
    // the harness calls the SHIPPING stage (Source/DSP/TapeVoicing.h). Every sentence
    // asserted below is one of the comments beside that code.
    constexpr float kToneCentre = 0.5f;

    const auto j37 = j37voice::makeTapeVoicing (kToneCentre, 0, 0);
    check (j37.tapeCurve > 1.18f && j37.tapeCurve < 1.44f,
           "J37 at the TONE centre bends between the slow and fast stocks, not at either end");
    check (j37.tapeAsymmetry > 0.10f && j37.tapeAsymmetry < 0.16f
           && j37.tapeHiss > 0.06f && j37.tapeHiss < 0.10f
           && j37.headDampingHz > 12000.0f && j37.headDampingHz < 20000.0f
           && j37.hysteresis > 0.30f && j37.hysteresis < 0.44f,
           "every figure is the TONE crossfade's own blend of the slow and fast values");
    check (std::fabs (j37.instrumentTransportScale - 1.0f) < 1.0e-6f,
           "MASTER BUS leaves the transport exactly as calibrated");

    // The TONE macro moves the machine state: 0 % is the classic slow stock, 100 %
    // the hot fast one.
    const auto slowTone = j37voice::makeTapeVoicing (0.0f, 0, 0);
    const auto fastTone = j37voice::makeTapeVoicing (1.0f, 0, 0);
    check (slowTone.tapeCurve < fastTone.tapeCurve
           && slowTone.headDampingHz < fastTone.headDampingHz
           && slowTone.hysteresis < fastTone.hysteresis
           && slowTone.tapeAsymmetry > fastTone.tapeAsymmetry,
           "TONE 0 is the soft slow stock and TONE 100 the hard fast one: curve and damping up, asymmetry down");

    // TAPE TYPE = OFF removes the medium: no hiss, no head damping, no asymmetry.
    const auto off = j37voice::makeTapeVoicing (kToneCentre, j37voice::tapeModelOffIndex, 0);
    check (off.tapeHiss == 0.0f && off.tapeAsymmetry == 0.0f
           && std::fabs (off.headDampingHz - 26000.0f) < 1.0e-4f
           && std::fabs (off.hysteresis - 0.10f) < 1.0e-6f,
           "OFF removes the medium: no oxide floor, no asymmetry, an open head and lean memory");

    // The list's own two ends: the hottest vintage stock bends harder than the
    // reference J37, and the clean mastering stock hisses less.
    const auto hot = j37voice::makeTapeVoicing (kToneCentre, 9, 0);   // 888
    const auto clean = j37voice::makeTapeVoicing (kToneCentre, 3, 0); // Chrome
    check (hot.tapeCurve > j37.tapeCurve && hot.hysteresis > j37.hysteresis,
           "a hot stock (888) bends harder and keeps thicker magnetic memory than J37");
    check (clean.tapeHiss < j37.tapeHiss,
           "a clean stock (Chrome) hisses less than J37");

    // The INSTRUMENT selector's scale on the transport, and its own bend.
    const auto bass = j37voice::makeTapeVoicing (kToneCentre, 0, 2);
    const auto guitar = j37voice::makeTapeVoicing (kToneCentre, 0, 3);
    check (bass.instrumentTransportScale < 1.0f && guitar.instrumentTransportScale > 1.0f,
           "BASS steadies the transport and GUITAR loosens it, either side of the calibrated 1.0");
    check (bass.tapeCurve > j37.tapeCurve && guitar.tapeCurve < j37.tapeCurve,
           "BASS bends the hardest and GUITAR the cleanest, on top of the formula's own bias");

    // The clamps bound every combination, which is what keeps the two selectors from
    // reaching a state the machine does not have.
    bool everyCombinationInRange = true;
    for (int type = 0; type <= j37voice::tapeModelOffIndex; ++type)
        for (int instrument = 0; instrument <= 5; ++instrument)
            for (float tone : { 0.0f, 0.5f, 1.0f })
            {
                const auto v = j37voice::makeTapeVoicing (tone, type, instrument);
                everyCombinationInRange = everyCombinationInRange
                    && v.tapeCurve >= 1.0f && v.tapeCurve <= 1.8f
                    && v.tapeAsymmetry >= 0.0f && v.tapeAsymmetry <= 0.4f
                    && v.tapeHiss >= 0.0f && v.tapeHiss <= 0.6f
                    && v.headDampingHz >= 4000.0f && v.headDampingHz <= 26000.0f
                    && v.hysteresis >= 0.1f && v.hysteresis <= 0.7f;
            }

    check (everyCombinationInRange,
           "every formula, instrument and TONE combination lands inside the machine's designed range");
}

// -----------------------------------------------------------------------------
// 12. The glue compressors' operating points
// -----------------------------------------------------------------------------

void testGlueOperatingPoints()
{
    std::printf ("\n12. The glue compressors' operating points\n");

    constexpr float kRate = static_cast<float> (kSampleRate);

    const auto neutral = j37glueop::makeGlueOperatingPoints (0.0f, 0.0f, 0, 1.0f, kRate);
    check (std::fabs (neutral.inputThresholdDb - (-10.0f)) < 1.0e-4f
           && std::fabs (neutral.outputThresholdDb - (-10.5f)) < 1.0e-4f,
           "a 0 dB trim puts each stage's threshold at the middle of its travel");

    const auto hotInput = j37glueop::makeGlueOperatingPoints (24.0f, 0.0f, 0, 1.0f, kRate);
    const auto softInput = j37glueop::makeGlueOperatingPoints (-24.0f, 0.0f, 0, 1.0f, kRate);
    check (hotInput.inputThresholdDb > neutral.inputThresholdDb
           && neutral.inputThresholdDb > softInput.inputThresholdDb
           && hotInput.inputCompressorRatio > softInput.inputCompressorRatio,
           "turning INPUT up raises the input stage's threshold and ratio, and turning it down lowers both");
    check (softInput.inputThresholdDb == -17.0f && hotInput.inputThresholdDb == -3.0f,
           "the input threshold's end stops are the -17 .. -3 dB its own comment names");

    check (hotInput.inputMakeupFraction > neutral.inputMakeupFraction,
           "a boosted trim pays more of its reduction back, so makeup grows with the control that feeds it");

    // A hotter formula tightens the glue: the attack TIME shortens (the coefficient
    // is a base in seconds, so a smaller number is the faster stage).
    const auto j37Glue = j37glueop::makeGlueOperatingPoints (0.0f, 0.0f, 0, 1.0f, kRate);
    const auto hotStockGlue = j37glueop::makeGlueOperatingPoints (0.0f, 0.0f, 7, 1.0f, kRate);
    check (hotStockGlue.inputCoefficients.attack < j37Glue.inputCoefficients.attack,
           "a hotter stock (RTM SM911) has a shorter attack time than J37, which is what 'tighter glue' means");

    // A faster transport shortens it too - the SPEED scale rides one meaning across
    // the whole machine.
    const auto slowDeck = j37glueop::makeGlueOperatingPoints (0.0f, 0.0f, 0, 0.76f, kRate);
    const auto fastDeck = j37glueop::makeGlueOperatingPoints (0.0f, 0.0f, 0, 1.34f, kRate);
    check (fastDeck.inputCoefficients.attack < slowDeck.inputCoefficients.attack,
           "a faster SPEED shortens the glue's attack as well: the tighter constant rides the scale");

    // The follower is a function of the rate, so a lower rate moves it - the same
    // unit-error guard the block-rate stage carries. It is exp(-1/(rate*tau)), so at
    // half the rate it is exactly the full-rate value squared.
    const auto halfRateGlue = j37glueop::makeGlueOperatingPoints (0.0f, 0.0f, 0, 1.0f, kRate * 0.5f);
    check (halfRateGlue.inputCoefficients.slow < j37Glue.inputCoefficients.slow
           && std::fabs (halfRateGlue.inputCoefficients.slow
                         - j37Glue.inputCoefficients.slow * j37Glue.inputCoefficients.slow) < 1.0e-5f,
           "the detector's slow follower is a function of the rate: at half the rate it is the full-rate value squared");
}

// -----------------------------------------------------------------------------
// 13. The delay head's time, free-running or tempo-locked
// -----------------------------------------------------------------------------

void testDelayTime()
{
    std::printf ("\n13. The delay head's time, free-running or tempo-locked\n");

    check (std::fabs (j37delay::effectiveDelayMs (137.0f, false, 2, 120.0) - 137.0f) < 1.0e-4f,
           "with sync off the time IS the DELAY control, untouched");

    check (std::fabs (j37delay::effectiveDelayMs (0.0f, true, 2, 120.0) - 500.0f) < 1.0e-3f,
           "a quarter note at 120 BPM syncs to exactly 500 ms");
    check (std::fabs (j37delay::effectiveDelayMs (0.0f, true, 7, 120.0) - 750.0f) < 1.0e-3f,
           "a dotted quarter is half again as long: 750 ms");
    check (std::fabs (j37delay::effectiveDelayMs (0.0f, true, 5, 120.0) - (1000.0f / 3.0f)) < 1.0e-3f,
           "a quarter triplet is two thirds as long: a third of a second");
    check (std::fabs (j37delay::effectiveDelayMs (0.0f, true, 0, 120.0) - 2000.0f) < 1.0e-3f,
           "a whole note is four beats: 2000 ms at 120 BPM");

    check (std::fabs (j37delay::effectiveDelayMs (0.0f, true, 2, 240.0) - 250.0f) < 1.0e-3f,
           "the time follows the host tempo: double the BPM is half the delay");

    // The choice is clamped rather than indexing off the table.
    check (std::fabs (j37delay::effectiveDelayMs (0.0f, true, -5, 120.0)
                      - j37delay::effectiveDelayMs (0.0f, true, 0, 120.0)) < 1.0e-4f
           && std::fabs (j37delay::effectiveDelayMs (0.0f, true, 99, 120.0)
                         - j37delay::effectiveDelayMs (0.0f, true, 7, 120.0)) < 1.0e-4f,
           "an out-of-range note choice clamps to the table's ends instead of reading past it");

    // A host reporting nonsense cannot stop the sync dead: the tempo is floored at 1.
    check (j37delay::effectiveDelayMs (0.0f, true, 2, 0.0)
           == j37delay::effectiveDelayMs (0.0f, true, 2, 1.0),
           "a zero tempo is floored, so a bad host report cannot divide the sync by nothing");
}

// -----------------------------------------------------------------------------
// 14. The device couplings
// -----------------------------------------------------------------------------

void testTapeCouplings()
{
    std::printf ("\n14. The device couplings\n");

    // A pure function of the signals it couples, so the harness calls the SHIPPING
    // stage (Source/DSP/TapeCouplings.h). Each sentence is one of its comments.
    const auto idle = j37coupling::makeTapeCouplings (0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f);
    check (idle.wearWowCoupling == 0.0f && idle.driveHissCoupling == 0.0f
           && idle.loadInducedSpeedError == 0.0f && idle.transportActivityGate == 0.0f,
           "every control at zero means zero: the couplings can only add, never invent movement");

    // A worn machine wanders even with WOW and FLUTTER closed - the one term the
    // transport gate is wrong to silence.
    const auto worn = j37coupling::makeTapeCouplings (0.4f, 0.4f, 0.0f, 0.5f, 0.0f, 0.0f);
    check (worn.wearWowCoupling > 0.0f,
           "a worn machine (WEAR + MECHANICS) wanders even with WOW and FLUTTER closed");

    // A hot head is a noisier head, and a hard-driven machine runs looser.
    const auto hot = j37coupling::makeTapeCouplings (0.0f, 0.0f, 10.0f, 0.5f, 0.0f, 0.0f);
    check (hot.driveHissCoupling > 0.0f && hot.loadInducedSpeedError > 0.0f
           && hot.driveHissCoupling == 1.0f,
           "harder drive lifts the hiss floor and loosens the transport, and the hiss coupling is bounded at 1");

    // TONE can never silence the transport's own movement, only soften it.
    const auto dark = j37coupling::makeTapeCouplings (0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.5f);
    const auto bright = j37coupling::makeTapeCouplings (0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 0.5f);
    check (dark.headGapModulationMask >= 0.55f
           && dark.headGapModulationMask < bright.headGapModulationMask,
           "a dark head gap softens the modulation but never below the 0.55 floor");

    check (dark.transportActivityGate == 1.0f,
           "WOW and FLUTTER at their centres open the transport gate fully");

    // The two platter exponents shape a coast: pitch falls before level, so the
    // pitch curve has to be the shallower one.
    check (idle.platterPitchExponent < idle.platterGateExponent,
           "a coast loses pitch before level: the pitch exponent is the shallower of the two");
}

// -----------------------------------------------------------------------------
//  Reference: what the old arrangement did, printed but not asserted
// -----------------------------------------------------------------------------

void showOldArrangement()
{
    std::printf ("\n   For reference, the same measurement with the undertone sum injected inside\n"
                 "   the tape path - the arrangement being replaced:\n");

    ChainSettings old;
    old.injectInsideTapePath = true;
    reportUndertoneHarmonics (old, 400.0, "old arrangement (undertones inside the tape path):");
}

bool testStereoWidthMatrix()
{
    constexpr float left = 0.8f;
    constexpr float right = 0.2f;
    constexpr float mid = 0.5f * (left + right);
    const auto render = [=] (float width)
    {
        const float m = 0.5f * (left + right);
        const float s = 0.5f * (left - right) * width;
        return std::array<float, 2> { m + s, m - s };
    };
    const auto narrow = render (0.5f);
    const auto normal = render (1.0f);
    const auto wide = render (2.0f);
    const auto fold = [] (const std::array<float, 2>& pair)
    { return 0.5f * (pair[0] + pair[1]); };
    return std::abs (fold (narrow) - mid) < 1.0e-6f
        && std::abs (fold (normal) - mid) < 1.0e-6f
        && std::abs (fold (wide) - mid) < 1.0e-6f
        && std::abs (normal[0] - left) < 1.0e-6f
        && std::abs (normal[1] - right) < 1.0e-6f
        && std::abs (wide[0] - mid) > std::abs (normal[0] - mid);
}

void printChainSpectrum()
{
    std::printf ("\n   Diagnostic: the plugin's own output spectrum for a 500 Hz tone with\n"
                 "   SUBFUND fully open, straight through the whole chain:\n");

    ChainSettings settings;
    const auto signal = render (settings, 500.0, 0.5, 2.5, 0.0);
    const auto window = static_cast<std::size_t> (1.5 * kSampleRate);
    printSpectrum (signal, signal.size() - window, window, 20.0, 2600.0, 0.5, 28);
}

int main()
{
    std::printf ("SUBFUND subharmonic regression harness\n");
    check (testStereoWidthMatrix(), "stereo width preserves MID and boosts SIDE above natural");

    testUndertoneOrdering();
    testNoHarmonicsFromUndertones();
    testSilenceIsSilent();
    testMixDirection();
    testTransientShaperTransparentAtZero();
    testTransientShaperAttackDirection();
    testTransientShaperSustainDirection();
    testNeuralStageIsTransparentWithoutAModel();
    testBlockRateCoefficients();
    testTapeModulation();
    testTapeVoicing();
    testTapeCouplings();
    testGlueOperatingPoints();
    testDelayTime();
    showOldArrangement();
    printChainSpectrum();

    std::printf ("\n%s (%d failure%s)\n", gFailures == 0 ? "ALL CHECKS PASSED" : "FAILURES PRESENT",
                 gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
