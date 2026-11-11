// =============================================================================
//  Every page chip's caption, against the button it is drawn in.
//
//  The page bar re-flows to the live family's page count, so the TAPE family's
//  nine names share one panel width. The paint routine used to shrink the type
//  to fit, CLAMP that shrink at 55 % of the asked size, and then draw with
//  "ellipsise if too big" turned OFF - so a caption needing more than the clamp
//  was clipped mid-glyph at the button's edge ("FRONT E"). Nothing measured it,
//  which is why it survived: a clipped caption is not a crash and not a warning.
//
//  This harness measures it. For every page and every supported panel width it
//  rebuilds the button width the bar gives that page (the same arithmetic as
//  placeTabBar: the panel minus 28 px of margin and the family's gaps, split
//  between the family's pages), asks ChipCaption.h for the type size that fits,
//  and then requires the caption to be whole - or, at the legibility floor, to
//  keep at least a word's worth of characters before its ellipsis.
//
//  The width model over-estimates: the panel's bold face runs 0.60-0.66 em per
//  uppercase glyph and ChipCaption.h charges 0.70, so a caption that fits here
//  fits the real face with room to spare.
//
//  tests/layout/run.sh compiles and runs this beside the knob-strip harness.
// =============================================================================
#include "GUI/ChipCaption.h"
#include "GUI/PanelTabs.h"

#include <cstdio>
#include <cstring>

namespace
{
    int failures = 0;
    int checks = 0;

    void check (bool condition, const char* what, const char* page = nullptr, int width = 0)
    {
        ++checks;

        if (! condition)
        {
            ++failures;
            std::printf ("  FAIL  %s", what);

            if (page != nullptr)
                std::printf ("  [%s at %d px]", page, width);

            std::printf ("\n");
        }
    }

    // The bar's own arithmetic, kept in step with placeTabBar: the family's page
    // count decides the gap, and the panel's width minus its margin is split
    // between them.
    int buttonWidthFor (int panelWidth, int visiblePages)
    {
        const auto gap = visiblePages <= 5 ? 6 : 4;
        return (panelWidth - 28 - gap * (visiblePages - 1)) / visiblePages;
    }

    int visiblePagesIn (int family)
    {
        auto count = 0;

        for (std::size_t tab = 0; tab < j37panel::numTabPages; ++tab)
            if (j37panel::tabSpecs[tab].family == family)
                ++count;

        return count > 0 ? count : 1;
    }
}

int main()
{
    using namespace j37panel;

    std::printf ("The page chips' captions, against their buttons\n");

    // What the bar used to do: shrink to fit but no further than 55 % of the
    // asked size, then clip. Counted rather than asserted, so the number in the
    // log is the size of the defect that was fixed.
    auto clippedBefore = 0;
    auto clippedNow = 0;

    for (auto family = 0; family < 4; ++family)
    {
        const auto pages = visiblePagesIn (family);

        for (auto panelWidth = 480; panelWidth <= 1800; panelWidth += 2)
        {
            const auto buttonWidth = buttonWidthFor (panelWidth, pages);
            // The stricter of the two paint routines' paddings: the in-tab look and
            // feel takes 8 px each side, the deck's takes 3.
            const auto available = static_cast<float> (buttonWidth - 16);

            for (std::size_t tab = 0; tab < numTabPages; ++tab)
            {
                const auto& spec = tabSpecs[tab];

                if (spec.family != family)
                    continue;

                const auto name = spec.name;
                const auto ascii = reinterpret_cast<const char*> (name);

                // The shipping policy: the type size the bar gives this button.
                const auto height = fittedChipCaptionHeight (ascii, chipCaptionMaxHeight,
                                                             chipCaptionMinHeight, available);

                check (height >= chipCaptionMinHeight - 1.0e-4f,
                       "the caption's type never drops below the legibility floor", name, panelWidth);
                check (height <= chipCaptionMaxHeight + 1.0e-4f,
                       "and never exceeds the size the bar asks for", name, panelWidth);

                const auto whole = chipCaptionWidth (ascii, height) <= available;
                const auto kept = ellipsisedChipCaptionCount (ascii, height, available);
                const auto length = static_cast<int> (std::strlen (ascii));

                if (! whole)
                {
                    ++clippedNow;
                    check (kept >= chipCaptionMinVisible && kept >= length / 2,
                           "a caption that has to be ellipsised keeps its first word's worth",
                           name, panelWidth);
                }

                // The old rule, for the record: 55 % clamp and no ellipsis.
                const auto oldHeight = chipCaptionMaxHeight * 0.55f;
                if (chipCaptionWidth (ascii, oldHeight) > available)
                    ++clippedBefore;
            }
        }
    }

    std::printf ("  captions whole or ellipsised inside their buttons: %d over every "
                 "page and every width\n", checks);
    std::printf ("  old rule (55 %% clamp, no ellipsis) would have clipped %d label(s); "
                 "the shipping rule ellipsises %d\n", clippedBefore, clippedNow);

    check (clippedBefore > 0,
          "the old clamp really did clip - the check above is not vacuous");

    std::printf ("\n%s (%d check(s), %d failed)\n",
                 failures == 0 ? "CHIP CAPTION CHECK PASSED" : "CHIP CAPTION CHECK FAILED",
                 checks, failures);
    return failures == 0 ? 0 : 1;
}
