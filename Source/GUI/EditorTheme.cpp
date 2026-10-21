// =============================================================================
//  The panel's theme: the one function that applies a theme, and the styling
//  helpers it drives.
//
//  Moved out of PluginEditor.cpp, prose and all. It is here rather than with the
//  paint routines because it is the DECISION - which theme, which palette, which
//  colour on which widget - and paint() is the drawing that follows from it.
//
//  tests/theme/check_theme_wiring.py hangs off applyTheme(): it reads every widget
//  declared in PluginEditor.h, follows the calls applyTheme() makes (one helper at
//  a time until nothing new appears) and fails on a widget the theme never reaches.
//  It reads the editor's own source as ONE text across the files the editor is
//  split into, so a helper that lives here and a widget styled from there are the
//  same graph it always was.
// =============================================================================

// The editor's own header: the class, the members and the palette type.
#include "PluginEditor.h"

// The panel vocabulary this file reads through, from the headers that hold
// it - the paint kit, the page table and the knob table.
#include "GUI/PanelPaint.h"

// Only the names this file's own text uses: a header member that is not
// reachable from here should not look reachable.
using j37panel::liveThemeChoice;
using j37panel::paletteFor;
using j37panel::paletteForTheme;
using j37panel::shrinkingFont;

void FirstAudioProcessorEditor::applyTheme() {
    // The bool selects only the dark/light rendering mode; the live enum carries
    // the specific palette, including the new Dark Metal theme.
    darkTheme = themeChoice != j37panel::ThemeChoice::ivory;
    liveThemeChoice = themeChoice;

    const auto &palette = paletteForTheme(themeChoice);
    customLookAndFeel.setTheme(themeChoice);
    themeButton.setButtonText(themeChoice == j37panel::ThemeChoice::darkMetal  ? "DARK METAL"
                              : themeChoice == j37panel::ThemeChoice::metal    ? "METAL"
                              : themeChoice == j37panel::ThemeChoice::charcoal ? "CHARCOAL"
                                                                               : "IVORY");
    // The in-tab look and feel carries its own theme for the same reason the
    // deck's does: it reads the palette live at paint time, so the lists and the
    // pill switches recolour on the same frame as everything else.
    inlineLookAndFeel.setTheme(themeChoice);
    inputMeter.setDarkTheme(darkTheme);
    outputMeter.setDarkTheme(darkTheme);

    // The 3D transport reads the same active palette as the panel.
    tapeScene.setPalette(palette.readout, palette.raised, palette.accent,
                         palette.knobFace.brighter(0.28f).withSaturation(0.55f));
    {
        const juce::SpinLock::ScopedLockType lock(renderOrbsLock);
        for (std::size_t i = 0; i < decorativeOrbCount; ++i) {
            const auto orbColour = palette.accent.withAlpha(0.25f + static_cast<float>(i) * 0.04f);
            physicsOrbs[i].colour = orbColour;
            renderOrbs[i].colour = orbColour;
        }
    }

    brandLabel.setColour(juce::Label::textColourId, palette.accent);
    titleLabel.setColour(juce::Label::textColourId, palette.text);
    subtitleLabel.setColour(juce::Label::textColourId, palette.secondary);
    statusLabel.setColour(juce::Label::textColourId, palette.status);

    // Texture doses depend on light versus dark, so update the shared weights
    // immediately when changing to the new palette.
    configureTextureWeights(panelTextureDrive, panelTextureGainReduction);
    rebuildPanelTextureLayers();
    deckHeadingLabel.setColour(juce::Label::textColourId, palette.accent);
    buildLabel.setColour(juce::Label::textColourId, palette.secondary);
    tapeTypeLabel.setColour(juce::Label::textColourId, palette.secondary);
    speedLabel.setColour(juce::Label::textColourId, palette.secondary);
    deckHintLabel.setColour(juce::Label::textColourId, palette.secondary);
    controlsHeadingLabel.setColour(juce::Label::textColourId, palette.accent);
    controlsHintLabel.setColour(juce::Label::textColourId, palette.secondary);
    sectionCaptionLabel.setColour(juce::Label::textColourId, palette.secondary);
    // The mix bar keeps its OWN dark flag rather than reading one: it is painted
    // from a child component, and a child cannot ask which theme the editor is on
    // without being told. Three lines of state instead of a wrong colour on a
    // theme the user just cycled.
    mixBar.setDarkTheme(darkTheme);
    metersHeadingLabel.setColour(juce::Label::textColourId, palette.accent);
    metersHintLabel.setColour(juce::Label::textColourId, palette.secondary);

    for (auto &label : controlLabels)
        label.setColour(juce::Label::textColourId, palette.secondary);

    // The COMP page's six captions are the same captions as the strip's - the same
    // 9 pt bold font, the same centred caption band, the same job - but they are a
    // separate array, and the constructor that builds them sets their text and
    // their font and never their ink. Unstyled, a juce::Label reads
    // Label::textColourId off the Look and Feel, which is LookAndFeel_V4's DARK
    // colour scheme whatever panel is showing: THRESH, RATIO, ATTACK, RELEASE,
    // MAKEUP and MIX were six white captions on an ivory panel, which is the
    // "font is not connected to the theme" on the COMP page.
    for (auto &label : compressorControlLabels)
        label.setColour(juce::Label::textColourId, palette.secondary);

    // Three captions the constructor inked and nothing re-inked: TUNE AMOUNT is a
    // knob page's caption now (the loop above places it in the knob strip), and the
    // TONIC and MODE captions are its peers - the two pages used to share one tab.
    // All three follow the panel like every other caption on it, instead of
    // keeping the ivory panel's ink on charcoal.
    autoTuneAmountLabel.setColour(juce::Label::textColourId, palette.secondary);
    tuningTonicLabel.setColour(juce::Label::textColourId, palette.secondary);
    tuningModeLabel.setColour(juce::Label::textColourId, palette.secondary);

    // The deck's and the tabs' remaining captions. Every label the constructor
    // styles with paletteFor(false) has to be re-inked here as well, or a theme
    // switch leaves it in the previous panel's colour - the six type selectors,
    // the machine readouts and the tab-member captions all kept ivory-brown text
    // on the charcoal and metal panels until this block existed.
    languageLabel.setColour(juce::Label::textColourId, palette.secondary);
    bpmLabel.setColour(juce::Label::textColourId, palette.secondary);
    bpmReadout.setColour(juce::Label::textColourId, palette.secondary);
    vinylTypeLabel.setColour(juce::Label::textColourId, palette.secondary);
    vinylSpeedLabel.setColour(juce::Label::textColourId, palette.secondary);
    valveTypeLabel.setColour(juce::Label::textColourId, palette.secondary);
    ampTypeLabel.setColour(juce::Label::textColourId, palette.secondary);
    transformerTypeLabel.setColour(juce::Label::textColourId, palette.secondary);
    digitalTypeLabel.setColour(juce::Label::textColourId, palette.secondary);
    diPadLabel.setColour(juce::Label::textColourId, palette.secondary);
    tracksLabel.setColour(juce::Label::textColourId, palette.secondary);
    inputEqOrderLabel.setColour(juce::Label::textColourId, palette.secondary);
    outputEqOrderLabel.setColour(juce::Label::textColourId, palette.secondary);
    vinylGenerationLabel.setColour(juce::Label::textColourId, palette.secondary);
    vinylTurntableLabel.setColour(juce::Label::textColourId, palette.secondary);
    vinylCartridgeLabel.setColour(juce::Label::textColourId, palette.secondary);
    delaySyncLabel.setColour(juce::Label::textColourId, palette.secondary);
    antiPhaseGuardLabel.setColour(juce::Label::textColourId, palette.secondary);
    neuralStatusLabel.setColour(juce::Label::textColourId, palette.secondary);
    // The IR readout re-inks itself in refreshIrStatus() on every change; this
    // covers the theme switch, which repaints without a status change.
    irStatusLabel.setColour(juce::Label::textColourId,
                            audioProcessor.hasIr() ? palette.accent : palette.secondary);
    // ... and the noise-print readout is the third of the same shape: it re-inks
    // itself on every change in refreshNoiseStatus(), and this is the theme
    // switch, which repaints without one.
    noiseStatusLabel.setColour(juce::Label::textColourId,
                               audioProcessor.hasNoisePrint() ? palette.accent : palette.secondary);

    // The tab buttons carry the theme, and styleTabButtons() reads currentTab as well,
    // so the selected tab keeps its accent highlight across a theme switch.
    styleTabButtons();

    // Every knob on the panel, not just the knob strip's. The COMP page's six and
    // the AUTOTUNE page's TUNE AMOUNT are their own arrays - they carry their own
    // parameter ids, ranges and text formatting - so a loop over `controls` alone
    // left their READOUTS unstyled, and an unstyled juce::Slider text box takes its
    // ink from the Look and Feel's colour scheme, which is V4's dark one on every
    // panel: those two pages printed their values in the same white as the labels
    // around them. One dose over all three groups, so the two cannot drift apart
    // again - and the AUTOTUNE and COMP pages read like the rest of the deck.
    const auto inkKnob = [&palette](juce::Slider &slider) {
        slider.setColour(juce::Slider::textBoxTextColourId, palette.text);
        slider.setColour(juce::Slider::textBoxBackgroundColourId, palette.readout);
        slider.setColour(juce::Slider::textBoxOutlineColourId, palette.border);
        slider.setColour(juce::Slider::thumbColourId, palette.accent);
    };

    for (auto &slider : controls)
        inkKnob(slider);

    for (auto &slider : compressorControls)
        inkKnob(slider);

    inkKnob(autoTuneAmountSlider);

    const auto styleCombo = [&palette](juce::ComboBox &box) {
        box.setColour(juce::ComboBox::backgroundColourId, palette.raised);
        box.setColour(juce::ComboBox::outlineColourId, palette.border);
        box.setColour(juce::ComboBox::textColourId, palette.text);
        box.setColour(juce::ComboBox::arrowColourId, palette.accent);
        box.setColour(juce::ComboBox::focusedOutlineColourId, palette.accent);
    };
    styleCombo(tapeTypeBox);
    styleCombo(valveTypeBox);
    styleCombo(ampTypeBox);
    styleCombo(transformerTypeBox);
    styleCombo(digitalTypeBox);
    styleCombo(vinylTypeBox);
    styleCombo(vinylSpeedBox);
    styleCombo(speedBox);
    styleCombo(presetBox);
    // The user-preset combo is the seventh ComboBox and was the one left out of this
    // list - and of the one in applyTheme() below - so it kept LookAndFeel_V4's light
    // default background on a dark panel and never followed the theme toggle.
    styleCombo(userPresetBox);
    // The language selector is drawn by the deck Look and Feel (which overrides
    // captions, not combo backgrounds), so like the user-preset list it needs its
    // palette colours set here or it keeps the base Look and Feel's own grey -
    // a box that matched nothing else on the panel. This styling block runs AFTER
    // applyLanguage above, so the box's text colour set there is overwritten by
    // this call with the palette's own text colour - the intended end state, and
    // the reason applyLanguage re-inks textColourId itself is only to colour the
    // window BETWEEN those two moments.
    styleCombo(languageBox);

    // The lists' popup menus paint through the LookAndFeel_V4 item drawing, which
    // reads THESE colour ids off the attached LookAndFeel - at theme time there is
    // no PopupMenu object to style directly, so both LookAndFeels carry the palette
    // and whichever box opens a menu uses its own. A dark panel without them opened
    // the base look's light menu, whose black text on the menu's black selection
    // highlight was unreadable (the complaint behind "text is not readable").
    const auto stylePopupMenu = [&palette](juce::LookAndFeel &lf) {
        lf.setColour(juce::PopupMenu::backgroundColourId, palette.panel.brighter(0.08f));
        lf.setColour(juce::PopupMenu::textColourId, palette.text);
        lf.setColour(juce::PopupMenu::highlightedBackgroundColourId,
                     palette.accent.withAlpha(0.85f));
        lf.setColour(juce::PopupMenu::highlightedTextColourId, palette.readout);
    };
    stylePopupMenu(customLookAndFeel);
    stylePopupMenu(inlineLookAndFeel);

    // The nothing-selected placeholder - the "Factory..." and "User..." the two
    // preset lists show before anything has been chosen - is drawn by
    // LookAndFeel_V2::drawComboBoxTextWhenNothingSelected, which reads
    // ComboBox::textColourId off the LOOK AND FEEL's own colour map and never off
    // the box it is drawing, so no per-box setColour can reach it. Unstyled it
    // followed V4's dark scheme and printed those two captions white: on the ivory
    // panel they were the first words a user reads and the ones they could not
    // read. Same reasoning as the popup menu above it - style the Look and Feel,
    // because there is no component to style. Set by NAME, on each of the two,
    // because tests/theme/check_theme_wiring.py reads exactly this statement to
    // hold the two in step.
    customLookAndFeel.setColour(juce::ComboBox::textColourId, palette.text);
    inlineLookAndFeel.setColour(juce::ComboBox::textColourId, palette.text);

    const auto styleWorkflowButton = [&palette](juce::TextButton &button, bool emphasised) {
        button.setColour(juce::TextButton::buttonColourId, palette.raised);
        button.setColour(juce::TextButton::buttonOnColourId, palette.accent);
        button.setColour(juce::TextButton::textColourOffId,
                         emphasised ? palette.text : palette.secondary);
        button.setColour(juce::TextButton::textColourOnId, palette.readout);
    };

    // bypassButton is a ToggleButton drawn by J37LookAndFeel::drawToggleButton.
    themeButton.setColour(juce::TextButton::buttonColourId, palette.raised);
    themeButton.setColour(juce::TextButton::textColourOffId, palette.text);

    // The model and IR pickers' four buttons. Their constructor says it re-inks
    // them per theme "in applyTheme()" and nothing here did, so on charcoal and
    // metal they kept the ivory panel's raised face and its dark ink - a light pill
    // with dark text on a black panel, on the one page whose whole point is that
    // the two pickers read as devices rather than as unstyled widgets. They carry
    // the same raised-face pill the workflow buttons do, from the same palette.
    const auto stylePickerButton = [&palette](juce::TextButton &button) {
        button.setColour(juce::TextButton::buttonColourId, palette.raised);
        button.setColour(juce::TextButton::textColourOffId, palette.text);
        button.setColour(juce::TextButton::buttonOnColourId, palette.raised.darker(0.15f));
    };
    stylePickerButton(loadNeuralButton);
    stylePickerButton(clearNeuralButton);
    stylePickerButton(loadIrButton);
    stylePickerButton(clearIrButton);
    stylePickerButton(loadNoiseButton);
    stylePickerButton(clearNoiseButton);
    stylePickerButton(parameterSheetButton);

    styleCombo(oversamplingBox);
    styleCombo(instrumentBox);
    // The seven tab-member lists deliberately do NOT go through styleCombo: they
    // are drawn by J37InlineLookAndFeel, which reads the palette live and has
    // its own, flatter treatment. Styling their field would fight it. The VALUE
    // colour and font below are not part of that treatment either - juce::ComboBox
    // paints its value with an internal Label reading ComboBox::textColourId, and
    // an unstyled list kept the base LookAndFeel's BLACK value on the dark panels:
    // DI PAD, TRACKS, ORDER and the vinyl trio were simply unreadable. The font is
    // set through the same Label, so the in-tab lists use the flat field's own
    // compact size instead of whatever V4 scales to the box.
    const auto styleInlineListValue = [&palette](juce::ComboBox &box) {
        box.setColour(juce::ComboBox::textColourId, palette.text);
    };
    styleInlineListValue(diPadBox);
    styleInlineListValue(tracksBox);
    styleInlineListValue(inputEqOrderBox);
    styleInlineListValue(outputEqOrderBox);
    styleInlineListValue(vinylGenerationBox);
    styleInlineListValue(vinylTurntableBox);
    styleInlineListValue(vinylCartridgeBox);
    // The tuning pair was missing from this list and from every other colour
    // call in the file, so TONIC and MODE were the only two lists on the panel
    // still painted with LookAndFeel_V4's own grey value - which on this panel
    // reads as a DISABLED control: a grey field under a live caption, beside an
    // OVERSAMPLING list that is the same widget in the same kind of cell and
    // looks nothing like it. Two lines, one cause.
    styleInlineListValue(tuningTonicBox);
    styleInlineListValue(tuningModeBox);
    // The delay trio is on the deck Look and Feel (see its construction), so it
    // follows styleCombo like every other deck list.
    styleCombo(delayTypeBox);
    styleCombo(delayRateBox);
    // The transport keys and the spindown button are TextButtons, not combos, so
    // they take their colours from styleTransportButtons() / styleSpindownButton(),
    // which read the palette AND the live state - that is what makes the active key
    // stay highlighted across a theme switch.
    styleTransportButtons();
    styleSpindownButton();
    // modeCycleButton is a TextButton whose colours styleWorkflowButton-style
    // calls do not own; it reads the palette through the look and feel, and its
    // caption is refreshed by refreshModeButtonCaption() below.
    styleCombo(userPresetBox);
    styleWorkflowButton(copyAButton, audioProcessor.getActiveCompareSlot() == 0);
    styleWorkflowButton(copyBButton, audioProcessor.getActiveCompareSlot() == 1);
    styleWorkflowButton(compareButton, false);
    styleWorkflowButton(undoButton, false);
    styleWorkflowButton(redoButton, false);
    styleWorkflowButton(savePresetButton, false);
    styleWorkflowButton(deletePresetButton, false);
    // polarityButton and autoGainButton are drawn by J37LookAndFeel::drawToggleButton
    // and need no per-theme colour calls here.
    presetHeadingLabel.setColour(juce::Label::textColourId, palette.secondary);
    oversamplingLabel.setColour(juce::Label::textColourId, palette.secondary);
    instrumentLabel.setColour(juce::Label::textColourId, palette.secondary);
    transportLabel.setColour(juce::Label::textColourId, palette.secondary);
    spindownLabel.setColour(juce::Label::textColourId, palette.secondary);
    delayTypeLabel.setColour(juce::Label::textColourId, palette.secondary);
    delayRateLabel.setColour(juce::Label::textColourId, palette.secondary);
    compareBadgeLabel.setColour(juce::Label::textColourId, audioProcessor.isCompareDirty()
                                                               ? palette.accent
                                                               : palette.secondary);

    compressorLabel.setColour(juce::Label::textColourId, palette.accent);
    compressorReadout.setColour(juce::Label::textColourId, palette.secondary);
    harmonicsLabel.setColour(juce::Label::textColourId, palette.accent);
    harmonicsReadout.setColour(juce::Label::textColourId, palette.secondary);
    subfundLabel.setColour(juce::Label::textColourId, palette.accent);
    subfundReadout.setColour(juce::Label::textColourId, palette.secondary);
    antiPhaseLabel.setColour(juce::Label::textColourId, palette.accent);
    antiPhaseReadout.setColour(juce::Label::textColourId, palette.secondary);
    compressorMeterIn.setDarkTheme(darkTheme);
    compressorMeterOut.setDarkTheme(darkTheme);

    // The caption names the theme you are ON, and the tooltip says what the next
    // press will do - so the label can never be read as "press this to become X"
    // when it is already X.
    switch (themeChoice) {
    case j37panel::ThemeChoice::ivory:
        themeButton.setButtonText("IVORY");
        break;
    case j37panel::ThemeChoice::charcoal:
        themeButton.setButtonText("CHARCOAL");
        break;
    case j37panel::ThemeChoice::metal:
        themeButton.setButtonText("METAL");
        break;
    case j37panel::ThemeChoice::darkMetal:
    default:
        themeButton.setButtonText("DARK METAL");
        break;
    }

    updateWorkflowButtons();
    // The mode button's colours read the palette (see refreshModeButtonCaption),
    // so a theme switch must re-run it - the caption is unchanged, the ink is not.
    refreshModeButtonCaption();

    // The sheet's stylesheet carries the palette it was built with, so a theme
    // switch rebuilds it rather than leaving the old colours under the new
    // panel. Rebuilt only if it had been opened; the next open builds it anyway.
    if (parameterSheet != nullptr) {
        const auto wasOpen = parameterSheet->isOpen();
        parameterSheet.reset();
        if (wasOpen)
            toggleParameterSheet();
    }

    repaint();
}

//==============================================================================
// The shared label look: text, font size and weight, colour, alignment, and no
// mouse interception so a caption never steals a click from the control under it.
// This is a member function, not a constructor-local lambda, because resized()
// styles the knob-grid section captions too and a lambda declared in the
// constructor is out of scope there - which is what broke the Linux, Windows and
// macOS builds with C2065.
void FirstAudioProcessorEditor::styleLabel(juce::Label &label,
                                           const juce::String &text,
                                           float size,
                                           juce::Colour colour,
                                           bool bold,
                                           juce::Justification justification) {
    // The small captions beside the engine switches and under the member rows
    // used to render a size (or two) larger than the control they name, which is
    // what read as off on this panel - a caption should sit UNDER its control,
    // not beside it. One size down from what the call site asks for, with a
    // slight squeeze floor so a long caption ellipsises instead of spilling into
    // the neighbour's cell.
    label.setText(text, juce::dontSendNotification);
    label.setFont(juce::Font(juce::FontOptions(juce::jmax(8.5f, size - 1.0f),
                                               bold ? juce::Font::bold : juce::Font::plain)));
    label.setJustificationType(justification);
    label.setMinimumHorizontalScale(0.82f);
    label.setColour(juce::Label::textColourId, colour);
    label.setInterceptsMouseClicks(false, false);
}

void FirstAudioProcessorEditor::styleTransportButtons() {
    const auto &palette = paletteFor(darkTheme);
    const auto state = lastShownTransport;

    const auto styleKey = [&palette](juce::TextButton &button, bool isOn) {
        button.setColour(juce::TextButton::buttonColourId, isOn ? palette.accent : palette.raised);
        button.setColour(juce::TextButton::buttonOnColourId,
                         isOn ? palette.accent : palette.raised);
        button.setColour(juce::TextButton::textColourOffId, isOn ? palette.readout : palette.text);
        button.setColour(juce::TextButton::textColourOnId, isOn ? palette.readout : palette.text);
        button.repaint();
    };

    // STOP is lit when the machine is at rest; PLAY when it is running; START while
    // the capstan is climbing - which is the one moment the other two are both
    // wrong: the machine is neither at rest nor up to speed yet.
    styleKey(transportStopButton, state == 0);
    styleKey(transportPlayButton, state == 1);
    styleKey(transportStartButton, state == 2);
}

void FirstAudioProcessorEditor::styleSpindownButton() {
    const auto &palette = paletteFor(darkTheme);
    const auto held = lastShownSpindown;

    // Held uses the needle colour rather than the accent: a spindown is a deliberate
    // fault (the platter is being starved of power), and the panel's one other red is
    // the meter needle, which is the same idea - something is being pushed.
    const auto heldColour = palette.needle;
    spindownButton.setColour(juce::TextButton::buttonColourId, held ? heldColour : palette.raised);
    spindownButton.setColour(juce::TextButton::buttonOnColourId,
                             held ? heldColour : palette.raised);
    spindownButton.setColour(juce::TextButton::textColourOffId,
                             held ? palette.readout : palette.secondary);
    spindownButton.setColour(juce::TextButton::textColourOnId,
                             held ? palette.readout : palette.secondary);
    spindownButton.repaint();
}

void FirstAudioProcessorEditor::styleTabButtons() {
    const auto &palette = paletteFor(darkTheme);

    for (int tab = 0; tab < numTabs; ++tab) {
        auto &button = tabButtons[static_cast<std::size_t>(tab)];
        const auto isOn = tab == currentTab;

        button.setColour(juce::TextButton::buttonColourId, isOn ? palette.accent : palette.raised);
        button.setColour(juce::TextButton::buttonOnColourId,
                         isOn ? palette.accent : palette.raised);
        button.setColour(juce::TextButton::textColourOffId,
                         isOn ? palette.readout : palette.secondary);
        button.setColour(juce::TextButton::textColourOnId,
                         isOn ? palette.readout : palette.secondary);
        button.repaint();
    }

    // The family buttons style themselves like the tab buttons do - pressed when
    // their slice of the bar is on screen - so the two levels read as one system.
    // Called from styleTabButtons rather than from setTabFamily, so a THEME cycle
    // repaints them too.
    const std::array<juce::TextButton *, tabFamilyCount> familyButtons{
        &tabFamilyTapeButton, &tabFamilyFxButton, &tabFamilySetupButton};
    for (std::size_t f = 0; f < familyButtons.size(); ++f) {
        const auto isOn = static_cast<int>(f) == currentTabFamily;
        familyButtons[f]->setColour(juce::TextButton::buttonColourId,
                                    isOn ? palette.accent : palette.raised);
        familyButtons[f]->setColour(juce::TextButton::buttonOnColourId,
                                    isOn ? palette.accent : palette.raised);
        familyButtons[f]->setColour(juce::TextButton::textColourOffId,
                                    isOn ? palette.readout : palette.secondary);
        familyButtons[f]->setColour(juce::TextButton::textColourOnId,
                                    isOn ? palette.readout : palette.secondary);
        familyButtons[f]->repaint();
    }
}

void FirstAudioProcessorEditor::updateWorkflowButtons() {
    // The A/B buttons carry the active side on their text, so the panel always says
    // which slot is live without needing a separate readout.
    const auto activeSlot = audioProcessor.getActiveCompareSlot();
    copyAButton.setButtonText(activeSlot == 0 ? "A (LIVE)" : "COPY A");
    copyBButton.setButtonText(activeSlot == 1 ? "B (LIVE)" : "COPY B");
    compareButton.setButtonText(activeSlot == 0 ? "A/B: B" : "A/B: A");

    // An edited dot on the inactive slot's caption: when the two stored slots differ,
    // the side you are NOT hearing has something different on it.
    const auto dirty = audioProcessor.isCompareDirty();
    copyAButton.setButtonText(copyAButton.getButtonText() + (dirty && activeSlot != 0 ? " *" : ""));
    copyBButton.setButtonText(copyBButton.getButtonText() + (dirty && activeSlot != 1 ? " *" : ""));

    auto &undoManager = audioProcessor.getUndoManager();
    undoButton.setEnabled(undoManager.canUndo());
    redoButton.setEnabled(undoManager.canRedo());

    // The toggles draw themselves through J37LookAndFeel::drawToggleButton, which
    // reads the palette live, so theme and state changes recolour on the same frame
    // without any per-colour bookkeeping here.

    // Short captions: the badge sits at the right end of the preset row, where the
    // old "A/B MATCHED" could push into the redo button on a narrow panel.
    compareBadgeLabel.setText(dirty ? "EDITED" : "MATCHED", juce::dontSendNotification);

    const auto presetName = audioProcessor.getCurrentPresetName();
    const auto presetIsDirty = audioProcessor.isPresetDirty();
    const auto presetBadgeText =
        presetName.isEmpty()
            ? "FACTORY STATE"
            : (presetIsDirty ? "PRESET: " + presetName + " (EDITED)" : "PRESET: " + presetName);
    presetBadgeLabel.setText(presetBadgeText, juce::dontSendNotification);
    // Re-fit on every caption change, not just on resize: a user can name a preset
    // anything at all, and the badge must shrink to fit rather than clip at the edge.
    presetBadgeLabel.setFont(shrinkingFont(presetBadgeText, 8.0f, juce::Font::plain,
                                           static_cast<float>(presetBadgeLabel.getWidth()) - 4.0f));
    presetBadgeLabel.setColour(juce::Label::textColourId, presetIsDirty
                                                              ? paletteFor(darkTheme).accent
                                                              : paletteFor(darkTheme).secondary);
}
