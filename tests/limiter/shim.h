// The toolkit stand-in the limiter harness compiles against.
//
// Source/DSP/SafetyLimiter.h names nothing of JUCE at all - it is std::max and
// std::abs over two floats - so the stage itself needs no stand-in. What does is
// Source/DSP/BlockRateCoefficients.h, which builds the limiter's four coefficients
// with onePoleCoefficient() and therefore names juce::jmax. That is the same shim
// the DSP harness uses, included rather than copied: the harness must not carry a
// second opinion about what those helpers mean.
#pragma once

#include "../dsp/shim.h"
