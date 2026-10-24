#pragma once
namespace j37::render::metal {

inline constexpr char sceneShaderSource[] = R"metal(
#include <metal_stdlib>
using namespace metal;

struct SceneVertexIn {
    float4 position [[attribute(0)]];
    float3 normal [[attribute(1)]];
};

struct SceneUniforms {
    float4x4 viewProjection;
    float4x4 model;
    float4x4 normalMatrix;
    float4 baseColour;
    float4 highlightColour;
    float4 parameters;
    float4 globals;
    float4 lightDirection;
};

struct SceneVertexOut {
    float4 position [[position]];
    float3 normal;
    float2 local;
    float detail;
};

vertex SceneVertexOut tapeSceneVertex(SceneVertexIn in [[stage_in]],
                                      constant SceneUniforms& u [[buffer(1)]]) {
    SceneVertexOut out;
    float3 p = in.position.xyz;
    out.local = in.position.xy;
    out.detail = length(in.position.xy);
    const uint material = uint(u.parameters.w + 0.5);

    if (material == 1) {
        const float radius = mix(u.parameters.x, u.parameters.y, in.position.z);
        p = float3(in.position.xy * radius, in.position.w);
        out.detail = in.position.z;
    } else if (material == 2) {
        const float t = in.position.w;
        const float envelope = t * (1.0 - t) * 4.0;
        p.z += sin(t * 11.0 - u.globals.x * 6.0) * u.parameters.x * 0.030 * envelope;
        p.y += sin(t * 7.0 + u.globals.x * 4.0) * u.parameters.x * 0.026 * envelope;
        out.detail = t * 40.0;
    }

    out.normal = (u.normalMatrix * float4(in.normal, 0.0)).xyz;
    out.position = u.viewProjection * u.model * float4(p, 1.0);
    return out;
}

float hash2(float2 p) {
    return fract(sin(dot(p, float2(127.1, 311.7))) * 43758.5453123);
}

float valueNoise(float2 p) {
    float2 i = floor(p);
    float2 f = fract(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash2(i), hash2(i + float2(1.0, 0.0)), u.x),
               mix(hash2(i + float2(0.0, 1.0)), hash2(i + float2(1.0, 1.0)), u.x), u.y);
}

fragment float4 tapeSceneFragment(SceneVertexOut in [[stage_in]],
                                  constant SceneUniforms& u [[buffer(1)]]) {
    const uint material = uint(u.parameters.w + 0.5);
    float3 n = normalize(in.normal);
    float3 l = normalize(u.lightDirection.xyz);
    float3 eye = float3(0.0, 0.0, 1.0);
    float3 h = normalize(l + eye);
    float diffuse = max(dot(n, l), 0.0);
    float specular = pow(max(dot(n, h), 0.0), 38.0);
    float rim = pow(1.0 - max(dot(n, eye), 0.0), 3.0);
    float3 colour = u.baseColour.rgb * (0.20 + 0.90 * diffuse);
    colour += u.highlightColour.rgb * specular * 0.70;
    colour += u.highlightColour.rgb * rim * (0.10 + 0.50 * u.globals.z);

    if (material <= 2) {
        const float3 tapeTextureWeights = float3(u.globals.w, u.lightDirection.w,
                                                  u.parameters.z);
        const float grain = (hash2(in.local * 7.5) - 0.5) * tapeTextureWeights.x;
        const float shimmer = (hash2(in.local * 40.0
                                     + float2(0.0, u.globals.x * 6.0)) - 0.5)
                              * tapeTextureWeights.y;
        const float wear = (1.0 - valueNoise(in.local * 4.0)) * tapeTextureWeights.z;
        colour += grain + shimmer - wear;
        colour = mix(colour, u.highlightColour.rgb, 0.20 * u.globals.y) * (1.0 + 0.30 * u.globals.y);
        const float band = 0.88 + 0.12 * smoothstep(0.18, 0.50, abs(fract(in.detail) - 0.5));
        return float4(clamp(colour * band, 0.0, 1.0), 1.0);
    }

    const float2 face = in.local;
    const float grain = (hash2(face * 7.5) - 0.5) * u.globals.w;
    const float shimmer = (hash2(face * 40.0 + float2(0.0, u.globals.x * 6.0)) - 0.5)
                          * u.lightDirection.w;
    const float wear = (1.0 - valueNoise(face * 4.0)) * u.parameters.z;

    if (material == 3) {
        colour += grain + shimmer - wear;
        const float brush = 0.965 + 0.035 * abs(fract(in.local.y * 46.0) - 0.5) * 2.0;
        const float2 panelUv = in.local / max(u.parameters.xy, float2(0.0001));
        colour *= 1.0 - 0.28 * smoothstep(0.35, 1.15, length(panelUv));
        return float4(clamp(colour * brush, 0.0, 1.0), 1.0);
    }

    if (material == 4) {
        colour += grain + shimmer - wear;
        const float r = length(in.local);
        const float angle = atan2(in.local.x, in.local.y);
        const float t = clamp((angle - u.parameters.x) / 0.30, 0.0, 1.0);
        const float arc = smoothstep(0.70, 0.80, r) * (1.0 - smoothstep(0.90, 0.99, r));
        colour += u.highlightColour.rgb * arc * (1.0 - t) * (0.35 + 0.65 * u.globals.z);
        return float4(clamp(colour, 0.0, 1.0), 1.0);
    }

    if (material == 5) {
        colour += grain + shimmer - wear;
        colour *= 1.0 - 0.35 * u.parameters.y;
        return float4(clamp(colour, 0.0, 1.0), 1.0);
    }

    return float4(u.highlightColour.rgb * u.parameters.x, u.parameters.x);
}
)metal";

} // namespace j37::render::metal
