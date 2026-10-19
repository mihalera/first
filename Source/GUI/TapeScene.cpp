/*
  ==============================================================================
    TapeScene - the deck's transport, rendered as geometry and lit on the GPU.
  ==============================================================================
*/

#include "TapeScene.h"
#include "TapeSceneShaders.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <thread>

// JUCE 9 keeps every GL command and every GL enum in juce::gl, and documents
// `using namespace ::juce::gl;` as the supported way to bring them into scope -
// the GL typedefs (GLuint, GLint, GLsizeiptr) are already global. Doing it in
// the .cpp rather than the header is the module's own advice.
using namespace juce::gl;

// The deck's shader sources, which were the middle of this file's
// scene namespace until they moved to a header of their own.
using j37scene::fragmentShaderSource;
using j37scene::vertexShaderSource;


namespace
{
    //==========================================================================
    //  The scene
    //
    //  Measured in the window's own units, not in pixels. The window is the deck's
    //  top-right block - the strip resized() keeps clear of every control, which
    //  is 64 x 60 whatever the panel is doing - and a scene sized in pixels would
    //  be a different picture at every panel size. The camera below frames this
    //  box instead, so the composition is the same at all of them.
    //
    //  It runs DIAGONALLY, because that is what a window a quarter as tall as it
    //  is wide can hold without wasting the space: the supply reel high on the
    //  left, the take-up low on the right, and the head low on the left with the
    //  tape coming down onto its face, round its edge and away to the right. Put
    //  the reels side by side instead and the box is half empty above and below.
    //
    //  Right-handed, +x right, +y up, +z toward the eye.
    //==========================================================================
    constexpr float supplyCentreX = -0.315f;
    constexpr float supplyCentreY =  0.235f;
    constexpr float supplyRadius  =  0.220f;

    constexpr float takeUpCentreX =  0.345f;
    constexpr float takeUpCentreY = -0.265f;
    constexpr float takeUpRadius  =  0.190f;

    // The pack is stored as fractions of its own reel's radius, so the same two
    // numbers describe both reels whatever size each is drawn at.
    constexpr float packInnerFraction = 0.30f;
    constexpr float packEmptyFraction = 0.50f;   // a supply reel running out
    constexpr float packFullFraction = 0.94f;    // a take-up reel running over

    // The pack stands proud of the reel's flange on both faces, which is what
    // makes a wound pack read as a thick roll rather than as a painted disc.
    constexpr float packHalfThickness = 0.100f;

    constexpr float tapeWidth = 0.040f;

    // The ribbon's six anchors: off the supply pack, down the middle, onto the
    // head's face, round the head's left edge, away along the bottom and onto the
    // take-up pack. Each one's z is the height the tape runs at there, which is
    // what puts it behind the reels' faces and in front of the head's without a
    // single depth-sorting rule. The first and last sit inside their own pack, so
    // the tape emerges from one and disappears into the other.
    const glm::vec3 ribbonAnchors[] = {
        { -0.205f,  0.085f, 0.00f },
        { -0.075f, -0.145f, 0.05f },
        { -0.125f, -0.265f, 0.07f },
        { -0.265f, -0.325f, 0.06f },
        {  0.045f, -0.365f, 0.03f },
        {  0.245f, -0.165f, 0.00f }
    };

    constexpr float headCentreX = -0.175f;
    constexpr float headCentreY = -0.285f;
    constexpr float headCentreZ =  0.000f;
    constexpr float headHalfX = 0.075f;
    constexpr float headHalfY = 0.065f;
    constexpr float headHalfZ = 0.050f;

    //==========================================================================
    //  The control bank - the row of knobs and keys standing in FRONT of the
    //  machine, in the foreground strip the camera's downward tilt leaves empty.
    //
    //  It is the one piece of geometry in the scene that is not part of the
    //  transport, and it is here rather than in the 2D panel because the two are
    //  the same picture: a knob drawn by a LookAndFeel cannot be turned by the
    //  same camera that turns the reels, so a panel of 2D controls beside a 3D
    //  machine reads as a screenshot pasted onto a window. These are lit by the
    //  same light, occluded by the same depth buffer and turned by the same
    //  matrices as everything else.
    //
    //  Every number is in the scene's own units, and the strip is laid out along
    //  x with the bank's own centre at the origin so the five placements below
    //  are symmetric about it by construction rather than by arithmetic.
    //==========================================================================
    constexpr float bankHalfWidth  = 0.345f;
    constexpr float bankHalfHeight = 0.085f;
    constexpr float bankHalfDepth  = 0.040f;
    constexpr float bankCentreZ    = -0.200f;

    // The face of the bank, and the depth a knob's disc sits at. A knob is built
    // round its own origin with its face at +0.020, so its dial stands 0.045
    // proud of the bank's face - which is what makes the rim cast the shadow that
    // reads as "this is a knob" rather than "this is a circle".
    constexpr float bankFaceZ     = bankCentreZ + bankHalfDepth;
    constexpr float bankKnobFaceZ = bankFaceZ + 0.020f;

    constexpr float bankKnobRadius = 0.060f;
    // The keycaps are smaller than they were (0.052 x 0.034). A key sits at the
    // end of the bank, one placement away from the third knob's own lit disc, and
    // at the old size the two met: the cap's right edge at 0.100 against a glow
    // that reaches 0.115 from that knob's centre - a key standing on the arc
    // around the knob beside it. Both numbers moved, and the glow moved further
    // (see its own scale below), so the gap is real rather than renamed.
    constexpr float bankKeyHalfX   = 0.044f;
    constexpr float bankKeyHalfY   = 0.028f;
    constexpr float bankKeyHalfZ   = 0.026f;

    // The sweep a knob's index mark travels, and the sweep its lit arc covers -
    // the same two numbers the 2D panel's rotary drawing uses, so a knob in the
    // window and a knob in the panel move through the same arc.
    constexpr float knobMinimumAngle = -2.443461f;   // -140 degrees
    constexpr float knobMaximumAngle =  2.443461f;   // +140 degrees
    constexpr float knobArcMargin    =  0.244346f;   // 14 degrees, where the arc starts

    // How much of the scene the camera has to keep in frame, in the same units
    // as everything above and with a little slack for the view angle. The camera
    // distance is derived from these and the window's aspect every frame, so the
    // same composition survives the deck being 40 px shorter.
    constexpr float sceneHalfWidth = 0.56f;
    constexpr float sceneHalfHeight = 0.48f;
    constexpr float sceneHalfDepth = 0.30f;

    constexpr float cameraFieldOfViewDegrees = 30.0f;

    // How far round and down onto the deck the scene is turned. A little of both
    // is the whole of the 3D in the composition: the reels' discs read as
    // ellipses, the packs read as cylinders, and the head reads as a block.
    constexpr float cameraPitchRadians = 0.20f;
    constexpr float cameraYawRadians   = -0.18f;

    // The light: over the operator's left shoulder and a little in front, which
    // is where a desk lamp on a real machine would be, and is what puts the
    // highlight on the upper-left of each reel and leaves the far rim dark.
    constexpr float lightX = -0.45f;
    constexpr float lightY =  0.72f;
    constexpr float lightZ =  0.53f;

    //==========================================================================
    //  Vertex layout, shared by every mesh: four floats of position - the
    //  fourth being the payload the vertex shader branches on - and three of
    //  normal. Twenty-eight bytes, which is what drawMesh() strides with.
    //==========================================================================
    constexpr GLsizei vertexStride = static_cast<GLsizei> (7 * sizeof (float));
    constexpr GLsizei normalOffset = static_cast<GLsizei> (4 * sizeof (float));

    enum MeshMode
    {
        solidMode = 0,
        packMode = 1,
        ribbonMode = 2,
        panelMode = 3,
        knobMode = 4,
        keyMode = 5,
        glowMode = 6
    };

    // The fragment shader's mode ranges are bands (uMode < 0.5, < 3.5, < 4.5,
    // < 5.5, else), so the bank's own modes sit above the transport's three and
    // below the caption plate, which is the last branch of them all.

    void addVertex (std::vector<float>& vertices,
                    const glm::vec3& position,
                    const glm::vec3& normal,
                    float payload)
    {
        vertices.insert (vertices.end(),
                         { position.x, position.y, position.z, payload,
                           normal.x, normal.y, normal.z });
    }

    juce::uint32 addVertexReturningIndex (std::vector<float>& vertices,
                                          const glm::vec3& position,
                                          const glm::vec3& normal,
                                          float payload = 0.0f)
    {
        const auto index = static_cast<juce::uint32> (vertices.size() / 7);
        addVertex (vertices, position, normal, payload);
        return index;
    }

    void addTriangle (std::vector<juce::uint32>& indices,
                      juce::uint32 a, juce::uint32 b, juce::uint32 c)
    {
        indices.push_back (a);
        indices.push_back (b);
        indices.push_back (c);
    }

    void addQuad (std::vector<juce::uint32>& indices,
                  juce::uint32 a, juce::uint32 b, juce::uint32 c, juce::uint32 d)
    {
        addTriangle (indices, a, b, c);
        addTriangle (indices, a, c, d);
    }

    /** A closed disc: a flat top, a flat bottom and a wall round the rim. The
        rim reuses the top and bottom rings' positions with a radial normal,
        which is why the edge reads as an edge rather than as a disc with a hard
        cut round it. Used twice per reel - once for the flange and once for the
        boss the tape is clamped to.

        `centreZ` is the MID-PLANE of the disc, which is what lets the control
        bank's knobs be built round their own origin at z = 0 and placed with a
        translation afterwards. It was implicit for the reel (which is modelled
        at z = 0 anyway) and is explicit here because the knurl below needs a
        band at a depth of its own. */
    void addDisc (std::vector<float>& vertices,
                  std::vector<juce::uint32>& indices,
                  const glm::vec3& centre,
                  float radius,
                  float halfHeight,
                  int segments,
                  float payload = 0.0f)
    {
        const auto top = centre.z + halfHeight;
        const auto bottom = centre.z - halfHeight;

        const auto centreTop = addVertexReturningIndex (vertices, { centre.x, centre.y, top },
                                                       { 0.0f, 0.0f, 1.0f }, payload);
        const auto centreBottom = addVertexReturningIndex (vertices, { centre.x, centre.y, bottom },
                                                          { 0.0f, 0.0f, -1.0f }, payload);

        std::vector<juce::uint32> topRing (static_cast<size_t> (segments));
        std::vector<juce::uint32> bottomRing (static_cast<size_t> (segments));

        for (int i = 0; i < segments; ++i)
        {
            const auto angle = juce::MathConstants<float>::twoPi
                                 * static_cast<float> (i) / static_cast<float> (segments);
            const glm::vec3 outward { std::cos (angle), std::sin (angle), 0.0f };

            topRing[static_cast<size_t> (i)] =
                addVertexReturningIndex (vertices,
                                         { centre.x + outward.x * radius,
                                           centre.y + outward.y * radius, top },
                                         { 0.0f, 0.0f, 1.0f }, payload);

            bottomRing[static_cast<size_t> (i)] =
                addVertexReturningIndex (vertices,
                                         { centre.x + outward.x * radius,
                                           centre.y + outward.y * radius, bottom },
                                         { 0.0f, 0.0f, -1.0f }, payload);
        }

        for (int i = 0; i < segments; ++i)
        {
            const auto next = static_cast<size_t> ((i + 1) % segments);
            const auto here = static_cast<size_t> (i);

            addTriangle (indices, centreTop, topRing[here], topRing[next]);
            addTriangle (indices, centreBottom, bottomRing[next], bottomRing[here]);
        }

        // The wall, round the rim. Its own vertices reuse the two rings'
        // positions with a radial normal, which is what makes the edge read as
        // an edge rather than as a disc with a hard cut round it.
        for (int i = 0; i < segments; ++i)
        {
            const auto angle = juce::MathConstants<float>::twoPi
                                 * static_cast<float> (i) / static_cast<float> (segments);
            const glm::vec3 outward { std::cos (angle), std::sin (angle), 0.0f };
            const glm::vec3 rim { centre.x + outward.x * radius,
                                  centre.y + outward.y * radius,
                                  0.0f };

            const auto wallTop = addVertexReturningIndex (vertices, rim + glm::vec3 (0.0f, 0.0f, top), outward, payload);
            const auto wallBottom = addVertexReturningIndex (vertices, rim + glm::vec3 (0.0f, 0.0f, bottom), outward, payload);

            const auto next = static_cast<size_t> ((i + 1) % segments);
            addQuad (indices, wallTop, wallBottom, bottomRing[next], topRing[next]);
        }
    }

    /** A box as six quads, optionally spun about z. The only place in the scene
        where a corner is a corner rather than a point on a circle: the head, and
        the spokes that make a reel's rotation readable at twenty pixels across. */
    void addBox (std::vector<float>& vertices,
                 std::vector<juce::uint32>& indices,
                 const glm::vec3& centre,
                 const glm::vec3& half,
                 float rotationAboutZ = 0.0f)
    {
        const auto cosR = std::cos (rotationAboutZ);
        const auto sinR = std::sin (rotationAboutZ);

        const auto corner = [&] (float sx, float sy, float sz) -> glm::vec3
        {
            const auto x = sx * half.x;
            const auto y = sy * half.y;
            return { centre.x + x * cosR - y * sinR,
                     centre.y + x * sinR + y * cosR,
                     centre.z + sz * half.z };
        };

        const glm::vec3 faceNormals[6] = {
            {  0.0f,  0.0f,  1.0f }, {  0.0f,  0.0f, -1.0f },
            {  1.0f,  0.0f,  0.0f }, { -1.0f,  0.0f,  0.0f },
            {  0.0f,  1.0f,  0.0f }, {  0.0f, -1.0f,  0.0f }
        };

        // Each face's four corners, in the same winding, with the normal
        // rotated into the box's own frame.
        const juce::uint32 faceCorners[6][4] = {
            { 0, 1, 3, 2 },   // +z
            { 4, 6, 7, 5 },   // -z
            { 1, 5, 7, 3 },   // +x
            { 2, 0, 4, 6 },   // -x
            { 3, 7, 5, 1 },   // +y
            { 2, 6, 4, 0 }    // -y
        };

        const glm::vec3 points[8] = {
            corner (-1.0f, -1.0f, -1.0f), corner ( 1.0f, -1.0f, -1.0f),
            corner (-1.0f,  1.0f, -1.0f), corner ( 1.0f,  1.0f, -1.0f),
            corner (-1.0f, -1.0f,  1.0f), corner ( 1.0f, -1.0f,  1.0f),
            corner (-1.0f,  1.0f,  1.0f), corner ( 1.0f,  1.0f,  1.0f)
        };

        for (int face = 0; face < 6; ++face)
        {
            const auto& n = faceNormals[face];
            const auto rotatedNormal = glm::vec3 (n.x * cosR - n.y * sinR,
                                                   n.x * sinR + n.y * cosR,
                                                   n.z);

            juce::uint32 ids[4];

            for (int cornerIndex = 0; cornerIndex < 4; ++cornerIndex)
                ids[cornerIndex] = addVertexReturningIndex (vertices,
                                                            points[faceCorners[face][cornerIndex]],
                                                            rotatedNormal);

            addQuad (indices, ids[0], ids[1], ids[2], ids[3]);
        }
    }

    /** One of the reel's three spokes: a box spun out to its own angle, running
        from inside the boss to just short of the flange's rim, and sitting clear
        above the pack so the one part a user counts turns on is never buried in
        tape. */
    void addSpoke (std::vector<float>& vertices,
                   std::vector<juce::uint32>& indices,
                   float angle)
    {
        addBox (vertices, indices,
                { 0.58f * std::cos (angle), 0.58f * std::sin (angle), 0.145f },
                { 0.40f, 0.055f, 0.022f },
                angle);
    }

    /** The knurled grip round a knob's rim: one flat-sided prism per segment,
        each carrying its own outward normal.

        A smooth cylinder lights as one continuous band of highlight and reads as
        plastic. Real knurling is a ring of tiny flats, and the reason to model it
        rather than to texture it is that the flats catch the light in STRIPES:
        the highlight jumps from facet to facet as the knob turns, which is the
        cue that says a knob is moving even when its index mark is a few pixels
        long.

        The prisms span the knob's barrel only - the face above and the back below
        are the flat discs from addDisc - so the knurl is a band round the side
        rather than a replacement for the whole cylinder. */
    void addKnurl (std::vector<float>& vertices,
                   std::vector<juce::uint32>& indices,
                   float radius,
                   float bottomZ,
                   float topZ,
                   int segments)
    {
        const auto step = juce::MathConstants<float>::twoPi / static_cast<float> (segments);

        for (int i = 0; i < segments; ++i)
        {
            const auto angle = step * static_cast<float> (i);
            const glm::vec2 here { std::cos (angle), std::sin (angle) };
            const glm::vec2 next { std::cos (angle + step), std::sin (angle + step) };

            // The facet's own outward direction, re-normalised: a chord of the
            // circle is shorter than its arc, and a normal left at the chord's
            // length would dim every facet by its own cosine.
            const glm::vec2 facet { here.x + next.x, here.y + next.y };
            const auto facetLength = std::max (glm::length (facet), 0.0001f);
            const glm::vec3 outward { facet.x / facetLength, facet.y / facetLength, 0.0f };

            const auto corner = [&] (const glm::vec2& direction, float z)
            {
                return glm::vec3 (direction.x * radius, direction.y * radius, z);
            };

            const auto a = addVertexReturningIndex (vertices, corner (here, topZ), outward);
            const auto b = addVertexReturningIndex (vertices, corner (next, topZ), outward);
            const auto c = addVertexReturningIndex (vertices, corner (next, bottomZ), outward);
            const auto d = addVertexReturningIndex (vertices, corner (here, bottomZ), outward);

            addQuad (indices, a, b, c, d);
        }
    }

    glm::vec3 catmullRom (const glm::vec3& p0, const glm::vec3& p1,
                          const glm::vec3& p2, const glm::vec3& p3,
                          float t)
    {
        const auto t2 = t * t;
        const auto t3 = t2 * t;

        return 0.5f * ((2.0f * p1)
                    + (-p0 + p2) * t
                    + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
                    + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
    }

    /** Where one control sits on the bank's face, and which mesh draws it.

        The five are a TABLE rather than five blocks of code, for the same reason
        the deck's rows are: the arithmetic that turns a placement into a matrix
        is written once, and the only thing that differs between a knob and a key
        is which row of this table it came from. It is also what the editor's
        click handling reads, so a knob cannot be drawn in one place and respond
        to the mouse in another. */
    struct BankPlacement
    {
        float x;
        bool isKey;
    };

    constexpr BankPlacement bankPlacements[] = {
        { -0.245f, false },   // a knob
        { -0.122f, false },   // a knob
        {  0.001f, false },   // a knob
        {  0.152f, true  },   // a key
        {  0.266f, true  }    // a key
    };

    constexpr int bankPlacementCount = static_cast<int> (std::size (bankPlacements));

    /** Pastes the precision literal through, and routes the source through
        JUCE's own v3 translators, which prepend `#version 150` and rewrite
        attribute/varying/gl_FragColor when - and only when - the context is a
        3.2 core profile. Below that they hand the text back untouched and the
        driver reads it as GLSL 1.10. */
    juce::String prepare (const char* source, bool isVertexShader)
    {
        const auto translated = isVertexShader
                              ? juce::OpenGLHelpers::translateVertexShaderToV3 (source)
                              : juce::OpenGLHelpers::translateFragmentShaderToV3 (source);

        return translated.replace ("TAPE_SCENE_HIGHP", TAPE_SCENE_HIGHP);
    }
}

//==============================================================================
TapeScene::TapeScene()
{
    nativeRenderer = j37::render::createNativeRenderer (j37::render::effectiveBackend());
    setOpaque (true);
    setInterceptsMouseClicks (false, false);
    setWantsKeyboardFocus (false);

    // A custom renderer, not a component painter: the editor's own context draws
    // the 2D panel, and this one draws the transport into this component only.
    openGLContext.setComponentPaintingEnabled (false);
    openGLContext.setContinuousRepainting (false);
}

TapeScene::~TapeScene()
{
    // Detaching is what tears the context down, and it is what gets
    // openGLContextClosing() called on the GL thread - which is the only thread
    // allowed to delete a buffer or a program.
    openGLContext.detach();
    if (nativeRenderer != nullptr)
        nativeRenderer->shutdown();
}

void TapeScene::setAudioState (float newOutputLevel,
                               float newDrive,
                               float newGainReduction,
                               float newWowFlutter,
                               float newTransportSpeed,
                               float newOutputPeak) noexcept
{
    outputLevel.store (juce::jlimit (0.0f, 1.0f, newOutputLevel), std::memory_order_relaxed);
    outputPeak.store (juce::jlimit (0.0f, 1.0f, newOutputPeak), std::memory_order_relaxed);
    drive.store (juce::jlimit (0.0f, 1.0f, newDrive), std::memory_order_relaxed);
    gainReduction.store (juce::jlimit (0.0f, 1.0f, newGainReduction), std::memory_order_relaxed);
    wowFlutter.store (juce::jlimit (-1.0f, 1.0f, newWowFlutter), std::memory_order_relaxed);
    transportSpeed.store (juce::jlimit (0.0f, 1.0f, newTransportSpeed), std::memory_order_relaxed);

    // The repaint is the whole animation loop, and it is the editor's 30 Hz
    // timer asking for it rather than a timer of this component's own: the panel
    // already has one, and a second one would be a second thing to stop when the
    // editor goes away. The GPU path also needs this invalidation because its
    // context is intentionally not continuously repainting.
    repaint();

    // ... and on the accelerated path that repaint() is NOT enough. This
    // component's context is attached with component painting DISABLED - the
    // panel's own context draws the 2D editor, this one draws the transport -
    // so there is no JUCE paint to invalidate here at all, and the context is
    // not continuously repainting either. JUCE documents that second setting
    // exactly: "if false, then after each render callback, it will wait for
    // another call to triggerRepaint() before rendering again". Nothing in this
    // class ever made that call, so the reels, the tape ribbon and the bank of
    // knobs were rendered ONCE, when the context came up, and then stood still
    // for as long as the window was open. This is the call that turns the five
    // floats above into motion - one frame per editor tick, on the render
    // thread, coalesced by JUCE if a frame is already queued.
    if (openGLContext.isAttached())
        openGLContext.triggerRepaint();
}

void TapeScene::setControlValues (const float* values, int count) noexcept
{
    // A caller that hands over fewer values than the bank holds leaves the rest
    // at the panel's rest position rather than retaining a stale angle.
    const auto usable = values != nullptr ? juce::jlimit (0, bankKnobCount, count) : 0;

    for (int i = 0; i < bankKnobCount; ++i)
        knobValues[static_cast<std::size_t> (i)].store (i < usable ? values[i] : 0.5f,
                                                        std::memory_order_relaxed);

    repaint();
}

void TapeScene::setPalette (juce::Colour background,
                            juce::Colour body,
                            juce::Colour highlight,
                            juce::Colour tape) noexcept
{
    // Colours cross the same thread boundary as the floats, so they travel as
    // the packed form they already are rather than as a Colour - which holds a
    // pointer into JUCE's named-colour table and must not be copied between
    // threads.
    backgroundColour.store (background.getARGB(), std::memory_order_relaxed);
    bodyColour.store (body.getARGB(), std::memory_order_relaxed);
    highlightColour.store (highlight.getARGB(), std::memory_order_relaxed);
    tapeColour.store (tape.getARGB(), std::memory_order_relaxed);
    repaint();
}

j37::render::NativeRenderer::Config TapeScene::nativeRendererConfig() const
{
    j37::render::NativeRenderer::Config config;
    config.width = juce::jmax (1, getWidth());
    config.height = juce::jmax (1, getHeight());

    // Native surfaces belong to this component, not to the editor peer. The
    // renderer therefore receives the component's position in its peer's
    // coordinate system, which is what child HWNDs and Metal layers expect.
    if (auto* peer = getPeer())
    {
        // The argument is spelled out: JUCE declares localPointToGlobal for both
        // Point<int> and Point<float>, and a brace-initialised { 0, 0 } is
        // convertible to each, so every compiler rejects it as ambiguous. The
        // Config fields are ints, so the int overload is the one meant here.
        const auto globalOrigin = localPointToGlobal (juce::Point<int> { 0, 0 });
        const auto peerOrigin = peer->getComponent().localPointToGlobal (juce::Point<int> { 0, 0 });
        config.positionX = globalOrigin.x - peerOrigin.x;
        config.positionY = globalOrigin.y - peerOrigin.y;
    }

    return config;
}

void TapeScene::setSceneEnabled (bool shouldBeEnabled)
{
    // OpenGL is automatic and never a user-facing mode. Non-OpenGL backends use
    // the software scene and must not create an OpenGL context.
    if (! j37::render::usesOpenGL())
    {
        sceneEnabled = false;
        openGLContext.detach();
        programLinked.store (0);
        return;
    }

    if (sceneEnabled == shouldBeEnabled)
        return;

    sceneEnabled = shouldBeEnabled;

    if (sceneEnabled)
    {
        // A component has no context until the host has given it a native peer,
        // and the editor may not have done that yet, so the first try is not a
        // verdict. Thirty is about a second of the editor's 30 Hz timer, which
        // is as long as the editor's own context gives itself.
        attachAttemptsLeft = 30;
        serviceContextAttachment();
    }
    else
    {
        openGLContext.detach();
    }
}

void TapeScene::serviceContextAttachment()
{
    if (! j37::render::usesOpenGL() || ! sceneEnabled)
        return;

    if (openGLContext.isAttached())
    {
        attachAttemptsLeft = 0;
        return;
    }

    if (attachAttemptsLeft <= 0)
        return;

    --attachAttemptsLeft;
    openGLContext.attachTo (*this);
}

void TapeScene::paint (juce::Graphics& g)
{
    // Native command adapters currently do not draw the scene mesh, so their
    // configured backends use the complete JUCE CPU scene.

    const auto width = static_cast<float> (getWidth());
    const auto height = static_cast<float> (getHeight());
    if (width <= 0.0f || height <= 0.0f)
        return;

    const auto background = juce::Colour (backgroundColour.load (std::memory_order_relaxed));
    const auto body = juce::Colour (bodyColour.load (std::memory_order_relaxed));
    const auto highlight = juce::Colour (highlightColour.load (std::memory_order_relaxed));
    const auto tape = juce::Colour (tapeColour.load (std::memory_order_relaxed));
    const auto speed = transportSpeed.load (std::memory_order_relaxed);
    const auto level = outputLevel.load (std::memory_order_relaxed);
    const auto peak = outputPeak.load (std::memory_order_relaxed);
    const auto flutter = std::abs (wowFlutter.load (std::memory_order_relaxed));

    // CPU paint runs on JUCE's message thread. Do not read supplyAngle here:
    // that value belongs to the OpenGL thread. A wall-clock phase gives the CPU
    // backend the same visible motion without introducing a cross-thread race.
    const auto angle = static_cast<float> (juce::Time::getMillisecondCounterHiRes()
                                           * 0.0026 * speed);

    g.fillAll (background);

    const auto centre = juce::Point<float> (width * 0.5f, height * 0.50f);
    const auto reelRadius = juce::jmin (width, height) * 0.25f;
    const auto hubRadius = reelRadius * 0.28f;
    const auto alpha = 0.22f + 0.68f * juce::jlimit (0.0f, 1.0f, speed);

    g.setColour (body.withAlpha (0.90f));
    g.fillEllipse (centre.x - reelRadius, centre.y - reelRadius,
                   reelRadius * 2.0f, reelRadius * 2.0f);
    g.setColour (highlight.withAlpha (0.24f + 0.24f * level));
    g.drawEllipse (centre.x - reelRadius, centre.y - reelRadius,
                   reelRadius * 2.0f, reelRadius * 2.0f, 1.4f);
    g.setColour (tape.withAlpha (0.65f));
    g.fillEllipse (centre.x - reelRadius * (0.62f - 0.10f * level),
                   centre.y - reelRadius * (0.62f - 0.10f * level),
                   reelRadius * (1.24f - 0.20f * level),
                   reelRadius * (1.24f - 0.20f * level));

    const auto drawSpokes = [&] (juce::Point<float> reelCentre, float radius,
                                  float hub, float rotation, float thickness)
    {
        for (int spoke = 0; spoke < 6; ++spoke)
        {
            const auto spokeAngle = rotation + static_cast<float> (spoke)
                                               * juce::MathConstants<float>::twoPi / 6.0f;
            const auto inner = reelCentre + juce::Point<float> (std::cos (spokeAngle),
                                                                 std::sin (spokeAngle)) * hub;
            const auto outer = reelCentre + juce::Point<float> (std::cos (spokeAngle),
                                                                 std::sin (spokeAngle)) * radius * 0.88f;
            g.setColour (highlight.withAlpha (alpha));
            g.drawLine (inner.x, inner.y, outer.x, outer.y, thickness);
        }
    };
    drawSpokes (centre, reelRadius, hubRadius, angle, 1.5f);

    g.setColour (highlight.withAlpha (0.35f + 0.35f * peak));
    g.fillEllipse (centre.x - hubRadius, centre.y - hubRadius,
                   hubRadius * 2.0f, hubRadius * 2.0f);
    g.setColour (background.withAlpha (0.90f));
    g.fillEllipse (centre.x - hubRadius * 0.42f, centre.y - hubRadius * 0.42f,
                   hubRadius * 0.84f, hubRadius * 0.84f);

    // The flat fallback shows both reels, the head and the moving tape route,
    // retaining the transport's key landmarks when GPU rendering is unavailable.
    const auto lowerCentre = juce::Point<float> (width * 0.5f, height * 0.82f);
    const auto lowerRadius = juce::jmin (width, height) * 0.16f;
    g.setColour (body.withAlpha (0.90f));
    g.fillEllipse (lowerCentre.x - lowerRadius, lowerCentre.y - lowerRadius,
                   lowerRadius * 2.0f, lowerRadius * 2.0f);
    g.setColour (highlight.withAlpha (0.24f + 0.24f * level));
    g.drawEllipse (lowerCentre.x - lowerRadius, lowerCentre.y - lowerRadius,
                   lowerRadius * 2.0f, lowerRadius * 2.0f, 1.4f);
    g.setColour (tape.withAlpha (0.65f));
    g.fillEllipse (lowerCentre.x - lowerRadius * (0.62f - 0.10f * level),
                   lowerCentre.y - lowerRadius * (0.62f - 0.10f * level),
                   lowerRadius * (1.24f - 0.20f * level),
                   lowerRadius * (1.24f - 0.20f * level));

    drawSpokes (lowerCentre, lowerRadius, lowerRadius * 0.28f, -angle, 1.5f);

    const auto headCentre = juce::Point<float> (width * 0.5f, height * 0.64f);
    const auto headWidth = juce::jmax (4.0f, width * 0.16f);
    const auto headHeight = juce::jmax (3.0f, height * 0.06f);
    g.setColour (highlight.withAlpha (0.80f));
    g.fillRoundedRectangle (headCentre.x - headWidth * 0.5f, headCentre.y - headHeight * 0.5f,
                            headWidth, headHeight, headHeight * 0.25f);

    juce::Path tapePath;
    tapePath.startNewSubPath (centre.x - reelRadius * 0.72f, centre.y + reelRadius * 0.62f);
    tapePath.quadraticTo (centre.x - reelRadius * 0.18f, height * (0.40f + flutter * 0.015f),
                          headCentre.x - headWidth * 0.5f, headCentre.y);
    tapePath.lineTo (headCentre.x + headWidth * 0.5f, headCentre.y);
    tapePath.quadraticTo (centre.x + reelRadius * 0.20f, height * (0.69f + flutter * 0.015f),
                          lowerCentre.x + lowerRadius * 0.72f, lowerCentre.y - lowerRadius * 0.55f);
    g.setColour (tape.brighter (0.25f).withAlpha (0.85f));
    g.strokePath (tapePath, juce::PathStrokeType (juce::jmax (1.0f, width * 0.018f)));
}

void TapeScene::resized()
{
    // Native swapchains/render targets are resized on the message thread, never
    // lazily from the render callback. The adapters defer GPU object recreation
    // until their next beginFrame().
    if (nativeRenderer != nullptr)
    {
        nativeRenderer->resize (nativeRendererConfig());
    }

    // Nothing to place: the projection is derived from the current size in
    // renderOpenGL(), and CPU paint uses the same component bounds. A resize is
    // also a frame the user must SEE - the viewport just changed shape - so the
    // accelerated path is asked for one here too (see setAudioState for why a
    // repaint() alone does not reach renderOpenGL() on this component).
    repaint();

    if (openGLContext.isAttached())
        openGLContext.triggerRepaint();
}

bool TapeScene::isSceneLive() const noexcept
{
    return j37::render::usesOpenGL()
        && sceneEnabled
        && programLinked.load() != 0
        && openGLContext.isAttached();
}

int TapeScene::controlIndexAt (juce::Point<int> position) const
{
    const auto width = getWidth();
    const auto height = getHeight();

    if (width <= 0 || height <= 0 || ! isSceneLive())
        return -1;

    // The camera, rebuilt here from the same constants renderOpenGL() uses. This
    // is deliberately a second computation rather than a matrix kept from the
    // last frame: that matrix would be written on the render thread and read on
    // the message thread, which is a data race for the sake of saving one
    // perspective divide per click.
    const auto aspect = static_cast<float> (width) / static_cast<float> (height);
    const auto halfFovTangent = std::tan (cameraFieldOfViewDegrees
                                            * juce::MathConstants<float>::pi / 360.0f);
    const auto requiredHalfHeight = std::max (sceneHalfHeight, sceneHalfWidth / aspect);
    const auto cameraDistance = requiredHalfHeight / halfFovTangent + sceneHalfDepth;

    const auto projection = glm::perspective (cameraFieldOfViewDegrees
                                                * juce::MathConstants<float>::pi / 180.0f,
                                              aspect,
                                              std::max (0.05f, cameraDistance - 1.2f),
                                              cameraDistance + 1.2f);

    const auto viewProjection = projection
                              * glm::translate (glm::mat4 (1.0f), { 0.0f, 0.0f, -cameraDistance });

    const auto scene = glm::rotate (glm::mat4 (1.0f), cameraPitchRadians, { 1.0f, 0.0f, 0.0f })
                     * glm::rotate (glm::mat4 (1.0f), cameraYawRadians, { 0.0f, 1.0f, 0.0f });

    // Where the click is, in the normalised device coordinates the projection
    // works in: x to the right, y UP, both -1 at the edge and +1 at the other.
    const auto ndcX = 2.0f * static_cast<float> (position.x) / static_cast<float> (width) - 1.0f;
    const auto ndcY = 1.0f - 2.0f * static_cast<float> (position.y) / static_cast<float> (height);

    // A ray through the click, built from the inverse projection. Check the
    // transform before inversion so a degenerate matrix cannot introduce NaNs.
    const auto determinant = glm::determinant (viewProjection);
    if (! std::isfinite (determinant) || std::abs (determinant) < 1.0e-8f)
        return -1;

    const auto inverse = glm::inverse (viewProjection);

    const auto nearPoint = inverse * glm::vec4 (ndcX, ndcY, -1.0f, 1.0f);
    const auto farPoint = inverse * glm::vec4 (ndcX, ndcY, 1.0f, 1.0f);

    if (std::abs (nearPoint.w) < 1.0e-6f || std::abs (farPoint.w) < 1.0e-6f)
        return -1;

    const auto rayOrigin = glm::vec3 (nearPoint) / nearPoint.w;
    const auto rayEnd = glm::vec3 (farPoint) / farPoint.w;
    const auto rayDelta = rayEnd - rayOrigin;
    const auto rayLengthSquared = glm::dot (rayDelta, rayDelta);
    if (! std::isfinite (rayLengthSquared) || rayLengthSquared < 1.0e-12f)
        return -1;
    const auto rayDirection = rayDelta / std::sqrt (rayLengthSquared);

    // The face's plane, in world space: its point is the bank's centre pushed
    // out to its front, and its normal is the scene's own z turned by the two
    // camera rotations.
    const auto planePoint = glm::vec3 (scene * glm::vec4 (0.0f, 0.0f, bankFaceZ, 1.0f));
    const auto normalVector = glm::vec3 (scene * glm::vec4 (0.0f, 0.0f, 1.0f, 0.0f));
    const auto normalLengthSquared = glm::dot (normalVector, normalVector);
    if (! std::isfinite (normalLengthSquared) || normalLengthSquared < 1.0e-12f)
        return -1;
    const auto planeNormal = normalVector / std::sqrt (normalLengthSquared);

    const auto denominator = glm::dot (rayDirection, planeNormal);

    // Parallel to the face, or leaving it: no hit either way. The epsilon is a
    // reciprocal rather than a zero so a ray that grazes the plane at a very
    // shallow angle cannot blow the intersection out to infinity.
    if (std::abs (denominator) < 1.0e-4f)
        return -1;

    const auto distance = glm::dot (planePoint - rayOrigin, planeNormal) / denominator;

    if (distance <= 0.0f)
        return -1;

    const auto hit = rayOrigin + rayDirection * distance;

    // Into the bank's own frame, where a placement's x is what it was written as.
    const auto bankTransform = scene * glm::translate (glm::mat4 (1.0f),
                                                       { 0.0f, 0.0f, bankCentreZ });
    const auto bankDeterminant = glm::determinant (bankTransform);
    if (! std::isfinite (bankDeterminant) || std::abs (bankDeterminant) < 1.0e-8f)
        return -1;

    const auto local = glm::vec3 (glm::inverse (bankTransform) * glm::vec4 (hit, 1.0f));

    // The nearest control whose own footprint contains the point. A knob is a
    // disc and a key is a rectangle, but both are tested as a box of their own
    // half-extents - which is what a control's clickable area IS: nobody aims at
    // the rim of a knob, they aim at the knob.
    auto best = -1;
    auto bestDistance = std::numeric_limits<float>::max();

    for (int i = 0; i < bankPlacementCount; ++i)
    {
        const auto& placement = bankPlacements[i];
        const auto halfWidth = placement.isKey ? bankKeyHalfX : bankKnobRadius;
        const auto halfHeight = placement.isKey ? bankKeyHalfY : bankKnobRadius;

        const auto offsetX = std::abs (local.x - placement.x);
        const auto offsetY = std::abs (local.y);

        if (offsetX > halfWidth || offsetY > halfHeight)
            continue;

        // A ray can cross two controls' boxes at a shallow angle; the one it
        // crosses first is the one the user sees, so the nearer wins.
        const auto squared = offsetX * offsetX + offsetY * offsetY;

        if (squared < bestDistance)
        {
            bestDistance = squared;
            best = i;
        }
    }

    return best;
}

float TapeScene::getControlValue (int controlIndex) const noexcept
{
    if (controlIndex < 0 || controlIndex >= bankKnobCount)
        return 0.5f;

    return knobValues[static_cast<std::size_t> (controlIndex)].load (std::memory_order_relaxed);
}

//==============================================================================
//  Geometry
//==============================================================================
TapeScene::Geometry TapeScene::buildReel()
{
    Geometry geometry;
    auto& vertices = geometry.vertices;
    auto& indices = geometry.indices;

    constexpr int segments = 40;

    // The reel is modelled at radius 1.0 and scaled to its own size afterwards,
    // so one mesh describes both reels and every number below is a fraction of
    // the reel rather than a length in the scene.
    constexpr float flangeHalfHeight = 0.045f;
    constexpr float hubRadius = 0.22f;
    constexpr float hubCentreZ = 0.130f;
    constexpr float hubHalfHeight = 0.095f;

    addDisc (vertices, indices, { 0.0f, 0.0f, 0.0f }, 1.0f, flangeHalfHeight, segments);

    // The boss. Its lower half is sunk into the flange so the two never end up
    // with coplanar faces fighting over the same pixels.
    addDisc (vertices, indices, { 0.0f, 0.0f, hubCentreZ }, hubRadius, hubHalfHeight, segments / 2);

    // Three spokes, because a bare disc does not read as spinning. They sit
    // clear above the pack, so the one part of the reel a user can count turns
    // on is the one part that is never buried in tape.
    for (int spoke = 0; spoke < 3; ++spoke)
    {
        const auto angle = juce::MathConstants<float>::twoPi * static_cast<float> (spoke) / 3.0f;
        addSpoke (vertices, indices, angle);
    }

    return geometry;
}

TapeScene::Geometry TapeScene::buildTapePack()
{
    Geometry geometry;
    auto& vertices = geometry.vertices;
    auto& indices = geometry.indices;

    constexpr int segments = 48;

    // A vertex here stores (direction, fraction, height): the direction says
    // which way round the reel it faces, the fraction how far out through the
    // pack it sits. The shader turns those into a radius between uPackInner and
    // uPackOuter, so the tape visibly leaves one reel and piles onto the other.
    struct Ring
    {
        juce::uint32 top[64] {};
        juce::uint32 bottom[64] {};
    };

    Ring inner, outer;

    for (int i = 0; i < segments; ++i)
    {
        const auto angle = juce::MathConstants<float>::twoPi
                             * static_cast<float> (i) / static_cast<float> (segments);
        const glm::vec2 direction { std::cos (angle), std::sin (angle) };

        inner.top[i] = addVertexReturningIndex (vertices, { direction.x, direction.y, 0.0f },
                                                { 0.0f, 0.0f, 1.0f }, packHalfThickness);
        inner.bottom[i] = addVertexReturningIndex (vertices, { direction.x, direction.y, 0.0f },
                                                   { 0.0f, 0.0f, -1.0f }, -packHalfThickness);
        outer.top[i] = addVertexReturningIndex (vertices, { direction.x, direction.y, 1.0f },
                                                { 0.0f, 0.0f, 1.0f }, packHalfThickness);
        outer.bottom[i] = addVertexReturningIndex (vertices, { direction.x, direction.y, 1.0f },
                                                   { 0.0f, 0.0f, -1.0f }, -packHalfThickness);
    }

    for (int i = 0; i < segments; ++i)
    {
        const auto next = (i + 1) % segments;

        addQuad (indices, inner.top[i], outer.top[i], outer.top[next], inner.top[next]);
        addQuad (indices, inner.bottom[i], inner.bottom[next], outer.bottom[next], outer.bottom[i]);
    }

    // The two rims, which is what gives the pack an edge instead of leaving it a
    // sheet of paper. The wall vertices carry the same fraction as the face they
    // belong to, so they follow the radius when the shader moves it.
    for (int i = 0; i < segments; ++i)
    {
        const auto angle = juce::MathConstants<float>::twoPi
                             * static_cast<float> (i) / static_cast<float> (segments);
        const glm::vec2 direction { std::cos (angle), std::sin (angle) };

        const auto outerTop = addVertexReturningIndex (vertices, { direction.x, direction.y, 1.0f },
                                                       { direction.x, direction.y, 0.0f }, packHalfThickness);
        const auto outerBottom = addVertexReturningIndex (vertices, { direction.x, direction.y, 1.0f },
                                                          { direction.x, direction.y, 0.0f }, -packHalfThickness);
        const auto innerTop = addVertexReturningIndex (vertices, { direction.x, direction.y, 0.0f },
                                                       { -direction.x, -direction.y, 0.0f }, packHalfThickness);
        const auto innerBottom = addVertexReturningIndex (vertices, { direction.x, direction.y, 0.0f },
                                                          { -direction.x, -direction.y, 0.0f }, -packHalfThickness);

        addQuad (indices, outerTop, outerBottom, outer.bottom[(i + 1) % segments], outer.top[(i + 1) % segments]);
        addQuad (indices, innerTop, inner.top[(i + 1) % segments], inner.bottom[(i + 1) % segments], innerBottom);
    }

    return geometry;
}

TapeScene::Geometry TapeScene::buildRibbon()
{
    Geometry geometry;
    auto& vertices = geometry.vertices;
    auto& indices = geometry.indices;

    constexpr int samples = 72;
    constexpr auto anchorCount = static_cast<int> (sizeof (ribbonAnchors) / sizeof (ribbonAnchors[0]));

    // Catmull-Rom through the anchors, with the ends repeated so the curve starts
    // and finishes ON the reels rather than on a copy of one.
    const auto sample = [&] (float t) -> glm::vec3
    {
        const auto scaled = t * static_cast<float> (anchorCount - 1);
        const auto index = juce::jlimit (0, anchorCount - 2, static_cast<int> (scaled));
        return catmullRom (ribbonAnchors[index],
                           ribbonAnchors[index + 1],
                           ribbonAnchors[juce::jmin (index + 2, anchorCount - 1)],
                           ribbonAnchors[juce::jmin (index + 3, anchorCount - 1)],
                           scaled - static_cast<float> (index));
    };

    juce::uint32 previous[2] {};

    for (int i = 0; i <= samples; ++i)
    {
        const auto t = static_cast<float> (i) / static_cast<float> (samples);
        const auto point = sample (t);
        const auto before = sample (juce::jmax (0.0f, t - 0.01f));
        const auto after = sample (juce::jmin (1.0f, t + 0.01f));

        // The strip's width runs along the path's normal in the scene's own
        // plane, so the tape keeps its width as it turns and its face stays
        // turned to the camera.
        const auto tangent = after - before;
        const glm::vec3 side { -tangent.y, tangent.x, 0.0f };
        const auto sideLength = std::max (glm::length (side), 0.0001f);
        const auto offset = side * (tapeWidth * 0.5f / sideLength);

        // The payload is t, which is what the shader waves the tape with.
        const auto left = static_cast<juce::uint32> (vertices.size() / 7);
        addVertex (vertices, point - offset, { 0.0f, 0.0f, 1.0f }, t);
        const auto right = left + 1;
        addVertex (vertices, point + offset, { 0.0f, 0.0f, 1.0f }, t);

        if (i > 0)
            addQuad (indices, previous[0], left, right, previous[1]);

        previous[0] = left;
        previous[1] = right;
    }

    return geometry;
}

TapeScene::Geometry TapeScene::buildHead()
{
    Geometry geometry;
    addBox (geometry.vertices, geometry.indices,
            { headCentreX, headCentreY, headCentreZ },
            { headHalfX, headHalfY, headHalfZ });
    return geometry;
}

TapeScene::ControlBank TapeScene::buildControlBank()
{
    ControlBank bank;

    // -------------------------------------------------------------------------
    //  The face the controls are set into. One box, modelled at the scene's
    //  origin rather than at bankCentreZ, so drawControlBank() places it with a
    //  single translation and the four meshes it draws share that one matrix.
    // -------------------------------------------------------------------------
    addBox (bank.body.vertices, bank.body.indices,
            { 0.0f, 0.0f, 0.0f },
            { bankHalfWidth, bankHalfHeight, bankHalfDepth });

    // -------------------------------------------------------------------------
    //  One knob, built round its own origin: a machined barrel with a knurled
    //  grip, a slightly domed face, and a pointer set into it.
    //
    //  Every radius below is a FRACTION of the knob's radius rather than a
    //  length, so one mesh is drawn at the size the bank asks for and a change to
    //  the knob is a change to all three of them at once.
    // -------------------------------------------------------------------------
    constexpr int segments = 32;

    // The body: the face and the back, with the knurl between them. The face is
    // a touch smaller than the grip so the rim reads as a machined step.
    addDisc (bank.knob.vertices, bank.knob.indices,
             { 0.0f, 0.0f, 0.020f }, 0.92f, 0.004f, segments);
    addDisc (bank.knob.vertices, bank.knob.indices,
             { 0.0f, 0.0f, -0.012f }, 0.94f, 0.004f, segments);
    addKnurl (bank.knob.vertices, bank.knob.indices, 1.0f, -0.008f, 0.016f, segments);

    // The domed face. A stack of shrinking discs rather than a sphere: at twenty
    // pixels across the difference is invisible, and the rings cost a fifth of
    // the vertices. Each ring's radius is the circle's own, so the dome's
    // silhouette is round rather than a cone.
    for (int ring = 1; ring <= 3; ++ring)
    {
        const auto t = static_cast<float> (ring) / 3.0f;
        const auto radius = 0.92f * std::sqrt (juce::jmax (0.0f, 1.0f - t * t));
        addDisc (bank.knob.vertices, bank.knob.indices,
                 { 0.0f, 0.0f, 0.020f + 0.016f * t }, radius, 0.003f, segments);
    }

    // The index mark: a raised bar from the hub to the rim. It is the whole of
    // how a user reads the knob's angle, so it is modelled rather than shaded - a
    // bar catches the key light on one side and goes dark on the other, which
    // reads at a glance where a painted line does not.
    addBox (bank.knob.vertices, bank.knob.indices,
            { 0.0f, 0.60f, 0.040f },
            { 0.045f, 0.34f, 0.012f });

    // -------------------------------------------------------------------------
    //  One keycap. A low prism with a shallow bevel, which is what a transport
    //  key on a deck is: flat enough to look pressed rather than turned.
    // -------------------------------------------------------------------------
    addBox (bank.key.vertices, bank.key.indices,
            { 0.0f, 0.0f, 0.0f },
            { 0.94f, 0.82f, 0.90f });
    addBox (bank.key.vertices, bank.key.indices,
            { 0.0f, 0.78f, 0.0f },
            { 0.72f, 0.62f, 0.30f });

    // -------------------------------------------------------------------------
    //  The lit disc: what a knob lays on the face when the machine is working.
    //  A disc of its own rather than a colour change on the body, because the
    //  glow is ADDITIVE - it is drawn with blending on, over whatever is behind
    //  it, so it can bleed past the knob's rim without a second geometry pass.
    // -------------------------------------------------------------------------
    addDisc (bank.knobGlow.vertices, bank.knobGlow.indices,
             { 0.0f, 0.0f, 0.0f }, 1.0f, 0.0005f, segments, 1.0f);

    return bank;
}

//==============================================================================
//  Buffers
//==============================================================================
void TapeScene::uploadMesh (Mesh& mesh, const Geometry& geometry)
{
    mesh.indexCount = static_cast<GLsizei> (geometry.indices.size());

    if (mesh.indexCount <= 0)
        return;

    if (mesh.vertexBuffer == 0)
        glGenBuffers (1, &mesh.vertexBuffer);

    if (mesh.indexBuffer == 0)
        glGenBuffers (1, &mesh.indexBuffer);

    glBindBuffer (GL_ARRAY_BUFFER, mesh.vertexBuffer);
    glBufferData (GL_ARRAY_BUFFER,
                  static_cast<GLsizeiptr> (geometry.vertices.size() * sizeof (float)),
                  geometry.vertices.data(),
                  GL_STATIC_DRAW);

    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, mesh.indexBuffer);
    glBufferData (GL_ELEMENT_ARRAY_BUFFER,
                  static_cast<GLsizeiptr> (geometry.indices.size() * sizeof (juce::uint32)),
                  geometry.indices.data(),
                  GL_STATIC_DRAW);

    glBindBuffer (GL_ARRAY_BUFFER, 0);
    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
}

void TapeScene::releaseMesh (Mesh& mesh) noexcept
{
    if (mesh.vertexBuffer != 0)
    {
        glDeleteBuffers (1, &mesh.vertexBuffer);
        mesh.vertexBuffer = 0;
    }

    if (mesh.indexBuffer != 0)
    {
        glDeleteBuffers (1, &mesh.indexBuffer);
        mesh.indexBuffer = 0;
    }

    mesh.indexCount = 0;
}

void TapeScene::drawMesh (const Mesh& mesh)
{
    if (mesh.indexCount <= 0 || positionLocation < 0 || normalLocation < 0)
        return;

    if (vertexArray != 0)
        glBindVertexArray (vertexArray);

    // The attribute pointers are re-stated per mesh rather than once per VAO.
    // A vertex array remembers them, so this is redundant exactly when a VAO
    // exists - and having one code path that is right in both cases is worth
    // more here than the handful of redundant calls a 64 px column costs.
    glBindBuffer (GL_ARRAY_BUFFER, mesh.vertexBuffer);
    glVertexAttribPointer (static_cast<GLuint> (positionLocation), 4, GL_FLOAT, GL_FALSE,
                           vertexStride, nullptr);
    // C4312 keeps a GLsizei off a pointer cast honest: the offset goes up
    // through uintptr_t, so the pointer is built from an integer of its own
    // width instead of the compiler guessing at a widening it cannot verify.
    glVertexAttribPointer (static_cast<GLuint> (normalLocation), 3, GL_FLOAT, GL_FALSE,
                           vertexStride,
                           reinterpret_cast<const void*> (
                               static_cast<std::uintptr_t> (normalOffset)));
    glEnableVertexAttribArray (static_cast<GLuint> (positionLocation));
    glEnableVertexAttribArray (static_cast<GLuint> (normalLocation));

    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, mesh.indexBuffer);
    glDrawElements (GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);

    // A core profile will not draw again with VAO zero bound, and the editor's
    // 2D context is a different one, so nothing else is relying on it.
    if (vertexArray != 0)
        glBindVertexArray (0);
}

//==============================================================================
//  The control bank
//==============================================================================
void TapeScene::drawControlBank (const glm::mat4& viewProjection,
                                 const glm::mat4& scene,
                                 const juce::Colour& body,
                                 const juce::Colour& highlight)
{
    // -------------------------------------------------------------------------
    //  Where the bank's face is, in the camera's own terms.
    //
    //  A knob's angle has to be set from the LOCAL position of the vertex - which
    //  is what the knob shader mode reads, and what makes the lit arc turn with
    //  the knob - so the arc's start angle is computed here, from the value, and
    //  handed over as one uniform. The 2D panel's rotary drawing sweeps the same
    //  two constants, so a knob in the window and a knob in the panel travel
    //  through the same arc.
    // -------------------------------------------------------------------------
    const auto bankModel = scene * glm::translate (glm::mat4 (1.0f), { 0.0f, 0.0f, bankCentreZ });
    const auto faceModel = bankModel * glm::translate (glm::mat4 (1.0f), { 0.0f, 0.0f, bankHalfDepth });

    // -------------------------------------------------------------------------
    // The face is drawn first so the controls sit visibly on its surface.
    // Draw the face first so the controls and glows sit above it.
    shaderProgram->setUniformMat4 ("uModel", glm::value_ptr (faceModel), 1, GL_FALSE);
    shaderProgram->setUniform ("uMode", static_cast<float> (panelMode));
    shaderProgram->setUniform ("uBaseColour", body.getFloatRed(), body.getFloatGreen(),
                               body.getFloatBlue());
    shaderProgram->setUniform ("uPanelHalf", bankHalfWidth, bankHalfHeight, bankHalfDepth);

    drawMesh (bankBody);

    // -------------------------------------------------------------------------
    //  The knobs and the keys, from the one placement table.
    // -------------------------------------------------------------------------
    for (int i = 0; i < bankPlacementCount; ++i)
    {
        const auto& placement = bankPlacements[i];
        const auto isKnob = ! placement.isKey;

        // A knob's value is its own parameter, 0..1. The keys have no value: they
        // are state, so they rest at the middle of their travel and are lit by
        // the same glow the knobs use when the machine is working.
        const auto value = isKnob
                             ? knobValues[static_cast<std::size_t> (i)]
                                   .load (std::memory_order_relaxed)
                             : 0.5f;

        const auto angle = knobMinimumAngle + value * (knobMaximumAngle - knobMinimumAngle);

        // The knob's own frame: place it, turn it, size it. A key is not turned -
        // its cap is what reads as pressed - so its rotation is the identity and
        // its scale is the cap's own half-extents.
        const auto model = isKnob
                             ? faceModel
                                 * glm::translate (glm::mat4 (1.0f), { placement.x, 0.0f, 0.0f })
                                 * glm::rotate (glm::mat4 (1.0f), angle, { 0.0f, 0.0f, 1.0f })
                                 * glm::scale (glm::mat4 (1.0f),
                                               { bankKnobRadius, bankKnobRadius, bankKnobRadius })
                             : faceModel
                                 * glm::translate (glm::mat4 (1.0f),
                                                   { placement.x, 0.0f, bankKeyHalfZ })
                                 * glm::scale (glm::mat4 (1.0f),
                                               { bankKeyHalfX, bankKeyHalfY, bankKeyHalfZ });

        shaderProgram->setUniformMat4 ("uModel", glm::value_ptr (model), 1, GL_FALSE);
        shaderProgram->setUniform ("uBaseColour", body.brighter (0.18f).getFloatRed(),
                                   body.brighter (0.18f).getFloatGreen(),
                                   body.brighter (0.18f).getFloatBlue());

        if (isKnob)
        {
            shaderProgram->setUniform ("uMode", static_cast<float> (knobMode));
            shaderProgram->setUniform ("uArcAngle", knobMinimumAngle);
            drawMesh (bankKnob);
        }
        else
        {
            // A key is pressed when the machine is running and released when it
            // is not - which is the whole of what a key on this bank has to say,
            // and is why there is no per-key state to keep in step with anything.
            const auto pressed = (i == bankPlacementCount - 2) ? bankSpinning : bankRecording;

            shaderProgram->setUniform ("uMode", static_cast<float> (keyMode));
            shaderProgram->setUniform ("uKeyDown", pressed ? 1.0f : 0.0f);
            drawMesh (bankKey);
        }
    }

    // -------------------------------------------------------------------------
    //  The lit discs, additive, over the face and under nothing.
    //
    //  One per knob, drawn with the blend on so it reads as light on the face
    //  rather than as a second material. The glow under a knob follows the
    //  machine rather than the knob's own value: it is the deck's activity lamp,
    //  repeated - so a bank of three knobs lights up together when the tape is
    //  working, which is what a real machine does.
    // -------------------------------------------------------------------------
    const auto glowAmount = juce::jlimit (0.0f, 1.0f,
                                          0.25f * bankDrive + 0.35f * bankPeak + 0.40f * bankReduction);

    glEnable (GL_BLEND);
    glBlendFunc (GL_SRC_ALPHA, GL_ONE);

    for (int i = 0; i < bankKnobCount; ++i)
    {
        // 1.55 rather than 1.9: at 1.9 the disc reached 0.115 from the knob's
        // centre and crossed a keycap's own edge at 0.100, which is the light the
        // key was standing on. At 1.55 it reaches 0.093 and the key's edge is at
        // 0.108 - the halo is still the deck's activity lamp, and it is now a
        // light ON the face around its own knob instead of one spilling onto the
        // control beside it.
        const auto model = faceModel
                         * glm::translate (glm::mat4 (1.0f), { bankPlacements[i].x, 0.0f, 0.008f })
                         * glm::scale (glm::mat4 (1.0f),
                                       { bankKnobRadius * 1.55f, bankKnobRadius * 1.55f, 1.0f });

        shaderProgram->setUniformMat4 ("uModel", glm::value_ptr (model), 1, GL_FALSE);
        shaderProgram->setUniform ("uMode", static_cast<float> (glowMode));
        shaderProgram->setUniform ("uHighlightColour", highlight.getFloatRed(),
                                   highlight.getFloatGreen(), highlight.getFloatBlue());
        shaderProgram->setUniform ("uGlow", glowAmount);
        drawMesh (bankGlow);
    }

    glDisable (GL_BLEND);
    glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    juce::ignoreUnused (viewProjection);
}//==============================================================================
//  OpenGLRenderer
//==============================================================================
void TapeScene::newOpenGLContextCreated()
{
    shaderProgram.reset();
    positionLocation = -1;
    normalLocation = -1;
    programLinked.store (0);

    // A 3.x core profile will not accept a draw with no vertex array bound; a
    // 1.x context has nowhere to bind one. The version test is the whole of the
    // difference between the two paths.
    if (openGLContext.getVersion().major >= 3)
        glGenVertexArrays (1, &vertexArray);

    shaderProgram = std::make_unique<juce::OpenGLShaderProgram> (openGLContext);

    if (shaderProgram->addVertexShader (prepare (vertexShaderSource, true))
        && shaderProgram->addFragmentShader (prepare (fragmentShaderSource, false))
        && shaderProgram->link())
    {
        const auto programId = shaderProgram->getProgramID();
        positionLocation = glGetAttribLocation (programId, "aPosition");
        normalLocation = glGetAttribLocation (programId, "aNormal");

        // Both attributes have to exist for anything to be drawn, so a program
        // that linked but lost one counts as no program at all - which is what
        // makes the editor fall back to its flat transport instead of showing a
        // black rectangle.
        programLinked.store (positionLocation >= 0 && normalLocation >= 0 ? 1 : 0);
    }

    if (programLinked.load() != 0)
    {
        uploadMesh (reel, buildReel());
        uploadMesh (tapePack, buildTapePack());
        uploadMesh (ribbon, buildRibbon());
        uploadMesh (head, buildHead());

        const auto bank = buildControlBank();
        uploadMesh (bankBody, bank.body);
        uploadMesh (bankKnob, bank.knob);
        uploadMesh (bankKey, bank.key);
        uploadMesh (bankGlow, bank.knobGlow);
    }
    else
    {
        releaseMesh (reel);
        releaseMesh (tapePack);
        releaseMesh (ribbon);
        releaseMesh (head);
        releaseMesh (bankBody);
        releaseMesh (bankKnob);
        releaseMesh (bankKey);
        releaseMesh (bankGlow);
        shaderProgram.reset();
    }

    // Restart the clock, so a context that comes up an hour after the plugin
    // did not open with the reels at an angle of 9360 radians and a jump.
    timeOriginSeconds = 0.0;
    previousFrameSeconds = 0.0;
    repaint();
}

void TapeScene::openGLContextClosing()
{
    programLinked.store (0);

    releaseMesh (reel);
    releaseMesh (tapePack);
    releaseMesh (ribbon);
    releaseMesh (head);
    releaseMesh (bankBody);
    releaseMesh (bankKnob);
    releaseMesh (bankKey);
    releaseMesh (bankGlow);

    if (vertexArray != 0)
    {
        glDeleteVertexArrays (1, &vertexArray);
        vertexArray = 0;
    }

    shaderProgram.reset();
    positionLocation = -1;
    normalLocation = -1;
}

void TapeScene::renderOpenGL()
{
    if (programLinked.load() == 0 || shaderProgram == nullptr)
        return;

    const auto width = getWidth();
    const auto height = getHeight();

    if (width <= 0 || height <= 0)
        return;

    const auto level = outputLevel.load (std::memory_order_relaxed);
    const auto peak = outputPeak.load (std::memory_order_relaxed);
    const auto driveAmount = drive.load (std::memory_order_relaxed);
    const auto reduction = gainReduction.load (std::memory_order_relaxed);
    const auto flutter = wowFlutter.load (std::memory_order_relaxed);
    const auto speed = transportSpeed.load (std::memory_order_relaxed);

    const auto background = juce::Colour (backgroundColour.load (std::memory_order_relaxed));
    const auto body = juce::Colour (bodyColour.load (std::memory_order_relaxed));
    const auto highlight = juce::Colour (highlightColour.load (std::memory_order_relaxed));
    const auto tape = juce::Colour (tapeColour.load (std::memory_order_relaxed));

    // -------------------------------------------------------------------------
    //  Time. The reel angles are INTEGRATED rather than derived from elapsed
    //  seconds, so a transport that is sped up or stopped turns the reels by the
    //  right amount instead of snapping to wherever elapsed * speed happens to
    //  land - which is the difference between a machine changing speed and a
    //  machine stuttering.
    //
    //  The bank's own values lag the ones they follow, for the same reason: a
    //  real knob does not jump. Each is a one-pole filter with a time constant
    //  chosen per signal - the knobs settle in about a fifth of a second, the
    //  peak meter falls slowly enough to be read.
    // -------------------------------------------------------------------------
    const auto now = juce::Time::getMillisecondCounterHiRes();

    if (timeOriginSeconds <= 0.0)
    {
        timeOriginSeconds = now;
        previousFrameSeconds = now;
    }

    const auto elapsed = now - timeOriginSeconds;
    const auto frameSeconds = juce::jlimit (0.0, 0.1, now - previousFrameSeconds);
    previousFrameSeconds = now;

    const auto frame = static_cast<float> (frameSeconds);
    const auto follow = [frame] (float current, float target, float timeConstant)
    {
        if (timeConstant <= 0.0f)
            return target;

        const auto coefficient = juce::jlimit (0.0f, 1.0f, frame / timeConstant);
        return current + (target - current) * coefficient;
    };

    bankDrive = follow (bankDrive, driveAmount, 0.18f);
    bankReduction = follow (bankReduction, reduction, 0.12f);

    // The peak readout rises on the signal and falls on its own clock, which is
    // what makes it read as a meter rather than as a copy of the level.
    bankPeak = juce::jmax (peak, bankPeak - frame * 0.8f);

    // The two keycaps: SPIN while the transport runs, REC while the machine is
    // doing something to the signal. Both are states of the machine, so both are
    // read from what the machine is already reporting rather than kept here.
    bankSpinning = speed > 0.05f;
    bankRecording = reduction > 0.02f || driveAmount > 0.25f;

    bankTransfer = follow (bankTransfer, tapeTransfer, 0.25f);

    supplyAngle = std::fmod (supplyAngle + frame * 2.6f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));
    takeUpAngle = std::fmod (takeUpAngle + frame * 2.3f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));

    if (speed > 0.001f)
    {
        // Tape off the supply reel and onto the take-up one, faster when there
        // is signal to record. It is the one number in the scene that has to
        // remember anything, which is why it is a member and not a uniform.
        tapeTransfer = juce::jlimit (0.0f, 1.0f,
                                     tapeTransfer + frame * (0.010f + 0.075f * level));
    }

    // -------------------------------------------------------------------------
    //  The camera. Perspective rather than orthographic, because a real transport
    //  is seen at an angle and the convergence is most of what sells it.
    //
    //  The distance is solved rather than fixed: the column is about a quarter as
    //  wide as it is tall, so a camera placed for the content alone would crop
    //  the reels the moment the deck got a few pixels shorter. Backing off until
    //  the scene's own half-extents fit the FRAME keeps the same composition at
    //  the panel's minimum and at the size it opens at, and the near and far
    //  planes follow the distance so the depth buffer is not asked to resolve a
    //  40-unit range to tell a pack's front face from its back.
    // -------------------------------------------------------------------------
    const auto aspect = static_cast<float> (width) / static_cast<float> (height);
    const auto halfFovTangent = std::tan (cameraFieldOfViewDegrees
                                            * juce::MathConstants<float>::pi / 360.0f);
    const auto requiredHalfHeight = std::max (sceneHalfHeight, sceneHalfWidth / aspect);
    const auto cameraDistance = requiredHalfHeight / halfFovTangent + sceneHalfDepth;

    const auto projection = glm::perspective (cameraFieldOfViewDegrees
                                                * juce::MathConstants<float>::pi / 180.0f,
                                              aspect,
                                              std::max (0.05f, cameraDistance - 1.2f),
                                              cameraDistance + 1.2f);

    const auto viewProjection = projection
                              * glm::translate (glm::mat4 (1.0f), { 0.0f, 0.0f, -cameraDistance });

    // The deck plate is turned twice, which is the whole of the 3D in the
    // composition: down onto it, and round towards the operator's left.
    const auto scene = glm::rotate (glm::mat4 (1.0f), cameraPitchRadians, { 1.0f, 0.0f, 0.0f })
                     * glm::rotate (glm::mat4 (1.0f), cameraYawRadians, { 0.0f, 1.0f, 0.0f });

    // Save shared-context GL state before configuring this scene pass.
    GLint previousDepthFunction = GL_LESS;
    GLint previousBlendSource = GL_ONE;
    GLint previousBlendDestination = GL_ZERO;
    GLint previousViewport[4] {};
    GLint previousProgram = 0;
    GLint previousVertexArray = 0;
    GLint previousArrayBuffer = 0;
    GLint previousElementBuffer = 0;
    GLboolean previousDepthWrite = GL_TRUE;
    const auto previousDepthTest = glIsEnabled (GL_DEPTH_TEST);
    const auto previousBlend = glIsEnabled (GL_BLEND);
    const auto previousCull = glIsEnabled (GL_CULL_FACE);
    glGetIntegerv (GL_DEPTH_FUNC, &previousDepthFunction);
    glGetIntegerv (GL_BLEND_SRC, &previousBlendSource);
    glGetIntegerv (GL_BLEND_DST, &previousBlendDestination);
    glGetIntegerv (GL_VIEWPORT, previousViewport);
    glGetIntegerv (GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv (GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
    glGetIntegerv (GL_ELEMENT_ARRAY_BUFFER_BINDING, &previousElementBuffer);
    if (vertexArray != 0)
        glGetIntegerv (GL_VERTEX_ARRAY_BINDING, &previousVertexArray);
    glGetBooleanv (GL_DEPTH_WRITEMASK, &previousDepthWrite);

    glViewport (0, 0, width, height);
    juce::OpenGLHelpers::clear (background);
    glDepthMask (GL_TRUE);

    glEnable (GL_DEPTH_TEST);
    glDepthFunc (GL_LEQUAL);
    glDisable (GL_CULL_FACE);
    glDisable (GL_BLEND);
    glDisable (GL_TEXTURE_2D);

    shaderProgram->use();
    shaderProgram->setUniformMat4 ("uViewProjection", glm::value_ptr (viewProjection), 1, GL_FALSE);
    shaderProgram->setUniform ("uTime", static_cast<float> (elapsed));
    shaderProgram->setUniform ("uFlutter", flutter);
    shaderProgram->setUniform ("uGlow", reduction);
    shaderProgram->setUniform ("uDrive", driveAmount);
    shaderProgram->setUniform ("uLightDirection", lightX, lightY, lightZ);
    const auto normalMatrix = glm::transpose (glm::inverse (glm::mat3 (scene)));
    shaderProgram->setUniformMat3 ("uNormalMatrix", glm::value_ptr (normalMatrix), 1, GL_FALSE);
    // The arithmetic texture's three doses (grain / tape shimmer / wear), one
    // palette-carried vector - see panelTexture in the fragment source. Snapshot
    // the packed atomic once so a theme switch cannot mix channels from two states.
    const auto texturePalette = juce::Colour (textureWeights.load (std::memory_order_relaxed));
    shaderProgram->setUniform ("uTextureWeights", texturePalette.getFloatRed(),
                               texturePalette.getFloatGreen(), texturePalette.getFloatBlue());


    const auto drawReel = [this, &scene, &body, &tape] (float centreX, float centreY, float radius,
                                                        float angle, float packOuter)
    {
        const auto model = scene
                         * glm::translate (glm::mat4 (1.0f), { centreX, centreY, 0.0f })
                         * glm::rotate (glm::mat4 (1.0f), angle, { 0.0f, 0.0f, 1.0f })
                         * glm::scale (glm::mat4 (1.0f), { radius, radius, radius });

        shaderProgram->setUniformMat4 ("uModel", glm::value_ptr (model), 1, GL_FALSE);

        shaderProgram->setUniform ("uMode", static_cast<float> (solidMode));
        shaderProgram->setUniform ("uBaseColour", body.getFloatRed(), body.getFloatGreen(),
                                   body.getFloatBlue());
        drawMesh (reel);

        shaderProgram->setUniform ("uMode", static_cast<float> (packMode));
        shaderProgram->setUniform ("uPackInner", packInnerFraction);
        shaderProgram->setUniform ("uPackOuter", packOuter);
        shaderProgram->setUniform ("uBaseColour", tape.getFloatRed(), tape.getFloatGreen(),
                                   tape.getFloatBlue());
        drawMesh (tapePack);
    };

    // The supply reel empties as the take-up one fills: the same two numbers,
    // one of them the other one's complement.
    const auto supplyPackOuter = packEmptyFraction
                               + (packFullFraction - packEmptyFraction) * (1.0f - tapeTransfer);
    const auto takeUpPackOuter = packEmptyFraction
                               + (packFullFraction - packEmptyFraction) * tapeTransfer;

    drawReel (supplyCentreX, supplyCentreY, supplyRadius, supplyAngle, supplyPackOuter);
    drawReel (takeUpCentreX, takeUpCentreY, takeUpRadius, takeUpAngle, takeUpPackOuter);

    // The head: the one solid in the scene that does not move, which is what
    // makes the tape's movement between the two reels readable as movement
    // rather than as everything spinning together.
    const auto headModel = scene * glm::translate (glm::mat4 (1.0f),
                                                   { headCentreX, headCentreY, headCentreZ });
    shaderProgram->setUniformMat4 ("uModel", glm::value_ptr (headModel), 1, GL_FALSE);
    shaderProgram->setUniform ("uMode", static_cast<float> (solidMode));
    shaderProgram->setUniform ("uBaseColour", highlight.getFloatRed() * 0.55f + 0.06f,
                               highlight.getFloatGreen() * 0.55f + 0.06f,
                               highlight.getFloatBlue() * 0.55f + 0.07f);
    drawMesh (head);

    // The tape itself, last so it is over the head's face.
    shaderProgram->setUniformMat4 ("uModel", glm::value_ptr (scene), 1, GL_FALSE);
    shaderProgram->setUniform ("uMode", static_cast<float> (ribbonMode));
    shaderProgram->setUniform ("uBaseColour", tape.getFloatRed() * 1.35f + 0.02f,
                               tape.getFloatGreen() * 1.35f + 0.02f,
                               tape.getFloatBlue() * 1.35f + 0.02f);
    drawMesh (ribbon);

    // The control bank, in FRONT of the machine: it is the only geometry here
    // with a z of its own, and drawing it last costs nothing because the depth
    // buffer is what decides, not the order.
    drawControlBank (viewProjection, scene, body, highlight);

    glDepthFunc (static_cast<GLenum> (previousDepthFunction));
    glDepthMask (previousDepthWrite);
    glBlendFunc (static_cast<GLenum> (previousBlendSource),
                 static_cast<GLenum> (previousBlendDestination));
    if (previousDepthTest) glEnable (GL_DEPTH_TEST); else glDisable (GL_DEPTH_TEST);
    if (previousBlend) glEnable (GL_BLEND); else glDisable (GL_BLEND);
    if (previousCull) glEnable (GL_CULL_FACE); else glDisable (GL_CULL_FACE);
    glViewport (previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    glUseProgram (static_cast<GLuint> (previousProgram));
    if (vertexArray != 0)
        glBindVertexArray (static_cast<GLuint> (previousVertexArray));
    glBindBuffer (GL_ARRAY_BUFFER, static_cast<GLuint> (previousArrayBuffer));
    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint> (previousElementBuffer));
}
