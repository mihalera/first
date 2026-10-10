// The saved-state round trip, measured on the shipping code.
//
//   tests/state/run.sh
//
// A session's state is the one thing in this plugin that has to keep working
// across versions: a project saved by an older build must open to the sound it
// was saved with, and a project saved by this build must open to the sound it
// has now. Neither is visible in a compile, a screenshot or a preset check -
// both live in the two functions that write and read the state tree - and the
// MIX rescale from 0..1 to 0..100 is exactly the kind of change that fails
// silently: the state still loads, it just loads as a different sound.
//
// What this harness runs:
//
//   * createParameterLayout(), cut out of Source/ as its twenty-three group
//     functions - so the table under test is the plugin's own 109 parameters,
//     not a handful written out here to match it
//   * getStateInformation() and setStateInformation(), cut whole
//   * copyToCompareSlot() and updateCompareDirty(), which a session load ends
//     with
//   * migrateStateFormat() and captureState(), the format marker and the one
//     migration it drives
//
// and it compiles them against a stand-in for the toolkit (juce_stub.h) that
// models the state tree, the block a state travels through and the
// AudioProcessorValueTreeState the parameters live in. See that header for
// exactly how much is modelled and what is deliberately not.
#include "juce_stub.h"

#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

#include "extracted_preamble.inc"
#include "expected_parameters.h"

/** The harness's stand-in for the plugin processor: the members the extracted
    functions touch, declared exactly as the shipping class declares them, with
    the shipping function bodies compiled in below. */
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
    std::atomic<bool> presetNameNonEmpty { false };

    void copyToCompareSlot (int slot);
    void updateCompareDirty();

    // The shipping body, cut out of the class in PluginProcessor.h.
#include "mark_preset_clean.inc"

    void getStateInformation (juce::MemoryBlock& destData);
    void setStateInformation (const void* data, int sizeInBytes);
};

// The shipping definitions - createParameterLayout() and the four state
// functions - cut verbatim by tests/state/extract.py.
#include "extracted_members.inc"

namespace
{

int checks = 0;
int failures = 0;

void check (bool condition, const std::string& what)
{
    ++checks;

    if (! condition)
    {
        ++failures;
        std::printf ("  FAIL  %s\n", what.c_str());
    }
}

void checkEqual (float actual, float expected, const std::string& what)
{
    check (actual == expected, what + ": expected " + std::to_string (expected)
                               + ", got " + std::to_string (actual));
}

/** The block a session would hold for a tree. */
juce::MemoryBlock blockFor (const juce::ValueTree& tree)
{
    juce::MemoryBlock block;
    std::unique_ptr<juce::XmlElement> xml (tree.createXml());
    juce::AudioProcessor::copyXmlToBinary (*xml, block);
    return block;
}

/** A state as an older build would have written it: the tree every version
    writes, one parameter overridden, and the format marker omitted entirely
    (which is what makes it old - see the note above the marker in the source). */
juce::ValueTree sessionTree (FirstAudioProcessor& processor, double mixValue, int marker)
{
    auto tree = processor.parameters.copyState();

    if (marker >= 0)
        tree.setProperty ("nonlinStateFormat", juce::var (marker), nullptr);

    auto mix = tree.getChildWithProperty ("id", juce::var ("mix"));

    if (mix.isValid())
        mix.setProperty ("value", juce::var (mixValue), nullptr);

    return tree;
}

float mixOf (FirstAudioProcessor& processor)
{
    const auto* mix = processor.parameters.getParameter ("mix");
    return mix != nullptr ? mix->getValue() : -1.0f;
}

//==============================================================================
void checkTheTable()
{
    std::printf ("the parameter table\n");

    FirstAudioProcessor processor;
    const auto count = processor.parameters.getNumParameters();

    checkEqual (static_cast<float> (count), static_cast<float> (expectedParameterCount),
                "the table holds every parameter the source builds ("
                + std::to_string (expectedParameterCount) + ")");

    std::vector<std::string> ids;
    bool everyNameUsed = true;
    bool everyDefaultInRange = true;
    bool everyChoiceHasChoices = true;

    for (int i = 0; i < count; ++i)
    {
        const auto* parameter = processor.parameters.getParameterAt (i);

        if (parameter == nullptr)
        {
            check (false, "parameter " + std::to_string (i) + " exists");
            continue;
        }

        ids.push_back (parameter->getParameterID().id.toStdString());

        if (parameter->getName().isEmpty())
            everyNameUsed = false;

        const auto defaultValue = parameter->getDefaultValue();

        if (parameter->isChoice())
        {
            if (parameter->getNumChoices() <= 0)
                everyChoiceHasChoices = false;
            else if (defaultValue < 0.0f || defaultValue >= static_cast<float> (parameter->getNumChoices()))
                everyDefaultInRange = false;
        }
        else if (const auto* asFloat = dynamic_cast<const juce::AudioParameterFloat*> (parameter))
        {
            if (defaultValue < asFloat->getRange().getStart() || defaultValue > asFloat->getRange().getEnd())
                everyDefaultInRange = false;
        }
        else if (defaultValue < 0.0f || defaultValue > 1.0f)
        {
            everyDefaultInRange = false;   // a switch: 0 or 1
        }
    }

    std::size_t duplicates = 0;
    for (std::size_t i = 0; i < ids.size(); ++i)
        for (std::size_t j = i + 1; j < ids.size(); ++j)
            if (ids[i] == ids[j])
                ++duplicates;

    check (duplicates == 0, "every parameter id is unique");
    check (everyNameUsed, "every parameter has a name for the host to show");
    check (everyDefaultInRange, "every parameter's default sits inside its own range");
    check (everyChoiceHasChoices, "every choice parameter lists its choices");

    // MIX's range is the whole reason the format marker exists: 0..100, with 50
    // the natural position. A table that came back at 0..1 would make the
    // migration below a no-op and every saved session wrong.
    const auto* mix = processor.parameters.getParameter ("mix");
    check (mix != nullptr, "MIX is a registered parameter");

    if (const auto* mixFloat = dynamic_cast<const juce::AudioParameterFloat*> (mix))
    {
        checkEqual (mixFloat->getRange().getEnd(), 100.0f, "MIX's range ends at 100 percent");
        checkEqual (mixFloat->getDefaultValue(), 50.0f, "MIX defaults to 50 percent");
    }
    else
    {
        check (false, "MIX is a float parameter");
    }
}

//==============================================================================
void checkTheRoundTrip()
{
    std::printf ("a state written by this build\n");

    FirstAudioProcessor saved;
    const auto count = saved.parameters.getNumParameters();

    // A distinct, in-range value for every parameter, so a value landing on the
    // wrong parameter cannot pass unnoticed.
    for (int i = 0; i < count; ++i)
    {
        auto* parameter = saved.parameters.getParameterAt (i);

        if (parameter == nullptr)
            continue;

        if (parameter->isChoice())
        {
            const auto choices = parameter->getNumChoices();
            parameter->setValue (static_cast<float> ((i + 1) % choices));
        }
        else
        {
            const auto* asFloat = dynamic_cast<const juce::AudioParameterFloat*> (parameter);
            const auto start = asFloat != nullptr ? asFloat->getRange().getStart() : 0.0f;
            const auto end = asFloat != nullptr ? asFloat->getRange().getEnd() : 1.0f;
            parameter->setValue (start + 0.37f * (end - start));
        }
    }

    juce::MemoryBlock block;
    saved.getStateInformation (block);
    check (block.getSize() > 0, "the session block is not empty");

    // The state a session holds is the whole table plus the marker, and nothing
    // else: a parameter that stopped being written would come back as whatever
    // the session happened to hold.
    std::unique_ptr<juce::XmlElement> xml (juce::AudioProcessor::getXmlFromBinary (block.getData(),
                                                                                  static_cast<int> (block.getSize())));
    check (xml != nullptr, "the block reads back as a state");

    if (xml != nullptr)
    {
        const auto written = juce::ValueTree::fromXml (*xml);
        checkEqual (static_cast<float> (written.getNumChildren()), static_cast<float> (count),
                    "the state carries one child per parameter");
        checkEqual (static_cast<float> (static_cast<int> (written.getProperty ("nonlinStateFormat", juce::var (0)))),
                    static_cast<float> (currentStateFormat),
                    "the state is stamped with the current format");
    }

    // Loaded into a fresh instance, which is what opening a project does.
    FirstAudioProcessor restored;
    restored.setStateInformation (block.getData(), static_cast<int> (block.getSize()));

    int mismatches = 0;
    std::string firstMismatch;

    for (int i = 0; i < count; ++i)
    {
        const auto* before = saved.parameters.getParameterAt (i);
        const auto* after = restored.parameters.getParameterAt (i);

        if (before == nullptr || after == nullptr || before->getValue() != after->getValue())
        {
            if (mismatches == 0 && before != nullptr && after != nullptr)
                firstMismatch = before->getParameterID().id.toStdString() + ": "
                              + std::to_string (before->getValue()) + " -> "
                              + std::to_string (after->getValue());
            ++mismatches;
        }
    }

    check (mismatches == 0, "every parameter comes back exactly as it went out"
                            + (mismatches != 0 ? (" (" + firstMismatch + ")") : std::string()));

    // And the instance's own tree is the state that was loaded, so a second save
    // is the same session.
    check (restored.parameters.copyState().isEquivalentTo (saved.parameters.copyState()),
           "the restored state is the saved state");
}

//==============================================================================
void checkTheMigration()
{
    std::printf ("states written by older builds\n");

    // A session from before the rescale: MIX stored 0..1, no marker.
    {
        FirstAudioProcessor processor;
        const auto block = blockFor (sessionTree (processor, 0.5, -1));
        processor.setStateInformation (block.getData(), static_cast<int> (block.getSize()));
        checkEqual (mixOf (processor), 50.0f, "MIX 0.5 loads as 50 percent");
    }

    // The same bytes twice must not rescale twice - the failure mode a
    // value-based migration has and a marker-based one does not.
    {
        FirstAudioProcessor processor;
        const auto block = blockFor (sessionTree (processor, 0.5, -1));
        processor.setStateInformation (block.getData(), static_cast<int> (block.getSize()));
        processor.setStateInformation (block.getData(), static_cast<int> (block.getSize()));
        checkEqual (mixOf (processor), 50.0f, "loading the same session twice does not rescale twice");
    }

    // The ends of the old scale: 1.0 was full wet, and a small value was a small
    // percentage rather than a second 100.
    {
        FirstAudioProcessor processor;
        const auto block = blockFor (sessionTree (processor, 1.0, -1));
        processor.setStateInformation (block.getData(), static_cast<int> (block.getSize()));
        checkEqual (mixOf (processor), 100.0f, "MIX 1.0 loads as 100 percent");
    }

    {
        FirstAudioProcessor processor;
        const auto block = blockFor (sessionTree (processor, 0.01, -1));
        processor.setStateInformation (block.getData(), static_cast<int> (block.getSize()));
        checkEqual (mixOf (processor), 1.0f, "MIX 0.01 loads as 1 percent");
    }

    // A state this build writes is already current, so it must come back
    // untouched - including a MIX below 1 percent, which the old value test
    // would have rewritten.
    {
        FirstAudioProcessor processor;
        const auto block = blockFor (sessionTree (processor, 0.5, currentStateFormat));
        processor.setStateInformation (block.getData(), static_cast<int> (block.getSize()));
        checkEqual (mixOf (processor), 0.5f, "a current state is not migrated");
    }

    // A state from a LATER build: the marker says it is newer than anything this
    // build knows, so nothing may be rewritten on the way in.
    {
        FirstAudioProcessor processor;
        const auto block = blockFor (sessionTree (processor, 0.5, currentStateFormat + 5));
        processor.setStateInformation (block.getData(), static_cast<int> (block.getSize()));
        checkEqual (mixOf (processor), 0.5f,
                    "a state from a future format is left alone");
    }

    {
        FirstAudioProcessor fresh;
        checkEqual (mixOf (fresh), 50.0f, "a fresh instance starts at MIX 50 percent");
    }
}

//==============================================================================
void checkTheSessionLoad()
{
    std::printf ("what loading a session leaves behind\n");

    FirstAudioProcessor processor;
    const auto block = blockFor (sessionTree (processor, 37.0, currentStateFormat));

    processor.activeSlot.store (1, std::memory_order_relaxed);
    processor.compareDirty.store (true, std::memory_order_relaxed);
    processor.lastPresetIndex.store (12, std::memory_order_relaxed);
    processor.markPresetClean (juce::String ("Some Preset"));

    processor.setStateInformation (block.getData(), static_cast<int> (block.getSize()));

    check (processor.compareSlots[0].isValid(), "slot A is seeded by a load");
    check (processor.compareSlots[1].isValid(), "slot B is seeded by a load");
    check (processor.compareSlots[0].isEquivalentTo (processor.parameters.copyState()),
           "slot A holds the state that was loaded");
    check (processor.compareSlots[1].isEquivalentTo (processor.parameters.copyState()),
           "slot B holds the state that was loaded");
    check (! processor.compareDirty.load (std::memory_order_relaxed),
           "the A/B comparison starts clean rather than dirty");
    checkEqual (static_cast<float> (processor.activeSlot.load (std::memory_order_relaxed)), 0.0f,
                "the loaded state is the active side");
    checkEqual (static_cast<float> (processor.lastPresetIndex.load (std::memory_order_relaxed)), -1.0f,
                "no factory preset is left selected");
    check (processor.currentPresetName.isEmpty(), "the preset badge forgets the previous preset");
    check (! processor.presetNameNonEmpty.load (std::memory_order_relaxed),
           "the badge reports no preset name");
    check (! processor.presetDirty.load (std::memory_order_relaxed),
           "the badge starts clean after a load");
}

//==============================================================================
void checkRefusal()
{
    std::printf ("a block that is not a state\n");

    FirstAudioProcessor processor;

    // Belt and braces: pin the parameters to values nothing else in this run
    // uses, so a block that arrives by mistake is visible.
    if (auto* mix = processor.parameters.getParameter ("mix"))
        mix->setValue (12.5f);

    processor.lastPresetIndex.store (7, std::memory_order_relaxed);
    processor.markPresetClean (juce::String ("Another Preset"));

    const char garbage[] = "this is not a state block at all";
    processor.setStateInformation (garbage, static_cast<int> (sizeof (garbage)));

    checkEqual (mixOf (processor), 12.5f, "a rejected block leaves the parameters alone");

    // The session bookkeeping at the end of setStateInformation runs whether or
    // not the block was accepted - the guard covers the state, not the tail. So a
    // refused block still re-seeds the A/B slots (with the CURRENT state, which
    // is harmless) and still clears the preset badge and the preset index. The
    // checks below pin that down: they are what the shipping code does today, and
    // the note in the report is that the tail is arguably outside the guard.
    check (processor.compareSlots[0].isEquivalentTo (processor.parameters.copyState()),
           "a rejected block still seeds slot A with the live state");
    check (processor.compareSlots[1].isEquivalentTo (processor.parameters.copyState()),
           "a rejected block still seeds slot B with the live state");
    check (processor.currentPresetName.isEmpty(),
           "a rejected block clears the preset badge (the tail is outside the guard)");
    checkEqual (static_cast<float> (processor.lastPresetIndex.load (std::memory_order_relaxed)), -1.0f,
                "a rejected block clears the preset index (the tail is outside the guard)");

    // An empty block is the other shape a host can hand over.
    {
        FirstAudioProcessor empty;
        if (auto* mix = empty.parameters.getParameter ("mix"))
            mix->setValue (33.0f);

        empty.setStateInformation (nullptr, 0);
        checkEqual (mixOf (empty), 33.0f, "an empty block leaves the parameters alone");
    }

    // A state whose ids the plugin does not know: the parameters it does know
    // must still load, and the strangers must be ignored rather than registered.
    FirstAudioProcessor other;
    auto tree = sessionTree (other, 73.0, currentStateFormat);
    juce::ValueTree stranger ("PARAM");
    stranger.setProperty ("id", juce::var ("no_such_parameter"), nullptr);
    stranger.setProperty ("value", juce::var (1.0), nullptr);
    tree.addChild (stranger, -1);

    const auto block = blockFor (tree);
    other.setStateInformation (block.getData(), static_cast<int> (block.getSize()));

    checkEqual (mixOf (other), 73.0f, "a session still loads around an unknown id");
    checkEqual (static_cast<float> (other.parameters.getNumParameters()),
                static_cast<float> (expectedParameterCount),
                "an unknown id cannot add a parameter");
}

} // namespace

int main()
{
    std::printf ("=== saved-state round trip (shipping parameter table, %d parameters)\n",
                 expectedParameterCount);

    checkTheTable();
    checkTheRoundTrip();
    checkTheMigration();
    checkTheSessionLoad();
    checkRefusal();

    std::printf ("\n%d checks, %d failures\n", checks, failures);

    if (failures == 0)
        std::printf ("ALL CHECKS PASSED (0 failures)\n");

    return failures == 0 ? 0 : 1;
}
