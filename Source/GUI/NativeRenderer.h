#pragma once
#include "RenderBackend.h"
#include <JuceHeader.h>

#include <memory>

namespace j37::render {
/**
    Native command-buffer renderer used by TapeScene.

    The renderer owns the native device, queue, per-frame command resources and
    an off-screen presentation target. Keeping this contract independent of
    JUCE/OpenGL means each API can record commands without leaking SDK types
    into the editor or audio thread. The target is intentionally off-screen:
    plugin hosts own the top-level window, so presenting to it directly would
    corrupt the host's surface. A platform adapter can attach a child surface
    when the host permits one.
*/
class NativeRenderer {
  public:
    struct Config {
        int width = 1;
        int height = 1;
        int framesInFlight = 2;
        bool validation = false;
    };

    virtual ~NativeRenderer() = default;
    virtual bool initialise(juce::Component &, Config) = 0;
    virtual void resize(Config) = 0;
    virtual bool isPresentable() const noexcept = 0;
    virtual void shutdown() noexcept = 0;
    virtual bool beginFrame() = 0;
    virtual void clear(juce::Colour) = 0;
    virtual void endFrame() = 0;
    virtual bool isInitialised() const noexcept = 0;
    virtual juce::String status() const = 0;
};

std::unique_ptr<NativeRenderer> createNativeRenderer(Backend);
} // namespace j37::render
