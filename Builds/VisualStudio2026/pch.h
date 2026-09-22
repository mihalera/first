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

    Two rules keep this scheme working, and both are easy to break by accident:

      1. Every PluginProcessor.cpp / PluginEditor.cpp must include this header as
         its very first include, or MSVC stops with C1010. The build compiles them
         with /Yu"pch.h", which is matched against the literal text of the include.

      2. Every ClCompile item that sets PrecompiledHeader - either Use or Create -
         must also set <PrecompiledHeaderFile>pch.h</PrecompiledHeaderFile>. MSBuild
         then emits /Yu"pch.h" and /Yc"pch.h". Leaving it off does not fall back to
         pch.h: the CL task defaults to stdafx.h, so pch.cpp would be built as
         /Ycstdafx.h, its "pch.h" include would never match, the precompiled header
         would silently never be created, and PluginProcessor.cpp would fail with
         C1010 while pch.cpp reported C2857.

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