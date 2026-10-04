#include "NativeRenderer_D3D11.h"

#if defined (J37_NATIVE_D3D11) && JUCE_WINDOWS
 #include <windows.h>
 #include <d3d11.h>
 #include <dxgi.h>
 #include <wrl/client.h>

namespace j37::render
{
namespace
{
using Microsoft::WRL::ComPtr;

/**
    DirectX 11 adapter.

    The device, the swapchain and the render target view are message-thread
    objects created once and kept. The child HWND is what makes the transport
    visible: a plugin host owns the top-level window, so this renderer parents
    its own WS_CHILD window to the host's peer and presents into that.

    Resize is ordered against the GPU. ResizeBuffers fails while a buffer is
    still referenced by an in-flight Present, so the target is detached from the
    context first and the view is dropped before the swapchain is resized.
*/
class D3D11Renderer final : public NativeRenderer
{
public:
    bool initialise (juce::Component& component, Config config) override
    {
        shutdown();

        const auto* peer = component.getPeer();
        if (peer == nullptr)
            return fail ("DirectX 11: the editor has no native peer yet");

        width = juce::jmax (1, config.width);
        height = juce::jmax (1, config.height);
        position = { config.positionX, config.positionY };
        vsyncInterval = juce::jlimit (0, 4, config.vsyncInterval);

        auto* parent = static_cast<HWND> (peer->getNativeHandle());
        if (parent == nullptr)
            return fail ("DirectX 11: the host peer exposes no HWND");

        // The device is created on the default adapter, so the scene shares the
        // host's GPU and needs no adapter enumeration or fallback ordering.
        const auto levels[] { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
        if (FAILED (D3D11CreateDevice (nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                       config.validation ? D3D11_CREATE_DEVICE_DEBUG : 0u,
                                       levels, 2, D3D11_SDK_VERSION,
                                       &device, &featureLevel, &context)))
            return fail ("DirectX 11: device creation failed");

        childWindow = CreateWindowExW (0, L"STATIC", L"first-d3d11-surface",
                                       WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                       position.x, position.y, width, height,
                                       parent, nullptr, GetModuleHandleW (nullptr), nullptr);
        if (childWindow == nullptr)
        {
            const auto reason = juce::String ("DirectX 11: child window creation failed (")
                                    + juce::String::toHexString ((int) GetLastError()) + ")";
            shutdown();
            return fail (reason);
        }

        DXGI_SWAP_CHAIN_DESC1 desc {};
        desc.Width = static_cast<UINT> (width);
        desc.Height = static_cast<UINT> (height);
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        ComPtr<IDXGIFactory2> factory;
        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter> adapter;
        if (FAILED (device.As (&dxgiDevice))
            || FAILED (dxgiDevice->GetAdapter (&adapter))
            || FAILED (adapter->GetParent (IID_PPV_ARGS (&factory)))
            || FAILED (factory->CreateSwapChainForHwnd (device.Get(), childWindow, &desc,
                                                        nullptr, nullptr, &swapChain))
            || FAILED (factory->MakeWindowAssociation (childWindow, DXGI_MWA_NO_ALT_ENTER)))
        {
            shutdown();
            return fail ("DirectX 11: swapchain creation failed");
        }

        if (! createRenderTarget())
        {
            shutdown();
            return fail ("DirectX 11: render target creation failed");
        }

        initialised = true;
        failure.clear();
        return true;
    }

    void resize (Config config) override
    {
        const auto newWidth = juce::jmax (1, config.width);
        const auto newHeight = juce::jmax (1, config.height);
        const auto newPosition = juce::Point<int> (config.positionX, config.positionY);

        if (! initialised)
            return;

        if (childWindow != nullptr)
            SetWindowPos (childWindow, nullptr, newPosition.x, newPosition.y,
                          newWidth, newHeight, SWP_NOZORDER | SWP_NOACTIVATE);

        if (newWidth == width && newHeight == height)
        {
            position = newPosition;
            return;
        }

        width = newWidth;
        height = newHeight;
        position = newPosition;

        // A buffer still referenced by the GPU cannot be resized, so the target
        // is detached from the context and dropped before ResizeBuffers.
        context->OMSetRenderTargets (0, nullptr, nullptr);
        renderTarget.Reset();

        if (swapChain != nullptr)
        {
            swapChain->ResizeBuffers (0, static_cast<UINT> (width), static_cast<UINT> (height),
                                      DXGI_FORMAT_UNKNOWN, 0);
            createRenderTarget();
        }
    }

    bool isPresentable() const noexcept override
    {
        return initialised && swapChain != nullptr && childWindow != nullptr;
    }

    void shutdown() noexcept override
    {
        // Unbind before releasing: a view pointing at a destroyed back buffer is
        // the classic D3D11 shutdown crash.
        if (context != nullptr)
        {
            context->OMSetRenderTargets (0, nullptr, nullptr);
            context->Flush();
        }

        renderTarget.Reset();
        swapChain.Reset();
        context.Reset();
        device.Reset();

        if (childWindow != nullptr)
        {
            DestroyWindow (childWindow);
            childWindow = nullptr;
        }

        initialised = false;
    }

    bool beginFrame() override
    {
        return isPresentable() && renderTarget != nullptr;
    }

    void clear (juce::Colour colour) override
    {
        clearColour = colour;

        if (! isPresentable() || renderTarget == nullptr)
            return;

        const auto values[] { colour.getFloatRed(), colour.getFloatGreen(),
                              colour.getFloatBlue(), colour.getFloatAlpha() };
        context->OMSetRenderTargets (1, renderTarget.GetAddressOf(), nullptr);
        context->ClearRenderTargetView (renderTarget.Get(), values);
    }

    void endFrame() override
    {
        if (! isPresentable())
            return;

        const auto result = swapChain->Present (static_cast<UINT> (vsyncInterval), 0);

        // DXGI_STATUS_OCCLUDED means the host minimised the editor: not an
        // error, and the frame is simply not shown.
        if (result == DXGI_STATUS_OCCLUDED)
            return;

        if (FAILED (result))
        {
            // A device removal is terminal. Leaving a dead device in place means
            // every later frame "succeeds" while drawing nothing.
            shutdown();
            failure = "DirectX 11: device removed";
        }
    }

    bool isInitialised() const noexcept override { return initialised; }

    juce::String status() const override
    {
        if (! initialised)
            return failure;

        return "DirectX 11 on feature level "
             + juce::String (static_cast<int> (featureLevel >> 12)) + "."
             + juce::String (static_cast<int> ((featureLevel >> 8) & 0xf));
    }

private:
    bool fail (juce::String reason)
    {
        failure = std::move (reason);
        return false;
    }

    bool createRenderTarget()
    {
        ComPtr<ID3D11Texture2D> backBuffer;
        if (swapChain == nullptr || device == nullptr)
            return false;

        return SUCCEEDED (swapChain->GetBuffer (0, IID_PPV_ARGS (&backBuffer)))
            && SUCCEEDED (device->CreateRenderTargetView (backBuffer.Get(), nullptr, &renderTarget));
    }

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain1> swapChain;
    ComPtr<ID3D11RenderTargetView> renderTarget;
    HWND childWindow = nullptr;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    juce::Point<int> position;
    juce::Colour clearColour;
    juce::String failure;
    int width = 1;
    int height = 1;
    int vsyncInterval = 1;
    bool initialised = false;
};
}

std::unique_ptr<NativeRenderer> createD3D11Renderer()
{
    return std::make_unique<D3D11Renderer>();
}
}
#else
namespace j37::render
{
std::unique_ptr<NativeRenderer> createD3D11Renderer() { return {}; }
}
#endif