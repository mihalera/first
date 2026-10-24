#include "NativeRenderer_Metal.h"
#if defined (J37_NATIVE_METAL) && JUCE_MAC
 #import <Metal/Metal.h>
 #import <QuartzCore/CAMetalLayer.h>

#include "SceneLayout.h"
#include "TapeSceneMetalShaders.h"
#include <juce_gui_extra/embedding/juce_NSViewComponent.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <utility>
#include <vector>

namespace j37::render
{
namespace
{
CGFloat backingScaleFor (NSView* view)
{
    if (view.window != nil && view.window.backingScaleFactor > 0.0)
        return view.window.backingScaleFactor;
    NSScreen* screen = [NSScreen mainScreen];
    return screen != nil && screen.backingScaleFactor > 0.0 ? screen.backingScaleFactor : 1.0;
}

struct alignas(16) MetalUniforms
{
    simd_float4x4 viewProjection;
    simd_float4x4 model;
    simd_float4 normalColumn0;
    simd_float4 normalColumn1;
    simd_float4 normalColumn2;
    simd_float4 baseColour;
    simd_float4 highlightColour;
    simd_float4 parameters;
    simd_float4 globals;
    simd_float4 lightDirection;
};

simd_float4x4 matrix4(const std::array<float, 16>& source)
{
    simd_float4x4 result;
    std::memcpy(&result, source.data(), sizeof(result));
    return result;
}

simd_float4 colour4(const std::array<float, 4>& c)
{
    return {c[0], c[1], c[2], c[3]};
}

simd_float4 colour3(const std::array<float, 3>& c)
{
    return {c[0], c[1], c[2], 1.0f};
}

simd_float4 pack3(const std::array<float, 3>& c, float w)
{
    return {c[0], c[1], c[2], w};
}

class MetalRenderer final : public NativeRenderer
{
    struct GpuMesh
    {
        id<MTLBuffer> vertices = nil;
        id<MTLBuffer> indices = nil;
        NSUInteger indexCount = 0;
    };

    struct MetalDrawUniforms
    {
        simd_float4x4 viewProjection;
        simd_float4x4 model;
        simd_float4x4 normalMatrix;
        simd_float4 baseColour;
        simd_float4 highlightColour;
        simd_float4 parameters;
        simd_float4 globals;
        simd_float4 lightDirection;
    };

    static simd_float4x4 copyMatrix4 (const std::array<float, 16>& source)
    {
        simd_float4x4 result {};
        std::memcpy (&result, source.data(), sizeof (result));
        return result;
    }

    static simd_float4 makeColour4 (const std::array<float, 4>& c)
    {
        return { c[0], c[1], c[2], c[3] };
    }

    static simd_float4 makeColour3 (const std::array<float, 3>& c)
    {
        return { c[0], c[1], c[2], 1.0f };
    }

public:
    bool initialise(juce::Component& component, Config config) override
    {
        shutdown();
        juce::ignoreUnused (component);
        width = juce::jmax(1, config.width);
        height = juce::jmax(1, config.height);

        sceneView = std::make_unique<juce::NSViewComponent>();
        sceneView->setInterceptsMouseClicks(false, false);
        sceneView->setOpaque(false);
        component.addAndMakeVisible(*sceneView);
        sceneView->setBounds(config.positionX, config.positionY, width, height);

        auto* childPeer = sceneView->getPeer();
        auto* childView = childPeer != nullptr ? (__bridge NSView*) childPeer->getNativeHandle() : nil;
        if (childView == nil)
            return fail("Metal: editor child NSView unavailable");

        metalDevice = MTLCreateSystemDefaultDevice();
        if (metalDevice == nil)
            return fail("Metal: no system device");

        // Mesh topology is immutable and shared with OpenGL, not rebuilt per frame.
        sceneMeshes = TapeScene::sharedSceneMeshes();
        if (sceneMeshes == nullptr || sceneMeshes->size() != 8)
            return fail("Metal: shared scene meshes unavailable");

        metalLayer = [CAMetalLayer layer];
        metalLayer.device = metalDevice;
        metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        metalLayer.framebufferOnly = YES;
        metalLayer.presentsWithTransaction = NO;
        metalLayer.maximumDrawableCount = 3;
        metalLayer.contentsScale = backingScaleFor(childView);
        metalLayer.frame = CGRectMake(0.0, 0.0, width, height);
        metalLayer.drawableSize = CGSizeMake(width * metalLayer.contentsScale,
                                              height * metalLayer.contentsScale);
        scaleObserver = [[NSNotificationCenter defaultCenter]
            addObserverForName:NSWindowDidChangeScreenNotification object:childView.window
                         queue:nil usingBlock:^(__unused NSNotification* notification) {
                if (metalLayer != nil && childView.window != nil) {
                    metalLayer.contentsScale = backingScaleFor(childView);
                    metalLayer.drawableSize = CGSizeMake(width * metalLayer.contentsScale,
                                                          height * metalLayer.contentsScale);
                    depthTexture = nil;
                    createDepthTexture();
                }
            }];
        childView.wantsLayer = YES;
        [childView.layer addSublayer:metalLayer];

        commandQueue = [metalDevice newCommandQueue];
        if (commandQueue == nil)
            return fail("Metal: command queue creation failed");
        if (!createPipeline() || !createDepthState() || !uploadMeshes())
            return fail(failure.isNotEmpty() ? failure : "Metal: GPU resource creation failed");

        initialised = true;
        failure.clear();
        return true;
    }

    void resize(Config config) override
    {
        if (sceneView == nullptr && !initialised)
            return;
        width = juce::jmax(1, config.width);
        height = juce::jmax(1, config.height);
        if (sceneView != nullptr)
            sceneView->setBounds(config.positionX, config.positionY, width, height);
        if (metalLayer != nil)
        {
            auto* peer = sceneView != nullptr ? sceneView->getPeer() : nullptr;
            auto* view = peer != nullptr ? (__bridge NSView*) peer->getNativeHandle() : nil;
            metalLayer.contentsScale = backingScaleFor(view);
            metalLayer.frame = CGRectMake(0.0, 0.0, width, height);
            metalLayer.drawableSize = CGSizeMake(width * metalLayer.contentsScale,
                                                  height * metalLayer.contentsScale);
            depthTexture = nil;
            createDepthTexture();
        }
    }

    bool isPresentable() const noexcept override
    {
        return initialised && metalLayer != nil && commandQueue != nil
            && pipeline != nil && depthTexture != nil && !gpuMeshes.empty();
    }

    void shutdown() noexcept override
    {
        if (commandBuffer != nil)
            [commandBuffer waitUntilCompleted];
        if (scaleObserver != nil) {
            [[NSNotificationCenter defaultCenter] removeObserver:scaleObserver];
            scaleObserver = nil;
        }
        if (metalLayer != nil)
        {
            [metalLayer removeFromSuperlayer];
            metalLayer = nil;
        }
        gpuMeshes.clear();
        sceneMeshes.reset();
        pipeline = nil;
        depthState = nil;
        depthTexture = nil;
        library = nil;
        commandQueue = nil;
        metalDevice = nil;
        commandBuffer = nil;
        drawable = nil;
        encoder = nil;
        sceneView.reset();
        initialised = false;
    }

    bool beginFrame() override
    {
        if (!isPresentable())
            return false;
        drawable = [metalLayer nextDrawable];
        if (drawable == nil)
            return false;
        commandBuffer = [commandQueue commandBuffer];
        return commandBuffer != nil;
    }

    void clear(juce::Colour colour) override
    {
        if (!isPresentable() || drawable == nil || commandBuffer == nil)
            return;
        auto* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = drawable.texture;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(colour.getFloatRed(),
                                                                 colour.getFloatGreen(),
                                                                 colour.getFloatBlue(),
                                                                 colour.getFloatAlpha());
        if (depthTexture == nil && !createDepthTexture())
        {
            failure = "Metal: depth texture allocation failed";
            return;
        }
        pass.depthAttachment.texture = depthTexture;
        pass.depthAttachment.loadAction = MTLLoadActionClear;
        pass.depthAttachment.storeAction = MTLStoreActionDontCare;
        pass.depthAttachment.clearDepth = 1.0;
        encoder = [commandBuffer renderCommandEncoderWithDescriptor:pass];
        if (encoder != nil)
        {
            [encoder setDepthStencilState:depthState];
            [encoder setCullMode:MTLCullModeNone];
            activePassIsDrawable = true;
        }
    }

    bool drawScene(const SceneFrame& frame) override
    {
        activePassIsDrawable = false;
        if (!isPresentable() || encoder == nil || !isValid(frame)
            || frame.meshes == nullptr || frame.meshes->size() != gpuMeshes.size())
            return false;

        for (const auto& draw : frame.draws)
        {
            if (draw.meshIndex >= gpuMeshes.size())
                return false;
            MetalUniforms uniforms {};
            uniforms.viewProjection = matrix4(frame.viewProjection);
            uniforms.model = matrix4(draw.model);
            uniforms.normalMatrix = matrix4(draw.normalMatrix);
            uniforms.baseColour = colour4(draw.baseColour);
            uniforms.highlightColour = colour3(draw.highlightColour);
            uniforms.parameters = {draw.parameters[0], draw.parameters[1], draw.parameters[2],
                                   static_cast<float>(draw.material)};
            uniforms.globals = {frame.timeSeconds, frame.drive, frame.glow, frame.textureWeights[2]};
            uniforms.lightDirection = {frame.lightDirection[0], frame.lightDirection[1],
                                       frame.lightDirection[2], frame.textureWeights[1]};

            [encoder setRenderPipelineState:pipeline];
            [encoder setVertexBuffer:gpuMeshes[draw.meshIndex].vertices offset:0 atIndex:0];
            [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
            [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                indexCount:gpuMeshes[draw.meshIndex].indexCount
                                 indexType:MTLIndexTypeUInt32
                               indexBuffer:gpuMeshes[draw.meshIndex].indices
                         indexBufferOffset:0];
        }
        return true;
    }

    void endFrame() override
    {
        if (commandBuffer == nil)
            return;
        if (encoder == nil && drawable != nil)
        {
            auto* fallbackPass = [MTLRenderPassDescriptor renderPassDescriptor];
            fallbackPass.colorAttachments[0].texture = drawable.texture;
            fallbackPass.colorAttachments[0].loadAction = MTLLoadActionClear;
            fallbackPass.colorAttachments[0].storeAction = MTLStoreActionStore;
            fallbackPass.colorAttachments[0].clearColor = MTLClearColorMake(0.04, 0.05, 0.07, 1.0);
            encoder = [commandBuffer renderCommandEncoderWithDescriptor:fallbackPass];
        }
        if (encoder != nil)
        {
            [encoder endEncoding];
            encoder = nil;
        }
        if (drawable != nil && activePassIsDrawable)
            [commandBuffer presentDrawable:drawable];
        else if (drawable != nil)
            failure = "Metal: scene draw refused; renderer fallback required";
        [commandBuffer commit];
        drawable = nil;
        commandBuffer = nil;
        activePassIsDrawable = false;
    }

    bool isInitialised() const noexcept override { return initialised; }
    juce::String status() const override { return initialised ? "Metal SceneFrame renderer" : failure; }

private:
    bool fail(juce::String message)
    {
        failure = std::move(message);
        shutdown();
        failure = std::move(message);
        return false;
    }

    bool createPipeline()
    {
        NSError* error = nil;
        NSString* source = [NSString stringWithUTF8String:j37::render::metal::sceneShaderSource];
        library = [metalDevice newLibraryWithSource:source options:nil error:&error];
        if (library == nil)
        {
            failure = error != nil ? juce::String([[error localizedDescription] UTF8String])
                                   : "Metal shader compilation failed";
            return false;
        }
        MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
        desc.vertexFunction = [library newFunctionWithName:@"tapeSceneVertex"];
        desc.fragmentFunction = [library newFunctionWithName:@"tapeSceneFragment"];
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        desc.vertexDescriptor = vertexDescriptor();
        if (desc.vertexFunction == nil || desc.fragmentFunction == nil)
            return false;
        auto* blend = desc.colorAttachments[0];
        blend.blendingEnabled = YES;
        blend.rgbBlendOperation = MTLBlendOperationAdd;
        blend.alphaBlendOperation = MTLBlendOperationAdd;
        blend.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        blend.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        blend.sourceAlphaBlendFactor = MTLBlendFactorOne;
        blend.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        pipeline = [metalDevice newRenderPipelineStateWithDescriptor:desc error:&error];
        if (pipeline == nil)
            failure = error != nil ? juce::String([[error localizedDescription] UTF8String])
                                   : "Metal render pipeline creation failed";
        return pipeline != nil;
    }

    MTLVertexDescriptor* vertexDescriptor()
    {
        auto* desc = [MTLVertexDescriptor vertexDescriptor];
        desc.attributes[0].format = MTLVertexFormatFloat4;
        desc.attributes[0].offset = offsetof(SceneVertex, position);
        desc.attributes[0].bufferIndex = 0;
        desc.attributes[1].format = MTLVertexFormatFloat3;
        desc.attributes[1].offset = offsetof(SceneVertex, normal);
        desc.attributes[1].bufferIndex = 0;
        desc.layouts[0].stride = sizeof(SceneVertex);
        desc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
        return desc;
    }

    bool createDepthState()
    {
        auto* desc = [MTLDepthStencilDescriptor new];
        desc.depthCompareFunction = MTLCompareFunctionLessEqual;
        desc.depthWriteEnabled = YES;
        depthState = [metalDevice newDepthStencilStateWithDescriptor:desc];
        return depthState != nil && createDepthTexture();
    }

    bool createDepthTexture()
    {
        if (metalLayer == nil)
            return false;
        MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                         width:juce::jmax(1, (int) metalLayer.drawableSize.width)
                                                                                        height:juce::jmax(1, (int) metalLayer.drawableSize.height)
                                                                                     mipmapped:NO];
        desc.storageMode = MTLStorageModePrivate;
        desc.usage = MTLTextureUsageRenderTarget;
        depthTexture = [metalDevice newTextureWithDescriptor:desc];
        return depthTexture != nil;
    }

    bool uploadMeshes()
    {
        if (sceneMeshes == nullptr || sceneMeshes->size() != 8)
            return false;
        gpuMeshes.resize(sceneMeshes->size());
        for (std::size_t i = 0; i < sceneMeshes->size(); ++i)
        {
            const auto& source = (*sceneMeshes)[i];
            if (source.vertices.empty() || source.indices.empty())
                return false;
            auto& gpu = gpuMeshes[i];
            gpu.vertices = [metalDevice newBufferWithBytes:source.vertices.data()
                                                    length:source.vertices.size() * sizeof(SceneVertex)
                                                   options:MTLResourceStorageModeShared];
            gpu.indices = [metalDevice newBufferWithBytes:source.indices.data()
                                                    length:source.indices.size() * sizeof(std::uint32_t)
                                                   options:MTLResourceStorageModeShared];
            gpu.indexCount = source.indices.size();
            if (gpu.vertices == nil || gpu.indices == nil)
                return false;
        }
        return true;
    }

    std::unique_ptr<juce::NSViewComponent> sceneView;
    std::shared_ptr<const std::vector<TapeScene::Geometry>> sceneMeshes;
    id<MTLDevice> metalDevice = nil;
    id<MTLCommandQueue> commandQueue = nil;
    id<MTLCommandBuffer> commandBuffer = nil;
    id<CAMetalDrawable> drawable = nil;
    id<MTLRenderCommandEncoder> encoder = nil;
    id<MTLDepthStencilState> depthState = nil;
    id<MTLTexture> depthTexture = nil;
    id<MTLLibrary> library = nil;
    id<MTLRenderPipelineState> pipeline = nil;
    id scaleObserver = nil;
    bool activePassIsDrawable = false;
    std::vector<GpuMesh> gpuMeshes;
    juce::String failure;
    juce::Point<int> position;
    int width = 1;
    int height = 1;
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
