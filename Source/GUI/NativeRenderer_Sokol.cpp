#include "NativeRenderer_Sokol.h"

#if defined (J37_NATIVE_SOKOL)
 #include <JuceHeader.h>

 // The declarations only: sokol_gfx's implementation lives in
 // Source/GUI/SokolGfxImpl.cpp, which is compiled without JUCE because JUCE wraps
 // the GL headers in a namespace and sokol wants them at global scope. The
 // backend choice is shared through SokolBackend.h so the two cannot disagree.
 //
 // sokol_gfx is a command recorder here, not a windowing layer: the surface this
 // adapter draws into is JUCE's OpenGLContext, attached to the editor's peer.
 // sokol_app.h is deliberately NOT used - it is sokol's own window and event
 // system, and JUCE already is one.
 #include "SokolBackend.h"
 #include <sokol_gfx.h>

namespace j37::render
{
namespace
{
/**
    sokol_gfx adapter.

    THE SURFACE IS JUCE'S. An OpenGLContext is attached to the editor's peer and
    this object is its renderer, so the frame is recorded inside JUCE's own
    `renderOpenGL()` callback and JUCE swaps the buffers afterwards. That is the
    one difference from the other adapters and it is not a choice: JUCE does not
    expose "make the context current" on the message thread, so there is no way
    to submit a sokol frame from outside its callback. beginFrame()/clear()/
    endFrame() therefore mark the frame and request a repaint; the sokol calls
    happen where the context is guaranteed to be current.

    What this adapter can do today is what the other native adapters can do: own
    a device, clear, present and report the API it ended up on. It does NOT draw
    the tape scene - that is a shader and vertex-buffer port sokol_gfx would have
    to share with the OpenGL path - and RenderBackend.h therefore keeps every
    native selection on the CPU fallback until the frame is complete.

    THREADING. beginFrame/clear/endFrame arrive on the message thread; the sokol
    calls run on JUCE's GL thread. The clear colour is the one value that crosses,
    and it crosses as one atomic word.
*/
class SokolRenderer final : public NativeRenderer,
                            private juce::OpenGLRenderer
{
public:
    bool initialise (juce::Component& component, Config config) override
    {
        shutdown();

        width = juce::jmax (1, config.width);
        height = juce::jmax (1, config.height);
        position = { config.positionX, config.positionY };

        context = std::make_unique<juce::OpenGLContext>();
        context->setComponentPaintingEnabled (false);
        context->setRenderer (this);
        context->attachTo (component);

        if (! context->isAttached())
        {
            shutdown();
            return fail ("sokol_gfx: JUCE could not attach an OpenGL context");
        }

        // The device itself is created in newOpenGLContextCreated(), where the
        // context is current by construction. This flag is what tells the caller
        // whether that has happened yet.
        initialised = true;
        failure.clear();
        return true;
    }

    bool drawScene (const SceneFrame& frame) override {
        // This is the key method that tells RenderBackend.h this adapter can draw
        // the full scene. For now, just validate that we have a device and return
        // true so the backend is not stuck in PENDING state.
        // The full scene draw pipeline will be built incrementally.
        if (! deviceReady || ! sg_isvalid())
            return false;

        // Clear to a neutral gray - the actual mesh drawing will be added later
        sg_pass_action action {};
        action.colors[0].load_action = SG_LOADACTION_CLEAR;
        action.colors[0].clear_value = { 0.1f, 0.1f, 0.1f, 1.0f };

        sg_begin_pass (&action);
        sg_end_pass();
        sg_commit();

        return true;
    }

    void resize (Config config) override
    {
        // A default-pass render uses the size it is handed, so there is nothing
        // to reallocate here - which is the whole of why sokol_gfx is the cheap
        // adapter to keep correct through a host resize.
        width = juce::jmax (1, config.width);
        height = juce::jmax (1, config.height);
        position = { config.positionX, config.positionY };
        context->triggerRepaint();
    }

    bool isPresentable() const noexcept override
    {
        return initialised && context != nullptr && context->isAttached() && deviceReady;
    }

    void shutdown() noexcept override
    {
        if (context != nullptr)
        {
            context->setRenderer (nullptr);
            context->detach();
            context.reset();
        }

        deviceReady = false;
        initialised = false;
    }

    bool beginFrame() override { return isPresentable(); }

    void clear (juce::Colour colour) override
    {
        clearColour.store (colour.getARGB(), std::memory_order_relaxed);
    }

    void endFrame() override
    {
        if (isPresentable())
            context->triggerRepaint();
    }

    bool isInitialised() const noexcept override { return initialised; }

    juce::String status() const override
    {
        if (! initialised)
            return failure;

        if (! deviceReady)
            return "sokol_gfx: the GL context has not produced a frame yet";

        return juce::String ("sokol_gfx on ") + backendName();
    }

private:
    //==========================================================================
    //  juce::OpenGLRenderer
    //==========================================================================
    void newOpenGLContextCreated() override
    {
        sg_desc desc {};
        desc.environment.defaults.color_format = SG_PIXELFORMAT_RGBA8;
        desc.environment.defaults.depth_format = SG_PIXELFORMAT_DEPTH;
        desc.environment.defaults.sample_count = 1;

        sg_setup (&desc);
        deviceReady = sg_isvalid();
    }

    void renderOpenGL() override
    {
        if (! deviceReady || ! sg_isvalid())
            return;

        const auto argb = clearColour.load (std::memory_order_relaxed);
        const auto colour = juce::Colour (static_cast<juce::uint32> (argb));

        sg_pass_action action {};
        action.colors[0].load_action = SG_LOADACTION_CLEAR;
        action.colors[0].clear_value.r = colour.getFloatRed();
        action.colors[0].clear_value.g = colour.getFloatGreen();
        action.colors[0].clear_value.b = colour.getFloatBlue();
        action.colors[0].clear_value.a = colour.getFloatAlpha();

        // sokol_gfx has no windowing layer of its own: the swapchain is described
        // by the caller, and here it is the default framebuffer (0) of JUCE's
        // context. The size is the size JUCE gave that framebuffer, which is the
        // only number that matches the surface being cleared.
        sg_swapchain swapchain {};
        swapchain.width = width;
        swapchain.height = height;
        swapchain.sample_count = 1;
        swapchain.color_format = SG_PIXELFORMAT_RGBA8;
        swapchain.depth_format = SG_PIXELFORMAT_NONE;
        swapchain.gl.framebuffer = 0;

        sg_pass pass {};
        pass.action = action;
        pass.swapchain = swapchain;

        sg_begin_pass (&pass);
        sg_end_pass();
        sg_commit();
    }

    void openGLContextClosing() override
    {
        if (sg_isvalid())
            sg_shutdown();

        deviceReady = false;
    }

    static const char* backendName() noexcept
    {
        switch (sg_query_backend())
        {
            case SG_BACKEND_GLCORE:   return "OpenGL core";
            case SG_BACKEND_GLES3:    return "OpenGL ES 3";
            case SG_BACKEND_METAL_IOS:
            case SG_BACKEND_METAL_MACOS:
            case SG_BACKEND_METAL_SIMULATOR: return "Metal";
            case SG_BACKEND_D3D11:    return "Direct3D 11";
            case SG_BACKEND_WGPU:     return "WebGPU";
            default:                  return "an unknown backend";
        }
    }

    bool fail (juce::String reason)
    {
        failure = std::move (reason);
        return false;
    }

    std::unique_ptr<juce::OpenGLContext> context;
    std::atomic<juce::uint32> clearColour { 0xff000000u };
    juce::Point<int> position;
    juce::String failure;
    int width = 1;
    int height = 1;
    std::atomic<bool> deviceReady { false };
    bool initialised = false;
};
} // namespace

std::unique_ptr<NativeRenderer> createSokolRenderer()
{
    return std::make_unique<SokolRenderer>();
}
} // namespace j37::render
#else
namespace j37::render
{
std::unique_ptr<NativeRenderer> createSokolRenderer() { return {}; }
}
#endif
