#!/usr/bin/env bash
#
# Builds and runs the effects rack harness.
#
#   tests/fx/run.sh
#
# The twenty-five creative stages live under Source/DSP/Fx/ and the rack that
# orders them is Source/DSP/FxRack.h. None of them names JUCE, so this compiles
# the shipping headers as they ship - no extractor, no replica, no shim - and
# renders signal through the same per-sample entry point the engine calls.
#
# What it measures is in the harness's own header: harmonics for the drives, a
# quantised grid for the bitcrusher, a held sample for the rate crusher, threshold
# behaviour for the gate, a removed single-sample spike for the anti-spike, band
# energy for the de-esser and the spectral gate, level movement for the tremolo,
# an octave down and an octave up for the two pitch stages, and the tape loop for
# SPEED. The multiband compressor is driven directly, because the signal path
# calls it outside the rack.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${TMPDIR:-/tmp}/j37-fx-test"
CXX="${CXX:-g++}"

mkdir -p "${BUILD}"

sanitize_flags=()
if [ -n "${J37_SANITIZE:-}" ]; then
    sanitize_flags=("-fsanitize=${J37_SANITIZE}" "-fno-sanitize-recover=all"
                    "-fno-omit-frame-pointer" "-g")
    echo "sanitizers: ${J37_SANITIZE}"
fi

"${CXX}" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
    "${sanitize_flags[@]}" \
    -I "${HERE}/../../Source" \
    "${HERE}/fx_test.cpp" -o "${BUILD}/fx_test"

"${BUILD}/fx_test"
