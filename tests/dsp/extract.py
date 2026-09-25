#!/usr/bin/env python3
"""Extracts the shipping DSP under test into an include the harness compiles.

The point of the harness is that it never retypes the plugin's code: the
SubharmonicGenerator struct, the GlueCompressor struct and the inline helpers are
cut verbatim out of Source/ at build time, so a change to the plugin cannot leave
the test measuring a stale copy of it.

Usage:  python3 tests/dsp/extract.py <output.inc>
"""

from __future__ import annotations

import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
HEADER = REPO / "Source" / "PluginProcessor.h"
IMPL = REPO / "Source" / "PluginProcessor.cpp"

HEADER_PIECES = [
    ("GlueCompressor", "struct GlueCompressor"),
    ("SubharmonicGenerator", "struct SubharmonicGenerator"),
]

IMPL_PIECES = [
    ("magneticHysteresis", "inline float magneticHysteresis"),
    ("softClip", "inline float softClip"),
    ("softKneeReductionDb", "inline float softKneeReductionDb"),
    ("onePoleCoefficient", "inline float onePoleCoefficient"),
    ("onePoleCoefficientHz", "inline float onePoleCoefficientHz"),
]


def extract_struct(text: str, declaration: str) -> str:
    """Cuts `declaration` out of `text` through its matching closing brace."""
    start = text.index(declaration)
    # A struct ends at the first "\n};" at column 0 after the declaration.
    end = text.index("\n};", start) + len("\n};")
    return text[start:end]


def extract_function(text: str, signature: str) -> str:
    """Cuts a top-level inline function out of `text` through its closing brace."""
    start = text.index(signature)
    end = text.index("\n    }\n", start) + len("\n    }\n")
    return text[start:end]


def main() -> int:
    destination = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "extracted_dsp.inc")
    header = HEADER.read_text()
    impl = IMPL.read_text()

    chunks = ["// GENERATED FILE - do not edit.",
              "// Cut verbatim out of Source/ by tests/dsp/extract.py.",
              ""]

    for name, declaration in HEADER_PIECES:
        chunks.append(f"// ---- {name} (PluginProcessor.h) " + "-" * 40)
        chunks.append(extract_struct(header, declaration))
        chunks.append("")

    for name, signature in IMPL_PIECES:
        chunks.append(f"// ---- {name} (PluginProcessor.cpp) " + "-" * 38)
        chunks.append(extract_function(impl, signature))
        chunks.append("")

    destination.write_text("\n".join(chunks))
    print(f"wrote {destination} ({destination.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
