#!/usr/bin/env python3
"""Checks that every widget the panel owns is re-inked when the theme changes.

    python3 tests/theme/check_theme_wiring.py

The panel has three themes - ivory, charcoal and metal - and ONE funnel that
paints them: applyTheme(). Every widget has to go through it, and the failures
of a widget that does not are all the same shape and all silent:

  * a widget the constructor inks with `paletteFor (false)` - the IVORY panel's
    colours - and nothing re-inks keeps the light panel's ink on charcoal and
    metal. The constructor's calls look right, the code compiles, and the
    control is simply invisible or unreadable until someone cycles the theme;
  * a widget that is never inked AT ALL falls back to the Look and Feel's own
    colour scheme, which is juce::LookAndFeel_V4's DARK one whatever panel is
    showing, so it prints near-white text on the ivory panel. That is what a
    juce::Slider text box, a juce::Label and a juce::ComboBox value all do
    unstyled.

Both were real defects in this panel, reported from screenshots: the COMP page's
six captions and the COMP and AUTOTUNE pages' knob readouts were white on every
theme, and the model and IR pickers' buttons kept the ivory plate on charcoal.
Every one of them was a widget that the theme never touched - which is a
property of the SOURCE, so it can be read out of the source here, with no JUCE,
no window and no compiler.

What is checked:

  1. Every widget member declared in Source/PluginEditor.h is named somewhere
     applyTheme() reaches - directly, or through a styling helper applyTheme()
     calls (styleCombo, styleTabButtons, updateWorkflowButtons, ...). A helper is
     followed one call at a time until nothing new appears, so the check does not
     care how the styling is factored; it only cares that the widget is reached.

  2. Every look-and-feel the editor owns carries a ComboBox::textColourId of its
     own. That is not a widget and no widget member accounts for it: the
     nothing-selected placeholder ("FACTORY...", "USER...", "Select tape") is
     drawn by LookAndFeel_V2::drawComboBoxTextWhenNothingSelected, which reads
     ComboBox::textColourId off the LOOK AND FEEL's own colour map and never off
     the box it is drawing - so the per-box styleCombo() calls cannot reach it,
     and an unstyled look and feel printed those captions white on the ivory
     panel. The one statement that can set it is written by name, and read by
     name here.

Exit status is 0 when the wiring is complete and 1 when it is not, with each
unreached widget named.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "Source" / "PluginEditor.h"
SOURCE = ROOT / "Source" / "PluginEditor.cpp"

# The widget types the theme has to reach. A plain data member (an int, a float,
# a juce::String, an atomic) is not a widget and is skipped; a new editor widget
# that is one of these types is covered the moment it is declared.
WIDGET_TYPES = {
    "juce::Slider",
    "juce::Label",
    "juce::ComboBox",
    "juce::TextButton",
    # The panel's own widgets. They are not JUCE controls - each paints itself
    # from the palette it is handed - but they are widgets on the panel all the
    # same, and the theme still has to reach them.
    "LoudnessMeter",
    "CompressorMeter",
    "MixBar",
    "TapeScene",
    "SpindownButton",
}

# The function the whole checker hangs off: the one place a theme change is
# applied. Declared here rather than discovered, because a checker that follows
# whatever looks like a theme function would pass on a file where the real one
# had been renamed away.
ROOT_FUNCTION = "applyTheme"

DECLARATION = re.compile(
    r"^(?:std::array\s*<\s*(?P<element>[\w:]+)\s*,\s*[^>]+>"
    r"|(?P<plain>[\w:]+))\s+"
    r"(?P<name>[A-Za-z_]\w*)\s*(?:\{[^;{}]*\})?\s*;",
)
LOOK_AND_FEEL_DECLARATION = re.compile(
    r"^\s*(?P<type>\w*LookAndFeel)\s+(?P<name>[A-Za-z_]\w*)\s*(?:\{[^;{}]*\})?\s*;", re.M
)


def strip_noise(text: str) -> str:
    """The source with comments and literals blanked, so a name that only occurs
    in a comment cannot be mistaken for a use of the widget."""
    out = []
    i = 0
    length = len(text)
    while i < length:
        if text.startswith("//", i):
            end = text.find("\n", i)
            end = length if end < 0 else end
            out.append(" " * (end - i))
            i = end
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            end = length if end < 0 else end + 2
            out.append("".join(" " if c != "\n" else "\n" for c in text[i:end]))
            i = end
        elif text[i] in "\"'":
            quote = text[i]
            j = i + 1
            while j < length:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == quote:
                    j += 1
                    break
                j += 1
            out.append(" " * (j - i))
            i = j
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def brace_end(text: str, open_index: int) -> int:
    """Index just past the brace that closes the one at open_index."""
    depth = 0
    i = open_index
    while i < len(text):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    raise ValueError("unbalanced braces")


def function_bodies(source: str) -> dict[str, str]:
    """Every editor member function and file-scope lambda, by name.

    Two shapes, because the styling in this file is written in both: a member
    function (`void FirstAudioProcessorEditor::styleCombo (...)`) and a local
    lambda (`const auto inkKnob = [&palette] (juce::Slider& slider) { ... };`),
    which is how most of applyTheme's own helpers are spelled.
    """
    bodies: dict[str, str] = {}
    stripped = strip_noise(source)

    for match in re.finditer(r"FirstAudioProcessorEditor::(\w+)\s*\(", stripped):
        name = match.group(1)
        open_index = stripped.find("{", match.end())
        if open_index < 0:
            continue
        bodies.setdefault(name, stripped[open_index:brace_end(stripped, open_index)])

    for match in re.finditer(r"const auto (\w+)\s*=\s*\[", stripped):
        name = match.group(1)
        open_index = stripped.find("{", match.end())
        if open_index < 0:
            continue
        bodies.setdefault(name, stripped[open_index:brace_end(stripped, open_index)])

    return bodies


def words(text: str) -> set[str]:
    return set(re.findall(r"[A-Za-z_]\w*", text))


def reached_text(bodies: dict[str, str], root: str) -> str:
    """The root function's body plus the body of every helper it reaches."""
    if root not in bodies:
        raise SystemExit(f"check_theme_wiring: Source/PluginEditor.cpp has no {root}()")

    accumulated = bodies[root]
    reached = {root}
    while True:
        for name, body in bodies.items():
            if name in reached:
                continue
            if name in words(accumulated):
                reached.add(name)
                accumulated += "\n" + body
        else:
            return accumulated


def widget_members(header: str) -> list[tuple[str, str]]:
    """The editor's widget members, read one declaration at a time.

    A declaration with a `*` in it is a POINTER - a LookAndFeel's own table of
    the knobs it has been told about, a hand-off to a renderer - and nothing
    this panel paints, so it is skipped rather than reported as an unreached
    widget.
    """
    members = []
    for line in strip_noise(header).splitlines():
        text = line.strip()
        if "*" in text:
            continue
        match = DECLARATION.match(text)
        if match is None:
            continue
        kind = match.group("element") or match.group("plain")
        if kind in WIDGET_TYPES:
            members.append((kind, match.group("name")))
    return members


def look_and_feel_members(header: str) -> list[str]:
    return [
        match.group("name")
        for match in LOOK_AND_FEEL_DECLARATION.finditer(strip_noise(header))
    ]


def main() -> int:
    header = HEADER.read_text(encoding="utf-8")
    source = SOURCE.read_text(encoding="utf-8")

    bodies = function_bodies(source)
    themed = reached_text(bodies, ROOT_FUNCTION)
    themed_words = words(themed)

    members = widget_members(header)
    unreached = [(kind, name) for kind, name in members if name not in themed_words]

    look_and_feels = look_and_feel_members(header)
    unplaced = [
        name
        for name in look_and_feels
        if not re.search(
            rf"\b{re.escape(name)}\s*\.\s*setColour\s*\(\s*juce::ComboBox::textColourId",
            themed,
        )
    ]

    print(
        f"check_theme_wiring: {len(members)} widgets, {len(bodies)} bodies, "
        f"{len(look_and_feels)} look-and-feel(s)"
    )

    for kind, name in unreached:
        print(f"  NOT RE-INKED  {kind} {name} - applyTheme() never reaches it")
    for name in unplaced:
        print(f"  NO PLACEHOLDER INK  {name} - ComboBox::textColourId is never set on it")

    if unreached or unplaced:
        print(
            "check_theme_wiring: FAILED - a widget keeps the previous theme, or "
            "prints the Look and Feel's own white"
        )
        return 1

    print("check_theme_wiring: PASSED - every widget and look and feel follows the theme")
    return 0


if __name__ == "__main__":
    sys.exit(main())
