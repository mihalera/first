/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/**
*/
class FirstAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    FirstAudioProcessorEditor (FirstAudioProcessor&);
    ~FirstAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    FirstAudioProcessor& audioProcessor;

    juce::Slider driveSlider;
    juce::Slider biasSlider;
    juce::Slider toneSlider;
    juce::Slider wowSlider;
    juce::Slider flutterSlider;
    juce::Slider mixSlider;
    juce::Slider outputSlider;
    juce::ComboBox tapeTypeBox;
    juce::ComboBox speedBox;
    juce::LookAndFeel_V4 customLookAndFeel;

    using SliderAttachment =
        juce::AudioProcessorValueTreeState::SliderAttachment;

    using ComboBoxAttachment =
        juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<SliderAttachment> driveAttachment;
    std::unique_ptr<SliderAttachment> biasAttachment;
    std::unique_ptr<SliderAttachment> toneAttachment;
    std::unique_ptr<SliderAttachment> wowAttachment;
    std::unique_ptr<SliderAttachment> flutterAttachment;
    std::unique_ptr<SliderAttachment> mixAttachment;
    std::unique_ptr<SliderAttachment> outputAttachment;
    std::unique_ptr<ComboBoxAttachment> tapeTypeAttachment;
    std::unique_ptr<ComboBoxAttachment> speedAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FirstAudioProcessorEditor)
};
