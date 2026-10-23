#!/usr/bin/env python3
"Static contract checks for native-renderer capability selection."""
from pathlib import Path
import re
import sys
root = Path(__file__).resolve().parents[2]
backend = (root / "Source/GUI/RenderBackend.h").read_text()
setup = (root / "Source/GUI/EditorSetup.cpp").read_text()
native_api = (root / "Source/GUI/NativeRenderer.h").read_text()
frame_submission = (root / "Source/GUI/NativeFrameSubmission.h").read_text()
scene_frame = (root / "Source/GUI/SceneFrame.h").read_text()
tape_frame = (root / "Source/GUI/TapeSceneFrame.cpp").read_text()
tape_scene = (root / "Source/GUI/TapeScene.cpp").read_text()

errors = []

def require(condition: bool, message: str) -> None:
    if not condition:
        errors.append(message)

# Native adapters currently only clear/present; they do not draw TapeScene's
# meshes, so they must not be reported as effective scene backends.
require("constexpr bool hasNativeImplementation(Backend) noexcept {\n    return false;" in backend,
        "native backends must remain disabled until scene rendering is implemented")
require("constexpr bool usesNativeCommandRenderer() noexcept\n{\n    return false;" in backend,
        "native command renderer must not be advertised as active")
require("if (!j37::render::usesOpenGL())" in setup,
        "editor must retain the CPU paint path when OpenGL is not selected")
require("SceneFrame" in native_api,
        "native renderer API must accept the backend-neutral SceneFrame contract")
require("virtual bool drawScene" in native_api and "return false;" in native_api,
        "native adapters must refuse scene draws until each API implements them")
require("bool submitSceneFrame" in frame_submission and "renderer.endFrame();" in frame_submission,
        "native submission must preserve fallback and retire every acquired frame")
require("!isValid(frame)" in frame_submission and "renderer.beginFrame()" in frame_submission,
        "native submission must validate a complete SceneFrame before acquiring resources")
require("renderer.endFrame();" in frame_submission,
        "native submission must retire the frame after the draw hook completes")
require("std::shared_ptr<const std::vector<SceneMesh>> meshes;" in scene_frame,
        "scene topology should be shared between frames, not recopied each tick")
require("static const auto sharedMeshes" in tape_frame,
        "scene geometry must be built once and reused by frame snapshots")
require("built->push_back(buildReel())" in tape_frame
        and "frame.meshes = sharedMeshes;" in tape_frame,
        "shared mesh cache must contain all scene topology")
layout = (root / "Source/GUI/SceneLayout.h").read_text()
require("inline constexpr std::array<float, 5> bankPlacementsX" in layout,
        "scene layout constants must have a single shared definition")
require("j37::render::scene::bankPlacementsX" in tape_scene,
        "OpenGL hit-testing and drawing must use the shared scene control positions")
require("void TapeScene::drawSceneFrameOpenGL" in tape_scene
        and "drawMesh(*gpuMeshes[draw.meshIndex])" in tape_scene,
        "the active OpenGL backend must consume the neutral draw list")
require("switch (draw.material)" in tape_scene and "SceneMaterial::glow" in tape_scene,
        "OpenGL must dispatch the neutral material variants including glow")
require("appendDraw(frame" in tape_frame and "SceneMaterial::glow" in tape_frame,
        "frame builder must describe the full scene including additive glows")
require("drawSceneFrameOpenGL(sceneFrame)" in tape_scene,
        "OpenGL must render the canonical SceneFrame rather than duplicate its draw list")
require("switch (draw.material)" in tape_scene and "drawMesh(*gpuMeshes[draw.meshIndex])" in tape_scene,
        "OpenGL SceneFrame dispatcher must map materials and mesh indices to GPU meshes")
require("drawControlBank(viewProjection, scene, body, highlight)" not in tape_scene,
        "legacy duplicated control-bank draw path must not remain active beside SceneFrame")


# The two GPU abstraction layers are build OPTIONS, not additions: each owns the
# device and the surface, so a build selects one and the other is not compiled.
for name in ("SOKOL", "BGFX"):
    require(f"J37_RENDER_BACKEND_{name}" in backend,
            f"{name} must map to its own Backend value")
    require(f'J37_RENDER_BACKEND_{name}=1' in (root / "CMakeLists.txt").read_text(),
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
require("editor-scoped native surface unavailable" in metal,
        "Metal must report why initialization declines the host-owned peer")
require("class MetalRenderer final" in metal and "bool drawScene (const SceneFrame&) override { return false; }" in metal,
        "Metal must compile as an explicitly pending adapter, not unreachable pseudo-implementation")
require("MTLCreateSystemDefaultDevice" not in metal and "CAMetalLayer" not in metal,
        "staged Metal adapter must not allocate or attach resources before a safe surface exists")

if errors:
    print("render backend checks failed:", file=sys.stderr)
    for error in errors:
        print(f" - {error}", file=sys.stderr)
    raise SystemExit(1)

print("render backend checks passed")
