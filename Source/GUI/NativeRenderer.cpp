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
/** The path taken when no native adapter is compiled in, or when the selected
    backend has no adapter for this platform. It initialises, reports a usable
    status and never claims to be presentable, so the caller keeps painting with
    JUCE instead of leaving the component blank. */
class CpuRenderer final : public NativeRenderer {
  public:
    bool initialise (juce::Component&, Config config) override {
        bounds = { juce::jmax (1, config.width), juce::jmax (1, config.height) };
        initialised = true;
        return true;
    }

    void resize (Config config) override
    {
        bounds = { juce::jmax (1, config.width), juce::jmax (1, config.height) };
    }

    bool isPresentable() const noexcept override { return false; }
    void shutdown() noexcept override { initialised = false; }
    bool beginFrame() override { return initialised; }
    void clear(juce::Colour) override {}
    void endFrame() override {}
    bool isInitialised() const noexcept override { return initialised; }
    juce::String status() const override { return "CPU compatibility renderer"; }

  private:
    juce::Point<int> bounds;
    bool initialised = false;
};
} // namespace

std::unique_ptr<NativeRenderer> createNativeRenderer(Backend backend) {
#if defined(J37_NATIVE_D3D11)
    if (backend == Backend::directX11)
        if (auto renderer = createD3D11Renderer())
            return renderer;
#endif
#if defined(J37_NATIVE_D3D12)
    if (backend == Backend::directX12)
        if (auto renderer = createD3D12Renderer())
            return renderer;
#endif
#if defined(J37_NATIVE_VULKAN)
    if (backend == Backend::vulkan)
        if (auto renderer = createVulkanRenderer())
            return renderer;
#endif
#if defined(J37_NATIVE_METAL)
    if (backend == Backend::metal)
        if (auto renderer = createMetalRenderer())
            return renderer;
#endif
    return std::make_unique<CpuRenderer>();
}
} // namespace j37::render
