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

    inline constexpr int phoneLayoutMaxWidth = 600;
    inline constexpr int phoneLandscapeMaxWidth = 1100;
    inline constexpr int phoneLandscapeMaxHeight = 520;
    inline constexpr int phoneLayoutInset = 8;
    inline constexpr int phoneHeaderHeight = 74;
    inline constexpr int phoneDeckHeight = 120;
    inline constexpr int phoneLandscapeDeckWidth = 240;
    inline constexpr int phoneBandGap = 4;

    constexpr bool usesPhoneLayout (int width, int height) noexcept
    {
        return width > 0 && height > 0
               && (width <= phoneLayoutMaxWidth
                   || (width <= phoneLandscapeMaxWidth
                       && height <= phoneLandscapeMaxHeight));
    }

    constexpr int nonNegative (int value) noexcept
    {
        return value > 0 ? value : 0;
    }

    constexpr PhoneEditorLayout computePhoneEditorLayout (int width, int height) noexcept
    {
        const auto contentWidth = nonNegative (width - 2 * phoneLayoutInset);
        const auto contentHeight = nonNegative (height - 2 * phoneLayoutInset);

        if (width > height && height <= phoneLandscapeMaxHeight
            && width <= phoneLandscapeMaxWidth)
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
