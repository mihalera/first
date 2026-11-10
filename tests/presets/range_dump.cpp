// The shipping parameter table, printed as data the preset check can read.
//
//   tests/presets/check_ranges.sh
//
// A factory preset states a number for every parameter, and nothing in a release
// build complains when one of those numbers is outside the parameter's range:
// the load clamps it, so a preset that says "noise level 110 %" is stored, shown
// and compared as 100 % and only the JSON remembers what was meant. That is how
// four presets came to ask for 1.1 on a 0..1 control.
//
// Ranges are not readable from a JSON file and they are not worth re-parsing out
// of C++ in Python: the table builds them through named locals, helpers and
// constants. So this file asks the table itself, through the same stand-in for
// the toolkit the state harness uses (tests/state/juce_stub.h), and prints one
// line per parameter. tests/presets/check_ranges.py does the comparing.
//
// The extraction is tests/state/extract.py's, unchanged: it cuts the layout
// function and the members it touches out of Source/PluginProcessor.h, so the
// table under test is the plugin's own, not a copy.
#include "juce_stub.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

#include "extracted_preamble.inc"

/** The stand-in processor: exactly the members tests/state/state_test.cpp
    declares, because compiled_members.inc is the shipping createParameterLayout()
    and the four state functions, cut whole. Only the layout is used here. */
struct FirstAudioProcessor : public juce::AudioProcessor
{
    FirstAudioProcessor() : parameters ("TAPE_NONLIN", createParameterLayout()) {}

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState parameters;

    juce::ValueTree compareSlots[2];
    std::atomic<int> activeSlot { 0 };
    std::atomic<bool> compareDirty { false };
    std::atomic<int> lastPresetIndex { -1 };
    juce::String currentPresetName;
    std::atomic<bool> presetDirty { false };
    std::vector<std::atomic<float>*> dirtyBadgeParameters;
    std::vector<float> presetParameterSnapshot;

    void copyToCompareSlot (int slot);
    void updateCompareDirty();

#include "mark_preset_clean.inc"

    void getStateInformation (juce::MemoryBlock& destData);
    void setStateInformation (const void* data, int sizeInBytes);
};

#include "extracted_members.inc"

int main()
{
    const auto layout = FirstAudioProcessor::createParameterLayout();

    for (const auto& parameter : layout.parameters)
    {
        const auto id = parameter->getParameterID().id.toRawUTF8();

        if (const auto* choice = dynamic_cast<const juce::AudioParameterChoice*> (parameter.get()))
        {
            std::printf ("%s|choice|0|%d|1|%.0f|\n", id,
                         choice->getNumChoices() - 1, parameter->getDefaultValue());
            continue;
        }

        if (const auto* ranged = dynamic_cast<const juce::AudioParameterFloat*> (parameter.get()))
        {
            const auto& range = ranged->getRange();
            std::printf ("%s|float|%.9g|%.9g|%.9g|%.9g|\n", id,
                         static_cast<double> (range.getStart()),
                         static_cast<double> (range.getEnd()),
                         static_cast<double> (range.interval),
                         static_cast<double> (parameter->getDefaultValue()));
            continue;
        }

        // Everything left is a switch: AudioParameterBool, which the table builds
        // with no range at all.
        std::printf ("%s|bool|0|1|1|%.0f|\n", id, parameter->getDefaultValue());
    }

    return 0;
}
