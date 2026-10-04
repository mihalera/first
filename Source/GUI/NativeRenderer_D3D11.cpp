#include "NativeRenderer_D3D11.h"

#if defined(J37_NATIVE_D3D11) && JUCE_WINDOWS
#include <d3d11.h>
#include <dxgi.h>
 #include <wrl/client.h>
 #include <windows.h>

namespace j37::render {
namespace {
using Microsoft::WRL::ComPtr;

class D3D11Renderer final : public NativeRenderer {
  public:
    bool initialise(juce::Component &component, Config config) override {
        auto *peer = component.getPeer();
        if (peer == nullptr) {
            failure = "DirectX 11: native peer is not ready";
            return false;
        }

        width = juce::jmax(1, config.width);
        height = juce::jmax(1, config.height);
        componentPosition = { 0, 0 };
        UINT flags = config.validation ? D3D11_CREATE_DEVICE_DEBUG : 0u;
        D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
        D3D_FEATURE_LEVEL selected{};

        const auto result =
            D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 2,
                              D3D11_SDK_VERSION, &device, &selected, &context);
        if (FAILED(result)) {
            failure = "DirectX 11: device creation failed";
            return false;
        }

        auto* parent = static_cast<HWND> (component.getPeer()->getNativeHandle());
        if (parent == nullptr)
        {
            failure = "DirectX 11: host peer has no HWND";
            shutdown();
            return false;
        }

        childWindow = CreateWindowExW (0, L"STATIC", L"first-d3d11",
                                       WS_CHILD | WS_VISIBLE, 0, 0, width, height,
                                       parent, nullptr, GetModuleHandleW (nullptr), nullptr);
        componentPosition = { 0, 0 };
        if (childWindow == nullptr)
        {
            failure = "DirectX 11: child surface creation failed";
            shutdown();
            return false;
        }

        DXGI_SWAP_CHAIN_DESC swapDesc {};
        swapDesc.BufferCount = 2;
        swapDesc.BufferDesc.Width = static_cast<UINT> (width);
        swapDesc.BufferDesc.Height = static_cast<UINT> (height);
        swapDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapDesc.OutputWindow = childWindow;
        swapDesc.SampleDesc.Count = 1;
        swapDesc.Windowed = TRUE;
        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<IDXGIFactory> factory;
        device.As (&dxgiDevice);
        dxgiDevice->GetAdapter (&adapter);
        adapter->GetParent (IID_PPV_ARGS (&factory));
        if (FAILED (factory->CreateSwapChain (device.Get(), &swapDesc, &swapChain)))
        {
            failure = "DirectX 11: swapchain creation failed";
            shutdown();
            return false;
        }

        createRenderTarget();
        initialised = renderTarget != nullptr;
        failure = initialised ? juce::String() : "DirectX 11: render target creation failed";
        return initialised;
    }

    void resize (Config config) override
    {
        width = juce::jmax (1, config.width);
        height = juce::jmax (1, config.height);
        if (childWindow != nullptr)
        {
            auto position = componentPosition;
            SetWindowPos (childWindow, nullptr, position.x, position.y, width, height,
                          SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (swapChain != nullptr)
        {
            context->OMSetRenderTargets (0, nullptr, nullptr);
            renderTarget.Reset();
            swapChain->ResizeBuffers (0, static_cast<UINT> (width), static_cast<UINT> (height),
                                      DXGI_FORMAT_UNKNOWN, 0);
            createRenderTarget();
        }
    }

    bool isPresentable() const noexcept override { return childWindow != nullptr && swapChain != nullptr; }

    void shutdown() noexcept override
    {
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

    bool beginFrame() override { return initialised && renderTarget != nullptr; }
    void clear (juce::Colour colour) override
    {
        clearColour = colour;
        if (context != nullptr && renderTarget != nullptr)
        {
            const float values[] { colour.getFloatRed(), colour.getFloatGreen(),
                                   colour.getFloatBlue(), colour.getFloatAlpha() };
            context->OMSetRenderTargets (1, renderTarget.GetAddressOf(), nullptr);
            context->ClearRenderTargetView (renderTarget.Get(), values);
        }
    }
    void endFrame() override
    {
        if (swapChain != nullptr)
            swapChain->Present (1, 0);
    }
    bool isInitialised() const noexcept override { return initialised; }
    juce::String status() const override {
        return initialised ? "DirectX 11 device/context ready" : failure;
    }

  private:
    void createRenderTarget()
    {
        if (swapChain == nullptr || device == nullptr)
            return;
        ComPtr<ID3D11Texture2D> backBuffer;
        if (SUCCEEDED (swapChain->GetBuffer (0, IID_PPV_ARGS (&backBuffer))))
            device->CreateRenderTargetView (backBuffer.Get(), nullptr, &renderTarget);
    }

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swapChain;
    ComPtr<ID3D11RenderTargetView> renderTarget;
    HWND childWindow = nullptr;
    juce::Point<int> componentPosition;
    juce::Colour clearColour;
    juce::String failure;
    int width = 1;
    int height = 1;
    bool initialised = false;
};
} // namespace

std::unique_ptr<NativeRenderer> createD3D11Renderer() { return std::make_unique<D3D11Renderer>(); }
} // namespace j37::render
#else
namespace j37::render {
std::unique_ptr<NativeRenderer> createD3D11Renderer() { return {}; }
} // namespace j37::render
#endif
