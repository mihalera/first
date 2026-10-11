// Minimal stand-in for the JUCE helpers the pitch stage's two extracted structs
// use, so the harness compiles with nothing but a C++17 compiler.
//
// It is the DSP harness's shim plus exactly one addition: roundToInt, which the
// pitch tracker's prepare() calls and nothing in the DSP harness ever does. The
// addition lives HERE rather than in the shared shim so that tests/dsp/shim.h
// keeps its own promise - that it contains only what its extracted code calls,
// so a missing helper there fails the build instead of quietly behaving
// differently. Duplicating the rest would be worse than inheriting it: the two
// harnesses would then be able to disagree about what juce::jlimit means.
#pragma once

#include "../dsp/shim.h"

namespace juce
{
inline int roundToInt (float value) noexcept
{
    return static_cast<int> (std::lround (value));
}

inline int roundToInt (double value) noexcept
{
    return static_cast<int> (std::lround (value));
}
} // namespace juce
