#!/usr/bin/env bash
#
# Builds and runs the output safety limiter harness.
#
#   tests/limiter/run.sh
#
# The limiter in Source/DSP/SafetyLimiter.h is one peak detector, one required
# gain and one smoothed gain over the frame's loudest channel, so it runs here one
# frame at a time with no JUCE, no block size and no host. The four coefficients
# come from Source/DSP/BlockRateCoefficients.h, built for the rate exactly as the
# engine builds them, which is what lets the harness say the shape is a duration
# rather than a sample count.
#
# No extractor and no replica: both headers are compiled as they ship. The only
# stand-in is tests/dsp/shim.h, for the juce:: helpers the coefficient builder
# names - the limiter itself names nothing of JUCE.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${TMPDIR:-/tmp}/j37-limiter-test"
CXX="${CXX:-g++}"

mkdir -p "${BUILD}"

# J37_SANITIZE=address,undefined puts the harness under those sanitizers, the same
# switch the other harnesses take. The stage runs per sample with two floats of
# state, so what they would catch here is an out-of-range read in the harness
# rather than in the header.
sanitize_flags=()
if [ -n "${J37_SANITIZE:-}" ]; then
    sanitize_flags=("-fsanitize=${J37_SANITIZE}" "-fno-sanitize-recover=all"
                    "-fno-omit-frame-pointer" "-g")
    echo "sanitizers: ${J37_SANITIZE}"
fi

"${CXX}" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
    "${sanitize_flags[@]}" \
    -I "${HERE}" -I "${HERE}/../../Source" \
    "${HERE}/limiter_test.cpp" -o "${BUILD}/limiter_test"

"${BUILD}/limiter_test"
