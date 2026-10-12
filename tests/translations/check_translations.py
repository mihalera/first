#!/usr/bin/env python3
"""Checks the shipped translation tables against the strings they translate.

The English IS the key in this plugin - see the note above xlat() in
PluginEditor.cpp. A tooltip is written once, where it is used, and a translation
is a line in Source/Translations/<language>.txt whose key is that exact sentence.
Nothing keeps the two ends in step by itself, and the failure mode is silent: the
sentence gets reworded, its translation stays behind, and from then on the panel
shows English for that one control. No build log says so and no screenshot shows
it - it takes somebody reading the translated panel to notice a single tooltip is
not translated.

So this checks the three things about the tables that can be checked without
launching the plugin:

  1. NO DEAD KEYS. Every key must still exist as a string in Source/. A key that
     matches nothing is a translation of a sentence the plugin no longer says:
     either the control was removed (fix: delete the entry) or the sentence was
     reworded (fix: re-key it, which is what makes the translation fire again).
     Reported as a failure because a dead entry is also a LIE about progress - it
     counts as translated while the user reads English.

  2. NO MISSING LOOKUPS. An identifier-shaped key ("LANGUAGE_ENGLISH") is a lookup
     rather than a sentence, and xlat() has no fallback for a lookup that is not in
     the table: the user reads the KEY. So every `xlat ("IDENTIFIER")` in the
     source has to have an entry at all, and - since one language needing real text
     for a lookup means the others need it too, or they are showing the key in a
     language that asked for a word - every language has to have it as soon as any
     table maps it to something other than the key itself. A lookup whose every
     entry reads back as the key ("MACHINE" = "MACHINE", the tab names) is exempt:
     absence gives the identical text, so there is nothing to translate.

  3. NO AMBIGUOUS OR EMPTY ENTRIES. JUCE keeps the translations in a map, so a key
     declared twice in one file silently resolves to the last one - two readings of
     the same sentence, one of which nobody can see. And loadFromText DROPS an
     entry whose key or value is empty, so an empty value is not "no translation",
     it is a line that does nothing at all while reading as a translated one.

The table is parsed with JUCE's own rules rather than with a parser that merely
looks reasonable - LocalisedStrings::loadFromText, in the vendored JUCE, and only
that: a line is used when it starts with a quote, and exactly five escapes are
understood (\\" \\' \\t \\r \\n). A key written with any other escape, or one that
depends on a leading or trailing space OUTSIDE the quotes, looks right here and
misses at runtime, which is the one failure a checker like this must not have.

Usage:  python3 tests/translations/check_translations.py
"""

from __future__ import annotations

import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
TRANSLATIONS = REPO / "Source" / "Translations"
SOURCE_DIR = REPO / "Source"

SOURCE_SUFFIXES = (".cpp", ".h", ".mm")

# A key that is a LOOKUP rather than a sentence: no spaces, upper case, digits and
# underscores only. Nothing in the source is user-facing text in that shape, which
# is exactly what makes it a key.
LOOKUP_KEY = re.compile(r"^[A-Z][A-Z0-9_]*$")

# `xlat ("SOMETHING_LIKE_THIS")` - the call sites of the keyed entries. Calls that
# are handed a variable (xlat (spec.name), xlat (label.getText())) are not matched,
# and do not need to be: what they are handed is a sentence, checked by rule 1.
LOOKUP_CALL = re.compile(r'xlat \(\s*"([^"]*)"\s*\)')

# JUCE's unescaping, and only JUCE's: these five replacements, in this order.
ESCAPES = (('\\"', '"'), ("\\'", "'"), ("\\t", "\t"), ("\\r", "\r"), ("\\n", "\n"))

# The C++ compiler understands more than JUCE does. Decoded here so the source side
# of the comparison is the string the compiler builds.
CPP_ESCAPES = {'"': '"', "'": "'", "\\": "\\", "n": "\n", "t": "\t", "r": "\r",
               "a": "\a", "b": "\b", "f": "\f", "v": "\v", "0": "\0"}


def juce_unescape (text: str) -> str:
    """The string JUCE's loader would install for this table text."""
    for escaped, plain in ESCAPES:
        text = text.replace(escaped, plain)

    return text


def parse_table (path: pathlib.Path) -> tuple[list[tuple[int, str, str]], list[str]]:
    """Returns the table's entries as (line number, key, value), plus any problems.

    Comments are ignored, as they are at runtime: JUCE only reads a line that starts
    with a quote after trimming, so `//` needs no special case here either.
    """
    entries: list[tuple[int, str, str]] = []
    problems: list[str] = []

    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw.strip()

        if not line.startswith('"'):
            continue

        quotes = [match.start() for match in re.finditer(r'(?<!\\)"', line)]

        if len(quotes) < 4:
            problems.append(f"{path.name}:{number}: not a `\"key\" = \"value\";` line "
                            f"(JUCE would skip it): {line[:70]}")
            continue

        opening, closing, value_opening, value_closing = quotes[:4]
        key = juce_unescape(line[opening + 1:closing])
        value = juce_unescape(line[value_opening + 1:value_closing])

        # JUCE installs neither an empty key nor an empty value: loadFromText drops
        # both, so a line with an empty value does nothing at all.
        if not key or not value:
            problems.append(f"{path.name}:{number}: an empty "
                            f"{'key' if not key else 'value'} is dropped by JUCE, so this line "
                            f"translates nothing")
            continue

        entries.append((number, key, value))

    return entries, problems


def cpp_strings (path: pathlib.Path) -> list[str]:
    """Every string literal in a C++ file, with adjacent literals joined.

    One pass over the file, because a comment is not a source of strings and a
    literal is not a comment: a tooltip that contains "//", or a sentence commented
    out beside the live one, must not be mistaken for the string the plugin says.
    Adjacent literals are joined because the formatter wraps a long tooltip across
    lines - `"a tooltip that runs "\n"on"` is one string to the compiler, and it is
    the string the table must key on.
    """
    text = path.read_text(encoding="utf-8", errors="replace")
    values: list[str] = []
    run = ""
    index = 0
    length = len(text)

    while index < length:
        character = text[index]

        if character == "/" and text.startswith("//", index):
            newline = text.find("\n", index)
            index = length if newline < 0 else newline
            continue

        if character == "/" and text.startswith("/*", index):
            end = text.find("*/", index + 2)
            index = length if end < 0 else end + 2
            continue

        if character == "'":
            index += 1
            while index < length and text[index] != "'":
                index += 2 if text[index] == "\\" else 1
            index += 1
            if run:
                values.append(run)
                run = ""
            continue

        if character == '"':
            index += 1
            while index < length and text[index] != '"':
                if text[index] == "\\" and index + 1 < length:
                    run += CPP_ESCAPES.get(text[index + 1], text[index + 1])
                    index += 2
                else:
                    run += text[index]
                    index += 1
            index += 1
            continue

        # Whitespace keeps a run of adjacent literals alive; anything else ends it.
        if character in " \t\r\n":
            index += 1
            continue

        if run:
            values.append(run)
            run = ""

        index += 1

    if run:
        values.append(run)

    return values


def main () -> int:
    if not TRANSLATIONS.is_dir():
        print(f"check_translations: no translation directory at {TRANSLATIONS}", file=sys.stderr)
        return 1

    tables = sorted(TRANSLATIONS.glob("*.txt"))

    if not tables:
        print(f"check_translations: no .txt tables in {TRANSLATIONS}", file=sys.stderr)
        return 1

    sources = sorted(path
                     for path in SOURCE_DIR.rglob("*")
                     if path.suffix in SOURCE_SUFFIXES and path.is_file())

    strings: set[str] = set()

    for path in sources:
        strings.update(cpp_strings(path))

    problems: list[str] = []
    entries_by_table: dict[str, dict[str, int]] = {}
    entry_count = 0

    for path in tables:
        entries, syntax_problems = parse_table(path)
        problems += syntax_problems
        entry_count += len(entries)
        seen: dict[str, int] = {}

        for number, key, _ in entries:
            if key in seen:
                problems.append(f"{path.name}:{number}: `{key[:60]}` is already defined on line "
                                f"{seen[key]}; JUCE keeps the last one, so the earlier translation "
                                f"is dead text")
            seen[key] = number

            if key not in strings:
                problems.append(f"{path.name}:{number}: no string in Source/ matches this key, so "
                                f"the entry can never fire - the sentence was reworded or its "
                                f"control removed: {key[:70]}")

        entries_by_table[path.name] = seen

    # Rule 2, from the call sites: what the code actually looks up, and what each
    # language would show for it.
    language_of = {name: {key: value for _, key, value in parse_table(TRANSLATIONS / name)[0]}
                   for name in entries_by_table}

    for path in sources:
        for call in LOOKUP_CALL.findall(path.read_text(encoding="utf-8", errors="replace")):
            if not LOOKUP_KEY.match(call):
                continue

            found = {name: table[call] for name, table in language_of.items() if call in table}

            if not found:
                problems.append(f"{path.name}: `xlat (\"{call}\")` has no entry in any table, so "
                                f"the panel shows `{call}` to the user")
                continue

            # Only once a language has real TEXT for the lookup does its absence
            # elsewhere mean showing the key instead of a word.
            if not any(value != call for value in found.values()):
                continue

            for name in entries_by_table:
                if name not in found:
                    problems.append(f"{path.name} looks up `{call}` and {name} has no entry for "
                                    f"it, so that language shows the key itself to the user")

    if problems:
        for problem in problems:
            print(f"check_translations: {problem}", file=sys.stderr)

        print(f"check_translations: {len(problems)} problem(s)", file=sys.stderr)
        return 1

    names = ", ".join(path.name for path in tables)
    print(f"check_translations: {len(tables)} table(s) [{names}], {entry_count} entries, "
          f"{len(strings)} distinct strings in {len(sources)} source file(s)")
    print("check_translations: PASSED - every key is live, every lookup is in every language, "
          "no entry is ambiguous or empty")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
