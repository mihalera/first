// =============================================================================
//  The presets: the factory table, the preset box's own files, the A/B compare
//  slots, and the whole-state undo they all go through.
//
//  Moved out of PluginProcessor.cpp by matching its exact text, in three spans that
//  kept their order: the state-swap action and the two functions it swaps state
//  through, the FactoryPresets table, and the code that indexes it. Nothing was
//  retyped; the prose came with the code because in this repository the note that
//  explains why a thing is shaped the way it is IS part of the thing.
//
//  What the move cost is the include list and nothing else. The state-format
//  helpers the preset writer stamps a file with are shared with the processor's own
//  save/load, so they are Source/ProcessorStateFormat.h; the table's entry points
//  are declared in Source/ProcessorPresets.h for the constructor's debug audit; and
//  FactoryPresetIndex.h - the generated list of preset files - is read here rather
//  than there, because this is where the table that reads it lives.
// =============================================================================
#include "PluginProcessor.h"
#include "ProcessorPresets.h"
#include "ProcessorStateFormat.h"

// The two generated headers from the build, included in this order because the
// second one names the first: BinaryData.h is juce_add_binary_data's accessors (one
// symbol pair per preset JSON), and FactoryPresetIndex.h is the list CMake writes
// from the same FACTORY_PRESET_FILES in the order the preset box shows them. Both
// are read HERE rather than in the processor, because the table built from them is
// here - the generated index does not include BinaryData.h itself, so the order is
// load-bearing rather than stylistic.
#include <BinaryData.h>
#include <FactoryPresetIndex.h>

#include <map>
#include <vector>

//==============================================================================
//  Full-state undo. Preset applications and A/B recalls are recorded as one
//  UndoableAction each that swaps the whole APVTS state tree, so a single Ctrl+Z
//  brings back exactly what was on screen before. Individual knob moves are not
//  booked into the history (the A/B slots and double-click reset cover those),
//  which keeps the undo stack meaningful instead of thousands of micro-steps.
//==============================================================================
namespace
{
    class StateSwapAction final : public juce::UndoableAction
    {
    public:
        StateSwapAction (FirstAudioProcessor& ownerIn,
                         const juce::ValueTree& stateBefore,
                         const juce::ValueTree& stateAfter)
            : owner (ownerIn), before (stateBefore), after (stateAfter) {}

        bool perform() override { owner.replaceParameterState (after); return true; }
        bool undo() override    { owner.replaceParameterState (before); return true; }

        int getSizeInUnits() override
        {
            return (int) (sizeof (*this) + (std::size_t) before.getNumChildren() * 128);
        }

    private:
        FirstAudioProcessor& owner;
        juce::ValueTree before, after;
    };
}

void FirstAudioProcessor::replaceParameterState (const juce::ValueTree& newState)
{
    if (! newState.isValid())
        return;

    // replaceState pushes every parameter change to the host and the editor, so the
    // UI, the DAW automation display and the engine all agree after a swap.
    parameters.replaceState (newState.createCopy());

    // A state swap can include the oversampling switch, so the active engine and the
    // reported latency must follow before the next block is rendered. The RAW value
    // is the choice index; getValue() returns the normalised 0..1 form, which for a
    // choice parameter would round the 4x entry down to 2x. The rebuild uses the
    // remembered host block size rather than a hardcoded one.
    if (auto* rawOversampling = parameters.getRawParameterValue ("oversampling"))
    {
        const auto requested = static_cast<int> (rawOversampling->load());
        if (requested != static_cast<int> (currentOversampling))
            setOversamplingFactor (static_cast<OversamplingFactor> (requested), lastBlockSize);
    }
}

void FirstAudioProcessor::applyStateWithUndo (const juce::ValueTree& targetState,
                                              const juce::String& transactionName)
{
    if (! targetState.isValid())
        return;

    const auto stateBefore = parameters.copyState().createCopy();
    const auto stateAfter = juce::ValueTree (targetState).createCopy();

    undoManager.beginNewTransaction (transactionName);
    undoManager.perform (new StateSwapAction (*this, stateBefore, stateAfter));
}

//==============================================================================
//  Factory presets.
//
//  Twenty starting points: the eighteen sounding machines, plus the two range
//  endpoints the machine is checked against - MINIMUM is the plugin doing the
//  least it can (MIX at zero, so the tape path is out of the signal entirely and the
//  output is the input), and MAXIMUM is every control at the top of its range, which
//  is the honest way to find out what the machine does when it is pushed hardest. Each
//  returns the full parameter map it represents - nothing is patched onto the user's
//  current state beyond the listed values, so a preset changes the machine, not the
//  session.
//
//  The two endpoints are listed SECOND, directly under DEFAULT TAPE, rather than
//  parked at the end of the list. They are reference points, not sounds anybody
//  reaches for while working, and the list is otherwise a set of things to listen
//  to; keeping the references next to the neutral machine they bracket means the
//  sounding presets read as one uninterrupted list with the calibration around it.
//  They are also the two presets a tester wants first, and at the end of twenty they
//  are the two easiest to forget exist.
//
//  The order here and the row order in the table below are ONE list written twice,
//  and they are index-aligned by hand. Changing one without the other hands the
//  panel a name for a different machine, which is the same class of bug as a
//  mismatched choice count: nothing fails, the list just quietly lies.
//==============================================================================
//  The factory presets.
//
//  These used to be a 160-field struct plus twenty-six rows of hand-written
//  C++ initialisers - about 590 lines of numbers, in the same file as the
//  processor, where a new preset meant retyping a row of positional floats and
//  recompiling. They are now twenty-six JSON files under Source/Presets/Factory,
//  compiled into the binary, and this is the whole of what knows how to read
//  them.
//
//  Three things fell out of the move, each of them a bug the old table could
//  have and these files cannot:
//
//    1. THE LIST IS WRITTEN ONCE. FactoryPresetIndex.h is generated by CMake
//       from the same list that feeds juce_add_binary_data, so the preset box,
//       the undo label, the dirty badge and the JSON files cannot disagree about
//       which file is preset 3. That disagreement was real: twenty names over a
//       twenty-six row table, and the box, the label and the badge all index by
//       POSITION, so six rows loaded as whichever preset the selection happened
//       to name.
//
//    2. THE NAME IS DATA TOO. It lives in the file under "name", so renaming a
//       preset is editing that file, not a list in a different one. There is no
//       second list of names left to fall out of step with the files.
//
//    3. A MALFORMED VALUE IS VISIBLE. A key that is not a number is the failure
//       this file used to be able to have quietly: a value silently dropped, so
//       the preset loaded "mostly". It cannot be silent here - see
//       isSentinelValue() below, which the debug audit turns into an assert that
//       names the key.
//==============================================================================
namespace FactoryPresets
{
    // One parse per process, not one per call.
    //
    // factoryPresetValues() is called in a loop by the debug audit and once per
    // preset click; parsing twenty-six JSON objects on each of those is work
    // done for nothing. The cache is a function-local static, so it is built on
    // first use and its initialisation is thread-safe without a mutex (C++11
    // guarantees this for function-local statics). The reference returned is to
    // storage that outlives every caller.
    struct Preset
    {
        juce::String name;
        std::map<juce::String, float> values;
    };

    // The value written in place of anything that was not a number. Chosen to be
    // outside every parameter's range, so even if the audit below is compiled out
    // the parameter rejects it and the control ends up at a legal value rather
    // than at a nonsense one.
    inline constexpr float notANumber = -1.0e9f;

    // External linkage, deliberately: the declaration this definition is paired
    // with is in Source/ProcessorPresets.h, and the caller is the constructor in
    // PluginProcessor.cpp. See the note on that declaration - inline here would
    // be emitted in this file only and would leave the Debug link unresolved.
    bool isSentinelValue (float value) noexcept
    {
        // Compared as int, not float: the sentinel is exactly representable, so
        // the comparison is exact, and -Wfloat-equal is right to object to a
        // float == in a codebase that has it on.
        return static_cast<int> (value) == static_cast<int> (notANumber);
    }

    const std::vector<Preset>& all()
    {
        static const std::vector<Preset> parsed = []
        {
            std::vector<Preset> result;
            result.reserve (juce::numElementsInArray (FactoryPresetIndex::entries));

            for (const auto& entry : FactoryPresetIndex::entries)
            {
                Preset preset;

                // The name is read separately because it is the one field with a
                // type of its own: everything else in the file is a number
                // destined for a parameter.
                const auto text = juce::String::createStringFromData (entry.data, entry.size);
                const auto parsedJson = juce::JSON::parse (text);

                if (const auto* object = parsedJson.getDynamicObject())
                {
                    preset.name = object->getProperty ("name").toString();

                    // getProperties(), not getPropertyNames(): a NamedValueSet is
                    // what a DynamicObject actually holds, and iterating it yields
                    // the name and the value together, so the two cannot be read
                    // out of step.
                    for (const auto& property : object->getProperties())
                    {
                        // toString(): a NamedValueSet names its entries with
                        // juce::Identifier, and comparing one of those to a string
                        // literal is ambiguous rather than merely wrong. The
                        // preset's keys are strings everywhere else, so this is
                        // where that conversion belongs.
                        const auto propertyName = property.name.toString();

                        if (propertyName == "name")
                            continue;

                        // A value that is not a number is a typo in a key, and a
                        // typo in a key is precisely the bug this file used to
                        // have. It is not skipped quietly; it is recorded, and the
                        // audit in the constructor fails on it by name.
                        const auto value = property.value;

                        if (value.isInt() || value.isInt64() || value.isDouble() || value.isBool())
                            preset.values.emplace (propertyName, static_cast<float> (value));
                        else
                            preset.values.emplace (propertyName, notANumber);
                    }
                }

                result.push_back (std::move (preset));
            }

            return result;
        }();

        return parsed;
    }

    // Same reason as isSentinelValue above: called from PluginProcessor.cpp's
    // constructor, so it needs external linkage to be emitted at all.
    int count()
    {
        return static_cast<int> (all().size());
    }
}

int FirstAudioProcessor::numFactoryPresets()
{
    return FactoryPresets::count();
}

juce::StringArray FirstAudioProcessor::getPresetNames()
{
    // Read from the data rather than written beside it. The names come back in
    // the ORDER of the files, because the preset box, the undo label and the
    // dirty badge all index by position - which is what used to break.
    juce::StringArray names;

    for (const auto& preset : FactoryPresets::all())
        names.add (preset.name);

    return names;
}

std::map<juce::String, float> FirstAudioProcessor::factoryPresetValues (int index)
{
    const auto& presets = FactoryPresets::all();
    // size() is unsigned and index is signed, and jmax/jlimit are templates that
    // will not mix the two - so the bound is converted once, here, rather than
    // at three call sites.
    const auto last = juce::jmax (1, static_cast<int> (presets.size())) - 1;
    const auto clamped = juce::jlimit (0, last, index);

    return presets[static_cast<std::size_t> (clamped)].values;
}


void FirstAudioProcessor::applyFactoryPreset (int index)
{
    const auto clampedIndex = juce::jlimit (0, FactoryPresets::count() - 1, index);
    const auto& values = factoryPresetValues (clampedIndex);
    if (values.empty())
        return;

    // Build the target tree from the CURRENT state so anything the map does not
    // list (nothing today, but future-proof) survives the preset change.
    auto target = parameters.copyState().createCopy();
    const auto idProperty = juce::Identifier ("id");
    const auto valueProperty = juce::Identifier ("value");

    for (const auto& valuePair : values)
    {
        // The lookup is only a guard against an id the plugin does not have; the
        // value itself goes in RAW. The tree stores denormalised values, so
        // normalising here and writing the result made every factory preset load
        // the wrong setting: a -3 dB INPUT came back as 0.45 dB, and once MIX became
        // a 0..100 percentage, 100 came back as 1.0 - i.e. 1%, which is why the
        // factory presets had all but gone dry.
        if (parameters.getParameter (valuePair.first) != nullptr)
        {
            for (int i = 0; i < target.getNumChildren(); ++i)
            {
                auto parameterChild = target.getChild (i);
                if (parameterChild.hasProperty (idProperty)
                    && parameterChild.getProperty (idProperty).toString() == valuePair.first)
                {
                    parameterChild.setProperty (valueProperty, valuePair.second, nullptr);
                    break;
                }
            }
        }
    }

    applyStateWithUndo (target, "Preset: " + getPresetNames()[clampedIndex]);
    lastPresetIndex.store (clampedIndex, std::memory_order_relaxed);
    markPresetClean (getPresetNames()[clampedIndex]);
}

void FirstAudioProcessor::copyToCompareSlot (int slot)
{
    const auto slotIndex = juce::jlimit (0, 1, slot);
    compareSlots[static_cast<std::size_t> (slotIndex)] = parameters.copyState().createCopy();
    updateCompareDirty();
}

void FirstAudioProcessor::toggleCompare()
{
    const auto nextSlot = 1 - activeSlot.load (std::memory_order_relaxed);
    applyStateWithUndo (compareSlots[static_cast<std::size_t> (nextSlot)],
                        nextSlot == 0 ? "Recall A" : "Recall B");
    activeSlot.store (nextSlot, std::memory_order_relaxed);
}

void FirstAudioProcessor::updateCompareDirty()
{
    const auto& slotA = compareSlots[0];
    const auto& slotB = compareSlots[1];
    const auto dirty = slotA.isValid() && slotB.isValid() && ! slotA.isEquivalentTo (slotB);
    compareDirty.store (dirty, std::memory_order_relaxed);
}

void FirstAudioProcessor::updateActiveCompareSlot()
{
    const auto slotIndex = static_cast<std::size_t> (
        juce::jlimit (0, 1, activeSlot.load (std::memory_order_relaxed)));

    // One deep copy of the live parameter tree, then compare before storing. The old
    // path called copyToCompareSlot, which made a SECOND full copy of the tree every
    // call - and the editor was calling this on every timer frame, so dragging a knob
    // piled two whole-tree copies per frame on top of the repaint. Skipping the second
    // copy when the live state already matches the stored slot keeps the A/B mirroring
    // cheap in the common case, where nothing has actually changed.
    const auto liveState = parameters.copyState();
    if (compareSlots[slotIndex].isEquivalentTo (liveState))
        return;

    compareSlots[slotIndex] = liveState.createCopy();
    updateCompareDirty();
}

//==============================================================================
//  User presets.
//
//  A factory preset covers the machine's designed range; a user preset freezes the
//  whole machine exactly as it stands. Files are XML state trees (the same format
//  getStateInformation writes into a session) with a .miboxpreset extension, stored in
//  the per-user application data directory, so they survive plugin updates, are
//  shared by every instance and never depend on the session.
//==============================================================================
namespace
{
    juce::String sanitiseUserPresetName (const juce::String& name)
    {
        juce::String safe;
        for (const auto character : name.trim())
            if (character != '/' && character != '\\\\' && character != ':' && character != '?'
                && character != '*' && character != '\"' && character != '<'
                && character != '>' && character != '|')
                safe += character;

        return safe.trim();
    }

    bool makeUserPresetFile (const juce::String& name, juce::String& safeName, juce::File& file)
    {
        safeName = sanitiseUserPresetName (name);
        if (safeName.isEmpty() || safeName == "." || safeName == "..")
            return false;

        const auto directory = FirstAudioProcessor::getUserPresetDirectory();
        file = directory.getChildFile (safeName + ".miboxpreset");

        // Reject any result that does not remain a direct child. This is a
        // second boundary check after removing path separators from the name.
        return file.getParentDirectory().getFullPathName() == directory.getFullPathName();
    }
}

juce::File FirstAudioProcessor::getUserPresetDirectory()
{
    auto directory = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile ("MiBoX")
                       .getChildFile ("Presets");
    if (! directory.isDirectory())
        directory.createDirectory();
    return directory;
}

juce::StringArray FirstAudioProcessor::getUserPresetNames() constיכּ:
{
    juce::StringArray names;
    for (const auto& entry : getUserPresetDirectory().findChildFiles (juce::File::findFiles,
                                                                      false, "*.miboxpreset"))
        names.add (entry.getFileNameWithoutExtension());
    names.sort (true);
    return names;
}

bool FirstAudioProcessor::saveUserPreset (const juce::String& name)
{
    juce::String safe;
    juce::File file;
    if (! makeUserPresetFile (name, safe, file))
        return false;

    // captureState stamps the saved-state format marker, so a preset this build
    // writes never has to be migrated again.
    auto state = captureState (parameters);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (xml == nullptr)
        return false;

    const auto saved = xml->writeTo (file);
    if (saved)
        markPresetClean (safe);
    return saved;
}

bool FirstAudioProcessor::applyUserPreset (const juce::String& name)
{
    juce::String safeName;
    juce::File file;
    if (! makeUserPresetFile (name, safeName, file))
        return false;

    const auto xml = juce::parseXML (file);
    if (xml == nullptr)
        return false;

    auto restored = juce::ValueTree::fromXml (*xml);
    if (! restored.isValid())
        return false;

    // Brought up to the current saved-state format before it is applied, so a preset
    // written by an older build loads the setting the user actually saved. See the
    // format note at the top of this file: the old code decided whether to rescale
    // from the stored value, and every value this plugin can store is <= 1.0 at MIX's
    // old scale, so it rewrote 50% presets as 100% and a preset saved at 1% as 50%.
    // The marker makes it a decision about the FILE rather than about the number.
    migrateStateFormat (restored);

    applyStateWithUndo (restored, "User preset: " + safeName);
    lastPresetIndex.store (-1, std::memory_order_relaxed);
    markPresetClean (safeName);
    return true;
}

bool FirstAudioProcessor::deleteUserPreset (const juce::String& name)
{
    juce::String safeName;
    juce::File file;
    if (! makeUserPresetFile (name, safeName, file) || ! file.existsAsFile())
        return false;

    const auto deleted = file.deleteFile();
    if (currentPresetName.equalsIgnoreCase (safeName))
        markPresetClean ({});
    return deleted;
}
