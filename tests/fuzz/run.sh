#!/usr/bin/env bash
#
# The WAV decoder's fuzz gate: tests/fuzz/fuzz_noise_decoder.cpp, run twice.
#
#   tests/fuzz/run.sh                       # the sweep, plus a bounded fuzz run
#   J37_FUZZ_SECONDS=120 tests/fuzz/run.sh  # a longer fuzz run, for a nightly
#
# Why this is not part of tests/dsp: the decoder's own harness checks the FILES
# it was written against. This one feeds it bytes nobody wrote - truncated
# headers, a channel count of zero, a data chunk claiming more frames than it
# holds - and holds every outcome to the contract in NoiseDecoder.h: a refusal
# names its reason and touches nothing, an acceptance is playable (a non-zero
# length, both channels that long, a positive rate, every sample finite), and the
# same bytes decode the same way twice.
#
# Two builds, one source, and the difference is the MUTATOR rather than the code
# under test:
#
#   g++       the file's own `main`: a fixed, reproducible sweep of mutations, so
#             a failure names an index that can be rebuilt by hand. This half must
#             run everywhere, so it is built here with the compiler tests/dsp uses.
#   clang++   `-fsanitize=fuzzer`: the same assertions, driven by coverage instead
#             of by a list, for a bounded number of seconds. The corpus it starts
#             from is the sweep's own three seeds. Skipped, with a line in the log,
#             on a compiler that has no libFuzzer - which is every MSVC and every
#             GCC, and is why the sweep is the half that must not be optional.
#
# Neither half links into the plugin, and neither is a runtime dependency: this is
# dev-and-CI evidence, like Catch2's suite and the libebur128 oracle next door.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${TMPDIR:-/tmp}/j37-fuzz-noise"
CXX="${CXX:-g++}"
SOURCE="${HERE}/../../Source"
DR_LIBS="${HERE}/../../ThirdParty/dr_libs"

mkdir -p "${BUILD}"

# dr_wav is vendored, so a checkout is enough - the same reason tests/dsp builds
# the decoder with and without it. A pruned ThirdParty/ skips this harness rather
# than failing a build over a tool that never ships.
if [ ! -f "${DR_LIBS}/dr_wav.h" ]; then
    echo "  skip  WAV decoder fuzz gate: ThirdParty/dr_libs is absent"
    exit 0
fi

SANITIZE="${J37_SANITIZE:-address,undefined}"
sanitize_flags=("-fsanitize=${SANITIZE}" "-fno-sanitize-recover=all"
                "-fno-omit-frame-pointer" "-g")
echo "sanitizers: ${SANITIZE}"

INCLUDES=(-I "${HERE}" -I "${SOURCE}" -I "${DR_LIBS}")

echo ""
echo "== the fixed sweep (g++/CXX, ${CXX})"
"${CXX}" -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter \
    "${sanitize_flags[@]}" "${INCLUDES[@]}" \
    "${HERE}/fuzz_noise_decoder.cpp" "${SOURCE}/DSP/NoiseDecoder.cpp" \
    -o "${BUILD}/fuzz_noise_decoder"

mkdir -p "${BUILD}/corpus"
"${BUILD}/fuzz_noise_decoder" --dump-seeds "${BUILD}/corpus"
"${BUILD}/fuzz_noise_decoder"

# Coverage-guided half. The probe is the library, not the compiler's name: a
# clang without libFuzzer's runtime links this and fails, which would be a gate
# that breaks on a tool it does not need.
#
# J37_FUZZ_COVERAGE=0 skips this half and keeps the sweep. It exists because the
# shape the sanitizer pass wants ("every harness under ASan and UBSan") is the
# sweep, and the coverage run already has its own step in the regression job. The
# first CI run that had both proved why a switch is worth having: the fuzzer was
# still inside one execution twenty minutes later, waiting on a symboliser, and a
# gate that can hold a job hostage is worse than one that says what it covers.
#
# Three bounds on top of that, none of them optional:
#   -timeout=10          libFuzzer's own per-execution limit: a single input that
#                        runs for ten seconds is a finding, not a corpus entry.
#   detect_leaks=0       LeakSanitizer's exit-time pass buys nothing here (every
#                        allocation is inside one decode) and is the usual reason
#                        an ASan process does not return.
#   timeout N+120        the belt to those braces: whatever the fuzzer thinks, this
#                        script returns. A run that trips it FAILS the gate with
#                        its log rather than hanging the job silently.
CLANG="$(command -v clang++ || true)"
SECONDS_TO_FUZZ="${J37_FUZZ_SECONDS:-20}"
FUZZ_COVERAGE="${J37_FUZZ_COVERAGE:-1}"

if [ "${FUZZ_COVERAGE}" = "0" ]; then
    echo ""
    echo "  skip  coverage-guided run: J37_FUZZ_COVERAGE=0 (the sweep above is the sanitizer pass)"
elif [ -n "${CLANG}" ] \
   && printf 'extern "C" int LLVMFuzzerTestOneInput(const unsigned char*, unsigned long) { return 0; }\n' \
      | "${CLANG}" -x c++ -fsanitize=fuzzer -o "${BUILD}/fuzzer_probe" - > /dev/null 2>&1; then
    rm -f "${BUILD}/fuzzer_probe"

    echo ""
    echo "== the coverage-guided run (${CLANG}, ${SECONDS_TO_FUZZ}s)"

    "${CLANG}" -std=c++17 -O1 -g -DJ37_FUZZ_LIBFUZZER \
        -fsanitize=fuzzer,address -fno-omit-frame-pointer \
        "${INCLUDES[@]}" \
        "${HERE}/fuzz_noise_decoder.cpp" "${SOURCE}/DSP/NoiseDecoder.cpp" \
        -o "${BUILD}/fuzz_noise_decoder_libfuzzer"

    # -rss_limit_mb keeps a header claiming a colossal frame count from taking the
    # machine down; a decoder that gets that far is a finding either way, and the
    # cap turns it into a reported OOM instead of an unrecoverable host. The corpus
    # directory is written by the run and is what a later run starts from.
    set +e
    ASAN_OPTIONS=detect_leaks=0:symbolize=1 \
    timeout $((SECONDS_TO_FUZZ + 120)) \
        "${BUILD}/fuzz_noise_decoder_libfuzzer" \
        -max_total_time="${SECONDS_TO_FUZZ}" -timeout=10 -rss_limit_mb=2048 \
        -print_final_stats=1 "${BUILD}/corpus" \
        > "${BUILD}/libfuzzer.log" 2>&1
    status=$?
    set -e

    tail -12 "${BUILD}/libfuzzer.log"

    if [ "${status}" -eq 124 ]; then
        echo ""
        echo "fuzz_noise_decoder: the fuzzer did not return inside its budget - log:" >&2
        tail -40 "${BUILD}/libfuzzer.log" >&2
        exit 1
    elif [ "${status}" -ne 0 ] \
       && grep -q "ERROR: libFuzzer: deadly signal\|ERROR: AddressSanitizer\|SUMMARY: \|ERROR: libFuzzer: out-of-memory\|ERROR: libFuzzer: timeout\|crash-\|timeout-" "${BUILD}/libfuzzer.log"; then
        echo ""
        echo "fuzz_noise_decoder: the fuzzer found a failure - full log below" >&2
        cat "${BUILD}/libfuzzer.log" >&2
        exit 1
    elif [ "${status}" -ne 0 ]; then
        echo ""
        echo "fuzz_noise_decoder: libFuzzer exited ${status} without a finding; log:" >&2
        tail -40 "${BUILD}/libfuzzer.log" >&2
        exit 1
    fi

    echo "  ok   ${SECONDS_TO_FUZZ}s of coverage-guided input, no crash and no contract violation"
else
    echo ""
    echo "  skip  coverage-guided run: no clang++ with -fsanitize=fuzzer on this machine"
    echo "         (the sweep above is the half that runs everywhere)"
fi
