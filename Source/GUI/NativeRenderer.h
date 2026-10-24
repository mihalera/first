#pragma once
#include "RenderBackend.h"
#include "SceneFrame.h"
#include <JuceHeader.h>

#include <memory>

namespace j37::render {
/**
    Native command-buffer renderer used by TapeScene.

    The renderer owns the native device, command queue, per-frame command
    resources and a presentation surface. Keeping this contract independent of
    JUCE/OpenGL means each API can record commands without leaking SDK types
    into the editor or the audio thread.

    PRESENTATION. A plugin host owns the top-level window. Adapters must use a
    surface scoped to the editor's scene bounds and must never paint over the
    host-owned peer. A presentable surface alone is not a complete renderer: the
    backend must draw the full scene before RenderBackend.h advertises it.

    THREADING. The current adapter methods are called from the message thread.
    Implementations must bound waits and must not retain pointers into editor
    components beyond shutdown. A later render-thread implementation must update
    this contract explicitly rather than silently changing callback ownership.
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

    /** Draws a validated scene frame into the surface begun by beginFrame().

        Adapters that only create, clear and present a surface keep this
        default: false, which is what tells NativeFrameSubmission's caller to
        leave the JUCE painting path in place. An adapter that implements the
        full scene draw overrides it and is the only kind RenderBackend.h may
        ever advertise as a scene backend. */
    virtual bool drawScene (const SceneFrame&) { return false; }
};

std::unique_ptr<NativeRenderer> createNativeRenderer(Backend);
} // namespace j37::render
