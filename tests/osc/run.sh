#!/usr/bin/env bash
#
# Builds and runs the OSC harness.
#
#   tests/osc/run.sh
#
# Source/Net/Osc.h and Source/Net/OscSurface.h are STAGED: they are compiled by the
# plugin and constructed by nothing, so no build and no user can tell whether the
# decoder pads a string correctly. That is what this runs - the codec against the
# bytes the specification fixes, in both directions, plus the address surface's
# refusals. The ring's ball (Source/OscBall.h) is the other JUCE-free piece here:
# its fixed step is what lets the editor and the engine agree, so the harness also
# drives it for two minutes and pins the contact mapping and the gain curve.
#
# The modal voice (Source/DSP/ModalVoice.h) is the third JUCE-free piece. The
# harness renders it at 48 and 32 kHz, checks the tail decays, that impact gain
# scales it and that reset leaves nothing ringing - the properties a compile
# cannot see and a listening test cannot localise.
#
# No JUCE, no socket, no toolkit: Source/ is on the include path and the headers
# are JUCE-free on purpose, so this is a few seconds of work on every pull request
# rather than a manual instruction.
#
# J37_SANITIZE=address,undefined rides along with the workflow's debug step, the same
# way the other harnesses do - and here it earns its place in a different way from
# the DSP ones: the decoder reads lengths out of a packet, which is the shape of code
# an address sanitizer is for.
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/../lib/harness.sh"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$(j37_build j37-osc-test)"

# The headers are included as `Net/Osc.h`, the way the plugin includes them, so
# Source/ is on the path.
SOURCE="$(j37_path source)"

mkdir -p "${BUILD}"

"${CXX}" -std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter \
    "${sanitize_flags[@]}" \
    -I "${SOURCE}" \
    "${HERE}/osc_test.cpp" -o "${BUILD}/osc_test"

"${BUILD}/osc_test"
