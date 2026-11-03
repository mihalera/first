#version 450

// Vulkan scene vertex shader - first deliverable of docs/backend-implementation-plan-vulkan-shaders.md.
//
// This is the GLSL 450 source the Vulkan pipeline consumes. The SPIR-V is derived from this
// file by glslangValidator at build time and embedded through the same discipline as
// TapeSceneMetalShaders.h. The Metal shader (e382d6ea:Source/GUI/TapeSceneMetalShaders.h) is the
// reference spelling of the same scene; the Vulkan shader reproduces that math, not a new one.
//
// Uniforms mirror MetalUniforms field-for-field (see SceneUniforms.h for the full contract and
// the per-material parameters[0..2] meanings). The material selector is packed into the w
// component of `parameters`, exactly as in Metal.
//
// Sources referenced in the contract:
//  - packing table: Source/GUI/SceneUniforms.h
//  - Metal shader source: Source/GUI/TapeSceneMetalShaders.h (commit e382d6ea)
//  - geometry layout: Source/GUI/SceneVertex.h / SceneFrame.h
//  - binding site the Metal adapter uses: NativeRenderer_Metal.mm (vertexShader, buffer(1))

layout(set = 0, binding = 0) uniform Uniforms
{
    mat4 viewProjection;
    mat4 model;
    mat3 normalMatrix;
    vec4 baseColour;
    vec4 highlightColour;
    vec4 parameters;   // xyz = per-material parameters[0..2]; w = material selector
    vec4 globals;      // x = timeSeconds, y = drive, z = glow, w = textureWeights[2]
    vec4 lightDirection; // xyz = light dir, w = textureWeights[1]
} u;

layout(location = 0) in vec4 aPosition;
layout(location = 1) in vec3 aNormal;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vLocal;
layout(location = 2) out float vDetail;

void main()
{
    vec3 p = aPosition.xyz;

    // Local frame derived from XY; detail is the radial distance, used by the tape band.
    vLocal = aPosition.xy;
    vDetail = length(aPosition.xy);

    const uint material = uint(u.parameters.w + 0.5);

    if (material == 1u) {
        // tapePack: radius interpolates between parameters.x and parameters.y along the ribbon's
        // Z position, so the tape expands/contracts with the ribbon parameter.
        float radius = mix(u.parameters.x, u.parameters.y, aPosition.z);
        p = vec3(aPosition.xy * radius, aPosition.w);
        vDetail = aPosition.z;
    } else if (material == 2u) {
        // ribbon: travel along the ribbon parameter W, with a small flutter-driven ripple on the
        // surface. parameters.x holds uFlutter (amplitude) in the Metal/contract sense.
        float t = aPosition.w;
        float envelope = t * (1.0 - t) * 4.0;
        p.z += sin(t * 11.0 - u.globals.x * 6.0) * u.parameters.x * 0.030 * envelope;
        p.y += sin(t * 7.0 + u.globals.x * 4.0) * u.parameters.x * 0.026 * envelope;
        vDetail = t * 40.0;
    }

    vNormal = normalize((u.normalMatrix * vec4(aNormal, 0.0)).xyz);
    gl_Position = u.viewProjection * u.model * vec4(p, 1.0);
}
