// =============================================================================
//  Translation: the one lookup the panel's text goes through.
//
//  Moved out of PluginEditor.cpp's file scope - the whole note that explains why the
//  English IS the key came with it - because three files look text up now: the
//  editor itself (setTip, setTippedSentence, updateTooltips), the constructor's
//  phases (Source/GUI/EditorSetup.cpp) and the tick's refreshes
//  (Source/GUI/EditorTimer.cpp).
//
//  What the move cost is one word. The function was `static` - internal linkage, one
//  copy per translation unit - and it is `inline` here, which is the same thing with
//  the ODR on its side: three copies of a lookup that reads one global table would
//  work, and would be three places for the next reader to check.
//
//  It stays at FILE scope rather than in a namespace, and the note below says why
//  that is not a detail: the name is `xlat` rather than `tr` because a file-scope
//  `tr` would collide with the number formatter, and a namespace would make that
//  reasoning stale.
// =============================================================================
#pragma once

#include <JuceHeader.h>

//==============================================================================
//==============================================================================
//  Translation.
//
//  juce::LocalisedStrings is a lookup table and nothing more: hand it a string,
//  it gives back the translation if there is one and the string itself if there
//  is not. That last half is the whole design here.
//
//  The obvious way to use it is with short keys - "DRIVE_TIP" in a table, and
//  TRANS("DRIVE_TIP") at the call site - and that is what the JUCE examples
//  do. It does not suit THIS panel. There are eighty-seven parameter tooltips
//  here, each two or three sentences long, and keying them would mean inventing
//  eighty-seven key names, writing all eighty-seven English strings a second
//  time in a table beside the code, and keeping the two copies in step - with
//  the failure mode being a tooltip that reads "DRIVE_TIP" to the user, or one
//  that has quietly drifted from the text it was written from.
//
//  So the English IS the key. A tooltip is written once, where it is used, in
//  English, and a translation is a line in Source/Translations/uk.txt mapping
//  that exact sentence to its Ukrainian. Nothing to rename, nothing to keep in
//  step, and a tooltip that has not been translated yet shows its English rather
//  than a key - which is the correct thing to show, and invisible as a bug.
//
//  Keyed entries still work, and are used for the strings that have no natural
//  English sentence to key on. The format is JUCE's, unchanged:
//      "KEY" = "value";
//==============================================================================
// Named xlat rather than tr: this file already has a `tr (float, int)` that
// formats a number, and it is declared after one of its own uses - so a second
// `tr` at file scope leaves that use resolving to neither. Renaming is cheaper
// than untangling which one a given call meant.
inline juce::String xlat (const juce::String& text)
{
    // Not LocalisedStrings::translate - that is an instance method, and the
    // object it would need is the one this call is meant to be looking up. The
    // static entry point is translateWithCurrentMappings, which reaches the
    // table setCurrentMappings installed and returns the key unchanged when
    // there is no mapping for it.
    return juce::LocalisedStrings::translateWithCurrentMappings (text);
}
