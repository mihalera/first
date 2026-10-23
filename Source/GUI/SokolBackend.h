/*
  ============================================================================
    Which graphics API sokol_gfx records commands for.

    sokol_gfx takes its backend as a COMPILE-TIME macro, and the macro has to be
    the same in the translation unit that compiles the implementation and in the
    one that calls it. Two files therefore include this header instead of each
    deciding for itself: Source/GUI/SokolGfxImpl.cpp (the implementation) and
    Source/GUI/NativeRenderer_Sokol.cpp (the caller).

    The conditions are the platform's own macros rather than JUCE's, because the
    implementation is compiled WITHOUT JUCE on purpose: JUCE wraps the GL headers
    in `namespace juce::gl`, sokol_gfx wants them at global scope, and the two
    cannot both win in one translation unit.
  ============================================================================
*/
#pragma once

#if defined (__ANDROID__)
 #define SOKOL_GLES3
#elif defined (__APPLE__) && defined (TARGET_OS_IPHONE) && TARGET_OS_IPHONE
 #define SOKOL_METAL
#else
 #define SOKOL_GLCORE
#endif
