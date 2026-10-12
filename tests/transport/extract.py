#!/usr/bin/env python3
"""Cuts the shipping transport out of Source/ into the harness's include.

The harness checks the transport's GESTURE - what the three keys do, how long
each of them takes, and what the platter speed is while they do it - and none of
that can be checked against a copy of the code: a copy would agree with itself.
So `struct TransportRig` is cut out of Source/PluginProcessor.h verbatim, exactly
as tests/dsp/extract.py cuts the DSP stages, and the harness drives the real
object.

The struct is the whole transport: the three states, the durations, the scales
the deck's own settings put on them (tape speed, turntable speed, tempo), the
momentary hold and the auto-advance out of START. The engine reads `platter()`
from it and follows that number, so a rig that arrives where it says it arrives
is the machine arriving there too.

Usage:  python3 tests/transport/extract.py <output.inc>
"""

from __future__ import annotations

import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
HEADER = REPO / "Source" / "PluginProcessor.h"
IMPL = REPO / "Source" / "PluginProcessor.cpp"
# The panel is read for ONE thing - that it asks whether START is armed - because
# the arm is invisible on the machine and a flag nobody reads is not a feature.
EDITOR = REPO / "Source" / "PluginEditor.cpp"

DECLARATIONS = [
    "struct TransportRig",
    # The repitch stage is cut out for the same reason the rig is: the harness has
    # to drive the real one. It is the stage that turns the platter's speed into
    # a PITCH - the rig says where the transport goes, this says what the machine
    # sounds like while it goes there - and neither can be checked against a copy.
    "struct RepitchStage",
]

# The slice of the toolkit the extracted stages reach for. The harness has no
# JUCE on its include path, so juce::jmin/jmax/jlimit are provided here with the
# semantics of the real ones; both stages are otherwise plain C++.
PREAMBLE = (
    "#include <algorithm>\n"
    "#include <array>\n"
    "#include <cmath>\n"
    "#include <vector>\n"
    "\n"
    "namespace juce\n"
    "{\n"
    "template <typename Type> constexpr Type jmin (Type a, Type b) noexcept { return a < b ? a : b; }\n"
    "template <typename Type> constexpr Type jmax (Type a, Type b) noexcept { return a > b ? a : b; }\n"
    "template <typename Type> constexpr Type jlimit (Type lower, Type upper, Type value) noexcept\n"
    "{ return value < lower ? lower : (value > upper ? upper : value); }\n"
    "}\n"
    "\n"
)

# The engine-side half of the repitch promise: the stage has to be a MEMBER, it
# has to be prepared at the engine's rate, and it has to be handed the frame the
# wet/dry crossfade produced together with the platter's speed. Written as text
# because that call lives in a 3,500-line function that cannot be compiled
# without JUCE - and a stage that is extracted, tested and then never called in
# the audio path is exactly the class of bug this file exists to catch.
REPITCH_CALL = re.compile(r"repitchStage\.processFrame \(([^;]*)\);")

# The engine-side half of the ARM: START waits for the host, and the host's
# answer is a bit the engine has to read off the playhead and hand to the rig.
# The rig half is driven directly below; this is the half that could be missing
# while the rig still passes - a rig that is never told whether the DAW is
# rolling would simply never arm, and an engine that arms but never publishes the
# flag would leave the panel saying "stopped" over a key that is waiting.
ARM_CALL = re.compile(r"transport\.beginBlock \(([^;]*)\);")

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


def check_engine_repitch() -> list[str]:
    """Returns the reasons the engine does not run the repitch stage."""
    impl = IMPL.read_text()
    header = HEADER.read_text()
    problems: list[str] = []

    calls = REPITCH_CALL.findall(impl)

    if len(calls) != 1:
        problems.append(f"`repitchStage.processFrame` appears {len(calls)} times in {IMPL.name}: "
                        f"the stage is advanced once per frame or it is not advanced at all")
    else:
        arguments = calls[0]

        if "tapeOutput" not in arguments:
            problems.append("the repitch stage is handed something other than the frame the "
                            f"wet/dry crossfade produced: {arguments.strip()}")

        if not re.search(r"\bplatterSpeed\b", arguments):
            problems.append("the repitch stage is not handed the platter's speed, so it has "
                            f"nothing to follow: {arguments.strip()}")

    if "RepitchStage repitchStage;" not in header:
        problems.append(f"{HEADER.name} does not declare a `RepitchStage repitchStage;` member, "
                        f"so the engine cannot be calling the stage it is calling")

    if not re.search(r"repitchStage\.prepare \(([^;]*)\);", impl):
        problems.append(f"{IMPL.name} never prepares the repitch stage, so its line would be "
                        f"sized for no rate at all")

    return problems


def check_engine_arm() -> list[str]:
    """Returns the reasons the engine does not arm START against the host."""
    impl = IMPL.read_text()
    header = HEADER.read_text()
    editor = EDITOR.read_text()
    problems: list[str] = []

    # 1. The host's play state has to be READ, and it has to be read as an optional
    #    with a ROLLING default. A host with no playhead, or a playhead that will
    #    not say, must leave the key working: the arm is the only thing this bit
    #    drives, and "unknown means parked" would leave START dead on every host
    #    that cannot answer.
    if "getIsPlaying" not in impl:
        problems.append(f"{IMPL.name} never asks the playhead whether the host is "
                        f"playing, so nothing can arm START against it")

    if not re.search(r"(?<!\bconst )\bbool hostRolling = true;", impl):
        problems.append(f"{IMPL.name} reads the host's play state without a rolling "
                        f"default: a host that will not answer would leave START dead")

    # ... and it has to keep it. JUCE reports the transport state as a flag inside
    # the playhead's own position and answers `false` for a host that never set it,
    # so a plugin that takes "not playing" at face value arms the key on every host
    # that does not report one - a standalone build among them - and the machine
    # then waits for a DAW that is never going to answer. Reading that answer as
    # PARKED requires the host to have supplied timing at all.
    if re.search(r"hostRolling\s*=\s*\*?\w*[Pp]laying", impl):
        problems.append("the engine takes the playhead's own answer as the host's transport "
                        "state, which is `false` for a host that never set it: an unanswered "
                        "playhead would leave START waiting forever")

    # 2. It has to reach the rig, in the call the rig is driven by.
    calls = ARM_CALL.findall(impl)

    if len(calls) != 1:
        problems.append(f"`transport.beginBlock` appears {len(calls)} times in {IMPL.name}: "
                        f"the engine drives the rig once per block or it does not drive it")
    elif not re.search(r"\bhostRolling\b", calls[0]):
        problems.append("the engine does not hand the rig the host's play state, so the "
                        f"arm can never fire: {calls[0].strip()}")

    # 3. The rig has to have the arm, and it has to be driven by the KEY rather
    #    than by the machine: armed means the parameter says START, the host is
    #    parked, and no finger is on SPINDOWN.
    if "bool armedForHost = false;" not in header:
        problems.append(f"{HEADER.name} does not declare `armedForHost`, so the panel "
                        f"cannot tell a waiting key from a stopped machine")

    if not re.search(r"armedForHost = requested == static_cast<int> \(State::start\)", header):
        problems.append(f"{HEADER.name} does not arm the key off the transport "
                        f"parameter's own START, so the arm and the lit key can disagree")

    # 4. And the panel has to be able to say so. A flag nothing reads is the same
    #    fault as no flag at all, and it is one the build cannot see either.
    if "isTransportArmedForHost" not in editor:
        problems.append(f"{EDITOR.name} never asks whether the key is armed, so the deck "
                        f"readout calls a waiting machine a stopped one")

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

    problems = check_engine_gate() + check_engine_repitch() + check_engine_arm()
    if problems:
        for problem in problems:
            print(f"extract.py: {problem}", file=sys.stderr)
        return 1

    print("gate: both MIX legs carry a platter curve above linear, so STOP is the machine"
          " stopping rather than a fade")
    print("repitch: the engine advances one stage per frame, on the machine's own frame,"
          " followed by the platter's speed")
    print("arm: the engine reads the host's play state off the playhead, defaults an"
          " unanswered host to rolling, and hands it to the rig")

    bodies: list[str] = []

    for declaration in DECLARATIONS:
        if header.count(declaration) != 1:
            raise SystemExit(f"extract.py: `{declaration}` appears {header.count(declaration)} times "
                             f"in {HEADER.name}; the transport harness cannot tell them apart.")

        start = header.index(declaration)
        end = brace_end(header, start)
        while end < len(header) and header[end] != "\n":
            end += 1                   # keep the trailing newline, not the next line
        body = header[start:end]
        bodies.append(body.rstrip())

    body = bodies[0]
    if not body.endswith("};"):
        body += ";"

    # The durations the harness reasons about are named constants in the struct,
    # so they are read back here and reported: a gesture that is documented for
    # one second and implemented as seven is the failure mode this exists for,
    # and seeing the numbers in the log is how a reviewer notices. The scale
    # tables are reported with them, because they are what turns those seconds
    # into the gesture a given machine performs.
    seconds = re.findall(r"static constexpr float (\w+Seconds)\s*=\s*([0-9.]+)f;", body)
    scales = re.findall(r"static constexpr float (\w+Scales)\[3\]\s*\{([^}]*)\};", body)
    if len(seconds) < 4:
        raise SystemExit(f"extract.py: only {len(seconds)} `...Seconds` constants found in "
                         f"TransportRig; its gestures are stated in seconds.")

    parts = ["// GENERATED FILE - do not edit. Cut verbatim out of Source/PluginProcessor.h by\n"
             "// tests/transport/extract.py: the shipping transport and repitch stage, as the\n"
             "// engine compiles them.\n"]

    parts.append(PREAMBLE)

    for index, extracted in enumerate(bodies):
        if index == 0 and not extracted.endswith("};"):
            extracted += ";"
        parts.append(extracted + "\n")

    destination.write_text("\n".join(parts))

    print(f"wrote {destination} ({len(body.splitlines())} + "
          f"{len(bodies[1].splitlines())} lines)")
    print("gestures: " + ", ".join(f"{name} = {value}s" for name, value in seconds))
    print("scales:   " + ", ".join(f"{name} = [{values.strip()}]" for name, values in scales))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
