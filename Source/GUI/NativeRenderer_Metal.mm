#include "NativeRenderer_Metal.h"

#if defined (J37_NATIVE_METAL) && JUCE_MAC
 #import <Metal/Metal.h>

namespace j37::render
{
namespace
{
class MetalRenderer final : public NativeRenderer
{
public:
    bool initialise (juce::Component&, Config config) override
    {
        width = juce::jmax (1, config.width);
        height = juce::jmax (1, config.height);
        frameCount = juce::jlimit (2, 3, config.framesInFlight);
        device = MTLCreateSystemDefaultDevice();
        if (device == nil)
        {
            failure = "Metal: no system device";
            return false;
        }

        queue = [device newCommandQueue];
        if (queue == nil)
        {
            failure = "Metal: command queue creation failed";
            shutdown();
            return false;
        }

        initialised = true;
        failure.clear();
        return true;
    }

    void shutdown() noexcept override
    {
        queue = nil;
        device = nil;
        initialised = false;
    }

    bool beginFrame() override { return initialised; }
    void clear (juce::Colour colour) override { clearColour = colour; }
    void endFrame() override {}
    bool isInitialised() const noexcept override { return initialised; }
    juce::String status() const override
    {
        return initialised ? "Metal device/queue ready" : failure;
    }

private:
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    juce::Colour clearColour;
    juce::String failure;
    int width = 1;
    int height = 1;
    int frameCount = 2;
    bool initialised = false;
};
}

std::unique_ptr<NativeRenderer> createMetalRenderer()
{
    return std::make_unique<MetalRenderer>();
}
}
#else
namespace j37::render
{
std::unique_ptr<NativeRenderer> createMetalRenderer() { return {}; }
}
#endif
