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
        LevelMeter (juce::String meterTitle, juce::String meterSubtitle);

        /** Feeds the four loudness views plus the equal-weighted combination, all in dB. */
        void setLoudness (float peakDbIn, float rmsDbIn, float lufsIn, float vuDbIn,
                          float combinedDbIn, bool clipping);

        void setDarkTheme (bool shouldUseDarkTheme) noexcept { darkTheme = shouldUseDarkTheme; }
        void paint (juce::Graphics&) override;

    private:
        juce::String title;
        juce::String subtitle;
        bool darkTheme = false;

        // The four independent loudness views, in dB. They are kept separately as well
        // as combined so the meter can show the spread between them: that spread is
        // itself useful information (a big peak-to-LUFS gap means a very dynamic signal).
        float peakDb = -70.0f;
        float rmsDb = -70.0f;
        float lufs = -70.0f;
        float vuDb = -70.0f;
        float combinedDb = -70.0f;
        bool clipping = false;

        // Smoothed display values, so the numbers move like a meter instead of flickering.
        float displayedRms = -70.0f;
        float displayedLufs = -70.0f;
        float displayedVu = -70.0f;
        float displayedCombined = -70.0f;
        float peakHoldDb = -70.0f;
        float peakHoldTime = 0.0f;
    };

    /** Vertical gain-reduction bar for one glue compressor stage. */
    class CompressorMeter final : public juce::Component
    {
    public:
        CompressorMeter (juce::String meterTitle, juce::String stageCaption);
        void setDarkTheme (bool shouldUseDarkTheme) noexcept { darkTheme = shouldUseDarkTheme; }
        void setReduction (float reductionDb, float activity);
        void paint (juce::Graphics&) override;

    private:
        juce::String title;
        juce::String caption;
        bool darkTheme = false;
        float displayedDb = 0.0f;   // Smoothed reduction, always <= 0.
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

    static constexpr std::size_t controlCount = 10;
    static constexpr int controlColumns = 5;
    static constexpr std::size_t decorativeOrbCount = 6;

    void timerCallback() override;
    void createDecorativePhysics();
    void applyTheme();
    EditorLayout getEditorLayout() const;

    void refreshPresetList();
    void refreshUserPresetList();
    void updateWorkflowButtons();
    bool keyPressed (const juce::KeyPress&) override;

    FirstAudioProcessor& audioProcessor;
    J37LookAndFeel customLookAndFeel;

    // Tooltips (set with setTooltip on the workflow controls) only render while a
    // TooltipWindow instance exists; without one the calls are silent no-ops.
    // TooltipWindow: a shared instance is required for any setTooltip text to show.
    // A short delay makes the tip appear quickly when the user hovers a control.
    juce::SharedResourcePointer<juce::TooltipWindow> tooltipWindow { 350 };

    std::array<juce::Slider, controlCount> controls;
    std::array<juce::Label, controlCount> controlLabels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,
               controlCount> controlAttachments;

    juce::ComboBox tapeTypeBox;
    juce::ComboBox speedBox;
    juce::TextButton bypassButton { "BYPASS" };
    juce::TextButton themeButton { "DARK THEME" };

    // Premium workflow bar: oversampling switch, factory + user presets, A/B compare,
    // undo/redo, polarity and auto-gain switches.
    juce::ComboBox presetBox;
    juce::ComboBox userPresetBox;
    juce::TextButton savePresetButton { "SAVE" };
    juce::TextButton deletePresetButton { "DEL" };
    juce::TextButton copyAButton { "COPY A" };
    juce::TextButton copyBButton { "COPY B" };
    juce::TextButton compareButton { "A/B" };
    juce::TextButton undoButton { "UNDO" };
    juce::TextButton redoButton { "REDO" };
    juce::TextButton polarityButton { "POLARITY" };
    juce::TextButton autoGainButton { "AUTO GAIN" };
    juce::ComboBox oversamplingBox;
    juce::Label oversamplingLabel;
    juce::Label presetHeadingLabel;
    juce::Label compareBadgeLabel;
    juce::Label presetBadgeLabel;
    int lastShownPreset = -2;
    int lastShownSlot = -1;
    bool lastShownDirty = false;
    bool lastShownCanUndo = false;
    bool lastShownCanRedo = false;
    juce::String lastShownUserPreset;
    bool lastShownPresetDirty = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tapeTypeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> speedAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> oversamplingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> polarityAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoGainAttachment;

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

    // Live harmonic readout. This is the one place the panel reports what the analogue
    // model is actually doing to the signal rather than what it is receiving.
    juce::Label harmonicsLabel;
    juce::Label harmonicsReadout;

    // Four metering surfaces, arranged two by two:
    //   top row    - the INPUT and OUTPUT level VU meters
    //   bottom row - one gain-reduction meter per glue compressor stage
    LevelMeter inputMeter { "INPUT", "LEVEL / dBFS" };
    LevelMeter outputMeter { "OUTPUT", "LEVEL / dBFS" };
    CompressorMeter compressorMeterIn { "COMP IN", "after input trim" };
    CompressorMeter compressorMeterOut { "COMP OUT", "before output trim" };
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

    std::unique_ptr<juce::AlertWindow> savePresetWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FirstAudioProcessorEditor)
};
