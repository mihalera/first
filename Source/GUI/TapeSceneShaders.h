// =============================================================================
//  The deck's shader programs: the vertex source, the fragment source, and the
//  precision macro the two of them are pasted through.
//
//  Moved out of TapeScene.cpp - prose, sources and the macro, verbatim - because
//  they are DATA: 335 lines of GLSL string literals that were sitting in the
//  middle of the scene namespace, between the geometry that feeds them and the
//  preparation that compiles them. Nothing cuts them, nothing measures them, and
//  reading the file for the deck's arithmetic no longer means stepping over them.
//
//  What the move cost is the two things a header changes:
//
//    * The two sources are `inline`, in a namespace of their own. In the .cpp they
//      were file-local objects - one copy per translation unit, internal linkage -
//      and that is what a header cannot keep quietly, even though a copy of a
//      `const char* const` is harmless here: the rule Source/GUI/PanelPaint.h
//      states applies to data as much as to state, and `inline` is one word.
//    * The TAPE_SCENE_HIGHP macro travelled with them rather than staying behind.
//      The strings NAME it - prepare() in TapeScene.cpp does a runtime
//      `.replace ("TAPE_SCENE_HIGHP", TAPE_SCENE_HIGHP)` on the compiled text, so
//      it is a fact about the shader text and belongs beside it. TapeScene.cpp
//      includes this header, so the macro is still defined where it is used.
//
//  The dialect is deliberate and stated in the banner below: JUCE's addShader()
//  hands the text to the driver verbatim, so this has to be GLSL that both a
//  legacy context and JUCE's own v3 translators accept.
// =============================================================================
#pragma once

// JUCE_HIGHP expands to nothing on desktop GL and to "highp" on GLES, so it
// cannot be written into the shader strings as an argument: pasting an empty
// macro into a function call is not valid C++ and would not compile. This picks
// the literal instead, and the shader text stays readable as shader text.
#if defined (JUCE_OPENGL_ES) && JUCE_OPENGL_ES
 #define TAPE_SCENE_HIGHP "highp"
#else
 #define TAPE_SCENE_HIGHP ""
#endif

namespace j37scene
{
//==========================================================================
//  GLSL
//
//  Written in the oldest dialect that runs everywhere, on purpose. JUCE's
//  addShader() hands the text to the driver verbatim - it prepends no
//  #version line of its own - so this has to compile as GLSL 1.10 on a
//  legacy context, and it has to be the same text that survives JUCE's
//  translateVertexShaderToV3() on a 3.2 core profile. `attribute`,
//  `varying` and `gl_FragColor` are exactly what those two translators
//  rewrite, so the source is written in the dialect they consume rather than
//  in one they would have to be told about.
//
//  The payload - aPosition.w - is what makes the tape move without a single
//  byte crossing the bus after start-up:
//
//      pack    x,y is a UNIT direction, z is the fraction of the pack's
//             width the vertex sits at, w is its height
//      ribbon  xyz is the point, w is how far along the tape it is
//
//  vDetail is the shader's one texture, and there is no sampler: it is the
//  fraction the vertex sat at, carried to the fragment stage and repeated
//  there. Concentric on a pack, longitudinal on a ribbon, radial on a reel
//  flange - which is what a machined face and a roll of tape both look like,
//  from arithmetic.
//==========================================================================
const char* const vertexShaderSource =
    "attribute vec4 aPosition;\n"
    "attribute vec3 aNormal;\n"
    "\n"
    "uniform mat4 uViewProjection;\n"
    "uniform mat4 uModel;\n"
    "uniform mat3 uNormalMatrix;\n"
    "uniform float uMode;\n"
    "uniform float uPackInner;\n"
    "uniform float uPackOuter;\n"
    "uniform float uTime;\n"
    "uniform float uFlutter;\n"
    "\n"
    "varying vec3 vNormal;\n"
    "varying float vDetail;\n"
    "varying vec2 vLocal;\n"
    "\n"
    "void main()\n"
    "{\n"
    "    vec3 p = aPosition.xyz;\n"
    "    vDetail = length (aPosition.xy);\n"
    "    vLocal = aPosition.xy;\n"
    "\n"
    "    if (uMode > 0.5 && uMode < 1.5)\n"
    "    {\n"
    "        TAPE_SCENE_HIGHP float radius = mix (uPackInner, uPackOuter, aPosition.z);\n"
    "        p = vec3 (aPosition.xy * radius, aPosition.w);\n"
    "        vDetail = aPosition.z;\n"
    "    }\n"
    "    else if (uMode > 1.5 && uMode < 2.5)\n"
    "    {\n"
    "        TAPE_SCENE_HIGHP float t = aPosition.w;\n"
    "        TAPE_SCENE_HIGHP float envelope = t * (1.0 - t) * 4.0;\n"
    "        p.z += sin (t * 11.0 - uTime * 6.0) * uFlutter * 0.030 * envelope;\n"
    "        p.y += sin (t *  7.0 + uTime * 4.0) * uFlutter * 0.026 * envelope;\n"
    "        vDetail = t * 40.0;\n"
    "    }\n"
    "    else if (uMode > 2.5)\n"
    "    {\n"
    "        // The one mode with a lifetime: the glow under a working knob\n"
    "        // breathes on the wall clock rather than on the transport, so it is\n"
    "        // the one place uTime reaches a vertex.\n"
    "        p.z += sin (uTime * 2.6) * 0.004 * aPosition.w;\n"
    "    }\n"
    "\n"
    "    vNormal = uNormalMatrix * aNormal;\n"
    "    gl_Position = uViewProjection * uModel * vec4 (p, 1.0);\n"
    "}\n";

const char* const fragmentShaderSource =
    "TAPE_SCENE_HIGHP varying vec3 vNormal;\n"
    "TAPE_SCENE_HIGHP varying float vDetail;\n"
    "TAPE_SCENE_HIGHP varying vec2 vLocal;\n"
    "\n"
    "uniform vec3 uBaseColour;\n"
    "uniform vec3 uHighlightColour;\n"
    "uniform vec3 uLightDirection;\n"
    "uniform float uGlow;\n"
    "uniform float uDrive;\n"
    "uniform float uMode;\n"
    "uniform float uTime;\n"
    "uniform float uArcAngle;\n"
    "uniform float uKeyDown;\n"
    "uniform vec3 uPanelHalf;\n"
    "uniform vec3 uTextureWeights;\n"
    "\n"
    "// Subtle procedural surface texture for the control bank.\n"
    "float hash (vec2 p)\n"
    "{\n"
    "    return fract (sin (dot (p, vec2 (127.1, 311.7))) * 43758.5453123);\n"
    "}\n"
    "\n"
    "float valueNoise (vec2 p)\n"
    "{\n"
    "    vec2 i = floor (p);\n"
    "    vec2 f = fract (p);\n"
    "    vec2 u = f * f * (3.0 - 2.0 * f);\n"
    "\n"
    "    return mix (mix (hash (i), hash (i + vec2 (1.0, 0.0)), u.x),\n"
    "                mix (hash (i + vec2 (0.0, 1.0)), hash (i + vec2 (1.0, 1.0)), u.x), u.y);\n"
    "}\n"
    "\n"
    "// Procedural grain, tape shimmer and broad wear marks share local\n"
    "// coordinates and user-controlled weights.\n"
    "vec3 panelTexture (vec2 face)\n"
    "{\n"
    "    vec3 textureAccumulator = vec3 (0.0);\n"
    "\n"
    "    textureAccumulator += vec3 ((hash (face * 7.5) - 0.5) * uTextureWeights.x);\n"
    "\n"
    "    float tapeNoise = hash (face * 40.0\n"
    "                            + vec2 (0.0, uTime * 6.0 * (1.0 + uGlow)));\n"
    "    textureAccumulator += vec3 ((tapeNoise - 0.5) * uTextureWeights.y);\n"
    "\n"
    "    float wearPattern = valueNoise (face * 4.0);\n"
    "    textureAccumulator -= vec3 ((1.0 - wearPattern) * uTextureWeights.z);\n"
    "\n"
    "    return textureAccumulator;\n"
    "}\n"
    "\n"
    "void main()\n"
    "{\n"
    "    // Normals and lighting are expressed in scene/view space. The vertex\n"
    "    // stage applies the inverse-transpose normal matrix for the model.\n"
    "    TAPE_SCENE_HIGHP vec3 n = normalize (vNormal);\n"
    "    TAPE_SCENE_HIGHP vec3 l = normalize (uLightDirection);\n"
    "    TAPE_SCENE_HIGHP vec3 eye = vec3 (0.0, 0.0, 1.0);\n"
    "    TAPE_SCENE_HIGHP vec3 h = normalize (l + eye);\n"
    "\n"
    "    TAPE_SCENE_HIGHP float diffuse = max (dot (n, l), 0.0);\n"
    "    TAPE_SCENE_HIGHP float specular = pow (max (dot (n, h), 0.0), 38.0);\n"
    "    TAPE_SCENE_HIGHP float rim = pow (1.0 - max (dot (n, eye), 0.0), 3.0);\n"
    "\n"
    "    TAPE_SCENE_HIGHP vec3 colour = uBaseColour * (0.20 + 0.90 * diffuse);\n"
    "    colour += uHighlightColour * specular * 0.70;\n"
    "    colour += uHighlightColour * rim * (0.10 + 0.50 * uGlow);\n"
    "    // Keep display output in the valid linear range even under bright\n"
    "    // highlight combinations and low-precision mobile fragment hardware.\n"
    "    colour = clamp (colour, vec3 (0.0), vec3 (1.0));\n"
    "\n"
    "    if (uMode < 0.5)\n"
    "    {\n"
    "        // The transport: the accent tint that follows DRIVE, then the detail\n"
    "        // bands, which cost a fract and save every texture.\n"
    "        colour = mix (colour, uHighlightColour, 0.20 * uDrive) * (1.0 + 0.30 * uDrive);\n"
    "\n"
    "        TAPE_SCENE_HIGHP float band = 0.88\n"
    "                                + 0.12 * smoothstep (0.18, 0.50, abs (fract (vDetail) - 0.5));\n"
    "        gl_FragColor = vec4 (colour * band, 1.0);\n"
    "        return;\n"
    "    }\n"
    "\n"
    "    if (uMode < 3.5)\n"
    "    {\n"
    "        // The control bank's body: a machined face with a brushed grain,\n"
    "        // plus the arithmetic texture (see panelTexture above) sampled\n"
    "        // against the face's local xy - grain, live tape shimmer and wear\n"
    "        // in one accumulation, doses governed by uTextureWeights.\n"
    "        TAPE_SCENE_HIGHP float brush = 0.965\n"
    "                                 + 0.035 * abs (fract (vLocal.y * 46.0) - 0.5) * 2.0;\n"
    "        colour += panelTexture (vLocal);\n"
    "        gl_FragColor = vec4 (colour * brush, 1.0);\n"
    "        return;\n"
    "    }\n"
    "\n"
    "    if (uMode < 4.5)\n"
    "    {\n"
    "        // A knob. The same two lights as everything else, plus a sheen that\n"
    "        // tracks the index mark - so the highlight walks round the knurl as\n"
    "        // the knob turns, which is how a knob reads as turning even when the\n"
    "        // mark is a few pixels long.\n"
    "        TAPE_SCENE_HIGHP float sheen = pow (max (dot (n, h), 0.0), 9.0);\n"
    "        colour += uHighlightColour * sheen * 0.16;\n"
    "        colour *= 1.0 - 0.10 * max (0.0, -n.z);\n"
    "\n"
    "        // The lit arc uses the knob mesh's local xy coordinates.\n"
    "        TAPE_SCENE_HIGHP float r = length (vLocal);\n"
    "        TAPE_SCENE_HIGHP float angle = atan (vLocal.x, vLocal.y);\n"
    "        // Fade radial edges and bound the angular sweep to its lit segment.\n"
    "        TAPE_SCENE_HIGHP float t = clamp ((angle - uArcAngle) / 0.30, 0.0, 1.0);\n"
    "        TAPE_SCENE_HIGHP float arc = smoothstep (0.70, 0.80, r) * (1.0 - smoothstep (0.90, 0.99, r));\n"
    "        colour += uHighlightColour * arc * (1.0 - t) * (0.35 + 0.65 * uGlow);\n"
    "\n"
    "        gl_FragColor = vec4 (colour, 1.0);\n"
    "        return;\n"
    "    }\n"
    "\n"
    "    if (uMode < 5.5)\n"
    "    {\n"
    "        // A keycap darkens slightly while pressed.\n"
    "        colour *= 1.0 - 0.35 * uKeyDown;\n"
    "        gl_FragColor = vec4 (colour, 1.0);\n"
    "        return;\n"
    "    }\n"
    "\n"
    "    // The panel face is the final material branch.\n"
    "    TAPE_SCENE_HIGHP vec2 uv = vLocal / uPanelHalf.xy;\n"
    "    colour += panelTexture (vLocal);\n"
    "    colour *= 1.0 - 0.28 * smoothstep (0.35, 1.15, length (uv));\n"
    "    gl_FragColor = vec4 (colour, 1.0);\n"
    "}\n";
}
