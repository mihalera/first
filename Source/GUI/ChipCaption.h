// =============================================================================
//  The page chips' captions: does the label fit the button it is drawn in?
//
//  The page bar re-flows to the live family's page count, so TAPE's nine pages
//  share a panel width that nine names cannot all have at once. The paint routine
//  already shrinks the type to fit, but it clamps the shrink at 0.55 of the asked
//  size and then draws with `useEllipsesIfTooBig = false` - so a caption that
//  needs more than that clamp to fit is CLIPPED, mid-glyph, at the button's edge.
//  That is the "FRONT E" that was visible on the TAPE bar.
//
//  The rule here is deliberately conservative and JUCE-free, so tests/layout can
//  compile it: an uppercase glyph is charged 0.70 em and a space 0.38 em. The
//  panel's bold face measures 0.60-0.66 em per uppercase glyph, so a caption that
//  fits this budget fits the real one with room to spare - the model can only
//  over-estimate the width, never under-estimate it.
//
//  Two policies come out of it:
//
//    * the caption's own type size is chosen per button, so a chip's label is as
//      large as its button allows and never larger;
//    * below the legibility floor the label is ellipsised rather than shrunk
//      further, and the harness checks that at least half the name survives -
//      a chip reading "CHARA…" is a chip you can find the page by, a chip clipped
//      at "CHARA" is one you cannot read at all.
// =============================================================================
#pragma once

#include <cstddef>

namespace j37panel
{
    inline constexpr float chipCaptionMaxHeight = 11.0f;
    inline constexpr float chipCaptionMinHeight = 7.0f;
    inline constexpr float chipCaptionAdvanceEm = 0.70f;      // an uppercase glyph
    inline constexpr float chipCaptionSpaceEm = 0.38f;
    inline constexpr int chipCaptionMinVisible = 4;           // characters before the ellipsis

    /** The component id the page bar tags its chips with. A juce::TextButton in
        JUCE 9 carries no font of its own, so this is how the bar and the two
        paint routines agree that a button's caption is fitted to its width. */
    inline constexpr const char* chipCaptionComponentId = "mibox.pagechip";

    inline float chipCaptionWidth (const char* text, float height) noexcept
    {
        if (text == nullptr || height <= 0.0f)
            return 0.0f;

        auto width = 0.0f;

        for (const char* glyph = text; *glyph != '\0'; ++glyph)
            width += height * (*glyph == ' ' ? chipCaptionSpaceEm : chipCaptionAdvanceEm);

        return width;
    }

    /** The largest type up to `maxHeight` at which `text` fits `availableWidth`,
        never below `minHeight`. A caption that does not fit even at the floor is
        ellipsised by the paint routine, which is why the floor exists at all: the
        chip must stay readable before it stays complete. */
    inline float fittedChipCaptionHeight (const char* text, float maxHeight,
                                          float minHeight, float availableWidth) noexcept
    {
        if (text == nullptr || *text == '\0' || availableWidth <= 0.0f || maxHeight <= minHeight)
            return maxHeight;

        const auto atMax = chipCaptionWidth (text, maxHeight);

        if (atMax <= availableWidth)
            return maxHeight;

        const auto wanted = maxHeight * availableWidth / atMax;
        return wanted < minHeight ? minHeight : wanted;
    }

    /** How many characters of `text` fit `availableWidth` at `height`, leaving room
        for the ellipsis itself. Zero means not even the ellipsis fits. */
    inline int ellipsisedChipCaptionCount (const char* text, float height,
                                           float availableWidth) noexcept
    {
        if (text == nullptr || height <= 0.0f || availableWidth <= 0.0f)
            return 0;

        // The ellipsis is ONE glyph however many bytes U+2026 takes in UTF-8, so
        // it is charged as one advance rather than measured as three characters.
        const auto budget = availableWidth - height * chipCaptionAdvanceEm;
        auto used = 0.0f;
        int count = 0;

        for (const char* glyph = text; *glyph != '\0'; ++glyph)
        {
            const auto advance = height * (*glyph == ' ' ? chipCaptionSpaceEm : chipCaptionAdvanceEm);

            if (used + advance > budget)
                break;

            used += advance;
            ++count;
        }

        return count;
    }
}
