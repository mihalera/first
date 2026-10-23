// The toolkit stand-in the room harness compiles against.
//
// Source/DSP/DattorroRoom.h uses jlimit (to clamp SIZE) and nothing else from
// juce::, and the DSP harness's shim already defines it for the same reason the
// phase harness reuses it: the header is the plugin's own file, so the harness
// must not carry a second opinion about what the helper means. Nothing is added
// here; this file exists so the include line in the test says where it comes from.
#pragma once

#include "../dsp/shim.h"
