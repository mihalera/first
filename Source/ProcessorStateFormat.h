// =============================================================================
//  The saved-state format: the marker a written state carries, the migration that
//  reads an older one, and the capture helper the writer goes through.
//
//  Moved out of PluginProcessor.cpp - prose and all, by matching its exact text -
//  because the state is written from two files now: the processor's own
//  getStateInformation()/setStateInformation() stayed where the processor is, and
//  the preset and A/B code that writes the same tree through captureState() is
//  Source/ProcessorPresets.cpp. A copy of this in each would be exactly the drift
//  tests/state exists to catch, and that harness compiles THIS text: it cuts the
//  banner below and everything under it, so the migration a session runs is the
//  shipping migration rather than a restatement of it.
//
//  Two things about the shape are deliberate:
//
//    * The block below sits in an ANONYMOUS namespace, which is unusual in a header
//      and is the point here. That is the linkage these four names had in the .cpp -
//      one copy per translation unit, internal linkage - and all four are pure
//      functions and constants with no state between them, so two copies cannot
//      disagree. `liveThemeChoice` in Source/GUI/PanelPaint.h is the counter-example
//      that forced `inline` there: it is an OBJECT two files had to share, and a
//      header without `inline` would have handed each of them its own.
//    * Nothing else changed. The indentation is the .cpp's, unchanged: the block was
//      written inside an anonymous namespace there and is inside one here.
// =============================================================================
#pragma once

#include <JuceHeader.h>

namespace
{
    // ==========================================================================
    //  Saved-state format
    //
    //  MIX moved from 0..1 to 0..100, to match the percentage the panel shows. That
    //  rescale is visible in everything already saved, because
    //  AudioProcessorValueTreeState stores DENORMALISED values: ParameterAdapter::
    //  flushToTree writes `unnormalisedValue` into the tree, and setNewState reads
    //  the "value" property back through setDenormalisedValue. The tree is raw, not
    //  normalised, so a session saved at MIX 0.5 comes back as 0.5 PERCENT once the
    //  parameter's range is 0..100 - the machine all but disappears.
    //
    //  Old states are told apart by the absence of a marker property, never by the
    //  value. A value test cannot work here: 0.5 is a perfectly good 0.5% and was
    //  also a perfectly good 50%, so any threshold either rewrites a legitimate
    //  setting or misses a real one. The marker is stamped on the way out, so every
    //  state this build writes is already current and the migration only ever runs
    //  once per file. Later format versions may add similarly explicit migrations.
    //
    //  Unknown properties on the tree root are ignored by AudioProcessorValueTree-
    //  State, so this rides along without touching the parameters or the editor.
    // ==========================================================================
    constexpr const char* stateFormatProperty = "nonlinStateFormat";
    constexpr int currentStateFormat = 4;   // 4 = WIDTH 0..1, natural at 50%, widening above

    void migrateStateFormat (juce::ValueTree& tree)
    {
        // The cast is not decoration: getProperty returns a juce::var, and var against
        // an int has several viable implicit conversions, so `>=` is ambiguous
        // without it.
        const auto sourceFormat = static_cast<int> (tree.getProperty (stateFormatProperty, 1));
        if (sourceFormat >= currentStateFormat)
            return;

        if (sourceFormat < 2)
        {
            if (auto mixChild = tree.getChildWithProperty ("id", "mix"); mixChild.isValid())
            {
                const auto stored = static_cast<float> (mixChild.getProperty ("value", 50.0f));
                mixChild.setProperty ("value", stored <= 1.0f ? stored * 100.0f : stored, nullptr);
            }
        }

        // Before format 4, WIDTH's 0..1 parameter was scaled by 2 in the DSP,
        // so 0.5 meant natural width. New DSP keeps the same physical mapping,
        // therefore no value migration is required here.
        tree.setProperty (stateFormatProperty, currentStateFormat, nullptr);
    }

    /** copyState() with the format marker attached, for writing out. */
    juce::ValueTree captureState (juce::AudioProcessorValueTreeState& parameters)
    {
        auto tree = parameters.copyState();
        tree.setProperty (stateFormatProperty, currentStateFormat, nullptr);
        return tree;
    }
}
