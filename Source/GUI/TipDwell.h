// =============================================================================
//  How long the pointer has to rest on a control before its tooltip is allowed.
//
//  A tooltip is only useful if it waits: the panel is a field of eighty-odd
//  controls, and a hint that appears the instant the pointer crosses one is a
//  flicker that hides what the user is looking at. juce::TooltipWindow has that
//  delay (setMillisecondsBeforeTipAppears), and it applies it the first time.
//
//  It does NOT apply it the second time. its timerCallback() has two paths, and
//  the second one is the one that reads a tip off a control:
//
//      if (isVisible() || now < lastHideTime + 500)
//      {
//          // if a tip is currently visible (or has just disappeared), update
//          // to a new one immediately if needed
//          ...
//          else if (tipChanged) showTip();
//      }
//      else
//      {
//          // ... only let it appear after a timeout
//      }
//
//  So while a tip is on screen - or within half a second of one disappearing -
//  moving onto the NEXT control shows that control's tip at once. On this panel
//  that is exactly the reported behaviour: hold on a switch until its hint
//  appears, move to the one beside it, and the second hint is there instantly
//  instead of after the same third of a second.
//
//  This type is the missing rule, kept apart from JUCE so it can be measured
//  without a window (tests/tooltip): the clock starts when the component under
//  the pointer CHANGES, and nothing may be shown until the same dwell has
//  passed. Source/GUI/DwellTooltipWindow.h is the ten lines that put it in
//  front of juce::TooltipWindow.
//
//  Deliberate consequences, each pinned by the harness:
//    * every change restarts the clock, including the first one - one rule, not
//      two, so the first hint and the tenth take the same time;
//    * staying on the same control does not restart anything, so a hint does not
//      blink out because the pointer moved a pixel;
//    * leaving the panel entirely counts as a change too, and the hint that
//      follows is still a fresh one.
//
//  One thing this rule does not remove, because it is the window's clock rather
//  than the rule's: juce::TooltipWindow polls every 123 ms, so a hint appears at
//  the first poll after the dwell - up to a tenth of a second late, never early.
//  The first hint of a session can take one poll longer than the ones after it,
//  because the base class then measures its own (zero) delay from that same poll.
//  Every hint after the first is the same distance from the pointer landing, which
//  is the property that was missing.
// =============================================================================
#pragma once

namespace j37panel
{

struct TipDwell
{
    /** The third of a second the panel has always quoted, and the one the
        editor sets on the window as well. */
    static constexpr int dwellMs = 350;

    /** What the pointer is resting on now. A `const void*` rather than a JUCE
        type: this header is compiled by a harness that has no JUCE, and the
        window only ever compares it with the address it was handed. */
    const void* control = nullptr;

    /** The earliest time, on the window's own millisecond clock, at which a tip
        for `control` may be returned. */
    long long allowedAtMs = 0;

    /** The component under the pointer changed - or this is the first call.

        Returns true when the change happened, which is also when the clock was
        restarted; false leaves the existing clock alone, so calling it with the
        same control on every timer tick is cheap and meaningless.
    */
    bool hoverChanged (const void* next, long long nowMs) noexcept
    {
        if (next == control)
            return false;

        control = next;
        allowedAtMs = nowMs + dwellMs;
        return true;
    }

    /** May the tip for the current control be shown at `nowMs`? */
    bool allowed (long long nowMs) const noexcept
    {
        return nowMs >= allowedAtMs;
    }

    /** Back to the state of a window that has never been asked. */
    void reset() noexcept
    {
        control = nullptr;
        allowedAtMs = 0;
    }
};

} // namespace j37panel
