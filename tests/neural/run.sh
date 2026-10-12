#!/usr/bin/env bash
#
# Builds and runs the neural rate-bridge harness.
#
#   tests/neural/run.sh
#
# The harness has no JUCE, no RTNeural, no model and no host: it cuts the shipping
# RateBridge out of Source/PluginProcessor.h with tests/dsp/extract.py - the same
# extraction the DSP harness uses, so there is one copy of it in the tree - and
# drives it with generated signal at the rate pairs the engine can actually
# produce. That makes it seconds of work on any machine with a C++17 compiler,
# which is why it can run on every pull request instead of only where the plugin is
# built.
#
# It exists because the defect it checks is invisible in the source and inaudible
# in a build log: the neural stage ran its model at whatever rate the ENGINE
# happened to be at, while a .nam model carries the rate it was TRAINED at. A
# network's memory is counted in samples rather than seconds, so those two are the
# same model only when the numbers agree - and nothing about the code said
# otherwise.
#
# Two binaries, because the stage has two halves: rate_test drives RateBridge on
# its own - plain arithmetic, no model, no library - and stage_test compiles the
# same extracted source with RTNeural's API stood in for, which is the only thing
# on this machine that exercises the lines inside `#if J37_HAS_RTNEURAL`.
#
# J37_SANITIZE=address,undefined rides along with the debug build in the workflow,
# the same way the other harnesses do.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${TMPDIR:-/tmp}/j37-neural-test"
CXX="${CXX:-g++}"

# The stages extracted by the DSP harness that already live in their own headers
# under Source/DSP/ are included from there rather than cut out of
# PluginProcessor.h, so Source/ has to be on the include path - the same reason
# tests/dsp/run.sh puts it there.
SOURCE="${HERE}/../../Source"

mkdir -p "${BUILD}"

# The DSP harness's extractor, which cuts RateBridge out with the rest of the
# engine's stages and runs its own checks on the way past. A second extractor for
# one struct would be a second thing to keep in step.
python3 "${HERE}/../dsp/extract.py" "${BUILD}/extracted_dsp.inc"

sanitize_flags=()
if [ -n "${J37_SANITIZE:-}" ]; then
    sanitize_flags=("-fsanitize=${J37_SANITIZE}" "-fno-sanitize-recover=all"
                    "-fno-omit-frame-pointer" "-g")
    echo "sanitizers: ${J37_SANITIZE}"
fi

"${CXX}" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
    "${sanitize_flags[@]}" \
    -I "${HERE}/../dsp" -I "${BUILD}" -I "${SOURCE}" \
    "${HERE}/rate_test.cpp" -o "${BUILD}/rate_test"

"${BUILD}/rate_test"

# The stage itself, compiled the other way round: with the library's own API stood
# in for and J37_HAS_RTNEURAL 1, so the half of NeuralStage that the DSP harness can
# never reach - everything inside the guard, which is where the rate conversion now
# lives - is compiled and driven rather than merely written. nlohmann/json is the
# real one, straight out of ThirdParty/; only RTNeural's two-method API surface and
# its one-function loader are stood in for.
"${CXX}" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
    "${sanitize_flags[@]}" \
    -DJ37_HAS_RTNEURAL=1 \
    -I "${HERE}/../dsp" -I "${BUILD}" -I "${SOURCE}" \
    -I "${HERE}/../../ThirdParty/nlohmann_json/single_include" \
    "${HERE}/stage_test.cpp" -o "${BUILD}/stage_test"

"${BUILD}/stage_test"
