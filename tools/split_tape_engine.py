#!/usr/bin/env python3
"""The big cut of processTapeEngine(): the sample lambda's captures become a struct.

`processTapeEngine()` is 3,307 lines and its middle is a 1,669-line lambda,
`[&] (int sample) noexcept`, called once per sample. The lambda is the machine; everything
above it is one block's worth of parameters, poles, curves and operating points, and it
reads 68 of those locals by CAPTURE - which is why the function cannot be taken apart:
there is no boundary a reader or a compiler can see between "what this block is set up as"
and "what happens to one sample", only a capture list that means all of it.

This writes that boundary down. The 68 names the lambda reads become the fields of
`TapeBlockState` (declared in PluginProcessor.h), the declarations that created them become
assignments into it, and the lambda becomes a member function that takes it:

    void FirstAudioProcessor::processOneSample (TapeBlockState& state, int sample) noexcept

Nothing is retyped. Every statement stays the same statement, and the only text that
changes is what has to: `tapeCurve` becomes `state.tapeCurve`, and a declaration such as

    const float tapeCurve = blend (slowMachineCurve, fastMachineCurve);

becomes an assignment into the field of that name:

    state.tapeCurve = blend (slowMachineCurve, fastMachineCurve);

One thing moved rather than changed shape. `nextNoise` is a lambda that takes its PRNG state
by reference and is called once, from inside the sample loop (`nextNoise (noiseState)`), so
it becomes a file-local function instead of a field.

What this refuses to do, and why each refusal is a hazard rather than caution:

  * The set of fields is asserted to be EXACTLY the set of function-scope names the sample
    lambda reads (plus the noise generator, which is accounted for separately). A name that
    is read but is not a field would fail at link; a field the lambda never reads would be
    filled in and never used.
  * A field whose name is USED before its declaration line is refused. In C++ that use is a
    MEMBER of the same name rather than the local - so renaming it would point the code at
    the wrong object, and it would still compile.
  * A field whose name is DECLARED AGAIN inside the function is refused for the same reason:
    inside that inner scope the name is a different variable, and a text rewrite cannot tell
    them apart. (`index` is the only such name, and it is not a field.)
  * Only CODE is rewritten. Comments and string literals are masked out first, so a name in
    prose - and this file is half prose - is left exactly as it is.

Usage:  python3 tools/split_tape_engine.py
"""

from __future__ import annotations

import pathlib
import re

REPO = pathlib.Path(__file__).resolve().parents[1]
PATH = REPO / "Source" / "ProcessorSignalPath.cpp"

LAMBDA = "    const auto processOneSample = [&] (int sample) noexcept"
NOISE = "    const auto nextNoise = [] (std::uint32_t& state) -> float"

# The fields of TapeBlockState, in the order the struct declares them and grouped by what
# they are. Each one is a function-scope local of processTapeEngine() that the per-sample
# lambda reads; the list is written here AND in the struct's declaration, and the first
# thing this script does is prove the two agree with the function itself.
FIELDS = [
    # The block's own frame.
    "engineSampleRate", "activeChannels", "channelData",
    # The controls the hot path reads.
    "antiPhaseGuardOn", "autoGainEnabled", "autoTuneStrength",
    "compressorAttack", "compressorMakeup", "compressorMixAmount", "compressorRatio",
    "compressorRelease", "compressorThreshold",
    "offsetRightChannel", "polaritySign", "trackBleedDelaySamples", "trackCrosstalk",
    "trackWidthScale",
    # The machine's curves, its poles and the levels it works between.
    "dcBlockR", "driveCurve", "finalOutputGain", "flutterDepth", "flutterFreq",
    "headGapModulationMask", "hissBandLimit", "loadInducedSpeedError",
    "platterGateExponent", "platterPitchExponent", "speedBias", "tapeCurve", "tapeHiss",
    "tapeTypeSwitching", "transportActivityGate", "twoPi", "wearWowCoupling", "wowDepth",
    "wowFreq",
    # The two equalisers' mid-band coefficients.
    "inputEqMidCoefficient", "outputEqMidCoefficient",
    # The glue pair: their operating points, their coefficients and their makeup.
    "compensationCoefficient", "inputCompressorCoefficients", "inputCompressorRatio",
    "inputKneeDb", "inputMakeupFraction", "inputReductionLimitDb", "inputThresholdDb",
    "outputCompressorCoefficients", "outputCompressorRatio", "outputKneeDb",
    "outputMakeupFraction", "outputReductionLimitDb", "outputThresholdDb",
    # The meters' own coefficients.
    "inputLoudnessCoefficient", "outputLoudnessCoefficient", "vuBallisticCoefficient",
    # What the loop carries from sample to sample, and what the telemetry tail reads back.
    "clippingThisBlock", "currentInputLufs", "currentLufs", "driftAccumulator",
    "inputChainHistory", "inputClippingThisBlock", "inputEnvelopeActivity", "inputPeak",
    "inputPeakReductionDb", "inputSquares", "outputPeak", "outputSquares",
    "peakReductionDb", "referenceBlockPower",
]

# The names declared `auto` in the function, whose real type the struct has to state.
AUTO_TYPES = {
    "engineSampleRate": "float",
    "autoTuneStrength": "float",
    "compressorAttack": "float",
    "compressorMakeup": "float",
    "compressorMixAmount": "float",
    "compressorRatio": "float",
    "compressorRelease": "float",
    "compressorThreshold": "float",
    "driveCurve": "float",
    "flutterFreq": "float",
    "wowFreq": "float",
    "twoPi": "float",
    "inputCompressorCoefficients": "GlueCompressor::Coefficients",
    "outputCompressorCoefficients": "GlueCompressor::Coefficients",
    "inputLoudnessCoefficient": "float",
    "outputLoudnessCoefficient": "float",
}

DECLARATION = re.compile(r"^(?P<indent>[ ]+)(?P<spec>(?:(?:const|constexpr|static|volatile) "
                         r")*)(?P<type>[A-Za-z_][\w:]*(?:<[^;=(){}]*>)?(?:[ ]*[&*])?)[ ]+"
                         r"(?P<name>[A-Za-z_]\w*)[ ]*(?P<rest>=|;|\{)")
CONTROL_WORDS = {"return", "if", "else", "for", "while", "switch", "case", "break",
                 "continue", "throw", "jassert", "do"}

BANNER = """//==============================================================================
//  The machine, one sample at a time.
//
//  This was a 1,669-line lambda inside processTapeEngine(), capturing 68 of that
//  function's own locals by reference - which is why that function could not be read:
//  nothing distinguished one block's setup from one sample's work but a capture list
//  that meant all of it. It takes the block's state as an argument now, TapeBlockState
//  (declared in PluginProcessor.h), and the `state` in its body is that argument.
//
//  The body is the lambda's, moved: every statement is the same statement, in the same
//  order, and the only text that changed is the names that now reach through `state`.
//  The block-rate reading the body does - the per-sample controls it reads once per
//  frame from the shared sample clock - is unchanged: those are members, not state.
//==============================================================================
void FirstAudioProcessor::processOneSample (TapeBlockState& state, int sample) noexcept
"""


def masked(line: str) -> list[bool]:
    """Which characters of `line` are CODE (True) rather than comment or literal."""
    out = [True] * len(line)
    i = 0
    while i < len(line):
        c = line[i]
        if c == "/" and i + 1 < len(line) and line[i + 1] == "/":
            for j in range(i, len(line)):
                out[j] = False
            break
        if c == "/" and i + 1 < len(line) and line[i + 1] == "*":
            end = line.find("*/", i + 2)
            end = len(line) if end < 0 else end + 2
            for k in range(i, end):
                out[k] = False
            i = end
            continue
        if c in "\"'":
            quote = c
            j = i + 1
            while j < len(line) and line[j] != quote:
                j += 2 if line[j] == "\\" else 1
            end = min(len(line), j + 1)
            for k in range(i, end):
                out[k] = False
            i = end
            continue
        i += 1
    return out


def code(line: str, mask: list[bool]) -> str:
    """`line` with its comment and literal characters blanked, for searching."""
    return "".join(c if keep else " " for c, keep in zip(line, mask))


def rewrite(line: str, mask: list[bool], pattern: re.Pattern, replacement: str) -> str:
    """Applies `pattern` to the CODE runs of `line` only."""
    out = []
    i = 0
    while i < len(line):
        if not mask[i]:
            out.append(line[i])
            i += 1
            continue
        j = i
        while j < len(line) and mask[j]:
            j += 1
        out.append(pattern.sub(replacement, line[i:j]))
        i = j
    return "".join(out)


def dedent(lines: list[str], spaces: int = 4) -> list[str]:
    return [line[spaces:] if line.startswith(" " * spaces) else line for line in lines]


def parentheses(line: str, mask: list[bool]) -> int:
    """The line's own paren balance, counting code characters only."""
    return sum((1 if c == "(" else -1) for c, keep in zip(line, mask) if keep
               and c in "()")


def last_open(line: str, mask: list[bool]) -> int:
    """The column of the last unclosed `(` on a line that has one, or -1."""
    depth = 0
    column = -1
    for index, (c, keep) in enumerate(zip(line, mask)):
        if not keep:
            continue
        if c == "(":
            depth += 1
            column = index
        elif c == ")":
            depth -= 1
            if depth == 0:
                column = -1
    return column


def realign(old: list[str], new: list[str], mask: list[list[bool]], body: tuple[int, int]) -> int:
    """Moves continuation lines that were aligned to an opening paren that has moved.

    The rename lengthened the first line of every statement it touched, so a continuation
    line indented to sit just under that statement's `(` is no longer under it. Only lines
    whose indentation was EXACTLY the paren's column plus one are moved: that is the one
    style this file states as a rule (`processFrame (a,\n                      b)`), and a
    continuation that was aligned some other way is the author's, not an artefact.
    """
    moved = 0
    index = body[0]
    while index <= body[1]:
        balance = parentheses(old[index], mask[index])
        if balance <= 0:
            index += 1
            continue

        was = last_open(old[index], mask[index])
        now = last_open(new[index], mask[index])
        statement = balance
        cursor = index + 1
        while statement > 0 and cursor <= body[1]:
            line = new[cursor]
            statement += parentheses(line, masked(line))
            indent = len(line) - len(line.lstrip(" "))
            if was >= 0 and indent == was + 1 and line.strip():
                new[cursor] = " " * (now + 1) + line.lstrip(" ")
                moved += 1
            cursor += 1
        index = cursor

    return moved


def main() -> int:
    lines = PATH.read_text().split("\n")

    # ----------------------------------------------------------------------
    #  Where everything is.
    # ----------------------------------------------------------------------
    signature = next((i for i, line in enumerate(lines)
                      if re.match(r"^[A-Za-z_][\w:<>,&*\s]*::processTapeEngine\s*\(", line)), None)
    if signature is None:
        raise SystemExit(f"split_tape_engine: processTapeEngine is not in {PATH.name}")

    open_brace = signature
    while lines[open_brace].strip() != "{":
        open_brace += 1
    close_brace = open_brace
    while lines[close_brace] != "}":
        close_brace += 1
    body = (open_brace + 1, close_brace - 1)

    for anchor, name in ((LAMBDA, "the sample lambda"), (NOISE, "the noise generator")):
        if sum(1 for line in lines if line == anchor) != 1:
            raise SystemExit(f"split_tape_engine: {name} is not in {PATH.name} exactly once:\n"
                             f"{anchor}")

    lambda_start = lines.index(LAMBDA)
    lambda_close = lambda_start
    while lines[lambda_close] != "    };":
        lambda_close += 1
    noise_start = lines.index(NOISE)
    noise_close = noise_start
    while not lines[noise_close].rstrip().endswith("};"):
        noise_close += 1

    mask = [masked(line) for line in lines]

    # ----------------------------------------------------------------------
    #  The declarations the fields replace: one each, at the function's own scope.
    # ----------------------------------------------------------------------
    declared: dict[str, tuple[int, str, str]] = {}
    for index in range(body[0], lambda_start):
        match = DECLARATION.match(lines[index])
        # The function's OWN scope is four spaces of indentation: a declaration deeper than
        # that belongs to a loop or a block, and its name is not one the lambda captures.
        # (`index` is declared five times inside this function and none of them is here.)
        if (match and match.group("type") not in CONTROL_WORDS and lines[index].strip()
                and match.group("indent") == "    "):
            name = match.group("name")
            if name in declared:
                raise SystemExit(f"split_tape_engine: `{name}` is declared twice at the "
                                 f"function's own scope (lines {declared[name][0] + 1} and "
                                 f"{index + 1}); the rewrite cannot tell them apart.")
            declared[name] = (index, match.group("type"), match.group("rest"))

    lambda_text = "\n".join(code(lines[i], mask[i]) for i in range(lambda_start, lambda_close + 1))
    reads = re.compile(r"(?<![\w.>])" + r"|".join(sorted(map(re.escape, declared),
                                                         key=len, reverse=True)) + r"\b")
    captured = {name for name in declared if re.search(r"(?<![\w.>])" + re.escape(name) + r"\b",
                                                       lambda_text)}
    if "noiseState" in captured:
        raise SystemExit("split_tape_engine: `noiseState` is a member of the processor, not a "
                         "local; the field list must not carry it.")

    problems = []
    if captured != set(FIELDS) | {"nextNoise"}:
        for name in sorted(captured - set(FIELDS) - {"nextNoise"}):
            problems.append(f"`{name}` is read by the sample lambda and is not a field")
        for name in sorted(set(FIELDS) - captured):
            problems.append(f"`{name}` is a field and the sample lambda never reads it")

    for name in FIELDS:
        if name not in declared:
            problems.append(f"`{name}` is a field and is not declared at the function's scope")
            continue
        line_index = declared[name][0]
        for index in range(body[0], line_index):
            if re.search(r"(?<![\w.>])" + re.escape(name) + r"\b", code(lines[index], mask[index])):
                problems.append(f"`{name}` is used on line {index + 1}, before its declaration on "
                                f"{line_index + 1}: that use is not this local")
                break
        for index in range(body[0], body[1] + 1):
            match = DECLARATION.match(lines[index])
            if (match and match.group("name") == name
                    and match.group("type") not in CONTROL_WORDS and index != line_index):
                problems.append(f"`{name}` is declared again on line {index + 1} (inside a nested "
                                f"scope), so a text rewrite cannot tell them apart")
                break

    if problems:
        raise SystemExit("split_tape_engine: the rewrite would not be a rewrite:\n  - "
                         + "\n  - ".join(problems))

    # ----------------------------------------------------------------------
    #  The rewrite: declarations become assignments, and uses reach through `state`.
    # ----------------------------------------------------------------------
    # The lookbehind keeps a qualified name out of it: `juce::MathConstants<float>::twoPi`
    # is not this function's `twoPi`, and `settings.foo` is not this `foo` either.
    use = {name: re.compile(r"(?<![\w.>:])" + re.escape(name) + r"\b") for name in FIELDS}
    new_lines = list(lines)

    for name in FIELDS:
        line_index, type_text, rest = declared[name]
        if name in AUTO_TYPES and type_text != "auto":
            raise SystemExit(f"split_tape_engine: `{name}` is declared `{type_text}` on line "
                             f"{line_index + 1}, but AUTO_TYPES calls it `auto`")
        if rest == ";":
            raise SystemExit(f"split_tape_engine: `{name}` on line {line_index + 1} has no "
                             f"initialiser to move into the field:\n{lines[line_index]}")

        line = lines[line_index]
        match = DECLARATION.match(line)
        after = line[match.end("rest"):] if rest == "=" else line[match.end("rest") - 1:]
        # The initialiser is rewritten too: a coefficient built from another one
        # (`inputEq.midBandCoefficient (engineSampleRate)`) reads that field through
        # `state` like every other use, and the mask keeps its comments alone.
        after = after.lstrip()
        for other in FIELDS:
            if use[other].search(after):
                after = rewrite(after, masked(after), use[other], "state." + other)
        new_lines[line_index] = match.group("indent") + "state." + name + " = " + after

    declaration_lines = {declared[name][0] for name in FIELDS}
    for index in range(body[0], body[1] + 1):
        if index in declaration_lines:
            continue
        line = new_lines[index]
        for name in FIELDS:
            if use[name].search(line):
                # The mask is recomputed per step: a line that has already had one name
                # rewritten into `state.<name>` is longer than the line it was measured
                # from, and the old mask would run off its end.
                line = rewrite(line, masked(line), use[name], "state." + name)
        new_lines[index] = line

    # ----------------------------------------------------------------------
    #  The lambda becomes the member function; the noise generator a file-local one.
    # ----------------------------------------------------------------------
    # The lambda's own `{` and `};` are dropped: the member function below has both, and
    # the body between them is the lambda's statements, four spaces less indented.
    lam_body = dedent(new_lines[lambda_start + 2:lambda_close])

    noise_brace = noise_start
    while lines[noise_brace].strip() != "{":
        noise_brace += 1
    noise_body = dedent(new_lines[noise_brace + 1:noise_close])
    noise_comment = []
    comment_at = noise_start - 1
    while comment_at >= 0 and lines[comment_at].strip().startswith("//"):
        noise_comment.insert(0, lines[comment_at])
        comment_at -= 1
    noise_comment = dedent(noise_comment)

    noise = ("namespace\n{\n" + "\n".join(noise_comment) + "\n"
             + "// It is a function rather than a field of TapeBlockState because it takes its own\n"
             + "// state by reference and touches nothing else of the block's, and because it is\n"
             + "// called from inside the machine - where holding it as a std::function would be an\n"
             + "// indirect call per sample.\n"
             + "inline float nextNoise (std::uint32_t& state) noexcept\n{\n"
             + "\n".join(noise_body) + "\n}\n}\n")

    moved = realign(lines, new_lines, mask, body)

    kept = []
    index = 0
    while index < len(new_lines):
        if index == noise_start:
            index = noise_close + 1
            continue
        if index == lambda_start:
            kept.append("")
            index = lambda_close + 1
            continue
        kept.append(new_lines[index])
        index += 1

    text = "\n".join(kept)
    text, calls = re.subn(
        r"    for \(int sample = 0; sample < numSamples; \+\+sample\)\n"
        r"        processOneSample \(sample\);\n",
        "    for (int sample = 0; sample < numSamples; ++sample)\n"
        "        processOneSample (state, sample);\n", text)
    if calls != 1:
        raise SystemExit("split_tape_engine: the per-sample loop does not read "
                         f"`processOneSample (sample);` exactly once ({calls} matches)")

    text = text.replace("#include \"HotPathProbe.h\"\n",
                        "#include \"HotPathProbe.h\"\n\n\n" + noise + "\n", 1)

    cut = text.index("void FirstAudioProcessor::processTapeEngine (")
    # The function's own closing brace is the first `}` that starts a line after it: this
    # file's last line is that brace, with no newline after it.
    end = text.index("\n}", cut) + 2
    text = (text[:end] + "\n\n" + BANNER + "{\n" + "\n".join(lam_body) + "\n}\n" + text[end:]
            + ("" if text.endswith("\n") else "\n"))

    # The struct itself, declared once at the top of the function it describes. It is the
    # first thing the function does because every field is written below it: what the
    # declarations it replaces used to say - `engineSampleRate = ...` - is now
    # `state.engineSampleRate = ...`, and the state has to exist before the first one.
    opening = text.index("void FirstAudioProcessor::processTapeEngine (")
    brace = text.index("\n{\n", opening) + 3
    text = (text[:brace]
            + "    // Everything this block is set up as, worked out below and read by the machine\n"
            + "    // one sample at a time (see TapeBlockState in PluginProcessor.h). The fields the\n"
            + "    // old locals put on the stack are here instead, because the machine is a function\n"
            + "    // now and cannot capture them.\n"
            + "    TapeBlockState state;\n\n" + text[brace:])

    PATH.write_text(text)

    print(f"split_tape_engine: {PATH.name} is now {len(text.splitlines())} lines")
    print(f"  {len(FIELDS)} fields, {len(declaration_lines)} declarations became assignments")
    print(f"  {moved} continuation line(s) followed the opening paren they were aligned to")
    print(f"  the sample lambda: {lambda_close - lambda_start - 1} lines -> "
          f"FirstAudioProcessor::processOneSample (TapeBlockState&, int)")
    print(f"  the noise generator: {len(noise_body)} lines -> a file-local nextNoise")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
