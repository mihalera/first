#include "NativeRenderer_Bgfx.h"

#if defined (J37_NATIVE_BGFX)
 #include <JuceHeader.h>
 #include <bgfx/bgfx.h>

 #if JUCE_WINDOWS
  #include <windows.h>
 #endif

namespace j37::render
{
namespace
{
/**
    bgfx adapter.

    SURFACE. bgfx renders into a native window it is handed, and a plugin host
    owns the editor's top-level window. Presenting into that window directly
    would paint over whatever the host has behind the editor, which is the one
    thing NativeRenderer's contract forbids - so this adapter does what the D3D11
    one does: it creates a CHILD surface inside the peer and gives bgfx that.
    Windows is where that exists today (the same WS_CHILD window D3D11 creates);
    on the other platforms the adapter refuses with a reason rather than
    presenting across the host's window, exactly as the Metal adapter refuses an
    embedded NSView.

    WHAT IT DRAWS. A cleared surface, like every other adapter in this directory.
    bgfx draws nothing without shader binaries, and its shaders are compiled
    offline by bgfx's own shaderc tool - the build here deliberately turns that
    tool off (see the BGFX block in cmake/J37Dependencies.cmake), so the tape
    scene's GLSL cannot be handed to it as it stands. RenderBackend.h therefore
    keeps every native selection on the CPU fallback until the frame is complete.

    THREADING. Message thread, per the contract: bgfx's view state is global and
    single-threaded, and nothing here is called from the audio thread.
*/
class BgfxRenderer final : public NativeRenderer
{
public:
    bool initialise (juce::Component& component, Config config) override
    {
        shutdown();

        width = juce::jmax (1, config.width);
        height = juce::jmax (1, config.height);
        position = { config.positionX, config.positionY };
        vsyncInterval = juce::jlimit (0, 4, config.vsyncInterval);

        const auto* peer = component.getPeer();
        if (peer == nullptr)
            return fail ("bgfx: the editor has no native peer yet");

       #if JUCE_WINDOWS
        auto* parent = static_cast<HWND> (peer->getNativeHandle());
        if (parent == nullptr)
            return fail ("bgfx: the host peer exposes no HWND");

        childWindow = CreateWindowExW (0, L"STATIC", L"first-bgfx-surface",
                                       WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                       position.x, position.y, width, height,
                                       parent, nullptr, GetModuleHandleW (nullptr), nullptr);
        if (childWindow == nullptr)
        {
            const auto reason = juce::String ("bgfx: child window creation failed (")
                                    + juce::String::toHexString (static_cast<int> (GetLastError()))
                                    + ")";
            shutdown();
            return fail (reason);
        }

        // bgfx's current API keeps the WINDOW in a swap-chain description and the
        // frame-global settings in Init.reset. The window is the child created
        // above, so bgfx never sees the host's peer and cannot paint across it.
        bgfx::Init init;
        init.type = bgfx::RendererType::Count;   // whichever backend bgfx prefers here
        init.vendorId = BGFX_PCI_ID_NONE;
        init.swapChain.nwh = childWindow;
        init.swapChain.width = static_cast<std::uint32_t> (width);
        init.swapChain.height = static_cast<std::uint32_t> (height);
        init.swapChain.flags = BGFX_SWAP_CHAIN_NONE;
        init.reset = vsyncInterval > 0 ? BGFX_RESET_VSYNC : BGFX_RESET_NONE;

        if (! bgfx::init (init))
        {
            shutdown();
            return fail ("bgfx: no renderer could be initialised on this machine");
        }

        initialised = true;
        presentFrame (juce::Colours::black);
        failure.clear();
        return true;
       #else
        juce::ignoreUnused (component);
        return fail ("bgfx: a dedicated child surface is not implemented on this platform");
       #endif
    }

    void resize (Config config) override
    {
        const auto newWidth = juce::jmax (1, config.width);
        const auto newHeight = juce::jmax (1, config.height);

        width = newWidth;
        height = newHeight;
        position = { config.positionX, config.positionY };

        if (! initialised)
            return;

       #if JUCE_WINDOWS
        if (childWindow != nullptr)
            SetWindowPos (childWindow, nullptr, position.x, position.y, width, height,
                          SWP_NOZORDER | SWP_NOACTIVATE);

        // A swap chain describes one SURFACE, so a resize is a new description
        // rather than two numbers handed to reset().
        bgfx::SwapChain swapChain {};
        swapChain.nwh = childWindow;
        swapChain.width = static_cast<std::uint32_t> (width);
        swapChain.height = static_cast<std::uint32_t> (height);
        swapChain.flags = BGFX_SWAP_CHAIN_NONE;

        bgfx::reset (vsyncInterval > 0 ? BGFX_RESET_VSYNC : BGFX_RESET_NONE, &swapChain);
       #endif

        bgfx::setViewRect (0, 0, 0, static_cast<std::uint16_t> (width),
                           static_cast<std::uint16_t> (height));
    }

    bool isPresentable() const noexcept override { return initialised; }

    void shutdown() noexcept override
    {
        if (initialised)
        {
            bgfx::shutdown();
            initialised = false;
        }

       #if JUCE_WINDOWS
        if (childWindow != nullptr)
        {
            DestroyWindow (childWindow);
            childWindow = nullptr;
        }
       #endif
    }

    bool beginFrame() override { return initialised; }

    void clear (juce::Colour colour) override { clearColour = colour; }

    void endFrame() override
    {
        if (initialised)
            presentFrame (clearColour);
    }

    bool isInitialised() const noexcept override { return initialised; }

    juce::String status() const override
    {
        if (! initialised)
            return failure;

        const auto name = bgfx::getRendererName (bgfx::getRendererType());
        return juce::String ("bgfx on ") + (name != nullptr ? name : "an unnamed renderer");
    }

private:
    /** One frame: clear view 0 with the colour, submit it, and let bgfx present. */
    void presentFrame (juce::Colour colour)
    {
        const auto packed = juce::uint32 (colour.getRed())
                          | (juce::uint32 (colour.getGreen()) << 8)
                          | (juce::uint32 (colour.getBlue()) << 16)
                          | (juce::uint32 (colour.getAlpha()) << 24);

        bgfx::setViewClear (0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, packed, 1.0f, 0);
        bgfx::setViewRect (0, 0, 0, static_cast<std::uint16_t> (width),
                           static_cast<std::uint16_t> (height));
        bgfx::touch (0);
        bgfx::frame();
    }

    bool fail (juce::String reason)
    {
        failure = std::move (reason);
        return false;
    }

   #if JUCE_WINDOWS
    HWND childWindow = nullptr;
   #endif

    juce::Colour clearColour = juce::Colours::black;
    juce::Point<int> position;
    juce::String failure;
    int width = 1;
    int height = 1;
    int vsyncInterval = 1;
    bool initialised = false;
};
} // namespace

std::unique_ptr<NativeRenderer> createBgfxRenderer()
{
    return std::make_unique<BgfxRenderer>();
}
} // namespace j37::render
#else
namespace j37::render
{
std::unique_ptr<NativeRenderer> createBgfxRenderer() { return {}; }
}
#endif
