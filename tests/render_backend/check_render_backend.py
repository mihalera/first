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

# Native adapters currently only clear/present; they do not draw TapeScene's
# meshes, so they must not be reported as effective scene backends.
require("constexpr bool hasNativeImplementation(Backend) noexcept {\n    return false;" in backend,
        "native backends must remain disabled until scene rendering is implemented")
require("constexpr bool usesNativeCommandRenderer() noexcept\n{\n    return false;" in backend,
        "native command renderer must not be advertised as active")
require("if (!j37::render::usesOpenGL())" in setup,
        "editor must retain the CPU paint path when OpenGL is not selected")

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
require("embedded host surface integration is not enabled" in metal,
        "Metal must not mutate a host-owned view until child-surface integration is safe")
require("__weak NSView* hostView" in metal,
        "Metal host view reference must not outlive the host view")

if errors:
    print("render backend checks failed:", file=sys.stderr)
    for error in errors:
        print(f" - {error}", file=sys.stderr)
    raise SystemExit(1)

print("render backend checks passed")
