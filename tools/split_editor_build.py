#!/usr/bin/env python3
"""The second split of Source/PluginEditor.cpp: the build, the look, the paint and the tick.

Four moves, each a cut of exact text (signature through closing brace, with the comment
block written directly above it):

  Source/GUI/EditorSetup.cpp   the 23 constructor phases and createDecorativePhysics:
                               everything the constructor BUILDS the panel with.
  Source/GUI/EditorTheme.cpp   applyTheme() and the six styling functions it drives.
  Source/GUI/EditorPaint.cpp   paint(), the texture weights and the layer rebuild: what
                               the panel DRAWS.
  Source/GUI/EditorTimer.cpp   timerCallback() and the eleven refreshes it drives: what
                               the panel does thirty times a second.

Each new file gets its own preamble rather than the editor's: the includes it needs, and
the `j37panel::` names ITS text actually uses, worked out from the text rather than
listed by hand. The editor's own preamble says why that matters - "the names this file
does not use are not imported: a header member that stops being reachable from here
should not look reachable" - and a file that imports the whole panel vocabulary to use
three of it says the opposite.

Two guard rails, because these are the functions a reader is most likely to break:

  * An unbalanced preprocessor conditional inside a cut aborts the move. `#if JUCE_DEBUG`
    wraps one of them, and moving the body out from under its `#if` is how a file ends up
    with a stray `#endif` and no error until a debug build.
  * Every anchor is asserted to occur exactly once, so a signature that has been renamed
    stops this rather than silently cutting nothing.
"""

from __future__ import annotations

import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parents[1]
EDITOR = REPO / "Source" / "PluginEditor.cpp"
GUI = REPO / "Source" / "GUI"

SETUP = [
    "void FirstAudioProcessorEditor::setUpWindowAndSize()",
    "void FirstAudioProcessorEditor::createDebugInspector()",
    "void FirstAudioProcessorEditor::setUpLanguageAndTuning()",
    "void FirstAudioProcessorEditor::setUpTooltipsAndPhysics()",
    "void FirstAudioProcessorEditor::stylePanelLabels()",
    "void FirstAudioProcessorEditor::setUpControlStrip()",
    "void FirstAudioProcessorEditor::setUpTabButtons()",
    "void FirstAudioProcessorEditor::setUpDeckSelectors()",
    "void FirstAudioProcessorEditor::setUpTopSwitches()",
    "void FirstAudioProcessorEditor::setUpOversamplingAndInstrument()",
    "void FirstAudioProcessorEditor::setUpVinylSelectors()",
    "void FirstAudioProcessorEditor::setUpTransportRow()",
    "void FirstAudioProcessorEditor::setUpDelayRow()",
    "void FirstAudioProcessorEditor::setUpModeAndGlSwitches()",
    "void FirstAudioProcessorEditor::setUpSettingsSwitches()",
    "void FirstAudioProcessorEditor::setUpModelButtons()",
    "void FirstAudioProcessorEditor::setUpInlineLists()",
    "void FirstAudioProcessorEditor::setUpEqCornerSwitches()",
    "void FirstAudioProcessorEditor::setUpPresetBar()",
    "void FirstAudioProcessorEditor::addDeckControlsToPanel()",
    "void FirstAudioProcessorEditor::setUpSceneAndMeters()",
    "void FirstAudioProcessorEditor::setUpOpenGlRendering()",
    "void FirstAudioProcessorEditor::finishConstruction()",
    "void FirstAudioProcessorEditor::createDecorativePhysics()",
]

THEME = [
    "void FirstAudioProcessorEditor::applyTheme()",
    "void FirstAudioProcessorEditor::styleLabel (juce::Label& label, const juce::String& text,",
    "void FirstAudioProcessorEditor::styleGlButton (bool isOn)",
    "void FirstAudioProcessorEditor::styleTransportButtons()",
    "void FirstAudioProcessorEditor::styleSpindownButton()",
    "void FirstAudioProcessorEditor::styleTabButtons()",
    "void FirstAudioProcessorEditor::updateWorkflowButtons()",
]

PAINT = [
    "void FirstAudioProcessorEditor::configureTextureWeights (float drive, float gainReduction)",
    "void FirstAudioProcessorEditor::rebuildPanelTextureLayers()",
    "void FirstAudioProcessorEditor::paint (juce::Graphics& g)",
]

TIMER = [
    "void FirstAudioProcessorEditor::refreshNeuralStatus()",
    "void FirstAudioProcessorEditor::refreshIrStatus()",
    "void FirstAudioProcessorEditor::refreshUserPresetList()",
    "void FirstAudioProcessorEditor::refreshPresetList()",
    "void FirstAudioProcessorEditor::refreshModeButtonCaption()",
    "void FirstAudioProcessorEditor::syncGlSwitchState()",
    "int FirstAudioProcessorEditor::currentTransportState() const",
    "juce::Rectangle<int> FirstAudioProcessorEditor::statusPillBounds() const",
    "juce::Rectangle<int> FirstAudioProcessorEditor::deckAnimationBounds() const",
    "void FirstAudioProcessorEditor::repaintAnimatedSurfaces()",
    "void FirstAudioProcessorEditor::timerCallback()",
]

PANEL_USING = [
    "minStageDb", "maxStageDb", "liveThemeChoice", "paletteForTheme", "paletteFor",
    "principleColour", "principleParameterIds", "principleOf", "fillCylinderBarrel",
    "fillLitDome", "tabSpecs", "tabSubtitles", "controlsDividerOffset", "controlIsInTab",
    "tabsCoverAllControls", "index_of_tab_named", "formatDb", "shrinkingFont", "drawScrew",
    "drawPanel", "ControlSpec", "controlSpecs", "parameterTooltipText", "KnobMetrics",
    "computeKnobMetrics",
]

PANEL_HEADERS = [
    '#include "GUI/PanelTabs.h"',
    '#include "GUI/PanelPaint.h"',
    '#include "GUI/PanelText.h"',
    '#include "GUI/KnobMetrics.h"',
]


def brace_end(text: str, start: int) -> int:
    depth = 0
    seen = False
    i = start

    while i < len(text):
        c = text[i]

        if c == "/" and i + 1 < len(text) and text[i + 1] == "/":
            j = text.find("\n", i)
            i = len(text) if j < 0 else j + 1
        elif c == "/" and i + 1 < len(text) and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            i = len(text) if j < 0 else j + 2
        elif c in "\"'":
            quote = c
            i += 1
            while i < len(text) and text[i] != quote:
                i += 2 if text[i] == "\\" else 1
            i += 1
        else:
            if c == "{":
                depth += 1
                seen = True
            elif c == "}":
                depth -= 1
                if seen and depth == 0:
                    return i + 1
            i += 1

    raise SystemExit("split_editor_build: a block was opened but never closed")


def with_leading_comment(text: str, start: int) -> int:
    """Walk back over the comment block written directly above a definition.

    A blank line ends the walk, so a section banner two lines up stays behind; the
    rule the callers rely on is that the note explaining WHY a function is shaped the
    way it is sits immediately above it, and travels with it.
    """
    position = start

    while position > 0:
        previous_end = position - 1
        previous_start = text.rindex("\n", 0, previous_end) + 1
        line = text[previous_start:previous_end].strip()

        if line.startswith("//") or line.startswith("*") or line.startswith("/*"):
            position = previous_start
            continue

        break

    return position


def cut_function(text: str, signature: str, label: str) -> str:
    if text.count(signature) != 1:
        raise SystemExit(f"split_editor_build: {label}: `{signature}` appears "
                         f"{text.count(signature)} times, not once")

    body_start = text.index(signature)
    end = brace_end(text, body_start)
    start = with_leading_comment(text, body_start)

    span = text[start:end]

    opens = len(re.findall(r"^\s*#\s*(?:if|ifdef|ifndef)\b", span, re.M))
    closes = len(re.findall(r"^\s*#\s*endif\b", span, re.M))
    if opens != closes:
        raise SystemExit(f"split_editor_build: {label}: the cut holds {opens} `#if` and "
                         f"{closes} `#endif`, so a conditional would be split by the move")

    return span


def preamble(block: str) -> str:
    """The includes and using-declarations THIS text needs, and no others.

    Worked out from the moved text rather than copied from PluginEditor.cpp's own
    preamble: the editor imports the whole panel vocabulary because it uses the whole
    of it, and a file that imports twenty-five names to use three says the opposite
    of what that comment says.
    """
    def uses(*names: str) -> bool:
        return any(re.search(r"\b" + re.escape(name) + r"\b", block) for name in names)

    sections = [["// The editor's own header: the class, the members and the palette type.",
                 '#include "PluginEditor.h"']]

    panel_headers = []
    if uses("tabSpecs", "tabSubtitles", "controlsDividerOffset", "controlIsInTab",
            "tabsCoverAllControls", "index_of_tab_named"):
        panel_headers.append('#include "GUI/PanelTabs.h"')
    if uses("paletteFor", "paletteForTheme", "principleColour", "principleOf",
            "principleParameterIds", "liveThemeChoice", "minStageDb", "maxStageDb",
            "fillCylinderBarrel", "fillLitDome", "formatDb", "shrinkingFont",
            "drawScrew", "drawPanel"):
        panel_headers.append('#include "GUI/PanelPaint.h"')
    if uses("ControlSpec", "controlSpecs", "parameterTooltipText"):
        panel_headers.append('#include "GUI/PanelText.h"')
    if uses("KnobMetrics", "computeKnobMetrics"):
        panel_headers.append('#include "GUI/KnobMetrics.h"')
    if uses("xlat"):
        panel_headers.append('#include "GUI/PanelTranslations.h"')

    if panel_headers:
        sections.append(["// The panel vocabulary this file reads through, from the headers that hold",
                         "// it - the paint kit, the page table and the knob table."] + panel_headers)

    used = [name for name in PANEL_USING if re.search(r"\b" + re.escape(name) + r"\b", block)]
    if used:
        sections.append(["// Only the names this file's own text uses: a header member that is not",
                         "// reachable from here should not look reachable."]
                        + [f"using j37panel::{name};" for name in used])

    standard = []
    if re.search(r"\bstd::(vector|unique_ptr|make_unique)\b", block):
        standard.append("#include <vector>")
    if re.search(r"\bstd::(move|pair|swap)\b", block):
        standard.append("#include <utility>")
    if re.search(r"\bstd::(cout|cerr)\b", block):
        standard.append("#include <iostream>")
    if standard:
        sections.append(sorted(standard))

    if "melatonin_inspector" in block:
        sections.append(["#if JUCE_DEBUG",
                         " #include <melatonin_inspector/melatonin_inspector.h>",
                         "#endif"])

    # A blank line after the banner, then one blank line between sections.
    return "\n" + "\n\n".join("\n".join(section) for section in sections) + "\n\n"


def banner_of(name: str) -> str:
    for file_name, _, banner in GROUPS:
        if file_name == name:
            return banner
    raise SystemExit(f"split_editor_build: no banner for {name}")


def cut_translation_block(src: str) -> tuple[str, str]:
    """xlat, with the whole note that explains why the English IS the key.

    Three files call it now (the editor's own setTip/setTippedSentence and the two
    phases and refreshes that set a caption), so it cannot stay file-local - and it
    cannot be copied, because the lookup table it reads is the same one every call
    goes through. It goes to a header of its own, `inline`, at file scope: the note
    above it says why the NAME is `xlat` rather than `tr`, and that note is about
    file scope, so a namespace would make it stale.
    """
    signature = "static juce::String xlat (const juce::String& text)"
    if src.count(signature) != 1:
        raise SystemExit("split_editor_build: xlat is not where it was")

    start = src.index(signature)
    start = with_leading_comment(src, start)
    # The block above xlat ENDS with a doubled rule - one closing the section before
    # it and one opening this one - and the pair belongs to the note that follows.
    previous = src.rindex("\n", 0, src.rindex("\n", 0, start) + 1) + 1
    if src[previous:start].strip().startswith("//="):
        start = previous

    end = brace_end(src, src.index(signature))
    block = src[start:end]

    if block.count("static juce::String xlat") != 1:
        raise SystemExit("split_editor_build: the xlat block was not cut whole")

    return src[:start] + src[end:], block.replace(
        "static juce::String xlat", "inline juce::String xlat", 1)


def main() -> int:
    src = EDITOR.read_text()
    original = src

    src, xlat = cut_translation_block(src)
    (GUI / "PanelTranslations.h").write_text(
        TRANSLATIONS_BANNER + TRANSLATIONS_PREAMBLE + xlat.rstrip("\n") + "\n")
    src = src.replace('#include "GUI/PanelText.h"\n',
                      '#include "GUI/PanelText.h"\n#include "GUI/PanelTranslations.h"\n', 1)
    print(f"wrote Source/GUI/PanelTranslations.h: {len(xlat.splitlines())} moved lines")

    for name, signatures, banner in GROUPS:
        pieces = []
        for signature in signatures:
            span = cut_function(src, signature, name)
            src = src.replace(span, "", 1)
            pieces.append(span.rstrip("\n"))

        body = "\n\n".join(pieces) + "\n"
        (GUI / name).write_text(banner + preamble(body) + body)
        print(f"wrote Source/GUI/{name}: {len(body.splitlines())} moved lines")

    if src == original:
        raise SystemExit("split_editor_build: nothing was cut")

    EDITOR.write_text(src)
    print(f"PluginEditor.cpp: {len(original.splitlines())} -> {len(src.splitlines())} lines")
    return 0


SETUP_BANNER = """// =============================================================================
//  The editor's build: the 23 phases the constructor calls, in the order it calls
//  them, plus the decorative physics they start.
//
//  Moved out of PluginEditor.cpp - prose and all, by matching each definition's
//  exact text - because the file's other half is the panel's LIFETIME: the
//  constructor, the timer's tick, the layout arithmetic and the handful of
//  commands the panel sends back to the processor. What the panel is BUILT from is
//  this file, and reading it no longer means scrolling through resized().
//
//  The rule the phases were written under travels with them and is repeated in
//  PluginEditor.h: a phase may rely on the phases BEFORE it having run and on
//  nothing else. tests/layout/extract.py still holds both ends of that promise -
//  the header's declared order and the constructor's call sequence - and now reads
//  them out of the editor's own source across every file it is split into.
// =============================================================================
"""

THEME_BANNER = """// =============================================================================
//  The panel's theme: the one function that applies a theme, and the styling
//  helpers it drives.
//
//  Moved out of PluginEditor.cpp, prose and all. It is here rather than with the
//  paint routines because it is the DECISION - which theme, which palette, which
//  colour on which widget - and paint() is the drawing that follows from it.
//
//  tests/theme/check_theme_wiring.py hangs off applyTheme(): it reads every widget
//  declared in PluginEditor.h, follows the calls applyTheme() makes (one helper at
//  a time until nothing new appears) and fails on a widget the theme never reaches.
//  It reads the editor's own source as ONE text across the files the editor is
//  split into, so a helper that lives here and a widget styled from there are the
//  same graph it always was.
// =============================================================================
"""

PAINT_BANNER = """// =============================================================================
//  What the panel draws: paint(), and the two functions that prepare the texture
//  layers it blits.
//
//  Moved out of PluginEditor.cpp, prose and all. The texture layers are here rather
//  than with the setup because they are re-built on a resize and read on every
//  frame: their whole reason for existing is the drawing below them.
// =============================================================================
"""

TIMER_BANNER = """// =============================================================================
//  The tick, and everything it refreshes: timerCallback(), the bounded repaint it
//  asks for, and the eleven readers that turn the processor's state into what the
//  panel shows.
//
//  Moved out of PluginEditor.cpp, prose and all. Thirty times a second this is what
//  runs, and it was spread through the file in eleven places, each next to the
//  control it updates. What it costs to keep them together is that a control's
//  styling and its refresh are now in two files (EditorTheme.cpp styles it, this
//  refreshes it); what it buys is that everything that happens on a tick is
//  readable as one list.
// =============================================================================
"""

TRANSLATIONS_BANNER = """// =============================================================================
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

"""

TRANSLATIONS_PREAMBLE = ""

GROUPS = [("EditorSetup.cpp", SETUP, SETUP_BANNER),
          ("EditorTheme.cpp", THEME, THEME_BANNER),
          ("EditorPaint.cpp", PAINT, PAINT_BANNER),
          ("EditorTimer.cpp", TIMER, TIMER_BANNER)]


if __name__ == "__main__":
    sys.exit(main())
