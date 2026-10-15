#include "NativeRenderer_Metal.h"

#if defined (J37_NATIVE_METAL) && JUCE_MAC

 #import <Metal/Metal.h>
 #import <QuartzCore/CAMetalLayer.h>

namespace j37::render
{
namespace
{
/**
    The scale factor a CAMetalLayer on this view should carry.

    A layer without a scale draws at 1 pixel per point, which on a Retina Mac is
    half the panel's real resolution - soft, half-size-looking geometry next to a
    crisp JUCE panel around it. Two things make the naive read unavailable:

      - `view.window` is nil whenever the peer has no window yet, and a plugin
        host may hand over the peer before its window exists (and again on every
        editor re-open). A nil window is not "scale 1": it is "not known yet".
      - a window that is on no screen reports a scale of 0, which would make the
        drawable 0 x 0 and present nothing at all - a blank scene rather than a
        soft one.

    So the answer goes outwards: the view's window, then the main screen, then 1.
    The value is read again on every resize, because a host can move a plugin
    window between a Retina display and an external one without the editor
    changing size, which leaves the drawable at the wrong resolution for the
    screen it is on.
*/
CGFloat backingScaleFor (NSView* view)
{
    if (view != nil)
    {
        NSWindow* window = view.window;
        const auto scale = window != nil ? window.backingScaleFactor : 0.0;

        if (scale > 0.0)
            return scale;
    }

    NSScreen* screen = [NSScreen mainScreen];
    const auto screenScale = screen != nil ? screen.backingScaleFactor : 0.0;

    return screenScale > 0.0 ? screenScale : 1.0;
}

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
        // The drawable is sized in PIXELS while the layer's frame is in points,
        // so the scale has to be known BEFORE the drawable size is computed.
        // Setting contentsScale after drawableSize (which is what this did) left
        // every Retina Mac presenting a half-resolution frame until the first
        // resize, and a scale of 0 when the host had not given the peer a window
        // yet, which is a drawable of 0 x 0 and no frames at all.
        metalLayer.frame = CGRectMake (position.x, position.y, width, height);
        metalLayer.contentsScale = backingScaleFor (view);
        metalLayer.drawableSize = CGSizeMake (width * metalLayer.contentsScale,
                                               height * metalLayer.contentsScale);

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
            // Read the scale again, never keep the one from initialise(): the
            // host can move this editor to another display without resizing it,
            // and a drawable still sized for the display it came from is the
            // wrong resolution on the one it is on now.
            metalLayer.contentsScale = backingScaleFor (hostView);
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