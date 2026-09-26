/*
  ==============================================================================
    Nonlin Analog Saturator processor editor.
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

#include <array>
#include <memory>

#include <juce_box2d/juce_box2d.h>
#include <juce_opengl/juce_opengl.h>

// melatonin_blur (fetched by CPM in CMakeLists.txt). Fast shadow and gradient
// blurring for JUCE Components. Available to the paint routines below, which
// draw the panel's soft analog shading by hand today; the include is conditional
// so a build without the module on its include path still compiles.
#if __has_include (<melatonin_blur/melatonin_blur.h>)
 #include <melatonin_blur/melatonin_blur.h>
 #define J37_HAS_MELATONIN_BLUR 1
#else
 #define J37_HAS_MELATONIN_BLUR 0
#endif

// foleys_gui_magic (fetched by CPM in CMakeLists.txt). Daniel Walz's declarative
// GUI framework: the editor's controls can be described in XML and edited live,
// instead of every position being computed by hand in resized(). It is linked and
// its header is available here so a MagicProcessor-style editor can be built on
// top of it; the current editor is still the hand-written one, so this is the
// seam rather than a replacement.
#if __has_include (<foleys_gui_magic/foleys_gui_magic.h>)
 #include <foleys_gui_magic/foleys_gui_magic.h>
 #define J37_HAS_FOLEYS 1
#else
 #define J37_HAS_FOLEYS 0
#endif

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

    /** Hardware rocker-switch drawing for the panel's on/off toggles. */
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool, bool) override;

    /** Caption drawing for the panel's text buttons.

        juce::TextButton has no per-button font, so the only way to keep a caption
        inside a fixed-width button is to draw the text here and size the font to
        the button's own width. The preset / A/B row is width-constrained at the
        minimum panel size, so a caption that grows ("A (LIVE) *") used to be
        ellipsised by the default Look and Feel.
    */
    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool, bool) override;

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

    // Twenty-one controls, in five columns:
    //
    //   row 1  INPUT    DRIVE   BIAS    BRIGHT   TONE       (the machine's front panel)
    //   row 2  WOW      FLUTTER MIX     OUTPUT   WIDTH
    //   row 3  BLEND    SHAPE   AMP BIAS SAG     PRESENCE   (the saturation core)
    //   row 4  CABINET  DELAY   DLY LVL ST OFFSET NOISE
    //   row 5  SUBFUND                  (the only control on a row of its own)
    //
    // The count here, the controlIds / controlNames lists and the defaultValues array
    // in the .cpp are four views of ONE list and must agree. That is why the array is
    // sized by controlCount rather than by a literal: a mismatch is then a compile
    // error (C2078) instead of a silent out-of-bounds read at run time.
    static constexpr std::size_t controlCount = 21;
    static constexpr int controlColumns = 5;
    static constexpr std::size_t decorativeOrbCount = 6;

    void timerCallback() override;
    void createDecorativePhysics();
    void applyTheme();
    EditorLayout getEditorLayout() const;

    // Shared by the constructor and by resized(): the knob-grid section captions
    // are created while laying out, so a constructor-local helper was out of scope
    // there and the build failed to compile (C2065 on `styleLabel`).
    void styleLabel (juce::Label& label, const juce::String& text, float size,
                     juce::Colour colour, bool bold, juce::Justification justification);

    // The GL switch's colours follow the context rather than the theme alone: it is a
    // plain TextButton, so it needs a themed background (it was the one TextButton in
    // the panel without one) and an explicit ON colour, since getToggleState() is
    // always false for it.
    void styleGlButton (bool isOn);

    void refreshPresetList();
    void refreshUserPresetList();
    void updateWorkflowButtons();
    bool keyPressed (const juce::KeyPress&) override;

    FirstAudioProcessor& audioProcessor;
    J37LookAndFeel customLookAndFeel;

    // Tooltips (set with setTooltip on the workflow controls) only render while a
    // TooltipWindow instance exists; without one the calls are silent no-ops.
    // TooltipWindow: a shared instance is required for any setTooltip text to show.
    // SharedResourcePointer default-constructs the object; the hover delay is set in
    // the editor's constructor through setMillisecondsBeforeTipAppears().
    juce::SharedResourcePointer<juce::TooltipWindow> tooltipWindow;

    std::array<juce::Slider, controlCount> controls;
    std::array<juce::Label, controlCount> controlLabels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,
               controlCount> controlAttachments;

    juce::ComboBox tapeTypeBox;
    juce::ComboBox speedBox;
    juce::ComboBox instrumentBox;
    // Transport: STOP / PLAY / START. A combo rather than three buttons, so the
    // state is one host-visible parameter and the panel shows which state is
    // engaged without a lamp per position.
    juce::ComboBox transportBox;
    juce::Label transportLabel;
    juce::TextButton glButton { "GL ON" };
    juce::ToggleButton bypassButton { "BYPASS" };
    juce::ToggleButton deltaButton { "DELTA" };
    juce::TextButton themeButton { "DARK THEME" };

#if JUCE_DEBUG
    // Melatonin's component inspector, debug builds only (the module is linked
    // by CMake in every config, but only this member and the editor include
    // compile it into the plugin). Created at the end of the constructor,
    // released first in the destructor so it never watches a dying tree.
    std::unique_ptr<melatonin::Inspector> inspector;
#endif

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
    juce::ToggleButton polarityButton { "POLARITY" };
    juce::ToggleButton autoGainButton { "AUTO GAIN" };
    juce::ComboBox oversamplingBox;
    juce::Label oversamplingLabel;
    juce::Label presetHeadingLabel;
    juce::Label compareBadgeLabel;
    juce::Label presetBadgeLabel;
    // A/B mirroring is throttled to roughly 5 Hz rather than run on every timer
    // frame, so the deep parameter-tree copy cannot compete with the audio thread
    // while a knob is being dragged.
    int compareMirrorTick = 0;
    int lastShownPreset = -2;
    int lastShownSlot = -1;
    bool lastShownDirty = false;
    bool lastShownCanUndo = false;
    bool lastShownCanRedo = false;
    juce::String lastShownUserPreset;
    bool lastShownPresetDirty = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tapeTypeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> speedAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> instrumentAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> transportAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> oversamplingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> deltaAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> polarityAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoGainAttachment;

    juce::Label brandLabel;
    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::Label statusLabel;
    juce::Label deckHeadingLabel;
    /** The commit this binary was built from, shown at the top of the deck. */
    juce::Label buildLabel;
    juce::Label tapeTypeLabel;
    juce::Label speedLabel;
    juce::Label instrumentLabel;
    juce::Label deckHintLabel;
    juce::Label controlsHeadingLabel;
    juce::Label controlsHintLabel;
    // Section captions inside the knob grid, so five rows of knobs read as three
    // groups (machine / saturation core / head and transport) rather than one block.
    juce::Label machineSectionLabel;
    juce::Label saturationSectionLabel;
    juce::Label headSectionLabel;
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

    // OpenGL is ON by default, but the first attach in the constructor can fail for a
    // reason that does not apply a moment later - the host has not necessarily created
    // the editor's native peer yet, and there is no context to attach to without one.
    // So the timer keeps trying for a few seconds and then stops for good: a driver, a
    // remote session or a VM that cannot create a context will not manage on the last
    // attempt either, and retrying forever would burn a frame every 33 ms for a panel
    // that will never use it. Zero once the context is up, or once the user says OFF.
    int glAttachAttemptsLeft = 0;

    std::unique_ptr<juce::AlertWindow> savePresetWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FirstAudioProcessorEditor)
};
