#include "../../Source/GUI/SceneUniforms.h"
#include "../../Source/GUI/SceneFrame.h"
#include "../../Source/GUI/SceneLayout.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace j37::render {

static SceneFrame makeFrameForMaterial(SceneMaterial material,
                                        std::array<float, 3> parameters) {
    SceneFrame frame{};
    frame.width = 640;
    frame.height = 480;
    frame.lightDirection = {0.5f, 0.8f, 0.3f};
    frame.timeSeconds = 1.0f;
    frame.drive = 0.3f;
    frame.flutter = 0.12f;
    frame.glow = 0.2f;
    frame.textureWeights = {1.0f, 0.5f, 0.25f};
    frame.backgroundColour = {0.03f, 0.03f, 0.05f, 1.0f};
    frame.highlightColour = {0.9f, 0.9f, 1.0f};
    auto mesh = std::make_shared<std::vector<SceneMesh>>();
    mesh->push_back({
        {{{1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
         {{0.0f, 1.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
         {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}}},
        {0, 1, 2}});
    frame.meshes = mesh;

    SceneDraw draw{};
    draw.model.fill(1.0f);
    draw.model[15] = 1.0f;
    draw.normalMatrix.fill(1.0f);
    draw.normalMatrix[15] = 1.0f;
    draw.baseColour = {0.3f, 0.3f, 0.3f, 1.0f};
    draw.highlightColour = {0.9f, 0.9f, 1.0f};
    draw.parameters = parameters;
    draw.meshIndex = 0;
    draw.material = material;
    frame.draws.push_back(draw);
    return frame;
}

// Inspects one draw the way a backend would read its uniform slots: each
// per-material parameters[0..2] slot, the draw base/highlight colours, and the
// model and normal-matrix base values the backends consume.
static void inspectDraw(const SceneDraw& draw) {
    assert(std::isfinite(draw.parameters[0]));
    assert(std::isfinite(draw.parameters[1]));
    assert(std::isfinite(draw.parameters[2]));
    assert(std::isfinite(draw.baseColour[0]));
    assert(std::isfinite(draw.highlightColour[0]));
    assert(std::isfinite(draw.model[0]));
    assert(std::isfinite(draw.normalMatrix[0]));
}

// Validates only the frame fields this harness sets, so the harness does not
// depend on whether SceneFrame::isValid exists today.
static bool framedOk(const SceneFrame& frame) {
    if (frame.width <= 0 || frame.height <= 0) {
        std::cerr << "frameOk: bad size " << frame.width << "x" << frame.height << "\n";
        return false;
    }
    if (!std::isfinite(frame.viewProjection[0])) {
        std::cerr << "frameOk: bad viewProjection[0]=" << frame.viewProjection[0] << "\n";
        return false;
    }
    if (!std::isfinite(frame.lightDirection[0])) {
        std::cerr << "frameOk: bad lightDirection[0]=" << frame.lightDirection[0] << "\n";
        return false;
    }
    if (!std::isfinite(frame.backgroundColour[0])) {
        std::cerr << "frameOk: bad backgroundColour[0]=" << frame.backgroundColour[0] << "\n";
        return false;
    }
    if (!std::isfinite(frame.highlightColour[0])) {
        std::cerr << "frameOk: bad highlightColour[0]=" << frame.highlightColour[0] << "\n";
        return false;
    }
    if (!std::isfinite(frame.timeSeconds)) {
        std::cerr << "frameOk: bad timeSeconds=" << frame.timeSeconds << "\n";
        return false;
    }
    if (!std::isfinite(frame.drive)) {
        std::cerr << "frameOk: bad drive=" << frame.drive << "\n";
        return false;
    }
    if (!std::isfinite(frame.flutter)) {
        std::cerr << "frameOk: bad flutter=" << frame.flutter << "\n";
        return false;
    }
    if (!std::isfinite(frame.glow)) {
        std::cerr << "frameOk: bad glow=" << frame.glow << "\n";
        return false;
    }
    if (!frame.meshes) {
        std::cerr << "frameOk: null meshes\n";
        return false;
    }
    if (frame.draws.empty()) {
        std::cerr << "frameOk: empty draws\n";
        return false;
    }
    return true;
}

}

int main() {
    using j37::render::SceneMaterial;
    using j37::render::SceneFrame;
    using j37::render::SceneDraw;
    using j37::render::makeFrameForMaterial;

    // solid: parameters unused by the shipped solid shader today.
    {
        auto frame = makeFrameForMaterial(SceneMaterial::solid, {7.0f, 9.0f, 11.0f});
        assert(j37::render::framedOk(frame));
        assert(frame.draws.size() == 1);
        assert(frame.draws[0].material == SceneMaterial::solid);
        j37::render::inspectDraw(frame.draws[0]);
    }

    // tapePack: parameters[0]=uPackInner, parameters[1]=uPackOuter, parameters[2] unused.
    {
        auto frame = makeFrameForMaterial(SceneMaterial::tapePack, {0.08f, 0.20f, 0.0f});
        assert(j37::render::framedOk(frame));
        assert(frame.draws[0].material == SceneMaterial::tapePack);
        assert(std::abs(frame.draws[0].parameters[0] - 0.08f) < 1e-6f);
        assert(std::abs(frame.draws[0].parameters[1] - 0.20f) < 1e-6f);
        // slot 2 is unused by the contract for tapePack.
        j37::render::inspectDraw(frame.draws[0]);
    }

    // ribbon: parameters[0]=uFlutter, parameters[1..2] unused. This is the one
    // material whose Metal path keeps parameters[0] live as uniforms.parameters.x.
    {
        auto frame = makeFrameForMaterial(SceneMaterial::ribbon, {0.15f, 0.0f, 0.0f});
        assert(j37::render::framedOk(frame));
        assert(frame.draws[0].material == SceneMaterial::ribbon);
        assert(std::abs(frame.draws[0].parameters[0] - 0.15f) < 1e-6f);
        j37::render::inspectDraw(frame.draws[0]);
    }

    // controlPanel: parameters[0..2] = uPanelHalf (a vec3 half-extents).
    {
        auto frame =
            makeFrameForMaterial(SceneMaterial::controlPanel, {0.12f, 0.09f, -0.02f});
        assert(j37::render::framedOk(frame));
        assert(frame.draws[0].material == SceneMaterial::controlPanel);
        assert(std::abs(frame.draws[0].parameters[0] - 0.12f) < 1e-6f);
        assert(std::abs(frame.draws[0].parameters[1] - 0.09f) < 1e-6f);
        assert(std::abs(frame.draws[0].parameters[2] - (-0.02f)) < 1e-6f);
        j37::render::inspectDraw(frame.draws[0]);
    }

    // knob: parameters[0]=uArcAngle, parameters[1]=uKeyDown, parameters[2] unused.
    {
        auto frame = makeFrameForMaterial(SceneMaterial::knob, {0.4f, 1.0f, 0.0f});
        assert(j37::render::framedOk(frame));
        assert(frame.draws[0].material == SceneMaterial::knob);
        assert(std::abs(frame.draws[0].parameters[0] - 0.4f) < 1e-6f);
        assert(std::abs(frame.draws[0].parameters[1] - 1.0f) < 1e-6f);
        j37::render::inspectDraw(frame.draws[0]);
    }

    // key: parameters[0]=uArcAngle, parameters[1]=uKeyDown, parameters[2] unused.
    {
        auto frame = makeFrameForMaterial(SceneMaterial::key, {0.0f, 0.0f, 0.0f});
        assert(j37::render::framedOk(frame));
        assert(frame.draws[0].material == SceneMaterial::key);
        assert(std::abs(frame.draws[0].parameters[0] - 0.0f) < 1e-6f);
        assert(std::abs(frame.draws[0].parameters[1] - 0.0f) < 1e-6f);
        j37::render::inspectDraw(frame.draws[0]);
    }

    // glow: parameters[0]=uGlow (this draw's own glow, not the frame glow),
    //        parameters[1..2] unused.
    {
        auto frame = makeFrameForMaterial(SceneMaterial::glow, {0.8f, 0.0f, 0.0f});
        assert(j37::render::framedOk(frame));
        assert(frame.draws[0].material == SceneMaterial::glow);
        assert(std::abs(frame.draws[0].parameters[0] - 0.8f) < 1e-6f);
        j37::render::inspectDraw(frame.draws[0]);
    }

    return 0;
}
