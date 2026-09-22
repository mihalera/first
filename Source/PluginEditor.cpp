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

            g.setColour (juce::Colour (0xff111820));
            g.fillEllipse (bounds);

            g.setColour (juce::Colour (0xffd4b061));
            g.drawEllipse (bounds, 1.5f);

            g.setColour (juce::Colour (0xfff0d39a));
            g.fillEllipse (centre.x - radius * 0.38f, centre.y - radius * 0.38f,
                           radius * 0.76f, radius * 0.76f);

            g.setColour (juce::Colour (0xffd4b061));
            juce::Path pointer;
            pointer.addRectangle (0.0f, -radius * 0.68f, 3.0f, radius * 0.8f);
            const auto transform = juce::AffineTransform().translated (centre.x, centre.y).rotated (angle - juce::MathConstants<float>::halfPi).translated (-1.5f, 0.0f);
            g.fillPath (pointer, transform);

            g.setColour (juce::Colour (0xfff5d88d));
            g.fillEllipse (centre.x - 5.0f, centre.y - 5.0f, 10.0f, 10.0f);

            juce::ignoreUnused (slider);
        }
    };
}

//==============================================================================
FirstAudioProcessorEditor::FirstAudioProcessorEditor (FirstAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setSize (700, 420);

    auto configureSlider = [this] (juce::Slider& slider)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters (0.12f, 1.0f, 0.5f);
        slider.setVelocityBasedMode (true);
        slider.setMouseDragSensitivity (110);
        slider.setSkewFactorFromMidPoint (0.6f);
        slider.setPopupDisplayEnabled (true, true, nullptr);
        slider.setLookAndFeel (&customLookAndFeel);
    };

    configureSlider (driveSlider);
    configureSlider (biasSlider);
    configureSlider (toneSlider);
    configureSlider (wowSlider);
    configureSlider (flutterSlider);
    configureSlider (mixSlider);
    configureSlider (outputSlider);

    driveSlider.setRange (0.0, 1.0);
    biasSlider.setRange (0.0, 1.0);
    toneSlider.setRange (0.0, 1.0);
    wowSlider.setRange (0.0, 1.0);
    flutterSlider.setRange (0.0, 1.0);
    mixSlider.setRange (0.0, 1.0);
    outputSlider.setRange (0.0, 1.0);

    driveSlider.setValue (0.62);
    biasSlider.setValue (0.48);
    toneSlider.setValue (0.63);
    wowSlider.setValue (0.28);
    flutterSlider.setValue (0.36);
    mixSlider.setValue (0.88);
    outputSlider.setValue (0.82);

    tapeTypeBox.addItemList (juce::StringArray { "J37", "Ampex 456", "Studer A800", "Chrome" }, 1);
    speedBox.addItemList (juce::StringArray { "7.5 ips", "15 ips", "30 ips" }, 1);

    tapeTypeBox.setSelectedId (1);
    speedBox.setSelectedId (2);

    tapeTypeBox.setLookAndFeel (&customLookAndFeel);
    speedBox.setLookAndFeel (&customLookAndFeel);
    tapeTypeBox.setTextWhenNothingSelected ("TAPE");
    speedBox.setTextWhenNothingSelected ("SPEED");

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
    g.setColour (juce::Colour (0xfff9f7f3));
    g.fillRoundedRectangle (panel.toFloat(), 22.0f);

    g.setColour (juce::Colour (0xffc9a867));
    g.drawRoundedRectangle (panel.toFloat().reduced (1.0f), 22.0f, 1.6f);

    g.setColour (juce::Colour (0xffd5b26f).withAlpha (0.18f));
    g.fillRoundedRectangle (juce::Rectangle<float> (panel.getX() + 16.0f, panel.getY() + 16.0f,
                                                  panel.getWidth() - 32.0f, 60.0f), 14.0f);

    g.setColour (juce::Colour (0xff9d7c39));
    g.setFont (juce::Font (26.0f, juce::Font::bold));
    g.drawText ("J37", juce::Rectangle<int> (panel.getX() + 22, panel.getY() + 18, 90, 30), juce::Justification::left, false);

    g.setColour (juce::Colour (0xff2c2d31));
    g.setFont (juce::Font (15.0f, juce::Font::bold));
    g.drawText ("TAPE SATURATOR", juce::Rectangle<int> (panel.getX() + 120, panel.getY() + 24, 220, 20), juce::Justification::left, false);

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

    g.drawText ("TAPE", juce::Rectangle<int> (knobRowX, knobRowY + 102, labelW, labelH), juce::Justification::centred, false);
    g.drawText ("SPEED", juce::Rectangle<int> (knobRowX + 96, knobRowY + 102, labelW + 18, labelH), juce::Justification::centred, false);
    g.drawText ("DRIVE", juce::Rectangle<int> (knobRowX + 255, knobRowY + 102, 80, labelH), juce::Justification::centred, false);
    g.drawText ("BIAS", juce::Rectangle<int> (knobRowX + 350, knobRowY + 102, 80, labelH), juce::Justification::centred, false);
    g.drawText ("TONE", juce::Rectangle<int> (knobRowX + 445, knobRowY + 102, 80, labelH), juce::Justification::centred, false);
    g.drawText ("WOW", juce::Rectangle<int> (knobRowX + 14, knobRowY + 264, 82, labelH), juce::Justification::centred, false);
    g.drawText ("FLUTTER", juce::Rectangle<int> (knobRowX + 110, knobRowY + 264, 92, labelH), juce::Justification::centred, false);
    g.drawText ("MIX", juce::Rectangle<int> (knobRowX + 215, knobRowY + 264, 82, labelH), juce::Justification::centred, false);
    g.drawText ("OUT", juce::Rectangle<int> (knobRowX + 310, knobRowY + 264, 82, labelH), juce::Justification::centred, false);

    g.setColour (juce::Colour (0xffcaa566).withAlpha (0.18f));
    g.fillRoundedRectangle (juce::Rectangle<float> (panel.getX() + 18.0f, panel.getY() + 135.0f,
                                                  panel.getWidth() - 36.0f, 3.0f), 2.0f);
}

void FirstAudioProcessorEditor::resized()
{
    const auto bounds = getLocalBounds().reduced (20, 22);

    tapeTypeBox.setBounds (bounds.getX() + 34, bounds.getY() + 82, 110, 30);
    speedBox.setBounds (bounds.getX() + 158, bounds.getY() + 82, 120, 30);

    driveSlider.setBounds (bounds.getX() + 282, bounds.getY() + 140, 88, 88);
    biasSlider.setBounds (bounds.getX() + 380, bounds.getY() + 140, 88, 88);
    toneSlider.setBounds (bounds.getX() + 478, bounds.getY() + 140, 88, 88);

    wowSlider.setBounds (bounds.getX() + 42, bounds.getY() + 250, 88, 88);
    flutterSlider.setBounds (bounds.getX() + 138, bounds.getY() + 250, 88, 88);
    mixSlider.setBounds (bounds.getX() + 240, bounds.getY() + 250, 88, 88);
    outputSlider.setBounds (bounds.getX() + 334, bounds.getY() + 250, 88, 88);

    driveSlider.setRotaryParameters (0.1f, 1.0f, 0.5f);
    biasSlider.setRotaryParameters (0.1f, 1.0f, 0.5f);
    toneSlider.setRotaryParameters (0.1f, 1.0f, 0.5f);
    wowSlider.setRotaryParameters (0.1f, 1.0f, 0.5f);
    flutterSlider.setRotaryParameters (0.1f, 1.0f, 0.5f);
    mixSlider.setRotaryParameters (0.1f, 1.0f, 0.5f);
    outputSlider.setRotaryParameters (0.1f, 1.0f, 0.5f);
}
