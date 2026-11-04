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

    // ------------------------------------------------------------------
    //  The frame clock's two knobs, decided by the same question.
    //
    //  HOW THE SWAP INTERVAL RELATES TO THE DISPLAY'S SYNC TECHNOLOGY.
    //
    //  v-sync: interval 1 makes the buffer swap wait for the display's own
    //  refresh, which is what removes tearing; 0 presents immediately and tears.
    //
    //  Adaptive sync (NVIDIA G-Sync, AMD FreeSync, VESA Adaptive-Sync) needs the
    //  same answer as v-sync and nothing extra from the application: the panel
    //  follows the rate the application presents at while that rate is inside the
    //  panel's range, so a drop from 60 to 45 fps is a smoother 45 rather than a
    //  hard fall to 30. That only works while presents are display-locked - the
    //  driver has to see a paced swap to vary the refresh around - and it is
    //  defeated outright by interval 2 and above, which locks the swap to a
    //  multiple of the refresh and puts the panel back on a fixed ladder. So the
    //  interval this panel asks for is 1 whenever anything moves and never a
    //  larger number.
    //
    //  A RESTING PANEL PRESENTS NOTHING. When the machine is still and the
    //  invalidation throttles to every third tick, an interval of 1 would block
    //  the message thread on a swap of a frame that has not changed - the thread
    //  that also serves the host's editor messages. At rest the interval is 0 and
    //  no swap is issued at all, so an idle panel costs neither CPU nor a
    //  display refresh, and adaptive sync has nothing to follow.
    //
    //  The decision lives here, in a header with no JUCE in it, because it is the
    //  same decision for the paint rate and for the swap: split across the two
    //  call sites they could disagree, and a 20 Hz panel locked to a 60 Hz swap is
    //  the worst of both.
    // ------------------------------------------------------------------
    constexpr int swapIntervalFor (bool animating) noexcept
    {
        return animating ? 1 : 0;
    }

    constexpr int paintIntervalTicksFor (bool animating) noexcept
    {
        return animating ? activeAnimationPaintIntervalTicks : idleAnimationPaintIntervalTicks;
    }

    /** Whether anything the panel draws is still moving: the transport is
        engaged, a spindown is held, a START waits for the host, or the machine's
        activity and drift have not settled. The thresholds are the ones the
        paint decision uses, so the swap interval and the paint rate cannot
        disagree about whether the panel is busy. */
    constexpr bool panelIsAnimating (bool transportEngaged, bool spindownHeld,
                                     bool waitingForHost, float activity,
                                     float drift) noexcept
    {
        return transportEngaged || spindownHeld || waitingForHost
            || activity > 0.02f || drift > 0.02f;
    }
=======
    inline constexpr int desktopAnimationPaintIntervalTicks = 2;
    inline constexpr int androidAnimationPaintIntervalTicks = 1;
}
