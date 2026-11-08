// GENERATED FILE - do not edit. Cut verbatim out of Source/PluginProcessor.h by
// tests/state/extract.py: the badge bookkeeping a session load performs.
    void markPresetClean (const juce::String& name)
    {
        currentPresetName = name;
        presetNameNonEmpty.store (! name.isEmpty(), std::memory_order_relaxed);
        presetDirty.store (false, std::memory_order_relaxed);
    }
