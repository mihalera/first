// Kept so the project still has a translation unit for the umbrella header in
// pch.h. Precompiled headers are switched off in first_SharedCode.vcxproj, so
// this file compiles to an empty object and does nothing else. See pch.h.
//
// It deliberately does not include pch.h: with the PCH options removed there is
// nothing here that needs the JUCE headers, and pulling them in would only add
// a needless compile pass.
