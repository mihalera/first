#!/usr/bin/env bash
#
# Builds and runs the spectrum analyser harness - TWICE.
#
#   tests/spectrum/run.sh
#
# The harness has no JUCE, no audio backend and no host: it includes the shipping
# header from Source/DSP/ and drives it with generated signal, which makes it
# seconds of work on any machine with a C++17 compiler.
#
# It is built twice on purpose, and that is the point of the script rather than an
# accident of it:
#
#   1. WITH ThirdParty/pocketfft on the include path. The probe in
#      Source/DSP/OptionalLibraries.h then reports J37_HAS_POCKETFFT 1, the header
#      includes the real transform and the harness measures calibration, linearity,
#      scalloping, band order, the edges, the window boundary and silence on it.
#
#   2. WITHOUT it. J37_HAS_POCKETFFT 0, which compiles the other half of the
#      header - the half a build that pruned ThirdParty/ gets - and asserts what
#      that half promises: it compiles, prepare() leaves the stage unready, a
#      full-scale tone reads the floor, the band ladder is still published, and a
#      null or zero-length feed is ignored rather than dereferenced.
#
#   3. WITH the ArmPL branch. J37_HAS_ARMPL 1 and pocketfft also present, which
#      is the state an ARM build is in: the stage prefers ArmPL's transform and
#      keeps pocketfft as the fallback. ArmPL is a downloaded AArch64 library, so
#      no x86 machine and neither Linux x64 CI job has a copy; the stand-in under
#      tests/spectrum/fftw_stub is therefore a REAL unnormalised forward DFT with
#      FFTW3's signatures, not a mock, and the same assertions run against it.
#      That keeps the C++ half of the integration compiled and measured here; the
#      real library is linked by the linux-arm64 and windows-arm64 jobs, which
#      configure with -DJ37_ARMPL_REQUIRED=ON.
#
# The second pass exists because no other harness in this suite compiles the
# absent-library branch of a source header. tests/neural does the same thing for
# RTNeural by DEFINING J37_HAS_RTNEURAL and standing in for the library's API;
# pocketfft needs no stand-in, so the switch here is simply whether the directory
# is on the include path - which is also exactly what decides it in the plugin.
#
# J37_SANITIZE=address,undefined rides along with the debug build in the workflow,
# the same way the other harnesses do.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "${HERE}/../.." && pwd)"
BUILD="${TMPDIR:-/tmp}/j37-spectrum-test"
CXX="${CXX:-g++}"
SOURCE="${REPO}/Source"
POCKETFFT="${REPO}/ThirdParty/pocketfft"

mkdir -p "${BUILD}"

sanitize_flags=()
if [ -n "${J37_SANITIZE:-}" ]; then
    sanitize_flags=("-fsanitize=${J37_SANITIZE}" "-fno-sanitize-recover=all"
                    "-fno-omit-frame-pointer" "-g")
    echo "sanitizers: ${J37_SANITIZE}"
fi

# The library build. ThirdParty/pocketfft is where the header is vendored, and
# cmake/J37Dependencies.cmake declares that same directory as the target's include
# directory, so this is the plugin's own include path rather than a second one.
if [ ! -f "${POCKETFFT}/pocketfft_hdronly.h" ]; then
    echo "tests/spectrum: ${POCKETFFT}/pocketfft_hdronly.h is missing." >&2
    echo "The library build of this harness cannot be built without it. The" >&2
    echo "absent-library pass below still runs with:" >&2
    echo "    tests/spectrum/run.sh" >&2
    exit 1
fi

echo "== with pocketfft (J37_HAS_POCKETFFT=1) =="
"${CXX}" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
    "${sanitize_flags[@]}" \
    -I "${SOURCE}" -I "${POCKETFFT}" \
    "${HERE}/spectrum_test.cpp" -o "${BUILD}/spectrum_with_library"

"${BUILD}/spectrum_with_library"

echo
echo "== without pocketfft (J37_HAS_POCKETFFT=0) =="
"${CXX}" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
    "${sanitize_flags[@]}" \
    -I "${SOURCE}" \
    "${HERE}/spectrum_test.cpp" -o "${BUILD}/spectrum_without_library"

"${BUILD}/spectrum_without_library"

echo
echo "== with the ArmPL branch (J37_HAS_ARMPL=1, stand-in FFTW) =="
"${CXX}" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
    "${sanitize_flags[@]}" \
    -I "${SOURCE}" -I "${POCKETFFT}" -I "${HERE}/fftw_stub" \
    -DJ37_HAS_ARMPL=1 \
    "${HERE}/spectrum_test.cpp" -o "${BUILD}/spectrum_with_armpl"

"${BUILD}/spectrum_with_armpl"
