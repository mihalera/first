#pragma once

namespace j37panel
{
    struct PhoneLayoutRectangle
    {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
    };

    struct PhoneEditorLayout
    {
        PhoneLayoutRectangle header;
        PhoneLayoutRectangle deck;
        PhoneLayoutRectangle controls;
    };

    struct PhoneTabStripLayout
    {
        PhoneLayoutRectangle families;
        PhoneLayoutRectangle pages;
    };

    inline constexpr int phoneLayoutMaxWidth = 800;
    inline constexpr int phoneLandscapeMaxWidth = 1440;
    inline constexpr int phoneLandscapeMaxHeight = 720;
    inline constexpr int phoneLayoutInset = 8;
    inline constexpr int phoneHeaderHeight = 74;
    inline constexpr int phoneDeckHeight = 120;
    inline constexpr int phoneLandscapeDeckWidth = 240;
    inline constexpr int phoneBandGap = 4;

    /** Where the landscape deck's own rows end, measured from the deck's top.

        The column is as tall as the window and the rows are not: transport,
        the two selectors and the preset workflow are ~130 px together at any
        landscape height. Everything below this line belongs to the OSC ring,
        which is why the ring is solved from the deck's height instead of being
        pinned to its foot at a fixed 78 px. */
    inline constexpr int phoneLandscapeDeckRowsBottom = 130;

    constexpr bool usesPhoneLayout (int width, int height) noexcept
    {
        return width > 0 && height > 0
               && (width <= phoneLayoutMaxWidth
                   || (width <= phoneLandscapeMaxWidth
                       && height <= phoneLandscapeMaxHeight));
    }

    /** Whether this build runs on a handheld. */
#if defined (JUCE_ANDROID) || defined (JUCE_IOS)
    inline constexpr bool phoneLayoutPlatform = true;
#else
    inline constexpr bool phoneLayoutPlatform = false;
#endif

    /** What the EDITOR asks: the phone bands are for a phone. A desktop window
        dragged narrow used to fall into them - a 700 px window on Windows got the
        phone header, the phone deck and a page selector, with no way back except
        resizing - which is not what the compact layout is for. A desktop window
        answers a small size with the resizable grid instead. */
    constexpr bool usesPhoneLayoutOnThisPlatform (int width, int height) noexcept
    {
        return phoneLayoutPlatform && usesPhoneLayout (width, height);
    }

    /** Landscape is a phone in landscape, not merely a window wider than it is
        tall. The width limit here is the LANDSCAPE limit; it used to be the
        portrait one (800), so a 915 x 412 phone, a 1280 x 720 tablet and every
        landscape phone wider than 800 px fell through to the PORTRAIT bands -
        header on top, deck under it, controls under that - which is exactly what
        a crooked Android landscape looked like. */
    constexpr bool isPhoneLandscape (int width, int height) noexcept
    {
        return width > height && height <= phoneLandscapeMaxHeight
               && width <= phoneLandscapeMaxWidth;
=======
    inline constexpr int phoneLayoutMaxWidth = 600;
    inline constexpr int phoneLayoutInset = 8;
    inline constexpr int phoneHeaderHeight = 74;
    inline constexpr int phoneDeckHeight = 120;
    inline constexpr int phoneBandGap = 4;

    constexpr bool usesPhoneLayout (int width) noexcept
    {
        return width > 0 && width <= phoneLayoutMaxWidth;
    }

    constexpr int nonNegative (int value) noexcept
    {
        return value > 0 ? value : 0;
    }

    constexpr PhoneTabStripLayout computePhoneLandscapeTabStrip (
        PhoneLayoutRectangle controls) noexcept
    {
        const PhoneLayoutRectangle row {
            controls.x + 14, controls.y + 34,
            nonNegative (controls.width - 28), 22
        };
        const PhoneLayoutRectangle pages {
            controls.x + 14, controls.y + 60,
            nonNegative (controls.width - 28), 22
        };
        return { row, pages };
    }

    constexpr PhoneLayoutRectangle computePhoneOscRingBounds (
        PhoneLayoutRectangle deck, bool landscape) noexcept
    {
        if (landscape)
        {
            // The ring owns everything the rows leave, centred in that band
            // rather than hanging off the deck's foot: a 1097 x 617 phone
            // handed the ring 78 px and left three hundred pixels of empty
            // panel above it. It is square and width-limited by the 240 px
            // deck, so the height only decides the band it is centred in.
            const auto bandTop = deck.y + phoneLandscapeDeckRowsBottom + 8;
            const auto bandBottom = deck.y + deck.height - 8;
            const auto bandHeight = nonNegative (bandBottom - bandTop);
            const auto widthLimit = nonNegative (deck.width - 16);
            const auto size = bandHeight < widthLimit ? bandHeight : widthLimit;
            return { deck.x + (deck.width - size) / 2,
                     bandTop + (bandHeight - size) / 2, size, size };
        }

        const auto size = deck.height > 4
                            ? (deck.height - 4 < 68 ? deck.height - 4 : 68)
                            : 0;
        return { deck.x + deck.width - size - 6,
                 deck.y + (deck.height - size) / 2, size, size };
    }

=======
    constexpr PhoneEditorLayout computePhoneEditorLayout (int width, int height) noexcept
    {
        const auto contentWidth = nonNegative (width - 2 * phoneLayoutInset);
        const auto contentHeight = nonNegative (height - 2 * phoneLayoutInset);
        if (isPhoneLandscape (width, height))
        {
            const auto headerHeight = contentHeight < phoneHeaderHeight
                                        ? contentHeight : phoneHeaderHeight;
            const PhoneLayoutRectangle header {
                phoneLayoutInset, phoneLayoutInset, contentWidth, headerHeight
            };
            const auto bodyY = phoneLayoutInset + headerHeight + phoneBandGap;
            const auto bodyHeight = nonNegative (height - phoneLayoutInset - bodyY);
            const auto deckWidth = contentWidth < phoneLandscapeDeckWidth
                                     ? contentWidth : phoneLandscapeDeckWidth;
            const PhoneLayoutRectangle deck {
                phoneLayoutInset, bodyY, deckWidth, bodyHeight
            };
            const auto controlsX = deck.x + deck.width + phoneBandGap;
            const PhoneLayoutRectangle controls {
                controlsX, bodyY, nonNegative (width - phoneLayoutInset - controlsX),
                bodyHeight
            };
            return { header, deck, controls };
        }

=======
        auto remainingHeight = contentHeight;
        auto y = phoneLayoutInset;

        const auto headerHeight = remainingHeight < phoneHeaderHeight
                                    ? remainingHeight : phoneHeaderHeight;
        const PhoneLayoutRectangle header { phoneLayoutInset, y, contentWidth, headerHeight };
        y += headerHeight;
        remainingHeight -= headerHeight;

        const auto firstGap = remainingHeight < phoneBandGap ? remainingHeight : phoneBandGap;
        y += firstGap;
        remainingHeight -= firstGap;

        const auto deckHeight = remainingHeight < phoneDeckHeight
                                  ? remainingHeight : phoneDeckHeight;
        const PhoneLayoutRectangle deck { phoneLayoutInset, y, contentWidth, deckHeight };
        y += deckHeight;
        remainingHeight -= deckHeight;

        const auto secondGap = remainingHeight < phoneBandGap ? remainingHeight : phoneBandGap;
        y += secondGap;
        remainingHeight -= secondGap;

        const PhoneLayoutRectangle controls { phoneLayoutInset, y, contentWidth, remainingHeight };
        return { header, deck, controls };
    }
}
