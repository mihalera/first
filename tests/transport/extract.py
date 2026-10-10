#!/usr/bin/env python3
"""Cuts the shipping transport out of Source/ into the harness's include.

The harness checks the transport's GESTURE - what the three keys do, how long
each of them takes, and what the platter speed is while they do it - and none of
that can be checked against a copy of the code: a copy would agree with itself.
So `struct TransportRig` is cut out of Source/PluginProcessor.h verbatim, exactly
as tests/dsp/extract.py cuts the DSP stages, and the harness drives the real
object.

The struct is the whole transport: the three states, the durations, the re-cue,
the momentary hold and the auto-advance out of START. The engine reads
`platter()` from it and follows that number, so a rig that arrives where it says
it arrives is the machine arriving there too.

Usage:  python3 tests/transport/extract.py <output.inc>
"""

from __future__ import annotations

import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
HEADER = REPO / "Source" / "PluginProcessor.h"

DECLARATION = "struct TransportRig"


def brace_end(text: str, start: int) -> int:
    """The index just past the brace that closes the block starting at or after
    `start`, skipping comments and string/char literals in one pass."""
    depth = 0
    seen = False
    i = start
    n = len(text)

    while i < n:
        c = text[i]

        if c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            i = n if j < 0 else j + 1
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
        elif c in "\"'":
            quote = c
            i += 1
            while i < n and text[i] != quote:
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

    raise SystemExit("extract.py: the transport struct was opened but never closed")


def main() -> int:
    destination = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "extracted_transport.inc")
    header = HEADER.read_text()

    if header.count(DECLARATION) != 1:
        raise SystemExit(f"extract.py: `{DECLARATION}` appears {header.count(DECLARATION)} times in "
                         f"{HEADER.name}; the transport harness cannot tell them apart.")

    start = header.index(DECLARATION)
    end = brace_end(header, start)
    while end < len(header) and header[end] != "\n":
        end += 1                       # keep the trailing newline, not the next line
    body = header[start:end]
    body = body.rstrip()
    if not body.endswith("};"):
        body += ";"

    # The durations the harness reasons about are named constants in the struct,
    # so they are read back here and reported: a gesture that is documented for
    # one second and implemented as seven is the failure mode this exists for,
    # and seeing the numbers in the log is how a reviewer notices.
    seconds = re.findall(r"static constexpr float (\w+Seconds)\s*=\s*([0-9.]+)f;", body)
    if len(seconds) < 4:
        raise SystemExit(f"extract.py: only {len(seconds)} `...Seconds` constants found in "
                         f"TransportRig; its gestures are stated in seconds.")

    destination.write_text(
        "// GENERATED FILE - do not edit. Cut verbatim out of Source/PluginProcessor.h by\n"
        "// tests/transport/extract.py: the shipping transport, as the engine compiles it.\n"
        + body + "\n")

    print(f"wrote {destination} ({len(body.splitlines())} lines)")
    print("gestures: " + ", ".join(f"{name} = {value}s" for name, value in seconds))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
