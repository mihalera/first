#include "NativeRenderer_D3D12.h"
#include <vector>

#if defined(J37_NATIVE_D3D12) && JUCE_WINDOWS
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

namespace j37::render {
namespace {
using Microsoft::WRL::ComPtr;

class D3D12Renderer final : public NativeRenderer {
  public:
    bool initialise(juce::Component &component, Config config) override {
        auto *peer = component.getPeer();
        if (peer == nullptr) {
            failure = "DirectX 12: native peer is not ready";
            return false;
        }

        width = juce::jmax(1, config.width);
        height = juce::jmax(1, config.height);
        frameCount = juce::jlimit(2, 3, config.framesInFlight);

        if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
            failure = "DirectX 12: device creation failed";
            return false;
        }

        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)))) {
            failure = "DirectX 12: command queue creation failed";
            shutdown();
            return false;
        }

        if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
            failure = "DirectX 12: fence creation failed";
            shutdown();
            return false;
        }

        allocators.resize (static_cast<size_t> (frameCount));
        for (auto& allocator : allocators)
            if (FAILED (device->CreateCommandAllocator (D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                        IID_PPV_ARGS (&allocator))))
            {
                failure = "DirectX 12: command allocator creation failed";
                shutdown();
                return false;
            }

        if (FAILED (device->CreateCommandList (0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                               allocators.front().Get(), nullptr,
                                               IID_PPV_ARGS (&commandList))))
        {
            failure = "DirectX 12: command list creation failed";
            shutdown();
            return false;
        }
        commandList->Close();

        auto* parent = static_cast<HWND> (component.getPeer()->getNativeHandle());
        if (parent == nullptr)
        {
            failure = "DirectX 12: host peer has no HWND";
            shutdown();
            return false;
        }

        childWindow = CreateWindowExW (0, L"STATIC", L"first-d3d12",
                                       WS_CHILD | WS_VISIBLE, 0, 0, width, height,
                                       parent, nullptr, GetModuleHandleW (nullptr), nullptr);
        if (childWindow == nullptr)
        {
            failure = "DirectX 12: child surface creation failed";
            shutdown();
            return false;
        }

        ComPtr<IDXGIFactory4> factory;
        if (FAILED (CreateDXGIFactory2 (0, IID_PPV_ARGS (&factory))))
        {
            failure = "DirectX 12: DXGI factory creation failed";
            shutdown();
            return false;
        }

        DXGI_SWAP_CHAIN_DESC1 swapDesc {};
        swapDesc.Width = static_cast<UINT> (width);
        swapDesc.Height = static_cast<UINT> (height);
        swapDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapDesc.BufferCount = static_cast<UINT> (frameCount);
        swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapDesc.SampleDesc.Count = 1;
        swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        ComPtr<IDXGISwapChain1> swapChain1;
        if (FAILED (factory->CreateSwapChainForHwnd (queue.Get(), childWindow, &swapDesc,
                                                     nullptr, nullptr, &swapChain1))
            || FAILED (swapChain1.As (&swapChain)))
        {
            failure = "DirectX 12: swapchain creation failed";
            shutdown();
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc {};
        heapDesc.NumDescriptors = static_cast<UINT> (frameCount);
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        if (FAILED (device->CreateDescriptorHeap (&heapDesc, IID_PPV_ARGS (&rtvHeap))))
        {
            failure = "DirectX 12: RTV heap creation failed";
            shutdown();
            return false;
        }

        const auto increment = device->GetDescriptorHandleIncrementSize (D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        auto handle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (int i = 0; i < frameCount; ++i)
        {
            if (FAILED (swapChain->GetBuffer (static_cast<UINT> (i), IID_PPV_ARGS (&backBuffers[i]))))
            {
                failure = "DirectX 12: back buffer acquisition failed";
                shutdown();
                return false;
            }
            device->CreateRenderTargetView (backBuffers[i].Get(), nullptr, handle);
            handle.ptr += increment;
        }

        fenceEvent = CreateEventW (nullptr, FALSE, FALSE, nullptr);
        if (fenceEvent == nullptr)
        {
            failure = "DirectX 12: fence event creation failed";
            shutdown();
            return false;
        }

        initialised = true;
        failure.clear();
        return true;
    }

    void resize (Config config) override
    {
        width = juce::jmax (1, config.width);
        height = juce::jmax (1, config.height);
        if (childWindow != nullptr)
            SetWindowPos (childWindow, nullptr, 0, 0, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        if (swapChain != nullptr)
        {
            for (auto& buffer : backBuffers) buffer.Reset();
            swapChain->ResizeBuffers (static_cast<UINT> (frameCount), static_cast<UINT> (width),
                                      static_cast<UINT> (height), DXGI_FORMAT_R8G8B8A8_UNORM, 0);
            auto handle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
            const auto increment = device->GetDescriptorHandleIncrementSize (D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            for (int i = 0; i < frameCount; ++i)
            {
                swapChain->GetBuffer (static_cast<UINT> (i), IID_PPV_ARGS (&backBuffers[i]));
                device->CreateRenderTargetView (backBuffers[i].Get(), nullptr, handle);
                handle.ptr += increment;
            }
        }
    }

    bool isPresentable() const noexcept override { return childWindow != nullptr && swapChain != nullptr; }

    void shutdown() noexcept override {
        if (queue != nullptr && fence != nullptr && fenceEvent != nullptr)
        {
            queue->Signal (fence.Get(), ++fenceValue);
            if (fence->GetCompletedValue() < fenceValue)
            {
                fence->SetEventOnCompletion (fenceValue, fenceEvent);
                WaitForSingleObject (fenceEvent, INFINITE);
            }
        }
        for (auto& buffer : backBuffers) buffer.Reset();
        rtvHeap.Reset();
        swapChain.Reset();
        commandList.Reset();
        allocators.clear();
        fence.Reset();
        queue.Reset();
        device.Reset();
        if (fenceEvent != nullptr) { CloseHandle (fenceEvent); fenceEvent = nullptr; }
        if (childWindow != nullptr) { DestroyWindow (childWindow); childWindow = nullptr; }
        initialised = false;
    }

    bool beginFrame() override
    {
        if (! initialised || swapChain == nullptr)
            return false;
        if (fence->GetCompletedValue() < frameFenceValues[frameIndex])
        {
            fence->SetEventOnCompletion (frameFenceValues[frameIndex], fenceEvent);
            WaitForSingleObject (fenceEvent, INFINITE);
        }
        auto& allocator = allocators[frameIndex];
        allocator->Reset();
        commandList->Reset (allocator.Get(), nullptr);
        return true;
    }

    void clear (juce::Colour colour) override
    {
        clearColour = colour;
        if (! initialised) return;
        const auto index = swapChain->GetCurrentBackBufferIndex();
        D3D12_RESOURCE_BARRIER barrier {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = backBuffers[index].Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier (1, &barrier);
        auto handle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T> (index) * device->GetDescriptorHandleIncrementSize (D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        const float values[] { colour.getFloatRed(), colour.getFloatGreen(), colour.getFloatBlue(), colour.getFloatAlpha() };
        commandList->OMSetRenderTargets (1, &handle, FALSE, nullptr);
        commandList->ClearRenderTargetView (handle, values, 0, nullptr);
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        commandList->ResourceBarrier (1, &barrier);
    }

    void endFrame() override
    {
        if (! initialised)
            return;
        commandList->Close();
        ID3D12CommandList* lists[] { commandList.Get() };
        queue->ExecuteCommandLists (1, lists);
        swapChain->Present (1, 0);
        frameFenceValues[frameIndex] = ++fenceValue;
        queue->Signal (fence.Get(), frameFenceValues[frameIndex]);
        frameIndex = swapChain->GetCurrentBackBufferIndex();
    }
    bool isInitialised() const noexcept override { return initialised; }
    juce::String status() const override {
        return initialised ? "DirectX 12 device/queue/fence ready" : failure;
    }

  private:
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ComPtr<IDXGISwapChain3> swapChain;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ComPtr<ID3D12Resource> backBuffers[3];
    std::vector<ComPtr<ID3D12CommandAllocator>> allocators;
    UINT64 frameFenceValues[3] {};
    HANDLE fenceEvent = nullptr;
    UINT64 fenceValue = 0;
    HWND childWindow = nullptr;
    size_t frameIndex = 0;
    juce::Colour clearColour;
    juce::String failure;
    int width = 1;
    int height = 1;
    int frameCount = 2;
    bool initialised = false;
};
} // namespace

std::unique_ptr<NativeRenderer> createD3D12Renderer() { return std::make_unique<D3D12Renderer>(); }
} // namespace j37::render
#else
namespace j37::render {
std::unique_ptr<NativeRenderer> createD3D12Renderer() { return {}; }
} // namespace j37::render
#endif
