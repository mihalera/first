#include "NativeRenderer_Vulkan.h"

#if defined(J37_NATIVE_VULKAN)
#include <vulkan/vulkan.h>
#include <vector>

namespace j37::render {
namespace {
class VulkanRenderer final : public NativeRenderer {
  public:
    bool initialise(juce::Component &component, Config config) override {
        juce::ignoreUnused (component);
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

        if (vkCreateInstance (&instanceInfo, nullptr, &instance) != VK_SUCCESS) {
            failure = "Vulkan: instance creation failed";
            return false;
        }

        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices (instance, &deviceCount, nullptr);
        if (deviceCount == 0) { failure = "Vulkan: no physical device"; shutdown(); return false; }
        physicalDevices.resize (deviceCount);
        vkEnumeratePhysicalDevices (instance, &deviceCount, physicalDevices.data());
        physicalDevice = physicalDevices.front();
        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties (physicalDevice, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families (familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties (physicalDevice, &familyCount, families.data());
        for (uint32_t i = 0; i < familyCount; ++i)
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) { graphicsFamily = i; break; }
        if (graphicsFamily == UINT32_MAX) { failure = "Vulkan: no graphics queue family"; shutdown(); return false; }
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        queueInfo.queueFamilyIndex = graphicsFamily; queueInfo.queueCount = 1; queueInfo.pQueuePriorities = &priority;
        VkDeviceCreateInfo deviceInfo { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        deviceInfo.queueCreateInfoCount = 1; deviceInfo.pQueueCreateInfos = &queueInfo;
        if (vkCreateDevice (physicalDevice, &deviceInfo, nullptr, &device) != VK_SUCCESS)
        { failure = "Vulkan: logical device creation failed"; shutdown(); return false; }
        vkGetDeviceQueue (device, graphicsFamily, 0, &queue);
        VkCommandPoolCreateInfo poolInfo { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        poolInfo.queueFamilyIndex = graphicsFamily; poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        if (vkCreateCommandPool (device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS)
        { failure = "Vulkan: command pool creation failed"; shutdown(); return false; }
        commandBuffers.resize (static_cast<size_t> (frameCount));
        VkCommandBufferAllocateInfo allocation { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        allocation.commandPool = commandPool; allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = static_cast<uint32_t> (commandBuffers.size());
        if (vkAllocateCommandBuffers (device, &allocation, commandBuffers.data()) != VK_SUCCESS)
        { failure = "Vulkan: command buffer allocation failed"; shutdown(); return false; }

        initialised = true;
        failure.clear();
        return true;
    }

    void resize (Config config) override
    {
        width = juce::jmax (1, config.width);
        height = juce::jmax (1, config.height);
    }

    bool isPresentable() const noexcept override { return false; }

    void shutdown() noexcept override
    {
        if (device != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle (device);
            if (commandPool != VK_NULL_HANDLE) vkDestroyCommandPool (device, commandPool, nullptr);
            vkDestroyDevice (device, nullptr);
        }
        commandBuffers.clear(); commandPool = VK_NULL_HANDLE; device = VK_NULL_HANDLE; queue = VK_NULL_HANDLE;
        physicalDevice = VK_NULL_HANDLE;
        if (instance != VK_NULL_HANDLE) vkDestroyInstance (instance, nullptr);
        instance = VK_NULL_HANDLE; initialised = false;
    }

    bool beginFrame() override
    {
        if (! initialised) return false;
        currentCommandBuffer = commandBuffers[frameIndex++ % commandBuffers.size()];
        VkCommandBufferBeginInfo beginInfo { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        return vkBeginCommandBuffer (currentCommandBuffer, &beginInfo) == VK_SUCCESS;
    }
    void clear (juce::Colour colour) override { clearColour = colour; }
    void endFrame() override
    {
        if (! initialised) return;
        vkEndCommandBuffer (currentCommandBuffer);
        VkSubmitInfo submit { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit.commandBufferCount = 1; submit.pCommandBuffers = &currentCommandBuffer;
        vkQueueSubmit (queue, 1, &submit, VK_NULL_HANDLE); vkQueueWaitIdle (queue);
    }
    bool isInitialised() const override { return initialised; }
    juce::String status() const override { return initialised ? "Vulkan instance ready" : failure; }

  private:
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;
    std::vector<VkPhysicalDevice> physicalDevices;
    uint32_t graphicsFamily = UINT32_MAX;
    size_t frameIndex = 0;
    VkCommandBuffer currentCommandBuffer = VK_NULL_HANDLE;
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
