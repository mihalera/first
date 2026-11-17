// =============================================================================
//  One unique AUDIBLE test per parameter, rendered through the real plugin.
//
//    target: j37_parameter_audio_tests   (cmake --build build --target ...)
//
//  tests/parameters/unique_test.cpp pins every parameter's TABLE row - the
//  numbers the host automates. This harness asks the question those rows cannot:
//  what does each parameter do to SOUND. Every one of the 149 parameters gets
//  its own unique audible test, rendered through the shipping processor - the
//  real FirstAudioProcessor, its real processBlock, no editor, no host:
//
//    - a fresh instance per render, prepared at 48 kHz, transport at PLAY;
//    - the parameter under test moved to the value its claim needs, the rest of
//      the context set so the effect is exposed (a delay TIME test engages the
//      delay head, a comp test opens COMP MIX, and so on);
//    - the claim asserted on rendered samples with the numbers that make it
//      THIS parameter's claim and no other's: a band, a lag, a ratio, a count,
//      a direction, or an exact bit-for-bit identity where the parameter's
//      audible contract is "the sound must not react".
//
//  Two meta rules keep the coverage complete, the same way the table harness's
//  are:
//
//    1. every registered parameter has exactly one audible test - a parameter
//       added without one fails the harness;
//    2. every test names a registered parameter, and the rows sit in the table's
//       own order - row i is parameter i.
//
//  This is the harness plugin-improvement-plan.md item 5 asked for ("a harness
//  ... that drives the whole chain") pointed at item 7's question ("nothing in
//  the suite compares a rendered buffer to a reference"): it drives processBlock
//  the way a host does and reads the answer back out of the samples.
// =============================================================================
#include "PluginProcessor.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;
constexpr int framesPerSecond = 48000;

// The level the tone tests run at: -18 dBFS, comfortably below the input glue's
// soft knee at the neutral trims, so compression does not answer a tone test.
constexpr float toneAmp = 0.125f;

int failures = 0;
int checksRun = 0;

bool check (bool condition, const std::string& what)
{
    ++checksRun;
    if (! condition)
    {
        std::printf ("      FAIL  %s\n", what.c_str());
        ++failures;
    }
    return condition;
}

// -----------------------------------------------------------------------------
//  Signals.
// -----------------------------------------------------------------------------

std::vector<float> sine (double hertz, int frames, float amplitude = toneAmp)
{
    std::vector<float> signal (static_cast<std::size_t> (frames));
    for (int i = 0; i < frames; ++i)
        signal[static_cast<std::size_t> (i)] = static_cast<float> (
            amplitude * std::sin (6.283185307179586 * hertz * static_cast<double> (i) / sampleRate));
    return signal;
}

std::vector<float> twoTone (double hzA, double hzB, int frames,
                            float amplitude = toneAmp * 0.7071f)
{
    std::vector<float> signal (static_cast<std::size_t> (frames));
    for (int i = 0; i < frames; ++i)
        signal[static_cast<std::size_t> (i)] = static_cast<float> (
            amplitude * (std::sin (6.283185307179586 * hzA * static_cast<double> (i) / sampleRate)
                         + std::sin (6.283185307179586 * hzB * static_cast<double> (i) / sampleRate)));
    return signal;
}

std::vector<float> zeros (int frames)
{
    return std::vector<float> (static_cast<std::size_t> (frames), 0.0f);
}

/** A tone that stops: `toneFrames` of signal then `totalFrames - toneFrames`
    of silence, for tests that read a tail or a noise floor in the gap. */
std::vector<float> toneThenSilence (double hertz, int toneFrames, int totalFrames)
{
    auto signal = zeros (totalFrames);
    const auto tone = sine (hertz, toneFrames);
    std::copy (tone.begin(), tone.end(), signal.begin());
    return signal;
}

// -----------------------------------------------------------------------------
//  The engine: a fresh real processor per render.
// -----------------------------------------------------------------------------

struct Render
{
    std::vector<float> left, right;
};

class Engine
{
public:
    Engine()
    {
        processor.prepareToPlay (sampleRate, blockSize);
    }

    void set (const char* id, float value)
    {
        if (auto* raw = processor.parameters.getRawParameterValue (id))
            raw->store (value);
    }

    Render run (const std::vector<float>& inL, const std::vector<float>& inR)
    {
        const auto frames = static_cast<int> (inL.size());
        Render out;
        out.left.resize (inL.size());
        out.right.resize (inR.size());

        juce::MidiBuffer midi;
        for (int offset = 0; offset < frames; offset += blockSize)
        {
            const auto chunk = std::min (blockSize, frames - offset);
            juce::AudioBuffer<float> buffer (2, chunk);
            for (int i = 0; i < chunk; ++i)
            {
                buffer.setSample (0, i, inL[static_cast<std::size_t> (offset + i)]);
                buffer.setSample (1, i, inR[static_cast<std::size_t> (offset + i)]);
            }
            processor.processBlock (buffer, midi);
            for (int i = 0; i < chunk; ++i)
            {
                out.left[static_cast<std::size_t> (offset + i)] = buffer.getSample (0, i);
                out.right[static_cast<std::size_t> (offset + i)] = buffer.getSample (1, i);
            }
        }
        return out;
    }

    FirstAudioProcessor processor;
};

using Overrides = std::vector<std::pair<const char*, float>>;

Render renderWith (const Overrides& overrides, const std::vector<float>& inL,
                   const std::vector<float>& inR)
{
    Engine engine;
    for (const auto& override_ : overrides)
        engine.set (override_.first, override_.second);
    return engine.run (inL, inR);
}

/** A stereo render of one mono signal, at MIX 100 (all machine) unless the
    overrides say otherwise - the position where the machine alone is heard. */
Render renderMono (const Overrides& overrides, const std::vector<float>& signal)
{
    return renderWith (overrides, signal, signal);
}

// -----------------------------------------------------------------------------
//  Measurements. All are plain reads of rendered samples.
// -----------------------------------------------------------------------------

double rmsRange (const std::vector<float>& signal, std::size_t from, std::size_t to)
{
    from = std::min (from, signal.size());
    to = std::min (to, signal.size());
    if (to <= from)
        return 0.0;
    double sum = 0.0;
    for (std::size_t i = from; i < to; ++i)
        sum += static_cast<double> (signal[i]) * static_cast<double> (signal[i]);
    return std::sqrt (sum / static_cast<double> (to - from));
}

double dbOf (double linear)
{
    return 20.0 * std::log10 (std::max (linear, 1.0e-12));
}

double rmsDb (const std::vector<float>& signal, std::size_t from, std::size_t to)
{
    return dbOf (rmsRange (signal, from, to));
}

/** Energy at one frequency, normalised by the window's length. */
double goertzel (const std::vector<float>& signal, double hertz,
                 std::size_t from = 0, std::size_t to = 0)
{
    if (to == 0 || to > signal.size())
        to = signal.size();
    if (to <= from)
        return 0.0;

    const auto omega = 6.283185307179586 * hertz / sampleRate;
    const auto coefficient = 2.0 * std::cos (omega);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (std::size_t i = from; i < to; ++i)
    {
        s0 = signal[i] + coefficient * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const auto power = s1 * s1 + s2 * s2 - coefficient * s1 * s2;
    return std::sqrt (std::max (0.0, power)) / static_cast<double> (to - from);
}

/** The loudest spectral peak's frequency, scanned +-`spanHertz` around `centre`
    at `stepHertz` resolution. This is the pitch reader the autotune tests use:
    a half-second window resolves a couple of Hz, which is under 10 cents there. */
double peakFrequency (const std::vector<float>& signal, double centre,
                      double spanHertz, double stepHertz,
                      std::size_t from, std::size_t to)
{
    double best = centre, bestLevel = 0.0;
    for (double hz = centre - spanHertz; hz <= centre + spanHertz; hz += stepHertz)
    {
        const auto level = goertzel (signal, hz, from, to);
        if (level > bestLevel)
        {
            bestLevel = level;
            best = hz;
        }
    }
    return best;
}

double correlation (const std::vector<float>& a, const std::vector<float>& b,
                    std::size_t from, std::size_t to)
{
    to = std::min (to, std::min (a.size(), b.size()));
    if (to <= from)
        return 0.0;
    double sab = 0.0, saa = 0.0, sbb = 0.0;
    for (std::size_t i = from; i < to; ++i)
    {
        sab += static_cast<double> (a[i]) * static_cast<double> (b[i]);
        saa += static_cast<double> (a[i]) * static_cast<double> (a[i]);
        sbb += static_cast<double> (b[i]) * static_cast<double> (b[i]);
    }
    if (saa <= 0.0 || sbb <= 0.0)
        return 0.0;
    return sab / std::sqrt (saa * sbb);
}

/** The inter-channel lag that best aligns b to a, searched over +-`maxLag`
    samples with a parabolic refinement. ST OFFSET's claim is a lag in
    microseconds; half of its travel is under the one-sample grid. */
double channelLag (const std::vector<float>& a, const std::vector<float>& b, int maxLag,
                   std::size_t from, std::size_t to)
{
    to = std::min (to, std::min (a.size(), b.size()));
    from = std::max (from, static_cast<std::size_t> (maxLag) + 1);
    if (to <= from + static_cast<std::size_t> (maxLag) + 1)
        return 0.0;

    int bestLag = 0;
    double bestScore = -1.0e30;
    for (int lag = -maxLag; lag <= maxLag; ++lag)
    {
        double sum = 0.0;
        for (std::size_t i = from; i < to; ++i)
            sum += static_cast<double> (a[i]) * static_cast<double> (b[i + lag]);
        if (sum > bestScore)
        {
            bestScore = sum;
            bestLag = lag;
        }
    }
    if (bestLag <= -maxLag || bestLag >= maxLag)
        return static_cast<double> (bestLag);

    auto scoreAt = [&] (int lag)
    {
        double sum = 0.0;
        for (std::size_t i = from; i < to; ++i)
            sum += static_cast<double> (a[i]) * static_cast<double> (b[i + lag]);
        return sum;
    };
    const auto cM = scoreAt (bestLag - 1);
    const auto c0 = scoreAt (bestLag);
    const auto cP = scoreAt (bestLag + 1);
    const auto denominator = cM - 2.0 * c0 + cP;
    const auto shift = std::abs (denominator) > 1.0e-30
                           ? 0.5 * (cM - cP) / denominator
                           : 0.0;
    return static_cast<double> (bestLag) + std::max (-1.0, std::min (1.0, shift));
}

/** How far the loudest sample stands above the 10 ms local RMS: the crackle and
    clicks reader (impulse noise stands tall over the surface around it). */
double spikeOverLocalRms (const std::vector<float>& signal, std::size_t from, std::size_t to)
{
    constexpr std::size_t window = 480;
    double worst = 0.0;
    for (std::size_t start = from; start + window < to; start += window)
    {
        const auto local = rmsRange (signal, start, start + window);
        if (local < 1.0e-9)
            continue;
        for (std::size_t i = start; i < start + window; ++i)
            worst = std::max (worst, std::abs (static_cast<double> (signal[i])) / local);
    }
    return worst;
}

/** Count of samples whose magnitude passes `factor` times the local RMS. */
int spikeCount (const std::vector<float>& signal, double factor, std::size_t from, std::size_t to)
{
    constexpr std::size_t window = 480;
    int count = 0;
    for (std::size_t start = from; start + window < to; start += window)
    {
        const auto local = rmsRange (signal, start, start + window);
        if (local < 1.0e-9)
            continue;
        for (std::size_t i = start; i < start + window; ++i)
            if (std::abs (static_cast<double> (signal[i])) > factor * local)
                ++count;
    }
    return count;
}

/** The ratio of the loudest to the quietest `window`-frame RMS after `from`:
    how much the level MOVES, which is what tremolo, warp and sag promise. */
double levelSwing (const std::vector<float>& signal, std::size_t window, std::size_t from)
{
    double loudest = 0.0, quietest = 1.0e30;
    for (std::size_t start = from; start + window < signal.size(); start += window)
    {
        const auto level = rmsRange (signal, start, start + window);
        loudest = std::max (loudest, level);
        quietest = std::min (quietest, level);
    }
    return quietest > 1.0e-9 ? loudest / quietest : 0.0;
}

std::size_t distinctValues (const std::vector<float>& signal, std::size_t from)
{
    std::vector<float> copy (signal.begin() + static_cast<std::ptrdiff_t> (from), signal.end());
    std::sort (copy.begin(), copy.end());
    copy.erase (std::unique (copy.begin(), copy.end()), copy.end());
    return copy.size();
}

/** Fraction of adjacent samples that are exactly equal: the rate crusher's
    held-sample grid, invisible to a level or spectrum read. */
double heldSampleFraction (const std::vector<float>& signal, std::size_t from, std::size_t to)
{
    to = std::min (to, signal.size());
    if (to <= from + 1)
        return 0.0;
    std::size_t held = 0;
    for (std::size_t i = from; i + 1 < to; ++i)
        if (signal[i] == signal[i + 1])
            ++held;
    return static_cast<double> (held) / static_cast<double> (to - from - 1);
}

/** Zero-crossing rate per second: the pitch direction a coast reads as. */
double zeroCrossingsPerSecond (const std::vector<float>& signal, std::size_t from, std::size_t to)
{
    to = std::min (to, signal.size());
    if (to <= from + 1)
        return 0.0;
    std::size_t crossings = 0;
    for (std::size_t i = from; i + 1 < to; ++i)
        if ((signal[i] < 0.0f) != (signal[i + 1] < 0.0f))
            ++crossings;
    return static_cast<double> (crossings) * sampleRate
           / static_cast<double> (to - from - 1);
}

double maxAbs (const std::vector<float>& signal, std::size_t from, std::size_t to)
{
    to = std::min (to, signal.size());
    double worst = 0.0;
    for (std::size_t i = from; i < to; ++i)
        worst = std::max (worst, std::abs (static_cast<double> (signal[i])));
    return worst;
}

bool finiteEverywhere (const Render& render)
{
    for (const auto sample : render.left)
        if (! std::isfinite (sample))
            return false;
    for (const auto sample : render.right)
        if (! std::isfinite (sample))
            return false;
    return true;
}

/** Largest per-sample difference between two renders (the identity reader). */
double maxDifference (const Render& a, const Render& b)
{
    const auto n = std::min (a.left.size(), b.left.size());
    double worst = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        worst = std::max (worst,
                          std::abs (static_cast<double> (a.left[i]) - static_cast<double> (b.left[i])));
    return worst;
}

} // namespace

#include <fstream>

// =============================================================================
//  Probe round 6 - calibrating the last ambiguous reads before the 149 test
//  bodies are pinned.
// =============================================================================
namespace
{

std::vector<float> whiteNoise (int frames, float amp, std::uint32_t seed = 7u)
{
    std::vector<float> s (static_cast<std::size_t> (frames));
    std::uint32_t r = seed;
    for (int i = 0; i < frames; ++i)
    {
        r = r * 1664525u + 1013904223u;
        const auto unit = static_cast<float> (((r >> 8) & 0x00ffffffu)) / 8388608.0f - 1.0f;
        s[static_cast<std::size_t> (i)] = amp * unit;
    }
    return s;
}

std::vector<float> mixed (const std::vector<float>& a, const std::vector<float>& b)
{
    const auto n = std::min (a.size(), b.size());
    std::vector<float> s (n);
    for (std::size_t i = 0; i < n; ++i)
        s[i] = a[i] + b[i];
    return s;
}

std::vector<float> burstTrain (int frames, double hertz, float amp, int onFrames, int offFrames)
{
    auto s = zeros (frames);
    int i = 0;
    while (i < frames)
    {
        const auto on = std::min (onFrames, frames - i);
        const auto burst = sine (hertz, on, amp);
        std::copy (burst.begin(), burst.end(), s.begin() + i);
        i += onFrames + offFrames;
    }
    return s;
}

double windowRmsAvg (const std::vector<float>& s, int firstStart, int stride,
                     int offIn, int len, int bursts)
{
    double sum = 0.0;
    for (int b = 0; b < bursts; ++b)
        sum += rmsRange (s, static_cast<std::size_t> (firstStart + b * stride + offIn),
                         static_cast<std::size_t> (firstStart + b * stride + offIn + len));
    return sum / static_cast<double> (bursts);
}

struct DiffPeak { double offset = 0.0; double level = 0.0; };

DiffPeak diffScan (const std::vector<float>& a, const std::vector<float>& b,
                   double carrier, double fromHz, double toHz, double stepHz,
                   std::size_t from, std::size_t to)
{
    DiffPeak best;
    for (double offset = fromHz; offset <= toHz; offset += stepHz)
    {
        const auto dPlus  = std::abs (goertzel (a, carrier + offset, from, to)
                                    - goertzel (b, carrier + offset, from, to));
        const auto dMinus = std::abs (goertzel (a, carrier - offset, from, to)
                                    - goertzel (b, carrier - offset, from, to));
        const auto level = dPlus + dMinus;
        if (level > best.level)
        {
            best.level = level;
            best.offset = offset;
        }
    }
    return best;
}

} // namespace

int main()
{
    const int F2 = 2 * framesPerSecond;
    const int F6 = 6 * framesPerSecond;
    const std::size_t sa = 9600;
    const std::size_t sb = static_cast<std::size_t> (F2) - 4800;
    const std::size_t mid = static_cast<std::size_t> (F6) / 2;
    const std::size_t s6 = static_cast<std::size_t> (F6) - 9600;

    {   // CA01 comp attack on a tight head window, at the knob's effective ends
        const auto t = burstTrain (F2, 440.0, 0.9f, 960, 9600);
        const auto slow = renderMono ({{ "auto_gain", 0.0f }, { "glue_on", 0.0f },
                                        { "comp_mix", 100.0f }, { "comp_threshold", -18.0f },
                                        { "comp_ratio", 20.0f }, { "comp_release", 50.0f },
                                        { "comp_attack", 100.0f }}, t);
        const auto fast = renderMono ({{ "auto_gain", 0.0f }, { "glue_on", 0.0f },
                                        { "comp_mix", 100.0f }, { "comp_threshold", -18.0f },
                                        { "comp_ratio", 20.0f }, { "comp_release", 50.0f },
                                        { "comp_attack", 0.2f }}, t);
        auto head = [] (const std::vector<float>& s)
        { return windowRmsAvg (s, 0, 10560, 240, 480, 8); };
        auto late = [] (const std::vector<float>& s)
        { return windowRmsAvg (s, 0, 10560, 480, 480, 8); };
        std::printf ("CA01 comp attack         : head slow %.2f dB fast %.2f dB | late slow %.2f fast %.2f\n",
                     dbOf (head (slow.left)), dbOf (head (fast.left)),
                     dbOf (late (slow.left)), dbOf (late (fast.left)));
    }
    {   // GL01 glue on a tone that sits over the input stage's threshold
        const auto t = sine (440.0, F2, 0.7f);
        for (const auto inputDb : { 0.0f, -6.0f })
        {
            const auto on = renderMono ({{ "auto_gain", 0.0f }, { "mix", 100.0f },
                                          { "input_db", inputDb }}, t);
            const auto off = renderMono ({{ "auto_gain", 0.0f }, { "mix", 100.0f },
                                           { "input_db", inputDb }, { "glue_on", 0.0f }}, t);
            std::printf ("GL01 glue in %4.0f        : on rms %.2f dB swing %.4f | off rms %.2f dB swing %.4f | diff %.3e\n",
                         inputDb,
                         rmsDb (on.left, sa, sb), levelSwing (on.left, 4800, sa),
                         rmsDb (off.left, sa, sb), levelSwing (off.left, 4800, sa),
                         maxDifference (on, off));
        }
    }
    {   // EQ06 the four shelves, on a quiet input that stays under the machine
        for (const auto hz : { 80.0, 6000.0 })
        {
            const auto t = sine (hz, F2, 0.03f);
            const auto flat = renderMono ({{ "mix", 100.0f }, { "auto_gain", 0.0f }}, t);
            const auto inLo = renderMono ({{ "mix", 100.0f }, { "auto_gain", 0.0f },
                                            { "in_low", 12.0f }}, t);
            const auto inHi = renderMono ({{ "mix", 100.0f }, { "auto_gain", 0.0f },
                                            { "in_high", 12.0f }}, t);
            const auto outLo = renderMono ({{ "mix", 100.0f }, { "auto_gain", 0.0f },
                                             { "out_low", 12.0f }}, t);
            const auto outHi = renderMono ({{ "mix", 100.0f }, { "auto_gain", 0.0f },
                                             { "out_high", 12.0f }}, t);
            std::printf ("EQ06 shelf @ %6.1f       : inLo %+.2f inHi %+.2f outLo %+.2f outHi %+.2f dB\n", hz,
                         dbOf (rmsRange (inLo.left, sa, sb)) - dbOf (rmsRange (flat.left, sa, sb)),
                         dbOf (rmsRange (inHi.left, sa, sb)) - dbOf (rmsRange (flat.left, sa, sb)),
                         dbOf (rmsRange (outLo.left, sa, sb)) - dbOf (rmsRange (flat.left, sa, sb)),
                         dbOf (rmsRange (outHi.left, sa, sb)) - dbOf (rmsRange (flat.left, sa, sb)));
        }
    }
    {   // EQ01b the bell on a quiet input, at two levels
        for (const auto amp : { 0.02f, 0.05f })
        {
            const auto t = sine (1000.0, F2, amp);
            const auto flat = renderMono ({{ "mix", 100.0f }, { "auto_gain", 0.0f }}, t);
            const auto up = renderMono ({{ "mix", 100.0f }, { "auto_gain", 0.0f },
                                          { "in_mid", 12.0f }}, t);
            std::printf ("EQ01b in_mid +12 @1k amp %.2f : gain %+.2f dB\n", amp,
                         dbOf (rmsRange (up.left, sa, sb)) - dbOf (rmsRange (flat.left, sa, sb)));
        }
    }
    {   // EQ03b the Q trim's corner shift, read above the corner
        const auto t = twoTone (650.0, 1000.0, F2, 0.2f);
        const auto wide = renderMono ({{ "mix", 100.0f }, { "auto_gain", 0.0f },
                                        { "in_hp_freq", 500.0f }, { "in_eq_q", 0.5f }}, t);
        const auto tight = renderMono ({{ "mix", 100.0f }, { "auto_gain", 0.0f },
                                         { "in_hp_freq", 500.0f }, { "in_eq_q", 1.5f }}, t);
        std::printf ("EQ03b Q                  : 650/1000 wide %+.2f dB tight %+.2f dB\n",
                     dbOf (goertzel (wide.left, 650.0, sa, sb) / goertzel (wide.left, 1000.0, sa, sb)),
                     dbOf (goertzel (tight.left, 650.0, sa, sb) / goertzel (tight.left, 1000.0, sa, sb)));
    }
    {   // EQ05b the mid EQ's position on a hot tone: the harmonics answer only the input side
        const auto t = sine (500.0, F2, 0.5f);
        const auto flat = renderMono ({{ "auto_gain", 0.0f }, { "mix", 100.0f }, { "drive", 0.9f }}, t);
        const auto inUp = renderMono ({{ "auto_gain", 0.0f }, { "mix", 100.0f }, { "drive", 0.9f },
                                        { "in_mid", 12.0f }}, t);
        const auto outUp = renderMono ({{ "auto_gain", 0.0f }, { "mix", 100.0f }, { "drive", 0.9f },
                                         { "out_mid", 12.0f }}, t);
        auto h2ratio = [&] (const std::vector<float>& s)
        { return goertzel (s, 1000.0, sa, sb) / std::max (1.0e-12, goertzel (s, 500.0, sa, sb)); };
        std::printf ("EQ05b mid position       : H2/500 flat %.4f in+12 %.4f out+12 %.4f\n",
                     h2ratio (flat.left), h2ratio (inUp.left), h2ratio (outUp.left));
    }
    {   // EQ04b the low shelf's position: the IM field only answers the input side
        const auto t = mixed (sine (80.0, F2, 0.4f), sine (8000.0, F2, 0.3f));
        const auto flat = renderMono ({{ "auto_gain", 0.0f }, { "mix", 100.0f }, { "drive", 0.9f }}, t);
        const auto inUp = renderMono ({{ "auto_gain", 0.0f }, { "mix", 100.0f }, { "drive", 0.9f },
                                        { "in_low", 12.0f }}, t);
        const auto outUp = renderMono ({{ "auto_gain", 0.0f }, { "mix", 100.0f }, { "drive", 0.9f },
                                         { "out_low", 12.0f }}, t);
        auto imRatio = [&] (const std::vector<float>& s)
        { return (goertzel (s, 7920.0, sa, sb) + goertzel (s, 8080.0, sa, sb))
                    / std::max (1.0e-12, goertzel (s, 8000.0, sa, sb)); };
        std::printf ("EQ04b low position       : IM/8k flat %.3e in+12 %.3e out+12 %.3e | 8k flat %.3e in %.3e out %.3e\n",
                     imRatio (flat.left), imRatio (inUp.left), imRatio (outUp.left),
                     goertzel (flat.left, 8000.0, sa, sb), goertzel (inUp.left, 8000.0, sa, sb),
                     goertzel (outUp.left, 8000.0, sa, sb));
    }
    {   // PD01 predelay by the late-window energy the shift moves
        const auto t = toneThenSilence (440.0, 14400, 3 * framesPerSecond);
        for (const auto pd : { 20.0f, 80.0f })
        {
            const auto wet = renderMono ({{ "mix", 100.0f }, { "reverb", 1.0f },
                                           { "reverb_predelay", pd }}, t);
            std::printf ("PD01 predelay %3.0f ms     : rms 320-420ms %.2f dB | rms 420-520ms %.2f dB\n", pd,
                         dbOf (rmsRange (wet.left, 15360, 20160)),
                         dbOf (rmsRange (wet.left, 20160, 24960)));
        }
    }
    {   // AT02 speed on a full-semitone glide
        const auto t = sine (415.30, F2, 0.2f);
        const auto slow = renderMono ({{ "mix", 100.0f }, { "tuning_tonic", 9.0f },
                                        { "autotune_amount", 1.0f }, { "autotune_speed", 0.0f }}, t);
        const auto fast = renderMono ({{ "mix", 100.0f }, { "tuning_tonic", 9.0f },
                                        { "autotune_amount", 1.0f }, { "autotune_speed", 1.0f }}, t);
        auto zcr = [] (const std::vector<float>& s)
        { return zeroCrossingsPerSecond (s, 480, 1920) * 0.5; };
        std::printf ("AT02 speed               : 10-40ms slow %.1f fast %.1f | settled slow %.1f fast %.1f\n",
                     zcr (slow.left), zcr (fast.left),
                     peakFrequency (slow.left, 440.0, 12.0, 0.25, sa, sb),
                     peakFrequency (fast.left, 440.0, 12.0, 0.25, sa, sb));
    }
    {   // VS03b all four vinyl types on the same noise print
        const auto n = whiteNoise (F2, 0.3f);
        for (const auto type : { 0.0f, 1.0f, 2.0f, 3.0f })
        {
            const auto r = renderMono ({{ "mix", 100.0f }, { "vinyl", 1.0f }, { "noise", 0.5f },
                                         { "auto_gain", 0.0f }, { "vinyl_type", type },
                                         { "vinyl_crackle", 0.0f }, { "vinyl_rumble", 0.0f }}, n);
            std::printf ("VS03b vinyl type %.0f       : rms %.2f | 30 %.3e 440 %.3e 6k %.3e | 6k/30 %.2f\n",
                         type, rmsDb (r.left, sa, sb),
                         goertzel (r.left, 30.0, sa, sb), goertzel (r.left, 440.0, sa, sb),
                         goertzel (r.left, 6000.0, sa, sb),
                         goertzel (r.left, 6000.0, sa, sb) / std::max (1.0e-12, goertzel (r.left, 30.0, sa, sb)));
        }
    }
    {   // ME01b the mechanics' roughening on the flutter field
        const auto t = sine (440.0, F6);
        const auto even = renderMono ({{ "mix", 100.0f }, { "flutter", 1.0f }, { "wow", 0.0f },
                                        { "mechanics", 0.0f }}, t);
        const auto rough = renderMono ({{ "mix", 100.0f }, { "flutter", 1.0f }, { "wow", 0.0f },
                                         { "mechanics", 1.0f }}, t);
        const auto scan = diffScan (even.left, rough.left, 440.0, 0.30, 30.0, 0.10, mid, s6);
        std::printf ("ME01b mech on flutter    : diff peak %.2f Hz level %.3e | +-8.9 even %.3e rough %.3e\n",
                     scan.offset, scan.level,
                     goertzel (even.left, 448.9, mid, s6) + goertzel (even.left, 431.1, mid, s6),
                     goertzel (rough.left, 448.9, mid, s6) + goertzel (rough.left, 431.1, mid, s6));
    }

    std::printf ("probe6 done\n");
    return 0;
}
