// =============================================================================
//  The panel's tooltip window: juce::TooltipWindow with a dwell that applies
//  every time the pointer changes control, not only the first.
//
//  juce::TooltipWindow shows a new tip immediately while one is on screen - see
//  the note at the top of Source/GUI/TipDwell.h for the branch and the reason.
//  The fix is one override: getTipFor() is what the window asks on every timer
//  tick, and returning an empty string withholds the tip until the dwell has
//  passed. The base class then has nothing to show, so it hides the old tip at
//  the moment the pointer moves and shows the new one when this returns text
//  again - which is the behaviour a tooltip is supposed to have.
//
//  The window's own delay is set to 0 by the editor (setUpTooltipsAndPhysics),
//  so there is ONE dwell in the code rather than two stacked ones.
// =============================================================================
#pragma once

#include <JuceHeader.h>

#include "TipDwell.h"

namespace j37panel
{

class DwellTooltipWindow final : public juce::TooltipWindow
{
public:
    DwellTooltipWindow()
        : juce::TooltipWindow (nullptr, 0)
    {
    }

    juce::String getTipFor (juce::Component& component) override
    {
        const auto now = static_cast<long long> (
            juce::Time::getApproximateMillisecondCounter());

        dwell.hoverChanged (&component, now);

        if (! dwell.allowed (now))
            return {};

        return juce::TooltipWindow::getTipFor (component);
    }

    TipDwell dwell;
};

} // namespace j37panel
