# Graph Report - first  (2026-11-14)

## Corpus Check
- cluster-only mode — file stats not available

## Summary
- 6374 nodes · 12001 edges · 315 communities (237 shown, 78 thin omitted)
- Extraction: 93% EXTRACTED · 7% INFERRED · 0% AMBIGUOUS · INFERRED: 877 edges (avg confidence: 0.85)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `0b9d258b`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- Community 0
- Community 1
- Community 2
- Community 3
- Community 4
- Community 5
- Community 6
- Community 7
- Community 8
- Community 9
- Community 10
- Community 11
- Community 12
- Community 13
- Community 14
- Community 15
- Community 16
- Community 17
- Community 18
- Community 19
- Community 20
- Community 21
- Community 22
- Community 23
- Community 24
- Community 25
- Community 26
- Community 27
- Community 28
- Community 29
- Community 30
- Community 31
- Community 32
- Community 33
- Community 34
- Community 35
- Community 36
- Community 37
- Community 38
- Community 39
- Community 40
- Community 41
- Community 42
- Community 43
- Community 44
- Community 45
- Community 46
- Community 47
- Community 48
- Community 49
- Community 50
- Community 51
- Community 52
- Community 53
- Community 54
- Community 55
- Community 56
- Community 57
- Community 58
- Community 59
- Community 60
- Community 61
- Community 62
- Community 63
- Community 64
- Community 65
- Community 66
- Community 67
- Community 68
- Community 69
- Community 70
- Community 71
- Community 72
- Community 73
- Community 74
- Community 75
- Community 76
- Community 77
- Community 78
- Community 79
- Community 80
- Community 81
- Community 82
- Community 83
- Community 84
- Community 85
- Community 86
- Community 87
- Community 88
- Community 89
- Community 90
- Community 91
- Community 92
- Community 93
- Community 94
- Community 95
- Community 96
- Community 97
- Community 98
- Community 99
- Community 100
- Community 101
- Community 102
- Community 103
- Community 104
- Community 105
- Community 106
- Community 107
- Community 108
- Community 109
- Community 110
- Community 111
- Community 112
- Community 113
- Community 114
- Community 115
- Community 116
- Community 117
- Community 118
- Community 119
- Community 120
- Community 121
- Community 122
- Community 123
- Community 124
- Community 125
- Community 126
- Community 127
- Community 128
- Community 129
- Community 130
- Community 131
- Community 132
- Community 133
- Community 134
- Community 135
- Community 136
- Community 137
- Community 138
- Community 139
- Community 140
- Community 141
- Community 142
- Community 143
- Community 144
- Community 145
- Community 146
- Community 147
- Community 148
- Community 149
- Community 150
- Community 151
- Community 152
- Community 153
- Community 154
- Community 156
- Community 157
- Community 158
- Community 159
- Community 160
- Community 161
- Community 162
- Community 163
- Community 164
- Community 165
- Community 166
- Community 168
- Community 169
- Community 170
- Community 172
- Community 173
- Community 174
- Community 175
- Community 176
- Community 177
- Community 178
- Community 179
- Community 180
- Community 181
- Community 182
- Community 183
- Community 184
- Community 185
- Community 187
- Community 188
- Community 189
- Community 190
- Community 191
- Community 192
- Community 193
- Community 194
- Community 195
- Community 196
- Community 197
- Community 198
- Community 199
- Community 200
- Community 201
- Community 202
- Community 203
- Community 204
- Community 205
- Community 206
- Community 207
- Community 208
- Community 210
- Community 213
- Community 214
- Community 215
- Community 217
- Community 218
- Community 219
- Community 220
- Community 221
- Community 222
- Community 223
- Community 224
- Community 225
- Community 226
- Community 227
- Community 228
- Community 229
- Community 230
- Community 231
- Community 232
- Community 233
- Community 234
- Community 237
- Community 238
- Community 239
- Community 240
- Community 241
- Community 242
- Community 243
- Community 244
- Community 246
- Community 248
- Community 249
- Community 250
- Community 251
- Community 252
- Community 253
- Community 254
- Community 255
- Community 256
- Community 258
- Community 259
- Community 260
- Community 261
- Community 262
- Community 263
- Community 264
- Community 265
- Community 267
- Community 268
- Community 269
- Community 271
- Community 272
- Community 273
- Community 274
- Community 275
- Community 278
- Community 279
- Community 280
- Community 281
- Community 282
- Community 283
- Community 284
- Community 285
- Community 286
- Community 287
- Community 288
- Community 289
- Community 290
- Community 291
- Community 292
- Community 293
- Community 294
- Community 295
- Community 296
- Community 297
- Community 298
- Community 299
- Community 300

## God Nodes (most connected - your core abstractions)
1. `FirstAudioProcessor` - 691 edges
2. `VulkanRenderer` - 111 edges
3. `TapeScene` - 102 edges
4. `TapeBlockState` - 83 edges
5. `FxRack` - 67 edges
6. `D3D12Renderer` - 66 edges
7. `DattorroRoom` - 62 edges
8. `ChainReplica` - 62 edges
9. `VinylStage` - 62 edges
10. `SokolRenderer` - 56 edges

## Surprising Connections (you probably didn't know these)
- `framedOk()` --references--> `SceneFrame`  [EXTRACTED]
  tests/scene_frame/pack_contract.cpp → Source/GUI/SceneFrame.h
- `inspectDraw()` --references--> `SceneDraw`  [EXTRACTED]
  tests/scene_frame/pack_contract.cpp → Source/GUI/SceneFrame.h
- `main()` --references--> `NeuralStage`  [INFERRED]
  tests/neural/fixture_test.cpp → Source/DSP/NeuralStage.h
- `drive()` --references--> `NeuralStage`  [INFERRED]
  tests/neural/nam_only_test.cpp → Source/DSP/NeuralStage.h
- `FirstAudioProcessorEditor::deckAnimationBounds()` --references--> `Rectangle`  [EXTRACTED]
  Source/GUI/EditorTimer.cpp → tests/layout/layout_test.cpp

## Import Cycles
- None detected.

## Communities (315 total, 78 thin omitted)

### Community 0 - "Community 0"
Cohesion: 0.00
Nodes (435): FirstAudioProcessor, activeSlot, activeTapeType, ampBiasParam, ampBiasSmoothed, ampL, ampR, ampSourceParam (+427 more)

### Community 1 - "Community 1"
Cohesion: 0.03
Nodes (138): applyGlobalBarLabelState(), applyLiveBarPreference(), applyParamDefaults(), applyParamValue(), applyPlaceholderSizingStyles(), applySavedParamValues(), averageRgb01(), bindEditBadgeProxy() (+130 more)

### Community 2 - "Community 2"
Cohesion: 0.07
Nodes (76): applyEditing(), beginNewLiveConfiguration(), buildInsertPlaceholderSnapshotFromDom(), buildLocatorForLeaf(), buildPickedAnchorSnapshot(), cancelEditing(), cancelEditingToPicking(), cancelInsertConfigure() (+68 more)

### Community 3 - "Community 3"
Cohesion: 0.03
Nodes (76): TapeBlockState, activeChannels, antiPhaseGuardOn, autoGainEnabled, autoTuneStrength, channelData, clippingThisBlock, compensationCoefficient (+68 more)

### Community 4 - "Community 4"
Cohesion: 0.06
Nodes (75): abortSvelteComponentInjection(), applySavedSessionMeta(), checkpointPayload(), clampVariantIndex(), closedClipPath(), commitAcceptedVariantToDom(), completeParameterGenerationIfReady(), completeParameterPublication() (+67 more)

### Community 5 - "Community 5"
Cohesion: 0.04
Nodes (39): makeBlockRateCoefficients(), onePoleCoefficientHz(), softClip(), softKneeReductionDb(), makeModulationDepths(), ModulationDepths, flutterDepth, speedBias (+31 more)

### Community 6 - "Community 6"
Cohesion: 0.03
Nodes (50): VulkanRenderer, acquiredSemaphorePending, activeImage, activeSlot, candidates, clearColour, commandBuffers, commandPool (+42 more)

### Community 7 - "Community 7"
Cohesion: 0.03
Nodes (44): TapeScene, attachAttemptsLeft, backgroundColour, bankBody, bankDrive, bankGlow, bankInteractiveIndex, bankKey (+36 more)

### Community 8 - "Community 8"
Cohesion: 0.05
Nodes (51): computeKnobMetrics(), FirstAudioProcessorEditor::shapersSpectrumBounds(), ActiveTabStub, controls, count, buildPage(), Built, areaHeight (+43 more)

### Community 9 - "Community 9"
Cohesion: 0.07
Nodes (61): actionLabel(), applyConfigureBarChrome(), bindConfigureCountPillTooltip(), bindConfigureInlineControlHover(), bindConfigureModifierPillHover(), buildConfigureActionControl(), buildConfigureCountControl(), buildConfigureRow() (+53 more)

### Community 10 - "Community 10"
Cohesion: 0.05
Nodes (35): configureControlSlider(), FirstAudioProcessorEditor::applyUiScaleToLabels(), FirstAudioProcessorEditor::createDecorativePhysics(), FirstAudioProcessorEditor::setUpControlStrip(), FirstAudioProcessorEditor::setUpDelayRow(), FirstAudioProcessorEditor::setUpInlineLists(), FirstAudioProcessorEditor::setUpLanguageAndTuning(), FirstAudioProcessorEditor::setUpModeSwitches() (+27 more)

### Community 11 - "Community 11"
Cohesion: 0.07
Nodes (46): PitchCorrectionRule, deadZoneCents, defaultRangeSemitones, maxOctave, maxRangeSemitones, maxRatio, minOctave, minRangeSemitones (+38 more)

### Community 12 - "Community 12"
Cohesion: 0.09
Nodes (53): ae(), be(), bt(), Ce(), Ct(), de(), dt(), _e() (+45 more)

### Community 13 - "Community 13"
Cohesion: 0.07
Nodes (56): agentHasWorkInFlight(), armPageChatForTyping(), attachSteerFocusDebug(), attachSteerFocusGuard(), buildSteerProcessingDots(), buildSteerQueueHint(), clearSteerAwaitTimer(), clearSteerFocusRecoverTimer() (+48 more)

### Community 14 - "Community 14"
Cohesion: 0.06
Nodes (39): Band, correction, magnitude, product, ButterworthSection, a1, a2, b0 (+31 more)

### Community 15 - "Community 15"
Cohesion: 0.04
Nodes (55): VinylStage, cartridgeBottomGain, cartridgeCoefficient, cartridgeHissGain, cartridgeNoiseGain, cartridgeTopGain, clickAmount, clickEnvelopeL (+47 more)

### Community 16 - "Community 16"
Cohesion: 0.04
Nodes (54): ChainReplica, biasAmount, dcBlockR, dcX, dcY, driveAmount, finalOutputGain, generator (+46 more)

### Community 17 - "Community 17"
Cohesion: 0.04
Nodes (31): TeardownScope, ControlSpec, caption, defaultValue, id, tab, AsyncFileChooser, active (+23 more)

### Community 19 - "Community 19"
Cohesion: 0.05
Nodes (34): StereoOffsetBuffer, length, samples, writePosition, State, play, start, stop (+26 more)

### Community 20 - "Community 20"
Cohesion: 0.07
Nodes (48): agentStatusText(), barPaletteForTheme(), brandMarkSvg(), buildDesignHeader(), buildParamsPanel(), cursorForInsertAxis(), designPanelCss(), detectPageTheme() (+40 more)

### Community 21 - "Community 21"
Cohesion: 0.07
Nodes (23): check(), format(), format2(), main(), MeterRun, meter, rate, window (+15 more)

### Community 22 - "Community 22"
Cohesion: 0.10
Nodes (16): main(), ValueTree::ValueTree(), ValueTreeData, depthOf(), escape(), Reader, position, text (+8 more)

### Community 23 - "Community 23"
Cohesion: 0.08
Nodes (37): BlockRateCoefficients, antiPhase, cabinetLow, cabinetPeak, limiterCatch, limiterCeiling, limiterDetectorAttack, limiterDetectorRelease (+29 more)

### Community 24 - "Community 24"
Cohesion: 0.04
Nodes (30): D3D12Renderer, activeBackBuffer, activeSlot, allocators, backBuffers, childWindow, clearColour, commandLists (+22 more)

### Community 25 - "Community 25"
Cohesion: 0.07
Nodes (25): FirstAudioProcessorEditor::refreshPresetList(), FirstAudioProcessor::auditFactoryPresets(), all(), FirstAudioProcessor::applyFactoryPreset(), FirstAudioProcessor::applyStateWithUndo(), FirstAudioProcessor::applyUserPreset(), FirstAudioProcessor::copyToCompareSlot(), FirstAudioProcessor::deleteUserPreset() (+17 more)

### Community 26 - "Community 26"
Cohesion: 0.05
Nodes (33): j37Tanh(), SaturationCore, ampBiasState, ampGain1, ampGain2, ampOff, ampWeight, cassetteBiasState (+25 more)

### Community 27 - "Community 27"
Cohesion: 0.07
Nodes (8): submitSceneFrame(), createBgfxRenderer(), createNativeRenderer(), createD3D11Renderer(), createD3D12Renderer(), NativeRenderer, createSokolRenderer(), createVulkanRenderer()

### Community 28 - "Community 28"
Cohesion: 0.06
Nodes (30): Wavefolder, dcLeft, dcRight, DeEsser, detector, rate, reduction, splitterLeft (+22 more)

### Community 29 - "Community 29"
Cohesion: 0.05
Nodes (39): DattorroRoom, active, controlCoefficient, currentDamping, currentLowCut, currentSize, currentWidth, lastDamping (+31 more)

### Community 30 - "Community 30"
Cohesion: 0.06
Nodes (19): SokolRenderer, context, deviceReady, deviceSetupFailed, drawFailure, failure, frameMutex, glowPipeline (+11 more)

### Community 31 - "Community 31"
Cohesion: 0.05
Nodes (30): CorrectorStage, activeChannels, blockFrames, blockPosition, blockReady, fallbackShifters, framesHeld, inputRings (+22 more)

### Community 32 - "Community 32"
Cohesion: 0.09
Nodes (3): Diag, Thread.UncaughtExceptionHandler, MainActivity

### Community 33 - "Community 33"
Cohesion: 0.06
Nodes (29): CombFilter, delaySamples, feedbackLeft, feedbackRight, leftDamping, leftDelay, rate, rightDamping (+21 more)

### Community 34 - "Community 34"
Cohesion: 0.06
Nodes (32): AntiSpike, detector, filled, historyLeft, historyRight, lastLeft, lastRight, rate (+24 more)

### Community 35 - "Community 35"
Cohesion: 0.08
Nodes (18): NoiseLoader, activePrint, controlLock, engineRate, generation, lastErrorMessage, loadedFileNameText, loadingFlag (+10 more)

### Community 36 - "Community 36"
Cohesion: 0.10
Nodes (27): panelFont(), applyPreset(), closeBounds(), defaultWindowIcon(), deserializeUserPalette(), getPalette(), mouseDown(), mouseExit() (+19 more)

### Community 37 - "Community 37"
Cohesion: 0.07
Nodes (18): AudioParameterBool, defaultValue, value, AudioParameterFloatAttributes, unit, AudioProcessorValueTreeState, parameters, type (+10 more)

### Community 38 - "Community 38"
Cohesion: 0.06
Nodes (19): FirstAudioProcessorEditor::setUpModelButtons(), FirstAudioProcessor::acceptsMidi(), FirstAudioProcessor::clearIr(), FirstAudioProcessor::createEditor(), FirstAudioProcessor::getCurrentProgram(), FirstAudioProcessor::getName(), FirstAudioProcessor::getNumPrograms(), FirstAudioProcessor::getTailLengthSeconds() (+11 more)

### Community 39 - "Community 39"
Cohesion: 0.11
Nodes (21): blockFor(), check(), checkEqual(), checkRefusal(), checkTheMigration(), checkTheRoundTrip(), checkTheSessionLoad(), checkTheTable() (+13 more)

### Community 40 - "Community 40"
Cohesion: 0.10
Nodes (20): FirstAudioProcessorEditor::toggleParameterSheet(), FirstAudioProcessorEditor::applyTheme(), comboTextInset(), J37InlineLookAndFeel::drawButtonBackground(), J37InlineLookAndFeel::drawButtonText(), J37InlineLookAndFeel::drawComboBox(), J37InlineLookAndFeel::drawLabel(), J37InlineLookAndFeel::drawToggleButton() (+12 more)

### Community 41 - "Community 41"
Cohesion: 0.06
Nodes (18): D3D11Renderer, childWindow, clearColour, constantBuffer, context, depthStencilView, device, failure (+10 more)

### Community 42 - "Community 42"
Cohesion: 0.06
Nodes (16): InputStage, diGroundHumPhase, diLoadState, distortionLowState, diTransformerState, preampBiasState, preampLowState, SampleClock (+8 more)

### Community 43 - "Community 43"
Cohesion: 0.08
Nodes (28): FirstAudioProcessorEditor::paint(), FirstAudioProcessorEditor::updateWorkflowButtons(), drawPanel(), drawScrew(), fillCylinderBarrel(), fillLitDome(), principleColour(), principleOf() (+20 more)

### Community 44 - "Community 44"
Cohesion: 0.06
Nodes (25): PitchTracker, analysisRate, analysisSize, analysisTargetRate, confidenceValue, decimation, decimationCounter, decimationSum (+17 more)

### Community 45 - "Community 45"
Cohesion: 0.06
Nodes (33): concurrency, cancel-in-progress, group, LOG_LEVEL, RENOVATE_ONBOARDING, RENOVATE_REPOSITORIES, RENOVATE_REQUIRE_CONFIG, TOKEN (+25 more)

### Community 47 - "Community 47"
Cohesion: 0.06
Nodes (31): FxRack, amounts, antiSpike, bitcrusher, chorus, comb, deEsser, dynamicEqualiser (+23 more)

### Community 48 - "Community 48"
Cohesion: 0.07
Nodes (29): ThreeBandEq, bellStateL, bellStateR, highCoefficient, highGain, hpActive, hpCoefficient, hpPoles (+21 more)

### Community 49 - "Community 49"
Cohesion: 0.07
Nodes (11): J37LookAndFeel::drawRotarySlider(), J37LookAndFeel::drawToggleButton(), J37LookAndFeel, activity, animationPhase, cameraPanel, cameraRoot, drift (+3 more)

### Community 50 - "Community 50"
Cohesion: 0.07
Nodes (16): FirstAudioProcessorEditor::CompressorMeter::CompressorMeter(), FirstAudioProcessorEditor::LevelMeter::LevelMeter(), FirstAudioProcessorEditor::MixBar::setShares(), FirstAudioProcessorEditor::rebuildPanelTextureLayers(), available(), cachedImage(), loadDrawable(), loadImage() (+8 more)

### Community 51 - "Community 51"
Cohesion: 0.07
Nodes (29): controlIsInTab(), index_of_tab_named(), tabsCoverAllControls(), TabSpec, controls, count, family, hint (+21 more)

### Community 52 - "Community 52"
Cohesion: 0.07
Nodes (26): Granular, grainLength, leftDelay, randomState, rate, rightDelay, untilNextGrain, voices (+18 more)

### Community 53 - "Community 53"
Cohesion: 0.08
Nodes (22): Converted, dry, wet, RateBridge, active, engineSamples, lookahead, modelSamples (+14 more)

### Community 54 - "Community 54"
Cohesion: 0.13
Nodes (24): add_defs(), atlas_image(), brand_atlas(), category_definitions(), control_ids(), cpp_names(), draw_help_card(), draw_icon() (+16 more)

### Community 55 - "Community 55"
Cohesion: 0.10
Nodes (19): FirstAudioProcessorEditor::applyFrozenPage(), FirstAudioProcessorEditor::handleTabPress(), FirstAudioProcessorEditor::keyPressed(), FirstAudioProcessorEditor::loadIrFile(), FirstAudioProcessorEditor::loadIrFromFile(), FirstAudioProcessorEditor::loadNeuralModelFile(), FirstAudioProcessorEditor::loadNeuralModelFromFile(), FirstAudioProcessorEditor::loadNoisePrintFile() (+11 more)

### Community 56 - "Community 56"
Cohesion: 0.06
Nodes (21): OscRing, ball, currentGain, currentSector, darkTheme, impactAge, impactLife, impactX (+13 more)

### Community 57 - "Community 57"
Cohesion: 0.16
Nodes (21): AutoGainCompensator, maximumRestorationDb, minimumOutputPower, smoothedDb, check(), compensationCoefficient(), decibels(), framePower() (+13 more)

### Community 58 - "Community 58"
Cohesion: 0.17
Nodes (4): Bitcrusher, Tremolo, lfo, rate

### Community 59 - "Community 59"
Cohesion: 0.06
Nodes (29): SubharmonicGenerator, armed, attackCoeff, baseWeights, cachedSampleRate, confidenceAttackCoeff, confidenceReleaseCoeff, cycleConfidence (+21 more)

### Community 60 - "Community 60"
Cohesion: 0.06
Nodes (30): FirstAudioProcessor::getTelemetry(), TelemetryFrame, antiPhaseAmount, bypassActive, compressorActivity, evenHarmonicRatio, harmonicCharacter, inputClipping (+22 more)

### Community 61 - "Community 61"
Cohesion: 0.15
Nodes (18): addBox(), addDisc(), addKnurl(), addQuad(), addSpoke(), addTriangle(), addVertex(), addVertexReturningIndex() (+10 more)

### Community 62 - "Community 62"
Cohesion: 0.16
Nodes (20): addressesAreValid(), appendString(), appendUint32(), Argument, integer, real, text, type (+12 more)

### Community 63 - "Community 63"
Cohesion: 0.07
Nodes (24): Flanger, baseDelay, depth, feedbackLeft, feedbackRight, leftDelay, leftLfo, rate (+16 more)

### Community 64 - "Community 64"
Cohesion: 0.07
Nodes (25): Band, badge, bandCount, heading, model, preset, switches, transport (+17 more)

### Community 65 - "Community 65"
Cohesion: 0.11
Nodes (15): buildFromNam(), buildWithNamCore(), convertLstm(), InferenceModel, isLstm(), NamCoreModel, backend, publish() (+7 more)

### Community 66 - "Community 66"
Cohesion: 0.14
Nodes (24): Backend, autoDetect, bgfx, cpu, directX11, directX12, metal, openGL (+16 more)

### Community 67 - "Community 67"
Cohesion: 0.11
Nodes (27): abandonForeignSession(), abandonSupersededGo(), applyOriginalAttrsToSvelteAnchor(), commitAcceptedSvelteComponentToDom(), componentModuleCandidates(), describeMountFailure(), detectDevServerBase(), discardOrphanedSession() (+19 more)

### Community 68 - "Community 68"
Cohesion: 0.12
Nodes (10): threadSlotName(), describeAddress(), dumpThread(), ScopedPhase, ShutdownWatchdog, armed, began, finished (+2 more)

### Community 69 - "Community 69"
Cohesion: 0.07
Nodes (25): Chorus, baseDelay, depth, leftDelay, leftLfo, rate, rightDelay, rightLfo (+17 more)

### Community 70 - "Community 70"
Cohesion: 0.12
Nodes (20): editorPreferences(), rememberedEditorSize(), rememberEditorSize(), rememberedRenderBackend(), rememberedThemeChoice(), rememberedUiScale(), rememberedUserPalette(), rememberRenderBackend() (+12 more)

### Community 71 - "Community 71"
Cohesion: 0.15
Nodes (20): FirstAudioProcessorEditor::CompressorMeter::paint(), FirstAudioProcessorEditor::LevelMeter::paint(), FirstAudioProcessorEditor::MixBar::paint(), FirstAudioProcessorEditor::PresetArtworkBadge::paint(), FirstAudioProcessorEditor::applyRenderBackend(), FirstAudioProcessorEditor::applyUiScale(), FirstAudioProcessorEditor::setUpNativeScene(), FirstAudioProcessorEditor::setUpOpenGlRendering() (+12 more)

### Community 72 - "Community 72"
Cohesion: 0.15
Nodes (20): audioReportText(), beginRun(), currentProcessId(), currentThreadId(), enabled(), environmentSaysOn(), logPath(), mutableLogPath() (+12 more)

### Community 73 - "Community 73"
Cohesion: 0.25
Nodes (26): addAmpVoicingControls(), addColourControls(), addCompressorControls(), addDeckControls(), addDelaySyncControls(), addDiBoxControls(), addEffectsRackControls(), addEqualiserControls() (+18 more)

### Community 74 - "Community 74"
Cohesion: 0.12
Nodes (15): Octaver, rate, shifter, subLeft, subRight, PitchShifter, fraction, leftDelay (+7 more)

### Community 75 - "Community 75"
Cohesion: 0.08
Nodes (25): concurrency, cancel-in-progress, group, name, permissions, runs-on, steps, timeout-minutes (+17 more)

### Community 76 - "Community 76"
Cohesion: 0.08
Nodes (26): FxTargets, antiSpike, bitcrusher, chorus, comb, deEsser, dynamicEq, flanger (+18 more)

### Community 77 - "Community 77"
Cohesion: 0.12
Nodes (22): advance(), Impact, normalX, normalY, speed, isFinite(), nextRandom(), sanitise() (+14 more)

### Community 78 - "Community 78"
Cohesion: 0.12
Nodes (12): extract_sources(), main(), unescape(), body_of(), brace_block(), check_constructor_phases(), check_deck_switch(), constant() (+4 more)

### Community 79 - "Community 79"
Cohesion: 0.12
Nodes (15): decodeWavMemory(), NoiseDecodeOutcome, fileChannels, frames, reason, status, NoisePrint, left (+7 more)

### Community 80 - "Community 80"
Cohesion: 0.10
Nodes (5): harnesses(), hits(), read(), source_files(), snap()

### Community 81 - "Community 81"
Cohesion: 0.08
Nodes (25): Amounts, antiSpike, bitcrusher, chorus, comb, deEsser, dynamicEq, flanger (+17 more)

### Community 82 - "Community 82"
Cohesion: 0.13
Nodes (11): Hit, normalised, slot, ParameterSlot, id, maximum, minimum, path (+3 more)

### Community 83 - "Community 83"
Cohesion: 0.19
Nodes (24): clearStoredManualApplyState(), fetchPendingCount(), handleManualEditActivity(), hidePendingApplyDock(), manualApplyLoadingText(), manualApplyStateKey(), manualEditEventForCurrentPage(), numberOrNull() (+16 more)

### Community 84 - "Community 84"
Cohesion: 0.08
Nodes (23): name, runs-on, steps, timeout-minutes, env, BUILD_TYPE, CC, CXX (+15 more)

### Community 85 - "Community 85"
Cohesion: 0.13
Nodes (10): fail(), main(), method_body(), call_arguments(), without_cmake_comments(), without_comments(), includes(), link_targets() (+2 more)

### Community 86 - "Community 86"
Cohesion: 0.09
Nodes (18): RepitchStage, bufferSeconds, consumeRate, hardCapSamples, hardCapSeconds, left, length, maxPullUp (+10 more)

### Community 87 - "Community 87"
Cohesion: 0.10
Nodes (17): SpectrumAnalyser, bandCount, bandsDb, compiledIn, edgesHz, fftSize, fill, floorDb (+9 more)

### Community 88 - "Community 88"
Cohesion: 0.10
Nodes (14): SceneFrame, backgroundColour, draws, drive, flutter, glow, height, highlightColour (+6 more)

### Community 89 - "Community 89"
Cohesion: 0.20
Nodes (20): check(), energyEnvelope(), firstWetOnset(), highFrequencyEnergyRatio(), lowFrequencyEnergy(), renderImpulse(), rms(), sideEnergy() (+12 more)

### Community 90 - "Community 90"
Cohesion: 0.11
Nodes (16): TransientShaper, appliedGain, fastEnvelope, slowEnvelope, peakIn(), pluck(), rmsIn(), through() (+8 more)

### Community 91 - "Community 91"
Cohesion: 0.27
Nodes (18): check(), main(), run(), testDurationsAndScalesAreSane(), testEngineFacingProperties(), testPlayIsSteady(), testRepitchBendsByThePlatter(), testRepitchIsIdentityAtSpeed() (+10 more)

### Community 92 - "Community 92"
Cohesion: 0.12
Nodes (8): AudioParameterChoice, choices, defaultIndex, index, jlimit(), jmax(), StringArray, values

### Community 93 - "Community 93"
Cohesion: 0.11
Nodes (13): HarmonicAnalyser, evenRatio, fundamentalLevel, lastMagnitude, oddRatio, previous1, previous2, previousPositive (+5 more)

### Community 94 - "Community 94"
Cohesion: 0.24
Nodes (20): onePoleCoefficient(), check(), main(), makeTransientTrain(), peakToSustainRatio(), renderTransientShaper(), showOldArrangement(), testBlockRateCoefficients() (+12 more)

### Community 95 - "Community 95"
Cohesion: 0.11
Nodes (16): Biquad, a1, a2, b0, b1, b2, x1, x2 (+8 more)

### Community 96 - "Community 96"
Cohesion: 0.14
Nodes (13): ModalVoice, excitation, modeCount, modes, modeSpecs, outputTrim, sampleRate, ModeSpec (+5 more)

### Community 97 - "Community 97"
Cohesion: 0.09
Nodes (18): LevelMeter, active, clipping, combinedDb, darkTheme, displayedCombined, displayedLufs, displayedRms (+10 more)

### Community 98 - "Community 98"
Cohesion: 0.15
Nodes (21): applyPlaceholderDimensions(), beginEditPin(), buildAnnotationsForCapture(), buildPinElement(), cancelEditingPin(), clampPlaceholderSize(), finalizeEditingPin(), initAnnotOverlay() (+13 more)

### Community 99 - "Community 99"
Cohesion: 0.10
Nodes (20): name, runs-on, steps, timeout-minutes, env, BUILD_TYPE, J37_BUILD_JOBS, jobs (+12 more)

### Community 100 - "Community 100"
Cohesion: 0.10
Nodes (10): NamBackend, inputFrame, inputPointers, lastErrorMessage, maxFrames, model, modelRate, outputFrame (+2 more)

### Community 101 - "Community 101"
Cohesion: 0.11
Nodes (16): EnvelopeWah, detector, leftFilter, rate, rightFilter, Output, band, high (+8 more)

### Community 102 - "Community 102"
Cohesion: 0.10
Nodes (20): computeKnobPerspective(), KnobMetrics, collar, faceCeiling, minorClearance, minorGap, minorLength, paintCeiling (+12 more)

### Community 103 - "Community 103"
Cohesion: 0.20
Nodes (15): Bytes, exercise(), failed(), LLVMFuzzerTestOneInput(), main(), makeWav(), randomBuffers(), report() (+7 more)

### Community 104 - "Community 104"
Cohesion: 0.22
Nodes (20): actOnAgentTarget(), agentTargetBusyReason(), agentTargetOverlayGone(), agentTargetTaken(), claimAgentTarget(), claimAndActOnAgentTarget(), declineAgentTargetBusy(), declineAgentTargetUnresolvable() (+12 more)

### Community 105 - "Community 105"
Cohesion: 0.12
Nodes (20): addManualContextText(), canRestoreManualEditElement(), collectManualContextPieces(), walk(), contextElementForManualEdit(), cssIdent(), directMixedTextRestoreNodes(), findManualEditRestoreElement() (+12 more)

### Community 106 - "Community 106"
Cohesion: 0.16
Nodes (15): createLiveBrowserDomHelpers(), activeElementDeep(), cloneWithoutChrome(), copyTextViaSelection(), cssId(), liveUiRoot(), makeFrozenAnchor(), raiseHitModal() (+7 more)

### Community 107 - "Community 107"
Cohesion: 0.19
Nodes (17): createLiveBrowserSessionState(), clearHandled(), clearScrollY(), clearSession(), isHandled(), loadSession(), markHandled(), nextCheckpointRevision() (+9 more)

### Community 108 - "Community 108"
Cohesion: 0.16
Nodes (6): FileDropButton, acceptsFiles, dragActive, onFileDropped, SpindownButton, onHoldChanged

### Community 109 - "Community 109"
Cohesion: 0.15
Nodes (14): coreShare(), Cost, bestNanoseconds, medianNanoseconds, fail(), makeSignal(), measure(), readJson() (+6 more)

### Community 110 - "Community 110"
Cohesion: 0.12
Nodes (15): J37LookAndFeel::offsetToTopLevel(), J37LookAndFeel::perspectiveFor(), J37LookAndFeel::setPanelCamera(), curve(), levelToY(), Point, x, y (+7 more)

### Community 111 - "Community 111"
Cohesion: 0.16
Nodes (19): computePhoneEditorLayout(), computePhoneLandscapeTabStrip(), computePhoneOscRingBounds(), isPhoneLandscape(), nonNegative(), PhoneEditorLayout, controls, deck (+11 more)

### Community 112 - "Community 112"
Cohesion: 0.19
Nodes (11): check_launcher(), check_library(), contains_string(), fail(), main(), cpp_string_array(), main(), png_has_alpha() (+3 more)

### Community 113 - "Community 113"
Cohesion: 0.10
Nodes (11): FirstAudioProcessor, activeSlot, compareDirty, compareSlots, currentPresetName, dirtyBadgeParameters, lastPresetIndex, parameters (+3 more)

### Community 114 - "Community 114"
Cohesion: 0.11
Nodes (10): AudioParameterFloat, attributes, defaultValue, range, value, NormalisableRange, centrePoint, end (+2 more)

### Community 115 - "Community 115"
Cohesion: 0.14
Nodes (8): finite(), isValid(), ControlPlacement, isKey, x, ControlTable, framedOk(), inspectDraw()

### Community 116 - "Community 116"
Cohesion: 0.11
Nodes (19): runs-on, runs-on, name, runs-on, timeout-minutes, runs-on, name, runs-on (+11 more)

### Community 117 - "Community 117"
Cohesion: 0.14
Nodes (12): Fuzz, leftShaper, rightShaper, Overdrive, leftShaper, rightShaper, RingModulator, phase (+4 more)

### Community 118 - "Community 118"
Cohesion: 0.11
Nodes (9): CompressorMeter, active, activity, caption, darkTheme, displayedDb, lastUpdate, title (+1 more)

### Community 119 - "Community 119"
Cohesion: 0.16
Nodes (9): check_auto_gain_compensation(), check_mono_delay_feedback(), check_neural_rate_wiring(), compensation_block(), impl_source_of(), main(), matching_brace(), mix_inverts() (+1 more)

### Community 120 - "Community 120"
Cohesion: 0.13
Nodes (7): AudioProcessor, MemoryBlock, bytes, data, XmlElement, name, payload

### Community 121 - "Community 121"
Cohesion: 0.11
Nodes (8): IrStage, convolverL, convolverR, hasIr, lastLoadWasStereo, preparedChannels, preparedMaxBlock, preparedSampleRate

### Community 122 - "Community 122"
Cohesion: 0.14
Nodes (10): UniformCache, fragmentApplied, fragmentBlock, fragmentGeneration, generation, skippedBlocks, uploadedBlocks, vertexApplied (+2 more)

### Community 123 - "Community 123"
Cohesion: 0.24
Nodes (14): fill(), HeadShape, bias, headShape(), kernel, size, lstmBlocks(), lstmWeightCount() (+6 more)

### Community 124 - "Community 124"
Cohesion: 0.16
Nodes (8): brace_end(), cut_banner_block(), cut_block(), cut_line(), header_source(), main(), processor_text(), source_of()

### Community 125 - "Community 125"
Cohesion: 0.11
Nodes (11): Kind, boolean, integer, nothing, number, text, var, integer (+3 more)

### Community 126 - "Community 126"
Cohesion: 0.22
Nodes (13): logLock(), BlockedThread, label, letGo, mutex, released, thread, check() (+5 more)

### Community 127 - "Community 127"
Cohesion: 0.32
Nodes (17): steps, steps, steps, steps, steps, steps, steps, steps (+9 more)

### Community 128 - "Community 128"
Cohesion: 0.14
Nodes (8): ParameterSheet, available, closeButton, palette, ParameterSheet::ParameterSheet(), sheet, subtitleLabel, titleLabel

### Community 129 - "Community 129"
Cohesion: 0.12
Nodes (14): SceneDraw, baseColour, highlightColour, material, meshIndex, model, normalMatrix, parameters (+6 more)

### Community 130 - "Community 130"
Cohesion: 0.17
Nodes (14): check(), Control, j37dsp, name, neutral, main(), Result, finite (+6 more)

### Community 131 - "Community 131"
Cohesion: 0.20
Nodes (9): brace_end(), editor_source(), function_bodies(), look_and_feel_members(), main(), reached_text(), strip_noise(), widget_members() (+1 more)

### Community 132 - "Community 132"
Cohesion: 0.12
Nodes (16): Drive, frames, held, hostRolling, play, rate, rig, start (+8 more)

### Community 133 - "Community 133"
Cohesion: 0.33
Nodes (14): isGesture(), relockOn(), seconds(), spindownReturnOn(), testAHeldSpindownOutranksTheArm(), testAParkedHostOnlyHoldsStart(), testArmedStartFollowsTheHostBackDown(), testArmedStartResumesWhereTheCoastStopped() (+6 more)

### Community 134 - "Community 134"
Cohesion: 0.13
Nodes (12): rmsIn(), settledLevelDb(), "StandardCompressor: a higher ratio reduces further", "StandardCompressor: a ratio below one behaves as ratio one rather than as a gate", "StandardCompressor: a signal under the threshold passes at unity", "StandardCompressor: a sine at the threshold is already just into the slope", "StandardCompressor: above the knee the reduction is the ratio's slope", "StandardCompressor: instances keep their own detector state" (+4 more)

### Community 135 - "Community 135"
Cohesion: 0.17
Nodes (4): TapeScene::newOpenGLContextCreated(), TapeScene::openGLContextClosing(), TapeScene::sharedSceneMeshes(), prepare()

### Community 136 - "Community 136"
Cohesion: 0.12
Nodes (15): :dependencyDashboard, schedule:automergeDaily, automerge, automergeType, branchPrefix, customManagers, enabledManagers, extends (+7 more)

### Community 137 - "Community 137"
Cohesion: 0.20
Nodes (3): NeuralStage, FirstAudioProcessor::clearNeuralModel(), main()

### Community 138 - "Community 138"
Cohesion: 0.12
Nodes (14): ChainSettings, bias, brightness, character, drive, flutter, injectInsideTapePath, inputDb (+6 more)

### Community 139 - "Community 139"
Cohesion: 0.27
Nodes (13): check(), drive(), main(), measure(), Partial, amplitude, phase, run() (+5 more)

### Community 140 - "Community 140"
Cohesion: 0.22
Nodes (10): PlanDeleter, process(), transform(), fftwf_complex, fftwf_destroy_plan(), fftwf_execute_dft_r2c(), fftwf_plan, fftwf_plan_dft_r2c_1d() (+2 more)

### Community 141 - "Community 141"
Cohesion: 0.13
Nodes (9): Sheet, builder, canvas, groupCount, hiddenByVirtualization, palette, parameterCount, state (+1 more)

### Community 142 - "Community 142"
Cohesion: 0.13
Nodes (13): Case, poles, slope, "ThreeBandEq: a neutral stage is off by the addition's rounding and nothing else", "ThreeBandEq: at neutral settings the stage is an identity to within a float step", "ThreeBandEq: HIGH lifts the top band and leaves the bottom alone", "ThreeBandEq: LOW is a shelf - it moves the low band and leaves the top alone", "ThreeBandEq: MID acts on its own band and only a little on the ends" (+5 more)

### Community 143 - "Community 143"
Cohesion: 0.18
Nodes (8): Coefficients, attack, release, slow, GlueCompressor, envelope, slowEnvelope, makeGlueOperatingPoints()

### Community 144 - "Community 144"
Cohesion: 0.13
Nodes (3): CpuRenderer, bounds, initialised

### Community 145 - "Community 145"
Cohesion: 0.48
Nodes (14): addsHarmonics(), check(), checkBypass(), checkCrushers(), checkDrivesAddHarmonics(), checkFilters(), checkGateAndSpike(), checkMultibandCompressor() (+6 more)

### Community 146 - "Community 146"
Cohesion: 0.33
Nodes (12): asString(), bundleOf(), check(), checkAddressGrammar(), checkArgumentReader(), checkBallPhysics(), checkBundles(), checkEncoding() (+4 more)

### Community 147 - "Community 147"
Cohesion: 0.21
Nodes (7): check_engine_arm(), check_engine_gate(), check_engine_repitch(), couplings_source(), editor_source(), main(), processor_impl()

### Community 148 - "Community 148"
Cohesion: 0.24
Nodes (9): check(), compare(), main(), makeNoise(), makeTone(), referenceReading(), StereoBuffer, frames (+1 more)

### Community 149 - "Community 149"
Cohesion: 0.15
Nodes (14): name, runs-on, strategy, timeout-minutes, name, runs-on, strategy, timeout-minutes (+6 more)

### Community 150 - "Community 150"
Cohesion: 0.14
Nodes (13): GlueOperatingPoints, inputCoefficients, inputCompressorRatio, inputKneeDb, inputMakeupFraction, inputReductionLimitDb, inputThresholdDb, outputCoefficients (+5 more)

### Community 151 - "Community 151"
Cohesion: 0.20
Nodes (8): LofiHoldClock, countdown, LofiHold, clock, held, holdFrames, main(), sameFrame()

### Community 152 - "Community 152"
Cohesion: 0.24
Nodes (10): check(), format(), format2(), inputs(), main(), Principle, name, which (+2 more)

### Community 153 - "Community 153"
Cohesion: 0.16
Nodes (6): DwellTooltipWindow, dwell, TipDwell, allowedAtMs, control, dwellMs

### Community 154 - "Community 154"
Cohesion: 0.15
Nodes (9): FirstAudioProcessorEditor::nativeSceneConfig(), Config, framesInFlight, height, positionX, positionY, validation, vsyncInterval (+1 more)

### Community 156 - "Community 156"
Cohesion: 0.32
Nodes (11): DrawUniforms, fragment, vertex, makeDrawUniforms(), check(), checkCache(), checkMapping(), checkMaterials() (+3 more)

### Community 157 - "Community 157"
Cohesion: 0.14
Nodes (12): VulkanSceneUniforms, baseColour, globals, highlightColour, lightDirection, model, normalMatrix, parameters (+4 more)

### Community 158 - "Community 158"
Cohesion: 0.14
Nodes (5): Rect, h, w, x, y

### Community 159 - "Community 159"
Cohesion: 0.37
Nodes (10): buildAndAdopt(), check(), fixtureShapeSupported(), inputSample(), isFiniteAndBounded(), main(), readFixture(), referenceWaveNet() (+2 more)

### Community 160 - "Community 160"
Cohesion: 0.32
Nodes (10): check(), install(), main(), minimalNamFile(), parseJson(), run(), sineSample(), testModelIsDrivenAtItsOwnRate() (+2 more)

### Community 161 - "Community 161"
Cohesion: 0.21
Nodes (4): main(), read_bytes(), to_c_array(), main()

### Community 162 - "Community 162"
Cohesion: 0.15
Nodes (8): Mesh, indexBuffer, indexBufferView, indexCount, startSlot, vertexBuffer, vertexBufferView, vertexStride

### Community 163 - "Community 163"
Cohesion: 0.15
Nodes (12): env, BUILD_TYPE, J37_BUILD_JOBS, name, on, pull_request, push, workflow_dispatch (+4 more)

### Community 164 - "Community 164"
Cohesion: 0.15
Nodes (8): setSink(), Sink, ThreadSlot, audio, count, gl, message, diagnosticsFilePath()

### Community 165 - "Community 165"
Cohesion: 0.32
Nodes (5): colour3(), colour4(), matrix4(), TapeScene::appendDraw(), TapeScene::buildSceneFrame()

### Community 166 - "Community 166"
Cohesion: 0.15
Nodes (10): TapeCondition, driftCountdown, driftState, driftTarget, fluxLowL, fluxLowR, wearLowL, wearLowR (+2 more)

### Community 168 - "Community 168"
Cohesion: 0.38
Nodes (10): check(), main(), makeTestPrint(), ramp(), silentStage(), testBuildShape(), testDecodeShapes(), testRateConversion() (+2 more)

### Community 169 - "Community 169"
Cohesion: 0.35
Nodes (11): isIntrinsicFrequency(), measureToneAmplitude(), printChainSpectrum(), printSpectrum(), render(), reportUndertoneHarmonics(), rms(), testMixDirection() (+3 more)

### Community 170 - "Community 170"
Cohesion: 0.18
Nodes (5): LiveSelector, onColour, PaletteField, name, UiPalette

### Community 172 - "Community 172"
Cohesion: 0.26
Nodes (6): StereoDelayFeedback, main(), near(), runRouting(), check(), main()

### Community 174 - "Community 174"
Cohesion: 0.23
Nodes (6): Gains, dry, wet, MixCrossfader, halfPi, openThreshold

### Community 175 - "Community 175"
Cohesion: 0.17
Nodes (12): FsUniforms, arcAngle, baseColour, drive, glow, highlightColour, keyDown, lightDirection (+4 more)

### Community 176 - "Community 176"
Cohesion: 0.17
Nodes (10): RecordingModel, inputs, Run, active, delay, dry, input, modelInputs (+2 more)

### Community 177 - "Community 177"
Cohesion: 0.38
Nodes (10): bandOf(), check(), feedTone(), format(), format2(), main(), peakBand(), prepared() (+2 more)

### Community 178 - "Community 178"
Cohesion: 0.31
Nodes (11): acceptedDomAlreadyClean(), clearHandledWrapperReloadStamp(), deferredRecoverySuperseded(), ensureAcceptedDomClean(), findAcceptedRuntimeWrappers(), handledWrapperReloadKey(), reloadAfterMissingAcceptedDom(), restoreAcceptedDomFromSnapshot() (+3 more)

### Community 179 - "Community 179"
Cohesion: 0.24
Nodes (4): main(), makeDetectorCoefficients(), makeSignal(), reportBudget()

### Community 181 - "Community 181"
Cohesion: 0.18
Nodes (4): FirstAudioProcessorEditor::styleLabel(), FirstAudioProcessorEditor::styleSpindownButton(), FirstAudioProcessorEditor::styleTabButtons(), FirstAudioProcessorEditor::styleTransportButtons()

### Community 182 - "Community 182"
Cohesion: 0.38
Nodes (8): ampTypeNameList(), digitalTypeNameList(), transformerTypeNameList(), valveTypeNameList(), FirstAudioProcessorEditor::setUpDeckSelectors(), addTypeSwitchControls(), tapeStockNameList(), vinylTypeNameList()

### Community 183 - "Community 183"
Cohesion: 0.24
Nodes (7): Coefficients, attack, release, StandardCompressor, envelope, gain, render()

### Community 184 - "Community 184"
Cohesion: 0.18
Nodes (10): SceneMaterial, controlPanel, glow, key, knob, ribbon, solid, tapePack (+2 more)

### Community 185 - "Community 185"
Cohesion: 0.33
Nodes (8): contains(), offsets(), ringNoteAt(), ringNoteCount(), ringSectorOfNote(), check(), checkMode(), main()

### Community 187 - "Community 187"
Cohesion: 0.20
Nodes (9): source, enabledPlugins, clangd-lsp@claude-plugins-official, feature-dev@claude-plugins-official, extraKnownMarketplaces, claude-plugins-official, $schema, repo (+1 more)

### Community 188 - "Community 188"
Cohesion: 0.20
Nodes (6): Mesh, indexBuffer, indexCount, indexFormat, vertexBuffer, vertexStride

### Community 189 - "Community 189"
Cohesion: 0.20
Nodes (10): with, distribution, fetch-depth, gradle-version, if-no-files-found, java-version, name, path (+2 more)

### Community 190 - "Community 190"
Cohesion: 0.20
Nodes (10): AudioReport, blocks, leftPeak, loudestPeak, nonFinitePeaks, nonFiniteSides, overBudget, rightPeak (+2 more)

### Community 191 - "Community 191"
Cohesion: 0.31
Nodes (4): main(), testBypassIsBitExact(), testSilenceStaysSilent(), testTheConversionIsAbsentAtTheTanksRate()

### Community 192 - "Community 192"
Cohesion: 0.20
Nodes (10): Rotary, centreDelay, depth, leftDelay, leftHornFilter, leftLfo, rate, rightDelay (+2 more)

### Community 193 - "Community 193"
Cohesion: 0.22
Nodes (9): makeTapeCouplings(), TapeCouplings, driveHissCoupling, headGapModulationMask, loadInducedSpeedError, platterGateExponent, platterPitchExponent, transportActivityGate (+1 more)

### Community 194 - "Community 194"
Cohesion: 0.22
Nodes (9): makeTapeModulation(), TapeModulation, biasCurve, driveCurve, flutterCurve, flutterFreq, speedScale, wowCurve (+1 more)

### Community 195 - "Community 195"
Cohesion: 0.20
Nodes (4): ModulationFrame, flutter, grain, wow

### Community 197 - "Community 197"
Cohesion: 0.20
Nodes (7): MeshBinding, index, indexCount, indexMemory, vertex, vertexCount, vertexMemory

### Community 198 - "Community 198"
Cohesion: 0.20
Nodes (4): MixBar, darkTheme, shares, total

### Community 199 - "Community 199"
Cohesion: 0.27
Nodes (4): BlockProbe, measuring, sampleRate, started

### Community 200 - "Community 200"
Cohesion: 0.47
Nodes (8): check(), main(), testBothLegsAreGated(), testClamp(), testEndpoints(), testEqualPower(), testLevelOnRenderedSignal(), testOpenThreshold()

### Community 201 - "Community 201"
Cohesion: 0.29
Nodes (4): cpp_strings(), juce_unescape(), main(), parse_table()

### Community 202 - "Community 202"
Cohesion: 0.22
Nodes (3): FirstAudioProcessorEditor::UiSoundEngine::audioDeviceAboutToStart(), FirstAudioProcessorEditor::UiSoundEngine::audioDeviceIOCallbackWithContext(), FirstAudioProcessorEditor::UiSoundEngine::trigger()

### Community 203 - "Community 203"
Cohesion: 0.22
Nodes (7): AmpVoicing, cabinetLow1, cabinetLow2, cabinetPeak, presenceHighState, sagEnvelope, sagGain

### Community 204 - "Community 204"
Cohesion: 0.25
Nodes (8): makeTapeVoicing(), TapeVoicing, headDampingHz, hysteresis, instrumentTransportScale, tapeAsymmetry, tapeCurve, tapeHiss

### Community 205 - "Community 205"
Cohesion: 0.42
Nodes (7): chipCaptionWidth(), ellipsisedChipCaptionCount(), fittedChipCaptionHeight(), buttonWidthFor(), check(), main(), visiblePagesIn()

### Community 206 - "Community 206"
Cohesion: 0.22
Nodes (9): VsUniforms, flutter, mode, model, normalMatrix, packInner, packOuter, time (+1 more)

### Community 208 - "Community 208"
Cohesion: 0.25
Nodes (7): interval, updates, directory, open-pull-requests-limit, package-ecosystem, schedule, version

### Community 210 - "Community 210"
Cohesion: 0.25
Nodes (7): Settings, damping, lowCutHz, mix, preDelayMs, size, width

### Community 214 - "Community 214"
Cohesion: 0.46
Nodes (7): computeOscDisplayPlate(), OscDisplayPlate, height, width, oscNoteFontHeight(), oscNoteTextHeight(), checkRingDisplayPlate()

### Community 215 - "Community 215"
Cohesion: 0.25
Nodes (8): ControlPlacement, angle, colour, isKey, isLit, radius, x, y

### Community 217 - "Community 217"
Cohesion: 0.29
Nodes (3): StateSwapAction, after, before

### Community 218 - "Community 218"
Cohesion: 0.29
Nodes (3): git(), normalise(), parse_gitmodules()

### Community 219 - "Community 219"
Cohesion: 0.46
Nodes (5): check(), drive(), inputSample(), main(), readFixture()

### Community 220 - "Community 220"
Cohesion: 0.32
Nodes (4): extract_sources(), read(), unescape(), without_comments()

### Community 221 - "Community 221"
Cohesion: 0.39
Nodes (7): scaleName(), spindownCoastOn(), spinUpOn(), stopCoastOn(), testTapeSpeedScalesTheCapstan(), testTempoScalesEveryGesture(), testVinylSpeedScalesThePlatter()

### Community 222 - "Community 222"
Cohesion: 0.52
Nodes (6): globToRegex(), matchesScope(), normalizeIgnoreRule(), normalizeIgnoreValue(), pageCandidates(), resolveDetectIgnores()

### Community 223 - "Community 223"
Cohesion: 0.38
Nodes (3): ParameterSheet::Sheet::appendGroup(), ParameterSheet::Sheet::buildTree(), ParameterSheet::Sheet::countParameters()

### Community 226 - "Community 226"
Cohesion: 0.33
Nodes (5): RateCrusher, counter, heldLeft, heldRight, hold

### Community 227 - "Community 227"
Cohesion: 0.29
Nodes (7): NoiseDecodeStatus, absent, noFrames, nonFinite, ok, tooLong, unreadable

### Community 228 - "Community 228"
Cohesion: 0.33
Nodes (4): StereoWidthFrame, stereoWidthFrame(), left, right

### Community 229 - "Community 229"
Cohesion: 0.33
Nodes (6): TapeTransportFrame, tapeTransportFrame(), platterGate, platterSpeed, speedError, spindown

### Community 230 - "Community 230"
Cohesion: 0.57
Nodes (5): checkModulation(), differenceFrom(), distinctValues(), levelSwing(), rmsRange()

### Community 231 - "Community 231"
Cohesion: 0.29
Nodes (7): Parameters, attackMs, kneeDb, makeupDb, ratio, releaseMs, thresholdDb

### Community 232 - "Community 232"
Cohesion: 0.33
Nodes (4): Mesh, indexBuffer, indexCount, vertexBuffer

### Community 233 - "Community 233"
Cohesion: 0.33
Nodes (6): ModeState, a1, a2, b0, y1, y2

### Community 237 - "Community 237"
Cohesion: 0.33
Nodes (5): ControlBank, body, key, knob, knobGlow

### Community 238 - "Community 238"
Cohesion: 0.40
Nodes (5): hotPathProbeSnapshot(), HotPathProbeStats, activeSamples, lastBlockActive, totalSamples

### Community 239 - "Community 239"
Cohesion: 0.53
Nodes (4): build_noise(), build_seam(), run_seam(), run.sh script

### Community 241 - "Community 241"
Cohesion: 0.33
Nodes (4): Crossings, crossings, frames, previous

### Community 242 - "Community 242"
Cohesion: 0.40
Nodes (4): interface, default_prompt, display_name, short_description

### Community 243 - "Community 243"
Cohesion: 0.40
Nodes (5): continue-on-error, name, runs-on, timeout-minutes, dsp-bench

### Community 244 - "Community 244"
Cohesion: 0.40
Nodes (4): GpuMesh, indexCount, indices, vertices

### Community 246 - "Community 246"
Cohesion: 0.50
Nodes (4): TrackBleedFrame, trackBleedFrame(), left, right

### Community 249 - "Community 249"
Cohesion: 0.40
Nodes (5): EditorLayout, controls, deck, header, meters

### Community 250 - "Community 250"
Cohesion: 0.40
Nodes (3): Sine, phase, step

### Community 252 - "Community 252"
Cohesion: 0.40
Nodes (3): Sine, phase, step

### Community 253 - "Community 253"
Cohesion: 0.50
Nodes (3): interface, display_name, short_description

### Community 254 - "Community 254"
Cohesion: 0.50
Nodes (3): about, licence_policy, slots

### Community 255 - "Community 255"
Cohesion: 0.50
Nodes (4): name, runs-on, timeout-minutes, build-linux-arm64

### Community 256 - "Community 256"
Cohesion: 0.50
Nodes (4): name, runs-on, timeout-minutes, build-linux-debug

### Community 258 - "Community 258"
Cohesion: 0.50
Nodes (4): TrailPoint, age, x, y

### Community 261 - "Community 261"
Cohesion: 0.50
Nodes (4): Type, float32, int32, string

### Community 262 - "Community 262"
Cohesion: 0.50
Nodes (4): ToggleGlide, initialised, position, target

### Community 264 - "Community 264"
Cohesion: 0.50
Nodes (4): Pair, engine, model, name

### Community 265 - "Community 265"
Cohesion: 0.67
Nodes (3): name, runs-on, build-android

## Knowledge Gaps
- **2175 isolated node(s):** `activeSlot`, `activeTapeType`, `ampBiasParam`, `ampBiasSmoothed`, `ampL` (+2170 more)
  These have ≤1 connection - possible missing edges. (Counts symbols only; 3210 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **78 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `FirstAudioProcessor` connect `Community 0` to `Community 3`, `Community 5`, `Community 137`, `Community 14`, `Community 143`, `Community 15`, `Community 270`, `Community 18`, `Community 19`, `Community 276`, `Community 277`, `Community 22`, `Community 151`, `Community 23`, `Community 25`, `Community 26`, `Community 29`, `Community 31`, `Community 34`, `Community 35`, `Community 37`, `Community 166`, `Community 38`, `Community 42`, `Community 44`, `Community 47`, `Community 48`, `Community 57`, `Community 59`, `Community 60`, `Community 73`, `Community 203`, `Community 76`, `Community 82`, `Community 86`, `Community 87`, `Community 216`, `Community 217`, `Community 90`, `Community 93`, `Community 95`, `Community 96`, `Community 235`, `Community 245`, `Community 120`, `Community 121`?**
  _High betweenness centrality (0.211) - this node is a cross-community bridge._
- **Are the 2 inferred relationships involving `FirstAudioProcessor` (e.g. with `.perform()` and `.undo()`) actually correct?**
  _`FirstAudioProcessor` has 2 INFERRED edges - model-reasoned connections that need verification._
- **What connects `activeSlot`, `activeTapeType`, `ampBiasParam` to the rest of the system?**
  _2175 weakly-connected nodes found - possible missing edges._
- **Should `Community 0` be split into smaller, more focused modules?**
  _Cohesion score 0.003816793893129771 - nodes in this community are weakly interconnected._
- **Why does `FxRack` connect `Community 47` to `Community 192`, `Community 33`, `Community 226`, `Community 34`, `Community 0`, `Community 101`, `Community 69`, `Community 74`, `Community 268`, `Community 76`, `Community 46`, `Community 81`, `Community 52`, `Community 117`, `Community 58`, `Community 28`, `Community 63`?**
  _High betweenness centrality (0.054) - this node is a cross-community bridge._
- **Should `Community 1` be split into smaller, more focused modules?**
  _Cohesion score 0.028943278943278944 - nodes in this community are weakly interconnected._
- **Why does `DattorroRoom` connect `Community 29` to `Community 0`, `Community 225`, `Community 210`, `Community 18`, `Community 191`?**
  _High betweenness centrality (0.033) - this node is a cross-community bridge._