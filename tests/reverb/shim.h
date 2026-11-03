// The toolkit stand-in the room harness compiles against.
//
// Source/DSP/DattorroRoom.h uses jlimit, jmax and MathConstants from juce::,
// and the DSP harness's shim already defines them for the same reason the phase
// harness reuses it: the header is the plugin's own file, so the harness must
// not carry a second opinion about what the helpers mean. Nothing is added here;
// this file exists so the include line in the test says where they come from.
#pragma once

#include "../dsp/shim.h"
