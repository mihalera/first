# Vulkan scene shaders - GLSL 450 source

This directory is the Vulkan scene shader source for
`docs/backend-implementation-plan-vulkan-shaders.md`.

Contents:
- `vert.glsl` - Vulkan scene vertex shader, GLSL 450
- `frag.glsl` - Vulkan scene fragment shader, GLSL 450

What these files are:
- The GLSL source the Vulkan pipeline consumes.
- The contract the SPIR-V is derived from.
- The readable shader text `tests/shaders/check_shaders.py` can inspect.

What these files are not:
- SPIR-V binaries. Those are derived by `VulkanShaderGen.cmake` / `glslangValidator`
  at build time and embedded through `Source/GUI/TapeSceneVulkanShaders.h`.
- A copy of the Metal shader. The Metal shader is the reference spelling; this is
  the Vulkan spelling of the same scene.

Anchor:
- Uniform block: `VulkanSceneUniforms` in
  `Source/GUI/TapeSceneVulkanShaders.h`, matching `MetalUniforms` field-for-field.
- Material selector: packed into the w component of `parameters`, exactly as in Metal.
- Per-material meanings: see `Source/GUI/SceneUniforms.h`.
- Reference shader: `Source/GUI/TapeSceneMetalShaders.h` (commit e382d6ea).
