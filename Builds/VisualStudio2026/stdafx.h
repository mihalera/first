/*
  ==============================================================================

    Compatibility alias for the shared precompiled header.

    Some build configurations ask the compiler to force-include a file literally
    named "stdafx.h" (the JUCE exporters other than the MSVC one, and any
    toolchain driven by the JuceLibraryCode layout). Reaching that spelling
    through this thin shim keeps a single copy of the real header and makes sure
    the forced include always resolves, no matter which name the build searches
    for.

    Do not add content here - add it to pch.h.

  ==============================================================================
*/

#pragma once

#include "pch.h"