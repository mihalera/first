#include "NativeRenderer_Sokol.h"

#if defined (J37_NATIVE_SOKOL)
 #include <JuceHeader.h>

 // The declarations only: sokol_gfx's implementation lives in
 // Source/GUI/SokolGfxImpl.cpp, which is compiled without JUCE because JUCE wraps
 // the GL headers in a namespace and sokol wants them at global scope. The
 // backend choice is shared through SokolBackend.h so the two cannot disagree.
 //
 // sokol_gfx is a command recorder here, not a windowing layer: the surface this
 // adapter draws into is JUCE's OpenGLContext, attached to the scene component.
 // sokol_app.h is deliberately NOT used - it is sokol's own window and event
 // system, and JUCE already is one.
 #include "SokolBackend.h"
 #include <sokol_gfx.h>

 #include <algorithm>
 #include <mutex>

namespace j37::render
{
namespace
{
//==============================================================================
//  The deck's shader programs, ported to GLSL 410.
//
//  These are TapeSceneShaders.h's vertex and fragment programs in sokol's
//  dialect rather than JUCE's. Three things change and nothing else does:
//
//    * sokol's GLCORE backend hands the source to the driver verbatim, so the
//      #version line is written here (GLSL 410 is the desktop minimum sokol
//      accepts) and `attribute`/`varying`/`gl_FragColor` become `in`/`out`/
//      a declared fragment output - the rewrite JUCE's translator used to do
//      for the OpenGL path.
//    * TAPE_SCENE_HIGHP is already resolved: it is empty on desktop, which is
//      where this adapter runs (SokolBackend.h picks SOKOL_GLCORE), so the
//      qualifier is simply gone from the ported text.
//    * uNormalMatrix is a mat4 here and mat3 there. The SceneFrame carries a
//      full 4x4 inverse transpose because a native backend's shader multiplies
//      a float4 (see drawSceneFrameOpenGL's note), and mat3() of that takes the
//      same upper-left 3x3 the GL path extracts by hand.
//
//  The raw string literals are deliberate: the previous spellings of this port
//  used concatenated quoted lines, and one missing backslash silently put a
//  newline INSIDE a string literal - textually plausible, and a compile error
//  only in a SOKOL build.
//==============================================================================
constexpr const char vertexShaderSource[] = R"GLSL(
#version 410

in vec4 aPosition;
in vec3 aNormal;

uniform mat4 uViewProjection;
uniform mat4 uModel;
uniform mat4 uNormalMatrix;
uniform float uMode;
uniform float uPackInner;
uniform float uPackOuter;
uniform float uTime;
uniform float uFlutter;

out vec3 vNormal;
out float vDetail;
out vec2 vLocal;

void main()
{
    vec3 p = aPosition.xyz;
    vDetail = length (aPosition.xy);
    vLocal = aPosition.xy;

    if (uMode > 0.5 && uMode < 1.5)
    {
        float radius = mix (uPackInner, uPackOuter, aPosition.z);
        p = vec3 (aPosition.xy * radius, aPosition.w);
        vDetail = aPosition.z;
    }
    else if (uMode > 1.5 && uMode < 2.5)
    {
        float t = aPosition.w;
        float envelope = t * (1.0 - t) * 4.0;
        p.z += sin (t * 11.0 - uTime * 6.0) * uFlutter * 0.030 * envelope;
        p.y += sin (t *  7.0 + uTime * 4.0) * uFlutter * 0.026 * envelope;
        vDetail = t * 40.0;
    }
    else if (uMode > 2.5)
    {
        // The one mode with a lifetime: the glow under a working knob
        // breathes on the wall clock rather than on the transport, so it is
        // the one place uTime reaches a vertex.
        p.z += sin (uTime * 2.6) * 0.004 * aPosition.w;
    }

    vNormal = mat3 (uNormalMatrix) * aNormal;
    gl_Position = uViewProjection * uModel * vec4 (p, 1.0);
}
)GLSL";

constexpr const char fragmentShaderSource[] = R"GLSL(
#version 410

in vec3 vNormal;
in float vDetail;
in vec2 vLocal;

out vec4 fragColor;

uniform vec3 uBaseColour;
uniform vec3 uHighlightColour;
uniform vec3 uLightDirection;
uniform float uGlow;
uniform float uDrive;
uniform float uMode;
uniform float uTime;
uniform float uArcAngle;
uniform float uKeyDown;
uniform vec3 uPanelHalf;
uniform vec3 uTextureWeights;

// Subtle procedural surface texture for the control bank.
float hash (vec2 p)
{
    return fract (sin (dot (p, vec2 (127.1, 311.7))) * 43758.5453123);
}

float valueNoise (vec2 p)
{
    vec2 i = floor (p);
    vec2 f = fract (p);
    vec2 u = f * f * (3.0 - 2.0 * f);

    return mix (mix (hash (i), hash (i + vec2 (1.0, 0.0)), u.x),
                mix (hash (i + vec2 (0.0, 1.0)), hash (i + vec2 (1.0, 1.0)), u.x), u.y);
}

// Procedural grain, tape shimmer and broad wear marks share local
// coordinates and user-controlled weights.
vec3 panelTexture (vec2 face)
{
    vec3 textureAccumulator = vec3 (0.0);

    textureAccumulator += vec3 ((hash (face * 7.5) - 0.5) * uTextureWeights.x);

    float tapeNoise = hash (face * 40.0
                            + vec2 (0.0, uTime * 6.0 * (1.0 + uGlow)));
    textureAccumulator += vec3 ((tapeNoise - 0.5) * uTextureWeights.y);

    float wearPattern = valueNoise (face * 4.0);
    textureAccumulator -= vec3 ((1.0 - wearPattern) * uTextureWeights.z);

    return textureAccumulator;
}

void main()
{
    // Normals and lighting are expressed in scene/view space. The vertex
    // stage applies the inverse-transpose normal matrix for the model.
    vec3 n = normalize (vNormal);
    vec3 l = normalize (uLightDirection);
    vec3 eye = vec3 (0.0, 0.0, 1.0);
    vec3 h = normalize (l + eye);

    float diffuse = max (dot (n, l), 0.0);
    float specular = pow (max (dot (n, h), 0.0), 38.0);
    float rim = pow (1.0 - max (dot (n, eye), 0.0), 3.0);

    vec3 colour = uBaseColour * (0.20 + 0.90 * diffuse);
    colour += uHighlightColour * specular * 0.70;
    colour += uHighlightColour * rim * (0.10 + 0.50 * uGlow);
    // Keep display output in the valid linear range even under bright
    // highlight combinations and low-precision mobile fragment hardware.
    colour = clamp (colour, vec3 (0.0), vec3 (1.0));

    if (uMode < 0.5)
    {
        // The transport: the accent tint that follows DRIVE, then the detail
        // bands, which cost a fract and save every texture.
        colour = mix (colour, uHighlightColour, 0.20 * uDrive) * (1.0 + 0.30 * uDrive);

        float band = 0.88
                     + 0.12 * smoothstep (0.18, 0.50, abs (fract (vDetail) - 0.5));
        fragColor = vec4 (colour * band, 1.0);
        return;
    }

    if (uMode < 3.5)
    {
        // The control bank's body: a machined face with a brushed grain,
        // plus the arithmetic texture (see panelTexture above) sampled
        // against the face's local xy - grain, live tape shimmer and wear
        // in one accumulation, doses governed by uTextureWeights.
        float brush = 0.965
                      + 0.035 * abs (fract (vLocal.y * 46.0) - 0.5) * 2.0;
        colour += panelTexture (vLocal);
        fragColor = vec4 (colour * brush, 1.0);
        return;
    }

    if (uMode < 4.5)
    {
        // A knob. The same two lights as everything else, plus a sheen that
        // tracks the index mark - so the highlight walks round the knurl as
        // the knob turns, which is how a knob reads as turning even when the
        // mark is a few pixels long.
        float sheen = pow (max (dot (n, h), 0.0), 9.0);
        colour += uHighlightColour * sheen * 0.16;
        colour *= 1.0 - 0.10 * max (0.0, -n.z);

        // The lit arc uses the knob mesh's local xy coordinates.
        float r = length (vLocal);
        float angle = atan (vLocal.x, vLocal.y);
        // Fade radial edges and bound the angular sweep to its lit segment.
        float t = clamp ((angle - uArcAngle) / 0.30, 0.0, 1.0);
        float arc = smoothstep (0.70, 0.80, r) * (1.0 - smoothstep (0.90, 0.99, r));
        colour += uHighlightColour * arc * (1.0 - t) * (0.35 + 0.65 * uGlow);

        fragColor = vec4 (colour, 1.0);
        return;
    }

    if (uMode < 5.5)
    {
        // A keycap darkens slightly while pressed.
        colour *= 1.0 - 0.35 * uKeyDown;
        fragColor = vec4 (colour, 1.0);
        return;
    }

    // The panel face is the final material branch.
    vec2 uv = vLocal / uPanelHalf.xy;
    colour += panelTexture (vLocal);
    colour *= 1.0 - 0.28 * smoothstep (0.35, 1.15, length (uv));
    fragColor = vec4 (colour, 1.0);
}
)GLSL";

//==============================================================================
//  The uniform blocks, tightly packed.
//
//  SG_UNIFORMLAYOUT_NATIVE is sokol's GL packing rule: members sit back to back
//  with no padding, in declaration order, and the desc's glsl_uniforms[] names
//  map each one to the GL uniform of the same name. The static_asserts below
//  pin the sizes the validation layer recomputes (VALIDATE_SHADERDESC_UNIFORMBLOCK
//  _SIZE_MISMATCH), so a reordered member fails the build rather than the frame.
//==============================================================================
struct VsUniforms
{
    float viewProjection[16];
    float model[16];
    float normalMatrix[16];
    float mode;
    float packInner;
    float packOuter;
    float time;
    float flutter;
};

struct FsUniforms
{
    float baseColour[3];
    float highlightColour[3];
    float lightDirection[3];
    float textureWeights[3];
    float panelHalf[3];
    float glow;
    float drive;
    float mode;
    float time;
    float arcAngle;
    float keyDown;
};

static_assert (sizeof (VsUniforms) == 3 * 64 + 5 * 4, "VS uniform block size drifted");
static_assert (sizeof (FsUniforms) == 5 * 12 + 6 * 4, "FS uniform block size drifted");
static_assert (sizeof (SceneVertex) == 28, "SceneVertex is the 4+3 float layout the pipeline declares");

/**
    sokol_gfx adapter.

    THE SURFACE IS JUCE'S. An OpenGLContext is attached to the scene component
    and this object is its renderer, so the frame is recorded inside JUCE's own
    `renderOpenGL()` callback and JUCE swaps the buffers afterwards. That is the
    one difference from the other adapters and it is not a choice: JUCE does not
    expose "make the context current" on the message thread, so there is no way
    to submit a sokol frame from outside its callback. drawScene() therefore
    COPIES the frame under a mutex and requests a repaint; renderOpenGL() takes
    the copy and records it where the context is guaranteed to be current.

    WHAT IT DRAWS. The complete TapeScene frame - the same SceneFrame the
    OpenGL path draws, so the deck's arithmetic lives in one place
    (TapeSceneFrame.cpp) and this file is only its sokol spelling: shader,
    buffers, pipelines and per-draw uniforms.

    THREADING. drawScene/clear/beginFrame/endFrame arrive on the message
    thread; renderOpenGL runs on JUCE's GL thread. The one frame in flight
    crosses as a copied SceneFrame under a mutex - meshes are a shared immutable
    vector, so the copy is the draws list and a shared_ptr, not the geometry.

    RESOURCE LIFETIME. Everything GL/sokol-owned is created and destroyed with
    the context: newOpenGLContextCreated() sets the device up, and
    openGLContextClosing() discards every handle before sg_shutdown() - which
    must run WHILE the context is still current, so shutdown() detaches first
    and clears the renderer afterwards.
*/
class SokolRenderer final : public NativeRenderer,
                            private juce::OpenGLRenderer
{
public:
    bool initialise (juce::Component& component, Config config) override
    {
        shutdown();

        width = juce::jmax (1, config.width);
        height = juce::jmax (1, config.height);
        position = { config.positionX, config.positionY };

        context = std::make_unique<juce::OpenGLContext>();
        context->setComponentPaintingEnabled (false);

        // The scene needs a depth buffer, and the pipeline's declared
        // DEPTH_STENCIL only validates against a swapchain that has one - the
        // pair is requested here rather than hoped for from the driver's
        // default pixel format.
        juce::OpenGLPixelFormat pixelFormat;
        pixelFormat.depthBufferBits = 24;
        pixelFormat.stencilBufferBits = 8;
        context->setPixelFormat (pixelFormat);

        context->setRenderer (this);
        context->attachTo (component);

        if (! context->isAttached())
        {
            shutdown();
            return fail ("sokol_gfx: JUCE could not attach an OpenGL context");
        }

        // The device itself is created in newOpenGLContextCreated(), where the
        // context is current by construction. This flag is what tells the caller
        // whether that has happened yet.
        initialised = true;
        failure.clear();
        return true;
    }

    bool drawScene (const SceneFrame& frame) override
    {
        if (! deviceReady || ! sg_isvalid() || ! isValid (frame))
            return false;

        // A copy, because the caller's frame dies at the end of its scope and
        // this lands on the GL thread some time later. The meshes are a shared
        // immutable vector, so topology is not duplicated - only the draws.
        {
            const std::lock_guard<std::mutex> lock (frameMutex);
            pendingFrame = frame;
            hasPendingFrame = true;
            drawFailure.clear();
        }

        return true;
    }

    void resize (Config config) override
    {
        // A default-pass render uses the size it is handed, so there is nothing
        // to reallocate here - which is the whole of why sokol_gfx is the cheap
        // adapter to keep correct through a host resize.
        width = juce::jmax (1, config.width);
        height = juce::jmax (1, config.height);
        position = { config.positionX, config.positionY };

        if (context != nullptr)
            context->triggerRepaint();
    }

    bool isPresentable() const noexcept override
    {
        return initialised && context != nullptr && context->isAttached() && deviceReady;
    }

    void shutdown() noexcept override
    {
        if (context != nullptr)
        {
            // Detach FIRST: JUCE calls openGLContextClosing() on the GL thread
            // during the detach, which is the only place sg_shutdown() can run
            // with a current context. Clearing the renderer first would skip
            // that callback and leave sokol set up over a dead context.
            context->detach();
            context->setRenderer (nullptr);
            context.reset();
        }

        deviceReady = false;
        initialised = false;
    }

    bool beginFrame() override { return isPresentable(); }

    void clear (juce::Colour colour) override
    {
        clearColour.store (colour.getARGB(), std::memory_order_relaxed);
    }

    void endFrame() override
    {
        if (isPresentable())
            context->triggerRepaint();
    }

    bool isInitialised() const noexcept override { return initialised; }

    juce::String status() const override
    {
        if (! initialised)
            return failure;

        if (! deviceReady)
            return "sokol_gfx: the GL context has not produced a frame yet";

        const std::lock_guard<std::mutex> lock (frameMutex);
        if (drawFailure.isNotEmpty())
            return drawFailure;

        return juce::String ("sokol_gfx on ") + backendName();
    }

private:
    //==========================================================================
    //  juce::OpenGLRenderer
    //==========================================================================
    void newOpenGLContextCreated() override
    {
        sg_desc desc {};
        desc.environment.defaults.color_format = SG_PIXELFORMAT_RGBA8;
        desc.environment.defaults.depth_format = SG_PIXELFORMAT_DEPTH;
        desc.environment.defaults.sample_count = 1;

        sg_setup (&desc);
        deviceReady = sg_isvalid();

        // Every handle below belonged to the previous context. GL objects die
        // with it, so the cached ids must not outlive them.
        shader = {};
        mainPipeline = {};
        glowPipeline = {};
        gpuMeshes.clear();
        uploadedFrom = nullptr;
    }

    void renderOpenGL() override
    {
        if (! deviceReady || ! sg_isvalid())
            return;

        // Take the pending frame, if the message thread submitted one.
        SceneFrame frame;
        bool haveFrame = false;
        {
            const std::lock_guard<std::mutex> lock (frameMutex);
            if (hasPendingFrame)
            {
                frame = std::move (pendingFrame);
                pendingFrame = SceneFrame {};
                hasPendingFrame = false;
                haveFrame = true;
            }
        }

        // GPU resources are created here, never on the message thread: this is
        // the only callback where the context is current by construction.
        if (haveFrame
            && ! (ensureShader() && ensurePipelines() && ensureMeshes (frame)))
        {
            // Degrade to the clear below. The reason is written under the same
            // mutex drawScene() and status() use, so the panel reads either the
            // failure or a later success - never a torn string.
            const std::lock_guard<std::mutex> lock (frameMutex);
            if (drawFailure.isEmpty())
                drawFailure = "sokol_gfx: the scene frame could not be drawn";
            haveFrame = false;
        }

        juce::Colour colour;
        if (haveFrame)
        {
            colour = juce::Colour::fromFloatRGBA (frame.backgroundColour[0],
                                                   frame.backgroundColour[1],
                                                   frame.backgroundColour[2],
                                                   frame.backgroundColour[3]);
        }
        else
        {
            const auto argb = clearColour.load (std::memory_order_relaxed);
            colour = juce::Colour (static_cast<juce::uint32> (argb));
        }

        sg_pass_action action {};
        action.colors[0].load_action = SG_LOADACTION_CLEAR;
        action.colors[0].clear_value.r = colour.getFloatRed();
        action.colors[0].clear_value.g = colour.getFloatGreen();
        action.colors[0].clear_value.b = colour.getFloatBlue();
        action.colors[0].clear_value.a = colour.getFloatAlpha();
        action.depth.load_action = SG_LOADACTION_CLEAR;
        action.depth.clear_value = 1.0f;

        // sokol_gfx has no windowing layer of its own: the swapchain is described
        // by the caller, and here it is the default framebuffer (0) of JUCE's
        // context. The size is the size JUCE gave that framebuffer, which is the
        // only number that matches the surface being cleared, and the depth
        // format is the one initialise() asked JUCE for.
        sg_swapchain swapchain {};
        swapchain.width = width;
        swapchain.height = height;
        swapchain.sample_count = 1;
        swapchain.color_format = SG_PIXELFORMAT_RGBA8;
        swapchain.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
        swapchain.gl.framebuffer = 0;

        sg_pass pass {};
        pass.action = action;
        pass.swapchain = swapchain;

        sg_begin_pass (&pass);

        if (haveFrame)
            drawFrame (frame);

        sg_end_pass();
        sg_commit();
    }

    void openGLContextClosing() override
    {
        releaseSceneResources();

        if (sg_isvalid())
            sg_shutdown();

        deviceReady = false;
    }

    //==========================================================================
    //  Scene resources
    //==========================================================================

    /** The shader: both deck programs plus the uniform-block descriptions the
        GL backend maps onto them. One creation per context. */
    bool ensureShader()
    {
        if (shader.id != 0)
            return true;

        sg_shader_desc desc {};
        desc.vertex_func.source = vertexShaderSource;
        desc.fragment_func.source = fragmentShaderSource;

        desc.attrs[0].base_type = SG_SHADERATTRBASETYPE_FLOAT;
        desc.attrs[0].glsl_name = "aPosition";
        desc.attrs[1].base_type = SG_SHADERATTRBASETYPE_FLOAT;
        desc.attrs[1].glsl_name = "aNormal";

        auto& vsBlock = desc.uniform_blocks[0];
        vsBlock.stage = SG_SHADERSTAGE_VERTEX;
        vsBlock.size = sizeof (VsUniforms);
        vsBlock.layout = SG_UNIFORMLAYOUT_NATIVE;
        vsBlock.glsl_uniforms[0] = { SG_UNIFORMTYPE_MAT4, 1, "uViewProjection" };
        vsBlock.glsl_uniforms[1] = { SG_UNIFORMTYPE_MAT4, 1, "uModel" };
        vsBlock.glsl_uniforms[2] = { SG_UNIFORMTYPE_MAT4, 1, "uNormalMatrix" };
        vsBlock.glsl_uniforms[3] = { SG_UNIFORMTYPE_FLOAT, 1, "uMode" };
        vsBlock.glsl_uniforms[4] = { SG_UNIFORMTYPE_FLOAT, 1, "uPackInner" };
        vsBlock.glsl_uniforms[5] = { SG_UNIFORMTYPE_FLOAT, 1, "uPackOuter" };
        vsBlock.glsl_uniforms[6] = { SG_UNIFORMTYPE_FLOAT, 1, "uTime" };
        vsBlock.glsl_uniforms[7] = { SG_UNIFORMTYPE_FLOAT, 1, "uFlutter" };

        auto& fsBlock = desc.uniform_blocks[1];
        fsBlock.stage = SG_SHADERSTAGE_FRAGMENT;
        fsBlock.size = sizeof (FsUniforms);
        fsBlock.layout = SG_UNIFORMLAYOUT_NATIVE;
        fsBlock.glsl_uniforms[0] = { SG_UNIFORMTYPE_FLOAT3, 1, "uBaseColour" };
        fsBlock.glsl_uniforms[1] = { SG_UNIFORMTYPE_FLOAT3, 1, "uHighlightColour" };
        fsBlock.glsl_uniforms[2] = { SG_UNIFORMTYPE_FLOAT3, 1, "uLightDirection" };
        fsBlock.glsl_uniforms[3] = { SG_UNIFORMTYPE_FLOAT3, 1, "uTextureWeights" };
        fsBlock.glsl_uniforms[4] = { SG_UNIFORMTYPE_FLOAT3, 1, "uPanelHalf" };
        fsBlock.glsl_uniforms[5] = { SG_UNIFORMTYPE_FLOAT, 1, "uGlow" };
        fsBlock.glsl_uniforms[6] = { SG_UNIFORMTYPE_FLOAT, 1, "uDrive" };
        fsBlock.glsl_uniforms[7] = { SG_UNIFORMTYPE_FLOAT, 1, "uMode" };
        fsBlock.glsl_uniforms[8] = { SG_UNIFORMTYPE_FLOAT, 1, "uTime" };
        fsBlock.glsl_uniforms[9] = { SG_UNIFORMTYPE_FLOAT, 1, "uArcAngle" };
        fsBlock.glsl_uniforms[10] = { SG_UNIFORMTYPE_FLOAT, 1, "uKeyDown" };

        shader = sg_make_shader (&desc);

        if (shader.id == 0)
        {
            const std::lock_guard<std::mutex> lock (frameMutex);
            drawFailure = "sokol_gfx: the deck's shader could not be compiled";
            return false;
        }

        return true;
    }

    /** Two pipelines off the one shader: the scene's own depth-tested pass, and
        the additive-blend variant the glow disc draw with - the same split the
        OpenGL path makes with glEnable(GL_BLEND)/glBlendFunc(SRC_ALPHA, ONE). */
    bool ensurePipelines()
    {
        if (mainPipeline.id != 0)
            return true;

        if (! ensureShader())
            return false;

        sg_pipeline_desc desc {};
        desc.shader = shader;
        desc.layout.buffers[0].stride = static_cast<int> (sizeof (SceneVertex));
        desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT4;   // aPosition
        desc.layout.attrs[0].offset = 0;
        desc.layout.attrs[0].buffer_index = 0;
        desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3;   // aNormal
        desc.layout.attrs[1].offset = 16;
        desc.layout.attrs[1].buffer_index = 0;
        desc.index_type = SG_INDEXTYPE_UINT32;
        desc.color_count = 1;
        desc.colors[0].pixel_format = SG_PIXELFORMAT_RGBA8;
        desc.depth.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
        desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        desc.depth.write_enabled = true;
        desc.cull_mode = SG_CULLMODE_NONE;
        desc.sample_count = 1;

        mainPipeline = sg_make_pipeline (&desc);

        desc.colors[0].blend.enabled = true;
        desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE;
        desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
        desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE;

        glowPipeline = sg_make_pipeline (&desc);

        if (mainPipeline.id == 0 || glowPipeline.id == 0)
        {
            const std::lock_guard<std::mutex> lock (frameMutex);
            drawFailure = "sokol_gfx: the scene pipelines could not be created";
            return false;
        }

        return true;
    }

    /** Uploads the frame's shared topology once. Keyed on the identity of the
        frame's meshes vector - sharedSceneMeshes() is a function-local static,
        so the same address means byte-identical geometry. */
    bool ensureMeshes (const SceneFrame& frame)
    {
        if (! frame.meshes)
            return false;

        if (uploadedFrom == frame.meshes.get() && ! gpuMeshes.empty())
            return true;

        releaseMeshes();

        gpuMeshes.reserve (frame.meshes->size());

        for (const auto& geometry : *frame.meshes)
        {
            GpuMesh mesh;

            if (! geometry.indices.empty() && ! geometry.vertices.empty())
            {
                sg_buffer_desc vertices {};
                vertices.size = geometry.vertices.size() * sizeof (SceneVertex);
                vertices.data.ptr = geometry.vertices.data();
                vertices.data.size = vertices.size;
                vertices.usage.vertex_buffer = true;
                vertices.usage.immutable = true;
                mesh.vertices = sg_make_buffer (&vertices);

                sg_buffer_desc indices {};
                indices.size = geometry.indices.size() * sizeof (std::uint32_t);
                indices.data.ptr = geometry.indices.data();
                indices.data.size = indices.size;
                indices.usage.vertex_buffer = false;
                indices.usage.index_buffer = true;
                indices.usage.immutable = true;
                mesh.indices = sg_make_buffer (&indices);

                mesh.indexCount = static_cast<int> (geometry.indices.size());

                if (mesh.vertices.id == 0 || mesh.indices.id == 0)
                {
                    if (mesh.vertices.id != 0)
                        sg_destroy_buffer (mesh.vertices);
                    if (mesh.indices.id != 0)
                        sg_destroy_buffer (mesh.indices);

                    releaseMeshes();
                    const std::lock_guard<std::mutex> lock (frameMutex);
                    drawFailure = "sokol_gfx: vertex buffer allocation failed";
                    return false;
                }
            }

            gpuMeshes.push_back (mesh);
        }

        uploadedFrom = frame.meshes.get();
        return true;
    }

    /** Records every draw of a validated frame. Called inside the frame's
        pass; pipeline, bindings and both uniform blocks are re-applied per
        draw because that is what differs - the meshes are indexed and the
        material constants change with every draw. */
    void drawFrame (const SceneFrame& frame) noexcept
    {
        for (const auto& draw : frame.draws)
        {
            if (draw.meshIndex >= gpuMeshes.size())
                continue;

            const auto& mesh = gpuMeshes[draw.meshIndex];
            if (mesh.vertices.id == 0 || mesh.indices.id == 0 || mesh.indexCount <= 0)
                continue;

            VsUniforms vs {};
            std::copy_n (frame.viewProjection.data(), 16, vs.viewProjection);
            std::copy_n (draw.model.data(), 16, vs.model);
            std::copy_n (draw.normalMatrix.data(), 16, vs.normalMatrix);
            vs.mode = static_cast<float> (draw.material);
            vs.time = frame.timeSeconds;

            FsUniforms fs {};
            std::copy_n (draw.baseColour.data(), 3, fs.baseColour);
            std::copy_n (draw.highlightColour.data(), 3, fs.highlightColour);
            std::copy_n (frame.lightDirection.data(), 3, fs.lightDirection);
            std::copy_n (frame.textureWeights.data(), 3, fs.textureWeights);
            fs.mode = vs.mode;
            fs.time = vs.time;
            fs.drive = frame.drive;
            fs.glow = frame.glow;

            // The parameters array means something different per material -
            // the same switch drawSceneFrameOpenGL() makes on the OpenGL path.
            switch (draw.material)
            {
                case SceneMaterial::tapePack:
                    vs.packInner = draw.parameters[0];
                    vs.packOuter = draw.parameters[1];
                    break;
                case SceneMaterial::ribbon:
                    vs.flutter = draw.parameters[0];
                    break;
                case SceneMaterial::controlPanel:
                    fs.panelHalf[0] = draw.parameters[0];
                    fs.panelHalf[1] = draw.parameters[1];
                    fs.panelHalf[2] = draw.parameters[2];
                    break;
                case SceneMaterial::knob:
                    fs.arcAngle = draw.parameters[0];
                    break;
                case SceneMaterial::key:
                    fs.keyDown = draw.parameters[1];
                    break;
                case SceneMaterial::glow:
                    // A lit disc is light ON the face, so it is added rather
                    // than composited - and uGlow here is the disc's own
                    // amount, not the machine's.
                    fs.glow = draw.parameters[0];
                    break;
                case SceneMaterial::solid:
                    break;
            }

            sg_apply_pipeline (draw.material == SceneMaterial::glow ? glowPipeline
                                                                    : mainPipeline);

            sg_bindings bindings {};
            bindings.vertex_buffers[0] = mesh.vertices;
            bindings.index_buffer = mesh.indices;
            sg_apply_bindings (&bindings);

            const sg_range vsRange { &vs, sizeof (vs) };
            const sg_range fsRange { &fs, sizeof (fs) };
            sg_apply_uniforms (0, &vsRange);
            sg_apply_uniforms (1, &fsRange);

            sg_draw (0, mesh.indexCount, 1);
        }
    }

    void releaseMeshes() noexcept
    {
        if (! sg_isvalid())
        {
            gpuMeshes.clear();
            uploadedFrom = nullptr;
            return;
        }

        for (auto& mesh : gpuMeshes)
        {
            if (mesh.vertices.id != 0)
                sg_destroy_buffer (mesh.vertices);
            if (mesh.indices.id != 0)
                sg_destroy_buffer (mesh.indices);
        }

        gpuMeshes.clear();
        uploadedFrom = nullptr;
    }

    /** Discards everything GL-owned. Runs from openGLContextClosing(), with
        the context current, and from nowhere else: the handles are dead the
        moment the context is gone, so destroying them later would be GL calls
        against another surface. */
    void releaseSceneResources() noexcept
    {
        if (sg_isvalid())
        {
            releaseMeshes();

            if (shader.id != 0)
            {
                sg_destroy_shader (shader);
                shader = {};
            }

            if (mainPipeline.id != 0)
            {
                sg_destroy_pipeline (mainPipeline);
                mainPipeline = {};
            }

            if (glowPipeline.id != 0)
            {
                sg_destroy_pipeline (glowPipeline);
                glowPipeline = {};
            }
        }
        else
        {
            gpuMeshes.clear();
            uploadedFrom = nullptr;
            shader = {};
            mainPipeline = {};
            glowPipeline = {};
        }
    }

    static const char* backendName() noexcept
    {
        switch (sg_query_backend())
        {
            case SG_BACKEND_GLCORE:   return "OpenGL core";
            case SG_BACKEND_GLES3:    return "OpenGL ES 3";
            case SG_BACKEND_METAL_IOS:
            case SG_BACKEND_METAL_MACOS:
            case SG_BACKEND_METAL_SIMULATOR: return "Metal";
            case SG_BACKEND_D3D11:    return "Direct3D 11";
            case SG_BACKEND_WGPU:     return "WebGPU";
            default:                  return "an unknown backend";
        }
    }

    bool fail (juce::String reason)
    {
        failure = std::move (reason);
        return false;
    }

    struct GpuMesh
    {
        sg_buffer vertices {};
        sg_buffer indices {};
        int indexCount = 0;
    };

    std::unique_ptr<juce::OpenGLContext> context;
    std::atomic<juce::uint32> clearColour { 0xff000000u };
    juce::Point<int> position;
    juce::String failure;
    int width = 1;
    int height = 1;
    std::atomic<bool> deviceReady { false };
    bool initialised = false;

    // The frame crossing from the message thread to the GL thread.
    mutable std::mutex frameMutex;
    SceneFrame pendingFrame;
    bool hasPendingFrame = false;
    juce::String drawFailure;

    // GL-thread-only scene resources.
    sg_shader shader {};
    sg_pipeline mainPipeline {};
    sg_pipeline glowPipeline {};
    std::vector<GpuMesh> gpuMeshes;
    const void* uploadedFrom = nullptr;
};

//==============================================================================
//  Factory
//==============================================================================
std::unique_ptr<NativeRenderer> createSokolRenderer()
{
    return std::make_unique<SokolRenderer>();
}
} // namespace j37::render
#else
namespace j37::render
{
std::unique_ptr<NativeRenderer> createSokolRenderer() { return {}; }
}
#endif
