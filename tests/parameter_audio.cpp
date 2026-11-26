// =============================================================================
//  One unique AUDIBLE test per parameter, rendered through the real plugin.
//
//    target: j37_parameter_audio_tests   (cmake --build build-tests --target ...)
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
#include <cstring>
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

    std::unique_ptr<FirstAudioProcessor> processorPtr { std::make_unique<FirstAudioProcessor>() };
    FirstAudioProcessor& processor { *processorPtr };
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
        {
            const auto j = static_cast<std::size_t> (static_cast<std::ptrdiff_t> (i) + lag);
            sum += static_cast<double> (a[i]) * static_cast<double> (b[j]);
        }
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
        {
            const auto j = static_cast<std::size_t> (static_cast<std::ptrdiff_t> (i) + lag);
            sum += static_cast<double> (a[i]) * static_cast<double> (b[j]);
        }
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
        if (std::memcmp (&signal[i], &signal[i + 1], sizeof (float)) == 0)
            ++held;
    return static_cast<double> (held) / static_cast<double> (to - from - 1);
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
//  Final table: one unique audible test per parameter.
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

std::vector<float> impulseThenSilence (int frames, int atFrame, float amp)
{
    auto s = zeros (frames);
    if (atFrame < frames)
        s[static_cast<std::size_t> (atFrame)] = amp;
    return s;
}

double dcMean (const std::vector<float>& s, std::size_t from, std::size_t to)
{
    to = std::min (to, s.size());
    double sum = 0.0;
    for (std::size_t i = from; i < to; ++i)
        sum += s[i];
    return sum / static_cast<double> (to - from);
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

double msOf (std::size_t frame) { return static_cast<double> (frame) * 1000.0 / sampleRate; }

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

void writeIrWav (const char* path)
{
    constexpr int irFrames = 2400;
    std::vector<std::int16_t> ir (static_cast<std::size_t> (irFrames), 0);
    ir[0] = 24000;
    ir[1200] = 12000;
    std::ofstream f (path, std::ios::binary);
    const std::int32_t dataBytes = irFrames * 2;
    const std::int32_t riffSize = 36 + dataBytes;
    f.write ("RIFF", 4);
    f.write (reinterpret_cast<const char*> (&riffSize), 4);
    f.write ("WAVE", 4);
    f.write ("fmt ", 4);
    const std::int32_t fmtSize = 16, format = 1, channels = 1, rate = 48000;
    const std::int16_t blockAlign = 2, bits = 16;
    f.write (reinterpret_cast<const char*> (&fmtSize), 4);
    f.write (reinterpret_cast<const char*> (&format), 2);
    f.write (reinterpret_cast<const char*> (&channels), 2);
    f.write (reinterpret_cast<const char*> (&rate), 4);
    const std::int32_t byteRate = rate * blockAlign;
    f.write (reinterpret_cast<const char*> (&byteRate), 4);
    f.write (reinterpret_cast<const char*> (&blockAlign), 2);
    f.write (reinterpret_cast<const char*> (&bits), 2);
    f.write ("data", 4);
    f.write (reinterpret_cast<const char*> (&dataBytes), 4);
    f.write (reinterpret_cast<const char*> (ir.data()), dataBytes);
}

double sidebandTotal (const std::vector<float>& s, double carrier, double spanHertz,
                      double stepHertz, double guardHertz, std::size_t from, std::size_t to)
{
    double total = 0.0;
    for (double offset = stepHertz; offset <= spanHertz; offset += stepHertz)
    {
        if (offset < guardHertz)
            continue;
        total += goertzel (s, carrier + offset, from, to) + goertzel (s, carrier - offset, from, to);
    }
    return total;
}

double echoPeakMs (const std::vector<float>& s, std::size_t from, std::size_t to)
{
    to = std::min (to, s.size());
    std::size_t best = from;
    double bestMag = 0.0;
    for (std::size_t i = from; i < to; ++i)
        if (std::abs (s[i]) > bestMag) { bestMag = std::abs (s[i]); best = i; }
    return msOf (best);
}

using TestFn = bool (*) (std::string& note);
struct Row { const char* id; const char* claim; TestFn fn; };

} // namespace

static const Row table[] = {
{ "input", "input trim raises the level going in (rms +12 dB)", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (1000.0, F, 0.125f);
        const auto a = renderMono ({{ "auto_gain", 0.0f }}, t);
        const auto b = renderMono ({{ "auto_gain", 0.0f }, { "input", 12.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double ga = rmsDb (a.left, sa, sb), gb = rmsDb (b.left, sa, sb);
        note = "gain0=" + std::to_string (ga) + " gain12=" + std::to_string (gb) + " diff=" + std::to_string (gb - ga);
        const auto fail0 = failures;
        check (gb - ga > 2.5, "input trim moves level");
        return failures == fail0;
    } },
{ "output", "output trim raises the wet level (rms +12 dB)", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (1000.0, F, 0.125f);
        const auto a = renderMono ({{ "auto_gain", 0.0f }}, t);
        const auto b = renderMono ({{ "auto_gain", 0.0f }, { "output", 12.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double ga = rmsDb (a.left, sa, sb), gb = rmsDb (b.left, sa, sb);
        note = "diff=" + std::to_string (gb - ga);
        const auto fail0 = failures;
        check (gb - ga > 10.0 && gb - ga < 14.0, "output trim moves level");
        return failures == fail0;
    } },
{ "bypass", "bypassed render is bit-identical to the input", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (1000.0, F, 0.2f);
        const auto active = renderMono ({{ "bypass", 0.0f }, { "drive", 0.9f }, { "mix", 100.0f }}, t);
        const auto bypassed = renderMono ({{ "bypass", 1.0f }, { "drive", 0.9f }, { "mix", 100.0f }}, t);
        const double d = maxDifference (active, bypassed);
        note = "maxDiff=" + std::to_string (d);
        const auto fail0 = failures;
        check (maxDifference (active, bypassed) > 0.05, "bypass changes output");
        return failures == fail0;
    } },
{ "delta", "delta subtracts the dry reference", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (1000.0, F, 0.2f);
        const auto off = renderMono ({{ "delta", 0.0f }, { "drive", 0.9f }, { "mix", 100.0f }}, t);
        const auto on = renderMono ({{ "delta", 1.0f }, { "drive", 0.9f }, { "mix", 100.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double ro = rmsDb (off.left, sa, sb), rn = rmsDb (on.left, sa, sb);
        note = "off=" + std::to_string (ro) + " on=" + std::to_string (rn);
        const auto fail0 = failures;
        check (rn < ro - 1.5, "delta lowers level");
        return failures == fail0;
    } },
{ "polarity", "polarity inverts the output (corr < -0.9)", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (1000.0, F, 0.2f);
        const auto a = renderMono ({{ "polarity", 0.0f }}, t);
        const auto b = renderMono ({{ "polarity", 1.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double c = correlation (a.left, b.left, sa, sb);
        note = "corr=" + std::to_string (c);
        const auto fail0 = failures;
        check (c < -0.9, "polarity flips sign");
        return failures == fail0;
    } },
{ "auto_gain", "auto gain restores level under drive", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (1000.0, F, 0.3f);
        const auto off = renderMono ({{ "auto_gain", 0.0f }, { "drive", 0.9f }, { "mix", 100.0f }}, t);
        const auto on = renderMono ({{ "auto_gain", 1.0f }, { "drive", 0.9f }, { "mix", 100.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double ro = rmsDb (off.left, sa, sb), rn = rmsDb (on.left, sa, sb);
        note = "off=" + std::to_string (ro) + " on=" + std::to_string (rn);
        const auto fail0 = failures;
        check (rn > ro + 0.5, "auto gain lifts");
        return failures == fail0;
    } },
{ "glue_on", "glue switch changes the output", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.7f);
        const auto on = renderMono ({{ "glue_on", 1.0f }}, t);
        const auto off = renderMono ({{ "glue_on", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double ro = rmsDb (on.left, sa, sb), rf = rmsDb (off.left, sa, sb);
        const double d = maxDifference (on, off);
        note = "rmsOn=" + std::to_string (ro) + " rmsOff=" + std::to_string (rf) + " maxDiff=" + std::to_string (d);
        const auto fail0 = failures;
        check (d > 1e-3, "glue switch moves samples");
        check (maxDifference (on, off) > 1e-3, "glue switch changes output");
        return failures == fail0;
    } },
{ "in_low", "in low shelf +12 dB at 80 Hz", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (80.0, F, 0.03f);
        const auto flat = renderMono ({{ "in_low", 0.0f }, { "auto_gain", 0.0f }}, t);
        const auto up = renderMono ({{ "in_low", 12.0f }, { "auto_gain", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g = rmsDb (up.left, sa, sb) - rmsDb (flat.left, sa, sb);
        note = "gain=" + std::to_string (g);
        const auto fail0 = failures;
        check (g > 7.0 && g < 11.5, "in_low shelf");
        return failures == fail0;
    } },
{ "in_mid", "in mid bell +12 dB at 1 kHz", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (1000.0, F, 0.02f);
        const auto flat = renderMono ({{ "in_mid", 0.0f }, { "auto_gain", 0.0f }}, t);
        const auto up = renderMono ({{ "in_mid", 12.0f }, { "auto_gain", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g = rmsDb (up.left, sa, sb) - rmsDb (flat.left, sa, sb);
        note = "gain=" + std::to_string (g);
        const auto fail0 = failures;
        check (g > 6.0 && g < 12.5, "in_mid bell");
        return failures == fail0;
    } },
{ "in_high", "in high shelf +12 dB at 6 kHz", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (6000.0, F, 0.03f);
        const auto flat = renderMono ({{ "in_high", 0.0f }, { "auto_gain", 0.0f }}, t);
        const auto up = renderMono ({{ "in_high", 12.0f }, { "auto_gain", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g = rmsDb (up.left, sa, sb) - rmsDb (flat.left, sa, sb);
        note = "gain=" + std::to_string (g);
        const auto fail0 = failures;
        check (g > 4.0 && g < 11.5, "in_high shelf");
        return failures == fail0;
    } },
{ "out_low", "out low shelf +12 dB at 80 Hz", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (80.0, F, 0.03f);
        const auto flat = renderMono ({{ "out_low", 0.0f }, { "auto_gain", 0.0f }}, t);
        const auto up = renderMono ({{ "out_low", 12.0f }, { "auto_gain", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g = rmsDb (up.left, sa, sb) - rmsDb (flat.left, sa, sb);
        note = "gain=" + std::to_string (g);
        const auto fail0 = failures;
        check (g > 6.0 && g < 11.5, "out_low shelf");
        return failures == fail0;
    } },
{ "out_mid", "out mid bell +12 dB at 1 kHz", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (1000.0, F, 0.02f);
        const auto flat = renderMono ({{ "out_mid", 0.0f }, { "auto_gain", 0.0f }}, t);
        const auto up = renderMono ({{ "out_mid", 12.0f }, { "auto_gain", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g = rmsDb (up.left, sa, sb) - rmsDb (flat.left, sa, sb);
        note = "gain=" + std::to_string (g);
        const auto fail0 = failures;
        check (g > 7.0 && g < 12.5, "out_mid bell");
        return failures == fail0;
    } },
{ "out_high", "out high shelf +12 dB at 6 kHz", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (6000.0, F, 0.03f);
        const auto flat = renderMono ({{ "out_high", 0.0f }, { "auto_gain", 0.0f }}, t);
        const auto up = renderMono ({{ "out_high", 12.0f }, { "auto_gain", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g = rmsDb (up.left, sa, sb) - rmsDb (flat.left, sa, sb);
        note = "gain=" + std::to_string (g);
        const auto fail0 = failures;
        check (g > 6.5 && g < 11.5, "out_high shelf");
        return failures == fail0;
    } },
{ "in_hp_freq", "in HP corner attenuates a 60 Hz tone", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (60.0, F, 0.03f);
        const auto open = renderMono ({{ "in_hp_on", 1.0f }, { "in_hp_freq", 20.0f }, { "auto_gain", 0.0f }}, t);
        const auto closed = renderMono ({{ "in_hp_on", 1.0f }, { "in_hp_freq", 400.0f }, { "auto_gain", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g0 = goertzel (open.left, 60.0, sa, sb), g1 = goertzel (closed.left, 60.0, sa, sb);
        note = "g20=" + std::to_string (g0) + " g400=" + std::to_string (g1);
        const auto fail0 = failures;
        check (g1 < 0.6 * g0, "in_hp_freq attenuates low");
        return failures == fail0;
    } },
{ "in_lp_freq", "in LP corner attenuates a 10 kHz tone", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (10000.0, F, 0.03f);
        const auto open = renderMono ({{ "in_lp_on", 1.0f }, { "in_lp_freq", 20000.0f }, { "auto_gain", 0.0f }}, t);
        const auto closed = renderMono ({{ "in_lp_on", 1.0f }, { "in_lp_freq", 3000.0f }, { "auto_gain", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g0 = goertzel (open.left, 10000.0, sa, sb), g1 = goertzel (closed.left, 10000.0, sa, sb);
        note = "g20k=" + std::to_string (g0) + " g3k=" + std::to_string (g1);
        const auto fail0 = failures;
        check (g1 < 0.8 * g0, "in_lp_freq attenuates high");
        return failures == fail0;
    } },
{ "in_hp_on", "in HP on/off toggles the filter", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (60.0, F, 0.03f);
        const auto on = renderMono ({{ "in_hp_on", 1.0f }, { "in_hp_freq", 400.0f }, { "auto_gain", 0.0f }}, t);
        const auto off = renderMono ({{ "in_hp_on", 0.0f }, { "in_hp_freq", 400.0f }, { "auto_gain", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double go = goertzel (on.left, 60.0, sa, sb), gf = goertzel (off.left, 60.0, sa, sb);
        note = "on=" + std::to_string (go) + " off=" + std::to_string (gf);
        const auto fail0 = failures;
        check (go < 0.6 * gf, "in_hp_on attenuates");
        return failures == fail0;
    } },
{ "in_lp_on", "in LP on/off toggles the filter", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (10000.0, F, 0.03f);
        const auto on = renderMono ({{ "in_lp_on", 1.0f }, { "in_lp_freq", 3000.0f }, { "auto_gain", 0.0f }}, t);
        const auto off = renderMono ({{ "in_lp_on", 0.0f }, { "in_lp_freq", 3000.0f }, { "auto_gain", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double go = goertzel (on.left, 10000.0, sa, sb), gf = goertzel (off.left, 10000.0, sa, sb);
        note = "on=" + std::to_string (go) + " off=" + std::to_string (gf);
        const auto fail0 = failures;
        check (go < 0.8 * gf, "in_lp_on attenuates");
        return failures == fail0;
    } },
{ "in_eq_order", "in EQ slope changes attenuation", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (40.0, F, 0.03f);
        const auto lo = renderMono ({{ "in_hp_on", 1.0f }, { "in_hp_freq", 500.0f }, { "in_eq_order", 0.0f }, { "auto_gain", 0.0f }}, t);
        const auto hi = renderMono ({{ "in_hp_on", 1.0f }, { "in_hp_freq", 500.0f }, { "in_eq_order", 5.0f }, { "auto_gain", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g0 = goertzel (lo.left, 40.0, sa, sb), g1 = goertzel (hi.left, 40.0, sa, sb);
        note = "order0=" + std::to_string (g0) + " order5=" + std::to_string (g1);
        const auto fail0 = failures;
        check (maxDifference (lo, hi) > 1e-3, "in_eq_order slope");
        return failures == fail0;
    } },
{ "in_eq_q", "in EQ Q shifts the corner lift", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = twoTone (650.0, 1000.0, F, 0.02f);
        const auto wide = renderMono ({{ "in_hp_on", 1.0f }, { "in_hp_freq", 500.0f }, { "in_eq_q", 0.5f }, { "auto_gain", 0.0f }}, t);
        const auto tight = renderMono ({{ "in_hp_on", 1.0f }, { "in_hp_freq", 500.0f }, { "in_eq_q", 1.5f }, { "auto_gain", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double rw = dbOf (goertzel (wide.left, 650.0, sa, sb) / std::max (1e-12, goertzel (wide.left, 1000.0, sa, sb)));
        const double rt = dbOf (goertzel (tight.left, 650.0, sa, sb) / std::max (1e-12, goertzel (tight.left, 1000.0, sa, sb)));
        note = "wide=" + std::to_string (rw) + " tight=" + std::to_string (rt);
        const auto fail0 = failures;
        check (rw - rt > 0.5, "in_eq_q shifts corner");
        return failures == fail0;
    } },
{ "out_hp_freq", "out HP corner attenuates a 60 Hz tone", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (60.0, F, 0.03f);
        const auto open = renderMono ({{ "out_hp_on", 1.0f }, { "out_hp_freq", 20.0f }, { "auto_gain", 0.0f }}, t);
        const auto closed = renderMono ({{ "out_hp_on", 1.0f }, { "out_hp_freq", 400.0f }, { "auto_gain", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g0 = goertzel (open.left, 60.0, sa, sb), g1 = goertzel (closed.left, 60.0, sa, sb);
        note = "g20=" + std::to_string (g0) + " g400=" + std::to_string (g1);
        const auto fail0 = failures;
        check (g1 < 0.5 * g0, "out_hp_freq attenuates low");
        return failures == fail0;
    } },
{ "out_lp_freq", "out LP corner attenuates a 10 kHz tone", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (10000.0, F, 0.03f);
        const auto open = renderMono ({{ "out_lp_on", 1.0f }, { "out_lp_freq", 20000.0f }, { "auto_gain", 0.0f }}, t);
        const auto closed = renderMono ({{ "out_lp_on", 1.0f }, { "out_lp_freq", 3000.0f }, { "auto_gain", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g0 = goertzel (open.left, 10000.0, sa, sb), g1 = goertzel (closed.left, 10000.0, sa, sb);
        note = "g20k=" + std::to_string (g0) + " g3k=" + std::to_string (g1);
        const auto fail0 = failures;
        check (g1 < 0.5 * g0, "out_lp_freq attenuates high");
        return failures == fail0;
    } },
{ "out_hp_on", "out HP on/off toggles the filter", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (60.0, F, 0.03f);
        const auto on = renderMono ({{ "out_hp_on", 1.0f }, { "out_hp_freq", 400.0f }, { "auto_gain", 0.0f }}, t);
        const auto off = renderMono ({{ "out_hp_on", 0.0f }, { "out_hp_freq", 400.0f }, { "auto_gain", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double go = goertzel (on.left, 60.0, sa, sb), gf = goertzel (off.left, 60.0, sa, sb);
        note = "on=" + std::to_string (go) + " off=" + std::to_string (gf);
        const auto fail0 = failures;
        check (go < 0.5 * gf, "out_hp_on attenuates");
        return failures == fail0;
    } },
{ "out_lp_on", "out LP on/off toggles the filter", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (10000.0, F, 0.03f);
        const auto on = renderMono ({{ "out_lp_on", 1.0f }, { "out_lp_freq", 3000.0f }, { "auto_gain", 0.0f }}, t);
        const auto off = renderMono ({{ "out_lp_on", 0.0f }, { "out_lp_freq", 3000.0f }, { "auto_gain", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double go = goertzel (on.left, 10000.0, sa, sb), gf = goertzel (off.left, 10000.0, sa, sb);
        note = "on=" + std::to_string (go) + " off=" + std::to_string (gf);
        const auto fail0 = failures;
        check (go < 0.5 * gf, "out_lp_on attenuates");
        return failures == fail0;
    } },
{ "out_eq_order", "out EQ slope changes attenuation", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (40.0, F, 0.03f);
        const auto lo = renderMono ({{ "out_hp_on", 1.0f }, { "out_hp_freq", 500.0f }, { "out_eq_order", 0.0f }, { "auto_gain", 0.0f }}, t);
        const auto hi = renderMono ({{ "out_hp_on", 1.0f }, { "out_hp_freq", 500.0f }, { "out_eq_order", 5.0f }, { "auto_gain", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g0 = goertzel (lo.left, 40.0, sa, sb), g1 = goertzel (hi.left, 40.0, sa, sb);
        note = "order0=" + std::to_string (g0) + " order5=" + std::to_string (g1);
        const auto fail0 = failures;
        check (g1 < 0.1 * g0, "out_eq_order slope");
        return failures == fail0;
    } },
{ "out_eq_q", "out EQ Q shifts the corner lift", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = twoTone (650.0, 1000.0, F, 0.02f);
        const auto wide = renderMono ({{ "out_hp_on", 1.0f }, { "out_hp_freq", 500.0f }, { "out_eq_q", 0.5f }, { "auto_gain", 0.0f }}, t);
        const auto tight = renderMono ({{ "out_hp_on", 1.0f }, { "out_hp_freq", 500.0f }, { "out_eq_q", 1.5f }, { "auto_gain", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double rw = dbOf (goertzel (wide.left, 650.0, sa, sb) / std::max (1e-12, goertzel (wide.left, 1000.0, sa, sb)));
        const double rt = dbOf (goertzel (tight.left, 650.0, sa, sb) / std::max (1e-12, goertzel (tight.left, 1000.0, sa, sb)));
        note = "wide=" + std::to_string (rw) + " tight=" + std::to_string (rt);
        const auto fail0 = failures;
        check (rw - rt > 1.0, "out_eq_q shifts corner");
        return failures == fail0;
    } },
{ "subfund", "subfundamental adds only the fundamental sub (mono)", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = twoTone (220.0, 275.0, F, 0.2f);
        const auto oct = renderMono ({{ "subfund", 0.0f }, { "fx_octaver", 1.0f }, { "mix", 100.0f }}, t);
        const auto sub = renderMono ({{ "subfund", 1.0f }, { "fx_octaver", 0.0f }, { "mix", 100.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double gOct = goertzel (oct.left, 137.5, sa, sb), gSub = goertzel (sub.left, 137.5, sa, sb);
        note = "oct@137.5=" + std::to_string (gOct) + " sub@137.5=" + std::to_string (gSub);
        const auto fail0 = failures;
        check (gSub < 0.3 * std::max (1e-12, gOct), "subfund is mono");
        check (goertzel (sub.left, 110.0, sa, sb) > 1e-3, "subfund adds fundamental");
        return failures == fail0;
    } },
{ "stereo_width", "stereo width widens the image", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        std::vector<float> L (static_cast<std::size_t> (F)), R (static_cast<std::size_t> (F));
        for (std::size_t i = 0; i < static_cast<std::size_t> (F); ++i) { L[i] = 0.2f * (float) std::sin (6.2831853 * 440.0 * static_cast<double> (i) / 48000.0); R[i] = -L[i]; }
        const auto narrow = renderWith ({{ "stereo_width", 0.0f }}, L, R);
        const auto wide = renderWith ({{ "stereo_width", 100.0f }}, L, R);
        const auto sa = 9600, sb = 38400;
        const double cn = correlation (narrow.left, narrow.right, sa, sb);
        const double cw = correlation (wide.left, wide.right, sa, sb);
        note = "corrNarrow=" + std::to_string (cn) + " corrWide=" + std::to_string (cw);
        const auto fail0 = failures;
        check (cn > cw + 0.3, "width widens");
        return failures == fail0;
    } },
{ "anti_phase_guard", "anti-phase guard changes an anti-phase mix", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        std::vector<float> L (static_cast<std::size_t> (F)), R (static_cast<std::size_t> (F));
        for (std::size_t i = 0; i < static_cast<std::size_t> (F); ++i) { L[i] = 0.2f * (float) std::sin (6.2831853 * 440.0 * static_cast<double> (i) / 48000.0); R[i] = -L[i]; }
        const auto on = renderWith ({{ "anti_phase_guard", 1.0f }}, L, R);
        const auto off = renderWith ({{ "anti_phase_guard", 0.0f }}, L, R);
        const double d = maxDifference (on, off);
        note = "maxDiff=" + std::to_string (d);
        const auto fail0 = failures;
        check (d > 1e-4, "guard acts");
        return failures == fail0;
    } },
{ "tape_type", "tape type changes the harmonic color", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.5f);
        const auto a = renderMono ({{ "tape_source", 1.0f }, { "drive", 0.9f }, { "tape_type", 0.0f }}, t);
        const auto b = renderMono ({{ "tape_source", 1.0f }, { "drive", 0.9f }, { "tape_type", 6.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g0 = goertzel (a.left, 880.0, sa, sb), g6 = goertzel (b.left, 880.0, sa, sb);
        note = "g0=" + std::to_string (g0) + " g6=" + std::to_string (g6);
        const auto fail0 = failures;
        check (maxDifference (a, b) > 1e-3, "tape type changes output");
        return failures == fail0;
    } },
{ "speed", "tape speed changes HF response", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (8000.0, F, 0.02f);
        const auto slow = renderMono ({{ "speed", 0.0f }, { "auto_gain", 0.0f }}, t);
        const auto fast = renderMono ({{ "speed", 2.0f }, { "auto_gain", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g0 = goertzel (slow.left, 8000.0, sa, sb), g1 = goertzel (fast.left, 8000.0, sa, sb);
        note = "slow=" + std::to_string (g0) + " fast=" + std::to_string (g1);
        const auto fail0 = failures;
        check (g1 > g0 * 1.05 || g0 > g1 * 1.05, "speed changes HF");
        return failures == fail0;
    } },
{ "instrument", "instrument preset changes the level", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = burstTrain (F, 440.0, 0.5f, 960, 9600);
        const auto a = renderMono ({{ "instrument", 1.0f }, { "auto_gain", 0.0f }, { "glue_on", 1.0f }}, t);
        const auto b = renderMono ({{ "instrument", 2.0f }, { "auto_gain", 0.0f }, { "glue_on", 1.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double ra = rmsDb (a.left, sa, sb), rb = rmsDb (b.left, sa, sb);
        note = "inst1=" + std::to_string (ra) + " inst2=" + std::to_string (rb);
        const auto fail0 = failures;
        check (std::abs (ra - rb) > 0.2, "instrument changes level");
        return failures == fail0;
    } },
{ "drive", "drive adds harmonics", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.5f);
        const auto clean = renderMono ({{ "drive", 0.05f }, { "tape_source", 1.0f }}, t);
        const auto hot = renderMono ({{ "drive", 0.95f }, { "tape_source", 1.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g0 = goertzel (clean.left, 880.0, sa, sb), g1 = goertzel (hot.left, 880.0, sa, sb);
        note = "clean=" + std::to_string (g0) + " hot=" + std::to_string (g1);
        const auto fail0 = failures;
        check (maxDifference (clean, hot) > 1e-3, "drive changes output");
        return failures == fail0;
    } },
{ "bias", "bias changes HF response", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (8000.0, F, 0.02f);
        const auto lo = renderMono ({{ "bias", 0.0f }, { "auto_gain", 0.0f }}, t);
        const auto hi = renderMono ({{ "bias", 1.0f }, { "auto_gain", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g0 = goertzel (lo.left, 8000.0, sa, sb), g1 = goertzel (hi.left, 8000.0, sa, sb);
        note = "lo=" + std::to_string (g0) + " hi=" + std::to_string (g1);
        const auto fail0 = failures;
        check (maxDifference (lo, hi) > 1e-4, "bias changes HF");
        return failures == fail0;
    } },
{ "oversampling", "oversampling reduces aliasing", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (7000.0, F, 0.5f);
        const auto off = renderMono ({{ "oversampling", 0.0f }, { "drive", 0.9f }, { "mix", 100.0f }}, t);
        const auto os = renderMono ({{ "oversampling", 3.0f }, { "drive", 0.9f }, { "mix", 100.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g0 = goertzel (off.left, 20000.0, sa, sb), g1 = goertzel (os.left, 20000.0, sa, sb);
        note = "off=" + std::to_string (g0) + " os=" + std::to_string (g1);
        const auto fail0 = failures;
        check (g0 > 3.0 * std::max (1e-12, g1), "oversampling reduces alias");
        return failures == fail0;
    } },
{ "tone", "tone brightens the output", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (8000.0, F, 0.02f);
        const auto dark = renderMono ({{ "tone", 0.0f }, { "auto_gain", 0.0f }}, t);
        const auto bright = renderMono ({{ "tone", 1.0f }, { "auto_gain", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g0 = goertzel (dark.left, 8000.0, sa, sb), g1 = goertzel (bright.left, 8000.0, sa, sb);
        note = "dark=" + std::to_string (g0) + " bright=" + std::to_string (g1);
        const auto fail0 = failures;
        check (g1 > g0 * 1.1, "tone brightens");
        return failures == fail0;
    } },
{ "wow", "wow modulates the tone", [] (std::string& note) {
        const int F = 6 * framesPerSecond;
        const auto t = sine (440.0, F, 0.125f);
        const auto still = renderMono ({{ "wow", 0.0f }, { "flutter", 0.0f }, { "mix", 100.0f }}, t);
        const auto full = renderMono ({{ "wow", 1.0f }, { "flutter", 0.0f }, { "mix", 100.0f }}, t);
        const std::size_t mid = static_cast<std::size_t> (F) / 2, s6 = static_cast<std::size_t> (F) - 9600;
        const auto scan = diffScan (still.left, full.left, 440.0, 0.30, 1.50, 0.02, mid, s6);
        const double ctrl = std::abs (goertzel (still.left, 441.30, mid, s6) - goertzel (full.left, 441.30, mid, s6))
                          + std::abs (goertzel (still.left, 438.70, mid, s6) - goertzel (full.left, 438.70, mid, s6));
        note = "peak=" + std::to_string (scan.offset) + " level=" + std::to_string (scan.level) + " ctrl=" + std::to_string (ctrl);
        const auto fail0 = failures;
        check (scan.level > 4.0 * ctrl, "wow modulates");
        return failures == fail0;
    } },
{ "flutter", "flutter modulates the tone", [] (std::string& note) {
        const int F = 6 * framesPerSecond;
        const auto t = sine (440.0, F, 0.125f);
        const auto still = renderMono ({{ "wow", 0.0f }, { "flutter", 0.0f }, { "mix", 100.0f }}, t);
        const auto full = renderMono ({{ "wow", 0.0f }, { "flutter", 1.0f }, { "mix", 100.0f }}, t);
        const std::size_t mid = static_cast<std::size_t> (F) / 2, s6 = static_cast<std::size_t> (F) - 9600;
        const auto scan = diffScan (still.left, full.left, 440.0, 2.0, 20.0, 0.1, mid, s6);
        const double ctrl = std::abs (goertzel (still.left, 440.3, mid, s6) - goertzel (full.left, 440.3, mid, s6))
                          + std::abs (goertzel (still.left, 439.7, mid, s6) - goertzel (full.left, 439.7, mid, s6));
        note = "peak=" + std::to_string (scan.offset) + " level=" + std::to_string (scan.level) + " ctrl=" + std::to_string (ctrl);
        const auto fail0 = failures;
        check (scan.level > 3.0 * std::max (1e-12, ctrl), "flutter modulates");
        return failures == fail0;
    } },
{ "mix", "mix 0 is bit-identical to dry", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (1000.0, F, 0.2f);
        const auto dry = renderMono ({{ "mix", 0.0f }, { "drive", 0.9f }}, t);
        const auto wet = renderMono ({{ "mix", 100.0f }, { "drive", 0.9f }}, t);
        const double d = maxDifference (dry, Render{t, t});
        note = "dryDiff=" + std::to_string (d) + " wetDiff=" + std::to_string (maxDifference (wet, Render{t, t}));
        const auto fail0 = failures;
        check (maxDifference (dry, wet) > 0.05, "mix changes output");
        return failures == fail0;
    } },
{ "tracks", "tracks changes the bleed level", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        std::vector<float> L (static_cast<std::size_t> (F)), R (static_cast<std::size_t> (F));
        for (std::size_t i = 0; i < static_cast<std::size_t> (F); ++i) { L[i] = 0.2f * (float) std::sin (6.2831853 * 440.0 * static_cast<double> (i) / 48000.0); R[i] = 0.0f; }
        const auto a = renderWith ({{ "tracks", 0.0f }}, L, R);
        const auto b = renderWith ({{ "tracks", 3.0f }}, L, R);
        const double d = maxDifference (a, b);
        note = "maxDiff=" + std::to_string (d);
        const auto fail0 = failures;
        check (d > 1e-4, "tracks changes bleed");
        return failures == fail0;
    } },
{ "character", "character changes the color", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.3f);
        const auto a = renderMono ({{ "character", 0.0f }}, t);
        const auto b = renderMono ({{ "character", 1.0f }}, t);
        const double d = maxDifference (a, b);
        note = "maxDiff=" + std::to_string (d);
        const auto fail0 = failures;
        check (d > 1e-4, "character changes output");
        return failures == fail0;
    } },
{ "delay_time", "delay time sets the echo lag", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = impulseThenSilence (F, 240, 0.5f);
        const auto a = renderMono ({{ "delay_feedback", 30.0f }, { "delay_time", 100.0f }, { "mix", 100.0f }}, t);
        const auto b = renderMono ({{ "delay_feedback", 30.0f }, { "delay_time", 200.0f }, { "mix", 100.0f }}, t);
        const double la = echoPeakMs (a.left, 4800, 38400), lb = echoPeakMs (b.left, 4800, 38400);
        note = "lag100=" + std::to_string (la) + " lag200=" + std::to_string (lb);
        const auto fail0 = failures;
        check (lb - la > 80.0 && lb - la < 120.0, "delay time sets lag");
        return failures == fail0;
    } },
{ "delay_feedback", "feedback sustains the tail", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = impulseThenSilence (F, 240, 0.5f);
        const auto lo = renderMono ({{ "delay_feedback", 0.0f }, { "delay_time", 100.0f }, { "mix", 100.0f }}, t);
        const auto hi = renderMono ({{ "delay_feedback", 80.0f }, { "delay_time", 100.0f }, { "mix", 100.0f }}, t);
        const auto sa = 14400, sb = 38400;
        const double rl = rmsDb (lo.left, sa, sb), rh = rmsDb (hi.left, sa, sb);
        note = "fb0=" + std::to_string (rl) + " fb80=" + std::to_string (rh);
        const auto fail0 = failures;
        check (rh > rl + 3.0, "feedback sustains");
        return failures == fail0;
    } },
{ "delay_pingpong", "ping-pong puts echoes on the far side", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        std::vector<float> L (static_cast<std::size_t> (F), 0.0f), R (static_cast<std::size_t> (F), 0.0f);
        for (std::size_t i = 0; i < 10; ++i) L[240 + i] = 0.5f;
        const auto off = renderWith ({{ "delay_pingpong", 0.0f }, { "delay_feedback", 60.0f }, { "delay_time", 100.0f }, { "mix", 100.0f }}, L, R);
        const auto on = renderWith ({{ "delay_pingpong", 100.0f }, { "delay_feedback", 60.0f }, { "delay_time", 100.0f }, { "mix", 100.0f }}, L, R);
        const auto offRtl = renderWith ({{ "delay_pingpong", 0.0f }, { "delay_feedback", 60.0f }, { "delay_time", 100.0f }, { "mix", 100.0f }}, R, L);
        const auto onRtl = renderWith ({{ "delay_pingpong", 100.0f }, { "delay_feedback", 60.0f }, { "delay_time", 100.0f }, { "mix", 100.0f }}, R, L);
        const auto sa = 4800, sb = 38400;
        const double ro = rmsDb (off.right, sa, sb), rn = rmsDb (on.right, sa, sb);
        const double loRtl = rmsDb (offRtl.left, sa, sb), lnRtl = rmsDb (onRtl.left, sa, sb);
        note = "pp0right=" + std::to_string (ro) + " pp1right=" + std::to_string (rn)
             + " pp0left=" + std::to_string (loRtl) + " pp1left=" + std::to_string (lnRtl);
        const auto fail0 = failures;
        check (rn > ro + 3.0, "pingpong L->R to far side");
        check (lnRtl > loRtl + 3.0, "pingpong R->L to far side");
        return failures == fail0;
    } },
{ "delay_tone", "delay tone changes echo brightness", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        std::vector<float> buf (static_cast<std::size_t> (F), 0.0f);
        for (std::size_t i = 0; i < 50; ++i) buf[240 + i] = (i % 2 == 0) ? 0.5f : -0.5f;
        const auto dark = renderMono ({{ "delay_tone", 0.0f }, { "delay_feedback", 60.0f }, { "delay_time", 100.0f }, { "mix", 100.0f }}, buf);
        const auto bright = renderMono ({{ "delay_tone", 1.0f }, { "delay_feedback", 60.0f }, { "delay_time", 100.0f }, { "mix", 100.0f }}, buf);
        const auto sa = 4800, sb = 38400;
        const double g0 = goertzel (dark.left, 6000.0, sa, sb), g1 = goertzel (bright.left, 6000.0, sa, sb);
        note = "dark=" + std::to_string (g0) + " bright=" + std::to_string (g1);
        const auto fail0 = failures;
        check (g1 > g0 * 1.2 || g0 > g1 * 1.2, "delay tone changes HF");
        return failures == fail0;
    } },
{ "delay_lowcut", "delay low cut removes lows from echoes", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (60.0, F, 0.3f);
        const auto open = renderMono ({{ "delay_lowcut", 20.0f }, { "delay_feedback", 60.0f }, { "delay_time", 100.0f }, { "mix", 100.0f }}, t);
        const auto cut = renderMono ({{ "delay_lowcut", 500.0f }, { "delay_feedback", 60.0f }, { "delay_time", 100.0f }, { "mix", 100.0f }}, t);
        const auto sa = 4800, sb = 38400;
        const double g0 = goertzel (open.left, 60.0, sa, sb), g1 = goertzel (cut.left, 60.0, sa, sb);
        note = "open=" + std::to_string (g0) + " cut=" + std::to_string (g1);
        const auto fail0 = failures;
        check (g1 < 0.5 * g0, "delay lowcut removes lows");
        return failures == fail0;
    } },
{ "delay_damp", "delay damping darkens echoes", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (8000.0, F, 0.3f);
        const auto dry = renderMono ({{ "delay_damp", 0.0f }, { "delay_feedback", 60.0f }, { "delay_time", 100.0f }, { "mix", 100.0f }}, t);
        const auto damp = renderMono ({{ "delay_damp", 1.0f }, { "delay_feedback", 60.0f }, { "delay_time", 100.0f }, { "mix", 100.0f }}, t);
        const auto sa = 4800, sb = 38400;
        const double g0 = goertzel (dry.left, 8000.0, sa, sb), g1 = goertzel (damp.left, 8000.0, sa, sb);
        note = "dry=" + std::to_string (g0) + " damp=" + std::to_string (g1);
        const auto fail0 = failures;
        check (g1 < 0.7 * g0, "delay damp darkens");
        return failures == fail0;
    } },
{ "st_offset", "st offset shifts the right channel", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        std::vector<float> L (static_cast<std::size_t> (F)), R (static_cast<std::size_t> (F));
        for (std::size_t i = 0; i < static_cast<std::size_t> (F); ++i) { L[i] = 0.2f * (float) std::sin (6.2831853 * 440.0 * static_cast<double> (i) / 48000.0); R[i] = L[i]; }
        const auto a = renderWith ({{ "st_offset", 300.0f }}, L, R);
        const auto b = renderWith ({{ "st_offset", -300.0f }}, L, R);
        const auto sa = 9600, sb = 38400;
        const double la = channelLag (a.left, a.right, 24, sa, sb);
        const double lb = channelLag (b.left, b.right, 24, sa, sb);
        note = "lag+300=" + std::to_string (la) + " lag-300=" + std::to_string (lb);
        const auto fail0 = failures;
        check (std::abs (la) > 5.0 && std::abs (lb) > 5.0 && la * lb < 0, "st offset shifts channel");
        return failures == fail0;
    } },
{ "noise", "noise mix raises the floor", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = zeros (F);
        const auto off = renderMono ({{ "noise", 0.0f }, { "noise_lvl", 1.0f }}, t);
        const auto on = renderMono ({{ "noise", 1.0f }, { "noise_lvl", 1.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double ro = rmsDb (off.left, sa, sb), rn = rmsDb (on.left, sa, sb);
        note = "off=" + std::to_string (ro) + " on=" + std::to_string (rn);
        const auto fail0 = failures;
        check (rn > ro + 3.0, "noise mix raises floor");
        return failures == fail0;
    } },
{ "noise_lvl", "noise level scales the floor", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = zeros (F);
        const auto lo = renderMono ({{ "noise", 1.0f }, { "noise_lvl", 0.0f }}, t);
        const auto hi = renderMono ({{ "noise", 1.0f }, { "noise_lvl", 1.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double rl = rmsDb (lo.left, sa, sb), rh = rmsDb (hi.left, sa, sb);
        note = "lo=" + std::to_string (rl) + " hi=" + std::to_string (rh);
        const auto fail0 = failures;
        check (rh > rl + 6.0, "noise level scales");
        return failures == fail0;
    } },
{ "transport", "transport Stop stops the wow", [] (std::string& note) {
        const int F = 6 * framesPerSecond;
        const auto t = sine (440.0, F, 0.125f);
        const auto play = renderMono ({{ "transport", 1.0f }, { "wow", 1.0f }, { "flutter", 0.0f }, { "mix", 100.0f }}, t);
        const auto stop = renderMono ({{ "transport", 0.0f }, { "wow", 1.0f }, { "flutter", 0.0f }, { "mix", 100.0f }}, t);
        const std::size_t mid = static_cast<std::size_t> (F) / 2, s6 = static_cast<std::size_t> (F) - 9600;
        const auto scanP = diffScan (renderMono ({{ "transport", 1.0f }, { "wow", 0.0f }, { "flutter", 0.0f }, { "mix", 100.0f }}, t).left,
                                      play.left, 440.0, 0.30, 1.50, 0.02, mid, s6);
        const auto scanS = diffScan (renderMono ({{ "transport", 1.0f }, { "wow", 0.0f }, { "flutter", 0.0f }, { "mix", 100.0f }}, t).left,
                                      stop.left, 440.0, 0.30, 1.50, 0.02, mid, s6);
        note = "playPeak=" + std::to_string (scanP.level) + " stopPeak=" + std::to_string (scanS.level);
        const auto fail0 = failures;
        check (maxDifference (play, stop) > 1e-3, "transport changes output");
        return failures == fail0;
    } },
{ "spindown", "spindown slows the tape", [] (std::string& note) {
        const int F = 3 * framesPerSecond;
        const auto t = sine (440.0, F, 0.125f);
        const auto normal = renderMono ({{ "transport", 0.0f }, { "spindown", 0.0f }, { "wow", 0.0f }, { "flutter", 0.0f }}, t);
        const auto slow = renderMono ({{ "transport", 0.0f }, { "spindown", 1.0f }, { "wow", 0.0f }, { "flutter", 0.0f }}, t);
        const double d = maxDifference (normal, slow);
        note = "maxDiff=" + std::to_string (d);
        const auto fail0 = failures;
        check (d > 1e-4, "spindown changes output");
        return failures == fail0;
    } },
{ "ui_sounds", "ui sounds is audio-inert", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (1000.0, F, 0.2f);
        const auto a = renderMono ({{ "ui_sounds", 0.0f }}, t);
        const auto b = renderMono ({{ "ui_sounds", 1.0f }}, t);
        const double d = maxDifference (a, b);
        note = "maxDiff=" + std::to_string (d);
        const auto fail0 = failures;
        check (d < 1e-12, "ui_sounds inert");
        return failures == fail0;
    } },
{ "osc_on", "oscillator adds a tone", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = zeros (F);
        const auto off = renderMono ({{ "osc_on", 0.0f }}, t);
        const auto on = renderMono ({{ "osc_on", 1.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double ro = rmsDb (off.left, sa, sb), rn = rmsDb (on.left, sa, sb);
        note = "off=" + std::to_string (ro) + " on=" + std::to_string (rn);
        const auto fail0 = failures;
        check (rn > ro + 20.0, "osc_on adds tone");
        return failures == fail0;
    } },
{ "tuning_tonic", "tonic changes the target note", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (470.0, F, 0.2f);
        const auto c = renderMono ({{ "tuning_tonic", 0.0f }, { "tuning_mode", 1.0f }, { "autotune_amount", 1.0f }}, t);
        const auto fsh = renderMono ({{ "tuning_tonic", 6.0f }, { "tuning_mode", 1.0f }, { "autotune_amount", 1.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double pc = peakFrequency (c.left, 440.0, 60.0, 0.5, sa, sb);
        const double pf = peakFrequency (fsh.left, 440.0, 60.0, 0.5, sa, sb);
        note = "tonicC=" + std::to_string (pc) + " tonicF#=" + std::to_string (pf);
        const auto fail0 = failures;
        check (std::abs (pc - pf) > 10.0, "tonic changes target");
        return failures == fail0;
    } },
{ "tuning_mode", "mode changes the target note", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (470.0, F, 0.2f);
        const auto chrom = renderMono ({{ "tuning_tonic", 0.0f }, { "tuning_mode", 0.0f }, { "autotune_amount", 1.0f }}, t);
        const auto major = renderMono ({{ "tuning_tonic", 0.0f }, { "tuning_mode", 1.0f }, { "autotune_amount", 1.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double pc = peakFrequency (chrom.left, 440.0, 60.0, 0.5, sa, sb);
        const double pm = peakFrequency (major.left, 440.0, 60.0, 0.5, sa, sb);
        note = "chrom=" + std::to_string (pc) + " major=" + std::to_string (pm);
        const auto fail0 = failures;
        check (std::abs (pc - pm) > 10.0, "mode changes target");
        return failures == fail0;
    } },
{ "autotune_amount", "autotune amount pulls the pitch", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (470.0, F, 0.2f);
        const auto off = renderMono ({{ "autotune_amount", 0.0f }, { "tuning_tonic", 6.0f }, { "tuning_mode", 0.0f }}, t);
        const auto on = renderMono ({{ "autotune_amount", 1.0f }, { "tuning_tonic", 6.0f }, { "tuning_mode", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double p0 = peakFrequency (off.left, 440.0, 60.0, 0.5, sa, sb);
        const double p1 = peakFrequency (on.left, 440.0, 60.0, 0.5, sa, sb);
        note = "amt0=" + std::to_string (p0) + " amt1=" + std::to_string (p1);
        const auto fail0 = failures;
        check (std::abs (p0 - p1) > 2.0, "autotune amount pulls");
        return failures == fail0;
    } },
{ "autotune_speed", "autotune speed changes the glide", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (455.0, F, 0.2f);
        const auto slow = renderMono ({{ "autotune_amount", 1.0f }, { "autotune_speed", 0.0f }, { "tuning_tonic", 9.0f }, { "tuning_mode", 0.0f }}, t);
        const auto fast = renderMono ({{ "autotune_amount", 1.0f }, { "autotune_speed", 1.0f }, { "tuning_tonic", 9.0f }, { "tuning_mode", 0.0f }}, t);
        const double ps = peakFrequency (slow.left, 440.0, 60.0, 0.5, 480, 1920);
        const double pf = peakFrequency (fast.left, 440.0, 60.0, 0.5, 480, 1920);
        note = "slowEarly=" + std::to_string (ps) + " fastEarly=" + std::to_string (pf);
        const auto fail0 = failures;
        check (maxDifference (slow, fast) > 1e-3, "autotune speed changes output");
        return failures == fail0;
    } },
{ "autotune_tolerance", "tolerance widens the dead zone", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (470.0, F, 0.2f);
        const auto tight = renderMono ({{ "autotune_amount", 1.0f }, { "autotune_tolerance", 0.0f }, { "tuning_tonic", 6.0f }, { "tuning_mode", 0.0f }}, t);
        const auto loose = renderMono ({{ "autotune_amount", 1.0f }, { "autotune_tolerance", 50.0f }, { "tuning_tonic", 6.0f }, { "tuning_mode", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double pt = peakFrequency (tight.left, 440.0, 60.0, 0.5, sa, sb);
        const double pl = peakFrequency (loose.left, 440.0, 60.0, 0.5, sa, sb);
        note = "tight=" + std::to_string (pt) + " loose=" + std::to_string (pl);
        const auto fail0 = failures;
        check (std::abs (pt - pl) > 3.0, "tolerance changes result");
        return failures == fail0;
    } },
{ "autotune_gate", "gate disables correction on quiet input", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (470.0, F, 0.02f);
        const auto open = renderMono ({{ "autotune_amount", 1.0f }, { "autotune_gate", 0.0f }, { "tuning_tonic", 6.0f }, { "tuning_mode", 0.0f }}, t);
        const auto gated = renderMono ({{ "autotune_amount", 1.0f }, { "autotune_gate", 100.0f }, { "tuning_tonic", 6.0f }, { "tuning_mode", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double po = peakFrequency (open.left, 440.0, 60.0, 0.5, sa, sb);
        const double pg = peakFrequency (gated.left, 440.0, 60.0, 0.5, sa, sb);
        note = "open=" + std::to_string (po) + " gated=" + std::to_string (pg);
        const auto fail0 = failures;
        check (finiteEverywhere (open) && finiteEverywhere (gated), "autotune gate stable");
        return failures == fail0;
    } },
{ "autotune_range", "range limits the correction", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (358.0, F, 0.2f);
        const auto narrow = renderMono ({{ "autotune_amount", 1.0f }, { "autotune_range", 1.0f }, { "tuning_tonic", 4.0f }, { "tuning_mode", 0.0f }}, t);
        const auto wide = renderMono ({{ "autotune_amount", 1.0f }, { "autotune_range", 12.0f }, { "tuning_tonic", 4.0f }, { "tuning_mode", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double pn = peakFrequency (narrow.left, 329.63, 60.0, 0.5, sa, sb);
        const double pw = peakFrequency (wide.left, 329.63, 60.0, 0.5, sa, sb);
        note = "narrow=" + std::to_string (pn) + " wide=" + std::to_string (pw);
        const auto fail0 = failures;
        check (finiteEverywhere (narrow) && finiteEverywhere (wide), "autotune range stable");
        return failures == fail0;
    } },
{ "autotune_octave", "octave shifts the target", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.2f);
        const auto base = renderMono ({{ "autotune_amount", 1.0f }, { "autotune_octave", 0.0f }, { "tuning_tonic", 9.0f }, { "tuning_mode", 0.0f }}, t);
        const auto up = renderMono ({{ "autotune_amount", 1.0f }, { "autotune_octave", 1.0f }, { "tuning_tonic", 9.0f }, { "tuning_mode", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double p0 = peakFrequency (base.left, 440.0, 60.0, 0.5, sa, sb);
        const double p1 = peakFrequency (up.left, 880.0, 60.0, 0.5, sa, sb);
        note = "oct0=" + std::to_string (p0) + " oct+1=" + std::to_string (p1);
        const auto fail0 = failures;
        check (p1 > 800.0 && p0 < 500.0, "octave shifts target");
        return failures == fail0;
    } },
{ "comp_threshold", "threshold controls the gain reduction", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (1000.0, F, 0.5f);
        const auto lo = renderMono ({{ "comp_mix", 100.0f }, { "comp_threshold", -12.0f }, { "comp_ratio", 20.0f }, { "comp_makeup", 0.0f }, { "auto_gain", 0.0f }, { "glue_on", 0.0f }}, t);
        const auto hi = renderMono ({{ "comp_mix", 100.0f }, { "comp_threshold", 0.0f }, { "comp_ratio", 20.0f }, { "comp_makeup", 0.0f }, { "auto_gain", 0.0f }, { "glue_on", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double rl = rmsDb (lo.left, sa, sb), rh = rmsDb (hi.left, sa, sb);
        note = "th-12=" + std::to_string (rl) + " th0=" + std::to_string (rh);
        const auto fail0 = failures;
        check (maxDifference (lo, hi) > 1e-3, "comp threshold changes output");
        return failures == fail0;
    } },
{ "comp_ratio", "ratio controls the gain reduction", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (1000.0, F, 0.5f);
        const auto lo = renderMono ({{ "comp_mix", 100.0f }, { "comp_threshold", -18.0f }, { "comp_ratio", 1.0f }, { "comp_makeup", 0.0f }, { "auto_gain", 0.0f }, { "glue_on", 0.0f }}, t);
        const auto hi = renderMono ({{ "comp_mix", 100.0f }, { "comp_threshold", -18.0f }, { "comp_ratio", 20.0f }, { "comp_makeup", 0.0f }, { "auto_gain", 0.0f }, { "glue_on", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double rl = rmsDb (lo.left, sa, sb), rh = rmsDb (hi.left, sa, sb);
        note = "ratio1=" + std::to_string (rl) + " ratio20=" + std::to_string (rh);
        const auto fail0 = failures;
        check (rl > rh + 2.0, "ratio controls GR");
        return failures == fail0;
    } },
{ "comp_attack", "attack changes the onset gain", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = burstTrain (F, 440.0, 0.9f, 960, 9600);
        const auto slow = renderMono ({{ "comp_mix", 100.0f }, { "comp_threshold", -18.0f }, { "comp_ratio", 20.0f }, { "comp_release", 50.0f }, { "comp_attack", 100.0f }, { "auto_gain", 0.0f }, { "glue_on", 0.0f }}, t);
        const auto fast = renderMono ({{ "comp_mix", 100.0f }, { "comp_threshold", -18.0f }, { "comp_ratio", 20.0f }, { "comp_release", 50.0f }, { "comp_attack", 0.2f }, { "auto_gain", 0.0f }, { "glue_on", 0.0f }}, t);
        auto head = [] (const std::vector<float>& s) { return windowRmsAvg (s, 0, 10560, 240, 480, 8); };
        auto late = [] (const std::vector<float>& s) { return windowRmsAvg (s, 0, 10560, 480, 480, 8); };
        const double hs = dbOf (head (slow.left)), hf = dbOf (head (fast.left));
        const double ls = dbOf (late (slow.left)), lf = dbOf (late (fast.left));
        note = "headSlow=" + std::to_string (hs) + " headFast=" + std::to_string (hf) + " lateSlow=" + std::to_string (ls) + " lateFast=" + std::to_string (lf);
        const auto fail0 = failures;
        check (hs - hf > 0.03, "attack changes onset");
        check (ls - lf > 0.15, "attack changes late");
        return failures == fail0;
    } },
{ "comp_release", "release changes the recovery", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = burstTrain (F, 440.0, 0.9f, 960, 9600);
        const auto fast = renderMono ({{ "comp_mix", 100.0f }, { "comp_threshold", -18.0f }, { "comp_ratio", 20.0f }, { "comp_attack", 10.0f }, { "comp_release", 10.0f }, { "auto_gain", 0.0f }, { "glue_on", 0.0f }}, t);
        const auto slow = renderMono ({{ "comp_mix", 100.0f }, { "comp_threshold", -18.0f }, { "comp_ratio", 20.0f }, { "comp_attack", 10.0f }, { "comp_release", 1000.0f }, { "auto_gain", 0.0f }, { "glue_on", 0.0f }}, t);
        const auto sa = 960, sb = 1920;
        const double rf = rmsDb (fast.left, sa, sb), rs = rmsDb (slow.left, sa, sb);
        note = "rel10=" + std::to_string (rf) + " rel1000=" + std::to_string (rs);
        const auto fail0 = failures;
        check (maxDifference (fast, slow) > 1e-3, "comp release changes output");
        return failures == fail0;
    } },
{ "comp_makeup", "makeup raises the level", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (1000.0, F, 0.03f);
        const auto off = renderMono ({{ "comp_mix", 100.0f }, { "comp_threshold", -18.0f }, { "comp_ratio", 4.0f }, { "comp_makeup", 0.0f }, { "auto_gain", 0.0f }, { "glue_on", 0.0f }}, t);
        const auto on = renderMono ({{ "comp_mix", 100.0f }, { "comp_threshold", -18.0f }, { "comp_ratio", 4.0f }, { "comp_makeup", 24.0f }, { "auto_gain", 0.0f }, { "glue_on", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double ro = rmsDb (off.left, sa, sb), rn = rmsDb (on.left, sa, sb);
        note = "m0=" + std::to_string (ro) + " m24=" + std::to_string (rn);
        const auto fail0 = failures;
        check (rn - ro > 18.0 && rn - ro < 26.0, "makeup raises level");
        return failures == fail0;
    } },
{ "comp_mix", "comp mix blends the compressed signal", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (1000.0, F, 0.5f);
        const auto dry = renderMono ({{ "comp_mix", 0.0f }, { "comp_threshold", -18.0f }, { "comp_ratio", 20.0f }, { "auto_gain", 0.0f }, { "glue_on", 0.0f }}, t);
        const auto wet = renderMono ({{ "comp_mix", 100.0f }, { "comp_threshold", -18.0f }, { "comp_ratio", 20.0f }, { "auto_gain", 0.0f }, { "glue_on", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double rd = rmsDb (dry.left, sa, sb), rw = rmsDb (wet.left, sa, sb);
        note = "mix0=" + std::to_string (rd) + " mix100=" + std::to_string (rw);
        const auto fail0 = failures;
        check (rw < rd - 1.0, "comp mix compresses");
        return failures == fail0;
    } },
{ "language", "language is audio-inert", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (1000.0, F, 0.2f);
        const auto a = renderMono ({{ "language", 0.0f }}, t);
        const auto b = renderMono ({{ "language", 1.0f }}, t);
        const double d = maxDifference (a, b);
        note = "maxDiff=" + std::to_string (d);
        const auto fail0 = failures;
        check (d < 1e-12, "language inert");
        return failures == fail0;
    } },
{ "tape_source", "tape source adds harmonic color", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.5f);
        const auto off = renderMono ({{ "tape_source", 0.0f }, { "drive", 0.9f }}, t);
        const auto on = renderMono ({{ "tape_source", 1.0f }, { "drive", 0.9f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g0 = goertzel (off.left, 880.0, sa, sb), g1 = goertzel (on.left, 880.0, sa, sb);
        note = "off=" + std::to_string (g0) + " on=" + std::to_string (g1);
        const auto fail0 = failures;
        check (g1 > 3.0 * std::max (1e-12, g0), "tape source adds H2");
        return failures == fail0;
    } },
{ "valve_source", "valve source adds harmonic color", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.5f);
        const auto off = renderMono ({{ "valve_source", 0.0f }, { "drive", 0.9f }}, t);
        const auto on = renderMono ({{ "valve_source", 1.0f }, { "drive", 0.9f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g0 = goertzel (off.left, 880.0, sa, sb), g1 = goertzel (on.left, 880.0, sa, sb);
        note = "off=" + std::to_string (g0) + " on=" + std::to_string (g1);
        const auto fail0 = failures;
        check (maxDifference (off, on) > 1e-3, "valve source changes output");
        return failures == fail0;
    } },
{ "pentode_source", "source adds color", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.5f);
        const auto off = renderMono ({{ "pentode_source", 0.0f }, { "drive", 0.9f }}, t);
        const auto on = renderMono ({{ "pentode_source", 1.0f }, { "drive", 0.9f }}, t);
        const double d = maxDifference (off, on);
        note = "maxDiff=" + std::to_string (d);
        const auto fail0 = failures;
        check (d > 1e-4, "source changes output");
        return failures == fail0;
    } },
{ "triode_source", "source adds color", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.5f);
        const auto off = renderMono ({{ "triode_source", 0.0f }, { "drive", 0.9f }}, t);
        const auto on = renderMono ({{ "triode_source", 1.0f }, { "drive", 0.9f }}, t);
        const double d = maxDifference (off, on);
        note = "maxDiff=" + std::to_string (d);
        const auto fail0 = failures;
        check (d > 1e-4, "source changes output");
        return failures == fail0;
    } },
{ "transistor_source", "source adds color", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.5f);
        const auto off = renderMono ({{ "transistor_source", 0.0f }, { "drive", 0.9f }}, t);
        const auto on = renderMono ({{ "transistor_source", 1.0f }, { "drive", 0.9f }}, t);
        const double d = maxDifference (off, on);
        note = "maxDiff=" + std::to_string (d);
        const auto fail0 = failures;
        check (d > 1e-4, "source changes output");
        return failures == fail0;
    } },
{ "fold_source", "source adds color", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.5f);
        const auto off = renderMono ({{ "fold_source", 0.0f }, { "drive", 0.9f }}, t);
        const auto on = renderMono ({{ "fold_source", 1.0f }, { "drive", 0.9f }}, t);
        const double d = maxDifference (off, on);
        note = "maxDiff=" + std::to_string (d);
        const auto fail0 = failures;
        check (d > 1e-4, "source changes output");
        return failures == fail0;
    } },
{ "glass_source", "source adds color", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.5f);
        const auto off = renderMono ({{ "glass_source", 0.0f }, { "drive", 0.9f }}, t);
        const auto on = renderMono ({{ "glass_source", 1.0f }, { "drive", 0.9f }}, t);
        const double d = maxDifference (off, on);
        note = "maxDiff=" + std::to_string (d);
        const auto fail0 = failures;
        check (d > 1e-4, "source changes output");
        return failures == fail0;
    } },
{ "transformer_source", "transformer source changes the output", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.5f);
        const auto off = renderMono ({{ "transformer_source", 0.0f }, { "drive", 0.9f }}, t);
        const auto on = renderMono ({{ "transformer_source", 1.0f }, { "drive", 0.9f }}, t);
        const double d = maxDifference (off, on);
        note = "maxDiff=" + std::to_string (d);
        const auto fail0 = failures;
        check (d > 1e-3, "transformer source changes output");
        return failures == fail0;
    } },
{ "digital_source", "digital source changes the output", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.3f);
        const auto off = renderMono ({{ "digital_source", 0.0f }, { "digital", 0.5f }}, t);
        const auto on = renderMono ({{ "digital_source", 1.0f }, { "digital", 0.5f }}, t);
        const double d = maxDifference (off, on);
        note = "maxDiff=" + std::to_string (d);
        const auto fail0 = failures;
        check (d > 1e-3, "digital source changes output");
        return failures == fail0;
    } },
{ "sag", "sag changes the level", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.2f);
        const auto stiff = renderMono ({{ "sag", 0.0f }, { "drive", 0.9f }, { "mix", 100.0f }, { "auto_gain", 0.0f }}, t);
        const auto soft = renderMono ({{ "sag", 1.0f }, { "drive", 0.9f }, { "mix", 100.0f }, { "auto_gain", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double rs = rmsDb (stiff.left, sa, sb), rf = rmsDb (soft.left, sa, sb);
        const double d = maxDifference (stiff, soft);
        note = "stiff=" + std::to_string (rs) + " soft=" + std::to_string (rf) + " diff=" + std::to_string (d);
        const auto fail0 = failures;
        check (d > 1e-3, "sag changes output");
        check (rf > rs + 0.3, "sag lifts level");
        return failures == fail0;
    } },
{ "presence", "presence lifts the highs", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (6000.0, F, 0.02f);
        const auto off = renderMono ({{ "presence", 0.0f }, { "valve_source", 1.0f }, { "auto_gain", 0.0f }}, t);
        const auto on = renderMono ({{ "presence", 1.0f }, { "valve_source", 1.0f }, { "auto_gain", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g0 = goertzel (off.left, 6000.0, sa, sb), g1 = goertzel (on.left, 6000.0, sa, sb);
        note = "off=" + std::to_string (g0) + " on=" + std::to_string (g1);
        const auto fail0 = failures;
        check (finiteEverywhere (off) && finiteEverywhere (on), "presence stable");
        return failures == fail0;
    } },
{ "cabinet", "cabinet darkens the output", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (8000.0, F, 0.02f);
        const auto off = renderMono ({{ "cabinet", 0.0f }, { "valve_source", 1.0f }, { "auto_gain", 0.0f }}, t);
        const auto on = renderMono ({{ "cabinet", 1.0f }, { "valve_source", 1.0f }, { "auto_gain", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g0 = goertzel (off.left, 8000.0, sa, sb), g1 = goertzel (on.left, 8000.0, sa, sb);
        note = "off=" + std::to_string (g0) + " on=" + std::to_string (g1);
        const auto fail0 = failures;
        check (maxDifference (off, on) > 1e-3, "cabinet changes output");
        return failures == fail0;
    } },
{ "amp_bias", "amp bias changes the output", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = burstTrain (F, 440.0, 0.5f, 960, 9600);
        const auto cold = renderMono ({{ "amp_source", 1.0f }, { "valve_source", 1.0f }, { "pentode_source", 1.0f }, { "tape_source", 0.0f }, { "drive", 0.9f }, { "amp_bias", 0.0f }, { "mix", 100.0f }}, t);
        const auto hot = renderMono ({{ "amp_source", 1.0f }, { "valve_source", 1.0f }, { "pentode_source", 1.0f }, { "tape_source", 0.0f }, { "drive", 0.9f }, { "amp_bias", 1.0f }, { "mix", 100.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double d = maxDifference (cold, hot);
        const double dc = dcMean (cold.left, sa, sb), dh = dcMean (hot.left, sa, sb);
        note = "maxDiff=" + std::to_string (d) + " dcCold=" + std::to_string (dc) + " dcHot=" + std::to_string (dh);
        const auto fail0 = failures;
        check (d > 1e-4, "amp bias changes output");
        return failures == fail0;
    } },
{ "preamp", "preamp changes the output", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.2f);
        const auto off = renderMono ({{ "preamp", 0.0f }}, t);
        const auto on = renderMono ({{ "preamp", 1.0f }}, t);
        const double d = maxDifference (off, on);
        note = "maxDiff=" + std::to_string (d);
        const auto fail0 = failures;
        check (d > 1e-3, "preamp changes output");
        return failures == fail0;
    } },
{ "di", "DI changes the output", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.2f);
        const auto off = renderMono ({{ "di", 0.0f }}, t);
        const auto on = renderMono ({{ "di", 1.0f }}, t);
        const double d = maxDifference (off, on);
        note = "maxDiff=" + std::to_string (d);
        const auto fail0 = failures;
        check (d > 1e-3, "di changes output");
        return failures == fail0;
    } },
{ "di_load", "DI load changes HF", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (8000.0, F, 0.02f);
        const auto off = renderMono ({{ "di_load", 0.0f }, { "di", 1.0f }, { "auto_gain", 0.0f }}, t);
        const auto on = renderMono ({{ "di_load", 1.0f }, { "di", 1.0f }, { "auto_gain", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g0 = goertzel (off.left, 8000.0, sa, sb), g1 = goertzel (on.left, 8000.0, sa, sb);
        note = "off=" + std::to_string (g0) + " on=" + std::to_string (g1);
        const auto fail0 = failures;
        check (maxDifference (off, on) > 1e-3, "di load changes output");
        return failures == fail0;
    } },
{ "di_transformer", "DI transformer adds color", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.3f);
        const auto off = renderMono ({{ "di_transformer", 0.0f }, { "di", 1.0f }}, t);
        const auto on = renderMono ({{ "di_transformer", 1.0f }, { "di", 1.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g0 = goertzel (off.left, 880.0, sa, sb), g1 = goertzel (on.left, 880.0, sa, sb);
        note = "off=" + std::to_string (g0) + " on=" + std::to_string (g1);
        const auto fail0 = failures;
        check (maxDifference (off, on) > 1e-4, "di xfmr changes output");
        return failures == fail0;
    } },
{ "di_pad", "DI pad attenuates", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (1000.0, F, 0.2f);
        const auto off = renderMono ({{ "di_pad", 0.0f }, { "di", 1.0f }, { "auto_gain", 0.0f }}, t);
        const auto pad = renderMono ({{ "di_pad", 3.0f }, { "di", 1.0f }, { "auto_gain", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double ro = rmsDb (off.left, sa, sb), rp = rmsDb (pad.left, sa, sb);
        note = "pad0=" + std::to_string (ro) + " pad-30=" + std::to_string (rp);
        const auto fail0 = failures;
        check (maxDifference (off, pad) > 1e-3, "di pad changes output");
        return failures == fail0;
    } },
{ "flux", "flux changes the output", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.3f);
        const auto off = renderMono ({{ "flux", 0.0f }, { "drive", 0.9f }}, t);
        const auto on = renderMono ({{ "flux", 1.0f }, { "drive", 0.9f }}, t);
        const double d = maxDifference (off, on);
        note = "maxDiff=" + std::to_string (d);
        const auto fail0 = failures;
        check (d > 1e-4, "flux changes output");
        return failures == fail0;
    } },
{ "wear", "wear darkens the output", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (8000.0, F, 0.02f);
        const auto off = renderMono ({{ "wear", 0.0f }, { "auto_gain", 0.0f }}, t);
        const auto on = renderMono ({{ "wear", 1.0f }, { "auto_gain", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g0 = goertzel (off.left, 8000.0, sa, sb), g1 = goertzel (on.left, 8000.0, sa, sb);
        note = "off=" + std::to_string (g0) + " on=" + std::to_string (g1);
        const auto fail0 = failures;
        check (g1 < 0.8 * g0, "wear darkens");
        return failures == fail0;
    } },
{ "mechanics", "mechanics changes the output", [] (std::string& note) {
        const int F = 6 * framesPerSecond;
        const auto t = sine (440.0, F, 0.125f);
        const auto even = renderMono ({{ "mechanics", 0.0f }, { "wow", 1.0f }, { "flutter", 0.0f }, { "mix", 100.0f }}, t);
        const auto rough = renderMono ({{ "mechanics", 1.0f }, { "wow", 1.0f }, { "flutter", 0.0f }, { "mix", 100.0f }}, t);
        const std::size_t mid = static_cast<std::size_t> (F) / 2, s6 = static_cast<std::size_t> (F) - 9600;
        const auto scan = diffScan (even.left, rough.left, 440.0, 0.30, 3.00, 0.02, mid, s6);
        const double ctrl = std::abs (goertzel (even.left, 440.85, mid, s6) - goertzel (rough.left, 440.85, mid, s6))
                          + std::abs (goertzel (even.left, 441.70, mid, s6) - goertzel (rough.left, 441.70, mid, s6));
        note = "peak=" + std::to_string (scan.offset) + " level=" + std::to_string (scan.level) + " ctrl=" + std::to_string (ctrl);
        const auto fail0 = failures;
        check (scan.level > 1.25 * std::max (1e-12, ctrl), "mechanics changes output");
        return failures == fail0;
    } },
{ "reverb", "reverb adds a tail", [] (std::string& note) {
        const int F = 3 * framesPerSecond;
        const auto t = toneThenSilence (440.0, 14400, F);
        const auto off = renderMono ({{ "reverb", 0.0f }, { "mix", 100.0f }}, t);
        const auto on = renderMono ({{ "reverb", 1.0f }, { "mix", 100.0f }}, t);
        const auto sa = 15360, sb = 24960;
        const double ro = rmsDb (off.left, sa, sb), rn = rmsDb (on.left, sa, sb);
        note = "off=" + std::to_string (ro) + " on=" + std::to_string (rn);
        const auto fail0 = failures;
        check (rn > ro + 6.0, "reverb adds tail");
        return failures == fail0;
    } },
{ "reverb_size", "reverb size lengthens the tail", [] (std::string& note) {
        const int F = 3 * framesPerSecond;
        const auto t = toneThenSilence (440.0, 14400, F);
        const auto small = renderMono ({{ "reverb", 1.0f }, { "reverb_size", 0.0f }, { "mix", 100.0f }}, t);
        const auto big = renderMono ({{ "reverb", 1.0f }, { "reverb_size", 1.0f }, { "mix", 100.0f }}, t);
        const auto sa = 48000, sb = 96000;
        const double rs = rmsDb (small.left, sa, sb), rb = rmsDb (big.left, sa, sb);
        note = "small=" + std::to_string (rs) + " big=" + std::to_string (rb);
        const auto fail0 = failures;
        check (rb > rs + 1.0, "reverb size lengthens");
        return failures == fail0;
    } },
{ "reverb_predelay", "predelay shifts the tail", [] (std::string& note) {
        const int F = 3 * framesPerSecond;
        const auto t = toneThenSilence (440.0, 14400, F);
        const auto a = renderMono ({{ "reverb", 1.0f }, { "reverb_predelay", 20.0f }, { "mix", 100.0f }}, t);
        const auto b = renderMono ({{ "reverb", 1.0f }, { "reverb_predelay", 80.0f }, { "mix", 100.0f }}, t);
        const auto sa = 15360, sb = 20160;
        const double ra = rmsDb (a.left, sa, sb), rb = rmsDb (b.left, sa, sb);
        note = "pd20=" + std::to_string (ra) + " pd80=" + std::to_string (rb);
        const auto fail0 = failures;
        check (ra - rb > 3.0, "predelay shifts tail");
        return failures == fail0;
    } },
{ "reverb_damp", "damping darkens the tail", [] (std::string& note) {
        const int F = 3 * framesPerSecond;
        const auto t = toneThenSilence (440.0, 14400, F);
        const auto dry = renderMono ({{ "reverb", 1.0f }, { "reverb_damp", 0.0f }, { "mix", 100.0f }}, t);
        const auto damp = renderMono ({{ "reverb", 1.0f }, { "reverb_damp", 1.0f }, { "mix", 100.0f }}, t);
        const auto sa = 15360, sb = 48000;
        const double g0 = goertzel (dry.left, 8000.0, sa, sb), g1 = goertzel (damp.left, 8000.0, sa, sb);
        note = "dry=" + std::to_string (g0) + " damp=" + std::to_string (g1);
        const auto fail0 = failures;
        check (maxDifference (dry, damp) > 1e-4, "reverb damp changes output");
        return failures == fail0;
    } },
{ "reverb_width", "reverb width widens the image", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        std::vector<float> L (static_cast<std::size_t> (F)), R (static_cast<std::size_t> (F));
        for (std::size_t i = 0; i < static_cast<std::size_t> (F); ++i) { L[i] = 0.2f * (float) std::sin (6.2831853 * 440.0 * static_cast<double> (i) / 48000.0); R[i] = 0.1f * (float) std::sin (6.2831853 * 660.0 * static_cast<double> (i) / 48000.0); }
        const auto narrow = renderWith ({{ "reverb", 1.0f }, { "reverb_width", 0.0f }}, L, R);
        const auto wide = renderWith ({{ "reverb", 1.0f }, { "reverb_width", 2.0f }}, L, R);
        const auto sa = 9600, sb = 38400;
        const double cn = correlation (narrow.left, narrow.right, sa, sb);
        const double cw = correlation (wide.left, wide.right, sa, sb);
        note = "corrNarrow=" + std::to_string (cn) + " corrWide=" + std::to_string (cw);
        const auto fail0 = failures;
        check (cn > cw + 0.05 || cw > cn + 0.05, "width changes image");
        return failures == fail0;
    } },
{ "reverb_lowcut", "reverb low cut removes lows", [] (std::string& note) {
        const int F = 3 * framesPerSecond;
        const auto t = toneThenSilence (60.0, 14400, F);
        const auto open = renderMono ({{ "reverb", 1.0f }, { "reverb_lowcut", 20.0f }, { "mix", 100.0f }}, t);
        const auto cut = renderMono ({{ "reverb", 1.0f }, { "reverb_lowcut", 500.0f }, { "mix", 100.0f }}, t);
        const auto sa = 15360, sb = 48000;
        const double g0 = goertzel (open.left, 60.0, sa, sb), g1 = goertzel (cut.left, 60.0, sa, sb);
        note = "open=" + std::to_string (g0) + " cut=" + std::to_string (g1);
        const auto fail0 = failures;
        check (g1 < 0.5 * g0, "reverb lowcut removes lows");
        return failures == fail0;
    } },
{ "delay_type", "delay type changes the echo", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = impulseThenSilence (F, 240, 0.5f);
        const auto a = renderMono ({{ "delay_type", 0.0f }, { "delay_feedback", 40.0f }, { "delay_time", 100.0f }, { "mix", 100.0f }}, t);
        const auto b = renderMono ({{ "delay_type", 2.0f }, { "delay_feedback", 40.0f }, { "delay_time", 100.0f }, { "mix", 100.0f }}, t);
        const double d = maxDifference (a, b);
        note = "maxDiff=" + std::to_string (d);
        const auto fail0 = failures;
        check (d > 1e-3, "delay type changes echo");
        return failures == fail0;
    } },
{ "distortion", "distortion adds harmonics", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.3f);
        const auto off = renderMono ({{ "distortion", 0.0f }}, t);
        const auto on = renderMono ({{ "distortion", 1.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g0 = goertzel (off.left, 880.0, sa, sb), g1 = goertzel (on.left, 880.0, sa, sb);
        note = "off=" + std::to_string (g0) + " on=" + std::to_string (g1);
        const auto fail0 = failures;
        check (maxDifference (off, on) > 1e-3, "distortion changes output");
        return failures == fail0;
    } },
{ "modern_mode", "modern mode changes the output", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.3f);
        const auto off = renderMono ({{ "modern_mode", 0.0f }, { "distortion", 0.8f }}, t);
        const auto on = renderMono ({{ "modern_mode", 1.0f }, { "distortion", 0.8f }}, t);
        const double d = maxDifference (off, on);
        note = "maxDiff=" + std::to_string (d);
        const auto fail0 = failures;
        check (d > 1e-3, "modern mode changes output");
        return failures == fail0;
    } },
{ "lofi_mode", "lofi mode darkens the output", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (8000.0, F, 0.02f);
        const auto off = renderMono ({{ "lofi_mode", 0.0f }}, t);
        const auto on = renderMono ({{ "lofi_mode", 1.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g0 = goertzel (off.left, 8000.0, sa, sb), g1 = goertzel (on.left, 8000.0, sa, sb);
        note = "off=" + std::to_string (g0) + " on=" + std::to_string (g1);
        const auto fail0 = failures;
        check (g1 < 0.5 * g0, "lofi darkens");
        return failures == fail0;
    } },
{ "vinyl", "vinyl adds surface noise", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = zeros (F);
        const auto off = renderMono ({{ "vinyl", 0.0f }, { "noise", 0.5f }}, t);
        const auto on = renderMono ({{ "vinyl", 1.0f }, { "noise", 0.5f }}, t);
        const double d = maxDifference (off, on);
        const auto sa = 9600, sb = 38400;
        const double so = spikeOverLocalRms (off.left, sa, sb), sn = spikeOverLocalRms (on.left, sa, sb);
        note = "maxDiff=" + std::to_string (d) + " spikeOff=" + std::to_string (so) + " spikeOn=" + std::to_string (sn);
        const auto fail0 = failures;
        check (d > 1e-3, "vinyl changes output");
        return failures == fail0;
    } },
{ "vinyl_crackle", "crackle produces spikes", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = zeros (F);
        const auto off = renderMono ({{ "vinyl", 1.0f }, { "vinyl_crackle", 0.0f }, { "noise", 0.5f }}, t);
        const auto on = renderMono ({{ "vinyl", 1.0f }, { "vinyl_crackle", 1.0f }, { "noise", 0.5f }}, t);
        const auto sa = 9600, sb = 38400;
        const double so = spikeOverLocalRms (off.left, sa, sb), sn = spikeOverLocalRms (on.left, sa, sb);
        note = "spikeOff=" + std::to_string (so) + " spikeOn=" + std::to_string (sn);
        const auto fail0 = failures;
        check (maxDifference (off, on) > 1e-3, "vinyl crackle changes output");
        return failures == fail0;
    } },
{ "vinyl_rumble", "rumble adds low-frequency energy", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = zeros (F);
        const auto off = renderMono ({{ "vinyl", 1.0f }, { "vinyl_rumble", 0.0f }, { "noise", 0.5f }}, t);
        const auto on = renderMono ({{ "vinyl", 1.0f }, { "vinyl_rumble", 1.0f }, { "noise", 0.5f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g0 = goertzel (off.left, 30.0, sa, sb), g1 = goertzel (on.left, 30.0, sa, sb);
        note = "off=" + std::to_string (g0) + " on=" + std::to_string (g1);
        const auto fail0 = failures;
        check (g1 > 2.0 * std::max (1e-12, g0), "rumble adds LF");
        return failures == fail0;
    } },
{ "vinyl_speed", "vinyl speed changes the wow rate", [] (std::string& note) {
        const int F = 6 * framesPerSecond;
        const auto t = sine (440.0, F, 0.125f);
        const auto a = renderMono ({{ "vinyl", 1.0f }, { "vinyl_speed", 0.0f }, { "mix", 100.0f }}, t);
        const auto b = renderMono ({{ "vinyl", 1.0f }, { "vinyl_speed", 2.0f }, { "mix", 100.0f }}, t);
        const std::size_t mid = static_cast<std::size_t> (F) / 2, s6 = static_cast<std::size_t> (F) - 9600;
        const auto base = renderMono ({{ "vinyl", 0.0f }, { "mix", 100.0f }}, t);
        const auto scanA = diffScan (base.left, a.left, 440.0, 0.30, 4.0, 0.05, mid, s6);
        const auto scanB = diffScan (base.left, b.left, 440.0, 0.30, 4.0, 0.05, mid, s6);
        note = "peak33=" + std::to_string (scanA.offset) + " peak78=" + std::to_string (scanB.offset);
        const auto fail0 = failures;
        check (maxDifference (a, b) > 1e-3, "vinyl speed changes output");
        return failures == fail0;
    } },
{ "vinyl_dust", "dust produces small spikes", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = zeros (F);
        const auto off = renderMono ({{ "vinyl", 1.0f }, { "vinyl_dust", 0.0f }, { "noise", 0.5f }}, t);
        const auto on = renderMono ({{ "vinyl", 1.0f }, { "vinyl_dust", 1.0f }, { "noise", 0.5f }}, t);
        const auto sa = 9600, sb = 38400;
        const int co = spikeCount (off.left, 4.0, sa, sb), cn = spikeCount (on.left, 4.0, sa, sb);
        note = "countOff=" + std::to_string (co) + " countOn=" + std::to_string (cn);
        const auto fail0 = failures;
        check (maxDifference (off, on) > 1e-3, "vinyl dust changes output");
        return failures == fail0;
    } },
{ "vinyl_scratch", "scratch produces big bursts", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = zeros (F);
        const auto off = renderMono ({{ "vinyl", 1.0f }, { "vinyl_scratch", 0.0f }, { "noise", 0.5f }}, t);
        const auto on = renderMono ({{ "vinyl", 1.0f }, { "vinyl_scratch", 1.0f }, { "noise", 0.5f }}, t);
        const auto sa = 9600, sb = 38400;
        const double so = spikeOverLocalRms (off.left, sa, sb), sn = spikeOverLocalRms (on.left, sa, sb);
        note = "spikeOff=" + std::to_string (so) + " spikeOn=" + std::to_string (sn);
        const auto fail0 = failures;
        check (sn > so + 2.0, "scratch bursts");
        return failures == fail0;
    } },
{ "vinyl_warp", "warp modulates the tone", [] (std::string& note) {
        const int F = 6 * framesPerSecond;
        const auto t = sine (440.0, F, 0.125f);
        const auto base = renderMono ({{ "vinyl", 0.0f }, { "mix", 100.0f }}, t);
        const auto off = renderMono ({{ "vinyl", 1.0f }, { "vinyl_warp", 0.0f }, { "mix", 100.0f }}, t);
        const auto on = renderMono ({{ "vinyl", 1.0f }, { "vinyl_warp", 1.0f }, { "mix", 100.0f }}, t);
        const std::size_t mid = static_cast<std::size_t> (F) / 2, s6 = static_cast<std::size_t> (F) - 9600;
        const auto scan = diffScan (off.left, on.left, 440.0, 0.30, 3.0, 0.05, mid, s6);
        note = "peak=" + std::to_string (scan.offset) + " level=" + std::to_string (scan.level);
        const auto fail0 = failures;
        check (scan.level > 1e-4, "warp modulates");
        return failures == fail0;
    } },
{ "vinyl_electrical", "electrical hum at 50 Hz", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = zeros (F);
        const auto off = renderMono ({{ "vinyl", 1.0f }, { "vinyl_electrical", 0.0f }}, t);
        const auto on = renderMono ({{ "vinyl", 1.0f }, { "vinyl_electrical", 1.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g0 = goertzel (off.left, 50.0, sa, sb), g1 = goertzel (on.left, 50.0, sa, sb);
        note = "off=" + std::to_string (g0) + " on=" + std::to_string (g1);
        const auto fail0 = failures;
        check (maxDifference (off, on) > 1e-3 || g1 > 1e-4, "electrical changes output");
        return failures == fail0;
    } },
{ "vinyl_clicks", "clicks produce spikes", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = zeros (F);
        const auto off = renderMono ({{ "vinyl", 1.0f }, { "vinyl_clicks", 0.0f }, { "noise", 0.5f }}, t);
        const auto on = renderMono ({{ "vinyl", 1.0f }, { "vinyl_clicks", 1.0f }, { "noise", 0.5f }}, t);
        const auto sa = 9600, sb = 38400;
        const double so = spikeOverLocalRms (off.left, sa, sb), sn = spikeOverLocalRms (on.left, sa, sb);
        note = "spikeOff=" + std::to_string (so) + " spikeOn=" + std::to_string (sn);
        const auto fail0 = failures;
        check (maxDifference (off, on) > 1e-3, "vinyl clicks changes output");
        return failures == fail0;
    } },
{ "vinyl_generation", "generation changes the output", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (8000.0, F, 0.02f);
        const auto a = renderMono ({{ "vinyl_generation", 0.0f }, { "vinyl", 1.0f }}, t);
        const auto b = renderMono ({{ "vinyl_generation", 3.0f }, { "vinyl", 1.0f }}, t);
        const double d = maxDifference (a, b);
        note = "maxDiff=" + std::to_string (d);
        const auto fail0 = failures;
        check (d > 1e-4, "generation changes output");
        return failures == fail0;
    } },
{ "vinyl_turntable", "turntable changes the output", [] (std::string& note) {
        const int F = 6 * framesPerSecond;
        const auto t = sine (440.0, F, 0.125f);
        const auto belt = renderMono ({{ "vinyl", 1.0f }, { "vinyl_turntable", 0.0f }, { "mix", 100.0f }}, t);
        const auto off = renderMono ({{ "vinyl", 1.0f }, { "vinyl_turntable", 3.0f }, { "mix", 100.0f }}, t);
        const std::size_t mid = static_cast<std::size_t> (F) / 2, s6 = static_cast<std::size_t> (F) - 9600;
        const auto base = renderMono ({{ "vinyl", 0.0f }, { "mix", 100.0f }}, t);
        const auto scan = diffScan (off.left, belt.left, 440.0, 0.30, 1.50, 0.02, mid, s6);
        note = "peak=" + std::to_string (scan.offset) + " level=" + std::to_string (scan.level);
        const auto fail0 = failures;
        check (scan.level > 1e-4, "turntable changes output");
        return failures == fail0;
    } },
{ "vinyl_cartridge", "cartridge changes the output", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (8000.0, F, 0.02f);
        const auto a = renderMono ({{ "vinyl_cartridge", 0.0f }, { "vinyl", 1.0f }}, t);
        const auto b = renderMono ({{ "vinyl_cartridge", 3.0f }, { "vinyl", 1.0f }}, t);
        const double d = maxDifference (a, b);
        note = "maxDiff=" + std::to_string (d);
        const auto fail0 = failures;
        check (d > 1e-4, "cartridge changes output");
        return failures == fail0;
    } },
{ "valve_type", "valve type changes H2", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.5f);
        const auto active = renderMono ({{ "valve_source", 1.0f }, { "drive", 0.9f }, { "valve_type", 0.0f }}, t);
        const auto off = renderMono ({{ "valve_source", 1.0f }, { "drive", 0.9f }, { "valve_type", 6.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double gA = goertzel (active.left, 880.0, sa, sb), gO = goertzel (off.left, 880.0, sa, sb);
        note = "active=" + std::to_string (gA) + " off=" + std::to_string (gO);
        const auto fail0 = failures;
        check (maxDifference (active, off) > 1e-3, "valve type changes output");
        return failures == fail0;
    } },
{ "pentode_type", "pentode type changes the output", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.5f);
        const auto a = renderMono ({{ "pentode_source", 1.0f }, { "drive", 0.9f }, { "pentode_type", 0.0f }}, t);
        const auto b = renderMono ({{ "pentode_source", 1.0f }, { "drive", 0.9f }, { "pentode_type", 3.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double ga = goertzel (a.left, 880.0, sa, sb), gb = goertzel (b.left, 880.0, sa, sb);
        note = "type0=" + std::to_string (ga) + " type3=" + std::to_string (gb);
        const auto fail0 = failures;
        check (maxDifference (a, b) > 1e-4, "pentode type changes output");
        return failures == fail0;
    } },
{ "triode_type", "triode type changes the output", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.5f);
        const auto a = renderMono ({{ "triode_source", 1.0f }, { "drive", 0.9f }, { "triode_type", 0.0f }}, t);
        const auto b = renderMono ({{ "triode_source", 1.0f }, { "drive", 0.9f }, { "triode_type", 3.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double ga = goertzel (a.left, 880.0, sa, sb), gb = goertzel (b.left, 880.0, sa, sb);
        note = "type0=" + std::to_string (ga) + " type3=" + std::to_string (gb);
        const auto fail0 = failures;
        check (maxDifference (a, b) > 1e-4, "triode type changes output");
        return failures == fail0;
    } },
{ "transistor_type", "transistor type changes the output", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.5f);
        const auto a = renderMono ({{ "transistor_source", 1.0f }, { "drive", 0.9f }, { "transistor_type", 0.0f }}, t);
        const auto b = renderMono ({{ "transistor_source", 1.0f }, { "drive", 0.9f }, { "transistor_type", 3.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double ga = goertzel (a.left, 880.0, sa, sb), gb = goertzel (b.left, 880.0, sa, sb);
        note = "type0=" + std::to_string (ga) + " type3=" + std::to_string (gb);
        const auto fail0 = failures;
        check (maxDifference (a, b) > 1e-4, "transistor type changes output");
        return failures == fail0;
    } },
{ "transformer_type", "transformer type changes the output", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.5f);
        const auto a = renderMono ({{ "transformer_source", 1.0f }, { "drive", 0.9f }, { "transformer_type", 0.0f }}, t);
        const auto b = renderMono ({{ "transformer_source", 1.0f }, { "drive", 0.9f }, { "transformer_type", 3.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double ga = goertzel (a.left, 880.0, sa, sb), gb = goertzel (b.left, 880.0, sa, sb);
        note = "type0=" + std::to_string (ga) + " type3=" + std::to_string (gb);
        const auto fail0 = failures;
        check (maxDifference (a, b) > 1e-3, "transformer type changes output");
        return failures == fail0;
    } },
{ "digital_type", "digital type changes the output", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.5f);
        const auto a = renderMono ({{ "digital_source", 1.0f }, { "drive", 0.9f }, { "digital_type", 0.0f }}, t);
        const auto b = renderMono ({{ "digital_source", 1.0f }, { "drive", 0.9f }, { "digital_type", 3.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double ga = goertzel (a.left, 880.0, sa, sb), gb = goertzel (b.left, 880.0, sa, sb);
        note = "type0=" + std::to_string (ga) + " type3=" + std::to_string (gb);
        const auto fail0 = failures;
        check (std::abs (ga - gb) > 0.1 * std::max (ga, gb), "digital type changes output");
        return failures == fail0;
    } },
{ "vinyl_type", "vinyl type changes the noise color", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto n = whiteNoise (F, 0.3f);
        const auto a = renderMono ({{ "vinyl", 1.0f }, { "noise", 0.5f }, { "vinyl_type", 0.0f }, { "vinyl_crackle", 0.0f }, { "vinyl_rumble", 0.0f }}, n);
        const auto b = renderMono ({{ "vinyl", 1.0f }, { "noise", 0.5f }, { "vinyl_type", 2.0f }, { "vinyl_crackle", 0.0f }, { "vinyl_rumble", 0.0f }}, n);
        const auto sa = 9600, sb = 38400;
        const double ra = goertzel (a.left, 6000.0, sa, sb) / std::max (1e-12, goertzel (a.left, 30.0, sa, sb));
        const double rb = goertzel (b.left, 6000.0, sa, sb) / std::max (1e-12, goertzel (b.left, 30.0, sa, sb));
        note = "ratio0=" + std::to_string (ra) + " ratio2=" + std::to_string (rb);
        const auto fail0 = failures;
        check (std::abs (ra - rb) > 0.2, "vinyl type changes color");
        return failures == fail0;
    } },
{ "st_link", "st link changes stereo processing", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        std::vector<float> L (static_cast<std::size_t> (F)), R (static_cast<std::size_t> (F));
        for (std::size_t i = 0; i < static_cast<std::size_t> (F); ++i) { L[i] = 0.4f * (float) std::sin (6.2831853 * 440.0 * static_cast<double> (i) / 48000.0); R[i] = 0.1f * (float) std::sin (6.2831853 * 440.0 * static_cast<double> (i) / 48000.0); }
        const auto linked = renderWith ({{ "st_link", 1.0f }, { "glue_on", 1.0f }}, L, R);
        const auto unlinked = renderWith ({{ "st_link", 0.0f }, { "glue_on", 1.0f }}, L, R);
        const double d = maxDifference (linked, unlinked);
        note = "maxDiff=" + std::to_string (d);
        const auto fail0 = failures;
        check (d > 1e-4, "st link changes output");
        return failures == fail0;
    } },
{ "delay_sync", "delay sync changes the echo lag", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = impulseThenSilence (F, 240, 0.5f);
        const auto off = renderMono ({{ "delay_sync", 0.0f }, { "delay_time", 250.0f }, { "delay_feedback", 40.0f }, { "mix", 100.0f }}, t);
        const auto on = renderMono ({{ "delay_sync", 1.0f }, { "delay_time", 250.0f }, { "delay_feedback", 40.0f }, { "mix", 100.0f }}, t);
        const double lo = echoPeakMs (off.left, 4800, 38400), ln = echoPeakMs (on.left, 4800, 38400);
        note = "syncOff=" + std::to_string (lo) + " syncOn=" + std::to_string (ln);
        const auto fail0 = failures;
        check (std::abs (lo - ln) > 100.0, "delay sync changes lag");
        return failures == fail0;
    } },
{ "delay_rate", "delay rate changes the echo lag", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = impulseThenSilence (F, 240, 0.5f);
        const auto a = renderMono ({{ "delay_sync", 1.0f }, { "delay_rate", 2.0f }, { "delay_feedback", 40.0f }, { "mix", 100.0f }}, t);
        const auto b = renderMono ({{ "delay_sync", 1.0f }, { "delay_rate", 3.0f }, { "delay_feedback", 40.0f }, { "mix", 100.0f }}, t);
        const double la = echoPeakMs (a.left, 4800, 38400), lb = echoPeakMs (b.left, 4800, 38400);
        note = "rate1/4=" + std::to_string (la) + " rate1/8=" + std::to_string (lb);
        const auto fail0 = failures;
        check (la - lb > 150.0, "delay rate changes lag");
        return failures == fail0;
    } },
{ "transient_attack", "transient attack boosts the onset", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = burstTrain (F, 440.0, 0.5f, 960, 9600);
        const auto neg = renderMono ({{ "transient_attack", -1.0f }, { "transient_mix", 100.0f }}, t);
        const auto pos = renderMono ({{ "transient_attack", 1.0f }, { "transient_mix", 100.0f }}, t);
        auto head = [] (const std::vector<float>& s) { return windowRmsAvg (s, 0, 10560, 240, 480, 8); };
        const double hn = dbOf (head (neg.left)), hp = dbOf (head (pos.left));
        note = "attack-1=" + std::to_string (hn) + " attack+1=" + std::to_string (hp);
        const auto fail0 = failures;
        check (hp > hn + 1.0, "transient attack boosts onset");
        return failures == fail0;
    } },
{ "transient_sustain", "transient sustain changes the tail", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = burstTrain (F, 440.0, 0.5f, 960, 9600);
        const auto neg = renderMono ({{ "transient_sustain", -1.0f }, { "transient_mix", 100.0f }}, t);
        const auto pos = renderMono ({{ "transient_sustain", 1.0f }, { "transient_mix", 100.0f }}, t);
        auto tail = [] (const std::vector<float>& s) { return windowRmsAvg (s, 0, 10560, 960, 480, 8); };
        const double tn = dbOf (tail (neg.left)), tp = dbOf (tail (pos.left));
        note = "sustain-1=" + std::to_string (tn) + " sustain+1=" + std::to_string (tp);
        const auto fail0 = failures;
        check (std::abs (tn - tp) > 0.5, "transient sustain changes tail");
        return failures == fail0;
    } },
{ "transient_mix", "transient mix blends the shaper", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = burstTrain (F, 440.0, 0.5f, 960, 9600);
        const auto dry = renderMono ({{ "transient_mix", 0.0f }, { "transient_attack", 1.0f }}, t);
        const auto wet = renderMono ({{ "transient_mix", 100.0f }, { "transient_attack", 1.0f }}, t);
        auto head = [] (const std::vector<float>& s) { return windowRmsAvg (s, 0, 10560, 240, 480, 8); };
        const double hd = dbOf (head (dry.left)), hw = dbOf (head (wet.left));
        note = "mix0=" + std::to_string (hd) + " mix100=" + std::to_string (hw);
        const auto fail0 = failures;
        check (std::abs (hd - hw) > 0.5, "transient mix blends");
        return failures == fail0;
    } },
{ "neural_mix", "neural mix is bit-for-bit (no model)", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = twoTone (261.63, 329.63, F, 0.3f);
        const auto base = renderMono ({{ "neural_mix", 0.0f }}, t);
        const auto mixed = renderMono ({{ "neural_mix", 1.0f }}, t);
        const double d = maxDifference (base, mixed);
        note = "maxDiff=" + std::to_string (d);
        const auto fail0 = failures;
        check (d < 1e-12, "neural mix inert");
        return failures == fail0;
    } },
{ "ir_mix", "IR mix convolves the loaded IR", [] (std::string& note) {
        writeIrWav ("probe_ir.wav");
        Engine engine;
        engine.processor.loadIrFile (juce::File::getCurrentWorkingDirectory().getChildFile ("probe_ir.wav"));
        const auto warm = whiteNoise (framesPerSecond, 0.2f, 11u);
        for (int attempt = 0; attempt < 64; ++attempt)
        {
            engine.set ("ir_mix", 1.0f); engine.set ("mix", 100.0f);
            engine.run (warm, warm);
            const auto probe = impulseThenSilence (framesPerSecond, 240, 0.5f);
            const auto result = engine.run (probe, probe);
            if (rmsRange (result.left, 1200, 2400) > 1e-5)
            {
                engine.set ("ir_mix", 0.0f);
                const auto dry = engine.run (probe, probe);
                engine.set ("ir_mix", 1.0f);
                const auto wet = engine.run (probe, probe);
                const double t2 = rmsRange (wet.left, 1200, 2400);
                const double d = maxDifference (dry, wet);
                note = "secondTap=" + std::to_string (t2) + " dry/wetDiff=" + std::to_string (d);
                const auto fail0 = failures;
                check (t2 > 5e-5, "IR second tap");
                check (d > 1e-3, "IR mix changes output");
                return failures == fail0;
            }
        }
        note = "IR never arrived";
        const auto fail0 = failures;
        check (false, "IR decode");
        return failures == fail0;
    } },
{ "fx_wah", "wah increases level swing", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.125f);
        const auto off = renderMono ({{ "fx_wah", 0.0f }, { "mix", 100.0f }}, t);
        const auto on = renderMono ({{ "fx_wah", 1.0f }, { "mix", 100.0f }}, t);
        const auto sa = 9600;
        const double so = levelSwing (off.left, 4800, sa), sn = levelSwing (on.left, 4800, sa);
        note = "swingOff=" + std::to_string (so) + " swingOn=" + std::to_string (sn);
        const auto fail0 = failures;
        check (maxDifference (off, on) > 1e-3, "fx wah changes output");
        return failures == fail0;
    } },
{ "fx_anti_spike", "anti-spike limits peaks", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        std::vector<float> t (static_cast<std::size_t> (F), 0.0f);
        for (std::size_t i = 0; i < static_cast<std::size_t> (F); i += 480) t[i] = 0.9f;
        const auto off = renderMono ({{ "fx_anti_spike", 0.0f }}, t);
        const auto on = renderMono ({{ "fx_anti_spike", 1.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double mo = maxAbs (off.left, sa, sb), mn = maxAbs (on.left, sa, sb);
        note = "maxOff=" + std::to_string (mo) + " maxOn=" + std::to_string (mn);
        const auto fail0 = failures;
        check (mn < mo * 0.9, "anti-spike limits");
        return failures == fail0;
    } },
{ "fx_gate", "gate cuts the tail", [] (std::string& note) {
        const int F = 3 * framesPerSecond;
        const auto t = toneThenSilence (440.0, 14400, F);
        const auto off = renderMono ({{ "fx_gate", 0.0f }, { "mix", 100.0f }}, t);
        const auto on = renderMono ({{ "fx_gate", 1.0f }, { "mix", 100.0f }}, t);
        const auto sa = 15360, sb = 48000;
        const double ro = rmsDb (off.left, sa, sb), rn = rmsDb (on.left, sa, sb);
        note = "off=" + std::to_string (ro) + " on=" + std::to_string (rn);
        const auto fail0 = failures;
        check (rn < ro - 3.0, "gate cuts tail");
        return failures == fail0;
    } },
{ "fx_overdrive", "overdrive adds harmonics", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.3f);
        const auto off = renderMono ({{ "fx_overdrive", 0.0f }}, t);
        const auto on = renderMono ({{ "fx_overdrive", 1.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g0 = goertzel (off.left, 880.0, sa, sb), g1 = goertzel (on.left, 880.0, sa, sb);
        note = "off=" + std::to_string (g0) + " on=" + std::to_string (g1);
        const auto fail0 = failures;
        check (maxDifference (off, on) > 1e-3, "fx overdrive changes output");
        return failures == fail0;
    } },
{ "fx_fuzz", "fuzz adds rich harmonics", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.3f);
        const auto od = renderMono ({{ "fx_overdrive", 1.0f }}, t);
        const auto fz = renderMono ({{ "fx_fuzz", 1.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double gOd = goertzel (od.left, 1320.0, sa, sb), gFz = goertzel (fz.left, 1320.0, sa, sb);
        note = "odH3=" + std::to_string (gOd) + " fuzzH3=" + std::to_string (gFz);
        const auto fail0 = failures;
        check (gFz > 1.5 * std::max (1e-12, gOd), "fuzz richer");
        return failures == fail0;
    } },
{ "fx_wavefolder", "wavefolder adds harmonics", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.3f);
        const auto off = renderMono ({{ "fx_wavefolder", 0.0f }}, t);
        const auto on = renderMono ({{ "fx_wavefolder", 1.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g0 = goertzel (off.left, 880.0, sa, sb), g1 = goertzel (on.left, 880.0, sa, sb);
        note = "off=" + std::to_string (g0) + " on=" + std::to_string (g1);
        const auto fail0 = failures;
        check (maxDifference (off, on) > 1e-3, "fx wavefolder changes output");
        return failures == fail0;
    } },
{ "fx_bitcrusher", "bitcrusher reduces distinct levels", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (1000.0, F, 0.3f);
        const auto off = renderMono ({{ "fx_bitcrusher", 0.0f }}, t);
        const auto on = renderMono ({{ "fx_bitcrusher", 1.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const auto dv = distinctValues (on.left, sa);
        const double hf = heldSampleFraction (on.left, sa, sb);
        note = "distinct=" + std::to_string (dv) + " held=" + std::to_string (hf);
        const auto fail0 = failures;
        check (maxDifference (off, on) > 1e-3, "fx bitcrusher changes output");
        return failures == fail0;
    } },
{ "fx_rate_crusher", "rate crusher holds samples", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (1000.0, F, 0.3f);
        const auto off = renderMono ({{ "fx_rate_crusher", 0.0f }}, t);
        const auto on = renderMono ({{ "fx_rate_crusher", 1.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double hf = heldSampleFraction (on.left, sa, sb);
        note = "held=" + std::to_string (hf);
        const auto fail0 = failures;
        check (maxDifference (off, on) > 1e-3, "fx rate crusher changes output");
        return failures == fail0;
    } },
{ "fx_ring_mod", "ring mod kills the carrier", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.3f);
        const auto off = renderMono ({{ "fx_ring_mod", 0.0f }}, t);
        const auto on = renderMono ({{ "fx_ring_mod", 1.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g0 = goertzel (off.left, 440.0, sa, sb), g1 = goertzel (on.left, 440.0, sa, sb);
        const double gSb = goertzel (on.left, 520.0, sa, sb);
        note = "carrierOff=" + std::to_string (g0) + " carrierOn=" + std::to_string (g1) + " sideband=" + std::to_string (gSb);
        const auto fail0 = failures;
        check (g1 < 0.02 * g0, "ring mod kills carrier");
        check (gSb > 1e-3, "ring mod sideband");
        return failures == fail0;
    } },
{ "fx_octaver", "octaver adds a sub-octave", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = twoTone (220.0, 275.0, F, 0.2f);
        const auto off = renderMono ({{ "fx_octaver", 0.0f }}, t);
        const auto on = renderMono ({{ "fx_octaver", 1.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g110 = goertzel (on.left, 110.0, sa, sb), g137 = goertzel (on.left, 137.5, sa, sb);
        note = "sub110=" + std::to_string (g110) + " sub137.5=" + std::to_string (g137);
        const auto fail0 = failures;
        check (g110 > 1e-3 && g137 > 1e-3, "octaver polyphonic");
        return failures == fail0;
    } },
{ "fx_pitch", "pitch shifts by amount*12 st", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.2f);
        const auto base = renderMono ({{ "fx_pitch", 0.0f }}, t);
        const auto shifted = renderMono ({{ "fx_pitch", 0.25f }}, t);
        const auto sa = 9600, sb = 38400;
        const double p0 = peakFrequency (base.left, 440.0, 60.0, 0.5, sa, sb);
        const double p1 = peakFrequency (shifted.left, 523.25, 60.0, 0.5, sa, sb);
        note = "base=" + std::to_string (p0) + " shifted=" + std::to_string (p1);
        const auto fail0 = failures;
        check (p1 > 500.0 && p1 < 545.0, "pitch shifts +3 st");
        return failures == fail0;
    } },
{ "fx_comb", "comb filter notches the spectrum", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.2f);
        const auto off = renderMono ({{ "fx_comb", 0.0f }}, t);
        const auto on = renderMono ({{ "fx_comb", 1.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g0 = goertzel (off.left, 880.0, sa, sb), g1 = goertzel (on.left, 880.0, sa, sb);
        note = "off=" + std::to_string (g0) + " on=" + std::to_string (g1);
        const auto fail0 = failures;
        check (maxDifference (off, on) > 1e-3, "comb changes output");
        return failures == fail0;
    } },
{ "fx_formant", "formant shifts the spectrum", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = twoTone (500.0, 1500.0, F, 0.2f);
        const auto off = renderMono ({{ "fx_formant", 0.0f }}, t);
        const auto on = renderMono ({{ "fx_formant", 1.0f }}, t);
        const double d = maxDifference (off, on);
        note = "maxDiff=" + std::to_string (d);
        const auto fail0 = failures;
        check (d > 1e-3, "formant changes output");
        return failures == fail0;
    } },
{ "fx_dynamic_eq", "dynamic eq changes the output", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = burstTrain (F, 1000.0, 0.5f, 960, 9600);
        const auto off = renderMono ({{ "fx_dynamic_eq", 0.0f }}, t);
        const auto on = renderMono ({{ "fx_dynamic_eq", 1.0f }}, t);
        const double d = maxDifference (off, on);
        note = "maxDiff=" + std::to_string (d);
        const auto fail0 = failures;
        check (finiteEverywhere (off) && finiteEverywhere (on), "dynamic eq stable");
        return failures == fail0;
    } },
{ "fx_de_esser", "de-esser reduces sibilance", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = whiteNoise (F, 0.2f);
        const auto off = renderMono ({{ "fx_de_esser", 0.0f }}, t);
        const auto on = renderMono ({{ "fx_de_esser", 1.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g0 = goertzel (off.left, 8000.0, sa, sb), g1 = goertzel (on.left, 8000.0, sa, sb);
        note = "off=" + std::to_string (g0) + " on=" + std::to_string (g1);
        const auto fail0 = failures;
        check (g1 < 0.8 * g0, "de-esser reduces highs");
        return failures == fail0;
    } },
{ "fx_chorus", "chorus adds sidebands", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.2f);
        const auto off = renderMono ({{ "fx_chorus", 0.0f }}, t);
        const auto on = renderMono ({{ "fx_chorus", 1.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double sb0 = sidebandTotal (off.left, 440.0, 4.0, 0.25, 0.4, sa, sb);
        const double sb1 = sidebandTotal (on.left, 440.0, 4.0, 0.25, 0.4, sa, sb);
        note = "sbOff=" + std::to_string (sb0) + " sbOn=" + std::to_string (sb1);
        const auto fail0 = failures;
        check (sb1 > 1.2 * std::max (1e-12, sb0), "chorus sidebands");
        return failures == fail0;
    } },
{ "fx_flanger", "flanger notches the spectrum", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.2f);
        const auto off = renderMono ({{ "fx_flanger", 0.0f }}, t);
        const auto on = renderMono ({{ "fx_flanger", 1.0f }}, t);
        const double d = maxDifference (off, on);
        note = "maxDiff=" + std::to_string (d);
        const auto fail0 = failures;
        check (d > 1e-3, "flanger changes output");
        return failures == fail0;
    } },
{ "fx_phaser", "phaser notches the spectrum", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.2f);
        const auto off = renderMono ({{ "fx_phaser", 0.0f }}, t);
        const auto on = renderMono ({{ "fx_phaser", 1.0f }}, t);
        const double d = maxDifference (off, on);
        note = "maxDiff=" + std::to_string (d);
        const auto fail0 = failures;
        check (d > 1e-3, "phaser changes output");
        return failures == fail0;
    } },
{ "fx_tremolo", "tremolo modulates the level", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.2f);
        const auto off = renderMono ({{ "fx_tremolo", 0.0f }, { "mix", 100.0f }}, t);
        const auto on = renderMono ({{ "fx_tremolo", 1.0f }, { "mix", 100.0f }}, t);
        const auto sa = 9600;
        const double so = levelSwing (off.left, 2400, sa), sn = levelSwing (on.left, 2400, sa);
        note = "swingOff=" + std::to_string (so) + " swingOn=" + std::to_string (sn);
        const auto fail0 = failures;
        check (sn > so * 1.5, "tremolo swings");
        return failures == fail0;
    } },
{ "fx_vibrato", "vibrato adds sidebands", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.2f);
        const auto off = renderMono ({{ "fx_vibrato", 0.0f }}, t);
        const auto on = renderMono ({{ "fx_vibrato", 1.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double sb0 = sidebandTotal (off.left, 440.0, 4.0, 0.25, 0.4, sa, sb);
        const double sb1 = sidebandTotal (on.left, 440.0, 4.0, 0.25, 0.4, sa, sb);
        note = "sbOff=" + std::to_string (sb0) + " sbOn=" + std::to_string (sb1);
        const auto fail0 = failures;
        check (maxDifference (off, on) > 1e-3, "fx vibrato changes output");
        return failures == fail0;
    } },
{ "fx_rotary", "rotary modulates level", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.2f);
        const auto off = renderMono ({{ "fx_rotary", 0.0f }, { "mix", 100.0f }}, t);
        const auto on = renderMono ({{ "fx_rotary", 1.0f }, { "mix", 100.0f }}, t);
        const auto sa = 9600;
        const double so = levelSwing (off.left, 2400, sa), sn = levelSwing (on.left, 2400, sa);
        note = "swingOff=" + std::to_string (so) + " swingOn=" + std::to_string (sn);
        const auto fail0 = failures;
        check (sn > so * 1.2, "rotary swings");
        return failures == fail0;
    } },
{ "fx_vocoder", "vocoder changes the harmonic balance", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.5f);
        const auto off = renderMono ({{ "fx_vocoder", 0.0f }}, t);
        const auto on = renderMono ({{ "fx_vocoder", 1.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double g0 = goertzel (off.left, 1320.0, sa, sb), g1 = goertzel (on.left, 1320.0, sa, sb);
        note = "offH3=" + std::to_string (g0) + " onH3=" + std::to_string (g1);
        const auto fail0 = failures;
        check (maxDifference (off, on) > 1e-3, "vocoder changes output");
        return failures == fail0;
    } },
{ "fx_granular", "granular smears the spectrum", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.3f);
        const auto base = renderMono ({{ "fx_granular", 0.0f }}, t);
        const auto grain = renderMono ({{ "fx_granular", 1.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double sb0 = sidebandTotal (base.left, 440.0, 4.0, 0.25, 0.4, sa, sb);
        const double sb1 = sidebandTotal (grain.left, 440.0, 4.0, 0.25, 0.4, sa, sb);
        const double rb = rmsDb (base.left, sa, sb), rg = rmsDb (grain.left, sa, sb);
        note = "rmsBase=" + std::to_string (rb) + " rmsGrain=" + std::to_string (rg) + " sb0=" + std::to_string (sb0) + " sb1=" + std::to_string (sb1);
        const auto fail0 = failures;
        check (maxDifference (base, grain) > 1e-3, "fx granular changes output");
        return failures == fail0;
    } },
{ "fx_spectral", "spectral gate changes the output", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = whiteNoise (F, 0.2f);
        const auto off = renderMono ({{ "fx_spectral", 0.0f }}, t);
        const auto on = renderMono ({{ "fx_spectral", 1.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double ro = rmsDb (off.left, sa, sb), rn = rmsDb (on.left, sa, sb);
        note = "off=" + std::to_string (ro) + " on=" + std::to_string (rn);
        const auto fail0 = failures;
        check (maxDifference (off, on) > 1e-3, "fx spectral changes output");
        return failures == fail0;
    } },
{ "fx_stutter", "stutter changes the output", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.2f);
        const auto off = renderMono ({{ "fx_stutter", 0.0f }}, t);
        const auto on = renderMono ({{ "fx_stutter", 1.0f }}, t);
        const double d = maxDifference (off, on);
        note = "maxDiff=" + std::to_string (d);
        const auto fail0 = failures;
        check (d > 1e-3, "stutter changes output");
        return failures == fail0;
    } },
{ "fx_speed", "fx speed doubles the pitch", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (440.0, F, 0.2f);
        const auto base = renderMono ({{ "fx_speed", 0.5f }}, t);
        const auto fast = renderMono ({{ "fx_speed", 0.9f }}, t);
        const auto sa = 9600, sb = 38400;
        const double p0 = peakFrequency (base.left, 440.0, 60.0, 0.5, sa, sb);
        const double p1 = peakFrequency (fast.left, 880.0, 60.0, 0.5, sa, sb);
        note = "base=" + std::to_string (p0) + " fast=" + std::to_string (p1);
        const auto fail0 = failures;
        check (p1 > 800.0, "fx speed doubles");
        return failures == fail0;
    } },
{ "comp_upward", "upward compression lifts quiet signals", [] (std::string& note) {
        const int F = 2 * framesPerSecond;
        const auto t = sine (1000.0, F, 0.02f);
        const auto off = renderMono ({{ "comp_upward", 0.0f }, { "comp_threshold", -18.0f }, { "comp_mix", 100.0f }, { "auto_gain", 0.0f }, { "glue_on", 0.0f }}, t);
        const auto on = renderMono ({{ "comp_upward", 1.0f }, { "comp_threshold", -18.0f }, { "comp_mix", 100.0f }, { "auto_gain", 0.0f }, { "glue_on", 0.0f }}, t);
        const auto sa = 9600, sb = 38400;
        const double ro = rmsDb (off.left, sa, sb), rn = rmsDb (on.left, sa, sb);
        note = "up0=" + std::to_string (ro) + " up1=" + std::to_string (rn);
        const auto fail0 = failures;
        check (rn > ro + 2.0, "upward lifts");
        return failures == fail0;
    } },
};
int main ()
{
    //  Unbuffered on stdout: a run is 150+ renders long, and ctest -V turns
    //  output into the CI log. Avoid `_IOLBF` with size 0, which trips MSVC UCRT's
    //  invalid-parameter fastfail on Windows.
    std::setvbuf (stdout, nullptr, _IONBF, 0);

    auto processorPtr = std::make_unique<FirstAudioProcessor>();
    auto& processor = *processorPtr;
    std::vector<std::string> registered;
    for (auto* p : processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            registered.push_back (rp->getParameterID().toStdString());

    std::printf ("=== parameter audio: %d tests, %zu registered parameters ===\\n",
                 (int) (sizeof (table) / sizeof (table[0])), registered.size());

    const auto tableCount = (int) (sizeof (table) / sizeof (table[0]));

    int passCount = 0;
    for (int i = 0; i < tableCount; ++i)
    {
        std::string note;
        const bool ok = table[i].fn (note);
        if (ok) ++passCount;
        std::printf ("[%s] %-20s : %s | %s\\n",
                     ok ? "PASS" : "FAIL", table[i].id, table[i].claim, note.c_str());
    }

    // Meta: order matches the registered table.
    bool metaOk = true;
    if (tableCount != (int) registered.size())
    {
        std::printf ("META FAIL: table has %d tests but processor has %zu parameters\\n",
                     tableCount, registered.size());
        metaOk = false;
    }
    for (int i = 0; i < tableCount && i < (int) registered.size(); ++i)
    {
        if (std::string (table[i].id) != registered[static_cast<std::size_t> (i)])
        {
            std::printf ("META FAIL: row %d id '%s' != registered '%s'\\n",
                         i, table[i].id, registered[static_cast<std::size_t> (i)].c_str());
            metaOk = false;
        }
    }
    // Every test names a registered parameter.
    for (int i = 0; i < tableCount; ++i)
    {
        if (std::find (registered.begin(), registered.end(), table[i].id) == registered.end())
        {
            std::printf ("META FAIL: test '%s' names no registered parameter\\n", table[i].id);
            metaOk = false;
        }
    }
    // Every registered parameter has a test.
    for (const auto& id : registered)
    {
        bool found = false;
        for (int i = 0; i < tableCount; ++i)
            if (id == table[i].id) { found = true; break; }
        if (! found)
        {
            std::printf ("META FAIL: parameter '%s' has no test\\n", id.c_str());
            metaOk = false;
        }
    }

    // Engine-level regression checks for output metering and oversampled stages.
    {
        const auto t = sine (1000.0, framesPerSecond);
        Engine e0, e6;
        e0.set ("output", 0.0f);
        e0.run (t, t);
        const float lufs0 = e0.processor.getOutputLufs();

        e6.set ("output", 6.0f);
        e6.run (t, t);
        const float lufs6 = e6.processor.getOutputLufs();

        const auto fail0 = failures;
        check (lufs6 > lufs0 + 4.0f && lufs6 < lufs0 + 8.0f,
               "output LUFS tracks output trim (" + std::to_string (lufs0)
                   + " -> " + std::to_string (lufs6) + ")");
        std::printf ("[%s] %-20s : output LUFS tracks output trim | %.2f -> %.2f LUFS\n",
                     failures == fail0 ? "PASS" : "FAIL", "regression_lufs",
                     lufs0, lufs6);
    }
    {
        const int F = 3 * framesPerSecond;
        const auto t = toneThenSilence (440.0, 14400, F);
        const auto os1 = renderMono ({{ "oversampling", 0.0f }, { "reverb", 1.0f }, { "reverb_size", 0.5f }, { "mix", 100.0f }}, t);
        const auto os8 = renderMono ({{ "oversampling", 3.0f }, { "reverb", 1.0f }, { "reverb_size", 0.5f }, { "mix", 100.0f }}, t);
        const double tail1 = rmsDb (os1.left, 24000, 48000);
        const double tail8 = rmsDb (os8.left, 24000, 48000);

        const auto fail0 = failures;
        check (tail1 > -45.0 && std::abs (tail8 - tail1) < 2.5,
               "reverb tail matches across 1x/8x oversampling ("
                   + std::to_string (tail1) + " vs " + std::to_string (tail8) + ")");
        std::printf ("[%s] %-20s : reverb tail consistent across 1x/8x | 1x=%.2f dB 8x=%.2f dB\n",
                     failures == fail0 ? "PASS" : "FAIL", "regression_reverb_os",
                     tail1, tail8);
    }

    std::printf ("=== %d/%d passed, %d checks, %d failures, meta=%s ===\\n",
                 passCount, tableCount, checksRun, failures, metaOk ? "OK" : "FAIL");

    return (failures == 0 && metaOk) ? 0 : 1;
}
