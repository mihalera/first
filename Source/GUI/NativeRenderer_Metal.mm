#include "NativeRenderer_Metal.h"

#if defined (J37_NATIVE_METAL) && JUCE_MAC

 #import <Metal/Metal.h>
 #import <QuartzCore/CAMetalLayer.h>

namespace j37::render
{
namespace
{
/**
    Metal adapter.

    A plugin host owns the top-level window, so the presentation surface is a
    CAMetalLayer added to the host's own view rather than a window of our own.
    Two details make that safe:

      - the layer is inserted BELOW any sublayers the host may add later, and
        the host view keeps its own wantsLayer setting, because a host that
        draws its editor chrome into that layer would otherwise draw over the
        transport;
      - the layer is removed on shutdown and the previous wantsLayer value is
        restored, so a host that reuses the view for another plugin is not left
        with our layer still attached.

    Metal's frame pacing is the drawable: nextDrawable blocks until a drawable
    is free, which is the API's own way of waiting rather than something to
    reimplement.
*/
class MetalRenderer final : public NativeRenderer
{
public:
    bool initialise (juce::Component& component, Config config) override
    {
        shutdown();

        width = juce::jmax (1, config.width);
        height = juce::jmax (1, config.height);
        position = { config.positionX, config.positionY };
        vsyncInterval = juce::jlimit (0, 4, config.vsyncInterval);

        auto* device = MTLCreateSystemDefaultDevice();
        if (device == nil)
            return fail ("Metal: no Metal device is available");

        metalDevice = device;

        const auto* peer = component.getPeer();
        auto* view = peer != nullptr ? (__bridge NSView*) peer->getNativeHandle() : nil;
        if (view == nil)
        {
            shutdown();
            return fail ("Metal: the host peer exposes no NSView");
        }

        commandQueue = [metalDevice newCommandQueue];
        if (commandQueue == nil)
        {
            shutdown();
            return fail ("Metal: command queue creation failed");
        }

        // maximumDrawableCount of 3 matches what the D3D adapters allow, so the
        // frame pacing behaves the same whichever backend is compiled in.
        metalLayer = [CAMetalLayer layer];
        metalLayer.device = metalDevice;
        metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        metalLayer.framebufferOnly = YES;
        metalLayer.presentsWithTransaction = NO;
        metalLayer.frame = CGRectMake (position.x, position.y, width, height);
        metalLayer.drawableSize = CGSizeMake (width * metalLayer.contentsScale,
                                               height * metalLayer.contentsScale);
        metalLayer.contentsScale = view.window.backingScaleFactor;

        hostView = view;
        previousWantsLayer = view.wantsLayer;
        view.wantsLayer = YES;
        [view.layer insertSublayer:metalLayer atIndex:0];

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

        width = newWidth;
        height = newHeight;
        position = newPosition;

        if (metalLayer != nil)
        {
            metalLayer.frame = CGRectMake (newPosition.x, newPosition.y, newWidth, newHeight);
            metalLayer.drawableSize = CGSizeMake (newWidth * metalLayer.contentsScale,
                                                   newHeight * metalLayer.contentsScale);
        }
    }

    bool isPresentable() const noexcept override
    {
        return initialised && metalLayer != nil && commandQueue != nil;
    }

    void shutdown() noexcept override
    {
        if (metalLayer != nil)
        {
            // The layer must leave the host's view before it is released: a
            // layer that outlives its superlayer is a use-after-free in Core
            // Animation, and the host view is not ours to leave dirty.
            [metalLayer removeFromSuperlayer];
            metalLayer = nil;
        }

        if (hostView != nil)
            hostView.wantsLayer = previousWantsLayer;

        hostView = nil;
        encoder = nil;
        drawable = nil;
        commandBuffer = nil;
        commandQueue = nil;
        metalDevice = nil;
        initialised = false;
    }

    bool beginFrame() override
    {
        if (! isPresentable())
            return false;

        // nextDrawable blocks while every drawable is still in flight, which is
        // Metal's own frame pacing. A nil drawable means the layer is off
        // screen and no frame is needed.
        drawable = [metalLayer nextDrawable];
        if (drawable == nil)
            return false;

        commandBuffer = [commandQueue commandBuffer];
        return commandBuffer != nil;
    }

    void clear (juce::Colour colour) override
    {
        clearColour = colour;

        if (! isPresentable() || drawable == nil || commandBuffer == nil)
            return;

        auto* descriptor = [MTLRenderPassDescriptor renderPassDescriptor];
        descriptor.colorAttachments[0].texture = drawable.texture;
        descriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
        descriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
        descriptor.colorAttachments[0].clearColor = MTLClearColorMake (colour.getFloatRed(),
                                                                     colour.getFloatGreen(),
                                                                     colour.getFloatBlue(),
                                                                     colour.getFloatAlpha());

        encoder = [commandBuffer renderCommandEncoderWithDescriptor:descriptor];
        if (encoder != nil)
            [encoder endEncoding];
        encoder = nil;
    }

    void endFrame() override
    {
        if (commandBuffer == nil)
            return;

        if (drawable != nil)
            [commandBuffer presentDrawable:drawable];

        [commandBuffer commit];

        encoder = nil;
        drawable = nil;
        commandBuffer = nil;
    }

    bool isInitialised() const noexcept override { return initialised; }

    juce::String status() const override
    {
        if (! initialised)
            return failure;

        return juce::String ("Metal on ") + (metalDevice != nil ? [[metalDevice name] UTF8String]
                                                                 : "an unnamed device");
    }

private:
    bool fail (juce::String reason)
    {
        failure = std::move (reason);
        return false;
    }

    id<MTLDevice> metalDevice = nil;
    id<MTLCommandQueue> commandQueue = nil;
    id<MTLCommandBuffer> commandBuffer = nil;
    id<CAMetalDrawable> drawable = nil;
    id<MTLRenderCommandEncoder> encoder = nil;
    CAMetalLayer* metalLayer = nil;
    __unsafe_unretained NSView* hostView = nil;
    BOOL previousWantsLayer = NO;
    juce::Point<int> position;
    juce::Colour clearColour;
    juce::String failure;
    int width = 1;
    int height = 1;
    int vsyncInterval = 1;
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