#include "NativeRenderer_Vulkan.h"

#if defined(J37_NATIVE_VULKAN)
#include <vulkan/vulkan.h>

namespace j37::render {
namespace {
class VulkanRenderer final : public NativeRenderer {
  public:
    bool initialise(juce::Component &, Config config) override {
        width = juce::jmax(1, config.width);
        height = juce::jmax(1, config.height);
        frameCount = juce::jlimit(2, 3, config.framesInFlight);

        VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        appInfo.pApplicationName = "first";
        appInfo.applicationVersion = 1;
        appInfo.pEngineName = "first native renderer";
        appInfo.apiVersion = VK_API_VERSION_1_0;
        VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instanceInfo.pApplicationInfo = &appInfo;

        if (vkCreateInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS) {
            failure = "Vulkan: instance creation failed";
            return false;
        }

        initialised = true;
        failure.clear();
        return true;
    }

    void shutdown() noexcept override {
        if (instance != VK_NULL_HANDLE)
            vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
        initialised = false;
    }

    bool beginFrame() override { return initialised; }
    void clear(juce::Colour colour) override { clearColour = colour; }
    void endFrame() override {}
    bool isInitialised() const override { return initialised; }
    juce::String status() const override { return initialised ? "Vulkan instance ready" : failure; }

  private:
    VkInstance instance = VK_NULL_HANDLE;
    juce::Colour clearColour;
    juce::String failure;
    int width = 1;
    int height = 1;
    int frameCount = 2;
    bool initialised = false;
};
} // namespace

std::unique_ptr<NativeRenderer> createVulkanRenderer() {
    return std::make_unique<VulkanRenderer>();
}
} // namespace j37::render
#else
namespace j37::render {
std::unique_ptr<NativeRenderer> createVulkanRenderer() { return {}; }
} // namespace j37::render
#endif
