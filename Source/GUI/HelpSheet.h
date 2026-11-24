// =============================================================================
//  The guide sheet: the signal-path diagram and the four help scenes.
//
//  A full-editor overlay, like the parameter sheet: opened from the SETTINGS page,
//  closed with Escape or the CLOSE button. Every image is optional - a build
//  without the assets leaves the captions and the procedural fallbacks, so the
//  sheet is never a blank window.
// =============================================================================
#pragma once

#include "GUI/PanelPaint.h"
#include "GUI/PanelAssets.h"
#include "GUI/PanelTranslations.h"

#include <functional>

namespace j37panel
{
class HelpSheet final : public juce::Component
{
  public:
    HelpSheet(const UiPalette &paletteToUse);

    void paint(juce::Graphics &) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress &) override;

    void open();
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return isVisible(); }

  private:
    UiPalette palette;
    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::TextButton closeButton{"CLOSE"};
};

inline HelpSheet::HelpSheet(const UiPalette &paletteToUse) : palette(paletteToUse)
{
    setOpaque(true);
    setInterceptsMouseClicks(true, true);
    setWantsKeyboardFocus(true);

    titleLabel.setText(xlat("GUIDE").toUpperCase(), juce::dontSendNotification);
    titleLabel.setFont(panelFont(17.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, palette.accent);
    addAndMakeVisible(titleLabel);

    subtitleLabel.setText(xlat("the signal path, A/B comparison, the preset workflow "
                               "and loading a model or impulse response"),
                          juce::dontSendNotification);
    subtitleLabel.setFont(panelFont(11.0f));
    subtitleLabel.setColour(juce::Label::textColourId, palette.secondary);
    addAndMakeVisible(subtitleLabel);

    closeButton.setColour(juce::TextButton::buttonColourId, palette.raised);
    closeButton.setColour(juce::TextButton::textColourOffId, palette.text);
    closeButton.setColour(juce::TextButton::buttonOnColourId, palette.raised.darker(0.15f));
    closeButton.setButtonText(xlat("CLOSE"));
    closeButton.onClick = [this] { close(); };
    addAndMakeVisible(closeButton);

    setVisible(false);
}

inline void HelpSheet::paint(juce::Graphics &g)
{
    g.fillAll(palette.background);

    g.setColour(palette.border.withAlpha(0.55f));
    g.fillRect(12, 46, getWidth() - 24, 1);

    auto area = getLocalBounds().reduced(12);
    area.removeFromTop(46 + 6);

    // Signal path diagram, full width.
    const auto &signalPath = j37panel::assets::cachedImage("signal_path_png");
    if (signalPath.isValid() && signalPath.getWidth() > 0)
    {
        const auto targetWidth = area.getWidth();
        const auto targetHeight = juce::jmax(1, juce::roundToInt(
            static_cast<float>(signalPath.getHeight()) * static_cast<float>(targetWidth)
            / static_cast<float>(signalPath.getWidth())));
        const auto imageRect = juce::Rectangle<int>(area.getX(), area.getY(), targetWidth,
                                                    juce::jmin(targetHeight, area.getHeight() / 2));
        g.setColour(juce::Colours::white);
        g.drawImage(signalPath, imageRect.getX(), imageRect.getY(), imageRect.getWidth(),
                    imageRect.getHeight(), 0, 0, signalPath.getWidth(), signalPath.getHeight(), true);
        area.removeFromTop(imageRect.getHeight() + 8);
    }
    else
    {
        g.setColour(palette.secondary);
        g.setFont(panelFont(11.0f));
        g.drawText(xlat("signal path diagram unavailable"), area.removeFromTop(24),
                   juce::Justification::centred, false);
    }

    // Four help scenes in a 2 x 2 grid.
    const char *captions[] = { "SIGNAL PATH", "A/B COMPARE", "PRESET WORKFLOW",
                               "LOAD A MODEL OR IR" };
    const auto cols = 2;
    const auto rows = 2;
    const auto cellWidth = area.getWidth() / cols;
    const auto cellHeight = area.getHeight() / rows;
    const auto &scenes = j37panel::assets::cachedImage("help_illustrations_png");
    const auto sceneValid = scenes.isValid() && scenes.getWidth() >= 1024 && scenes.getHeight() >= 576;
    for (int i = 0; i < 4; ++i)
    {
        const auto col = i % cols;
        const auto row = i / cols;
        auto cell = juce::Rectangle<int>(area.getX() + col * cellWidth,
                                         area.getY() + row * cellHeight,
                                         cellWidth, cellHeight).reduced(6);
        auto imageArea = cell.removeFromTop(cell.getHeight() - 16);
        g.setColour(palette.raised);
        g.fillRoundedRectangle(imageArea.toFloat(), 3.0f);
        if (sceneValid)
        {
            const auto srcX = (i % 2) * 512;
            const auto srcY = (i / 2) * 288;
            g.setColour(juce::Colours::white);
            g.drawImage(scenes, imageArea.getX(), imageArea.getY(), imageArea.getWidth(),
                        imageArea.getHeight(), srcX, srcY, 512, 288, true);
        }
        g.setColour(palette.border.withAlpha(0.6f));
        g.drawRoundedRectangle(imageArea.toFloat().reduced(0.5f), 3.0f, 1.0f);
        g.setColour(palette.secondary);
        g.setFont(panelFont(9.0f, juce::Font::bold));
        g.drawText(xlat(captions[i]), cell, juce::Justification::centred, false);
    }
}

inline void HelpSheet::resized()
{
    auto area = getLocalBounds().reduced(12);
    auto header = area.removeFromTop(46);
    titleLabel.setBounds(header.removeFromTop(24));
    subtitleLabel.setBounds(header.removeFromTop(16));
    closeButton.setBounds(getWidth() - 12 - 84, 10, 84, 26);
}

inline bool HelpSheet::keyPressed(const juce::KeyPress &key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }
    return false;
}

inline void HelpSheet::open()
{
    setVisible(true);
    toFront(true);
    grabKeyboardFocus();
    resized();
}

inline void HelpSheet::close()
{
    setVisible(false);
}
} // namespace j37panel
