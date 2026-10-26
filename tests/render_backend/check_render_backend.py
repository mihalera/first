#!/usr/bin/env python3
"Static contract checks for native-renderer capability selection."""
from pathlib import Path
import re
import sys
root = Path(__file__).resolve().parents[2]
backend = (root / "Source/GUI/RenderBackend.h").read_text()
setup = (root / "Source/GUI/EditorSetup.cpp").read_text()

errors = []

def require(condition: bool, message: str) -> None:
    if not condition:
        errors.append(message)

# Native adapters may be advertised ONLY behind their real scene draw. sokol
# crossed that line (Source/GUI/NativeRenderer_Sokol.cpp records the frame's
# meshes, uniforms and draws, and the editor attaches it only behind the
# capability probe), so the gate is now the build macro rather than a flat
# false - and every other native selection still falls through to CPU.
require("return backend == Backend::sokol && sokolDrawsScene;" in backend and
        "#if defined(J37_NATIVE_SOKOL)" in backend,
        "the native scene gate must be the sokol adapter's build macro, not an open door")
require("constexpr bool usesNativeCommandRenderer() noexcept\n{\n    return false;" in backend,
        "native command renderer must not be advertised as active")
require("if (!j37::render::usesOpenGL())" in setup,
        "editor must retain the CPU paint path when OpenGL is not selected")
require("hasNativeImplementation" in setup and "setUpNativeScene" in setup
        and "shutDownNativeScene" in setup,
        "the editor must attach a native scene adapter only behind the capability probe")

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

# sokol is the native backend that is ADVERTISED now, so its evidence has to be
# as concrete as Metal's: the frame's own meshes, the uniforms the deck's
# material branches read, and the draws themselves - recorded where JUCE's
# context is current, never by reaching for the host's peer.
sokol = (root / "Source/GUI/NativeRenderer_Sokol.cpp").read_text()
require("bool drawScene (const SceneFrame& frame) override" in sokol,
        "sokol must implement the scene draw its backend claims")
require("ensureMeshes" in sokol,
        "sokol must upload the frame's own immutable topology, not rebuild the scene")
require("sg_apply_uniforms" in sokol and "sg_draw" in sokol,
        "sokol must apply the deck's uniforms and record the frame's draws")
require("attachTo (component)" in sokol and "getPeer()" not in sokol,
        "sokol must attach to the component it is given, never reach for the host peer")
require("#version 410" in sokol,
        "sokol's GLCORE backend needs the ported shader's own #version line")

# The frame crossing into a native adapter comes from ONE builder, whether the
# OpenGL callback or the editor's timer asks for it - two builders would be two
# scenes that can drift apart.
tape_scene = (root / "Source/GUI/TapeScene.cpp").read_text()
require("advanceAndBuildFrame" in tape_scene
        and "drawSceneFrameOpenGL(sceneFrame)" in tape_scene,
        "the OpenGL path must draw the same builder's frame a native adapter receives")
editor_setup = setup
require("tapeScene.advanceAndBuildFrame" in editor_setup,
        "the native path must submit the one frame builder, not its own copy")

if errors:
    print("render backend checks failed:", file=sys.stderr)
    for error in errors:
        print(f" - {error}", file=sys.stderr)
    raise SystemExit(1)

print("render backend checks passed")
