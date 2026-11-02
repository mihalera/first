#version 450

// Vulkan scene fragment shader - first deliverable of docs/backend-implementation-plan-vulkan-shaders.md.
//
// This is the GLSL 450 source the Vulkan pipeline consumes. The SPIR-V is derived from this
// file by glslangValidator at build time and embedded through the same discipline as
// TapeSceneMetalShaders.h. The Metal shader (e382d6ea:Source/GUI/TapeSceneMetalShaders.h) is the
// reference spelling of the same scene; the Vulkan shader reproduces that math, not a new one.
//
// Material selection is on the fragment side from the w component of `parameters`, matching Metal.
//
// Texture weights today come from the packed global/view uniforms the Metal adapter sets:
//  - grain weight   = u.globals.w
//  - shimmer weight = u.lightDirection.w
//  - wear weight    = u.parameters.z
// Those are the values the Metal fragment shader reads today; Vulkan uses the same spelling so the
// two paths stay visually equivalent and the packing contract stays the source of truth.

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vLocal;
layout(location = 2) in float vDetail;

layout(location = 0) out vec4 fragColor;

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

// Simple GL-style procedural noise helpers, mirroring the Metal helpers exactly.
float hash2(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float valueNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash2(i), hash2(i + vec2(1.0, 0.0)), u.x),
               mix(hash2(i + vec2(0.0, 1.0)), hash2(i + vec2(1.0, 1.0)), u.x), u.y);
}

void main()
{
    const uint material = uint(u.parameters.w + 0.5);
    vec3 n = normalize(vNormal);
    vec3 l = normalize(u.lightDirection.xyz);
    vec3 eye = vec3(0.0, 0.0, 1.0);
    vec3 h = normalize(l + eye);
    float diffuse = max(dot(n, l), 0.0);
    float specular = pow(max(dot(n, h), 0.0), 38.0);
    float rim = pow(1.0 - max(dot(n, eye), 0.0), 3.0);

    vec3 colour = u.baseColour.rgb * (0.20 + 0.90 * diffuse);
    colour += u.highlightColour.rgb * specular * 0.70;
    colour += u.highlightColour.rgb * rim * (0.10 + 0.50 * u.globals.z);

    // Tape-like texture for the grouped tape/pack materials (material 0..2 in Vulkan indexing
    // matches the Metal grouping this shader was derived from). Weights today are spread across the
    // same three packed fields the Metal fragment shader reads.
    if (material <= 2u) {
        vec3 tapeTextureWeights = vec3(u.globals.w, u.lightDirection.w, u.parameters.z);
        float grain = (hash2(vLocal * 7.5) - 0.5) * tapeTextureWeights.x;
        float shimmer = (hash2(vLocal * 40.0 + vec2(0.0, u.globals.x * 6.0)) - 0.5)
                        * tapeTextureWeights.y;
        float wear = (1.0 - valueNoise(vLocal * 4.0)) * tapeTextureWeights.z;
        colour += grain + shimmer - wear;
        colour = mix(colour, u.highlightColour.rgb, 0.20 * u.globals.y)
                 * (1.0 + 0.30 * u.globals.y);
        float band = 0.88 + 0.12 * smoothstep(0.18, 0.50, abs(fract(vDetail) - 0.5));
        fragColor = vec4(clamp(colour * band, 0.0, 1.0), 1.0);
        return;
    }

    vec2 face = vLocal;
    float grain = (hash2(face * 7.5) - 0.5) * u.globals.w;
    float shimmer = (hash2(face * 40.0 + vec2(0.0, u.globals.x * 6.0)) - 0.5)
                    * u.lightDirection.w;
    float wear = (1.0 - valueNoise(face * 4.0)) * u.parameters.z;

    if (material == 3u) {
        // controlPanel: base texture plus a panel-half-extent vignette.
        colour += grain + shimmer - wear;
        float brush = 0.965 + 0.035 * abs(fract(vLocal.y * 46.0) - 0.5) * 2.0;
        vec2 panelHalf = max(abs(u.parameters.xy), vec2(0.0001));
        vec2 panelUv = face / panelHalf;
        colour *= 1.0 - 0.28 * smoothstep(0.35, 1.15, length(panelUv));
        fragColor = vec4(clamp(colour * brush, 0.0, 1.0), 1.0);
        return;
    }

    if (material == 4u) {
        // knob: arc-based highlight around the disc, scaled by parameters.x (uArcAngle) and
        // modulated by the frame glow.
        colour += grain + shimmer - wear;
        float r = length(vLocal);
        float angle = atan(vLocal.x, vLocal.y);
        float t = clamp((angle - u.parameters.x) / 0.30, 0.0, 1.0);
        float arc = smoothstep(0.70, 0.80, r) * (1.0 - smoothstep(0.90, 0.99, r));
        colour += u.highlightColour.rgb * arc * (1.0 - t) * (0.35 + 0.65 * u.globals.z);
        fragColor = vec4(clamp(colour, 0.0, 1.0), 1.0);
        return;
    }

    if (material == 5u) {
        // key: panel-style texture plus a simple key-down darkening on parameters.y (uKeyDown).
        colour += grain + shimmer - wear;
        colour *= 1.0 - 0.35 * u.parameters.y;
        fragColor = vec4(clamp(colour, 0.0, 1.0), 1.0);
        return;
    }

    // fallback solid-ish glow used for the one remaining material path in the Metal shader.
    fragColor = vec4(u.highlightColour.rgb * u.parameters.x, 1.0);
}
