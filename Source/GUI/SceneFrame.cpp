#include "SceneFrame.h"

#include <algorithm>
#include <cmath>

namespace j37::render {
namespace {
bool finite(const std::array<float, 16> &values) noexcept {
    return std::all_of(values.begin(), values.end(),
                       [](float value) { return std::isfinite(value); });
}

bool finite(const std::array<float, 9> &values) noexcept {
    return std::all_of(values.begin(), values.end(),
                       [](float value) { return std::isfinite(value); });
}

bool finite(const std::array<float, 4> &values) noexcept {
    return std::all_of(values.begin(), values.end(),
                       [](float value) { return std::isfinite(value); });
}

bool finite(const std::array<float, 3> &values) noexcept {
    return std::all_of(values.begin(), values.end(),
                       [](float value) { return std::isfinite(value); });
}
} // namespace
bool isValid(const SceneFrame &frame) noexcept {
    if (frame.width <= 0 || frame.height <= 0 || frame.meshes.empty() || frame.draws.empty() ||
        !finite(frame.viewProjection) || !finite(frame.lightDirection) ||
        !finite(frame.textureWeights) || !finite(frame.backgroundColour) ||
        !finite(frame.highlightColour) || !std::isfinite(frame.timeSeconds) ||
        !std::isfinite(frame.drive) || !std::isfinite(frame.flutter) || !std::isfinite(frame.glow))
        return false;

    for (const auto &mesh : frame.meshes) {
        if (mesh.vertices.empty() || mesh.indices.empty() || mesh.indices.size() % 3 != 0)
            return false;

        for (const auto &vertex : mesh.vertices)
            if (!finite(vertex.position) || !finite(vertex.normal))
                return false;

        for (const auto index : mesh.indices)
            if (index >= mesh.vertices.size())
                return false;
    }

    for (const auto &draw : frame.draws) {
        if (draw.meshIndex >= frame.meshes.size() || !finite(draw.model) ||
            !finite(draw.normalMatrix) || !finite(draw.baseColour) || !finite(draw.parameters))
            return false;
    }

    return true;
}

} // namespace j37::render
