#!/usr/bin/env bash
# Shared preamble for every harness under tests/. SOURCE it; never run it.
#
#   source "${HERE}/../lib/harness.sh"
#
# Twenty-eight scripts used to derive the same five facts by hand: the repository
# root, the compiler, the sanitizer flags, the build directory, and where each
# dependency's headers are. The last one is the reason this file exists. When
# signalsmith-linear stopped being a directory inside the stretch checkout and
# became a sibling checkout of its own, that single fact had to be corrected in
# three harnesses in three different syntaxes - and one of the three failures was
# silent, because a comment had been placed inside a backslash-continued argument
# list. CMake already knew the answer; the harnesses kept their own copies.
#
# What this file deliberately does NOT do is decide which optional libraries a
# harness sees. Several harnesses compile the same source twice - once with
# dr_libs or xsimd on the include path and once without - to pin BOTH branches of
# a J37_HAS_* decision. A fixed include list would collapse that pair into one
# shape and quietly delete half of what the harness asserts, so includes are
# asked for by name, per binary: -I "$(j37_path dr_libs)".
#
# Keeping the invocation style is deliberate too: `bash tests/pitch/run.sh` still
# works, still needs no CMake configure, and still finishes in seconds.

J37_HARNESS_PREAMBLE=1

J37_TESTS="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
J37_REPO="$(cd "${J37_TESTS}/.." && pwd)"
J37_SOURCE="${J37_REPO}/Source"
J37_THIRDPARTY="${J37_REPO}/ThirdParty"

# The historical names still resolve, because the harnesses and their comments
# refer to them: REPO is the repository root, CXX/CC the compilers with the same
# defaults they always had.
REPO="${J37_REPO}"
CXX="${CXX:-g++}"
CC="${CC:-cc}"

# J37_SANITIZE=address,undefined is what build-linux-debug passes; every harness
# that compiles a binary adds "${sanitize_flags[@]}" to the command line.
sanitize_flags=()
if [ -n "${J37_SANITIZE:-}" ]; then
    sanitize_flags=("-fsanitize=${J37_SANITIZE}" "-fno-sanitize-recover=all"
                    "-fno-omit-frame-pointer" "-g")
    echo "sanitizers: ${J37_SANITIZE}"
fi

# j37_path <name> - the directory holding that dependency's headers, which is the
# one fact that used to be spelled out per harness. An unknown name is an error
# rather than an empty string, so a typo fails the harness instead of dropping an
# include path and testing something else.
j37_path() {
    case "${1:-}" in
        repo)                 printf '%s' "${J37_REPO}" ;;
        tests)                printf '%s' "${J37_TESTS}" ;;
        source)               printf '%s' "${J37_SOURCE}" ;;
        source/dsp)           printf '%s' "${J37_SOURCE}/DSP" ;;
        juce)                 printf '%s' "${J37_REPO}/JUCE/modules" ;;
        # The parent of every vendored checkout. A quoted include written as
        # "signalsmith-linear/stft.h" resolves against the INCLUDING file's
        # directory first, so this is what answers it now that linear is a
        # sibling checkout rather than a directory inside the stretch one.
        thirdparty)           printf '%s' "${J37_THIRDPARTY}" ;;
        farbot)               printf '%s' "${J37_THIRDPARTY}/farbot/include" ;;
        pocketfft)            printf '%s' "${J37_THIRDPARTY}/pocketfft" ;;
        glm)                  printf '%s' "${J37_THIRDPARTY}/glm" ;;
        nlohmann_json)        printf '%s' "${J37_THIRDPARTY}/nlohmann_json/single_include" ;;
        nlohmann_json/json)   printf '%s' "${J37_THIRDPARTY}/nlohmann_json/single_include/nlohmann" ;;
        nlohmann_json/embedded) printf '%s' "${J37_THIRDPARTY}/RTNeural/modules/json" ;;
        xsimd)                printf '%s' "${J37_THIRDPARTY}/xsimd/include" ;;
        rtneural)             printf '%s' "${J37_THIRDPARTY}/RTNeural" ;;
        dr_libs)              printf '%s' "${J37_THIRDPARTY}/dr_libs" ;;
        dattorro)             printf '%s' "${J37_THIRDPARTY}/dattorro-verb" ;;
        ebur128)              printf '%s' "${J37_THIRDPARTY}/libebur128" ;;
        eigen)                printf '%s' "${J37_THIRDPARTY}/eigen" ;;
        namcore)              printf '%s' "${J37_THIRDPARTY}/namcore" ;;
        signalsmith-stretch)  printf '%s' "${J37_THIRDPARTY}/signalsmith-stretch/include" ;;
        *)
            printf 'j37_path: unknown dependency "%s"\n' "${1:-}" >&2
            return 1
            ;;
    esac
}

# j37_build <name> - a scratch directory for one harness, created on the spot.
j37_build() {
    local dir="${TMPDIR:-/tmp}/${1}"
    mkdir -p "${dir}"
    printf '%s' "${dir}"
}
