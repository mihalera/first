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
IMPL = REPO / "Source" / "PluginProcessor.cpp"

DECLARATION = "struct TransportRig"

# The engine-side half of the transport's promise, checked as text because it lives
# in a 3,400-line function that cannot be compiled without JUCE. The MIX crossfade
# has two legs - the machine's and the input's - and the platter has to gate BOTH of
# them, which is what turns STOP from "the machine went quiet and the input is still
# there" (the report) into silence. The gate's SHAPE is checked too: the platter put
# through a curve above linear is a machine holding its level and then going, where a
# straight multiplication is a fader fading. Both still compile and both still sound
# like a transport, just a quieter, fader-shaped one - which is what this exists for.
GATE_LEGS = [
    ("wet", r"const float wetMix = ([^;]*);"),
    ("dry", r"const float dryMix = ([^;]*);"),
]
GATE_DEFINITION = re.compile(r"const float (\w+) = std::pow \(platterSpeed, (\w+)\)")


def check_engine_gate() -> list[str]:
    """Returns the reasons the MIX crossfade does not gate what it should.

    Two things are read back, because both of them are the fix and neither is
    visible in a build: the gate has to be a CURVE over the platter (the straight
    multiplication this replaced is a fader, which is what the report heard), and
    both legs of the crossfade have to carry it.
    """
    impl = IMPL.read_text()
    problems: list[str] = []

    # Every platter-speed curve in the engine, by the name it is bound to. The
    # transport has two of them and they are not the same shape: the modulation term
    # and the output gate. Whichever one a leg of the crossfade carries is the one
    # whose exponent has to be read back.
    curves = dict(GATE_DEFINITION.findall(impl))
    exponents: dict[str, float] = {}

    for leg, pattern in GATE_LEGS:
        matches = re.findall(pattern, impl)

        if len(matches) != 1:
            problems.append(f"the {leg} leg of the MIX crossfade matches {len(matches)} places in "
                            f"{IMPL.name}, so the harness cannot read it")
            continue

        carried = [name for name in curves if re.search(rf"\b{name}\b", matches[0])]

        if not carried:
            if re.search(r"\b\w*platter\w*\b", matches[0], re.IGNORECASE):
                problems.append(f"the {leg} leg of the MIX crossfade rides the platter's own"
                                f" speed instead of the gate curve: {' '.join(matches[0].split())}")
            else:
                problems.append(f"the {leg} leg of the MIX crossfade is not gated by the platter:"
                                f" {' '.join(matches[0].split())}")
            continue

        if len(carried) > 1:
            problems.append(f"the {leg} leg of the MIX crossfade carries more than one platter"
                            f" curve ({', '.join(sorted(carried))}), so what gates it is ambiguous")
            continue

        exponent_name = curves[carried[0]]
        if exponent_name in exponents:
            continue

        exponent = re.search(rf"constexpr float {exponent_name} = ([0-9.]+)f;", impl)

        if exponent is None:
            problems.append(f"`{exponent_name}` shapes the output gate but is not a named"
                            f" constant, so the shape it gives is not readable")
            continue

        exponents[exponent_name] = float(exponent.group(1))

        if exponents[exponent_name] <= 1.0:
            problems.append(f"`{exponent_name}` is {exponent.group(1)}, and the gate it shapes"
                            f" holds a machine's level through a coast only above 1. At or below"
                            f" it the gate is a fader fading - the sound the transport was"
                            f" reported as making.")

    return problems


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

    problems = check_engine_gate()
    if problems:
        for problem in problems:
            print(f"extract.py: {problem}", file=sys.stderr)
        return 1

    print("gate: both MIX legs carry a platter curve above linear, so STOP is the machine"
          " stopping rather than a fade")

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
