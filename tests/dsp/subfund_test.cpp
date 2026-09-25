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
#include <cmath>
#include <cstdio>
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

const int kDividers[SubharmonicGenerator::numStages] = { 2, 3, 4, 5, 6, 7, 8, 9 };

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
        if (matches (f0 / kDividers[d]))
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
    }

    float processSample (float rawInput)
    {
        const float sr = static_cast<float> (sampleRate);

        // ---- input stage ---------------------------------------------------
        const float inputTrimmed = rawInput * inputGain;
        const auto inputEnvelopeDb = inputCompressor.processDetection (
            inputTrimmed * inputTrimmed, sr, inputAttackSeconds, inputReleaseSeconds, inputDriveLoad);
        const auto inputReductionDb = juce::jmax (
            inputReductionLimitDb,
            softKneeReductionDb (inputEnvelopeDb, inputThresholdDb, inputKneeDb, inputCompressorRatio));
        const float x = inputTrimmed * juce::Decibels::decibelsToGain (inputReductionDb);

        // ---- record head and magnetic shaper --------------------------------
        const float preDrive = x * (1.0f + driveAmount * 1.2f * speedBias * preDriveGain);
        const float shapedCore = magneticHysteresis (preDrive, shaperDrive, shaperAsymmetry, hysteresisMemory);
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
            tapeOutput * tapeOutput, sr, outputAttackSeconds, outputReleaseSeconds, outputDriveLoad);
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
        const float shaped = magneticHysteresis (preDrive, chain.shaperDrive, chain.shaperAsymmetry, 0.0f);
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
            const double frequency = 500.0 / kDividers[s];
            const double amplitude = measureToneAmplitude (signal, start, window, frequency);
            std::printf ("       1/%d  %7.2f Hz  %7.2f dB%s\n", kDividers[s], frequency, toDb (amplitude),
                         s > 0 ? "" : "   <- top of the staircase");

            char label[160];
            std::snprintf (label, sizeof label, "at DRIVE %.2f, 1/%d is present and quieter than 1/%d",
                           drive, kDividers[s], kDividers[s > 0 ? s - 1 : 0]);
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
        const double undertone = f0 / kDividers[s];
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
                         kDividers[s], undertone, level, harmonic, product,
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
        const double undertone = 400.0 / kDividers[s];
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
                           harmonic, kDividers[s], product, margin);
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
            const double undertone = 400.0 / kDividers[s];
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
        const double frequency = 500.0 / kDividers[s];
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

    testUndertoneOrdering();
    testNoHarmonicsFromUndertones();
    testSilenceIsSilent();
    testMixDirection();
    showOldArrangement();
    printChainSpectrum();

    std::printf ("\n%s (%d failure%s)\n", gFailures == 0 ? "ALL CHECKS PASSED" : "FAILURES PRESENT",
                 gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
