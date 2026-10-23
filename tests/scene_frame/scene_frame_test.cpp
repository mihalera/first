#include "../../Source/GUI/SceneFrame.h"
#include "../../Source/GUI/SceneLayout.h"

#include <cassert>
#include <limits>
#include <memory>

int main() {
    using namespace j37::render;

    static_assert(scene::controlBankCount == static_cast<int>(scene::bankPlacementsX.size()));
    static_assert(scene::bankKnobCount == 3);
    assert(scene::packInnerFraction < scene::packEmptyFraction);
    assert(scene::packEmptyFraction < scene::packFullFraction);

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
    auto mesh = std::make_shared<std::vector<SceneMesh>>();
    mesh->push_back({{{{0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
                      {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
                      {{0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}},
                     {0, 1, 2}});
    valid.meshes = mesh;
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
    auto badIndices = std::make_shared<std::vector<SceneMesh>>(*valid.meshes);
    badIndices->at(0).indices[2] = 99;
    invalid.meshes = badIndices;
    assert(!isValid(invalid));

    invalid = valid;
    invalid.draws[0].meshIndex = 1;
    assert(!isValid(invalid));

    invalid = valid;
    auto badVertex = std::make_shared<std::vector<SceneMesh>>(*valid.meshes);
    badVertex->at(0).vertices[0].normal[1] = std::numeric_limits<float>::quiet_NaN();
    invalid.meshes = badVertex;
    assert(!isValid(invalid));

    invalid = valid;
    invalid.draws[0].model[3] = std::numeric_limits<float>::infinity();
    assert(!isValid(invalid));

    invalid = valid;
    invalid.draws[0].baseColour[0] = std::numeric_limits<float>::quiet_NaN();
    assert(!isValid(invalid));

    invalid = valid;
    auto incompleteTriangle = std::make_shared<std::vector<SceneMesh>>(*valid.meshes);
    incompleteTriangle->at(0).indices.pop_back();
    invalid.meshes = incompleteTriangle;
    assert(!isValid(invalid));

    return 0;
}
