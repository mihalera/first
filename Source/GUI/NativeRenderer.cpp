#include "NativeRenderer.h"

#if defined(J37_NATIVE_D3D11)
#include "NativeRenderer_D3D11.h"
#endif
#if defined(J37_NATIVE_D3D12)
#include "NativeRenderer_D3D12.h"
#endif
#if defined(J37_NATIVE_VULKAN)
#include "NativeRenderer_Vulkan.h"
#endif
#if defined(J37_NATIVE_METAL)
#include "NativeRenderer_Metal.h"
#endif
namespace j37::render {
namespace {
class CpuRenderer final : public NativeRenderer {
  public:
    bool initialise(juce::Component &, Config config) override {
        size = { juce::jmax (1, config.width), juce::jmax (1, config.height) };
        initialised = true;
        return true;
    }

    void resize (Config config) override
    {
        size = { juce::jmax (1, config.width), juce::jmax (1, config.height) };
    }

    bool isPresentable() const noexcept override { return false; }
    void shutdown() noexcept override { initialised = false; }
    bool beginFrame() override { return initialised; }
    void clear(juce::Colour) override {}
    void endFrame() override {}
    bool isInitialised() const noexcept override { return initialised; }
    juce::String status() const override { return "CPU compatibility renderer"; }

  private:
    juce::Point<int> size;
    bool initialised = false;
};
} // namespace

std::unique_ptr<NativeRenderer> createNativeRenderer(Backend backend) {
#if defined(J37_NATIVE_D3D11)
    if (backend == Backend::directX11)
        return createD3D11Renderer();
#endif
#if defined(J37_NATIVE_D3D12)
    if (backend == Backend::directX12)
        return createD3D12Renderer();
#endif
#if defined(J37_NATIVE_VULKAN)
    if (backend == Backend::vulkan)
        return createVulkanRenderer();
#endif
#if defined(J37_NATIVE_METAL)
    if (backend == Backend::metal)
        return createMetalRenderer();
#endif
    return std::make_unique<CpuRenderer>();
}
} // namespace j37::render
