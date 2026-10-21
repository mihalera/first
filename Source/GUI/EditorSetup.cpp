// =============================================================================
//  The editor's build: the 23 phases the constructor calls, in the order it calls
//  them, plus the decorative physics they start.
//
//  Moved out of PluginEditor.cpp - prose and all, by matching each definition's
//  exact text - because the file's other half is the panel's LIFETIME: the
//  constructor, the timer's tick, the layout arithmetic and the handful of
//  commands the panel sends back to the processor. What the panel is BUILT from is
//  this file, and reading it no longer means scrolling through resized().
//
//  The rule the phases were written under travels with them and is repeated in
//  PluginEditor.h: a phase may rely on the phases BEFORE it having run and on
//  nothing else. tests/layout/extract.py still holds both ends of that promise -
//  the header's declared order and the constructor's call sequence - and now reads
//  them out of the editor's own source across every file it is split into.
// =============================================================================

// The editor's own header: the class, the members and the palette type.
#include "PluginEditor.h"

// The panel vocabulary this file reads through, from the headers that hold
// it - the paint kit, the page table and the knob table.
#include "GUI/PanelPaint.h"
#include "GUI/PanelTabs.h"
#include "GUI/PanelText.h"
#include "GUI/PanelTranslations.h"

// The renderer probe setUpOpenGlRendering() reads. It used to reach this file
// through PluginEditor.cpp's include of it, which stopped being enough the day
// this phase moved here: a translation unit includes what its own text names.
#include "GUI/RenderBackend.h"

// Only the names this file's own text uses: a header member that is not
// reachable from here should not look reachable.
using j37panel::controlSpecs;
using j37panel::maxStageDb;
using j37panel::minStageDb;
using j37panel::paletteFor;
using j37panel::parameterTooltipText;
using j37panel::principleOf;
using j37panel::tabSpecs;

#include <vector>

void FirstAudioProcessorEditor::setUpWindowAndSize() {
    setResizable(true, true);

    // The editor is deliberately sized in logical units. JUCE and the host apply
    // display scaling when mapping component coordinates to physical pixels, so
    // the layout below works entirely in logical bounds.
    //
    // Sizing has TWO independent jobs, and confusing them is what left the editor unable
    // to fit on a laptop screen:
    //
    //   1. The FLOOR is the layout's own minimum, derived from the fixed furniture in
    //      getEditorLayout() and the six bands of controls in the deck. The deck needs
    //      752 px of width, which is 780 px of window once the 14 px outer padding is
    //      added; vertically it is the header, the deck and the space the knob grid
    //      needs for its three rows.
    //
    //   2. The OPENING size is the preferred size CLAMPED TO THE SCREEN IT OPENS ON.
    //      A fixed size fits a 27" monitor and nothing smaller, and the host is
    //      under no obligation to shrink the window for us. So the display the editor
    //      lands on decides: the preferred size when there is room, and the work area
    //      minus a frame allowance when there is not, centred so the whole panel is
    //      reachable instead of hanging off the bottom edge.
    //
    //   3. The FLOOR IS ALSO CLAMPED TO THE SCREEN, which is the part that was
    //      missing and the reason the window stopped fitting. Clamping the OPENING
    //      size was never enough: setResizeLimits is what the host reads when IT
    //      sizes the window, and what the user cannot drag below afterwards. So a
    //      768 px laptop - a work area of roughly 1366 x 728 - was handed a floor
    //      of 816, and a window that could not be made small enough to live on the
    //      screen no matter which of the two rules was applied. A window you cannot
    //      see is worse than a tight one, so the floor yields: below the layout's
    //      own minimum the panel gets cramped, which it is written to survive, and
    //      above it nothing changes at all.
    // The height is 664 because the layout adds up to exactly that: 28 of outer
    // padding, the 84 px header, an 8 px gap, the deck at its 236 px minimum,
    // another 8 px gap, and the control grid's own 300. It was 772, and the
    // hundred and eight came almost entirely from the deck refusing to be
    // shorter than its preferred 278 - which is what left a 1366x768 laptop,
    // with a work area of about 728 and 664 usable after the frame allowance,
    // unable to open the window at all. Every one of those terms is now a real
    // minimum something needs rather than a number that was written once.
    constexpr int minEditorWidth = 780;
    constexpr int minEditorHeight = 664;
    constexpr int preferredEditorWidth = 1060;
    constexpr int preferredEditorHeight = 916;

    // The horizontal margin keeps the frame off the edge of the display. The vertical
    // one covers a decorated window's title bar and border, which sit OUTSIDE the size
    // requested here - on Windows and GNOME that is another 30-45 px of screen the
    // work area has to have room for, and over-reserving it is how a window that
    // "fits" ends up smaller than it needs to be. It was 48 a side, which spent 96 px
    // of a 728 px work area on margins the frame does not actually use.
    constexpr int screenMarginX = 16;
    constexpr int screenMarginY = 32;

    const auto &displays = juce::Desktop::getInstance().getDisplays();
    juce::Rectangle<int> workArea;

    // The display under the mouse is the one the user is looking at, and the host will
    // almost certainly put the editor near it. The other two cases are the mouse sitting
    // outside every known display (a KVM switch, a locked session) and a headless build
    // with no displays at all - in both of them there is nothing sensible to clamp to, so
    // the preferred size is used unchanged.
    // The Point<float> overload: the Point<int> one is deprecated, and Clang says so on
    // every macOS build. Same screen, same result - only the coordinate type differs.
    if (const auto *display =
            displays.getDisplayForPoint(juce::Desktop::getMousePosition().toFloat()))
        workArea = display->userBounds.toNearestInt();
    else if (const auto *primary = displays.getPrimaryDisplay())
        workArea = primary->userBounds.toNearestInt();
    else
        workArea = displays.getTotalBounds(true);

    if (workArea.isEmpty()) {
        setResizeLimits(minEditorWidth, minEditorHeight, 1500, 1180);
        setSize(preferredEditorWidth, preferredEditorHeight);
    } else {
        // toNearestInt() above already rounded, so subtracting the margin afterwards
        // cannot leave the window half a pixel - or a whole one - over the edge.
        const auto fittingWidth = workArea.getWidth() - 2 * screenMarginX;
        const auto fittingHeight = workArea.getHeight() - 2 * screenMarginY;

        // The floor this screen can actually honour, and the size it opens at. Both
        // read from the same two numbers, so the window cannot open at a size its
        // own limits would refuse - the version of this that clamped only the
        // opening size and left the floor alone was the bug, not a near miss of it.
        const auto floorWidth = juce::jmax(1, juce::jmin(minEditorWidth, fittingWidth));
        const auto floorHeight = juce::jmax(1, juce::jmin(minEditorHeight, fittingHeight));
        const auto openingWidth =
            juce::jlimit(floorWidth, juce::jmax(floorWidth, fittingWidth), preferredEditorWidth);
        const auto openingHeight = juce::jlimit(floorHeight, juce::jmax(floorHeight, fittingHeight),
                                                preferredEditorHeight);

        setResizeLimits(floorWidth, floorHeight, 1500, 1180);
        setSize(openingWidth, openingHeight);

        // Centred only when the screen forced a smaller window. At the preferred size the
        // host decides where the editor goes, and re-centring it here would fight the
        // DAW's own window management.
        if (openingWidth != preferredEditorWidth || openingHeight != preferredEditorHeight)
            centreWithSize(openingWidth, openingHeight);
    }
}

void FirstAudioProcessorEditor::createDebugInspector() {
#if JUCE_DEBUG
    // The Melatonin component inspector, debug builds only. It is created after
    // the whole panel exists so it can walk the finished component tree; it
    // opens lazily and stays invisible unless asked for.
    inspector = std::make_unique<melatonin::Inspector>(*this, false);
#endif
}

void FirstAudioProcessorEditor::setUpLanguageAndTuning() {
    // ---------------------------------------------------------------------
    //  The language selector.
    //
    //  A ComboBox bound straight to the "language" parameter, so choosing a
    //  language is an ordinary parameter change: it is saved with the session,
    //  restored with it, and undone with everything else. There is no separate
    //  preferences file to keep in step with the host's idea of the project.
    //
    //  The listener is what makes the change take effect immediately. The
    //  parameter is what makes it survive a restart; the two are different jobs
    //  and either alone would be wrong - a parameter with no listener would
    //  store the choice and never act on it, and a listener with no parameter
    //  would apply it and forget it.
    // ---------------------------------------------------------------------
    languageBox.addItem(xlat("LANGUAGE_ENGLISH"), 1);
    languageBox.addItem(xlat("LANGUAGE_UKRAINIAN"), 2);
    languageBox.setLookAndFeel(&customLookAndFeel);
    languageLabel.setVisible(false);
    languageBox.setVisible(false);

    languageAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.parameters, "language", languageBox);

    // The stored value arrives AFTER the constructor on a session load, so the
    // language is applied here from the parameter rather than assumed to be
    // English - otherwise a session saved in Ukrainian would open with
    // Ukrainian in the parameter and English on screen.
    if (auto *languageParameter = dynamic_cast<juce::AudioParameterChoice *>(
            audioProcessor.parameters.getParameter("language"))) {
        applyLanguage(languageParameter->getIndex());
    } else {
        applyLanguage(0);
    }

    languageBox.onChange = [this] {
        if (auto *languageParam = dynamic_cast<juce::AudioParameterChoice *>(
                audioProcessor.parameters.getParameter("language")))
            applyLanguage(languageParam->getIndex());
    };

    // The caption is an all-caps engraving like every other caption on the panel,
    // while the keyed string carries ordinary words ("Language"); the case is
    // applied here rather than in the table, which keeps the lookup key stable
    // and the translation readable.
    styleLabel(languageLabel, xlat("LANGUAGE_CAPTION").toUpperCase(), 10.0f,
               paletteFor(false).secondary, true, juce::Justification::left);
    addAndMakeVisible(languageLabel);
    addAndMakeVisible(languageBox);

    // AUTOTUNE's key and scale select the correction grid; the correction itself
    // remains opt-in and its amount continuously automatable.
    tuningTonicBox.addItemList(
        juce::StringArray{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}, 1);
    tuningModeBox.addItemList(juce::StringArray{"Chromatic", "Major", "Minor", "Dorian",
                                                "Pentatonic Major", "Pentatonic Minor"},
                              1);
    tuningTonicBox.setLookAndFeel(&inlineLookAndFeel);
    tuningModeBox.setLookAndFeel(&inlineLookAndFeel);
    tuningTonicAttachment =
        std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            audioProcessor.parameters, "tuning_tonic", tuningTonicBox);
    tuningModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.parameters, "tuning_mode", tuningModeBox);
    autoTuneButton.setLookAndFeel(&inlineLookAndFeel);
    autoTuneAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.parameters, "autotune", autoTuneButton);
    // A rotary, laid out by the knob strip like every other page's controls - see
    // the AUTOTUNE branch in resized(). As a LinearHorizontal bar this was the
    // one control on the panel the toolkit drew for itself (J37LookAndFeel
    // overrides the rotary, the switch and the toggle, and inherits JUCE's stock
    // slider art), and the band it rode was 30 px tall: the toolkit kept a 15 px
    // value box out of that and handed the art a paint area three pixels deep, so
    // the page's only knob was a two-pixel bead beside a stock grey track.
    autoTuneAmountSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    // 16 px for the readout, as on the deck's knobs - see the note there.
    autoTuneAmountSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 78, 16);
    autoTuneAmountSlider.setRange(0.0, 1.0, 0.001);
    // The readout reads "50 %", the way the panel's other fraction knobs read (see
    // the transient shaper's two amounts, the same pair of functions): the
    // parameter IS a fraction and its unit IS percent, so the toolkit's own
    // 0.500-with-a-percent-sign was the wrong number WITH a percent sign after
    // it. The unit rides inside the text function rather than in
    // setTextValueSuffix, so it cannot be written twice.
    autoTuneAmountSlider.textFromValueFunction = [](double value) {
        return juce::String(juce::roundToInt(value * 100.0)) + " %";
    };
    autoTuneAmountSlider.valueFromTextFunction = [](const juce::String &text) {
        return juce::jlimit(0.0, 1.0, text.getDoubleValue() / 100.0);
    };
    autoTuneAmountSlider.setLookAndFeel(&customLookAndFeel);
    autoTuneAmountAttachment =
        std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.parameters, "autotune_amount", autoTuneAmountSlider);
    styleLabel(tuningTonicLabel, "TONIC", 9.0f, paletteFor(false).secondary, true,
               juce::Justification::left);
    styleLabel(tuningModeLabel, "MODE", 9.0f, paletteFor(false).secondary, true,
               juce::Justification::left);
    // Centred, and click-through, because this caption is now a knob page's - the
    // strip places it in the tile's caption band like every other page's label,
    // and a left-aligned caption over a centred knob reads as a stray word.
    styleLabel(autoTuneAmountLabel, "TUNE AMOUNT", 9.0f, paletteFor(false).secondary, true,
               juce::Justification::centred);
    autoTuneAmountLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(tuningTonicLabel);
    addAndMakeVisible(tuningModeLabel);
    addAndMakeVisible(tuningTonicBox);
    addAndMakeVisible(tuningModeBox);
    addAndMakeVisible(autoTuneButton);
    addAndMakeVisible(autoTuneAmountLabel);
    addAndMakeVisible(autoTuneAmountSlider);

    // The four tuning controls are the one group on the panel that had no
    // tooltips at all, which is why the page read as unexplained: two of them
    // (TONIC, MODE) only mean something together, and the corrector's own two
    // are the pair a user has to understand before turning either on. Keyed on
    // the English sentence like every other tip, so a translation is one line in
    // the table.
    setTip(tuningTonicBox, "TONIC - the key the scale is built on: the note the "
                           "corrector treats as home when MODE is anything but "
                           "CHROMATIC. On CHROMATIC it changes nothing, because "
                           "every semitone is already in the scale. Default C.");
    setTip(tuningModeBox, "MODE - which notes the corrector is allowed to land on. "
                          "CHROMATIC is every semitone and is the default: a note is "
                          "pulled to the nearest one and nowhere else. MAJOR, MINOR, "
                          "DORIAN and the two pentatonics let a reading travel up to "
                          "a semitone to the nearest note the chosen scale contains, "
                          "so the correction follows the key rather than the grid. "
                          "Default Chromatic.");
    setTip(autoTuneButton, "AUTO TUNE - switches the corrector in. The stage reads "
                           "the note that is playing with a period detector, decides "
                           "the nearest note the key allows, and moves the audio onto "
                           "it by RE-READING the signal at the correction ratio out "
                           "of a delay line, spliced a whole period apart so the join "
                           "is inaudible. Below its gate - silence, noise, a chord, or "
                           "a note outside 45 Hz to 1.4 kHz - it glides back to unity "
                           "and passes the signal untouched rather than chasing a "
                           "number it cannot trust. While it runs the stage adds a "
                           "period of the tracked note as delay, and at full TUNE "
                           "AMOUNT the correction is a snap. Default off.");
    setTip(autoTuneAmountSlider, "TUNE AMOUNT - how far and how fast the correction is "
                                 "allowed to travel: half the amount is half the pull "
                                 "at about twice the time constant, which is the "
                                 "difference between a note being nudged and a note "
                                 "being snapped onto the grid. At full amount a note "
                                 "lands within a few milliseconds; at 0 the stage is "
                                 "out of the path entirely, detector included, and "
                                 "the signal is bit-for-bit the one that arrived.");
}

void FirstAudioProcessorEditor::setUpTooltipsAndPhysics() {
    // Tooltips use DwellTooltipWindow's single 350 ms dwell on every control change;
    // the base class delay is zero to avoid stacking a second timeout.
    tooltipWindow->setMillisecondsBeforeTipAppears(0);
    createDecorativePhysics();
}

void FirstAudioProcessorEditor::stylePanelLabels() {
    styleLabel(brandLabel, "MASTER MACHINE", 9.0f, paletteFor(false).accent, true,
               juce::Justification::left);
    titleLabel.setText("MiBoX", juce::dontSendNotification);

    // Serif display face with a cross-platform fallback chain. "Georgia" only exists on
    // Windows, so naming it alone made the title fall back to an arbitrary face (and an
    // unpredictable width) on macOS and Linux. FontOptions accepts a comma-separated
    // list and uses the first family that resolves, ending in a generic fallback.
    titleLabel.setFont(juce::Font(
        juce::FontOptions("Georgia, Times New Roman, Times, serif", 30.0f, juce::Font::bold)));
    titleLabel.setJustificationType(juce::Justification::left);
    titleLabel.setInterceptsMouseClicks(false, false);
    styleLabel(subtitleLabel, "NONLINEAR ANALOG / MIX BOX", 10.0f, paletteFor(false).secondary,
               true, juce::Justification::left);
    styleLabel(statusLabel, "STEREO / REAL TIME", 9.0f, paletteFor(false).status, true,
               juce::Justification::centred);
    styleLabel(deckHeadingLabel, "Deck", 10.0f, paletteFor(false).accent, true,
               juce::Justification::left);

    // Build identity, printed on the deck's heading strip. CMake resolves
    // J37_BUILD_COMMIT from `git rev-parse --short=8 HEAD` at configure time and marks
    // a dirty tree with a trailing "+"; when the sources are built outside a git
    // checkout the definition still exists and reads "unknown", so this line can never
    // be empty and the two cases are always distinguishable at a glance.
#ifdef J37_BUILD_COMMIT
    const juce::String buildId(J37_BUILD_COMMIT);
#else
    const juce::String buildId("unknown");
#endif
    styleLabel(buildLabel, "BUILD " + buildId, 8.0f, paletteFor(false).secondary, true,
               juce::Justification::centredRight);
    styleLabel(tapeTypeLabel, "TAPE", 9.0f, paletteFor(false).secondary, true,
               juce::Justification::left);
    styleLabel(valveTypeLabel, "VALVE", 9.0f, paletteFor(false).secondary, true,
               juce::Justification::left);
    styleLabel(ampTypeLabel, "AMP", 9.0f, paletteFor(false).secondary, true,
               juce::Justification::left);
    styleLabel(transformerTypeLabel, "XFMR", 9.0f, paletteFor(false).secondary, true,
               juce::Justification::left);
    styleLabel(digitalTypeLabel, "DIGITAL", 9.0f, paletteFor(false).secondary, true,
               juce::Justification::left);
    styleLabel(vinylTypeLabel, "VINYL", 9.0f, paletteFor(false).secondary, true,
               juce::Justification::left);
    // The second selector of the six-type row is the turntable's motor speed; it
    // had a layout slot and a combo but was never given a caption, so "33 RPM"
    // sat under an empty label. RPM is what the box itself shows (33 / 45 / 78).
    styleLabel(vinylSpeedLabel, "RPM", 9.0f, paletteFor(false).secondary, true,
               juce::Justification::left);
    styleLabel(speedLabel, "SPEED", 9.0f, paletteFor(false).secondary, true,
               juce::Justification::left);
    styleLabel(deckHintLabel, "PLAY / AT SPEED", 9.0f, paletteFor(false).secondary, false,
               juce::Justification::centredLeft);
    styleLabel(controlsHeadingLabel, "TABS", 10.0f, paletteFor(false).accent, true,
               juce::Justification::left);
    // The section caption. Its text is not set here: setCurrentTab writes the
    // active page's own subject into it on every page change, and the constructor
    // calls setCurrentTab before the first paint, so anything set now would be
    // overwritten a few lines later. It is styled here only.
    styleLabel(sectionCaptionLabel, "-", 9.0f, paletteFor(false).secondary, false,
               juce::Justification::left);
    styleLabel(controlsHintLabel, "Shift = fine tune", 9.0f, paletteFor(false).secondary, false,
               juce::Justification::right);
    styleLabel(bpmLabel, "BPM", 9.0f, paletteFor(false).secondary, true, juce::Justification::left);
    styleLabel(bpmReadout, xlat("-"), 9.0f, paletteFor(false).secondary, false,
               juce::Justification::centredLeft);
    styleLabel(metersHeadingLabel, "LEVELS", 10.0f, paletteFor(false).accent, true,
               juce::Justification::left);
    styleLabel(metersHintLabel, "dBFS / PEAK + RMS", 8.0f, paletteFor(false).secondary, true,
               juce::Justification::left);
    styleLabel(compressorLabel, "GLUE x2", 10.0f, paletteFor(false).accent, true,
               juce::Justification::right);
    styleLabel(compressorReadout, "one meter per stage", 8.0f, paletteFor(false).secondary, false,
               juce::Justification::right);
    // The three readout pairs share one alignment discipline: the caption is
    // centred over its value, and every pair uses the SAME scheme. BPM read
    // left while these three read right, so a caption sat over the right half
    // of its number while BPM's sat over nothing - the four looked staggered
    // against each other even though each pair was internally consistent.
    styleLabel(harmonicsLabel, "HARMONICS", 10.0f, paletteFor(false).accent, true,
               juce::Justification::centredLeft);
    styleLabel(harmonicsReadout, xlat("even / odd"), 8.0f, paletteFor(false).secondary, false,
               juce::Justification::centredLeft);
    styleLabel(subfundLabel, "SUBFUND TRACK", 10.0f, paletteFor(false).accent, true,
               juce::Justification::centredLeft);
    styleLabel(subfundReadout, xlat("idle"), 8.0f, paletteFor(false).secondary, false,
               juce::Justification::centredLeft);
    styleLabel(antiPhaseLabel, "ANTI-PHASE", 10.0f, paletteFor(false).accent, true,
               juce::Justification::centredLeft);
    styleLabel(antiPhaseReadout, xlat("clean"), 8.0f, paletteFor(false).secondary, false,
               juce::Justification::centredLeft);
    addAndMakeVisible(antiPhaseLabel);
    addAndMakeVisible(antiPhaseReadout);

    addAndMakeVisible(brandLabel);
    addAndMakeVisible(titleLabel);
    addAndMakeVisible(subtitleLabel);
    addAndMakeVisible(statusLabel);
    addAndMakeVisible(deckHeadingLabel);
    addAndMakeVisible(buildLabel);
    addAndMakeVisible(tapeTypeLabel);
    addAndMakeVisible(valveTypeLabel);
    addAndMakeVisible(ampTypeLabel);
    addAndMakeVisible(transformerTypeLabel);
    addAndMakeVisible(digitalTypeLabel);
    addAndMakeVisible(vinylTypeLabel);
    addAndMakeVisible(vinylSpeedLabel);
    addAndMakeVisible(speedLabel);
    addAndMakeVisible(deckHintLabel);
    addAndMakeVisible(controlsHeadingLabel);
    addAndMakeVisible(sectionCaptionLabel);
    addAndMakeVisible(controlsHintLabel);
    // The MIX page's bar rides the member band, which resized() sizes and
    // positions; adding it visible here is what lets the first paint see it.
    addAndMakeVisible(mixBar);
    addAndMakeVisible(metersHeadingLabel);
    addAndMakeVisible(metersHintLabel);
    addAndMakeVisible(compressorLabel);
    addAndMakeVisible(compressorReadout);
    addAndMakeVisible(harmonicsLabel);
    addAndMakeVisible(harmonicsReadout);
    addAndMakeVisible(bpmLabel);
    addAndMakeVisible(bpmReadout);
    addAndMakeVisible(subfundLabel);
    addAndMakeVisible(subfundReadout);
}

void FirstAudioProcessorEditor::setUpControlStrip() {
    // The three arrays the strip below works from, DERIVED from the table above
    // so they cannot disagree with it. Each is sized by controlCount, so a
    // table that is too short or too long is a compile error rather than a silent
    // out-of-bounds read.
    juce::StringArray controlIds;
    juce::StringArray controlNames;
    std::array<double, controlCount> defaultValues{};

    for (std::size_t i = 0; i < controlCount; ++i) {
        controlIds.add(controlSpecs[i].id);
        controlNames.add(controlSpecs[i].caption);
        defaultValues[i] = controlSpecs[i].defaultValue;
    }

    // Cross-check the table's `tab` column against the tabSpecs table below.
    //
    // The two tables describe the same partition from opposite ends - one says
    // "which tab does this knob belong to", the other "which knobs does this tab
    // hold" - and without this check they could disagree silently: a knob could
    // name DYNAMICS in this table and be listed under NOISE in tabSpecs, and the
    // panel would simply draw it on the tab tabSpecs chose while the comment here
    // said otherwise. The rebuild still works (tabSpecs is what the grid reads),
    // which is exactly why nothing else would catch it.
    //
    // This runs once, at construction, in a debug build only - the same discipline
    // the parameter/preset audit follows. It is the one place both tables are in
    // scope: controlSpecs is a file-scope table and tabSpecs is a namespace
    // constant, so a static_assert cannot span them.
    for (std::size_t i = 0; i < controlCount; ++i) {
        const auto *declaredTab = controlSpecs[i].tab;
        const auto listedTab = [&]() -> const char * {
            for (const auto &spec : tabSpecs)
                for (std::size_t c = 0; c < spec.count; ++c)
                    if (spec.controls[c] == i)
                        return spec.name;

            return nullptr;
        }();

        jassert(listedTab != nullptr);
        jassert(listedTab != nullptr && std::strcmp(declaredTab, listedTab) == 0);

        // jassert compiles away in a release build, which would leave both
        // locals unused - and the project builds with -Wall -Werror-grade warning
        // flags. Naming them as deliberately-ignored keeps the check a
        // debug-only diagnostic without turning release into a warning.
        juce::ignoreUnused(declaredTab, listedTab);
    }

    for (std::size_t i = 0; i < controlCount; ++i) {
        auto &slider = controls[i];
        slider.setName(controlNames[static_cast<int>(i)]);
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setRotaryParameters(juce::MathConstants<float>::pi * 0.75f,
                                   juce::MathConstants<float>::pi * 2.25f, true);
        // The readout band under the knob is 16 px, not the 18 it used to be, for
        // the reason the knob's own rectangle is measured from: a knob draws in
        // the space the slider has left after its readout, so two pixels off every
        // cell's readout is two pixels onto every knob on every page - and on the
        // pages whose rows are short (MACHINE holds seven knobs in two rows) that
        // is the difference between a scale the ticks have room for and one they
        // do not. The readout itself is unaffected: the font it holds is smaller
        // than the band either way.
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 16);
        slider.setTextBoxIsEditable(true);
        // Drag response tuned by ear: velocity-based mode throttled the first pixels
        // of every drag behind an acceleration ramp, which read as "slow knobs".
        // Direct movement plus a high sensitivity gives a full 0..1 sweep in about a
        // third of the panel width; Shift still engages JUCE's low-velocity mode for
        // fine control.
        slider.setMouseDragSensitivity(1400);
        slider.setVelocityBasedMode(true);
        slider.setVelocityModeParameters(1.0, 2, 0.06, true, juce::ModifierKeys::shiftModifier);
        slider.setPopupDisplayEnabled(true, true, this);
        slider.setScrollWheelEnabled(true);
        slider.setDoubleClickReturnValue(true, defaultValues[i]);
        slider.setLookAndFeel(&customLookAndFeel);
        slider.setColour(juce::Slider::textBoxOutlineColourId, paletteFor(false).border);

        // A SOURCE knob is registered with the LookAndFeel as that machine's own
        // control, so drawRotarySlider can paint its arc, its tracer and its
        // indicator in that machine's colour without knowing anything about the
        // control list. Every other knob is simply never registered and keeps
        // the panel's accent.
        const auto principle = principleOf(controlIds[static_cast<int>(i)]);
        if (principle >= 0)
            customLookAndFeel.registerPrincipleKnob(slider, principle);

        setTippedSentence(slider, parameterTooltipText(controlIds[static_cast<int>(i)]),
                          " Hold Shift for fine control, mouse wheel for small "
                          "steps, double-click to reset.");

        // Index 8 is OUTPUT, not 7: the control order is INPUT, DRIVE, BIAS, BRIGHT,
        // TONE, WOW, FLUTTER, MIX, OUTPUT, WIDTH, SUBFUND, DELAY, DLY LVL, ST OFFSET,
        // NOISE. The old check (i == 7) handed MIX the -32..+32 dB range and suffix -
        // which is why the Mix knob displayed dB - and left OUTPUT stuck in the
        // 0..1 percentage branch, where its slider range clipped the real +/-32 dB
        // parameter down to 0..1.
        //
        // DELAY (11) and ST OFFSET (13) are the other two exceptions: they are real
        // units (milliseconds and microseconds), not percentages, so a percentage
        // range would make the knob unable to reach either end of its own parameter.
        // The range is chosen by the control's ID, not by its position in the array.
        // It used to be `i == 11` and `i == 13`, which was correct only while the list
        // happened to be ordered that way - and when the list grew, index 11 became
        // SHAPE and index 13 became SAG, so the DELAY knob silently acquired a
        // percentage range and could no longer reach 250 ms, and ST OFFSET lost its
        // bipolar range entirely. An id cannot be renumbered by an unrelated edit.
        const auto &id = controlIds[static_cast<int>(i)];

        if (id == "input" || id == "output") {
            // Input and Output are both calibrated decibel trims over the same range.
            slider.setRange(minStageDb, maxStageDb, 0.1);
            slider.setNumDecimalPlacesToDisplay(1);
            slider.setTextValueSuffix(" dB");
        } else if (id == "delay_time") {
            // DELAY: the head spacing, in milliseconds.
            slider.setRange(0.0, 250.0, 0.1);
            slider.setNumDecimalPlacesToDisplay(1);
            slider.setTextValueSuffix(" ms");
        } else if (id == "st_offset") {
            // ST OFFSET: the inter-channel time offset, in microseconds.
            slider.setRange(-500.0, 500.0, 1.0);
            slider.setNumDecimalPlacesToDisplay(0);
            slider.setTextValueSuffix(" us");
        } else if (id == "in_low" || id == "in_mid" || id == "in_high" || id == "out_low" ||
                   id == "out_mid" || id == "out_high") {
            // The two equalisers' six bands: real dB, centred on 0, and with a
            // NEUTRAL default rather than a percentage one. They are deliberately
            // NOT given the percentage text functions below - an EQ reads in dB,
            // and a band showing "50 %" for its flat position would be a control
            // whose readout does not say what it is.
            slider.setRange(-12.0, 12.0, 0.1);
            slider.setNumDecimalPlacesToDisplay(1);
            slider.setTextValueSuffix(" dB");

            // A bipolar EQ wants its NEUTRAL position in the middle of the
            // travel, so a double-click returns to 0 rather than to one end.
            // The defaultValues table already holds 0.0 for these six; this is
            // the readout to match.
            slider.textFromValueFunction = [](double value) {
                return juce::String(value, 1) + " dB";
            };
            slider.valueFromTextFunction = [](const juce::String &text) {
                return juce::jlimit(-12.0, 12.0, text.getDoubleValue());
            };
        } else if (id == "transient_attack" || id == "transient_sustain") {
            // The transient shaper's two amounts are BIPOLAR and centred on 0:
            // negative softens/shortens, positive sharpens/lengthens, and 0 is
            // the neutral that leaves the stage transparent. The range is the
            // parameter's own (-1..1), shown as a signed percentage so the two
            // halves of the control read the same way they behave.
            slider.setRange(-1.0, 1.0, 0.001);
            slider.setNumDecimalPlacesToDisplay(0);
            slider.textFromValueFunction = [](double value) {
                return juce::String(juce::roundToInt(value * 100.0)) + " %";
            };
            slider.valueFromTextFunction = [](const juce::String &text) {
                return juce::jlimit(-1.0, 1.0, text.getDoubleValue() / 100.0);
            };
        } else if (id == "in_hp_freq" || id == "in_lp_freq" || id == "out_hp_freq" ||
                   id == "out_lp_freq") {
            // The EQ filters' corners, in Hz. The two ends are the two REAL
            // bypasses: 20 Hz is below anything on a record, and 20 kHz is at or
            // above the top of the band - so the engine treats each end as "no
            // filter" rather than as a very gentle one, which is what makes a
            // neutral EQ bit-for-bit transparent.
            //
            // The high-pass is skewed toward its low end because that is where
            // the useful range is (a rumble filter lives between 20 and 120 Hz),
            // and the low-pass toward its middle for the same reason.
            if (id == "in_hp_freq" || id == "out_hp_freq") {
                slider.setSkewFactorFromMidPoint(100.0);
                slider.setRange(20.0, 500.0, 1.0);
            } else {
                slider.setSkewFactorFromMidPoint(8000.0);
                slider.setRange(2000.0, 20000.0, 10.0);
            }

            slider.setNumDecimalPlacesToDisplay(0);
            slider.setTextValueSuffix(" Hz");
        } else if (id == "in_eq_q" || id == "out_eq_q") {
            // The filters' corner resonance. Capped at 1.5: a resonant filter
            // ringing on a tape emulation is a fault rather than a feature, so
            // the control exists to let a steep filter be usable rather than to
            // make it sing.
            slider.setRange(0.5, 1.5, 0.01);
            slider.setNumDecimalPlacesToDisplay(2);
            slider.setTextValueSuffix(" Q");
        } else {
            slider.setRange(0.0, 1.0, 0.001);
            slider.textFromValueFunction = [](double value) {
                return juce::String(juce::roundToInt(value * 100.0)) + " %";
            };
            slider.valueFromTextFunction = [](const juce::String &text) {
                return juce::jlimit(0.0, 1.0, text.getDoubleValue() / 100.0);
            };
        }

        controlLabels[i].setText(controlNames[static_cast<int>(i)], juce::dontSendNotification);
        controlLabels[i].setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
        controlLabels[i].setJustificationType(juce::Justification::centred);
        controlLabels[i].setInterceptsMouseClicks(false, false);

        addAndMakeVisible(controlLabels[i]);
        addAndMakeVisible(slider);
        controlAttachments[i] =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                audioProcessor.parameters, controlIds[static_cast<int>(i)], slider);
    }
}

void FirstAudioProcessorEditor::setUpTabButtons() {
    // The three tabs the knob grid is split across. Only the active tab's knobs are on
    // screen: the other thirteen used to be laid out underneath it, invisible but still
    // sitting in the panel, and crowding the grid is what the captions were there to
    // hide. setCurrentTab() also does the first layout, now with the tab bar and a
    // single tab's grid. (resized() has already run once by now - setSize() above
    // triggers it - so this is an ordinary relayout, not a layout over unset members.)
    for (int tab = 0; tab < numTabs; ++tab) {
        const auto &spec = tabSpecs[static_cast<std::size_t>(tab)];
        auto &button = tabButtons[static_cast<std::size_t>(tab)];

        button.setButtonText(spec.name);
        setTip(button, spec.hint);
        button.setLookAndFeel(&customLookAndFeel);
        button.onClick = [this, tab] { setCurrentTab(tab); };
        addAndMakeVisible(button);
    }

    // The three tab families share one row above the page buttons. The grid
    // below reflows to accommodate them.
    const std::array<juce::TextButton *, tabFamilyCount> familyButtons{
        &tabFamilyTapeButton, &tabFamilyFxButton, &tabFamilySetupButton};
    for (std::size_t f = 0; f < familyButtons.size(); ++f) {
        auto &button = *familyButtons[f];
        button.setLookAndFeel(&customLookAndFeel);
        button.onClick = [this, f] { setTabFamily(static_cast<int>(f)); };
        addAndMakeVisible(button);
    }

    // Every other path - applyLanguage's applyTheme, a knob drag, a preset load -
    // only ever touches styling; the single setTabFamily here is what initially
    // filters the bar and resolves the restored currentTab's family.
    //
    // The FAMILY comes out of the spec table rather than out of currentTab
    // itself: this used to pass the PAGE number straight through, and
    // setTabFamily's jlimit(0, 2) read page 1 (FRONT END, a TAPE page) as family
    // 1 (FX) and page 2 (SATURATION) as family 2 - so a session that opened on
    // any page but the first landed in the wrong family's bar.
    const auto initialTab = juce::jlimit(0, numTabs - 1, currentTab);
    setTabFamily(tabSpecs[static_cast<std::size_t>(initialTab)].family);
}

void FirstAudioProcessorEditor::setUpDeckSelectors() {
    // The same tapeStockNameList() the choice parameter is built from, so the panel
    // cannot offer a stock the parameter would refuse to select. It did once: the
    // panel drew twelve and the parameter accepted eight, so the last four were
    // drawn, implemented and unreachable. The default row is overwritten by the
    // attachment below, which syncs the box to the parameter.
    tapeTypeBox.addItemList(tapeStockNameList(), 1);
    valveTypeBox.addItemList(valveTypeNameList(), 1);
    ampTypeBox.addItemList(ampTypeNameList(), 1);
    transformerTypeBox.addItemList(transformerTypeNameList(), 1);
    digitalTypeBox.addItemList(digitalTypeNameList(), 1);
    vinylTypeBox.addItemList(vinylTypeNameList(), 1);
    vinylSpeedBox.addItemList(juce::StringArray{"33 RPM", "45 RPM", "78 RPM"}, 1);
    speedBox.addItemList(juce::StringArray{"7.5 ips", "15 ips", "30 ips"}, 1);
    tapeTypeBox.setTextWhenNothingSelected("Select tape");
    speedBox.setTextWhenNothingSelected("Select speed");
    setTip(tapeTypeBox, "Tape formula. Each stock bends the sound differently: "
                        "J37 is soft and classic, Ampex and Studer are hotter, "
                        "Chrome and Type 111 are cleaner, GP9 and 499 are dense "
                        "modern formulas, SM911 is the broadcast reference, "
                        "SM 468 is high-output low-noise, 888 is the hot thick "
                        "vintage stock, 815 is dark and dense, 811 is the clean "
                        "open mastering stock. The formula also shapes the glue "
                        "compressors' timing.");
    setTip(speedBox, "Transport speed. 7.5 ips is dark and loose, 15 ips is the "
                     "classic studio speed, 30 ips keeps the most top end and "
                     "the tightest glue. Speed also shapes the glue timing.");
    setTip(valveTypeBox, "VALVE TYPE - the voice of the VALVE principle. Only "
                         "audible in proportion to how much VALVE is in the "
                         "BLEND: a 12AX7 runs cold and tight, an EL34 or 6L6 "
                         "is fatter and more compressed, a 300B is the softest "
                         "and most even, a KT88 has the widest drift.");
    setTip(ampTypeBox, "AMP TYPE - the voice of the AMP principle. Only audible "
                       "in proportion to how much AMP is in the BLEND: the "
                       "Blackface is clean and firm, the Plexi bites, the AC30 "
                       "chimes, the Recto slams hardest in the second stage.");
    setTip(transformerTypeBox, "TRANSFORMER TYPE - the voice of the TRANSFORMER "
                               "principle. Only audible in proportion to how much "
                               "TRANSFORMER is in the BLEND: the types set how "
                               "fast the core tracks and how early it starts to "
                               "saturate - a nickel core bends earlier, steel "
                               "later and harder.");
    setTip(digitalTypeBox, "DIGITAL TYPE - the voice of the DIGITAL principle. "
                           "Only audible in proportion to how much DIGITAL is in "
                           "the BLEND: the types shorten the word length and "
                           "deepen the sample-and-hold, from a 16-bit ceiling to "
                           "full bit-crush.");
    setTip(vinylTypeBox, "VINYL TYPE - the record itself, not the machine: how it "
                         "was pressed and how worn it is. Audible whenever VINYL "
                         "is up, and independent of the BLEND. Shellac 78 plays "
                         "with loud surface between every note, a dubplate is a "
                         "fresh loud lacquer, a half-speed master is nearly "
                         "silent between the grooves. The speed of the motor is "
                         "VINYL SPEED.");
    setTip(vinylSpeedBox, "VINYL SPEED - the speed of the turntable's motor, not "
                          "of the record: the VINYL TYPE describes the disc, this "
                          "describes what drives it. Each speed carries its own "
                          "wow rate and depth, so the same record wanders "
                          "differently at 33 and 45, and a 78's wind-up motor "
                          "wobbles hardest. 33 RPM is the default.");

    // The deck's readout pairs: the numbers change at 30 Hz, so a tooltip on
    // the VALUE would sit under a moving mouse on every frame. The caption
    // carries the explanation instead - it is static, and it is what a user
    // actually points at when reading a column.
    setTip(harmonicsLabel, "HARMONICS - the ratio the harmonic engine settled on: "
                           "E is the even-order share (tape and valve warmth), "
                           "O the odd-order share (transistor edge). The mix "
                           "follows the BLEND knob; this readout shows what the "
                           "blend actually produced.");
    setTip(subfundLabel, "SUBFUND TRACK - the pitch the subharmonic generator is "
                         "currently locked to. It only tracks while the machine "
                         "is fed a clean enough fundamental; 'idle - no note "
                         "tracked' means the input did not give it one.");
    setTip(antiPhaseLabel, "ANTI-PHASE - how much of the stereo pair cancels when "
                           "the channels are summed to mono. 'clean' is safe; a "
                           "percentage warns that a mono fold-down will lose "
                           "bass or body. It watches the OUTPUT side, so output "
                           "stage switches move it too.");
    setTip(bpmLabel, "BPM - the host tempo the delay and modulation sections sync "
                     "to. It follows your DAW's transport; it is a readout here, "
                     "not a setting.");
    setTip(tapeTypeLabel, "TAPE - the formula the machine's saturation is built "
                          "from. Each stock bends harmonics, compression and "
                          "noise differently. The selector below is where it is "
                          "chosen.");
    tapeTypeBox.setLookAndFeel(&customLookAndFeel);
    valveTypeBox.setLookAndFeel(&customLookAndFeel);
    ampTypeBox.setLookAndFeel(&customLookAndFeel);
    transformerTypeBox.setLookAndFeel(&customLookAndFeel);
    digitalTypeBox.setLookAndFeel(&customLookAndFeel);
    vinylTypeBox.setLookAndFeel(&customLookAndFeel);
    vinylSpeedBox.setLookAndFeel(&customLookAndFeel);
    speedBox.setLookAndFeel(&customLookAndFeel);
}

void FirstAudioProcessorEditor::setUpTopSwitches() {
    bypassButton.setClickingTogglesState(true);
    bypassButton.setButtonText("BYPASS");
    bypassButton.setLookAndFeel(&customLookAndFeel);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.parameters, "bypass", bypassButton);

    deltaButton.setClickingTogglesState(true);
    deltaButton.setLookAndFeel(&customLookAndFeel);
    deltaAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.parameters, "delta", deltaButton);
    addAndMakeVisible(deltaButton);

    themeButton.setButtonText(themeChoice == j37panel::ThemeChoice::darkMetal  ? "DARK METAL"
                              : themeChoice == j37panel::ThemeChoice::metal    ? "METAL"
                              : themeChoice == j37panel::ThemeChoice::charcoal ? "CHARCOAL"
                                                                               : "IVORY");
    setTip(themeButton, "Cycle the front panel: IVORY, CHARCOAL, METAL, then DARK METAL. "
                        "The button names the panel you are on; pressing it moves "
                        "to the next one.");
    themeButton.setLookAndFeel(&customLookAndFeel);
    themeButton.onClick = [this] {
        switch (themeChoice) {
        case j37panel::ThemeChoice::ivory:
            themeChoice = j37panel::ThemeChoice::charcoal;
            break;
        case j37panel::ThemeChoice::charcoal:
            themeChoice = j37panel::ThemeChoice::metal;
            break;
        case j37panel::ThemeChoice::metal:
            themeChoice = j37panel::ThemeChoice::darkMetal;
            break;
        case j37panel::ThemeChoice::darkMetal:
        default:
            themeChoice = j37panel::ThemeChoice::ivory;
            break;
        }
        applyTheme();
    };

    // ---------------------------------------------------------------
    //  Output-stage switches: polarity invert and auto gain.
    // ---------------------------------------------------------------
    setTip(bypassButton, "Hard bypass: the tape engine and both glue compressors are "
                         "switched out. The switch is ramped, so toggling it never clicks.");
    setTip(deltaButton, "DELTA listen: the output becomes the finished signal minus the "
                        "machine's own dry signal, so you hear ONLY what the machine "
                        "adds - harmonics, glue, transport wander. MIX keeps its meaning; "
                        "digital silence means the machine is being transparent. The "
                        "switch is ramped, and BYPASS fades the difference out rather "
                        "than snapping back to the dry signal. Turn it off before you "
                        "print.");
    polarityButton.setClickingTogglesState(true);
    setTip(polarityButton, "Inverts the output polarity (180-degree phase flip). "
                           "Use it to correct an inverted source or to align two "
                           "machines feeding the same bus.");
    polarityButton.setLookAndFeel(&customLookAndFeel);
    polarityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.parameters, "polarity", polarityButton);
    addAndMakeVisible(polarityButton);

    autoGainButton.setClickingTogglesState(true);
    setTip(autoGainButton, "Auto gain lets the slow programme compensator restore "
                           "the level the INPUT trim dialled in. Switch it off to "
                           "keep the output exactly at the level the chain produced.");
    autoGainButton.setLookAndFeel(&customLookAndFeel);
    autoGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.parameters, "auto_gain", autoGainButton);
    addAndMakeVisible(autoGainButton);
}

void FirstAudioProcessorEditor::setUpOversamplingAndInstrument() {
    // ---------------------------------------------------------------
    //  Oversampling switch on the panel. The parameter has always been
    //  host-visible; without a control it could only be reached from the
    //  DAW's own parameter list, which is not how a premium plugin works.
    // ---------------------------------------------------------------
    styleLabel(oversamplingLabel, "OVER", 9.0f, paletteFor(false).secondary, true,
               juce::Justification::left);
    addAndMakeVisible(oversamplingLabel);
    oversamplingBox.addItemList(juce::StringArray{"Off", "2x", "4x", "8x"}, 1);
    setTip(oversamplingBox, "Internal rate of the tape engine. 2x and 4x reduce the "
                            "aliasing of the magnetic shaper; the added filter delay "
                            "is reported to the host, so DAW PDC compensates.");
    oversamplingBox.setLookAndFeel(&customLookAndFeel);
    oversamplingAttachment =
        std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            audioProcessor.parameters, "oversampling", oversamplingBox);
    addAndMakeVisible(oversamplingBox);

    // ---------------------------------------------------------------
    //  Instrument voicing selector, on the deck's second row.
    // ---------------------------------------------------------------
    styleLabel(instrumentLabel, "INSTRUMENT", 9.0f, paletteFor(false).secondary, true,
               juce::Justification::left);
    addAndMakeVisible(instrumentLabel);
    instrumentBox.addItemList(
        juce::StringArray{"Master Bus", "Vocal", "Bass", "Guitar", "Piano", "Drums"}, 1);
    setTip(instrumentBox, "Re-voices the machine for what is being recorded: how hard "
                          "the tape bends, how much top end survives, how loud the "
                          "floor sits and how steady the transport runs. Master Bus "
                          "is the neutral calibration. Drums is the slam "
                          "calibration - harder bend, open head, tight memory.");
    instrumentBox.setLookAndFeel(&customLookAndFeel);
    instrumentAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.parameters, "instrument", instrumentBox);
    addAndMakeVisible(instrumentBox);
}

void FirstAudioProcessorEditor::setUpVinylSelectors() {
    // ---------------------------------------------------------------
    //  The three vinyl selectors: what was cut, what plays it, what reads it.
    //
    //  They are the same idea as TAPE TYPE and VINYL TYPE, one level up: the
    //  record TYPE says what was pressed, these say how it was cut, which deck
    //  it is on and which cartridge is in the arm. All three use the IN-TAB look
    //  and feel rather than the deck's, because they live inside a tab - see
    //  J37InlineLookAndFeel for why that is a different drawing and not a smaller
    //  copy of the same one.
    // ---------------------------------------------------------------
    const auto setUpVinylSelector = [this](juce::Label &label, juce::ComboBox &box,
                                           const juce::String &caption,
                                           const juce::StringArray &items, const juce::String &tip,
                                           const juce::String &parameterID) {
        styleLabel(label, caption, 9.0f, paletteFor(false).secondary, true,
                   juce::Justification::left);
        addAndMakeVisible(label);
        box.addItemList(items, 1);
        setTip(box, tip);
        // The flat field is painted by the inline Look and Feel, but the VALUE
        // is painted by juce::ComboBox's internal Label reading
        // ComboBox::textColourId - unstyled, that stayed the base LookAndFeel's
        // black on the dark panels, which is what made these lists unreadable.
        // applyTheme() re-inks it on every theme switch; this call is what makes
        // the very first paint legible.
        box.setColour(juce::ComboBox::textColourId, paletteFor(false).text);
        box.setLookAndFeel(&inlineLookAndFeel);
        addAndMakeVisible(box);
        juce::ignoreUnused(parameterID);
    };

    setUpVinylSelector(vinylGenerationLabel, vinylGenerationBox, "GENERATION",
                       juce::StringArray{"Laquer", "Direct", "Printed"},
                       "How the record was cut and pressed. LAQUER is the reference "
                       "cut - almost none of the cutter head's own colour, loud and "
                       "clean. DIRECT is a direct-metal master, cleaner still, with "
                       "the most top end and the quietest surface of the three. "
                       "PRINTED is the pressing you actually buy: a copy of a copy, "
                       "so the top end is duller, the surface noisier and the bass a "
                       "little fuller. It also scales this stage's own noise, so a "
                       "printed record is not merely darker - it is dirtier.",
                       "vinyl_generation");

    setUpVinylSelector(vinylTurntableLabel, vinylTurntableBox, "TURNTABLE",
                       juce::StringArray{"Belt", "Direct", "Idler"},
                       "What drives the platter. BELT is an audiophile belt-drive: "
                       "the elastic belt isolates the motor, so the drive is smooth "
                       "and quiet but marginally less steady. DIRECT is a high-torque "
                       "direct-drive DJ deck: the speed is locked solid. IDLER is a "
                       "vintage idler-wheel deck, where the motor bears on the inside "
                       "of the platter through a rubber wheel and couples its own "
                       "rumble straight into the groove. Each scales the stage's "
                       "wander and its stability, so the same record wanders "
                       "differently on each deck.",
                       "vinyl_turntable");

    setUpVinylSelector(vinylCartridgeLabel, vinylCartridgeBox, "CARTRIDGE",
                       juce::StringArray{"MM", "MC", "DJ"},
                       "What reads the groove, and the largest single difference of "
                       "the three selectors. MM is a moving magnet: warm, soft on "
                       "top, broad. MC is a moving coil: more detail, a brighter and "
                       "tighter top, and a lower output - so it carries more hiss "
                       "with it. DJ is a Concorde-style DJ cart: heavier, hotter, "
                       "tracks harder, with more surface noise and a firm bottom. "
                       "Each is a gain pair plus its own noise multipliers, which is "
                       "why it sounds like a different cartridge rather than like a "
                       "tone knob.",
                       "vinyl_cartridge");

    vinylGenerationAttachment =
        std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            audioProcessor.parameters, "vinyl_generation", vinylGenerationBox);
    vinylTurntableAttachment =
        std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            audioProcessor.parameters, "vinyl_turntable", vinylTurntableBox);
    vinylCartridgeAttachment =
        std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            audioProcessor.parameters, "vinyl_cartridge", vinylCartridgeBox);
}

void FirstAudioProcessorEditor::setUpTransportRow() {
    // ---------------------------------------------------------------
    //  Transport: STOP / PLAY / START, plus the momentary SPINDOWN hold.
    //
    //  Three buttons, not a combo. A tape machine's transport is a row of keys
    //  and the gesture matters: START and STOP are momentary, PLAY is where the
    //  machine rests. The three drive the `transport` choice through
    //  setTransportState(), and the engine writes that same parameter back when
    //  START settles into PLAY, so the lit key always matches the engine.
    //
    //  STOP-to-PLAY is handled as a TOGGLE on the same key: pressing STOP while
    //  running stops, pressing STOP again starts, which is what a single transport
    //  key does on a deck. START is the engagement gesture and nothing else: it
    //  drives the capstan UP, and a machine already at speed has nothing left to
    //  engage, so the state settles on PLAY. It never brakes first - a key that
    //  stopped the machine before starting it would be a re-cue, not a start.
    // ---------------------------------------------------------------
    styleLabel(transportLabel, "TRANSPORT", 9.0f, paletteFor(false).secondary, true,
               juce::Justification::left);
    addAndMakeVisible(transportLabel);
    setTip(transportStopButton, "STOP - brakes the capstan to rest and leaves it "
                                "there. A machine that is not moving passes nothing, so "
                                "the output goes silent: no tape, no dry input, no hiss, "
                                "no delay tail. Pressing it again spins the machine "
                                "back up to PLAY.");
    setTip(transportPlayButton, "PLAY - normal running. This is where the machine rests, "
                                "and where START settles by itself.");
    setTip(transportStartButton, "START - the engagement gesture: the capstan comes up "
                                 "over about a second, the way a deck sounds when you "
                                 "hit play on a take, and the control settles on PLAY "
                                 "when the platter reaches speed. It is a spin-up and "
                                 "nothing else - catching the machine mid-coast it brings "
                                 "it back up, and on a machine already running there is "
                                 "nothing left to engage, so it lands on PLAY. Its "
                                 "timing follows the deck: tape speed, turntable speed "
                                 "for the run-down, and the session's tempo.\n\n"
                                 "Pressed while the DAW is not rolling it is ARMED "
                                 "instead: the machine winds down and waits, and the "
                                 "take engages the moment the host rolls - so pressing "
                                 "START before the DAW starts does not spend the "
                                 "spin-up on a pause. Press PLAY or STOP to take the "
                                 "key back.");

    for (auto *button : {&transportStopButton, &transportPlayButton, &transportStartButton}) {
        button->setLookAndFeel(&customLookAndFeel);
        addAndMakeVisible(*button);
    }

    transportStopButton.onClick = [this] {
        // Toggle on the same key: STOP stops, and a second press starts. That is
        // the "Start is Stop and vice versa" behaviour, and it keeps the key
        // useful whichever state the machine is in.
        const auto state = currentTransportState();
        if (state == 0)
            audioProcessor.setTransportState(2); // stopped -> spin up (START)
        else
            audioProcessor.setTransportState(0); // running -> stop
    };
    transportPlayButton.onClick = [this] { audioProcessor.setTransportState(1); };
    transportStartButton.onClick = [this] { audioProcessor.setTransportState(2); };

    // SPINDOWN: a momentary hold, so its hold edges come from the button's own
    // mouseDown/mouseUp rather than onClick. setSpindownHeld() writes both the
    // engine atomic and the host-visible parameter, so an automation lane and a
    // finger on this button are the same event.
    styleLabel(spindownLabel, "SPINDOWN", 9.0f, paletteFor(false).secondary, true,
               juce::Justification::left);
    addAndMakeVisible(spindownLabel);
    spindownButton.setLookAndFeel(&customLookAndFeel);
    setTip(spindownButton, "Hold to cut the platter's power: the machine winds down "
                           "over about a second and a half, and comes back up when you "
                           "release it. A hold also brings the platter up if the "
                           "machine was stopped, so the gesture always has something "
                           "to act on.");
    spindownButton.onHoldChanged = [this](bool held) {
        audioProcessor.setSpindownHeld(held);
        lastShownSpindown = held;
        styleSpindownButton();
    };
    addAndMakeVisible(spindownButton);
}

void FirstAudioProcessorEditor::setUpDelayRow() {
    // ---------------------------------------------------------------
    //  DELAY TYPE - which machine the second head behaves as.
    //
    //  Three discrete machines rather than a scale: TAPE loses top end on every
    //  pass because the repeat is re-recorded, BBD is darker still with clock
    //  noise and a bandwidth that narrows as the delay lengthens, and MODERN is
    //  clean and full-bandwidth. A combo, because there is no meaningful point
    //  between them.
    // ---------------------------------------------------------------
    styleLabel(delayTypeLabel, "DLY TYPE", 9.0f, paletteFor(false).secondary, true,
               juce::Justification::left);
    addAndMakeVisible(delayTypeLabel);
    addAndMakeVisible(delaySyncLabel);
    delayTypeBox.addItemList(juce::StringArray{"Tape", "BBD", "Modern"}, 1);
    setTip(delayTypeBox, "What the second head's repeats sound like. TAPE loses "
                         "top end on every pass, because the repeat is recorded "
                         "onto the tape and played back through the same losses "
                         "the main path has. BBD is a bucket-brigade chip: darker "
                         "still, with clock noise on the repeats and a bandwidth "
                         "that narrows as the delay lengthens - which is what the "
                         "technology physically does. MODERN is a clean digital "
                         "delay with full bandwidth.");
    delayTypeBox.setLookAndFeel(&customLookAndFeel);
    delayTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.parameters, "delay_type", delayTypeBox);
    addAndMakeVisible(delayTypeBox);

    // ---------------------------------------------------------------
    //  DELAY SYNC + RATE - the second head locked to the host's tempo.
    //
    //  SYNC switches between a free time in milliseconds (what a real head
    //  spacing gives) and a note value derived from the host's tempo. With SYNC
    //  on, the repeat lands on the beat whatever the session is at, and follows a
    //  tempo change without the user touching anything - the plugin reads the
    //  tempo from the playhead every block.
    //
    //  RATE is the note value. The list is the one a delay is actually used with:
    //  the straight divisions, the two common triplets, and the dotted quarter,
    //  which is the one that gives the classic off-beat repeat.
    // ---------------------------------------------------------------
    delaySyncButton.setClickingTogglesState(true);
    setTip(delaySyncButton, "SYNC - lock the delay to the host's tempo instead of "
                            "a time in milliseconds. With SYNC on, RATE picks a "
                            "note value and the repeat lands on the beat whatever "
                            "the session is at, following a tempo change without "
                            "the user touching anything. The tempo is read from "
                            "the host's playhead every block. With SYNC off, "
                            "DELAY is a free time in milliseconds, which is what a "
                            "real head spacing gives.");
    delaySyncButton.setLookAndFeel(&customLookAndFeel);
    delaySyncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.parameters, "delay_sync", delaySyncButton);
    addAndMakeVisible(delaySyncButton);

    styleLabel(delayRateLabel, "RATE", 9.0f, paletteFor(false).secondary, true,
               juce::Justification::left);
    styleLabel(delaySyncLabel, "SYNC DELAY", 9.0f, paletteFor(false).secondary, true,
               juce::Justification::left);
    addAndMakeVisible(delayRateLabel);
    delayRateBox.addItemList(
        juce::StringArray{"1/1", "1/2", "1/4", "1/8", "1/16", "1/4 T", "1/8 T", "1/4 D"}, 1);
    setTip(delayRateBox, "RATE - the note value the delay is locked to when SYNC "
                         "is on. 1/4 is a quarter note, 1/4 T is a quarter-note "
                         "triplet (a third of a beat faster) and 1/4 D is dotted "
                         "(half again as long) - the one that gives the classic "
                         "off-beat repeat. At 120 BPM a 1/4 is 500 ms and a 1/4 D "
                         "is 750 ms.");
    delayRateBox.setLookAndFeel(&customLookAndFeel);
    delayRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.parameters, "delay_rate", delayRateBox);
    addAndMakeVisible(delayRateBox);
}

void FirstAudioProcessorEditor::setUpModeSwitches() {
    // ---------------------------------------------------------------
    //  The two whole-machine modes.
    //
    //  MODERN re-voices the deck for a well-maintained 1990s machine: the head
    //  losses move out of the audio band, the floor drops, the magnetic memory
    //  thins. LO-FI is the deliberate degradation - a hard 3.2 kHz limit and a
    //  sample-and-hold quantisation, applied after the protection chain so the
    //  fault survives to the output.
    //
    //  They are mutually exclusive by design: a machine cannot be both, and the
    //  engine resolves MODERN first when both are set. These handlers make the
    //  exclusivity visible on the panel rather than leaving the user to discover
    //  it by ear - turning one on releases the other, so the two lamps can never
    //  both be lit.
    // ---------------------------------------------------------------
    // One button cycles OFF -> LO-FI -> MODERN -> OFF. The two modes are
    // mutually exclusive by design, so a single state holder is honest in a way
    // two toggles plus exclusivity code never quite was: the caption always
    // names the mode that is live, and no sequence of clicks can leave both
    // engaged. The two parameters are written directly (a ButtonAttachment per
    // side would fight the cycle); the engine reads them as it always did.
    setTip(modeCycleButton, "The whole-machine mode, cycling OFF - LO-FI - "
                            "MODERN. LO-FI is the deliberate degradation: a hard "
                            "3.2 kHz bandwidth limit and sample-and-hold "
                            "quantisation on the finished sample. MODERN "
                            "re-voices the machine for a well-maintained 1990s "
                            "deck: head losses out of the audio band, a lower "
                            "noise floor, thinner magnetic memory. They are "
                            "mutually exclusive by design, so one button holds "
                            "the state.");
    modeCycleButton.setLookAndFeel(&customLookAndFeel);
    modeCycleButton.onClick = [this] {
        const auto modernOn = isModeOn("modern_mode");
        const auto lofiOn = isModeOn("lofi_mode");

        auto nextModern = false;
        auto nextLofi = false;
        if (!modernOn && !lofiOn) {
            nextLofi = true;
        } // OFF -> LO-FI
        else if (lofiOn) {
            nextModern = true;
        } // LO-FI -> MODERN
        /* MODERN -> OFF */ // stays both false

        audioProcessor.parameters.getParameterAsValue("modern_mode").setValue(nextModern);
        audioProcessor.parameters.getParameterAsValue("lofi_mode").setValue(nextLofi);
        refreshModeButtonCaption();
    };
    addAndMakeVisible(modeCycleButton);
    refreshModeButtonCaption();
}

void FirstAudioProcessorEditor::setUpSettingsSwitches() {
    // ---------------------------------------------------------------
    //  UI SOUNDS - the panel's own interface clicks.
    //
    //  A host-visible parameter, so the choice survives in the session and can
    //  be automated - and so the engine, the preset system and this switch all
    //  agree by construction rather than by being kept in step by hand. The
    //  editor reads it every frame and enables its sound engine from it.
    //
    //  It is OFF by default and deliberately quiet about being on: the engine
    //  is its own audio device, so a click never reaches the plugin's output,
    //  the DAW's meters or a render. See UiSoundEngine for why that is a hard
    //  requirement rather than a nicety.
    // ---------------------------------------------------------------
    uiSoundsButton.setClickingTogglesState(true);
    setTip(uiSoundsButton, "UI SOUNDS - the panel's own interface clicks: switches "
                           "click, keys thump, knobs tick as they cross a step. They "
                           "are synthesised and play on a SEPARATE audio device, so "
                           "they never reach the plugin's output, the DAW's meters or "
                           "a render. Off by default; a studio at 3 a.m. does not "
                           "want its interface ticking.");
    uiSoundsButton.setLookAndFeel(&inlineLookAndFeel);
    uiSoundsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.parameters, "ui_sounds", uiSoundsButton);
    addAndMakeVisible(uiSoundsButton);

    // ---------------------------------------------------------------
    //  The anti-phase guard's switch.
    //
    //  The deck already reports the guard: the ANTI-PHASE readout beside the
    //  transport says "clean" while it is idle and a percentage while it is
    //  working, because a fault that only exists after a mono fold-down is
    //  otherwise invisible. What the readout could not do is give the user a way
    //  to say "I know - pass it through": a deliberately inverted source, a
    //  mid/side rig, an out-of-phase effect chain, all of them legitimate, all of
    //  them things the guard would quietly rotate away.
    //
    //  A host-visible parameter like UI SOUNDS, so the choice is stored in the
    //  session, automated, and undone with everything else. It is deliberately
    //  NOT presettable: a factory preset must not be able to lower a safety net.
    // ---------------------------------------------------------------
    antiPhaseGuardButton.setClickingTogglesState(true);
    setTip(antiPhaseGuardButton, "ANTI-PHASE GUARD - the output protection that "
                                 "rotates a stereo pair whose sides are in "
                                 "opposite polarity back toward mono, so a "
                                 "mono fold-down (a club PA, a phone, a mono "
                                 "mastering check) still has a centre and a "
                                 "low end. Leave it on unless the reversal is "
                                 "deliberate: off, the pair passes through "
                                 "exactly as it arrives.");
    antiPhaseGuardButton.setLookAndFeel(&inlineLookAndFeel);
    antiPhaseGuardAttachment =
        std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            audioProcessor.parameters, "anti_phase_guard", antiPhaseGuardButton);
    addAndMakeVisible(antiPhaseGuardButton);
    styleLabel(antiPhaseGuardLabel, "ANTI-PHASE", 9.0f, paletteFor(false).secondary, true,
               juce::Justification::left);
    addAndMakeVisible(antiPhaseGuardLabel);
}

void FirstAudioProcessorEditor::setUpModelButtons() {
    // ---------------------------------------------------------------
    //  The neural model picker, on the MODELS tab beside the NEURAL knob.
    //  It has no parameter and no attachment: a model is data read from a file,
    //  not a value a session or a preset carries, so the two buttons drive the
    //  processor directly and the status label reports what the stage holds.
    // ---------------------------------------------------------------
    loadNeuralButton.setLookAndFeel(&inlineLookAndFeel);
    clearNeuralButton.setLookAndFeel(&inlineLookAndFeel);
    // "Currently black" (the user's screenshot of the model picker): these two
    // are TextButtons whose colours were never set, so the inline Look and
    // Feel's unstyled defaults ran - a near-black slab on every theme. They get
    // the SAME raised-face pill the workflow buttons carry, re-inked per theme
    // in applyTheme().
    loadNeuralButton.setColour(juce::TextButton::buttonColourId, paletteFor(false).raised);
    loadNeuralButton.setColour(juce::TextButton::textColourOffId, paletteFor(false).text);
    loadNeuralButton.setColour(juce::TextButton::buttonOnColourId,
                               paletteFor(false).raised.darker(0.15f));
    clearNeuralButton.setColour(juce::TextButton::buttonColourId, paletteFor(false).raised);
    clearNeuralButton.setColour(juce::TextButton::textColourOffId, paletteFor(false).text);
    clearNeuralButton.setColour(juce::TextButton::buttonOnColourId,
                                paletteFor(false).raised.darker(0.15f));
    setTip(loadNeuralButton, "LOAD MODEL - reads a neural model and installs it as "
                             "the plugin's optional learned nonlinearity. Both wire "
                             "formats are read: RTNeural's own JSON and the .nam files "
                             "NeuralAmpModeler writes, of which the LSTM models are "
                             "played and the WaveNet and Feather ones are refused by "
                             "name. The network runs per channel, after the tape "
                             "stage. NEURAL sets how much of its output reaches the "
                             "signal, and a load that succeeds opens NEURAL to full "
                             "while it is still closed, so the new model is heard at "
                             "once. The picker reopens in the folder the last model "
                             "came from. Loading a model does not change any other "
                             "parameter and is not saved with the session.");
    setTip(clearNeuralButton, "CLEAR - releases the loaded model, returning the NEURAL "
                              "stage to its transparent pass-through.");
    loadNeuralButton.onClick = [this] { loadNeuralModelFromFile(); };
    clearNeuralButton.onClick = [this] {
        audioProcessor.clearNeuralModel();
        loadedNeuralName = {};
        // A refusal belongs to the file that was refused, so clearing the stage
        // clears the reason with it: "NO MODEL" is the truth again afterwards.
        neuralStatusReason = {};
        refreshNeuralStatus();
    };
    addAndMakeVisible(loadNeuralButton);
    addAndMakeVisible(clearNeuralButton);

    styleLabel(neuralStatusLabel, "NO MODEL", 8.5f, paletteFor(false).secondary, false,
               juce::Justification::centred);
    addAndMakeVisible(neuralStatusLabel);

    // ---------------------------------------------------------------
    //  The IR picker rides the MODELS page's member band, under the IR MIX
    //  knob it belongs to: LOAD IR / CLEAR IR / status, styled and tipped
    //  exactly like the model picker beside it. An IR is data read from a
    //  file for the same reason a model is - no parameter, no attachment.
    // ---------------------------------------------------------------
    loadIrButton.setLookAndFeel(&inlineLookAndFeel);
    clearIrButton.setLookAndFeel(&inlineLookAndFeel);
    loadIrButton.setColour(juce::TextButton::buttonColourId, paletteFor(false).raised);
    loadIrButton.setColour(juce::TextButton::textColourOffId, paletteFor(false).text);
    loadIrButton.setColour(juce::TextButton::buttonOnColourId,
                           paletteFor(false).raised.darker(0.15f));
    clearIrButton.setColour(juce::TextButton::buttonColourId, paletteFor(false).raised);
    clearIrButton.setColour(juce::TextButton::textColourOffId, paletteFor(false).text);
    clearIrButton.setColour(juce::TextButton::buttonOnColourId,
                            paletteFor(false).raised.darker(0.15f));
    setTip(loadIrButton, "LOAD IR - reads an impulse response (a WAV or AIFF "
                         "capture of a cabinet, a room or any other space) and "
                         "convolves the finished signal with it. IR MIX sets how "
                         "much of the convolved signal is blended in, and a load "
                         "that succeeds opens IR MIX to full while it is still "
                         "closed, so the capture is heard at once. The picker "
                         "reopens in the folder the last impulse response came "
                         "from. Loading an IR does not change any other parameter "
                         "and is not saved with the session.");
    setTip(clearIrButton, "CLEAR IR - releases the loaded impulse response, "
                          "returning the IR stage to its transparent "
                          "pass-through.");
    loadIrButton.onClick = [this] { loadIrFromFile(); };
    clearIrButton.onClick = [this] {
        audioProcessor.clearIr();
        loadedIrName = {};
        irStatusReason = {};
        refreshIrStatus();
    };
    addAndMakeVisible(loadIrButton);
    addAndMakeVisible(clearIrButton);

    styleLabel(irStatusLabel, "NO IR", 8.5f, paletteFor(false).secondary, false,
               juce::Justification::centred);
    addAndMakeVisible(irStatusLabel);

    // ---------------------------------------------------------------
    //  The VINYL page's noise-print picker, in the band under its three knobs.
    //  A print is the record's own SURFACE as a file - a WAV recording of a real
    //  pressing, which replaces the surface the stage synthesises - so the third
    //  file-fed picker is built here, beside the other two, and styled, tipped
    //  and wired the same way: a raise-face pill rather than a parameter, and a
    //  status readout that reports what the stage actually holds.
    // ---------------------------------------------------------------
    loadNoiseButton.setLookAndFeel(&inlineLookAndFeel);
    clearNoiseButton.setLookAndFeel(&inlineLookAndFeel);
    loadNoiseButton.setColour(juce::TextButton::buttonColourId, paletteFor(false).raised);
    loadNoiseButton.setColour(juce::TextButton::textColourOffId, paletteFor(false).text);
    loadNoiseButton.setColour(juce::TextButton::buttonOnColourId,
                              paletteFor(false).raised.darker(0.15f));
    clearNoiseButton.setColour(juce::TextButton::buttonColourId, paletteFor(false).raised);
    clearNoiseButton.setColour(juce::TextButton::textColourOffId, paletteFor(false).text);
    clearNoiseButton.setColour(juce::TextButton::buttonOnColourId,
                               paletteFor(false).raised.darker(0.15f));
    setTip(loadNoiseButton, "LOAD PRINT - reads a WAV recording of a record's "
                            "surface - a noise print, in the sense a record "
                            "collector means it - and plays it as the VINYL "
                            "stage's surface. A loaded print REPLACES the "
                            "surface the stage synthesises, and the FAULTS "
                            "- CRACKLE, DUST, CLICKS, SCRATCH, WARP and "
                            "ELECTRICAL - keep working over it, because those "
                            "are the deck and the damage rather than the "
                            "surface. The file's own level is kept, so NOISE "
                            "and VINYL MIX are what set how much of it is in "
                            "the output; the decode runs on a background "
                            "thread and the audio thread never waits for it. "
                            "The picker reopens in the folder the last print "
                            "came from, and a print is not saved with the "
                            "session.");
    setTip(clearNoiseButton, "CLEAR PRINT - releases the loaded print, and the "
                             "VINYL stage goes back to synthesising its own "
                             "surface.");
    loadNoiseButton.onClick = [this] { loadNoisePrintFromFile(); };
    clearNoiseButton.onClick = [this] {
        audioProcessor.clearNoiseFile();
        // A refusal belongs to the file that was refused, so releasing the stage
        // releases the reason with it - the same rule the other two pickers
        // follow, and it is what makes "NO PRINT" the truth again afterwards.
        noiseStatusReason = {};
        refreshNoiseStatus();
    };
    addAndMakeVisible(loadNoiseButton);
    addAndMakeVisible(clearNoiseButton);

    styleLabel(noiseStatusLabel, "NO PRINT", 8.5f, paletteFor(false).secondary, false,
               juce::Justification::centred);
    addAndMakeVisible(noiseStatusLabel);
}

void FirstAudioProcessorEditor::setUpInlineLists() {
    // ---------------------------------------------------------------
    //  The remaining choice lists: the DI pad, the track layout and the two
    //  EQ orders. All four are lists rather than knobs because their values are
    //  discrete settings, not points on a scale - and all four use the IN-TAB
    //  look and feel, because that is what a list inside a tab is drawn with.
    // ---------------------------------------------------------------
    const auto setUpInlineList = [this](juce::Label &label, juce::ComboBox &box,
                                        const juce::String &caption, const juce::StringArray &items,
                                        const juce::String &tip) {
        styleLabel(label, caption, 9.0f, paletteFor(false).secondary, true,
                   juce::Justification::left);
        addAndMakeVisible(label);
        box.addItemList(items, 1);
        setTip(box, tip);
        // The flat field is painted by the inline Look and Feel, but the VALUE
        // is painted by juce::ComboBox's internal Label reading
        // ComboBox::textColourId - unstyled, that stayed the base LookAndFeel's
        // black on the dark panels, which is what made these lists unreadable.
        // applyTheme() re-inks it on every theme switch; this call is what makes
        // the very first paint legible.
        box.setColour(juce::ComboBox::textColourId, paletteFor(false).text);
        box.setLookAndFeel(&inlineLookAndFeel);
        addAndMakeVisible(box);
    };

    setUpInlineList(diPadLabel, diPadBox, "DI PAD",
                    juce::StringArray{"0 dB", "-10 dB", "-20 dB", "-30 dB"},
                    "The DI box's input pad, applied BEFORE its transformer. A pad "
                    "is a switch rather than a level trim because that is the "
                    "decision it makes: a hot source (an active synth, a boosted "
                    "pedal) can be plugged in without driving the box's own core "
                    "into saturation. 0 dB is no pad.");

    setUpInlineList(tracksLabel, tracksBox, "TRACKS", juce::StringArray{"2", "2+3", "3"},
                    "The machine's track layout, which is geometry rather than "
                    "taste. 2 is a stereo deck: two tracks each with half the "
                    "tape, the most low end per channel and no adjacent track to "
                    "leak from. 2+3 is a four-track deck used as two on tracks 2 "
                    "and 3: a whole track's worth of tape between the channels, "
                    "so the widest spacing and the least crosstalk. 3 is a "
                    "three-track deck: narrower tracks, so less low end and a "
                    "higher noise floor for the same tape, with the most bleed "
                    "between the heads.");

    const juce::StringArray eqOrderItems{"6 dB/oct",  "12 dB/oct", "18 dB/oct",
                                         "24 dB/oct", "36 dB/oct", "48 dB/oct"};
    setUpInlineList(inputEqOrderLabel, inputEqOrderBox, "ORDER", eqOrderItems,
                    "The slope of the input EQ's high-pass and low-pass, in dB "
                    "per octave. 6 is one pole, 12 is two, up to 48 for a filter "
                    "that gets out of the way completely. The number on the panel "
                    "is the slope you get: the engine stacks one-pole sections, "
                    "one per 6 dB.");

    setUpInlineList(outputEqOrderLabel, outputEqOrderBox, "ORDER", eqOrderItems,
                    "The slope of the output EQ's high-pass and low-pass, in dB "
                    "per octave. Separate from the input EQ's because the two "
                    "equalisers make different decisions: the input one shapes "
                    "what the machine hears, the output one corrects what it "
                    "produced.");

    const std::array<const char *, 6> compressorIds{"comp_threshold", "comp_ratio",  "comp_attack",
                                                    "comp_release",   "comp_makeup", "comp_mix"};
    const std::array<const char *, 6> compressorNames{"THRESH",  "RATIO",  "ATTACK",
                                                      "RELEASE", "MAKEUP", "MIX"};
    for (std::size_t i = 0; i < compressorControls.size(); ++i) {
        auto &slider = compressorControls[i];
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        // 16 px for the readout, as on the deck's knobs - see the note there.
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 78, 16);
        if (i == 0)
            slider.setRange(-60.0, 0.0, 0.1);
        else if (i == 1)
            slider.setRange(1.0, 20.0, 0.1);
        else if (i == 2)
            slider.setRange(0.1, 100.0, 0.1);
        else if (i == 3)
            slider.setRange(10.0, 1000.0, 1.0);
        else if (i == 4)
            slider.setRange(0.0, 24.0, 0.1);
        else
            slider.setRange(0.0, 1.0, 0.001);
        slider.setLookAndFeel(&customLookAndFeel);
        slider.setName(compressorNames[i]);
        compressorControlLabels[i].setText(compressorNames[i], juce::dontSendNotification);
        // Same caption as every other page's knobs - the COMP page's strip is the
        // common one now, so its labels must match it rather than the meters' type.
        compressorControlLabels[i].setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
        compressorControlLabels[i].setJustificationType(juce::Justification::centred);
        compressorControlLabels[i].setInterceptsMouseClicks(false, false);
        addAndMakeVisible(compressorControls[i]);
        addAndMakeVisible(compressorControlLabels[i]);
        compressorAttachments[i] =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                audioProcessor.parameters, compressorIds[i], compressorControls[i]);
    }

    diPadAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.parameters, "di_pad", diPadBox);
    tracksAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.parameters, "tracks", tracksBox);
    inputEqOrderAttachment =
        std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            audioProcessor.parameters, "in_eq_order", inputEqOrderBox);
    outputEqOrderAttachment =
        std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            audioProcessor.parameters, "out_eq_order", outputEqOrderBox);

    //  The four corner switches. The corner knobs already bypass themselves at
    //  the ends of their travel, but that is a fallback rather than a control:
    //  a user who wants the equaliser's high-pass gone has to find the knob's
    //  end stop, and then cannot tell the filter from one sitting at its
    //  bypass. These are explicit answers, and they are pill switches
    //  because they are state, like the mode button.
}

void FirstAudioProcessorEditor::setUpEqCornerSwitches() {
    const auto setUpEqCornerSwitch = [this](juce::ToggleButton &button, const juce::String &which,
                                            const juce::String &corner) {
        button.setClickingTogglesState(true);
        setTip(button, which.toUpperCase() + " " + corner.toUpperCase() +
                           " ON/OFF. "
                           "The corner frequency beside it sets where the filter "
                           "turns; this says whether the filter is in the signal at "
                           "all. With the switch off the corner keeps its setting, "
                           "so turning the filter back on does not make the user "
                           "find the frequency again - which is the difference "
                           "between a switch and parking the knob at its end stop.");
        button.setLookAndFeel(&inlineLookAndFeel);
        addAndMakeVisible(button);
    };

    setUpEqCornerSwitch(inputEqHpButton, "Input EQ", "HP");
    setUpEqCornerSwitch(inputEqLpButton, "Input EQ", "LP");
    setUpEqCornerSwitch(outputEqHpButton, "Output EQ", "HP");
    setUpEqCornerSwitch(outputEqLpButton, "Output EQ", "LP");

    inputEqHpAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.parameters, "in_hp_on", inputEqHpButton);
    inputEqLpAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.parameters, "in_lp_on", inputEqLpButton);
    outputEqHpAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.parameters, "out_hp_on", outputEqHpButton);
    outputEqLpAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.parameters, "out_lp_on", outputEqLpButton);
}

void FirstAudioProcessorEditor::setUpPresetBar() {
    // ---------------------------------------------------------------
    //  Premium workflow bar.
    // ---------------------------------------------------------------
    styleLabel(presetHeadingLabel, "PRESET", 9.0f, paletteFor(false).secondary, true,
               juce::Justification::centredLeft);
    addAndMakeVisible(presetHeadingLabel);

    refreshPresetList();
    // The box now starts with nothing selected on a fresh instance (honest display:
    // no factory preset has been applied yet), so it needs a placeholder for the
    // empty row rather than the base Look and Feel's blank field.
    presetBox.setTextWhenNothingSelected("FACTORY...");
    setTip(presetBox, "Factory presets. Loading one replaces the whole machine state "
                      "in a single undoable step.");
    presetBox.setLookAndFeel(&customLookAndFeel);
    presetBox.onChange = [this] {
        const auto selectedId = presetBox.getSelectedId();
        if (selectedId <= 0)
            return;
        const auto presetIndex = selectedId - 1;

        // Re-selecting the displayed entry IS a state change whenever the machine
        // has drifted from that preset since it was loaded (the EDITED badge is
        // exactly that fact, on screen). The old guard returned whenever the
        // index matched, which made the one gesture a user reaches for after
        // tweaking - "put this preset back" - do nothing at all: the box already
        // showed the preset, the click re-picked it, and the panel kept the
        // edited state. Reload whenever the machine is dirty; skip only when
        // that preset is both loaded AND clean.
        if (presetIndex == audioProcessor.getLastPresetIndex() && !audioProcessor.isPresetDirty())
            return;

        audioProcessor.applyFactoryPreset(presetIndex);
        lastShownPreset = presetIndex;
    };
    addAndMakeVisible(presetBox);

    const auto workflowButtonSetup = [&](juce::TextButton &button, const juce::String &tip) {
        setTip(button, tip);
        button.setLookAndFeel(&customLookAndFeel);
        addAndMakeVisible(button);
    };

    // ---------------------------------------------------------------
    //  User presets: the combo lists what is on disk, SAVE opens a name
    //  dialog and DEL removes the selected file. Every action refreshes
    //  the list so two instances of the plugin stay honest with each
    //  other about what exists.
    // ---------------------------------------------------------------
    refreshUserPresetList();
    setTip(userPresetBox, "User presets - your own saved machine states. "
                          "Selecting one recalls it as a single undoable step.");
    userPresetBox.setLookAndFeel(&customLookAndFeel);
    userPresetBox.setTextWhenNothingSelected("USER...");
    userPresetBox.onChange = [this] {
        const auto selectedId = userPresetBox.getSelectedId();
        if (selectedId <= 0)
            return;
        const auto name = userPresetBox.getItemText(selectedId - 1);
        if (name.isEmpty() || name == audioProcessor.getCurrentPresetName())
            return;
        if (audioProcessor.applyUserPreset(name)) {
            lastShownUserPreset = name;
            lastShownPreset = -1;
            // A user preset clears the factory selection: ID 0 is "no selection".
            presetBox.setSelectedId(audioProcessor.getLastPresetIndex() + 1,
                                    juce::dontSendNotification);
            updateWorkflowButtons();
        } else {
            refreshUserPresetList(); // the file vanished; rebuild the list
        }
    };
    addAndMakeVisible(userPresetBox);

    workflowButtonSetup(savePresetButton, "Store the whole machine state as a user "
                                          "preset you can recall in any session.");
    workflowButtonSetup(deletePresetButton, "Delete the selected user preset file.");
    savePresetButton.onClick = [this] {
        if (savePresetWindow != nullptr) {
            savePresetWindow->exitModalState(0);
            savePresetWindow.reset();
        }

        auto *window = new juce::AlertWindow(
            "Save user preset", "Name this machine state:", juce::MessageBoxIconType::NoIcon);
        window->addTextEditor("preset_name", audioProcessor.getCurrentPresetName(), "Name:");
        window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
        window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        savePresetWindow.reset(window);

        window->enterModalState(
            true, juce::ModalCallbackFunction::create([this, window](int result) {
                // Guard on the pointer: an older dialog's asynchronous callback must
                // never dismiss a newer one.
                if (savePresetWindow.get() != window)
                    return;
                const auto chosen = window->getTextEditorContents("preset_name");
                savePresetWindow.reset();
                if (result == 1 && chosen.trim().isNotEmpty()) {
                    if (audioProcessor.saveUserPreset(chosen))
                        refreshUserPresetList();
                }
            }),
            false);
    };
    deletePresetButton.onClick = [this] {
        const auto selectedId = userPresetBox.getSelectedId();
        if (selectedId <= 0)
            return;
        const auto name = userPresetBox.getItemText(selectedId - 1);
        if (name.isNotEmpty() && audioProcessor.deleteUserPreset(name))
            refreshUserPresetList();
    };

    styleLabel(presetBadgeLabel, "", 8.0f, paletteFor(false).secondary, false,
               juce::Justification::centredLeft);
    addAndMakeVisible(presetBadgeLabel);

    workflowButtonSetup(copyAButton, "Store the current settings in slot A. "
                                     "A is the side the plugin starts on.");
    workflowButtonSetup(copyBButton, "Store the current settings in slot B.");
    workflowButtonSetup(compareButton, "Toggle between slots A and B to compare settings "
                                       "with identical level, bypass state and oversampling.");
    workflowButtonSetup(undoButton, "Undo the last preset or A/B change (Ctrl+Z).");
    workflowButtonSetup(redoButton, "Redo an undone preset or A/B change (Ctrl+Y).");

    copyAButton.onClick = [this] {
        audioProcessor.copyToCompareSlot(0);
        updateWorkflowButtons();
    };
    copyBButton.onClick = [this] {
        audioProcessor.copyToCompareSlot(1);
        updateWorkflowButtons();
    };
    compareButton.onClick = [this] {
        audioProcessor.toggleCompare();
        updateWorkflowButtons();
    };
    undoButton.onClick = [this] {
        audioProcessor.getUndoManager().undo();
        updateWorkflowButtons();
    };
    redoButton.onClick = [this] {
        audioProcessor.getUndoManager().redo();
        updateWorkflowButtons();
    };

    styleLabel(compareBadgeLabel, "", 8.5f, paletteFor(false).accent, true,
               juce::Justification::centred);
    addAndMakeVisible(compareBadgeLabel);
}

void FirstAudioProcessorEditor::addDeckControlsToPanel() {
    addAndMakeVisible(tapeTypeBox);
    addAndMakeVisible(valveTypeBox);
    addAndMakeVisible(ampTypeBox);
    addAndMakeVisible(transformerTypeBox);
    addAndMakeVisible(digitalTypeBox);
    addAndMakeVisible(vinylTypeBox);
    addAndMakeVisible(vinylSpeedBox);
    addAndMakeVisible(speedBox);
    addAndMakeVisible(bypassButton);
    addAndMakeVisible(themeButton);
    tapeTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.parameters, "tape_type", tapeTypeBox);
    valveTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.parameters, "valve_type", valveTypeBox);
    ampTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.parameters, "amp_type", ampTypeBox);
    transformerTypeAttachment =
        std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            audioProcessor.parameters, "transformer_type", transformerTypeBox);
    digitalTypeAttachment =
        std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            audioProcessor.parameters, "digital_type", digitalTypeBox);
    vinylTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.parameters, "vinyl_type", vinylTypeBox);
    vinylSpeedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.parameters, "vinyl_speed", vinylSpeedBox);
    speedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.parameters, "speed", speedBox);
}

void FirstAudioProcessorEditor::setUpSceneAndMeters() {
    // The 3D transport goes in FIRST, so it is the bottom-most child. It is a
    // child component rather than part of this editor's own painting because it
    // needs a custom renderer and this editor's context is a component painter -
    // see Source/GUI/TapeScene.h. It sits in the strip resized() keeps clear of
    // every control, so nothing here can end up drawn on top of it.
    addAndMakeVisible(tapeScene);

    addAndMakeVisible(inputMeter);
    addAndMakeVisible(outputMeter);
    addAndMakeVisible(compressorMeterIn);
    addAndMakeVisible(compressorMeterOut);
    applyTheme();
}

void FirstAudioProcessorEditor::setUpOpenGlRendering() {
    // Native command renderers currently implement presentation only. Do not
    // attach them to a host-owned child surface until TapeScene's scene geometry
    // and clipping are implemented for that API; keep JUCE's CPU painting active.
    // This is intentionally a no-op for native backends: selecting Vulkan,
    // Metal, or DirectX must not create a second surface that covers the host UI.
    if (!j37::render::usesOpenGL())
        return;

    // OpenGL is automatic and best-effort. JUCE's ordinary component paint
    // path remains enabled when context creation fails.
    tapeScene.setSceneEnabled(true);
    openGLContext.setComponentPaintingEnabled(true);
    openGLContext.setContinuousRepainting(false);
    openGLContext.setSwapInterval(1);
    openGLContext.attachTo(*this);
    openGLAttachAttemptsLeft = openGLContext.isAttached() ? 0 : 90;
}

void FirstAudioProcessorEditor::finishConstruction() {
    // The interface sounds are attached LAST, after every control they watch
    // exists AND after every existing onClick handler has been installed - so a
    // control's own handler is always the one that runs first and the sound
    // cannot replace it. See attachInterfaceSounds() for how each control's
    // voice is chosen.
    attachInterfaceSounds();

    startTimerHz(30);
}

void FirstAudioProcessorEditor::createDecorativePhysics() {
    physicsWorld = std::make_unique<b2World>(b2Vec2(0.0f, 0.0f));

    b2BodyDef boundaryDefinition;
    auto *boundaryBody = physicsWorld->CreateBody(&boundaryDefinition);

    const auto addWall = [boundaryBody](float centreX, float centreY, float halfWidth,
                                        float halfHeight) {
        b2PolygonShape wallShape;
        wallShape.SetAsBox(halfWidth, halfHeight, b2Vec2(centreX, centreY), 0.0f);

        b2FixtureDef wallFixture;
        wallFixture.shape = &wallShape;
        wallFixture.friction = 0.05f;
        wallFixture.restitution = 0.82f;
        boundaryBody->CreateFixture(&wallFixture);
    };

    addWall(0.75f, 0.0f, 0.75f, 0.02f);
    addWall(0.75f, 1.2f, 0.75f, 0.02f);
    addWall(0.0f, 0.6f, 0.02f, 0.6f);
    addWall(1.5f, 0.6f, 0.02f, 0.6f);

    for (std::size_t i = 0; i < decorativeOrbCount; ++i) {
        const auto index = static_cast<int>(i);
        const auto radius = 0.045f + static_cast<float>(index % 2) * 0.012f;
        const auto position = b2Vec2(0.30f + static_cast<float>(index) * 0.18f,
                                     0.28f + static_cast<float>(index % 3) * 0.16f);

        b2BodyDef bodyDefinition;
        bodyDefinition.type = b2_dynamicBody;
        bodyDefinition.position = position;
        bodyDefinition.linearVelocity.Set((index % 2 == 0 ? 1.0f : -1.0f) * 0.34f,
                                          (index % 3 == 0 ? 0.5f : -0.35f) * 0.42f);
        bodyDefinition.linearDamping = 0.04f;
        auto *body = physicsWorld->CreateBody(&bodyDefinition);

        b2CircleShape circleShape;
        circleShape.m_radius = radius;

        b2FixtureDef orbFixture;
        orbFixture.shape = &circleShape;
        orbFixture.density = 0.7f;
        orbFixture.friction = 0.15f;
        orbFixture.restitution = 0.82f;
        body->CreateFixture(&orbFixture);

        const auto colour =
            paletteFor(false).accent.withAlpha(0.25f + static_cast<float>(index) * 0.04f);
        physicsOrbs[i] = {body, radius, colour};
        renderOrbs[i] = {juce::Point<float>(position.x, position.y), radius, colour};
    }
}
