#pragma once
#include <stddef.h>

namespace j37::render::vulkan
{

/**
 * Vulkan scene shaders - GLSL 450 source plus the SPIR-V embed plumbing the
 * Vulkan renderer consumes.
 *
 * The GLSL source lives in tests/shaders/VulkanSceneShaders/vert.glsl and
 * frag.glsl. That source is the contract; SPIR-V is derived data produced by
 * VulkanShaderGen.cmake / glslangValidator at build time and embedded here
 * through the same shape as TapeSceneMetalShaders.h.
 *
 * When no validator is available, VulkanShaderGen.cmake currently emits
 * placeholder headers (empty array, *_spirv_words = 0). That is the expected
 * abstract-correctness state for the plumbing itself; a real SPIR-V embed needs
 * the .spv input the CMake block already describes.
 *
 * The Vulkan adapter consumes these uniforms exactly as Metal does, field by
 * field (see SceneUniforms.h). The material selector is packed into the w
 * component of `parameters`, matching Metal.
 */

// Vulkan scene shaders live under tests/shaders/VulkanSceneShaders/ because the
// GLSL source is one of the things tests/shaders/check_shaders.py can inspect.
// The embed header is written by the build and included from there; this header
// is the umbrella the Vulkan adapter includes once the build plumbing exists.
//
// Until the build plumbing is wired, this is the contract document for the Vulkan
// scene shaders: the uniform layout, the material selector packing, and the
// sources the SPIR-V is derived from.
//
// GLSL source anchors (the contract the SPIR-V is derived from):
//  - vertex: tests/shaders/VulkanSceneShaders/vert.glsl
//  - fragment: tests/shaders/VulkanSceneShaders/frag.glsl
//
// Reference shader for the same scene: Source/GUI/TapeSceneMetalShaders.h
// (commit e382d6ea).
//
// Build plumbing that derives the SPIR-V: cmake/VulkanShaderGen.cmake,
// cmake/VulkanExtractGlsl.py, cmake/VulkanEmbedSpv.py, and a local
// glslangValidator.

// Uniform block the GLSL shaders and the Vulkan adapter agree on.
// This layout is the GLSL 450 spelling of the Metal `MetalUniforms` block; the
// Vulkan adapter uploads it field-for-field from SceneFrame + SceneDraw, same as
// Metal does from SceneFrame::viewProjection / frame.draws[i].
struct VulkanSceneUniforms
{
    float viewProjection[16];
    float model[16];
    float normalMatrix[9];
    float baseColour[4];
    float highlightColour[4];
    float parameters[4];   // xyz = per-material parameters[0..2]; w = material selector
    float globals[4];      // x = timeSeconds, y = drive, z = glow, w = textureWeights[2]
    float lightDirection[4]; // xyz = light dir, w = textureWeights[1]
};

// Vertex input layout the Vulkan adapter uses for SceneVertex.
// Matches the Metal vertex layout (float4 position, float3 normal), interleaved,
// same offset/stride discipline as TapeSceneMetalShaders.h + NativeRenderer_Metal.mm.
struct VulkanSceneVertex
{
    float position[4];
    float normal[3];
};

// Static anchor declarations the Vulkan adapter includes once SPIR-V is embedded.
// These are the symbols the build-generated *_spirv.h headers define. Until the
// build plumbing is wired, these are the names the Vulkan shader path must
// eventually consume.
extern const unsigned char VulkanSceneShaders_vert_spirv[];
extern const unsigned int VulkanSceneShaders_vert_spirv_words;

extern const unsigned char VulkanSceneShaders_frag_spirv[];
extern const unsigned int VulkanSceneShaders_frag_spirv_words;

} // namespace j37::render::vulkan
