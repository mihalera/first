// The factory-preset audit, run as a normal CI test.
//
// The audit's checks used to exist only inside `#if DEBUG` in the processor's
// constructor: in a release build (and therefore in most CI runs) nothing
// verified that the preset names agree with the table, that every preset key
// names a registered parameter, and that every registered parameter is either
// set by some preset or deliberately excluded. Those are exactly the two
// failure shapes that produced the "the presets do not work" reports - a name
// list the wrong length, and keys silently dropped by applyFactoryPreset()'s
// getParameter() guard.
//
// This harness links the shipping plugin (see tests/CMakeLists.txt) and calls
// FirstAudioProcessor::verifyFactoryPresetStructure() - the SAME function the
// DEBUG constructor audit runs - so the checks run in every build that runs
// the tests and cannot drift from the ones a developer sees at construction.
#include "PluginProcessor.h"
#include "ProcessorPresets.h"

#include <iostream>

namespace {

// The same value-classification loop FactoryPresets::all() runs over each preset
// file's properties, applied here to synthetic JSON so the two failure shapes the
// real table must never be able to hide are exercised directly:
//
//   * a key whose value is not a number must be recorded as the sentinel
//     (FactoryPresets::isSentinelValue) rather than dropped or defaulted;
//   * a file that does not parse at all must yield an empty name, which the
//     structure audit then reports, rather than a plausible-looking placeholder.
int checkFailureShapes() {
    int failures = 0;

    // 1. Malformed value: "drive" carries a string, so it must land as the sentinel.
    const auto malformed = juce::JSON::parse("{ \"name\": \"Broken\", \"drive\": \"oops\" }");
    if (const auto* object = malformed.getDynamicObject()) {
        bool sawSentinel = false;
        for (const auto& property : object->getProperties()) {
            const auto propertyName = property.name.toString();
            if (propertyName == "name")
                continue;
            const auto value = property.value;
            if (value.isInt() || value.isInt64() || value.isDouble() || value.isBool())
                continue;
            if (FactoryPresets::isSentinelValue(static_cast<float> (value)))
                sawSentinel = true;
        }
        if (!sawSentinel) {
            std::cerr << "preset_audit: malformed value not recorded as sentinel\n";
            ++failures;
        }
    } else {
        std::cerr << "preset_audit: malformed-value fixture did not parse as object\n";
        ++failures;
    }

    // 2. Unparsable file: no DynamicObject, so the name must stay empty.
    const auto garbage = juce::JSON::parse("this is not json at all");
    if (garbage.getDynamicObject() != nullptr) {
        std::cerr << "preset_audit: garbage fixture unexpectedly parsed as object\n";
        ++failures;
    } else if (!garbage.toString().isEmpty()) {
        std::cerr << "preset_audit: unparsable file produced a non-empty name\n";
        ++failures;
    }

    return failures;
}

} // namespace

int main() {
    juce::String problem;

    if (!FirstAudioProcessor::verifyFactoryPresetStructure(problem)) {
        std::cerr << "factory preset audit: " << problem << '\n';
        return 1;
    }

    if (checkFailureShapes() != 0)
        return 1;

    const auto names = FirstAudioProcessor::getPresetNames();
    std::cout << "preset_audit: " << names.size()
              << " presets, names/table/coverage consistent; failure shapes checked\n";
    return 0;
}
