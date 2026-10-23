#!/usr/bin/env bash
#
# Builds and runs the room harness.
#
#   tests/reverb/run.sh
#
# Source/DSP/DattorroRoom.h is the plugin's reverb stage, and the tank behind it
# is el-visio/dattorro-verb - a C library, compiled here exactly as the plugin
# compiles it: verb.c as C, linked against the header compiled as C++. That is
# the other half of what this script checks. The stage wraps the library's API in
# extern "C"; if that ever went missing, every other harness would still pass and
# the platform link would be the first thing to notice.
#
# The library is not copied: ThirdParty/dattorro-verb/ is the vendored pin the
# CMake build compiles, so the test and the plugin measure the same verb.c.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="${HERE}/../.."
LIBRARY="${REPO}/ThirdParty/dattorro-verb"
BUILD="${TMPDIR:-/tmp}/j37-reverb-test"
CXX="${CXX:-g++}"
CC="${CC:-cc}"

if [ ! -f "${LIBRARY}/verb.c" ]; then
    echo "reverb harness: ${LIBRARY}/verb.c is missing - the vendored library is not" \
         "checked out" >&2
    exit 1
fi

mkdir -p "${BUILD}"

# J37_SANITIZE=address,undefined puts the harness under those sanitizers, the same
# switch the other harnesses take. A feedback network with a ring of doubles behind
# it is exactly the shape an out-of-bounds read hides in.
sanitize_flags=()
if [ -n "${J37_SANITIZE:-}" ]; then
    sanitize_flags=("-fsanitize=${J37_SANITIZE}" "-fno-sanitize-recover=all"
                    "-fno-omit-frame-pointer" "-g")
    echo "sanitizers: ${J37_SANITIZE}"
fi

# verb.c as C, with the warnings the plugin's build does not suppress: the vendor
# code is not ours to change, but a compiler that starts objecting to it is worth
# seeing in this log rather than in a platform build. The sanitizers are passed
# here as well as to the C++ half, because the tank's own delay buffers are where
# an out-of-bounds read would happen: instrumenting only the wrapper would watch
# the code that calls the network and not the code that runs it.
"${CC}" -std=c99 -O2 "${sanitize_flags[@]}" -c "${LIBRARY}/verb.c" -o "${BUILD}/verb.o"

"${CXX}" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
    "${sanitize_flags[@]}" \
    -I "${HERE}" -I "${REPO}/Source" -I "${LIBRARY}" \
    "${HERE}/reverb_test.cpp" "${BUILD}/verb.o" -o "${BUILD}/reverb_test" -lm

"${BUILD}/reverb_test"
