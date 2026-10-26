// =============================================================================
//  The tick, and everything it refreshes: timerCallback(), the bounded repaint it
//  asks for, and the eleven readers that turn the processor's state into what the
//  panel shows.
//
//  Moved out of PluginEditor.cpp, prose and all. Thirty times a second this is what
//  runs, and it was spread through the file in eleven places, each next to the
//  control it updates. What it costs to keep them together is that a control's
//  styling and its refresh are now in two files (EditorTheme.cpp styles it, this
//  refreshes it); what it buys is that everything that happens on a tick is
//  readable as one list.
// =============================================================================

// The editor's own header: the class, the members and the palette type.
#include "PluginEditor.h"

// The panel vocabulary this file reads through, from the headers that hold
// it - the paint kit, the page table and the knob table.
#include "GUI/PanelPaint.h"
#include "GUI/PanelText.h"
#include "GUI/PanelTranslations.h"

// Only the names this file's own text uses: a header member that is not
// reachable from here should not look reachable.
using j37panel::paletteFor;
using j37panel::parameterTooltipText;
using j37panel::principleParameterIds;

void FirstAudioProcessorEditor::refreshNeuralStatus() {
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
    const auto modelKilo = juce::roundToInt(modelRate / 1000.0);
    const auto rateTag = modelRate > 1.0 && std::abs(modelRate - engineRate) > 1.0
                             ? juce::String(" @ ") + juce::String(modelKilo) + "K"
                             : juce::String();

    const auto status = neuralStatusReason.isNotEmpty()
                            ? xlat("INVALID MODEL") + " (" + neuralStatusReason + ")"
                        : audioProcessor.hasNeuralModel() && loadedNeuralName.isNotEmpty()
                            ? loadedNeuralName + rateTag
                            : xlat("NO MODEL");

    if (status == lastShownNeuralStatus)
        return;

    lastShownNeuralStatus = status;
    // The file's name and the reason are DATA, not sentences, so they are not
    // looked up: the two state words around them are (see the note above xlat).
    neuralStatusLabel.setText(status, juce::dontSendNotification);
    neuralStatusLabel.setColour(juce::Label::textColourId, audioProcessor.hasNeuralModel()
                                                               ? paletteFor(false).accent
                                                               : paletteFor(false).secondary);
}

void FirstAudioProcessorEditor::refreshIrStatus() {
    // The processor is the source of truth, exactly as with the model status:
    // the stage can be cleared from either button, so the readout follows
    // whatever the convolvers actually hold.
    const auto status = irStatusReason.isNotEmpty()
                            ? xlat("INVALID IR") + " (" + irStatusReason + ")"
                        : audioProcessor.hasIr() && loadedIrName.isNotEmpty() ? loadedIrName
                                                                              : xlat("NO IR");

    if (status == lastShownIrStatus)
        return;

    lastShownIrStatus = status;
    irStatusLabel.setText(status, juce::dontSendNotification);
    irStatusLabel.setColour(juce::Label::textColourId, audioProcessor.hasIr()
                                                           ? paletteFor(false).accent
                                                           : paletteFor(false).secondary);
}

void FirstAudioProcessorEditor::refreshNoiseStatus() {
    // The VINYL page's readout, in FOUR states rather than the two the other
    // pickers have - because this stage is the only one whose load is not
    // finished when the picker's callback returns. The decode runs on the
    // loader's own thread (Source/DSP/NoiseLoader.h), so between the click and
    // the sound there is a real interval during which "NO PRINT" would be a lie
    // about a file that is being read: LOADING says which of the two is true.
    //
    // The refusal of the last load arrives through the LOADER, not through the
    // chooser's callback: the decode runs after that callback has returned, so
    // this tick is where the reason is adopted. The model and IR pickers read
    // theirs synchronously from lastLoadError(), which is why only this readout
    // needs the poll. The loader clears its error when the next load starts or
    // the stage is released, so the reason stays attached to the file that
    // earned it and to nothing else.
    if (!audioProcessor.isNoiseLoading())
        noiseStatusReason = audioProcessor.getNoiseError();

    // Order matters and is: the reason a load was REFUSED outranks everything
    // (the previous print may well still be playing), then the busy state, then
    // the loaded file's own name, then the synthesised surface.
    const auto status = noiseStatusReason.isNotEmpty()
                            ? xlat("INVALID PRINT") + " (" + noiseStatusReason + ")"
                        : audioProcessor.isNoiseLoading() ? xlat("LOADING PRINT")
                        : audioProcessor.hasNoisePrint()  ? audioProcessor.getNoiseFileName()
                                                          : xlat("NO PRINT");

    if (status == lastShownNoiseStatus)
        return;

    lastShownNoiseStatus = status;
    noiseStatusLabel.setText(status, juce::dontSendNotification);
    noiseStatusLabel.setColour(juce::Label::textColourId, audioProcessor.hasNoisePrint()
                                                              ? paletteFor(false).accent
                                                              : paletteFor(false).secondary);
}

void FirstAudioProcessorEditor::refreshUserPresetList() {
    userPresetBox.clear(juce::dontSendNotification);
    const auto names = audioProcessor.getUserPresetNames();
    for (auto i = 0; i < names.size(); ++i)
        userPresetBox.addItem(names[i], i + 1);

    const auto loaded = audioProcessor.getCurrentPresetName();
    const auto index = names.indexOf(loaded);
    userPresetBox.setSelectedItemIndex(index >= 0 ? index : -1, juce::dontSendNotification);
    lastShownUserPreset = index >= 0 ? loaded : juce::String();
}

void FirstAudioProcessorEditor::refreshPresetList() {
    presetBox.clear(juce::dontSendNotification);
    const auto names = FirstAudioProcessor::getPresetNames();
    for (auto i = 0; i < names.size(); ++i)
        presetBox.addItem(names[i], i + 1);

    // A fresh instance has NO preset loaded (index -1): showing "Default" as the
    // box's selection then was a lie - the panel held the raw parameter defaults
    // (DRIVE 0.30), not that preset's values (DRIVE 0.28), and picking "Default"
    // looked like it had done nothing. Nothing selected is the honest display; the
    // first honest load happens when the user (or a session) actually applies one.
    const auto loaded = audioProcessor.getLastPresetIndex();
    presetBox.setSelectedItemIndex(loaded >= 0 ? loaded : -1, juce::dontSendNotification);
    lastShownPreset = loaded;
}

void FirstAudioProcessorEditor::refreshModeButtonCaption() {
    // The caption names the live mode, read from the parameters rather than from
    // a button-local bool: a session load, an undo or a preset can all change the
    // modes behind the panel's back, and the caption must follow all of them.
    const auto modernOn = isModeOn("modern_mode");
    const auto lofiOn = isModeOn("lofi_mode");
    modeCycleButton.setButtonText(modernOn ? "MODE: MODERN" : lofiOn ? "MODE: LO-FI" : "MODE: OFF");

    // The button's background is drawn by the base Look and Feel - this class
    // overrides captions, not button backgrounds - so the state has to arrive as
    // colours: lit with the accent while a mode is engaged, the raised panel
    // colour at OFF, matching the transport keys.
    const auto &palette = paletteFor(darkTheme);
    const auto modeOn = modernOn || lofiOn;
    modeCycleButton.setColour(juce::TextButton::buttonColourId,
                              modeOn ? palette.accent : palette.raised);
    modeCycleButton.setColour(juce::TextButton::buttonOnColourId,
                              modeOn ? palette.accent : palette.raised);
    modeCycleButton.setColour(juce::TextButton::textColourOffId,
                              modeOn ? palette.readout : palette.text);
    modeCycleButton.setColour(juce::TextButton::textColourOnId,
                              modeOn ? palette.readout : palette.text);
}

// The three transport keys plus the momentary spindown button. Same rule as the tab
// bar: a plain TextButton's getToggleState() is always false, so the lit key's
// colours are written to BOTH the ...Id and the ...OnId pair. START is the one state
// the engine advances out of by itself - it becomes PLAY when the capstan arrives - so
// its lamp is the only one that goes out on its own, and the key that is lit is the
// state the machine is in.
int FirstAudioProcessorEditor::currentTransportState() const {
    // Read through the parameter, not a local mirror, so an automation lane moving
    // the transport repaints the panel on the next frame. convertFrom0to1() gives
    // the choice index back as a 0..2 float for a three-entry AudioParameterChoice.
    if (auto *parameter = audioProcessor.parameters.getParameter("transport"))
        return juce::jlimit(0, 2,
                            juce::roundToInt(parameter->convertFrom0to1(parameter->getValue())));

    return 1;
}

//==============================================================================
juce::Rectangle<int> FirstAudioProcessorEditor::statusPillBounds() const {
    constexpr int headerGridWidth =
        headerColumnCount * headerColumnWidth + (headerColumnCount - 1) * headerColumnGap;
    const auto layout = getEditorLayout();
    const juce::Rectangle<int> pill(layout.header.getRight() - headerGridWidth,
                                    layout.header.getY() + 62, headerGridWidth - 18, 18);
    return pill.expanded(2, 6);
}

juce::Rectangle<int> FirstAudioProcessorEditor::deckAnimationBounds() const {
    // The deck's animated picture is drawn by this component rather than by a
    // widget, so it can only be invalidated by REGION:
    //
    //   * the software transport - the small reel at deck.right - 42, deck.y + 14
    //     and the ribbon that falls from it to deck.y + 60. paint() draws it only
    //     while the 3D scene is not live (for example, when a driver cannot
    //     create a context), which is exactly the case in which nothing else is
    //     moving there at all.
    //   * the particles, in the corridor resized() measured from the gap the
    //     switches row actually left (deckParticleCorridorX/Y).
    //
    // Neither rectangle covers a combo box, a slider or a button: the reel hangs
    // in the heading strip's right corner, past the build badge's right inset, and
    // the corridor is the free gap the row reported. That is what keeps this from
    // reintroducing the popup lag a whole-panel repaint caused.
    const auto layout = getEditorLayout();
    juce::Rectangle<int> area(layout.deck.getRight() - 60, layout.deck.getY() + 2, 52, 68);

    if (!deckParticleCorridorX.isEmpty() && !deckParticleCorridorY.isEmpty())
        area = area.getUnion(
            {deckParticleCorridorX.getStart() - 2, deckParticleCorridorY.getStart() - 2,
             deckParticleCorridorX.getLength() + 4, deckParticleCorridorY.getLength() + 4});

    return area;
}

void FirstAudioProcessorEditor::repaintAnimatedSurfaces() {
    // The knobs. Their halos breathe with the compressor and their pointers ride
    // an orbit of the same phase; the ticks tremble with the transport's drift.
    // All of that is read from the look and feel at PAINT time, and a slider only
    // repaints itself when its VALUE changes - so a knob nobody is touching was
    // frozen mid-breath for as long as the panel stayed open.
    for (auto &slider : controls)
        if (slider.isVisible())
            slider.repaint();

    // The switches. Their thumbs GLIDE between stops for about five frames after
    // a click (noteToggle / updateToggleAnimations in the look and feel) and their
    // lamps light with the machine's activity, so they need the same per-frame
    // invalidation the knobs do. Twelve widgets, most of them hidden behind a
    // page: the visible ones are two to four per frame.
    for (auto *toggle : {&deltaButton, &bypassButton, &polarityButton, &autoGainButton,
                         &autoTuneButton, &uiSoundsButton, &antiPhaseGuardButton, &inputEqHpButton,
                         &inputEqLpButton, &outputEqHpButton, &outputEqLpButton, &delaySyncButton})
        if (toggle->isVisible())
            toggle->repaint();

    // The status lamp and the deck's own transport, both drawn by paint().
    repaint(statusPillBounds());

    if (!tapeScene.isSceneLive())
        repaint(deckAnimationBounds());
}

//==============================================================================
void FirstAudioProcessorEditor::timerCallback() {
    audioProcessor.flushPendingTransportHostSync();

    // Hosts can keep an editor alive while its window is hidden. Do not poll
    // telemetry, advance physics, or issue repaint work in that state; the next
    // visible timer tick reconstructs all display-only state from the processor.
    if (!isShowing())
        return;

    // Retry context creation while the host is still creating its peer. The
    // budget is bounded so an unavailable driver is not polled forever.
    if (openGLAttachAttemptsLeft > 0) {
        --openGLAttachAttemptsLeft;
        if (openGLContext.isAttached()) {
            openGLAttachAttemptsLeft = 0;
        } else {
            openGLContext.attachTo(*this);
            if (openGLContext.isAttached()) {
                openGLAttachAttemptsLeft = 0;
            }
        }
    }

    if (openGLContext.isAttached())
        tapeScene.serviceContextAttachment();

    // The native scene path: while a scene-capable adapter owns the deck's
    // surface, every tick builds the frame the OpenGL callback would have built
    // and hands it over - the adapter records it on its own GL thread.
    if (nativeRenderer != nullptr)
        submitNativeSceneFrame();

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
    // The noise-print readout is the same rule again, and here the polling is
    // the ONLY way the readout can ever leave LOADING: the decode has no
    // callback into the panel, it simply publishes the print and clears its own
    // busy flag when it is done (Source/DSP/NoiseLoader.h).
    refreshNoiseStatus();

    inputMeter.setLoudness(telemetry.inputPeakDb, telemetry.inputRmsDb, telemetry.inputLufs,
                           telemetry.inputVuDb, telemetry.inputCombinedDb, telemetry.inputClipping);
    outputMeter.setLoudness(telemetry.outputPeakDb, telemetry.outputRmsDb, telemetry.outputLufs,
                            telemetry.outputVuDb, telemetry.outputCombinedDb,
                            telemetry.outputClipping);

    compressorMeterIn.setReduction(telemetry.inputGainReductionDb,
                                   telemetry.inputCompressorActivity);
    compressorMeterOut.setReduction(telemetry.outputGainReductionDb,
                                    telemetry.outputCompressorActivity);

    // The MIX page's bar. Read from the seven SOURCE parameters and normalised
    // exactly the way SaturationCore::setWeights normalises them - including its
    // all-zero fallback, so a bar that shows pure tape really is pure tape. It is
    // read only while the page that shows it is on screen: seven parameter reads
    // and a divide per frame is nothing, but nothing is cheaper than nothing.
    if (mixBar.isVisible()) {
        std::array<float, principleCount> shares{};
        float shareSum = 0.0f;

        for (int p = 0; p < principleCount; ++p) {
            if (const auto *parameter = audioProcessor.parameters.getParameter(
                    juce::String(principleParameterIds[static_cast<std::size_t>(p)])))
                shares[static_cast<std::size_t>(p)] =
                    juce::jlimit(0.0f, 1.0f, static_cast<float>(parameter->getValue()));

            shareSum += shares[static_cast<std::size_t>(p)];
        }

        if (shareSum > 1.0e-6f)
            for (auto &share : shares)
                share /= shareSum;
        else {
            // The same answer the core gives an empty mix: pure tape, the machine
            // the plugin is calibrated as. Painting anything else here would put
            // the bar and the sound in front of different stories.
            shares = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        }

        mixBar.setShares(shares, juce::jlimit(0.0f, 1.0f, shareSum));
    }

    const auto reduction = telemetry.inputGainReductionDb + telemetry.outputGainReductionDb;
    const auto activity = telemetry.compressorActivity;

    // Report the harmonic balance the tape stage is producing. Even and odd are shown
    // side by side because the ratio between them is the character: even-dominant reads as
    // warm and full, odd-dominant as hard and edgy, and real tape has both.
    // The MODELS page's spectrum, from the same frame as the ratio below it: the
    // curve and the even/odd split describe one block of audio rather than two,
    // which is the whole reason the frame carries the bands instead of the editor
    // reading twenty-four atomics.
    spectrumBandsDb = telemetry.spectrumBandsDb;

    // Only the page that DRAWS the curve is invalidated, and only its own cell: the
    // panel is repainted in bands on purpose, and a whole-window repaint per tick is
    // the cost this file's repaint strategy exists to avoid. shapersSpectrumBounds()
    // is the same function paint() draws with, so the rectangle that is invalidated
    // is the rectangle that is drawn - see its declaration in PluginEditor.h.
    if (activeTabIsShapers)
        repaint(shapersSpectrumBounds());

    const auto evenRatio = telemetry.evenHarmonicRatio;
    const auto oddRatio = telemetry.oddHarmonicRatio;
    harmonicsReadout.setText("E " + juce::String(evenRatio * 100.0f, 1) + " %" + "   O " +
                                 juce::String(oddRatio * 100.0f, 1) + " %",
                             juce::dontSendNotification);

    // Colour the readout by which family dominates, so the character is readable at a
    // glance without needing to compare the numbers.
    const auto harmonicPalette = paletteFor(darkTheme);
    const auto totalHarmonics = evenRatio + oddRatio;
    // The readouts breathe with the machine: a gentle luminance oscillation on
    // top of the state colour, so the numbers feel powered by the same signal
    // they report rather than painted on. The breath is bounded (±20 %), so the
    // colour coding stays readable at every phase.
    const auto readoutBreath = 1.0f - 0.2f * (0.5f + 0.5f * std::sin(customLookAndFeel.phase()));
    const auto breathe = [&](juce::Colour c) { return c.withMultipliedBrightness(readoutBreath); };
    harmonicsReadout.setColour(juce::Label::textColourId,
                               breathe(totalHarmonics < 0.001f ? harmonicPalette.secondary
                                       : evenRatio >= oddRatio ? harmonicPalette.status
                                                               : harmonicPalette.needle));

    // The deck's tempo, as the host reports it. A host that offers no playhead
    // (an offline render, a bare player) leaves the readout at the machine's
    // default and says so, because a number that looks measured but is only a
    // fallback is worse than an honest dash.
    const auto deckBpm = audioProcessor.getDeckTempoBpm();
    const auto deckBpmValid = audioProcessor.getDeckTempoValid();
    bpmReadout.setText(deckBpmValid ? juce::String(deckBpm, 1) + " " + xlat("BPM")
                                    : juce::String(juce::roundToInt(deckBpm)) + " " + xlat("BPM") +
                                          " (" + xlat("default") + ")",
                       juce::dontSendNotification);
    bpmReadout.setColour(
        juce::Label::textColourId,
        breathe(deckBpmValid ? paletteFor(darkTheme).status : paletteFor(darkTheme).secondary));

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

    if (confidence < 0.05f || trackedHz <= 0.0f) {
        subfundReadout.setText(xlat("idle - no note tracked"), juce::dontSendNotification);
        subfundReadout.setColour(juce::Label::textColourId, harmonicPalette.secondary);
    } else {
        // The confidence is shown as a percentage rather than hidden, because a
        // partial lock (say 40 %) is a real and useful state: the cascade IS
        // generating, but it is following a signal that is not steady enough for
        // a clean track, and the user should be able to see that.
        subfundReadout.setText(juce::String(trackedHz, 1) + " Hz   " +
                                   juce::String(juce::roundToInt(confidence * 100.0f)) + " %",
                               juce::dontSendNotification);
        // Green while the track is solid, amber while it is partial: the colour is
        // the readout's own confidence bar.
        subfundReadout.setColour(
            juce::Label::textColourId,
            breathe(confidence > 0.75f ? harmonicPalette.status : harmonicPalette.needle));
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
    const auto *antiPhaseGuardSetting =
        audioProcessor.parameters.getRawParameterValue("anti_phase_guard");
    const auto antiPhaseGuardOn =
        antiPhaseGuardSetting == nullptr || antiPhaseGuardSetting->load() >= 0.5f;
    const auto antiPhaseText =
        !antiPhaseGuardOn ? xlat("off")
        : antiPhaseNow < 0.01f
            ? xlat("clean")
            : juce::String(juce::roundToInt(antiPhaseNow * 100.0f)) + " " + xlat("% corrected");
    if (antiPhaseText != lastShownAntiPhase) {
        lastShownAntiPhase = antiPhaseText;
        antiPhaseReadout.setText(antiPhaseText, juce::dontSendNotification);
        antiPhaseReadout.setColour(juce::Label::textColourId,
                                   breathe(!antiPhaseGuardOn      ? harmonicPalette.secondary
                                           : antiPhaseNow < 0.01f ? harmonicPalette.status
                                                                  : harmonicPalette.needle));
    }

    // Animated presentation state. Everything here is derived from audio
    // telemetry, so the panel visibly reacts to what the plugin is doing.
    glowPhase += 0.13f;
    if (glowPhase > juce::MathConstants<float>::twoPi)
        glowPhase -= juce::MathConstants<float>::twoPi;

    const auto targetGlow = juce::jlimit(0.0f, 1.0f, activity * 0.7f + std::abs(reduction) / 6.0f);
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
    statusLabel.setText(bypassed ? xlat("BYPASSED / DRY") : xlat("STEREO / REAL TIME"),
                        juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, bypassed ? paletteFor(darkTheme).secondary
                                                              : paletteFor(darkTheme).status);

    // The reels spin at the machine's ACTUAL platter speed, which is the transport
    // ramp multiplied by the spindown ramp - so a START makes them visibly come up
    // to speed and a held SPINDOWN makes them coast down and stop. That is the
    // whole point of publishing getTransportRamp(): before this the reels only knew
    // the SPEED combo, so the transport did not move them at all.
    // getSelectedId() returns 0 while nothing is selected, which would index the
    // combo's item list at -1, so the index is clamped before it is used.
    const auto speedIndex = juce::jlimit(0, 2, speedBox.getSelectedId() - 1);
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
    const auto spindownNow = audioProcessor.isSpindownHeld() || spindownButton.isDown();

    // The three transport keys relight only when the state actually changes.
    if (transportNow != lastShownTransport) {
        lastShownTransport = transportNow;
        styleTransportButtons();
    }

    if (spindownNow != lastShownSpindown) {
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
    const auto machineStateText = spindownNow         ? "SPINDOWN / POWER CUT"
                                  : waitingForHost    ? "START / WAITING FOR DAW"
                                  : transportNow == 0 ? "STOPPED / AT REST"
                                  : transportNow == 2 ? "START / SPINNING UP"
                                                      : "PLAY / AT SPEED";
    // Explicit juce::String construction: comparing const char* against a
    // juce::String with != is ambiguous (candidates on both sides), so the
    // left operand is materialised first.
    // The mode caption rides the same refresh: any path that flips a mode
    // (session load, undo, preset, automation) shows up within one frame.
    refreshModeButtonCaption();

    if (juce::String(machineStateText) != lastShownMachineState) {
        lastShownMachineState = machineStateText;
        deckHintLabel.setText(machineStateText, juce::dontSendNotification);
        deckHintLabel.setColour(juce::Label::textColourId,
                                spindownNow || waitingForHost ? paletteFor(darkTheme).needle
                                : transportNow == 0           ? paletteFor(darkTheme).secondary
                                                              : paletteFor(darkTheme).status);
    }

    using Clock = std::chrono::steady_clock;
    const auto animationNow = Clock::now();
    const auto animationDelta =
        lastAnimationFrame == Clock::time_point{}
            ? 1.0f / 30.0f
            : juce::jlimit(0.0f, 0.25f,
                           std::chrono::duration<float>(animationNow - lastAnimationFrame).count());
    lastAnimationFrame = animationNow;
    customLookAndFeel.setActivity(glowAmount);
    customLookAndFeel.setDrift(driftAmount);
    customLookAndFeel.advanceFrame(animationDelta);
    customLookAndFeel.updateToggleAnimations(animationDelta);

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
    if (uiSoundEnabled != lastShownUiSoundEnabled) {
        lastShownUiSoundEnabled = uiSoundEnabled;
        uiSounds.setEnabled(uiSoundEnabled);
    }

    uiSounds.setBrightness(juce::jlimit(0.0f, 1.0f, glowAmount * 0.7f + activity * 0.3f));

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
        for (std::size_t i = 0; i < controlCount; ++i)
            if (controls[i].isVisible() && controls[i].getBounds().contains(mouse)) {
                hoveredControl = controls[i].getName();
                break;
            }
        if (hoveredControl != lastShownHoverHint) {
            lastShownHoverHint = hoveredControl;
            controlsHintLabel.setText(
                hoveredControl.isNotEmpty() ? hoveredControl : juce::String("Shift = fine tune"),
                juce::dontSendNotification);
        }
    }

    // Is a list or a menu on screen? A combo's popup and a popup menu are
    // components of their own, and JUCE tears one down the moment the tree it was
    // built from is rewritten - so the two polls below, which DO rewrite combo
    // items, must not run while one is open. That was the "the list needs two
    // clicks" behaviour: the click opened the list, the next tick cleared and
    // re-added the items underneath it, and the list closed itself before the
    // pointer could land. The checks are asked of the two preset boxes by name
    // and of the component tree in general, because a popup menu (the theme, the
    // oversampling list, a right-click) is modal in JUCE rather than a ComboBox's
    // child.
    const auto popupIsOpen = presetBox.isPopupActive() || userPresetBox.isPopupActive()
                             || juce::Component::getCurrentlyModalComponent() != nullptr;

    // Workflow state is cheap to poll at 30 Hz and makes the panel self-healing:
    // if the host, a session load or a preset change moves anything, the buttons,
    // badge and preset display catch up on the next frame instead of lying.
    // Skipped while a popup is open, so nothing the poll touches can pull the
    // menu out from under the pointer; the closed-menu tick does the work.
    const auto canUndoNow = popupIsOpen ? lastShownCanUndo
                                       : audioProcessor.getUndoManager().canUndo();
    const auto canRedoNow = popupIsOpen ? lastShownCanRedo
                                       : audioProcessor.getUndoManager().canRedo();
    const auto slotNow = popupIsOpen ? lastShownSlot : audioProcessor.getActiveCompareSlot();
    const auto dirtyNow = popupIsOpen ? lastShownDirty : audioProcessor.isCompareDirty();
    const auto presetNow = popupIsOpen ? lastShownPreset : audioProcessor.getLastPresetIndex();
    if (slotNow != lastShownSlot || dirtyNow != lastShownDirty || canUndoNow != lastShownCanUndo ||
        canRedoNow != lastShownCanRedo) {
        lastShownSlot = slotNow;
        lastShownDirty = dirtyNow;
        lastShownCanUndo = canUndoNow;
        lastShownCanRedo = canRedoNow;
        updateWorkflowButtons();
    }
    if (presetNow != lastShownPreset) {
        lastShownPreset = presetNow;
        // Item IDs are index + 1, so 0 means "no selection" - what a user preset
        // (index -1) should show in the FACTORY combo.
        presetBox.setSelectedId(presetNow + 1, juce::dontSendNotification);
        refreshUserPresetList(); // a factory load clears the user-preset selection
    }
    // The box's SELECTED ENTRY follows the loaded preset, but a preset that is
    // loaded-and-then-edited keeps its entry while the machine drifts; that is
    // correct (the badge says EDITED) and needs no per-frame re-selection here.

    const auto userPresetNow = popupIsOpen ? lastShownUserPreset
                                          : audioProcessor.getCurrentPresetName();
    const auto presetDirtyNow = popupIsOpen ? lastShownPresetDirty
                                           : audioProcessor.isPresetDirty();
    if (userPresetNow != lastShownUserPreset || presetDirtyNow != lastShownPresetDirty) {
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
    if (isShowing() && ++compareMirrorTick >= 6) {
        compareMirrorTick = 0;
        audioProcessor.updateActiveCompareSlot();
    }

    if (physicsWorld == nullptr)
        return;

    physicsWorld->Step(1.0f / 30.0f, 6, 2);

    std::array<RenderOrb, decorativeOrbCount> nextFrame;
    for (std::size_t i = 0; i < decorativeOrbCount; ++i) {
        const auto position = physicsOrbs[i].body->GetPosition();
        nextFrame[i] = {juce::Point<float>(position.x, position.y), physicsOrbs[i].radius,
                        physicsOrbs[i].colour};
    }

    {
        const juce::SpinLock::ScopedLockType lock(renderOrbsLock);
        renderOrbs = nextFrame;
    }

    // Finally, the 3D transport. It is fed last so every number it sees is the
    // one this tick settled on, and it is fed FIVE PLAIN FLOATS and nothing else:
    // the scene owns a second OpenGL context on its own render thread, and the
    // one thing that must never happen is a parameter tree or a JUCE Component
    // being read from there. Each is normalised here, in the thread that owns the
    // parameter, so the render thread has no idea what a dB is.
    if (const auto *rawDrive = audioProcessor.parameters.getRawParameterValue("drive"))
        sceneDrive = rawDrive->load();

    tapeScene.setAudioState(
        // Output loudness over the range the meters themselves use for "moving":
        // below -48 dB the machine is at the noise floor and the scene goes quiet.
        juce::jlimit(0.0f, 1.0f, juce::jmap(telemetry.outputCombinedDb, -48.0f, 0.0f, 0.0f, 1.0f)),
        juce::jlimit(0.0f, 1.0f, sceneDrive),
        // Both compressors' total reduction, on the same six-decibel scale the
        // panel's glow uses, so the reels' rim light and the knobs agree.
        juce::jlimit(0.0f, 1.0f, std::abs(reduction) / 6.0f),
        // The transport's drift, re-centred: the panel carries it as 0..1 with
        // 0.5 as steady, and the scene wants it signed so the ribbon waves to
        // either side of the path rather than always to one.
        juce::jlimit(-1.0f, 1.0f, (driftAmount - 0.5f) * 2.0f),
        // The machine's own platter speed, already lagged in smoothedMachineSpeed
        // - so a STOP stops the reels and a held SPINDOWN coasts them down, and
        // the tape only transfers while the transport is actually running.
        juce::jlimit(0.0f, 1.0f, smoothedMachineSpeed),
        // The frame's own loudest sample, linear 0..1, drained from the processor
        // once per tick (getOutputPeakLevel is an exchange) so the scene's peak
        // lamp reads the same transient the meter needle just fell for.
        juce::jlimit(0.0f, 1.0f, audioProcessor.getOutputPeakLevel()));

    // The texture's doses follow the machine: the shimmer term scales with
    // drive and with the compressors' reduction, so the panel's surface is
    // alive where the signal pushes. One call feeds both renderers - the 2D
    // ink in paint() and the scene's uTextureWeights uniform.
    configureTextureWeights(sceneDrive, juce::jlimit(0.0f, 1.0f, std::abs(reduction) / 6.0f));

    // TapeScene retries its own context attachment from its resized callback
    // and from the repaint timer while its bounded attach budget remains.

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
    // ...and never while a menu is up. An invalidation on the frame the menu is
    // open is an invalidation of the panel BEHIND it, which the popup's own
    // window then has to repaint over; that is the same class of problem as the
    // whole-panel repaint noted above, one rectangle at a time. The frame after
    // the menu closes catches the animation up, which at 30 Hz is invisible.
    if (!popupIsOpen && ++animationPaintTick >= 2) {
        animationPaintTick = 0;
        repaintAnimatedSurfaces();
    }
}
