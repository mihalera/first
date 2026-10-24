#pragma once
#include <array>

/*
  ============================================================================
    The deck, as numbers.

    One file, because everything that draws the machine reads it: the geometry
    builders and the OpenGL path in Source/GUI/TapeScene.cpp, the frame builder
    in Source/GUI/TapeSceneFrame.cpp that the native backends consume, and
    tests/scene_frame, which checks the layout is internally consistent. A
    number changed here is a number changed in all three.

    The values are in the scene's own units, not in pixels: the deck is a box
    about 1.1 wide and 1.0 tall that the camera frames, so the composition is
    the same whatever size the strip is drawn at.

    Right-handed, +x right, +y up, +z toward the eye.
  ============================================================================
*/
namespace j37::render::scene {

//==============================================================================
//  The reels
//
//  The deck runs DIAGONALLY, because that is what a window a quarter as tall as
//  it is wide can hold without wasting the space: the supply reel high on the
//  left, the take-up low on the right, and the head low on the left with the
//  tape coming down onto its face, round its edge and away to the right. Put
//  the reels side by side instead and the box is half empty above and below.
//==============================================================================
inline constexpr float supplyCentreX = -0.315f;
inline constexpr float supplyCentreY = 0.235f;
inline constexpr float supplyRadius = 0.220f;
inline constexpr float takeUpCentreX = 0.345f;
inline constexpr float takeUpCentreY = -0.265f;
inline constexpr float takeUpRadius = 0.190f;

// The pack is stored as fractions of its own reel's radius, so the same two
// numbers describe both reels whatever size each is drawn at.
inline constexpr float packInnerFraction = 0.30f;
inline constexpr float packEmptyFraction = 0.50f; // a supply reel running out
inline constexpr float packFullFraction = 0.94f;  // a take-up reel running over

// The pack stands proud of the reel's flange on both faces, which is what
// makes a wound pack read as a thick roll rather than as a painted disc.
inline constexpr float packHalfThickness = 0.100f;
inline constexpr float tapeWidth = 0.040f;

//==============================================================================
//  The ribbon
//
//  Its six anchors: off the supply pack, down the middle, onto the head's face,
//  round the head's left edge, away along the bottom and onto the take-up pack.
//  Each one's z is the height the tape runs at there, which is what puts it
//  behind the reels' faces and in front of the head's without a single
//  depth-sorting rule. The first and last sit inside their own pack, so the
//  tape emerges from one and disappears into the other.
//
//  Plain floats rather than vectors: this header is included by harnesses that
//  have no GPU library to lean on.
//==============================================================================
inline constexpr std::array<std::array<float, 3>, 6> ribbonAnchorPositions{{
    {{-0.205f, 0.085f, 0.00f}},
    {{-0.075f, -0.145f, 0.05f}},
    {{-0.125f, -0.265f, 0.07f}},
    {{-0.265f, -0.325f, 0.06f}},
    {{0.045f, -0.365f, 0.03f}},
    {{0.245f, -0.165f, 0.00f}},
}};

//==============================================================================
//  The head
//==============================================================================
inline constexpr float headCentreX = -0.175f;
inline constexpr float headCentreY = -0.285f;
inline constexpr float headCentreZ = 0.000f;
inline constexpr float headHalfX = 0.075f;
inline constexpr float headHalfY = 0.065f;
inline constexpr float headHalfZ = 0.050f;

//==============================================================================
//  The control bank - the row of knobs and keys standing in FRONT of the
//  machine, in the foreground strip the camera's downward tilt leaves empty.
//
//  It is the one piece of geometry in the scene that is not part of the
//  transport, and it is here rather than in the 2D panel because the two are
//  the same picture: a knob drawn by a LookAndFeel cannot be turned by the same
//  camera that turns the reels, so a panel of 2D controls beside a 3D machine
//  reads as a screenshot pasted onto a window. These are lit by the same light,
//  occluded by the same depth buffer and turned by the same matrices as
//  everything else.
//
//  Every number is in the scene's own units, and the strip is laid out along x
//  with the bank's own centre at the origin so the five placements below are
//  symmetric about it by construction rather than by arithmetic.
//==============================================================================
inline constexpr float bankHalfWidth = 0.345f;
inline constexpr float bankHalfHeight = 0.085f;
inline constexpr float bankHalfDepth = 0.040f;
inline constexpr float bankCentreZ = -0.200f;

// The face of the bank, and the depth a knob's disc sits at. A knob is built
// round its own origin with its face at +0.020, so its dial stands 0.045 proud
// of the bank's face - which is what makes the rim cast the shadow that reads
// as "this is a knob" rather than "this is a circle".
inline constexpr float bankFaceZ = bankCentreZ + bankHalfDepth;
inline constexpr float bankKnobFaceZ = bankFaceZ + 0.020f;

inline constexpr float bankKnobRadius = 0.060f;

// The keycaps are smaller than they were (0.052 x 0.034). A key sits at the end
// of the bank, one placement away from the third knob's own lit disc, and at
// the old size the two met: the cap's right edge at 0.100 against a glow that
// reaches 0.115 from that knob's centre - a key standing on the arc around the
// knob beside it. Both numbers moved, and the glow moved further (see its own
// scale in drawControlBank), so the gap is real rather than renamed.
inline constexpr float bankKeyHalfX = 0.044f;
inline constexpr float bankKeyHalfY = 0.028f;
inline constexpr float bankKeyHalfZ = 0.026f;

// The sweep a knob's index mark travels, and the sweep its lit arc covers - the
// same two numbers the 2D panel's rotary drawing uses, so a knob in the window
// and a knob in the panel move through the same arc.
inline constexpr float knobMinimumAngle = -2.443461f; // -140 degrees
inline constexpr float knobMaximumAngle = 2.443461f;  // +140 degrees
inline constexpr float knobArcMargin = 0.244346f;     // 14 degrees, where the arc starts

inline constexpr int controlBankCount = 5;
inline constexpr int bankKnobCount = 3;

/** Where one control sits on the bank's face, and which mesh draws it.

    The five are a TABLE rather than five blocks of code, because the arithmetic
    that turns a placement into a matrix is written once, and the only thing
    that differs between a knob and a key is which row of this table it came
    from. It is also what the editor's click handling reads, so a knob cannot be
    drawn in one place and respond to the mouse in another. */
struct ControlPlacement {
    float x;
    bool isKey;
};

using ControlTable = std::array<ControlPlacement, static_cast<std::size_t>(controlBankCount)>;

inline constexpr ControlTable controlPlacements{{
    {-0.245f, false}, // a knob
    {-0.122f, false}, // a knob
    {0.001f, false},  // a knob
    {0.152f, true},   // a key
    {0.266f, true}    // a key
}};

//==============================================================================
//  The camera and the light
//==============================================================================

// How much of the scene the camera has to keep in frame, in the same units as
// everything above and with a little slack for the view angle. The camera
// distance is derived from these and the window's aspect every frame, so the
// same composition survives the deck being 40 px shorter.
inline constexpr float sceneHalfWidth = 0.56f;
inline constexpr float sceneHalfHeight = 0.48f;
inline constexpr float sceneHalfDepth = 0.30f;

inline constexpr float cameraFieldOfViewDegrees = 30.0f;

// How far round and down onto the deck the scene is turned. A little of both is
// the whole of the 3D in the composition: the reels' discs read as ellipses,
// the packs read as cylinders, and the head reads as a block.
inline constexpr float cameraPitchRadians = 0.20f;
inline constexpr float cameraYawRadians = -0.18f;

// The light: over the operator's left shoulder and a little in front, which is
// where a desk lamp on a real machine would be, and is what puts the highlight
// on the upper-left of each reel and leaves the far rim dark.
inline constexpr float lightX = -0.45f;
inline constexpr float lightY = 0.72f;
inline constexpr float lightZ = 0.53f;

} // namespace j37::render::scene
