/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    struct J37LookAndFeel : juce::LookAndFeel_V4
    {
        void drawRotarySlider (juce::Graphics& g,
                              int x, int y, int width, int height,
                              float sliderPos,
                              const float rotaryStartAngle,
                              const float rotaryEndAngle,
                              juce::Slider& slider) override
        {
            auto bounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                                static_cast<float> (width), static_cast<float> (height)).reduced (8.0f);
            const auto centre = bounds.getCentre();
            const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.43f;
            const auto angle = juce::jmap (sliderPos, 0.0f, 1.0f, rotaryStartAngle, rotaryEndAngle);

            g.setColour (juce::Colour (0xff171d22));
            g.fillEllipse (bounds);

            g.setColour (juce::Colour (0xffd0a95a));
            g.drawEllipse (bounds.reduced (1.0f), 1.6f);

            for (int i = 0; i < 24; ++i)
            {
                const auto tickAngle = juce::jmap (static_cast<float> (i), 0.0f, 24.0f, rotaryStartAngle, rotaryEndAngle);
                const auto tickRadius = radius * 0.9f;
                const auto inner = centre + juce::Point<float> (std::cos (tickAngle) * tickRadius,
                                                             std::sin (tickAngle) * tickRadius);
                const auto outer = centre + juce::Point<float> (std::cos (tickAngle) * (tickRadius + 7.0f),
                                                             std::sin (tickAngle) * (tickRadius + 7.0f));
                g.setColour (juce::Colour (0xffd4b06c).withAlpha (0.20f + (i % 2) * 0.18f));
                g.drawLine (inner.x, inner.y, outer.x, outer.y, 1.0f);
            }

            g.setColour (juce::Colour (0xfff3dcc0));
            g.fillEllipse (centre.x - radius * 0.38f, centre.y - radius * 0.38f,
                           radius * 0.76f, radius * 0.76f);

            g.setColour (juce::Colour (0xffcc9d4a));
            juce::Path pointer;
            pointer.addRectangle (0.0f, -radius * 0.62f, 3.5f, radius * 0.78f);
            const auto transform = juce::AffineTransform().translated (centre.x, centre.y)
                .rotated (angle - juce::MathConstants<float>::halfPi)
                .translated (-1.75f, 0.0f);
            g.fillPath (pointer, transform);

            g.setColour (juce::Colour (0xfff6d58d));
            g.fillEllipse (centre.x - 5.0f, centre.y - 5.0f, 10.0f, 10.0f);

            if (slider.hasKeyboardFocus (false))
            {
                g.setColour (juce::Colour (0xffd8b868));
                g.drawEllipse (bounds.reduced (1.5f), 1.2f);
            }
        }

        void drawLinearSlider (juce::Graphics& g,
                              int x, int y, int width, int height,
                              float sliderPos,
                              float minPos, float maxPos,
                              const juce::Slider::SliderStyle style,
                              juce::Slider& slider) override
        {
            auto bounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                                static_cast<float> (width), static_cast<float> (height));
            auto track = bounds.reduced (4.0f);

            g.setColour (juce::Colour (0xffece8df));
            g.fillRoundedRectangle (track, 8.0f);

            g.setColour (juce::Colour (0xffd7b15f));
            g.drawRoundedRectangle (track.reduced (1.0f), 8.0f, 1.2f);

            juce::Rectangle<float> fill;
            if (style == juce::Slider::LinearVertical || style == juce::Slider::LinearBarVertical)
            {
                const auto activeTop = juce::jmap (sliderPos, 0.0f, 1.0f, track.getBottom(), track.getY());
                fill = juce::Rectangle<float> (track.getX() + 4.0f, activeTop, track.getWidth() - 8.0f, track.getBottom() - activeTop);
            }
            else
            {
                const auto activeLeft = juce::jmap (sliderPos, 0.0f, 1.0f, track.getX(), track.getRight());
                fill = juce::Rectangle<float> (track.getX(), track.getY() + 4.0f, activeLeft - track.getX(), track.getHeight() - 8.0f);
            }

            g.setColour (juce::Colour (0xffd8b868).withAlpha (0.9f));
            g.fillRoundedRectangle (fill, 6.0f);

            if (slider.isEnabled ())
            {
                const auto thumbX = (style == juce::Slider::LinearVertical || style == juce::Slider::LinearBarVertical)
                    ? track.getCentreX()
                    : juce::jmap (sliderPos, 0.0f, 1.0f, track.getX(), track.getRight());
                const auto thumbY = (style == juce::Slider::LinearVertical || style == juce::Slider::LinearBarVertical)
                    ? juce::jmap (sliderPos, 0.0f, 1.0f, track.getBottom(), track.getY())
                    : track.getCentreY();

                g.setColour (juce::Colour (0xfff5f3ef));
                g.fillEllipse (thumbX - 7.5f, thumbY - 7.5f, 15.0f, 15.0f);
                g.setColour (juce::Colour (0xffc89b49));
                g.drawEllipse (thumbX - 7.5f, thumbY - 7.5f, 15.0f, 15.0f, 1.5f);
            }

            juce::ignoreUnused (minPos, maxPos);
        }
    };
}

//==============================================================================
FirstAudioProcessorEditor::FirstAudioProcessorEditor (FirstAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setSize (760, 460);

    auto configureRotary = [this] (juce::Slider& slider, float startValue)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 54, 18);
        slider.setTextBoxIsEditable (true);
        slider.setRotaryParameters (0.08f, 1.0f, 0.5f);
        slider.setVelocityBasedMode (true);
        slider.setMouseDragSensitivity (120);
        slider.setSkewFactorFromMidPoint (0.82f);
        slider.setPopupDisplayEnabled (true, true, nullptr);
        slider.setLookAndFeel (&customLookAndFeel);
        slider.setRange (0.0, 1.0);
        slider.setValue (startValue);
    };

    auto configureLinear = [this] (juce::Slider& slider, juce::Slider::SliderStyle style, float startValue)
    {
        slider.setSliderStyle (style);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, true, 52, 18);
        slider.setTextBoxIsEditable (true);
        slider.setVelocityBasedMode (true);
        slider.setMouseDragSensitivity (80);
        slider.setSkewFactorFromMidPoint (0.7f);
        slider.setPopupDisplayEnabled (true, true, nullptr);
        slider.setLookAndFeel (&customLookAndFeel);
        slider.setRange (0.0, 1.0);
        slider.setValue (startValue);
    };

    configureRotary (driveSlider, 0.62f);
    configureRotary (biasSlider, 0.46f);
    configureRotary (toneSlider, 0.66f);
    configureRotary (wowSlider, 0.22f);
    configureRotary (flutterSlider, 0.30f);

    configureLinear (mixSlider, juce::Slider::LinearHorizontal, 0.82f);
    configureLinear (outputSlider, juce::Slider::LinearVertical, 0.78f);

    tapeTypeBox.addItemList (juce::StringArray { "J37", "Ampex 456", "Studer A800", "Chrome" }, 1);
    speedBox.addItemList (juce::StringArray { "7.5 ips", "15 ips", "30 ips" }, 1);
    tapeTypeBox.setSelectedId (1);
    speedBox.setSelectedId (2);
    tapeTypeBox.setLookAndFeel (&customLookAndFeel);
    speedBox.setLookAndFeel (&customLookAndFeel);
    tapeTypeBox.setTextWhenNothingSelected ("TAPE");
    speedBox.setTextWhenNothingSelected ("SPEED");
    tapeTypeBox.setEditableText (true);
    speedBox.setEditableText (true);

    driveAttachment = std::make_unique<SliderAttachment> (audioProcessor.parameters, "drive", driveSlider);
    biasAttachment = std::make_unique<SliderAttachment> (audioProcessor.parameters, "bias", biasSlider);
    toneAttachment = std::make_unique<SliderAttachment> (audioProcessor.parameters, "tone", toneSlider);
    wowAttachment = std::make_unique<SliderAttachment> (audioProcessor.parameters, "wow", wowSlider);
    flutterAttachment = std::make_unique<SliderAttachment> (audioProcessor.parameters, "flutter", flutterSlider);
    mixAttachment = std::make_unique<SliderAttachment> (audioProcessor.parameters, "mix", mixSlider);
    outputAttachment = std::make_unique<SliderAttachment> (audioProcessor.parameters, "output", outputSlider);
    tapeTypeAttachment = std::make_unique<ComboBoxAttachment> (audioProcessor.parameters, "tape_type", tapeTypeBox);
    speedAttachment = std::make_unique<ComboBoxAttachment> (audioProcessor.parameters, "speed", speedBox);

    addAndMakeVisible (driveSlider);
    addAndMakeVisible (biasSlider);
    addAndMakeVisible (toneSlider);
    addAndMakeVisible (wowSlider);
    addAndMakeVisible (flutterSlider);
    addAndMakeVisible (mixSlider);
    addAndMakeVisible (outputSlider);
    addAndMakeVisible (tapeTypeBox);
    addAndMakeVisible (speedBox);
}

FirstAudioProcessorEditor::~FirstAudioProcessorEditor()
{
}

//==============================================================================
void FirstAudioProcessorEditor::paint (juce::Graphics& g)
{
    auto bg = getLocalBounds().toFloat();

    juce::ColourGradient lightGrad (juce::Colour (0xfff5f2ea), bg.getTopLeft(),
                                   juce::Colour (0xffdfe3e8), bg.getBottomRight(),
                                   false);
    g.setGradientFill (lightGrad);
    g.fillRoundedRectangle (bg.reduced (6.0f), 26.0f);

    const auto panel = getLocalBounds().reduced (12);

    juce::Path grain;
    for (int i = 0; i < 40; ++i)
    {
        const auto y = panel.getY() + 10 + i * 11;
        const auto x = panel.getX() + 16 + (i % 5) * 55;
        grain.startNewSubPath (x, y);
        grain.lineTo (x + 18, y + 4);
    }

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff8f5ef), panel.getTopLeft(),
                                            juce::Colour (0xffdfe5ea), panel.getBottomRight(), false));
    g.fillRoundedRectangle (panel.toFloat(), 24.0f);

    g.setColour (juce::Colour (0xffc1a169).withAlpha (0.18f));
    for (int i = 0; i < 18; ++i)
    {
        const auto y = panel.getY() + 18 + i * 16;
        g.drawLine (panel.getX() + 18.0f, static_cast<float> (y), panel.getRight() - 18.0f, static_cast<float> (y), 1.0f);
    }

    g.setColour (juce::Colour (0xffc9a867));
    g.drawRoundedRectangle (panel.toFloat().reduced (1.0f), 24.0f, 1.6f);

    g.setColour (juce::Colour (0xffd5b26f).withAlpha (0.22f));
    g.fillRoundedRectangle (juce::Rectangle<float> (panel.getX() + 16.0f, panel.getY() + 16.0f,
                                                  panel.getWidth() - 32.0f, 60.0f), 14.0f);

    g.setColour (juce::Colour (0xff9d7c39));
    g.setFont (juce::Font (26.0f, juce::Font::bold));
    g.drawText ("J37", juce::Rectangle<int> (panel.getX() + 22, panel.getY() + 18, 90, 30), juce::Justification::left, false);

    g.setColour (juce::Colour (0xff2d2d2d));
    g.setFont (juce::Font (15.0f, juce::Font::bold));
    g.drawText ("TAPE SATURATOR", juce::Rectangle<int> (panel.getX() + 122, panel.getY() + 24, 220, 20), juce::Justification::left, false);

    g.setColour (juce::Colour (0xffc8a15a));
    g.fillRect (panel.getX() + 440, panel.getY() + 20, 170, 2);
    g.fillRect (panel.getX() + 440, panel.getY() + 34, 170, 2);

    g.setColour (juce::Colour (0xffd8bd7d));
    g.fillEllipse (panel.getX() + 646, panel.getY() + 18, 18, 18);
    g.setColour (juce::Colour (0xff5bc58f));
    g.fillEllipse (panel.getX() + 646, panel.getY() + 18, 8, 8);

    g.setColour (juce::Colour (0xff726d63));
    g.setFont (juce::Font (10.0f, juce::Font::bold));
    g.drawText ("INPUT", juce::Rectangle<int> (panel.getX() + 575, panel.getY() + 20, 60, 18), juce::Justification::centred, false);

    g.setColour (juce::Colour (0xffedf0f2));
    g.fillRoundedRectangle (juce::Rectangle<float> (panel.getX() + 30.0f, panel.getY() + 88.0f,
                                                  panel.getWidth() - 60.0f, 40.0f), 12.0f);
    g.setColour (juce::Colour (0xffbf9a52));
    g.drawRoundedRectangle (juce::Rectangle<float> (panel.getX() + 30.0f, panel.getY() + 88.0f,
                                                  panel.getWidth() - 60.0f, 40.0f), 12.0f, 1.2f);

    g.setColour (juce::Colour (0xff5f564f));
    g.setFont (juce::Font (11.0f, juce::Font::bold));
    g.drawText ("REEL / TAPE / LOSS / WARMTH", juce::Rectangle<int> (panel.getX() + 40, panel.getY() + 99, 250, 20), juce::Justification::left, false);

    g.setColour (juce::Colour (0xffa9823d));
    g.setFont (juce::Font (11.0f, juce::Font::bold));

    const auto knobRowY = panel.getY() + 150;
    const auto knobRowX = panel.getX() + 36;
    const auto labelW = 82;
    const auto labelH = 18;

    g.drawText ("TAPE", juce::Rectangle<int> (knobRowX, knobRowY + 112, labelW, labelH), juce::Justification::centred, false);
    g.drawText ("SPEED", juce::Rectangle<int> (knobRowX + 96, knobRowY + 112, labelW + 18, labelH), juce::Justification::centred, false);
    g.drawText ("DRIVE", juce::Rectangle<int> (knobRowX + 255, knobRowY + 112, 80, labelH), juce::Justification::centred, false);
    g.drawText ("BIAS", juce::Rectangle<int> (knobRowX + 350, knobRowY + 112, 80, labelH), juce::Justification::centred, false);
    g.drawText ("TONE", juce::Rectangle<int> (knobRowX + 445, knobRowY + 112, 80, labelH), juce::Justification::centred, false);
    g.drawText ("WOW", juce::Rectangle<int> (knobRowX + 12, knobRowY + 260, 82, labelH), juce::Justification::centred, false);
    g.drawText ("FLUTTER", juce::Rectangle<int> (knobRowX + 108, knobRowY + 260, 92, labelH), juce::Justification::centred, false);
    g.drawText ("MIX", juce::Rectangle<int> (knobRowX + 214, knobRowY + 260, 82, labelH), juce::Justification::centred, false);
    g.drawText ("OUT", juce::Rectangle<int> (knobRowX + 308, knobRowY + 260, 82, labelH), juce::Justification::centred, false);

    g.setColour (juce::Colour (0xffcaa566).withAlpha (0.18f));
    g.fillRoundedRectangle (juce::Rectangle<float> (panel.getX() + 18.0f, panel.getY() + 135.0f,
                                                  panel.getWidth() - 36.0f, 3.0f), 2.0f);
}

void FirstAudioProcessorEditor::resized()
{
    const auto bounds = getLocalBounds().reduced (20, 22);

    tapeTypeBox.setBounds (bounds.getX() + 34, bounds.getY() + 82, 110, 30);
    speedBox.setBounds (bounds.getX() + 158, bounds.getY() + 82, 120, 30);

    driveSlider.setBounds (bounds.getX() + 290, bounds.getY() + 138, 92, 92);
    biasSlider.setBounds (bounds.getX() + 386, bounds.getY() + 138, 92, 92);
    toneSlider.setBounds (bounds.getX() + 482, bounds.getY() + 138, 92, 92);

    wowSlider.setBounds (bounds.getX() + 42, bounds.getY() + 248, 92, 92);
    flutterSlider.setBounds (bounds.getX() + 138, bounds.getY() + 248, 92, 92);
    mixSlider.setBounds (bounds.getX() + 236, bounds.getY() + 248, 120, 22);
    outputSlider.setBounds (bounds.getX() + 236, bounds.getY() + 330, 18, 80);

    driveSlider.setRotaryParameters (0.08f, 1.0f, 0.5f);
    biasSlider.setRotaryParameters (0.08f, 1.0f, 0.5f);
    toneSlider.setRotaryParameters (0.08f, 1.0f, 0.5f);
    wowSlider.setRotaryParameters (0.08f, 1.0f, 0.5f);
    flutterSlider.setRotaryParameters (0.08f, 1.0f, 0.5f);
}
