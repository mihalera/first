// =============================================================================
//  Frame-synchronized clock for the LO-FI sample-and-hold stage.
// =============================================================================
#pragma once
#include <algorithm>

struct LofiHoldClock {
    bool beginFrame(int holdFrames) noexcept {
        if (--countdown > 0)
            return false;

        countdown = std::max(1, holdFrames);
        return true;
    }

    void reset() noexcept { countdown = 0; }

  private:
    int countdown = 0;
};
