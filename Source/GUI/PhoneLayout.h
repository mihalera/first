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

    constexpr PhoneEditorLayout computePhoneEditorLayout (int width, int height) noexcept
    {
        const auto contentWidth = nonNegative (width - 2 * phoneLayoutInset);
        const auto contentHeight = nonNegative (height - 2 * phoneLayoutInset);
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
