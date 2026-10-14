#include "PluginProcessor.h"

// The generated resource accessors: the factory presets and the translation
// tables, both compiled into the binary by CMakeLists.txt.
#include <BinaryData.h>
#include "PluginEditor.h"

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
// The shared label look: text, font size and weight, colour, alignment, and no
// mouse interception so a caption never steals a click from the control under it.
// This is a member function, not a constructor-local lambda, because resized()
// styles the knob-grid section captions too and a lambda declared in the
// constructor is out of scope there - which is what broke the Linux, Windows and
// macOS builds with C2065.
void FirstAudioProcessorEditor::styleLabel (juce::Label& label, const juce::String& text,
                                            float size, juce::Colour colour,
                                            bool bold, juce::Justification justification)
{
    // The small captions beside the engine switches and under the member rows
    // used to render a size (or two) larger than the control they name, which is
    // what read as off on this panel - a caption should sit UNDER its control,
    // not beside it. One size down from what the call site asks for, with a
    // slight squeeze floor so a long caption ellipsises instead of spilling into
    // the neighbour's cell.
    label.setText (text, juce::dontSendNotification);
    label.setFont (juce::Font (juce::FontOptions (juce::jmax (8.5f, size - 1.0f),
                                                  bold ? juce::Font::bold : juce::Font::plain)));
    label.setJustificationType (justification);
    label.setMinimumHorizontalScale (0.82f);
    label.setColour (juce::Label::textColourId, colour);
    label.setInterceptsMouseClicks (false, false);
}

//==============================================================================
// The GL switch is the one panel control whose LOOK is its state, so both come from
// the context. It is a plain TextButton, not a ToggleButton, which is what made it
// look wrong in both states: it was the only TextButton in the panel never given a
// themed buttonColourId, so it kept LookAndFeel_V4's light default background on a
// dark panel, and because getToggleState() is always false for it, drawButtonText
// read textColourOffId for ON as well - so GL ON and GL OFF were the same grey with
// different words in them.
void FirstAudioProcessorEditor::styleGlButton (bool isOn)
{
    const auto& palette = paletteFor (darkTheme);

    glButton.setColour (juce::TextButton::buttonColourId, isOn ? palette.accent : palette.raised);
    glButton.setColour (juce::TextButton::buttonOnColourId, isOn ? palette.accent : palette.raised);

    // drawButtonText picks between textColourOnId and textColourOffId with
    // getToggleState(), which is always false here, so OFF is the id that matters.
    glButton.setColour (juce::TextButton::textColourOffId, isOn ? palette.readout : palette.text);
    glButton.setColour (juce::TextButton::textColourOnId, isOn ? palette.readout : palette.text);
    glButton.repaint();
}

void FirstAudioProcessorEditor::syncGlSwitchState()
{
    // A non-OpenGL backend is currently represented by the CPU fallback. Keep
    // the existing control for compatibility, but do not claim that an
    // unsupported native API has been initialised.
    if (! j37::render::usesOpenGL())
    {
        glButton.setButtonText (juce::String ("CPU / ") + TapeScene::effectiveRenderBackendName());
        glButton.setTooltip (TapeScene::renderBackendStatus());
        styleGlButton (false);
        tapeScene.setSceneEnabled (false);
        rebuildPanelTextureLayers();
        return;
    }

    // Read the context, never a cached flag: attachTo() returns void, so the only
    // honest answer to "did it work" is to ask afterwards. The 3D transport takes
    // the same answer, which is what stops the panel from showing a lit button
    // beside a blank window - or a 3D transport on a machine where the user has
    // turned the accelerator off.
    const auto isOn = openGLContext.isAttached();

    glButton.setButtonText (isOn ? "GL ON" : "GL OFF");
    glButton.setTooltip ("OpenGL accelerated scene");
    styleGlButton (isOn);
    tapeScene.setSceneEnabled (isOn);

    // The texture image is palette-aware (the ink follows the theme), so a
    // theme flip rebuilds it on the spot.
    rebuildPanelTextureLayers();
}

// The arithmetic texture's doses, one call for both renderers (see paint() and
// TapeScene::panelTexture): grain = static tooth, shimmer = live tape grain,
// wear = the blotches. The shimmer term carries the drive and the gain
// reduction, so the surface is ALIVE where the machine pushes signal - the
// same weights the fragment shader reads.
void FirstAudioProcessorEditor::configureTextureWeights (float drive, float gainReduction)
{
    constexpr float grainWeight  = 0.040f;
    constexpr float shimmerBase  = 0.015f;
    constexpr float shimmerDrive = 0.030f;
    constexpr float wearWeight   = 0.030f;

    // The doses above were tuned on the dark panel, where a faint light ink
    // reads against a near-black face. On the light panel the SAME alpha of a
    // DARK ink sits on a bright face and mostly vanishes - the user's own
    // report: the texture simply was not there. So light-theme doses are
    // multiplied up: both renderers receive the boosted weights (this feeds
    // the 2D image AND the scene's uTextureWeights), so GPU and software
    // panels keep showing the same surface.
    const auto doseBoost = darkTheme ? 1.0f : 2.2f;

    panelTextureDrive = juce::jlimit (0.0f, 1.0f, drive);
    panelTextureGainReduction = juce::jlimit (0.0f, 1.0f, gainReduction);

    const auto shimmer = (shimmerBase + shimmerDrive * panelTextureDrive) * doseBoost;
    panelTextureWeights = { grainWeight * doseBoost, shimmer, wearWeight * doseBoost };

    tapeScene.setTextureWeights (grainWeight * doseBoost, shimmer, wearWeight * doseBoost);
}

// One image per resize: the static grain (hash of screen pixels) and the wear
// blotches (value noise over a coarse grid), both at the design doses. Built at
// half resolution and scaled on draw, which is what keeps the one-time cost at
// a fraction of a second and the per-frame cost at a single blit.
void FirstAudioProcessorEditor::rebuildPanelTextureLayers()
{
    const auto bounds = getLocalBounds();
    if (bounds.isEmpty())
    {
        panelTextureImage = {};
        return;
    }

    constexpr float grainWeight = 0.040f;
    constexpr float wearWeight  = 0.030f;

    // Light theme prints DARK ink on a bright face, where the same alpha
    // nearly disappears - so the static layers are boosted by the same
    // factor configureTextureWeights uses for the live ones. The two dose
    // sets have to agree or the static grain and the shimmer read as two
    // different materials.
    const auto doseBoost = darkTheme ? 1.0f : 2.2f;
    const auto grainDose = grainWeight * doseBoost;
    const auto wearDose  = wearWeight * doseBoost;

    const auto w = juce::jmax (1, bounds.getWidth() / 2);
    const auto h = juce::jmax (1, bounds.getHeight() / 2);
    auto image = juce::Image (juce::Image::ARGB, w, h, true);

    const auto hashOf = [] (float x)
    {
        return std::fmod (std::abs (std::sin (x) * 43758.5453123f), 1.0f);
    };

    // Grain: the shader's hash(face * 7.5) against half-res pixel coordinates,
    // one centered ink dot per cell, centered on the panel colour so the layer
    // only adds texture, never tint.
    const auto grainInk = paletteFor (darkTheme).text;
    const auto grainEdge = paletteFor (darkTheme).knobEdge;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const auto n = hashOf (static_cast<float> (x * y) * 7.5f
                                   + static_cast<float> (x) * 1.7f
                                   + static_cast<float> (y) * 2.3f);
            const auto bright = n > 0.5f;
            const auto alpha = grainDose * std::abs (n - 0.5f) * 2.0f;
            image.setPixelAt (x, y, (bright ? grainInk : grainEdge)
                                        .withAlpha (juce::jlimit (0.0f, 1.0f, alpha)));
        }

    // Wear: big soft blotches from the shader's value-noise grid, drawn as
    // translucent ellipses over the grain.
    juce::Graphics imageGraphics (image);
    for (int gy = 0; gy < 5; ++gy)
        for (int gx = 0; gx < 9; ++gx)
        {
            const auto fi = static_cast<float> (gy * 9 + gx);
            const auto n = hashOf (fi * 4.19f + 2.17f);
            if (n < 0.55f)
                continue;

            const auto cx = (static_cast<float> (gx) + hashOf (fi * 1.61f)) / 9.0f;
            const auto cy = (static_cast<float> (gy) + hashOf (fi * 2.71f)) / 5.0f;
            const auto size = 30.0f + 40.0f * hashOf (fi * 3.37f);
            const auto alpha = wearDose * (n - 0.5f) * 2.0f;

            imageGraphics.setColour (grainEdge.withAlpha (
                juce::jlimit (0.0f, 1.0f, alpha)));
            imageGraphics.fillEllipse (cx * static_cast<float> (w) - size * 0.5f,
                                       cy * static_cast<float> (h) - size * 0.5f,
                                       size, size);
        }

    panelTextureImage = std::move (image);
}

// The three transport keys plus the momentary spindown button. Same rule as the tab
// bar: a plain TextButton's getToggleState() is always false, so the lit key's
// colours are written to BOTH the ...Id and the ...OnId pair. START is the one state
// the engine advances out of by itself - it becomes PLAY when the capstan arrives - so
// its lamp is the only one that goes out on its own, and the key that is lit is the
// state the machine is in.
int FirstAudioProcessorEditor::currentTransportState() const
{
    // Read through the parameter, not a local mirror, so an automation lane moving
    // the transport repaints the panel on the next frame. convertFrom0to1() gives
    // the choice index back as a 0..2 float for a three-entry AudioParameterChoice.
    if (auto* parameter = audioProcessor.parameters.getParameter ("transport"))
        return juce::jlimit (0, 2,
                             juce::roundToInt (parameter->convertFrom0to1 (parameter->getValue())));

    return 1;
}

void FirstAudioProcessorEditor::styleTransportButtons()
{
    const auto& palette = paletteFor (darkTheme);
    const auto state = lastShownTransport;

    const auto styleKey = [&palette] (juce::TextButton& button, bool isOn)
    {
        button.setColour (juce::TextButton::buttonColourId, isOn ? palette.accent : palette.raised);
        button.setColour (juce::TextButton::buttonOnColourId, isOn ? palette.accent : palette.raised);
        button.setColour (juce::TextButton::textColourOffId, isOn ? palette.readout : palette.text);
        button.setColour (juce::TextButton::textColourOnId, isOn ? palette.readout : palette.text);
        button.repaint();
    };

    // STOP is lit when the machine is at rest; PLAY when it is running; START while
    // the capstan is climbing - which is the one moment the other two are both
    // wrong: the machine is neither at rest nor up to speed yet.
    styleKey (transportStopButton, state == 0);
    styleKey (transportPlayButton, state == 1);
    styleKey (transportStartButton, state == 2);
}

//==============================================================================
//  Interface sounds: attaching the four voices to the panel's controls.
//
//  Every control that has a distinct physical feel gets the voice that matches
//  it, so the panel sounds like hardware rather than like a generic UI:
//
//    switches and toggles   a CLICK, because that is what a rocker does
//    the momentary keys     PRESS and RELEASE, because they are two events
//    every knob             a DETENT, but ONLY on the value change - not on
//                           every mouse move, which would be a machine-gun
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
    // A switch or a momentary key: on click. juce::Button::onClick covers both
    // the TextButtons and the ToggleButtons, so one lambda wires the whole panel.
    const auto attachClick = [this] (juce::Button& button, UiSoundEngine::Voice voice)
    {
        // The button's own onClick is left alone: this listener rides ALONGSIDE
        // whatever the button already does, so a control whose click changes the
        // machine still sounds right without its handler being rewritten.
        button.addMouseListener (this, false);
        juce::ignoreUnused (voice);
    };
    juce::ignoreUnused (attachClick);

    // The switches and keys, wired through onClick wrappers. Each lambda calls
    // the existing handler only if one is already set - capturing nothing but
    // `this`, so there is no ownership question and nothing to disconnect.
    const auto playClick = [this] { uiSounds.trigger (UiSoundEngine::Voice::click); };

    // juce::Button* named explicitly: the members mix ToggleButton and
    // TextButton, and an initializer_list cannot be deduced from a
    // heterogeneous braced list - the common base is what the loop wants.
    const std::array<juce::Button*, 15> stateButtons { { &bypassButton, &deltaButton, &polarityButton, &autoGainButton,
                          &modeCycleButton, &delaySyncButton,
                          &themeButton, &glButton, &savePresetButton, &deletePresetButton,
                          &copyAButton, &copyBButton, &compareButton,
                          &undoButton, &redoButton } };
    for (auto* button : stateButtons)
        button->onStateChange = [this, button, playClick, previous = button->onStateChange]
        {
            playClick();
            if (previous != nullptr)
                previous();
        };

    // The transport keys are TextButtons with an onClick rather than a toggle
    // state, so they take the press/release pair instead of the click - which is
    // exactly how a transport key on a deck behaves.
    transportStopButton.onStateChange = [this] { uiSounds.trigger (UiSoundEngine::Voice::press); };
    transportPlayButton.onStateChange = [this] { uiSounds.trigger (UiSoundEngine::Voice::press); };
    transportStartButton.onStateChange = [this] { uiSounds.trigger (UiSoundEngine::Voice::press); };

    // The tab buttons and the spindown key: the tab bar is a row of switches and
    // the spindown is a momentary hold, so each takes its own voice.
    for (auto& tab : tabButtons)
        tab.onStateChange = [this, previous = &tab] (void)
        {
            juce::ignoreUnused (previous);
            uiSounds.trigger (UiSoundEngine::Voice::click);
        };

    spindownButton.onStateChange = [this] { uiSounds.trigger (UiSoundEngine::Voice::press); };

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
            box->onChange = [this, playClick, previous]
            {
                if (previous != nullptr)
                    previous();
                playClick();
            };
        }
}

void FirstAudioProcessorEditor::refreshModeButtonCaption()
{
    // The caption names the live mode, read from the parameters rather than from
    // a button-local bool: a session load, an undo or a preset can all change the
    // modes behind the panel's back, and the caption must follow all of them.
    const auto modernOn = isModeOn ("modern_mode");
    const auto lofiOn   = isModeOn ("lofi_mode");
    modeCycleButton.setButtonText (modernOn ? "MODE: MODERN"
                                  : lofiOn  ? "MODE: LO-FI"
                                            : "MODE: OFF");

    // The button's background is drawn by the base Look and Feel - this class
    // overrides captions, not button backgrounds - so the state has to arrive as
    // colours: lit with the accent while a mode is engaged, the raised panel
    // colour at OFF, the same language the transport keys and the GL switch
    // speak. Without this the button kept the base Look and Feel's own grey,
    // which matched nothing else on the panel.
    const auto& palette = paletteFor (darkTheme);
    const auto modeOn = modernOn || lofiOn;
    modeCycleButton.setColour (juce::TextButton::buttonColourId,
                               modeOn ? palette.accent : palette.raised);
    modeCycleButton.setColour (juce::TextButton::buttonOnColourId,
                               modeOn ? palette.accent : palette.raised);
    modeCycleButton.setColour (juce::TextButton::textColourOffId,
                               modeOn ? palette.readout : palette.text);
    modeCycleButton.setColour (juce::TextButton::textColourOnId,
                               modeOn ? palette.readout : palette.text);
}

void FirstAudioProcessorEditor::styleSpindownButton()
{
    const auto& palette = paletteFor (darkTheme);
    const auto held = lastShownSpindown;

    // Held uses the needle colour rather than the accent: a spindown is a deliberate
    // fault (the platter is being starved of power), and the panel's one other red is
    // the meter needle, which is the same idea - something is being pushed.
    const auto heldColour = palette.needle;
    spindownButton.setColour (juce::TextButton::buttonColourId, held ? heldColour : palette.raised);
    spindownButton.setColour (juce::TextButton::buttonOnColourId, held ? heldColour : palette.raised);
    spindownButton.setColour (juce::TextButton::textColourOffId, held ? palette.readout : palette.secondary);
    spindownButton.setColour (juce::TextButton::textColourOnId, held ? palette.readout : palette.secondary);
    spindownButton.repaint();
}

void FirstAudioProcessorEditor::styleTabButtons()
{
    const auto& palette = paletteFor (darkTheme);

    for (int tab = 0; tab < numTabs; ++tab)
    {
        auto& button = tabButtons[static_cast<std::size_t> (tab)];
        const auto isOn = tab == currentTab;

        button.setColour (juce::TextButton::buttonColourId, isOn ? palette.accent : palette.raised);
        button.setColour (juce::TextButton::buttonOnColourId, isOn ? palette.accent : palette.raised);
        button.setColour (juce::TextButton::textColourOffId, isOn ? palette.readout : palette.secondary);
        button.setColour (juce::TextButton::textColourOnId, isOn ? palette.readout : palette.secondary);
        button.repaint();
    }

    // The family buttons style themselves like the tab buttons do - pressed when
    // their slice of the bar is on screen - so the two levels read as one system.
    // Called from styleTabButtons rather than from setTabFamily, so a THEME cycle
    // repaints them too.
    const std::array<juce::TextButton*, tabFamilyCount> familyButtons
        { &tabFamilyTapeButton, &tabFamilyFxButton, &tabFamilySetupButton };
    for (std::size_t f = 0; f < familyButtons.size(); ++f)
    {
        const auto isOn = static_cast<int> (f) == currentTabFamily;
        familyButtons[f]->setColour (juce::TextButton::buttonColourId,
                                     isOn ? palette.accent : palette.raised);
        familyButtons[f]->setColour (juce::TextButton::buttonOnColourId,
                                     isOn ? palette.accent : palette.raised);
        familyButtons[f]->setColour (juce::TextButton::textColourOffId,
                                     isOn ? palette.readout : palette.secondary);
        familyButtons[f]->setColour (juce::TextButton::textColourOnId,
                                     isOn ? palette.readout : palette.secondary);
        familyButtons[f]->repaint();
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
                                       "SHAPERS") == 0);

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

    // A page's own painted furniture has to leave with the page. The SHAPERS
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

//==============================================================================
//==============================================================================
//  Translation.
//
//  juce::LocalisedStrings is a lookup table and nothing more: hand it a string,
//  it gives back the translation if there is one and the string itself if there
//  is not. That last half is the whole design here.
//
//  The obvious way to use it is with short keys - "DRIVE_TIP" in a table, and
//  TRANS("DRIVE_TIP") at the call site - and that is what the JUCE examples
//  do. It does not suit THIS panel. There are eighty-seven parameter tooltips
//  here, each two or three sentences long, and keying them would mean inventing
//  eighty-seven key names, writing all eighty-seven English strings a second
//  time in a table beside the code, and keeping the two copies in step - with
//  the failure mode being a tooltip that reads "DRIVE_TIP" to the user, or one
//  that has quietly drifted from the text it was written from.
//
//  So the English IS the key. A tooltip is written once, where it is used, in
//  English, and a translation is a line in Source/Translations/uk.txt mapping
//  that exact sentence to its Ukrainian. Nothing to rename, nothing to keep in
//  step, and a tooltip that has not been translated yet shows its English rather
//  than a key - which is the correct thing to show, and invisible as a bug.
//
//  Keyed entries still work, and are used for the strings that have no natural
//  English sentence to key on. The format is JUCE's, unchanged:
//      "KEY" = "value";
//==============================================================================
// Named xlat rather than tr: this file already has a `tr (float, int)` that
// formats a number, and it is declared after one of its own uses - so a second
// `tr` at file scope leaves that use resolving to neither. Renaming is cheaper
// than untangling which one a given call meant.
static juce::String xlat (const juce::String& text)
{
    // Not LocalisedStrings::translate - that is an instance method, and the
    // object it would need is the one this call is meant to be looking up. The
    // static entry point is translateWithCurrentMappings, which reaches the
    // table setCurrentMappings installed and returns the key unchanged when
    // there is no mapping for it.
    return juce::LocalisedStrings::translateWithCurrentMappings (text);
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

    updateTooltips();
}

void FirstAudioProcessorEditor::setTabFamily (int newFamily)
{
    // The tab table's family column is the single source of truth: each page
    // states which family hosts it, and this strip reads the column rather
    // than a parallel count array that could drift from the table. The assert
    // pins the intended layout - nine TAPE pages, then four FX, then three
    // SETUP, in table order - so a page moved between families without the
    // order following is a build error, not a button that lights wrong.
    static_assert (tabSpecs[0].family == 0 && tabSpecs[1].family == 0
                       && tabSpecs[2].family == 0 && tabSpecs[3].family == 0
                       && tabSpecs[4].family == 0 && tabSpecs[5].family == 0
                       && tabSpecs[6].family == 0 && tabSpecs[7].family == 0
                       && tabSpecs[8].family == 0 && tabSpecs[9].family == 1
                       && tabSpecs[10].family == 1 && tabSpecs[11].family == 1
                       && tabSpecs[12].family == 1 && tabSpecs[13].family == 2
                       && tabSpecs[14].family == 2 && tabSpecs[15].family == 2,
                   "the tab table's family column must be nine TAPE pages, "
                   "then four FX, then three SETUP, in order");

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
    juce::PropertiesFile& folderMemory()
    {
        static juce::PropertiesFile settings ([] {
            juce::PropertiesFile::Options options;
            options.applicationName      = "Nonlin Analog Saturator";
            options.folderName           = "Nonlin Analog Saturator";
            options.filenameSuffix       = "settings";
            options.osxLibrarySubFolder  = "Application Support";
            options.commonToAllUsers     = false;
            options.ignoreCaseOfKeyNames = true;
            return options;
        }());
        return settings;
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
    // whole dialogue. `chooser` is captured by the lambda so the chooser stays
    // alive for the duration of the callback, which is what makes the launch
    // safe to fire and forget.
    //
    // "*.nam" is NeuralAmpModeler's own extension - its files carry the SAME
    // networks in a different wire format (one flat weight vector rather than
    // a stack of named layers), and the picker reads both through the
    // processor's own two entry points below.
    auto chooser = std::make_shared<juce::FileChooser> (
        "Load a neural model",
        rememberedFolder ("lastModelFolder"),
        "*.json;*.rtn;*.nam;*");

    // browserFlags, not `flags`: a local name that shadows nothing is cheaper
    // than proving no future member will ever be called flags (C4458).
    const auto browserFlags = juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync (browserFlags, [this, chooser] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (! file.existsAsFile())
            return;

        // A .nam file goes through its own converter; every other extension
        // keeps the RTNeural JSON path. Both load the same kind of network -
        // the wire format is the only thing that differs.
        const auto isNam = file.hasFileExtension (".nam");
        const auto text = file.loadFileAsString();
        const bool loaded = isNam ? audioProcessor.loadNamModel (text)
                                  : audioProcessor.loadNeuralModel (text);

        if (! loaded)
        {
            // A refused file leaves whatever was loaded before in place, and the
            // readout says WHY this one was refused - a .nam that needs the LSTM
            // architecture is a different download, not a corrupt file, and the
            // reason is the one thing the user can act on.
            neuralStatusReason = audioProcessor.lastLoadError();
            lastShownNeuralStatus = {};   // force the next refresh to repaint the label
            refreshNeuralStatus();
            return;
        }

        loadedNeuralName = file.getFileNameWithoutExtension();
        neuralStatusReason = {};
        rememberFolder ("lastModelFolder", file);
        lastShownNeuralStatus = {};   // force the next refresh to repaint the label
        openStageMix ("neural_mix");
        refreshNeuralStatus();
    });
}

void FirstAudioProcessorEditor::refreshNeuralStatus()
{
    // The processor is the source of truth, not the label: a model can be
    // cleared from either button, so the readout follows whatever the stage
    // actually holds. It reports in THREE states: the loaded file's name while
    // the stage runs, "NO MODEL" while it is inert, and the refused file's
    // reason after a load that failed - which outranks the stage's own state,
    // because the stage may well still hold the previous model. That third
    // state is what the old two-state readout got wrong: a refused .nam read as
    // "no model" even though the previous one was still playing.
    // A model is run at the rate it was TRAINED at whatever rate the engine is
    // running at (a net's memory is counted in samples, so the rate is part of
    // what it sounds like), and that rate is the one fact about this stage the
    // user cannot infer from the panel. It is named only when the two rates
    // actually differ, so a session that needs no conversion says nothing extra -
    // and a session that DOES need one can see that it is happening.
    const auto modelRate = audioProcessor.getNeuralModelRate();
    const auto engineRate = audioProcessor.getEngineSampleRate();
    const auto modelKilo = juce::roundToInt (modelRate / 1000.0);
    const auto rateTag = modelRate > 1.0 && std::abs (modelRate - engineRate) > 1.0
                             ? juce::String (" @ ") + juce::String (modelKilo) + "K"
                             : juce::String();

    const auto status = neuralStatusReason.isNotEmpty()
                            ? xlat ("INVALID MODEL") + " (" + neuralStatusReason + ")"
                            : audioProcessor.hasNeuralModel() && loadedNeuralName.isNotEmpty()
                                  ? loadedNeuralName + rateTag
                                  : xlat ("NO MODEL");

    if (status == lastShownNeuralStatus)
        return;

    lastShownNeuralStatus = status;
    // The file's name and the reason are DATA, not sentences, so they are not
    // looked up: the two state words around them are (see the note above xlat).
    neuralStatusLabel.setText (status, juce::dontSendNotification);
    neuralStatusLabel.setColour (juce::Label::textColourId,
                                 audioProcessor.hasNeuralModel()
                                     ? paletteFor (false).accent
                                     : paletteFor (false).secondary);
}

void FirstAudioProcessorEditor::loadIrFromFile()
{
    // Same asynchronous-chooser shape as the model picker: the chooser is kept
    // alive by its own shared_ptr for the length of the callback. The filter
    // names the two formats a DAW user actually meets; "*" keeps any other
    // file JUCE's registered decoders can open selectable.
    auto chooser = std::make_shared<juce::FileChooser> (
        "Load an impulse response",
        rememberedFolder ("lastIrFolder"),
        "*.wav;*.aif*;*");

    const auto browserFlags = juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync (browserFlags, [this, chooser] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (! file.existsAsFile())
            return;

        if (! audioProcessor.loadIrFile (file))
        {
            // A file that would not decode leaves the previous IR in place and
            // says WHICH failure it was, in the same shape the model picker uses.
            irStatusReason = audioProcessor.lastLoadError();
            lastShownIrStatus = {};      // force the next refresh to repaint
            refreshIrStatus();
            return;
        }

        loadedIrName = file.getFileNameWithoutExtension();
        irStatusReason = {};
        rememberFolder ("lastIrFolder", file);
        lastShownIrStatus = {};      // force the next refresh to repaint
        openStageMix ("ir_mix");
        refreshIrStatus();
    });
}

void FirstAudioProcessorEditor::refreshIrStatus()
{
    // The processor is the source of truth, exactly as with the model status:
    // the stage can be cleared from either button, so the readout follows
    // whatever the convolvers actually hold.
    const auto status = irStatusReason.isNotEmpty()
                            ? xlat ("INVALID IR") + " (" + irStatusReason + ")"
                            : audioProcessor.hasIr() && loadedIrName.isNotEmpty()
                                  ? loadedIrName
                                  : xlat ("NO IR");

    if (status == lastShownIrStatus)
        return;

    lastShownIrStatus = status;
    irStatusLabel.setText (status, juce::dontSendNotification);
    irStatusLabel.setColour (juce::Label::textColourId,
                             audioProcessor.hasIr()
                                 ? paletteFor (false).accent
                                 : paletteFor (false).secondary);
}

//==============================================================================
//  The knob table and its tooltip text are Source/GUI/PanelText.h: one row per
//  control and one sentence per parameter, which is DATA the panel is built
//  from rather than code that builds it.
//==============================================================================


FirstAudioProcessorEditor::FirstAudioProcessorEditor (FirstAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
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
    setUpModeAndGlSwitches();
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

void FirstAudioProcessorEditor::setUpWindowAndSize()
{
    setResizable (true, true);

    // The editor is deliberately sized in LOGICAL units and the DPI scale is NOT applied
    // here. JUCE already handles display scaling for plugin editors: component coordinates
    // are logical, and the host (or JUCE on the desktop) multiplies them by the display
    // scale when it maps them to physical pixels. Multiplying setSize() by the display
    // scale as well applies it a second time, which is why the window opened far too large
    // - on a 150 % display this asked for 1860 x 1230 physical pixels for a 1240 x 820
    // panel. The layout code below is all relative to getLocalBounds(), so it needs no
    // knowledge of DPI at all.
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

    const auto& displays = juce::Desktop::getInstance().getDisplays();
    juce::Rectangle<int> workArea;

    // The display under the mouse is the one the user is looking at, and the host will
    // almost certainly put the editor near it. The other two cases are the mouse sitting
    // outside every known display (a KVM switch, a locked session) and a headless build
    // with no displays at all - in both of them there is nothing sensible to clamp to, so
    // the preferred size is used unchanged.
    // The Point<float> overload: the Point<int> one is deprecated, and Clang says so on
    // every macOS build. Same screen, same result - only the coordinate type differs.
    if (const auto* display = displays.getDisplayForPoint (juce::Desktop::getMousePosition().toFloat()))
        workArea = display->userBounds.toNearestInt();
    else if (const auto* primary = displays.getPrimaryDisplay())
        workArea = primary->userBounds.toNearestInt();
    else
        workArea = displays.getTotalBounds (true);

    if (workArea.isEmpty())
    {
        setResizeLimits (minEditorWidth, minEditorHeight, 1500, 1180);
        setSize (preferredEditorWidth, preferredEditorHeight);
    }
    else
    {
        // toNearestInt() above already rounded, so subtracting the margin afterwards
        // cannot leave the window half a pixel - or a whole one - over the edge.
        const auto fittingWidth = workArea.getWidth() - 2 * screenMarginX;
        const auto fittingHeight = workArea.getHeight() - 2 * screenMarginY;

        // The floor this screen can actually honour, and the size it opens at. Both
        // read from the same two numbers, so the window cannot open at a size its
        // own limits would refuse - the version of this that clamped only the
        // opening size and left the floor alone was the bug, not a near miss of it.
        const auto floorWidth = juce::jmin (minEditorWidth, fittingWidth);
        const auto floorHeight = juce::jmin (minEditorHeight, fittingHeight);
        const auto openingWidth = juce::jlimit (floorWidth, fittingWidth, preferredEditorWidth);
        const auto openingHeight = juce::jlimit (floorHeight, fittingHeight, preferredEditorHeight);

        setResizeLimits (floorWidth, floorHeight, 1500, 1180);
        setSize (openingWidth, openingHeight);

        // Centred only when the screen forced a smaller window. At the preferred size the
        // host decides where the editor goes, and re-centring it here would fight the
        // DAW's own window management.
        if (openingWidth != preferredEditorWidth || openingHeight != preferredEditorHeight)
            centreWithSize (openingWidth, openingHeight);
    }
}

void FirstAudioProcessorEditor::createDebugInspector()
{
#if JUCE_DEBUG
    // The Melatonin component inspector, debug builds only. It is created after
    // the whole panel exists so it can walk the finished component tree; it
    // opens lazily and stays invisible unless asked for.
    inspector = std::make_unique<melatonin::Inspector> (*this, false);
#endif
}

void FirstAudioProcessorEditor::setUpLanguageAndTuning()
{
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
    languageBox.addItem (xlat ("LANGUAGE_ENGLISH"), 1);
    languageBox.addItem (xlat ("LANGUAGE_UKRAINIAN"), 2);
    languageBox.setLookAndFeel (&customLookAndFeel);
    languageLabel.setVisible (false);
    languageBox.setVisible (false);

    languageAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "language", languageBox);

    // The stored value arrives AFTER the constructor on a session load, so the
    // language is applied here from the parameter rather than assumed to be
    // English - otherwise a session saved in Ukrainian would open with
    // Ukrainian in the parameter and English on screen.
    if (auto* languageParameter = dynamic_cast<juce::AudioParameterChoice*> (
            audioProcessor.parameters.getParameter ("language")))
    {
        applyLanguage (languageParameter->getIndex());
    }
    else
    {
        applyLanguage (0);
    }

    languageBox.onChange = [this]
    {
        if (auto* languageParam = dynamic_cast<juce::AudioParameterChoice*> (
                audioProcessor.parameters.getParameter ("language")))
            applyLanguage (languageParam->getIndex());
    };

    // The caption is an all-caps engraving like every other caption on the panel,
    // while the keyed string carries ordinary words ("Language"); the case is
    // applied here rather than in the table, which keeps the lookup key stable
    // and the translation readable.
    styleLabel (languageLabel, xlat ("LANGUAGE_CAPTION").toUpperCase(), 10.0f,
                paletteFor (false).secondary, true, juce::Justification::left);
    addAndMakeVisible (languageLabel);
    addAndMakeVisible (languageBox);

    // SETUP musical controls: key selection is independent of the UI language,
    // while autotune remains opt-in and its amount is continuously automatable.
    tuningTonicBox.addItemList (juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 1);
    tuningModeBox.addItemList (juce::StringArray { "Chromatic", "Major", "Minor", "Dorian", "Pentatonic Major", "Pentatonic Minor" }, 1);
    tuningTonicBox.setLookAndFeel (&inlineLookAndFeel);
    tuningModeBox.setLookAndFeel (&inlineLookAndFeel);
    tuningTonicAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "tuning_tonic", tuningTonicBox);
    tuningModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "tuning_mode", tuningModeBox);
    autoTuneButton.setLookAndFeel (&inlineLookAndFeel);
    autoTuneAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (audioProcessor.parameters, "autotune", autoTuneButton);
    // A rotary, laid out by the knob strip like every other page's controls - see
    // the AUTOTUNE branch in resized(). As a LinearHorizontal bar this was the
    // one control on the panel the toolkit drew for itself (J37LookAndFeel
    // overrides the rotary, the switch and the toggle, and inherits JUCE's stock
    // slider art), and the band it rode was 30 px tall: the toolkit kept a 15 px
    // value box out of that and handed the art a paint area three pixels deep, so
    // the page's only knob was a two-pixel bead beside a stock grey track.
    autoTuneAmountSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    // 16 px for the readout, as on the deck's knobs - see the note there.
    autoTuneAmountSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 16);
    autoTuneAmountSlider.setRange (0.0, 1.0, 0.001);
    // The readout reads "50 %", the way the panel's other fraction knobs read (see
    // the transient shaper's two amounts, the same pair of functions): the
    // parameter IS a fraction and its unit IS percent, so the toolkit's own
    // 0.500-with-a-percent-sign was the wrong number WITH a percent sign after
    // it. The unit rides inside the text function rather than in
    // setTextValueSuffix, so it cannot be written twice.
    autoTuneAmountSlider.textFromValueFunction = [] (double value)
    {
        return juce::String (juce::roundToInt (value * 100.0)) + " %";
    };
    autoTuneAmountSlider.valueFromTextFunction = [] (const juce::String& text)
    {
        return juce::jlimit (0.0, 1.0, text.getDoubleValue() / 100.0);
    };
    autoTuneAmountSlider.setLookAndFeel (&customLookAndFeel);
    autoTuneAmountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
        (audioProcessor.parameters, "autotune_amount", autoTuneAmountSlider);
    styleLabel (tuningTonicLabel, "TONIC", 9.0f, paletteFor (false).secondary, true, juce::Justification::left);
    styleLabel (tuningModeLabel, "MODE", 9.0f, paletteFor (false).secondary, true, juce::Justification::left);
    // Centred, and click-through, because this caption is now a knob page's - the
    // strip places it in the tile's caption band like every other page's label,
    // and a left-aligned caption over a centred knob reads as a stray word.
    styleLabel (autoTuneAmountLabel, "TUNE AMOUNT", 9.0f, paletteFor (false).secondary, true, juce::Justification::centred);
    autoTuneAmountLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (tuningTonicLabel);
    addAndMakeVisible (tuningModeLabel);
    addAndMakeVisible (tuningTonicBox);
    addAndMakeVisible (tuningModeBox);
    addAndMakeVisible (autoTuneButton);
    addAndMakeVisible (autoTuneAmountLabel);
    addAndMakeVisible (autoTuneAmountSlider);

    // The four tuning controls are the one group on the panel that had no
    // tooltips at all, which is why the page read as unexplained: two of them
    // (TONIC, MODE) only mean something together, and the corrector's own two
    // are the pair a user has to understand before turning either on. Keyed on
    // the English sentence like every other tip, so a translation is one line in
    // the table.
    setTip (tuningTonicBox, "TONIC - the key the scale is built on: the note the "
                            "corrector treats as home when MODE is anything but "
                            "CHROMATIC. On CHROMATIC it changes nothing, because "
                            "every semitone is already in the scale. Default C.");
    setTip (tuningModeBox, "MODE - which notes the corrector is allowed to land on. "
                           "CHROMATIC is every semitone and is the default: a note is "
                           "pulled to the nearest one and nowhere else. MAJOR, MINOR, "
                           "DORIAN and the two pentatonics let a reading travel up to "
                           "a semitone to the nearest note the chosen scale contains, "
                           "so the correction follows the key rather than the grid. "
                           "Default Chromatic.");
    setTip (autoTuneButton, "AUTO TUNE - switches the corrector in. The stage reads "
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
    setTip (autoTuneAmountSlider, "TUNE AMOUNT - how far and how fast the correction is "
                                  "allowed to travel: half the amount is half the pull "
                                  "at about twice the time constant, which is the "
                                  "difference between a note being nudged and a note "
                                  "being snapped onto the grid. At full amount a note "
                                  "lands within a few milliseconds; at 0 the stage is "
                                  "out of the path entirely, detector included, and "
                                  "the signal is bit-for-bit the one that arrived.");
}

void FirstAudioProcessorEditor::setUpTooltipsAndPhysics()
{
    // Tooltips appear after a third of a second of hover: quick enough to be
    // discoverable, slow enough not to flash while the user sweeps the panel.
    tooltipWindow->setMillisecondsBeforeTipAppears (350);
    createDecorativePhysics();
}

void FirstAudioProcessorEditor::stylePanelLabels()
{
    styleLabel (brandLabel, "NONLIN ANALOG", 9.0f, paletteFor (false).accent, true, juce::Justification::left);
    titleLabel.setText ("Saturator", juce::dontSendNotification);

    // Serif display face with a cross-platform fallback chain. "Georgia" only exists on
    // Windows, so naming it alone made the title fall back to an arbitrary face (and an
    // unpredictable width) on macOS and Linux. FontOptions accepts a comma-separated
    // list and uses the first family that resolves, ending in a generic fallback.
    titleLabel.setFont (juce::Font (juce::FontOptions ("Georgia, Times New Roman, Times, serif",
                                                       30.0f, juce::Font::bold)));
    titleLabel.setJustificationType (juce::Justification::left);
    titleLabel.setInterceptsMouseClicks (false, false);
    styleLabel (subtitleLabel, "NONLINEAR ANALOG MACHINE  /  SATURATION", 10.0f,
                paletteFor (false).secondary, true, juce::Justification::left);
    styleLabel (statusLabel, "STEREO / REAL TIME", 9.0f,
                paletteFor (false).status, true, juce::Justification::centred);
    styleLabel (deckHeadingLabel, "Deck", 10.0f, paletteFor (false).accent,
                true, juce::Justification::left);

    // Build identity, printed on the deck's heading strip. CMake resolves
    // J37_BUILD_COMMIT from `git rev-parse --short=8 HEAD` at configure time and marks
    // a dirty tree with a trailing "+"; when the sources are built outside a git
    // checkout the definition still exists and reads "unknown", so this line can never
    // be empty and the two cases are always distinguishable at a glance.
#ifdef J37_BUILD_COMMIT
    const juce::String buildId (J37_BUILD_COMMIT);
#else
    const juce::String buildId ("unknown");
#endif
    styleLabel (buildLabel, "BUILD " + buildId, 8.0f, paletteFor (false).secondary,
                true, juce::Justification::centredRight);
    styleLabel (tapeTypeLabel, "TAPE", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    styleLabel (valveTypeLabel, "VALVE", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    styleLabel (ampTypeLabel, "AMP", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    styleLabel (transformerTypeLabel, "XFMR", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    styleLabel (digitalTypeLabel, "DIGITAL", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    styleLabel (vinylTypeLabel, "VINYL", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    // The second selector of the six-type row is the turntable's motor speed; it
    // had a layout slot and a combo but was never given a caption, so "33 RPM"
    // sat under an empty label. RPM is what the box itself shows (33 / 45 / 78).
    styleLabel (vinylSpeedLabel, "RPM", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    styleLabel (speedLabel, "SPEED", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    styleLabel (deckHintLabel, "PLAY / AT SPEED", 9.0f,
                paletteFor (false).secondary, false, juce::Justification::centredLeft);
    styleLabel (controlsHeadingLabel, "TABS", 10.0f, paletteFor (false).accent,
                true, juce::Justification::left);
    // The section caption. Its text is not set here: setCurrentTab writes the
    // active page's own subject into it on every page change, and the constructor
    // calls setCurrentTab before the first paint, so anything set now would be
    // overwritten a few lines later. It is styled here only.
    styleLabel (sectionCaptionLabel, "-", 9.0f, paletteFor (false).secondary,
                false, juce::Justification::left);
    styleLabel (controlsHintLabel, "Shift = fine tune", 9.0f,
                paletteFor (false).secondary, false, juce::Justification::right);
    styleLabel (bpmLabel, "BPM", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    styleLabel (bpmReadout, xlat ("-"), 9.0f, paletteFor (false).secondary,
                false, juce::Justification::centredLeft);
    styleLabel (metersHeadingLabel, "LEVELS", 10.0f, paletteFor (false).accent,
                true, juce::Justification::left);
    styleLabel (metersHintLabel, "dBFS / PEAK + RMS", 8.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    styleLabel (compressorLabel, "GLUE x2", 10.0f, paletteFor (false).accent,
                true, juce::Justification::right);
    styleLabel (compressorReadout, "one meter per stage", 8.0f, paletteFor (false).secondary,
                false, juce::Justification::right);
    // The three readout pairs share one alignment discipline: the caption is
    // centred over its value, and every pair uses the SAME scheme. BPM read
    // left while these three read right, so a caption sat over the right half
    // of its number while BPM's sat over nothing - the four looked staggered
    // against each other even though each pair was internally consistent.
    styleLabel (harmonicsLabel, "HARMONICS", 10.0f, paletteFor (false).accent,
                true, juce::Justification::centredLeft);
    styleLabel (harmonicsReadout, xlat ("even / odd"), 8.0f, paletteFor (false).secondary,
                false, juce::Justification::centredLeft);
    styleLabel (subfundLabel, "SUBFUND TRACK", 10.0f, paletteFor (false).accent,
                true, juce::Justification::centredLeft);
    styleLabel (subfundReadout, xlat ("idle"), 8.0f, paletteFor (false).secondary,
                false, juce::Justification::centredLeft);
    styleLabel (antiPhaseLabel, "ANTI-PHASE", 10.0f, paletteFor (false).accent,
                true, juce::Justification::centredLeft);
    styleLabel (antiPhaseReadout, xlat ("clean"), 8.0f, paletteFor (false).secondary,
                false, juce::Justification::centredLeft);
    addAndMakeVisible (antiPhaseLabel);
    addAndMakeVisible (antiPhaseReadout);

    addAndMakeVisible (brandLabel);
    addAndMakeVisible (titleLabel);
    addAndMakeVisible (subtitleLabel);
    addAndMakeVisible (statusLabel);
    addAndMakeVisible (deckHeadingLabel);
    addAndMakeVisible (buildLabel);
    addAndMakeVisible (tapeTypeLabel);
    addAndMakeVisible (valveTypeLabel);
    addAndMakeVisible (ampTypeLabel);
    addAndMakeVisible (transformerTypeLabel);
    addAndMakeVisible (digitalTypeLabel);
    addAndMakeVisible (vinylTypeLabel);
    addAndMakeVisible (vinylSpeedLabel);
    addAndMakeVisible (speedLabel);
    addAndMakeVisible (deckHintLabel);
    addAndMakeVisible (controlsHeadingLabel);
    addAndMakeVisible (sectionCaptionLabel);
    addAndMakeVisible (controlsHintLabel);
    // The MIX page's bar rides the member band, which resized() sizes and
    // positions; adding it visible here is what lets the first paint see it.
    addAndMakeVisible (mixBar);
    addAndMakeVisible (metersHeadingLabel);
    addAndMakeVisible (metersHintLabel);
    addAndMakeVisible (compressorLabel);
    addAndMakeVisible (compressorReadout);
    addAndMakeVisible (harmonicsLabel);
    addAndMakeVisible (harmonicsReadout);
    addAndMakeVisible (bpmLabel);
    addAndMakeVisible (bpmReadout);
    addAndMakeVisible (subfundLabel);
    addAndMakeVisible (subfundReadout);
}

void FirstAudioProcessorEditor::setUpControlStrip()
{
    // The three arrays the strip below works from, DERIVED from the table above
    // so they cannot disagree with it. Each is sized by controlCount, so a
    // table that is too short or too long is a compile error rather than a silent
    // out-of-bounds read.
    juce::StringArray controlIds;
    juce::StringArray controlNames;
    std::array<double, controlCount> defaultValues {};

    for (std::size_t i = 0; i < controlCount; ++i)
    {
        controlIds.add (controlSpecs[i].id);
        controlNames.add (controlSpecs[i].caption);
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
    for (std::size_t i = 0; i < controlCount; ++i)
    {
        const auto* declaredTab = controlSpecs[i].tab;
        const auto listedTab = [&] () -> const char*
        {
            for (const auto& spec : tabSpecs)
                for (std::size_t c = 0; c < spec.count; ++c)
                    if (spec.controls[c] == i)
                        return spec.name;

            return nullptr;
        }();

        jassert (listedTab != nullptr);
        jassert (listedTab != nullptr && std::strcmp (declaredTab, listedTab) == 0);

        // jassert compiles away in a release build, which would leave both
        // locals unused - and the project builds with -Wall -Werror-grade warning
        // flags. Naming them as deliberately-ignored keeps the check a
        // debug-only diagnostic without turning release into a warning.
        juce::ignoreUnused (declaredTab, listedTab);
    }

    for (std::size_t i = 0; i < controlCount; ++i)
    {
        auto& slider = controls[i];
        slider.setName (controlNames[static_cast<int> (i)]);
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 0.75f,
                                    juce::MathConstants<float>::pi * 2.25f, true);
        // The readout band under the knob is 16 px, not the 18 it used to be, for
        // the reason the knob's own rectangle is measured from: a knob draws in
        // the space the slider has left after its readout, so two pixels off every
        // cell's readout is two pixels onto every knob on every page - and on the
        // pages whose rows are short (MACHINE holds seven knobs in two rows) that
        // is the difference between a scale the ticks have room for and one they
        // do not. The readout itself is unaffected: the font it holds is smaller
        // than the band either way.
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
        slider.setTextBoxIsEditable (true);
        // Drag response tuned by ear: velocity-based mode throttled the first pixels
        // of every drag behind an acceleration ramp, which read as "slow knobs".
        // Direct movement plus a high sensitivity gives a full 0..1 sweep in about a
        // third of the panel width; Shift still engages JUCE's low-velocity mode for
        // fine control.
        slider.setMouseDragSensitivity (1400);
        slider.setVelocityBasedMode (true);
        slider.setVelocityModeParameters (1.0, 2, 0.06, true, juce::ModifierKeys::shiftModifier);
        slider.setPopupDisplayEnabled (true, true, this);
        slider.setScrollWheelEnabled (true);
        slider.setDoubleClickReturnValue (true, defaultValues[i]);
        slider.setLookAndFeel (&customLookAndFeel);
        slider.setColour (juce::Slider::textBoxOutlineColourId, paletteFor (false).border);

        // A SOURCE knob is registered with the LookAndFeel as that machine's own
        // control, so drawRotarySlider can paint its arc, its tracer and its
        // indicator in that machine's colour without knowing anything about the
        // control list. Every other knob is simply never registered and keeps
        // the panel's accent.
        const auto principle = principleOf (controlIds[static_cast<int> (i)]);
        if (principle >= 0)
            customLookAndFeel.registerPrincipleKnob (slider, principle);

        setTippedSentence (slider, parameterTooltipText (controlIds[static_cast<int> (i)]),
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
        const auto& id = controlIds[static_cast<int> (i)];

        if (id == "input" || id == "output")
        {
            // Input and Output are both calibrated decibel trims over the same range.
            slider.setRange (minStageDb, maxStageDb, 0.1);
            slider.setNumDecimalPlacesToDisplay (1);
            slider.setTextValueSuffix (" dB");
        }
        else if (id == "delay_time")
        {
            // DELAY: the head spacing, in milliseconds.
            slider.setRange (0.0, 250.0, 0.1);
            slider.setNumDecimalPlacesToDisplay (1);
            slider.setTextValueSuffix (" ms");
        }
        else if (id == "st_offset")
        {
            // ST OFFSET: the inter-channel time offset, in microseconds.
            slider.setRange (-500.0, 500.0, 1.0);
            slider.setNumDecimalPlacesToDisplay (0);
            slider.setTextValueSuffix (" us");
        }
        else if (id == "in_low" || id == "in_mid" || id == "in_high"
                 || id == "out_low" || id == "out_mid" || id == "out_high")
        {
            // The two equalisers' six bands: real dB, centred on 0, and with a
            // NEUTRAL default rather than a percentage one. They are deliberately
            // NOT given the percentage text functions below - an EQ reads in dB,
            // and a band showing "50 %" for its flat position would be a control
            // whose readout does not say what it is.
            slider.setRange (-12.0, 12.0, 0.1);
            slider.setNumDecimalPlacesToDisplay (1);
            slider.setTextValueSuffix (" dB");

            // A bipolar EQ wants its NEUTRAL position in the middle of the
            // travel, so a double-click returns to 0 rather than to one end.
            // The defaultValues table already holds 0.0 for these six; this is
            // the readout to match.
            slider.textFromValueFunction = [] (double value)
            {
                return juce::String (value, 1) + " dB";
            };
            slider.valueFromTextFunction = [] (const juce::String& text)
            {
                return juce::jlimit (-12.0, 12.0, text.getDoubleValue());
            };
        }
        else if (id == "transient_attack" || id == "transient_sustain")
        {
            // The transient shaper's two amounts are BIPOLAR and centred on 0:
            // negative softens/shortens, positive sharpens/lengthens, and 0 is
            // the neutral that leaves the stage transparent. The range is the
            // parameter's own (-1..1), shown as a signed percentage so the two
            // halves of the control read the same way they behave.
            slider.setRange (-1.0, 1.0, 0.001);
            slider.setNumDecimalPlacesToDisplay (0);
            slider.textFromValueFunction = [] (double value)
            {
                return juce::String (juce::roundToInt (value * 100.0)) + " %";
            };
            slider.valueFromTextFunction = [] (const juce::String& text)
            {
                return juce::jlimit (-1.0, 1.0, text.getDoubleValue() / 100.0);
            };
        }
        else if (id == "in_hp_freq" || id == "in_lp_freq"
                 || id == "out_hp_freq" || id == "out_lp_freq")
        {
            // The EQ filters' corners, in Hz. The two ends are the two REAL
            // bypasses: 20 Hz is below anything on a record, and 20 kHz is at or
            // above the top of the band - so the engine treats each end as "no
            // filter" rather than as a very gentle one, which is what makes a
            // neutral EQ bit-for-bit transparent.
            //
            // The high-pass is skewed toward its low end because that is where
            // the useful range is (a rumble filter lives between 20 and 120 Hz),
            // and the low-pass toward its middle for the same reason.
            if (id == "in_hp_freq" || id == "out_hp_freq")
            {
                slider.setSkewFactorFromMidPoint (100.0);
                slider.setRange (20.0, 500.0, 1.0);
            }
            else
            {
                slider.setSkewFactorFromMidPoint (8000.0);
                slider.setRange (2000.0, 20000.0, 10.0);
            }

            slider.setNumDecimalPlacesToDisplay (0);
            slider.setTextValueSuffix (" Hz");
        }
        else if (id == "in_eq_q" || id == "out_eq_q")
        {
            // The filters' corner resonance. Capped at 1.5: a resonant filter
            // ringing on a tape emulation is a fault rather than a feature, so
            // the control exists to let a steep filter be usable rather than to
            // make it sing.
            slider.setRange (0.5, 1.5, 0.01);
            slider.setNumDecimalPlacesToDisplay (2);
            slider.setTextValueSuffix (" Q");
        }
        else
        {
            slider.setRange (0.0, 1.0, 0.001);
            slider.textFromValueFunction = [] (double value)
            {
                return juce::String (juce::roundToInt (value * 100.0)) + " %";
            };
            slider.valueFromTextFunction = [] (const juce::String& text)
            {
                return juce::jlimit (0.0, 1.0, text.getDoubleValue() / 100.0);
            };
        }

        controlLabels[i].setText (controlNames[static_cast<int> (i)], juce::dontSendNotification);
        controlLabels[i].setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
        controlLabels[i].setJustificationType (juce::Justification::centred);
        controlLabels[i].setInterceptsMouseClicks (false, false);

        addAndMakeVisible (controlLabels[i]);
        addAndMakeVisible (slider);
        controlAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
            (audioProcessor.parameters, controlIds[static_cast<int> (i)], slider);
    }
}

void FirstAudioProcessorEditor::setUpTabButtons()
{
    // The three tabs the knob grid is split across. Only the active tab's knobs are on
    // screen: the other thirteen used to be laid out underneath it, invisible but still
    // sitting in the panel, and crowding the grid is what the captions were there to
    // hide. setCurrentTab() also does the first layout, now with the tab bar and a
    // single tab's grid. (resized() has already run once by now - setSize() above
    // triggers it - so this is an ordinary relayout, not a layout over unset members.)
    for (int tab = 0; tab < numTabs; ++tab)
    {
        const auto& spec = tabSpecs[static_cast<std::size_t> (tab)];
        auto& button = tabButtons[static_cast<std::size_t> (tab)];

        button.setButtonText (spec.name);
        setTip (button, spec.hint);
        button.setLookAndFeel (&customLookAndFeel);
        button.onClick = [this, tab] { setCurrentTab (tab); };
        addAndMakeVisible (button);
    }

    // The three tab families ride ONE row of the 4-wide knob grid: one button per
    // family, always visible, pages beside each. There is no free header row to
    // put them in (its three rows hold the switches, the language pair and the
    // machine-state pill), so the Control strip's own grid hosts them - the grid
    // below is empty while the row is visible and just reflows when it is not.
    const std::array<juce::TextButton*, tabFamilyCount> familyButtons
        { &tabFamilyTapeButton, &tabFamilyFxButton, &tabFamilySetupButton };
    for (std::size_t f = 0; f < familyButtons.size(); ++f)
    {
        auto& button = *familyButtons[f];
        button.setLookAndFeel (&customLookAndFeel);
        button.onClick = [this, f] { setTabFamily (static_cast<int> (f)); };
        addAndMakeVisible (button);
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
    const auto initialTab = juce::jlimit (0, numTabs - 1, currentTab);
    setTabFamily (tabSpecs[static_cast<std::size_t> (initialTab)].family);
}

void FirstAudioProcessorEditor::setUpDeckSelectors()
{
    // The same tapeStockNameList() the choice parameter is built from, so the panel
    // cannot offer a stock the parameter would refuse to select. It did once: the
    // panel drew twelve and the parameter accepted eight, so the last four were
    // drawn, implemented and unreachable. The default row is overwritten by the
    // attachment below, which syncs the box to the parameter.
    tapeTypeBox.addItemList (tapeStockNameList(), 1);
    valveTypeBox.addItemList (valveTypeNameList(), 1);
    ampTypeBox.addItemList (ampTypeNameList(), 1);
    transformerTypeBox.addItemList (transformerTypeNameList(), 1);
    digitalTypeBox.addItemList (digitalTypeNameList(), 1);
    vinylTypeBox.addItemList (vinylTypeNameList(), 1);
    vinylSpeedBox.addItemList (juce::StringArray { "33 RPM", "45 RPM", "78 RPM" }, 1);
    speedBox.addItemList (juce::StringArray { "7.5 ips", "15 ips", "30 ips" }, 1);
    tapeTypeBox.setTextWhenNothingSelected ("Select tape");
    speedBox.setTextWhenNothingSelected ("Select speed");
    setTip (tapeTypeBox, "Tape formula. Each stock bends the sound differently: "
                            "J37 is soft and classic, Ampex and Studer are hotter, "
                            "Chrome and Type 111 are cleaner, GP9 and 499 are dense "
                            "modern formulas, SM911 is the broadcast reference, "
                            "SM 468 is high-output low-noise, 888 is the hot thick "
                            "vintage stock, 815 is dark and dense, 811 is the clean "
                            "open mastering stock. The formula also shapes the glue "
                            "compressors' timing.");
    setTip (speedBox, "Transport speed. 7.5 ips is dark and loose, 15 ips is the "
                         "classic studio speed, 30 ips keeps the most top end and "
                         "the tightest glue. Speed also shapes the glue timing.");
    setTip (valveTypeBox, "VALVE TYPE - the voice of the VALVE principle. Only "
                             "audible in proportion to how much VALVE is in the "
                             "BLEND: a 12AX7 runs cold and tight, an EL34 or 6L6 "
                             "is fatter and more compressed, a 300B is the softest "
                             "and most even, a KT88 has the widest drift.");
    setTip (ampTypeBox, "AMP TYPE - the voice of the AMP principle. Only audible "
                           "in proportion to how much AMP is in the BLEND: the "
                           "Blackface is clean and firm, the Plexi bites, the AC30 "
                           "chimes, the Recto slams hardest in the second stage.");
    setTip (transformerTypeBox, "TRANSFORMER TYPE - the voice of the TRANSFORMER "
                                   "principle. Only audible in proportion to how much "
                                   "TRANSFORMER is in the BLEND: the types set how "
                                   "fast the core tracks and how early it starts to "
                                   "saturate - a nickel core bends earlier, steel "
                                   "later and harder.");
    setTip (digitalTypeBox, "DIGITAL TYPE - the voice of the DIGITAL principle. "
                               "Only audible in proportion to how much DIGITAL is in "
                               "the BLEND: the types shorten the word length and "
                               "deepen the sample-and-hold, from a 16-bit ceiling to "
                               "full bit-crush.");
    setTip (vinylTypeBox, "VINYL TYPE - the record itself, not the machine: how it "
                             "was pressed and how worn it is. Audible whenever VINYL "
                             "is up, and independent of the BLEND. Shellac 78 plays "
                             "with loud surface between every note, a dubplate is a "
                             "fresh loud lacquer, a half-speed master is nearly "
                             "silent between the grooves. The speed of the motor is "
                             "VINYL SPEED.");
    setTip (vinylSpeedBox, "VINYL SPEED - the speed of the turntable's motor, not "
                              "of the record: the VINYL TYPE describes the disc, this "
                              "describes what drives it. Each speed carries its own "
                              "wow rate and depth, so the same record wanders "
                              "differently at 33 and 45, and a 78's wind-up motor "
                              "wobbles hardest. 33 RPM is the default.");

    // The deck's readout pairs: the numbers change at 30 Hz, so a tooltip on
    // the VALUE would sit under a moving mouse on every frame. The caption
    // carries the explanation instead - it is static, and it is what a user
    // actually points at when reading a column.
    setTip (harmonicsLabel, "HARMONICS - the ratio the harmonic engine settled on: "
                               "E is the even-order share (tape and valve warmth), "
                               "O the odd-order share (transistor edge). The mix "
                               "follows the BLEND knob; this readout shows what the "
                               "blend actually produced.");
    setTip (subfundLabel, "SUBFUND TRACK - the pitch the subharmonic generator is "
                             "currently locked to. It only tracks while the machine "
                             "is fed a clean enough fundamental; 'idle - no note "
                             "tracked' means the input did not give it one.");
    setTip (antiPhaseLabel, "ANTI-PHASE - how much of the stereo pair cancels when "
                               "the channels are summed to mono. 'clean' is safe; a "
                               "percentage warns that a mono fold-down will lose "
                               "bass or body. It watches the OUTPUT side, so output "
                               "stage switches move it too.");
    setTip (bpmLabel, "BPM - the host tempo the delay and modulation sections sync "
                         "to. It follows your DAW's transport; it is a readout here, "
                         "not a setting.");
    setTip (tapeTypeLabel, "TAPE - the formula the machine's saturation is built "
                              "from. Each stock bends harmonics, compression and "
                              "noise differently. The selector below is where it is "
                              "chosen.");
    tapeTypeBox.setLookAndFeel (&customLookAndFeel);
    valveTypeBox.setLookAndFeel (&customLookAndFeel);
    ampTypeBox.setLookAndFeel (&customLookAndFeel);
    transformerTypeBox.setLookAndFeel (&customLookAndFeel);
    digitalTypeBox.setLookAndFeel (&customLookAndFeel);
    vinylTypeBox.setLookAndFeel (&customLookAndFeel);
    vinylSpeedBox.setLookAndFeel (&customLookAndFeel);
    speedBox.setLookAndFeel (&customLookAndFeel);
}

void FirstAudioProcessorEditor::setUpTopSwitches()
{
    bypassButton.setClickingTogglesState (true);
    bypassButton.setButtonText ("BYPASS");
    bypassButton.setLookAndFeel (&customLookAndFeel);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (audioProcessor.parameters, "bypass", bypassButton);

    deltaButton.setClickingTogglesState (true);
    deltaButton.setLookAndFeel (&customLookAndFeel);
    deltaAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (audioProcessor.parameters, "delta", deltaButton);
    addAndMakeVisible (deltaButton);

    themeButton.setButtonText ("DARK THEME");
    setTip (themeButton, "Cycle the front panel: IVORY is the paper-and-brass "
                            "studio panel, CHARCOAL the old console, and METAL a "
                            "rack unit - a cold neutral grey with a cool cyan "
                            "indicator. The button names the panel you are on; "
                            "pressing it moves to the next one.");
    themeButton.setLookAndFeel (&customLookAndFeel);
    themeButton.onClick = [this]
    {
        // The theme button now CYCLES through all three panels rather than
        // toggling two, because there are three. The order is the order they
        // read in: the paper panel, the old console, the rack unit - and it
        // wraps, so there is no "stuck" state and no second button to find.
        switch (themeChoice)
        {
            case j37panel::ThemeChoice::ivory:
                themeChoice = j37panel::ThemeChoice::charcoal;
                break;
            case j37panel::ThemeChoice::charcoal:
                themeChoice = j37panel::ThemeChoice::metal;
                break;
            case j37panel::ThemeChoice::metal:
            default:
                themeChoice = j37panel::ThemeChoice::ivory;
                break;
        }
        applyTheme();
    };

    // ---------------------------------------------------------------
    //  Output-stage switches: polarity invert and auto gain.
    // ---------------------------------------------------------------
    setTip (bypassButton, "Hard bypass: the tape engine and both glue compressors are "
                             "switched out. The switch is ramped, so toggling it never clicks.");
    setTip (deltaButton, "DELTA listen: the output becomes the finished signal minus the "
                            "machine's own dry signal, so you hear ONLY what the machine "
                            "adds - harmonics, glue, transport wander. MIX keeps its meaning; "
                            "digital silence means the machine is being transparent. The "
                            "switch is ramped, and BYPASS fades the difference out rather "
                            "than snapping back to the dry signal. Turn it off before you "
                            "print.");
    polarityButton.setClickingTogglesState (true);
    setTip (polarityButton, "Inverts the output polarity (180-degree phase flip). "
                               "Use it to correct an inverted source or to align two "
                               "machines feeding the same bus.");
    polarityButton.setLookAndFeel (&customLookAndFeel);
    polarityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (audioProcessor.parameters, "polarity", polarityButton);
    addAndMakeVisible (polarityButton);

    autoGainButton.setClickingTogglesState (true);
    setTip (autoGainButton, "Auto gain lets the slow programme compensator restore "
                               "the level the INPUT trim dialled in. Switch it off to "
                               "keep the output exactly at the level the chain produced.");
    autoGainButton.setLookAndFeel (&customLookAndFeel);
    autoGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (audioProcessor.parameters, "auto_gain", autoGainButton);
    addAndMakeVisible (autoGainButton);
}

void FirstAudioProcessorEditor::setUpOversamplingAndInstrument()
{
    // ---------------------------------------------------------------
    //  Oversampling switch on the panel. The parameter has always been
    //  host-visible; without a control it could only be reached from the
    //  DAW's own parameter list, which is not how a premium plugin works.
    // ---------------------------------------------------------------
    styleLabel (oversamplingLabel, "OVER", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    addAndMakeVisible (oversamplingLabel);
    oversamplingBox.addItemList (juce::StringArray { "Off", "2x", "4x", "8x" }, 1);
    setTip (oversamplingBox, "Internal rate of the tape engine. 2x and 4x reduce the "
                                "aliasing of the magnetic shaper; the added filter delay "
                                "is reported to the host, so DAW PDC compensates.");
    oversamplingBox.setLookAndFeel (&customLookAndFeel);
    oversamplingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "oversampling", oversamplingBox);
    addAndMakeVisible (oversamplingBox);

    // ---------------------------------------------------------------
    //  Instrument voicing selector, on the deck's second row.
    // ---------------------------------------------------------------
    styleLabel (instrumentLabel, "INSTRUMENT", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    addAndMakeVisible (instrumentLabel);
    instrumentBox.addItemList (juce::StringArray { "Master Bus", "Vocal", "Bass",
                                                   "Guitar", "Piano", "Drums" }, 1);
    setTip (instrumentBox, "Re-voices the machine for what is being recorded: how hard "
                              "the tape bends, how much top end survives, how loud the "
                              "floor sits and how steady the transport runs. Master Bus "
                              "is the neutral calibration. Drums is the slam "
                              "calibration - harder bend, open head, tight memory.");
    instrumentBox.setLookAndFeel (&customLookAndFeel);
    instrumentAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "instrument", instrumentBox);
    addAndMakeVisible (instrumentBox);
}

void FirstAudioProcessorEditor::setUpVinylSelectors()
{
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
    const auto setUpVinylSelector = [this] (juce::Label& label, juce::ComboBox& box,
                                            const juce::String& caption,
                                            const juce::StringArray& items,
                                            const juce::String& tip,
                                            const juce::String& parameterID)
    {
        styleLabel (label, caption, 9.0f, paletteFor (false).secondary,
                    true, juce::Justification::left);
        addAndMakeVisible (label);
        box.addItemList (items, 1);
        setTip (box, tip);
        // The flat field is painted by the inline Look and Feel, but the VALUE
        // is painted by juce::ComboBox's internal Label reading
        // ComboBox::textColourId - unstyled, that stayed the base LookAndFeel's
        // black on the dark panels, which is what made these lists unreadable.
        // applyTheme() re-inks it on every theme switch; this call is what makes
        // the very first paint legible.
        box.setColour (juce::ComboBox::textColourId, paletteFor (false).text);
        box.setLookAndFeel (&inlineLookAndFeel);
        addAndMakeVisible (box);
        juce::ignoreUnused (parameterID);
    };

    setUpVinylSelector (vinylGenerationLabel, vinylGenerationBox, "GENERATION",
                        juce::StringArray { "Laquer", "Direct", "Printed" },
                        "How the record was cut and pressed. LAQUER is the reference "
                        "cut - almost none of the cutter head's own colour, loud and "
                        "clean. DIRECT is a direct-metal master, cleaner still, with "
                        "the most top end and the quietest surface of the three. "
                        "PRINTED is the pressing you actually buy: a copy of a copy, "
                        "so the top end is duller, the surface noisier and the bass a "
                        "little fuller. It also scales this stage's own noise, so a "
                        "printed record is not merely darker - it is dirtier.",
                        "vinyl_generation");

    setUpVinylSelector (vinylTurntableLabel, vinylTurntableBox, "TURNTABLE",
                        juce::StringArray { "Belt", "Direct", "Idler" },
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

    setUpVinylSelector (vinylCartridgeLabel, vinylCartridgeBox, "CARTRIDGE",
                        juce::StringArray { "MM", "MC", "DJ" },
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

    vinylGenerationAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "vinyl_generation", vinylGenerationBox);
    vinylTurntableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "vinyl_turntable", vinylTurntableBox);
    vinylCartridgeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "vinyl_cartridge", vinylCartridgeBox);
}

void FirstAudioProcessorEditor::setUpTransportRow()
{
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
    styleLabel (transportLabel, "TRANSPORT", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    addAndMakeVisible (transportLabel);        setTip (transportStopButton, "STOP - brakes the capstan to rest and leaves it "
                                    "there. A machine that is not moving passes nothing, so "
                                    "the output goes silent: no tape, no dry input, no hiss, "
                                    "no delay tail. Pressing it again spins the machine "
                                    "back up to PLAY.");
    setTip (transportPlayButton, "PLAY - normal running. This is where the machine rests, "
                                     "and where START settles by itself.");
    setTip (transportStartButton, "START - the engagement gesture: the capstan comes up "
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

    for (auto* button : { &transportStopButton, &transportPlayButton, &transportStartButton })
    {
        button->setLookAndFeel (&customLookAndFeel);
        addAndMakeVisible (*button);
    }

    transportStopButton.onClick = [this]
    {
        // Toggle on the same key: STOP stops, and a second press starts. That is
        // the "Start is Stop and vice versa" behaviour, and it keeps the key
        // useful whichever state the machine is in.
        const auto state = currentTransportState();
        if (state == 0)
            audioProcessor.setTransportState (2);   // stopped -> spin up (START)
        else
            audioProcessor.setTransportState (0);   // running -> stop
    };
    transportPlayButton.onClick = [this] { audioProcessor.setTransportState (1); };
    transportStartButton.onClick = [this] { audioProcessor.setTransportState (2); };

    // SPINDOWN: a momentary hold, so its hold edges come from the button's own
    // mouseDown/mouseUp rather than onClick. setSpindownHeld() writes both the
    // engine atomic and the host-visible parameter, so an automation lane and a
    // finger on this button are the same event.
    styleLabel (spindownLabel, "SPINDOWN", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    addAndMakeVisible (spindownLabel);
    spindownButton.setLookAndFeel (&customLookAndFeel);
    setTip (spindownButton, "Hold to cut the platter's power: the machine winds down "
                               "over about a second and a half, and comes back up when you "
                               "release it. A hold also brings the platter up if the "
                               "machine was stopped, so the gesture always has something "
                               "to act on.");
    spindownButton.onHoldChanged = [this] (bool held)
    {
        audioProcessor.setSpindownHeld (held);
        lastShownSpindown = held;
        styleSpindownButton();
    };
    addAndMakeVisible (spindownButton);
}

void FirstAudioProcessorEditor::setUpDelayRow()
{
    // ---------------------------------------------------------------
    //  DELAY TYPE - which machine the second head behaves as.
    //
    //  Three discrete machines rather than a scale: TAPE loses top end on every
    //  pass because the repeat is re-recorded, BBD is darker still with clock
    //  noise and a bandwidth that narrows as the delay lengthens, and MODERN is
    //  clean and full-bandwidth. A combo, because there is no meaningful point
    //  between them.
    // ---------------------------------------------------------------
    styleLabel (delayTypeLabel, "DLY TYPE", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    addAndMakeVisible (delayTypeLabel);
    addAndMakeVisible (delaySyncLabel);
    delayTypeBox.addItemList (juce::StringArray { "Tape", "BBD", "Modern" }, 1);
    setTip (delayTypeBox, "What the second head's repeats sound like. TAPE loses "
                             "top end on every pass, because the repeat is recorded "
                             "onto the tape and played back through the same losses "
                             "the main path has. BBD is a bucket-brigade chip: darker "
                             "still, with clock noise on the repeats and a bandwidth "
                             "that narrows as the delay lengthens - which is what the "
                             "technology physically does. MODERN is a clean digital "
                             "delay with full bandwidth.");
    delayTypeBox.setLookAndFeel (&customLookAndFeel);
    delayTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "delay_type", delayTypeBox);
    addAndMakeVisible (delayTypeBox);

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
    delaySyncButton.setClickingTogglesState (true);
    setTip (delaySyncButton, "SYNC - lock the delay to the host's tempo instead of "
                                "a time in milliseconds. With SYNC on, RATE picks a "
                                "note value and the repeat lands on the beat whatever "
                                "the session is at, following a tempo change without "
                                "the user touching anything. The tempo is read from "
                                "the host's playhead every block. With SYNC off, "
                                "DELAY is a free time in milliseconds, which is what a "
                                "real head spacing gives.");
    delaySyncButton.setLookAndFeel (&customLookAndFeel);
    delaySyncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (audioProcessor.parameters, "delay_sync", delaySyncButton);
    addAndMakeVisible (delaySyncButton);

    styleLabel (delayRateLabel, "RATE", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    styleLabel (delaySyncLabel, "SYNC DELAY", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    // The GL pill carries its own caption ("GL ON" / "GL OFF"), so its LABEL is
    // never drawn - the caption lives in placeDeckSwitch's SETTINGS branch, which
    // setText()s it when that tab is laid out. The constructor keeps the text off:
    // an empty label paints nothing, so the first frame (and every frame until the
    // SETTINGS tab is opened) shows no stray "GL" floating in the deck block. The
    // label still exists as the caption slot for placeDeckSwitch to fill.
    styleLabel (glLabel, "", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    addAndMakeVisible (delayRateLabel);
    delayRateBox.addItemList (juce::StringArray { "1/1", "1/2", "1/4", "1/8", "1/16",
                                                  "1/4 T", "1/8 T", "1/4 D" }, 1);
    setTip (delayRateBox, "RATE - the note value the delay is locked to when SYNC "
                             "is on. 1/4 is a quarter note, 1/4 T is a quarter-note "
                             "triplet (a third of a beat faster) and 1/4 D is dotted "
                             "(half again as long) - the one that gives the classic "
                             "off-beat repeat. At 120 BPM a 1/4 is 500 ms and a 1/4 D "
                             "is 750 ms.");
    delayRateBox.setLookAndFeel (&customLookAndFeel);
    delayRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "delay_rate", delayRateBox);
    addAndMakeVisible (delayRateBox);
}

void FirstAudioProcessorEditor::setUpModeAndGlSwitches()
{
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
    setTip (modeCycleButton, "The whole-machine mode, cycling OFF - LO-FI - "
                                "MODERN. LO-FI is the deliberate degradation: a hard "
                                "3.2 kHz bandwidth limit and sample-and-hold "
                                "quantisation on the finished sample. MODERN "
                                "re-voices the machine for a well-maintained 1990s "
                                "deck: head losses out of the audio band, a lower "
                                "noise floor, thinner magnetic memory. They are "
                                "mutually exclusive by design, so one button holds "
                                "the state.");
    modeCycleButton.setLookAndFeel (&customLookAndFeel);
    modeCycleButton.onClick = [this]
    {
        const auto modernOn = isModeOn ("modern_mode");
        const auto lofiOn   = isModeOn ("lofi_mode");

        auto nextModern = false;
        auto nextLofi = false;
        if (! modernOn && ! lofiOn)      { nextLofi = true; }        // OFF -> LO-FI
        else if (lofiOn)                 { nextModern = true; }      // LO-FI -> MODERN
        /* MODERN -> OFF */                                          // stays both false

        audioProcessor.parameters.getParameterAsValue ("modern_mode").setValue (nextModern);
        audioProcessor.parameters.getParameterAsValue ("lofi_mode").setValue (nextLofi);
        refreshModeButtonCaption();
    };
    addAndMakeVisible (modeCycleButton);
    refreshModeButtonCaption();

    // GL switch: the context is a best-effort accelerator (see the attach note
    // above), and this button hands the choice to the user - drivers, remote
    // sessions and VMs differ, and the software path draws the same panel.
    setTip (glButton, "OpenGL - GPU-accelerated rendering of the panel. Turn off if "
                         "your driver or remote session misbehaves; the software path "
                         "draws the identical panel.");
    glButton.setLookAndFeel (&customLookAndFeel);
    addAndMakeVisible (glButton);
    addAndMakeVisible (glLabel);
}

void FirstAudioProcessorEditor::setUpSettingsSwitches()
{
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
    uiSoundsButton.setClickingTogglesState (true);
    setTip (uiSoundsButton, "UI SOUNDS - the panel's own interface clicks: switches "
                               "click, keys thump, knobs tick as they cross a step. They "
                               "are synthesised and play on a SEPARATE audio device, so "
                               "they never reach the plugin's output, the DAW's meters or "
                               "a render. Off by default; a studio at 3 a.m. does not "
                               "want its interface ticking.");
    uiSoundsButton.setLookAndFeel (&inlineLookAndFeel);
    uiSoundsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (audioProcessor.parameters, "ui_sounds", uiSoundsButton);
    addAndMakeVisible (uiSoundsButton);

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
    antiPhaseGuardButton.setClickingTogglesState (true);
    setTip (antiPhaseGuardButton, "ANTI-PHASE GUARD - the output protection that "
                                     "rotates a stereo pair whose sides are in "
                                     "opposite polarity back toward mono, so a "
                                     "mono fold-down (a club PA, a phone, a mono "
                                     "mastering check) still has a centre and a "
                                     "low end. Leave it on unless the reversal is "
                                     "deliberate: off, the pair passes through "
                                     "exactly as it arrives.");
    antiPhaseGuardButton.setLookAndFeel (&inlineLookAndFeel);
    antiPhaseGuardAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (audioProcessor.parameters, "anti_phase_guard", antiPhaseGuardButton);
    addAndMakeVisible (antiPhaseGuardButton);
    styleLabel (antiPhaseGuardLabel, "ANTI-PHASE", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    addAndMakeVisible (antiPhaseGuardLabel);
}

void FirstAudioProcessorEditor::setUpModelButtons()
{
    // ---------------------------------------------------------------
    //  The neural model picker, on the SHAPERS tab beside the NEURAL knob.
    //  It has no parameter and no attachment: a model is data read from a file,
    //  not a value a session or a preset carries, so the two buttons drive the
    //  processor directly and the status label reports what the stage holds.
    // ---------------------------------------------------------------
    loadNeuralButton.setLookAndFeel (&inlineLookAndFeel);
    clearNeuralButton.setLookAndFeel (&inlineLookAndFeel);
    // "Currently black" (the user's screenshot of the model picker): these two
    // are TextButtons whose colours were never set, so the inline Look and
    // Feel's unstyled defaults ran - a near-black slab on every theme. They get
    // the SAME raised-face pill the workflow buttons carry, re-inked per theme
    // in applyTheme().
    loadNeuralButton.setColour (juce::TextButton::buttonColourId, paletteFor (false).raised);
    loadNeuralButton.setColour (juce::TextButton::textColourOffId, paletteFor (false).text);
    loadNeuralButton.setColour (juce::TextButton::buttonOnColourId, paletteFor (false).raised.darker (0.15f));
    clearNeuralButton.setColour (juce::TextButton::buttonColourId, paletteFor (false).raised);
    clearNeuralButton.setColour (juce::TextButton::textColourOffId, paletteFor (false).text);
    clearNeuralButton.setColour (juce::TextButton::buttonOnColourId, paletteFor (false).raised.darker (0.15f));
    setTip (loadNeuralButton, "LOAD MODEL - reads a neural model and installs it as "
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
    setTip (clearNeuralButton, "CLEAR - releases the loaded model, returning the NEURAL "
                                  "stage to its transparent pass-through.");
    loadNeuralButton.onClick = [this] { loadNeuralModelFromFile(); };
    clearNeuralButton.onClick = [this]
    {
        audioProcessor.clearNeuralModel();
        loadedNeuralName = {};
        // A refusal belongs to the file that was refused, so clearing the stage
        // clears the reason with it: "NO MODEL" is the truth again afterwards.
        neuralStatusReason = {};
        refreshNeuralStatus();
    };
    addAndMakeVisible (loadNeuralButton);
    addAndMakeVisible (clearNeuralButton);

    styleLabel (neuralStatusLabel, "NO MODEL", 8.5f, paletteFor (false).secondary,
                false, juce::Justification::centred);
    addAndMakeVisible (neuralStatusLabel);

    // ---------------------------------------------------------------
    //  The IR picker rides the SHAPERS page's member band, under the IR MIX
    //  knob it belongs to: LOAD IR / CLEAR IR / status, styled and tipped
    //  exactly like the model picker beside it. An IR is data read from a
    //  file for the same reason a model is - no parameter, no attachment.
    // ---------------------------------------------------------------
    loadIrButton.setLookAndFeel (&inlineLookAndFeel);
    clearIrButton.setLookAndFeel (&inlineLookAndFeel);
    loadIrButton.setColour (juce::TextButton::buttonColourId, paletteFor (false).raised);
    loadIrButton.setColour (juce::TextButton::textColourOffId, paletteFor (false).text);
    loadIrButton.setColour (juce::TextButton::buttonOnColourId, paletteFor (false).raised.darker (0.15f));
    clearIrButton.setColour (juce::TextButton::buttonColourId, paletteFor (false).raised);
    clearIrButton.setColour (juce::TextButton::textColourOffId, paletteFor (false).text);
    clearIrButton.setColour (juce::TextButton::buttonOnColourId, paletteFor (false).raised.darker (0.15f));
    setTip (loadIrButton, "LOAD IR - reads an impulse response (a WAV or AIFF "
                             "capture of a cabinet, a room or any other space) and "
                             "convolves the finished signal with it. IR MIX sets how "
                             "much of the convolved signal is blended in, and a load "
                             "that succeeds opens IR MIX to full while it is still "
                             "closed, so the capture is heard at once. The picker "
                             "reopens in the folder the last impulse response came "
                             "from. Loading an IR does not change any other parameter "
                             "and is not saved with the session.");
    setTip (clearIrButton, "CLEAR IR - releases the loaded impulse response, "
                              "returning the IR stage to its transparent "
                              "pass-through.");
    loadIrButton.onClick = [this] { loadIrFromFile(); };
    clearIrButton.onClick = [this]
    {
        audioProcessor.clearIr();
        loadedIrName = {};
        irStatusReason = {};
        refreshIrStatus();
    };
    addAndMakeVisible (loadIrButton);
    addAndMakeVisible (clearIrButton);

    styleLabel (irStatusLabel, "NO IR", 8.5f, paletteFor (false).secondary,
                false, juce::Justification::centred);
    addAndMakeVisible (irStatusLabel);
}

void FirstAudioProcessorEditor::setUpInlineLists()
{
    // ---------------------------------------------------------------
    //  The remaining choice lists: the DI pad, the track layout and the two
    //  EQ orders. All four are lists rather than knobs because their values are
    //  discrete settings, not points on a scale - and all four use the IN-TAB
    //  look and feel, because that is what a list inside a tab is drawn with.
    // ---------------------------------------------------------------
    const auto setUpInlineList = [this] (juce::Label& label, juce::ComboBox& box,
                                         const juce::String& caption,
                                         const juce::StringArray& items,
                                         const juce::String& tip)
    {
        styleLabel (label, caption, 9.0f, paletteFor (false).secondary,
                    true, juce::Justification::left);
        addAndMakeVisible (label);
        box.addItemList (items, 1);
        setTip (box, tip);
        // The flat field is painted by the inline Look and Feel, but the VALUE
        // is painted by juce::ComboBox's internal Label reading
        // ComboBox::textColourId - unstyled, that stayed the base LookAndFeel's
        // black on the dark panels, which is what made these lists unreadable.
        // applyTheme() re-inks it on every theme switch; this call is what makes
        // the very first paint legible.
        box.setColour (juce::ComboBox::textColourId, paletteFor (false).text);
        box.setLookAndFeel (&inlineLookAndFeel);
        addAndMakeVisible (box);
    };

    setUpInlineList (diPadLabel, diPadBox, "DI PAD",
                     juce::StringArray { "0 dB", "-10 dB", "-20 dB", "-30 dB" },
                     "The DI box's input pad, applied BEFORE its transformer. A pad "
                     "is a switch rather than a level trim because that is the "
                     "decision it makes: a hot source (an active synth, a boosted "
                     "pedal) can be plugged in without driving the box's own core "
                     "into saturation. 0 dB is no pad.");

    setUpInlineList (tracksLabel, tracksBox, "TRACKS",
                     juce::StringArray { "2", "2+3", "3" },
                     "The machine's track layout, which is geometry rather than "
                     "taste. 2 is a stereo deck: two tracks each with half the "
                     "tape, the most low end per channel and no adjacent track to "
                     "leak from. 2+3 is a four-track deck used as two on tracks 2 "
                     "and 3: a whole track's worth of tape between the channels, "
                     "so the widest spacing and the least crosstalk. 3 is a "
                     "three-track deck: narrower tracks, so less low end and a "
                     "higher noise floor for the same tape, with the most bleed "
                     "between the heads.");

    const juce::StringArray eqOrderItems { "6 dB/oct", "12 dB/oct", "18 dB/oct",
                                           "24 dB/oct", "36 dB/oct", "48 dB/oct" };
    setUpInlineList (inputEqOrderLabel, inputEqOrderBox, "ORDER", eqOrderItems,
                     "The slope of the input EQ's high-pass and low-pass, in dB "
                     "per octave. 6 is one pole, 12 is two, up to 48 for a filter "
                     "that gets out of the way completely. The number on the panel "
                     "is the slope you get: the engine stacks one-pole sections, "
                     "one per 6 dB.");

    setUpInlineList (outputEqOrderLabel, outputEqOrderBox, "ORDER", eqOrderItems,
                     "The slope of the output EQ's high-pass and low-pass, in dB "
                     "per octave. Separate from the input EQ's because the two "
                     "equalisers make different decisions: the input one shapes "
                     "what the machine hears, the output one corrects what it "
                     "produced.");

    const std::array<const char*, 6> compressorIds { "comp_threshold", "comp_ratio", "comp_attack", "comp_release", "comp_makeup", "comp_mix" };
    const std::array<const char*, 6> compressorNames { "THRESH", "RATIO", "ATTACK", "RELEASE", "MAKEUP", "MIX" };
    for (std::size_t i = 0; i < compressorControls.size(); ++i)
    {
        auto& slider = compressorControls[i];
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        // 16 px for the readout, as on the deck's knobs - see the note there.
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 16);
        if (i == 0) slider.setRange (-60.0, 0.0, 0.1);
        else if (i == 1) slider.setRange (1.0, 20.0, 0.1);
        else if (i == 2) slider.setRange (0.1, 100.0, 0.1);
        else if (i == 3) slider.setRange (10.0, 1000.0, 1.0);
        else if (i == 4) slider.setRange (0.0, 24.0, 0.1);
        else slider.setRange (0.0, 1.0, 0.001);
        slider.setLookAndFeel (&customLookAndFeel);
        slider.setName (compressorNames[i]);
        compressorControlLabels[i].setText (compressorNames[i], juce::dontSendNotification);
        // Same caption as every other page's knobs - the COMP page's strip is the
        // common one now, so its labels must match it rather than the meters' type.
        compressorControlLabels[i].setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
        compressorControlLabels[i].setJustificationType (juce::Justification::centred);
        compressorControlLabels[i].setInterceptsMouseClicks (false, false);
        addAndMakeVisible (compressorControls[i]);
        addAndMakeVisible (compressorControlLabels[i]);
        compressorAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
            (audioProcessor.parameters, compressorIds[i], compressorControls[i]);
    }

    diPadAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "di_pad", diPadBox);
    tracksAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "tracks", tracksBox);
    inputEqOrderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "in_eq_order", inputEqOrderBox);
    outputEqOrderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "out_eq_order", outputEqOrderBox);

    //  The four corner switches. The corner knobs already bypass themselves at
    //  the ends of their travel, but that is a fallback rather than a control:
    //  a user who wants the equaliser's high-pass gone has to find the knob's
    //  end stop, and then cannot tell the filter from one sitting at its
    //  bypass. These are the explicit answer, and they are pill switches
    //  because they are state, like GL and the mode button.
}

void FirstAudioProcessorEditor::setUpEqCornerSwitches()
{
    const auto setUpEqCornerSwitch = [this] (juce::ToggleButton& button,
                                             const juce::String& which,
                                             const juce::String& corner)
    {
        button.setClickingTogglesState (true);
        setTip (button, which.toUpperCase() + " " + corner.toUpperCase() + " ON/OFF. "
                           "The corner frequency beside it sets where the filter "
                           "turns; this says whether the filter is in the signal at "
                           "all. With the switch off the corner keeps its setting, "
                           "so turning the filter back on does not make the user "
                           "find the frequency again - which is the difference "
                           "between a switch and parking the knob at its end stop.");
        button.setLookAndFeel (&inlineLookAndFeel);
        addAndMakeVisible (button);
    };

    setUpEqCornerSwitch (inputEqHpButton, "Input EQ", "HP");
    setUpEqCornerSwitch (inputEqLpButton, "Input EQ", "LP");
    setUpEqCornerSwitch (outputEqHpButton, "Output EQ", "HP");
    setUpEqCornerSwitch (outputEqLpButton, "Output EQ", "LP");

    inputEqHpAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (audioProcessor.parameters, "in_hp_on", inputEqHpButton);
    inputEqLpAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (audioProcessor.parameters, "in_lp_on", inputEqLpButton);
    outputEqHpAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (audioProcessor.parameters, "out_hp_on", outputEqHpButton);
    outputEqLpAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (audioProcessor.parameters, "out_lp_on", outputEqLpButton);
}

void FirstAudioProcessorEditor::setUpPresetBar()
{
    // ---------------------------------------------------------------
    //  Premium workflow bar.
    // ---------------------------------------------------------------
    styleLabel (presetHeadingLabel, "PRESET", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::centredLeft);
    addAndMakeVisible (presetHeadingLabel);

    refreshPresetList();
    // The box now starts with nothing selected on a fresh instance (honest display:
    // no factory preset has been applied yet), so it needs a placeholder for the
    // empty row rather than the base Look and Feel's blank field.
    presetBox.setTextWhenNothingSelected ("FACTORY...");
    setTip (presetBox, "Factory presets. Loading one replaces the whole machine state "
                          "in a single undoable step.");
    presetBox.setLookAndFeel (&customLookAndFeel);
    presetBox.onChange = [this]
    {
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
        if (presetIndex == audioProcessor.getLastPresetIndex()
            && ! audioProcessor.isPresetDirty())
            return;

        audioProcessor.applyFactoryPreset (presetIndex);
        lastShownPreset = presetIndex;
    };
    addAndMakeVisible (presetBox);

    const auto workflowButtonSetup = [&] (juce::TextButton& button, const juce::String& tip)
    {
        setTip (button, tip);
        button.setLookAndFeel (&customLookAndFeel);
        addAndMakeVisible (button);
    };

    // ---------------------------------------------------------------
    //  User presets: the combo lists what is on disk, SAVE opens a name
    //  dialog and DEL removes the selected file. Every action refreshes
    //  the list so two instances of the plugin stay honest with each
    //  other about what exists.
    // ---------------------------------------------------------------
    refreshUserPresetList();
    setTip (userPresetBox, "User presets - your own saved machine states. "
                              "Selecting one recalls it as a single undoable step.");
    userPresetBox.setLookAndFeel (&customLookAndFeel);
    userPresetBox.setTextWhenNothingSelected ("USER...");
    userPresetBox.onChange = [this]
    {
        const auto selectedId = userPresetBox.getSelectedId();
        if (selectedId <= 0)
            return;
        const auto name = userPresetBox.getItemText (selectedId - 1);
        if (name.isEmpty() || name == audioProcessor.getCurrentPresetName())
            return;
        if (audioProcessor.applyUserPreset (name))
        {
            lastShownUserPreset = name;
            lastShownPreset = -1;
            // A user preset clears the factory selection: ID 0 is "no selection".
            presetBox.setSelectedId (audioProcessor.getLastPresetIndex() + 1,
                                     juce::dontSendNotification);
            updateWorkflowButtons();
        }
        else
        {
            refreshUserPresetList(); // the file vanished; rebuild the list
        }
    };
    addAndMakeVisible (userPresetBox);

    workflowButtonSetup (savePresetButton, "Store the whole machine state as a user "
                                           "preset you can recall in any session.");
    workflowButtonSetup (deletePresetButton, "Delete the selected user preset file.");
    savePresetButton.onClick = [this]
    {
        if (savePresetWindow != nullptr)
        {
            savePresetWindow->exitModalState (0);
            savePresetWindow.reset();
        }

        auto* window = new juce::AlertWindow ("Save user preset",
                                              "Name this machine state:",
                                              juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("preset_name", audioProcessor.getCurrentPresetName(), "Name:");
        window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        savePresetWindow.reset (window);

        window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window] (int result)
        {
            // Guard on the pointer: an older dialog's asynchronous callback must
            // never dismiss a newer one.
            if (savePresetWindow.get() != window)
                return;
            const auto chosen = window->getTextEditorContents ("preset_name");
            savePresetWindow.reset();
            if (result == 1 && chosen.trim().isNotEmpty())
            {
                if (audioProcessor.saveUserPreset (chosen))
                    refreshUserPresetList();
            }
        }), false);
    };
    deletePresetButton.onClick = [this]
    {
        const auto selectedId = userPresetBox.getSelectedId();
        if (selectedId <= 0)
            return;
        const auto name = userPresetBox.getItemText (selectedId - 1);
        if (name.isNotEmpty() && audioProcessor.deleteUserPreset (name))
            refreshUserPresetList();
    };

    styleLabel (presetBadgeLabel, "", 8.0f, paletteFor (false).secondary,
                false, juce::Justification::centredLeft);
    addAndMakeVisible (presetBadgeLabel);

    workflowButtonSetup (copyAButton, "Store the current settings in slot A. "
                                      "A is the side the plugin starts on.");
    workflowButtonSetup (copyBButton, "Store the current settings in slot B.");
    workflowButtonSetup (compareButton, "Toggle between slots A and B to compare settings "
                                        "with identical level, bypass state and oversampling.");
    workflowButtonSetup (undoButton, "Undo the last preset or A/B change (Ctrl+Z).");
    workflowButtonSetup (redoButton, "Redo an undone preset or A/B change (Ctrl+Y).");

    copyAButton.onClick = [this]
    {
        audioProcessor.copyToCompareSlot (0);
        updateWorkflowButtons();
    };
    copyBButton.onClick = [this]
    {
        audioProcessor.copyToCompareSlot (1);
        updateWorkflowButtons();
    };
    compareButton.onClick = [this]
    {
        audioProcessor.toggleCompare();
        updateWorkflowButtons();
    };
    undoButton.onClick = [this]
    {
        audioProcessor.getUndoManager().undo();
        updateWorkflowButtons();
    };
    redoButton.onClick = [this]
    {
        audioProcessor.getUndoManager().redo();
        updateWorkflowButtons();
    };

    styleLabel (compareBadgeLabel, "", 8.5f, paletteFor (false).accent,
                true, juce::Justification::centred);
    addAndMakeVisible (compareBadgeLabel);
}

void FirstAudioProcessorEditor::addDeckControlsToPanel()
{
    addAndMakeVisible (tapeTypeBox);
    addAndMakeVisible (valveTypeBox);
    addAndMakeVisible (ampTypeBox);
    addAndMakeVisible (transformerTypeBox);
    addAndMakeVisible (digitalTypeBox);
    addAndMakeVisible (vinylTypeBox);
    addAndMakeVisible (vinylSpeedBox);
    addAndMakeVisible (speedBox);
    addAndMakeVisible (bypassButton);
    addAndMakeVisible (themeButton);
    tapeTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "tape_type", tapeTypeBox);
    valveTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "valve_type", valveTypeBox);
    ampTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "amp_type", ampTypeBox);
    transformerTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "transformer_type", transformerTypeBox);
    digitalTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "digital_type", digitalTypeBox);
    vinylTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "vinyl_type", vinylTypeBox);
    vinylSpeedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "vinyl_speed", vinylSpeedBox);
    speedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "speed", speedBox);
}

void FirstAudioProcessorEditor::setUpSceneAndMeters()
{
    // The 3D transport goes in FIRST, so it is the bottom-most child. It is a
    // child component rather than part of this editor's own painting because it
    // needs a custom renderer and this editor's context is a component painter -
    // see Source/GUI/TapeScene.h. It sits in the strip resized() keeps clear of
    // every control, so nothing here can end up drawn on top of it.
    addAndMakeVisible (tapeScene);

    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);
    addAndMakeVisible (compressorMeterIn);
    addAndMakeVisible (compressorMeterOut);
    applyTheme();
}

void FirstAudioProcessorEditor::setUpOpenGlRendering()
{
    // The selected backend is compile-time. OpenGL is only an optimisation for
    // the animated panel, and it is the least portable part of the UI: some
    // Windows drivers, remote sessions, virtual machines and headless hosts
    // cannot create a context at all. Attaching unconditionally
    // means those setups get a broken or blank editor, so the context is treated as a
    // best-effort accelerator. If it cannot attach, the component renderer draws the
    // same panel through the software path with no visual difference.
    openGLContext.setComponentPaintingEnabled (true);
    openGLContext.setContinuousRepainting (false);
    // V-sync prevents the component-painting context from spinning at the
    // monitor/driver maximum when the editor has no invalidated regions.
    // Explicit repaint() calls still wake it when the panel actually changes.
    openGLContext.setSwapInterval (1);

    if (! j37::render::usesOpenGL())
    {
        syncGlSwitchState();
    }
    else
    {
        // attachTo() returns void, so it cannot be tested directly - it would read as
    // `if (!void)`, which is not a valid expression. The documented way to find out whether
    // the context came up is to ask afterwards, and detach if it did not so the component
    // renderer draws the panel instead.
        openGLContext.attachTo (*this);

        if (! openGLContext.isAttached())
            openGLContext.detach();
    }

    // The renderer switch mirrors the context's real state rather than a wish: on
    // machines where the context never came up the button reads OFF and the
    // software renderer is what the user sees. The 3D transport follows the same
    // switch, because it is an accelerator rather than a feature.
    syncGlSwitchState();

    // OpenGL is ON by default, and a failed first attach is usually not a final
    // answer: the host may not have created the editor's native peer yet, and there
    // is no context to attach to without one. The timer keeps trying for a few
    // seconds and then gives up, because a driver, a remote session or a VM that
    // cannot create a context will not manage on the last attempt either.
    glAttachAttemptsLeft = (! j37::render::usesOpenGL() || openGLContext.isAttached()) ? 0 : 90;
    glButton.onClick = [this]
    {
        if (! j37::render::usesOpenGL())
        {
            syncGlSwitchState();
            return;
        }

        // Whatever the user asked for wins: stop the startup retries, or they would
        // switch the accelerator back on seconds after the user turned it off.
        // glUserDisabled records WHICH side dropped the context - only the user's
        // own click sets it, so the timer's retries still recover a context the
        // HOST lost (see the retry block in timerCallback).
        glAttachAttemptsLeft = 0;

        if (openGLContext.isAttached())
        {
            glUserDisabled = true;
            openGLContext.detach();
        }
        else
        {
            glUserDisabled = false;
            openGLContext.attachTo (*this);
        }

        // One place decides what the switch says, how it looks, and whether the
        // 3D transport gets a context, and all three read the context's state
        // afterwards: attachTo() can fail on a driver, a remote session or a VM,
        // and a branch that assumed success would leave the button claiming GL is
        // on while the software renderer is drawing the panel.
        syncGlSwitchState();
    };
}

void FirstAudioProcessorEditor::finishConstruction()
{
    // The interface sounds are attached LAST, after every control they watch
    // exists AND after every existing onClick handler has been installed - so a
    // control's own handler is always the one that runs first and the sound
    // cannot replace it. See attachInterfaceSounds() for how each control's
    // voice is chosen.
    attachInterfaceSounds();

    startTimerHz (30);
}



FirstAudioProcessorEditor::~FirstAudioProcessorEditor()
{
    stopTimer();
    openGLContext.detach();

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
    glButton.setLookAndFeel (nullptr);
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

void FirstAudioProcessorEditor::refreshUserPresetList()
{
    userPresetBox.clear (juce::dontSendNotification);
    const auto names = audioProcessor.getUserPresetNames();
    for (auto i = 0; i < names.size(); ++i)
        userPresetBox.addItem (names[i], i + 1);

    const auto loaded = audioProcessor.getCurrentPresetName();
    const auto index = names.indexOf (loaded);
    userPresetBox.setSelectedItemIndex (index >= 0 ? index : -1, juce::dontSendNotification);
    lastShownUserPreset = index >= 0 ? loaded : juce::String();
}

void FirstAudioProcessorEditor::refreshPresetList()
{
    presetBox.clear (juce::dontSendNotification);
    const auto names = FirstAudioProcessor::getPresetNames();
    for (auto i = 0; i < names.size(); ++i)
        presetBox.addItem (names[i], i + 1);

    // A fresh instance has NO preset loaded (index -1): showing "Default" as the
    // box's selection then was a lie - the panel held the raw parameter defaults
    // (DRIVE 0.30), not that preset's values (DRIVE 0.28), and picking "Default"
    // looked like it had done nothing. Nothing selected is the honest display; the
    // first honest load happens when the user (or a session) actually applies one.
    const auto loaded = audioProcessor.getLastPresetIndex();
    presetBox.setSelectedItemIndex (loaded >= 0 ? loaded : -1, juce::dontSendNotification);
    lastShownPreset = loaded;
}

void FirstAudioProcessorEditor::updateWorkflowButtons()
{
    // The A/B buttons carry the active side on their text, so the panel always says
    // which slot is live without needing a separate readout.
    const auto activeSlot = audioProcessor.getActiveCompareSlot();
    copyAButton.setButtonText (activeSlot == 0 ? "A (LIVE)" : "COPY A");
    copyBButton.setButtonText (activeSlot == 1 ? "B (LIVE)" : "COPY B");
    compareButton.setButtonText (activeSlot == 0 ? "A/B: B" : "A/B: A");

    // An edited dot on the inactive slot's caption: when the two stored slots differ,
    // the side you are NOT hearing has something different on it.
    const auto dirty = audioProcessor.isCompareDirty();
    copyAButton.setButtonText (copyAButton.getButtonText()
                               + (dirty && activeSlot != 0 ? " *" : ""));
    copyBButton.setButtonText (copyBButton.getButtonText()
                               + (dirty && activeSlot != 1 ? " *" : ""));


    auto& undoManager = audioProcessor.getUndoManager();
    undoButton.setEnabled (undoManager.canUndo());
    redoButton.setEnabled (undoManager.canRedo());

    // The toggles draw themselves through J37LookAndFeel::drawToggleButton, which
    // reads the palette live, so theme and state changes recolour on the same frame
    // without any per-colour bookkeeping here.

    // Short captions: the badge sits at the right end of the preset row, where the
    // old "A/B MATCHED" could push into the redo button on a narrow panel.
    compareBadgeLabel.setText (dirty ? "EDITED" : "MATCHED",
                               juce::dontSendNotification);

    const auto presetName = audioProcessor.getCurrentPresetName();
    const auto presetIsDirty = audioProcessor.isPresetDirty();
    const auto presetBadgeText = presetName.isEmpty()
                                ? "FACTORY STATE"
                                : (presetIsDirty ? "PRESET: " + presetName + " (EDITED)"
                                                 : "PRESET: " + presetName);
    presetBadgeLabel.setText (presetBadgeText, juce::dontSendNotification);
    // Re-fit on every caption change, not just on resize: a user can name a preset
    // anything at all, and the badge must shrink to fit rather than clip at the edge.
    presetBadgeLabel.setFont (shrinkingFont (presetBadgeText, 8.0f, juce::Font::plain,
                                             static_cast<float> (presetBadgeLabel.getWidth()) - 4.0f));
    presetBadgeLabel.setColour (juce::Label::textColourId,
                                presetIsDirty ? paletteFor (darkTheme).accent
                                              : paletteFor (darkTheme).secondary);
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
    // sizes and on high-DPI displays. The header is 84 px - tall enough for three
    // right-hand rows: the four engine switches (BYPASS / DELTA / POLARITY / AUTO GAIN),
    // THEME with the language pair, and the machine-state readout on a full-width
    // band of its own (see the header grid in resized()).
    //
    // The deck is 278 px and was 322. The forty-four came out of the machine readouts,
    // which were a three-high vertical stack pinned to the deck's right edge and are
    // now three caption-over-value pairs side by side inside the 32 px band they share
    // with OVER, INSTRUMENT and GL. Nothing was moved off the deck to buy it - the
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

void FirstAudioProcessorEditor::applyTheme()
{
    // The editor's own bool is derived from the three-way choice, so every one of
    // the ~50 `paletteFor (darkTheme)` call sites in this file keeps working
    // unchanged - and the two dark themes genuinely DO share all of them. The
    // only place the two are told apart is this next line.
    darkTheme = themeChoice != j37panel::ThemeChoice::ivory;

    liveThemeChoice = themeChoice;

    const auto& palette = paletteForTheme (themeChoice);
    customLookAndFeel.setTheme (themeChoice);
    // The in-tab look and feel carries its own theme for the same reason the
    // deck's does: it reads the palette live at paint time, so the lists and the
    // pill switches recolour on the same frame as everything else.
    inlineLookAndFeel.setTheme (themeChoice);
    inputMeter.setDarkTheme (darkTheme);
    outputMeter.setDarkTheme (darkTheme);

    // The 3D transport reads the same palette rather than keeping colours of its
    // own, so it follows a theme switch on the same frame as everything else.
    // The four are chosen for what each one IS rather than for which palette slot
    // it resembles: the window is the panel's dead-black readout recess, the reel
    // is its machined plate, the highlight and the head are the accent (so the
    // specular reads as the machine's one indicator lamp), and the tape is the
    // knob face warmed and desaturated - a roll of tape, not a second accent.
    tapeScene.setPalette (palette.readout,
                          palette.raised,
                          palette.accent,
                          palette.knobFace.brighter (0.28f).withSaturation (0.55f));
    {
        const juce::SpinLock::ScopedLockType lock (renderOrbsLock);
        for (std::size_t i = 0; i < decorativeOrbCount; ++i)
        {
            const auto orbColour = palette.accent.withAlpha (0.25f + static_cast<float> (i) * 0.04f);
            physicsOrbs[i].colour = orbColour;
            renderOrbs[i].colour = orbColour;
        }
    }

    brandLabel.setColour (juce::Label::textColourId, palette.accent);
    titleLabel.setColour (juce::Label::textColourId, palette.text);
    subtitleLabel.setColour (juce::Label::textColourId, palette.secondary);
    statusLabel.setColour (juce::Label::textColourId, palette.status);

    // The texture's doses are theme-dependent (light ink needs more), so a
    // theme flip recomputes them for BOTH renderers before the static layers
    // are rebuilt - otherwise a light panel would keep the dark theme's
    // weights until the next timer tick pushed new ones in.
    configureTextureWeights (panelTextureDrive, panelTextureGainReduction);
    deckHeadingLabel.setColour (juce::Label::textColourId, palette.accent);
    buildLabel.setColour (juce::Label::textColourId, palette.secondary);
    tapeTypeLabel.setColour (juce::Label::textColourId, palette.secondary);
    speedLabel.setColour (juce::Label::textColourId, palette.secondary);
    deckHintLabel.setColour (juce::Label::textColourId, palette.secondary);
    controlsHeadingLabel.setColour (juce::Label::textColourId, palette.accent);
    controlsHintLabel.setColour (juce::Label::textColourId, palette.secondary);
    sectionCaptionLabel.setColour (juce::Label::textColourId, palette.secondary);
    // The mix bar keeps its OWN dark flag rather than reading one: it is painted
    // from a child component, and a child cannot ask which theme the editor is on
    // without being told. Three lines of state instead of a wrong colour on a
    // theme the user just cycled.
    mixBar.setDarkTheme (darkTheme);
    metersHeadingLabel.setColour (juce::Label::textColourId, palette.accent);
    metersHintLabel.setColour (juce::Label::textColourId, palette.secondary);

    for (auto& label : controlLabels)
        label.setColour (juce::Label::textColourId, palette.secondary);

    // The COMP page's six captions are the same captions as the strip's - the same
    // 9 pt bold font, the same centred caption band, the same job - but they are a
    // separate array, and the constructor that builds them sets their text and
    // their font and never their ink. Unstyled, a juce::Label reads
    // Label::textColourId off the Look and Feel, which is LookAndFeel_V4's DARK
    // colour scheme whatever panel is showing: THRESH, RATIO, ATTACK, RELEASE,
    // MAKEUP and MIX were six white captions on an ivory panel, which is the
    // "font is not connected to the theme" on the COMP page.
    for (auto& label : compressorControlLabels)
        label.setColour (juce::Label::textColourId, palette.secondary);

    // Three captions the constructor inked and nothing re-inked: TUNE AMOUNT is a
    // knob page's caption now (the loop above places it in the knob strip), and the
    // TONIC and MODE captions are its peers - the two pages used to share one tab.
    // All three follow the panel like every other caption on it, instead of
    // keeping the ivory panel's ink on charcoal.
    autoTuneAmountLabel.setColour (juce::Label::textColourId, palette.secondary);
    tuningTonicLabel.setColour (juce::Label::textColourId, palette.secondary);
    tuningModeLabel.setColour (juce::Label::textColourId, palette.secondary);

    // The deck's and the tabs' remaining captions. Every label the constructor
    // styles with paletteFor(false) has to be re-inked here as well, or a theme
    // switch leaves it in the previous panel's colour - the six type selectors,
    // the machine readouts and the tab-member captions all kept ivory-brown text
    // on the charcoal and metal panels until this block existed.
    languageLabel.setColour (juce::Label::textColourId, palette.secondary);
    bpmLabel.setColour (juce::Label::textColourId, palette.secondary);
    bpmReadout.setColour (juce::Label::textColourId, palette.secondary);
    vinylTypeLabel.setColour (juce::Label::textColourId, palette.secondary);
    vinylSpeedLabel.setColour (juce::Label::textColourId, palette.secondary);
    valveTypeLabel.setColour (juce::Label::textColourId, palette.secondary);
    ampTypeLabel.setColour (juce::Label::textColourId, palette.secondary);
    transformerTypeLabel.setColour (juce::Label::textColourId, palette.secondary);
    digitalTypeLabel.setColour (juce::Label::textColourId, palette.secondary);
    diPadLabel.setColour (juce::Label::textColourId, palette.secondary);
    tracksLabel.setColour (juce::Label::textColourId, palette.secondary);
    inputEqOrderLabel.setColour (juce::Label::textColourId, palette.secondary);
    outputEqOrderLabel.setColour (juce::Label::textColourId, palette.secondary);
    vinylGenerationLabel.setColour (juce::Label::textColourId, palette.secondary);
    vinylTurntableLabel.setColour (juce::Label::textColourId, palette.secondary);
    vinylCartridgeLabel.setColour (juce::Label::textColourId, palette.secondary);
    delaySyncLabel.setColour (juce::Label::textColourId, palette.secondary);
    glLabel.setColour (juce::Label::textColourId, palette.secondary);
    antiPhaseGuardLabel.setColour (juce::Label::textColourId, palette.secondary);
    neuralStatusLabel.setColour (juce::Label::textColourId, palette.secondary);
    // The IR readout re-inks itself in refreshIrStatus() on every change; this
    // covers the theme switch, which repaints without a status change.
    irStatusLabel.setColour (juce::Label::textColourId,
                             audioProcessor.hasIr() ? palette.accent : palette.secondary);

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
    const auto inkKnob = [&palette] (juce::Slider& slider)
    {
        slider.setColour (juce::Slider::textBoxTextColourId, palette.text);
        slider.setColour (juce::Slider::textBoxBackgroundColourId, palette.readout);
        slider.setColour (juce::Slider::textBoxOutlineColourId, palette.border);
        slider.setColour (juce::Slider::thumbColourId, palette.accent);
    };

    for (auto& slider : controls)
        inkKnob (slider);

    for (auto& slider : compressorControls)
        inkKnob (slider);

    inkKnob (autoTuneAmountSlider);

    const auto styleCombo = [&palette] (juce::ComboBox& box)
    {
        box.setColour (juce::ComboBox::backgroundColourId, palette.raised);
        box.setColour (juce::ComboBox::outlineColourId, palette.border);
        box.setColour (juce::ComboBox::textColourId, palette.text);
        box.setColour (juce::ComboBox::arrowColourId, palette.accent);
        box.setColour (juce::ComboBox::focusedOutlineColourId, palette.accent);
    };
    styleCombo (tapeTypeBox);
    styleCombo (valveTypeBox);
    styleCombo (ampTypeBox);
    styleCombo (transformerTypeBox);
    styleCombo (digitalTypeBox);
    styleCombo (vinylTypeBox);
    styleCombo (vinylSpeedBox);
    styleCombo (speedBox);
    styleCombo (presetBox);
    // The user-preset combo is the seventh ComboBox and was the one left out of this
    // list - and of the one in applyTheme() below - so it kept LookAndFeel_V4's light
    // default background on a dark panel and never followed the theme toggle.
    styleCombo (userPresetBox);
    // The language selector is drawn by the deck Look and Feel (which overrides
    // captions, not combo backgrounds), so like the user-preset list it needs its
    // palette colours set here or it keeps the base Look and Feel's own grey -
    // a box that matched nothing else on the panel. This styling block runs AFTER
    // applyLanguage above, so the box's text colour set there is overwritten by
    // this call with the palette's own text colour - the intended end state, and
    // the reason applyLanguage re-inks textColourId itself is only to colour the
    // window BETWEEN those two moments.
    styleCombo (languageBox);

    // The lists' popup menus paint through the LookAndFeel_V4 item drawing, which
    // reads THESE colour ids off the attached LookAndFeel - at theme time there is
    // no PopupMenu object to style directly, so both LookAndFeels carry the palette
    // and whichever box opens a menu uses its own. A dark panel without them opened
    // the base look's light menu, whose black text on the menu's black selection
    // highlight was unreadable (the complaint behind "text is not readable").
    const auto stylePopupMenu = [&palette] (juce::LookAndFeel& lf)
    {
        lf.setColour (juce::PopupMenu::backgroundColourId, palette.panel.brighter (0.08f));
        lf.setColour (juce::PopupMenu::textColourId, palette.text);
        lf.setColour (juce::PopupMenu::highlightedBackgroundColourId, palette.accent.withAlpha (0.85f));
        lf.setColour (juce::PopupMenu::highlightedTextColourId, palette.readout);
    };
    stylePopupMenu (customLookAndFeel);
    stylePopupMenu (inlineLookAndFeel);

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
    customLookAndFeel.setColour (juce::ComboBox::textColourId, palette.text);
    inlineLookAndFeel.setColour (juce::ComboBox::textColourId, palette.text);

    const auto styleWorkflowButton = [&palette] (juce::TextButton& button, bool emphasised)
    {
        button.setColour (juce::TextButton::buttonColourId, palette.raised);
        button.setColour (juce::TextButton::buttonOnColourId, palette.accent);
        button.setColour (juce::TextButton::textColourOffId, emphasised ? palette.text : palette.secondary);
        button.setColour (juce::TextButton::textColourOnId, palette.readout);
    };

    // bypassButton is a ToggleButton drawn by J37LookAndFeel::drawToggleButton.
    themeButton.setColour (juce::TextButton::buttonColourId, palette.raised);
    themeButton.setColour (juce::TextButton::textColourOffId, palette.text);

    // The model and IR pickers' four buttons. Their constructor says it re-inks
    // them per theme "in applyTheme()" and nothing here did, so on charcoal and
    // metal they kept the ivory panel's raised face and its dark ink - a light pill
    // with dark text on a black panel, on the one page whose whole point is that
    // the two pickers read as devices rather than as unstyled widgets. They carry
    // the same raised-face pill the workflow buttons do, from the same palette.
    const auto stylePickerButton = [&palette] (juce::TextButton& button)
    {
        button.setColour (juce::TextButton::buttonColourId, palette.raised);
        button.setColour (juce::TextButton::textColourOffId, palette.text);
        button.setColour (juce::TextButton::buttonOnColourId, palette.raised.darker (0.15f));
    };
    stylePickerButton (loadNeuralButton);
    stylePickerButton (clearNeuralButton);
    stylePickerButton (loadIrButton);
    stylePickerButton (clearIrButton);

    styleCombo (oversamplingBox);
    styleCombo (instrumentBox);
    // The seven tab-member lists deliberately do NOT go through styleCombo: they
    // are drawn by J37InlineLookAndFeel, which reads the palette live and has
    // its own, flatter treatment. Styling their field would fight it. The VALUE
    // colour and font below are not part of that treatment either - juce::ComboBox
    // paints its value with an internal Label reading ComboBox::textColourId, and
    // an unstyled list kept the base LookAndFeel's BLACK value on the dark panels:
    // DI PAD, TRACKS, ORDER and the vinyl trio were simply unreadable. The font is
    // set through the same Label, so the in-tab lists use the flat field's own
    // compact size instead of whatever V4 scales to the box.
    const auto styleInlineListValue = [&palette] (juce::ComboBox& box)
    {
        box.setColour (juce::ComboBox::textColourId, palette.text);
    };
    styleInlineListValue (diPadBox);
    styleInlineListValue (tracksBox);
    styleInlineListValue (inputEqOrderBox);
    styleInlineListValue (outputEqOrderBox);
    styleInlineListValue (vinylGenerationBox);
    styleInlineListValue (vinylTurntableBox);
    styleInlineListValue (vinylCartridgeBox);
    // The tuning pair was missing from this list and from every other colour
    // call in the file, so TONIC and MODE were the only two lists on the panel
    // still painted with LookAndFeel_V4's own grey value - which on this panel
    // reads as a DISABLED control: a grey field under a live caption, beside an
    // OVERSAMPLING list that is the same widget in the same kind of cell and
    // looks nothing like it. Two lines, one cause.
    styleInlineListValue (tuningTonicBox);
    styleInlineListValue (tuningModeBox);
    // The delay trio is on the deck Look and Feel (see its construction), so it
    // follows styleCombo like every other deck list.
    styleCombo (delayTypeBox);
    styleCombo (delayRateBox);
    // The transport keys and the spindown button are TextButtons, not combos, so
    // they take their colours from styleTransportButtons() / styleSpindownButton(),
    // which read the palette AND the live state - that is what makes the active key
    // stay highlighted across a theme switch.
    styleTransportButtons();
    styleSpindownButton();
    // modeCycleButton is a TextButton whose colours styleWorkflowButton-style
    // calls do not own; it reads the palette through the look and feel, and its
    // caption is refreshed by refreshModeButtonCaption() below.
    styleCombo (userPresetBox);
    styleGlButton (openGLContext.isAttached());
    styleWorkflowButton (copyAButton, audioProcessor.getActiveCompareSlot() == 0);
    styleWorkflowButton (copyBButton, audioProcessor.getActiveCompareSlot() == 1);
    styleWorkflowButton (compareButton, false);
    styleWorkflowButton (undoButton, false);
    styleWorkflowButton (redoButton, false);
    styleWorkflowButton (savePresetButton, false);
    styleWorkflowButton (deletePresetButton, false);
    // polarityButton and autoGainButton are drawn by J37LookAndFeel::drawToggleButton
    // and need no per-theme colour calls here.
    presetHeadingLabel.setColour (juce::Label::textColourId, palette.secondary);
    oversamplingLabel.setColour (juce::Label::textColourId, palette.secondary);
    instrumentLabel.setColour (juce::Label::textColourId, palette.secondary);
    transportLabel.setColour (juce::Label::textColourId, palette.secondary);
    spindownLabel.setColour (juce::Label::textColourId, palette.secondary);
    delayTypeLabel.setColour (juce::Label::textColourId, palette.secondary);
    delayRateLabel.setColour (juce::Label::textColourId, palette.secondary);
    compareBadgeLabel.setColour (juce::Label::textColourId,
                                 audioProcessor.isCompareDirty() ? palette.accent : palette.secondary);

    compressorLabel.setColour (juce::Label::textColourId, palette.accent);
    compressorReadout.setColour (juce::Label::textColourId, palette.secondary);
    harmonicsLabel.setColour (juce::Label::textColourId, palette.accent);
    harmonicsReadout.setColour (juce::Label::textColourId, palette.secondary);
    subfundLabel.setColour (juce::Label::textColourId, palette.accent);
    subfundReadout.setColour (juce::Label::textColourId, palette.secondary);
    antiPhaseLabel.setColour (juce::Label::textColourId, palette.accent);
    antiPhaseReadout.setColour (juce::Label::textColourId, palette.secondary);
    compressorMeterIn.setDarkTheme (darkTheme);
    compressorMeterOut.setDarkTheme (darkTheme);

    // The caption names the theme you are ON, and the tooltip says what the next
    // press will do - so the label can never be read as "press this to become X"
    // when it is already X.
    switch (themeChoice)
    {
        case j37panel::ThemeChoice::ivory:
            themeButton.setButtonText ("IVORY");
            break;
        case j37panel::ThemeChoice::charcoal:
            themeButton.setButtonText ("CHARCOAL");
            break;
        case j37panel::ThemeChoice::metal:
        default:
            themeButton.setButtonText ("METAL");
            break;
    }

    updateWorkflowButtons();
    // The mode button's colours read the palette (see refreshModeButtonCaption),
    // so a theme switch must re-run it - the caption is unchanged, the ink is not.
    refreshModeButtonCaption();
    repaint();
}

//==============================================================================
void FirstAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto& palette = paletteFor (darkTheme);
    // The band this paint actually has to cover. The panel is repainted in bands
    // (see timerCallback) and most of what this function draws never changes, so
    // the clip is used to skip whole sections instead of drawing them and letting
    // the rasteriser discard the pixels - the draw calls, the geometry and the
    // trigonometry are all still paid for if they are issued.
    const auto clip = g.getClipBounds();
    const auto inClip = [&clip] (const juce::Rectangle<int>& area) { return area.intersects (clip); };

    g.fillAll (palette.background);

    const auto layout = getEditorLayout();
    // The neural card sits behind the SHAPERS page's picker cell, and its
    // geometry MIRRORS resized()'s: the grid is the controls band reduced by
    // 14, cut 78 from the top (controlsDividerOffset 92 - 14 + 8) and 8 from
    // the bottom; the member band is a fixed 50 px strip at the grid's bottom.
    // The picker rides the member band's FIRST column - its cell x is grid.x,
    // width cellWidth (cellWidth = gridW / 4).
    constexpr int tabColumnsNow = 4;
    constexpr int memberBandHeightNow = 50;
    auto gridNow = layout.controls.reduced (14);
    gridNow.removeFromTop (controlsDividerOffset - 14 + 8);
    gridNow.removeFromBottom (8);
    const auto memberBandY = gridNow.getBottom() - memberBandHeightNow;
    const auto cellWidthNow = gridNow.getWidth() / tabColumnsNow;

    // ------------------------------------------------------------------
    //  Chassis grain.
    //
    //  Drawn BEFORE the section panels so it sits on the chassis rather than over
    //  the controls - the old version painted its 18 rules across the whole
    //  window, which put lines through every panel and read as scratches rather
    //  than as a surface.
    //
    //  Two passes: a fine 3 px weave for the texture, and a much fainter 12 px
    //  one to break up the regularity. A single period reads as a pattern; two
    //  beating against each other read as brushed metal.
    // ------------------------------------------------------------------
    const auto chassis = getLocalBounds().reduced (8).toFloat();
    const auto grainAlpha = darkTheme ? 0.016f : 0.022f;
    const auto grainColour = palette.text.withAlpha (grainAlpha);

    // Two passes over the whole chassis is a few hundred rules, and the panels are
    // drawn over the top of them immediately afterwards - so most of them are
    // covered within the same paint. Skipping the ones the clip cannot see is what
    // keeps that affordable at 30 Hz with a context attached, where every one of
    // these repaints is a render of this component.
    // Float, to match the float y the loops below count with. getY()/getBottom()
    // are int, and the implicit conversions warned on every Clang build.
    const auto grainTop = static_cast<float> (clip.getY());
    const auto grainBottom = static_cast<float> (clip.getBottom());

    g.setColour (grainColour);
    for (float y = chassis.getY() + 2.0f; y < chassis.getBottom(); y += 3.0f)
        if (y >= grainTop && y <= grainBottom)
            g.drawHorizontalLine (juce::roundToInt (y),
                                  chassis.getX() + 6.0f, chassis.getRight() - 6.0f);

    g.setColour (palette.text.withAlpha (grainAlpha * 0.6f));
    for (float y = chassis.getY() + 7.0f; y < chassis.getBottom(); y += 12.0f)
        if (y >= grainTop && y <= grainBottom)
            g.drawHorizontalLine (juce::roundToInt (y),
                                  chassis.getX() + 6.0f, chassis.getRight() - 6.0f);

    // (The arithmetic texture is blitted further down, AFTER the panels, so the
    // panels' own faces sit over the chassis grain rather than over the texture.)


    drawPanel (g, getLocalBounds().reduced (8), palette, 7.0f);

    // The camera is set here rather than in the constructor because it depends
    // on the panel's own size: the viewer stands over the CENTRE of whatever
    // the panel currently is, so the same drawing code answers "how edge-on is
    // this knob" correctly at the minimum window size and at four times it.
    // The panel ITSELF, not a rectangle: the L&F converts it into the same
    // top-level space every control is measured in, which is what it needs to
    // know where each control stands relative to the viewer.
    customLookAndFeel.setPanelCamera (*this);
    // The in-tab lists' inset is solved from the same panel height the camera is
    // measured in (comboTextInset), so the second look-and-feel is told the same
    // thing here rather than reading a size of its own.
    inlineLookAndFeel.setPanelBounds (*this);
    drawPanel (g, layout.header, palette, 5.0f);
    drawPanel (g, layout.deck, palette, 5.0f);
    drawPanel (g, layout.controls, palette, 5.0f);
    drawPanel (g, layout.meters, palette, 5.0f);

    // ----------------------------------------------------------------------
    //  The panel's texture, 2D and mathematical - the same three terms the GPU
    //  scene shades, in the same doses (uTextureWeights there):
    //
    //   - static grain + wear blotches: ONE image, built once per resize in
    //     rebuildPanelTextureLayers() from the same hash spaces the shader
    //     uses (grain = fract(sin(p*7.5)), wear = value noise), blitted here
    //     in one call - AFTER the panels, so their faces carry it rather than
    //     hiding it, and BEFORE the child controls, which paint on top of
    //     everything here by being children;
    //   - live shimmer: a sparse drift of dots whose positions are the shader's
    //     hash(face*40 + t*(1+GR)) evaluated at a walking time offset - so the
    //     surface visibly responds to DRIVE and to the compressors. THIS is
    //     what the GL switch switches between: with the context attached the
    //     scene's fragment shader draws the identical texture definition on
    //     the GPU; without it, this ink does.
    //  Every dose is centered, so no branch can shift the panel's colour.
    // ----------------------------------------------------------------------
    if (panelTextureImage.isValid())
    {
        // Two rules keep the surface from reading as "square blocks":
        //
        //  1. RESAMPLING QUALITY. The ten-argument drawImage carries no quality
        //     of its own - the blit runs at whatever Graphics::setImageResamplingQuality
        //     last set, and Graphics' default is LOW, i.e. nearest-neighbour:
        //     a half-res layer drawn through it arrives as 2x2 blocks, which is
        //     precisely the "quadrats" the user saw on alternating frames. The
        //     quality is set explicitly for this one blit and restored after, so
        //     every other image in the panel keeps its old speed.
        //
        //  2. OPACITY-EXACT. The layer was built with per-pixel alpha already
        //     carrying the dose, so multiplying it again would brighten the grain
        //     whenever a repaint had to re-blend the layer, and the texture would
        //     visibly jump between "normal" and "too strong" as frames
        //     alternated. The alpha in the image IS the final amount.
        g.setOpacity (1.0f);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (panelTextureImage,
                     0, 0, getWidth(), getHeight(),
                     0, 0, panelTextureImage.getWidth(), panelTextureImage.getHeight(),
                     false /* fillAlphaChannelWithCurrentBrush: NO - the alpha in
                              the image is the final dose, re-multiplying it with
                              a brush would double the ink */);
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    }

    if (panelTextureWeights.shimmer > 0.0f)
    {
        const auto timeNow = static_cast<float> (juce::Time::getMillisecondCounterHiRes()) * 0.001f;
        // The shimmer drifts across the WHOLE panel: the timer now repaints
        // everything every tick, and the dots' hash chains are keyed to the
        // area they land in, so pinning them to the header+deck union while
        // the grain layer covered the whole editor read as two different
        // materials on one surface.
        const auto area = getLocalBounds().toFloat();
        const auto hashOf = [] (float x)
        {
            return std::fmod (std::abs (std::sin (x) * 43758.5453123f), 1.0f);
        };
        const auto timeStep = std::floor (timeNow * 8.0f);
        const auto drift = timeStep * 0.35f * (1.0f + panelTextureGainReduction);

        for (int i = 0; i < panelShimmerDotCount; ++i)
        {
            const auto fi = static_cast<float> (i);
            const auto u = hashOf (fi * 1.37f + drift);
            const auto v = hashOf (fi * 3.11f + drift * 0.73f + 19.7f);
            const auto a = hashOf (fi * 7.77f - drift * 1.31f + 5.3f);
            const auto bright = a > 0.5f;
            const auto ink = bright ? palette.text : palette.knobEdge;
            const auto alpha = panelTextureWeights.shimmer * (0.35f + 0.65f * std::abs (a - 0.5f) * 2.0f);

            g.setColour (ink.withAlpha (juce::jlimit (0.0f, 1.0f, alpha)));
            g.fillEllipse (area.getX() + u * area.getWidth(),
                           area.getY() + v * area.getHeight(), 1.7f, 1.7f);
        }
    }

    g.setColour (palette.accent.withAlpha (0.85f));
    g.fillRect (layout.header.getX() + 17, layout.header.getY() + 14, 3,
                layout.header.getHeight() - 28);

    g.setColour (palette.border.withAlpha (0.75f));
    // Under the tab bar, not through it: the divider used to sit 42 px down, which is
    // where the first tab button now starts.
    g.drawHorizontalLine (layout.controls.getY() + controlsDividerOffset,
                          static_cast<float> (layout.controls.getX() + 18),
                          static_cast<float> (layout.controls.getRight() - 18));
    g.drawHorizontalLine (layout.meters.getY() + 48,
                          static_cast<float> (layout.meters.getX() + 18),
                          static_cast<float> (layout.meters.getRight() - 18));
    // The deck divider sits one pixel above the panel's bottom edge, BELOW the preset
    // badge band. It used to be eight pixels above the bottom, which put it straight
    // through the middle of the badge text at the minimum editor size.
    g.drawHorizontalLine (layout.deck.getBottom() - 1,
                          static_cast<float> (layout.deck.getX() + 16),
                          static_cast<float> (layout.deck.getRight() - 16));

    // The machine-state badge: a raised pill across the header grid's THIRD row,
    // wrapped around the statusLabel that lives there. The text moved to the
    // deck in an earlier pass but the drawn pill stayed in the header, so the
    // header switches printed over an empty badge plate - the user saw controls
    // on a design that had lost its text. The design now lives with its text
    // again, on a row nothing else occupies: the pill cannot be collided with.
    // The pill is the header grid's own width - four columns and three gaps,
    // less the 18 px the lamp takes - read from the SAME constants resized()
    // places the cells with (see PluginEditor.h). The first pass left a second
    // copy of the arithmetic here with the old 92 px cell in it, so the pill ran
    // 72 px wide of its band and sat off to the left of the statusLabel it wraps.
    // The pill's own rectangle comes from statusPillBounds(), which the timer
    // invalidates: one definition, so the lamp inside it cannot be drawn here and
    // repainted there.
    const auto statusPill = statusPillBounds().toFloat();
    g.setColour (palette.raised.darker (0.16f));
    g.fillRoundedRectangle (statusPill, 4.0f);

    // Status lamp: pulses with the compressor, so the panel shows that the
    // plugin is alive and working even when no gain reduction is happening.
    const auto lampCentre = juce::Point<float> (statusPill.getX() + 15.0f,
                                                statusPill.getCentreY());
    const auto lampPulse = 4.5f + 2.5f * glowAmount;
    g.setColour (palette.status.withAlpha (0.12f + 0.30f * glowAmount));
    g.fillEllipse (lampCentre.x - lampPulse * 2.2f, lampCentre.y - lampPulse * 2.2f,
                   lampPulse * 4.4f, lampPulse * 4.4f);
    g.setColour (palette.status);
    g.fillEllipse (lampCentre.x - 3.5f, lampCentre.y - 3.5f, 7.0f, 7.0f);

    // The neural model picker's card: the moved picker reads as a device with a
    // face, not as three unstyled widgets on the page - "it's black" was the
    // unstyled defaults speaking. The card is drawn behind the picker's member
    // cell only when SHAPERS is the visible tab - the page whose NEURAL knob it
    // belongs to.
    if (activeTabIsShapers)
    {
        const auto card = juce::Rectangle<float> (
            static_cast<float> (gridNow.getX()),
            static_cast<float> (memberBandY),
            static_cast<float> (cellWidthNow),
            static_cast<float> (memberBandHeightNow));
        g.setColour (palette.raised.brighter (0.06f));
        g.fillRoundedRectangle (card, 4.0f);
        g.setColour (palette.border.withAlpha (0.55f));
        g.drawRoundedRectangle (card, 4.0f, 1.0f);
    }

    // A hairline rule under the header strip separates the title block from the
    // controls; it runs the full width again - the badge it used to dodge lives
    // on the grid's third row now.
    g.setColour (palette.border.withAlpha (0.5f));
    g.drawHorizontalLine (layout.header.getBottom() - 4,
                          static_cast<float> (layout.header.getX() + 18),
                          static_cast<float> (layout.header.getRight() - 18));

    // getX()/getRight() return int, so the 9.0f insets are added *after* the cast.
    // `getX() + 9.0f` promotes the int to float implicitly, which is exactly what
    // the int-to-float conversion warnings on macOS flag. Reading the edges into
    // float locals once also keeps the four screw positions consistent with each
    // other instead of each one repeating its own mixed int/float arithmetic.
    const auto headerX = static_cast<float> (layout.header.getX());
    const auto headerRight = static_cast<float> (layout.header.getRight());
    const auto headerY = static_cast<float> (layout.header.getY());
    const auto headerBottom = static_cast<float> (layout.header.getBottom());

    for (const auto point : { juce::Point<float> (headerX + 9.0f, headerY + 9.0f),
                              juce::Point<float> (headerRight - 9.0f, headerY + 9.0f),
                              juce::Point<float> (headerX + 9.0f, headerBottom - 9.0f),
                              juce::Point<float> (headerRight - 9.0f, headerBottom - 9.0f) })
        drawScrew (g, point.x, point.y, palette);

    // The reel, the spokes, the tape ribbon and the orbs are the deck's, and they
    // are the most expensive thing in here: six spokes' worth of cos/sin, a path
    // stroke, and a copy of the particle array out from under its spin lock. They
    // are also the last thing this function draws, so when the clip is the header
    // band there is nothing after them to skip and this can simply stop.
    if (! inClip (layout.deck))
        return;

    // The reel and the ribbon below are the deck's SOFTWARE transport, and they are
    // drawn only when the 3D one is not. tapeScene.isSceneLive() is true only once
    // its context is up AND its shader linked, so a failed context, a driver that
    // will not oblige, and the user turning GL off all land back here - the panel
    // is never left with a hole where the transport was, and never shows two
    // transports at once. The particles further down are NOT gated: they live in
    // the gap the switches row left, a different part of the deck from the 3D
    // transport's window, so the two never overlap.
    //
    // The decorative reel lives in the deck's TOP-RIGHT corner, in the heading band
    // (y + 6..22) where no control sits, and clear of the harmonics readout, which is
    // right-aligned below it on the switches line. It used to sit ON the readout text.
    //
    // The reel is now a real transport indicator rather than decoration: its hub
    // dims as the platter slows, so a STOP empties it and a held SPINDOWN is
    // visible as the ring going grey before the spokes stop.
    if (! tapeScene.isSceneLive())
    {
        const auto reelCentre = juce::Point<float> (static_cast<float> (layout.deck.getRight() - 42),
                                                    static_cast<float> (layout.deck.getY() + 14));
        const auto platterNow = juce::jlimit (0.0f, 1.0f, smoothedMachineSpeed);
        g.setColour (palette.accent.withAlpha (0.10f + 0.30f * platterNow));
        for (const auto radius : { 11.0f, 7.0f, 2.0f })
            g.drawEllipse (reelCentre.x - radius, reelCentre.y - radius,
                           radius * 2.0f, radius * 2.0f, 1.0f);
        g.drawLine (reelCentre.x - 14.0f, reelCentre.y, reelCentre.x + 14.0f, reelCentre.y, 0.8f);
        g.drawLine (reelCentre.x, reelCentre.y - 14.0f, reelCentre.x, reelCentre.y + 14.0f, 0.8f);

        // Spokes: density tracks the selected tape speed, drift tracks the modulation,
        // and the whole set fades out as the platter coasts down - which is what makes a
        // spindown read as a turntable losing its drive rather than as a paused picture.
        const auto spokeAlpha = (0.20f + 0.45f * glowAmount) * (0.25f + 0.75f * platterNow);
        for (int spoke = 0; spoke < 6; ++spoke)
        {
            const auto spokeAngle = reelAngle + static_cast<float> (spoke)
                                                  * juce::MathConstants<float>::pi / 3.0f;
            const auto spokeInner = juce::Point<float> (reelCentre.x + std::cos (spokeAngle) * 2.0f,
                                                        reelCentre.y + std::sin (spokeAngle) * 2.0f);
            const auto spokeOuter = juce::Point<float> (reelCentre.x + std::cos (spokeAngle) * 10.5f,
                                                        reelCentre.y + std::sin (spokeAngle) * 10.5f);
            g.setColour (palette.accent.withAlpha (spokeAlpha));
            g.drawLine (spokeInner.x, spokeInner.y, spokeOuter.x, spokeOuter.y, 1.4f);

            g.setColour (palette.readout.withAlpha (0.10f + 0.25f * glowAmount));
            g.fillEllipse (spokeOuter.x - 1.6f, spokeOuter.y - 1.6f, 3.2f, 3.2f);
        }

        // Tape ribbon from the small reel down the deck's right edge, sagging with the
        // wow/flutter drift. It hangs from the reel at deck.right - 42, so the only row
        // that has to stay clear of it is the model row, and that row stops at the
        // standard right inset (deck.right - 14) - twenty-eight pixels to the right of
        // the ribbon's leftmost point. That is a fact about the ribbon, not a number
        // that has to be kept in step with the row's width by hand.
        const auto sag = (driftAmount - 0.5f) * 5.0f;
        juce::Path tapeRibbon;
        tapeRibbon.startNewSubPath (reelCentre.x, reelCentre.y + 12.0f);
        const auto deckY = static_cast<float> (layout.deck.getY());
        tapeRibbon.quadraticTo (reelCentre.x + 30.0f + sag,
                                deckY + 56.0f,
                                reelCentre.x + 6.0f + sag,
                                deckY + 60.0f);
        g.setColour (palette.knobEdge.withAlpha (0.35f));
        g.strokePath (tapeRibbon, juce::PathStrokeType (1.4f));
    } // ! tapeScene.isSceneLive()

    std::array<RenderOrb, decorativeOrbCount> orbsToDraw;
    {
        const juce::SpinLock::ScopedLockType lock (renderOrbsLock);
        orbsToDraw = renderOrbs;
    }

    // The corridor the switches row left free, which resized() measured rather
    // than assumed - see deckParticleCorridorX. An empty range means the row had
    // no gap wide enough to be worth drawing into (a very narrow window), and
    // the right answer then is no particles at all: they are decoration, and
    // decoration on top of a control is the failure this whole pass is about.
    if (deckParticleCorridorX.getLength() < 20)
        return;

    for (const auto& orb : orbsToDraw)
    {
        const auto x = juce::jmap (orb.position.x, 0.0f, 1.5f,
                                   static_cast<float> (deckParticleCorridorX.getStart()) + 6.0f,
                                   static_cast<float> (deckParticleCorridorX.getEnd()) - 6.0f);
        const auto y = juce::jmap (orb.position.y, 0.0f, 1.2f,
                                   static_cast<float> (deckParticleCorridorY.getStart()),
                                   static_cast<float> (deckParticleCorridorY.getEnd()));
        const auto radius = juce::jmap (orb.radius, 0.045f, 0.057f, 2.0f, 3.5f);

        // Halos breathe so the orbs read as particles rather than static dots.
        const auto haloScale = 1.8f + 0.6f * std::sin (glowPhase + orb.position.x * 6.0f);
        g.setColour (orb.colour.withAlpha (0.07f + 0.05f * glowAmount));
        g.fillEllipse (x - radius * haloScale, y - radius * haloScale,
                       radius * haloScale * 2.0f, radius * haloScale * 2.0f);
        g.setColour (orb.colour.withAlpha (0.17f + 0.14f * glowAmount));
        g.fillEllipse (x - radius, y - radius, radius * 2.0f, radius * 2.0f);
    }
}

void FirstAudioProcessorEditor::createDecorativePhysics()
{
    physicsWorld = std::make_unique<b2World> (b2Vec2 (0.0f, 0.0f));

    b2BodyDef boundaryDefinition;
    auto* boundaryBody = physicsWorld->CreateBody (&boundaryDefinition);

    const auto addWall = [boundaryBody] (float centreX, float centreY,
                                         float halfWidth, float halfHeight)
    {
        b2PolygonShape wallShape;
        wallShape.SetAsBox (halfWidth, halfHeight, b2Vec2 (centreX, centreY), 0.0f);

        b2FixtureDef wallFixture;
        wallFixture.shape = &wallShape;
        wallFixture.friction = 0.05f;
        wallFixture.restitution = 0.82f;
        boundaryBody->CreateFixture (&wallFixture);
    };

    addWall (0.75f, 0.0f, 0.75f, 0.02f);
    addWall (0.75f, 1.2f, 0.75f, 0.02f);
    addWall (0.0f, 0.6f, 0.02f, 0.6f);
    addWall (1.5f, 0.6f, 0.02f, 0.6f);

    for (std::size_t i = 0; i < decorativeOrbCount; ++i)
    {
        const auto index = static_cast<int> (i);
        const auto radius = 0.045f + static_cast<float> (index % 2) * 0.012f;
        const auto position = b2Vec2 (0.30f + static_cast<float> (index) * 0.18f,
                                      0.28f + static_cast<float> (index % 3) * 0.16f);

        b2BodyDef bodyDefinition;
        bodyDefinition.type = b2_dynamicBody;
        bodyDefinition.position = position;
        bodyDefinition.linearVelocity.Set ((index % 2 == 0 ? 1.0f : -1.0f) * 0.34f,
                                           (index % 3 == 0 ? 0.5f : -0.35f) * 0.42f);
        bodyDefinition.linearDamping = 0.04f;
        auto* body = physicsWorld->CreateBody (&bodyDefinition);

        b2CircleShape circleShape;
        circleShape.m_radius = radius;

        b2FixtureDef orbFixture;
        orbFixture.shape = &circleShape;
        orbFixture.density = 0.7f;
        orbFixture.friction = 0.15f;
        orbFixture.restitution = 0.82f;
        body->CreateFixture (&orbFixture);

        const auto colour = paletteFor (false).accent.withAlpha (
            0.25f + static_cast<float> (index) * 0.04f);
        physicsOrbs[i] = { body, radius, colour };
        renderOrbs[i] = { juce::Point<float> (position.x, position.y), radius, colour };
    }
}

//==============================================================================
juce::Rectangle<int> FirstAudioProcessorEditor::statusPillBounds() const
{
    // The machine-state badge: a raised pill across the header grid's THIRD row,
    // wrapped around the statusLabel that lives there. The geometry is the header
    // grid's own constants (see PluginEditor.h), read from the same three numbers
    // resized() places the cells with - and it is written ONCE, here, because the
    // pill contains a lamp that pulses with the compressor: a pill drawn on one
    // rectangle by paint() and invalidated on another by the timer would be a lamp
    // that never moves.
    constexpr int headerGridWidth = headerColumnCount * headerColumnWidth
                                  + (headerColumnCount - 1) * headerColumnGap;
    const auto layout = getEditorLayout();
    const juce::Rectangle<int> pill (layout.header.getRight() - headerGridWidth,
                                     layout.header.getY() + 62,
                                     headerGridWidth - 18, 18);

    // The lamp's halo is an ellipse wider than the pill it sits in (2.2 x the
    // pulse radius on each side), so the invalidated rectangle has to be too -
    // otherwise the glow is clipped at the pill's edges and reads as a hard-edged
    // bar rather than as light.
    return pill.expanded (2, 6);
}

juce::Rectangle<int> FirstAudioProcessorEditor::deckAnimationBounds() const
{
    // The deck's animated picture is drawn by this component rather than by a
    // widget, so it can only be invalidated by REGION:
    //
    //   * the software transport - the small reel at deck.right - 42, deck.y + 14
    //     and the ribbon that falls from it to deck.y + 60. paint() draws it only
    //     while the 3D scene is not live (a driver that will not give us a
    //     context, GL switched off), which is exactly the case in which nothing
    //     else is moving there at all.
    //   * the particles, in the corridor resized() measured from the gap the
    //     switches row actually left (deckParticleCorridorX/Y).
    //
    // Neither rectangle covers a combo box, a slider or a button: the reel hangs
    // in the heading strip's right corner, past the build badge's right inset, and
    // the corridor is the free gap the row reported. That is what keeps this from
    // reintroducing the popup lag a whole-panel repaint caused.
    const auto layout = getEditorLayout();
    juce::Rectangle<int> area (layout.deck.getRight() - 60, layout.deck.getY() + 2, 52, 68);

    if (! deckParticleCorridorX.isEmpty() && ! deckParticleCorridorY.isEmpty())
        area = area.getUnion ({ deckParticleCorridorX.getStart() - 2,
                                deckParticleCorridorY.getStart() - 2,
                                deckParticleCorridorX.getLength() + 4,
                                deckParticleCorridorY.getLength() + 4 });

    return area;
}

void FirstAudioProcessorEditor::repaintAnimatedSurfaces()
{
    // The knobs. Their halos breathe with the compressor and their pointers ride
    // an orbit of the same phase; the ticks tremble with the transport's drift.
    // All of that is read from the look and feel at PAINT time, and a slider only
    // repaints itself when its VALUE changes - so a knob nobody is touching was
    // frozen mid-breath for as long as the panel stayed open.
    for (auto& slider : controls)
        if (slider.isVisible())
            slider.repaint();

    // The switches. Their thumbs GLIDE between stops for about five frames after
    // a click (noteToggle / updateToggleAnimations in the look and feel) and their
    // lamps light with the machine's activity, so they need the same per-frame
    // invalidation the knobs do. Twelve widgets, most of them hidden behind a
    // page: the visible ones are two to four per frame.
    for (auto* toggle : { &deltaButton, &bypassButton, &polarityButton, &autoGainButton,
                          &autoTuneButton, &uiSoundsButton, &antiPhaseGuardButton,
                          &inputEqHpButton, &inputEqLpButton,
                          &outputEqHpButton, &outputEqLpButton, &delaySyncButton })
        if (toggle->isVisible())
            toggle->repaint();

    // The status lamp and the deck's own transport, both drawn by paint().
    repaint (statusPillBounds());

    if (! tapeScene.isSceneLive())
        repaint (deckAnimationBounds());
}

//==============================================================================
void FirstAudioProcessorEditor::timerCallback()
{
    audioProcessor.flushPendingTransportHostSync();

    // Hosts can keep an editor alive while its window is hidden. Do not poll
    // telemetry, advance physics, or issue repaint work in that state; the next
    // visible timer tick reconstructs all display-only state from the processor.
    if (! isShowing())
        return;

    // The startup retry for the OpenGL context, bounded and then forgotten. It runs
    // first so a context that comes up is live before the meters ask for a repaint.
    if (glAttachAttemptsLeft > 0)
    {
        --glAttachAttemptsLeft;

        // Attach retries run while the user has NOT turned GL off by hand. The
        // old guard short-circuited on isAttached() alone, which on a host whose
        // context is killed/recreated (resize, plugin scan, some sandboxed
        // players) left the retry budget dead with GL detached forever - the
        // user's "no shaders, no 3D, ever". glUserDisabled is the switch's own
        // state: only the user's click on GL OFF sets it, so a context the HOST
        // dropped is retried, one the USER dropped is not.
        if (glUserDisabled)
        {
            glAttachAttemptsLeft = 0;
        }
        else if (openGLContext.isAttached())
        {
            glAttachAttemptsLeft = 0;
        }
        else
        {
            openGLContext.attachTo (*this);

            if (openGLContext.isAttached())
            {
                glAttachAttemptsLeft = 0;
                syncGlSwitchState();
            }
        }
    }

    // Four meters: the two level meters each show the full four-way loudness reading
    // (peak dB, RMS, LUFS, VU and their equal-weighted combination), and each glue
    // compressor stage gets its own reduction meter fed from its own telemetry, so the
    // panel shows both what the signal level is doing and where the work is being done.
    //
    // The whole frame comes from ONE call. With readerwriterqueue compiled in, that
    // call drains the lock-free queue and hands back the newest COMPLETE frame the
    // audio thread published, so every number on the panel belongs to the same block
    // of audio - a new output peak can no longer be drawn beside the previous block's
    // RMS. Without the library the call assembles the same struct from the atomics,
    // so this code is identical either way.
    const auto telemetry = audioProcessor.getTelemetry();

    // The neural model readout is cheap to refresh and cheap to check: the
    // status only changes when a model is loaded or cleared, so the label is
    // only rewritten when its text would actually differ. That keeps a 30 Hz
    // timer from re-laying out a label sixty times a second for no reason.
    refreshNeuralStatus();
    // The IR readout follows the same rule - and it is the one place a load
    // that finished on the convolution's background thread still gets drawn,
    // since the load hands its result to the stage asynchronously.
    refreshIrStatus();

    inputMeter.setLoudness (telemetry.inputPeakDb,
                            telemetry.inputRmsDb,
                            telemetry.inputLufs,
                            telemetry.inputVuDb,
                            telemetry.inputCombinedDb,
                            telemetry.inputClipping);
    outputMeter.setLoudness (telemetry.outputPeakDb,
                             telemetry.outputRmsDb,
                             telemetry.outputLufs,
                             telemetry.outputVuDb,
                             telemetry.outputCombinedDb,
                             telemetry.outputClipping);

    compressorMeterIn.setReduction (telemetry.inputGainReductionDb,
                                    telemetry.inputCompressorActivity);
    compressorMeterOut.setReduction (telemetry.outputGainReductionDb,
                                     telemetry.outputCompressorActivity);

    // The MIX page's bar. Read from the seven SOURCE parameters and normalised
    // exactly the way SaturationCore::setWeights normalises them - including its
    // all-zero fallback, so a bar that shows pure tape really is pure tape. It is
    // read only while the page that shows it is on screen: seven parameter reads
    // and a divide per frame is nothing, but nothing is cheaper than nothing.
    if (mixBar.isVisible())
    {
        std::array<float, principleCount> shares {};
        float shareSum = 0.0f;

        for (int p = 0; p < principleCount; ++p)
        {
            if (const auto* parameter = audioProcessor.parameters.getParameter (
                                          juce::String (principleParameterIds[static_cast<std::size_t> (p)])))
                shares[static_cast<std::size_t> (p)] =
                    juce::jlimit (0.0f, 1.0f, static_cast<float> (parameter->getValue()));

            shareSum += shares[static_cast<std::size_t> (p)];
        }

        if (shareSum > 1.0e-6f)
            for (auto& share : shares)
                share /= shareSum;
        else
        {
            // The same answer the core gives an empty mix: pure tape, the machine
            // the plugin is calibrated as. Painting anything else here would put
            // the bar and the sound in front of different stories.
            shares = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        }

        mixBar.setShares (shares, juce::jlimit (0.0f, 1.0f, shareSum));
    }

    const auto reduction = telemetry.inputGainReductionDb + telemetry.outputGainReductionDb;
    const auto activity = telemetry.compressorActivity;

    // Report the harmonic balance the tape stage is producing. Even and odd are shown
    // side by side because the ratio between them is the character: even-dominant reads as
    // warm and full, odd-dominant as hard and edgy, and real tape has both.
    const auto evenRatio = telemetry.evenHarmonicRatio;
    const auto oddRatio = telemetry.oddHarmonicRatio;
    harmonicsReadout.setText ("E " + juce::String (evenRatio * 100.0f, 1) + " %"
                                  + "   O " + juce::String (oddRatio * 100.0f, 1) + " %",
                              juce::dontSendNotification);

    // Colour the readout by which family dominates, so the character is readable at a
    // glance without needing to compare the numbers.
    const auto harmonicPalette = paletteFor (darkTheme);
    const auto totalHarmonics = evenRatio + oddRatio;
    // The readouts breathe with the machine: a gentle luminance oscillation on
    // top of the state colour, so the numbers feel powered by the same signal
    // they report rather than painted on. The breath is bounded (±20 %), so the
    // colour coding stays readable at every phase.
    const auto readoutBreath = 1.0f - 0.2f
                                  * (0.5f + 0.5f * std::sin (customLookAndFeel.phase()));
    const auto breathe = [&] (juce::Colour c)
    {
        return c.withMultipliedBrightness (readoutBreath);
    };
    harmonicsReadout.setColour (juce::Label::textColourId,
                                breathe (totalHarmonics < 0.001f ? harmonicPalette.secondary
                                            : evenRatio >= oddRatio ? harmonicPalette.status
                                                                    : harmonicPalette.needle));

    // The deck's tempo, as the host reports it. A host that offers no playhead
    // (an offline render, a bare player) leaves the readout at the machine's
    // default and says so, because a number that looks measured but is only a
    // fallback is worse than an honest dash.
    const auto deckBpm = audioProcessor.getDeckTempoBpm();
    const auto deckBpmValid = audioProcessor.getDeckTempoValid();
    bpmReadout.setText (deckBpmValid
                            ? juce::String (deckBpm, 1) + " " + xlat ("BPM")
                            : juce::String (juce::roundToInt (deckBpm)) + " "
                                  + xlat ("BPM") + " (" + xlat ("default") + ")",
                        juce::dontSendNotification);
    bpmReadout.setColour (juce::Label::textColourId,
                          breathe (deckBpmValid ? paletteFor (darkTheme).status
                                                : paletteFor (darkTheme).secondary));

    // ------------------------------------------------------------------
    //  Subharmonic tracking readout.
    //
    //  Reports the note the cascade is locked to and how solidly. This is the
    //  one piece of information the panel could not previously give: a depth
    //  knob at 60 % looks identical whether the generator is producing a clean
    //  undertone series or sitting idle because the detector never found a note
    //  to derive one from. The confidence is what distinguishes those two
    //  states, and the tracked frequency says WHICH note it locked to - which is
    //  how a user finds out that a mix is being read an octave low.
    // ------------------------------------------------------------------
    const auto trackedHz = telemetry.subfundTrackedHz;
    const auto confidence = telemetry.subfundConfidence;

    if (confidence < 0.05f || trackedHz <= 0.0f)
    {
        subfundReadout.setText (xlat ("idle - no note tracked"), juce::dontSendNotification);
        subfundReadout.setColour (juce::Label::textColourId, harmonicPalette.secondary);
    }
    else
    {
        // The confidence is shown as a percentage rather than hidden, because a
        // partial lock (say 40 %) is a real and useful state: the cascade IS
        // generating, but it is following a signal that is not steady enough for
        // a clean track, and the user should be able to see that.
        subfundReadout.setText (juce::String (trackedHz, 1) + " Hz   "
                                    + juce::String (juce::roundToInt (confidence * 100.0f)) + " %",
                                juce::dontSendNotification);
        // Green while the track is solid, amber while it is partial: the colour is
        // the readout's own confidence bar.
        subfundReadout.setColour (juce::Label::textColourId,
                                  breathe (confidence > 0.75f ? harmonicPalette.status
                                                              : harmonicPalette.needle));
    }

    // ------------------------------------------------------------------
    //  Anti-phase guard readout.
    //
    //  The guard's correction, as a percentage. "clean" is the healthy state and
    //  is what the panel says almost always; a rising number means the two sides
    //  were found to be in opposition and are being pulled back into agreement.
    //  It is shown rather than hidden because this is the one fault that is
    //  INVISIBLE in stereo: without the readout a user would never know the guard
    //  was doing anything, and with it they can see that a fault was caught.
    //
    //  "off" is the third state and it is not decoration: with the switch on the
    //  SETTINGS page the readout is the ONLY place the deck says whether the
    //  protection is armed, so an idle guard and a disarmed one must never look
    //  the same. The readout is deliberately NOT a button - a guard that can be
    //  switched off by clicking the thing that reports it is one mis-click away
    //  from being off without the user knowing.
    // ------------------------------------------------------------------
    const auto antiPhaseNow = telemetry.antiPhaseAmount;
    const auto* antiPhaseGuardSetting = audioProcessor.parameters
                                            .getRawParameterValue ("anti_phase_guard");
    const auto antiPhaseGuardOn = antiPhaseGuardSetting == nullptr
                                  || antiPhaseGuardSetting->load() >= 0.5f;
    const auto antiPhaseText = ! antiPhaseGuardOn
                                   ? xlat ("off")
                                   : antiPhaseNow < 0.01f
                                       ? xlat ("clean")
                                       : juce::String (juce::roundToInt (antiPhaseNow * 100.0f))
                                             + " " + xlat ("% corrected");
    if (antiPhaseText != lastShownAntiPhase)
    {
        lastShownAntiPhase = antiPhaseText;
        antiPhaseReadout.setText (antiPhaseText, juce::dontSendNotification);
        antiPhaseReadout.setColour (juce::Label::textColourId,
                                    breathe (! antiPhaseGuardOn ? harmonicPalette.secondary
                                            : antiPhaseNow < 0.01f ? harmonicPalette.status
                                                                   : harmonicPalette.needle));
    }

    // Animated presentation state. Everything here is derived from audio
    // telemetry, so the panel visibly reacts to what the plugin is doing.
    glowPhase += 0.13f;
    if (glowPhase > juce::MathConstants<float>::twoPi)
        glowPhase -= juce::MathConstants<float>::twoPi;

    const auto targetGlow = juce::jlimit (0.0f, 1.0f,
                                          activity * 0.7f + std::abs (reduction) / 6.0f);
    glowAmount += (targetGlow - glowAmount) * 0.18f;

    const auto drift = telemetry.transportDrift;
    driftAmount += (drift - driftAmount) * 0.25f;

    // The header status text follows the real bypass state of the processor.
    // Set UNCONDITIONALLY rather than on change: Label::setText early-outs on an
    // identical string, and the unconditional call is what makes a language
    // switch re-translate the readout on the very next frame without waiting
    // for a bypass toggle to change the state first.
    const auto bypassed = telemetry.bypassActive;
    currentBypassDisplay = bypassed;
    statusLabel.setText (bypassed ? xlat ("BYPASSED / DRY") : xlat ("STEREO / REAL TIME"),
                         juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId,
                           bypassed ? paletteFor (darkTheme).secondary
                                    : paletteFor (darkTheme).status);

    // The reels spin at the machine's ACTUAL platter speed, which is the transport
    // ramp multiplied by the spindown ramp - so a START makes them visibly come up
    // to speed and a held SPINDOWN makes them coast down and stop. That is the
    // whole point of publishing getTransportRamp(): before this the reels only knew
    // the SPEED combo, so the transport did not move them at all.
    // getSelectedId() returns 0 while nothing is selected, which would index the
    // combo's item list at -1, so the index is clamped before it is used.
    const auto speedIndex = juce::jlimit (0, 2, speedBox.getSelectedId() - 1);
    const auto speedScale = speedIndex == 0 ? 0.55f : (speedIndex == 1 ? 0.85f : 1.25f);

    // Follow the machine's own platter with a display-only lag, so the reels ease
    // into a spindown rather than snapping when a block boundary lands.
    const auto machineSpeed = telemetry.transportRamp;
    smoothedMachineSpeed += (machineSpeed - smoothedMachineSpeed) * 0.20f;

    const auto targetReelSpeed = speedScale * (0.9f + driftAmount * 0.5f) * smoothedMachineSpeed;
    reelSpeed += (targetReelSpeed - reelSpeed) * 0.08f;
    reelAngle += reelSpeed * 0.09f;
    if (reelAngle > juce::MathConstants<float>::twoPi)
        reelAngle -= juce::MathConstants<float>::twoPi;

    // The spindown lamp follows both the parameter (so automation lights it) and
    // the button's own mouse state (so the lamp is instant on a press, before the
    // 30 Hz poll would see the parameter change).
    const auto transportNow = currentTransportState();
    const auto spindownNow = audioProcessor.isSpindownHeld()
                              || spindownButton.isDown();

    // The three transport keys relight only when the state actually changes.
    if (transportNow != lastShownTransport)
    {
        lastShownTransport = transportNow;
        styleTransportButtons();
    }

    if (spindownNow != lastShownSpindown)
    {
        lastShownSpindown = spindownNow;
        styleSpindownButton();
    }

    // The deck hint reads the machine's actual state rather than a fixed caption,
    // so the panel answers "what is the transport doing" in words as well as with
    // the key lamps and the reels. The five states are the ones the engine can be
    // in, and they are read in this order: the spindown takes priority because it
    // is the momentary one, and the ARM comes next because a START that is waiting
    // for the DAW reads as START in the parameter (that is what keeps the key lit)
    // and is only distinguishable from a spin-up by asking the engine.
    const auto waitingForHost = audioProcessor.isTransportArmedForHost();
    const auto machineStateText = spindownNow ? "SPINDOWN / POWER CUT"
                                    : waitingForHost ? "START / WAITING FOR DAW"
                                    : transportNow == 0 ? "STOPPED / AT REST"
                                    : transportNow == 2 ? "START / SPINNING UP"
                                                        : "PLAY / AT SPEED";
    // Explicit juce::String construction: comparing const char* against a
    // juce::String with != is ambiguous (candidates on both sides), so the
    // left operand is materialised first.
    // The mode caption rides the same refresh: any path that flips a mode
    // (session load, undo, preset, automation) shows up within one frame.
    refreshModeButtonCaption();

    if (juce::String (machineStateText) != lastShownMachineState)
    {
        lastShownMachineState = machineStateText;
        deckHintLabel.setText (machineStateText, juce::dontSendNotification);
        deckHintLabel.setColour (juce::Label::textColourId,
                                 spindownNow || waitingForHost ? paletteFor (darkTheme).needle
                                 : transportNow == 0 ? paletteFor (darkTheme).secondary
                                                     : paletteFor (darkTheme).status);
    }

    customLookAndFeel.setActivity (glowAmount);
    customLookAndFeel.setDrift (driftAmount);
    customLookAndFeel.advanceFrame();
    // The toggle thumbs' glide is one per-frame ease for every tracked switch
    // (see J37LookAndFeel::updateToggleAnimations); run it on the same tick as
    // the panel's other animation so the whole surface shares one clock.
    customLookAndFeel.updateToggleAnimations();

    // ------------------------------------------------------------------
    //  Interface sounds.
    //
    //  The enable flag follows the parameter, so the switch on the SETTINGS
    //  tab (and an automation lane, and a preset) all reach it by the same
    //  route. The gate counter is decremented once per frame here rather than
    //  being a wall-clock timestamp, which keeps the knob detent's rate tied to
    //  the panel's own frame rate instead of the system clock.
    //
    //  The brightness the sounds are pitched from is the machine's own activity
    //  - the same value the knob halos breathe with - so a machine doing work
    //  ticks at a slightly higher, tighter pitch than an idle one. That is the
    //  one refinement that makes the sounds belong to THIS plugin rather than
    //  being a generic UI beep.
    // ------------------------------------------------------------------
    if (uiSoundTickCountdown > 0)
        --uiSoundTickCountdown;

    const auto uiSoundEnabled = audioProcessor.getUiSoundsEnabled();
    if (uiSoundEnabled != lastShownUiSoundEnabled)
    {
        lastShownUiSoundEnabled = uiSoundEnabled;
        uiSounds.setEnabled (uiSoundEnabled);
    }

    uiSounds.setBrightness (juce::jlimit (0.0f, 1.0f, glowAmount * 0.7f + activity * 0.3f));

    // The texture layer is rebuilt (and the whole panel repainted) only when
    // something STRUCTURAL changes - a theme switch, a resize - through
    // rebuildPanelTextureLayers(); the timer used to repaint the whole editor
    // every tick besides, and that was the lists' lag: with a popup menu open
    // (presets, vinyl types, oversampling - every combo), each 30 Hz tick
    // buried the popup's window under a full-panel invalidation and the menu
    // repainted itself behind it, which the eye read as the list dragging.
    // The knobs need none of it - they repaint themselves when their own value
    // changes - and the animated furniture (lamp, needles, hint bar) rides the
    // look-and-feel's own frame advance plus its components' own repaints.

    // The Arturia-style hint line: the controls band's right caption names the
    // knob the pointer is over (the slider's own name - the same caption the
    // grid draws under it), or falls back to the interaction hint when the
    // pointer rests on nothing. A tooltip needs hover-and-wait; this bar
    // answers in one frame, which is why the two coexist. The name is read
    // from the component rather than from parameterTooltipText: the bar wants
    // the knob's own caption - the short name the grid already draws under it,
    // and the same one the host reports - while the tooltip's text is the
    // sentence that explains the control, which is the tooltip's job.
    {
        juce::String hoveredControl;
        const auto mouse = getMouseXYRelative();
        for (int i = 0; i < static_cast<int> (controlCount); ++i)
            if (controls[i].isVisible() && controls[i].getBounds().contains (mouse))
            {
                hoveredControl = controls[i].getName();
                break;
            }
        if (hoveredControl != lastShownHoverHint)
        {
            lastShownHoverHint = hoveredControl;
            controlsHintLabel.setText (hoveredControl.isNotEmpty()
                                          ? hoveredControl
                                          : juce::String ("Shift = fine tune"),
                                       juce::dontSendNotification);
        }
    }

    // Workflow state is cheap to poll at 30 Hz and makes the panel self-healing:
    // if the host, a session load or a preset change moves anything, the buttons,
    // badge and preset display catch up on the next frame instead of lying.
    const auto canUndoNow = audioProcessor.getUndoManager().canUndo();
    const auto canRedoNow = audioProcessor.getUndoManager().canRedo();
    const auto slotNow = audioProcessor.getActiveCompareSlot();
    const auto dirtyNow = audioProcessor.isCompareDirty();
    const auto presetNow = audioProcessor.getLastPresetIndex();
    if (slotNow != lastShownSlot || dirtyNow != lastShownDirty
        || canUndoNow != lastShownCanUndo || canRedoNow != lastShownCanRedo)
    {
        lastShownSlot = slotNow;
        lastShownDirty = dirtyNow;
        lastShownCanUndo = canUndoNow;
        lastShownCanRedo = canRedoNow;
        updateWorkflowButtons();
    }
    if (presetNow != lastShownPreset)
    {
        lastShownPreset = presetNow;
        // Item IDs are index + 1, so 0 means "no selection" - what a user preset
        // (index -1) should show in the FACTORY combo.
        presetBox.setSelectedId (presetNow + 1, juce::dontSendNotification);
        refreshUserPresetList(); // a factory load clears the user-preset selection
    }
    // The box's SELECTED ENTRY follows the loaded preset, but a preset that is
    // loaded-and-then-edited keeps its entry while the machine drifts; that is
    // correct (the badge says EDITED) and needs no per-frame re-selection here.

    const auto userPresetNow = audioProcessor.getCurrentPresetName();
    const auto presetDirtyNow = audioProcessor.isPresetDirty();
    if (userPresetNow != lastShownUserPreset || presetDirtyNow != lastShownPresetDirty)
    {
        lastShownUserPreset = userPresetNow;
        lastShownPresetDirty = presetDirtyNow;
        updateWorkflowButtons();
    }

    // While the user turns knobs, the live state drifts away from the stored side, so
    // the active A/B slot is re-mirrored periodically and COPY A / COPY B always capture
    // the machine as it is right now. This used to run on EVERY timer frame: each call
    // deep-copies the whole parameter tree twice, so at 30 Hz that was sixty whole-tree
    // copies a second landing on the message thread in the middle of a knob drag - on top
    // of the repaint work. That is exactly when the GUI thread is busiest and the audio
    // thread has least headroom, and the resulting dropouts are heard as clicks. It now
    // runs at ~5 Hz and only while the editor is actually on screen, which is far faster
    // than a person can hear the EDITED badge appear.
    if (isShowing() && ++compareMirrorTick >= 6)
    {
        compareMirrorTick = 0;
        audioProcessor.updateActiveCompareSlot();
    }

    if (physicsWorld == nullptr)
        return;

    physicsWorld->Step (1.0f / 30.0f, 6, 2);

    std::array<RenderOrb, decorativeOrbCount> nextFrame;
    for (std::size_t i = 0; i < decorativeOrbCount; ++i)
    {
        const auto position = physicsOrbs[i].body->GetPosition();
        nextFrame[i] = { juce::Point<float> (position.x, position.y),
                         physicsOrbs[i].radius,
                         physicsOrbs[i].colour };
    }

    {
        const juce::SpinLock::ScopedLockType lock (renderOrbsLock);
        renderOrbs = nextFrame;
    }

    // Finally, the 3D transport. It is fed last so every number it sees is the
    // one this tick settled on, and it is fed FIVE PLAIN FLOATS and nothing else:
    // the scene owns a second OpenGL context on its own render thread, and the
    // one thing that must never happen is a parameter tree or a JUCE Component
    // being read from there. Each is normalised here, in the thread that owns the
    // parameter, so the render thread has no idea what a dB is.
    if (const auto* rawDrive = audioProcessor.parameters.getRawParameterValue ("drive"))
        sceneDrive = rawDrive->load();

    tapeScene.setAudioState (
        // Output loudness over the range the meters themselves use for "moving":
        // below -48 dB the machine is at the noise floor and the scene goes quiet.
        juce::jlimit (0.0f, 1.0f, juce::jmap (telemetry.outputCombinedDb, -48.0f, 0.0f, 0.0f, 1.0f)),
        juce::jlimit (0.0f, 1.0f, sceneDrive),
        // Both compressors' total reduction, on the same six-decibel scale the
        // panel's glow uses, so the reels' rim light and the knobs agree.
        juce::jlimit (0.0f, 1.0f, std::abs (reduction) / 6.0f),
        // The transport's drift, re-centred: the panel carries it as 0..1 with
        // 0.5 as steady, and the scene wants it signed so the ribbon waves to
        // either side of the path rather than always to one.
        juce::jlimit (-1.0f, 1.0f, (driftAmount - 0.5f) * 2.0f),
        // The machine's own platter speed, already lagged in smoothedMachineSpeed
        // - so a STOP stops the reels and a held SPINDOWN coasts them down, and
        // the tape only transfers while the transport is actually running.
        juce::jlimit (0.0f, 1.0f, smoothedMachineSpeed),
        // The frame's own loudest sample, linear 0..1, drained from the processor
        // once per tick (getOutputPeakLevel is an exchange) so the scene's peak
        // lamp reads the same transient the meter needle just fell for.
        juce::jlimit (0.0f, 1.0f, audioProcessor.getOutputPeakLevel()));

    // The texture's doses follow the machine: the shimmer term scales with
    // drive and with the compressors' reduction, so the panel's surface is
    // alive where the signal pushes. One call feeds both renderers - the 2D
    // ink in paint() and the scene's uTextureWeights uniform.
    configureTextureWeights (sceneDrive,
                             juce::jlimit (0.0f, 1.0f, std::abs (reduction) / 6.0f));

    // The scene's own attach retries, on the same timer and the same bounded
    // budget as this editor's context: a child component cannot have a context
    // until the host has given it a native peer, and that may not have happened
    // on the first tick.
    tapeScene.serviceContextAttachment();

    // ------------------------------------------------------------------
    //  The frame's animations, invalidated.
    //
    //  Everything above this line ADVANCES the animation - advanceFrame() moved
    //  the look and feel's phase, updateToggleAnimations() eased every switch's
    //  thumb, glowAmount and driftAmount followed the machine - and NONE of it
    //  draws anything by itself. A repaint is what turns those numbers into
    //  motion, and the timer deliberately stopped repainting the panel every tick
    //  (see the note about the combo popups above), which is exactly what the
    //  user saw as "the animations render once and then never move": the panel
    //  was a still frame of a live machine.
    //
    //  So the invalidations are targeted rather than global - a bounded set of
    //  rectangles that carries all of the animated content and none of the
    //  combos, sliders and popups the old whole-panel repaint used to stamp over.
    //  See repaintAnimatedSurfaces().
    // ------------------------------------------------------------------
    if (++animationPaintTick >= 2)
    {
        animationPaintTick = 0;
        repaintAnimatedSurfaces();
    }
}

void FirstAudioProcessorEditor::resized()
{
    // The static texture layers (grain + wear) are sized to the window, so a
    // resize rebuilds them. Cheap enough at half resolution to pay on every
    // drag of the window's edge, and it keeps the texture from ever being
    // stretched over a size it was not built for.
    rebuildPanelTextureLayers();

    const auto layout = getEditorLayout();

    // Header captions: the small brand strip sits ABOVE the big title (they used to
    // share the same band and printed over each other), the subtitle starts right of
    // the title box, and all three stay clear of the theme button / status badge.
    brandLabel.setBounds (layout.header.getX() + 26, layout.header.getY() + 8, 120, 12);
    titleLabel.setBounds (layout.header.getX() + 25, layout.header.getY() + 24, 96, 46);
    subtitleLabel.setBounds (layout.header.getX() + 126, layout.header.getY() + 46, 220, 18);
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
    //  THE GRID. Five switches used to sit here in two rows of per-button
    //  widths (74 / 92 / 96 / 100 / 181), cursor-placed: no two buttons the
    //  same width, their columns never lining up, which is exactly the
    //  "arranged at random" look. The header is now ONE GRID - four equal
    //  columns, three rows - and every switch is one cell. Rows read left to
    //  right in the order a user scans them; nothing overlaps by construction
    //  (cells cannot intersect) and nothing drifts (no elastic gaps to grow).
    //
    //  Row three is the machine-state readout's own band. The first grid had
    //  two rows and put them into the drawn badge's plate (right - 205, +20,
    //  30 px tall) - switches ON the pill, which is what the user reported as
    //  "overlapping the Real-Time/Stereo caption AND its design". The badge
    //  lives on the grid's third row now: full grid width, 18 px, pill painted
    //  under it in paint(), lamp at its left end, nothing else ever placed
    //  there.
    //
    //  Clearance at the 780 px minimum: the grid is 314 px wide (4 x 74 + 3 x 6)
    //  and starts at header.right - 314 = x + 438 on a 780 panel, clearing the
    //  title block (ends x + 346) by 92 px at the tightest size and more above it.
    // ------------------------------------------------------------------------------
    const auto headerRight = layout.header.getRight();
    // The grid's own geometry - the column count, the cell width and the gap -
    // is declared ON THE CLASS (see PluginEditor.h), because paint() draws the
    // status pill on the same arithmetic and a second copy of it is exactly how
    // the pill kept the old 92 px cell and overhung its own band by 72 px.
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
    const auto headerRowOneY = layout.header.getY() + 10;
    const auto headerRowTwoY = layout.header.getY() + 36;
    const auto headerRowThreeY = layout.header.getY() + 62;
    constexpr int headerStatusHeight = 18;
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

    // Row one: the four engine switches, equal cells, one grid.
    headerCell (deltaButton,     0, headerRowOneY, headerSwitchHeight);
    headerCell (bypassButton,    1, headerRowOneY, headerSwitchHeight);
    headerCell (polarityButton,  2, headerRowOneY, headerSwitchHeight);
    headerCell (autoGainButton,  3, headerRowOneY, headerSwitchHeight);

    // Row two: THEME, then the language pair - caption cell + a box that spans
    // the last two cells.
    headerCell (themeButton,     0, headerRowTwoY, headerSwitchHeight);
    headerCell (languageLabel,   1, headerRowTwoY, headerSwitchHeight);
    languageLabel.setJustificationType (juce::Justification::centredRight);
    languageBox.setBounds (headerGridLeft + 2 * (headerColumnWidth + headerGap),
                           headerRowTwoY,
                           2 * headerColumnWidth + headerGap,
                           headerSwitchHeight);

    // Row three is the machine-state readout's OWN band, full grid width minus
    // the header's corner screw. The
    // first grid put row-two switches into the drawn badge's 30 px-tall plate
    // (right - 205, y + 20) - switches ON the pill, the user's complaint. A
    // two-row grid has no cell a 30 px badge fits in, so the header grew a
    // third row: the pill returns to the header as a strip under the
    // switches, painted in paint() exactly on these bounds, lamp at its left
    // end, text after it. BYPASS ON swaps the text to BYPASSED / DRY from the
    // telemetry timer; nothing else is ever placed on this row, so the pill
    // cannot be collided with by construction.
    statusLabel.setBounds (headerGridLeft, headerRowThreeY,
                           headerColumns * headerColumnWidth
                               + (headerColumns - 1) * headerGap - 18,
                           headerStatusHeight);
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setFont (statusLabel.getFont().withHeight (9.0f));
    statusLabel.setBorderSize (juce::BorderSize<int> (0, 26, 0, 0));

    // ------------------------------------------------------------------
    //  The deck.
    //
    //  Six bands, none of them shared with another:
    //   y +   6  the DECK heading and the build id
    //   y +  30  MODEL     tape type | speed | BPM
    //   y +  74  SWITCHES  oversampling | instrument | GL | HARMONICS | SUB
    //              FUND | ANTI PHASE
    //   y + 116  the six type switches, their captions above their boxes
    //   y + 183  TRANSPORT keys, the machine state, the mode switch
    //   y + 217  the preset workflow chain
    //   y + 253  the preset badge band          (the deck is 278 px)
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
    //  Six bands at hardcoded offsets inside a rectangle of exactly 278 px is
    //  why the deck could not be made shorter: the deck is now elastic (see
    //  getEditorLayout), and these offsets are what has to follow it. They are
    //  written as a TABLE of bands - each one's own height - and only the GAPS
    //  between them compress. A band's height is never touched, so a control
    //  cannot be squeezed by a shorter window; a gap is what gives way, and it
    //  is floored, so two bands can never end up touching however short the
    //  deck gets.
    //
    //  At the reference height the scale is exactly 1 and every offset below is
    //  the number that is written in the table - so this is a refactor with no
    //  visual effect at the size the window opens at, which is the only way to
    //  be sure of a change made without being able to look at it.
    // ------------------------------------------------------------------
    struct DeckBand
    {
        int top;
        int height;
    };

    constexpr DeckBand referenceBands[] = {
        {   6, 16 },   // the heading strip: "Deck" and the build id
        {  30, 32 },   // MODEL
        {  74, 32 },   // SWITCHES
        { 116, 45 },   // the six type selectors, caption over box
        { 170, 45 },   // TRANSPORT: caption band, then the keys
        { 217, 32 },   // the preset chain
        { 253, 14 }    // the badge line
    };

    constexpr int deckReferenceHeight = 278;
    constexpr int minimumBandGap = 2;
    constexpr auto bandCount = static_cast<int> (juce::numElementsInArray (referenceBands));

    // The working copy is what the compression below rewrites; the table above
    // stays the written-down reference for the size the panel was drawn at.
    DeckBand deckBands[bandCount];

    for (int i = 0; i < bandCount; ++i)
        deckBands[i] = referenceBands[i];

    {
        // The gaps between the bands as the table states them, and then the gaps
        // they are allowed once the deck is as short as it can get.
        int gap[bandCount - 1] {};
        int gapTotal = 0;

        for (int i = 1; i < bandCount; ++i)
        {
            gap[i - 1] = referenceBands[i].top
                       - (referenceBands[i - 1].top + referenceBands[i - 1].height);
            gapTotal += gap[i - 1];
        }

        const auto shrink = juce::jmax (0, deckReferenceHeight - layout.deck.getHeight());
        const auto wantedGapTotal = juce::jmax ((bandCount - 1) * minimumBandGap, gapTotal - shrink);
        const auto gapScale = gapTotal > 0 ? static_cast<float> (wantedGapTotal)
                                           / static_cast<float> (gapTotal)
                                           : 0.0f;

        for (int i = 1; i < bandCount; ++i)
            deckBands[i].top = deckBands[i - 1].top + deckBands[i - 1].height
                             + juce::jmax (minimumBandGap,
                                           juce::roundToInt (gap[i - 1] * gapScale));
    }

    // [&] and not [&layout]: the band table is a local array, and a lambda that
    // names one thing in its capture list captures nothing else - so a narrow
    // capture silently leaves deckBands uncaptured.
    const auto deckTop = [&] (int band) { return layout.deck.getY() + deckBands[band].top; };

    deckHeadingLabel.setBounds (layout.deck.getX() + 18, deckTop (0), 150, 16);

    // The build id rides the deck's heading strip, right-aligned and stopping short of
    // the reel (whose left edge is deck.right - 56). This strip is the only full-width
    // band near the top of the panel that is guaranteed empty at the 780 px minimum:
    // the header's equivalent gap shrinks to about 58 px there, and the switches row
    // is reserved for the drifting particles.
    buildLabel.setBounds (layout.deck.getX() + 190, deckTop (0),
                          layout.deck.getWidth() - 280, 16);

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
    //  The row returns the widest gap it left, so the drifting particles are
    //  told where the free space actually is instead of being told a pair of
    //  coordinates that was only ever right at one width.
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
        const std::vector<DeckRowItem> row {
            { &speedLabel,       108, -1,  0, 15, false },
            { &speedBox,           0, -1, 15, 30, true  },
            { &vinylSpeedLabel,  108, -1,  0, 15, false },
            { &vinylSpeedBox,      0, -1, 15, 30, true  },
            { &bpmLabel,         108, -1,  0, 15, false },
            { &bpmReadout,         0, -1, 15, 13, true  }
        };

        placeDeckRow (row, layout.deck.getX() + 18, layout.deck.getRight() - reelCorridor,
                      deckTop (1), 14);
    }

    // ---- row 2: SWITCHES ----------------------------------------------------
    // OVERSAMPLING and GL are NOT in this row: they are SETTINGS-tab members and
    // live in the member row under that tab's knobs (placeDeckSwitch below).
    // They used to sit here on EVERY tab while being visible only on SETTINGS -
    // so five tabs out of six showed two unexplained holes in this line.
    //
    //  Four equal 108 px columns, the type row's grid again: INSTRUMENT, then
    //  the three readout pairs - caption above value, one column each, all
    //  standing exactly above the type row's columns.
    {
        const std::vector<DeckRowItem> row {
            { &instrumentLabel,  108, -1,  0, 15, false },
            { &instrumentBox,      0, -1, 15, 30, true  },

            { &harmonicsLabel,   108, -1,  0, 15, false },
            { &harmonicsReadout,   0, -1, 15, 13, true  },
            { &subfundLabel,     108, -1,  0, 15, false },
            { &subfundReadout,     0, -1, 15, 13, true  },
            { &antiPhaseLabel,   108, -1,  0, 15, false },
            { &antiPhaseReadout,   0, -1, 15, 13, true  }
        };

        const auto freeGap = placeDeckRow (row, layout.deck.getX() + 18,
                                           layout.deck.getRight() - deckRightInset,
                                           deckTop (2), 12);

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

        // Where the particles may drift: the gap this row actually left. Under
        // twenty pixels there is nowhere free to put them, and drawing them
        // anyway would put decoration on top of a control - the one thing this
        // whole pass exists to stop. paint() skips them when this is empty.
        deckParticleCorridorX = freeGap.getLength() >= 20 ? freeGap
                                                          : juce::Range<int> (0, 0);
        deckParticleCorridorY = juce::Range<int> (deckTop (2) + 2, deckTop (2) + 30);
    }

    // ---- row 3: the six type selectors --------------------------------------
    {
        // The model selector (renamed TAPE, its old caption read MODEL) joined
        // the row of type selectors - the user read MODEL | SPEED | BPM as
        // three unrelated things on one line, when the selector is a TYPE like
        // VINYL or VALVE and belongs with them. Six columns as before, so the
        // row arithmetic and the 108 px column are unchanged.
        //
        // 108 per column, not 100: the longest stock values ("Fender Blackface",
        // "Jensen JT-11P") ellipsised in the deck's own combo drawing at the old
        // width on every panel under ~1400 px. 108 is the largest the row holds at
        // the 780 px minimum ((720 - 5 x 14 gaps) / 6 = 108), and above that width
        // the row's elastic gap shrinks before any column does.
        const std::vector<DeckRowItem> row {
            { &tapeTypeLabel,        108, -1,  0, 15, false },
            { &tapeTypeBox,            0, -1, 15, 30, true  },
            { &vinylTypeLabel,       108, -1,  0, 15, false },
            { &vinylTypeBox,           0, -1, 15, 30, true  },
            { &valveTypeLabel,       108, -1,  0, 15, false },
            { &valveTypeBox,           0, -1, 15, 30, true  },
            { &ampTypeLabel,         108, -1,  0, 15, false },
            { &ampTypeBox,             0, -1, 15, 30, true  },
            { &transformerTypeLabel, 108, -1,  0, 15, false },
            { &transformerTypeBox,     0, -1, 15, 30, true  },
            { &digitalTypeLabel,     108, -1,  0, 15, false },
            { &digitalTypeBox,         0, -1, 15, 30, true  }
        };

        placeDeckRow (row, layout.deck.getX() + 18, layout.deck.getRight() - deckRightInset,
                      deckTop (3), 14);
    }

    // ---- row 4: TRANSPORT ---------------------------------------------------
    {
        const std::vector<DeckRowItem> row {
            { &transportStopButton,   62,  4,  0, 28, false },
            { &transportPlayButton,   62,  4,  0, 28, false },
            { &transportStartButton,  62,  4,  0, 28, false },
            { &spindownButton,        74, -1,  0, 28, false },
            { &deckHintLabel,        190, -1,  2, 24, false },
            { &modeCycleButton,      150, -1,  0, 32, false }
        };

        const auto transportRowY = deckTop (4) + 13;

        placeDeckRow (row, layout.deck.getX() + 18, layout.deck.getRight() - deckRightInset,
                      transportRowY, 14);

        // The two transport captions sit ABOVE the key bank rather than beside
        // their own key, so they cannot be a column of the row - they are placed
        // from the buttons the row just gave them. Reading the placed bounds
        // back is what keeps them correct at every width: written as
        // `keyWidth * 3 + gap * 2` they would name a span three keys wider than
        // the bank actually is once the row starts growing them.
        transportLabel.setBounds (transportStopButton.getX(), transportRowY - 13,
                                  transportStartButton.getRight() - transportStopButton.getX(), 12);
        spindownLabel.setBounds (spindownButton.getX(), transportRowY - 13,
                                 spindownButton.getWidth(), 12);

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
    const auto presetRowY = deckTop (5);
    const auto presetRowHeight = 32;
    constexpr int presetRowGap = 5;

    const auto placeForward = [presetRowY, presetRowHeight, presetRowGap]
                                  (int& edge, juce::Component& component, int width)
    {
        component.setBounds (edge, presetRowY, width, presetRowHeight);
        edge += width + presetRowGap;
    };

    const auto placeBackward = [presetRowY, presetRowHeight, presetRowGap]
                                   (int& edge, juce::Component& component, int width)
    {
        component.setBounds (edge - width, presetRowY, width, presetRowHeight);
        edge -= width + presetRowGap;
    };

    presetHeadingLabel.setBounds (layout.deck.getX() + 18, deckTop (5) + 9, 46, 16);

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
        const auto chainSpan = (layout.deck.getRight() - 14)
                                   - (layout.deck.getX() + 66);
        const auto chainVoid = juce::jmax (0, chainSpan - (headWidths + tailWidths
                                                              + 9 * presetRowGap));
        const auto presetGrowth = juce::jmin (chainVoid * 3 / 5, 160);
        const auto userGrowth   = juce::jmin (chainVoid - presetGrowth, 120);

        int headEdge = layout.deck.getX() + 66;
        placeForward (headEdge, presetBox,          presetBoxWidth  + presetGrowth);
        placeForward (headEdge, userPresetBox,      userPresetWidth + userGrowth);
        placeForward (headEdge, savePresetButton,  40);
        placeForward (headEdge, deletePresetButton, 38);
    }

    {
        // Tail, from the right: the badge, REDO, UNDO, COMPARE, A, B.
        int tailEdge = layout.deck.getRight() - 14;
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
    presetBadgeLabel.setBounds (layout.deck.getX() + 22, deckTop (6),
                                layout.deck.getWidth() - 44, 14);
    presetBadgeLabel.setFont (shrinkingFont (presetBadgeLabel.getText(), 8.0f,
                                             juce::Font::plain,
                                             static_cast<float> (presetBadgeLabel.getWidth()) - 4.0f));


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

    // The 3D transport's window: the deck's top-right block, which is the one
    // region resized() keeps free of controls. reelCorridor is 70 px wide and the
    // heading band starts six px down, so the scene gets a 64 x 58 block that is
    // the same size at every panel size - which is what lets the scene be laid out
    // in its own units and framed by the camera rather than by a pixel count.
    // It stops 8 px short of the switches row below it, so the two never touch.
    tapeScene.setBounds (layout.deck.getRight() - reelCorridor,
                         layout.deck.getY() + 6,
                         reelCorridor - 6,
                         58);

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
    const auto tabBarLeft = layout.controls.getX() + 14;
    // The two rows of tabs-inside-tabs live between the heading and the divider:
    // FAMILIES on top (the three large buttons), the active family's PAGES under
    // them. The divider (controlsDividerOffset) sits below both, so the knob grid
    // itself never pays a row for either strip.
    constexpr int familyRowHeight = 22;
    const auto familyRow = juce::Rectangle<int> (layout.controls.getX() + 14,
                                                 layout.controls.getY() + 34,
                                                 layout.controls.getWidth() - 28,
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

    const auto tabBarTop = layout.controls.getY() + 60;
    const auto tabBarHeight = 24;

    // Pages of the CURRENT family only: the bar re-flows to the family's page
    // count, so TAPE's five buttons get a fifth more width than the old ten-in-a-
    // row bar ever could, and SETUP's three get three broad ones.
    int visibleTabCount = 0;
    for (int tab = 0; tab < numTabs; ++tab)
        if (tabSpecs[static_cast<std::size_t> (tab)].family == currentTabFamily)
            ++visibleTabCount;
    visibleTabCount = juce::jmax (1, visibleTabCount);
    const auto tabGap = visibleTabCount <= 5 ? 6 : 4;
    const auto tabButtonWidth = (layout.controls.getWidth() - 28
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
    metersHeadingLabel.setBounds (layout.meters.getX() + 16, layout.meters.getY() + 8, 130, 18);
    metersHintLabel.setBounds (layout.meters.getX() + 16, layout.meters.getY() + 27, 150, 14);
    const auto compressorLabelWidth = 120;
    compressorLabel.setBounds (layout.meters.getRight() - compressorLabelWidth - 14,
                               layout.meters.getY() + 8, compressorLabelWidth, 18);
    compressorReadout.setBounds (layout.meters.getRight() - compressorLabelWidth - 14,
                                 layout.meters.getY() + 27, compressorLabelWidth, 14);

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
    const auto shapersTab = index_of_tab_named ("SHAPERS") == currentTab;
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
    // the panel - GL and OVERSAMPLING were clipped by the window's bottom edge on
    // the very tab that holds nothing else.
    //
    // The MIX page counts as a member page even though it owns no switch: the
    // band it reserves is where the saturation mix bar lives, which is the whole
    // reason the page is worth its own strip. FRONT END counts because its DI PAD
    // is a combo - a member - and can no longer ride the knob grid: a page whose
    // knobs fill their row has no free cell to put it in (it used to take the
    // fourth cell of a second row that had three knobs in it). DYN does NOT: its
    // knobs moved to CHARACTER and nothing ever rode the band it reserved, so the
    // page now gives that 50 px to its own three knobs instead.
    const auto memberRows = (settingsTab || delayTab || characterTab
                             || frontEndTab || shapersTab || inEqTab || outEqTab
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
    const auto placeDeckSwitch = [&] (juce::Label& label, juce::Component& box,
                                      int column, int columnsWide,
                                      const juce::String& caption)
    {
        auto cell = juce::Rectangle<int> (grid.getX() + column * cellWidth,
                                          memberRowY,
                                          columnsWide == tabColumns
                                              ? grid.getRight() - (grid.getX() + column * cellWidth)
                                              : columnsWide * cellWidth
                                                  + (columnsWide - 1) * (cellWidth / columnsWide),
                                          grid.getBottom() - memberRowY);
        // This setText() every layout is also why the GL label's constructor
        // emptiness is safe: the caption is re-written here every time its tab is
        // live, so a resize between tab switches can never leave a stale one.
        label.setText (caption, juce::dontSendNotification);
        label.setBounds (cell.getX() + 5, cell.getY() + 1, cell.getWidth() - 10, 17);
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
        // that threw GL and OVERSAMPLING 168 px below their own captions while
        // UI SOUNDS, which is placed directly at +20, stayed at the top: one
        // page, two layouts. With the nominal band they sit under their captions
        // on every page, and the arithmetic is unchanged everywhere the band is
        // 50 px or a few pixels taller.
        const auto centringBand = juce::jmin (cell.getHeight(), 50);
        const auto bandY = cell.getY() + 20
                           + juce::jmax (0, (centringBand - 22 - bandHeight) / 2);
        box.setBounds (cell.getX() + 12, bandY,
                       cell.getWidth() - 24, bandHeight);
    };

    // Visibility for the deck switches is re-asserted here rather than only in
    // setCurrentTab, because resized() also runs from setSize() in the constructor
    // - BEFORE some of these widgets had a parent - and a show/hide decision made
    // only at tab-switch time left the switches permanently visible on tabs that
    // were not theirs. The active tab decides; every resize re-applies it. The
    // flags themselves are read from the tabSpecs table at the top of the grid
    // section above, where the member-row arithmetic needs them too.

    // The four new lists follow the same rule as the deck switches: each is a
    // full member of exactly one tab, so it is visible only while that tab is.
    diPadLabel.setVisible (frontEndTab);
    diPadBox.setVisible (frontEndTab);
    tracksLabel.setVisible (machineTab);
    tracksBox.setVisible (machineTab);
    inputEqOrderLabel.setVisible (inEqTab);
    inputEqOrderBox.setVisible (inEqTab);
    outputEqOrderLabel.setVisible (outEqTab);
    outputEqOrderBox.setVisible (outEqTab);
    // The corner switches follow their own equaliser's page, like the ORDER
    // combo beside them.
    inputEqHpButton.setVisible (inEqTab);
    inputEqLpButton.setVisible (inEqTab);
    outputEqHpButton.setVisible (outEqTab);
    outputEqLpButton.setVisible (outEqTab);
    oversamplingLabel.setVisible (settingsTab);
    oversamplingBox.setVisible (settingsTab);
    languageLabel.setVisible (settingsTab);
    languageBox.setVisible (settingsTab);
    tuningTonicLabel.setVisible (settingsTab);
    tuningModeLabel.setVisible (settingsTab);
    tuningTonicBox.setVisible (settingsTab);
    tuningModeBox.setVisible (settingsTab);
    autoTuneButton.setVisible (autotuneTab);
    autoTuneAmountLabel.setVisible (autotuneTab);
    autoTuneAmountSlider.setVisible (autotuneTab);
    for (std::size_t i = 0; i < compressorControls.size(); ++i)
    {
        compressorControls[i].setVisible (compressorTab);
        compressorControlLabels[i].setVisible (compressorTab);
    }
    glButton.setVisible (settingsTab);
    glLabel.setVisible (settingsTab);
    // The UI-sounds switch is on the same page as GL and OVERSAMPLING - it is the
    // same kind of engine-level preference - so it follows the same tab.
    uiSoundsButton.setVisible (settingsTab);
    antiPhaseGuardLabel.setVisible (settingsTab);
    antiPhaseGuardButton.setVisible (settingsTab);
    delayTypeLabel.setVisible (delayTab);
    delayTypeBox.setVisible (delayTab);
    delayRateLabel.setVisible (delayTab);
    delayRateBox.setVisible (delayTab);
    delaySyncLabel.setVisible (delayTab);
    delaySyncButton.setVisible (delayTab);

    // The neural model picker follows SHAPERS - under its own NEURAL knob - and
    // the IR loader follows it for the same reason, beside IR MIX. Each file-fed
    // stage keeps its loader and its status readout on the page its knob lives on.
    loadNeuralButton.setVisible (shapersTab);
    clearNeuralButton.setVisible (shapersTab);
    neuralStatusLabel.setVisible (shapersTab);
    loadIrButton.setVisible (shapersTab);
    clearIrButton.setVisible (shapersTab);
    irStatusLabel.setVisible (shapersTab);

    // The three vinyl selectors stayed on CHARACTER: they re-voice the whole
    // vinyl stage, so they live with the machine's other voicing rather than
    // beside the record's faults (which are on RECORD).
    vinylGenerationLabel.setVisible (characterTab);
    vinylGenerationBox.setVisible (characterTab);
    vinylTurntableLabel.setVisible (characterTab);
    vinylTurntableBox.setVisible (characterTab);
    vinylCartridgeLabel.setVisible (characterTab);
    vinylCartridgeBox.setVisible (characterTab);

    // The mix bar takes the MIX page's whole member band: seven cells across the
    // full width is the only size at which each machine's name and percentage
    // both fit, and the band is exactly the strip the geometry above already
    // reserved for it.
    mixBar.setVisible (mixTab);
    if (mixTab)
    {
        mixBar.setBounds (grid.getX() + 4, memberRowY + 3, grid.getWidth() - 8,
                          juce::jmax (20, grid.getBottom() - memberRowY - 6));
        mixBar.repaint();
    }

    if (frontEndTab)
    {
        // FRONT END: the DI's own pad/load selector rides the band under the
        // knobs - the row the DI feeds. It used to take the fourth cell of the
        // knob grid's second row; the strip is one full row now, so the band is
        // the only place on the page a combo can be, and it is where the DI's
        // own input switch belongs anyway.
        placeDeckSwitch (diPadLabel, diPadBox, 0, 1, "DI PAD");
    }
    else if (machineTab)
    {
        // MACHINE: the machine's TRACKS selector rides the band at the page's
        // foot, under its seven knobs.
        placeDeckSwitch (tracksLabel, tracksBox, 0, 1, "TRACKS");
    }
    else if (settingsTab)
    {
        // SETTINGS: the first band carries the four engine-level selectors - the
        // two switches and the tuning pair - and the second the two interface
        // preferences.
        placeDeckSwitch (glLabel, glButton, 0, 1, "GL");
        placeDeckSwitch (oversamplingLabel, oversamplingBox, 1, 1, "OVER");
        placeDeckSwitch (tuningTonicLabel, tuningTonicBox, 2, 1, "TONIC");
        tuningModeLabel.setBounds (grid.getX() + 3 * cellWidth + 5, memberRowY + 1, cellWidth - 10, 17);
        tuningModeBox.setBounds (grid.getX() + 3 * cellWidth + 12, memberRowY + 20, cellWidth - 24, 30);

        // LANGUAGE keeps the first cell of the second band; UI SOUNDS takes the
        // second. UI SOUNDS used to be written into the THIRD CELL OF THE FIRST
        // BAND, which is the rectangle the TONIC combo owns - so the two
        // controls were given the same bounds, the switch was added later and
        // therefore sat on top, and the page showed a control labelled
        // "UI Sounds" under the TONIC caption while the key itself could not be
        // set at all. One caption band, one control band, two cells nothing
        // else claims.
        //
        // The AUTOTUNE pair no longer takes bounds here at all: those rectangles
        // were left over from when AUTOTUNE was a sub-page of SETTINGS (the page
        // has its own tab now - see the autotuneTab branch) and they described
        // cells that are free on THIS page, which is exactly the belief that
        // produced the double booking above.
        languageLabel.setBounds (grid.getX() + 5, memberRowY + 56, cellWidth - 10, 17);
        languageBox.setBounds (grid.getX() + 12, memberRowY + 76, cellWidth - 24, 30);
        uiSoundsButton.setBounds (grid.getX() + cellWidth + 12, memberRowY + 76,
                                  cellWidth - 24, 30);
        // The guard's switch takes the THIRD cell of that same band. It is the
        // one cell on this page nothing else claims - the TONIC pair owns the
        // third cell of the band ABOVE - and it is where a protection setting
        // belongs: with GL, OVERSAMPLING and UI SOUNDS, not with the machine's
        // sound. Same caption band, same control band, same insets as the two
        // controls beside it, so the row reads as one line of preferences.
        antiPhaseGuardLabel.setBounds (grid.getX() + 2 * cellWidth + 5, memberRowY + 56,
                                       cellWidth - 10, 17);
        antiPhaseGuardButton.setBounds (grid.getX() + 2 * cellWidth + 12, memberRowY + 76,
                                        cellWidth - 24, 30);

        // The caption is put back to the left, where every other caption on the
        // page sits: the header grid also places this one Label - right-aligned,
        // beside THEME - and that justification survived into this page, which is
        // why LANGUAGE was the one caption on SETTINGS that read centred.
        languageLabel.setJustificationType (juce::Justification::left);
    }
    else if (autotuneTab)
    {
        sectionCaptionLabel.setText ("AUTOTUNE", juce::dontSendNotification);
        // The page's knob is on the strip above, with its caption and its readout
        // in its own tile (see knobCount), so the band under the strip carries the
        // control the page has left: the AUTO TUNE pill. It names itself - the
        // inline look-and-feel draws the button's own text beside its pill - so it
        // needs no caption band, and it is placed in the band's control row with
        // the same insets as the switches on every other page's member row.
        const auto bandHeight = juce::jmin (30, grid.getBottom() - memberRowY - 22);
        autoTuneButton.setBounds (grid.getX() + 12, memberRowY + 20,
                                  cellWidth - 24, bandHeight);
    }
    // The COMP page has no branch here, and that is the point: its six knobs are
    // laid out by the knob strip above, like every other page's, and the page's
    // caption comes from its own tabSubtitles entry ("the conventional
    // compressor, independent of glue") like every other page's. It used to own
    // a placement of its own - each slider given a cell as tall as the whole
    // grid, which drew a small cap at the top of that cell and left its readout
    // a hundred-odd pixels below the knob it belonged to - and a caption of its
    // own, which is what the uppercase line above this chain used to say.
    else if (delayTab)
    {
        // SPACE: the delay trio rides the member row under the delay knobs.
        placeDeckSwitch (delayTypeLabel, delayTypeBox, 0, 1, "TYPE");
        placeDeckSwitch (delaySyncLabel, delaySyncButton, 1, 1, "SYNC DELAY");
        placeDeckSwitch (delayRateLabel, delayRateBox, 2, 1, "RATE");
    }
    else if (characterTab)
    {
        // CHARACTER: three knobs in one row, so the member band under them
        // carries the page's three voicing lists - left to right, the order
        // the record is made in: what was cut, what plays it, what reads it.
        placeDeckSwitch (vinylGenerationLabel, vinylGenerationBox, 0, 1, "GENERATION");
        placeDeckSwitch (vinylTurntableLabel, vinylTurntableBox, 1, 1, "TURNTABLE");
        placeDeckSwitch (vinylCartridgeLabel, vinylCartridgeBox, 2, 1, "CARTRIDGE");
    }
    else if (shapersTab)
    {
        // SHAPERS: the two file-fed stages on one page, each with its loader
        // under its own knob. The NEURAL picker takes the band's FIRST column
        // - LOAD MODEL on the control line, CLEAR beneath it, the model-status
        // readout in the caption line above them - and the IR loader takes the
        // second and third: the status readout as the second column's caption
        // (the same label-above-control convention the knobs use), the two
        // buttons on the control line. paint() draws the picker's card behind
        // this band's first column - the two geometries share the arithmetic
        // (grid / 4 columns, 50 px band), so they cannot disagree.
        const auto buttonY = memberRowY + 20;
        const auto buttonH = juce::jmin (30, grid.getBottom() - memberRowY - 22);
        // LOAD and CLEAR share the band's ONE control line, side by side. CLEAR
        // used to sit UNDER LOAD (buttonY + buttonH + 6), which is exactly six
        // pixels past the band's bottom edge on every panel size - the user's
        // "Clear falls out of the window". Two buttons across the cell fit with
        // room to spare: the cell is ~170 px wide, the pair needs ~160.
        const auto halfButtonWidth = (cellWidth - 30) / 2;
        loadNeuralButton.setBounds (grid.getX() + 12, buttonY,
                                    halfButtonWidth, buttonH);
        clearNeuralButton.setBounds (grid.getX() + 18 + halfButtonWidth, buttonY,
                                     halfButtonWidth, buttonH);
        neuralStatusLabel.setBounds (grid.getX() + 6, memberRowY - 1,
                                     cellWidth - 12, 17);
        irStatusLabel.setBounds (grid.getX() + cellWidth + 5, memberRowY - 1,
                                 cellWidth - 10, 17);
        loadIrButton.setBounds (grid.getX() + cellWidth + 12,
                                buttonY, cellWidth - 24, buttonH);
        clearIrButton.setBounds (grid.getX() + 2 * cellWidth + 12,
                                 buttonY, cellWidth - 24, buttonH);
    }
    else if (inEqTab || outEqTab)
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
        const auto isIn = inEqTab;
        placeDeckSwitch (isIn ? inputEqOrderLabel : outputEqOrderLabel,
                         isIn ? inputEqOrderBox : outputEqOrderBox, 0, 1, "ORDER");

        const auto placeEqCorner = [&] (juce::Component& button, int column)
        {
            auto cell = juce::Rectangle<int> (grid.getX() + column * cellWidth,
                                              memberRowY,
                                              cellWidth, grid.getBottom() - memberRowY);
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

#if DEBUG
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
            { "title",         &titleLabel },
            { "subtitle",      &subtitleLabel },
            { "status",        &statusLabel },
            { "bypass",        &bypassButton },
            { "delta",         &deltaButton },
            { "polarity",      &polarityButton },
            { "autoGain",      &autoGainButton },
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
            { "gl",            &glButton },
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
            { "uiSounds",       &uiSoundsButton },
            { "guardLabel",     &antiPhaseGuardLabel },
            { "guardBtn",       &antiPhaseGuardButton },
            { "glLabel",        &glLabel },
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
