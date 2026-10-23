#pragma once
namespace j37::render::metal {
inline constexpr char sceneShaderSource[] = R"metal(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float4 position [[attribute(0)]];
    float3 normal [[attribute(1)]];
};

struct SceneUniforms {
    float4x4 viewProjection;
    float4x4 model;
    float3x3 normalMatrix;
    float4 baseColour;
    float4 highlightColour;
    float4 parameters;
    float4 globals;
    float4 lightDirection;
};

struct VertexOut {
    float4 position [[position]];
    float3 normal;
    float3 local;
    float detail;
    float2 uv;
};

vertex VertexOut tapeSceneVertex(VertexIn in [[stage_in]],
                                 constant SceneUniforms& u [[buffer(1)]]) {
    VertexOut out;
    float3 p = in.position.xyz;
    out.local = p;
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
    out.normal = u.normalMatrix * in.normal;
    out.position = u.viewProjection * u.model * float4(p, 1.0);
    out.uv = in.position.xy;
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

fragment float4 tapeSceneFragment(VertexOut in [[stage_in]],
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

    if (material == 0 || material == 1 || material == 2) {
        colour = mix(colour, u.highlightColour.rgb, 0.20 * u.globals.y) * (1.0 + 0.30 * u.globals.y);
        float band = 0.88 + 0.12 * smoothstep(0.18, 0.50, abs(fract(in.detail) - 0.5));
        return float4(clamp(colour * band, 0.0, 1.0), 1.0);
    }

    float2 face = in.uv;
    float grain = (hash2(face * 7.5) - 0.5) * u.globals.w;
    float shimmer = (hash2(face * 40.0 + float2(0.0, u.globals.x * 6.0)) - 0.5) * u.lightDirection.w;
    float wear = (1.0 - valueNoise(face * 4.0)) * u.parameters.z;
    colour += grain + shimmer - wear;

    if (material == 3) {
        float brush = 0.965 + 0.035 * abs(fract(in.uv.y * 46.0) - 0.5) * 2.0;
        float2 panelUv = in.uv / max(u.parameters.xy, float2(0.0001));
        colour *= 1.0 - 0.28 * smoothstep(0.35, 1.15, length(panelUv));
        return float4(clamp(colour * brush, 0.0, 1.0), 1.0);
    }
    if (material == 4) {
        float r = length(in.uv);
        float angle = atan2(in.uv.x, in.uv.y);
        float t = clamp((angle - u.parameters.x) / 0.30, 0.0, 1.0);
        float arc = smoothstep(0.70, 0.80, r) * (1.0 - smoothstep(0.90, 0.99, r));
        colour += u.highlightColour.rgb * arc * (1.0 - t) * (0.35 + 0.65 * u.globals.z);
        return float4(clamp(colour, 0.0, 1.0), 1.0);
    }
    if (material == 5) {
        colour *= 1.0 - 0.35 * u.parameters.y;
        return float4(clamp(colour, 0.0, 1.0), 1.0);
    }
    return float4(u.highlightColour.rgb * u.parameters.x, u.parameters.x);
}
)metal";
} // namespace j37::render::metal
