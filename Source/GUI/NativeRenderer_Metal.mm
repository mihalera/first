#include "NativeRenderer_Metal.h"

#if defined (J37_NATIVE_METAL) && JUCE_MAC
 #import <Metal/Metal.h>
 #import <QuartzCore/CAMetalLayer.h>

namespace j37::render
{
namespace
{
class MetalRenderer final : public NativeRenderer
{
public:
    bool initialise (juce::Component& component, Config config) override
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

        auto* peer = component.getPeer();
        auto* view = peer != nullptr ? (__bridge NSView*) peer->getNativeHandle() : nil;
        if (view == nil)
        {
            failure = "Metal: native view is not available";
            shutdown();
            return false;
        }
        layer = [CAMetalLayer layer];
        layer.device = device;
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.drawableSize = CGSizeMake (width, height);
        view.wantsLayer = YES;
        [view.layer addSublayer:layer];

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

    void resize (Config config) override
    {
        width = juce::jmax (1, config.width);
        height = juce::jmax (1, config.height);
        if (layer != nil) layer.drawableSize = CGSizeMake (width, height);
    }

    bool isPresentable() const noexcept override { return layer != nil; }

    void shutdown() noexcept override
    {
        queue = nil;
        device = nil;
        initialised = false;
    }

    bool beginFrame() override
    {
        if (! initialised || layer == nil) return false;
        drawable = [layer nextDrawable];
        commandBuffer = [queue commandBuffer];
        return drawable != nil && commandBuffer != nil;
    }
    void clear (juce::Colour colour) override
    {
        clearColour = colour;
        if (commandBuffer != nil)
        {
            auto pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = drawable.texture;
            pass.colorAttachments[0].loadAction = MTLLoadActionClear;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            pass.colorAttachments[0].clearColor = MTLClearColorMake (colour.getFloatRed(), colour.getFloatGreen(), colour.getFloatBlue(), colour.getFloatAlpha());
            encoder = [commandBuffer renderCommandEncoderWithDescriptor:pass];
            [encoder endEncoding];
            encoder = nil;
        }
    }
    void endFrame() override
    {
        if (commandBuffer != nil)
        {
            [commandBuffer presentDrawable:drawable];
            [commandBuffer commit];
            commandBuffer = nil;
            drawable = nil;
        }
    }
    bool isInitialised() const noexcept override { return initialised; }
    juce::String status() const override
    {
        return initialised ? "Metal device/queue ready" : failure;
    }

private:
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    id<MTLCommandBuffer> commandBuffer = nil;
    id<CAMetalDrawable> drawable = nil;
    id<MTLRenderCommandEncoder> encoder = nil;
    CAMetalLayer* layer = nil;
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
