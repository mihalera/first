#include "LoFiHoldClock.h"

#include <array>
#include <cstddef>
#include <cstdio>

namespace {
struct LofiHold {
    std::array<float, 2> held{};
    LofiHoldClock clock;
    static constexpr int holdFrames = 4;

    void processFrame(const std::array<float, 2> &frame) noexcept {
        if (clock.beginFrame(holdFrames))
            held = frame;
    }
};

bool sameFrame(const std::array<float, 2> &actual, const std::array<float, 2> &expected) noexcept {
    return actual[0] == expected[0] && actual[1] == expected[1];
}
} // namespace

int main() {
    LofiHold hold;
    constexpr std::array<std::array<float, 2>, 9> input{{{{1.0f, 10.0f}},
                                                         {{2.0f, 20.0f}},
                                                         {{3.0f, 30.0f}},
                                                         {{4.0f, 40.0f}},
                                                         {{5.0f, 50.0f}},
                                                         {{6.0f, 60.0f}},
                                                         {{7.0f, 70.0f}},
                                                         {{8.0f, 80.0f}},
                                                         {{9.0f, 90.0f}}}};
    constexpr std::array<std::array<float, 2>, 9> expected{{{{1.0f, 10.0f}},
                                                            {{1.0f, 10.0f}},
                                                            {{1.0f, 10.0f}},
                                                            {{1.0f, 10.0f}},
                                                            {{5.0f, 50.0f}},
                                                            {{5.0f, 50.0f}},
                                                            {{5.0f, 50.0f}},
                                                            {{5.0f, 50.0f}},
                                                            {{9.0f, 90.0f}}}};

    for (std::size_t frame = 0; frame < input.size(); ++frame) {
        hold.processFrame(input[frame]);
        if (!sameFrame(hold.held, expected[frame])) {
            std::fprintf(
                stderr, "LO-FI sample hold diverged between stereo channels at frame %zu\n", frame);
            return 1;
        }
    }

    std::puts("LO-FI sample hold captures synchronized stereo frames");
    return 0;
}
