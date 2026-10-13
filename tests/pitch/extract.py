#!/usr/bin/env python3
"""Extracts the shipping pitch stage into an include the harness compiles.

The pitch harness measures two structs that only ever run together in the plugin:
PitchTracker, which says what note is playing, and RealtimePitchShifter, which
moves the audio onto it. Neither is retyped here - both are cut verbatim out of
Source/ at build time or included from the header the plugin itself compiles, so a
change to the plugin cannot leave the harness measuring a stale copy of it.

Both of them have now moved into Source/DSP/, so this script does what the DSP
extractor does for its stages: it emits the includes rather than the text. That is
the whole of it - the two headers are the shipping ones, and `run.sh` puts Source/
on the include path for exactly this. The cut-by-text half is gone rather than kept
as a fallback, because a fallback is a second way to measure the wrong thing.

Usage:  python3 tests/pitch/extract.py <output.inc>
"""

from __future__ import annotations

import pathlib
import sys

# The stages that have moved into Source/DSP/, in the order they have to be included
# in - which is the header's own order, and the only order that works for a stage
# that holds another one by value (the shifter reads what the tracker found, but as
# an argument rather than as a member, so today the order is simply reading order).
# The list is also read by tests/dsp/check_headers_juce.sh, which compiles every
# header named here against real JUCE - so a stage added to it is checked there on
# the next run, with no second place to remember.
SOURCE_HEADERS = [
    "DSP/PitchTracker.h",
    "DSP/RealtimePitchShifter.h",
]


def main() -> int:
    destination = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "extracted_pitch.inc")

    chunks = ["// GENERATED FILE - do not edit.",
              "// Includes the shipping pitch stage out of Source/, by tests/pitch/extract.py.",
              ""]

    for header in SOURCE_HEADERS:
        chunks.append(f'#include "{header}"')

    destination.write_text("\n".join(chunks) + "\n")
    print(f"wrote {destination} ({destination.stat().st_size} bytes, "
          f"{len(SOURCE_HEADERS)} header(s) from Source/DSP/)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
