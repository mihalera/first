#pragma once
#include "NativeRenderer.h"

namespace j37::render {

/** Submit a complete, validated display frame through a native adapter.

    The helper deliberately refuses adapters that only create/present a surface:
    their drawScene() default returns false. Callers must keep the existing JUCE
    rendering path active unless this function succeeds. Once beginFrame succeeds,
    endFrame is always called, including when scene drawing refuses the frame, so
    per-frame command lists/fences/semaphores are retired consistently.
*/
inline bool submitSceneFrame(NativeRenderer &renderer, const SceneFrame &frame) {
    if (!isValid(frame) || !renderer.isInitialised() || !renderer.isPresentable()
        || !renderer.beginFrame())
        return false;

    const auto background = juce::Colour::fromFloatRGBA (frame.backgroundColour[0],
                                                          frame.backgroundColour[1],
                                                          frame.backgroundColour[2],
                                                          frame.backgroundColour[3]);
    renderer.clear (background);
    const auto drewScene = renderer.drawScene (frame);
    renderer.endFrame();
    return drewScene;
}

} // namespace j37::render
