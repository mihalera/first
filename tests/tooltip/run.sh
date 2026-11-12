#!/usr/bin/env bash
#
# Builds and runs the tooltip dwell harness.
#
#   tests/tooltip/run.sh
#
# The rule it measures - j37panel::TipDwell in Source/GUI/TipDwell.h - is pure
# arithmetic on a millisecond clock, and it was split out of the JUCE window for
# exactly this reason: the reported defect is "the second hint appears instantly,
# the first one waits", which is a claim about two changes of control, and it can
# be run here at exact millisecond boundaries with no display, no window and no
# JUCE.
#
# No extractor and no shim: the header includes nothing, so it is compiled as it
# ships - the same file Source/GUI/DwellTooltipWindow.h includes.
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/../lib/harness.sh"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$(j37_build j37-tooltip-test)"

mkdir -p "${BUILD}"

# J37_SANITIZE=address,undefined puts the harness under those sanitizers, the
# same switch the other harnesses take.

"${CXX}" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
    "${sanitize_flags[@]}" \
    -I "$(j37_path source)" \
    "${HERE}/dwell_test.cpp" -o "${BUILD}/dwell_test"

"${BUILD}/dwell_test"
