/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

#include <array>

#include <juce_box2d/juce_box2d.h>
#include <juce_opengl/juce_opengl.h>

//==============================================================================
/**
*/
class FirstAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                   private juce::Timer
{
public:
    FirstAudioProcessorEditor (FirstAudioProcessor&);
    ~FirstAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void createDecorativePhysics();

    struct PhysicsOrb
    {
        b2Body* body = nullptr;
        float radius = 0.0f;
        juce::Colour colour;
    };

    struct RenderOrb
    {
        juce::Point<float> position;
        float radius = 0.0f;
        juce::Colour colour;
    };

    static constexpr std::size_t decorativeOrbCount = 4;

    FirstAudioProcessor& audioProcessor;
    std::unique_ptr<b2World> physicsWorld;
    std::array<PhysicsOrb, decorativeOrbCount> physicsOrbs {};
    std::array<RenderOrb, decorativeOrbCount> renderOrbs {};
    juce::SpinLock renderOrbsLock;
    juce::OpenGLContext openGLContext;

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
