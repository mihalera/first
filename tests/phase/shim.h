// The toolkit stand-in the phase harness compiles against.
//
// Source/DSP/MultibandPhaseGuard.h uses exactly three helpers - jmax, jlimit and
// MathConstants - and the DSP harness's shim already defines them for the same
// reason: the header is the plugin's own file, so the harness must not carry a
// second opinion about what those helpers mean. Nothing is added here; this file
// exists so the include line in the test says where the definitions come from.
#pragma once

#include "../dsp/shim.h"
