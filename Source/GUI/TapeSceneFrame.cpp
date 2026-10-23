#include "TapeScene.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>

namespace {
constexpr std::uint32_t reelMeshIndex = 0;
constexpr std::uint32_t tapePackMeshIndex = 1;
constexpr std::uint32_t ribbonMeshIndex = 2;
constexpr std::uint32_t headMeshIndex = 3;
constexpr std::uint32_t bankBodyMeshIndex = 4;
constexpr std::uint32_t bankKnobMeshIndex = 5;
constexpr std::uint32_t bankKeyMeshIndex = 6;
constexpr std::uint32_t bankGlowMeshIndex = 7;

constexpr float bankHalfWidth = 0.345f;
constexpr float bankHalfHeight = 0.085f;
constexpr float bankHalfDepth = 0.040f;
constexpr float bankCentreZ = -0.200f;
constexpr float bankFaceZ = bankCentreZ + bankHalfDepth;
constexpr float bankKnobRadius = 0.060f;
constexpr float bankKeyHalfX = 0.044f;
constexpr float bankKeyHalfY = 0.028f;
constexpr float bankKeyHalfZ = 0.026f;
constexpr float knobMinimumAngle = -2.443461f;
constexpr float knobMaximumAngle = 2.443461f;
constexpr float packInnerFraction = 0.30f;
constexpr float packEmptyFraction = 0.50f;
constexpr float packFullFraction = 0.94f;
constexpr float bankPlacementsX[]{-0.245f, -0.122f, 0.001f, 0.152f, 0.266f};
constexpr bool bankPlacementsAreKeys[]{false, false, false, true, true};

std::array<float, 16> matrix4(const glm::mat4 &matrix) {
    std::array<float, 16> result{};
    std::copy_n(glm::value_ptr(matrix), result.size(), result.begin());
    return result;
}

std::array<float, 9> matrix3(const glm::mat3 &matrix) {
    std::array<float, 9> result{};
    std::copy_n(glm::value_ptr(matrix), result.size(), result.begin());
    return result;
}

std::array<float, 4> colour4(juce::Colour colour) {
    return {colour.getFloatRed(), colour.getFloatGreen(), colour.getFloatBlue(),
            colour.getFloatAlpha()};
}

std::array<float, 3> colour3(juce::Colour colour) {
    return {colour.getFloatRed(), colour.getFloatGreen(), colour.getFloatBlue()};
}

} // namespace
void TapeScene::appendDraw(j37::render::SceneFrame &frame,
                           std::uint32_t meshIndex,
                           j37::render::SceneMaterial material,
                           const glm::mat4 &model,
                           const juce::Colour &baseColour,
                           std::array<float, 3> parameters,
                           const juce::Colour& highlightColour) const {
    j37::render::SceneDraw draw;
    draw.model = matrix4(model);
    const auto normal = glm::transpose(glm::inverse(glm::mat3(model)));
    draw.normalMatrix = matrix3(normal);
    draw.baseColour = colour4(baseColour);
    draw.highlightColour = colour3(highlightColour);
    draw.parameters = parameters;
    draw.meshIndex = meshIndex;
    draw.material = material;
    frame.draws.push_back(draw);
}

j37::render::SceneFrame TapeScene::buildSceneFrame(int width,
                                                   int height,
                                                   double elapsedSeconds,
                                                   const glm::mat4 &viewProjection,
                                                   const glm::mat4 &scene,
                                                   const juce::Colour &background,
                                                   const juce::Colour &body,
                                                   const juce::Colour &highlight,
                                                   const juce::Colour &tape,
                                                   float driveAmount,
                                                   float flutter,
                                                   float reduction,
                                                   float elapsed) const {
    j37::render::SceneFrame frame;
    frame.width = width;
    frame.height = height;
    frame.viewProjection = matrix4(viewProjection);
    frame.lightDirection = {lightX, lightY, lightZ};
    frame.backgroundColour = colour4(background);
    frame.highlightColour = colour4(highlight);
    frame.timeSeconds = static_cast<float>(elapsedSeconds);
    frame.drive = driveAmount;
    frame.flutter = flutter;
    frame.glow = reduction;

    const auto packedWeights = juce::Colour(textureWeights.load(std::memory_order_relaxed));
    frame.textureWeights = {packedWeights.getFloatRed(), packedWeights.getFloatGreen(),
                            packedWeights.getFloatBlue()};

    frame.meshes.reserve(8);
    frame.meshes.push_back(buildReel());
    frame.meshes.push_back(buildTapePack());
    frame.meshes.push_back(buildRibbon());
    frame.meshes.push_back(buildHead());
    const auto bank = buildControlBank();
    frame.meshes.push_back(bank.body);
    frame.meshes.push_back(bank.knob);
    frame.meshes.push_back(bank.key);
    frame.meshes.push_back(bank.knobGlow);

    frame.draws.reserve(2 * 2 + 1 + 1 + 1 + controlBankCount + bankKnobCount);
    const auto bodyLight = body.brighter(0.18f);
    const auto tapeOuterSupply =
        packEmptyFraction + (packFullFraction - packEmptyFraction) * (1.0f - tapeTransfer);
    const auto tapeOuterTakeUp =
        packEmptyFraction + (packFullFraction - packEmptyFraction) * tapeTransfer;

    const auto addReelDraws = [&](float x, float y, float radius, float angle, float outer) {
        const auto model = scene * glm::translate(glm::mat4(1.0f), {x, y, 0.0f}) *
                           glm::rotate(glm::mat4(1.0f), angle, {0.0f, 0.0f, 1.0f}) *
                           glm::scale(glm::mat4(1.0f), {radius, radius, radius});
        appendDraw(frame, reelMeshIndex, SceneMaterial::solid, model, body, {}, highlight);
        appendDraw(frame, tapePackMeshIndex, SceneMaterial::tapePack, model, tape,
                   {packInnerFraction, outer, packHalfThickness}, highlight);
    };
    addReelDraws(supplyCentreX, supplyCentreY, supplyRadius, supplyAngle, tapeOuterSupply);
    addReelDraws(takeUpCentreX, takeUpCentreY, takeUpRadius, takeUpAngle, tapeOuterTakeUp);

    const auto headModel =
        scene * glm::translate(glm::mat4(1.0f), {headCentreX, headCentreY, headCentreZ});
    const juce::Colour headColour(juce::Colour::fromFloatRGBA(
        highlight.getFloatRed() * 0.55f + 0.06f, highlight.getFloatGreen() * 0.55f + 0.06f,
        highlight.getFloatBlue() * 0.55f + 0.07f, 1.0f));
    appendDraw(frame, headMeshIndex, SceneMaterial::solid, headModel, headColour, {}, highlight);
    const auto ribbonColour = juce::Colour::fromFloatRGBA(
        tape.getFloatRed() * 1.35f + 0.02f, tape.getFloatGreen() * 1.35f + 0.02f,
        tape.getFloatBlue() * 1.35f + 0.02f, 1.0f);
    appendDraw(frame, ribbonMeshIndex, SceneMaterial::ribbon, scene, ribbonColour,
               {flutter, elapsed, tapeWidth}, highlight);

    const auto bankModel = scene * glm::translate(glm::mat4(1.0f), {0.0f, 0.0f, bankCentreZ});
    const auto faceModel = bankModel * glm::translate(glm::mat4(1.0f), {0.0f, 0.0f, bankHalfDepth});
    appendDraw(frame, bankBodyMeshIndex, SceneMaterial::controlPanel, faceModel, body,
               {bankHalfWidth, bankHalfHeight, bankHalfDepth}, highlight);

    for (int i = 0; i < controlBankCount; ++i) {
        const auto isKnob = !bankPlacementsAreKeys[i];
        const auto value =
            isKnob ? knobValues[static_cast<std::size_t>(i)].load(std::memory_order_relaxed) : 0.5f;
        const auto angle = knobMinimumAngle + value * (knobMaximumAngle - knobMinimumAngle);
        const auto model =
            isKnob
                ? faceModel * glm::translate(glm::mat4(1.0f), {bankPlacementsX[i], 0.0f, 0.0f}) *
                      glm::rotate(glm::mat4(1.0f), angle, {0.0f, 0.0f, 1.0f}) *
                      glm::scale(glm::mat4(1.0f), {bankKnobRadius, bankKnobRadius, bankKnobRadius})
                : faceModel *
                      glm::translate(glm::mat4(1.0f), {bankPlacementsX[i], 0.0f, bankKeyHalfZ}) *
                      glm::scale(glm::mat4(1.0f), {bankKeyHalfX, bankKeyHalfY, bankKeyHalfZ});
        appendDraw(frame, isKnob ? bankKnobMeshIndex : bankKeyMeshIndex,
                   isKnob ? SceneMaterial::knob : SceneMaterial::key, model, bodyLight,
                   {isKnob ? knobMinimumAngle : 0.0f,
                    i == controlBankCount - 2 ? (bankSpinning ? 1.0f : 0.0f)
                                              : (bankRecording ? 1.0f : 0.0f),
                    0.0f},
                   isKnob ? highlight : bodyLight);
    }

    const auto glowAmount =
        juce::jlimit(0.0f, 1.0f, 0.25f * bankDrive + 0.35f * bankPeak + 0.40f * bankReduction);
    for (int i = 0; i < bankKnobCount; ++i) {
        const auto model =
            faceModel * glm::translate(glm::mat4(1.0f), {bankPlacementsX[i], 0.0f, 0.008f}) *
            glm::scale(glm::mat4(1.0f), {bankKnobRadius * 1.55f, bankKnobRadius * 1.55f, 1.0f});
        appendDraw(frame, bankGlowMeshIndex, SceneMaterial::glow, model, highlight,
                   {glowAmount, elapsed, 0.0f}, highlight);
    }

    return frame;
}
