/*
  ==============================================================================

    Shared precompiled header for the first / J37 tape plugin.

    Including <JuceHeader.h> pulls in every JUCE module header plus the full STL
    set used by the plugin, which is by far the most expensive part of building
    every translation unit. Compiling that once and reusing it removes tens of
    seconds from a clean build and makes incremental rebuilds far cheaper.

    This header is only used by the Visual Studio projects; the Projucer can be
    configured to generate the same setup via the project's PCH option.

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
