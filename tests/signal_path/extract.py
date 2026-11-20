#!/usr/bin/env python3
"""Extracts the signal-path helpers the harness measures into its include.

    python3 tests/dsp/extract.py            # (the existing DSP include)
    python3 tests/signal_path/extract.py <output.inc>

The transport harness learned the lesson this one follows: everything the plugin
compiles is INCLUDED from Source/, never cut. `configureTrackGeometry` and
`nextNoise` are file-local `inline` functions in Source/ProcessorSignalPath.cpp -
they cannot be included without the 3,000-line file, and the file cannot be
compiled without JUCE. So the shipping text of exactly those two helpers is read
out of the file, each under a marker naming where it was found, with brace
matching that skips comments and string literals. A helper that moves, is
renamed, or loses its `noexcept` fails here loudly instead of the harness going
on measuring a copy of something else.
"""

from __future__ import annotations

import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
SIGNAL_PATH = REPO / "Source" / "ProcessorSignalPath.cpp"

# The helpers, by the exact signatures the file declares. Both are `inline`
# functions in an anonymous namespace, so a signature match is the whole
# identification: there is nothing else in Source/ with these names.
HELPERS = [
    ("nextNoise", r"inline float nextNoise \(std::uint32_t& state\) noexcept"),
    ("configureTrackGeometry",
     r"inline float configureTrackGeometry \(FirstAudioProcessor::TapeBlockState& state,\n"
     r"\s*int tracksIndex, int bleedBufferLength\) noexcept"),
]


def matching_brace(text: str, start: int) -> int:
    """The index just past the `}` closing the block opened at or after `start`.

    Comments and string literals are skipped so a brace in prose cannot be
    counted - the same reader tests/dsp/extract.py uses for its block cuts.
    """
    depth = 0
    index = text.index("{", start)

    while index < len(text):
        c = text[index]
        if c == "/" and text[index + 1:index + 2] == "/":
            newline = text.find("\n", index)
            index = len(text) if newline < 0 else newline
            continue
        if c == "/" and text[index + 1:index + 2] == "*":
            close = text.find("*/", index + 2)
            index = len(text) if close < 0 else close + 2
            continue
        if c == '"':
            index += 1
            while index < len(text) and index < len(text) and text[index] != '"':
                index += 2 if text[index] == "\\" else 1
            index += 1
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return index + 1
        index += 1

    raise SystemExit("extract.py: the helper opened at %d is never closed" % start)


def cut_helper(impl: str, signature: str, name: str) -> str:
    match = re.search(signature, impl)
    if match is None:
        raise SystemExit(
            f"extract.py: `{name}` was not found in {SIGNAL_PATH.name} by its "
            f"signature. It moved, was renamed, or no longer says what the "
            f"harness compiles - update the pattern or the harness, never both "
            f"quietly.")

    return impl[match.start():matching_brace(impl, match.start())]


def main() -> int:
    destination = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "extracted_signal_path.inc")

    if not SIGNAL_PATH.exists():
        raise SystemExit(f"extract.py: {SIGNAL_PATH} is missing; the signal-path helpers "
                         f"are read out of it.")

    impl = SIGNAL_PATH.read_text()

    parts = [
        "// GENERATED FILE - do not edit.",
        "// Built out of Source/ProcessorSignalPath.cpp by tests/signal_path/extract.py:",
        "// the shipping block-setup helpers, cut by signature and brace-matched.",
        "",
        "// The extracted text calls these three juce helpers and reads TrackBleed's",
        "// tables, so the shim and the stage header come first.",
        "#include \"shim.h\"",
        "#include \"DSP/TrackBleed.h\"",
        "",
        "namespace firstaudio_signal_path_harness {",
        "",
    ]

    for name, signature in HELPERS:
        body = cut_helper(impl, signature, name)
        parts.append(f"// ---- {SIGNAL_PATH.name}: {name} ----")
        parts.append(body)
        parts.append("")

    parts.append("} // namespace firstaudio_signal_path_harness")

    destination.write_text("\n".join(parts))
    print(f"extract.py: wrote {destination} with {len(HELPERS)} helper(s) from "
          f"{SIGNAL_PATH.name}: " + ", ".join(name for name, _ in HELPERS))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
