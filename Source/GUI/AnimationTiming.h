#pragma once

namespace j37panel
{
    inline constexpr int editorTimerHz = 60;

    // The panel's paint rate is chosen per frame rather than per platform: a
    // machine that is running, or a panel whose activity is still breathing,
    // is repainted on every tick, and a panel at rest on every third one. The
    // old pair was one constant for desktop (30 Hz, too coarse while the reels
    // turn) and one for Android (60 Hz, which repainted a completely still
    // phone sixty times a second and is part of why it could not hold 30 fps).
    inline constexpr int activeAnimationPaintIntervalTicks = 1;
    inline constexpr int idleAnimationPaintIntervalTicks = 3;
}
