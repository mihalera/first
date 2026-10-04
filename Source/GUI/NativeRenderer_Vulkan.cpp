#include "NativeRenderer_Vulkan.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#if defined (J37_NATIVE_VULKAN)
 #include <vulkan/vulkan.h>
 #if JUCE_WINDOWS
  #include <windows.h>
 #endif
namespace j37::render
{
namespace
{
class VulkanRenderer final : public NativeRenderer
{
public:
    bool initialise (juce::Component& component, Config config) override
    {
        shutdown();
        width = juce::jmax (1, config.width);
        height = juce::jmax (1, config.height);
        position = { config.positionX, config.positionY };
        frameCount = juce::jlimit (2, 3, config.framesInFlight);

        if (! createInstance())
            return false;

       #if JUCE_WINDOWS
        if (! createWin32Surface (component))
            presentationNote = "Vulkan device will run without presentation surface";
       #else
        juce::ignoreUnused (component);
        presentationNote = "Vulkan presentation surface is unavailable on this platform";
       #endif
        if (! selectPhysicalDevice() || ! createDevice() || ! createFrameResources())
        {
            shutdown();
            return false;
        }

        if (surface != VK_NULL_HANDLE && ! createSwapchain())
        {
            if (device != VK_NULL_HANDLE)
                vkDeviceWaitIdle (device);
            destroySwapchain();
            destroySurfaceAndWindow();
            presentationNote = "Vulkan device ready; swapchain creation failed";
        }

        initialised = true;
        failure.clear();
        return true;
    }

    void resize (Config config) override
    {
        width = juce::jmax (1, config.width);
        height = juce::jmax (1, config.height);
        position = { config.positionX, config.positionY };

       #if JUCE_WINDOWS
        if (childWindow != nullptr)
            SetWindowPos (childWindow, nullptr, position.x, position.y, width, height,
                          SWP_NOZORDER | SWP_NOACTIVATE);
       #endif
        if (initialised && swapchain != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle (device);
            destroySwapchain();
            if (! createSwapchain())
                presentationNote = "Vulkan swapchain resize failed; using CPU fallback";
        }
    }

    bool isPresentable() const noexcept override
    {
        return initialised && swapchain != VK_NULL_HANDLE && ! swapchainImages.empty();
    }

    void shutdown() noexcept override
    {
        if (device != VK_NULL_HANDLE)
            vkDeviceWaitIdle (device);

        destroySwapchain();

        if (device != VK_NULL_HANDLE)
        {
            for (auto semaphore : imageAvailable)
                if (semaphore != VK_NULL_HANDLE) vkDestroySemaphore (device, semaphore, nullptr);
            for (auto semaphore : renderFinished)
                if (semaphore != VK_NULL_HANDLE) vkDestroySemaphore (device, semaphore, nullptr);
            for (auto fence : frameFences)
                if (fence != VK_NULL_HANDLE) vkDestroyFence (device, fence, nullptr);
            if (commandPool != VK_NULL_HANDLE) vkDestroyCommandPool (device, commandPool, nullptr);
            vkDestroyDevice (device, nullptr);
        }

        imageAvailable.clear(); renderFinished.clear(); frameFences.clear(); commandBuffers.clear();
        commandPool = VK_NULL_HANDLE; queue = VK_NULL_HANDLE; presentQueue = VK_NULL_HANDLE;
        device = VK_NULL_HANDLE; physicalDevice = VK_NULL_HANDLE;
        destroySurfaceAndWindow();

        if (instance != VK_NULL_HANDLE)
            vkDestroyInstance (instance, nullptr);
        instance = VK_NULL_HANDLE;
        initialised = false;
        frameIndex = 0;
        activeSlot = invalidIndex;
    }

    bool beginFrame() override
    {
        if (! initialised || activeSlot != invalidIndex)
            return false;

        const auto slot = frameIndex;
        if (vkWaitForFences (device, 1, &frameFences[slot], VK_TRUE, frameWaitNs) != VK_SUCCESS)
            return false;
        vkResetFences (device, 1, &frameFences[slot]);
        vkResetCommandBuffer (commandBuffers[slot], 0);

        VkCommandBufferBeginInfo beginInfo { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer (commandBuffers[slot], &beginInfo) != VK_SUCCESS)
            return false;

        activeSlot = slot;
        activeImage = 0;

        if (isPresentable())
        {
            const auto result = vkAcquireNextImageKHR (device, swapchain, frameWaitNs,
                                                       imageAvailable[slot], VK_NULL_HANDLE,
                                                       &activeImage);
            if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
            {
                vkEndCommandBuffer (commandBuffers[slot]);
                activeSlot = invalidIndex;
                return false;
            }
        }

        return true;
    }

    void clear (juce::Colour colour) override
    {
        clearColour = colour;
        if (activeSlot == invalidIndex)
            return;

        auto command = commandBuffers[activeSlot];
        if (! isPresentable())
            return;

        VkImageMemoryBarrier toTransfer { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransfer.srcAccessMask = 0;
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = swapchainImages[activeImage];
        toTransfer.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdPipelineBarrier (command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                              VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                              1, &toTransfer);

        VkClearColorValue clearValue {};
        clearValue.float32[0] = colour.getFloatRed();
        clearValue.float32[1] = colour.getFloatGreen();
        clearValue.float32[2] = colour.getFloatBlue();
        clearValue.float32[3] = colour.getFloatAlpha();
        const VkImageSubresourceRange range { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdClearColorImage (command, swapchainImages[activeImage],
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &range);

        VkImageMemoryBarrier toPresent = toTransfer;
        toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toPresent.dstAccessMask = 0;
        vkCmdPipelineBarrier (command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                              VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr,
                              1, &toPresent);
    }

    void endFrame() override
    {
        (activeSlot == invalidIndex)
            return;

        const auto slot = activeSlot;
        if (vkEndCommandBuffer (commandBuffers[slot]) != VK_SUCCESS)
        {
            activeSlot = invalidIndex;
            return;
        }

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkSubmitInfo submit { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        if (isPresentable())
        {
            submit.waitSemaphoreCount = 1;
            submit.pWaitSemaphores = &imageAvailable[slot];
            submit.pWaitDstStageMask = &waitStage;
            submit.signalSemaphoreCount = 1;
            submit.pSignalSemaphores = &renderFinished[slot];
        }
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &commandBuffers[slot];

        submit.waitSemaphoreCount = isPresentable() ? 1u : 0u;
        submit.signalSemaphoreCount = isPresentable() ? 1u : 0u;

        if (vkQueueSubmit (queue, 1, &submit, frameFences[slot]) != VK_SUCCESS)
        {
            activeSlot = invalidIndex;
            failure = "Vulkan: queue submission failed";
            return;
        }

        if (isPresentable())
        {
            VkPresentInfoKHR present { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
            present.waitSemaphoreCount = 1;
            present.pWaitSemaphores = &renderFinished[slot];
            present.swapchainCount = 1;
            present.pSwapchains = &swapchain;
            present.pImageIndices = &activeImage;
            const auto result = vkQueuePresentKHR (presentQueue, &present);
            if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR && result != VK_ERROR_OUT_OF_DATE_KHR)
                failure = "Vulkan: presentation failed";
        }

        frameIndex = (slot + 1) % static_cast<std::size_t> (frameCount);
        activeSlot = invalidIndex;
    }

    bool isInitialised() const noexcept override { return initialised; }

    juce::String status() const override
    {
        if (! initialised)
            return failure;
        if (! isPresentable())
            return presentationNote.isNotEmpty() ? presentationNote : "Vulkan device ready";
        return "Vulkan swapchain ready";
    }

private:
    static constexpr std::size_t invalidIndex = static_cast<std::size_t> (-1);
    static constexpr std::uint64_t frameWaitNs = 500ull * 1000ull * 1000ull;

    bool fail (juce::String reason)
    {
        failure = std::move (reason);
        shutdown();
        return false;
    }

    bool createInstance()
    {
        VkApplicationInfo app { VK_STRUCTURE_TYPE_APPLICATION_INFO };
        app.pApplicationName = "first";
        app.applicationVersion = VK_MAKE_VERSION (1, 0, 0);
        app.pEngineName = "first";
        app.apiVersion = VK_API_VERSION_1_0;

        std::vector<const char*> extensions;
        uint32_t extensionCount = 0;
        vkEnumerateInstanceExtensionProperties (nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> available (extensionCount);
        vkEnumerateInstanceExtensionProperties (nullptr, &extensionCount, available.data());
        auto has = [&] (const char* wanted)
        {
            return std::any_of (available.begin(), available.end(), [&] (const auto& item)
            { return std::strcmp (item.extensionName, wanted) == 0; });
        };
       #if JUCE_WINDOWS
        // The Win32 adapter creates a real child HWND surface. Requesting the
        // extensions only on that path keeps Linux/macOS device-only Vulkan
        // builds valid even when no XCB/Wayland surface is available.
        if (! has (VK_KHR_SURFACE_EXTENSION_NAME)
            || ! has (VK_KHR_WIN32_SURFACE_EXTENSION_NAME))
            return fail ("Vulkan: Win32 surface extensions are unavailable");
        extensions.push_back (VK_KHR_SURFACE_EXTENSION_NAME);
        extensions.push_back (VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
       #endif
        VkInstanceCreateInfo info { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        info.pApplicationInfo = &app;
        info.enabledExtensionCount = static_cast<uint32_t> (extensions.size());
        info.ppEnabledExtensionNames = extensions.data();
        if (vkCreateInstance (&info, nullptr, &instance) != VK_SUCCESS)
            return fail ("Vulkan: instance creation failed");
        return true;
    }

    bool selectPhysicalDevice()
    {
        uint32_t count = 0;
        vkEnumeratePhysicalDevices (instance, &count, nullptr);
        if (count == 0) return fail ("Vulkan: no physical device");
        candidates.resize (count);
        vkEnumeratePhysicalDevices (instance, &count, candidates.data());

        auto score = [] (VkPhysicalDevice gpu)
        {
            VkPhysicalDeviceProperties properties {};
            vkGetPhysicalDeviceProperties (gpu, &properties);
            return properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 0
                 : properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 1 : 2;
        };
        physicalDevice = *std::min_element (candidates.begin(), candidates.end(),
                                            [&] (auto a, auto b) { return score (a) < score (b); });
        return true;
    }

    bool createDevice()
    {
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties (physicalDevice, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families (count);
        vkGetPhysicalDeviceQueueFamilyProperties (physicalDevice, &count, families.data());
        graphicsFamily = UINT32_MAX;
        presentFamily = UINT32_MAX;

        for (uint32_t i = 0; i < count; ++i)
        {
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 && graphicsFamily == UINT32_MAX)
                graphicsFamily = i;
            if (surface != VK_NULL_HANDLE)
            {
                VkBool32 supported = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR (physicalDevice, i, surface, &supported);
                if (supported && presentFamily == UINT32_MAX) presentFamily = i;
            }
        }
        if (graphicsFamily == UINT32_MAX) return fail ("Vulkan: no graphics queue family");
        if (surface == VK_NULL_HANDLE) presentFamily = graphicsFamily;
        if (presentFamily == UINT32_MAX) return fail ("Vulkan: no present queue family");
        queueFamilyIndices = { graphicsFamily, presentFamily };

        const float priority = 1.0f;
        std::array<VkDeviceQueueCreateInfo, 2> queues {};
        const auto queueCount = graphicsFamily == presentFamily ? 1u : 2u;
        for (uint32_t i = 0; i < queueCount; ++i)
        {
            queues[i] = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
            queues[i].queueFamilyIndex = i == 0 ? graphicsFamily : presentFamily;
            queues[i].queueCount = 1;
            queues[i].pQueuePriorities = &priority;
        }
        VkPhysicalDeviceFeatures features {};
        VkDeviceCreateInfo info { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        info.queueCreateInfoCount = queueCount;
        info.pQueueCreateInfos = queues.data();
        info.pEnabledFeatures = &features;
        const char* deviceExtensions[] { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        if (surface != VK_NULL_HANDLE)
        {
            info.enabledExtensionCount = 1;
            info.ppEnabledExtensionNames = deviceExtensions;
        }
        if (vkCreateDevice (physicalDevice, &info, nullptr, &device) != VK_SUCCESS)
            return fail ("Vulkan: logical device creation failed");
        vkGetDeviceQueue (device, graphicsFamily, 0, &queue);
        vkGetDeviceQueue (device, presentFamily, 0, &presentQueue);
        return true;
    }

    bool createFrameResources()
    {
        VkCommandPoolCreateInfo pool { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool.queueFamilyIndex = graphicsFamily;
        if (vkCreateCommandPool (device, &pool, nullptr, &commandPool) != VK_SUCCESS)
            return fail ("Vulkan: command pool creation failed");

        commandBuffers.resize (frameCount);
        VkCommandBufferAllocateInfo allocation { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        allocation.commandPool = commandPool;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = static_cast<uint32_t> (commandBuffers.size());
        if (vkAllocateCommandBuffers (device, &allocation, commandBuffers.data()) != VK_SUCCESS)
            return fail ("Vulkan: command buffer allocation failed");

        imageAvailable.resize (frameCount);
        renderFinished.resize (frameCount);
        frameFences.resize (frameCount);
        for (int i = 0; i < frameCount; ++i)
        {
            VkSemaphoreCreateInfo semaphore { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
            VkFenceCreateInfo fence { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
            fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            if (vkCreateSemaphore (device, &semaphore, nullptr, &imageAvailable[i]) != VK_SUCCESS)
                return fail ("Vulkan: image-acquire semaphore creation failed");
            if (vkCreateSemaphore (device, &semaphore, nullptr, &renderFinished[i]) != VK_SUCCESS)
                return fail ("Vulkan: render-finished semaphore creation failed");
            if (vkCreateFence (device, &fence, nullptr, &frameFences[i]) != VK_SUCCESS)
                return fail ("Vulkan: frame fence creation failed");
        }
        return true;
    }

   #if JUCE_WINDOWS
    bool createWin32Surface (juce::Component& component)
    {
        const auto* peer = component.getPeer();
        auto* parent = peer != nullptr ? static_cast<HWND> (peer->getNativeHandle()) : nullptr;
        if (parent == nullptr) return false;
        childWindow = CreateWindowExW (0, L"STATIC", L"first-vulkan-surface",
                                       WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                       position.x, position.y, width, height, parent, nullptr,
                                       GetModuleHandleW (nullptr), nullptr);
        if (childWindow == nullptr) return false;
        VkWin32SurfaceCreateInfoKHR info { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
        info.hinstance = GetModuleHandleW (nullptr);
        info.hwnd = childWindow;
        if (vkCreateWin32SurfaceKHR (instance, &info, nullptr, &surface) != VK_SUCCESS)
        {
            destroySurfaceAndWindow();
            return false;
        }
        return true;
    }
   #endif
    void destroySurfaceAndWindow() noexcept
    {
        if (surface != VK_NULL_HANDLE && instance != VK_NULL_HANDLE)
            vkDestroySurfaceKHR (instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
       #if JUCE_WINDOWS
        if (childWindow != nullptr) { DestroyWindow (childWindow); childWindow = nullptr; }
       #endif
    }

    bool createSwapchain()
    {
        VkSurfaceCapabilitiesKHR caps {};
        if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR (physicalDevice, surface, &caps) != VK_SUCCESS)
            return false;
        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR (physicalDevice, surface, &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats (formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR (physicalDevice, surface, &formatCount, formats.data());
        if (formats.empty()) return false;
        const auto format = formats.front();
        auto extent = VkExtent2D { static_cast<uint32_t> (width), static_cast<uint32_t> (height) };
        extent.width = std::clamp (extent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp (extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
        auto imageCount = std::max (caps.minImageCount + 1,
                                     static_cast<uint32_t> (frameCount));
        if (caps.maxImageCount != 0)
            imageCount = std::min (imageCount, caps.maxImageCount);

        VkSwapchainCreateInfoKHR info { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
        info.surface = surface; info.minImageCount = imageCount;
        info.imageFormat = format.format; info.imageColorSpace = format.colorSpace;
        info.imageExtent = extent; info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = caps.currentTransform;
        info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        info.clipped = VK_TRUE;
        if (vkCreateSwapchainKHR (device, &info, nullptr, &swapchain) != VK_SUCCESS)
            return false;
        uint32_t actualCount = 0;
        vkGetSwapchainImagesKHR (device, swapchain, &actualCount, nullptr);
        swapchainImages.resize (actualCount);
        vkGetSwapchainImagesKHR (device, swapchain, &actualCount, swapchainImages.data());
        return ! swapchainImages.empty();
    }

    void destroySwapchain() noexcept
    {
        if (swapchain != VK_NULL_HANDLE && device != VK_NULL_HANDLE)
            vkDestroySwapchainKHR (device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
        swapchainImages.clear();
    }

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkPhysicalDevice> candidates;
    std::vector<VkCommandBuffer> commandBuffers;
    std::vector<VkSemaphore> imageAvailable, renderFinished;
    std::vector<VkFence> frameFences;
    std::vector<VkImage> swapchainImages;
    std::size_t frameIndex = 0;
    std::size_t activeSlot = invalidIndex;
    uint32_t activeImage = 0;
    uint32_t graphicsFamily = UINT32_MAX;
    uint32_t presentFamily = UINT32_MAX;
    int width = 1, height = 1, frameCount = 2;
    juce::Point<int> position;
    juce::Colour clearColour;
    juce::String failure, presentationNote;
    bool initialised = false;
   #if JUCE_WINDOWS
    HWND childWindow = nullptr;
   #endif
};
}

std::unique_ptr<NativeRenderer> createVulkanRenderer()
{
    return std::make_unique<VulkanRenderer>();
}
}
#else
namespace j37::render
{
std::unique_ptr<NativeRenderer> createVulkanRenderer() { return {}; }
}
#endif