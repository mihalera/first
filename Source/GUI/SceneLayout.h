#pragma once
#include <array>

namespace j37::render::scene {
inline constexpr float supplyCentreX = -0.315f;
inline constexpr float supplyCentreY = 0.235f;
inline constexpr float supplyRadius = 0.220f;
inline constexpr float takeUpCentreX = 0.345f;
inline constexpr float takeUpCentreY = -0.265f;
inline constexpr float takeUpRadius = 0.190f;
inline constexpr float packInnerFraction = 0.30f;
inline constexpr float packEmptyFraction = 0.50f;
inline constexpr float packFullFraction = 0.94f;
inline constexpr float packHalfThickness = 0.100f;
inline constexpr float tapeWidth = 0.040f;
inline constexpr float headCentreX = -0.175f;
inline constexpr float headCentreY = -0.285f;
inline constexpr float headCentreZ = 0.000f;
inline constexpr float headHalfX = 0.075f;
inline constexpr float headHalfY = 0.065f;
inline constexpr float headHalfZ = 0.050f;
inline constexpr float bankHalfWidth = 0.345f;
inline constexpr float bankHalfHeight = 0.085f;
inline constexpr float bankHalfDepth = 0.040f;
inline constexpr float bankCentreZ = -0.200f;
inline constexpr float bankKnobRadius = 0.060f;
inline constexpr float bankKeyHalfX = 0.044f;
inline constexpr float bankKeyHalfY = 0.028f;
inline constexpr float bankKeyHalfZ = 0.026f;
inline constexpr float knobMinimumAngle = -2.443461f;
inline constexpr float knobMaximumAngle = 2.443461f;
inline constexpr float lightX = -0.45f;
inline constexpr float lightY = 0.72f;
inline constexpr float lightZ = 0.53f;
inline constexpr std::array<float, 5> bankPlacementsX{-0.245f, -0.122f, 0.001f, 0.152f, 0.266f};
inline constexpr std::array<bool, 5> bankPlacementsAreKeys{false, false, false, true, true};
inline constexpr int controlBankCount = 5;
inline constexpr int bankKnobCount = 3;
} // namespace j37::render::scene
