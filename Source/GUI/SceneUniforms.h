#pragma once
#include "SceneFrame.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>

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
 *    material? No вЂ” viewProjection is per-FRAME only (`SceneFrame::viewProjection`).
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


/** The vertex stage's uniform block: three matrices and the five scalars the
    deck's vertex program reads. Plain floats - no JUCE, no GPU handle - so the
    mapping from a SceneDraw to the block a backend uploads is arithmetic a
    harness can check without a device.

    The layout is the shader's, member for member: 3 mat4 then 5 float, which is
    what UNIFORMLAYOUT_NATIVE maps in the sokol adapter and what the OpenGL path
    fills by hand. The sizes are asserted here rather than in one adapter, so a
    backend that grows a member cannot drift from the shader it feeds. */
struct VsUniforms
{
    float viewProjection[16];
    float model[16];
    float normalMatrix[16];
    float mode;
    float packInner;
    float packOuter;
    float time;
    float flutter;
};

/** The fragment stage's uniform block: colours, light, and the per-material
    scalars, in the order the shader declares them. */
struct FsUniforms
{
    float baseColour[3];
    float highlightColour[3];
    float lightDirection[3];
    float textureWeights[3];
    float panelHalf[3];
    float glow;
    float drive;
    float mode;
    float time;
    float arcAngle;
    float keyDown;
};

static_assert (sizeof (VsUniforms) == 3 * 64 + 5 * 4, "VS uniform block size drifted");
static_assert (sizeof (FsUniforms) == 5 * 12 + 6 * 4, "FS uniform block size drifted");

/** Both blocks of one draw, as a backend uploads them. */
struct DrawUniforms
{
    VsUniforms vertex {};
    FsUniforms fragment {};
};

/** Turns one draw of a frame into the blocks the shaders read.

    The parameters triple means something different per material - the same
    switch the OpenGL path makes - so that translation lives here once instead of
    in every adapter: tapePack reads the pack's inner and outer radii, ribbon its
    flutter, controlPanel the panel's half extents, knob the arc angle, key its
    pressed flag, and a glow disc takes its own amount as the glow term. */
inline DrawUniforms makeDrawUniforms (const SceneFrame& frame, const SceneDraw& draw) noexcept
{
    DrawUniforms uniforms;

    std::memcpy (uniforms.vertex.viewProjection, frame.viewProjection.data(),
                 sizeof (uniforms.vertex.viewProjection));
    std::memcpy (uniforms.vertex.model, draw.model.data(), sizeof (uniforms.vertex.model));
    std::memcpy (uniforms.vertex.normalMatrix, draw.normalMatrix.data(),
                 sizeof (uniforms.vertex.normalMatrix));

    uniforms.vertex.mode = static_cast<float> (draw.material);
    uniforms.vertex.time = frame.timeSeconds;

    std::memcpy (uniforms.fragment.baseColour, draw.baseColour.data(),
                 sizeof (uniforms.fragment.baseColour));
    std::memcpy (uniforms.fragment.highlightColour, draw.highlightColour.data(),
                 sizeof (uniforms.fragment.highlightColour));
    std::memcpy (uniforms.fragment.lightDirection, frame.lightDirection.data(),
                 sizeof (uniforms.fragment.lightDirection));
    std::memcpy (uniforms.fragment.textureWeights, frame.textureWeights.data(),
                 sizeof (uniforms.fragment.textureWeights));

    uniforms.fragment.mode = uniforms.vertex.mode;
    uniforms.fragment.time = uniforms.vertex.time;
    uniforms.fragment.drive = frame.drive;
    uniforms.fragment.glow = frame.glow;

    switch (draw.material)
    {
        case SceneMaterial::tapePack:
            uniforms.vertex.packInner = draw.parameters[0];
            uniforms.vertex.packOuter = draw.parameters[1];
            break;

        case SceneMaterial::ribbon:
            uniforms.vertex.flutter = draw.parameters[0];
            break;

        case SceneMaterial::controlPanel:
            uniforms.fragment.panelHalf[0] = draw.parameters[0];
            uniforms.fragment.panelHalf[1] = draw.parameters[1];
            uniforms.fragment.panelHalf[2] = draw.parameters[2];
            break;

        case SceneMaterial::knob:
            uniforms.fragment.arcAngle = draw.parameters[0];
            break;

        case SceneMaterial::key:
            uniforms.fragment.keyDown = draw.parameters[1];
            break;

        case SceneMaterial::glow:
            // A lit disc is light ON the face, so it is added rather than
            // composited - and the glow term here is the disc's own amount,
            // not the machine's.
            uniforms.fragment.glow = draw.parameters[0];
            break;

        case SceneMaterial::solid:
            break;
    }

    return uniforms;
}

/** What one renderer instance has already uploaded, so a draw sends a block only
    when it differs from the one the GPU is holding.

    Three facts decide the shape of this:

    * sokol_gfx's sg_apply_uniforms takes ONE stage's block per call, so the
      finest granularity the API can offer is the stage: a draw whose vertex
      block is unchanged still uploads nothing on the fragment side, and the
      other way round. That is the part of "upload what changed" the API can
      honour, and it is why the block is cached separately per stage.
    * the uploaded values live in the GPU program, not in this object. A program
      created after a context loss starts with zeroed uniforms, so the adapter
      must call reset() when it makes a new one; remembering what the previous
      program held would skip an upload into an empty program.
    * the scene owns state the block does not show: an editor that changed the
      palette or moved the control bank has changed what the uniforms MEAN even
      when the floats happen to match. invalidate() is its lever, and the compare
      cannot replace it.

    invalidate() is the only member the message thread may call; everything else
    belongs to the thread that records the frame. */
class UniformCache
{
public:
    void invalidate() noexcept { generation.fetch_add (1, std::memory_order_relaxed); }

    /** Forgets what the GPU holds, for a program that has just been created. */
    void reset() noexcept
    {
        vertexApplied = false;
        fragmentApplied = false;
    }

    /** Records the block and answers true when it has to be uploaded: the first
        time, after a reset or an invalidate(), or when the bytes differ. */
    bool take (const VsUniforms& block) noexcept
    {
        return takeBlock (vertexBlock, vertexApplied, vertexGeneration, block);
    }

    bool take (const FsUniforms& block) noexcept
    {
        return takeBlock (fragmentBlock, fragmentApplied, fragmentGeneration, block);
    }

    /** Counters, for a harness or a status line: every upload this instance made
        and every block it skipped because the GPU already held it. */
    std::uint64_t uploads() const noexcept { return uploadedBlocks; }
    std::uint64_t skips() const noexcept { return skippedBlocks; }

private:
    template <typename Block>
    bool takeBlock (Block& applied, bool& hasApplied, std::uint64_t& appliedAt,
                    const Block& block) noexcept
    {
        static_assert (std::is_trivially_copyable<Block>::value,
                       "a uniform block is compared byte for byte");

        const auto current = generation.load (std::memory_order_relaxed);

        if (hasApplied && appliedAt == current
            && std::memcmp (&applied, &block, sizeof (Block)) == 0)
        {
            ++skippedBlocks;
            return false;
        }

        applied = block;
        appliedAt = current;
        hasApplied = true;
        ++uploadedBlocks;
        return true;
    }

    std::atomic<std::uint64_t> generation { 0 };

    VsUniforms vertexBlock {};
    FsUniforms fragmentBlock {};
    bool vertexApplied = false;
    bool fragmentApplied = false;
    std::uint64_t vertexGeneration = 0;
    std::uint64_t fragmentGeneration = 0;
    std::uint64_t uploadedBlocks = 0;
    std::uint64_t skippedBlocks = 0;
};

} // namespace j37::render
