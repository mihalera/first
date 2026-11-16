// =============================================================================
//  Every parameter in the shipping table, checked one by one.
//
//    tests/parameters/run.sh
//
//  check_parameters.py reads the table and the editor statically; this harness
//  constructs the table the plugin actually builds - createParameterLayout() is
//  cut out of Source/ by tests/state/extract.py, not copied - and asks the
//  questions a host, a preset or a session load will ask of each of the ~150
//  parameters:
//
//    1. IDENTITY. The id is non-empty and unique. A duplicate id is not a
//       cosmetic bug: the host addresses a parameter by it, and one of the two
//       silently becomes unreachable.
//    2. RANGE. For a float: start < end, the default is a legal value of its own
//       grid, and the interval (if any) divides the span - the three numbers that
//       decide what every stored preset value means.
//    3. TRAVEL. Setting the parameter to its start, its end and its middle and
//       reading it back gives back exactly that value. This is where an inverted
//       range, a clamp in the wrong unit or an interval that does not reach the
//       end of the range shows up.
//    4. CHOICES. A choice parameter has at least two options, names that are
//       non-empty and unique, a default inside the list, and every index reads
//       back after being selected. Model-list OFF entries are LAST; oversampling
//       keeps its historical Off index at ZERO. The index is the ABI: a saved
//       session stores it, so moving an option silently repoints existing sessions.
//    5. VERSION. Every ParameterID carries version 1. Changing it is how JUCE
//       re-maps a saved session, and it is not something to do by accident.
//
//  The table is the plugin's own: no JUCE, no host, no audio - juce_stub.h is
//  the same stand-in the state harness uses.
// =============================================================================
#include "juce_stub.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "extracted_preamble.inc"

/** The stand-in for the plugin processor: only the members the extracted
    functions touch, declared as the shipping class declares them. */
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

namespace
{
int failures = 0;

void check (bool condition, const std::string& what)
{
    if (! condition)
    {
        std::printf ("  FAIL  %s\n", what.c_str());
        ++failures;
    }
}

std::string idOf (const juce::RangedAudioParameter& parameter)
{
    return parameter.getParameterID().id.toStdString();
}

/** A float parameter's own grid: start, end, and the step between values. */
void checkFloat (const juce::RangedAudioParameter& parameter)
{
    const auto* floating = dynamic_cast<const juce::AudioParameterFloat*> (&parameter);

    if (floating == nullptr)
        return;

    const auto id = idOf (parameter);
    const auto range = floating->getRange();
    const auto start = range.getStart();
    const auto end = range.getEnd();
    const auto interval = range.interval;

    check (start < end, id + ": range start " + std::to_string (start)
                            + " is not below its end " + std::to_string (end));

    check (interval >= 0.0f, id + ": negative interval " + std::to_string (interval));

    if (interval > 0.0f)
    {
        const auto steps = (end - start) / interval;
        check (std::abs (steps - std::round (steps)) < 1.0e-2f,
               id + ": interval " + std::to_string (interval) + " does not divide the span "
                   + std::to_string (end - start));
    }

    const auto defaultValue = floating->getDefaultValue();
    check (defaultValue >= start && defaultValue <= end,
           id + ": default " + std::to_string (defaultValue) + " is outside "
               + std::to_string (start) + ".." + std::to_string (end));

    // The default has to be a value the grid actually has, or every session that
    // never touches this control loads a value the panel cannot show.
    check (range.snapToLegalValue (defaultValue) == defaultValue,
           id + ": default " + std::to_string (defaultValue) + " is not a legal value "
               "(snaps to " + std::to_string (range.snapToLegalValue (defaultValue)) + ")");

    auto* mutableParameter = const_cast<juce::RangedAudioParameter*> (&parameter);

    for (const auto target : { start, end, 0.5f * (start + end) })
    {
        const auto expected = range.snapToLegalValue (target);
        mutableParameter->setValue (target);

        check (mutableParameter->getValue() == expected,
               id + ": setting " + std::to_string (target) + " read back as "
                   + std::to_string (mutableParameter->getValue()) + ", expected "
                   + std::to_string (expected));
    }

    mutableParameter->setValue (defaultValue);
}

void checkBool (const juce::RangedAudioParameter& parameter)
{
    if (dynamic_cast<const juce::AudioParameterBool*> (&parameter) == nullptr)
        return;

    const auto id = idOf (parameter);
    const auto defaultValue = parameter.getDefaultValue();

    check (defaultValue == 0.0f || defaultValue == 1.0f,
           id + ": boolean default is " + std::to_string (defaultValue));

    auto* mutableParameter = const_cast<juce::RangedAudioParameter*> (&parameter);
    mutableParameter->setValue (1.0f);
    check (mutableParameter->getValue() == 1.0f, id + ": on did not read back as 1");
    mutableParameter->setValue (0.0f);
    check (mutableParameter->getValue() == 0.0f, id + ": off did not read back as 0");
    mutableParameter->setValue (defaultValue);
}

void checkChoice (const juce::RangedAudioParameter& parameter)
{
    if (! parameter.isChoice())
        return;

    const auto id = idOf (parameter);
    const auto* choice = dynamic_cast<const juce::AudioParameterChoice*> (&parameter);

    if (choice == nullptr)
    {
        check (false, id + ": reports itself as a choice but is not an AudioParameterChoice");
        return;
    }

    const auto count = choice->getNumChoices();
    check (count >= 2, id + ": choice with " + std::to_string (count) + " option(s)");

    std::vector<std::string> names;

    for (int index = 0; index < count; ++index)
    {
        const auto name = choice->getChoiceName (index).toStdString();
        check (! name.empty(), id + ": choice " + std::to_string (index) + " has no name");

        for (const auto& seen : names)
            check (seen != name, id + ": duplicate choice name \"" + name + "\"");

        names.push_back (name);
    }

    const auto defaultIndex = static_cast<int> (choice->getDefaultValue());
    check (defaultIndex >= 0 && defaultIndex < count,
           id + ": default index " + std::to_string (defaultIndex) + " is outside 0.."
               + std::to_string (count - 1));

    auto* mutableParameter = const_cast<juce::RangedAudioParameter*> (&parameter);
    for (int index = 0; index < count; ++index)
    {
        mutableParameter->setValue (static_cast<float> (index));
        check (mutableParameter->getValue() == static_cast<float> (index),
               id + ": selecting choice " + std::to_string (index) + " read back as "
                   + std::to_string (mutableParameter->getValue()));
    }
    mutableParameter->setValue (static_cast<float> (defaultIndex));

    // Model-list Off entries are appended so existing option indices are stable.
    // Oversampling predates that convention: Off is the original index zero and
    // must remain there for saved sessions to retain their meaning.
    for (int index = 0; index < count; ++index)
        if (names[static_cast<std::size_t> (index)] == "Off")
        {
            const auto expectedIndex = id == "oversampling" ? 0 : count - 1;
            check (index == expectedIndex,
                   id + ": \"Off\" sits at index " + std::to_string (index) + ", expected "
                       + std::to_string (expectedIndex) + " to preserve saved choice indices");
        }
}
} // namespace

int main()
{
    FirstAudioProcessor processor;
    const auto count = processor.parameters.getNumParameters();

    std::printf ("parameter sweep: %d parameters in the shipping table\n", count);

    check (count > 0, "the table is empty");

    std::vector<std::string> seen;

    for (int index = 0; index < count; ++index)
    {
        const auto* parameter = processor.parameters.getParameterAt (index);

        if (parameter == nullptr)
        {
            check (false, "parameter " + std::to_string (index) + " is null");
            continue;
        }

        const auto id = idOf (*parameter);

        check (! id.empty(), "parameter " + std::to_string (index) + " has no id");

        for (const auto& other : seen)
            check (other != id, "duplicate parameter id \"" + id + "\"");

        seen.push_back (id);

        check (parameter->getParameterID().version == 1,
               id + ": version is " + std::to_string (parameter->getParameterID().version)
                   + ", not 1");

        check (! parameter->getName().toStdString().empty(), id + ": no name");

        checkFloat (*parameter);
        checkBool (*parameter);
        checkChoice (*parameter);
    }

    if (failures == 0)
        std::printf ("parameter sweep: every parameter has a unique id, a legal range and "
                     "default, a working travel, sound choices and version 1\n");

    std::printf ("%s (%d failure%s)\n",
                 failures == 0 ? "PARAMETER SWEEP PASSED" : "PARAMETER SWEEP FAILED",
                 failures, failures == 1 ? "" : "s");

    return failures == 0 ? 0 : 1;
}
