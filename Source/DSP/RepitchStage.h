// =============================================================================
//  The repitch stage: what the machine made, played back at the platter's speed.
//
//  Driven by tests/transport/transport_test.cpp, which includes this header rather
//  than a copy cut out of PluginProcessor.h, and instantiated by
//  tests/dsp/check_headers_juce.sh against real JUCE. The conventions of
//  Source/DSP/ are stated in J37Math.h.
// =============================================================================
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

//==============================================================================
/**
    The repitch stage: what the machine made, played back at the platter's speed.

    A deck whose transport slows does not merely get quieter - the programme on
    the tape comes out LOWER, because the tape crosses the playback head more
    slowly. Until this stage existed the plugin could not do that at all: the
    transport scaled the machine's level, its modulation and its speed response
    and then stopped the sound, so the gestures the panel's tips describe (the
    pitch going first on a spindown, a start lifting back up to speed) were
    unanswerable - the signal was never being READ at the platter's speed, it was
    being MULTIPLIED by it, and a multiplier cannot bend a pitch.

    So this is a delay line with two heads. The WRITE head records the machine's
    finished frame at the session rate, and the READ head plays the buffer back
    at the platter's own speed. At speed the two heads move together - the read
    standing exactly one frame behind the write, which is the sample it has just
    seen recorded - so the stage is the identity, with no latency to report and
    no pitch of its own. While the platter slows, the read head falls behind the
    write head, and that falling-behind IS the pitch. Read at 0.5 and the frame
    comes out an octave down, read at 0.25 and it is two octaves down - which is
    where a spindown coasts to, since its 1.6 s run-down walks the read head down
    through the octaves exactly the way a hand on a platter does.

    The stage is deliberately bounded, and the bounds are the design:

      * minLag, 2 frames - the floor the servo pulls to, and the whole of this
        stage's added latency. It sits just above the one frame the write head's
        own advance puts between the heads, because a lag is latency added to a
        live input and this one is meant to be nothing at all.

      * below the gate, the read EATS the slack. The machine's own gate is
        `platter^2.5`, so at a quarter speed it is 0.031 - 30 dB down and still
        falling - and below that the machine is gone by any reading. There the
        read stops being a playback head and becomes the machine putting itself
        back together: it runs AHEAD of the platter and consumes the delay a
        coast left, because nobody can hear it happen. The gain is scaled by how
        far below the threshold the platter is, so the pitch glides into it
        instead of stepping, and the whole glide sits at the bottom of a fade
        that is already 30 dB down when the glide starts.

      * a servo of up to +0.15 (2.4 semitones) - WITH THE PLATTER BACK AT SPEED,
        and only there: what a gesture spent while it was audible is slid back
        with a small spearhead of extra speed rather than left as latency for the
        rest of the session. The pull scales with the slack and starts at zero
        with it, so it has no edge to hear and no dead band to park in, and it
        never touches the bend: while the machine is between the two regimes the
        read is the platter's speed and nothing else, which is the entire effect.
        The cost of that fidelity is the tail: a spin-up spends about a third of
        a second of slack, and the servo needs a few seconds to take it back at a
        rate nobody would call a pitch.

      * the hard cap, 1.0 s - a slack no gesture can justify stops growing: past
        the cap the read tracks the write and the material that would have
        queued up is let go. The gate is at zero there, so what is let go is
        silence.

    The stage has no gain of its own, no gate and no dry/wet - the engine's
    platter gate is already the machine's level and already sits upstream of it,
    so STOP still fades as it did and this stage only bends what the fade is
    fading. It runs on the frame the wet/dry crossfade produced, so a MIX 0 dry
    leg is repitched with the machine's own signal, exactly as the transport's
    gate treats the two legs.
*/
struct RepitchStage
{
    // The gestures, in seconds, and the servo that keeps the slack from
    // becoming latency. See the block comment above for what each one buys.
    static constexpr float minLagSamples    = 2.0f;
    static constexpr float bufferSeconds    = 1.5f;
    static constexpr float silentPlatter    = 0.25f;
    static constexpr float consumeRate      = 0.50f;
    static constexpr float pullUpSeconds    = 0.10f;
    static constexpr float hardCapSeconds   = 1.0f;
    static constexpr float maxPullUp        = 0.15f;
    static constexpr float ratePoleSeconds  = 0.015f;

    /** The read head's own clock, in frames per frame. 1 at rest, 0 with the
        platter stopped, and never above 1 + maxPullUp. */
    float rate = 1.0f;

    /** Prepares the ring for the engine's rate. Called from prepareToPlay, so
        nothing here allocates on the audio thread. */
    void prepare (double sampleRateIn)
    {
        const auto sampleRate = sampleRateIn > 1.0 ? sampleRateIn : 48000.0;
        const auto wanted = static_cast<int> (sampleRate * bufferSeconds);

        if (wanted != length)
        {
            length = juce::jmax (64, wanted);
            left.assign (static_cast<std::size_t> (length), 0.0f);
            right.assign (static_cast<std::size_t> (length), 0.0f);
        }

        pullUpSpanSamples  = static_cast<float> (sampleRate) * pullUpSeconds;
        hardCapSamples     = static_cast<float> (sampleRate) * hardCapSeconds;
        rateCoefficient    = 1.0f - std::exp (-1.0f / (static_cast<float> (sampleRate) * ratePoleSeconds));

        reset();
    }

    /** Drops the programme and puts both heads at the same place. */
    void reset() noexcept
    {
        std::fill (left.begin(), left.end(), 0.0f);
        std::fill (right.begin(), right.end(), 0.0f);
        writePosition = 0;
        readPosition = 0.0f;
        rate = 1.0f;
    }

    /** One frame of the machine's output, played back at `speed` - the platter,
        the one number the whole transport follows. Reads and writes in place. */
    void processFrame (std::array<float, 2>& frame, int channels, float speed) noexcept
    {
        if (length <= 1)
            return;

        // Record the frame under the write head, then advance it. The lag below
        // is measured from the NEW position, so a read that has not moved has
        // fallen a frame further behind.
        left[static_cast<std::size_t> (writePosition)] = frame[0];
        if (channels > 1)
            right[static_cast<std::size_t> (writePosition)] = frame[1];

        writePosition = (writePosition + 1) % length;

        // How far the read head trails the write head, in frames. Both positions
        // sit in [0, length), so one wrap correction is all this needs.
        const auto writeAsFloat = static_cast<float> (writePosition);
        auto lag = writeAsFloat - readPosition;
        if (lag < 0.0f)
            lag += static_cast<float> (length);

        // What the read head turns at, in the three regimes the platter moves
        // through - and the order of these branches is the design:
        //
        //   * AT SPEED - the platter is 1, so the pitch is the platter's and the
        //     only question left is whether a gesture left the read behind:
        //     past its budget, the servo slides it home.
        //   * BENDING - the platter's own speed and NOTHING added to it. This is
        //     the effect itself: the pitch the user hears is the number the
        //     transport follows, exactly, for the whole of the gesture.
        //   * BELOW THE GATE - silent, so the read may eat the slack instead of
        //     reading at the platter's speed. It is the one moment a delay can
        //     be spent without anyone hearing it, and spending it there is what
        //     keeps a stop from costing the rest of the session a quarter of a
        //     second.
        //
        // The third regime's gain is scaled by how far below the threshold the
        // platter is, so the pitch glides into it rather than stepping - and the
        // glide lives under a 46 dB fade.
        auto target = speed;

        if (speed < silentPlatter)
        {
            const auto depth = juce::jlimit (0.0f, 1.0f,
                                             (silentPlatter - speed) / silentPlatter);
            target = 1.0f + consumeRate * depth * depth;
        }
        else if (speed >= 0.999f)
        {
            const auto surplus = lag - minLagSamples;

            if (surplus > 0.0f)
            {
                const auto pull = juce::jmin (1.0f, surplus / pullUpSpanSamples);
                target = juce::jmax (speed, 1.0f + maxPullUp * pull);
            }
        }

        if (lag > hardCapSamples)
            target = juce::jmax (target, 1.0f);

        // One pole on the read rate, so the platter's own linear ramps arrive as
        // a glide in the pitch rather than as steps - and so the servo cannot
        // switch on with an edge of its own.
        rate += (target - rate) * rateCoefficient;
        rate = juce::jlimit (0.0f, 1.0f + maxPullUp, rate);

        // PLAY FIRST, THEN MOVE. A head reads the tape under it and only then
        // crosses to the next sample, which is also what makes the stage the
        // identity at speed: the read stands where the write stood one frame ago.
        const auto index = static_cast<int> (readPosition);
        const auto next = (index + 1) % length;
        const auto fraction = readPosition - static_cast<float> (index);

        const auto headLeft = left[static_cast<std::size_t> (index)];
        frame[0] = headLeft + (left[static_cast<std::size_t> (next)] - headLeft) * fraction;

        if (channels > 1)
        {
            const auto headRight = right[static_cast<std::size_t> (index)];
            frame[1] = headRight + (right[static_cast<std::size_t> (next)] - headRight) * fraction;
        }

        readPosition += rate;

        // A tape cannot play what has not been recorded yet: a read that has
        // crossed the write head (the wrapped lag goes past half the ring) is
        // held exactly on it instead.
        auto lagAfter = writeAsFloat - readPosition;
        if (lagAfter < 0.0f)
            lagAfter += static_cast<float> (length);

        if (lagAfter > static_cast<float> (length) * 0.5f)
            readPosition = writeAsFloat;

        if (readPosition >= static_cast<float> (length))
            readPosition -= static_cast<float> (length);
    }

    /** What the read head trails the write head by, in frames - the stage's own
        latency. Read by the harnesses (and by nothing in the audio path). */
    float slackFrames() const noexcept
    {
        if (length <= 0)
            return 0.0f;

        auto lag = static_cast<float> (writePosition) - readPosition;
        if (lag < 0.0f)
            lag += static_cast<float> (length);
        return lag;
    }

private:
    std::vector<float> left;
    std::vector<float> right;
    int length = 0;
    int writePosition = 0;
    float readPosition = 0.0f;

    float pullUpSpanSamples = 0.0f;
    float hardCapSamples = 0.0f;
    float rateCoefficient = 0.0f;
};
