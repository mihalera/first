// =============================================================================
//  The one translation unit that owns dr_wav's implementation.
//
//  dr_libs is a single-header library in the sense that matters: the header
//  holds declarations AND the implementation, and exactly one translation unit
//  may compile the latter by defining DR_WAV_IMPLEMENTATION first. So that
//  definition lives here, in a file whose only job is this, rather than at a use
//  site - Source/DSP/NoiseLoader.h is included by more than one translation
//  unit, and two copies of dr_wav's functions is a link error that arrives the
//  day the second caller appears.
//
//  This file carries no JUCE, no plugin state and no audio path, so tests/dsp
//  compiles it directly, twice: once with ThirdParty/dr_libs on the include path
//  and once without it. That is how the absent-library branch of
//  Source/DSP/NoiseDecoder.h is compiled rather than assumed - the same reason
//  tests/spectrum builds the analyser both ways.
// =============================================================================

#include "OptionalLibraries.h"

#if J37_HAS_DRWAV
    #define DR_WAV_IMPLEMENTATION
#endif

#include "NoiseDecoder.h"
