// Checks the two things the sokol adapter now relies on and cannot show on its
// own: that a draw becomes the uniform block the shader declares, and that the
// cache uploads a block exactly when the program does not already hold it.
//
// Neither needs a GPU, sokol or JUCE - Source/GUI/SceneUniforms.h is plain C++ -
// so this runs anywhere a C++17 compiler does, including CI jobs with no display.

#include "GUI/SceneUniforms.h"

#include <cstddef>
#include <cstdio>

namespace {

using namespace j37::render;

int failures = 0;

void check (bool condition, const char* name)
{
    std::printf ("%s  %s\n", condition ? "PASS" : "FAIL", name);

    if (! condition)
        ++failures;
}

SceneFrame makeFrame()
{
    SceneFrame frame;

    for (std::size_t i = 0; i < frame.viewProjection.size(); ++i)
        frame.viewProjection[i] = 0.5f * static_cast<float> (i);

    frame.lightDirection = { 0.25f, 0.5f, 0.75f };
    frame.textureWeights = { 0.1f, 0.2f, 0.3f };
    frame.backgroundColour = { 0.0f, 0.05f, 0.1f, 1.0f };
    frame.highlightColour = { 0.8f, 0.6f, 0.3f, 1.0f };
    frame.timeSeconds = 1.25f;
    frame.drive = 0.4f;
    frame.flutter = 0.15f;
    frame.glow = 0.7f;

    return frame;
}

SceneDraw makeDraw (SceneMaterial material)
{
    SceneDraw draw;
    draw.material = material;

    for (std::size_t i = 0; i < draw.model.size(); ++i)
        draw.model[i] = 2.0f + static_cast<float> (i);

    for (std::size_t i = 0; i < draw.normalMatrix.size(); ++i)
        draw.normalMatrix[i] = 100.0f + static_cast<float> (i);

    draw.baseColour = { 0.11f, 0.22f, 0.33f, 1.0f };
    draw.highlightColour = { 0.44f, 0.55f, 0.66f };
    draw.parameters = { 0.125f, 0.375f, 0.625f };

    return draw;
}

void checkMapping()
{
    const auto frame = makeFrame();
    const auto draw = makeDraw (SceneMaterial::knob);
    const auto uniforms = makeDrawUniforms (frame, draw);

    bool matrices = true;

    for (std::size_t i = 0; i < frame.viewProjection.size(); ++i)
        matrices = matrices && uniforms.vertex.viewProjection[i] == frame.viewProjection[i];

    for (std::size_t i = 0; i < draw.model.size(); ++i)
        matrices = matrices && uniforms.vertex.model[i] == draw.model[i]
                   && uniforms.vertex.normalMatrix[i] == draw.normalMatrix[i];

    check (matrices, "a draw's matrices reach the vertex block unchanged");

    check (uniforms.vertex.mode == static_cast<float> (SceneMaterial::knob)
               && uniforms.fragment.mode == uniforms.vertex.mode,
           "the material reaches both blocks as the mode");

    check (uniforms.vertex.time == frame.timeSeconds && uniforms.fragment.time == frame.timeSeconds,
           "the frame's time reaches both blocks");

    check (uniforms.fragment.baseColour[2] == draw.baseColour[2]
               && uniforms.fragment.highlightColour[1] == draw.highlightColour[1],
           "a draw's colours reach the fragment block");

    check (uniforms.fragment.lightDirection[2] == frame.lightDirection[2]
               && uniforms.fragment.textureWeights[0] == frame.textureWeights[0],
           "the frame's light and texture weights reach the fragment block");

    check (uniforms.fragment.drive == frame.drive && uniforms.fragment.glow == frame.glow,
           "the frame's drive and glow reach the fragment block as defaults");

    check (uniforms.fragment.arcAngle == draw.parameters[0],
           "a knob reads its angle from parameters[0]");

    check (uniforms.vertex.packInner == 0.0f && uniforms.vertex.packOuter == 0.0f
               && uniforms.vertex.flutter == 0.0f && uniforms.fragment.keyDown == 0.0f
               && uniforms.fragment.panelHalf[0] == 0.0f && uniforms.fragment.panelHalf[2] == 0.0f,
           "a knob leaves the other materials' terms at zero");
}

void checkMaterials()
{
    const auto frame = makeFrame();

    {
        const auto draw = makeDraw (SceneMaterial::tapePack);
        const auto uniforms = makeDrawUniforms (frame, draw);
        check (uniforms.vertex.packInner == draw.parameters[0]
                   && uniforms.vertex.packOuter == draw.parameters[1],
               "a tape pack reads its inner and outer radii from parameters");
    }

    {
        const auto draw = makeDraw (SceneMaterial::ribbon);
        const auto uniforms = makeDrawUniforms (frame, draw);
        check (uniforms.vertex.flutter == draw.parameters[0],
               "a ribbon reads its flutter from parameters[0]");
        check (uniforms.fragment.arcAngle == 0.0f,
               "a ribbon does not borrow the knob's angle");
    }

    {
        const auto draw = makeDraw (SceneMaterial::controlPanel);
        const auto uniforms = makeDrawUniforms (frame, draw);
        check (uniforms.fragment.panelHalf[0] == draw.parameters[0]
                   && uniforms.fragment.panelHalf[1] == draw.parameters[1]
                   && uniforms.fragment.panelHalf[2] == draw.parameters[2],
               "a control panel reads its half extents from all three parameters");
    }

    {
        const auto draw = makeDraw (SceneMaterial::key);
        const auto uniforms = makeDrawUniforms (frame, draw);
        check (uniforms.fragment.keyDown == draw.parameters[1],
               "a key reads its pressed flag from parameters[1]");
    }

    {
        const auto draw = makeDraw (SceneMaterial::glow);
        const auto uniforms = makeDrawUniforms (frame, draw);
        check (uniforms.fragment.glow == draw.parameters[0],
               "a glow disc's own amount replaces the frame's glow term");
        check (uniforms.vertex.flutter == 0.0f, "a glow disc sends no ribbon flutter");
    }

    {
        const auto draw = makeDraw (SceneMaterial::solid);
        const auto uniforms = makeDrawUniforms (frame, draw);
        check (uniforms.fragment.panelHalf[0] == 0.0f && uniforms.fragment.arcAngle == 0.0f
                   && uniforms.fragment.keyDown == 0.0f && uniforms.vertex.packInner == 0.0f
                   && uniforms.fragment.glow == frame.glow,
               "a solid draw takes no per-material term and keeps the frame's glow");
    }
}

void checkCache()
{
    const auto frame = makeFrame();
    const auto draw = makeDraw (SceneMaterial::knob);
    const auto first = makeDrawUniforms (frame, draw);

    UniformCache cache;

    check (cache.uploads() == 0 && cache.skips() == 0, "a fresh cache has counted nothing");

    check (cache.take (first.vertex), "the first draw uploads the vertex block");
    check (cache.take (first.fragment), "the first draw uploads the fragment block");
    check (! cache.take (first.vertex), "an identical vertex block is not uploaded again");
    check (! cache.take (first.fragment), "an identical fragment block is not uploaded again");

    auto turned = first;
    turned.fragment.arcAngle = 0.9f;
    check (! cache.take (turned.vertex),
           "a draw that only turned its knob does not re-upload the matrices");
    check (cache.take (turned.fragment), "a turned knob uploads the fragment block");

    auto moved = turned;
    moved.vertex.model[12] = 5.0f;
    check (cache.take (moved.vertex), "a moved draw uploads the vertex block");
    check (! cache.take (moved.fragment), "a moved draw leaves the unchanged fragment block alone");

    const auto skipsBefore = cache.skips();
    check (! cache.take (moved.fragment) && cache.skips() == skipsBefore + 1,
           "a skipped block is counted");

    UniformCache repeated;
    int uploads = 0;

    for (int i = 0; i < 8; ++i)
        uploads += repeated.take (first.fragment) ? 1 : 0;

    check (uploads == 1, "eight draws sharing a fragment block upload it once");

    UniformCache marked;
    marked.take (moved.vertex);
    marked.take (moved.fragment);
    marked.invalidate();
    check (marked.take (moved.vertex) && marked.take (moved.fragment),
           "a scene mark re-uploads both blocks even though the bytes match");
    check (! marked.take (moved.vertex) && ! marked.take (moved.fragment),
           "the re-upload settles back into skipping");

    UniformCache recreated;
    recreated.take (moved.vertex);
    recreated.take (moved.fragment);
    recreated.reset();
    check (recreated.take (moved.vertex) && recreated.take (moved.fragment),
           "a new program re-uploads both blocks it does not hold");

    check (moved.fragment.arcAngle != first.fragment.arcAngle, "the harness changed what it meant to");
}

} // namespace

int main()
{
    checkMapping();
    checkMaterials();
    checkCache();

    if (failures == 0)
        std::printf ("\nall uniform checks passed\n");

    return failures == 0 ? 0 : 1;
}
