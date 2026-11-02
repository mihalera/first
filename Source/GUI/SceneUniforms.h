#pragma once
#include "SceneFrame.h"

namespace j37::render {

/** The per-draw uniform layout every native backend must reproduce.
 *
 * `SceneFrame` is the only data that crosses the boundary; backends translate it
 * into their own uniform/constant-buffer layout. This header is the contract the
 * later ports (Phase 1 onward) follow so they do not re-derive it from the GL
 * and Metal paths and drift apart.
 *
 * Field packing:
 *  - `SceneDraw::viewProjection` is the per-draw view-projection for the draw's
 *    material? No — viewProjection is per-FRAME only (`SceneFrame::viewProjection`).
 *    Each `SceneDraw` carries `model`, `normalMatrix`, `baseColour`,
 *    `highlightColour`, `parameters[3]`, `meshIndex`, `material`.
 *
 * Per-draw `parameters[0..2]` mean different things per `SceneMaterial`; the
 * Metal path packs them into one `simd_float4 parameters` with the fourth
 * component holding a packed material selector, the GL path sets discrete GLSL
 * uniforms, and the Sokol path declares discrete GLSL uniforms as well. The
 * mapping is the reference; any new backend must reproduce it exactly.
 *
 * Frame-level globals that every backend sets:
 *  - `viewProjection`  (SceneFrame): camera. GL binding site
 *    `TapeScene.cpp:1072`ish; Metal: `MetalUniforms::viewProjection`.
 *  - `lightDirection`  (SceneFrame): world-space light dir for the Lambert term.
 *  - `textureWeights`  (SceneFrame): unused by the scene shaders today; reserved.
 *  - `backgroundColour` (SceneFrame): background clear colour the adapters read.
 *  - `highlightColour` (SceneFrame): disc / panel highlight tint. GL binding site
 *    `TapeScene.cpp:1094` (per draw); Metal: `MetalUniforms::highlightColour`.
 *  - `timeSeconds`     (SceneFrame): `uTime` in the shaders.
 *  - `drive`           (SceneFrame): read by the tape/pack logic (the radial offset
 *    and the tape surface motion); not a named GLSL uniform in the shipped shaders
 *    today, it is consumed in C++ before the draw (`TapeScene.cpp` draw-loop).
 *  - `flutter`         (SceneFrame): amplitude that feeds `uFlutter` per `ribbon`
 *    draw. In Metal it is packed into `parameters.x` of the `ribbon` uniform.
 *  - `glow`            (SceneFrame): the frame glow passed to the glow material and
 *    also used by a few material paths; the shader-level `uGlow` is set per-draw
 *    from `draw.parameters[0]` for `glow`, and from `frame.glow` elsewhere.
 *
 * Per-draw `parameters[0..2]` by material (GL binding site:
 * `TapeScene.cpp:1076-1096`; Metal packing site `NativeRenderer_Metal.mm:252-270`;
 * Sokol GLSL uniforms declared in `NativeRenderer_Sokol.cpp:59-120` and bound at
 * `NativeRenderer_Sokol.cpp:577-596`):
 *
 *  - `solid`:          parameters unused by the shipped solid shader today.
 *  - `tapePack`:        parameters[0] = uPackInner,  parameters[1] = uPackOuter,
 *                       parameters[2] = unused.
 *  - `ribbon`:          parameters[0] = uFlutter (amplitude), parameters[1..2] unused
 *                       in the shipped ribbon shader. In Metal this is the one case
 *                       where `uFlutter` becomes `parameters.x` (the vertex stage
 *                       reads `uniforms.parameters.x`), so `ribbon` is the material
 *                       that keeps `parameters[0]` live in the Metal path.
 *  - `controlPanel`:    parameters[0] = uPanelHalf.x, parameters[1] = uPanelHalf.y,
 *                       parameters[2] = uPanelHalf.z (the panel half-extents vector,
 *                       a `vec3` in the shader; GL sets it as three floats, Metal
 *                       packs it into `parameters[0..2]`).
 *  - `knob`:           parameters[0] = uArcAngle, parameters[1] = uKeyDown,
 *                       parameters[2] = unused.
 *  - `key`:            parameters[0] = uArcAngle, parameters[1] = uKeyDown,
 *                       parameters[2] = unused. (Same slot meanings as `knob`; the
 *                       material flag is what distinguishes them.)
 *  - `glow`:          parameters[0] = uGlow (the disc's own glow amount for this
 *                       draw, NOT the frame `glow`), parameters[1] = unused,
 *                       parameters[2] = unused.
 *
 * Backend uniform naming (reference spellings, not all backends use all of these
 * as separate uniforms):
 *  GL/MSL/Sokol shared names: uPackInner, uPackOuter, uFlutter, uPanelHalf
 *  (vec3), uArcAngle, uKeyDown, uGlow, uHighlightColour, uTime, uLightDirection.
 *  Metal additionally: viewProjection, model, normalMatrix, baseColour,
 *  highlightColour (per-draw), parameters (packed), and a material selector packed
 *  into the w component of `parameters`.
 *
 * Invariant checked by `tests/scene_frame/pack_contract.cpp`: a frame built the
 * way `TapeScene::buildSceneFrame` does must carry, for each material present in
 * the frame, the parameter slots the contract above claims, and the frame-level
 * globals must be the ones the contract lists. A backend is out of contract when
 * its uniform names or its per-material slot meanings disagree with this table.
 */

} // namespace j37::render
