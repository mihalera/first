#include "../../Source/GUI/SceneFrame.h"

#include <cassert>
#include <limits>

int main() {
    using namespace j37::render;

    SceneFrame valid;
    valid.width = 64;
    valid.height = 120;
    valid.viewProjection[0] = 1.0f;
    valid.viewProjection[5] = 1.0f;
    valid.viewProjection[10] = 1.0f;
    valid.viewProjection[15] = 1.0f;
    valid.lightDirection = {0.0f, 0.0f, 1.0f};
    valid.backgroundColour = {0.1f, 0.1f, 0.1f, 1.0f};
    valid.highlightColour = {1.0f, 0.5f, 0.0f, 1.0f};
    valid.meshes.push_back({{{{0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
                             {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
                             {{0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}},
                            {0, 1, 2}});
    SceneDraw draw;
    draw.model[0] = draw.model[5] = draw.model[10] = draw.model[15] = 1.0f;
    draw.normalMatrix[0] = draw.normalMatrix[4] = draw.normalMatrix[8] = 1.0f;
    draw.baseColour = {1.0f, 1.0f, 1.0f, 1.0f};
    valid.draws.push_back(draw);
    assert(isValid(valid));

    auto invalid = valid;
    invalid.width = 0;
    assert(!isValid(invalid));

    invalid = valid;
    invalid.meshes[0].indices[2] = 99;
    assert(!isValid(invalid));

    invalid = valid;
    invalid.draws[0].meshIndex = 1;
    assert(!isValid(invalid));

    invalid = valid;
    invalid.meshes[0].vertices[0].normal[1] = std::numeric_limits<float>::quiet_NaN();
    assert(!isValid(invalid));

    invalid = valid;
    invalid.draws[0].model[3] = std::numeric_limits<float>::infinity();
    assert(!isValid(invalid));

    invalid = valid;
    invalid.draws[0].baseColour[0] = std::numeric_limits<float>::quiet_NaN();
    assert(!isValid(invalid));

    invalid = valid;
    invalid.meshes[0].indices.pop_back();
    assert(!isValid(invalid));

    return 0;
}
