# Native render backend: Vulkan shader generation plumbing

> **Phase 2.1 first deliverable.** The remaining 600-800 lines of
> `NativeRenderer_Vulkan.cpp` draw a cleared surface today. This plan makes the
> shader pipeline the first thing that runs against it, before any draw, so Phase
> 2.1 can land with a verified shader path and the Vulkan adapter can be gated as
> scene-draw-capable on its way back from staged.
>
> Written for a machine with no CMake/CMakeLists.txt hands-on right now, so the
> configure blocks are proposals pinned to the CMake already in the tree plus a
> local `glslangValidator` check, not to a finished build.

## Where this lands

Files touched:

- `Source/GUI/TapeSceneVulkanShaders.h` - new, the shipped SPIR-V bundle for the
  Vulkan scene path.
- `tests/shaders/check_shaders.py` - grows the Vulkan GLSL source check.
- `tests/shaders/VulkanSceneShaders` - new directory, GLSL source the harness can
  read.
- CMake pieces: the Phase 2.1 port later extends this plumbing into the Vulkan
  backend's `find_program`/`try_compile` configure block. This document is the
  first deliverable only.

What this deliverable is **not**: drawing, pipelines, uniform buffers, meshes.
Those stay in the Vulkan adapter until the rest of Phase 2.1.

## Why start with shaders

Vulkan is the only pending backend where the shader story is
**toolchain-constrained** rather than **adapter-constrained**. The device and
present path already exist; what it cannot do today is render because it has no
shaders and no way to get SPIR-V into the adapter at compile time.

The Metal port already solved this exact problem once: compile once, embed once,
consume once. Vulkan should follow the same shape, because the alternative -
runtime shaderc, runtime compile, runtime fallback - is a dependency the plugin
does not carry and a runtime cost the scene path does not need.

There is also an evidence reason. `Chip8PixelShader.spv` was already committed
as a SPIR-V binary under `Source/GUI/`. That is useful as a pattern and as a
data file the generate/build path can point at, but it is not the shaders the
scene path consumes. This deliverable makes the relationship explicit: the chip
shader is regional rasterisation data, the scene shaders are the rendered scene
path, and each comes from its own GLSL source bundle.

## What the scene path actually needs

From `SceneFrame.h` plus the Phase 1 packing contract in `SceneUniforms.h`, the
Vulkan scene shaders need to reproduce exactly what the Metal path does, with
GLSL 450 as the source and SPIR-V as the shipped artefact.

Geometry input:

- `SceneVertex` is `float4 position`, `float3 normal`, interleaved. Vulkan will
  use the same `VkVertexInputAttributeDescription` layout the Metal adapter uses
  for its `MTLVertex` - same field order, same packed stride, same offset rules.

Shaders:

- One vertex shader, one fragment shader, one pipeline for all materials.
- The material switch is shader-side, exactly as in Metal: the vertex shader
  writes the packed material selector into the `w` component of `parameters`, and
  the fragment shader switches on it. No separate pipeline per material.
- Uniforms mirror `MetalUniforms` field-for-field so Phase 1's table is copied
  rather than re-derived:
  - per-frame: `viewProjection`, `lightDirection`, `backgroundColour`,
    `highlightColour`, `timeSeconds`, `drive`, `flutter`, `glow`,
    `textureWeights`
  - per-draw: `model`, `normalMatrix`, `baseColour`, `highlightColour`,
    `parameters` (with the material selector in `w`), plus the draw-level
    material flag used by the shader-side switch.
- Packing contract by material (mirrors `SceneUniforms.h`):
  - `tapePack`: `parameters[0]` = pack inner radius, `parameters[1]` = pack outer
    radius, `parameters[2]` unused.
  - `ribbon`: `parameters[0]` = flutter amplitude, `parameters[1..2]` unused.
    This is the one case where `uFlutter` becomes `parameters.x` in the vertex
    stage - the same shape the Metal path landed on.
  - `controlPanel`: `parameters[0..2]` = panel half-extents `vec3`.
  - `knob` and `key`: `parameters[0]` = arc angle, `parameters[1]` = key down,
    `parameters[2]` unused.
  - `glow`: `parameters[0]` = the draw's own glow, not the frame glow.
  - `solid`: parameters unused by the shipped solid shader today.

Nothing else. No extra UBOs, no per-material uniform arrays, no feature flags
that split the pipeline. The Metal port already showed this fits one pipeline,
and the Vulkan port should not re-split it.

## Source stays source, SPIR-V is derived

The rule from the plan and from the Metal port is simple and it is the same here:

- GLSL source is a string literal the compiler, the harness and the reader can all
  see.
- SPIR-V is a generated C array header, embedded in the binary exactly as
  `TapeSceneMetalShaders.h` is.
- The shader **source** is the contract; the SPIR-V is data derived from it.

That means this deliverable owns three things in lockstep:

1. GLSL source in `tests/shaders/VulkanSceneShaders`.
2. The embed header `Source/GUI/TapeSceneVulkanShaders.h`.
3. A harness that asserts source, embed header and SPIR-V binary all exist and
   that the anchor constants in them agree.

The Vulkan adapter itself does not compile shaders at build time here. It consumes
the embed header the way Metal consumes `TapeSceneMetalShaders.h`. That is a Phase
2.1 decision, but the plumbing this deliverable builds is exactly what lets it.

## How the SPIR-V is produced (proposal)

Configure-time:

- `find_program(glslangValidator REQUIRED)` guarded by the usual Vulkan SDK
  pattern `find_package(Vulkan)` already runs for this backend.
- If not found, `J37_NATIVE_VULKAN` is not set and the build warns, the same way
  the existing platform guards handle a missing backend toolchain. Better than a
  runtime dependency the plugin does not otherwise carry.

Generate step:

- GLSL source -> `glslangValidator -V` -> `.spv`.
- The `.spv` is embedded as a generated C array header through the same shape as
  the Metal embed: a single C array, an anchor constant for the byte count, and
  a comment pinning the source file and the compilation command.

Fallback shape:

- If the toolchain is absent, the backend is excluded from the build. Phase 2.1
  still wants the embed header committed so the generated artefact is reviewable
  and reproducible when the toolchain is present; that means the generate step is
  part of the Phase 2.1 port, and this deliverable can be the shader source plus
  the embed header placeholder the generate step fills.

## What this deliverable produces right now

Because the environment has no CMake to run against right now, the first tangible
deliverable is the shader source plus the embed header shape, plus the harness
that ties them together. That is enough for Phase 2.1 to continue with a real
SPIR-V production step once a Vulkan SDK is available.

Concrete pieces:

- `tests/shaders/VulkanSceneShaders/vert.glsl`
- `tests/shaders/VulkanSceneShaders/frag.glsl`
- `Source/GUI/TapeSceneVulkanShaders.h` - embed header, same shape as the Metal
  one, with the same anchor-constant discipline.
- `tests/shaders/VulkanSceneShaders/README.md` - the Vulkan shader source anchor
  document.
- `docs/backend-implementation-plan.md` status table updated to reflect this
  shader-plumbing step.

That is the deliverable. Pipelines and draw follow in the rest of Phase 2.1.

## Why the Metal embed shape is the right template

`TapeSceneMetalShaders.h` is the one shipped example in the tree of "here is a
SPIR-V/MSL bundle the adapter consumes directly." It is not abstract; it is the
pattern the Vulkan port should copy so the Vulkan adapter's shader consumption is
identical in shape to Metal's - same embed, same anchor constants, same comment
pinning the source and the compilation step.

That also means the Vulkan embed header can be reviewed against the Metal one for
parity: same discipline, same consume site pattern, same "this is derived data"
line.

## What the harness checks

`tests/shaders/check_shaders.py` grows a Vulkan section that asserts:

- `tests/shaders/VulkanSceneShaders/vert.glsl` and `frag.glsl` exist and are
  plain GLSL, not SPIR-V and not a binary blob.
- `Source/GUI/TapeSceneVulkanShaders.h` exists and carries the anchor constants
  the adapter consumes.
- The GLSL source still parses as GLSL 450 (no accidental SPIR-V drift into the
  source tree).
- The embed header's anchor constants agree with the committed SPIR-V byte count
  once the generate step has run; in this environment that assertion is documented
  as "pending the toolchain", not skipped.

This is the same discipline the chip-shader path should have had from the start:
source, binary and embed header all present and agreeing, not one of them floating
as a lone binary file.

## Where this sits in the plan order

- Phase 0 is done.
- Phase 1 wrote the packing contract to `SceneUniforms.h` and added
  `tests/scene_frame/pack_contract.cpp`.
- This deliverable is the first Vulkan-specific piece: the shader source and the
  embed plumbing, before the adapter draws.
- Phase 2.1 then finishes Vulkan by adding the pipeline, UBOs, meshes and
  `drawScene`, and flips the Vulkan gate.

The rest of the plan order stays the same: Vulkan first among the four ports
because it is three platforms, its adapter is furthest along, and lavapipe makes
it CI-testable.

## Limits

- Written on a machine with no CMake run here right now, so the configure blocks
  are pinned to the CMake already in the tree and a local `glslangValidator`
  check, not to a finished build.
- The SPIR-V artefacts this deliverable wants are the scene shaders, not the chip
  shader already committed - those are different data for a different purpose.
- The embed header produced here is the same shape the Metal port uses; the Vulkan
  adapter's actual consume site is part of the rest of Phase 2.1, not this first
  deliverable.
