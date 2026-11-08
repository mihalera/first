// GENERATED FILE - do not edit. Cut verbatim out of Source/ by tests/state/extract.py.

// ---- FirstAudioProcessor::createParameterLayout() ----
juce::AudioProcessorValueTreeState::ParameterLayout FirstAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // One call per group, in the order the parameters were declared in before the
    // split - the order IS the table, so it cannot be shuffled without changing what
    // a saved session and a preset mean.
    addMonitorControls (layout);
    addEqualiserControls (layout);
    addSubFundAndWidthControls (layout);
    addDeckControls (layout);
    addTapeDelayControls (layout);
    addOffsetAndNoiseControls (layout);
    addTransportControls (layout);
    addTuningControls (layout);
    addCompressorControls (layout);
    addLanguageControl (layout);
    addSaturationMixControls (layout);
    addAmpVoicingControls (layout);
    addPreampControl (layout);
    addDiBoxControls (layout);
    addMediaWearControls (layout);
    addReverbControls (layout);
    addColourControls (layout);
    addVinylControls (layout);
    addTypeSwitchControls (layout);
    addStereoLinkControl (layout);
    addDelaySyncControls (layout);
    addTransientControls (layout);
    addModelMixControls (layout);
    addEffectsRackControls (layout);

    return layout;
}

// ---- slot) ----
void FirstAudioProcessor::copyToCompareSlot (int slot)
{
    const auto slotIndex = juce::jlimit (0, 1, slot);
    compareSlots[static_cast<std::size_t> (slotIndex)] = parameters.copyState().createCopy();
    updateCompareDirty();
}

// ---- FirstAudioProcessor::updateCompareDirty() ----
void FirstAudioProcessor::updateCompareDirty()
{
    const auto& slotA = compareSlots[0];
    const auto& slotB = compareSlots[1];
    const auto dirty = slotA.isValid() && slotB.isValid() && ! slotA.isEquivalentTo (slotB);
    compareDirty.store (dirty, std::memory_order_relaxed);
}

// ---- destData) ----
void FirstAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Stamped with the current format, so the session this saves never needs
    // migrating when it is opened again.
    auto state = captureState (parameters);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

// ---- sizeInBytes) ----
void FirstAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // sizeInBytes is already an int, and so is getXmlFromBinary()'s second parameter, so
    // casting it to size_t only bought a size_t -> int narrowing warning.
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState != nullptr)
    {
        // This is the path every existing DAW project arrives on, and the one the
        // MIX rescale hits hardest. The tree holds RAW parameter values, so a
        // session saved at MIX 0.5 would be read as 0.5 PERCENT by the new 0..100
        // range and the machine would come back all but silent. Migrated here,
        // before the state reaches the parameters, and only for states written
        // before the format marker existed.
        auto tree = juce::ValueTree::fromXml (*xmlState);
        migrateStateFormat (tree);
        parameters.replaceState (tree);
    }

    // A freshly loaded session defines both A/B slots: the loaded state becomes
    // the active side and both slots are seeded with it, so compare starts clean.
    copyToCompareSlot (0);
    copyToCompareSlot (1);
    activeSlot.store (0, std::memory_order_relaxed);

    // The engine's own copy of the momentary spindown is synced by
    // parameterChanged(), which replaceState() above drives for every restored
    // parameter - so there is nothing to do here for it.

    // A session load restores the parameters, not the preset that produced them:
    // the badge starts clean and unnamed, exactly like a freshly opened plugin.
    markPresetClean ({});
    lastPresetIndex.store (-1, std::memory_order_relaxed);
}
