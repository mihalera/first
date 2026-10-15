#!/usr/bin/env python3
"""The second split of Source/PluginProcessor.cpp: the signal path leaves the file.

Three moves, each a cut of EXACT text (asserted to occur once):

  Source/DSP/HotPathMath.h       the five inline shapers the engine's per-sample code
                                 calls - j37Tanh, the two one-pole coefficient
                                 builders, softClip and the soft-knee gain computer -
                                 which are needed by the signal path AND by the
                                 preparation code that stays behind, so they cannot
                                 travel with either one. Their bodies are untouched
                                 except for the four spaces of indentation the .cpp's
                                 anonymous namespace gave them.
  Source/HotPathProbe.h          the four probe counters (now `inline` variables: one
                                 translation unit writes them and another reads them)
                                 and the readout that turns them into a snapshot.
  Source/ProcessorSignalPath.cpp the signal path itself - processBlock, the transport
                                 commands, the bypass and post-machine stages, the
                                 autotune frame, the telemetry publish and the 3,366
                                 line processTapeEngine - moved whole, in order.

The two headers are what makes the move possible rather than a rewrite: the file-local
helpers the moving code and the staying code both call have to become headers for
either of them to compile, and a header is also what makes them measurable - the DSP
harness includes Source/DSP/HotPathMath.h now instead of cutting a copy of it out of
this file.
"""

from __future__ import annotations

import pathlib
import sys

REPO = pathlib.Path(__file__).resolve().parents[1]
PROCESSOR = REPO / "Source" / "PluginProcessor.cpp"


def brace_end(text: str, start: int) -> int:
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

    raise SystemExit("split_processor_path: a block was opened but never closed")


def block_start(text: str, title: str) -> int:
    """Where the rule line above a banner comment's title begins.

    The rule may be drawn with `=` or with `-`; both are used in this file, and the
    count of the characters is not something a cut should depend on.
    """
    if text.count(title) != 1:
        raise SystemExit(f"split_processor_path: `{title.strip()}` appears "
                         f"{text.count(title)} times")

    position = text.rindex("\n", 0, text.index(title)) + 1

    while position > 0:
        previous_end = position - 1
        previous_start = text.rindex("\n", 0, previous_end) + 1
        line = text[previous_start:previous_end].strip()

        if line.startswith("//") and len(line) > 12 and set(line[2:].strip()) in ({"="}, {"-"}):
            return previous_start

        if not line.startswith("//"):
            break

        position = previous_start

    return position


def span(text: str, start_anchor: str, end_anchor: str, label: str) -> str:
    """The exact text from `start_anchor` through `end_anchor`, each asserted once."""
    for anchor in (start_anchor, end_anchor):
        if text.count(anchor) != 1:
            raise SystemExit(f"split_processor_path: {label}: an anchor appears "
                             f"{text.count(anchor)} times, not once")

    start = text.index(start_anchor)
    end = text.index(end_anchor, start) + len(end_anchor)
    return text[start:end]


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if text.count(old) != 1:
        raise SystemExit(f"split_processor_path: {label}: the text to replace appears "
                         f"{text.count(old)} times, not once")
    return text.replace(old, new, 1)


def dedent(block: str, width: int = 4) -> str:
    """The text, shifted left by the anonymous namespace's indentation.

    The ONLY change made inside any moved span in this script, and it is made where
    it is visible: see the header banners for why there is no enclosing namespace to
    keep the indentation honest against.
    """
    out = []
    for line in block.splitlines():
        out.append(line[width:] if line.startswith(" " * width) else line)
    return "\n".join(out) + "\n"


def main() -> int:
    src = PROCESSOR.read_text()
    original = src

    counters_at = block_start(src, "    //  Hot-path probe counters (J37_ENABLE_HOTPATH_PROBE).")
    counters_end_anchor = "    std::atomic<bool> hotPathThisBlockActive { false };\n#endif\n"
    if src.count(counters_end_anchor) != 1:
        raise SystemExit("split_processor_path: the probe counters: the closing anchor "
                         "appears %d times, not once" % src.count(counters_end_anchor))
    atomics = src[counters_at:src.index(counters_end_anchor, counters_at) + len(counters_end_anchor)]
    src = src.replace(atomics, "", 1)

    shapers = span(
        src,
        "    /**\n        The tanh the magnetic shaper calls.\n",
        "        return -(1.0f - 1.0f / ratio) * overshootDb;\n    }\n",
        "the five inline shapers")
    src = src.replace(shapers, "", 1)

    probe = span(
        src,
        "#if ! defined (J37_DSP_HARNESS) && defined (J37_ENABLE_HOTPATH_PROBE)\n"
        "// ===========================================================================\n"
        "//  HOT-PATH PROBE READOUT.\n",
        "        return s;\n    }\n}\n#endif\n",
        "the probe readout")
    src = src.replace(probe, "", 1)

    path_start = src.index("void FirstAudioProcessor::processBlock "
                           "(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)")
    path_end = brace_end(src, src.index("void FirstAudioProcessor::processTapeEngine "
                                        "(juce::dsp::AudioBlock<float> block,", path_start))
    signal_path = src[path_start:path_end]
    src = src[:path_start] + src[path_end:]

    if src == original:
        raise SystemExit("split_processor_path: nothing was cut; the anchors no longer match")

    # The residual file's own two edits, both of them consequences of the move rather
    # than of it being tidy: it still calls two of the shapers (from the preparation
    # code that stayed), and the four cuts left the blank lines that separated the
    # removed sections stacked up.
    src = replace_once(src, '''#include "ProcessorStateFormat.h"
#include "ProcessorPresets.h"

#include <array>''', '''#include "ProcessorStateFormat.h"
#include "ProcessorPresets.h"

// The five inline shapers the preparation code below builds its coefficients with.
// They are called from the signal path as well, which is why they are a header
// rather than a file-local block in either - see its own banner.
#include "DSP/HotPathMath.h"

#include <array>''', "the include of the shapers' header")

    src = replace_once(src, "        return ids;\n    }\n\n\n\n    /**\n        Subharmonic generator",
                       "        return ids;\n    }\n\n    /**\n        Subharmonic generator",
                       "the blank lines the shapers left")

    src = replace_once(src, "    return true;\n  #endif\n}\n#endif\n\n\n\n\n//==============================================================================\n"
                       "FirstAudioProcessor::TelemetryFrame FirstAudioProcessor::getTelemetry() const",
                       "    return true;\n  #endif\n}\n#endif\n\n\n//==============================================================================\n"
                       "FirstAudioProcessor::TelemetryFrame FirstAudioProcessor::getTelemetry() const",
                       "the blank lines the signal path left")

    # `inline` on the four counters, because they are the one thing here that is not a
    # pure function: the signal path writes them and the readout below reads them.
    atomics = atomics.replace("    std::atomic<int> hotPathSampleCount { 0 };",
                              "    inline std::atomic<int> hotPathSampleCount { 0 };", 1)
    atomics = atomics.replace("    std::atomic<int> hotPathActiveSampleCount { 0 };",
                              "    inline std::atomic<int> hotPathActiveSampleCount { 0 };", 1)
    atomics = atomics.replace("    std::atomic<bool> hotPathThisSampleActive { false };",
                              "    inline std::atomic<bool> hotPathThisSampleActive { false };", 1)
    atomics = atomics.replace("    std::atomic<bool> hotPathThisBlockActive { false };",
                              "    inline std::atomic<bool> hotPathThisBlockActive { false };", 1)
    if atomics.count("inline std::atomic") != 4:
        raise SystemExit("split_processor_path: not all four counters were made inline")

    # The one sentence inside a moved comment that named a position in the old file.
    atomics = atomics.replace("hotPathProbeSnapshot() at the bottom of this file",
                              "hotPathProbeSnapshot() at the bottom of this header", 1)

    # The readout's own guard goes: the whole body of the header is behind the same
    # condition now, and a second, identical one around a namespace inside it is a
    # line a reader has to check for no reason. The note that says so travels with it.
    probe = probe.replace(
        "#if ! defined (J37_DSP_HARNESS) && defined (J37_ENABLE_HOTPATH_PROBE)\n"
        "// ===========================================================================\n",
        "// ===========================================================================\n", 1)
    probe = probe[:probe.rindex("}\n#endif\n")] + "}\n"

    # ... and so does the counters' own, which becomes the one guard the whole header
    # sits inside (the banner above carries the `#if` line itself).
    atomics = atomics.replace(
        "#if ! defined (J37_DSP_HARNESS) && defined (J37_ENABLE_HOTPATH_PROBE)\n", "", 1)
    atomics = atomics[:atomics.rindex("#endif\n")].rstrip("\n") + "\n"

    # The one code change the move carries. Compiling the probe's own branch
    # (`-DJ37_ENABLE_HOTPATH_PROBE=1`) turned it up in the .cpp: an atomic was being
    # copy-assigned from another atomic, which is a deleted function - so the opt-in
    # probe the counters were declared for did not compile even after the counters
    # existed. Found by the move, fixed by it, and checked the same way afterwards.
    signal_path = signal_path.replace(
        "    hotPathThisBlockActive = hotPathThisSampleActive;\n"
        "    if (hotPathThisSampleActive)\n",
        "    hotPathThisBlockActive.store (hotPathThisSampleActive.load());\n"
        "    if (hotPathThisSampleActive.load())\n", 1)
    if ".store (" not in signal_path:
        raise SystemExit("split_processor_path: the probe's block assignment was not found")

    # No dedent for either block: the counters sit at four spaces under the guard's
    # `#if` exactly as they did under the .cpp's, and the readout was already written
    # at file scope. Only the shapers' indentation changes.
    (REPO / "Source" / "HotPathProbe.h").write_text(
        HOT_PATH_PROBE_BANNER + atomics + "\n" + probe
        + "\n#endif  // J37_ENABLE_HOTPATH_PROBE\n")
    (REPO / "Source" / "DSP" / "HotPathMath.h").write_text(
        HOT_PATH_MATH_BANNER + dedent(shapers))
    (REPO / "Source" / "ProcessorSignalPath.cpp").write_text(
        SIGNAL_PATH_BANNER + signal_path)
    PROCESSOR.write_text(src)

    print(f"wrote Source/HotPathProbe.h: {len(atomics.splitlines()) + len(probe.splitlines())} moved lines")
    print(f"wrote Source/DSP/HotPathMath.h: {len(shapers.splitlines())} moved lines")
    print(f"wrote Source/ProcessorSignalPath.cpp: {len(signal_path.splitlines())} moved lines")
    print(f"PluginProcessor.cpp: {len(original.splitlines())} -> {len(src.splitlines())} lines")
    return 0


HOT_PATH_PROBE_BANNER = """// =============================================================================
//  The hot-path probe: four counters the per-sample loop increments, and the
//  readout that turns them into a snapshot.
//
//  Moved out of PluginProcessor.cpp, prose and all, when the signal path left for
//  Source/ProcessorSignalPath.cpp. It is a header because the probe is one mechanism
//  split across two translation units: the loop that increments the counters is in
//  the signal path, and the snapshot is called from the processor's telemetry
//  readout. A file-local copy in each would be two different probes.
//
//  Everything here is behind `J37_ENABLE_HOTPATH_PROBE`, which no build defines -
//  the probe is instrumentation for a benchmark or an audit, never a shipping path,
//  and the guard is what keeps it out of the audio code when it is off. What the move
//  cost is two spellings and one sentence:
//
//    * The four counters are `inline` VARIABLES rather than file-local objects. That
//      is the whole reason they could not stay in the .cpp: one translation unit
//      writes them and another reads them, and `inline` is what makes those the same
//      four objects instead of one set per unit. The shapers in
//      Source/DSP/HotPathMath.h are `inline` FUNCTIONS by contrast - pure, so copies
//      of them would be harmless, while copies of these would silently read zero.
//    * The two blocks' own `#if` lines are gone in favour of ONE guard around the
//      whole body. In the .cpp they carried identical guards of their own, far apart
//      from each other in a 5,000 line file; here they are the two halves of one
//      mechanism in one header, and a second guard inside the first is a line a
//      reader has to check for no reason. The readout's comment about not having a
//      condition of its own is still true and still here.
//    * The comment above the counters said the readout sits "at the bottom of this
//      file". It says "header" now, which is where it is.
//
//  The indentation is the .cpp's, unchanged: the counters sat at four spaces under
//  the .cpp's guard and do the same under this one, and the readout was already at
//  file scope.
//
//  ONE CODE CHANGE came out of the move, and it is the sort this project keeps
//  looking for: with the probe's own flag defined (`-DJ37_ENABLE_HOTPATH_PROBE=1`,
//  which no build defines and no CI job sets), `hotPathThisBlockActive =
//  hotPathThisSampleActive;` is a copy assignment between two atomics - a deleted
//  function. The probe was declared so that turning the documented flag on would
//  compile, and it still did not. Both loads and the store spell out what they mean
//  now, and the flag's own branch is compiled on every change to these two files:
//  it is one -fsyntax-only pass with the macro defined, and it is what would have
//  caught this in the first place.
// =============================================================================
#pragma once

#include <atomic>

// One guard for the whole probe. In PluginProcessor.cpp the counters and the readout
// carried identical guards of their own, written apart from each other in a 5,000
// line file; here they are the two halves of one mechanism in one header, and a
// build that has the flag has both.
#if ! defined (J37_DSP_HARNESS) && defined (J37_ENABLE_HOTPATH_PROBE)

"""

HOT_PATH_MATH_BANNER = """// =============================================================================
//  The hot path's scalar helpers: the shaper the magnetic stage calls, the two
//  one-pole coefficient builders, the output soft clipper and the soft-knee gain
//  computer the glue stages use.
//
//  Moved out of PluginProcessor.cpp, prose and all, when the signal path left for
//  Source/ProcessorSignalPath.cpp. They are a header because they are called from
//  BOTH halves of what that move separated - processTapeEngine runs them per sample,
//  and resetSampleRateDependentState() and updateToneCoefficients() build their
//  coefficients with the same two functions - so they could travel with neither.
//
//  This is the last of what tests/dsp/extract.py used to CUT out of
//  PluginProcessor.cpp. The harness includes this header now, which is the same
//  result the stage headers under Source/DSP/ already had, all 23 of them: what the harness
//  measures is the shipping text rather than a copy of it.
//
//  The conventions of the directory apply, and they are stated in J37Math.h: one
//  concern per header, the header includes what it needs, and it does not pull JUCE
//  in - what these name is `juce::jmax`, `juce::jlimit` and `juce::MathConstants`,
//  which the plugin's own JuceHeader.h declares and which the DSP harness's shim
//  declares in its own words. J37_HAS_XSIMD comes from the probe beside it, included
//  here for the reason OptionalLibraries.h gives: a header that reads a macro has to
//  be able to find the file that decides it.
//
//  The five bodies are unchanged, including their comments. The only edit the move
//  made is the four spaces of indentation the .cpp's anonymous namespace gave them,
//  removed because there is no enclosing namespace here.
// =============================================================================
#pragma once

#include <cmath>

#include "OptionalLibraries.h"

"""

SIGNAL_PATH_BANNER = """// =============================================================================
//  The signal path: everything that runs between the host's block and the machine's
//  output.
//
//  Moved out of PluginProcessor.cpp whole - 3,870 lines of it, in order - because
//  that file is the processor's LIFECYCLE (what it is constructed with, what a
//  session saves, what the host asks it) and this is what it does to the audio. The
//  two are read for different reasons and by different people, and the one that
//  dominates the file by length was the one nobody could find anything in.
//
//  What is here, in the order the engine runs it:
//
//    processBlock()              the host entry point: pick the oversampling engine,
//                                hand the block to processTapeEngine()
//    setTransportState()         the public transport command, and the host sync it
//    flushPendingTransportHostSync()  flushes that the editor asks for
//    setSpindownHeld()           the platter brake, held from the panel
//    processBypassedBlock()      what the machine does in BYPASS and DELTA
//    processPostMachineStages()  the EQ pair, the IR and the neural stage after it
//    scaleOffsetsForMode()       the twelve scale degrees autotune snaps to
//    processAutoTuneSample()     the optional pitch correction, one frame at a time
//    publishBlockTelemetry()     what the panel's meters read, published per block
//    processTapeEngine()         the machine: 3,366 lines of the tape model
//
//  Nothing was retyped. The spans came out of PluginProcessor.cpp by matching their
//  exact bytes and went in unchanged, so every comment that explains why a stage is
//  shaped the way it is came along with it.
//
//  Two headers came out of the same move, because the helpers this file calls are
//  also called by code that stayed behind: Source/DSP/HotPathMath.h holds the five
//  inline shapers (the preparation code builds its one-pole coefficients with the
//  same two functions), and Source/HotPathProbe.h holds the probe counters the hot
//  loop increments and the processor's telemetry readout inspects. Both say in their
//  own banners why they had to stop being file-local.
//
//  processTapeEngine() is still one function, and that is deliberate rather than left
//  over: splitting it into named stages is the change that would most improve this
//  project's readability, and it is also the one no harness covers today - see
//  SPLITTING-BIG-FILES.md, section F. Moving it to a file of its own is the part that
//  needs no new measurement.
// =============================================================================
#include "PluginProcessor.h"

#include "DSP/HotPathMath.h"
#include "HotPathProbe.h"

"""


if __name__ == "__main__":
    sys.exit(main())
