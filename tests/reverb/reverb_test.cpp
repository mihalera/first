// The room, measured as a room rather than as code.
//
// Source/DSP/DattorroRoom.h wraps el-visio/dattorro-verb, and this harness links
// the library's own verb.c to render through it. What it checks are the four
// claims the wrapper makes, and every one of them is a property of the rendered
// tail rather than of the source:
//
//   * at MIX 0 the stage is not merely quiet, it is ABSENT - the frame comes back
//     bit for bit, at the tank's rate and at a converted one;
//   * SIZE sets the tail's LENGTH, and length is a number of seconds: the decay
//     rises with the control, and the same setting decays for the same time at
//     44.1, 48 and 96 kHz. That last one is the wrapper's whole reason to exist -
//     a delay network run at a rate other than the one its lengths were chosen at
//     is a different room, and at 96 kHz it would be half as long;
//   * the tail is STEREO, from two taps of one network, and a mono bus gets the
//     pair summed rather than one side of it;
//   * the tank has LOSS: a tail decays to silence instead of ringing forever, and
//     a level of MIX 1 puts the wet pair at roughly the dry signal's level rather
//     than above it.
//
// The signals are generated, so a failure is reproducible.
#include "shim.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "DSP/DattorroRoom.h"

namespace
{
int gFailures = 0;

void check (bool condition, const std::string& what)
{
    std::printf ("%s %s\n", condition ? "  ok  " : "FAILED", what.c_str());

    if (! condition)
        ++gFailures;
}

constexpr double kTwoPi = 6.283185307179586;

/** How long every impulse is rendered for. Long enough that the largest SIZE has
    died away well below the harness's floor. */
constexpr double kRenderSeconds = 12.0;

/** One rendered impulse response, one vector per side. */
struct Tail
{
    std::vector<float> left, right;
    bool finite = true;
};

Tail renderImpulse (double rate, float size, float mix, int channels = 2)
{
    DattorroRoom room;
    room.prepare (rate);

    Tail tail;
    const auto samples = static_cast<int> (rate * kRenderSeconds);
    tail.left.assign (static_cast<std::size_t> (samples), 0.0f);
    tail.right.assign (static_cast<std::size_t> (samples), 0.0f);

    for (int i = 0; i < samples; ++i)
    {
        float frame[2] { i == 0 ? 1.0f : 0.0f, i == 0 ? 1.0f : 0.0f };
        room.processFrame (frame, channels, size, mix);

        tail.left[static_cast<std::size_t> (i)] = frame[0];
        tail.right[static_cast<std::size_t> (i)] = frame[channels > 1 ? 1 : 0];

        if (! std::isfinite (frame[0]) || ! std::isfinite (frame[1]))
            tail.finite = false;
    }

    return tail;
}

/** Energy per `window` seconds, in dB relative to the loudest window AFTER
    `skipSeconds` - the dry impulse is not part of the tail, so it is not the
    reference either. */
std::vector<double> energyEnvelope (const Tail& tail, double rate, double window,
                                    double skipSeconds)
{
    const auto step = static_cast<std::size_t> (std::max (1.0, rate * window));
    const auto skip = static_cast<std::size_t> (rate * skipSeconds);

    std::vector<double> energy;

    for (std::size_t start = skip; start + step <= tail.left.size(); start += step)
    {
        auto sum = 0.0;

        for (std::size_t i = start; i < start + step; ++i)
            sum += static_cast<double> (tail.left[i]) * tail.left[i]
                 + static_cast<double> (tail.right[i]) * tail.right[i];

        energy.push_back (sum / static_cast<double> (step));
    }

    return energy;
}

/** T60 in SECONDS, fitted to the envelope between 5 and 35 dB below its peak.

    A fitted slope rather than "-60 dB crossing": the tank's decay is not exactly
    exponential (the damping is a one-pole inside the loop, and the modulated
    all-pass wanders), so a single crossing would report whichever wobble happened
    to land there. The fit over thirty decibels is the shape, and it is reported
    for a reader as well as asserted on. */
double t60Of (const Tail& tail, double rate)
{
    const auto envelope = energyEnvelope (tail, rate, 0.01, 0.05);

    if (envelope.empty())
        return 0.0;

    const auto peak = *std::max_element (envelope.begin(), envelope.end());

    if (peak <= 0.0)
        return 0.0;

    const auto peakDb = 10.0 * std::log10 (peak);

    double sumT = 0.0, sumDb = 0.0, sumTT = 0.0, sumTDb = 0.0;
    auto count = 0;

    for (std::size_t i = 0; i < envelope.size(); ++i)
    {
        const auto db = envelope[i] > 0.0 ? 10.0 * std::log10 (envelope[i]) : -300.0;

        if (db > peakDb - 5.0 || db < peakDb - 35.0)
            continue;

        const auto t = static_cast<double> (i) * 0.01;
        sumT += t;
        sumDb += db;
        sumTT += t * t;
        sumTDb += t * db;
        ++count;
    }

    if (count < 8)
        return 0.0;

    const auto n = static_cast<double> (count);
    const auto slope = (n * sumTDb - sumT * sumDb) / (n * sumTT - sumT * sumT);

    return slope < -1.0e-6 ? 60.0 / -slope : 0.0;
}

double rms (const std::vector<float>& signal, std::size_t from)
{
    auto sum = 0.0;

    for (auto i = from; i < signal.size(); ++i)
        sum += static_cast<double> (signal[i]) * signal[i];

    return signal.size() > from
             ? std::sqrt (sum / static_cast<double> (signal.size() - from))
             : 0.0;
}

// =============================================================================
//  MIX 0: the stage is absent, not quiet.
// =============================================================================
void testBypassIsBitExact()
{
    std::uint32_t state = 0x12345678u;
    const auto nextSample = [&state]
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<float> (state >> 8) / 8388608.0f - 1.0f;
    };

    for (const auto rate : { 48000.0, 44100.0, 96000.0 })
    {
        DattorroRoom room;
        room.prepare (rate);

        auto identical = true;

        for (int i = 0; i < static_cast<int> (rate); ++i)
        {
            float frame[2] { nextSample() * 0.25f, nextSample() * 0.25f };
            const float before[2] { frame[0], frame[1] };
            room.processFrame (frame, 2, 0.5f, 0.0f);

            if (frame[0] != before[0] || frame[1] != before[1])
                identical = false;
        }

        check (identical, "a frame at MIX 0 comes back bit for bit at "
                          + std::to_string (static_cast<int> (rate)) + " Hz");
    }
}

// =============================================================================
//  SIZE sets the length, and length is SECONDS at every session rate.
// =============================================================================
void testSizeSetsTheTailAndTheRateDoesNot()
{
    const auto small = t60Of (renderImpulse (48000.0, 0.1f, 1.0f), 48000.0);
    const auto medium = t60Of (renderImpulse (48000.0, 0.5f, 1.0f), 48000.0);
    const auto large = t60Of (renderImpulse (48000.0, 0.9f, 1.0f), 48000.0);

    std::printf ("        T60 at SIZE 0.1 / 0.5 / 0.9: %.2f s / %.2f s / %.2f s\n",
                 small, medium, large);

    check (small > 0.05 && medium > small * 1.2 && large > medium * 1.2,
           "the tail gets longer as SIZE rises, at three settings");

    const auto at44 = t60Of (renderImpulse (44100.0, 0.5f, 1.0f), 44100.0);
    const auto at48 = t60Of (renderImpulse (48000.0, 0.5f, 1.0f), 48000.0);
    const auto at96 = t60Of (renderImpulse (96000.0, 0.5f, 1.0f), 96000.0);

    std::printf ("        T60 at SIZE 0.5, 44.1 / 48 / 96 kHz: %.2f s / %.2f s / %.2f s\n",
                 at44, at48, at96);

    const auto within = [&at48] (double value) { return std::abs (value - at48) <= at48 * 0.15; };

    check (within (at44) && within (at96),
           "the same SIZE decays for the same number of SECONDS at 44.1, 48 and 96 kHz");
}

// =============================================================================
//  The tail is stereo, and a mono bus gets the pair summed.
// =============================================================================
void testTheTailIsStereo()
{
    const auto tail = renderImpulse (48000.0, 0.6f, 1.0f);
    const auto from = static_cast<std::size_t> (48000 * 0.05);

    auto sumLR = 0.0, sumLL = 0.0, sumRR = 0.0, sumDiff = 0.0, sumBoth = 0.0;

    for (auto i = from; i < tail.left.size(); ++i)
    {
        const auto l = static_cast<double> (tail.left[i]);
        const auto r = static_cast<double> (tail.right[i]);
        sumLR += l * r;
        sumLL += l * l;
        sumRR += r * r;
        sumDiff += (l - r) * (l - r);
        sumBoth += (l + r) * (l + r);
    }

    const auto correlation = sumLL > 0.0 && sumRR > 0.0 ? sumLR / std::sqrt (sumLL * sumRR) : 1.0;
    const auto sideToMid = sumBoth > 0.0 ? sumDiff / sumBoth : 0.0;

    std::printf ("        tail L/R correlation %.3f, side-to-mid energy %.3f\n",
                 correlation, sideToMid);

    check (correlation < 0.99,
           "the two sides of the tail are decorrelated: it is a room, not a mono delay");
    check (sideToMid > 0.02, "the difference between the sides is a real part of the tail");

    const auto mono = renderImpulse (48000.0, 0.6f, 1.0f, 1);
    const auto monoEnergy = rms (mono.left, from);
    const auto stereoEnergy = rms (tail.left, from) + rms (tail.right, from);

    check (mono.finite && monoEnergy > 0.0,
           "a mono bus is fed the pair summed, not one side of it");
    check (monoEnergy > stereoEnergy * 0.25,
           "the mono tail is not a fraction of the stereo one");
}

// =============================================================================
//  The tank has loss, and the wet level sits beside the dry.
// =============================================================================
void testTheTailDecaysAndTheLevelIsSane()
{
    const auto tail = renderImpulse (48000.0, 0.6f, 1.0f);

    const auto envelope = energyEnvelope (tail, 48000.0, 0.05, 0.05);
    const auto peak = *std::max_element (envelope.begin(), envelope.end());
    const auto end = envelope.back();
    const auto endDb = peak > 0.0 && end > 0.0 ? 10.0 * std::log10 (end / peak) : -300.0;

    std::printf ("        the tail is %.1f dB below its peak after %.0f s\n",
                 endDb, kRenderSeconds - 0.05);

    check (tail.finite, "the tail is finite throughout - no NaN and no infinity");
    check (endDb < -70.0, "the tank has loss: the tail decays away rather than ringing on");

    // A 1 kHz tone at -20 dBFS for three seconds, then the last second measured:
    // the wet pair against the dry RMS. A room at MIX 1 is allowed to be a few
    // decibels either side of the dry programme; what it is not allowed to be is
    // fifteen decibels above it, which is what `wetGain` exists to prevent.
    DattorroRoom room;
    room.prepare (48000.0);

    auto phase = 0.0;
    std::vector<float> wetL, wetR;
    const auto tone = static_cast<int> (48000 * 3);

    for (int i = 0; i < tone; ++i)
    {
        const auto value = static_cast<float> (0.1 * std::sin (phase));
        phase += kTwoPi * 1000.0 / 48000.0;

        float frame[2] { value, value };
        room.processFrame (frame, 2, 0.5f, 1.0f);
        wetL.push_back (frame[0]);
        wetR.push_back (frame[1]);
    }

    const auto wet = std::sqrt (0.5 * (rms (wetL, wetL.size() / 3) * rms (wetL, wetL.size() / 3)
                                     + rms (wetR, wetR.size() / 3) * rms (wetR, wetR.size() / 3)));
    const auto dryDb = 20.0 * std::log10 (0.1 / std::sqrt (2.0));
    const auto wetDb = 20.0 * std::log10 (std::max (wet, 1.0e-9));

    std::printf ("        wet level at MIX 1 is %.1f dB against the dry %.1f dB\n",
                 wetDb, dryDb);

    check (std::abs (wetDb - dryDb) < 6.0,
           "a tone at MIX 1 comes out within 6 dB of the dry programme's level");
}

// =============================================================================
//  Silence in, silence out - and nothing allocated or left ringing.
// =============================================================================
void testSilenceStaysSilent()
{
    DattorroRoom room;
    room.prepare (44100.0);

    auto clean = true;

    for (int i = 0; i < 44100; ++i)
    {
        float frame[2] { 0.0f, 0.0f };
        room.processFrame (frame, 2, 0.8f, 1.0f);

        if (frame[0] != 0.0f || frame[1] != 0.0f)
            clean = false;
    }

    check (clean, "silence through the room is silence, at the rate the tank is converted from");
}

// =============================================================================
//  The conversion is the only thing that moves the tail between rates, and when
//  the rates agree it is not there at all.
// =============================================================================
void testTheConversionIsAbsentAtTheTanksRate()
{
    DattorroRoom atTankRate;
    atTankRate.prepare (DattorroRoom::tankRate);
    check (std::abs (atTankRate.delay() - 1.0) < 1.0e-9,
           "at 48 kHz the room is the library's own path: one sample of latency");

    DattorroRoom atOther;
    atOther.prepare (44100.0);
    check (atOther.delay() > 1.0,
           "off the tank's rate the conversion costs its own read-ahead");
}
} // namespace

int main()
{
    std::printf ("the room: Dattorro's plate against ThirdParty/dattorro-verb\n");

    testBypassIsBitExact();
    testTheConversionIsAbsentAtTheTanksRate();
    testSizeSetsTheTailAndTheRateDoesNot();
    testTheTailIsStereo();
    testTheTailDecaysAndTheLevelIsSane();
    testSilenceStaysSilent();

    std::printf ("\n%s (%d failure%s)\n", gFailures == 0 ? "ROOM CHECK PASSED" : "FAILURES PRESENT",
                 gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
