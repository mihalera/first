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
    void setDarkTheme (bool shouldUseDarkTheme) noexcept { darkTheme = shouldUseDarkTheme; }
    void setActivity (float newActivity) noexcept { activity = newActivity; }
    void setDrift (float newDrift) noexcept { drift = newDrift; }
    void advanceFrame() noexcept { animationPhase += 0.11f; }

    void drawRotarySlider (juce::Graphics&,
                           int, int, int, int,
                           float, float, float,
                           juce::Slider&) override;

private:
    bool darkTheme = false;
    float activity = 0.0f;   ///< Compressor activity, drives the glow around the knobs.
    float drift = 0.0f;      ///< Transport drift, drives the fine wobble in the ticks.
    float animationPhase = 0.0f;
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
        void setDarkTheme (bool shouldUseDarkTheme) noexcept { darkTheme = shouldUseDarkTheme; }
        void paint (juce::Graphics&) override;

    private:
        juce::String title;
        bool darkTheme = false;
        float rmsDb = -60.0f;
        float peakHoldDb = -60.0f;
        float peakHoldTime = 0.0f;
        float animatedRmsDb = -60.0f;
    };

    /** Vertical gain-reduction bar showing how hard the tape glue compressor works. */
    class CompressorMeter final : public juce::Component
    {
    public:
        CompressorMeter() { setInterceptsMouseClicks (false, false); }
        void setDarkTheme (bool shouldUseDarkTheme) noexcept { darkTheme = shouldUseDarkTheme; }
        void setReduction (float reductionDb, float activity);
        void paint (juce::Graphics&) override;

    private:
        bool darkTheme = false;
        float displayedDb = 0.0f;   // Smoothed, always <= 0.
        float activity = 0.0f;
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

    static constexpr std::size_t controlCount = 9;
    static constexpr int controlColumns = 3;
    static constexpr std::size_t decorativeOrbCount = 6;

    void timerCallback() override;
    void createDecorativePhysics();
    void applyTheme();
    EditorLayout getEditorLayout() const;

    FirstAudioProcessor& audioProcessor;
    J37LookAndFeel customLookAndFeel;

    std::array<juce::Slider, controlCount> controls;
    std::array<juce::Label, controlCount> controlLabels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,
               controlCount> controlAttachments;

    juce::ComboBox tapeTypeBox;
    juce::ComboBox speedBox;
    juce::TextButton bypassButton { "BYPASS" };
    juce::TextButton themeButton { "DARK THEME" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tapeTypeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> speedAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

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
    juce::Label compressorLabel;
    juce::Label compressorReadout;

    LevelMeter inputMeter { "INPUT" };
    LevelMeter outputMeter { "OUTPUT" };
    CompressorMeter compressorMeter;
    bool darkTheme = false;

    // Animated presentation state, advanced one step per editor frame.
    float glowPhase = 0.0f;
    float glowAmount = 0.0f;
    float driftAmount = 0.5f;
    float reelAngle = 0.0f;
    float reelSpeed = 0.0f;
    bool currentBypassDisplay = false;

    std::unique_ptr<b2World> physicsWorld;
    std::array<PhysicsOrb, decorativeOrbCount> physicsOrbs {};
    std::array<RenderOrb, decorativeOrbCount> renderOrbs {};
    juce::SpinLock renderOrbsLock;
    juce::OpenGLContext openGLContext;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FirstAudioProcessorEditor)
};
