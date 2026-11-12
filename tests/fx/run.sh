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
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/../lib/harness.sh"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$(j37_build j37-fx-test)"

mkdir -p "${BUILD}"

"${CXX}" "${J37_CXX_STD}" -O2 "${J37_WARNINGS[@]}" \
    "${sanitize_flags[@]}" \
    -I "$(j37_path source)" \
    "${HERE}/fx_test.cpp" -o "${BUILD}/fx_test"

"${BUILD}/fx_test"

# The second binary, and the one that answers the panel's own complaint: every
# rack CONTROL, one at a time, neutral / alone / bounded / audible. It found the
# vocoder's 14x makeup and the formant's Q-gain, neither of which fx_test above
# could see - that one asks whether a stage does its job, this one asks whether
# its knob can be turned anywhere without the output leaving the rails.
"${CXX}" "${J37_CXX_STD}" -O2 "${J37_WARNINGS[@]}"     "${sanitize_flags[@]}"     -I "$(j37_path source)"     "${HERE}/per_parameter_test.cpp" -o "${BUILD}/per_parameter_test"

"${BUILD}/per_parameter_test"
