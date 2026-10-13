#!/usr/bin/env python3
"""Extracts the shipping DSP under test into an include the harness compiles.

The point of the harness is that it never retypes the plugin's code: a stage is
cut verbatim out of Source/ at build time (SubharmonicGenerator, NeuralStage) or
included from the header the plugin itself compiles (SOURCE_HEADERS), so a change
to the plugin cannot leave the test measuring a stale copy of it.

This file is on its way to doing almost nothing. A stage that lives in its own
header under Source/DSP/ is INCLUDED by the generated file - the same header the
plugin compiles - so it needs no string-span extraction at all, and the fragile
half of this script (find a declaration by text, read to the next `\n};`, and get
the order right) disappears with it. SOURCE_HEADERS below lists what has moved;
the pieces still cut further down are the ones that have not. When the header is
empty enough to read, the last thing to go is this script.

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

# The stages that have already moved into Source/DSP/, in the order they have to
# be included in - the header's own order, which is the only order that works for
# a stage that holds another one by value.
#
# J37Math.h carries the bridge the DSP blocks call. Under the harness
# J37_HAS_CHOWDSP_MATH is 0 (undefined), so what compiles is the std:: fallback:
# the harness always measures the exact reference math, never the chowdsp
# approximation, which is what lets the benchmark compare the two.
#
# SaturationCore.h carries the struct AND the four type lists it voices
# (`valveTypeNames` and its three siblings), which is what retired the constant
# cutting that used to sit below: the counts those lists are built from now
# arrive with the struct instead of being pasted in ahead of it.
SOURCE_HEADERS = [
    "DSP/J37Math.h",
    "DSP/SaturationCore.h",
    "DSP/VinylStage.h",
    "DSP/LoudnessMeter.h",
    "DSP/TransientShaper.h",
    "DSP/GlueCompressor.h",
    "DSP/RateBridge.h",
]

# There is no CONSTANT_PIECES any more. It existed for the four type counts
# SaturationCore's set<Family>Voice methods read (`valveTypeCount` and its three
# siblings), which live at namespace scope: cutting the struct alone delivered a
# method that read a constant which had not arrived - exactly how adc23c9's new
# DIGITAL voice broke the harness. The counts, their name arrays and their
# completeness checks moved into Source/DSP/SaturationCore.h with the struct, so
# they now come in by include. A constant that goes back to being cut alone is a
# compile failure here rather than a silent zero.
HEADER_PIECES = [
    ("SubharmonicGenerator", "struct SubharmonicGenerator"),
    # The vinyl stage used to be cut here. It lives in Source/DSP/VinylStage.h now,
    # so the include above puts it in this file the same way it puts the saturation
    # core, the transient shaper and the glue compressor in - and it is worth
    # saying that nothing in this harness names it any more: the entry was kept
    # because the noise tab's controls act on the stage, and whatever check that
    # was now drives the chain rather than the struct. The include costs nothing
    # and means a check that wants it again finds the shipping text.
    # The transient shaper used to be cut here as well, and it moved for the same
    # reason: it now comes from Source/DSP/TransientShaper.h. Its two claims are
    # still measured - that at both amounts zero it is a bit-for-bit pass-through,
    # and that a positive attack amount makes an edge sharper while a negative one
    # rounds it - but measured on the shipping header rather than on a cut copy of
    # it, which is the whole point of SOURCE_HEADERS.
    # The neural stage is extracted, which is what proves the guard is honest:
    # with J37_HAS_RTNEURAL 0 (the harness case) every member reduces to the
    # pass-through, so the struct still compiles and still renders the input
    # unchanged. A source that only compiled WITH the library would fail here.
    # The rate bridge used to be cut here, before NeuralStage, because the stage
    # HOLDS one: it now comes from Source/DSP/RateBridge.h, which SOURCE_HEADERS
    # includes above and ahead of everything in this list, so the member's type is
    # complete by the time the stage below is compiled. The order rule has not
    # gone away - it has moved into that list.
    ("NeuralStage", "struct NeuralStage"),
]

IMPL_PIECES = [
    # j37Tanh is still extracted because SaturationCore's curves may call it, and
    # under the harness J37_HAS_XSIMD is 0 - so this is the exact std::tanh form
    # the shipping build uses by default. The harness never measures the SIMD
    # approximation, which is opt-in and deliberately not the default.
    #
    # magneticHysteresis is deliberately NOT extracted any more. The engine moved
    # to SaturationCore and stopped calling it, and continuing to extract it would
    # let the harness keep rendering the old curve while the plugin rendered the
    # new one - which is precisely the drift this file exists to prevent. Leaving
    # it out means the harness fails to compile if anything still reaches for it.
    ("j37Tanh", "inline float j37Tanh"),
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


# The wiring the neural rate conversion depends on, checked as TEXT because it
# lives in prepareToPlay, processBlock and the two model loaders - thousands of
# lines that cannot be compiled without JUCE. RateBridge itself is driven on signal
# by tests/neural/rate_test.cpp; what is checked here is that anything ever REACHES
# it, which is the one failure a harness on the struct alone cannot see: a
# conversion that is correct, tested and never told what the model's rate is is a
# model running at the engine's rate, which is exactly the defect it exists for.
RATE_DECLARATION = "void updateNeuralSampleRates();"
RATE_DEFINITION = "void FirstAudioProcessor::updateNeuralSampleRates()"


def check_neural_rate_wiring(header: str, impl: str) -> list[str]:
    """Returns the reasons a model's own sample rate never reaches the bridge."""
    problems: list[str] = []

    if RATE_DECLARATION not in header:
        problems.append(f"{HEADER.name} does not declare `{RATE_DECLARATION}`, so nothing "
                        f"hands the stages the two rates")

    if RATE_DEFINITION not in impl:
        problems.append(f"{IMPL.name} does not define `updateNeuralSampleRates()`, so the "
                        f"declaration above it leads nowhere")
        return problems

    # The body, so the checks below are about THIS function rather than about the
    # file. It is cut at the next top-level closing brace, which is where the next
    # definition starts.
    body = impl[impl.index(RATE_DEFINITION):]
    body = body[:body.index("\n}\n")]

    # 1. The engine's rate is the session's TIMES THE OVERSAMPLING FACTOR. That
    #    multiplication is the whole reason a conversion is needed at all, and a
    #    version that read the session rate alone would look right and be wrong in
    #    every oversampled session.
    if "sampleRate * oversamplingRateFactor" not in body:
        problems.append("the engine's rate is not the session's times the oversampling factor, "
                        "so a model would be converted to a rate the engine is not running at")

    # 2. Both channels, or the two sides of the image are different networks - and
    #    each of them handed the MODEL's rate, read back from what the file set,
    #    rather than a literal or the engine's rate twice. A stage told 0 is a stage
    #    that never converts, which is the defect this whole check is about.
    model_rate = re.search(r"const auto (\w+) = neuralModelRate\.load \(", body)

    if model_rate is None:
        problems.append("the model's rate is never read back, so whatever the file said is "
                        "not what the stages are told")

    for channel in ("neuralL", "neuralR"):
        match = re.search(rf"{channel}\.setSampleRates \(([^)]*)\);", impl)

        if match is None:
            problems.append(f"`{channel}.setSampleRates` is never called in {IMPL.name}")
        elif model_rate is not None:
            arguments = [argument.strip() for argument in match.group(1).split(",")]

            if model_rate.group(1) not in arguments:
                problems.append(f"`{channel}.setSampleRates` is handed `{match.group(1).strip()}` "
                                f"and not the model's own rate (`{model_rate.group(1)}`), so the "
                                f"conversion has no model rate to work from")

    # 3. The rate has to come from the FILE, on the one path that has one. The
    #    format's own key, read where the file is parsed.
    if impl.count('parsed.find ("sample_rate")') != 1:
        problems.append(f"{IMPL.name} does not read `sample_rate` out of the .nam file, so a "
                        f"model's own rate is never known and the bridge stays idle")

    # 4. And the format that carries no rate must not inherit the last one that
    #    did: an RTNeural JSON model has no rate, and running it at a .nam file's
    #    rate would be a conversion invented out of a model it does not describe.
    if impl.count("neuralModelRate.store (modelRate,") != 1:
        problems.append(f"the .nam path does not record the model's rate, so nothing can be "
                        f"handed to the bridge")

    if impl.count("neuralModelRate.store (0.0,") != 1:
        problems.append(f"the model format that carries no rate does not clear the rate, so it "
                        f"would inherit the previous file's")

    # 5. The pair has to be pushed again whenever either end of it moves - which is
    #    the model loader AND the oversampling switch - or a session that changes
    #    its oversampling keeps converting to the rate it used to run at.
    pushes = impl.count("updateNeuralSampleRates();")
    if pushes < 3:
        problems.append(f"`updateNeuralSampleRates` is called {pushes} times in {IMPL.name}: "
                        f"a model load and a change of oversampling factor both move a rate "
                        f"that the conversion follows")

    # 6. And the stage has to actually keep the pair it is given. The stage latches
    #    it on the audio thread (the bridge is audio-thread state, and a model is
    #    loaded on the message thread), so what it keeps are the three atomics.
    for kept, name in (("wantedModelRate.store (", "the model's rate"),
                       ("wantedEngineRate.store (", "the engine's rate"),
                       ("ratesPending.store (true", "a pending-rate flag")):
        if kept not in header:
            problems.append(f"the stage keeps no {name}, so what it is handed never reaches "
                            f"the bridge")

    return problems


# The AUTO GAIN compensator. It is the one gain stage in the chain that nothing
# else measures - the loudness meter does not read it, and it does not read the
# loudness meter: it is block power in, up to 12 dB of programme-level correction
# out, applied to the signal that leaves the plugin - and it lives inline in
# processBlock's per-sample loop, in the thousands of lines that cannot be
# compiled without JUCE. So its invariants are checked as text, exactly like the
# neural rate wiring above: each of them is a sentence in the code's own comment,
# and each of them is a level error on every project if an edit drops it.
COMPENSATION_GATE = "if (autoGainEnabled && transport.atSpeed() && referenceBlockPower > 0.0f)"
COMPENSATION_CLAMP = "juce::jlimit (0.0f, 12.0f, levelRatioDb)"
COMPENSATION_SMOOTHING = ("smoothedCompensationDb += (targetCompensationDb - smoothedCompensationDb)")
COMPENSATION_TIME_MS = "onePoleCoefficient (450.0f, engineSampleRate)"


def compensation_block(impl: str) -> str | None:
    """The `if` that folds the correction in, cut through its closing brace."""
    if COMPENSATION_GATE not in impl:
        return None

    start = impl.index(COMPENSATION_GATE)
    end = impl.index("\n        }\n", start) + len("\n        }\n")
    return impl[start:end]


def check_auto_gain_compensation(impl: str) -> list[str]:
    """Returns the reasons the AUTO GAIN correction could be wrong on signal."""
    problems: list[str] = []

    # 1. It must not try to correct a machine that is not running. With STOP
    #    gating the output the measurement collapses, the compensator ramps to
    #    its +12 dB limit while the platter sits still, and START then comes back
    #    loud before the correction relaxes.
    if COMPENSATION_GATE not in impl:
        problems.append("the compensation is not gated on AUTO GAIN being on, the platter being at "
                        "speed and a live reference level, so a stopped machine is corrected as if "
                        "it were silent output")
        return problems

    block = compensation_block(impl)
    assert block is not None

    # 2. The measurement is the signal BEFORE the correction is folded in.
    #    Measuring the compensated output makes the correction chase its own tail.
    measured = re.search(r"const auto signal = ([^;]*);", block, re.S)

    if measured is None or "tapeOutput[" not in measured.group(1) \
            or "stageGainPerChannel[" not in measured.group(1):
        problems.append("the compensated level is not measured on the signal as it leaves the "
                        "compressors and tape stage, so the amount being corrected is not the "
                        "amount the chain lost")
    elif "compensationGain" in measured.group(1):
        problems.append("the compensated level is measured AFTER the compensation is folded in, so "
                        "the correction chases its own tail")

    # 3. Restore only, never exaggerate. The correction may pay back a loss and
    #    must not become a boost stage of its own.
    if COMPENSATION_CLAMP not in block:
        problems.append("the correction is not clamped to 0 .. 12 dB, so AUTO GAIN can act as a "
                        "boost stage of its own rather than restoring what the chain lost")

    # 4. Programme level, not sample level: a correction that moved at the rate of
    #    the signal would pump it. The smoother is 450 ms, and it is a one-pole
    #    towards the target rather than a step.
    if COMPENSATION_SMOOTHING not in block or "* compensationCoefficient;" not in block:
        problems.append("the correction is not smoothed towards its target, so it steps the output "
                        "level rather than riding the programme")

    if COMPENSATION_TIME_MS not in impl:
        problems.append("the correction's time constant is not built from the engine's rate, so the "
                        "smoothing is not the same duration at every sample rate")

    # 5. It starts from unity, so the first block of a session is not nudged by a
    #    correction left over from a previous one or a previous sample rate.
    resets = impl.count("smoothedCompensationDb = 0.0f;")

    if resets != 1:
        problems.append(f"`smoothedCompensationDb = 0.0f` appears {resets} times in {IMPL.name}: the "
                        f"correction has to start from unity exactly once per prepare")

    # 6. Both legs of the MIX crossfade carry it. DELTA subtracts the dry leg from
    #    the wet one, so a correction applied to one of them and not the other
    #    reads as character the machine did not add - the exact fault the two
    #    comments above these lines describe.
    for leg, pattern in (("outputSignal", r"outputSignal\[index\] = ([^;]*);"),
                         ("machineDry", r"machineDry\[index\] = ([^;]*);")):
        expression = re.search(pattern, impl, re.S)

        if expression is None or "compensationGain" not in expression.group(1):
            problems.append(f"`{leg}` does not carry the compensation, so the MIX 0 leg and the "
                            f"machine leg differ by the whole correction and DELTA monitors it as "
                            f"character")

    return problems


def main() -> int:
    destination = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "extracted_dsp.inc")
    header = HEADER.read_text()
    impl = IMPL.read_text()

    problems = check_neural_rate_wiring(header, impl)
    problems += check_auto_gain_compensation(impl)
    if problems:
        for problem in problems:
            print(f"extract.py: {problem}", file=sys.stderr)
        return 1

    print("neural rates: the .nam file's own rate reaches both stages, beside the engine's "
          "session rate times its oversampling factor")
    print("auto gain: the compensator measures before it corrects, only ever restores, holds its "
          "correction on a stopped machine, and rides both legs of the MIX crossfade")

    chunks = ["// GENERATED FILE - do not edit.",
              "// Cut verbatim out of Source/ by tests/dsp/extract.py.",
              "",
              f"#define SUBFUND_MIX_INVERTS {1 if mix_inverts(header) else 0}",
              ""]

    # The stages that live in their own headers are included, in header order,
    # before anything that is still cut: a cut stage may hold one of them.
    chunks.append("// ---- Source/DSP/ headers, included rather than cut " + "-" * 18)

    for header_name in SOURCE_HEADERS:
        chunks.append(f'#include "{header_name}"')

    chunks.append("")
    chunks.append("")

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
