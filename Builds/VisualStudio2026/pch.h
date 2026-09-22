/*
  ==============================================================================

    Shared precompiled header for the first / J37 tape plugin.

    Including <JuceHeader.h> pulls in every JUCE module header plus the full STL
    set used by the plugin, which is by far the most expensive part of building
    every translation unit. Compiling that once and reusing it removes tens of
    seconds from a clean build and makes incremental rebuilds far cheaper.

    Only the plugin's own translation units opt into this header. The JUCE module
    wrappers (include_juce_*.cpp) deliberately keep PrecompiledHeader = NotUsing,
    because force-injecting JuceHeader.h into them trips JUCE's own
    incorrect-use-of-JUCE-cpp-file guard.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>
