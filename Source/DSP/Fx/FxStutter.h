// =============================================================================
//  The stutter: the transport catching, holding the phrase it just played and
//  repeating it before the platter gets going again.
//
//  WHAT IT IS. A tape deck's transport does not only run at one speed - a reel
//  that slips, a capstan that is nudged, a stopped platter that is released
//  again all repeat a fraction of a second of programme. This stage is that
//  behaviour made repeatable: it LISTENS for a slice of the signal, then plays
//  that slice back to the listener a few times, then listens again.
//
//  It listens and it repeats, in that order, and the two halves are the same
//  length: one cycle is `repeats + 1` slices long, so the effect is a rhythm
//  rather than a gate. The amount sets both halves at once - higher is a
//  shorter slice and more repeats, which is the difference between a slow reel
//  hiccup and a machine-gun hold - because a single "how much stutter"
//  control has to move the two together to stay musical.
//
//  NO CLICKS. The slice has a 4 ms raised-cosine window at each end, and the
//  output is a crossfade between the live signal and the windowed repeat
//  scaled by the amount. At window 0 or amount 0 the stage is bit-exactly the
//  input, so an idle knob cannot colour the signal.
//
//  NO ALLOCATION ON THE AUDIO THREAD. Both the ring and the captured slice are
//  sized in prepare(), and the capture is a copy into the slice buffer that
//  already exists.
//
//  Driven by tests/dsp/stutter_test.cpp, which includes this header directly.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace j37dsp
{

class FxStutter
{
public:
    void prepare (double sampleRate)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;

        // The ring is the longest slice the stage can capture, so a shorter
        // slice is a shorter read of the same buffer rather than a reallocation.
        ringLength = std::max (16, static_cast<int> (std::lround (rate * 0.30)));
        ringL.assign (static_cast<std::size_t> (ringLength), 0.0f);
        ringR.assign (static_cast<std::size_t> (ringLength), 0.0f);
        sliceL.assign (static_cast<std::size_t> (ringLength), 0.0f);
        sliceR.assign (static_cast<std::size_t> (ringLength), 0.0f);

        // The window's own length. 4 ms is long enough to hide a seam and short
        // enough that it is not heard as a fade at the slice rate.
        windowLength = std::max (2, static_cast<int> (std::lround (rate * 0.004)));

        reset();
    }

    void reset()
    {
        std::fill (ringL.begin(), ringL.end(), 0.0f);
        std::fill (ringR.begin(), ringR.end(), 0.0f);
        std::fill (sliceL.begin(), sliceL.end(), 0.0f);
        std::fill (sliceR.begin(), sliceR.end(), 0.0f);
        writePosition = 0;
        sliceLength = ringLength / 2;
        replayPosition = 0;
        repetitionsLeft = 0;
        listening = true;
        listenRemaining = sliceLength;
    }

    /** One frame.

        `amount` is 0..1. Zero is a passthrough, and that is exact rather than
        approximate: the stage only starts a cycle when the amount is above the
        rack's threshold, and the mix below is written so that a zero amount
        leaves the input untouched.
    */
    void process (float& left, float& right, float amount) noexcept
    {
        amount = juce::jlimit (0.0f, 1.0f, amount);

        // Higher amount = a shorter slice and more passes over it. 100 ms...240 ms
        // covers a machine-gun hold at the top and a slow reel hiccup at the
        // bottom, which is the whole useful range of the effect.
        const auto wantedSlice = static_cast<int> (
            std::lround (rate * static_cast<double> (0.24f - 0.14f * amount)));
        sliceLength = juce::jlimit (windowLength * 4, ringLength, wantedSlice);
        const auto repeats = 1 + static_cast<int> (std::lround (amount * 3.0f));

        // The live signal is always recorded, so the slice a cycle captures is
        // the programme as it was played, not the repeats from the last cycle.
        ringL[static_cast<std::size_t> (writePosition)] = left;
        ringR[static_cast<std::size_t> (writePosition)] = right;
        writePosition = (writePosition + 1) % ringLength;

        if (listening)
        {
            left = mixRepeat (left, left, 0.0f, amount);
            right = mixRepeat (right, right, 0.0f, amount);

            if (--listenRemaining <= 0)
            {
                captureSlice();
                listening = false;
                replayPosition = 0;
                repetitionsLeft = repeats;
            }
            return;
        }

        // -- replaying -------------------------------------------------------
        const auto window = sliceWindow (replayPosition);
        const auto loopLeft = sliceL[static_cast<std::size_t> (replayPosition)];
        const auto loopRight = sliceR[static_cast<std::size_t> (replayPosition)];
        const auto stutteredLeft = mixRepeat (left, loopLeft, window, amount);
        const auto stutteredRight = mixRepeat (right, loopRight, window, amount);

        // The repeated signal is what leaves the stage while a repeat is under
        // way; the live signal keeps running into the ring above either way, so
        // the next slice is already the programme the listener heard.
        left = stutteredLeft;
        right = stutteredRight;

        if (++replayPosition >= sliceLength)
        {
            replayPosition = 0;
            if (--repetitionsLeft <= 0)
            {
                listening = true;
                listenRemaining = sliceLength;
            }
        }
    }

    /** The slice length this amount is currently using, in samples. The harness
        checks the cycle against it rather than against a copy of the formula. */
    int currentSliceLength() const noexcept { return sliceLength; }

private:
    /** The crossfade from the live signal into the windowed repeat, and then
        the amount's own blend. Written as two mixes so that window 0 leaves the
        live signal exactly as it arrived. */
    static float mixRepeat (float live, float loop, float window, float amount) noexcept
    {
        const auto wet = live + (loop - live) * window;
        return live + (wet - live) * amount;
    }

    /** Raised-cosine in, raised-cosine out, flat in between. */
    float sliceWindow (int position) const noexcept
    {
        const auto fadeIn = static_cast<float> (position + 1)
                          / static_cast<float> (windowLength);
        const auto fadeOut = static_cast<float> (sliceLength - position)
                           / static_cast<float> (windowLength);
        const auto ramp = juce::jlimit (0.0f, 1.0f, std::min (fadeIn, fadeOut));
        return 0.5f - 0.5f * std::cos (ramp * juce::MathConstants<float>::pi);
    }

    /** Copies the slice that just ended out of the ring, oldest first. It is a
        short copy of memory that is already there - the only work a cycle does
        off the sample path, and small enough to sit inside the block budget at
        the highest slice rate. */
    void captureSlice() noexcept
    {
        const auto first = (writePosition - sliceLength + ringLength) % ringLength;
        for (int i = 0; i < sliceLength; ++i)
        {
            const auto from = static_cast<std::size_t> ((first + i) % ringLength);
            sliceL[static_cast<std::size_t> (i)] = ringL[from];
            sliceR[static_cast<std::size_t> (i)] = ringR[from];
        }
    }

    double rate = 48000.0;
    int ringLength = 0;
    int windowLength = 0;
    int sliceLength = 0;
    int writePosition = 0;
    int replayPosition = 0;
    int repetitionsLeft = 0;
    int listenRemaining = 0;
    bool listening = true;

    // Reserved in prepare() to the longest slice, so the capture never grows.
    std::vector<float> ringL, ringR;
    std::vector<float> sliceL, sliceR;
};

} // namespace j37dsp
