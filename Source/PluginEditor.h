/*
  ==============================================================================
    J37 tape processor editor.
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

#include <array>
#include <memory>

#include <juce_box2d/juce_box2d.h>
#include <juce_opengl/juce_opengl.h>

class J37LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&,
                           int, int, int, int,
                           float, float, float,
                           juce::Slider&) override;
};

class FirstAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
public:
    explicit FirstAudioProcessorEditor (FirstAudioProcessor&);
    ~FirstAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class LevelMeter final : public juce::Component
    {
    public:
        explicit LevelMeter (juce::String title);
        void setLevels (float peakLinear, float rmsLinear);
        void paint (juce::Graphics&) override;

    private:
        juce::String title;
        float rmsDb = -60.0f;
        float peakHoldDb = -60.0f;
        float peakHoldTime = 0.0f;
    };

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

    struct EditorLayout
    {
        juce::Rectangle<int> header;
        juce::Rectangle<int> deck;
        juce::Rectangle<int> controls;
        juce::Rectangle<int> meters;
    };

    static constexpr std::size_t controlCount = 8;
    static constexpr std::size_t decorativeOrbCount = 4;

    void timerCallback() override;
    void createDecorativePhysics();
    EditorLayout getEditorLayout() const;

    FirstAudioProcessor& audioProcessor;
    J37LookAndFeel customLookAndFeel;

    std::array<juce::Slider, controlCount> controls;
    std::array<juce::Label, controlCount> controlLabels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,
               controlCount> controlAttachments;

    juce::ComboBox tapeTypeBox;
    juce::ComboBox speedBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tapeTypeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> speedAttachment;

    juce::Label brandLabel;
    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::Label statusLabel;
    juce::Label deckHeadingLabel;
    juce::Label tapeTypeLabel;
    juce::Label speedLabel;
    juce::Label deckHintLabel;
    juce::Label controlsHeadingLabel;
    juce::Label controlsHintLabel;
    juce::Label metersHeadingLabel;
    juce::Label metersHintLabel;

    LevelMeter inputMeter { "INPUT" };
    LevelMeter outputMeter { "OUTPUT" };

    std::unique_ptr<b2World> physicsWorld;
    std::array<PhysicsOrb, decorativeOrbCount> physicsOrbs {};
    std::array<RenderOrb, decorativeOrbCount> renderOrbs {};
    juce::SpinLock renderOrbsLock;
    juce::OpenGLContext openGLContext;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FirstAudioProcessorEditor)
};
