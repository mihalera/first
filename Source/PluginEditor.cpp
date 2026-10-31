#include "PluginProcessor.h"

// The generated resource accessors: the factory presets and the translation
// tables, both compiled into the binary by CMakeLists.txt.
#include <BinaryData.h>
#include "PluginEditor.h"

#include <array>
#include <iostream>
#include <utility>
#include <vector>

#if JUCE_DEBUG
 #include <melatonin_inspector/melatonin_inspector.h>
#endif

//==============================================================================
//  Interface sounds.
//
//  The engine - a four-voice tick synthesiser on its own audio device, with
//  the four constraints it is designed under - is Source/GUI/UiSoundEngine.cpp.
//  It moved there with those constraints, because they are the reason it is a
//  separate device rather than a voice in the plugin's engine, and a reader of
//  this file was reading them as a parenthesis about the panel.
//==============================================================================


// The panel's own vocabulary, moved out of this file whole: the palettes and
// drawing helpers (Source/GUI/PanelPaint.h), the page table the tabs are built
// from (Source/GUI/PanelTabs.h), and the knob table with the sentence every
// parameter shows (Source/GUI/PanelText.h). The using block below is what every
// call site in this file already reads through.
#include "GUI/PanelTabs.h"
#include "GUI/PanelPaint.h"
#include "GUI/PanelText.h"
// The deck's vertical rhythm: the band solve placeDeck reads instead of a table
// of fixed offsets, and the one header tests/layout can compile without JUCE.
#include "GUI/DeckBands.h"
#include "GUI/PanelTranslations.h"
#include "GUI/RenderBackend.h"
// The one machine-wide preferences file: the pickers' folders and the renderer
// selector's choice live there together (see the header for why it is ONE file).
#include "GUI/EditorPreferences.h"

// The panel's paint kit and page table, from Source/GUI/PanelPaint.h and
// Source/GUI/PanelTabs.h. Every call site below was written when the two lived
// in this file's anonymous namespace, so these are what keep them reading as
// they did instead of becoming j37panel::... in four hundred places. The names
// this file does not use are not imported: a header member that stops being
// reachable from here should not look reachable.
using j37panel::minStageDb;
using j37panel::maxStageDb;
using j37panel::liveThemeChoice;
using j37panel::paletteForTheme;
using j37panel::paletteFor;
using j37panel::principleColour;
using j37panel::principleParameterIds;
using j37panel::principleOf;
using j37panel::fillCylinderBarrel;
using j37panel::fillLitDome;
using j37panel::tabSpecs;
using j37panel::tabSubtitles;
using j37panel::controlsDividerOffset;
using j37panel::controlIsInTab;
using j37panel::tabsCoverAllControls;
using j37panel::index_of_tab_named;
using j37panel::formatDb;
using j37panel::shrinkingFont;
using j37panel::drawScrew;
using j37panel::drawPanel;
using j37panel::ControlSpec;
using j37panel::controlSpecs;
using j37panel::parameterTooltipText;


// The knob's radial budget - KnobMetrics and computeKnobMetrics - is
// Source/GUI/KnobMetrics.h. It is called from here (the strip solver in
// resized()) and from J37LookAndFeel::drawRotarySlider, which is
// Source/GUI/J37LookAndFeel.cpp, so it had to stop being file-local.
#include "GUI/KnobMetrics.h"
using j37panel::KnobMetrics;
using j37panel::computeKnobMetrics;


//==============================================================================
//  The two look-and-feels. J37LookAndFeel paints the knobs, the switches, the
//  labels and the panel's camera; J37InlineLookAndFeel paints the deck's inline
//  lists. Both are declared in PluginEditor.h and defined in
//  Source/GUI/J37LookAndFeel.cpp, which is where the drawing now lives.
//
//  The combo-box inset arithmetic they share (comboTextInset) moved with them:
//  it is a look-and-feel's own placement, not the panel's geometry.
//==============================================================================


//==============================================================================
//  The three meters: the input and output LevelMeter, the CompressorMeter and
//  the MIX page's MixBar. Defined in Source/GUI/EditorMeters.cpp; declared in
//  PluginEditor.h above.
//==============================================================================
















//==============================================================================
//  Interface sounds: attaching the four voices to the panel's controls.
//
//  Every control that has a distinct physical feel gets the voice that matches
//  it, so the panel sounds like hardware rather than like a generic UI:
//
//    switches and toggles   a CLICK, because that is what a rocker does
//    the momentary keys     PRESS, because that is a key being pushed down
//    every knob             a DETENT, but ONLY on the value change - not on
//                           every mouse move, which would be a machine-gun
//
//  WHY onClick AND NOT onStateChange. A juce::TextButton that does not toggle
//  never changes its toggle state, so Button::onStateChange never fires for it -
//  which is why the tab bar, the three transport keys, SPINDOWN, PARAMETERS and
//  the six LOAD/CLEAR keys were all silent: they were wired through a callback
//  their kind of button does not send. onClick fires for every user click of
//  every Button subclass, so one helper now carries the whole panel, and a
//  toggle still gets exactly one sound because a click is still one click. The
//  button's own handler is left in place and called after the sound, so a
//  control that opens a dialog or rebuilds the page is not rewritten here.
//
//  The knobs are the interesting case. A real detented pot ticks as it passes
//  each step, so the sound is driven by the VALUE crossing a boundary rather
//  than by the mouse moving. juce::Slider::onValueChange fires for exactly that
//  (a real value change), and it does not fire for a mouse move that leaves the
//  value where it was - which is precisely the distinction wanted. The rate is
//  then limited, because a fast drag would otherwise fire dozens of ticks a
//  second and sound like a buzz rather than a detent.
//==============================================================================
void FirstAudioProcessorEditor::attachInterfaceSounds()
{
    // The one wrapper. It chains onto whatever the button already does rather
    // than replacing it: the click is played first, so a handler that opens a
    // dialog cannot swallow it, and the existing handler runs immediately after.
    const auto attachVoice = [this] (juce::Button& button, UiSoundEngine::Voice voice)
    {
        const auto previous = button.onClick;
        button.onClick = [this, voice, previous]
        {
            uiSounds.trigger (voice);

            if (previous != nullptr)
                previous();
        };
    };

    const auto playClick = [this] { uiSounds.trigger (UiSoundEngine::Voice::click); };

    // juce::Button* named explicitly: the members mix ToggleButton and
    // TextButton, and an initializer_list cannot be deduced from a
    // heterogeneous braced list - the common base is what the loop wants.
    const std::array<juce::Button*, 15> stateButtons { { &bypassButton, &deltaButton, &polarityButton, &autoGainButton, &oscButton,
                          &modeCycleButton, &delaySyncButton,
                          &themeButton, &savePresetButton, &deletePresetButton,
                          &copyAButton, &copyBButton, &compareButton,
                          &undoButton, &redoButton } };
    for (auto* button : stateButtons)
        attachVoice (*button, UiSoundEngine::Voice::click);

    // The transport keys and SPINDOWN: momentary keys, so they take the press
    // voice rather than the switch's click - which is how a deck's own keys
    // sound. SpindownButton is a TextButton subclass and converts to Button&.
    const std::array<juce::Button*, 4> transportKeys { { &transportStopButton, &transportPlayButton,
                          &transportStartButton, &spindownButton } };
    for (auto* button : transportKeys)
        attachVoice (*button, UiSoundEngine::Voice::press);

    // The tab bar: a row of switches, each with its own voice.
    for (auto& tab : tabButtons)
        attachVoice (tab, UiSoundEngine::Voice::click);

    for (auto* button : { &tabFamilyTapeButton, &tabFamilyFxButton, &tabFamilySetupButton })
        attachVoice (*button, UiSoundEngine::Voice::click);

    // The rest of the panel's keys: the SETTINGS preferences, the four EQ corner
    // switches and the three LOAD/CLEAR pairs. They were the group the old
    // onStateChange wiring could not reach - the toggles sent no click because
    // nothing listened, and the TextButtons sent no state change at all.
    const std::array<juce::Button*, 15> utilityButtons { { &autoTuneButton, &uiSoundsButton, &antiPhaseGuardButton,
                          &parameterSheetButton, &userThemeButton,
                          &inputEqHpButton, &inputEqLpButton,
                          &outputEqHpButton, &outputEqLpButton,
                          &loadNeuralButton, &clearNeuralButton,
                          &loadIrButton, &clearIrButton,
                          &loadNoiseButton, &clearNoiseButton } };
    for (auto* button : utilityButtons)
        attachVoice (*button, UiSoundEngine::Voice::click);

    // Every knob: a detent per value change, rate-limited so a fast drag ticks
    // rather than buzzes. The counter is a member rather than a lambda capture
    // because a `mutable` capture inside a loop would give every knob its own
    // independent budget, and a drag that moved two knobs would tick twice as
    // often.
    for (auto& slider : controls)
    {
        slider.onValueChange = [this]
        {
            if (uiSoundTickCountdown > 0)
            {
                --uiSoundTickCountdown;
                return;
            }

            // At 30 timer frames a second, decrementing once per frame through
            // the existing timer gives roughly a 100 ms gate - about ten ticks a
            // second, which is a detent rather than a machine-gun.
            uiSoundTickCountdown = uiSoundTickGateFrames;
            uiSounds.trigger (UiSoundEngine::Voice::detent);
        };
    }

    // The combo boxes (the deck's type switches): a click when the selection
    // actually changes, which is what flipping a rotary switch feels like.
    //
    //  THE OLD CODE WAS THE PRESET BUG. `box->onChange = ...` ASSIGNS the
    //  std::function, so for presetBox and userPresetBox this line REPLACED the
    //  handler the constructor had installed two hundred lines earlier - the
    //  one that applies the preset through the processor. attachInterfaceSounds
    //  runs last, so the sound wrapper won: a click played, and the machine
    //  kept the values it had, on every preset, every time. The buttons in this
    //  file never had this bug because they were already wrapped as a CHAIN
    //  (previous + play) - the boxes now take exactly that chain too: the
    //  previous handler first, the click on top.
    for (auto* box : { &tapeTypeBox, &valveTypeBox, &ampTypeBox, &transformerTypeBox,
                       &digitalTypeBox, &vinylTypeBox, &vinylSpeedBox, &speedBox,
                       &instrumentBox, &oversamplingBox, &presetBox, &userPresetBox,
                       &delayTypeBox, &delayRateBox, &vinylGenerationBox,
                       &vinylTurntableBox, &vinylCartridgeBox })
        if (box != nullptr)
        {
            const auto previous = box->onChange;
            box->onChange = [playClick, previous]
            {
                if (previous != nullptr)
                    previous();
                playClick();
            };
        }
}







void FirstAudioProcessorEditor::setCurrentTab (int newTab)
{
    // The tab table and the control list are two views of one set of knobs, and this is
    // the place that depends on it holding. numTabs and tabSpecs drifting apart would
    // leave a tab with no table entry; a control listed by no tab (or by two) would be
    // a knob on the panel that is unreachable or duplicated.
    static_assert (tabSpecs.size() == static_cast<std::size_t> (numTabs),
                   "numTabs and tabSpecs must describe the same number of tabs");
    static_assert (tabsCoverAllControls<controlCount> (tabSpecs),
                   "every knob must be listed by exactly one tab");
    static_assert (tabSubtitles.size() == tabSpecs.size(),
                   "every tab page needs its own section caption");

    // Read before the assignment: the page's own painted furniture has to be
    // invalidated with the page (see the repaint at the foot of this function).
    const auto previousTab = currentTab;
    currentTab = juce::jlimit (0, numTabs - 1, newTab);
    // The section caption: the active page's own one-line subject, from the same
    // index the page itself came from, so the two can never name different pages.
    sectionCaptionLabel.setText (tabSubtitles[static_cast<std::size_t> (currentTab)],
                                 juce::dontSendNotification);
    // paint() gates page furniture on this, looked up by NAME, not by index: a
    // name cannot drift the way a hard-coded page number did when the pages
    // were reordered.
    activeTabIsShapers = (std::strcmp (tabSpecs[static_cast<std::size_t> (currentTab)].name,
                                       "MODELS") == 0);

    // The family follows the page: the bar's two rows are two views of one
    // state, and a path that set one without the other left the bar showing a
    // family that the page was not in - the number-key shortcuts did exactly
    // that, and the panel could then open a page with NO tab and NO family lit.
    // setTabFamily returns through resized() here rather than back through this
    // function, because the page it is given is already inside the family it is
    // switching to (its range test is false by construction).
    if (tabSpecs[static_cast<std::size_t> (currentTab)].family != currentTabFamily)
        setTabFamily (tabSpecs[static_cast<std::size_t> (currentTab)].family);

    for (std::size_t i = 0; i < controlCount; ++i)
    {
        const auto onActiveTab = controlIsInTab (i, currentTab);
        controls[i].setVisible (onActiveTab);
        controlLabels[i].setVisible (onActiveTab);
    }

    // The deck's non-knob switches are FULL MEMBERS of their tabs: resized() lays
    // them out on the same grid the knobs use AND re-asserts which of them are on
    // screen - the visibility note lives beside that layout. Without the resized()
    // call a tab switch changed currentTab and drew nothing, and without
    // styleTabButtons() the selected tab never took its selected colours.
    styleTabButtons();
    resized();

    // A page's own painted furniture has to leave with the page. The MODELS
    // card behind the model picker is drawn by THIS component's paint(), not by
    // the picker's widgets - and the 30 Hz timer only repaints the knobs, the
    // switches, the lamp and the deck (see repaintAnimatedSurfaces), which is why
    // hiding the picker was not enough: its own cells were restored and the card
    // around and behind them stayed, as the phantom squares on every other page.
    // The controls panel is the only band paint() varies by tab, so that is the
    // region, and only when the page actually changed.
    if (previousTab != currentTab)
        repaint (getEditorLayout().controls);
}



void FirstAudioProcessorEditor::setTip (juce::SettableTooltipClient& component, const juce::String& english)
{
    tooltipSources.push_back ({ &component, english });
    component.setTooltip (xlat (english));
}

void FirstAudioProcessorEditor::setTippedSentence (juce::SettableTooltipClient& component,
                                                   const juce::String& englishSentence,
                                                   const juce::String& englishHints)
{
    // The knob tooltips are sentence + interaction hints in ONE string, but the
    // lookup key is the sentence alone. Storing the concatenation was the bug:
    // "INPUT - ... 0 dB. Hold Shift for..." is not in the table, so the language
    // switch replayed the English every time no matter how complete the file was.
    // Here the sentence is registered and shown separately, the hints are a
    // constant that xlat()s on the spot, and updateTooltips() re-translates
    // each half on its own key.
    tooltipSources.push_back ({ &component, englishSentence });
    component.setTooltip (xlat (englishSentence) + xlat (englishHints));
}

void FirstAudioProcessorEditor::updateTooltips()
{
    // The English is replayed through the CURRENT table rather than the text
    // that is on the component being overwritten - translating an already
    // translated string would be a lookup that can never hit, because no key in
    // the table is a translation.
    for (const auto& source : tooltipSources)
        if (source.component != nullptr)
            source.component->setTooltip (xlat (source.english));
}

void FirstAudioProcessorEditor::applyLanguage (int languageIndex)
{
    // The tables are compiled into the binary by juce_add_binary_data (see the
    // TRANSLATION_FILES list in CMakeLists.txt), so there is nothing to install
    // beside the plugin and nothing for the user to delete. Adding a language is
    // one file plus one line in that list.
    const char* text = nullptr;
    int size = 0;

    if (languageIndex == 1)
    {
        text = BinaryData::uk_txt;
        size = BinaryData::uk_txtSize;
    }
    else
    {
        text = BinaryData::en_txt;
        size = BinaryData::en_txtSize;
    }

    // setCurrentMappings TAKES OWNERSHIP and deletes whatever it was given
    // before, so this is the whole of the memory management: no delete, no
    // member to keep, and no leak on the second switch. The second argument is
    // "this text is already in the wrong encoding", which is false - the files
    // are UTF-8, and the plugin reads them as such.
    juce::LocalisedStrings::setCurrentMappings (
        new juce::LocalisedStrings (juce::String::createStringFromData (text, size), false));

    // The language selector's own rows are keyed strings, and they are added to
    // the box BEFORE this function first runs - through a table that did not
    // exist yet, so the box showed its raw keys ("LANGUAGE_...") where the
    // language names belong. Re-apply the rows through the table just installed:
    // the items exist on every path that reaches here (the constructor adds them
    // before its first call), and a later switch re-translates them the same way.
    languageBox.changeItemText (1, xlat ("LANGUAGE_ENGLISH"));
    languageBox.changeItemText (2, xlat ("LANGUAGE_UKRAINIAN"));

    // ... and then re-select, because changeItemText() rewrites the POPUP's own
    // row and NOTHING else: juce::ComboBox keeps the string it is displaying in
    // its value Label, and only setSelectedId()/setText() ever write that. The
    // item texts above were therefore correct while the field kept whatever it
    // was showing - the raw key it was added with, since the constructor adds
    // both rows before any table exists. This is the row the user was looking
    // at, so it is the one that has to be re-applied; the index this call was
    // handed is its ID minus one, which is the only mapping the box cannot get
    // wrong (getSelectedId() itself returns 0 while the field is showing text
    // that does not match its rows).
    languageBox.setSelectedId (juce::jlimit (1, 2, languageIndex + 1),
                               juce::dontSendNotification);
    // applyLanguage runs BEFORE the constructor finishes its styling block
    // (styleCombo below), so re-ink the box's text colour here as well: on a
    // session that opens Ukrainian, the constructor's paletteFor(false) would
    // otherwise repaint the box with the theme's text colour and wipe this -
    // which is exactly the raw-key value the user screenshotted.
    languageBox.setColour (juce::ComboBox::textColourId, paletteFor (darkTheme).text);
    languageBox.repaint();

    // The caption above that selector is a keyed string too, so it follows the
    // new table as well. It used to be styled once in the constructor and keep
    // the previous language until the editor was rebuilt.
    languageLabel.setText (xlat ("LANGUAGE_CAPTION").toUpperCase(), juce::dontSendNotification);

    // The PARAMETERS key is a keyed string as well, and its sheet's captions
    // were translated with the table that was installed when it was BUILT. The
    // sheet is created once and kept (109 attachments are not rebuilt on every
    // press), so the honest way to re-language it is to release it: the next
    // press builds a sheet in the language the user just chose. Re-setting 109
    // captions in place would be the same work with a second code path.
    parameterSheetButton.setButtonText (xlat ("OPEN"));
    parameterSheetLabel.setText (xlat ("PARAMETERS").toUpperCase(), juce::dontSendNotification);
    parameterSheet.reset();

    updateTooltips();
}

void FirstAudioProcessorEditor::setTabFamily (int newFamily)
{
    // The tab table's family column is the single source of truth: each page
    // states which family hosts it, and this strip reads the column rather
    // than a parallel count array that could drift from the table. The assert
    // pins the CABINET of pages - nine TAPE, six FX, three SETUP - so a page
    // moved between families is a build error rather than a button that lights
    // wrong, and the totals cannot drift from the family buttons' labels.
    //
    // Table ORDER is deliberately not asserted. The bar is a FILTER: it walks
    // the table once and shows the pages whose family matches, so AUTOTUNE and
    // COMP sitting at the end of the table - they were added after SETTINGS -
    // still land at the end of FX. Demanding contiguous families here would
    // force a reordering of the table and of the subtitle array beside it for a
    // bar that was already right, which is the kind of "fix" that breaks the
    // two things it was meant to protect.
    static_assert (tabSpecs.size() == static_cast<std::size_t> (numTabs),
                   "the tab family layout and tab table must have equal size");
    static_assert ([]
    {
        std::array<int, tabFamilyCount> familyCounts {};
        for (const auto& tab : tabSpecs)
        {
            if (tab.family < 0 || tab.family >= tabFamilyCount)
                return false;
            ++familyCounts[static_cast<std::size_t> (tab.family)];
        }
        return familyCounts[0] == 9 && familyCounts[1] == 6 && familyCounts[2] == 3;
    }(), "tab families must contain 9 TAPE, 6 FX and 3 SETUP pages");

    // A family change only filters the tab bar's visibility and jumps to the
    // family's first page: currentTab is set through setCurrentTab so the
    // knobs' visibility, the static_asserts and the styling all run through
    // the one authoritative path - the same path a click on any tab takes.
    const auto family = juce::jlimit (0, tabFamilyCount - 1, newFamily);
    currentTabFamily = family;

    int firstOfFamily = numTabs;
    int lastOfFamily = 0;
    for (int tab = 0; tab < numTabs; ++tab)
    {
        if (tabSpecs[static_cast<std::size_t> (tab)].family == family)
        {
            firstOfFamily = juce::jmin (firstOfFamily, tab);
            lastOfFamily = juce::jmax (lastOfFamily, tab + 1);
        }
    }

    for (int tab = 0; tab < numTabs; ++tab)
        tabButtons[static_cast<std::size_t> (tab)]
            .setVisible (tabSpecs[static_cast<std::size_t> (tab)].family == family);

    if (currentTab < firstOfFamily || currentTab >= lastOfFamily)
        setCurrentTab (firstOfFamily);
    else
        resized();
}

//==============================================================================
//  The two file pickers' shared ground: where they OPEN, and what happens to the
//  stage's blend when a load SUCCEEDS.
//==============================================================================
namespace
{
    // The folder each picker starts in, remembered across sessions.
    //
    // The chooser used to open on the user's Documents folder every single time,
    // so a user who keeps their models and their impulse responses somewhere else
    // walked back to that place on every load. The memory is machine-wide (a
    // settings file beside the plugin's other user data) rather than session
    // state on purpose: it is a convenience of USING the plugin, not part of the
    // sound, so a saved session must not carry the folder a file was picked from
    // and a preset must not be able to move it.
    //
    // One file for the whole plugin and one KEY per picker, so the two stages
    // never fight over a single "last folder" - models and IRs live in different
    // places and each picker returns to its own.
    // The store itself is Source/GUI/EditorPreferences.h: the renderer selector
    // remembers its choice in the SAME file, and two caches of one file would
    // overwrite each other's keys on save.
    juce::PropertiesFile& folderMemory()
    {
        return j37panel::editorPreferences();
    }

    juce::File rememberedFolder (const juce::String& key)
    {
        const auto remembered = juce::File (folderMemory().getValue (key));

        // A remembered folder that has since been moved or unmounted falls back
        // to Documents rather than opening the chooser on a path that is gone.
        return remembered.isDirectory() ? remembered
                                        : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    }

    void rememberFolder (const juce::String& key, const juce::File& chosen)
    {
        const auto folder = chosen.isDirectory() ? chosen : chosen.getParentDirectory();
        if (! folder.isDirectory())
            return;

        folderMemory().setValue (key, folder.getFullPathName());
        folderMemory().saveIfNeeded();
    }
}

void FirstAudioProcessorEditor::openStageMix (const juce::String& parameterId)
{
    // Loading a file is a request to HEAR the file. Both file-fed stages default
    // their MIX to 0 - a stage with nothing loaded is a bypass, and a session that
    // unexpectedly carries a model must still load to the sound an earlier build
    // made - so a picker that installed the model and left the blend shut changed
    // nothing audible, which is exactly what "LOAD MODEL / LOAD IR does nothing"
    // was reporting.
    //
    // The blend is opened to full ONLY while it is still fully closed, so a user
    // who has dialled a partial blend keeps it, and the change goes through the
    // parameter rather than the atomic so the host records it, the knob's
    // attachment follows, and Undo can take it back. The test is against
    // "closed" rather than "not already full" on purpose: the second one is a
    // different sentence - it would override a blend the user set - and it is the
    // one the comment above would have been lying about.
    if (auto* parameter = audioProcessor.parameters.getParameter (parameterId))
        if (parameter->getValue() <= 1.0e-6f)
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (1.0f);
            parameter->endChangeGesture();
        }
}

void FirstAudioProcessorEditor::loadNeuralModelFromFile()
{
    // A model is chosen from a file, so a plain asynchronous file chooser is the
    // whole dialogue. The chooser is retained while active and captured to keep
    // it alive; SafePointer below prevents callbacks from accessing a closed editor.
    if (neuralFileChooser.active)
        return;
    //
    // "*.nam" is NeuralAmpModeler's own extension - its files carry the SAME
    // networks in a different wire format (one flat weight vector rather than
    // a stack of named layers), and the picker reads both through the
    // processor's own two entry points below. Keep one picker active per editor;
    // the callback uses a SafePointer because a host may close the editor while
    // the native file dialog is still open.
    if (neuralFileChooser.active)
        return;

    auto chooser = std::make_shared<juce::FileChooser> (
        "Load a neural model",
        rememberedFolder ("lastModelFolder"),
        "*.json;*.rtn;*.nam;*");
    neuralFileChooser.chooser = chooser;
    neuralFileChooser.active = true;

    // browserFlags, not `flags`: a local name that shadows nothing is cheaper
    // than proving no future member will ever be called flags (C4458).
    const auto browserFlags = juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync (browserFlags,
        [safeThis = juce::Component::SafePointer<FirstAudioProcessorEditor> (this), chooser]
        (const juce::FileChooser& fc)
    {
        if (safeThis == nullptr)
            return;
        auto& editor = *safeThis;
        if (editor.neuralFileChooser.chooser != chooser)
            return;
        editor.neuralFileChooser = {};
        const auto file = fc.getResult();
        if (! file.existsAsFile())
            return;

        //  The folder is remembered the moment a file is CHOSEN, not when it
        //  loads. A picker that only remembered a folder after a successful
        //  load reopened on Documents after every refused file - and a refused
        //  model is exactly when a user goes looking for another one, in the
        //  same folder. The name is set at the same point so the readout names
        //  the file that was just picked even if the stage refuses it.
        const auto previousName = editor.audioProcessor.getLoadedNeuralName();
        editor.audioProcessor.setLoadedNeuralName (file.getFileNameWithoutExtension());
        rememberFolder ("lastModelFolder", file);

        // A .nam file goes through its own converter; every other extension
        // keeps the RTNeural JSON path. Both load the same kind of network -
        // the wire format is the only thing that differs.
        const auto isNam = file.hasFileExtension (".nam");
        const auto text = file.loadFileAsString();
        const bool loaded = isNam ? editor.audioProcessor.loadNamModel (text)
                                  : editor.audioProcessor.loadNeuralModel (text);

        if (! loaded)
        {
            // A refused file leaves whatever was loaded before in place, and the
            // readout says WHY this one was refused - a .nam that needs the LSTM
            // architecture is a different download, not a corrupt file, and the
            // reason is the one thing the user can act on.
            editor.audioProcessor.setLoadedNeuralName (previousName);
            editor.neuralStatusReason = editor.audioProcessor.lastLoadError();
            editor.neuralStatusDetail = editor.audioProcessor.lastLoadDetail();
            editor.lastShownNeuralStatus = {};   // force the next refresh to repaint the label
            editor.refreshNeuralStatus();
            return;
        }

        editor.neuralStatusReason = {};
        editor.neuralStatusDetail = {};
        editor.lastShownNeuralStatus = {};   // force the next refresh to repaint the label
        editor.openStageMix ("neural_mix");
        editor.refreshNeuralStatus();
    });
}



void FirstAudioProcessorEditor::loadIrFromFile()
{
    // Same asynchronous-chooser shape as the model picker: the chooser is kept
    // alive by its own shared_ptr for the length of the callback, and the
    // editor's handle makes a second click while one dialog is open a no-op for
    // the same reason it is one on LOAD MODEL. It used to launch a chooser and
    // never record it, so `irFileChooser` was a member nothing ever set - and a
    // host closing the editor could not cancel a dialog it was still holding.
    if (irFileChooser.active)
        return;

    auto chooser = std::make_shared<juce::FileChooser> (
        "Load an impulse response",
        rememberedFolder ("lastIrFolder"),
        "*.wav;*.aif;*.aiff;*");
    irFileChooser.chooser = chooser;
    irFileChooser.active = true;

    const auto browserFlags = juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync (browserFlags,
        [safeThis = juce::Component::SafePointer<FirstAudioProcessorEditor> (this), chooser]
        (const juce::FileChooser& fc)
    {
        if (safeThis == nullptr)
            return;

        auto& editor = *safeThis;
        if (editor.irFileChooser.chooser != chooser)
            return;
        editor.irFileChooser = {};

        const auto file = fc.getResult();
        if (! file.existsAsFile())
            return;

        //  Remembered on CHOICE, like the model picker beside it: an IR that
        //  fails to decode still tells the user where their IRs live.
        const auto previousName = editor.audioProcessor.getLoadedIrName();
        editor.audioProcessor.setLoadedIrName (file.getFileNameWithoutExtension());
        rememberFolder ("lastIrFolder", file);

        if (! editor.audioProcessor.loadIrFile (file))
        {
            // A file that would not decode leaves the previous IR in place and
            // says WHICH failure it was, in the same shape the model picker uses.
            editor.audioProcessor.setLoadedIrName (previousName);
            editor.irStatusReason = editor.audioProcessor.lastLoadError();
            editor.irStatusDetail = editor.audioProcessor.lastLoadDetail();
            editor.lastShownIrStatus = {};      // force the next refresh to repaint
            editor.refreshIrStatus();
            return;
        }

        editor.irStatusReason = {};
        editor.irStatusDetail = {};
        editor.lastShownIrStatus = {};      // force the next refresh to repaint
        editor.openStageMix ("ir_mix");
        editor.refreshIrStatus();
    });
}

void FirstAudioProcessorEditor::loadNoisePrintFromFile()
{
    // Same asynchronous-chooser shape as the two pickers above. The filter names
    // the one format this build DECODES - dr_wav is a WAV decoder, not a general
    // audio-file reader (see Source/DSP/NoiseDecoder.h) - and "*" keeps anything
    // else selectable, so a user who picks an AIFF is told by the readout what
    // it was rather than left wondering why the file is not in the dialogue at
    // all.
    if (noiseFileChooser.active)
        return;

    auto chooser = std::make_shared<juce::FileChooser> (
        "Load a surface-noise print",
        rememberedFolder ("lastNoiseFolder"),
        "*.wav;*");
    noiseFileChooser.chooser = chooser;
    noiseFileChooser.active = true;

    const auto browserFlags = juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync (browserFlags,
        [safeThis = juce::Component::SafePointer<FirstAudioProcessorEditor> (this), chooser]
        (const juce::FileChooser& fc)
    {
        if (safeThis == nullptr)
            return;

        auto& editor = *safeThis;
        if (editor.noiseFileChooser.chooser != chooser)
            return;
        editor.noiseFileChooser = {};

        const auto file = fc.getResult();
        if (! file.existsAsFile())
            return;

        // Choice, not success: the print's decode finishes later and can fail,
        // but the folder is where the user's prints are either way.
        rememberFolder ("lastNoiseFolder", file);

        // Whatever this returns, the answer is NOT known here: a print is read
        // and decoded on the loader's own thread, so the processor only refuses
        // a file that is not there. Everything else - bad WAV, no frames, NaN,
        // over the minute cap - arrives through getNoiseError() on a later
        // tick, which is exactly what the readout is for.
        editor.audioProcessor.loadNoiseFile (file);

        editor.noiseStatusReason = {};
        editor.lastShownNoiseStatus = {};    // force the next refresh to repaint
        editor.refreshNoiseStatus();
    });
}



//==============================================================================
//  The knob table and its tooltip text are Source/GUI/PanelText.h: one row per
//  control and one sentence per parameter, which is DATA the panel is built
//  from rather than code that builds it.
//==============================================================================


FirstAudioProcessorEditor::FirstAudioProcessorEditor (FirstAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // The panel the desk was left on, read BEFORE the build phases and before the
    // applyTheme() at the end of them, so the first frame is already on it. A host
    // destroys and re-creates an editor every time its window is closed, which is why
    // the themeChoice member's own default (IVORY) was what a USER panel came back on.
    // The choice and the USER palette are machine-wide preferences, the same file the
    // renderer uses (Source/GUI/EditorPreferences.h) - they must outlive the editor and
    // must not travel in a session.
    themeChoice = j37panel::rememberedThemeChoice();
    j37panel::userPalette = j37panel::rememberedUserPalette();

    // The OSC generator exists only while the panel does: its hops are this
    // editor's timer, so with no window there is no clock and the last note would
    // sit there droning. Announced here rather than in a setup phase because it is
    // about the window's LIFETIME, not about any control - and undone first thing
    // in the destructor, for the same reason.
    audioProcessor.setOscPanelOpen (true);

    // One call per phase, in the order the panel has always been built in. The
    // window and its size come first because setSize() runs resized(), which is why
    // the tab build below can rely on the editor already having a size; the
    // attachments and handlers follow the controls they belong to, and the render
    // context and the timer come last. Each phase is declared in PluginEditor.h -
    // see the note there.
    setUpWindowAndSize();
    createDebugInspector();
    setUpLanguageAndTuning();
    setUpTooltipsAndPhysics();
    stylePanelLabels();
    setUpControlStrip();
    setUpTabButtons();
    setUpDeckSelectors();
    setUpTopSwitches();
    setUpOversamplingAndInstrument();
    setUpVinylSelectors();
    setUpTransportRow();
    setUpDelayRow();
    setUpModeSwitches();
    setUpSettingsSwitches();
    setUpModelButtons();
    setUpInlineLists();
    setUpEqCornerSwitches();
    setUpPresetBar();
    addDeckControlsToPanel();
    setUpSceneAndMeters();
    setUpOpenGlRendering();
    finishConstruction();
}

















































FirstAudioProcessorEditor::~FirstAudioProcessorEditor()
{
    // The editor is the last thing to go when a host closes a plugin window, so this
    // is the last chance to remember a USER palette edited but never re-selected. The
    // theme choice itself is written when the key is pressed; this catches the colours.
    // See rememberPanelAppearance() in Source/GUI/EditorTheme.cpp.
    rememberPanelAppearance();

    // The window is going away, so the ball stops hopping: silence the generator
    // before the clock that lands it is stopped.
    audioProcessor.setOscPanelOpen (false);

    stopTimer();

    // The native scene adapter holds an OpenGLContext attached to tapeScene;
    // release it before anything it points at is torn down.
    shutDownNativeScene();

    openGLContext.detach();

    // A FileChooser owns its asynchronous operation. Releasing it here cancels
    // the picker and discards its callback before the editor or processor refs
    // can be accessed by a later completion.
    neuralFileChooser.reset();
    irFileChooser.reset();
    noiseFileChooser.reset();

    // FileChooser callbacks use SafePointer, so clearing these handles prevents
    // teardown from retaining a native dialog beyond the editor's lifetime.
    neuralFileChooser = {};
    irFileChooser = {};
    noiseFileChooser = {};

    for (auto& slider : controls)
        slider.setLookAndFeel (nullptr);

    tapeTypeBox.setLookAndFeel (nullptr);
    valveTypeBox.setLookAndFeel (nullptr);
    ampTypeBox.setLookAndFeel (nullptr);
    transformerTypeBox.setLookAndFeel (nullptr);
    digitalTypeBox.setLookAndFeel (nullptr);
    vinylTypeBox.setLookAndFeel (nullptr);
    vinylSpeedBox.setLookAndFeel (nullptr);
    speedBox.setLookAndFeel (nullptr);
    bypassButton.setLookAndFeel (nullptr);
    deltaButton.setLookAndFeel (nullptr);
    themeButton.setLookAndFeel (nullptr);
    presetBox.setLookAndFeel (nullptr);
    copyAButton.setLookAndFeel (nullptr);
    copyBButton.setLookAndFeel (nullptr);
    compareButton.setLookAndFeel (nullptr);
    undoButton.setLookAndFeel (nullptr);
    redoButton.setLookAndFeel (nullptr);
    polarityButton.setLookAndFeel (nullptr);
    autoGainButton.setLookAndFeel (nullptr);
    oscButton.setLookAndFeel (nullptr);
    oversamplingBox.setLookAndFeel (nullptr);
    instrumentBox.setLookAndFeel (nullptr);
    vinylGenerationBox.setLookAndFeel (nullptr);
    vinylTurntableBox.setLookAndFeel (nullptr);
    vinylCartridgeBox.setLookAndFeel (nullptr);
    uiSoundsButton.setLookAndFeel (nullptr);
    inputEqHpButton.setLookAndFeel (nullptr);
    inputEqLpButton.setLookAndFeel (nullptr);
    outputEqHpButton.setLookAndFeel (nullptr);
    outputEqLpButton.setLookAndFeel (nullptr);
    transportStopButton.setLookAndFeel (nullptr);
    transportPlayButton.setLookAndFeel (nullptr);
    transportStartButton.setLookAndFeel (nullptr);
    spindownButton.setLookAndFeel (nullptr);
    delayTypeBox.setLookAndFeel (nullptr);
    delayRateBox.setLookAndFeel (nullptr);
    delaySyncButton.setLookAndFeel (nullptr);
    modeCycleButton.setLookAndFeel (nullptr);
    savePresetButton.setLookAndFeel (nullptr);
    deletePresetButton.setLookAndFeel (nullptr);

    if (savePresetWindow != nullptr)
        savePresetWindow->exitModalState (0);
    savePresetWindow.reset();
#if JUCE_DEBUG
    // Release the inspector before the components it watches are destroyed.
    inspector.reset();
#endif

}







bool FirstAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    // Ctrl/Cmd+Z and Ctrl/Cmd+Y mirror the DAW convention inside the plugin's own
    // preset/A/B history. The Shift+Z (redo) test comes FIRST: a bare Ctrl+Z also
    // matches the Shift variant, so testing undo first would swallow the redo chord.
    const auto commandDown = key.getModifiers().isCommandDown();
    if (commandDown && key.isKeyCode ('Y'))
    {
        audioProcessor.getUndoManager().redo();
        updateWorkflowButtons();
        return true;
    }
    if (commandDown && key.isKeyCode ('Z'))
    {
        if (key.getModifiers().isShiftDown())
            audioProcessor.getUndoManager().redo();
        else
            audioProcessor.getUndoManager().undo();
        updateWorkflowButtons();
        return true;
    }

    // 1 / 2 / 3 switch knob-grid tabs, the way any other plugin pages its controls.
    // This has to sit BEFORE the fall-through return: it was added after it, which left
    // the whole block unreachable and the digits doing nothing at all. Clang reported it
    // as -Wunreachable-code on the macOS build, which is how it was noticed. Tested
    // after the undo chords, and only without Shift, so a capitalised digit typed into
    // a slider's editable text box is still left alone.
    if (! commandDown && ! key.getModifiers().isShiftDown())
        for (int tab = 0; tab < numTabs; ++tab)
            if (key.getKeyCode() == '1' + tab)
            {
                setCurrentTab (tab);
                return true;
            }

    return Component::keyPressed (key);
}

FirstAudioProcessorEditor::EditorLayout FirstAudioProcessorEditor::getEditorLayout() const
{
    // Outer padding of 14 px instead of 18: the screws and the panel border only need
    // that much clearance, and every pixel saved here goes to the working areas.
    auto remaining = getLocalBounds().reduced (14);
    EditorLayout layout;

    // Header and deck are fixed-height, so they keep their proportions at small panel
    // sizes and on high-DPI displays. The header is 84 px - tall enough for the two
    // rows of engine switches and the machine-state readout on its own band below.
    //
    // The deck is 278 px and was 322. The forty-four came out of the machine readouts,
    // which were a three-high vertical stack pinned to the deck's right edge and are
    // now three caption-over-value pairs side by side inside the 32 px band they share
    // with OVER and INSTRUMENT. Nothing was moved off the deck to buy it - the
    // stack is the only thing that was taller than the band it was in - and the deck
    // keeps one band per group, so no two of them share a row.
    //
    // It is the same forty-four that comes off the editor's minimum height, which is
    // the difference between a window that fits a 768 px laptop and one that does
    // not. The floor is not cosmetic: it is what the host sizes the window to and
    // what the user cannot drag below.
    layout.header = remaining.removeFromTop (84);
    remaining.removeFromTop (8);

    // The deck's height is the one number in this function that was still fixed
    // when everything else had learned to flex, and it was the reason a short
    // window looked broken rather than merely tight.
    //
    // The control grid below divides whatever it is given (grid.getHeight() /
    // gridRows), so it copes with being squeezed. The deck could not: its six
    // bands sit at absolute offsets inside a rectangle of exactly 278 px, so on
    // a 1366x768 laptop - where the window has to be clamped to about 664 px to
    // stay on the screen at all - the deck kept all 278 of them and the grid got
    // what was left, roughly 258 px for three rows of knobs and a 2x2 meter
    // block. The panel did not fit because the part of it that had to give was
    // the part that could not.
    //
    // So the deck now flexes between a floor and its preferred height, and the
    // grid is guaranteed its minimum FIRST - the order matters, because taking
    // the grid's minimum off the top is what stops the two bands from both
    // ending up squeezed.
    //
    // 236 is the deck at its tightest, and it is a number rather than a guess:
    // the six bands hold 216 px of content and start 6 px down, so 222 px is
    // what they need before any gap at all. The 14 px that is left over is six
    // gaps of two - which is why the gap floor below is 2 and not 4. A floor of
    // 4 would need 246 px, and asking for 236 against it is a deck whose badge
    // gets drawn 10 px outside its own bottom edge.
    constexpr int deckPreferredHeight = 278;
    constexpr int deckMinimumHeight = 236;
    constexpr int controlsMinimumHeight = 300;
    constexpr int deckToControlsGap = 8;

    const auto availableForDeck = juce::jmax (0, remaining.getHeight() - deckToControlsGap);

    // jlimit keeps this ordered even when the window is shorter than the two
    // minimums together, which is exactly when a naive min/max would invert and
    // hand removeFromTop a negative height.
    const auto wantedDeckHeight = juce::jlimit (deckMinimumHeight, deckPreferredHeight,
                                                availableForDeck - controlsMinimumHeight);

    layout.deck = remaining.removeFromTop (juce::jmin (availableForDeck, wantedDeckHeight));
    remaining.removeFromTop (deckToControlsGap);

    // The meters panel has to hold a 2 x 2 grid of dials, so it claims a share of the
    // width rather than a fixed pixel count. That keeps both rows legible whether the
    // editor is at its minimum size or opened large.
    const auto metersWidth = juce::jlimit (320, 460, juce::roundToInt (static_cast<float> (remaining.getWidth()) * 0.30f));
    layout.meters = remaining.removeFromRight (metersWidth);
    remaining.removeFromRight (14);
    layout.controls = remaining;

    return layout;
}















//==============================================================================
//  resized() and the bands it places.
//
//  resized() was one 1,561-line body: the header's grid, the deck's seven bands, the
//  tab bar, the knob strip's solve, the member band, the visibility pass and the
//  meters, all sharing one scope. It is one call per band now, and each band is a
//  function handed what it reads - the rectangle it owns, or the geometry the strip
//  solved. The order of the calls is the order of the old body and is not arbitrary;
//  see the declaration block in PluginEditor.h.
//
//  Every band below is the old body's text, moved - not retyped. The bands that the
//  layout harness cuts fragments out of (the grid, the band, the knob count, the
//  solver, the member row and the placement) are the reason it is a move: those
//  fragments are compiled by tests/layout, so a retyped statement would quietly stop
//  being the panel's own arithmetic.
//==============================================================================
void FirstAudioProcessorEditor::resized()
{
    // The static texture layers (grain + wear) are sized to the window, so a
    // resize rebuilds them. Cheap enough at half resolution to pay on every
    // drag of the window's edge, and it keeps the texture from ever being
    // stretched over a size it was not built for.
    rebuildPanelTextureLayers();

    const auto layout = getEditorLayout();

    placeHeader (layout.header);
    placeDeck (layout.deck);
    placeControls (layout);
    placeTabBar (layout.controls);

    const auto geometry = placeControlStrip (layout);
    updateTabVisibility (geometry);
    placeTabMembers (geometry);
    placeMeters (layout);

    // The sheet is an overlay, not a band: it covers the panel while open, so its
    // bounds are the editor's own and it is deliberately not in the overlap
    // check's furniture list (it would collide with every item on that list by
    // design).
    if (parameterSheet != nullptr)
        parameterSheet->setBounds (getLocalBounds());

    // The native scene adapter sizes its swapchain from the same numbers its
    // setup used; without this the surface and the frame would disagree about
    // the viewport the moment the window is dragged.
    if (nativeRenderer != nullptr)
        nativeRenderer->resize (nativeSceneConfig());

    verifyPanelHasNoOverlaps();
}

//==============================================================================
//  The header: the brand strip, the title, the subtitle, and the one grid the five
//  engine switches, THEME, the language pair and the machine-state readout all sit
//  in - placed by a cursor that consumes each item's own width, which is what keeps
//  a row from overlapping itself however the panel is resized.
//==============================================================================
void FirstAudioProcessorEditor::placeHeader (juce::Rectangle<int> header)
{
    // Header captions: the small brand strip sits ABOVE the big title (they used to
    // share the same band and printed over each other), the subtitle starts right of
    // the title box, and all three stay clear of the theme button / status badge.
    brandLabel.setBounds (header.getX() + 26, header.getY() + 8, 120, 12);
    titleLabel.setBounds (header.getX() + 25, header.getY() + 24, 96, 46);
    subtitleLabel.setBounds (header.getX() + 126, header.getY() + 46, 220, 18);
    // The engine switches live in the header's right half, two per row - the
    // request was to move BYPASS / DELTA / POLARITY / AUTO GAIN up here, where
    // the theme button already sat. THEME and the status readout keep the
    // second row's right end; the switches take the row above them.
    //
    // Both rows are placed by a CURSOR that walks right-to-left and consumes
    // each item's own width, rather than by writing a coordinate per button.
    // It used to be five hard-coded x positions, and they did not add up: the
    // second row's AUTO GAIN ended at right-192 while THEME started at
    // right-202, so the two overlapped by ten pixels at every window size - the
    // kind of collision a table of literals cannot show you, because each
    // individual line is correct and only the SUM is wrong. With the cursor the
    // gap is the only number, and a row cannot overlap itself.
    //
    //  THE GRID. Five switches share the first row of equal-width cells and the
    //  second row puts THEME and the language pair under them, so their columns
    //  line up and no pair overlaps at any editor size.
    //
    //  OSC took the fifth cell when the note generator arrived. It fits because
    //  the cells are sized by the widest caption in the row (POLARITY, AUTO GAIN)
    //  and a third short caption costs the row a cell rather than a wider one.
    //
    //  Clearance at the 780 px minimum: the grid is 394 px wide (5 x 74 + 4 x 6)
    //  and starts at header.right - 394 = x + 358 on a 780 panel, clearing the
    //  title block (ends x + 346) by 12 px at the tightest size and more above it.
    // ------------------------------------------------------------------------------
    const auto headerRight = header.getRight();
    // The grid dimensions are declared on the class so these cells stay aligned.
    constexpr int headerGap = headerColumnGap;
    constexpr int headerColumns = headerColumnCount;
    constexpr int headerSwitchHeight = 22;
    // 74 rather than 92. The grid's cells are sized by the widest caption in
    // them (POLARITY, AUTO GAIN) plus room for the lamp, and 92 was sizing every
    // cell by the WIDEST one - so BYPASS and DELTA, which need half as much,
    // carried twenty pixels of empty metal each and the row read as four large
    // slabs rather than four switches. The height stays at 22 because the
    // rocker draws three stacked things inside it (caption, groove, thumb) and
    // they are already sharing that height three ways; the WIDTH was the one
    // dimension with slack in it.
    // (The width itself lives on the class now: see headerColumnWidth.)
    //  Six and thirty-two rather than ten and thirty-six. Both rows moved up
    //  together by four pixels so the METAL key stops touching the status pill:
    //  row two used to end at the header's +58 and the pill starts at +58 as
    //  well (the label sits at +60 and the pill is two pixels larger), which the
    //  user saw as the theme key sitting on the STEREO / REAL TIME badge. There
    //  are now four clear pixels between the key and the pill, and the same
    //  four between the two rows of switches.
    const auto headerRowOneY = header.getY() + 6;
    const auto headerRowTwoY = header.getY() + 32;
    // 60 rather than 62, and 16 rather than 18: the third row has to end at or
    // above the header's bottom edge once the pill around it is taken into
    // account (see the note at the label below).
    const auto statusRowY = header.getY() + 60;
    constexpr int headerStatusHeight = 16;
    const auto headerGridLeft = headerRight
                              - (headerColumns * headerColumnWidth
                                 + (headerColumns - 1) * headerGap);

    // No default argument on height: a lambda's default argument may not name
    // a local variable (the exact "local variable may not appear in this
    // context" GCC hands back), so every call passes its row height in full.
    const auto headerCell = [&] (juce::Component& component, int column, int rowY,
                                 int height)
    {
        component.setBounds (headerGridLeft + column * (headerColumnWidth + headerGap),
                             rowY, headerColumnWidth, height);
    };

    // Row one: the five engine switches, equal cells, one grid.
    headerCell (deltaButton,     0, headerRowOneY, headerSwitchHeight);
    headerCell (bypassButton,    1, headerRowOneY, headerSwitchHeight);
    headerCell (polarityButton,  2, headerRowOneY, headerSwitchHeight);
    headerCell (autoGainButton,  3, headerRowOneY, headerSwitchHeight);
    headerCell (oscButton,       4, headerRowOneY, headerSwitchHeight);

    // Row two: THEME, then the language pair - caption cell + a box that spans
    // whatever the grid has left of it. Written as a count of cells rather than
    // as a literal 154 px so a sixth column would widen the box instead of
    // leaving a hole at the end of the row.
    headerCell (themeButton,     0, headerRowTwoY, headerSwitchHeight);
    headerCell (languageLabel,   1, headerRowTwoY, headerSwitchHeight);
    languageLabel.setJustificationType (juce::Justification::centredRight);
    constexpr int languageColumns = headerColumns - 2;
    languageBox.setBounds (headerGridLeft + 2 * (headerColumnWidth + headerGap),
                           headerRowTwoY,
                           languageColumns * headerColumnWidth
                               + (languageColumns - 1) * headerGap,
                           headerSwitchHeight);

    // The renderer caption sits on the brand row's right end, ending one gap
    // before the switch grid: the row above the title is the only part of the
    // header with horizontal slack in it, and ending at headerGridLeft rather
    // than at a fixed x is what keeps it clear of the switches at every width.
    rendererLabel.setBounds (headerGridLeft - headerGap - rendererCaptionWidth,
                             header.getY() + 8, rendererCaptionWidth, 12);
    rendererLabel.setJustificationType (juce::Justification::centredRight);

    // The status readout gets its own full-width band below the switches - and it
    // ends INSIDE the header's own rectangle. The band used to be drawn on a pill
    // that was expanded six pixels past the label, which put its bottom edge two
    // pixels below the header panel at every size: the STEREO / REAL TIME badge
    // hung off the frame it belongs to. The pill is now the label's own rectangle
    // plus two pixels, and the hairline under the header is drawn before it (see
    // paint()), so nothing cuts through it either.
    statusLabel.setBounds (headerGridLeft, statusRowY,
                           headerColumns * headerColumnWidth
                               + (headerColumns - 1) * headerGap - 18,
                           headerStatusHeight);
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setFont (statusLabel.getFont().withHeight (8.5f));
    statusLabel.setBorderSize (juce::BorderSize<int> (0, 26, 0, 0));
}

//==============================================================================
//  The deck: seven bands solved from the height it was given (see DeckBands.h), the
//  rows inside them, and the window the 3D transport is framed in - which shares
//  reelCorridor with the rows, and is why it is here rather than in the controls
//  panel's own function.
//==============================================================================
void FirstAudioProcessorEditor::placeDeck (juce::Rectangle<int> deck)
{
    // ------------------------------------------------------------------
    //  The deck.
    //
    //  Seven bands, none of them shared with another:
    //   the DECK heading and the build id
    //   MODEL     tape type | speed | BPM
    //   SWITCHES  instrument | HARMONICS | SUBFUND | ANTI-PHASE
    //   the six type switches, their captions above their boxes, and the OSC
    //   ring at the right of that row
    //   TRANSPORT keys, the machine state, the mode switch
    //   the preset workflow chain
    //   the preset badge band          (the deck is 278 px at its preferred size)
    //
    //  BYPASS / DELTA / POLARITY / AUTO GAIN are not here: they moved to the
    //  header (see above), and the delay trio is a tab member of SPACE.
    //
    //  The three machine readouts were a vertical stack pinned to the deck's
    //  right edge - 130 px wide and 88 px tall. That is why the type row could
    //  not start until y + 168, why the deck had to be 322 px to hold it, and
    //  why the row it shared with OVER, INSTRUMENT and GL had two hundred
    //  pixels of nothing between them at the minimum width. They are three
    //  caption-over-value pairs side by side now, and each fits inside the
    //  32 px band the switches share, which is what brings the deck back to
    //  278 and takes forty-four pixels off the editor's minimum height.
    // ------------------------------------------------------------------
    // ------------------------------------------------------------------
    //  The deck's vertical rhythm.
    //
    //  Seven bands, solved from the height the deck was actually given (see
    //  Source/GUI/DeckBands.h). The table this replaced wrote a height per
    //  band and then placed a 15 px caption plus a 30 px control inside a 32 px
    //  one, so every row overhung its own band by thirteen pixels: at the
    //  preferred 278 px deck that was a one-pixel kiss into the caption band
    //  below, and at the 236 px floor - where the gaps compress to their
    //  minimum - it was eleven pixels of combo box painted over the next row's
    //  captions. That is exactly the user's "the lists are on top of the labels"
    //  (BPM, ANTI-PHASE, SUBFUND).
    //
    //  The solve owns all of it now: a row IS its caption plus its control, and
    //  when the deck is short the two shrink together (30 -> 24 px of control)
    //  before a gap gives way. Nothing below writes a height that is not one of
    //  these numbers, so no row can reach into the one under it again.
    // ------------------------------------------------------------------
    const auto bands = j37panel::DeckBands::solve (deck.getHeight());
    const auto captionHeight = bands.captionHeight;
    const auto controlHeight = bands.controlHeight;

    const auto deckTop = [&] (int band)
    {
        return deck.getY() + bands.top[static_cast<std::size_t> (band)];
    };
    const auto deckBandHeight = [&] (int band)
    {
        return bands.height[static_cast<std::size_t> (band)];
    };

    deckHeadingLabel.setBounds (deck.getX() + 18, deckTop (j37panel::DeckBands::heading), 150,
                                deckBandHeight (j37panel::DeckBands::heading));

    // The build id rides the deck's heading strip, right-aligned and stopping short of
    // the reel (whose left edge is deck.right - 56). This strip is the only full-width
    // band near the top of the panel that is guaranteed empty at the 780 px minimum:
    // the header's equivalent gap shrinks to about 58 px there.
    buildLabel.setBounds (deck.getX() + 190, deckTop (j37panel::DeckBands::heading),
                          deck.getWidth() - 280, deckBandHeight (j37panel::DeckBands::heading));

    // ------------------------------------------------------------------
    //  Spreading a row across the deck.
    //
    //  The deck was drawn for the 752 px minimum and the editor opens at 1060,
    //  so every row that pinned a group to the left and another to the right
    //  left a hole in the middle that GREW with the window: 32 to 96 px of it
    //  at the minimum, and 312 to 486 px at the size the window actually opens
    //  at. Nothing was wrong with any single number - the widths were right and
    //  the anchors were right - and the sum of the two was a deck with its
    //  middle missing. A layout written for one width does not look wrong at
    //  that width; it looks wrong at every other one, which is the whole of
    //  what "the deck is crooked" meant.
    //
    //  So the slack is shared. Half of it opens up the gaps and half goes into
    //  the items themselves, which is what stops a row from looking like small
    //  controls drifting apart: a row reads as one thing that grew, rather than
    //  as three things with holes between them.
    //
    //  Three properties keep this safe rather than merely different:
    //
    //    - every item's base width is still written down, next to its own name,
    //      so a row can never claim more space than its content needs, and the
    //      same number describes the item at every panel size;
    //    - the gap is FLOORED at the constant the row used before, so at the
    //      minimum width this is a small perturbation of the layout that
    //      shipped, and the guarantee is one-sided - it can only ever add space,
    //      never take any away, and so can never bring two controls closer
    //      together than they have ever been;
    //    - a caption and the control it names are GLUED: their gap is written
    //      in the table and is never elastic, and a caption stacked on its own
    //      readout rides the readout's column rather than adding a second one.
    //      That is the exact failure the previous pass fixed on this panel -
    //      INSTRUMENT's caption printed straight through its own combo box -
    //      and a rule that redistributed slack between the two would be a way
    //      of walking back into it.
    //
    //  The row still returns the widest gap it left. Nothing reads it now: the
    //  drifting dust moved to the deck's foot (see the badge band below), which
    //  is the only wide free strip this deck has. The return is kept because it
    //  is the row's own report of its slack, and the next thing that needs to
    //  know where the air is should not have to recompute it.
    // ------------------------------------------------------------------
    struct DeckRowItem
    {
        juce::Component* component;
        int width;         // the column this item opens; ignored when sameColumn
        int gapAfter;      // -1 elastic, or a fixed gap; ignored when sameColumn
        int y;             // relative to the row's own y
        int height;
        bool sameColumn;   // rides the column the previous item opened (a
                          // caption above its own readout, or its own box)
    };

    const auto placeDeckRow = [] (const std::vector<DeckRowItem>& items,
                                  int left, int right, int rowY, int minimumGap)
    {
        int content = 0;
        int elasticGaps = 0;

        for (std::size_t i = 0; i < items.size(); ++i)
        {
            if (! items[i].sameColumn)
                content += items[i].width;

            // A gap belongs to the item that ENDS a column: the caption of a
            // horizontal pair, and the value of a stacked one.
            if (i + 1 < items.size() && ! items[i + 1].sameColumn)
            {
                if (items[i].gapAfter < 0)
                {
                    content += minimumGap;
                    ++elasticGaps;
                }
                else
                {
                    content += items[i].gapAfter;
                }
            }
        }

        // Never negative: a row whose own widths are wider than the panel packs
        // left at the minimum gap rather than overlapping itself.
        const auto slack = juce::jmax (0, right - left - content);

        // The gap grows, but not without limit. Past a point, a wider gap stops
        // reading as "this row was justified" and starts reading as "these two
        // controls are unrelated", and on the model row that point arrives
        // before the maximum window does: at 1500 px the unbounded share put
        // two hundred and thirty pixels between TAPE TYPE and SPEED. So the
        // gap's share is capped and everything above the cap goes into the
        // items instead - a combo box that is a little wider looks like a combo
        // box, and a gap that is a little wider looks like a mistake.
        //
        // 48 px is chosen against the numbers rather than by taste. It sits
        // just above what the model row asks for at the 780 px minimum (a 37 px
        // share on top of its 16 px base, i.e. 53 px), so the tightest window
        // this plugin can open at is laid out exactly as it was before the cap
        // existed - a minimum that changed shape would be a bug, not a layout.
        // Above the minimum it takes effect: the model row's gap is 64 px at
        // 900, at 1060 and at 1500 instead of 83, 123 and 233.
        constexpr int maximumGapGrowth = 48;
        const auto wantedGapShare = elasticGaps > 0
                                      ? static_cast<int> (slack * 0.5) / elasticGaps : 0;
        const auto gapShare = juce::jmin (wantedGapShare, maximumGapGrowth);
        const auto usedByGaps = gapShare * elasticGaps;

        // The width share divides by COLUMNS, not by items: a stacked caption
        // rides its column and must not eat a share of its own. Dividing by the
        // item count gave five-share voids on every row that stacks caption over
        // value - slack the row collected and never spent, which the user saw as
        // controls spaced 'at random' with a hole at the row's end.
        int columnCount = 0;
        for (const auto& item : items)
            if (! item.sameColumn)
                ++columnCount;

        const auto widthShare = columnCount == 0
                                  ? 0
                                  : (slack - usedByGaps) / columnCount;

        auto widest = juce::Range<int> (0, 0);
        int edge = left;
        int columnLeft = left;
        int columnWidth = 0;

        for (std::size_t i = 0; i < items.size(); ++i)
        {
            const auto& item = items[i];

            // A stacked item does not open a column: it rides the one the item
            // before it opened, at that item's width, so a caption and the
            // readout under it are always the same width by construction and
            // there is no second number that could disagree with the first.
            if (! item.sameColumn)
            {
                columnLeft = edge;
                columnWidth = item.width + widthShare;
            }

            item.component->setBounds (columnLeft, rowY + item.y, columnWidth, item.height);

            // The gap belongs to the item that ENDS the column, so a stacked
            // pair still leaves the row with the gap it was promised.
            if (i + 1 < items.size() && ! items[i + 1].sameColumn)
            {
                const auto elastic = item.gapAfter < 0;
                const auto gap = elastic ? minimumGap + gapShare : item.gapAfter;

                if (elastic && gap > widest.getLength())
                    widest = { columnLeft + columnWidth, columnLeft + columnWidth + gap };

                edge = columnLeft + columnWidth + gap;
            }
        }

        return widest;
    };

    constexpr int deckRightInset = 14;

    // The reel and the tape ribbon own the deck's top-right corner, and the ribbon
    // hangs down through the model row's band: it starts at the reel (deck.right -
    // 42, y + 26) and falls to y + 60, which is the row the tape box and the BPM
    // readout sit in. So the model row stops 70 px short - twenty-eight to the
    // right of the ribbon's leftmost point - and every other row uses the standard
    // inset. This is not a margin that has to be re-tuned per row; it is the
    // ribbon's own width, and the rows below it do not need it because the ribbon
    // ends above them.
    constexpr int reelCorridor = 70;

    // ---- row 1: TRANSPORT SPEED ---------------------------------------------
    //  Everything that reads as a RATE lives in one row now, which is what the
    //  user asked for: the tape speed (ips) and the turntable's motor speed
    //  (RPM) sit side by side, and the host-tempo readout rides with them -
    //  three rate readouts a user scans as one group instead of MODEL | SPEED
    //  | BPM, where the model sat between two kinds of speed.
    //  The empty columns to the right are deliberate: the type row below has
    //  six columns and the rows should breathe together, not stretch to fill.
    {
        //  Every height in the table is the band's own: the caption band, the
        //  control band, and the two pixels a readout gives back under it. A
        //  literal here is what put the boxes thirteen pixels into the row below.
        const std::vector<DeckRowItem> row {
            { &speedLabel,       108, -1,  0, captionHeight, false },
            { &speedBox,           0, -1, captionHeight, controlHeight, true  },
            { &vinylSpeedLabel,  108, -1,  0, captionHeight, false },
            { &vinylSpeedBox,      0, -1, captionHeight, controlHeight, true  },
            { &bpmLabel,         108, -1,  0, captionHeight, false },
            { &bpmReadout,         0, -1, captionHeight, captionHeight - 2, true  }
        };

        placeDeckRow (row, deck.getX() + 18, deck.getRight() - reelCorridor,
                      deckTop (j37panel::DeckBands::model), 14);
    }

    // ---- row 2: SWITCHES ----------------------------------------------------
    // OVERSAMPLING is a SETTINGS-tab member and lives in the member row under
    // that tab's knobs (placeDeckSwitch below).
    //
    //  Four equal 108 px columns, the type row's grid again: INSTRUMENT, then
    //  the three readout pairs - caption above value, one column each, all
    //  standing exactly above the type row's columns.
    {
        const std::vector<DeckRowItem> row {
            { &instrumentLabel,  108, -1,  0, captionHeight, false },
            { &instrumentBox,      0, -1, captionHeight, controlHeight, true  },

            { &harmonicsLabel,   108, -1,  0, captionHeight, false },
            { &harmonicsReadout,   0, -1, captionHeight, captionHeight - 2, true  },
            { &subfundLabel,     108, -1,  0, captionHeight, false },
            { &subfundReadout,     0, -1, captionHeight, captionHeight - 2, true  },
            { &antiPhaseLabel,   108, -1,  0, captionHeight, false },
            { &antiPhaseReadout,   0, -1, captionHeight, captionHeight - 2, true  }
        };

        placeDeckRow (row, deck.getX() + 18,
                      deck.getRight() - deckRightInset,
                      deckTop (j37panel::DeckBands::switches), 12);

        // The readouts are the only deck text whose width now depends on the
        // panel, so they are the only ones that can run out of room - and they
        // are also the only ones whose TEXT changes while the editor is open
        // ("idle - no note tracked", "412.5 Hz 87 %"). setMinimumHorizontalScale
        // is the one fit that keeps working: it is a property of the label, so
        // every later setText is measured against the width the row gave it,
        // rather than being clipped at whatever size it happened to be at when
        // the last resize happened to run.
        for (auto* label : { &harmonicsLabel, &harmonicsReadout,
                             &subfundLabel, &subfundReadout,
                             &antiPhaseLabel, &antiPhaseReadout })
            label->setMinimumHorizontalScale (0.55f);

        // The particles are NOT placed here any more. This row's leftover gap
        // was where they drifted, and at every real width it was a single 50-70
        // px slot between two readouts: six orbs bouncing inside it read as a
        // handful of dots stuck in one corner of the deck rather than as dust
        // moving through the machine. They get the deck's foot instead - see the
        // badge band below - where the strip is a thousand pixels wide and only
        // the badge's text sits in it.
    }

    // ---- row 3: the six type selectors, and the OSC ring --------------------
    {
        // The model selector (renamed TAPE, its old caption read MODEL) joined
        // the row of type selectors - the user read MODEL | SPEED | BPM as
        // three unrelated things on one line, when the selector is a TYPE like
        // VINYL or VALVE and belongs with them.
        //
        //  THE OSC RING takes the row's right end, where the user asked for it:
        //  to the right of the TYPE boxes. It is a square as tall as the whole
        //  band (the caption strip is part of the band, so the ring is not
        //  squeezed into the control strip alone), which makes it the same size
        //  as a knob and leaves the row one number to solve instead of two.
        //
        //  THE COLUMNS ARE SOLVED, NOT WRITTEN DOWN. 108 was the width the six
        //  selectors wanted and the largest the row held at the old 780 px
        //  minimum; with the ring standing in the row that number is no longer
        //  true, and a literal here would have run the boxes straight under the
        //  ring on the narrowest window. So 108 is a CEILING and what is left
        //  after the ring and the minimum gaps is the column - the longest
        //  stock values ellipsise a little sooner on a minimum-width panel and
        //  every window above it is the row it always was.
        const auto typesRight = deck.getRight() - deckRightInset;
        const auto typesTop = deckTop (j37panel::DeckBands::types);
        const auto ringSize = deckBandHeight (j37panel::DeckBands::types);

        constexpr int ringGap = 10;
        oscRing.setBounds (typesRight - ringSize, typesTop, ringSize, ringSize);

        const auto rowLeft = deck.getX() + 18;
        const auto rowRight = typesRight - ringSize - ringGap;
        const auto typeColumnWidth = juce::jmax (60, juce::jmin (108, (rowRight - rowLeft - 5 * 14) / 6));

        const std::vector<DeckRowItem> row {
            { &tapeTypeLabel,        typeColumnWidth, -1,  0, captionHeight, false },
            { &tapeTypeBox,            0, -1, captionHeight, controlHeight, true  },
            { &vinylTypeLabel,        typeColumnWidth, -1,  0, captionHeight, false },
            { &vinylTypeBox,           0, -1, captionHeight, controlHeight, true  },
            { &valveTypeLabel,        typeColumnWidth, -1,  0, captionHeight, false },
            { &valveTypeBox,           0, -1, captionHeight, controlHeight, true  },
            { &ampTypeLabel,          typeColumnWidth, -1,  0, captionHeight, false },
            { &ampTypeBox,             0, -1, captionHeight, controlHeight, true  },
            { &transformerTypeLabel, typeColumnWidth, -1,  0, captionHeight, false },
            { &transformerTypeBox,     0, -1, captionHeight, controlHeight, true  },
            { &digitalTypeLabel,      typeColumnWidth, -1,  0, captionHeight, false },
            { &digitalTypeBox,         0, -1, captionHeight, controlHeight, true  }
        };

        placeDeckRow (row, rowLeft, rowRight, typesTop, 14);
    }

    // ---- row 4: TRANSPORT ---------------------------------------------------
    {
        const std::vector<DeckRowItem> row {
            { &transportStopButton,   62,  4,  0, controlHeight, false },
            { &transportPlayButton,   62,  4,  0, controlHeight, false },
            { &transportStartButton,  62,  4,  0, controlHeight, false },
            { &spindownButton,        74, -1,  0, controlHeight, false },
            { &deckHintLabel,        190, -1,  2, controlHeight - 4, false },
            { &modeCycleButton,      150, -1,  0, controlHeight, false }
        };

        // The keys start where every other row's control starts: under the
        // caption band the solve just gave this row.
        const auto transportRowY = deckTop (j37panel::DeckBands::transport) + captionHeight;

        placeDeckRow (row, deck.getX() + 18, deck.getRight() - deckRightInset,
                      transportRowY, 14);

        // The two transport captions sit ABOVE the key bank rather than beside
        // their own key, so they cannot be a column of the row - they are placed
        // from the buttons the row just gave them. Reading the placed bounds
        // back is what keeps them correct at every width: written as
        // `keyWidth * 3 + gap * 2` they would name a span three keys wider than
        // the bank actually is once the row starts growing them.
        transportLabel.setBounds (transportStopButton.getX(), transportRowY - captionHeight,
                                  transportStartButton.getRight() - transportStopButton.getX(),
                                  captionHeight - 2);
        spindownLabel.setBounds (spindownButton.getX(), transportRowY - captionHeight,
                                 spindownButton.getWidth(), captionHeight - 2);

        transportLabel.setFont (shrinkingFont (transportLabel.getText(), 7.0f,
                                              juce::Font::plain,
                                              static_cast<float> (transportLabel.getWidth()) - 3.0f));
        spindownLabel.setFont (shrinkingFont (spindownLabel.getText(), 7.0f,
                                              juce::Font::plain,
                                              static_cast<float> (spindownLabel.getWidth()) - 3.0f));
    }

    // The preset workflow chain is eleven items and it did not fit: written as
    // a left-to-right chain of `previous.getRight() + gap`, the last item - the
    // compare badge - ended sixteen pixels past the deck's right edge at the
    // 780 px minimum, so it was drawn over the panel border and the frame.
    //
    // It is walked from BOTH ends: the head of the chain from the deck's left
    // inset, the tail from its right inset, each with the same cursor
    // discipline as the header's switch rows. Every item's width is written
    // once, next to its own name, and the two chains meet in the middle - so
    // adding a button to either end costs one line and cannot push the other
    // end off the panel, which is the failure a single chain always has. It is
    // not spread like the rows above because it already spans the full width at
    // the minimum: there is no slack to share, and a gap that grew with the
    // window would only pull its two halves further apart.
    const auto presetRowY = deckTop (j37panel::DeckBands::preset);
    const auto presetRowHeight = deckBandHeight (j37panel::DeckBands::preset);
    constexpr int presetRowGap = 5;

    // presetRowHeight IS captured now. It used to be a literal 32 with a constant
    // initialiser, which a lambda can read without capturing; it is the solved
    // band height, so the lambdas have to hold it like any other local.
    const auto placeForward = [presetRowY, presetRowHeight]
                                  (int& edge, juce::Component& component, int width)
    {
        component.setBounds (edge, presetRowY, width, presetRowHeight);
        edge += width + presetRowGap;
    };

    const auto placeBackward = [presetRowY, presetRowHeight]
                                   (int& edge, juce::Component& component, int width)
    {
        component.setBounds (edge - width, presetRowY, width, presetRowHeight);
        edge -= width + presetRowGap;
    };

    presetHeadingLabel.setBounds (deck.getX() + 18, presetRowY + 9, 46, 16);

    {
        // Head, from the left: the factory list, the user list, save, delete.
        // 120 and 94 rather than 128 and 100: at the 780 px minimum the two
        // chains meet with two pixels between DELETE and COPY A, which is a
        // clearance, not a margin. Fourteen pixels taken off the two widest
        // items - both of them lists that shorten their own font before they
        // clip - buys sixteen, and the chain then has room for a button added
        // at either end without the two ends touching.
        //
        //  THE WIDE-WINDOW HOLE. Both chains were laid out against the 780 px
        //  minimum, and on anything wider the unspent width sat between them
        //  as one hole that grew with the panel - the deck's last "random
        //  gap". The hole belongs to the two LISTS: they are the chain's
        //  widest members and the only two items a wider bounds actually
        //  improves (a longer preset name shown instead of a longer emptiness;
        //  a button or a badge would only read as stretched). The lists absorb
        //  it in shares - 60/40, each capped - and at the minimum the shares
        //  are zero by construction, so the tight layout is untouched.
        constexpr int presetBoxWidth  = 120;
        constexpr int userPresetWidth =  94;
        constexpr int headWidths = presetBoxWidth + userPresetWidth + 40 + 38;
        constexpr int tailWidths = 54 + 44 + 44 + 54 + 64 + 64;
        const auto chainSpan = (deck.getRight() - 14)
                                   - (deck.getX() + 66);
        const auto chainVoid = juce::jmax (0, chainSpan - (headWidths + tailWidths
                                                              + 9 * presetRowGap));
        const auto presetGrowth = juce::jmin (chainVoid * 3 / 5, 160);
        const auto userGrowth   = juce::jmin (chainVoid - presetGrowth, 120);

        int headEdge = deck.getX() + 66;
        placeForward (headEdge, presetBox,          presetBoxWidth  + presetGrowth);
        placeForward (headEdge, userPresetBox,      userPresetWidth + userGrowth);
        placeForward (headEdge, savePresetButton,  40);
        placeForward (headEdge, deletePresetButton, 38);
    }

    {
        // Tail, from the right: the badge, REDO, UNDO, COMPARE, A, B.
        int tailEdge = deck.getRight() - 14;
        placeBackward (tailEdge, compareBadgeLabel, 54);
        placeBackward (tailEdge, redoButton,        44);
        placeBackward (tailEdge, undoButton,        44);
        placeBackward (tailEdge, compareButton,     54);
        placeBackward (tailEdge, copyBButton,       64);
        placeBackward (tailEdge, copyAButton,       64);
    }

    // The badge band is inset further than the other deck text (22 px instead of 18)
    // and its caption is fitted to the width it actually has, so neither "FACTORY
    // STATE" nor a long user-preset name can run into the panel edges or be clipped.
    //
    // The machine state moved here from the header: it is a readout of what the
    // engine is doing, and this is the band the deck's other readouts (the
    // preset badge, the compare badge) already live on. It takes a fixed cell
    // off the band's right end and the preset badge gives up exactly that
    // width, so a long preset name shrinks into what is left rather than
    // printing under the state text - the shrinking font below measures the
    // label's ACTUAL bounds, so narrowing it is the whole fix.
    // The state readout is back in the header on its drawn pill (see the header
    // grid above), so the badge line is the preset badge's alone again - full
    // band width, the pill painted UNDER this line in paint() needs no cell.
    presetBadgeLabel.setBounds (deck.getX() + 22, deckTop (j37panel::DeckBands::badge),
                                deck.getWidth() - 44,
                                deckBandHeight (j37panel::DeckBands::badge));
    presetBadgeLabel.setFont (shrinkingFont (presetBadgeLabel.getText(), 8.0f,
                                             juce::Font::plain,
                                             static_cast<float> (presetBadgeLabel.getWidth()) - 4.0f));
    // The 3D transport's window: the deck's top-right block, which is the one
    // region resized() keeps free of controls. reelCorridor is 70 px wide and the
    // heading band starts six px down, so the scene gets a 64 x 58 block that is
    // the same size at every panel size - which is what lets the scene be laid out
    // in its own units and framed by the camera rather than by a pixel count.
    // It stops 8 px short of the switches row below it, so the two never touch.
    // Its height is capped by the switches band above it, so the eight pixels of
    // clearance the comment above promises hold at the deck's 236 px floor too -
    // where the solved bands start higher and a fixed 58 px block would have met
    // the row instead of stopping short of it.
    const auto sceneTop = deck.getY() + 6;
    const auto sceneBottom = deckTop (j37panel::DeckBands::switches) - 8;
    tapeScene.setBounds (deck.getRight() - reelCorridor,
                         sceneTop,
                         reelCorridor - 6,
                         juce::jmax (24, juce::jmin (58, sceneBottom - sceneTop)));

    // ------------------------------------------------------------------
    //  Where the drifting dust lives.
    //
    //  It used to be the switches row's leftover gap: a single slot fifty-odd
    //  pixels wide between two readouts, which is where six orbs bouncing off
    //  each other read as "a few dots stuck in a corner". The deck has no wide
    //  free air to give them - every band is full - except its foot: the badge
    //  line is a thousand pixels of near-empty metal with one centred caption
    //  on it, and a 14 px strip is cheap to repaint every tick. So the motes
    //  drift along the machine's bottom edge, behind the badge text, which is
    //  the one place on the panel where motion can be seen without being drawn
    //  over a control.
    // ------------------------------------------------------------------
    deckParticleCorridorX = { deck.getX() + 24, deck.getRight() - 24 };
    deckParticleCorridorY = { deckTop (j37panel::DeckBands::badge) + 1,
                              deckTop (j37panel::DeckBands::badge)
                                  + deckBandHeight (j37panel::DeckBands::badge) - 1 };
}

//==============================================================================
//  The controls panel's heading, its hint and the active page's own section caption -
//  the three captions above the divider, which the knob strip is laid out under.
//==============================================================================
void FirstAudioProcessorEditor::placeControls (const EditorLayout& layout)
{
    controlsHeadingLabel.setBounds (layout.controls.getX() + 18, layout.controls.getY() + 10, 210, 19);
    // The section caption takes the gap between the heading and the hint, which is
    // where the panel has always had a wide empty stretch: the hint is drawn
    // RIGHT-aligned, so its own left half is free space this caption can live in
    // without moving anything. It stops short of the hint's own left edge, so the
    // two cannot collide however long the caption or the hint grows.
    const auto controlsHintRight = layout.controls.getRight() - 12;
    const auto controlsHintLeft = juce::jmax (layout.controls.getX() + 240,
                                              controlsHeadingLabel.getRight() + 24);
    controlsHintLabel.setBounds (controlsHintLeft, layout.controls.getY() + 10,
                                 juce::jmax (0, controlsHintRight - controlsHintLeft), 19);
    sectionCaptionLabel.setBounds (controlsHeadingLabel.getRight() + 14,
                                   layout.controls.getY() + 11,
                                   juce::jmax (0, controlsHintLeft - controlsHeadingLabel.getRight() - 24),
                                   17);
}

//==============================================================================
//  The tab bar: the two rows of tabs-inside-tabs, re-flowed to the live family's page
//  count.
//==============================================================================
void FirstAudioProcessorEditor::placeTabBar (const juce::Rectangle<int>& controlsPanel)
{
    // ------------------------------------------------------------------
    //  The tab bar.
    //
    //  The ten pages are grouped into three FAMILIES, so the bar never has to fit
    //  ten names into one row: TAPE carries the machine and its colouring, FX the
    //  time and dynamics stages, SETUP the two equalisers and the engine switches.
    //  A family only filters WHICH tab buttons are visible - no tab is re-indexed,
    //  the spec table, setCurrentTab and every knob's membership stay as they were.
    //
    //  The caption block that used to float in the row gaps was the panel's
    //  worst-placed furniture: the gap above row 3 lies INSIDE row 2 and the gap
    //  above row 4 lies inside row 3, so SATURATION CORE was painted on top of the
    //  row of knobs above it and HEAD / TRANSPORT on top of the next one. The
    //  captions are gone and the groups are real tabs, each named on a button tall
    //  enough to read, the knobs underneath get a whole grid to themselves, and
    //  only the selected tab's controls are on screen at all.
    // ------------------------------------------------------------------
    const auto tabBarLeft = controlsPanel.getX() + 14;
    // The two rows of tabs-inside-tabs live between the heading and the divider:
    // FAMILIES on top (the three large buttons), the active family's PAGES under
    // them. The divider (controlsDividerOffset) sits below both, so the knob grid
    // itself never pays a row for either strip.
    //  The family row and the page row are the SAME height, and the same 22 px
    //  the header's engine switches use. They were 22 and 24: two rows of keys
    //  one above the other, drawn half a millimetre differently, which is the
    //  kind of difference that reads as a mistake long before anyone can name
    //  it - and exactly what "the toggles on one tab are stretched differently"
    //  was pointing at.
    constexpr int familyRowHeight = 22;
    const auto familyRow = juce::Rectangle<int> (controlsPanel.getX() + 14,
                                                 controlsPanel.getY() + 34,
                                                 controlsPanel.getWidth() - 28,
                                                 familyRowHeight);
    const auto familyButtonWidth = (familyRow.getWidth() - 2 * 6) / tabFamilyCount;
    tabFamilyTapeButton.setBounds (familyRow.getX(), familyRow.getY(),
                                   familyButtonWidth, familyRow.getHeight());
    tabFamilyFxButton.setBounds (familyRow.getX() + familyButtonWidth + 6,
                                 familyRow.getY(), familyButtonWidth,
                                 familyRow.getHeight());
    tabFamilySetupButton.setBounds (familyRow.getX() + 2 * (familyButtonWidth + 6),
                                    familyRow.getY(), familyButtonWidth,
                                    familyRow.getHeight());

    const auto tabBarTop = controlsPanel.getY() + 60;
    constexpr int tabBarHeight = 22;

    // Pages of the CURRENT family only: the bar re-flows to the family's page
    // count, so TAPE's five buttons get a fifth more width than the old ten-in-a-
    // row bar ever could, and SETUP's three get three broad ones.
    int visibleTabCount = 0;
    for (int tab = 0; tab < numTabs; ++tab)
        if (tabSpecs[static_cast<std::size_t> (tab)].family == currentTabFamily)
            ++visibleTabCount;
    visibleTabCount = juce::jmax (1, visibleTabCount);
    const auto tabGap = visibleTabCount <= 5 ? 6 : 4;
    const auto tabButtonWidth = (controlsPanel.getWidth() - 28
                                 - tabGap * (visibleTabCount - 1)) / visibleTabCount;

    int placedTabIndex = 0;
    for (int tab = 0; tab < numTabs; ++tab)
    {
        if (tabSpecs[static_cast<std::size_t> (tab)].family != currentTabFamily)
        {
            tabButtons[static_cast<std::size_t> (tab)].setBounds (tabBarLeft,
                                                                  tabBarTop, 0, 0);
            continue;
        }
        tabButtons[static_cast<std::size_t> (tab)]
            .setBounds (tabBarLeft + placedTabIndex * (tabButtonWidth + tabGap),
                        tabBarTop, tabButtonWidth, tabBarHeight);
        ++placedTabIndex;
    }

    // The family buttons are never hidden by a page change (only a repaint)
    // and there is nothing on the panel that flips their visibility, so this is
    // a plain always-on strip.
    tabFamilyTapeButton.setVisible (true);
    tabFamilyFxButton.setVisible (true);
    tabFamilySetupButton.setVisible (true);
}

//==============================================================================
//  The knob strip: the solve, and the one band that hands anything on.
//
//  Its grid, the live page's flags, the solved tile and the member band's top edge are
//  what the member band, the visibility pass and the mix bar measure in, so they leave
//  here as a ControlGeometry rather than as locals the neighbouring bands could read.
//==============================================================================
FirstAudioProcessorEditor::ControlGeometry
FirstAudioProcessorEditor::placeControlStrip (const EditorLayout& layout)
{
    // The grid starts below the divider paint() drew, with a little air under it. The
    // 14 px undoes the reduced() inset, so the offset is measured from the panel edge
    // that controlsDividerOffset is measured from, not from the inset grid.
    auto grid = layout.controls.reduced (14);
    grid.removeFromTop (controlsDividerOffset - 14 + 8);
    grid.removeFromBottom (8);

    // Only the ACTIVE tab is laid out, and its own control count decides the
    // strip's column count, so a tab is never sized as if it still had to hold the
    // whole panel's knobs: a page of two gets two large knobs, and the fullest
    // page (seven) gets one row of seven across the whole grid.
    const auto& activeTab = tabSpecs[static_cast<std::size_t> (currentTab)];
    const auto tabControlCount = static_cast<int> (activeTab.count);
    // The MEMBER BAND's cell width - what placeDeckSwitch and every page that
    // spans a cell of its own measures in. The knob strip solved below does not
    // use it: its own slot width is solved from the page's knob count.
    const auto cellWidth = grid.getWidth() / tabColumns;

    // Which tab is live, looked up by name from the tabSpecs table - the single
    // source of truth - rather than carried as numbers that drift when a page is
    // added or reordered. Hoisted ABOVE the row arithmetic because the member row
    // is decided by the same flags the layout chain below reads.
    const auto settingsTab = index_of_tab_named ("SETTINGS") == currentTab;
    const auto autotuneTab = index_of_tab_named ("AUTOTUNE") == currentTab;
    const auto delayTab = index_of_tab_named ("DELAY") == currentTab;
    const auto characterTab = index_of_tab_named ("CHARACTER") == currentTab;
    const auto frontEndTab = index_of_tab_named ("FRONT END") == currentTab;
    const auto vinylTab = index_of_tab_named ("VINYL") == currentTab;
    const auto shapersTab = index_of_tab_named ("MODELS") == currentTab;
    const auto inEqTab = index_of_tab_named ("IN EQ") == currentTab;
    const auto outEqTab = index_of_tab_named ("OUT EQ") == currentTab;
    const auto machineTab = index_of_tab_named ("MACHINE") == currentTab;
    const auto mixTab = index_of_tab_named ("MIX") == currentTab;
    const auto compressorTab = index_of_tab_named ("COMP") == currentTab;

    // How many knobs the LIVE page has to place, and which list they come from.
    // Two pages own knobs the tabSpecs table cannot describe: COMP's six are the
    // compressor's own, built beside the meters they belong to, and AUTOTUNE's one
    // is the tune amount, built with the page's pill switch. Neither is declared
    // as a knob page, so the strip is handed their count here and reads their
    // widgets in its placement loop below. Everything after this point is written
    // against knobCount rather than against the tab's own count, and both pages are
    // laid out by the same solve as every other page: COMP used to place its own
    // six by hand, in cells as tall as the whole grid, which drew a small cap at
    // the top of each cell with its readout a hundred-odd pixels under it, and
    // AUTOTUNE rode a 30 px band beside its switch, which the toolkit turned into a
    // three-pixel paint area - a bead on a stock bar, one knob on the panel drawn
    // by JUCE's own slider art.
    const auto knobCount = compressorTab ? static_cast<int> (compressorControls.size())
                                         : autotuneTab ? 1
                                                       : tabControlCount;

    // Which pages own a MEMBER BAND - the strip under the knobs that the
    // non-knob combos and pills (placeDeckSwitch) are laid out in. It is no
    // longer the same question as "how many rows of knobs does the page need":
    // the knob strip sizes itself (see below) and the band is a fixed strip of
    // its own. SETTINGS is the worst case and the reason the arithmetic is
    // shared: with no knobs at all, sizing the grid for knob rows alone left its
    // whole height for a member row that was then appended BELOW the grid, off
    // the panel - OVERSAMPLING was clipped by the window's bottom edge on
    // the very tab that holds nothing else.
    //
    // The MIX page counts as a member page even though it owns no switch: the
    // band it reserves is where the saturation mix bar lives, which is the whole
    // reason the page is worth its own strip. FRONT END counts because its DI PAD
    // is a combo - a member - and can no longer ride the knob grid: a page whose
    // knobs fill their row has no free cell to put it in (it used to take the
    // fourth cell of a second row that had three knobs in it). VINYL counts for
    // the same reason the two picker pages do: the surface the record plays is a
    // FILE the user loads, and the band under VINYL MIX is where that loader now
    // lives - the page carried nothing but three knobs before, so a print had no
    // way to be reached from the page whose stage plays it. DYN does NOT: its
    // knobs moved to CHARACTER and nothing ever rode the band it reserved, so the
    // page now gives that 50 px to its own three knobs instead.
    const auto memberRows = (settingsTab || delayTab || characterTab
                             || frontEndTab || vinylTab || shapersTab || inEqTab || outEqTab
                             || machineTab || mixTab || autotuneTab) ? 1 : 0;

    // The member band is a FIXED strip - a caption band plus a control band -
    // pinned to the grid's BOTTOM edge, and the knob strip takes what is above
    // it. 50 px is the label band (17) plus the control's 30 at its 20 px offset
    // - the geometry placeDeckSwitch below already assumes. Pinned rather than
    // stacked under the knob rows because the knob strip is COMPACT (see the next
    // block): its tiles take the height they need and no more, so "under the
    // knobs" would leave the band floating at whatever height the page's knob
    // count put it, with the panel's surplus beneath it. On the bottom edge it
    // sits where a machine's own row of mode switches sits, on every page.
    constexpr int memberBandHeight = 50;
    // A page with no knobs keeps the whole grid as its band: SETTINGS places two
    // lines of preferences in it, and they belong at the top, under the divider,
    // where a page of knobs puts its own captions.
    const auto knobAreaHeight = (memberRows != 0 && knobCount > 0)
                                    ? grid.getHeight() - memberBandHeight
                                    : grid.getHeight();

    // ------------------------------------------------------------------
    //  The knob strip.
    //
    //  A knob's disc is capped by the SHORTER side of the paint area it is given
    //  (see computeKnobMetrics), so the number of rows a page's knobs are laid
    //  out in is what decides how big they are. Four columns of two rows gave
    //  every knob half the page's height to draw in and left the second row with
    //  three of its four cells empty - a small disc in a wide, half-empty cell.
    //  The column count is solved instead of fixed: every count from one to the
    //  page's knob count is tried, each is given the tile it would get, and the
    //  page takes the count whose disc comes out largest - measured, not guessed:
    //  MACHINE's seven come out as 54 px discs across a 1200 x 823 panel where the
    //  four-column grid gave 42, and as 17 px ones at the 780 x 664 floor, where
    //  that grid drew a 4 px dot in a half-empty cell.
    //
    //  A tile is then the knob's own geometry and nothing else: a 17 px caption
    //  band, the 8 px of clearance under it, the paint area itself, and the
    //  toolkit's 16 px value box under that, all inside the 5 px inset the slider
    //  rides in. The paint area is at least SQUARE - the shape whose ceilings bind
    //  at once - and the same 46 px of chrome comes off its width and its height,
    //  so a square tile is a square paint area:
    //
    //      paint width  = tile width  - 10 (the cell's inset)  - 12 (the paint's)
    //      paint height = tile height - 10 - 8 (clearance) - 16 (value) - 12
    //      square       => tile height = tile width + 24
    //
    //  and 24 is the tile's FLOOR. The tile is never shorter than the square one,
    //  because a shorter one caps the disc twice; and it is never taller than the
    //  height at which a taller one stops buying radius (that height is solved
    //  below rather than assumed, and on a small knob it is well past square).
    //  Either way the tile is COMPACT rather than stretched to the row: the old
    //  grid gave every cell the page's whole row height, the disc was drawn in a
    //  square capped by the CELL's short side, and the surplus height between the
    //  caption and the knob - and again between the knob and its own value - was
    //  left as void on every tile on every page.
    //
    //  The strip is centred in the area it has: the surplus height a page with
    //  few knobs has left over - or a page whose discs the panel's WIDTH capped,
    //  which is what a row of seven does - is split into two margins, where all
    //  of it heaped under the knobs would read as a hole in the page.
    // ------------------------------------------------------------------
    // The tile's CHROME, down the height: the caption band inside the cell (18 px),
    // the toolkit's own 16 px value box under the rotary, and the six pixels
    // drawRotarySlider keeps round itself. Across the width the same three cost
    // the cell's 10 px of inset plus the same 12 - 22 in all - and the 24 px
    // between the two is exactly the height a square tile adds to its width.
    constexpr int knobTileChrome = 46;
    // Between two rows of tiles. A gap is air the rows are separated by, and it is
    // charged to the knobs of every row - so it is the smallest one that still
    // separates two lines of type: 8 px here leaves the readout of the row above
    // and the caption band of the row below 14 px apart, where the 14 px gap this
    // was cost the height-capped pages a further 1.4 px of disc (MACHINE at
    // 1060 x 916 measures 70.2 with it and 68.8 without).
    constexpr int knobRowGap = 8;
    const auto rowsForColumns = [knobCount] (int columns)
    {
        return (knobCount + columns - 1) / columns;
    };
    // The camera the CANDIDATES are scored at. The score compares counts within
    // one page - the same band of the same panel - so a mid value is all it needs
    // to be: the wall moves by a few hundredths across the strip and moves every
    // candidate the same way.
    constexpr float knobScoringWall = 0.4f;

    // The counts are walked from the MOST rows to the fewest (one column is n rows
    // by definition, so fewer columns means more rows), and a count only displaces
    // the one held if it buys a disc a twentieth larger: a page settles on more
    // rows when the panel is tall enough for that to mean bigger knobs, and stays
    // on the single row otherwise rather than splitting itself for nothing - a
    // strip that flipped between the two arrangements on a few pixels of window
    // resize would be worse than either.
    // At each step only the count where the ROW COUNT CHANGES is scored: within
    // one row count every further column is a narrower slot, so that count is the
    // widest slot the row count has, and the only one it has to answer with.
    // The score is the disc computeKnobMetrics would give the tile - the same
    // function the knob itself is drawn from, so the layout cannot promise a disc
    // the paint routine then fails to draw.
    auto knobColumns = juce::jmax (1, knobCount);
    auto knobRows = 0;
    auto knobTileWidth = 0;
    auto knobTileHeight = 0;
    auto knobScore = 0.0f;
    auto scoredRows = 0;
    for (int columns = 1; columns <= knobCount; ++columns)
    {
        const auto rows = rowsForColumns (columns);
        if (rows == scoredRows)
            continue;

        scoredRows = rows;
        // What this count offers: the slot width it would give each tile, and the
        // height one row of `rows` has to be drawn in.
        const auto slotWidth = grid.getWidth() / columns;
        const auto rowHeight = (knobAreaHeight - (rows - 1) * knobRowGap) / rows;
        const auto paintWidth = slotWidth - 22;

        if (paintWidth < 4 || rowHeight <= knobTileChrome)
            continue;

        // The tile's HEIGHT is solved, not derived: it is the smallest height at
        // which the cap stops growing. On a big knob that height is the square
        // one - what caps the cap is the face ceiling, and a taller paint area
        // buys nothing past it. On a small knob the groove and its marks bind
        // first, and a taller paint area goes on buying radius until the ring's
        // own ceiling is reached, several pixels past square. Solving it this way
        // is what the old grid was giving away on exactly the pages the user saw
        // as cramped - and it keeps the readout under the knob as close to it as
        // the cap allows, on the pages where it does not cost anything either.
        auto tileHeight = knobTileChrome + 1;
        auto disc = 0.0f;
        for (int candidate = knobTileChrome + 1; candidate <= rowHeight; ++candidate)
        {
            const auto radius = computeKnobMetrics (static_cast<float> (paintWidth),
                                                    static_cast<float> (candidate - knobTileChrome),
                                                    knobScoringWall).radius;
            if (radius > disc)
            {
                disc = radius;
                tileHeight = candidate;
            }
        }

        if (disc > knobScore * 1.04f)
        {
            knobScore = disc;
            knobColumns = columns;
            knobRows = rows;
            knobTileWidth = slotWidth;
            knobTileHeight = tileHeight;
        }
    }
    // Nothing a panel at its own minimum size allows can make this zero, but a
    // strip with no tile would place its knobs at negative coordinates: fall back
    // to one row of the widest slots the grid's width alone can give.
    if (knobTileHeight < 1)
    {
        knobRows = knobCount > 0 ? 1 : 0;
        knobColumns = juce::jmax (1, knobCount);
        knobTileWidth = juce::jmax (1, grid.getWidth() / juce::jmax (1, knobCount));
        knobTileHeight = knobTileWidth + 24;
    }
    const auto knobStripHeight = knobRows > 0
                                     ? knobRows * knobTileHeight + (knobRows - 1) * knobRowGap
                                     : 0;
    const auto knobStripTop = grid.getY()
                              + juce::jmax (0, (knobAreaHeight - knobStripHeight) / 2);
    const auto knobSlotWidth = knobColumns > 0 ? grid.getWidth() / knobColumns : grid.getWidth();

    for (int slot = 0; slot < knobCount; ++slot)
    {
        // Which widget this slot IS. The tab table holds every knob page's
        // controls and nothing else, so a page's slot is an index into it - but
        // COMP's six and AUTOTUNE's one are the widgets the table cannot hold
        // (they belong to their page's own machinery rather than to a knob page),
        // so those two pages' slots address their label and slider directly.
        const auto slotIndex = static_cast<std::size_t> (slot);
        auto& label = compressorTab ? compressorControlLabels[slotIndex]
                                    : autotuneTab ? autoTuneAmountLabel
                                                  : controlLabels[activeTab.controls[slotIndex]];
        auto& control = compressorTab ? compressorControls[slotIndex]
                                      : autotuneTab ? autoTuneAmountSlider
                                                    : controls[activeTab.controls[slotIndex]];
        const auto row = slot / knobColumns;
        const auto column = slot % knobColumns;
        // The tiles ride EQUAL SLOTS rather than sitting edge to edge, so a page
        // whose discs the panel's height capped keeps its knobs spread across the
        // panel instead of stacking them against the left edge with all the air
        // on the right - and a last row with fewer knobs than the one above it is
        // centred as a group under it.
        const auto slotsInRow = juce::jmin (knobColumns, knobCount - row * knobColumns);
        const auto rowLeft = grid.getX() + (grid.getWidth() - slotsInRow * knobSlotWidth) / 2;
        const auto tileLeft = rowLeft + column * knobSlotWidth
                              + (knobSlotWidth - knobTileWidth) / 2;
        auto cell = juce::Rectangle<int> (tileLeft,
                                          knobStripTop + row * (knobTileHeight + knobRowGap),
                                          knobTileWidth, knobTileHeight);
        label.setBounds (cell.getX() + 5, cell.getY() + 1,
                         cell.getWidth() - 10, 17);
        auto sliderBounds = cell.reduced (5);
        // 8 px of clearance under the caption, not the 18 this used to reserve.
        // The caption's own band is 17 px tall and is laid out above this cell,
        // so the extra 10 px were never caption clearance at all - they were
        // taken off the knob, and the knob's drawing area is what is left here
        // after the readout band. The ticks stop short of the top of that area
        // (see scaleBand in drawRotarySlider), so the scale ends where this
        // clearance begins and nothing touches the caption.
        sliderBounds.removeFromTop (8);
        control.setBounds (sliderBounds);
    }

    // The deck's non-knob switches occupy grid cells of their own tab, exactly
    // where a knob would sit - same cell arithmetic, same label-above-control
    // convention - so they are laid out HERE rather than on the fixed deck rows.
    // A combo is taller than a knob cell wants, so the box rides the cell's lower
    // half under its label.
    //
    // Tab members (the combos and switches that are not knobs) ride the SAME
    // grid the knobs use, in one reserved band under the knob rows. This is the
    // systemic fix for the crooked DELAY TYPE / SYNC / RATE trio: the old code
    // hand-computed each member's rectangle from raw coordinates, so every
    // caller reinvented the cell arithmetic and the results could (and did)
    // collide with the knob rows and with each other. Now the arithmetic exists
    // exactly once: label band 17 px, control band 30 px at its 20 px offset,
    // the control capped at 30 px so it can never overflow the band.
    // memberRowY is the band's top edge, and it is measured from the grid's
    // BOTTOM - the band is the last 50 px of the grid on every page that has one,
    // and the knob strip above it is laid out in what is left. On SETTINGS, which
    // holds no knobs, the band is the whole grid and its top edge is the grid's
    // own, so the page's two lines of preferences sit under the divider as they
    // always have.
    const auto memberRowY = (memberRows != 0 && knobCount > 0)
                                ? grid.getBottom() - memberBandHeight
                                : grid.getY();

    ControlGeometry geometry;
    geometry.grid = grid;
    geometry.cellWidth = cellWidth;
    geometry.memberRowY = memberRowY;
    geometry.settingsTab = settingsTab;
    geometry.autotuneTab = autotuneTab;
    geometry.delayTab = delayTab;
    geometry.characterTab = characterTab;
    geometry.frontEndTab = frontEndTab;
    geometry.vinylTab = vinylTab;
    geometry.shapersTab = shapersTab;
    geometry.inEqTab = inEqTab;
    geometry.outEqTab = outEqTab;
    geometry.machineTab = machineTab;
    geometry.mixTab = mixTab;
    geometry.compressorTab = compressorTab;

    return geometry;
}

//==============================================================================
//  Which tab members exist. Re-asserted on EVERY resize rather than only on a tab
//  change: resized() also runs from setSize() in the constructor, before some of these
//  widgets had a parent, and a show/hide decision made only at tab-switch time left
//  them permanently visible on tabs that were not theirs.
//==============================================================================
void FirstAudioProcessorEditor::updateTabVisibility (const ControlGeometry& geometry)
{
    // Visibility for the deck switches is re-asserted here rather than only in
    // setCurrentTab, because resized() also runs from setSize() in the constructor
    // - BEFORE some of these widgets had a parent - and a show/hide decision made
    // only at tab-switch time left the switches permanently visible on tabs that
    // were not theirs. The active tab decides; every resize re-applies it. The
    // flags themselves are read from the tabSpecs table at the top of the grid
    // section above, where the member-row arithmetic needs them too.

    // The four new lists follow the same rule as the deck switches: each is a
    // full member of exactly one tab, so it is visible only while that tab is.
    diPadLabel.setVisible (geometry.frontEndTab);
    diPadBox.setVisible (geometry.frontEndTab);
    tracksLabel.setVisible (geometry.machineTab);
    tracksBox.setVisible (geometry.machineTab);
    inputEqOrderLabel.setVisible (geometry.inEqTab);
    inputEqOrderBox.setVisible (geometry.inEqTab);
    outputEqOrderLabel.setVisible (geometry.outEqTab);
    outputEqOrderBox.setVisible (geometry.outEqTab);
    // The corner switches follow their own equaliser's page, like the ORDER
    // combo beside them.
    inputEqHpButton.setVisible (geometry.inEqTab);
    inputEqLpButton.setVisible (geometry.inEqTab);
    outputEqHpButton.setVisible (geometry.outEqTab);
    outputEqLpButton.setVisible (geometry.outEqTab);
    oversamplingLabel.setVisible (geometry.settingsTab);
    oversamplingBox.setVisible (geometry.settingsTab);
    languageLabel.setVisible (geometry.settingsTab);
    languageBox.setVisible (geometry.settingsTab);
    tuningTonicLabel.setVisible (geometry.autotuneTab);
    tuningModeLabel.setVisible (geometry.autotuneTab);
    tuningTonicBox.setVisible (geometry.autotuneTab);
    tuningModeBox.setVisible (geometry.autotuneTab);
    autoTuneButton.setVisible (geometry.autotuneTab);
    autoTuneAmountLabel.setVisible (geometry.autotuneTab);
    autoTuneAmountSlider.setVisible (geometry.autotuneTab);
    for (std::size_t i = 0; i < compressorControls.size(); ++i)
    {
        compressorControls[i].setVisible (geometry.compressorTab);
        compressorControlLabels[i].setVisible (geometry.compressorTab);
    }
    // The UI-sounds switch shares SETTINGS with OVERSAMPLING as an engine-level preference.
    uiSoundsButton.setVisible (geometry.settingsTab);
    uiSoundsLabel.setVisible (geometry.settingsTab);
    antiPhaseGuardLabel.setVisible (geometry.settingsTab);
    antiPhaseGuardButton.setVisible (geometry.settingsTab);
    parameterSheetButton.setVisible (geometry.settingsTab);
    parameterSheetLabel.setVisible (geometry.settingsTab);
    // The USER theme's colour key rides SETTINGS's first band, beside OVERSAMPLING -
    // both are interface preferences rather than controls of the machine.
    userThemeButton.setVisible (geometry.settingsTab);
    userThemeLabel.setVisible (geometry.settingsTab);
    // The renderer selector is an interface preference like the two above it.
    backendLabel.setVisible (geometry.settingsTab);
    backendBox.setVisible (geometry.settingsTab);
    delayTypeLabel.setVisible (geometry.delayTab);
    delayTypeBox.setVisible (geometry.delayTab);
    delayRateLabel.setVisible (geometry.delayTab);
    delayRateBox.setVisible (geometry.delayTab);
    delaySyncLabel.setVisible (geometry.delayTab);
    delaySyncButton.setVisible (geometry.delayTab);

    // The neural model picker follows MODELS - under its own NEURAL knob - and
    // the IR loader follows it for the same reason, beside IR MIX. Each file-fed
    // stage keeps its loader and its status readout on the page its knob lives on.
    loadNeuralButton.setVisible (geometry.shapersTab);
    clearNeuralButton.setVisible (geometry.shapersTab);
    neuralStatusLabel.setVisible (geometry.shapersTab);
    loadIrButton.setVisible (geometry.shapersTab);
    clearIrButton.setVisible (geometry.shapersTab);
    irStatusLabel.setVisible (geometry.shapersTab);

    // The noise-print picker follows VINYL, the page whose VINYL MIX knob its
    // print feeds: a file-fed stage keeps its loader on the page its own knob
    // lives on, exactly like the pair above. It used to be reachable only
    // through the processor's public API - see the WP2 note in
    // docs/library-integration-plan.md - because this band did not exist.
    loadNoiseButton.setVisible (geometry.vinylTab);
    clearNoiseButton.setVisible (geometry.vinylTab);
    noiseStatusLabel.setVisible (geometry.vinylTab);

    // The three vinyl selectors stayed on CHARACTER: they re-voice the whole
    // vinyl stage, so they live with the machine's other voicing rather than
    // beside the record's faults (which are on RECORD).
    vinylGenerationLabel.setVisible (geometry.characterTab);
    vinylGenerationBox.setVisible (geometry.characterTab);
    vinylTurntableLabel.setVisible (geometry.characterTab);
    vinylTurntableBox.setVisible (geometry.characterTab);
    vinylCartridgeLabel.setVisible (geometry.characterTab);
    vinylCartridgeBox.setVisible (geometry.characterTab);

    // The mix bar takes the MIX page's whole member band: seven cells across the
    // full width is the only size at which each machine's name and percentage
    // both fit, and the band is exactly the strip the geometry above already
    // reserved for it.
    mixBar.setVisible (geometry.mixTab);
}

//==============================================================================
//  Where the live page's members are: the mix bar's own strip, then one branch per
//  page that owns combos or pills in the member band.
//==============================================================================
//==============================================================================
//  Where the MODELS page's spectrum is drawn.
//
//  The arithmetic MIRRORS paint()'s, deliberately and in the same words it uses for
//  the two loader cards: the member band is a fixed 50 px strip at the grid's bottom,
//  the grid is the controls band reduced by 14 and cut by the divider offset, and the
//  band is four columns wide. The NEURAL card takes the first column and the IR card
//  the second; this takes the TWO that are left, so the curve is a wide readout
//  instead of one narrow column with a caption that has to shrink to fit.
//
//  It exists as a function rather than as a second copy inside the timer because
//  paint() and the timer MUST agree: one draws the curve, the other decides which
//  pixels are redrawn, and a disagreement between them is a curve that is stale at
//  its edges and frozen where the invalidation missed. That is not hypothetical - it
//  is what the status pill did (see the declaration in PluginEditor.h).
//==============================================================================
juce::Rectangle<int> FirstAudioProcessorEditor::shapersSpectrumBounds() const
{
    constexpr int memberBandHeight = 50;
    constexpr int spectrumColumn = 2;

    auto grid = getEditorLayout().controls.reduced (14);
    grid.removeFromTop (controlsDividerOffset - 14 + 8);
    grid.removeFromBottom (8);

    const auto cellWidth = grid.getWidth() / tabColumns;
    const auto left = grid.getX() + spectrumColumn * cellWidth;

    //  The width is measured to the grid's own right edge rather than written as
    //  two cell widths: the grid's width is not always divisible by four, and the
    //  three leftover pixels belong to the last column of the band, not to nothing.
    return { left, grid.getBottom() - memberBandHeight,
             grid.getRight() - left, memberBandHeight };
}

void FirstAudioProcessorEditor::placeTabMembers (const ControlGeometry& geometry)
{
    if (geometry.mixTab)
    {
        mixBar.setBounds (geometry.grid.getX() + 4, geometry.memberRowY + 3, geometry.grid.getWidth() - 8,
                          juce::jmax (20, geometry.grid.getBottom() - geometry.memberRowY - 6));
        mixBar.repaint();
    }

    if (geometry.frontEndTab)
    {
        // FRONT END: the DI's own pad/load selector rides the band under the
        // knobs - the row the DI feeds. It used to take the fourth cell of the
        // knob grid's second row; the strip is one full row now, so the band is
        // the only place on the page a combo can be, and it is where the DI's
        // own input switch belongs anyway.
        placeDeckSwitch (geometry, diPadLabel, diPadBox, 0, 1, "DI PAD");
    }
    else if (geometry.machineTab)
    {
        // MACHINE: the machine's TRACKS selector rides the band at the page's
        // foot, under its seven knobs - and CENTRED in it.
        //
        // Pinned to the first cell it left three cells of empty metal beside it,
        // which is the user's "there is still a lot of free space, to the right
        // of TRACKS": the page has exactly one member, so the member band is the
        // control's whole world and a page of one should not be laid out as if
        // three more were coming. Two cells wide, centred under the knob row,
        // gives the list room for a name and keeps the air either side equal.
        //  The cell is written from the grid's two EDGES rather than as two cell
        //  widths: the grid's width is not always divisible by four, and the
        //  leftover pixels belong to the band's outer margins, not to one side.
        const auto tracksLeft = geometry.grid.getX() + geometry.cellWidth;
        const auto tracksRight = geometry.grid.getRight() - geometry.cellWidth;
        const auto tracksCell = juce::Rectangle<int> (tracksLeft, geometry.memberRowY,
                                                      juce::jmax (1, tracksRight - tracksLeft),
                                                      geometry.grid.getBottom() - geometry.memberRowY);
        tracksLabel.setText ("TRACKS", juce::dontSendNotification);
        tracksLabel.setJustificationType (juce::Justification::centred);
        tracksLabel.setBounds (tracksCell.getX() + 5, tracksCell.getY() + 1,
                               tracksCell.getWidth() - 10, 17);
        tracksBox.setBounds (tracksCell.getX() + 12, tracksCell.getY() + 20,
                             tracksCell.getWidth() - 24,
                             juce::jmin (30, tracksCell.getHeight() - 22));
    }
    else if (geometry.settingsTab)
    {
        // ---- SETTINGS: a preference sheet, not a clump at the top ---------
        //
        // This page holds no knobs, so its grid IS the page - and until now its
        // seven preferences were placed in the first ~106 px of it: three through
        // placeDeckSwitch and three hand-written at +56 / +76, with the rest of
        // the page left as empty metal. That is the user's report exactly: "many
        // switches and one list, the switches are small, and there is a lot of
        // free space on the tab".
        //
        // Two rows now share the whole page - four cells on the first, three
        // centred on the second - and the control height is solved from the row
        // it is given: 30 px at the tightest window, up to 46 px when there is
        // room. Every one of the seven goes through ONE placement, so a list,
        // a key and a switch are the same rectangle by construction; what
        // differs is only what is drawn on the plate.
        constexpr int settingsRowGap = 12;
        const auto rowHeight = juce::jmax (54, (geometry.grid.getHeight() - settingsRowGap) / 2);
        //  56 rather than 46. A SETTINGS row is half the page - around 144 px at
        //  the preferred window - and a 46 px control left a hundred pixels of
        //  empty metal around every switch on the one page with no knobs to use
        //  it. The plate grows into its row now; 56 is still a key and not a
        //  slab, and it is the same cap the roomier rows have always solved to.
        const auto controlHeight = juce::jlimit (30, 56, rowHeight - 30);

        const auto placeSetting = [&] (juce::Label& targetLabel, juce::Component& targetBox,
                                       int column, int row, int rowLeft)
        {
            const auto rowTop = geometry.grid.getY()
                                + row * (rowHeight + settingsRowGap);
            // The caption-over-control pair is centred in its row, so a tall page
            // gets air above and below the pair rather than a growing hole under
            // the page's last line.
            const auto blockTop = rowTop
                                  + juce::jmax (0, (rowHeight - (17 + controlHeight)) / 2);
            const auto cellLeft = rowLeft + column * geometry.cellWidth + 12;

            targetLabel.setBounds (cellLeft, blockTop, geometry.cellWidth - 24, 17);
            targetBox.setBounds (cellLeft, blockTop + 17, geometry.cellWidth - 24,
                                 controlHeight);
        };

        const auto fullRow = geometry.grid.getX();
        // A three-item row is centred under the four-item one - the same rule the
        // knob strip uses for its short last row, so a page whose contents do not
        // divide evenly still reads as one grid.
        const auto shortRow = geometry.grid.getX()
                              + (geometry.grid.getWidth() - 3 * geometry.cellWidth) / 2;

        // Row one: the two choices, the colour window and the second view of the
        // machine - the four things that say what the panel IS.
        placeSetting (oversamplingLabel,    oversamplingBox,     0, 0, fullRow);
        placeSetting (backendLabel,         backendBox,          1, 0, fullRow);
        placeSetting (userThemeLabel,       userThemeButton,     2, 0, fullRow);
        placeSetting (parameterSheetLabel,  parameterSheetButton, 3, 0, fullRow);

        // Row two: the language, the interface sounds and the guard - the three
        // things that describe how it behaves while you use it.
        placeSetting (languageLabel,         languageBox,          0, 1, shortRow);
        placeSetting (uiSoundsLabel,         uiSoundsButton,       1, 1, shortRow);
        placeSetting (antiPhaseGuardLabel,   antiPhaseGuardButton, 2, 1, shortRow);

        // The caption is put back to the left, where every other caption on the
        // page sits: the header grid also places this one Label - right-aligned,
        // beside THEME - and that justification survived into this page, which is
        // why LANGUAGE was the one caption on SETTINGS that read centred.
        languageLabel.setJustificationType (juce::Justification::left);
    }
    else if (geometry.autotuneTab)
    {
        sectionCaptionLabel.setText ("AUTOTUNE", juce::dontSendNotification);
        // Keep the key and scale together with the corrector that uses them. The
        // tune-amount knob occupies the strip above; the member band holds TONIC,
        // MODE and the AUTO TUNE switch in its four columns.
        placeDeckSwitch (geometry, tuningTonicLabel, tuningTonicBox, 0, 1, "TONIC");
        // ONE band for the page: the MODE list, the AUTO TUNE switch and the TONIC
        // list beside them are the same plate, so a short window shrinks all three
        // together - the combo used to be pinned at 30 px while its neighbour gave
        // way, which is the "two sizes on one row" report in miniature.
        const auto bandHeight = juce::jmin (30, geometry.grid.getBottom() - geometry.memberRowY - 22);
        const auto controlY = geometry.memberRowY + 20;
        tuningModeLabel.setBounds (geometry.grid.getX() + geometry.cellWidth + 5,
                                   geometry.memberRowY + 1,
                                   2 * geometry.cellWidth - 10, 17);
        tuningModeBox.setBounds (geometry.grid.getX() + geometry.cellWidth + 12,
                                 controlY,
                                 2 * geometry.cellWidth - 24, bandHeight);
        autoTuneButton.setBounds (geometry.grid.getX() + 3 * geometry.cellWidth + 12,
                                  controlY,
                                  geometry.cellWidth - 24, bandHeight);
    }
    // The COMP page has no branch here, and that is the point: its six knobs are
    // laid out by the knob strip above, like every other page's, and the page's
    // caption comes from its own tabSubtitles entry ("the conventional
    // compressor, independent of glue") like every other page's. It used to own
    // a placement of its own - each slider given a cell as tall as the whole
    // grid, which drew a small cap at the top of that cell and left its readout
    // a hundred-odd pixels below the knob it belonged to - and a caption of its
    // own, which is what the uppercase line above this chain used to say.
    else if (geometry.delayTab)
    {
        // SPACE: the delay trio rides the member row under the delay knobs.
        placeDeckSwitch (geometry, delayTypeLabel, delayTypeBox, 0, 1, "TYPE");
        placeDeckSwitch (geometry, delaySyncLabel, delaySyncButton, 1, 1, "SYNC DELAY");
        placeDeckSwitch (geometry, delayRateLabel, delayRateBox, 2, 1, "RATE");
    }
    else if (geometry.characterTab)
    {
        // CHARACTER: three knobs in one row, so the member band under them
        // carries the page's three voicing lists - left to right, the order
        // the record is made in: what was cut, what plays it, what reads it.
        placeDeckSwitch (geometry, vinylGenerationLabel, vinylGenerationBox, 0, 1, "GENERATION");
        placeDeckSwitch (geometry, vinylTurntableLabel, vinylTurntableBox, 1, 1, "TURNTABLE");
        placeDeckSwitch (geometry, vinylCartridgeLabel, vinylCartridgeBox, 2, 1, "CARTRIDGE");
    }
    else if (geometry.shapersTab)
    {
        //  MODELS: the two file-fed stages on one page, each with its loader
        //  under its own knob, and each drawn as the SAME CARD.
        //
        //  The card is the unit of the page, not the keys: the NEURAL pair and
        //  the IR pair are the same two-control device with the same readout
        //  line over it, so they get the same cell, the same inset, the same
        //  readout height and the same key width - by construction, through one
        //  placement routine called twice. That is what the user was asking for
        //  when they said the two stages "look different": before this, the
        //  neural pair sat on the painted card in paint() and the IR pair sat on
        //  bare metal, and the IR readout - two cells wide and centred - landed
        //  over the spectrum's own caption while the neural one sat over its
        //  keys.
        //
        //  The band is four cells wide: two cards, then the tape spectrum across
        //  the remaining two. Every number below is a literal shared by both
        //  cards, and paint() draws each card from the same cell arithmetic.
        const auto bandHeight = geometry.grid.getBottom() - geometry.memberRowY;
        constexpr int cardInset = 8;
        constexpr int keyGap = 6;
        constexpr int readoutHeight = 14;
        constexpr int keyTop = 22;          // under the readout line, inside the card
        const auto keyH = juce::jlimit (16, 26, bandHeight - keyTop - 4);
        const auto keyWidth = juce::jmax (24, (geometry.cellWidth - 2 * cardInset - keyGap) / 2);

        const auto placeLoaderCard = [&] (juce::Label& readout, juce::TextButton& load,
                                          juce::TextButton& clear, int column)
        {
            const auto cellLeft = geometry.grid.getX() + column * geometry.cellWidth;
            const auto cardWidth = geometry.cellWidth - 2 * cardInset;

            //  The readout is the card's caption line - it belongs INSIDE the
            //  card, three pixels below its top edge. The previous placement put
            //  it at memberRowY - 1, straddling the border it should sit inside.
            readout.setBounds (cellLeft + cardInset, geometry.memberRowY + 3,
                               cardWidth, readoutHeight);
            readout.setJustificationType (juce::Justification::centred);

            load.setBounds (cellLeft + cardInset, geometry.memberRowY + keyTop,
                            keyWidth, keyH);
            clear.setBounds (cellLeft + cardInset + keyWidth + keyGap,
                             geometry.memberRowY + keyTop, keyWidth, keyH);
        };

        placeLoaderCard (neuralStatusLabel, loadNeuralButton, clearNeuralButton, 0);
        placeLoaderCard (irStatusLabel, loadIrButton, clearIrButton, 1);
    }
    else if (geometry.vinylTab)
    {
        // VINYL: the record-playing stage's three knobs fill the row above, and
        // the band under them carries the surface those knobs act ON - the
        // user's own noise print. The readout is the band's caption line across
        // TWO cells, centred, because a file name is the one string on this
        // page whose length is not known in advance, and the two buttons share
        // the control line under it: LOAD PRINT under the readout's left half,
        // CLEAR PRINT under its right. Centred rather than pinned left because
        // the three knobs above span the whole row - a pair at the left edge
        // would read as belonging to the first knob, which is VINYL MIX.
        const auto buttonY = geometry.memberRowY + 20;
        const auto buttonH = juce::jmin (30, geometry.grid.getBottom() - geometry.memberRowY - 22);
        const auto leftEdge = geometry.grid.getX() + geometry.cellWidth;
        loadNoiseButton.setBounds (leftEdge + 12, buttonY, geometry.cellWidth - 24, buttonH);
        clearNoiseButton.setBounds (leftEdge + geometry.cellWidth + 12, buttonY,
                                    geometry.cellWidth - 24, buttonH);
        noiseStatusLabel.setBounds (leftEdge + 5, geometry.memberRowY - 1,
                                    2 * geometry.cellWidth - 10, 17);
    }
    else if (geometry.inEqTab || geometry.outEqTab)
    {
        // IN EQ / OUT EQ: six knobs fill the first row, so the row below is
        // where the equaliser's three corner controls go - the slope ORDER
        // first, then the two on/off switches for the corners whose frequency
        // knobs are in the row above.
        //
        // The switches are placed by the SAME cell arithmetic as everything
        // else (label band, control band, cell width), through placeEqCorner
        // below, rather than by hand-placed coordinates: the two switches
        // share the row with the ORDER combo, and a coordinate written for one
        // of them would be a guess about the other's.
        const auto isIn = geometry.inEqTab;
        placeDeckSwitch (geometry, isIn ? inputEqOrderLabel : outputEqOrderLabel,
                         isIn ? inputEqOrderBox : outputEqOrderBox, 0, 1, "ORDER");

        const auto placeEqCorner = [&] (juce::Component& button, int column)
        {
            auto cell = juce::Rectangle<int> (geometry.grid.getX() + column * geometry.cellWidth,
                                              geometry.memberRowY,
                                              geometry.cellWidth, geometry.grid.getBottom() - geometry.memberRowY);
            button.setBounds (cell.getX() + 12, cell.getY() + 20,
                              cell.getWidth() - 24,
                              juce::jmin (30, cell.getHeight() - 22));
        };

        if (isIn)
        {
            placeEqCorner (inputEqHpButton, 1);
            placeEqCorner (inputEqLpButton, 2);
        }
        else
        {
            placeEqCorner (outputEqHpButton, 1);
            placeEqCorner (outputEqLpButton, 2);
        }
    }
}

//==============================================================================
//  A tab member - a combo or a pill that is not a knob - in the member band's own cell
//  arithmetic: the same cell the knob above it would have had, a 17 px caption band,
//  and a 30 px control band centred under it.
//
//  This was a lambda inside resized(), which meant every caller had to be in that one
//  body and the cell arithmetic was only reachable from there; it is a member function
//  now because the band it measures in arrives as the geometry the strip solved. The
//  name is the lambda's, minus its capture list.
//==============================================================================
void FirstAudioProcessorEditor::placeDeckSwitch (const ControlGeometry& geometry,
                                                 juce::Label& label,
                                                 juce::Component& box,
                                                 int column, int columnsWide,
                                                 const juce::String& caption)
{
    // The body is a lambda called once, at the bottom, and that shape is what
    // tests/layout asserts on: it checks that this member INVOKES its
    // bounds-setting helper rather than only defining one, which is a mistake this
    // function has made before. The lambda keeps the function's name for the same
    // reason - it IS the lambda this member function was extracted from - and its
    // parameters are named apart from the member's own on purpose: taking the same
    // five names is five -Wshadow warnings, on Clang and GCC alike, for a call
    // that passes each one straight through.
    const auto placeDeckSwitch = [&] (juce::Label& targetLabel, juce::Component& targetBox,
                                      int columnIndex, int columns, const juce::String& text)
    {
        auto cell = juce::Rectangle<int> (geometry.grid.getX() + columnIndex * geometry.cellWidth,
                                          geometry.memberRowY,
                                          columns == tabColumns
                                              ? geometry.grid.getRight() - (geometry.grid.getX() + columnIndex * geometry.cellWidth)
                                              : columns * geometry.cellWidth
                                                  + (columns - 1) * (geometry.cellWidth / columns),
                                          geometry.grid.getBottom() - geometry.memberRowY);
        // This setText() every layout is also why the GL label's constructor
        // emptiness is safe: the caption is re-written here every time its tab is
        // live, so a resize between tab switches can never leave a stale one.
        targetLabel.setText (text, juce::dontSendNotification);
        targetLabel.setBounds (cell.getX() + 5, cell.getY() + 1, cell.getWidth() - 10, 17);
        // The control band used to be pinned to the cell's TOP (y + 20) at full
        // remaining height, so on tabs whose member band is taller than 30 px
        // (DRIVE's member row inherits three knob rows' worth of height) the
        // toggle stretched into a tall pill with the capsule's rounding reading
        // as a bulge - the crooked switches on the DRIVE page. The band is now
        // a 30 px control vertically CENTRED under its caption, the same height
        // every other switch in the panel shows.
        const auto bandHeight = juce::jmin (30, cell.getHeight() - 22);
        // The centring is measured against the band's NOMINAL height (50 px),
        // never against the cell's. On SETTINGS the band is the whole grid - the
        // page holds no knobs to take the rest of it - and measuring against
        // that threw OVERSAMPLING 168 px below its own caption while UI SOUNDS,
        // which is placed directly at +20, stayed at the top: one
        // page, two layouts. With the nominal band they sit under their captions
        // on every page, and the arithmetic is unchanged everywhere the band is
        // 50 px or a few pixels taller.
        const auto centringBand = juce::jmin (cell.getHeight(), 50);
        const auto bandY = cell.getY() + 20
                           + juce::jmax (0, (centringBand - 22 - bandHeight) / 2);
        targetBox.setBounds (cell.getX() + 12, bandY,
                             cell.getWidth() - 24, bandHeight);
    };

    placeDeckSwitch (label, box, column, columnsWide, caption);
}

//==============================================================================
//  The meters panel: its heading, hint and the compressor's readout pair, then the
//  four dials in a 2 x 2 grid - the rows sharing the available height, so the
//  arrangement holds at any panel size.
//==============================================================================
void FirstAudioProcessorEditor::placeMeters (const EditorLayout& layout)
{
    metersHeadingLabel.setBounds (layout.meters.getX() + 16, layout.meters.getY() + 8, 130, 18);
    metersHintLabel.setBounds (layout.meters.getX() + 16, layout.meters.getY() + 27, 150, 14);
    const auto compressorLabelWidth = 120;
    compressorLabel.setBounds (layout.meters.getRight() - compressorLabelWidth - 14,
                               layout.meters.getY() + 8, compressorLabelWidth, 18);
    compressorReadout.setBounds (layout.meters.getRight() - compressorLabelWidth - 14,
                                 layout.meters.getY() + 27, compressorLabelWidth, 14);

    // Four meters in a 2 x 2 grid inside the meters panel:
    //   row 1 - INPUT and OUTPUT level VU meters
    //   row 2 - COMP IN and COMP OUT gain-reduction meters
    // The rows share the available height evenly, so the arrangement (and the
    // input-left / output-right reading order) holds at any panel size.
    auto meterArea = layout.meters.reduced (12, 0);
    meterArea.removeFromTop (46);           // heading + hint labels, with 4 px clearance
    meterArea.removeFromBottom (8);

    const auto columnGap = 8;
    const auto rowGap = 8;
    const auto columnWidth = (meterArea.getWidth() - columnGap) / 2;

    // Note the separate name: the knob strip above has already spent "tile" and
    // "slot" on the grid's cells, so keeping the meters' own vocabulary here is
    // what stops a later edit from silently reusing the wrong cell size.
    const auto meterRowHeight = (meterArea.getHeight() - rowGap) / 2;

    const auto leftColumn = meterArea.getX();
    const auto rightColumn = meterArea.getRight() - columnWidth;
    const auto topRow = meterArea.getY();
    const auto bottomRow = meterArea.getBottom() - meterRowHeight;

    inputMeter.setBounds (leftColumn, topRow, columnWidth, meterRowHeight);
    outputMeter.setBounds (rightColumn, topRow, columnWidth, meterRowHeight);
    compressorMeterIn.setBounds (leftColumn, bottomRow, columnWidth, meterRowHeight);
    compressorMeterOut.setBounds (rightColumn, bottomRow, columnWidth, meterRowHeight);
}

//==============================================================================
//  The panel's overlap check, DEBUG only - a quadratic sweep on a path that runs
//  whenever the window moves, which is nothing to pay for in a development build and
//  nothing at all in a release one.
//
//  It runs LAST, on the bounds every band above has just written: a function called
//  after resized() returns would measure the rectangles of the PREVIOUS resize, which
//  is a stale rectangle and a collision that is not there.
//==============================================================================
void FirstAudioProcessorEditor::verifyPanelHasNoOverlaps() const
{
#if JUCE_DEBUG || DEBUG
    // ------------------------------------------------------------------
    //  The overlap check.
    //
    //  The panel's most persistent complaint has been furniture printed on top
    //  of furniture - captions over knobs, switches over switches - and it
    //  kept coming back because nothing in the build can see it: every
    //  individual line of a hand-placed layout is a correct rectangle, and the
    //  error lives in the RELATIONSHIP between two of them. Ten pixels is a
    //  real collision and a perfectly reasonable-looking number. It is found
    //  here, in the two places it happened: the header's AUTO GAIN ran into
    //  THEME by ten pixels, and the mode button's own row was the preset row's,
    //  so COMPARE, UNDO, REDO and the badge were printed underneath it.
    //
    //  So the relationship is what gets checked. The furniture is listed BY
    //  NAME rather than swept up from getNumChildComponents(), because the
    //  things that are laid out by arithmetic rather than by hand - the knob
    //  grid, the tab bar, the meters, the vinyl reel - are computed from a
    //  member count and cannot overlap by construction, and sweeping them in
    //  would bury the real signal in a hundred legitimate grid contacts. The
    //  list is the panel's hand-placed surface, and it is the one place a new
    //  switch has to be added to be covered.
    //
    //  It runs LAST in resized(), at the real bounds just set: an item placed
    //  further down the function - the tab grid, the deck switches, the meters
    //  - would otherwise be measured at the bounds it had before this resize,
    //  which is a stale rectangle and reports a collision that is not there.
    //  Opening the editor at the 780 x 816 minimum - where the clearance is
    //  thinnest and where the collisions actually happened - is therefore
    //  enough to fail on the next one, rather than on the next person to open
    //  it on a big screen.
    //
    //  DEBUG only: a quadratic sweep on a path that runs whenever the window
    //  moves is nothing to pay for in a development build and nothing at all
    //  in a release one.
    // ------------------------------------------------------------------
    {
        const struct { const char* name; const juce::Component* component; } furniture[] = {
            // The header: the two switch rows, the theme key and the readout.
            { "brand",         &brandLabel },
            { "renderer",      &rendererLabel },
            { "title",         &titleLabel },
            { "subtitle",      &subtitleLabel },
            { "status",        &statusLabel },
            { "bypass",        &bypassButton },
            { "delta",         &deltaButton },
            { "polarity",      &polarityButton },
            { "autoGain",      &autoGainButton },
            { "osc",           &oscButton },
            { "theme",         &themeButton },

            // The deck, line by line, in the order resized() places them.
            { "deckHeading",   &deckHeadingLabel },
            { "build",         &buildLabel },
            { "tapeTypeLabel", &tapeTypeLabel },
            { "tapeTypeBox",   &tapeTypeBox },
            { "speedLabel",    &speedLabel },
            { "speedBox",      &speedBox },
            { "bpmLabel",      &bpmLabel },
            { "bpmReadout",    &bpmReadout },
            { "oversampLabel", &oversamplingLabel },
            { "oversampBox",   &oversamplingBox },
            { "instrLabel",    &instrumentLabel },
            { "instrBox",      &instrumentBox },
            { "harmonics",     &harmonicsLabel },
            { "harmonicsVal",  &harmonicsReadout },
            { "subfund",       &subfundLabel },
            { "subfundVal",    &subfundReadout },
            { "antiPhase",     &antiPhaseLabel },
            { "antiPhaseVal",  &antiPhaseReadout },
            { "vinylTypeLabel",     &vinylTypeLabel },
            { "vinylTypeBox",       &vinylTypeBox },
            { "vinylSpeedLabel",    &vinylSpeedLabel },
            { "vinylSpeedBox",      &vinylSpeedBox },
            { "valveTypeLabel",     &valveTypeLabel },
            { "valveTypeBox",       &valveTypeBox },
            { "ampTypeLabel",       &ampTypeLabel },
            { "ampTypeBox",         &ampTypeBox },
            { "transformerLabel",   &transformerTypeLabel },
            { "transformerBox",     &transformerTypeBox },
            { "digitalTypeLabel",   &digitalTypeLabel },
            { "digitalTypeBox",     &digitalTypeBox },
            { "oscRing",            &oscRing },
            { "transportLabel",     &transportLabel },
            { "spindownLabel",      &spindownLabel },
            { "stop",               &transportStopButton },
            { "play",               &transportPlayButton },
            { "start",              &transportStartButton },
            { "spindown",           &spindownButton },
            { "deckHint",           &deckHintLabel },
            { "mode",               &modeCycleButton },
            { "presetHeading",      &presetHeadingLabel },
            { "presetBox",          &presetBox },
            { "userPresetBox",      &userPresetBox },
            { "savePreset",         &savePresetButton },
            { "deletePreset",       &deletePresetButton },
            { "copyA",              &copyAButton },
            { "copyB",              &copyBButton },
            { "compare",            &compareButton },
            { "undo",               &undoButton },
            { "redo",               &redoButton },
            { "compareBadge",       &compareBadgeLabel },
            { "presetBadge",        &presetBadgeLabel },

            // The tab pages' own members: one page's worth is hand-placed on the
            // member band at a time, exactly like the deck's line above - and
            // they were NOT in this list, which is how UI SOUNDS came to be
            // painted on top of the TONIC combo for a whole release: both
            // rectangles were reasonable on their own, and nothing looked at
            // their relationship. Only the visible page's members are measured,
            // so this stays a sweep of what a person can actually see.
            { "langLabel",      &languageLabel },
            { "langBox",        &languageBox },
            { "tuningTonicLbl", &tuningTonicLabel },
            { "tuningTonicBox", &tuningTonicBox },
            { "tuningModeLbl",  &tuningModeLabel },
            { "tuningModeBox",  &tuningModeBox },
            { "uiSoundsLbl",    &uiSoundsLabel },
            { "uiSounds",       &uiSoundsButton },
            { "guardLabel",     &antiPhaseGuardLabel },
            { "guardBtn",       &antiPhaseGuardButton },
            { "paramSheetLbl",  &parameterSheetLabel },
            { "paramSheet",     &parameterSheetButton },
            // The USER theme's caption and colour key, in SETTINGS's first band.
            // They are here because this list is the one place a hand-placed switch
            // has to be named to be covered: the key shares its page with the four
            // second-band preferences, and "plausible on its own, painted over the
            // control below it" is exactly the defect this sweep exists for.
            { "userThemeLbl",   &userThemeLabel },
            { "userThemeBtn",   &userThemeButton },
            // The renderer selector's caption and combo share the first band with
            // them, and a hand-placed pair is covered here or not at all.
            { "backendLbl",     &backendLabel },
            { "backendBox",     &backendBox },
            { "diPadLabel",     &diPadLabel },
            { "diPadBox",       &diPadBox },
            { "tracksLabel",    &tracksLabel },
            { "tracksBox",      &tracksBox },
            { "delayTypeLabel", &delayTypeLabel },
            { "delayTypeBox",   &delayTypeBox },
            { "delaySyncLabel", &delaySyncLabel },
            { "delaySyncBtn",   &delaySyncButton },
            { "delayRateLabel", &delayRateLabel },
            { "delayRateBox",   &delayRateBox },
            { "vinylGenLabel",  &vinylGenerationLabel },
            { "vinylGenBox",    &vinylGenerationBox },
            { "vinylTTLabel",   &vinylTurntableLabel },
            { "vinylTTBox",     &vinylTurntableBox },
            { "vinylCartLabel", &vinylCartridgeLabel },
            { "vinylCartBox",   &vinylCartridgeBox },
            { "loadNeural",     &loadNeuralButton },
            { "clearNeural",    &clearNeuralButton },
            { "neuralStatus",   &neuralStatusLabel },
            { "loadIr",         &loadIrButton },
            { "clearIr",        &clearIrButton },
            { "irStatus",       &irStatusLabel },
            { "loadNoise",      &loadNoiseButton },
            { "clearNoise",     &clearNoiseButton },
            { "noiseStatus",    &noiseStatusLabel },
            { "inEqOrderLbl",   &inputEqOrderLabel },
            { "inEqOrderBox",   &inputEqOrderBox },
            { "outEqOrderLbl",  &outputEqOrderLabel },
            { "outEqOrderBox",  &outputEqOrderBox },
            { "inEqHp",         &inputEqHpButton },
            { "inEqLp",         &inputEqLpButton },
            { "outEqHp",        &outputEqHpButton },
            { "outEqLp",        &outputEqLpButton },
            { "autoTune",       &autoTuneButton },
            { "autoTuneLbl",    &autoTuneAmountLabel },
            { "autoTuneAmt",    &autoTuneAmountSlider },
        };

        // Only what is on screen counts: a hidden tab member is deliberately
        // placed on top of its neighbours and shown later, one tab at a time.
        std::vector<std::pair<const char*, juce::Rectangle<int>>> placed;

        for (const auto& item : furniture)
        {
            const auto bounds = item.component->getBounds();

            // A zero-sized or invisible item cannot collide with anything a
            // person can see, and the panel uses one-pixel rules as dividers.
            if (! item.component->isVisible() || bounds.getWidth() <= 1 || bounds.getHeight() <= 1)
                continue;

            placed.emplace_back (item.name, bounds);
        }

        for (std::size_t a = 0; a < placed.size(); ++a)
        {
            for (std::size_t b = a + 1; b < placed.size(); ++b)
            {
                const auto& first  = placed[a].second;
                const auto& second = placed[b].second;

                // Touching edges are a row, not a collision, and the panel
                // places several pairs exactly edge to edge on purpose. Only a
                // TRUE overlap - positive area in both axes - is a fault.
                if (first.getRight() <= second.getX() || second.getRight() <= first.getX()
                    || first.getBottom() <= second.getY() || second.getBottom() <= first.getY())
                    continue;

                jassertfalse;
                std::cout << "overlap: " << placed[a].first << " " << first.toString()
                          << "  with  " << placed[b].first << " " << second.toString()
                          << std::endl;
            }
        }
    }
#endif
}
