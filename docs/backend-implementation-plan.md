# Native render backend implementation plan

> **How every `J37_RENDER_BACKEND` selection becomes a real scene renderer.**
>
> The plugin already has one boundary (`Source/GUI/RenderBackend.h`), one
> backend-neutral frame (`Source/GUI/SceneFrame.h`) and six adapters under
> `Source/GUI/NativeRenderer_*`. Two of them draw the full tape scene today;
> four create a device, present a cleared surface and stop. This plan closes
> that gap in the order the evidence supports, with the verification each port
> needs rather than a check at the end.
>
> Like the other plans in this directory it states **what**, **why**, **cost**,
> **payoff**, **evidence** and **how to verify** per phase, and it is honest
> about what cannot be measured in the environment it was written in.

## How to read this

- **Phase 0** is a correctness fix, not a port: the capability gate lies about
  one backend today. It comes first because it is hours of work and it makes an
  already-complete renderer selectable.
- **Phases 2.1-2.4** are the four ports, ordered by readiness, not by API
  prestige. Each is independently shippable and independently verified.
- **Phase 3** is not a phase you visit once: every port carries its own
  verification with it, and this section defines what "carries it" means.
- Status words: **done** (shipping), **staged** (device + present only),
  **blocked** (needs a decision or a toolchain first).

## Where the backends stand today

Measured on `main` at `03284c8f`, 2026-10-31. Line counts are
`wc -l Source/GUI/NativeRenderer_*`.

| Backend | Adapter | Lines | Device + child surface | `drawScene` | Gate says | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| OpenGL | JUCE context on `TapeScene` | - | done | **done** (reference path, GLSL in `TapeSceneShaders.h`) | selectable | `TapeScene.cpp` `renderOpenGL()`, uniform binding at lines 1041-1110 |
| Sokol | `NativeRenderer_Sokol.cpp` | 921 | done (JUCE `OpenGLContext` + `SokolGfxImpl.cpp` JUCE-free TU) | **done** (GLSL 410 port, `ensureShader/ensurePipelines/ensureMeshes`) | selectable | `drawScene` at line 348; `hasNativeImplementation` reads `J37_NATIVE_SOKOL` |
| Metal | `NativeRenderer_Metal.mm` | 462 | done (child `NSView` + `CAMetalLayer`, MSL compiled at runtime from `TapeSceneMetalShaders.h`) | **done** (`drawScene` line 229, `ensureMeshes` line 415) | **WRONG: listed as PENDING** | `hasNativeImplementation` returns false for Metal |
| Vulkan | `NativeRenderer_Vulkan.cpp` | 732 | done (Win32 child `HWND`, swapchain, frames-in-flight, transfer barriers) | staged | pending | no `drawScene`, no pipeline, no shaders in the file |
| D3D11 | `NativeRenderer_D3D11.cpp` | 282 | done (`WS_CHILD` window, swapchain, RTV) | staged | pending | no `drawScene`, no shader references |
| D3D12 | `NativeRenderer_D3D12.cpp` | 499 | done (queue, fence + event, RTV heap, per-slot allocators) | staged | pending | no `drawScene`, no root signature/PSO |
| BGFX | `NativeRenderer_Bgfx.cpp` | 218 | done (child window, `bgfx::init`) | staged, **blocked** | pending | no `drawScene`; bgfx needs precompiled shader binaries and the build turns shaderc off (`cmake/J37Dependencies.cmake`) |

### The defect Phase 0 fixes

`hasNativeImplementation` (`RenderBackend.h` line 67) reads only
`J37_NATIVE_SOKOL`. Metal's adapter satisfies every claim the boundary makes -
it draws the complete `SceneFrame` into a child surface of the editor, never
the host peer, and `tests/render_backend/check_render_backend.py` already
asserts exactly that (`drawScene` override, `ensureMeshes`) - yet a
`J37_RENDER_BACKEND=METAL` build reports METAL as PENDING in SETTINGS, refuses
to select it, and resolves `runningBackend()` to CPU. The editor would draw the
scene through Metal if the gate let it: `setUpNativeScene()` only asks
`hasNativeImplementation(running)` (`EditorSetup.cpp` line 2121). This is the
same defect the never-merged branch `freebuff/backends-e6606487` tried to fix;
it is re-done here on the current tree.

`docs/architecture.md` ("Of those, only OpenGL draws the tape scene") and the
roadmap rows for Sokol/Metal are stale in the same direction: Sokol draws the
scene now. Phase 0 updates them.

## The contracts every port must honour

These are not restated per phase; they are the acceptance floor.

1. **Child surface, never the host peer.** A plugin host owns the top-level
   window. Every adapter presents into a surface scoped to the editor's scene
   bounds (`NativeRenderer.h` PRESENTATION block). The Metal and D3D comments
   state it; the check for bgfx (`WS_CHILD`) enforces it.
2. **`SceneFrame` is the only data that crosses.** No JUCE object, no GL/D3D
   handle, no editor class pointer. Adapters receive immutable frame data and
   own their own resource lifecycle (`SceneFrame.h`).
3. **One GPU abstraction layer per build.** `sokol` and `bgfx` each own device,
   surface and frame; a binary carrying both would be two owners of one window
   (`CMakeLists.txt` line 771, `RenderBackend.h` enum comment).
4. **A refused draw leaves the CPU path alive.** `submitSceneFrame()`
   (`NativeFrameSubmission.h`) always calls `endFrame()` after a successful
   `beginFrame()`, and the editor keeps the software transport underneath: a
   backend that fails at any point shows the flat deck, never a blank rect.
5. **Message-thread calls, bounded waits.** The current contract says so
   explicitly (`NativeRenderer.h` THREADING block); a render-thread version
   must change the contract, not the callback ownership silently.
6. **The per-material uniform packing.** `draw.parameters[0..2]` mean different
   things per `SceneMaterial`: pack radii for `tapePack`, flutter amplitude for
   `ribbon`, panel half-extents for `controlPanel`, arc angle and key-down for
   `knob`/`key`, glow for `glow` (GL binding site: `TapeScene.cpp` lines
   1076-1096; shader-side switching: `TapeSceneMetalShaders.h`). The GL path
   switches in C++, the Metal port switches in the shader; both read the same
   frame. Every new port must reproduce this mapping exactly - Phase 1 writes
   it down once so ports 2.1-2.4 cannot each re-derive it.

## Phase 0 - Make the gate tell the truth (hours, do first)

**What.**

1. `hasNativeImplementation` checks each backend's own macro - `SOKOL` and
   `METAL` are `true` when compiled in; `VULKAN`, `D3D11`, `D3D12`, `BGFX`
   return `false` until their phase lands. The function's body names every
   `J37_NATIVE_*` macro so a new adapter cannot be forgotten silently.
2. `isNativeBackendPending` derives from `hasNativeImplementation` alone
   ("a compiled-in native selection without scene draw"), instead of
   hard-coding today's five-name list that already excludes Sokol by hand.
3. `tests/render_backend/check_render_backend.py` gains one line per backend:
   `J37_NATIVE_{VULKAN,METAL,D3D11,D3D12,SOKOL,BGFX}` must appear in
   `RenderBackend.h`. CMake already throws each macro
   (`CMakeLists.txt` lines 238-266), so the check couples the option list, the
   macros and the gate - the drift it fails on is exactly the drift that
   shipped.
4. Docs sync: `docs/architecture.md` rendering paragraph, the Sokol/Metal rows
   in `docs/requested-roadmap-status.md`, the adapter descriptions in
   `docs/library-integration-plan.md`, and `README.md` § Rendering backends.

**Why.** A build configured for Metal currently cannot use a renderer that is
finished, tested and waiting; SETTINGS tells the user it is pending. The gate
is the one file every backend already passes through, so it is also the cheapest
place to make "an adapter exists" and "the adapter draws" into two states the
build can distinguish.

**Cost.** One sitting. **Payoff.** METAL builds draw the scene with Metal on
the next run; every later port flips one line to become selectable.
**Verify.** The updated `check_render_backend.py` fails if a macro is added to
CMake but not to the gate; a `J37_RENDER_BACKEND=METAL` build's SETTINGS header
reads `BACKEND METAL` instead of `BACKEND CPU (METAL PENDING)`.

## Phase 1 - Write the uniform packing contract down (small)

**What.** A single table - in `SceneFrame.h`'s doc comment or a short
`Source/GUI/SceneUniforms.h` - giving, per `SceneMaterial`, what
`parameters[0..2]` and the frame-level `drive/flutter/glow/textureWeights`
feed in the shader, with the GL binding site and the MSL name as references.
`tests/scene_frame` (which compiles the boundary with no JUCE and no GPU) grows
a packing section: build a frame the way `TapeScene::buildSceneFrame` does and
assert the per-material parameter slots carry the values the contract claims.

**Why.** Three ports are about to re-implement one mapping that today lives in
two implementations held together only by reading both. The Metal port already
shows how a port drifts unnoticed (`uFlutter` became `parameters.x` in the
vertex stage; correct for `ribbon`, invisible until a comparison exists).

**Cost.** Half a day. **Payoff.** Each of the four ports becomes "follow the
table", not "reverse-engineer two files". **Verify.** `tests/scene_frame`
fails when a frame builder and the contract disagree.

## Phase 2.1 - Vulkan (the biggest API with the most scaffolding done)

**State.** 732 lines already own: instance with the Win32 surface extension,
device, swapchain, per-frame command buffers and fences, transfer barriers for
staging uploads, `SetWindowPos` resize, a recovery command path. Missing: render
pass, depth buffer, graphics pipeline, shaders, vertex/index buffers,
per-draw uniforms, `drawScene`.

**What.**

1. **Shaders: GLSL 450 -> SPIR-V, compiled at build time** with
   `glslangValidator` (`find_program` in CMake; the Vulkan SDK ships it, and
   `find_package(Vulkan)` already runs for this backend). The `.spv` is
   embedded as a generated C array header, exactly the shape
   `TapeSceneMetalShaders.h` is in. The GLSL **source** stays a string the
   compiler and `tests/shaders/check_shaders.py` can read; the SPIR-V is data
   derived from it. Configure-time fallback: if no compiler is found, the
   backend is not compiled in (`J37_NATIVE_VULKAN` off) and the build warns -
   the same pattern the CMake platform guards already use, and better than a
   runtime shaderc dependency the plugin does not otherwise carry.
2. **Pipeline:** one render pass (color + depth), one graphics pipeline for all
   materials - the material switch is shader-side, as in the Metal port. Vertex
   input mirrors `SceneVertex` (float4 position, float3 normal, interleaved);
   blend state matches the Metal pipeline (src alpha / one-minus-src-alpha);
   depth test less-equal, write on.
3. **Uniforms:** one UBO per frame-in-flight, sub-allocated per draw with
   dynamic offsets; the per-draw block mirrors `MetalUniforms` field for field
   (viewProjection, model, normalMatrix, base/highlight colour, parameters,
   globals, lightDirection) so the packing contract from Phase 1 is copied, not
   re-invented.
4. **Meshes:** `ensureMeshes` by `shared_ptr` identity (the pattern both
   existing ports use), uploaded through a staging buffer with the transfer
   barriers the file already contains.
5. **Gate:** `hasNativeImplementation` flips `J37_NATIVE_VULKAN` to true.

**Why first.** Vulkan is the one pending backend that runs on Windows, Linux
and Android; the adapter is 732 lines past device creation; and Mesa's lavapipe
makes it testable on the Ubuntu CI runner with no GPU (Phase 3).

**Cost.** The largest single port, ~600-800 lines plus the shader pair and the
CMake toolchain block. **Payoff.** A native scene renderer on three platforms
and the recipe the D3D ports will follow.
**Verify.** Phase 3's matrix entry; `check_render_backend.py` gains the Vulkan
contract lines (child surface, `drawScene`, `ensureMeshes`) mirroring Metal's.

**Seam to keep.** `createWin32Surface` is already isolated; Linux (xcb/Wayland
via JUCE's peer handle) and Android (ANativeWindow) are follow-ups behind the
same seam, not part of this phase.

## Phase 2.2 - D3D11 (the simple DirectX)

**State.** 282 lines own device, `WS_CHILD` window, swapchain, RTV, resize
ordering (detach target -> drop view -> resize -> reattach). Missing: shaders,
input layout, constant buffer, depth-stencil, `drawScene`.

**What.**

1. **HLSL compiled at runtime with `D3DCompile`** from a string literal in the
   adapter - the same choice the Metal port made (runtime `newLibraryWithSource`)
   and for the same reason: no offline toolchain in the build, one source, and
   the blob is cached after the first compile. `d3dcompiler` ships with every
   Windows 10+ system.
2. Input layout for `SceneVertex`; one 16-byte-aligned constant buffer struct
   mirroring `MetalUniforms`; depth buffer + depth-stencil state; a
   `drawIndexedInstanced` loop over `frame.draws`.
3. Same material switch shader-side; HLSL is the second spelling of the Phase 1
   table, not a new design.
4. Gate flip for `J37_NATIVE_D3D11`.

**Why second.** It is the smallest of the three real API ports, it shares its
shader source with D3D12, and WARP (the software rasterizer built into Windows)
makes the same CI verification as lavapipe possible on the Windows runner.

**Cost.** ~300-400 lines. **Payoff.** The DirectX family gets its reference
port, and the HLSL that 2.3 reuses exists.
**Verify.** Windows compile-matrix job; WARP render check in Phase 3.

## Phase 2.3 - D3D12 (ceremony, reusing 2.2's shaders)

**State.** 499 lines own device, direct queue, fence + manual-reset event
(reset before each `SetEventOnCompletion` - pinned by the existing check), RTV
heap, per-slot command allocators and fence values, back buffers. Missing: root
signature, PSO, shader blobs, descriptor/upload heaps, depth resource,
`drawScene`.

**What.** Root signature (one CBV slot with root constants or a small descriptor
ring, vertex buffer view, index buffer view); PSO from the same HLSL as 2.2
compiled at `vs_5_0`/`ps_5_0`; a depth resource and its DSV; an upload heap for
meshes and a per-frame constant ring keyed on the existing slot/fence scheme;
barrier management beside the ones the file already has; the same draw loop.
Gate flip for `J37_NATIVE_D3D12`.

**Why third.** Everything hard about the shaders is done in 2.2; what remains
is D3D12's explicit memory and synchronisation, which the adapter's own
fence-per-slot structure was built to carry.

**Cost.** ~500-700 lines - the most ceremony per pixel of the four.
**Payoff.** The backend Windows actually prefers, on the same footing as the
others. **Verify.** Same Windows matrix entry and WARP check as 2.2.

## Phase 2.4 - BGFX (blocked on a toolchain decision, port last)

**State.** 218 lines own the child window and `bgfx::init`; the adapter
presents a cleared frame. bgfx does not compile shaders at runtime: it consumes
precompiled binaries per target API, produced by its `shaderc` tool, which this
build deliberately does not build (`cmake/J37Dependencies.cmake`, and
`docs/library-integration-plan.md` records why).

**Decision required first - pick one:**

- **A. Build bgfx's shaderc at configure time** (`BGFX_BUILD_TOOLS` via the
  pinned `bgfx.cmake`), compile a shader pair per API into the build tree.
  Heaviest configure, zero committed binaries, always in sync with the source.
- **B. Precompile and commit the `.bin` files** under `ThirdParty/`, with a
  regeneration script and a CI check that a manifest of source hashes matches
  the committed binaries - the same "vendored pins are re-pinnable" discipline
  the repo already applies to libraries. Lightest configure; the binaries are
  reviewable only as hashes.
- **C. Keep BGFX staged and documented** as an opt-in layer that clears, with
  the roadmap row saying exactly why. Honest if the other three ports deliver
  everything the project needs; bgfx's marginal value over Sokol (which is the
  other abstraction layer and already draws the scene) is cross-API shader
  reuse the project does not otherwise need.

**Then, given A or B:** `bgfx::VertexLayout` matching `SceneVertex`, one
`bgfx::Program`, `ensureMeshes` via `createVertexBuffer`/`createIndexBuffer`
cached by frame identity, `setTransform`/`setUniform`/`setState`/`submit` per
draw, gate flip for `J37_NATIVE_BGFX`.

**Cost.** Decision: one sitting. A or B: ~250-350 adapter lines plus the
toolchain block. **Verify.** The existing bgfx check lines plus the standard
contract lines; the shader manifest check for option B.

## Phase 3 - Verification each port carries (not a phase you visit once)

1. **Compile matrix.** One CI job per `J37_RENDER_BACKEND` value on its platform
   (Linux: OPENGL/SOKOL/VULKAN/CPU; Windows: D3D11/D3D12/SOKOL/VULKAN; macOS:
   METAL/SOKOL), configure + compile, no GPU needed. This is exactly the gap
   [plugin-improvement-plan.md](plugin-improvement-plan.md) item 6 names: the
   adapters are compiled by nothing except the one platform job whose default
   never selects them, so today a break in `NativeRenderer_Vulkan.cpp` is
   invisible in every green run.
2. **Software-rasterizer render checks.** lavapipe (Ubuntu), WARP (Windows),
   and the macOS runner's own Metal: a headless harness initialises the adapter
   offscreen, submits one fixed `SceneFrame`, reads the surface back and
   compares it to the CPU-side expectation within a tolerance - or, cheaper and
   nearly as strong, against a committed checksum per backend with a documented
   tolerance. "It presents" becomes "it draws the same picture as the
   reference".
3. **Contract lines in `check_render_backend.py`** per adapter, mirroring the
   Metal ones now in the file: `drawScene` override present, `ensureMeshes`
   present, child-surface rule held, never the host peer.
4. **The refusal path.** Per adapter, one harness case forces `drawScene` to
   refuse (invalid frame) and asserts `submitSceneFrame` still returned after
   `endFrame` and the caller's CPU path remained active - the property
   `NativeFrameSubmission.h` promises and nothing currently tests.

## Order, and why

**Phase 0 -> 1 -> Vulkan -> D3D11 -> D3D12 -> BGFX (after its decision).**

- Phase 0 is first because it is hours and it unlocks a finished renderer.
- Vulkan before the D3Ds because it is three platforms rather than one, its
  adapter is furthest along, and lavapipe makes it CI-testable today.
- D3D11 before D3D12 because the HLSL and the packing are proven there first.
- BGFX last because it is the only one blocked on a decision the project has
  not made, and the only one whose value overlaps a backend that already works.

Each phase ends with a green matrix, a flipped gate line and updated docs; none
of them depends on a later one.

## Limits of this plan

- Written on a machine with no CMake and no GPU: every runtime claim above is
  a proposal pinned to the Phase 3 harnesses, not a measurement. The line
  counts and the gate state are measured; the estimates are not.
- The Win32-only Vulkan surface is a real scope line: Linux and Android Vulkan
  are behind `createWin32Surface`'s seam and are not in Phase 2.1.
- The uniform-packing table (Phase 1) is derived from two existing
  implementations; if a third reading of `TapeScene.cpp`'s draw loop disagrees
  with it, the table loses and the code wins - re-measure before trusting it.
- WGPU and DAWN stay out of scope, deliberately: they are wired inside
  `j37_setup_graphics_backend` but have no adapter and are not accepted by
  `J37_RENDER_BACKEND`, and inventing a seventh backend is not what "implement
  all backends" means while four of the six named ones do not draw.
