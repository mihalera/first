#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace j37::render {

enum class SceneMaterial : std::uint32_t {
    solid,
    tapePack,
    ribbon,
    controlPanel,
    knob,
    key,
    glow
};

/** Backend-neutral vertex representation shared by the scene and GPU adapters. */
struct SceneVertex {
    std::array<float, 4> position{};
    std::array<float, 3> normal{};
};

/** CPU-side indexed mesh. Backend upload code owns its API-specific buffers. */
struct SceneMesh {
    std::vector<SceneVertex> vertices;
    std::vector<std::uint32_t> indices;
};

/** Per-draw constants in column-major form, matching GLM's default matrix layout. */
struct SceneDraw {
    std::array<float, 16> model{};
    std::array<float, 9> normalMatrix{};
    std::array<float, 4> baseColour{};
    std::array<float, 3> parameters{};
    std::uint32_t meshIndex = 0;
    SceneMaterial material = SceneMaterial::solid;
};

/** One complete display-only scene snapshot. No JUCE objects or GL handles cross
    into native backend code; adapters receive immutable frame data and upload it
    using their own resource lifecycle. */
struct SceneFrame {
    int width = 1;
    int height = 1;
    std::array<float, 16> viewProjection{};
    std::array<float, 3> lightDirection{};
    std::array<float, 3> textureWeights{};
    std::array<float, 4> backgroundColour{};
    std::array<float, 4> highlightColour{};
    float timeSeconds = 0.0f;
    float drive = 0.0f;
    float flutter = 0.0f;
    float glow = 0.0f;
    std::vector<SceneMesh> meshes;
    std::vector<SceneDraw> draws;
};

/** Validate structural and numeric invariants before a frame crosses the backend boundary. */
bool isValid(const SceneFrame& frame) noexcept;

} // namespace j37::render
