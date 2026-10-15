// =============================================================================
//  The realtime granular pitch shifter the autotune stage moves the audio with.
//
//  Driven on signal by tests/pitch/pitch_test.cpp, which includes this header
//  rather than a copy cut out of PluginProcessor.h, and instantiated by
//  tests/dsp/check_headers_juce.sh against real JUCE. The conventions of
//  Source/DSP/ are stated in J37Math.h.
// =============================================================================
#pragma once

#include <array>
#include <cmath>
#include <cstddef>

//==============================================================================
/**
    Realtime granular pitch shifter used by the optional autotune stage.

    Two read heads over a delay line, each with its own raised-cosine window, half
    a window apart, so exactly one of them is at full window at any moment and
    their windows sum to the input. Each head advances at the pitch ratio and
    restarts - SPLICES - when its own window reaches zero, which is the one moment
    a head contributes nothing and can therefore jump to a new position unheard.

    Where it restarts is this shifter's whole quality, and the earlier version got
    both halves of it wrong:

      * it applied the restart correction to BOTH heads at once, so the head that
        was at FULL window took a step the size of the correction - a click, once
        per grain, on every note; and

      * it restarted at a fixed offset from wherever the head happened to be, so a
        pitched signal came back as two segments that were not phase-aligned. That
        is the granular flutter of the old stage, and it is why a corrector built
        on it could not be tuned by ear.

    The restart here is PHASE-LOCKED: a head comes back at a delay that differs
    from the OTHER head's by a whole number of periods of the material (when the
    caller knows the period), so the half of the crossfade that is fading out and
    the half that is fading in are the same waveform. The delay is also kept far
    enough behind the write head to survive the head's own window at whatever
    ratio it is reading at, so a splice can never run off the end of the line.
*/
struct RealtimePitchShifter
{
    static constexpr int bufferSize = 8192;          // 170 ms at 48 kHz
    static constexpr int fallbackPeriod = 512;       // no pitch known: the old grain
    static constexpr int minPeriod = 48;
    static constexpr int maxPeriod = 1024;
    static constexpr float ratioSmoothing = 0.0025f;

    std::array<float, bufferSize> buffer {};
    int writePosition = 0;
    std::array<float, 2> tapPosition {};
    std::array<float, 2> tapPhase { 0.0f, 0.5f };
    int period = fallbackPeriod;
    float ratio = 1.0f;

    void reset() noexcept
    {
        buffer.fill (0.0f);
        writePosition = 0;
        tapPosition = { 0.0f, 0.0f };
        tapPhase = { 0.0f, 0.5f };
        period = fallbackPeriod;
        ratio = 1.0f;
    }

    /** One frame through the shifter.

        `targetRatio` is the pitch to read at (1 is the input's own pitch),
        `periodHint` the period of the material in frames - 0 when the caller has
        no reading, in which case the fallback grain is used.
    */
    float process (float input, float targetRatio, int periodHint) noexcept
    {
        buffer[static_cast<std::size_t> (writePosition)] = input;
        writePosition = (writePosition + 1) % bufferSize;

        period = juce::jlimit (minPeriod, maxPeriod,
                               periodHint > 0 ? periodHint : fallbackPeriod);
        ratio += (juce::jlimit (0.5f, 2.0f, targetRatio) - ratio) * ratioSmoothing;

        const auto read = [&] (float position) noexcept
        {
            while (position < 0.0f) position += static_cast<float> (bufferSize);
            while (position >= static_cast<float> (bufferSize)) position -= static_cast<float> (bufferSize);

            const auto index = static_cast<int> (position);
            const auto next = (index + 1) % bufferSize;
            const auto fraction = position - static_cast<float> (index);

            return buffer[static_cast<std::size_t> (index)]
                 + (buffer[static_cast<std::size_t> (next)]
                    - buffer[static_cast<std::size_t> (index)]) * fraction;
        };

        const auto window = [] (float phase) noexcept
        { return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * phase); };

        auto output = read (tapPosition[0]) * window (tapPhase[0])
                    + read (tapPosition[1]) * window (tapPhase[1]);

        // One clock drives both heads. They sit half a grain apart, so their
        // windows are the two halves of a single Hann and advance by the same
        // increment; two independent accumulators would round apart over a
        // session and leave a ripple where the halves stop summing to one, so
        // the second phase is DERIVED from the first rather than accumulated.
        auto closing = -1;

        tapPhase[0] += 1.0f / static_cast<float> (period);

        if (tapPhase[0] >= 1.0f)
        {
            tapPhase[0] -= 1.0f;
            closing = 0;
        }

        tapPhase[1] = tapPhase[0] + 0.5f;

        if (tapPhase[1] >= 1.0f)
        {
            tapPhase[1] -= 1.0f;
            closing = 1;
        }

        // Both heads move BEFORE the one that just closed its window is put
        // back. That order is the whole point: restartPosition() measures the
        // head it aligns to through delayOf(), and a head that has not advanced
        // yet this frame reads one frame further back than the head doing the
        // measuring - so the pair crept forward by a frame on every grain. At
        // 220 grains a second that is a few cents of permanent detune and a
        // grain-rate wobble on top of it, which is exactly what "autotune does
        // nothing / sounds broken" was.
        for (int tap = 0; tap < 2; ++tap)
        {
            const auto index = static_cast<std::size_t> (tap);
            tapPosition[index] += ratio;

            if (tapPosition[index] >= static_cast<float> (bufferSize))
                tapPosition[index] -= static_cast<float> (bufferSize);
        }

        if (closing >= 0)
            tapPosition[static_cast<std::size_t> (closing)] = restartPosition (1 - closing);

        return output;
    }

    /** How far behind the write head a given head is reading, in frames. Public
        because the harnesses measure the shifter's own delay through it. */
    float delayOf (int tap) const noexcept
    {
        auto delay = static_cast<float> (writePosition) - tapPosition[static_cast<std::size_t> (tap)];

        if (delay < 0.0f)
            delay += static_cast<float> (bufferSize);

        return delay;
    }

private:
    /** Where the head that just closed its window comes back - see the note on
        the struct for both halves of the rule. */
    float restartPosition (int other) const noexcept
    {
        const auto otherDelay = delayOf (other);

        // Room for this head's own window at the ratio it is reading at. The head
        // advances `ratio` frames per frame while the write head advances one, so
        // its delay SHORTENS by (ratio - 1) per frame and a whole grain takes
        // that much off the delay it came in with: it has to start a period plus
        // that saving behind the write head to still be behind it - and not
        // skating along the sample just written - when the grain ends. One period
        // is also the floor at unity, which is the whole of this stage's latency
        // when nothing is being corrected.
        const auto runway = static_cast<float> (period) * juce::jmax (1.0f, ratio);

        // Phase-locked to the OTHER head: the same delay plus a whole number of
        // periods. For a periodic signal those two positions are the same
        // waveform, which is what makes the crossfade a crossfade rather than a
        // splice - so the number of periods is free to choose, and it is chosen
        // to put the head in [runway, runway + one period).
        //
        // The fold matters as much as the lift. A head reading faster than the
        // write head (a ratio above one) catches up with it and has to be moved
        // back; one reading slower FALLS BEHIND a little more every grain, and
        // without folding that down again the stage would creep towards a whole
        // line of latency and sit there - a monitor delay nobody asked for, and
        // the second half of "autotune does something strange". Either way the
        // result is inside half the ring, so no second clamp is needed: the
        // period is at most maxPeriod and the runway at most two of them.
        const auto span = static_cast<float> (period);
        const auto delay = otherDelay + span * std::ceil ((runway - otherDelay) / span);

        auto position = static_cast<float> (writePosition) - delay;

        if (position < 0.0f)
            position += static_cast<float> (bufferSize);

        return position;
    }
};
