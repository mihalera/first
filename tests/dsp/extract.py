#!/usr/bin/env python3
"""Extracts the shipping DSP under test into an include the harness compiles.

The point of the harness is that it never retypes the plugin's code: every stage
is INCLUDED from the header the plugin itself compiles (SOURCE_HEADERS below), so
a change to the plugin cannot leave the test measuring a stale copy of it.

The half of this script that cut those stages out of PluginProcessor.h is gone,
which is what the first header to move was aiming at: a stage used to be found by
text (`struct NeuralStage`), read to the next `\n};` and pasted in, in an order
that had to mirror the plugin header's own. Every one of them is a file under
Source/DSP/ now - NeuralStage was the last, and it could not move until the macro
probe it is compiled under had a header of its own, Source/DSP/OptionalLibraries.h.

There is nothing left to CUT at all. The five inline shapers were the .cpp half of
the same recipe - `IMPL_PIECES`, cut out of PluginProcessor.cpp by signature - and
they are Source/DSP/HotPathMath.h now, included by the list below with the stages.
They followed the stages out for the same reason the stages left the header: the
engine's signal path moved to Source/ProcessorSignalPath.cpp and the shapers are
called from BOTH halves of the processor, so they could not travel with either.

What is still checked here as TEXT is the wiring no harness can compile: the two
facts that live in the engine's thousands of lines rather than in a stage (the
neural rate conversion, and the AUTO GAIN compensator that a stage is not yet),
plus the MIX smoother's own inversion flag, which is read out of the real
declaration and handed to the harness rather than restated here. Those reads are
taken from the processor's own source AS ONE TEXT, across every file it is split
into, so a fact that moves to another of its files is still found - and a fact that
ends up in two of them stops this harness rather than measuring whichever came
first.

Usage:  python3 tests/dsp/extract.py <output.inc>
"""

from __future__ import annotations

import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
HEADER = REPO / "Source" / "PluginProcessor.h"
IMPL = REPO / "Source" / "PluginProcessor.cpp"

# The processor's own source is two files since the signal path moved out, and the
# text checks below read both: the neural rate wiring is in the processor's
# preparation code, and the AUTO GAIN compensator is inside processTapeEngine, which
# is Source/ProcessorSignalPath.cpp now. Each file is read under a marker naming it,
# so a span cannot be cut across a boundary that no real file has.
IMPL_SOURCES = [IMPL, REPO / "Source" / "ProcessorSignalPath.cpp"]
# Where the neural stage keeps the two rates it is handed. They used to be read
# out of PluginProcessor.h, and they moved with the stage - which is why this is a
# path of its own and why the check that reads them NAMES THE FILE it read. A text
# check that passes because its subject moved away is the failure worth naming the
# file to prevent: the transport harness learned the same lesson the hard way when
# the rig's `armedForHost` flag left PluginProcessor.h (tests/transport/extract.py).
STAGE_HEADER = REPO / "Source" / "DSP" / "NeuralStage.h"

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
# The engine's own scalar helpers come with them, and they are not one of the
# stages: Source/DSP/HotPathMath.h is the five inline shapers (j37Tanh, the two
# one-pole coefficient builders, softClip and the soft-knee gain computer) that
# everything downstream of this list calls. They are listed where they belong in the
# include order - after the bridge they build on - rather than where the plugin's
# header happens to include them, because the plugin's order is the processor's
# business and this is a directory of independent headers.
SOURCE_HEADERS = [
    "DSP/J37Math.h",
    "DSP/HotPathMath.h",
    # The first stage of processTapeEngine() to leave that function: the machine's
    # block-rate coefficients (the cabinet's poles, the SAG pair, the limiter's four
    # and its ceiling, the anti-phase guard's pole). It names only the two builders
    # above, so it sits directly under them, and the harness checks the relationships
    # between those numbers - which are the behaviour, not decoration.
    "DSP/BlockRateCoefficients.h",
    # The second stage out of processTapeEngine(): the machine's shaping curves, the
    # SPEED/platter scale and the two wander rates, as a pure function of seven
    # numbers. It names `juce::jlimit` and `juce::MathConstants`, which the shim
    # declares for the builder above it already.
    "DSP/TapeModulation.h",
    "DSP/SaturationCore.h",
    "DSP/VinylStage.h",
    "DSP/SubharmonicGenerator.h",
    "DSP/LoudnessMeter.h",
    "DSP/TransientShaper.h",
    "DSP/GlueCompressor.h",
    "DSP/RateBridge.h",
    "DSP/NeuralStage.h",
]

# That is the last stage the plugin holds, and with it the last thing this file
# ever had to cut out of PluginProcessor.h. NeuralStage is the one that needed
# another header to move first: it is compiled in two shapes, chosen by
# J37_HAS_RTNEURAL, and a build may decide that macro itself (tests/neural does,
# with RTNeural's API stood in for) - so the probe left PluginProcessor.h for
# Source/DSP/OptionalLibraries.h, which this header includes. Under the harness
# the macro is 0 and every member reduces to the pass-through, which is the
# build subfund_test.cpp renders.

# There is no HEADER_PIECES any more, and there is no CONSTANT_PIECES either -
# both were lists of text this file cut out of PluginProcessor.h, and both are
# empty because there is nothing left in it that a stage owns.
#
# HEADER_PIECES was the list of structs found by declaration text (`struct
# NeuralStage`) and read to the next `\n};`. It had three ways to go wrong: an
# anchor that stopped matching, an order that had to mirror the plugin header's
# own, and - the one that matters - a copy that could drift from the code the
# plugin compiles while still passing. NeuralStage was its last entry, and it is
# Source/DSP/NeuralStage.h now, included by the list above. Every stage the list
# ever held is a header under Source/DSP/, so a stage that goes back to being cut
# here is a stage that stopped being readable on its own.
#
# CONSTANT_PIECES pasted the four type counts SaturationCore's set<Family>Voice
# methods read (`valveTypeCount` and its three siblings) in ahead of the struct,
# because cutting the struct alone delivered a method that read a constant which
# had not arrived - exactly how adc23c9's new DIGITAL voice broke the harness.
# The counts, their name arrays and their completeness checks moved into
# Source/DSP/SaturationCore.h with the struct, so a constant that goes back to
# being cut alone is a compile failure here rather than a silent zero.

# There is no IMPL_PIECES any more either. It cut five inline functions out of
# PluginProcessor.cpp by their signature (`[[maybe_unused]] inline float j37Tanh`)
# and pasted them ahead of the harness, and it was the last text this file cut out
# of the plugin rather than including. Two things about it were worth keeping in
# mind now that it is gone:
#
#   * j37Tanh was extracted because SaturationCore's curves may call it, and under
#     the harness J37_HAS_XSIMD is 0 - so what the harness renders is the exact
#     std::tanh form the shipping build uses by default. That is still true of the
#     header it lives in now: J37_HAS_XSIMD arrives with the probe, which cannot
#     answer 1 without xsimd on the include path, which the harness does not have.
#   * magneticHysteresis was deliberately NOT in the list: the engine moved to
#     SaturationCore and stopped calling it, and continuing to extract it would have
#     let the harness keep rendering the old curve while the plugin rendered the new
#     one. Leaving it out means anything that still reaches for it fails to compile.


def processor_impl() -> str:
    """The processor's source as one text, each file under a marker naming it."""
    chunks = []

    for path in IMPL_SOURCES:
        if not path.exists():
            raise SystemExit(f"extract.py: {path.relative_to(REPO)} is missing; the text checks "
                             f"below read the processor's own source out of it.")
        chunks.append(f"// ---- {path.relative_to(REPO).as_posix()} ----\n" + path.read_text())

    return "\n".join(chunks)


def impl_source_of(anchor: str) -> str:
    """Which of the processor's files contains `anchor`, for a message that names it."""
    found = [path.relative_to(REPO).as_posix()
             for path in IMPL_SOURCES if anchor in path.read_text()]

    if len(found) != 1:
        raise SystemExit(f"extract.py: `{anchor}` is in {len(found)} of the processor's files "
                         f"({', '.join(found)}); the message cannot name it.")

    return found[0]


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


def check_neural_rate_wiring(header: str, impl: str, stage: str) -> list[str]:
    """Returns the reasons a model's own sample rate never reaches the bridge."""
    problems: list[str] = []

    if RATE_DECLARATION not in header:
        problems.append(f"{HEADER.name} does not declare `{RATE_DECLARATION}`, so nothing "
                        f"hands the stages the two rates")

    if RATE_DEFINITION not in impl:
        problems.append(f"the processor's source does not define `updateNeuralSampleRates()`, so the "
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
            problems.append(f"`{channel}.setSampleRates` is never called in the processor's source")
        elif model_rate is not None:
            arguments = [argument.strip() for argument in match.group(1).split(",")]

            if model_rate.group(1) not in arguments:
                problems.append(f"`{channel}.setSampleRates` is handed `{match.group(1).strip()}` "
                                f"and not the model's own rate (`{model_rate.group(1)}`), so the "
                                f"conversion has no model rate to work from")

    # 3. The rate has to come from the FILE, on the one path that has one. The
    #    format's own key, read where the file is parsed.
    if impl.count('parsed.find ("sample_rate")') != 1:
        problems.append(f"the processor's source does not read `sample_rate` out of the .nam file, so a "
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
        problems.append(f"`updateNeuralSampleRates` is called {pushes} times in the processor's "
                        f"source: "
                        f"a model load and a change of oversampling factor both move a rate "
                        f"that the conversion follows")

    # 6. And the stage has to actually keep the pair it is given. The stage latches
    #    it on the audio thread (the bridge is audio-thread state, and a model is
    #    loaded on the message thread), so what it keeps are the three atomics -
    #    and they live in the stage's own header, which is read here rather than
    #    PluginProcessor.h.
    for kept, name in (("wantedModelRate.store (", "the model's rate"),
                       ("wantedEngineRate.store (", "the engine's rate"),
                       ("ratesPending.store (true", "a pending-rate flag")):
        if kept not in stage:
            problems.append(f"{STAGE_HEADER.name} keeps no {name}, so what it is handed never "
                            f"reaches the bridge")

    return problems


# The AUTO GAIN compensator. It is the one gain stage in the chain that nothing
# else measures - the loudness meter does not read it, and it does not read the
# loudness meter: it is block power in, up to 12 dB of programme-level correction
# out, applied to the signal that leaves the plugin.
#
# The stage itself moved out of the per-sample loop into
# Source/DSP/AutoGainCompensator.h, where tests/gain measures it on generated signal -
# the pole, the ceiling, the no-boost rule and the silence floor are numbers there
# rather than sentences here. What stays a TEXT check is the wiring around it, which
# no harness can see: whether the call site is gated on the platter being at speed,
# whether the power it hands over is measured BEFORE the correction, whether both
# legs of the MIX crossfade carry it, and whether prepare() starts it neutral. Each
# of those is a level error on every project if an edit drops it, and each names the
# file it read so a fact that moves is found where it went.
# The gate is `state.autoGainEnabled && transport.atSpeed()` since the reference
# floor moved inside the stage; the `state.` is optional in the pattern on purpose -
# the check is about the gate's SHAPE, and a gate that lost a term is the fault
# whether or not the split is undone.
COMPENSATOR_HEADER = REPO / "Source" / "DSP" / "AutoGainCompensator.h"
COMPENSATION_GATE = re.compile(r"if \((?:state\.)?autoGainEnabled && transport\.atSpeed\(\)\)")
COMPENSATION_CALL = re.compile(r"compensationGain = autoGainCompensator\.process "
                               r"\(state\.referenceBlockPower,\s*postCompressorPower,\s*"
                               r"state\.compensationCoefficient\)")
COMPENSATION_CLAMP = "std::clamp (levelRatioDb, 0.0f, maximumRestorationDb)"
COMPENSATION_SMOOTHING = "smoothedDb += (targetDb - smoothedDb) * coefficient;"
COMPENSATION_FLOOR = "if (referencePower <= 0.0f || outputPower <= minimumOutputPower)"
# ... and the coefficient it rides is the block's own 450 ms pole, read through the state
# since the split.
COMPENSATION_TIME_MS = re.compile(r"onePoleCoefficient \(450\.0f, (?:state\.)?engineSampleRate\)")


def matching_brace(text: str, start: int) -> int:
    """The index just past the `}` that closes the block opened at or after `start`.

    Counted rather than found as `\n    }\n`, which is what this used to do: that anchor was
    the gate's own indentation, and the indentation of the block is a fact about how deeply
    it is nested (it was eight spaces inside a lambda and is four inside a function since the
    TapeBlockState split). The cut has to be the block itself, or a check about what the
    block contains starts measuring whatever brace comes next. Comments and literals are
    skipped so a brace in prose cannot be counted.
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
            while index < len(text) and text[index] != '"':
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

    raise SystemExit("extract.py: the compensation's gate is never closed")


def compensation_block(impl: str) -> str | None:
    """The `if` that folds the correction in, cut through its closing brace."""
    gate = COMPENSATION_GATE.search(impl)
    if gate is None:
        return None

    return impl[gate.start():matching_brace(impl, gate.start())]


def check_auto_gain_compensation(impl: str, compensator: str) -> list[str]:
    """Returns the reasons the AUTO GAIN correction could be wrong on signal.

    `impl` is the processor's own source (the wiring), `compensator` is the stage
    header it now calls (the rule). Splitting the two is the point: the rule has a
    harness that renders it, so what is checked as text here is only what a harness
    cannot see.
    """
    problems: list[str] = []

    # 1. It must not try to correct a machine that is not running. With STOP
    #    gating the output the measurement collapses, the compensator ramps to
    #    its +12 dB limit while the platter sits still, and START then comes back
    #    loud before the correction relaxes.
    if COMPENSATION_GATE.search(impl) is None:
        problems.append("the compensation is not gated on AUTO GAIN being on and the platter being "
                        "at speed, so a stopped machine is corrected as if it were silent output")
        return problems

    block = compensation_block(impl)
    assert block is not None

    # 2. The measurement is the signal BEFORE the correction is folded in, and it
    #    is handed to the stage rather than smoothed here. Measuring the
    #    compensated output makes the correction chase its own tail.
    measured = re.search(r"const auto signal = ([^;]*);", block, re.S)

    if measured is None or "tapeOutput[" not in measured.group(1) \
            or "stageGainPerChannel[" not in measured.group(1):
        problems.append("the compensated level is not measured on the signal as it leaves the "
                        "compressors and tape stage, so the amount being corrected is not the "
                        "amount the chain lost")
    elif "compensationGain" in measured.group(1):
        problems.append("the compensated level is measured AFTER the compensation is folded in, so "
                        "the correction chases its own tail")

    if COMPENSATION_CALL.search(block) is None:
        problems.append("the call site does not hand the reference power, the pre-correction power "
                        "and the block's coefficient to Source/DSP/AutoGainCompensator.h, so the "
                        "stage tests/gain measures is not the one the plugin runs")

    # 3. The stage's own rule, read out of the header: silence is not a measurement
    #    (it returns unity and holds the state), a loss is clamped to 0 .. 12 dB so
    #    AUTO GAIN cannot act as a boost stage, and the correction is a one-pole
    #    towards its target rather than a step. tests/gain proves each of those on
    #    rendered signal; this is what keeps the header the file that renders.
    if COMPENSATION_FLOOR not in compensator:
        problems.append("AutoGainCompensator.h no longer floors a silent frame, so a pause in the "
                        "programme would drive the correction to its limit")

    if COMPENSATION_CLAMP not in compensator:
        problems.append("AutoGainCompensator.h no longer clamps the correction to 0 .. 12 dB, so "
                        "AUTO GAIN can act as a boost stage of its own rather than restoring what "
                        "the chain lost")

    if COMPENSATION_SMOOTHING not in compensator:
        problems.append("AutoGainCompensator.h no longer smooths towards its target, so the "
                        "correction steps the output level rather than riding the programme")

    # 4. The pole is 450 ms and it is built from the engine's rate, so the smoothing
    #    is the same duration at every sample rate.
    if COMPENSATION_TIME_MS.search(impl) is None:
        problems.append("the correction's time constant is not built from the engine's rate, so the "
                        "smoothing is not the same duration at every sample rate")

    # 5. It starts from unity, so the first block of a session is not nudged by a
    #    correction left over from a previous one or a previous sample rate.
    resets = impl.count("autoGainCompensator.reset();")

    if resets != 1:
        problems.append(f"`autoGainCompensator.reset();` appears {resets} times in "
                        f"{impl_source_of('autoGainCompensator.reset();')}: the correction has to "
                        f"start from unity exactly once per prepare")

    if compensator.count("void reset() noexcept { smoothedDb = 0.0f; }") != 1:
        problems.append("AutoGainCompensator.h does not have exactly one place that returns the "
                        "correction to unity, so a reset either does nothing or happens twice")

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


def check_mono_delay_feedback(impl: str) -> list[str]:
    """Ensure mono feedback never writes to the absent right-channel delay line."""
    problems: list[str] = []
    delay_gates = list(re.finditer(r"if\s*\(\s*delayBufferLength\s*>\s*0\s*\)\s*\{", impl))
    block = next(
        (impl[gate.start():matching_brace(impl, gate.end() - 1)]
         for gate in reversed(delay_gates)
         if "feedbackAmount" in impl[gate.start():matching_brace(impl, gate.end() - 1)]),
        None,
    )

    if block is None:
        return ["the tape delay write block is missing, so mono feedback routing cannot be checked"]

    if re.search(r"const float requestedDelaySamples\s*=\s*delaySamplesSmoothed\.getCurrentValue\(\);"
                 r"\s*const bool delayEnabled\s*=\s*requestedDelaySamples\s*>\s*0\.0f;"
                 r"\s*const float delaySamples\s*=\s*juce::jmax\s*\(1\.0f,\s*requestedDelaySamples\);",
                 impl) is None:
        problems.append("zero delay is not treated as a disengaged playback head, or an active head "
                        "can read from its current write slot")

    if "const bool stereo = state.activeChannels > 1;" not in block:
        problems.append("the tape delay feedback does not distinguish mono from stereo, so mono "
                        "may write to a delay channel that does not exist")
        return problems

    if re.search(r"if\s*\(\s*delayEnabled\s*\)\s*\{[^{}]*feedbackAmount", block, re.S) is None:
        problems.append("zero delay can still write feedback into the delay ring")

    if re.search(r"delayWrite\[delayWritePosition\]\s*\+=\s*feedbackAmount\s*"
                 r"\*\s*\(stereo\s*\?\s*\(1\.0f\s*-\s*pingPongNow\)\s*:\s*1\.0f\);",
                 block) is None:
        problems.append("mono tape delay feedback is not kept entirely in its only delay line")

    other_channel_write = re.search(
        r"if\s*\(\s*stereo\s*\)\s*\{\s*const int feedbackChannel\s*=\s*1\s*-\s*channel\s*;"
        r"\s*delayBuffer\.getWritePointer\s*\(\s*feedbackChannel\s*\)",
        block,
        re.S,
    )
    if other_channel_write is None:
        problems.append("the tape delay's other-channel feedback write is not guarded by a stereo "
                        "channel-count check")

    return problems


def main() -> int:
    destination = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "extracted_dsp.inc")
    header = HEADER.read_text()
    impl = processor_impl()
    stage = STAGE_HEADER.read_text()
    compensator = COMPENSATOR_HEADER.read_text()

    problems = check_neural_rate_wiring(header, impl, stage)
    problems += check_auto_gain_compensation(impl, compensator)
    problems += check_mono_delay_feedback(impl)
    if problems:
        for problem in problems:
            print(f"extract.py: {problem}", file=sys.stderr)
        return 1

    print("neural rates: the .nam file's own rate reaches both stages, beside the engine's "
          "session rate times its oversampling factor")
    print("auto gain: the compensator measures before it corrects, is gated on the platter, only "
          "ever restores, starts neutral and rides both legs of the MIX crossfade")
    print("tape delay: mono keeps feedback in its only channel; stereo alone writes ping-pong "
          "feedback to the other channel, and zero delay disengages the playback head")

    chunks = ["// GENERATED FILE - do not edit.",
              "// Built out of Source/ by tests/dsp/extract.py: the DSP stages and the",
              "// engine's inline shapers are included from their own headers.",
              "",
              f"#define SUBFUND_MIX_INVERTS {1 if mix_inverts(header) else 0}",
              ""]

    # The stages, all of them, in the order the plugin's own header includes them.
    chunks.append("// ---- Source/DSP/ headers, included rather than cut " + "-" * 18)

    for header_name in SOURCE_HEADERS:
        chunks.append(f'#include "{header_name}"')

    destination.write_text("\n".join(chunks))
    print(f"wrote {destination} ({destination.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
