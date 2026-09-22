/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::Rectangle<int> makeSliderBounds (int x, int y, int w, int h)
    {
        return { x, y, w, h };
    }
}

//==============================================================================
FirstAudioProcessorEditor::FirstAudioProcessorEditor (FirstAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setSize (520, 260);

    auto configureSlider = [] (juce::Slider& slider)
    {
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters (0.0f, 1.0f, 0.5f);
        slider.setVelocityBasedMode (true);
        slider.setPopupDisplayEnabled (true, true, nullptr);
        slider.setLookAndFeel (&juce::LookAndFeel::getDefaultLookAndFeel());
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

    driveSlider.setValue (0.58);
    biasSlider.setValue (0.36);
    toneSlider.setValue (0.52);
    wowSlider.setValue (0.18);
    flutterSlider.setValue (0.22);
    mixSlider.setValue (0.82);
    outputSlider.setValue (0.86);

    tapeTypeBox.addItemList (juce::StringArray { "J37", "Ampex 456", "Studer A800", "Chrome" }, 1);
    speedBox.addItemList (juce::StringArray { "7.5 ips", "15 ips", "30 ips" }, 1);

    tapeTypeBox.setSelectedId (1);
    speedBox.setSelectedId (2);

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
    g.fillAll (juce::Colour (0xff12171d));

    auto frame = getLocalBounds().reduced (10);
    g.setColour (juce::Colour (0xffd6b26d).withAlpha (0.25f));
    g.drawRoundedRectangle (frame.toFloat(), 18.0f, 1.0f);

    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (18.0f, juce::Font::bold));
    g.drawText ("J37 TAPE SATURATOR", getLocalBounds().removeFromTop (36), juce::Justification::centred, true);

    g.setColour (juce::Colour (0xffd9c08d));
    g.setFont (juce::Font (12.0f, juce::Font::plain));
    g.drawText ("TAPE", juce::Rectangle<int> (35, 150, 80, 20), juce::Justification::centred, false);
    g.drawText ("SPEED", juce::Rectangle<int> (125, 150, 80, 20), juce::Justification::centred, false);
    g.drawText ("DRIVE", juce::Rectangle<int> (215, 170, 80, 20), juce::Justification::centred, false);
    g.drawText ("BIAS", juce::Rectangle<int> (305, 170, 80, 20), juce::Justification::centred, false);
    g.drawText ("TONE", juce::Rectangle<int> (395, 170, 80, 20), juce::Justification::centred, false);
    g.drawText ("WOW", juce::Rectangle<int> (35, 215, 80, 20), juce::Justification::centred, false);
    g.drawText ("FLUTTER", juce::Rectangle<int> (125, 215, 80, 20), juce::Justification::centred, false);
    g.drawText ("MIX", juce::Rectangle<int> (215, 215, 80, 20), juce::Justification::centred, false);
    g.drawText ("OUT", juce::Rectangle<int> (305, 215, 80, 20), juce::Justification::centred, false);
}

void FirstAudioProcessorEditor::resized()
{
    const auto bounds = getLocalBounds().reduced (18, 20);

    tapeTypeBox.setBounds (bounds.getX(), bounds.getY() + 10, 80, 24);
    speedBox.setBounds (bounds.getX() + 90, bounds.getY() + 10, 90, 24);

    driveSlider.setBounds (bounds.getX() + 190, bounds.getY(), 70, 130);
    biasSlider.setBounds (bounds.getX() + 280, bounds.getY(), 70, 130);
    toneSlider.setBounds (bounds.getX() + 370, bounds.getY(), 70, 130);

    wowSlider.setBounds (bounds.getX(), bounds.getY() + 100, 70, 130);
    flutterSlider.setBounds (bounds.getX() + 90, bounds.getY() + 100, 70, 130);
    mixSlider.setBounds (bounds.getX() + 190, bounds.getY() + 100, 70, 130);
    outputSlider.setBounds (bounds.getX() + 280, bounds.getY() + 100, 70, 130);

    driveSlider.setRotaryParameters (0.2f, 0.8f, 0.5f);
    biasSlider.setRotaryParameters (0.2f, 0.8f, 0.5f);
    toneSlider.setRotaryParameters (0.2f, 0.8f, 0.5f);
    wowSlider.setRotaryParameters (0.2f, 0.8f, 0.5f);
    flutterSlider.setRotaryParameters (0.2f, 0.8f, 0.5f);
    mixSlider.setRotaryParameters (0.2f, 0.8f, 0.5f);
    outputSlider.setRotaryParameters (0.2f, 0.8f, 0.5f);
}
