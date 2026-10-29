#!/usr/bin/env bash
#
# Builds and runs the neural stage harness.
#
#   tests/neural/run.sh
#
# The harness has no JUCE, no host and no shim for the model libraries: it cuts
# the shipping RateBridge out of Source/PluginProcessor.h with tests/dsp/extract.py
# - the same extraction the DSP harness uses, so there is one copy of it in the
# tree - and drives it with generated signal at the rate pairs the engine can
# actually produce. That makes it seconds of work on any machine with a C++20
# compiler, which is why it can run on every pull request instead of only where
# the plugin is built.
#
# It exists because the defect it checks is invisible in the source and inaudible
# in a build log: the neural stage ran its model at whatever rate the ENGINE
# happened to be at, while a .nam model carries the rate it was TRAINED at. A
# network's memory is counted in samples rather than seconds, so those two are the
# same model only when the numbers agree - and nothing about the code said
# otherwise.
#
# Five binaries, because the stage has five questions to answer:
#
#   rate_test     drives RateBridge on its own - plain arithmetic, no model, no
#                 library;
#   stage_test    compiles the same extracted source with RTNeural's API stood in
#                 for, which is the only thing on this machine that exercises the
#                 lines inside `#if J37_HAS_RTNEURAL` with no library fetched;
#   fixture_test  loads the .nam FIXTURES in tests/neural/fixtures/ against the
#                 REAL vendored RTNeural AND the REAL vendored NAM Core, drives
#                 both engines, and compares them with the reference player's own
#                 output - the WP3 gate;
#   fixture_test_no_namcore
#                 the same file compiled WITHOUT NAM Core, which is the shipping
#                 default (`J37_NAMCORE` is OFF). It asserts that WaveNet is
#                 refused and that the LSTM path is untouched, which is the other
#                 half of "a build without the library compiles the same stage it
#                 always did";
#   nam_only_test
#                 NAM Core WITHOUT RTNeural, which is the third shape of
#                 NeuralStage's dispatch: there is no converter, so the LSTM has to
#                 go to NAM Core too or the one architecture this stage has always
#                 supported would stop loading the day RTNeural was pruned.
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

# The two inference engines, vendored under ThirdParty/ (see
# cmake/J37Dependencies.cmake for the pins and the licences). NAM Core needs all
# three: its own headers, Eigen, and a directory holding json.hpp by that bare
# name - which is the nlohmann target's include directory one level down.
RTNEURAL_ROOT="${HERE}/../../ThirdParty/RTNeural"
NAM_ROOT="${HERE}/../../ThirdParty/namcore"
EIGEN_ROOT="${HERE}/../../ThirdParty/eigen"
NLOHMANN_ROOT="${HERE}/../../ThirdParty/nlohmann_json/single_include"
NAM_JSON_INCLUDE="${NLOHMANN_ROOT}/nlohmann"

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
    -I "${NLOHMANN_ROOT}" \
    "${HERE}/stage_test.cpp" -o "${BUILD}/stage_test"

"${BUILD}/stage_test"

# -----------------------------------------------------------------------------
#  NAM Core, compiled once and linked into the fixture test.
#
#  This is the one place in the harness where a third-party library is BUILT
#  rather than included: NAM Core is .cpp files, and the fixtures cannot be
#  loaded without them. The source list is NAM's own (`NAM/*.cpp` plus
#  `NAM/*/*.cpp`, which is what upstream's tools/CMakeLists.txt globs), and it is
#  compiled here rather than through NAM's CMake because that file is a standalone
#  project with its own flags and tools - see cmake/J37Dependencies.cmake for the
#  long version.
#
#  NAM_ENABLE_A2_FAST is deliberately not defined: without it the generic WaveNet
#  handles every model, which is the single code path these fixtures measure.
#
#  The objects go on the LINK LINE, not into an archive. NAM Core registers each
#  architecture with a file-scope static object in its own .cpp, and get_dsp()
#  reaches them only through the registry - no architecture-owning object is
#  referenced by name, so an archive would be pruned and every model would fail
#  at runtime with "No config parser registered for architecture: LSTM" after
#  linking cleanly. cmake/J37Dependencies.cmake declares the same set as an
#  OBJECT library for exactly this reason.
#
#  -w is for the library's own sources; the headers are -isystem at the call site,
#  so a diagnostic inside them is theirs. -Wall -Wextra still apply to the harness
#  and to every plugin header it pulls in, which is this repository's half.
# -----------------------------------------------------------------------------
NAM_BUILD="${BUILD}/namcore"
mkdir -p "${NAM_BUILD}"

# Sixteen translation units, each pulling in Eigen - a minute of work serially,
# and the ASan/UBSan run in CI pays it after the plain one. They are independent,
# so they are compiled in waves of one per core. The wave-based pool is bash 3.2
# compatible on purpose (`wait -n` is 4.3+ and macOS's /bin/bash is 3.2), and the
# wave is waited for before the next one starts, so a failure stops the script
# with set -e rather than being swallowed.
nam_jobs=1
if command -v nproc >/dev/null 2>&1; then
    nam_jobs="$(nproc)"
elif command -v sysctl >/dev/null 2>&1; then
    nam_jobs="$(sysctl -n hw.ncpu 2>/dev/null || echo 1)"
fi
case "${nam_jobs}" in
    ''|*[!0-9]*) nam_jobs=1 ;;
esac
[ "${nam_jobs}" -ge 1 ] || nam_jobs=1

nam_objects=()
nam_wave=()

wait_for_wave() {
    for pid in "${nam_wave[@]}"; do
        wait "${pid}"
    done
    nam_wave=()
}

for source in "${NAM_ROOT}"/NAM/*.cpp "${NAM_ROOT}"/NAM/*/*.cpp; do
    object="${NAM_BUILD}/$(basename "${source}" .cpp).o"
    nam_objects+=("${object}")

    "${CXX}" -std=c++20 -O2 "${sanitize_flags[@]}" -w \
        -isystem "${NAM_ROOT}" -isystem "${EIGEN_ROOT}" -isystem "${NAM_JSON_INCLUDE}" \
        -c "${source}" -o "${object}" &
    nam_wave+=("$!")

    if [ "${#nam_wave[@]}" -ge "${nam_jobs}" ]; then
        wait_for_wave
    fi
done

wait_for_wave

# -----------------------------------------------------------------------------
#  The .nam FIXTURES, loaded and processed - WP3's gate.
#
#  The two binaries above never read a model file: one has no library at all and
#  the other has RTNeural's API stood in, so neither can answer "does a real
#  .nam document load, and does the code that consumes it render what the
#  reference player renders?" This pair answers that:
#
#    * fixture_test links BOTH engines and is the differential build: the LSTM
#      fixture through the RTNeural converter and through NAM Core's own LSTM,
#      and the WaveNet fixture through NAM Core, all against the recorded oracle.
#    * fixture_test_no_namcore is the default build (J37_NAMCORE OFF): the same
#      file, same fixtures, and WaveNet refused - which is what keeps the
#      without-the-library branch compiled by something.
#
#  The fixtures directory is passed in rather than guessed, so the binary fails
#  loudly if it is run from somewhere else rather than silently finding nothing.
#
#  RTNeural's modules/json is deliberately NOT on the include path: it is a
#  different nlohmann version (3.11.1) from the vendored 3.12.0, RTNeural reaches
#  its copy by relative path anyway, and two copies in one translation unit would
#  give NAM Core's `const nlohmann::json&` parameters a different mangled name
#  from the one this TU calls. Source/DSP/OptionalLibraries.h includes the
#  vendored header ahead of model_loader.h for the same reason.
# -----------------------------------------------------------------------------
"${CXX}" -std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-cpp \
    "${sanitize_flags[@]}" \
    -DJ37_NAMCORE=1 \
    -I "${HERE}/../dsp" -I "${SOURCE}" -I "${SOURCE}/DSP" \
    -isystem "${RTNEURAL_ROOT}" \
    -isystem "${NLOHMANN_ROOT}" \
    -isystem "${NAM_ROOT}" -isystem "${EIGEN_ROOT}" -isystem "${NAM_JSON_INCLUDE}" \
    "${HERE}/fixture_test.cpp" "${SOURCE}/DSP/NamCoreRegistration.cpp" \
    "${nam_objects[@]}" -o "${BUILD}/fixture_test"

"${BUILD}/fixture_test" "${HERE}/fixtures"

"${CXX}" -std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-cpp \
    "${sanitize_flags[@]}" \
    -I "${HERE}/../dsp" -I "${SOURCE}" -I "${SOURCE}/DSP" \
    -isystem "${RTNEURAL_ROOT}" \
    -isystem "${NLOHMANN_ROOT}" \
    "${HERE}/fixture_test.cpp" -o "${BUILD}/fixture_test_no_namcore"

"${BUILD}/fixture_test_no_namcore" "${HERE}/fixtures"

# -----------------------------------------------------------------------------
#  And the third shape of the dispatch: NAM Core with NO RTNeural.
#
#  Not on the include path at all, and `-DJ37_HAS_RTNEURAL=0` so the probe cannot
#  turn it on by accident - the point is the build where the LSTM converter does
#  not exist. Source/DSP/OptionalLibraries.h honours a macro that is already
#  defined, which is what makes that possible without moving ThirdParty/RTNeural.
# -----------------------------------------------------------------------------
"${CXX}" -std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-cpp \
    "${sanitize_flags[@]}" \
    -DJ37_HAS_RTNEURAL=0 -DJ37_NAMCORE=1 \
    -I "${HERE}/../dsp" -I "${SOURCE}" -I "${SOURCE}/DSP" \
    -isystem "${NLOHMANN_ROOT}" \
    -isystem "${NAM_ROOT}" -isystem "${EIGEN_ROOT}" -isystem "${NAM_JSON_INCLUDE}" \
    "${HERE}/nam_only_test.cpp" "${SOURCE}/DSP/NamCoreRegistration.cpp" \
    "${nam_objects[@]}" -o "${BUILD}/nam_only_test"

"${BUILD}/nam_only_test" "${HERE}/fixtures"
