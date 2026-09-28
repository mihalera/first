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
#include <vector>

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
    /** The three front panels. A plain int-typed enum so it can be a parameter
        value, a member and a switch subject without casts. */
    enum class ThemeChoice { ivory = 0, charcoal = 1, metal = 2 };

    // The bool entry point is kept because every paint routine in this file calls
    // it and the two DARK themes share every code path - this is the older,
    // coarser question ("is this a dark panel?"). setTheme() is the finer one.
    void setDarkTheme (bool shouldUseDarkTheme) noexcept
    {
        theme = shouldUseDarkTheme ? ThemeChoice::charcoal : ThemeChoice::ivory;
    }

    void setTheme (ThemeChoice newTheme) noexcept { theme = newTheme; }
    ThemeChoice getTheme() const noexcept { return theme; }

    /** True for the two dark panels. Used by every drawing routine, which only
        ever needs to know dark-or-light rather than which dark. */
    bool isDarkTheme() const noexcept { return theme != ThemeChoice::ivory; }

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
    ThemeChoice theme = ThemeChoice::ivory;
    float activity = 0.0f;   ///< Compressor activity, drives the glow around the knobs.
    float drift = 0.0f;      ///< Transport drift, drives the fine wobble in the ticks.
    float animationPhase = 0.0f;
};

//==============================================================================
/**
    The IN-TAB look and feel: a second, deliberately different design for the
    lists and the on/off switches that live INSIDE a tab rather than on the deck.

    Why a second LookAndFeel rather than the same one at a smaller size. The
    deck's controls are drawn as the front panel of a machine - a rocker switch
    with a sliding thumb and a lamp, a combo with a heavy recessed bezel. That is
    right for the deck, which IS the machine. Inside a tab the same drawing is
    wrong for three reasons:

      1. SCALE. A tab cell is a knob-sized square. A rocker drawn at that size
         has a 10 px track and a caption that no longer fits, which is exactly
         the crowding the tabs exist to avoid.

      2. ROLE. A switch on the deck is a machine state (BYPASS, POLARITY); a
         switch in a tab is a MODE of the thing beside it (a type selector, a
         list). They should not look like the same kind of control.

      3. LEGIBILITY. A tab's list is read while looking at knobs, so it has to
         read as a compact, flat field with a clear value - like a settings row
         in software - not as a hardware rocker.

    So the in-tab style is deliberately FLATTER and more typographic:

      - lists are a single recessed row with the value left-aligned and a
        slim caret on the right, and the popup menu keeps the panel's colours;
      - on/off switches are a small pill with a sliding dot and an ON/OFF
        word, drawn at whatever size the cell gives them;
      - both use the SAME palette as the rest of the panel, so the tab still
        belongs to the plugin - it is a different drawing, not a different skin.

    It carries its own theme flag for the same reason the deck's does: the
    colours are read live at paint time, so a theme switch recolours everything
    on the same frame with no per-control bookkeeping.
*/
class J37InlineLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void setTheme (J37LookAndFeel::ThemeChoice newTheme) noexcept { theme = newTheme; }
    void setDarkTheme (bool shouldUseDarkTheme) noexcept
    {
        theme = shouldUseDarkTheme ? J37LookAndFeel::ThemeChoice::charcoal
                                   : J37LookAndFeel::ThemeChoice::ivory;
    }

    /** The list drawing: a flat recessed field. */
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    /** The popup's own geometry, so the in-tab lists open a compact menu rather
        than the deck's tall rows. */
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;

    /** The small pill switch used for the tab's on/off controls. */
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;

private:
    J37LookAndFeel::ThemeChoice theme = J37LookAndFeel::ThemeChoice::ivory;
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

    // Twenty-one controls, split across three tabs of at most nine, in signal-flow
    // order: consecutive slices of one chain, read left to right.
    //
    //   tab 1  MACHINE           INPUT  BRIGHT TONE   MIX   WIDTH OUTPUT
    //   tab 2  SATURATION CORE   DRIVE  BIAS   SUBFUND BLEND  SHAPE  AMP BIAS SAG
    //   tab 3  HEAD / TRANSPORT  CABINET PRESENCE WOW   FLUTTER ST OFFSET
    //                            DELAY DLY LVL NOISE
    //
    // Tabs hold between three and nine controls, so at tabColumns wide most come to
    // two rows and NOISE to three: the grid sizes its own row count from the active
    // tab, so neither grows nor jumps when the user switches tabs.
    //
    // The count here, the controlIds / controlNames lists, the defaultValues array
    // and the tabSpecs table in the .cpp are all views of ONE list and must agree.
    // That is why the array is sized by controlCount rather than by a literal: a
    // mismatch is then a compile error (C2078) instead of a silent out-of-bounds
    // read at run time. The tab table is checked against controlCount by a
    // static_assert too, so a knob that no tab lists is a build error, not a knob
    // that silently disappears from the panel.
    static constexpr std::size_t controlCount = 54;
    static constexpr int tabColumns = 4;
    // Seven tabs: MACHINE, DRIVE, CHARACTER, NOISE, VINYL, SPACE, SETTINGS. The
    // seventh arrived with the four record faults (DUST / SCRATCH / WARP /
    // ELECTRICAL): cramming them onto NOISE would have put thirteen knobs on one
    // page, which is exactly the crowding the tabs exist to avoid. VINYL gets its
    // own page because the four are one subject - the state of the record itself -
    // rather than four unrelated controls.
    static constexpr int numTabs = 9;
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

    // ---------------------------------------------------------------------
    //  Language and tooltips.
    //
    //  setTip() is what every tooltip in this panel goes through, rather than
    //  Component::setTooltip directly. It does two things: it translates the
    //  English through the current language table, and it REMEMBERS the English
    //  against the component. The remembering is the whole point - a tooltip is
    //  set once, in the constructor, and changing the language has to put the
    //  new text back on all thirty-three of them. Without the record there is
    //  nothing to re-apply from, because by then the English has been replaced
    //  by the translation and the original cannot be recovered from the
    //  component. So the English is kept, and updateTooltips() replays it.
    // ---------------------------------------------------------------------
    // juce::SettableTooltipClient, not juce::Component: setTooltip is on the
    // mixin, and Component itself has no such method. (TooltipClient alone is
    // not enough either - that is the read-only half, getTooltip with no
    // setter.) Every component this panel gives a tooltip to derives from
    // SettableTooltipClient.
    void setTip (juce::SettableTooltipClient& component, const juce::String& english);
    void updateTooltips();

    /** Loads the translation table for `languageIndex` (0 = English, 1 = Ukrainian)
        from the embedded Source/Translations data, installs it as JUCE's current
        mappings, and re-applies every tooltip. */
    void applyLanguage (int languageIndex);

    // The GL switch's colours follow the context rather than the theme alone: it is a
    // plain TextButton, so it needs a themed background (it was the one TextButton in
    // the panel without one) and an explicit ON colour, since getToggleState() is
    // always false for it.
    void styleGlButton (bool isOn);

    // Tabs. The grid used to be one five-row block of all twenty-one knobs with three
    // small section captions floating in the gaps above rows 3 and 4 - which is to
    // say, on top of the rows of knobs above them. The captions are gone and the
    // three groups are real tabs, so only the active tab's controls are on screen
    // and each group has a whole grid to itself.
    void setCurrentTab (int newTab);
    void styleTabButtons();

    void refreshPresetList();
    void refreshUserPresetList();
    void updateWorkflowButtons();
    bool keyPressed (const juce::KeyPress&) override;

    FirstAudioProcessor& audioProcessor;
    J37LookAndFeel customLookAndFeel;
    // The second, deliberately different design for the lists and switches that
    // live INSIDE a tab. See the class comment for why it is a separate LookAndFeel
    // rather than the deck's at a smaller size.
    J37InlineLookAndFeel inlineLookAndFeel;

    // Tooltips (set with setTooltip on the workflow controls) only render while a
    // TooltipWindow instance exists; without one the calls are silent no-ops.
    // TooltipWindow: a shared instance is required for any setTooltip text to show.
    // SharedResourcePointer default-constructs the object; the hover delay is set in
    // the editor's constructor through setMillisecondsBeforeTipAppears().
    juce::SharedResourcePointer<juce::TooltipWindow> tooltipWindow;

    // component -> the English it was given, so updateTooltips() has something to
    // replay. A vector rather than a map: the order tooltips were set in is the
    // order they are refreshed in, and nothing here needs lookup by key.
    struct TooltipSource
    {
        juce::SettableTooltipClient* component;
        juce::String english;
    };
    std::vector<TooltipSource> tooltipSources;

    juce::Label languageLabel { {}, "LANGUAGE" };
    juce::ComboBox languageBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> languageAttachment;

    std::array<juce::Slider, controlCount> controls;
    std::array<juce::Label, controlCount> controlLabels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,
               controlCount> controlAttachments;

    juce::ComboBox tapeTypeBox;
    juce::ComboBox valveTypeBox;
    juce::ComboBox ampTypeBox;
    juce::ComboBox transformerTypeBox;
    juce::ComboBox digitalTypeBox;
    juce::ComboBox vinylTypeBox;
    juce::ComboBox vinylSpeedBox;
    juce::ComboBox speedBox;
    juce::ComboBox instrumentBox;
    // The three selectors that describe how the record was made, what plays it
    // and what reads it. They sit on the VINYL tab beside the record's own
    // faults, because together they are the story of one record: what was cut,
    // what it is played on, what is reading it, and what is wrong with it.
    juce::ComboBox vinylGenerationBox;
    juce::ComboBox vinylTurntableBox;
    juce::ComboBox vinylCartridgeBox;
    juce::Label vinylGenerationLabel;
    juce::Label vinylTurntableLabel;
    juce::Label vinylCartridgeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> vinylGenerationAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> vinylTurntableAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> vinylCartridgeAttachment;
    // The UI-sounds switch, on the SETTINGS tab beside GL.
    juce::ToggleButton uiSoundsButton { "UI SOUNDS" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> uiSoundsAttachment;

    // The remaining choice parameters: the DI pad, the machine's track layout,
    // and the two EQ orders. Each is a list rather than a knob because its
    // values are discrete machines/settings, not points on a scale - and each
    // uses the IN-TAB look and feel, because that is what a list inside a tab
    // is drawn with.
    juce::ComboBox diPadBox;
    juce::ComboBox tracksBox;
    juce::ComboBox inputEqOrderBox;
    juce::ComboBox outputEqOrderBox;
    juce::Label diPadLabel;
    juce::Label tracksLabel;
    juce::Label inputEqOrderLabel;
    juce::Label outputEqOrderLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> diPadAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tracksAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> inputEqOrderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> outputEqOrderAttachment;

    // The four corner switches: one per equaliser, one per filter. The corner
    // knobs already bypass themselves at the ends of their travel, but that is
    // a FALLBACK, not a control - a user who wants the equaliser's high-pass
    // gone has to find the knob's end stop to get it, and then cannot tell the
    // filter apart from a filter sitting at its bypass. These are the explicit
    // answer, and they are pill switches rather than knobs because they are
    // state, like GL and the mode button.
    juce::ToggleButton inputEqHpButton { "HP" };
    juce::ToggleButton inputEqLpButton { "LP" };
    juce::ToggleButton outputEqHpButton { "HP" };
    juce::ToggleButton outputEqLpButton { "LP" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> inputEqHpAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> inputEqLpAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> outputEqHpAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> outputEqLpAttachment;
    // Transport: STOP / PLAY / START as three buttons rather than a combo, because
    // the whole point of the gesture is that START and STOP are momentary presses
    // and PLAY is a resting state. A combo made the user open a menu to stop a
    // machine; three buttons make the deck behave like a deck.
    //
    // The buttons do NOT carry a ButtonAttachment: the transport is not a simple
    // on/off parameter, it is a three-way choice the engine also writes back to
    // (START settles into PLAY), and START-to-STOP is a TOGGLE on the same key.
    // The editor drives it through FirstAudioProcessor::setTransportState and
    // paints the active state from the parameter, so panel and engine agree.
    juce::TextButton transportStopButton { "STOP" };
    juce::TextButton transportPlayButton { "PLAY" };
    juce::TextButton transportStartButton { "START" };
    juce::Label transportLabel;
    // SPINDOWN - the momentary platter hold. Press and hold and the machine runs
    // down under the current transport; release and it spins back up. A button's
    // onClick is a click, so the press/release edges are read from its
    // mouseDown/mouseUp through a Button subclass below.
    class SpindownButton final : public juce::TextButton
    {
    public:
        // The member is brace-initialised with its caption ({ "SPINDOWN" }),
        // which needs TextButton's own constructors; a derived class without
        // this using-declaration has none of them and the initialiser does
        // not compile.
        using juce::TextButton::TextButton;

        std::function<void (bool)> onHoldChanged;
        void mouseDown (const juce::MouseEvent& e) override
        {
            juce::TextButton::mouseDown (e);
            if (onHoldChanged != nullptr)
                onHoldChanged (true);
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            juce::TextButton::mouseUp (e);
            if (onHoldChanged != nullptr)
                onHoldChanged (false);
        }
    };
    SpindownButton spindownButton { "SPINDOWN" };
    juce::Label spindownLabel;
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

    // DELAY TYPE - which machine the second head behaves as. A combo rather than a
    // knob because the three are discrete machines, not points on a scale.
    juce::ComboBox delayTypeBox;
    juce::Label delayTypeLabel;
    juce::Label delaySyncLabel;
    juce::Label glLabel;

    // Tempo sync: a switch plus the note value. A combo rather than a knob
    // because the note values are discrete and a knob through them would be a
    // scale the user has to learn.
    juce::ToggleButton delaySyncButton { "SYNC DELAY" };
    juce::ComboBox delayRateBox;
    juce::Label delayRateLabel;

    // The two whole-machine mode switches became ONE cycling button: OFF ->
    // LO-FI -> MODERN -> OFF. The modes are mutually exclusive by design and the
    // engine reads two bools, so the button is the only state holder on the panel
    // and writes both parameters itself (no attachment: a ButtonAttachment on
    // each side would fight the cycle).
    juce::TextButton modeCycleButton { "MODE: OFF" };
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
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> valveTypeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> ampTypeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> transformerTypeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> digitalTypeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> vinylTypeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> vinylSpeedAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> speedAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> instrumentAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> delayTypeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> delayRateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> delaySyncAttachment;

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
    juce::Label valveTypeLabel;
    juce::Label ampTypeLabel;
    juce::Label transformerTypeLabel;
    juce::Label digitalTypeLabel;
    juce::Label vinylTypeLabel;
    juce::Label vinylSpeedLabel;
    juce::Label speedLabel;
    juce::Label instrumentLabel;
    juce::Label deckHintLabel;
    juce::Label controlsHeadingLabel;
    juce::Label controlsHintLabel;
    // The three knob-grid tabs, in the order tabSpecs lists them. Clicking one shows
    // that group's controls and hides every other knob.
    std::array<juce::TextButton, numTabs> tabButtons;
    int currentTab = 0;
    juce::Label metersHeadingLabel;
    juce::Label metersHintLabel;
    juce::Label compressorLabel;
    juce::Label compressorReadout;

    // Live harmonic readout. This is the one place the panel reports what the analogue
    // model is actually doing to the signal rather than what it is receiving.
    juce::Label harmonicsLabel;
    juce::Label harmonicsReadout;
    juce::Label bpmLabel;
    juce::Label bpmReadout;

    // Subharmonic tracking readout. The cascade reports the note it is locked to
    // and how solidly, so the panel can answer the one question a depth knob
    // cannot: is the stage actually generating, or is it idle because the
    // detector has not found a note? A depth of 60 % means nothing without it.
    juce::Label subfundLabel;
    juce::Label subfundReadout;

    // Anti-phase guard readout. The one fault the panel could not otherwise show:
    // a stereo pair whose sides oppose looks completely normal in stereo and then
    // cancels in mono, so the guard's own correction is published as a number the
    // user can watch. "clean" means the guard is idle, which is the healthy state.
    juce::Label antiPhaseLabel;
    juce::Label antiPhaseReadout;
    juce::String lastShownAntiPhase;

    // Four metering surfaces, arranged two by two:
    //   top row    - the INPUT and OUTPUT level VU meters
    //   bottom row - one gain-reduction meter per glue compressor stage
    LevelMeter inputMeter { "INPUT", "LEVEL / dBFS" };
    LevelMeter outputMeter { "OUTPUT", "LEVEL / dBFS" };
    CompressorMeter compressorMeterIn { "COMP IN", "after input trim" };
    CompressorMeter compressorMeterOut { "COMP OUT", "before output trim" };

    // Animated presentation state, advanced one step per editor frame.
    float glowPhase = 0.0f;
    float glowAmount = 0.0f;
    float driftAmount = 0.5f;
    float reelAngle = 0.0f;
    float reelSpeed = 0.0f;
    bool currentBypassDisplay = false;

    // -----------------------------------------------------------------------
    //  The front panel's theme.
    //
    //  Three panels - ivory, charcoal and metal - so this is an enum rather
    //  than the bool it used to be. The bool survives as isDarkTheme() because
    //  every drawing routine only ever needs to know dark-or-light and the two
    //  dark themes share all of those code paths; the enum is what tells the two
    //  of them apart, in the one place that has to (paletteForTheme).
    // -----------------------------------------------------------------------
    J37LookAndFeel::ThemeChoice themeChoice = J37LookAndFeel::ThemeChoice::ivory;
    bool darkTheme = false;   // true for charcoal AND metal; see the note above

    // Transport presentation. `lastShownTransport` is the parameter index the
    // buttons were last painted for, so the three of them are only restyled when
    // the state actually changes; `lastShownSpindown` does the same for the
    // momentary button's lamp. `smoothedMachineSpeed` follows
    // getTransportRamp() with a display-only lag, which is what drives the reels.
    int lastShownTransport = -1;
    bool lastShownSpindown = false;
    juce::String lastShownMachineState;
    float smoothedMachineSpeed = 1.0f;

    // Which transport state is live, read from the parameter rather than tracked
    // locally, so an automation lane moving it repaints the panel.
    int currentTransportState() const;
    void styleTransportButtons();
    void styleSpindownButton();
    void refreshModeButtonCaption();

    // Where the deck's drifting particles are allowed to be, in HORIZONTAL and
    // VERTICAL bands. resized() writes them from the gap the switches row
    // actually left rather than from a pair of hard-coded coordinates, because
    // a coordinate is only correct at the width it was chosen for: the row
    // spreads with the panel, so the free space moves, and a fixed corridor
    // either clips the particles short or puts them on top of a control. An
    // empty horizontal range means there is nowhere free to put them and
    // paint() draws none.
    juce::Range<int> deckParticleCorridorX;
    juce::Range<int> deckParticleCorridorY;

    // Reads a BOOL parameter's current value. Written as a helper because the
    // obvious spelling does not compile: getParameterAsValue returns a
    // juce::var, not an optional, so it has no hasValue() and its getValue()
    // returns another var rather than a bool. Every reader wants the same
    // three things - the parameter exists, it is a bool, and it is on - and
    // answering that in one place is what keeps the mode button and the
    // attachment from disagreeing about the machine's state.
    bool isModeOn (const juce::String& parameterID) const
    {
        // getParameterAsValue hands back a juce::Value BY VALUE, and Value has
        // no conversion to var: its accessor is getValue(), which returns a var
        // by value. The local keeps that Value alive for the read.
        const auto value = audioProcessor.parameters.getParameterAsValue (parameterID);
        const auto underlying = value.getValue();

        return underlying.isBool() && static_cast<bool> (underlying);
    }

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

    // -----------------------------------------------------------------------
    //  Interface sounds.
    //
    //  A plugin's editor is a piece of hardware as far as the user is concerned,
    //  and hardware answers when you touch it: a switch clicks, a knob's detent
    //  ticks, a key thumps. These are synthesised here rather than loaded as
    //  samples, for three reasons - no asset to ship or lose, no file I/O at
    //  editor construction, and a palette that can be shaped by code (the pitch
    //  of every sound rises with the machine's own activity, so the panel sounds
    //  like it is doing work rather than playing a fixed beep).
    //
    //  The player is deliberately SELF-CONTAINED: its own audio device, its own
    //  short buffer, its own timer. It never touches the plugin's audio thread,
    //  never goes through the host's output and therefore never appears in the
    //  rendered file or in the DAW's metering. That is a hard requirement, not a
    //  nicety - an interface click that leaked into the render would be a bug.
    //
    //  It is OFF by default. A plugin that starts making noise the moment a
    //  window opens is a plugin that gets uninstalled, and a user working at
    //  3 a.m. does not want their interface ticking. The switch is on the
    //  SETTINGS tab next to GL.
    // -----------------------------------------------------------------------
    class UiSoundEngine final : private juce::AudioIODeviceCallback
    {
    public:
        UiSoundEngine();
        ~UiSoundEngine() override;

        /** One of the four interface sounds. Each is a different synthesis
            rather than the same beep at a different pitch. */
        enum class Voice
        {
            click,    ///< a switch: a short, dry, bright tick
            detent,   ///< a knob crossing a step: a softer, lower tick
            press,    ///< a momentary key going down: a low thump
            release   ///< ...and coming back up: a slightly higher thump
        };

        void setEnabled (bool shouldBeEnabled) noexcept { enabled = shouldBeEnabled; }
        bool isEnabled() const noexcept { return enabled; }

        /** Sets the brightness/pitch scale, 0..1. The panel feeds it the machine's
            own activity, so a busy machine ticks at a slightly higher pitch. */
        void setBrightness (float newBrightness) noexcept
        {
            brightness = juce::jlimit (0.0f, 1.0f, newBrightness);
        }

        /** Plays a voice. Safe to call from the message thread at any rate. */
        void trigger (Voice voice) noexcept;

    private:
        void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                               int numInputChannels,
                                               float* const* outputChannelData,
                                               int numOutputChannels,
                                               int numSamples,
                                               const juce::AudioIODeviceCallbackContext& context) override;
        void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
        void audioDeviceStopped() override;

        // The current voice's state. The render is a simple two-stage envelope
        // over a sine and a noise burst, which is enough for a tick and cheap
        // enough to be plainly not worth a fancier synthesiser.
        std::atomic<float> pendingAmplitude { 0.0f };
        std::atomic<float> pendingPitch { 440.0f };
        std::atomic<float> pendingDecay { 0.9995f };
        std::atomic<float> pendingNoiseMix { 0.5f };

        float phase = 0.0f;
        float envelope = 0.0f;
        float noiseState = 0.0f;
        float decay = 0.9995f;
        float noiseMix = 0.5f;
        double deviceRate = 44100.0;

        std::atomic<bool> enabled { false };
        std::atomic<float> brightness { 0.5f };

        juce::AudioDeviceManager deviceManager;
        bool deviceOpen = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UiSoundEngine)
    };

    UiSoundEngine uiSounds;
    bool lastShownUiSoundEnabled = false;

    // The knob-detent gate. onValueChange fires on every real value change, and a
    // fast drag produces dozens a second - which would be a buzz rather than a
    // detent. The timer decrements this once per frame, so the gate is a number
    // of FRAMES rather than a wall-clock time and it costs nothing to check.
    int uiSoundTickCountdown = 0;
    static constexpr int uiSoundTickGateFrames = 3;

    /** Attaches the click/detent sounds to every control that should have them.
        Called once from the constructor, after the controls exist. */
    void attachInterfaceSounds();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FirstAudioProcessorEditor)
};
