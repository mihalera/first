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

# The MIX crossfade is a two-part fact: the formula in the .cpp and an inversion
# flag on the smoother in the .h. Each was correct on its own once, and the pair
# was wrong together - the flag was a patch over a formula that put sin() and cos()
# on the wrong sides, and correcting the formula without removing the patch left
# the two ends of the control swapped. A replica that hard-codes the intended
# behaviour cannot see that class of bug at all, so the flag is read out of the
# real declaration and handed to the harness.
MIX_SMOOTHER = re.compile(
    r"SampleSmoother\s+mixSmoothed\s*\{([^}]*)\}")

HEADER_PIECES = [
    ("GlueCompressor", "struct GlueCompressor"),
    ("LoudnessMeter", "struct LoudnessMeter"),
    ("SubharmonicGenerator", "struct SubharmonicGenerator"),
]

IMPL_PIECES = [
    # j37Tanh must come before magneticHysteresis: the shaper now calls it, so the
    # harness needs the definition in the same translation unit. Under the harness
    # J37_HAS_XSIMD is 0, so this extracts the exact std::tanh form the shipping
    # build uses by default - the harness never measures the SIMD approximation,
    # which is opt-in and deliberately not the default.
    ("j37Tanh", "inline float j37Tanh"),
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


def mix_inverts(header: str) -> bool:
    """True when the real `mixSmoothed` member is declared with invertOutput set."""
    match = MIX_SMOOTHER.search(header)
    if match is None:
        raise SystemExit(
            "extract.py: no `SampleSmoother mixSmoothed {...}` declaration found in "
            "PluginProcessor.h. The MIX crossfade cannot be checked without it.")

    arguments = [argument.strip() for argument in match.group(1).split(",")]

    # SampleSmoother (clock, startsSample = false, invertOutput = false, initial = 0)
    if len(arguments) < 2:
        return False

    return arguments[1].lower() == "true"


def main() -> int:
    destination = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "extracted_dsp.inc")
    header = HEADER.read_text()
    impl = IMPL.read_text()

    chunks = ["// GENERATED FILE - do not edit.",
              "// Cut verbatim out of Source/ by tests/dsp/extract.py.",
              "",
              f"#define SUBFUND_MIX_INVERTS {1 if mix_inverts(header) else 0}",
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
