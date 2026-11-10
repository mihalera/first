// =============================================================================
//  Stereo delay feedback routing, accumulated once per frame.
// =============================================================================
#pragma once

#include <array>
#include <cstddef>

struct StereoDelayFeedback {
    static void accumulate(std::array<float, 2> &feedbackWrites,
                           int channel,
                           int channels,
                           float feedback,
                           float pingPong,
                           bool enabled) noexcept {
        if (!enabled)
            return;

        const auto index = static_cast<std::size_t>(channel);
        const bool stereo = channels > 1;
        feedbackWrites[index] += feedback * (stereo ? (1.0f - pingPong) : 1.0f);
        if (stereo)
            feedbackWrites[static_cast<std::size_t>(1 - channel)] += feedback * pingPong;
    }
};
