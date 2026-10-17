// The tooltip dwell, checked without a window, a pointer or a JUCE.
//
// j37panel::TipDwell (Source/GUI/TipDwell.h) is the rule that decides WHEN a
// hint may be shown. Everything about it is a function of two numbers - which
// control the pointer is on, and what the clock says - so the rule can be run
// here directly, at exact millisecond boundaries, on a machine with no display.
//
// What it checks is the thing that was reported: holding on a switch until its
// hint appears and then moving to the switch beside it must wait the same third
// of a second again. juce::TooltipWindow does not do that on its own - it shows
// the next tip immediately while one is on screen (see the note at the top of
// TipDwell.h) - so the whole point of the type is the second change, not the
// first, and that is what most of the checks below are about.
//
// It also checks the two things a naive "restart on every tick" would break:
// staying on one control must not blink the hint out, and hovering off the panel
// must still be a change when the pointer comes back.
#include <cstdio>

#include "GUI/TipDwell.h"

int failures = 0;

void check (bool ok, const char* what)
{
    if (! ok)
    {
        std::printf ("  FAIL %s\n", what);
        ++failures;
    }
}

int main()
{
    using j37panel::TipDwell;

    const int dwell = TipDwell::dwellMs;

    std::printf ("tooltip dwell: %d ms from the moment the pointer lands on a control\n", dwell);

    // -------------------------------------------------------------------------
    //  The first hint: one dwell from the moment the pointer arrives, no matter
    //  what the window was doing before it.
    // -------------------------------------------------------------------------
    {
        int a = 0, b = 0;
        TipDwell t;

        check (t.hoverChanged (&a, 1000), "landing on a control restarts the clock");
        check (! t.allowed (1000), "nothing is shown the instant the pointer lands");
        check (! t.allowed (1000 + dwell - 1), "the hint is withheld one millisecond short of the dwell");
        check (t.allowed (1000 + dwell), "the hint is allowed at exactly the dwell");

        // ---------------------------------------------------------------------
        //  The reported case: a tip was on screen, the pointer moves to the
        //  control beside it, and the second hint must wait just as long. This
        //  is the check the base class fails - it would allow `b` at 2000.
        // ---------------------------------------------------------------------
        check (t.hoverChanged (&b, 2000), "moving to another control restarts the clock");
        check (! t.allowed (2000), "the NEXT control's hint does not appear immediately");
        check (! t.allowed (2000 + dwell - 1), "the next hint waits the whole dwell too");
        check (t.allowed (2000 + dwell), "the next hint appears when the same time has passed");

        // ---------------------------------------------------------------------
        //  Staying put. The window asks on every timer tick, so a pointer that
        //  wobbles inside one control passes the same address again - that must
        //  not restart anything, or a hint could never be read.
        // ---------------------------------------------------------------------
        check (! t.hoverChanged (&b, 2400), "the same control is not a change");
        check (t.allowed (2400), "a repeated reading of the same control leaves the hint up");
        check (! t.hoverChanged (&b, 5000), "and it is still not a change later on");
        check (t.allowed (5000), "with the hint still allowed");

        // ---------------------------------------------------------------------
        //  Leaving the panel. The pointer over nothing is a change like any
        //  other, so coming back to the same control is a fresh arrival.
        // ---------------------------------------------------------------------
        check (t.hoverChanged (nullptr, 6000), "leaving the panel counts as a change");
        check (! t.allowed (6000), "nothing is allowed while the pointer is over nothing");
        check (t.hoverChanged (&b, 6100), "coming back is an arrival, not a continuation");
        check (! t.allowed (6100 + dwell - 1), "and it waits again");

        // ---------------------------------------------------------------------
        //  reset(): the editor's window can be reset without waiting out a dwell
        //  that a previous session started - and, more importantly, resetting
        //  forgets WHICH control was hovered, so coming back to the one already
        //  under the pointer is a fresh arrival rather than a continuation.
        // ---------------------------------------------------------------------
        t.reset();
        check (t.hoverChanged (&b, 7000), "after reset the same control is a fresh arrival");
        check (! t.allowed (7000 + dwell - 1), "and its hint waits the dwell again");
        check (t.allowed (7000 + dwell), "which allows its hint one dwell later");
    }

    // -------------------------------------------------------------------------
    //  The number itself. 350 ms is what the panel has always quoted and what
    //  the editor comment states; a change to it is a change to how the panel
    //  feels, so it is pinned here rather than left to whichever file is read
    //  first.
    // -------------------------------------------------------------------------
    check (dwell == 350, "the dwell is the panel's documented 350 ms");

    std::printf ("\n%s (%d failures)\n",
                 failures == 0 ? "TOOLTIP CHECK PASSED" : "TOOLTIP CHECK FAILED", failures);
    return failures == 0 ? 0 : 1;
}
