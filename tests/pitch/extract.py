#!/usr/bin/env python3
"""Extracts the shipping pitch stage into an include the harness compiles.

The pitch harness measures two structs that only ever run together in the plugin:
PitchTracker, which says what note is playing, and RealtimePitchShifter, which
moves the audio onto it. Neither is retyped here - both are cut verbatim out of
Source/PluginProcessor.h at build time, so a change to the plugin cannot leave
the harness measuring a stale copy of it. A rename or a move makes this script
fail loudly rather than silently measuring nothing.

Usage:  python3 tests/pitch/extract.py <output.inc>
"""

from __future__ import annotations

import pathlib
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
HEADER = REPO / "Source" / "PluginProcessor.h"

# Both structs are self-contained: the tracker reaches for std:: and the juce::
# scalar helpers the shim provides, and so does the shifter. Neither touches a
# member of the processor, which is what makes them measurable in isolation.
PIECES = [
    ("PitchTracker", "struct PitchTracker"),
    ("RealtimePitchShifter", "struct RealtimePitchShifter"),
]


def extract_struct(text: str, declaration: str) -> str:
    """Cuts `declaration` out of `text` through its matching closing brace."""
    start = text.index(declaration)
    end = text.index("\n};", start) + len("\n};")
    return text[start:end]


def main() -> int:
    destination = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "extracted_pitch.inc")
    header = HEADER.read_text()

    chunks = ["// GENERATED FILE - do not edit.",
              "// Cut verbatim out of Source/ by tests/pitch/extract.py.",
              ""]

    for name, declaration in PIECES:
        chunks.append(f"// ---- {name} (PluginProcessor.h) " + "-" * 40)
        chunks.append(extract_struct(header, declaration))
        chunks.append("")

    destination.write_text("\n".join(chunks))
    print(f"wrote {destination} ({destination.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
