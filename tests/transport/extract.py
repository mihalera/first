#!/usr/bin/env python3
"""Cuts the shipping transport out of Source/ into the harness's include.

The harness checks the transport's GESTURE - what the three keys do, how long
each of them takes, and what the platter speed is while they do it - and none of
that can be checked against a copy of the code: a copy would agree with itself.
Both structs have moved into Source/DSP/, so this script INCLUDES them - the same
headers the plugin compiles, exactly as tests/dsp/extract.py includes its stages -
and the harness drives the real objects.

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

# The processor's own source is two files since the signal path moved out, and every
# fact checked below is in one of the two: the MIX gate, the repitch call and the arm
# are inside processTapeEngine (PluginProcessor.cpp until that move, now
# Source/ProcessorSignalPath.cpp), while the stage's prepare() and the members'
# declarations stayed with the processor.
#
# So they are read AS ONE TEXT, each file under a marker naming it. That is not
# tidiness: each of these checks failed the moment the move landed, naming its own
# subject, which is what made the re-pointing obvious rather than silent. A harness
# that goes on passing because the text it reads moved away is the failure mode this
# arrangement rules out - and a fact that ends up in BOTH files stops the harness
# instead of matching whichever one it read first.
IMPL_SOURCES = [IMPL, REPO / "Source" / "ProcessorSignalPath.cpp"]
IMPL_NAMES = " + ".join(path.name for path in IMPL_SOURCES)

# The gate's exponent is written by makeTapeCouplings() in this header since the
# tape split, so the value is read there rather than from a literal in the engine.
COUPLINGS = REPO / "Source" / "DSP" / "TapeCouplings.h"


def couplings_source() -> str:
    """The couplings header, where makeTapeCouplings() gives the gate its exponent."""
    if not COUPLINGS.exists():
        raise SystemExit(f"extract.py: {COUPLINGS.relative_to(REPO)} is missing; the gate's "
                         f"exponent is read where makeTapeCouplings() writes it.")
    return COUPLINGS.read_text()


def processor_impl() -> str:
    """The processor's source as one text, each file under a marker naming it."""
    chunks = []

    for path in IMPL_SOURCES:
        if not path.exists():
            raise SystemExit(f"extract.py: {path.relative_to(REPO)} is missing; the engine-side "
                             f"half of the transport promise is read out of it.")
        chunks.append(f"// ---- {path.relative_to(REPO).as_posix()} ----\n" + path.read_text())

    return "\n".join(chunks)
# The panel is read for ONE thing - that it asks whether START is armed - because
# the arm is invisible on the machine and a flag nobody reads is not a feature.
#
# The panel's own source is five files since the constructor's build, the theme, the
# paint and the tick moved out beside it, and the readout that asks is in the tick
# (Source/GUI/EditorTimer.cpp). This check failed the moment that move landed - the
# message named PluginEditor.cpp and said what it could not find there, which is the
# failure mode worth keeping. So the five are read AS ONE TEXT, each under a marker
# naming it, the same way the processor's two are above: a name that turns up in two
# of them stops the harness rather than matching whichever it read first.
EDITOR_SOURCES = [
    REPO / "Source" / "PluginEditor.cpp",
    REPO / "Source" / "GUI" / "EditorSetup.cpp",
    REPO / "Source" / "GUI" / "EditorTheme.cpp",
    REPO / "Source" / "GUI" / "EditorPaint.cpp",
    REPO / "Source" / "GUI" / "EditorTimer.cpp",
]
EDITOR_NAMES = " + ".join(path.name for path in EDITOR_SOURCES)


def editor_source() -> str:
    """The editor's own source as one text, each file under a marker naming it."""
    chunks = []

    for path in EDITOR_SOURCES:
        if not path.exists():
            raise SystemExit(f"extract.py: {path.relative_to(REPO)} is missing; the panel's "
                             f"half of the transport promise is read out of it.")
        chunks.append(f"// ---- {path.relative_to(REPO).as_posix()} ----\n" + path.read_text())

    return "\n".join(chunks)

# The stages that have moved into Source/DSP/, in the order they have to be included
# in. Both are driven by the harness below: the rig says where the transport goes, the
# repitch stage says what the machine sounds like while it goes there, and neither can
# be checked against a copy of the code.
#
# The list is also read by tests/dsp/check_headers_juce.sh, which compiles every header
# named here against real JUCE - so a stage added to it is checked there on the next
# run, with no second place to remember.
SOURCE_HEADERS = [
    "DSP/TransportRig.h",
    "DSP/RepitchStage.h",
]

# Where the rig's gestures live now. The report at the end of main() reads the seconds
# and the scale tables out of the shipping header, so a gesture that is documented for
# one second and implemented as seven still shows up in the log.
RIG_HEADER = REPO / "Source" / "DSP" / "TransportRig.h"

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
# The exponent is one of the values processTapeEngine works out once per block, so it is
# read through the state the sample loop is handed since the TapeBlockState split
# (`state.platterGateExponent`). The qualification is optional in the pattern: what this
# reads back is the SHAPE, and which object the number hangs off is not the shape.
GATE_DEFINITION = re.compile(r"const float (\w+) = std::pow \(platterSpeed, (?:state\.)?(\w+)\)")


def check_engine_gate() -> list[str]:
    """Returns the reasons the MIX crossfade does not gate what it should.

    Two things are read back, because both of them are the fix and neither is
    visible in a build: the gate has to be a CURVE over the platter (the straight
    multiplication this replaced is a fader, which is what the report heard), and
    both legs of the crossfade have to carry it.
    """
    impl = processor_impl()
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
                            f"{IMPL_NAMES}, so the harness cannot read it")
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

        # The exponent is a named value, and it is read where it is GIVEN its value. It
        # was a `constexpr` in the engine before the split, then a field of the block's
        # state (`state.platterGateExponent = 0.62f;`), and since the tape couplings moved
        # out it is a field makeTapeCouplings() fills in TapeCouplings.h
        # (`couplings.platterGateExponent = 2.5f;`). Either way it is one named number
        # with one value, which is what makes the shape auditable - a bare
        # `std::pow (platterSpeed, 0.62f)` would not be.
        exponent = re.search(rf"(?:constexpr float {exponent_name}"
                             rf"|state\.{exponent_name}"
                             rf"|couplings\.{exponent_name})"
                             rf" = ([0-9.]+)f;", impl + "\n" + couplings_source())

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
    impl = processor_impl()
    header = HEADER.read_text()
    problems: list[str] = []

    calls = REPITCH_CALL.findall(impl)

    if len(calls) != 1:
        problems.append(f"`repitchStage.processFrame` appears {len(calls)} times in {IMPL_NAMES}: "
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
        problems.append(f"{IMPL_NAMES} never prepares the repitch stage, so its line would be "
                        f"sized for no rate at all")

    return problems


def check_engine_arm() -> list[str]:
    """Returns the reasons the engine does not arm START against the host.

    The arm has two halves and they live in different files now: the rig holds the
    flag and the rule that sets it (read out of Source/DSP/TransportRig.h, which is
    the shipping header rather than a copy of it), and the engine is what reads the
    host's play state and hands that half of the decision to the rig. Reading the
    flag out of PluginProcessor.h after the rig moved would have found nothing and
    said so - which is how this was noticed.
    """
    impl = processor_impl()
    rig = RIG_HEADER.read_text()
    editor = editor_source()
    problems: list[str] = []

    # 1. The host's play state has to be READ, and it has to be read as an optional
    #    with a ROLLING default. A host with no playhead, or a playhead that will
    #    not say, must leave the key working: the arm is the only thing this bit
    #    drives, and "unknown means parked" would leave START dead on every host
    #    that cannot answer.
    if "getIsPlaying" not in impl:
        problems.append(f"{IMPL_NAMES} never asks the playhead whether the host is "
                        f"playing, so nothing can arm START against it")

    if not re.search(r"(?<!\bconst )\bbool hostRolling = true;", impl):
        problems.append(f"{IMPL_NAMES} reads the host's play state without a rolling "
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
        problems.append(f"`transport.beginBlock` appears {len(calls)} times in {IMPL_NAMES}: "
                        f"the engine drives the rig once per block or it does not drive it")
    elif not re.search(r"\bhostRolling\b", calls[0]):
        problems.append("the engine does not hand the rig the host's play state, so the "
                        f"arm can never fire: {calls[0].strip()}")

    # 3. The rig has to have the arm, and it has to be driven by the KEY rather
    #    than by the machine: armed means the parameter says START, the host is
    #    parked, and no finger is on SPINDOWN.
    if "bool armedForHost = false;" not in rig:
        problems.append(f"{RIG_HEADER.name} does not declare `armedForHost`, so the panel "
                        f"cannot tell a waiting key from a stopped machine")

    if not re.search(r"armedForHost = requested == static_cast<int> \(State::start\)", rig):
        problems.append(f"{RIG_HEADER.name} does not arm the key off the transport "
                        f"parameter's own START, so the arm and the lit key can disagree")

    # 4. And the panel has to be able to say so. A flag nothing reads is the same
    #    fault as no flag at all, and it is one the build cannot see either.
    if "isTransportArmedForHost" not in editor:
        problems.append(f"{EDITOR_NAMES} never asks whether the key is armed, so the deck "
                        f"readout calls a waiting machine a stopped one")

    return problems


def main() -> int:
    destination = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "extracted_transport.inc")

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

    # The durations the harness reasons about are named constants in the rig, so
    # they are read back out of the SHIPPING header and reported: a gesture that is
    # documented for one second and implemented as seven is the failure mode this
    # exists for, and seeing the numbers in the log is how a reviewer notices. The
    # scale tables are reported with them, because they are what turns those seconds
    # into the gesture a given machine performs.
    rig = RIG_HEADER.read_text()
    seconds = re.findall(r"static constexpr float (\w+Seconds)\s*=\s*([0-9.]+)f;", rig)
    scales = re.findall(r"static constexpr float (\w+Scales)\[3\]\s*\{([^}]*)\};", rig)
    if len(seconds) < 4:
        raise SystemExit(f"extract.py: only {len(seconds)} `...Seconds` constants found in "
                         f"{RIG_HEADER.name}; its gestures are stated in seconds.")

    parts = ["// GENERATED FILE - do not edit. Includes the shipping transport and repitch\n"
             "// stage out of Source/DSP/, by tests/transport/extract.py, as the engine\n"
             "// compiles them. The stand-in for juce below has to come first: the headers\n"
             "// reach for those three helpers from whoever includes them.\n"]

    parts.append(PREAMBLE)

    for header_name in SOURCE_HEADERS:
        parts.append(f'#include "{header_name}"\n')

    destination.write_text("\n".join(parts))

    print(f"wrote {destination} ({len(SOURCE_HEADERS)} header(s) from Source/DSP/)")
    print("gestures: " + ", ".join(f"{name} = {value}s" for name, value in seconds))
    print("scales:   " + ", ".join(f"{name} = [{values.strip()}]" for name, values in scales))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
