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

# The scalar math bridge the DSP blocks call. It sits in its own guarded namespace
# block in the header, so it is cut out by span rather than by the struct rule.
# Under the harness J37_HAS_CHOWDSP_MATH is 0, so what gets extracted is the
# std:: fallback - the harness always measures the exact reference math, never
# the chowdsp approximation, which is what lets the benchmark compare the two.
MATH_BRIDGE_START = "#if J37_HAS_CHOWDSP_MATH\nnamespace j37math"
MATH_BRIDGE_END = "#endif\n"

# The namespace-level constants SaturationCore's set<Family>Voice methods
# reference. The struct extraction cuts only the struct's own text, and these
# live at namespace scope above it - so without this list the harness compiles
# against constants that do not exist (exactly how adc23c9's new DIGITAL voice
# broke it: the method text arrived, the constant it reads did not).
CONSTANT_PIECES = [
    "inline constexpr int valveTypeCount",
    "inline constexpr int ampTypeCount",
    "inline constexpr int transformerTypeCount",
    "inline constexpr int digitalTypeCount",
]

HEADER_PIECES = [
    ("GlueCompressor", "struct GlueCompressor"),
    ("LoudnessMeter", "struct LoudnessMeter"),
    ("SaturationCore", "struct SaturationCore"),
    ("SubharmonicGenerator", "struct SubharmonicGenerator"),
    # The vinyl stage is extracted for the same reason the saturation core is:
    # the noise tab's controls act on it, so a check that the NOISE trims and the
    # VINYL SPEED do what they claim has to run the shipping stage, not a copy.
    ("VinylStage", "struct VinylStage"),
    # The transient shaper is extracted so its two claims can be measured rather
    # than asserted in a comment: that at both amounts zero it is a bit-for-bit
    # pass-through, and that a positive attack amount makes an edge sharper while
    # a negative one rounds it. Under the harness it compiles exactly as shipped -
    # it reaches for nothing the harness does not already provide.
    ("TransientShaper", "struct TransientShaper"),
    # The neural stage is extracted too, which is what proves the guard is honest:
    # with J37_HAS_RTNEURAL 0 (the harness case) every member reduces to the
    # pass-through, so the struct still compiles and still renders the input
    # unchanged. A source that only compiled WITH the library would fail here.
    # The rate bridge is extracted because it is the one part of the neural stage
    # that is plain arithmetic: it carries a signal between the engine's rate and a
    # model's own, and whether it does that correctly - the model driven at ITS
    # rate, the two legs of the stage's crossfade in phase, and a matching model
    # bit-for-bit untouched - is measurable on a signal without a model present.
    #
    # BEFORE the stage, in this list as in the header: the stage HOLDS one, so an
    # extraction that emitted it afterwards would compile a member of an incomplete
    # type. The order here is the header's order, which is the only order that
    # works.
    ("RateBridge", "struct RateBridge"),
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


def extract_math_bridge(header: str) -> str:
    """Cuts the j37math namespace block out of the header.

    The bridge is two guarded namespace blocks; the harness only wants the second
    one (the std:: fallback), because it never has chowdsp on its include path.
    Extracting the SHIPPING text rather than retyping it means the harness cannot
    drift from what the plugin actually compiles.
    """
    start = header.index(MATH_BRIDGE_START)
    # The fallback block is the `#else ... #endif` half.
    else_at = header.index("\n#else\n", start)
    end = header.index(MATH_BRIDGE_END, else_at) + len(MATH_BRIDGE_END)
    return header[start:end]


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


def main() -> int:
    destination = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "extracted_dsp.inc")
    header = HEADER.read_text()
    impl = IMPL.read_text()

    problems = check_neural_rate_wiring(header, impl)
    if problems:
        for problem in problems:
            print(f"extract.py: {problem}", file=sys.stderr)
        return 1

    print("neural rates: the .nam file's own rate reaches both stages, beside the engine's "
          "session rate times its oversampling factor")

    chunks = ["// GENERATED FILE - do not edit.",
              "// Cut verbatim out of Source/ by tests/dsp/extract.py.",
              "",
              f"#define SUBFUND_MIX_INVERTS {1 if mix_inverts(header) else 0}",
              ""]

    chunks.append("// ---- j37math bridge (PluginProcessor.h) " + "-" * 34)
    chunks.append(extract_math_bridge(header))
    chunks.append("")
    chunks.append("")

    # Namespace constants first: the structs below reference them.
    for declaration in CONSTANT_PIECES:
        line_start = header.index(declaration)
        end = header.index("\n", line_start) + 1
        chunks.append("// ---- constant (PluginProcessor.h) " + "-" * 38)
        chunks.append(header[line_start:end])
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
