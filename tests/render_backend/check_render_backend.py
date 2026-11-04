#!/usr/bin/env python3
"""Static contract checks for native-renderer capability selection."""
from pathlib import Path
import re
import sys
root = Path(__file__).resolve().parents[2]
backend = (root / "Source/GUI/RenderBackend.h").read_text()
setup = (root / "Source/GUI/EditorSetup.cpp").read_text()
timer = (root / "Source/GUI/EditorTimer.cpp").read_text()
preferences = (root / "Source/GUI/EditorPreferences.h").read_text()

errors = []

def require(condition: bool, message: str) -> None:
    if not condition:
        errors.append(message)

cmake = (root / "CMakeLists.txt").read_text()

# Sokol's shaders are desktop GLSL 410; its Android backend selects GLES3, so a
# Sokol APK cannot create a pipeline. That is a second, independent Android bug
# from the launcher crash fixed by MainActivity.java, but it is still real: the
# platform now defaults to OpenGL and an explicit Sokol request is a configure
# error rather than a bad APK.
android_default = re.search(
    r'if\(CMAKE_SYSTEM_NAME STREQUAL "Android"\)\s*'
    r'set\(J37_DEFAULT_RENDER_BACKEND "OPENGL"\)\s*'
    r'else\(\)\s*'
    r'set\(J37_DEFAULT_RENDER_BACKEND "SOKOL"\)', cmake)
require(android_default is not None,
        "Android must default to OpenGL and every other platform to Sokol")
require('set(J37_RENDER_BACKEND "${J37_DEFAULT_RENDER_BACKEND}" CACHE STRING' in cmake,
        "J37_RENDER_BACKEND must take the platform default")
sokol_branch = cmake.find('elseif(J37_RENDER_BACKEND STREQUAL "SOKOL")')
android_guard = cmake.find('if(CMAKE_SYSTEM_NAME STREQUAL "Android")', sokol_branch)
sokol_native = cmake.find("J37_NATIVE_SOKOL=1", sokol_branch)
require(sokol_branch >= 0 and sokol_branch < android_guard < sokol_native
        and "message(FATAL_ERROR" in cmake[android_guard:sokol_native],
        "an explicit Sokol build on Android must fail at configure time, not ship a broken APK")

# The native implementation gate follows the backend's build macro.
require("constexpr bool hasNativeImplementation(Backend backend) noexcept {" in backend,
        "hasNativeImplementation must check build macros per backend")
require("if (hasNativeImplementation(configured))" in backend,
        "effectiveBackend must gate the configured native value on scene-draw capability")

# J37_NATIVE_* means "this adapter draws the complete SceneFrame", not "the
# platform SDK was found". The staged adapters must not be compiled in as scene
# backends until their drawScene path is complete; otherwise a native build
# advertises a renderer that only clears its surface.
for staged in ("VULKAN", "D3D11", "D3D12", "BGFX"):
    require(f"J37_NATIVE_{staged}=1" not in cmake,
            f"{staged} is still staged and must not define J37_NATIVE_{staged}")
for complete in ("SOKOL", "METAL"):
    require(f"J37_NATIVE_{complete}=1" in cmake,
            f"{complete} draws the scene and must define J37_NATIVE_{complete}")
require("constexpr bool usesNativeCommandRenderer() noexcept\n{\n    return false;" in backend,
        "native command renderer must not be advertised as active")
# The panel's JUCE OpenGL context is independent of the deck's chosen renderer:
# a SOKOL/native build can still accelerate the 2D panel, and only an explicit
# CPU choice should detach it. The CPU paint path remains JUCE's fallback when
# the context cannot be created.
require("const auto running = j37::render::runningBackend(renderBackendChoice);" in setup,
        "editor must resolve the running backend before choosing a paint path")
require("for (const auto backend : j37::render::allBackends)" in setup,
        "the backend menu must iterate over the complete backend list")
require("! j37::render::isBackendSelectable (backend)" in setup,
        "the backend menu must omit backends this build cannot run")
require("j37::render::backendChoiceId (backend)" in setup,
        "every backend menu item must use its stable choice ID")
require("backendFromChoiceId (backendBox.getSelectedId())" in setup,
        "backend menu selections must map back to their backend value")
require("backendChoiceId (renderBackendChoice)" in setup,
        "the remembered backend must select the matching menu item")
require("for (const auto backend : j37::render::allBackends)" in preferences,
        "preferences must restore every backend supported by the current build")
require("j37::render::isBackendSelectable (backend)" in preferences,
        "preferences must reject a renderer unavailable in the current build")
require("constexpr std::array<Backend, 9> allBackends" in backend,
        "all backend values must be listed once for menu and preference handling")
require("return Backend::autoDetect;" in backend[backend.index("constexpr Backend backendFromChoiceId"):],
        "an invalid backend menu ID must safely resolve to AUTO")
choice_test = (root / "tests/render_backend/backend_choice_test.cpp").read_text()
require("static_assert (j37::render::backendChoiceId (Backend::sokol) == 7);" in choice_test,
        "the Sokol menu choice must have a stable ID")
require("backendFromChoiceId (0) == Backend::autoDetect" in choice_test
        and "backendFromChoiceId (10) == Backend::autoDetect" in choice_test,
        "invalid menu IDs must be covered by the executable choice tests")
workflow = (root / ".github/workflows/build.yml").read_text()
require("run: bash tests/render_backend/run.sh" in workflow,
        "the executable backend choice tests must run in CI")
require("const auto panelOnGpu = running != j37::render::Backend::cpu;" in setup,
        "panel acceleration must follow CPU choice, not the deck's OpenGL backend")
require("openGLContext.setComponentPaintingEnabled(true);" in setup,
        "the panel must enable JUCE component painting on its OpenGL context")
require("openGLContext.attachTo(*this);" in setup,
        "the panel context must be attached best-effort, leaving JUCE's CPU path as fallback")
require("tapeScene.setSceneEnabled(deckOnOpenGl);" in setup,
        "the deck's OpenGL scene must be gated separately from panel acceleration")
require("nativeSceneAttachAttemptsLeft = 90;" in setup,
        "failed native scene attachment must schedule bounded startup retries")
require("if (nativeRenderer == nullptr)\n            setUpNativeScene();" in timer
        and "nativeSceneAttachAttemptsLeft = 0;" in timer,
        "the timer must retry a missing native scene adapter until it attaches")
require("int nativeSceneAttachAttemptsLeft = 0;" in (root / "Source/PluginEditor.h").read_text(),
        "native scene retry state must be owned by the editor")

# The two GPU abstraction layers are build OPTIONS, not additions: each owns the
# device and the surface, so a build selects one and the other is not compiled.
for name in ("SOKOL", "BGFX"):
    require(f"J37_RENDER_BACKEND_{name}" in backend,
            f"{name} must map to its own Backend value")
    require(f'J37_RENDER_BACKEND_{name}=1' in cmake,
            f"{name} must be a selectable J37_RENDER_BACKEND")

require("backend == Backend::sokol" in (root / "Source/GUI/NativeRenderer.cpp").read_text(),
        "the sokol adapter must be reachable from createNativeRenderer")
require("backend == Backend::bgfx" in (root / "Source/GUI/NativeRenderer.cpp").read_text(),
        "the bgfx adapter must be reachable from createNativeRenderer")

# sokol_gfx's implementation is compiled once, in a JUCE-free TU: JUCE wraps the
# GL headers in a namespace and sokol needs them at global scope.
sokol_impl = (root / "Source/GUI/SokolGfxImpl.cpp").read_text()
require("SOKOL_IMPL" in sokol_impl and "#include <JuceHeader.h>" not in sokol_impl,
        "sokol_gfx's implementation must be compiled once without JUCE's GL namespace")
require("#include <sokol_gfx.h>" in (root / "Source/GUI/NativeRenderer_Sokol.cpp").read_text(),
        "the sokol adapter must include sokol_gfx itself")
sokol = (root / "Source/GUI/NativeRenderer_Sokol.cpp").read_text()
sokol_backend = (root / "Source/GUI/SokolBackend.h").read_text()
require("context->setContinuousRepainting (false);" in sokol
        and "context->triggerRepaint();" in sokol,
        "Sokol must render only when a new scene frame is submitted")
require("context->setPreferredVersion ({ 3, 3 });" in sokol
        and "context->setPreferredProfile (juce::OpenGLProfile::core);" in sokol,
        "desktop Sokol must request the GLCORE version/profile required by its shaders")
require("#define J37_SOKOL_SHADER_VERSION \"#version 300 es" in sokol
        and "#define J37_SOKOL_SHADER_VERSION \"#version 330 core" in sokol,
        "Sokol shaders must provide GLES 3 and desktop GL 3.3 sources")
require("#if defined (__ANDROID__)\n #define SOKOL_GLES3\n#elif defined (__APPLE__)" in sokol_backend
        and "#if TARGET_OS_IPHONE\n  #define SOKOL_GLES3\n #else\n  #define SOKOL_GLCORE" in sokol_backend
        and "#define SOKOL_METAL" not in sokol_backend,
        "Sokol must match JUCE's GLES context on Android/iOS rather than selecting Metal")
require("constexpr auto sceneDepthFormat = SG_PIXELFORMAT_DEPTH;" in sokol
        and "constexpr auto sceneDepthFormat = SG_PIXELFORMAT_DEPTH_STENCIL;" in sokol
        and "desc.depth.pixel_format = sceneDepthFormat;" in sokol
        and "swapchain.depth_format = sceneDepthFormat;" in sokol,
        "Sokol pipeline and swapchain depth formats must match the platform framebuffer")

shader_sources = {}
for name in ("vertex", "fragment"):
    match = re.search(
        rf"constexpr const char {name}ShaderSource\[\] = J37_SOKOL_SHADER_VERSION "
        rf'R"GLSL\((.*?)\)GLSL";',
        sokol,
        re.DOTALL,
    )
    require(match is not None, f"Sokol {name} shader source must remain extractable for checks")
    if match is not None:
        shader_sources[name] = match.group(1)

uniform_metadata = sokol[sokol.index("auto& vsBlock"):sokol.index("shader = sg_make_shader")]
vertex_metadata = uniform_metadata[uniform_metadata.index("auto& vsBlock"):uniform_metadata.index("auto& fsBlock")]
fragment_metadata = uniform_metadata[uniform_metadata.index("auto& fsBlock"):]
for stage, block in (("vertex", vertex_metadata), ("fragment", fragment_metadata)):
    shader_uniforms = set(
        re.findall(r"\buniform\s+[A-Za-z_]\w*\s+([A-Za-z_]\w*)", shader_sources.get(stage, ""))
    )
    bound_uniforms = set(
        re.findall(
            r'glsl_uniforms\[\d+\]\s*=\s*\{\s*SG_UNIFORMTYPE_\w+,\s*\d+,\s*"(u\w+)"\s*\}',
            block,
        )
    )
    require(shader_uniforms == bound_uniforms,
            f"Sokol {stage} shader uniforms and sokol_gfx bindings must match exactly")

render_callback = sokol[
    sokol.index("void renderOpenGL() override"):sokol.index("void openGLContextClosing() override")
]
require("if (! hasPendingFrame)\n                return;" in render_callback,
        "Sokol must not clear its scene surface between submitted frames")
require("context->getRenderingScale()" in render_callback
        and "context->getFrameBufferID()" in render_callback,
        "Sokol must target JUCE's physical-size framebuffer, including iOS and high-DPI displays")
resize_body = sokol[
    sokol.index("void resize (Config config) override"):
    sokol.index("bool isPresentable() const noexcept override")
]
draw_scene = sokol[
    sokol.index("bool drawScene (const SceneFrame& frame) override"):
    sokol.index("void resize (Config config) override")
]
require("std::lock_guard<std::mutex> lock (frameMutex);" in resize_body
        and "logicalWidth = width;" in render_callback
        and "logicalHeight = height;" in render_callback,
        "Sokol resize dimensions must be synchronized with the frame handoff")
require("isValid (frame)" not in draw_scene,
        "Sokol must not revalidate the full shared mesh topology after common frame submission")
require("sg_destroy_pipeline (mainPipeline);" in sokol
        and "sg_destroy_pipeline (glowPipeline);" in sokol,
        "partial pipeline creation failure must release successful pipeline allocations")
theme = (root / "Source/GUI/EditorTheme.cpp").read_text()
require("tapeScene.setPalette(palette.panel," in theme,
        "the Sokol scene background must blend with the deck panel rather than look like a black inset")

# bgfx renders into a native window bgfx is handed. A plugin host owns the
# editor's window, so the adapter must present into a CHILD surface, never the
# host's peer - the same rule the Metal adapter states for its NSView.
bgfx = (root / "Source/GUI/NativeRenderer_Bgfx.cpp").read_text()
require("CreateWindowExW" in bgfx and "WS_CHILD" in bgfx,
        "bgfx must present into a child surface, not the host-owned window")
require("bgfx::init (init)" in bgfx,
        "bgfx must be initialised through its own Init structure")

# Backend-specific adapters should preserve explicit resource/lifecycle safety.
d3d12 = (root / "Source/GUI/NativeRenderer_D3D12.cpp").read_text()
require("if (queue != nullptr && fence != nullptr && fenceEvent != nullptr" in d3d12,
        "D3D12 shutdown must support an empty pre-initialisation state")
require("ResetEvent (fenceEvent);\n        const auto eventResult = fence->SetEventOnCompletion" in d3d12,
        "D3D12 fence event must be reset before registering each wait")
require("if (! createRenderTargets())" in d3d12,
        "D3D12 resize must handle back-buffer recreation failure")
require("const auto submittedSlot = activeSlot;" in d3d12,
        "D3D12 endFrame must advance from the submitted slot")

d3d11 = (root / "Source/GUI/NativeRenderer_D3D11.cpp").read_text()
require("if (FAILED (createResult) && config.validation)" in d3d11,
        "D3D11 validation mode must fall back when the debug runtime is unavailable")
require("&& renderTarget != nullptr;" in d3d11,
        "D3D11 presentability must require a valid render target")

metal = (root / "Source/GUI/NativeRenderer_Metal.mm").read_text()
# Metal now draws the scene frame, into a CHILD NSView of the editor rather than
# the host-owned peer: a CAMetalLayer attached to the editor's own view would
# paint outside the plugin's bounds and outlive it.
require("sceneView->getPeer()" in metal and "addSublayer:metalLayer" in metal,
        "Metal must present into the editor's child NSView, never a host-owned view")
require("component.getPeer()" not in metal,
        "Metal must not attach its layer to the host's peer")
require("bool drawScene(const SceneFrame& frame) override" in metal,
        "Metal must implement the scene draw its backend claims")
require("ensureMeshes(frame)" in metal,
        "Metal must upload the frame's own immutable topology, not rebuild the scene")

# Vulkan's scene-draw contract is staged: the adapter compiles in, but it still
# clears the surface and refuses the scene frame. The gate-checking line is the
# authoritative build-time rule, and the adapter's own doc/markers are the one
# place the pipeline/UBO/mesh/drawScene work is tracked until it lands. No native
# Vulkan selection may be advertised as drawing the scene until that work is in.
vulkan = (root / "Source/GUI/NativeRenderer_Vulkan.cpp").read_text()
vulkan_initialise = vulkan[vulkan.index("bool initialise"):vulkan.index("void resize")]
vulkan_frame_resources = vulkan[vulkan.index("bool createFrameResources"):vulkan.index("bool createSceneResources")]
require(vulkan_initialise.index("createSwapchain()") < vulkan_initialise.index("createSceneResources()"),
        "Vulkan scene resources must be created after the swapchain sets its format")
require("createDefaultRenderPass" not in vulkan_frame_resources,
        "Vulkan frame resources must not create a render pass before swapchain setup")
require("drawScene" in vulkan and "activePassIsDrawable" not in vulkan,
        "the Vulkan adapter must keep the staged scene-draw contract, not a Metal-style active-pass flag")
require("docs/backend-implementation-plan-vulkan-shaders.md" in vulkan,
        "the Vulkan adapter must point at the Vulkan next-deliverable spec it is staged behind")
require("vulkan::VulkanSceneShaders_vert_spirv" in vulkan
        or "VulkanSceneShaders_vert_spirv" in vulkan
        or "uint32_t VulkanSceneShaders_vert_spirv" in vulkan
        or "uint32_t VulkanSceneShaders_frag_spirv" in vulkan
        or "extern const unsigned" in vulkan
        or "TapeSceneVulkanShaders.h" in vulkan
        or "hlsl" in vulkan,
        "the Vulkan adapter must reference the generated SPIR-V umbrella/symbols or its contract header, including the embedded hlsl string")
require("render pass" in vulkan.lower() or "renderPass" in vulkan or "depth buffer" in vulkan.lower() or "depthImage" in vulkan,
        "the Vulkan adapter must call out the render-pass + depth-buffer + pipeline + UBO + ensureMeshes + drawScene boundary it is staged behind")

if errors:
    print("render backend checks failed:", file=sys.stderr)
    for error in errors:
        print(f" - {error}", file=sys.stderr)
    raise SystemExit(1)

print("render backend checks passed")
