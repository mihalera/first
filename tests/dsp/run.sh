#!/usr/bin/env bash
#
# Builds and runs the SUBFUND regression harness.
#
#   tests/dsp/run.sh
#
# The harness has no JUCE, no audio backend and no host dependency: it cuts the
# shipping generator out of Source/, compiles it against a small shim and renders
# audio through a reduced replica of the signal path. That makes it a few seconds
# of work on any machine with a C++17 compiler, which is why it can run on every
# pull request instead of only where the plugin is built.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${TMPDIR:-/tmp}/subfund-dsp-test"
CXX="${CXX:-g++}"

# The stages that have moved into Source/DSP/ are INCLUDED by the generated header
# (see extract.py's SOURCE_HEADERS) rather than cut out of PluginProcessor.h, so
# the compiler needs Source/ on its include path. Source/ and tests/ are both part
# of the checkout, so this is the plugin's own header being compiled, not a copy.
SOURCE="${HERE}/../../Source"

mkdir -p "${BUILD}"

python3 "${HERE}/extract.py" "${BUILD}/extracted_dsp.inc"

# J37_SANITIZE=address,undefined puts the harness, and with it the shipped DSP it
# renders, under those sanitizers. The harness is seconds of work, so the
# sanitizer pass rides along with the debug build in the workflow rather than
# waiting for a separate sanitized build of the plugin.
sanitize_flags=()
if [ -n "${J37_SANITIZE:-}" ]; then
    sanitize_flags=("-fsanitize=${J37_SANITIZE}" "-fno-sanitize-recover=all"
                    "-fno-omit-frame-pointer" "-g")
    echo "sanitizers: ${J37_SANITIZE}"
fi

"${CXX}" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
    "${sanitize_flags[@]}" \
    -I "${HERE}" -I "${BUILD}" -I "${SOURCE}" \
    "${HERE}/subfund_test.cpp" -o "${BUILD}/subfund_test"

"${BUILD}/subfund_test"

# The loudness meter, in its own binary against the same extracted source. It is
# the number the panel's INPUT and OUTPUT LUFS readouts are - the ground truth a
# user mixes against - and a meter that reads wrong is invisible until somebody
# has already taken the session to the desk. (AUTO GAIN does not read it: that
# compensator measures block power on the signal itself.) Separate binary rather
# than one more section of the harness above so a failure names the meter in the
# log line that carries the exit code.
"${CXX}" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
    "${sanitize_flags[@]}" \
    -I "${HERE}" -I "${BUILD}" -I "${SOURCE}" \
    "${HERE}/loudness_test.cpp" -o "${BUILD}/loudness_test"

"${BUILD}/loudness_test"

# -----------------------------------------------------------------------------
#  The loudness ORACLE: the shipping meter and libebur128, on the same frames.
#
#  The loudness harness above checks the meter against the standard and against
#  EBU Tech 3341's own signals. Neither can say the CURVE is BS.1770's rather than
#  a self-consistent approximation - only a second implementation can, and the
#  comparison the harness header quotes was made outside this repository. This
#  builds it here: two implementations of the same specification, one buffer, and
#  0.25 LU of disagreement allowed (see the test's banner for why a band rather
#  than an equality, and where the two are SUPPOSED to differ).
#
#  This is the one library in the tree that is a DEV dependency and never reaches
#  the plugin: libebur128 is MIT, vendored under ThirdParty/libebur128 at v1.2.6,
#  and it is compiled into this binary and nothing else. A pruned checkout, or a
#  compiler whose C library has no <sys/queue.h> (MSVC's does not), SKIPS it and
#  says so rather than failing a build for a tool that does not ship.
# -----------------------------------------------------------------------------
ORACLE_SOURCE="${HERE}/../../ThirdParty/libebur128"

if [ -f "${ORACLE_SOURCE}/ebur128.c" ] \
   && printf '#include <sys/queue.h>\n' | "${CXX}" -E -x c - > /dev/null 2>&1; then
    # gnu99, not c99: the library uses M_PI, which glibc only declares under a
    # non-strict dialect (or _GNU_SOURCE). It is C either way; the extensions are
    # the header's, which is why the upstream file carries the MSVC note about
    # _USE_MATH_DEFINES next to its own includes.
    "${CXX}" -std=gnu99 -O2 "${sanitize_flags[@]}" -x c -c \
        "${ORACLE_SOURCE}/ebur128.c" -o "${BUILD}/ebur128.o"

    "${CXX}" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
        "${sanitize_flags[@]}" \
        -I "${HERE}" -I "${SOURCE}" -I "${ORACLE_SOURCE}" \
        "${HERE}/loudness_oracle_test.cpp" "${BUILD}/ebur128.o" -lm \
        -o "${BUILD}/loudness_oracle_test"

    "${BUILD}/loudness_oracle_test"
else
    echo "  skip  loudness oracle: ThirdParty/libebur128 is absent, or this platform" \
         "has no <sys/queue.h>"
fi

# -----------------------------------------------------------------------------
#  The shaper's tanh seam, in the three shapes a build can have it in.
#
#  j37Tanh (Source/DSP/HotPathMath.h) is the one place the saturation core's
#  cost can be traded for xsimd's batch kernel, and the switch that trades it
#  (J37_USE_SIMD_TANH, the CMake option of the same name) is ON in every build
#  this repository produces - which is exactly why the seam needs a harness. A
#  branch that only one configuration compiles rots, and this one had rotted
#  twice over:
#  nothing called the function the flag switched, and the call it made
#  (xsimd::tanh on a float) was std::tanh, so even with the flag thrown the
#  branch was a no-op.
#
#  Three binaries, one per shape, each TOLD which shape it is, and then their
#  curve digests are compared ACROSS the binaries, because "the flag reaches
#  the rendered audio" is a claim about two builds rather than about one:
#
#    flag + xsimd   J37_HAS_XSIMD 1, the approximation, the curves move  (SHIPPING)
#    flag alone     J37_HAS_XSIMD 0 and identical to the std::tanh shape
#    neither        the -DJ37_USE_SIMD_TANH=OFF shape
#
#  A build that asks for a library it has not got must render the default, and a
#  build with the flag and the library must render different audio by the amount
#  the header quotes - that is the pair this block exists to keep true.
# -----------------------------------------------------------------------------
XSIMD_INCLUDE="${HERE}/../../ThirdParty/xsimd/include"

build_seam() {                     # name, then that build's extra flags
    local name="$1"; shift

    "${CXX}" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
        "${sanitize_flags[@]}" \
        -I "${HERE}" -I "${SOURCE}" "$@" \
        "${HERE}/simd_tanh_test.cpp" -o "${BUILD}/${name}"
}

run_seam() {                       # name - stderr/stdout shown, digest kept
    local name="$1"

    if ! "${BUILD}/${name}" > "${BUILD}/${name}.txt"; then
        cat "${BUILD}/${name}.txt"
        echo "simd_tanh: ${name} failed" >&2
        exit 1
    fi

    grep -v '^curve ' "${BUILD}/${name}.txt"
}

build_seam simd_default -DJ37_EXPECT_XSIMD=0
build_seam simd_flag_no_library -DJ37_EXPECT_XSIMD=0 -DJ37_USE_SIMD_TANH=1
build_seam simd_kernel -DJ37_EXPECT_XSIMD=1 -DJ37_USE_SIMD_TANH=1 -I "${XSIMD_INCLUDE}"

run_seam simd_default
run_seam simd_flag_no_library
run_seam simd_kernel

digest() { grep '^curve ' "${BUILD}/$1.txt"; }

if diff <(digest simd_flag_no_library) <(digest simd_default) > /dev/null; then
    echo "  ok   the flag without xsimd renders the default curves, byte for byte"
else
    echo ""
    diff <(digest simd_flag_no_library) <(digest simd_default) | head -20
    echo "simd_tanh: J37_USE_SIMD_TANH without xsimd on the include path changed the curves" >&2
    exit 1
fi

# The cross-build claim, and the only place it can be made: one binary can only
# ever be one of the three shapes. Both bounds are asserted - a difference of
# zero means the flag no longer reaches the audio, and a difference over the
# bound means the kernel is not the approximation the header describes.
python3 - "${BUILD}/simd_kernel.txt" "${BUILD}/simd_default.txt" <<'PY'
import sys

def digest(path):
    rows = []
    for line in open(path):
        parts = line.split()
        if len(parts) == 5 and parts[0] == "curve":
            rows.append(tuple(parts[1:4]) + (float(parts[4]),))
    return rows

kernel, default = digest(sys.argv[1]), digest(sys.argv[2])
if not kernel or len(kernel) != len(default):
    raise SystemExit(f"simd_tanh: the two digests are not the same shape ({len(kernel)} vs {len(default)})")

worst, at, moved = 0.0, None, 0
for k, d in zip(kernel, default):
    if k[:3] != d[:3]:
        raise SystemExit(f"simd_tanh: digest order differs at {k[:3]} vs {d[:3]}")
    delta = abs(k[3] - d[3])
    moved += delta > 0.0
    if delta > worst:
        worst, at = delta, k

limit = 2.0e-6
if worst == 0.0:
    raise SystemExit("simd_tanh: the flag with xsimd present rendered identical curves - the seam does not reach the audio path")
if worst > limit:
    raise SystemExit(f"simd_tanh: the flag moved the curves by {worst:g} at {'/'.join(at[:3])}, over the {limit:g} bound")

print(f"  ok   with xsimd the flag moves {moved} of {len(kernel)} curve samples, worst {worst:.3g}"
      f" at {'/'.join(at[:3])}, within the {limit:g} bound")
PY

# -----------------------------------------------------------------------------
#  The vinyl stage's noise print, in the two shapes a build can be in.
#
#  Source/DSP/NoiseDecoder.h is the WAV decode behind the user's noise print and
#  dr_wav is the library under it, so the interesting claim is not "a file loads"
#  but "a build WITHOUT the decoder still compiles, still refuses the file, and
#  says which library is missing" - and the only way to know that branch says the
#  truth is to compile it. ThirdParty/dr_libs is vendored precisely so this does
#  not need a configure: the checkout is enough.
#
#  Both binaries compile Source/DSP/NoiseDecoder.cpp, the one translation unit
#  that owns dr_wav's implementation. The absent build compiles the same file and
#  gets the header's absent branch, which is what a pruned ThirdParty/ would.
# -----------------------------------------------------------------------------
DR_LIBS_INCLUDE="${HERE}/../../ThirdParty/dr_libs"

build_noise() {                     # name, then that build's extra flags
    local name="$1"; shift

    "${CXX}" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
        "${sanitize_flags[@]}" \
        -I "${HERE}" -I "${SOURCE}" "$@" \
        "${HERE}/noise_loader_test.cpp" "${SOURCE}/DSP/NoiseDecoder.cpp" \
        -o "${BUILD}/${name}"
}

build_noise noise_library -DJ37_EXPECT_DRWAV=1 -I "${DR_LIBS_INCLUDE}"
build_noise noise_absent -DJ37_EXPECT_DRWAV=0

"${BUILD}/noise_library"
"${BUILD}/noise_absent"
