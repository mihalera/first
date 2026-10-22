// =============================================================================
//  sokol_gfx's implementation, in a translation unit of its own.
//
//  sokol_gfx is a single-header library: the declarations are ordinary, and the
//  definitions are emitted in exactly ONE translation unit, the one that defines
//  SOKOL_IMPL. That unit is this file.
//
//  WHY IT IS NOT INSIDE THE ADAPTER. JUCE includes the platform's GL headers
//  inside `namespace juce::gl` on every desktop platform. sokol_gfx's GL backend
//  includes the same headers at global scope and uses the constants they define,
//  so a translation unit that has JUCE first and sokol second sees neither
//  (JUCE's include guard has already been set, and the names live in juce::gl).
//  Keeping the implementation JUCE-free is what lets both libraries exist in one
//  binary without a header fight: nothing below includes JuceHeader.h, and the
//  backend macro comes from a header both sides can read.
//
//  The file compiles to nothing when the sokol backend is not selected, which is
//  the default - so a build that does not ask for it pays for neither the header
//  nor the object.
// =============================================================================
#include "SokolBackend.h"

#if defined (J37_NATIVE_SOKOL)
 #define SOKOL_IMPL
 #include <sokol_gfx.h>
#endif
