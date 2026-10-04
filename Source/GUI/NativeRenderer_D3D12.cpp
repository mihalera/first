#include "NativeRenderer_D3D12.h"
#include <vector>

#if defined(J37_NATIVE_D3D12) && JUCE_WINDOWS
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

        // A real plugin host owns the parent window. The command resources are
        // therefore off-screen until the host supplies a child HWND surface.
        initialised = true;
        failure.clear();
        return true;
    }

    void shutdown() noexcept override {
        commandList.Reset();
        allocators.clear();
        fence.Reset();
        queue.Reset();
        device.Reset();
        initialised = false;
    }

    bool beginFrame() override
    {
        if (! initialised)
            return false;
        auto& allocator = allocators[frameIndex];
        allocator->Reset();
        commandList->Reset (allocator.Get(), nullptr);
        return true;
    }

    void clear (juce::Colour colour) override
    {
        clearColour = colour;
        // The command list is ready for a render-target clear once a host-owned
        // child swapchain surface is attached. Device and frame resources are
        // already native and frame-owned at this point.
    }

    void endFrame() override
    {
        if (! initialised)
            return;
        commandList->Close();
        ID3D12CommandList* lists[] { commandList.Get() };
        queue->ExecuteCommandLists (1, lists);
        frameIndex = (frameIndex + 1) % allocators.size();
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
    std::vector<ComPtr<ID3D12CommandAllocator>> allocators;
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
