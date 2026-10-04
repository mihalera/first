#pragma once
#include "RenderBackend.h"
#include <JuceHeader.h>

#include <memory>

namespace j37::render {
/**
    Native command-buffer renderer used by TapeScene.

    The renderer owns the native device, command queue, per-frame command
    resources and a presentation surface. Keeping this contract independent of
    JUCE/OpenGL means each API can record commands without leaking SDK types
    into the editor or the audio thread.

    PRESENTATION. A plugin host owns the top-level window, so an adapter never
    presents to the host's own surface. Each one attaches a child surface
    instead - a child HWND (D3D11/D3D12), a CAMetalLayer (Metal) or a native
    window handle plus an XCB/Wayland surface (Vulkan) - parented to the host
    view. isPresentable() reports whether that surface exists: only then does
    the caller suppress its own CPU painting, because a native surface that
    failed to attach would otherwise leave the component blank.

    THREADING. Every method runs on the message thread. The adapters therefore
    create and destroy their native objects here rather than on a render
    thread, which is also why the interfaces carry no synchronisation of their
    own.
*/
class NativeRenderer {
  public:
    struct Config {
        int width = 1;
        int height = 1;
        int framesInFlight = 2;
        bool validation = false;

        /** The surface origin inside the host's peer, in peer coordinates. A
            child window is positioned by these, so getting them wrong shows
            the transport somewhere other than the deck. */
        int positionX = 0;
        int positionY = 0;

        /** 0 disables presentation synchronisation, 1 waits for one refresh. */
        int vsyncInterval = 1;
    };

    virtual ~NativeRenderer() = default;

    /** Creates the device, the frame resources and the child surface. Returns
        false and leaves a reason in status() on any failure - the caller then
        keeps painting with JUCE. Safe to call again after a failure. */
    virtual bool initialise (juce::Component& peer, Config) = 0;

    /** Moves and resizes the child surface. Implementations must not tear down
        their device here, and must not resize a swapchain whose buffers are
        still in use by the GPU. */
    virtual void resize (Config) = 0;

    /** Whether a presentation surface is attached and can be drawn into. */
    virtual bool isPresentable() const noexcept = 0;

    /** Releases every native object, including the child surface. */
    virtual void shutdown() noexcept = 0;

    /** Acquires the next frame's resources, blocking if the GPU is still using
        this slot. Returns false when the frame must be skipped entirely. */
    virtual bool beginFrame() = 0;

    /** Records the clear for the frame begun by beginFrame(). */
    virtual void clear (juce::Colour) = 0;

    /** Submits and presents the frame. Always safe to call after a failed
        beginFrame(), where it is a no-op. */
    virtual void endFrame() = 0;

    virtual bool isInitialised() const noexcept = 0;
    virtual juce::String status() const = 0;
};

std::unique_ptr<NativeRenderer> createNativeRenderer(Backend);
} // namespace j37::render
