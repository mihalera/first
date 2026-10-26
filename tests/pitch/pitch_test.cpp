// =============================================================================
//  Regression harness for the pitch stage: the autotune's detector and shifter.
//
//  Autotune was reported as doing nothing audible. The two reasons were both
//  properties of the SIGNAL, not of the code as written, so both are measured
//  here on signal:
//
//    1. The detector counted zero crossings and called their rate a frequency.
//       On a signal with strong harmonics it locks onto whichever overtone
//       supplies the crossings, so a guitar's second harmonic reads an octave
//       high and the "correction" leaves the note exactly where it was; on NOISE
//       it reports a confident frequency that is nothing but hiss. A corrector
//       built on it therefore chased the wrong number, or a number that was not
//       a pitch at all - which is heard as "nothing happened".
//
//    2. The shifter applied its grain restart correction to BOTH read heads at
//       once, so the head that was at full window took the whole correction as a
//       step - a click, once per grain - and it restarted at a fixed offset, so
//       the two halves of the crossfade were not the same waveform. That is the
//       granular flutter the old stage had, and it is why it could not be tuned
//       by ear.
//
//  So this file asserts, on rendered audio:
//
//    * a note is detected as a note, at the right pitch, at every session rate;
//    * a strong octave partial does not move the reading an octave;
//    * noise and silence are reported as UNKNOWN rather than as a pitch;
//    * the shifter moves a tone by the ratio it is given, and the tone comes out
//      as a tone - the granular residue is bounded, not just present;
//    * at unity the shifter is as transparent as its design allows;
//    * the corrector STAGE - the audio path AUTOTUNE is built on - moves the pitch
//      by the ratio it is handed, keeps the note's duration and level, and applies
//      exactly the lookahead it reports to the host. That last one is measured on
//      an impulse, and it is the number the plugin adds to its reported latency,
//      so a stage that quietly changed it would put every other track in the
//      session a block out of place.
//
//  run.sh builds the file TWICE: once without signalsmith-stretch on the include
//  path (the fallback shape) and once with it (the shape the plugin ships).
//
//  Both structs are cut out of Source/ by tests/pitch/extract.py and #included
//  below, so this harness always measures the shipping code rather than a copy of
//  it that can drift. Every control value below is the plugin's own.
// =============================================================================

#include "shim.h"

#include <algorithm>
// <array> explicitly: the extracted structs hold their analysis window, their
// difference tables, the shifter's delay line and its two read heads as
// std::array, and whether the headers above happen to pull <array> in is a
// property of the TOOLCHAIN rather than of this harness - it did on the machine
// this was written on and did NOT on the CI runner's, where every one of those
// members came out as "field has incomplete type 'std::array<float, N>'" and the
// harness did not build at all.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "extracted_pitch.inc"
#include "DSP/PitchCorrectionRule.h"

namespace
{
constexpr double kTwoPi = 6.283185307179586;
int gFailures = 0;

void check (bool condition, const std::string& what)
{
    std::printf ("%s %s\n", condition ? "  ok  " : "FAILED", what.c_str());

    if (! condition)
        ++gFailures;
}

/** A deterministic noise source, so a failure is reproducible. */
struct Noise
{
    explicit Noise (std::uint32_t seed) : state (seed) {}

    float next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float> (static_cast<std::int32_t> (state)) / 2147483648.0f;
    }

    std::uint32_t state;
};

/** Amplitude of a sinusoid at `frequency` over a window of `signal`.

    Hann-windowed, because none of the frequencies under test lands on a window
    boundary, and an unwindowed measurement of those is off by a leak factor
    rather than by a rounding error. Normalised by the window's coherent gain, so
    the result is the amplitude of the partial itself.
*/
double toneAmplitude (const std::vector<float>& signal, std::size_t start,
                      std::size_t length, double frequency, double sampleRate)
{
    if (length < 4 || start + length > signal.size())
        return 0.0;

    double real = 0.0, imaginary = 0.0, windowSum = 0.0;
    const auto n = static_cast<double> (length - 1);

    for (std::size_t i = 0; i < length; ++i)
    {
        const double w = 0.5 * (1.0 - std::cos (kTwoPi * static_cast<double> (i) / n));
        windowSum += w;
        const double phase = kTwoPi * frequency * static_cast<double> (i) / sampleRate;
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
    if (length == 0 || start + length > signal.size())
        return 0.0;

    double sum = 0.0;

    for (std::size_t i = 0; i < length; ++i)
    {
        const double sample = static_cast<double> (signal[start + i]);
        sum += sample * sample;
    }

    return std::sqrt (sum / static_cast<double> (length));
}

/** The frequency carrying the most energy in `[low, high]`, by a coarse scan and
    then a finer one on the winner's neighbourhood. The scan is deliberately
    crude: what the harness needs is the peak's LOCATION to within a percent, and
    a parabola on three sampled points then pins it. */
double dominantFrequency (const std::vector<float>& signal, std::size_t start,
                          std::size_t length, double sampleRate,
                          double low, double high)
{
    auto best = low;
    auto bestAmplitude = -1.0;

    const auto scan = [&] (double from, double to, int steps)
    {
        for (int i = 0; i <= steps; ++i)
        {
            const auto frequency = from + (to - from) * static_cast<double> (i) / steps;
            const auto amplitude = toneAmplitude (signal, start, length, frequency, sampleRate);

            if (amplitude > bestAmplitude)
            {
                bestAmplitude = amplitude;
                best = frequency;
            }
        }
    };

    scan (low, high, 400);
    const auto resolution = (high - low) / 100.0;
    scan (std::max (low, best - resolution), std::min (high, best + resolution), 200);
    return best;
}

/** One note through the tracker, decayed to a reading. */
struct TrackerReading
{
    float frequency = 0.0f;
    float confidence = 0.0f;
    float periodFrames = 0.0f;
};

TrackerReading readNote (double sampleRate, double frequency, double seconds,
                         double secondsOfNoise = 0.0, double partialGain = 0.0,
                         double partialFrequency = 0.0, std::uint32_t seed = 1,
                         double level = 0.5)
{
    PitchTracker tracker;
    tracker.prepare (sampleRate);

    Noise noise (seed);
    const auto frames = static_cast<int> (seconds * sampleRate);

    for (int n = 0; n < frames; ++n)
    {
        const auto t = static_cast<double> (n) / sampleRate;
        double sample = level * std::sin (kTwoPi * frequency * t);

        if (partialGain > 0.0)
            sample += level * partialGain * std::sin (kTwoPi * partialFrequency * t);

        if (secondsOfNoise > 0.0)
            sample += secondsOfNoise * noise.next();

        tracker.push (static_cast<float> (sample));
    }

    return { tracker.frequency(), tracker.confidence(), tracker.periodFrames() };
}

std::vector<float> renderShifter (double sampleRate, double inputFrequency,
                                  float ratio, int periodHint, double seconds,
                                  double level = 0.5)
{
    RealtimePitchShifter shifter;
    shifter.reset();

    const auto frames = static_cast<int> (seconds * sampleRate);
    std::vector<float> output (static_cast<std::size_t> (frames), 0.0f);

    for (int n = 0; n < frames; ++n)
    {
        const auto t = static_cast<double> (n) / sampleRate;
        const auto input = static_cast<float> (level * std::sin (kTwoPi * inputFrequency * t));
        output[static_cast<std::size_t> (n)] = shifter.process (input, ratio, periodHint);
    }

    return output;
}

/** How the shifter's own delay behaved over a render: the two things that make a
    reading of "it sounds wrong" concrete. A delay that reaches zero means a head
    read the write head - a splice running off the end of the line is the click.
    A delay that grows without bound means the stage is adding latency nobody
    asked for, which is a performer hearing themselves late. */
struct Footing
{
    float minimum = 0.0f;
    float maximum = 0.0f;
};

Footing measureFooting (double sampleRate, float ratio, int periodHint, double seconds)
{
    RealtimePitchShifter shifter;
    shifter.reset();

    Footing footing { std::numeric_limits<float>::max(), 0.0f };
    const auto frames = static_cast<int> (seconds * sampleRate);

    for (int n = 0; n < frames; ++n)
    {
        const auto t = static_cast<double> (n) / sampleRate;
        const auto input = static_cast<float> (0.5 * std::sin (kTwoPi * 220.0 * t));
        shifter.process (input, ratio, periodHint);

        // The first frames are the line filling up, before any head has been set.
        if (n < static_cast<int> (sampleRate * 0.1))
            continue;

        for (int tap = 0; tap < 2; ++tap)
        {
            const auto delay = shifter.delayOf (tap);
            footing.minimum = std::min (footing.minimum, delay);
            footing.maximum = std::max (footing.maximum, delay);
        }
    }

    return footing;
}

double cents (double measured, double expected)
{
    return 1200.0 * std::log2 (measured / expected);
}

void testTrackerFollowsNotes()
{
    std::printf ("\nthe detector reads the note that is playing\n");

    for (const auto frequency : { 55.0, 82.407, 110.0, 220.0, 440.0, 880.0 })
    {
        const auto reading = readNote (48000.0, frequency, 1.5);
        const auto error = std::abs (cents (reading.frequency, frequency));

        check (error < 20.0,
               "a " + std::to_string (frequency) + " Hz tone reads "
                   + std::to_string (reading.frequency) + " Hz ("
                   + std::to_string (static_cast<int> (error)) + " cents off, confidence "
                   + std::to_string (reading.confidence) + ")");
        check (reading.confidence > 0.7f, "  ... and it is confident about it");
    }
}

void testTrackerIsRateIndependent()
{
    std::printf ("\nthe reading does not move with the session rate\n");

    for (const auto sampleRate : { 44100.0, 48000.0, 88200.0, 96000.0 })
    {
        const auto reading = readNote (sampleRate, 220.0, 1.5);
        const auto error = std::abs (cents (reading.frequency, 220.0));

        check (error < 20.0,
               "220 Hz at " + std::to_string (static_cast<int> (sampleRate)) + " Hz reads "
                   + std::to_string (reading.frequency) + " Hz ("
                   + std::to_string (static_cast<int> (error)) + " cents off)");
    }
}

void testTrackerHoldsTheOctave()
{
    std::printf ("\na strong octave partial does not move the reading an octave\n");

    // A tone whose second harmonic is nearly as loud as its fundamental - the
    // reed and the guitar string the crossing counter used to read an octave
    // high. YIN's normalisation is what keeps it on the note.
    const auto reading = readNote (48000.0, 220.0, 1.5, 0.0, 0.8, 440.0);
    const auto error = std::abs (cents (reading.frequency, 220.0));

    check (error < 40.0,
           "220 Hz with an 0.8x 440 Hz partial reads " + std::to_string (reading.frequency)
               + " Hz (" + std::to_string (static_cast<int> (error)) + " cents off)");
}

void testTrackerRejectsNoise()
{
    std::printf ("\nnoise and silence are reported as unknown, not as a pitch\n");

    PitchTracker silent;
    silent.prepare (48000.0);

    for (int n = 0; n < 96000; ++n)
        silent.push (0.0f);

    check (silent.confidence() < 1.0e-3f,
           "silence reports no confidence at all (" + std::to_string (silent.confidence()) + ")");

    // Half a second of noise in, half a second of a note after it: the detector
    // has to have said nothing during the noise and to find the note after it.
    PitchTracker tracker;
    tracker.prepare (48000.0);
    Noise noise (7);

    auto worstDuringNoise = 0.0f;

    for (int n = 0; n < 192000; ++n)
    {
        const auto t = static_cast<double> (n) / 48000.0;
        const auto sample = n < 96000 ? 0.3f * noise.next()
                                      : static_cast<float> (0.5 * std::sin (kTwoPi * 330.0 * t));
        tracker.push (sample);

        if (n > 24000 && n < 96000)
            worstDuringNoise = std::max (worstDuringNoise, tracker.confidence());
    }

    check (worstDuringNoise < 0.5f,
           "white noise never reaches the correction gate (peak confidence "
               + std::to_string (worstDuringNoise) + ")");
    check (std::abs (cents (tracker.frequency(), 330.0)) < 20.0,
           "the note after the noise reads " + std::to_string (tracker.frequency()) + " Hz");
}

void testTrackerFollowsThroughNoise()
{
    std::printf ("\na note is still found when it is riding on noise\n");

    // 10 dB SNR: a breathy voice or a distorted guitar. The reading may wander,
    // the note has to stay the note.
    const auto reading = readNote (48000.0, 440.0, 1.5, 0.15, 0.0, 0.0, 3);
    const auto error = std::abs (cents (reading.frequency, 440.0));

    check (error < 40.0,
           "440 Hz at 10 dB SNR reads " + std::to_string (reading.frequency) + " Hz ("
               + std::to_string (static_cast<int> (error)) + " cents off, confidence "
               + std::to_string (reading.confidence) + ")");
}

void testShifterMovesThePitch()
{
    std::printf ("\nthe shifter moves a tone by the ratio it is given\n");

    for (const auto ratio : { 1.029302f, 0.971532f, 1.059463f })
    {
        const auto input = 220.0;
        const auto expected = input * ratio;
        const auto hint = static_cast<int> (std::lround (48000.0 / input));
        const auto rendered = renderShifter (48000.0, input, ratio, hint, 2.0);

        const auto window = static_cast<std::size_t> (48000.0);
        const auto start = rendered.size() - window;
        const auto measured = dominantFrequency (rendered, start, window, 48000.0,
                                                 expected * 0.9, expected * 1.1);
        const auto error = std::abs (cents (measured, expected));

        check (error < 20.0,
               "a 220 Hz tone at ratio " + std::to_string (ratio) + " reads "
                   + std::to_string (measured) + " Hz (expected "
                   + std::to_string (expected) + ", "
                   + std::to_string (static_cast<int> (error)) + " cents off)");
    }
}

void testSpliceResidueIsBounded()
{
    std::printf ("\nthe shifted tone comes out as a tone, not as grains\n");

    const auto input = 220.0;
    const auto ratio = 1.029302f;
    const auto expected = input * ratio;
    const auto hint = static_cast<int> (std::lround (48000.0 / input));
    const auto rendered = renderShifter (48000.0, input, ratio, hint, 2.0);

    const auto window = static_cast<std::size_t> (48000.0);
    const auto start = rendered.size() - window;
    const auto total = rms (rendered, start, window);
    const auto carrier = toneAmplitude (rendered, start, window, expected, 48000.0) / std::sqrt (2.0);

    // Whatever is in the output that is not the shifted tone is the granular
    // residue: grain-rate amplitude modulation, sidebands from a splice that is
    // not phase-locked, clicks. It is reported in dB below the tone, which is
    // the only form of the number a listener can be asked to agree with.
    const auto residue = std::sqrt (std::max (0.0, total * total - carrier * carrier));
    const auto residueDb = total > 0.0 ? 20.0 * std::log10 (std::max (1.0e-9, residue / total)) : 0.0;

    check (residueDb < -20.0,
           "granular residue sits " + std::to_string (residueDb) + " dB below the shifted tone");

    const auto expectedLevel = 0.5 / std::sqrt (2.0);
    check (std::abs (20.0 * std::log10 (std::max (1.0e-9, total / expectedLevel))) < 1.5,
           "and the tone keeps its level through the shifter ("
               + std::to_string (20.0 * std::log10 (total / expectedLevel)) + " dB)");
}

void testUnityIsTransparent()
{
    std::printf ("\nat unity the shifter is as transparent as its design allows\n");

    const auto input = 220.0;
    const auto hint = static_cast<int> (std::lround (48000.0 / input));
    const auto rendered = renderShifter (48000.0, input, 1.0f, hint, 2.0);

    const auto window = static_cast<std::size_t> (48000.0);
    const auto start = rendered.size() - window;
    const auto total = rms (rendered, start, window);
    const auto expectedLevel = 0.5 / std::sqrt (2.0);

    check (std::abs (20.0 * std::log10 (std::max (1.0e-9, total / expectedLevel))) < 0.5,
           "a unity-ratio sine keeps its level (" + std::to_string (20.0 * std::log10 (total / expectedLevel))
               + " dB)");

    // With a whole-period splice there is no grain-rate modulation to hear: what
    // is left is the interpolation error at the join. A ratio well inside one
    // cent is what "inaudible" means for a stationary tone.
    const auto carrier = toneAmplitude (rendered, start, window, input, 48000.0) / std::sqrt (2.0);
    const auto residue = std::sqrt (std::max (0.0, total * total - carrier * carrier));
    const auto residueDb = 20.0 * std::log10 (std::max (1.0e-9, residue / total));

    check (residueDb < -25.0,
           "and the unity splice leaves " + std::to_string (residueDb) + " dB of residue");
}

void testShifterKeepsItsFooting()
{
    std::printf ("\nthe read heads stay behind the write head at every ratio\n");

    const auto input = 220.0;
    const auto hint = static_cast<int> (std::lround (48000.0 / input));

    for (const auto ratio : { 0.5f, 1.0f, 1.029302f, 1.5f, 2.0f })
    {
        const auto footing = measureFooting (48000.0, ratio, hint, 2.0);

        // A head that reaches the write head reads silence next block and a
        // half-written sample now: that is the splice click, and it is the one
        // failure this design can have that a pitch measurement cannot see.
        check (footing.minimum > 1.0f,
               "at ratio " + std::to_string (ratio) + " the closest a head came to the write head was "
                   + std::to_string (footing.minimum) + " frames");

        // The delay is bounded by the period and the reading ratio, and the bound
        // is the whole latency story of this stage: a runway of one period (more
        // when the head reads fast), the period the seam folds in, and - below
        // unity, where a head falls behind instead of catching up - one more
        // period of drift before the next fold takes it back. Anything larger
        // than that is latency the stage invented, heard as a late monitor.
        const auto ceiling = static_cast<float> (hint)
                                 * (juce::jmax (1.0f, ratio) + 1.0f + juce::jmax (0.0f, 1.0f - ratio))
                           + 1.0f;
        check (footing.maximum <= ceiling,
               "  ... and its delay stayed at " + std::to_string (footing.maximum)
                   + " frames, inside the " + std::to_string (ceiling) + " its grain allows");
    }

    // No reading at all: the fallback grain is long, which is the case that would
    // quietly double the stage's latency if the runway were written as two
    // periods instead of one.
    const auto idle = measureFooting (48000.0, 1.0f, 0, 1.0);
    check (idle.maximum <= static_cast<float> (RealtimePitchShifter::fallbackPeriod) + 1.0f,
           "with no pitch reading the stage idles at " + std::to_string (idle.maximum)
               + " frames of delay, one fallback grain");
}

// =============================================================================
//  THE CORRECTOR STAGE: the autotune audio path itself.
//
//  run.sh builds this harness TWICE - once with signalsmith-stretch on the
//  include path and once without it - because Source/DSP/CorrectorStage.h has two
//  shapes and the plugin can be built with either. What each shape must do is
//  asserted here rather than described:
//
//    * the library shape moves the pitch by the ratio it is handed, hands the
//      material back at the DURATION and LEVEL it arrived with (that is the whole
//      reason a corrector wants a phase vocoder: a resampler would move the pitch
//      AND shorten the note), and applies a delay that is exactly the lookahead it
//      reports to the plugin for the host;
//
//    * the fallback shape is the granular shifter the plugin shipped, sample for
//      sample, with no lookahead to report - the plugin has never reported the
//      period of delay that shifter costs, and this stage does not change that.
// =============================================================================
struct CorrectorRender
{
    std::vector<float> output;
    int latency = 0;
};

/** The stage, driven exactly as the plugin drives it: one frame at a time, one
    ratio and one period hint per channel.

    The input is a 100 ms burst of 220 Hz inside silence when `burst` is set, and
    a continuous tone otherwise. The burst is what the duration test measures -
    where the material begins and ends - and a continuous tone is what the pitch
    and level tests measure, where an edge would only add leakage. */
CorrectorRender renderCorrector (double sampleRate, float ratio, int periodHint,
                                 double seconds, bool burst)
{
    CorrectorStage stage;
    stage.prepare (sampleRate, 2);

    const auto frames = static_cast<int> (seconds * sampleRate);
    const float ratios[2] { ratio, ratio };
    const int hints[2] { periodHint, periodHint };

    const auto burstFrames = static_cast<int> (0.1 * sampleRate);
    const auto burstStart = static_cast<int> (0.2 * sampleRate);

    CorrectorRender render;
    render.latency = stage.latencyFrames();
    render.output.assign (static_cast<std::size_t> (frames), 0.0f);

    for (int n = 0; n < frames; ++n)
    {
        const auto on = ! burst || (n >= burstStart && n < burstStart + burstFrames);
        const auto t = static_cast<double> (n) / sampleRate;
        const auto input = static_cast<float> (on ? 0.5 * std::sin (kTwoPi * 220.0 * t) : 0.0);

        std::array<float, 2> frame { input, input };
        stage.processFrame (frame.data(), ratios, hints);
        render.output[static_cast<std::size_t> (n)] = frame[0];
    }

    return render;
}

void testCorrectorUsesBlockAlignedRatio()
{
    std::printf ("\nthe corrector captures the ratio belonging to its input block\n");
#if J37_HAS_SIGNALSMITH
    CorrectorStage stage;
    stage.prepare (48000.0, 1);
    const int hints[2] { 220, 220 };
    std::array<float, 2> frame {};
    float ratios[2] { 1.0f, 1.0f };

    for (int n = 0; n < CorrectorStage::blockFrames; ++n)
    {
        frame[0] = frame[1] = static_cast<float> (std::sin (kTwoPi * 220.0 * n / 48000.0));
        ratios[0] = ratios[1] = n < CorrectorStage::blockFrames / 2 ? 0.95f : 1.05f;
        stage.processFrame (frame.data(), ratios, hints);
    }

    // The block's input trajectory averages to unity. A bug applying only the
    // last-frame value would instead transpose this whole block by +5 percent.
    const auto& captured = stage.capturedRatiosForTest();
    check (captured[0] > 0.999f && captured[0] < 1.001f,
           "captured block ratio is the mean of its own samples, not the final ratio");

    stage.reset();
    const float transitionRatios[2] { 1.0f, 1.0f };
    const int transitionHints[2] { 0, 0 };
    for (int n = 0; n < CorrectorStage::blockFrames - 1; ++n)
    {
        frame = { 0.25f, 0.75f };
        stage.processFrame (frame.data(), transitionRatios, transitionHints, 2);
    }
    frame = { 0.5f, 123.0f };
    stage.processFrame (frame.data(), transitionRatios, transitionHints, 1);
    for (int n = 0; n < CorrectorStage::blockFrames; ++n)
    {
        frame = { 0.0f, 456.0f };
        stage.processFrame (frame.data(), transitionRatios, transitionHints, 1);
    }
    const auto& transitionCaptured = stage.capturedRatiosForTest();
    check (transitionCaptured[1] > 0.999f && transitionCaptured[1] < 1.001f,
           "mono frames after stereo input fill absent channel slots with silence and neutral ratios");

    stage.reset();
    std::array<float, 2> transitionRatios { 1.0f, 1.0f };
    std::array<int, 2> transitionHints { 0, 0 };
    for (int n = 0; n < CorrectorStage::blockFrames; ++n)
    {
        frame = { 0.25f, -0.5f };
        stage.processFrame (frame.data(), transitionRatios.data(), transitionHints.data(), 2);
    }
    for (int n = 0; n < CorrectorStage::blockFrames / 2; ++n)
    {
        frame = { 0.0f, 777.0f };
        stage.processFrame (frame.data(), transitionRatios.data(), transitionHints.data(), 1);
    }
    for (int n = 0; n < CorrectorStage::blockFrames; ++n)
    {
        frame = { 0.0f, 777.0f };
        stage.processFrame (frame.data(), transitionRatios.data(), transitionHints.data(), 1);
    }
    for (int n = 0; n < CorrectorStage::blockFrames; ++n)
    {
        frame = { 0.0f, 0.0f };
        stage.processFrame (frame.data(), transitionRatios.data(), transitionHints.data(), 2);
        check (std::abs (frame[1]) < 1.0e-6f,
               "stereo channel stays silent after a mono interval rather than replaying stale output");
    }

    // Enter mono while the initial stereo output block is only half consumed:
    // the transition must discard that remainder rather than resume mid-block.
    stage.reset();
    for (int n = 0; n < CorrectorStage::blockFrames; ++n)
    {
        frame = { 0.2f, -0.4f };
        stage.processFrame (frame.data(), transitionRatios.data(), transitionHints.data(), 2);
    }
    for (int n = 0; n < CorrectorStage::blockFrames / 2; ++n)
    {
        frame = { 0.0f, 0.0f };
        stage.processFrame (frame.data(), transitionRatios.data(), transitionHints.data(), 2);
    }
    frame = { 0.0f, 123.0f };
    stage.processFrame (frame.data(), transitionRatios.data(), transitionHints.data(), 1);
    check (frame[1] == 123.0f, "mono transition does not write beyond the supplied channels");
    for (int n = 0; n < CorrectorStage::blockFrames; ++n)
    {
        frame = { 0.0f, 0.0f };
        stage.processFrame (frame.data(), transitionRatios.data(), transitionHints.data(), 2);
        check (std::abs (frame[1]) < 1.0e-6f,
               "mid-block mono transition discards stale stereo output");
    }

    stage.reset();
    std::vector<float> stereoInput (static_cast<std::size_t> (CorrectorStage::blockFrames));
    for (int n = 0; n < CorrectorStage::blockFrames; ++n)
    {
        stereoInput[static_cast<std::size_t> (n)] = n < CorrectorStage::blockFrames / 2 ? 0.25f : 0.75f;
        frame = { stereoInput[static_cast<std::size_t> (n)],
                  n < CorrectorStage::blockFrames / 2 ? -0.5f : 0.5f };
        stage.delayFrame (frame.data(), 2);
    }
    stage.reset();
    std::vector<float> monoExpected (static_cast<std::size_t> (CorrectorStage::blockFrames * 3));
    for (int n = 0; n < CorrectorStage::blockFrames; ++n)
        monoExpected[static_cast<std::size_t> (n)] = stereoInput[static_cast<std::size_t> (n)];
    std::fill (monoExpected.begin() + CorrectorStage::blockFrames, monoExpected.end(), 0.25f);
    for (int n = 0; n < CorrectorStage::blockFrames * 3; ++n)
    {
        frame = { 0.25f, 987.0f };
        stage.delayFrame (frame.data(), 1);
        if (n >= CorrectorStage::blockFrames)
        {
            const auto expected = monoExpected[static_cast<std::size_t> (n - CorrectorStage::blockFrames)];
            check (std::abs (frame[0] - expected) < 1.0e-6f,
                   "mono delay output stays aligned and channel-safe after block wraps");
        }
    }
#endif
}

void testCorrectorShape()
{
    std::printf ("\nthe corrector stage: which engine this build has, and what it costs\n");

    CorrectorStage stage;
    stage.prepare (48000.0, 2);

   #if J37_HAS_SIGNALSMITH
    check (CorrectorStage::hasLibrary(),
           "J37_CORRECTOR_USE_STRETCH is thrown and signalsmith-stretch is present, so the"
           " stage is the phase vocoder");
    check (stage.latencyFrames() > 0,
           "and it costs the plugin " + std::to_string (stage.latencyFrames())
               + " frames of lookahead at 48 kHz, which is what the host is told");
   #else
    check (! CorrectorStage::hasLibrary(),
           "this build has no vocoder, so the stage is the granular shifter the plugin ships");
    check (stage.latencyFrames() == 0,
           "and it reports no lookahead - the shifter's own period of delay has never been"
           " reported, and this stage does not start now");
   #endif
}

void testCorrectorMovesThePitch ()
{
    std::printf ("\na tone through the corrector comes out at the ratio it was asked for\n");

    for (const auto ratio : { 1.0293f, 1.0595f, 0.9439f })
    {
        const auto expected = 220.0 * static_cast<double> (ratio);
        const auto hint = static_cast<int> (std::lround (48000.0 / 220.0));
        const auto render = renderCorrector (48000.0, ratio, hint, 2.0, false);

        // The last second of the render, so whatever the stage did while its
        // window filled has settled. The measurement is a direct one: the tone's
        // own frequency, by the same Hann-windowed scan the shifter tests use.
        const auto window = static_cast<std::size_t> (48000.0);
        const auto start = render.output.size() - window;
        const auto measured = dominantFrequency (render.output, start, window, 48000.0,
                                                 expected * 0.9, expected * 1.1);
        const auto error = std::abs (cents (measured, expected));

        // The tolerance is each engine's own, and the number is printed either
        // way. The granular shifter reads a stationary tone to a couple of cents
        // at every ratio. The vocoder's transposition is a property of its analysis
        // GEOMETRY, not of the window alone: with the stage's 20 ms window it reads
        // 15.8 cents wide at 1.0595x and 56.9 at 0.9439x at four analyses per
        // window, 8.4 and 32.4 at the eight this stage runs (the same numbers at 110 Hz
        // with twice the window, so the cents do not follow the note). 2.5 % is
        // therefore the vocoder's bound here rather than an aspiration - it is the
        // eight-analysis measurement with a little room - and the cents in the line
        // below are the measurement itself.
       #if J37_HAS_SIGNALSMITH
        const auto withinTolerance = std::abs (measured / expected - 1.0) < 0.025;
       #else
        const auto withinTolerance = error < 20.0;
       #endif

        check (withinTolerance,
               "a 220 Hz tone at ratio " + std::to_string (ratio) + " reads "
                   + std::to_string (measured) + " Hz (expected "
                   + std::to_string (expected) + ", "
                   + std::to_string (static_cast<int> (error)) + " cents off)");
    }
}

void testCorrectorKeepsTheDuration ()
{
    std::printf ("\nand it keeps the note as long as it was - the point of the vocoder\n");

    const auto ratio = 1.0293f;
    const auto hint = static_cast<int> (std::lround (48000.0 / 220.0));
    const auto render = renderCorrector (48000.0, ratio, hint, 2.0, true);

    auto peak = 0.0;

    for (const auto sample : render.output)
        peak = std::max (peak, static_cast<double> (std::abs (sample)));

    std::size_t first = render.output.size();
    std::size_t last = 0;

    for (std::size_t i = 0; i < render.output.size(); ++i)
        if (std::abs (render.output[i]) > 0.05 * peak)
        {
            first = std::min (first, i);
            last = i;
        }

    const auto length = static_cast<double> (last >= first ? last - first + 1 : 0);
    const auto inputLength = 0.1 * 48000.0;

    // What a resampler would have handed back is the number that matters here:
    // reading the material 2.93 % faster makes 100 ms of note into 97.2 ms of it,
    // and a corrector that did that would shorten every note it corrected. What
    // the stage is allowed is the smear of its own window (the edges ramp rather
    // than step, so the 5 % level is crossed a few ms early and late).
    check (std::abs (length - inputLength) < 0.05 * inputLength,
           "a 100 ms note comes back " + std::to_string (length * 100.0 / 48000.0)
               + " ms long at ratio " + std::to_string (ratio)
               + " (a resampler would answer "
               + std::to_string (inputLength * 100.0 / 48000.0 / ratio) + " ms)");

   #if J37_HAS_SIGNALSMITH
    // The library shape's own claim, and the one the fallback cannot make: the
    // delay it applies is the number it reports. The measure is the impulse
    // response's FIRST MOMENT - a Hann STFT answers an impulse with a windowed
    // image of it, so the centroid is the delay rather than a threshold on noise.
    {
        CorrectorStage stage;
        stage.prepare (48000.0, 2);

        const float ratios[2] { 1.0f, 1.0f };
        const int hints[2] { 0, 0 };
        const auto frames = 48000;
        const auto impulseAt = 4800;
        double weighted = 0.0, total = 0.0;

        for (int n = 0; n < frames; ++n)
        {
            std::array<float, 2> frame { 0.0f, 0.0f };

            if (n == impulseAt)
                frame[0] = frame[1] = 1.0f;

            stage.processFrame (frame.data(), ratios, hints);

            const auto magnitude = static_cast<double> (std::abs (frame[0]));
            weighted += magnitude * static_cast<double> (n - impulseAt);
            total += magnitude;
        }

        const auto measured = total > 0.0 ? weighted / total : -1.0;

        check (std::abs (measured - static_cast<double> (stage.latencyFrames())) <= 2.0,
               "an impulse arrives " + std::to_string (measured)
                   + " frames later, and the stage told the host "
                   + std::to_string (stage.latencyFrames()));
    }
   #endif
}

void printDiagnostics()
{
    std::printf ("\n   Diagnostic: the detector's reading for a set of real notes, at 48 kHz\\n");

    for (const auto frequency : { 55.0, 110.0, 146.832, 196.0, 246.942, 329.628, 440.0, 659.255 })
    {
        const auto reading = readNote (48000.0, frequency, 1.5);
        std::printf ("       %8.3f Hz  ->  %8.3f Hz  %6.1f cents  conf %.2f\n", frequency,
                     static_cast<double> (reading.frequency),
                     cents (reading.frequency, frequency),
                     static_cast<double> (reading.confidence));
    }
}
// =============================================================================
//  The corrector's rule: the +-10 cent dead zone, the semitone grip, and how
//  TUNE AMOUNT scales what is left.
//
//  This is the one part of the autotune path that is pure arithmetic - the note
//  the detector heard and the note the scale wants go in, the ratio the shifter
//  reads at comes out - so it is checked at exact cent values either side of the
//  boundary rather than through a rendered tone, where 10 cents is inside the
//  tracker's own error.
//
//  What it pins is the claim the header makes: inside the zone the ratio is 1
//  BIT FOR BIT (so the shifter reads at unity and the note passes untouched),
//  outside it the pull is the interval the reading asked for, amount scales the
//  pull rather than the zone, and the grip never exceeds a semitone.
// =============================================================================
void testCorrectionDeadZone()
{
    using j37dsp::PitchCorrectionRule;

    const auto atCents = [] (double cents)
    {
        return static_cast<float> (440.0 * std::pow (2.0, cents / 1200.0));
    };

    constexpr float target = 440.0f;

    check (PitchCorrectionRule::deadZoneCents == 10.0f,
           "the dead zone is the documented +-10 cents");

    // --- inside the zone: no correction at all ------------------------------
    for (const auto cents : { 0.0, 3.0, -3.0, 9.9, -9.9, 10.0, -10.0 })
    {
        const auto detected = atCents (cents);
        check (PitchCorrectionRule::ratio (detected, target, 1.0f) == 1.0f,
               "  " + std::to_string (cents) + " cents is left alone (ratio exactly 1)");
    }

    check (PitchCorrectionRule::inDeadZone (atCents (10.0), target)
           && ! PitchCorrectionRule::inDeadZone (atCents (10.5), target),
           "the boundary itself is inside the zone and half a cent past it is not");

    // --- outside it: the interval the reading asked for ---------------------
    {
        const auto detected = atCents (-10.5);            // a hair flat
        const auto expected = std::pow (2.0, 10.5 / 1200.0);
        const auto ratio = PitchCorrectionRule::ratio (detected, target, 1.0f);

        check (ratio > 1.0f && std::abs (ratio - static_cast<float> (expected)) < 2.0e-4f,
               "10.5 cents flat is pulled up by the interval it is off by");
    }
    {
        const auto detected = atCents (-37.0);            // a third of a semitone flat
        const auto expected = std::pow (2.0, 37.0 / 1200.0);
        const auto ratio = PitchCorrectionRule::ratio (detected, target, 1.0f);

        check (std::abs (ratio - static_cast<float> (expected)) < 2.0e-4f,
               "and a third of a semitone is pulled the whole way at full amount");
    }

    // --- TUNE AMOUNT scales the PULL, not the zone --------------------------
    {
        const auto detected = atCents (-50.0);
        const auto full = PitchCorrectionRule::ratio (detected, target, 1.0f);
        const auto half = PitchCorrectionRule::ratio (detected, target, 0.5f);
        const auto none = PitchCorrectionRule::ratio (detected, target, 0.0f);

        check (std::abs (half - (1.0f + (full - 1.0f) * 0.5f)) < 2.0e-5f,
               "half amount is half the pull on a note outside the zone");
        check (none == 1.0f, "and zero amount is no pull at all");
    }

    // --- the grip: a semitone either way, never more ------------------------
    {
        // A reading SHARP of the target has to be pulled DOWN (ratio below 1) and
        // a flat one pushed UP, so the clamps are the other way round from the
        // direction of the error.
        const auto sharp = PitchCorrectionRule::ratio (atCents (200.0), target, 1.0f);
        const auto flat = PitchCorrectionRule::ratio (atCents (-200.0), target, 1.0f);

        check (std::abs (sharp - PitchCorrectionRule::minRatio) < 1.0e-6f,
               "a reading two semitones sharp is gripped to the -6 % clamp");
        check (std::abs (flat - PitchCorrectionRule::maxRatio) < 1.0e-6f,
               "and two semitones flat to the +6 % one");
    }

    // --- nonsense in, unity out --------------------------------------------
    check (PitchCorrectionRule::ratio (0.0f, target, 1.0f) == 1.0f
           && PitchCorrectionRule::ratio (target, 0.0f, 1.0f) == 1.0f,
           "a missing reading or target yields unity rather than a division");
}
} // namespace

int main()
{
    std::printf ("pitch stage harness (autotune detector and shifter)\n");

    testTrackerFollowsNotes();
    testTrackerIsRateIndependent();
    testTrackerHoldsTheOctave();
    testTrackerRejectsNoise();
    testTrackerFollowsThroughNoise();
    testShifterMovesThePitch();
    testSpliceResidueIsBounded();
    testUnityIsTransparent();
    testShifterKeepsItsFooting();
    testCorrectionDeadZone();
    testCorrectorUsesBlockAlignedRatio();
    testCorrectorShape();
    testCorrectorMovesThePitch();
    testCorrectorKeepsTheDuration();
    printDiagnostics();

    std::printf ("\n%s (%d failure%s)\n", gFailures == 0 ? "PITCH CHECK PASSED" : "FAILURES PRESENT",
                 gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
