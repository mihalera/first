/*
  ==============================================================================

    Shared umbrella header for the first / J37 tape plugin.

    Including <JuceHeader.h> pulls in every JUCE module header plus the full STL
    set used by the plugin.

    This file is NOT used as a precompiled header. The .vcxproj sets no PCH
    options at all, so every translation unit compiles its own copy of these
    headers. That costs some build time, but it removes a whole class of
    failures: a PCH only works when the PrecompiledHeader mode, the
    PrecompiledHeaderFile name and a matching top-of-file #include in every
    participating source all agree, and when they drift MSVC reports it as
    C2857 on pch.cpp plus C1010 on the plugin sources rather than as one
    readable error.

    Nothing in Source/ includes this header. If you ever want to switch
    precompiled headers back on, re-read the note inside
    first_SharedCode.vcxproj first - the steps have to be taken together.

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