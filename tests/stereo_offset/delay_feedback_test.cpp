#include "StereoDelayFeedback.h"

#include <array>
#include <cmath>
#include <cstdio>

namespace {
bool near(float actual, float expected) noexcept { return std::abs(actual - expected) < 1.0e-6f; }

bool runRouting(float pingPong, bool expectCrossFeedback) {
    std::array<float, 2> delayLines { 0.25f, -0.4f };
    std::array<float, 2> feedbackWrites {};

    // Match the shipping channel-loop order: L routes into R before R's own
    // sample is committed, then both accumulated feedback writes are applied.
    StereoDelayFeedback::accumulate(feedbackWrites, 0, 2, 0.5f, pingPong, true);
    StereoDelayFeedback::accumulate(feedbackWrites, 1, 2, 0.2f, pingPong, true);
    delayLines[0] += feedbackWrites[0];
    delayLines[1] += feedbackWrites[1];

    return near(delayLines[0], 0.25f + 0.5f * (1.0f - pingPong) + 0.2f * pingPong) &&
           near(delayLines[1], -0.4f + 0.2f * (1.0f - pingPong) + 0.5f * pingPong) &&
           (expectCrossFeedback
                ? (!near(delayLines[0], 0.25f + 0.5f * (1.0f - pingPong)) &&
                   !near(delayLines[1], -0.4f + 0.2f * (1.0f - pingPong)))
                : (near(delayLines[0], 0.25f + 0.5f) && near(delayLines[1], -0.4f + 0.2f)));
}
} // namespace

int main() {
    if (!runRouting(0.0f, false)) {
        std::fprintf(stderr, "normal delay feedback routing failed\n");
        return 1;
    }

    if (!runRouting(1.0f, true)) {
        std::fprintf(stderr, "ping-pong did not route both channels symmetrically\n");
        return 1;
    }

    std::array<float, 2> monoWrites{};
    StereoDelayFeedback::accumulate(monoWrites, 0, 1, 0.4f, 1.0f, true);
    if (!near(monoWrites[0], 0.4f)) {
        std::fprintf(stderr, "mono delay did not retain full local feedback\n");
        return 1;
    }

    std::array<float, 2> disabledWrites{};
    StereoDelayFeedback::accumulate(disabledWrites, 0, 2, 0.4f, 1.0f, false);
    StereoDelayFeedback::accumulate(disabledWrites, 1, 2, 0.8f, 1.0f, false);
    if (!near(disabledWrites[0], 0.0f) || !near(disabledWrites[1], 0.0f)) {
        std::fprintf(stderr, "disabled delay wrote feedback into either channel\n");
        return 1;
    }

    std::puts("Stereo delay feedback: local, ping-pong, mono and disabled routing pass");
    return 0;
}
