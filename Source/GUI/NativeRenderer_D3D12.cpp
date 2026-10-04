#include "NativeRenderer_D3D12.h"

#include <vector>

#if defined (J37_NATIVE_D3D12) && JUCE_WINDOWS
 #include <windows.h>
 #include <d3d12.h>
 #include <dxgi1_6.h>
 #include <wrl/client.h>

namespace j37::render
{
namespace
{
using Microsoft::WRL::ComPtr;

/**
    DirectX 12 adapter.

    DirectX 12 has no implicit state, so this adapter owns the whole of the
    per-frame contract explicitly:

      - one command LIST per frame slot, each with its own allocator;
      - a fence value per slot, so slot N is only reused once the GPU has
        finished the frame recorded into it (a fence alone is not enough - the
        allocator has to be reset on the CPU only after that, and the same is
        true of the back buffers, which resize needs to release);
      - an RTV descriptor heap with one descriptor per back buffer.

    A swapchain buffer is normally left in the PRESENT state after Present(), so
    every frame records an explicit transition to RENDER_TARGET and back. That
    is not boilerplate: without it the debug layer reports the barrier mismatch
    and the swapchain texture can be torn down while still in flight.

    The child HWND is the presentation surface. A plugin host owns the
    top-level window, so this renderer parents its own WS_CHILD window to the
    host's peer rather than presenting into the host's own window.
*/
class D3D12Renderer final : public NativeRenderer
{
public:
    bool initialise (juce::Component& component, Config config) override
    {
        shutdown();

        const auto* peer = component.getPeer();
        if (peer == nullptr)
            return fail ("DirectX 12: the editor has no native peer yet");

        width = juce::jmax (1, config.width);
        height = juce::jmax (1, config.height);
        position = { config.positionX, config.positionY };
        frameCount = juce::jlimit (2, 3, config.framesInFlight);
        vsyncInterval = juce::jlimit (0, 4, config.vsyncInterval);

        auto* parent = static_cast<HWND> (peer->getNativeHandle());
        if (parent == nullptr)
            return fail ("DirectX 12: the host peer exposes no HWND");

        if (FAILED (D3D12CreateDevice (nullptr, D3D_FEATURE_LEVEL_11_0,
                                       IID_PPV_ARGS (&device))))
            return fail ("DirectX 12: device creation failed");

        D3D12_COMMAND_QUEUE_DESC queueDesc {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED (device->CreateCommandQueue (&queueDesc, IID_PPV_ARGS (&queue))))
        {
            shutdown();
            return fail ("DirectX 12: command queue creation failed");
        }

        if (FAILED (device->CreateFence (fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS (&fence))))
        {
            shutdown();
            return fail ("DirectX 12: fence creation failed");
        }

        fenceEvent = CreateEventExW (nullptr, nullptr, CREATE_EVENT_MANUAL_RESET_EVENT, 0);
        if (fenceEvent == nullptr)
        {
            shutdown();
            return fail ("DirectX 12: fence event creation failed");
        }

        childWindow = CreateWindowExW (0, L"STATIC", L"first-d3d12-surface",
                                       WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                       position.x, position.y, width, height,
                                       parent, nullptr, GetModuleHandleW (nullptr), nullptr);
        if (childWindow == nullptr)
        {
            shutdown();
            return fail ("DirectX 12: child window creation failed");
        }

        ComPtr<IDXGIFactory4> factory;
        if (FAILED (CreateDXGIFactory2 (0, IID_PPV_ARGS (&factory))))
        {
            shutdown();
            return fail ("DirectX 12: DXGI factory creation failed");
        }

        DXGI_SWAP_CHAIN_DESC1 desc {};
        desc.Width = static_cast<UINT> (width);
        desc.Height = static_cast<UINT> (height);
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BufferCount = static_cast<UINT> (frameCount);
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.SampleDesc.Count = 1;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

        ComPtr<IDXGISwapChain1> created;
        if (FAILED (factory->CreateSwapChainForHwnd (queue.Get(), childWindow, &desc,
                                                     nullptr, nullptr, &created))
            || FAILED (created.As (&swapChain)))
        {
            shutdown();
            return fail ("DirectX 12: swapchain creation failed");
        }

        factory->MakeWindowAssociation (childWindow, DXGI_MWA_NO_ALT_ENTER);

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc {};
        heapDesc.NumDescriptors = static_cast<UINT> (frameCount);
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        if (FAILED (device->CreateDescriptorHeap (&heapDesc, IID_PPV_ARGS (&rtvHeap))))
        {
            shutdown();
            return fail ("DirectX 12: RTV heap creation failed");
        }

        allocators.resize (static_cast<std::size_t> (frameCount));
        commandLists.resize (static_cast<std::size_t> (frameCount));

        for (std::size_t i = 0; i < allocators.size(); ++i)
        {
            if (FAILED (device->CreateCommandAllocator (D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                        IID_PPV_ARGS (&allocators[i]))))
            {
                shutdown();
                return fail ("DirectX 12: command allocator creation failed");
            }

            if (FAILED (device->CreateCommandList (0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                   allocators[i].Get(), nullptr,
                                                   IID_PPV_ARGS (&commandLists[i]))))
            {
                shutdown();
                return fail ("DirectX 12: command list creation failed");
            }

            // A command list is created open; it must be closed before it can be
            // executed, and before its allocator can be reset.
            commandLists[i]->Close();
            frameFenceValues[i] = fenceValue;
        }

        if (! createRenderTargets())
        {
            shutdown();
            return fail ("DirectX 12: back buffer acquisition failed");
        }

        frameIndex = 0;
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

        // Resizing a swapchain while frames are in flight is undefined, so the
        // GPU is brought to a known state first and every back buffer is
        // released before ResizeBuffers is allowed to drop its own references.
        waitForGpuIdle();

        width = newWidth;
        height = newHeight;
        position = newPosition;

        for (auto& buffer : backBuffers)
            buffer.Reset();

        if (FAILED (swapChain->ResizeBuffers (static_cast<UINT> (frameCount),
                                              static_cast<UINT> (width), static_cast<UINT> (height),
                                              DXGI_FORMAT_R8G8B8A8_UNORM, 0)))
        {
            shutdown();
            failure = "DirectX 12: swapchain resize failed";
            return;
        }

        createRenderTargets();
    }

    bool isPresentable() const noexcept override
    {
        return initialised && swapChain != nullptr && childWindow != nullptr;
    }

    void shutdown() noexcept override
    {
        waitForGpuIdle();

        for (auto& buffer : backBuffers)
            buffer.Reset();

        rtvHeap.Reset();
        swapChain.Reset();
        commandLists.clear();
        allocators.clear();
        fence.Reset();
        queue.Reset();
        device.Reset();

        if (fenceEvent != nullptr)
        {
            CloseHandle (fenceEvent);
            fenceEvent = nullptr;
        }

        if (childWindow != nullptr)
        {
            DestroyWindow (childWindow);
            childWindow = nullptr;
        }

        initialised = false;
    }

    bool beginFrame() override
    {
        if (! isPresentable())
            return false;

        const auto slot = frameIndex;

        // Wait for this slot's previous frame to retire before touching its
        // allocator or its back buffer. The wait is bounded: a fence that does
        // not signal means the GPU is gone or wedged, and blocking the editor's
        // message thread forever would take the whole host down with it. A
        // dropped frame is the correct answer here, not a shutdown.
        if (fence->GetCompletedValue() < frameFenceValues[slot])
        {
            if (FAILED (fence->SetEventOnCompletion (frameFenceValues[slot], fenceEvent)))
            {
                shutdown();
                return false;
            }

            if (WaitForSingleObject (fenceEvent, kFrameWaitMs) != WAIT_OBJECT_0)
                return false;
        }

        allocators[slot]->Reset();
        if (FAILED (commandLists[slot]->Reset (allocators[slot].Get(), nullptr)))
        {
            shutdown();
            return false;
        }

        activeSlot = slot;
        activeBackBuffer = swapChain->GetCurrentBackBufferIndex();
        return true;
    }

    void clear (juce::Colour colour) override
    {
        clearColour = colour;

        if (! isPresentable() || activeSlot >= commandLists.size())
            return;

        auto* list = commandLists[activeSlot].Get();

        D3D12_RESOURCE_BARRIER toTarget {};
        toTarget.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toTarget.Transition.pResource = backBuffers[activeBackBuffer].Get();
        toTarget.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        toTarget.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        toTarget.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier (1, &toTarget);

        auto handle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T> (activeBackBuffer) * descriptorIncrement();

        const auto values[] { colour.getFloatRed(), colour.getFloatGreen(),
                              colour.getFloatBlue(), colour.getFloatAlpha() };
        list->OMSetRenderTargets (1, &handle, FALSE, nullptr);
        list->ClearRenderTargetView (handle, values, 0, nullptr);

        D3D12_RESOURCE_BARRIER toPresent = toTarget;
        toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        toPresent.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        list->ResourceBarrier (1, &toPresent);
    }

    void endFrame() override
    {
        if (! isPresentable() || activeSlot >= commandLists.size())
            return;

        auto* list = commandLists[activeSlot].Get();

        if (FAILED (list->Close()))
        {
            shutdown();
            return;
        }

        ID3D12CommandList* lists[] { list };
        queue->ExecuteCommandLists (1, lists);

        const auto presentResult = swapChain->Present (static_cast<UINT> (vsyncInterval), 0);

        frameFenceValues[activeSlot] = ++fenceValue;
        queue->Signal (fence.Get(), frameFenceValues[activeSlot]);

        frameIndex = (activeSlot + 1) % commandLists.size();
        activeSlot = commandLists.size();

        // DXGI_STATUS_OCCLUDED is the host minimising the editor, not a fault.
        if (FAILED (presentResult) && presentResult != DXGI_STATUS_OCCLUDED)
        {
            shutdown();
            failure = "DirectX 12: device removed";
        }
    }

    bool isInitialised() const noexcept override { return initialised; }

    juce::String status() const override
    {
        if (! initialised)
            return failure;

        return "DirectX 12 with " + juce::String (frameCount) + " frames in flight";
    }

private:
    /** How long a frame may wait for its slot's fence before the frame is
        dropped. Well above the worst case for a 3-buffer swapchain, and well
        below anything a user would call a hang. */
    static constexpr auto kFrameWaitMs = 500;

    bool fail (juce::String reason)
    {
        failure = std::move (reason);
        return false;
    }

    SIZE_T descriptorIncrement() const
    {
        return device != nullptr
            ? device->GetDescriptorHandleIncrementSize (D3D12_DESCRIPTOR_HEAP_TYPE_RTV)
            : 0;
    }

    bool createRenderTargets()
    {
        auto handle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        const auto increment = descriptorIncrement();

        for (int i = 0; i < frameCount; ++i)
        {
            if (FAILED (swapChain->GetBuffer (static_cast<UINT> (i), IID_PPV_ARGS (&backBuffers[i]))))
                return false;

            device->CreateRenderTargetView (backBuffers[i].Get(), nullptr, handle);
            handle.ptr += increment;
        }

        return true;
    }

    void waitForGpuIdle() noexcept
    {
        if (queue == nullptr || fence == nullptr || fenceEvent == nullptr)
            return;

        const auto target = ++fenceValue;

        if (queue->Signal (fence.Get(), target) != S_OK
            || fence->SetEventOnCompletion (target, fenceEvent) != S_OK
            || WaitForSingleObject (fenceEvent, 2000) == WAIT_FAILED)
        {
            // A fence that will not signal means the GPU is gone or wedged.
            // Waiting longer would hang the editor's shutdown, so this is a
            // deliberate bound rather than an unbounded wait.
            return;
        }
    }

    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> fence;
    ComPtr<IDXGISwapChain3> swapChain;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    std::vector<ComPtr<ID3D12CommandAllocator>> allocators;
    std::vector<ComPtr<ID3D12GraphicsCommandList>> commandLists;
    ComPtr<ID3D12Resource> backBuffers[3];
    UINT64 frameFenceValues[3] {};
    HANDLE fenceEvent = nullptr;
    UINT64 fenceValue = 0;
    HWND childWindow = nullptr;
    std::size_t frameIndex = 0;
    std::size_t activeSlot = 3;   ///< past the end means "no frame in flight"
    unsigned activeBackBuffer = 0;
    juce::Point<int> position;
    juce::Colour clearColour;
    juce::String failure;
    int width = 1;
    int height = 1;
    int frameCount = 2;
    int vsyncInterval = 1;
    bool initialised = false;
};
}

std::unique_ptr<NativeRenderer> createD3D12Renderer()
{
    return std::make_unique<D3D12Renderer>();
}
}
#else
namespace j37::render
{
std::unique_ptr<NativeRenderer> createD3D12Renderer() { return {}; }
}
#endif