/*
  ==============================================================================
    TapeScene - the deck's transport, rendered as geometry and lit on the GPU.
  ==============================================================================
*/

#include "TapeScene.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>

// JUCE 9 keeps every GL command and every GL enum in juce::gl, and documents
// `using namespace ::juce::gl;` as the supported way to bring them into scope -
// the GL typedefs (GLuint, GLint, GLsizeiptr) are already global. Doing it in
// the .cpp rather than the header is the module's own advice.
using namespace juce::gl;

// JUCE_HIGHP expands to nothing on desktop GL and to "highp" on GLES, so it
// cannot be written into the shader strings as an argument: pasting an empty
// macro into a function call is not valid C++ and would not compile. This picks
// the literal instead, and the shader text stays readable as shader text.
#if defined (JUCE_OPENGL_ES) && JUCE_OPENGL_ES
 #define TAPE_SCENE_HIGHP "highp"
#else
 #define TAPE_SCENE_HIGHP ""
#endif

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
    constexpr float bankKeyHalfX   = 0.052f;
    constexpr float bankKeyHalfY   = 0.034f;
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
        ribbonMode = 2
    };

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
                  int segments)
    {
        const auto top = centre.z + halfHeight;
        const auto bottom = centre.z - halfHeight;

        const auto centreTop = addVertexReturningIndex (vertices, { centre.x, centre.y, top },
                                                       { 0.0f, 0.0f, 1.0f });
        const auto centreBottom = addVertexReturningIndex (vertices, { centre.x, centre.y, bottom },
                                                          { 0.0f, 0.0f, -1.0f });

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
                                         { 0.0f, 0.0f, 1.0f });

            bottomRing[static_cast<size_t> (i)] =
                addVertexReturningIndex (vertices,
                                         { centre.x + outward.x * radius,
                                           centre.y + outward.y * radius, bottom },
                                         { 0.0f, 0.0f, -1.0f });
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

            const auto wallTop = addVertexReturningIndex (vertices, rim + glm::vec3 (0.0f, 0.0f, top), outward);
            const auto wallBottom = addVertexReturningIndex (vertices, rim + glm::vec3 (0.0f, 0.0f, bottom), outward);

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

    /** The knurled grip round a knob's rim: one flat-sided prism per segment, cut
        down the middle so each side of the knurl carries its own outward normal.

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

    /** The knurled grip round a knob's rim: one flat-sided prism per segment, cut
        down the middle so each side of the knurl carries its own outward normal.

        A smooth cylinder would light as one continuous band of highlight and read
        as plastic. Real knurling is a ring of tiny flats, and the reason to model
        it rather than texture it is that the flats catch the light in STRIPES -
        the highlight jumps from facet to facet as the knob turns, which is the
        cue that tells a user the knob is moving even when its index mark is too
        small to follow.

        The prisms span the knob's barrel only: the face above and the back below
        are the flat discs from addDisc, so the knurl is a band round the side
        rather than a replacement for the whole cylinder. */
    void addKnurl (std::vector<float>& vertices,
                   std::vector<juce::uint32>& indices,
                   float radius,
                   float bottomZ,
                   float topZ,
                   int segments)
    {
        for (int i = 0; i < segments; ++i)
        {
            const auto angle = juce::MathConstants<float>::twoPi
                                 * static_cast<float> (i) / static_cast<float> (segments);

            const glm::vec2 centre { std::cos (angle), std::sin (angle) };
            const glm::vec2 next { std::cos (angle + juce::MathConstants<float>::twoPi
                                                     / static_cast<float> (segments)),
                                   std::sin (angle + juce::MathConstants<float>::twoPi
                                                     / static_cast<float> (segments)) };

            // The facet's own outward direction, normalised: a chord of the circle
            // is shorter than its arc, and a normal that was not re-normalised
            // would dim every facet by the chord's own cosine.
            const glm::vec2 facet { centre.x + next.x, centre.y + next.y };
            const auto facetLength = std::max (glm::length (facet), 0.0001f);
            const glm::vec3 outward { facet.x / facetLength, facet.y / facetLength, 0.0f };

            const auto corner = [&] (const glm::vec2& direction, float z)
            {
                return glm::vec3 (direction.x * radius, direction.y * radius, z);
            };

            const auto a = addVertexReturningIndex (vertices, corner (centre, topZ), outward);
            const auto b = addVertexReturningIndex (vertices, corner (next,   topZ), outward);
            const auto c = addVertexReturningIndex (vertices, corner (next,   bottomZ), outward);
            const auto d = addVertexReturningIndex (vertices, corner (centre, bottomZ), outward);

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

    //==========================================================================
    //  GLSL
    //
    //  Written in the oldest dialect that runs everywhere, on purpose. JUCE's
    //  addShader() hands the text to the driver verbatim - it prepends no
    //  #version line of its own - so this has to compile as GLSL 1.10 on a
    //  legacy context, and it has to be the same text that survives JUCE's
    //  translateVertexShaderToV3() on a 3.2 core profile. `attribute`,
    //  `varying` and `gl_FragColor` are exactly what those two translators
    //  rewrite, so the source is written in the dialect they consume rather than
    //  in one they would have to be told about.
    //
    //  The payload - aPosition.w - is what makes the tape move without a single
    //  byte crossing the bus after start-up:
    //
    //      pack    x,y is a UNIT direction, z is the fraction of the pack's
    //             width the vertex sits at, w is its height
    //      ribbon  xyz is the point, w is how far along the tape it is
    //
    //  vDetail is the shader's one texture, and there is no sampler: it is the
    //  fraction the vertex sat at, carried to the fragment stage and repeated
    //  there. Concentric on a pack, longitudinal on a ribbon, radial on a reel
    //  flange - which is what a machined face and a roll of tape both look like,
    //  from arithmetic.
    //==========================================================================
    const char* const vertexShaderSource =
        "attribute vec4 aPosition;\n"
        "attribute vec3 aNormal;\n"
        "\n"
        "uniform mat4 uViewProjection;\n"
        "uniform mat4 uModel;\n"
        "uniform float uMode;\n"
        "uniform float uPackInner;\n"
        "uniform float uPackOuter;\n"
        "uniform float uTime;\n"
        "uniform float uFlutter;\n"
        "\n"
        "varying vec3 vNormal;\n"
        "varying float vDetail;\n"
        "varying vec2 vLocal;\n"
        "\n"
        "void main()\n"
        "{\n"
        "    vec3 p = aPosition.xyz;\n"
        "    vDetail = length (aPosition.xy);\n"
        "    vLocal = aPosition.xy;\n"
        "\n"
        "    if (uMode > 0.5 && uMode < 1.5)\n"
        "    {\n"
        "        TAPE_SCENE_HIGHP float radius = mix (uPackInner, uPackOuter, aPosition.z);\n"
        "        p = vec3 (aPosition.xy * radius, aPosition.w);\n"
        "        vDetail = aPosition.z;\n"
        "    }\n"
        "    else if (uMode > 1.5 && uMode < 2.5)\n"
        "    {\n"
        "        TAPE_SCENE_HIGHP float t = aPosition.w;\n"
        "        TAPE_SCENE_HIGHP float envelope = t * (1.0 - t) * 4.0;\n"
        "        p.z += sin (t * 11.0 - uTime * 6.0) * uFlutter * 0.030 * envelope;\n"
        "        p.y += sin (t *  7.0 + uTime * 4.0) * uFlutter * 0.026 * envelope;\n"
        "        vDetail = t * 40.0;\n"
        "    }\n"
        "    else if (uMode > 2.5)\n"
        "    {\n"
        "        // The one mode with a lifetime: the glow under a working knob\n"
        "        // breathes on the wall clock rather than on the transport, so it is\n"
        "        // the one place uTime reaches a vertex.\n"
        "        p.z += sin (uTime * 2.6) * 0.004 * aPosition.w;\n"
        "    }\n"
        "\n"
        "    vNormal = mat3 (uModel) * aNormal;\n"
        "    gl_Position = uViewProjection * uModel * vec4 (p, 1.0);\n"
        "}\n";

    const char* const fragmentShaderSource =
        "TAPE_SCENE_HIGHP varying vec3 vNormal;\n"
        "TAPE_SCENE_HIGHP varying float vDetail;\n"
        "TAPE_SCENE_HIGHP varying vec2 vLocal;\n"
        "\n"
        "uniform vec3 uBaseColour;\n"
        "uniform vec3 uHighlightColour;\n"
        "uniform vec3 uLightDirection;\n"
        "uniform float uGlow;\n"
        "uniform float uDrive;\n"
        "uniform float uMode;\n"
        "uniform float uTime;\n"
        "uniform float uArcAngle;\n"
        "uniform float uKeyDown;\n"
        "uniform vec3 uPanelHalf;\n"
        "uniform float uLabelHalfWidth;\n"
        "uniform float uLabelBaseY;\n"
        "uniform float uLabelSize;\n"
        "uniform int uLabelLength;\n"
        "uniform int uLabel[8];\n"
        "\n"
        "// ---------------------------------------------------------------------------\n"
        "//  A procedural alphabet.\n"
        "//\n"
        "//  The captions under the controls are drawn HERE, in the shader, rather than\n"
        "//  by a juce::Label over the window. A 2D label sits at a fixed screen\n"
        "//  position; the bank it names moves with the camera, so the two would part\n"
        "//  company the moment the window changed shape - which is the whole reason\n"
        "//  this component draws its own picture instead of being a backdrop for the\n"
        "//  panel.\n"
        "//\n"
        "//  Each glyph is a 5 x 7 bitmask, one bit per cell, packed into a single int:\n"
        "//  cell (x, y) is bit (y * 5 + x), x left to right and y top to bottom. The\n"
        "//  alphabet is the subset a control caption needs - A to Z, 0 to 9, and the\n"
        "//  four separators - and nothing else, because every glyph is sixteen lines\n"
        "//  of geometry here and a full ASCII set would be three hundred.\n"
        "//\n"
        "//  The ints arrive as uniforms and are indexed dynamically, which is why this\n"
        "//  is written in the oldest dialect that runs everywhere: an array of ints\n"
        "//  indexed by a loop variable needs no sampler and no texture unit, and it\n"
        "//  compiles as GLSL 1.10 on a legacy context and as 1.50 after JUCE's own\n"
        "//  translators on a core profile.\n"
        "// ---------------------------------------------------------------------------\n"
        "int glyphBits (int glyph)\n"
        "{\n"
        "    if (glyph < 0)\n"
        "        return 0;\n"
        "\n"
        "    if (glyph < 26)\n"
        "    {\n"
        "        if (glyph ==  0) return 7298027;   // A\n"
        "        if (glyph ==  1) return 14259331;  // B\n"
        "        if (glyph ==  2) return 15901074;  // C\n"
        "        if (glyph ==  3) return 14764563;  // D\n"
        "        if (glyph ==  4) return 3150769;   // E\n"
        "        if (glyph ==  5) return 3149889;   // F\n"
        "        if (glyph ==  6) return 15902578;  // G\n"
        "        if (glyph ==  7) return 14815374;  // H\n"
        "        if (glyph ==  8) return 3171189;   // I\n"
        "        if (glyph ==  9) return 7889202;   // J\n"
        "        if (glyph == 10) return 14821742;  // K\n"
        "        if (glyph == 11) return 2105377;   // L\n"
"        "        if (glyph == 12) return 19952798;  // M\n"
        "        if (glyph == 13) return 19071646;  // N\n"
        "        if (glyph == 14) return 15858106;  // O\n"
        "        if (glyph == 15) return 14703234;  // P\n"
        "        if (glyph == 16) return 17905338;  // Q\n"
        "        if (glyph == 17) return 14830706;  // R\n"
        "        if (glyph == 18) return 1085474;   // S\n"
        "        if (glyph == 19) return 3171188;   // T\n"
        "        if (glyph == 20) return 7895166;   // U\n"
        "        if (glyph == 21) return 5147996;   // V\n"
        "        if (glyph == 22) return 19069502;  // W\n"
        "        if (glyph == 23) return 14793372;  // X\n"
        "        if (glyph == 24) return 7894892;   // Y\n"
        "        if (glyph == 25) return 6287716;   // Z\n"
        "    }\n"
        "\n"
        "    if (glyph < 36)\n"
        "    {\n"
        "        if (glyph == 26) return 15845338;  // 0\n"
        "        if (glyph == 27) return 3170890;   // 1\n"
        "        if (glyph == 28) return 13485026;  // 2\n"
        "        if (glyph == 29) return 13964194;  // 3\n"
        "        if (glyph == 30) return 3248702;   // 4\n"
        "        if (glyph == 31) return 6834946;   // 5\n"
        "        if (glyph == 32) return 15843362;  // 6\n"
        "        if (glyph == 33) return 6291714;   // 7\n"
        "        if (glyph == 34) return 15859490;  // 8\n"
        "        if (glyph == 35) return 7366162;   // 9\n"
        "    }\n"
        "\n"
        "    if (glyph == 36) return 2105376;      // -\n"
        "    if (glyph == 37) return 3171;         // .\n"
        "    if (glyph == 38) return 67604;        // /\n"
"        "    if (glyph == 39) return 1441792;      // :\n"
        "\n"
        "    return 0;\n"
        "}\n"
        "\n"
        "float glyphCoverage (int glyph, vec2 cell)\n"
        "{\n"
        "    if (cell.x < 0.0 || cell.x > 5.0 || cell.y < 0.0 || cell.y > 7.0)\n"
        "        return 0.0;\n"
        "\n"
        "    int x = int (floor (cell.x));\n"
        "    int y = int (floor (cell.y));\n"
        "\n"
        "    int bits = glyphBits (glyph);\n"
        "    int mask = 1 << (y * 5 + x);\n"
        "\n"
        "    return (bits & mask) != 0 ? 1.0 : 0.0;\n"
        "}\n"
        "\n"
        "float captionCoverage (vec2 local)\n"
        "{\n"
        "    if (uLabelLength <= 0)\n"
        "        return 0.0;\n"
        "\n"
        "    TAPE_SCENE_HIGHP float y = (local.y - uLabelBaseY) / uLabelSize;\n"
        "    if (y < 0.0 || y > 7.0)\n"
        "        return 0.0;\n"
        "\n"
        "    TAPE_SCENE_HIGHP float x = (local.x + uLabelHalfWidth) / uLabelSize;\n"
        "    if (x < 0.0)\n"
        "        return 0.0;\n"
        "\n"
        "    // Six columns per character: five of glyph and one of gap, so a caption\n"
        "    // reads as words rather than as a solid bar.\n"
        "    int index = int (floor (x / 6.0));\n"
        "    if (index >= uLabelLength)\n"
        "        return 0.0;\n"
        "\n"
        "    int glyph = 0;\n"
        "    for (int i = 0; i < 8; ++i)\n"
        "        if (i == index)\n"
        "            glyph = uLabel[i];\n"
        "\n"
        "    TAPE_SCENE_HIGHP float cellX = mod (x, 6.0);\n"
        "    return glyphCoverage (glyph, vec2 (cellX, y));\n"
        "}\n"
        "\n"
        "void main()\n"
        "{\n"
        "    TAPE_SCENE_HIGHP vec3 n = normalize (vNormal);\n"
        "    TAPE_SCENE_HIGHP vec3 l = normalize (uLightDirection);\n"
        "    TAPE_SCENE_HIGHP vec3 eye = vec3 (0.0, 0.0, 1.0);\n"
        "    TAPE_SCENE_HIGHP vec3 h = normalize (l + eye);\n"
        "\n"
        "    TAPE_SCENE_HIGHP float diffuse = max (dot (n, l), 0.0);\n"
        "    TAPE_SCENE_HIGHP float specular = pow (max (dot (n, h), 0.0), 38.0);\n"
        "    TAPE_SCENE_HIGHP float rim = pow (1.0 - max (dot (n, eye), 0.0), 3.0);\n"
        "\n"
        "    TAPE_SCENE_HIGHP vec3 colour = uBaseColour * (0.20 + 0.90 * diffuse);\n"
        "    colour += uHighlightColour * specular * 0.70;\n"
        "    colour += uHighlightColour * rim * (0.10 + 0.50 * uGlow);\n"
        "\n"
        "    if (uMode < 0.5)\n"
        "    {\n"
        "        // The transport: the accent tint that follows DRIVE, then the detail\n"
        "        // bands, which cost a fract and save every texture.\n"
        "        colour = mix (colour, uHighlightColour, 0.20 * uDrive) * (1.0 + 0.30 * uDrive);\n"
        "\n"
        "        TAPE_SCENE_HIGHP float band = 0.88\n"
        "                                + 0.12 * smoothstep (0.18, 0.50, abs (fract (vDetail) - 0.5));\n"
        "        gl_FragColor = vec4 (colour * band, 1.0);\n"
        "        return;\n"
        "    }\n"
        "\n"
        "    if (uMode < 3.5)\n"
        "    {\n"
        "        // The control bank's body: a machined face with a brushed grain. The\n"
        "        // grain runs along x and is one line of arithmetic - what makes a\n"
        "        // flat plate read as metal rather than as a coloured rectangle is\n"
        "        // that its highlight is not perfectly uniform.\n"
        "        TAPE_SCENE_HIGHP float brush = 0.965\n"
        "                                 + 0.035 * abs (fract (vLocal.y * 46.0) - 0.5) * 2.0;\n"
        "        gl_FragColor = vec4 (colour * brush, 1.0);\n"
        "        return;\n"
        "    }\n"
        "\n"
        "    if (uMode < 4.5)\n"
        "    {\n"
        "        // A knob. The same two lights as everything else, plus a sheen that\n"
        "        // tracks the index mark - so the highlight walks round the knurl as\n"
        "        // the knob turns, which is how a knob reads as turning even when the\n"
        "        // mark is a few pixels long.\n"
        "        TAPE_SCENE_HIGHP float sheen = pow (max (dot (n, h), 0.0), 9.0);\n"
        "        colour += uHighlightColour * sheen * 0.16;\n"
        "        colour *= 1.0 - 0.10 * max (0.0, -n.z);\n"
        "\n"
        "        // The lit arc, in the knob's own frame: vLocal is the vertex's\n        "        // position BEFORE the model matrix, so the arc turns with the knob\n"
        "        // without a second uniform or a second draw.\n"
        "        TAPE_SCENE_HIGHP float r = length (vLocal);\n"
        "        TAPE_SCENE_HIGHP float angle = atan (vLocal.x, vLocal.y);\n"
        "        TAPE_SCENE_HIGHP float t = clamp ((angle - uArcAngle) / 0.30, 0.0, 1.0);\n"
        "        TAPE_SCENE_HIGHP float arc = smoothstep (0.70, 0.80, r) * (1.0 - smoothstep (0.90, 0.99, r));\n"
        "        colour += uHighlightColour * arc * (1.0 - t) * (0.35 + 0.65 * uGlow);\n"
        "\n"
        "        gl_FragColor = vec4 (colour, 1.0);\n"
        "        return;\n"
        "    }\n"
        "\n"
        "    if (uMode < 5.5)\n"
        "    {\n"
        "        // A keycap. Pressed, it sinks: the uniform moves the model down and\n"
        "        // this darkens the cap that bit further, so a press reads as a press\n"
        "        // rather than as the whole scene dropping a millimetre.\n"
        "        colour *= 1.0 - 0.35 * uKeyDown;\n"
        "        gl_FragColor = vec4 (colour, 1.0);\n"
        "        return;\n"
        "    }\n"
        "\n"
        "    // The face the bank is set into, and the captions on it. The captions are\n"
        "    // a shade of the panel's own text colour rather than a colour of their\n        "    // own, so they belong to the theme like everything else - and they are\n"
        "    // written on a plate: a caption over a bare brushed face at this size is\n        "
        "    // unreadable, and the plate is what makes it an engraved label.\n"
        "    TAPE_SCENE_HIGHP vec2 uv = vLocal / uPanelHalf;\n"
        "\n"
        "    TAPE_SCENE_HIGHP float plateY = (vLocal.y - uLabelBaseY - 3.5 * uLabelSize)\n"
        "                                 / (uLabelSize * 5.4);\n"
        "    TAPE_SCENE_HIGHP float plateX = vLocal.x / (uLabelHalfWidth + uLabelSize * 0.9);\n"
        "    if (abs (plateY) < 1.0 && abs (plateX) < 1.0)\n"
        "    {\n"
        "        TAPE_SCENE_HIGHP float plate = smoothstep (1.0, 0.86, abs (plateX))\n"
        "                                    * smoothstep (1.0, 0.55, abs (plateY));\n"
        "        colour = mix (colour, uBaseColour * 0.62, plate * 0.85);\n"
        "\n"
        "        TAPE_SCENE_HIGHP float ink = captionCoverage (vLocal);\n"
        "        colour = mix (colour, uHighlightColour, ink * 0.80);\n"
        "    }\n"
        "\n"
        "    // A soft vignette on the plate, so the bank's edges fall away rather than\n"
        "    // ending on a hard line under the machine's front.\n"
        "    colour *= 1.0 - 0.28 * smoothstep (0.35, 1.15, length (uv));\n"
        "\n"
        "    gl_FragColor = vec4 (colour, 1.0);\n"
        "}\n";

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
    // editor goes away.
    repaint();
}

void TapeScene::setControlValues (const float* values, int count) noexcept
{
    // The knobs and their captions come from the same list, so the count is
    // clamped once against both. A caller that hands over fewer than the bank
    // holds leaves the rest at the panel's rest position rather than at whatever
    // the previous frame put there, which is what makes this safe to call with a
    // partial list instead of something that has to be got exactly right or it
    // shows a stale angle.
    const auto usable = juce::jlimit (0, bankKnobCount, count);

    for (int i = 0; i < bankKnobCount; ++i)
        knobValues[static_cast<std::size_t> (i)].store (i < usable ? values[i] : 0.5f,
                                                        std::memory_order_relaxed);

    repaint();
}

void TapeScene::setKnobCaptions (const juce::String* captions, int count) noexcept
{
    // Safe to call from the message thread only, and it is: the editor calls it
    // when the tab changes, never per frame. A String and the UTF-8 conversion
    // below are exactly the kind of work the render thread must not be asked to
    // do, which is why the result is published as a fixed byte buffer rather
    // than as a String.
    const auto usable = juce::jlimit (0, bankKnobCount, count);

    for (int i = 0; i < bankKnobCount; ++i)
    {
        auto& bytes = captionBytes[static_cast<std::size_t> (i)];
        std::memset (bytes.data(), 0, bytes.size());

        if (i >= usable)
            continue;

        // The GL thread reads this buffer once per frame, so the bytes are
        // written BEFORE the length that publishes them: the store below is a
        // release and the load in renderOpenGL is an acquire. Both are atomics,
        // and this is the one ordering in the class that matters - it is the
        // standard publish-a-buffer pattern, and without it a caption can be
        // read half-written.
        const auto text = captions[i].toUTF8();
        const auto length = juce::jmin (static_cast<int> (text.length()),
                                        static_cast<int> (bytes.size()) - 1);

        if (length <= 0)
        {
            captionLengths[static_cast<std::size_t> (i)].store (0, std::memory_order_release);
            continue;
        }

        std::memcpy (bytes.data(), text.getAddress(), static_cast<std::size_t> (length));
        captionLengths[static_cast<std::size_t> (i)].store (length, std::memory_order_release);
    }

    repaint();
}

void TapeScene::setControlValues (const float* values, int count) noexcept
{
    // The knobs and their captions come from the same list, so the count is
    // clamped once against both. A caller that hands over fewer than the panel
    // has leaves the rest of the bank at the panel's rest position rather than
    // at whatever the previous frame put there - which is what makes this safe
    // to call with a partial list rather than something that has to be got
    // exactly right or it shows a stale angle.
    const auto usable = juce::jlimit (0, bankKnobCount, count);

    for (int i = 0; i < bankKnobCount; ++i)
        knobValues[static_cast<std::size_t> (i)].store (i < usable ? values[i] : 0.5f,
                                                        std::memory_order_relaxed);

    repaint();
}

void TapeScene::setKnobCaptions (const juce::String* captions, int count) noexcept
{
    // Safe to call from the message thread only, and it is: the editor calls it
    // when the tab changes, never per frame. A String allocation and the
    // conversion to UTF-8 below are exactly the kind of work the render thread
    // must not be asked to do, which is why the result is published as a fixed
    // byte buffer instead of as a String.
    const auto usable = juce::jlimit (0, bankKnobCount, count);

    for (int i = 0; i < bankKnobCount; ++i)
    {
        std::memset (captionBytes[static_cast<std::size_t> (i)].data(), 0,
                     captionBytes[static_cast<std::size_t> (i)].size());

        if (i >= usable)
            continue;

        // The GL thread reads this buffer once per frame, so the write has to be
        // released BEFORE the length that publishes it. Both are atomics; this
        // is the one ordering in the class that matters, and it is the standard
        // publish-a-buffer pattern.
        const auto text = captions[i].toUTF8();
        const auto bytes = juce::jmin (static_cast<int> (text.length()),
                                       captionBytes[static_cast<std::size_t> (i)].size() - 1);

        if (bytes <= 0)
            continue;

        std::memcpy (captionBytes[static_cast<std::size_t> (i)].data(), text.getAddress(),
                     static_cast<std::size_t> (bytes));

        captionLengths[static_cast<std::size_t> (i)].store (bytes, std::memory_order_release);
    }

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

void TapeScene::setSceneEnabled (bool shouldBeEnabled)
{
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
    if (! sceneEnabled)
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

void TapeScene::resized()
{
    // Nothing to place: the projection is derived from the current size in
    // renderOpenGL(), and JUCE sets the viewport to this component's bounds
    // before every frame. Asking for one is enough to redraw at the new size.
    repaint();
}

bool TapeScene::isSceneLive() const noexcept
{
    return sceneEnabled && programLinked.load() != 0 && openGLContext.isAttached();
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
    //  single translation and the three meshes it draws with share that one
    //  matrix.
    // -------------------------------------------------------------------------
    addBox (bank.body.vertices, bank.body.indices,
            { 0.0f, 0.0f, 0.0f },
            { bankHalfWidth, bankHalfHeight, bankHalfDepth });

    // -------------------------------------------------------------------------
    //  One knob, built round its own origin: a machined barrel with a knurled
    //  grip, a slightly domed face, and a pointer set into it.
    //
    //  Every radius below is a FRACTION of the knob's radius rather than a length,
    //  because the same mesh is drawn at five different scales - a knob and a
    //  keycap are the same two meshes at different sizes, which is what keeps a
    //  change to the knob a change to every knob on the panel.
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
    // pixels across, the difference is invisible and the rings cost a fifth of
    // the vertices. Each ring is lifted by the sagitta of the cap it approximates,
    // so the dome's silhouette is a circle rather than a cone.
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
            { 0.0f, 0.0f, 0.78f },
            { 0.72f, 0.62f, 0.30f });

    // -------------------------------------------------------------------------
    //  The lit disc: what a knob lays on the face when the machine is working.
    //  A disc of its own rather than a colour change on the body, because the
    //  glow is additive - it is drawn with blending on, over whatever is behind
    //  it, so it can bleed past the knob's rim without a second geometry pass.
    // -------------------------------------------------------------------------
    addDisc (bank.knobGlow.vertices, bank.knobGlow.indices,
             { 0.0f, 0.0f, 0.0f }, 1.0f, 0.0005f, segments);

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
    // more here than the handful of redundant calls a 66 px column costs.
    glBindBuffer (GL_ARRAY_BUFFER, mesh.vertexBuffer);
    glVertexAttribPointer (static_cast<GLuint> (positionLocation), 4, GL_FLOAT, GL_FALSE,
                           vertexStride, nullptr);
    glVertexAttribPointer (static_cast<GLuint> (normalLocation), 3, GL_FLOAT, GL_FALSE,
                           vertexStride, reinterpret_cast<const void*> (normalOffset));
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

    // -------------------------------------------------------------------------
    //  What the machine is doing, read once. Each of the five is a single atomic
    //  load, which is why this can be called on the render thread at all.
    // -------------------------------------------------------------------------
    const auto level = outputLevel.load (std::memory_order_relaxed);
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

    supplyAngle = std::fmod (supplyAngle + static_cast<float> (frameSeconds) * 2.6f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));
    takeUpAngle = std::fmod (takeUpAngle + static_cast<float> (frameSeconds) * 2.3f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));

    if (speed > 0.001f)
    {
        // Tape off the supply reel and onto the take-up one, faster when there
        // is signal to record. It is the one number in the scene that has to
        // remember anything, which is why it is a member and not a uniform.
        tapeTransfer = juce::jlimit (0.0f, 1.0f,
                                     tapeTransfer + static_cast<float> (frameSeconds)
                                       * (0.010f + 0.075f * level));
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

    // -------------------------------------------------------------------------
    //  Draw state. The clear takes the colour and the depth, and the background
    //  is the panel's own so the window reads as a recess rather than as a hole.
    // -------------------------------------------------------------------------
    juce::OpenGLHelpers::clear (background);

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

    const auto drawReel = [this, &scene, &body, &tape] (float centreX, float centreY, float centreZ,
                                                        float radius, float angle, float packOuter)
    {
        const auto model = scene
                         * glm::translate (glm::mat4 (1.0f), { centreX, centreY, centreZ })
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

    drawReel (supplyCentreX, supplyCentreY, 0.00f, supplyRadius, supplyAngle, supplyPackOuter);
    drawReel (takeUpCentreX, takeUpCentreY, 0.00f, takeUpRadius, takeUpAngle, takeUpPackOuter);

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
    const auto ribbonModel = scene;
    shaderProgram->setUniformMat4 ("uModel", glm::value_ptr (ribbonModel), 1, GL_FALSE);
    shaderProgram->setUniform ("uMode", static_cast<float> (ribbonMode));
    shaderProgram->setUniform ("uBaseColour", tape.getFloatRed() * 1.35f + 0.02f,
                               tape.getFloatGreen() * 1.35f + 0.02f,
                               tape.getFloatBlue() * 1.35f + 0.02f);
    drawMesh (ribbon);
}
    }

    if (programLinked.load() != 0)
    {
        uploadMesh (reel, buildReel());
        uploadMesh (tapePack, buildTapePack());
        uploadMesh (ribbon, buildRibbon());
        uploadMesh (head, buildHead());
    }
    else
    {
        releaseMesh (reel);
        releaseMesh (tapePack);
        releaseMesh (ribbon);
        releaseMesh (head);
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

    // -------------------------------------------------------------------------
    //  What the machine is doing, read once. Each of the five is a single atomic
    //  load, which is why this can be called on the render thread at all.
    // -------------------------------------------------------------------------
    const auto level = outputLevel.load (std::memory_order_relaxed);
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

    supplyAngle = std::fmod (supplyAngle + static_cast<float> (frameSeconds) * 2.6f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));
    takeUpAngle = std::fmod (takeUpAngle + static_cast<float> (frameSeconds) * 2.3f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));

    if (speed > 0.001f)
    {
        // Tape off the supply reel and onto the take-up one, faster when there
        // is signal to record. It is the one number in the scene that has to
        // remember anything, which is why it is a member and not a uniform.
        tapeTransfer = juce::jlimit (0.0f, 1.0f,
                                     tapeTransfer + static_cast<float> (frameSeconds)
                                       * (0.010f + 0.075f * level));
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

    // -------------------------------------------------------------------------
    //  Draw state. The clear takes the colour and the depth, and the background
    //  is the panel's own so the window reads as a recess rather than as a hole.
    // -------------------------------------------------------------------------
    juce::OpenGLHelpers::clear (background);

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

    const auto drawReel = [this, &scene, &body, &tape] (float centreX, float centreY, float centreZ,
                                                        float radius, float angle, float packOuter)
    {
        const auto model = scene
                         * glm::translate (glm::mat4 (1.0f), { centreX, centreY, centreZ })
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

    drawReel (supplyCentreX, supplyCentreY, 0.00f, supplyRadius, supplyAngle, supplyPackOuter);
    drawReel (takeUpCentreX, takeUpCentreY, 0.00f, takeUpRadius, takeUpAngle, takeUpPackOuter);

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
    const auto ribbonModel = scene;
    shaderProgram->setUniformMat4 ("uModel", glm::value_ptr (ribbonModel), 1, GL_FALSE);
    shaderProgram->setUniform ("uMode", static_cast<float> (ribbonMode));
    shaderProgram->setUniform ("uBaseColour", tape.getFloatRed() * 1.35f + 0.02f,
                               tape.getFloatGreen() * 1.35f + 0.02f,
                               tape.getFloatBlue() * 1.35f + 0.02f);
    drawMesh (ribbon);
}
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
    // more here than the handful of redundant calls a 66 px column costs.
    glBindBuffer (GL_ARRAY_BUFFER, mesh.vertexBuffer);
    glVertexAttribPointer (static_cast<GLuint> (positionLocation), 4, GL_FLOAT, GL_FALSE,
                           vertexStride, nullptr);
    glVertexAttribPointer (static_cast<GLuint> (normalLocation), 3, GL_FLOAT, GL_FALSE,
                           vertexStride, reinterpret_cast<const void*> (normalOffset));
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
    }
    else
    {
        releaseMesh (reel);
        releaseMesh (tapePack);
        releaseMesh (ribbon);
        releaseMesh (head);
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

    // -------------------------------------------------------------------------
    //  What the machine is doing, read once. Each of the five is a single atomic
    //  load, which is why this can be called on the render thread at all.
    // -------------------------------------------------------------------------
    const auto level = outputLevel.load (std::memory_order_relaxed);
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

    supplyAngle = std::fmod (supplyAngle + static_cast<float> (frameSeconds) * 2.6f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));
    takeUpAngle = std::fmod (takeUpAngle + static_cast<float> (frameSeconds) * 2.3f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));

    if (speed > 0.001f)
    {
        // Tape off the supply reel and onto the take-up one, faster when there
        // is signal to record. It is the one number in the scene that has to
        // remember anything, which is why it is a member and not a uniform.
        tapeTransfer = juce::jlimit (0.0f, 1.0f,
                                     tapeTransfer + static_cast<float> (frameSeconds)
                                       * (0.010f + 0.075f * level));
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

    // -------------------------------------------------------------------------
    //  Draw state. The clear takes the colour and the depth, and the background
    //  is the panel's own so the window reads as a recess rather than as a hole.
    // -------------------------------------------------------------------------
    juce::OpenGLHelpers::clear (background);

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

    const auto drawReel = [this, &scene, &body, &tape] (float centreX, float centreY, float centreZ,
                                                        float radius, float angle, float packOuter)
    {
        const auto model = scene
                         * glm::translate (glm::mat4 (1.0f), { centreX, centreY, centreZ })
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

    drawReel (supplyCentreX, supplyCentreY, 0.00f, supplyRadius, supplyAngle, supplyPackOuter);
    drawReel (takeUpCentreX, takeUpCentreY, 0.00f, takeUpRadius, takeUpAngle, takeUpPackOuter);

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
    const auto ribbonModel = scene;
    shaderProgram->setUniformMat4 ("uModel", glm::value_ptr (ribbonModel), 1, GL_FALSE);
    shaderProgram->setUniform ("uMode", static_cast<float> (ribbonMode));
    shaderProgram->setUniform ("uBaseColour", tape.getFloatRed() * 1.35f + 0.02f,
                               tape.getFloatGreen() * 1.35f + 0.02f,
                               tape.getFloatBlue() * 1.35f + 0.02f);
    drawMesh (ribbon);
}
    }

    if (programLinked.load() != 0)
    {
        uploadMesh (reel, buildReel());
        uploadMesh (tapePack, buildTapePack());
        uploadMesh (ribbon, buildRibbon());
        uploadMesh (head, buildHead());
    }
    else
    {
        releaseMesh (reel);
        releaseMesh (tapePack);
        releaseMesh (ribbon);
        releaseMesh (head);
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

    // -------------------------------------------------------------------------
    //  What the machine is doing, read once. Each of the five is a single atomic
    //  load, which is why this can be called on the render thread at all.
    // -------------------------------------------------------------------------
    const auto level = outputLevel.load (std::memory_order_relaxed);
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

    supplyAngle = std::fmod (supplyAngle + static_cast<float> (frameSeconds) * 2.6f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));
    takeUpAngle = std::fmod (takeUpAngle + static_cast<float> (frameSeconds) * 2.3f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));

    if (speed > 0.001f)
    {
        // Tape off the supply reel and onto the take-up one, faster when there
        // is signal to record. It is the one number in the scene that has to
        // remember anything, which is why it is a member and not a uniform.
        tapeTransfer = juce::jlimit (0.0f, 1.0f,
                                     tapeTransfer + static_cast<float> (frameSeconds)
                                       * (0.010f + 0.075f * level));
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

    // -------------------------------------------------------------------------
    //  Draw state. The clear takes the colour and the depth, and the background
    //  is the panel's own so the window reads as a recess rather than as a hole.
    // -------------------------------------------------------------------------
    juce::OpenGLHelpers::clear (background);

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

    const auto drawReel = [this, &scene, &body, &tape] (float centreX, float centreY, float centreZ,
                                                        float radius, float angle, float packOuter)
    {
        const auto model = scene
                         * glm::translate (glm::mat4 (1.0f), { centreX, centreY, centreZ })
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

    drawReel (supplyCentreX, supplyCentreY, 0.00f, supplyRadius, supplyAngle, supplyPackOuter);
    drawReel (takeUpCentreX, takeUpCentreY, 0.00f, takeUpRadius, takeUpAngle, takeUpPackOuter);

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
    const auto ribbonModel = scene;
    shaderProgram->setUniformMat4 ("uModel", glm::value_ptr (ribbonModel), 1, GL_FALSE);
    shaderProgram->setUniform ("uMode", static_cast<float> (ribbonMode));
    shaderProgram->setUniform ("uBaseColour", tape.getFloatRed() * 1.35f + 0.02f,
                               tape.getFloatGreen() * 1.35f + 0.02f,
                               tape.getFloatBlue() * 1.35f + 0.02f);
    drawMesh (ribbon);
}
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
    // more here than the handful of redundant calls a 66 px column costs.
    glBindBuffer (GL_ARRAY_BUFFER, mesh.vertexBuffer);
    glVertexAttribPointer (static_cast<GLuint> (positionLocation), 4, GL_FLOAT, GL_FALSE,
                           vertexStride, nullptr);
    glVertexAttribPointer (static_cast<GLuint> (normalLocation), 3, GL_FLOAT, GL_FALSE,
                           vertexStride, reinterpret_cast<const void*> (normalOffset));
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

    // -------------------------------------------------------------------------
    //  What the machine is doing, read once. Each of the five is a single atomic
    //  load, which is why this can be called on the render thread at all.
    // -------------------------------------------------------------------------
    const auto level = outputLevel.load (std::memory_order_relaxed);
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

    supplyAngle = std::fmod (supplyAngle + static_cast<float> (frameSeconds) * 2.6f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));
    takeUpAngle = std::fmod (takeUpAngle + static_cast<float> (frameSeconds) * 2.3f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));

    if (speed > 0.001f)
    {
        // Tape off the supply reel and onto the take-up one, faster when there
        // is signal to record. It is the one number in the scene that has to
        // remember anything, which is why it is a member and not a uniform.
        tapeTransfer = juce::jlimit (0.0f, 1.0f,
                                     tapeTransfer + static_cast<float> (frameSeconds)
                                       * (0.010f + 0.075f * level));
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

    // -------------------------------------------------------------------------
    //  Draw state. The clear takes the colour and the depth, and the background
    //  is the panel's own so the window reads as a recess rather than as a hole.
    // -------------------------------------------------------------------------
    juce::OpenGLHelpers::clear (background);

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

    const auto drawReel = [this, &scene, &body, &tape] (float centreX, float centreY, float centreZ,
                                                        float radius, float angle, float packOuter)
    {
        const auto model = scene
                         * glm::translate (glm::mat4 (1.0f), { centreX, centreY, centreZ })
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

    drawReel (supplyCentreX, supplyCentreY, 0.00f, supplyRadius, supplyAngle, supplyPackOuter);
    drawReel (takeUpCentreX, takeUpCentreY, 0.00f, takeUpRadius, takeUpAngle, takeUpPackOuter);

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
    const auto ribbonModel = scene;
    shaderProgram->setUniformMat4 ("uModel", glm::value_ptr (ribbonModel), 1, GL_FALSE);
    shaderProgram->setUniform ("uMode", static_cast<float> (ribbonMode));
    shaderProgram->setUniform ("uBaseColour", tape.getFloatRed() * 1.35f + 0.02f,
                               tape.getFloatGreen() * 1.35f + 0.02f,
                               tape.getFloatBlue() * 1.35f + 0.02f);
    drawMesh (ribbon);
}
    }

    if (programLinked.load() != 0)
    {
        uploadMesh (reel, buildReel());
        uploadMesh (tapePack, buildTapePack());
        uploadMesh (ribbon, buildRibbon());
        uploadMesh (head, buildHead());
    }
    else
    {
        releaseMesh (reel);
        releaseMesh (tapePack);
        releaseMesh (ribbon);
        releaseMesh (head);
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

    // -------------------------------------------------------------------------
    //  What the machine is doing, read once. Each of the five is a single atomic
    //  load, which is why this can be called on the render thread at all.
    // -------------------------------------------------------------------------
    const auto level = outputLevel.load (std::memory_order_relaxed);
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

    supplyAngle = std::fmod (supplyAngle + static_cast<float> (frameSeconds) * 2.6f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));
    takeUpAngle = std::fmod (takeUpAngle + static_cast<float> (frameSeconds) * 2.3f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));

    if (speed > 0.001f)
    {
        // Tape off the supply reel and onto the take-up one, faster when there
        // is signal to record. It is the one number in the scene that has to
        // remember anything, which is why it is a member and not a uniform.
        tapeTransfer = juce::jlimit (0.0f, 1.0f,
                                     tapeTransfer + static_cast<float> (frameSeconds)
                                       * (0.010f + 0.075f * level));
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

    // -------------------------------------------------------------------------
    //  Draw state. The clear takes the colour and the depth, and the background
    //  is the panel's own so the window reads as a recess rather than as a hole.
    // -------------------------------------------------------------------------
    juce::OpenGLHelpers::clear (background);

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

    const auto drawReel = [this, &scene, &body, &tape] (float centreX, float centreY, float centreZ,
                                                        float radius, float angle, float packOuter)
    {
        const auto model = scene
                         * glm::translate (glm::mat4 (1.0f), { centreX, centreY, centreZ })
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

    drawReel (supplyCentreX, supplyCentreY, 0.00f, supplyRadius, supplyAngle, supplyPackOuter);
    drawReel (takeUpCentreX, takeUpCentreY, 0.00f, takeUpRadius, takeUpAngle, takeUpPackOuter);

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
    const auto ribbonModel = scene;
    shaderProgram->setUniformMat4 ("uModel", glm::value_ptr (ribbonModel), 1, GL_FALSE);
    shaderProgram->setUniform ("uMode", static_cast<float> (ribbonMode));
    shaderProgram->setUniform ("uBaseColour", tape.getFloatRed() * 1.35f + 0.02f,
                               tape.getFloatGreen() * 1.35f + 0.02f,
                               tape.getFloatBlue() * 1.35f + 0.02f);
    drawMesh (ribbon);
}
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
    // more here than the handful of redundant calls a 66 px column costs.
    glBindBuffer (GL_ARRAY_BUFFER, mesh.vertexBuffer);
    glVertexAttribPointer (static_cast<GLuint> (positionLocation), 4, GL_FLOAT, GL_FALSE,
                           vertexStride, nullptr);
    glVertexAttribPointer (static_cast<GLuint> (normalLocation), 3, GL_FLOAT, GL_FALSE,
                           vertexStride, reinterpret_cast<const void*> (normalOffset));
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
    }
    else
    {
        releaseMesh (reel);
        releaseMesh (tapePack);
        releaseMesh (ribbon);
        releaseMesh (head);
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

    // -------------------------------------------------------------------------
    //  What the machine is doing, read once. Each of the five is a single atomic
    //  load, which is why this can be called on the render thread at all.
    // -------------------------------------------------------------------------
    const auto level = outputLevel.load (std::memory_order_relaxed);
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

    supplyAngle = std::fmod (supplyAngle + static_cast<float> (frameSeconds) * 2.6f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));
    takeUpAngle = std::fmod (takeUpAngle + static_cast<float> (frameSeconds) * 2.3f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));

    if (speed > 0.001f)
    {
        // Tape off the supply reel and onto the take-up one, faster when there
        // is signal to record. It is the one number in the scene that has to
        // remember anything, which is why it is a member and not a uniform.
        tapeTransfer = juce::jlimit (0.0f, 1.0f,
                                     tapeTransfer + static_cast<float> (frameSeconds)
                                       * (0.010f + 0.075f * level));
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

    // -------------------------------------------------------------------------
    //  Draw state. The clear takes the colour and the depth, and the background
    //  is the panel's own so the window reads as a recess rather than as a hole.
    // -------------------------------------------------------------------------
    juce::OpenGLHelpers::clear (background);

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

    const auto drawReel = [this, &scene, &body, &tape] (float centreX, float centreY, float centreZ,
                                                        float radius, float angle, float packOuter)
    {
        const auto model = scene
                         * glm::translate (glm::mat4 (1.0f), { centreX, centreY, centreZ })
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

    drawReel (supplyCentreX, supplyCentreY, 0.00f, supplyRadius, supplyAngle, supplyPackOuter);
    drawReel (takeUpCentreX, takeUpCentreY, 0.00f, takeUpRadius, takeUpAngle, takeUpPackOuter);

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
    const auto ribbonModel = scene;
    shaderProgram->setUniformMat4 ("uModel", glm::value_ptr (ribbonModel), 1, GL_FALSE);
    shaderProgram->setUniform ("uMode", static_cast<float> (ribbonMode));
    shaderProgram->setUniform ("uBaseColour", tape.getFloatRed() * 1.35f + 0.02f,
                               tape.getFloatGreen() * 1.35f + 0.02f,
                               tape.getFloatBlue() * 1.35f + 0.02f);
    drawMesh (ribbon);
}
    }

    if (programLinked.load() != 0)
    {
        uploadMesh (reel, buildReel());
        uploadMesh (tapePack, buildTapePack());
        uploadMesh (ribbon, buildRibbon());
        uploadMesh (head, buildHead());
    }
    else
    {
        releaseMesh (reel);
        releaseMesh (tapePack);
        releaseMesh (ribbon);
        releaseMesh (head);
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

    // -------------------------------------------------------------------------
    //  What the machine is doing, read once. Each of the five is a single atomic
    //  load, which is why this can be called on the render thread at all.
    // -------------------------------------------------------------------------
    const auto level = outputLevel.load (std::memory_order_relaxed);
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

    supplyAngle = std::fmod (supplyAngle + static_cast<float> (frameSeconds) * 2.6f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));
    takeUpAngle = std::fmod (takeUpAngle + static_cast<float> (frameSeconds) * 2.3f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));

    if (speed > 0.001f)
    {
        // Tape off the supply reel and onto the take-up one, faster when there
        // is signal to record. It is the one number in the scene that has to
        // remember anything, which is why it is a member and not a uniform.
        tapeTransfer = juce::jlimit (0.0f, 1.0f,
                                     tapeTransfer + static_cast<float> (frameSeconds)
                                       * (0.010f + 0.075f * level));
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

    // -------------------------------------------------------------------------
    //  Draw state. The clear takes the colour and the depth, and the background
    //  is the panel's own so the window reads as a recess rather than as a hole.
    // -------------------------------------------------------------------------
    juce::OpenGLHelpers::clear (background);

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

    const auto drawReel = [this, &scene, &body, &tape] (float centreX, float centreY, float centreZ,
                                                        float radius, float angle, float packOuter)
    {
        const auto model = scene
                         * glm::translate (glm::mat4 (1.0f), { centreX, centreY, centreZ })
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

    drawReel (supplyCentreX, supplyCentreY, 0.00f, supplyRadius, supplyAngle, supplyPackOuter);
    drawReel (takeUpCentreX, takeUpCentreY, 0.00f, takeUpRadius, takeUpAngle, takeUpPackOuter);

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
    const auto ribbonModel = scene;
    shaderProgram->setUniformMat4 ("uModel", glm::value_ptr (ribbonModel), 1, GL_FALSE);
    shaderProgram->setUniform ("uMode", static_cast<float> (ribbonMode));
    shaderProgram->setUniform ("uBaseColour", tape.getFloatRed() * 1.35f + 0.02f,
                               tape.getFloatGreen() * 1.35f + 0.02f,
                               tape.getFloatBlue() * 1.35f + 0.02f);
    drawMesh (ribbon);
}
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
    //  single translation and the three meshes it draws with share that one
    //  matrix.
    // -------------------------------------------------------------------------
    addBox (bank.body.vertices, bank.body.indices,
            { 0.0f, 0.0f, 0.0f },
            { bankHalfWidth, bankHalfHeight, bankHalfDepth });

    // -------------------------------------------------------------------------
    //  One knob, built round its own origin: a machined barrel with a knurled
    //  grip, a slightly domed face, and a pointer set into it.
    //
    //  Every radius below is a FRACTION of the knob's radius rather than a length,
    //  because the same mesh is drawn at five different scales - a knob and a
    //  keycap are the same two meshes at different sizes, which is what keeps a
    //  change to the knob a change to every knob on the panel.
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
    // pixels across, the difference is invisible and the rings cost a fifth of
    // the vertices. Each ring is lifted by the sagitta of the cap it approximates,
    // so the dome's silhouette is a circle rather than a cone.
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
            { 0.0f, 0.0f, 0.78f },
            { 0.72f, 0.62f, 0.30f });

    // -------------------------------------------------------------------------
    //  The lit disc: what a knob lays on the face when the machine is working.
    //  A disc of its own rather than a colour change on the body, because the
    //  glow is additive - it is drawn with blending on, over whatever is behind
    //  it, so it can bleed past the knob's rim without a second geometry pass.
    // -------------------------------------------------------------------------
    addDisc (bank.knobGlow.vertices, bank.knobGlow.indices,
             { 0.0f, 0.0f, 0.0f }, 1.0f, 0.0005f, segments);

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
    // more here than the handful of redundant calls a 66 px column costs.
    glBindBuffer (GL_ARRAY_BUFFER, mesh.vertexBuffer);
    glVertexAttribPointer (static_cast<GLuint> (positionLocation), 4, GL_FLOAT, GL_FALSE,
                           vertexStride, nullptr);
    glVertexAttribPointer (static_cast<GLuint> (normalLocation), 3, GL_FLOAT, GL_FALSE,
                           vertexStride, reinterpret_cast<const void*> (normalOffset));
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

    // -------------------------------------------------------------------------
    //  What the machine is doing, read once. Each of the five is a single atomic
    //  load, which is why this can be called on the render thread at all.
    // -------------------------------------------------------------------------
    const auto level = outputLevel.load (std::memory_order_relaxed);
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

    supplyAngle = std::fmod (supplyAngle + static_cast<float> (frameSeconds) * 2.6f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));
    takeUpAngle = std::fmod (takeUpAngle + static_cast<float> (frameSeconds) * 2.3f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));

    if (speed > 0.001f)
    {
        // Tape off the supply reel and onto the take-up one, faster when there
        // is signal to record. It is the one number in the scene that has to
        // remember anything, which is why it is a member and not a uniform.
        tapeTransfer = juce::jlimit (0.0f, 1.0f,
                                     tapeTransfer + static_cast<float> (frameSeconds)
                                       * (0.010f + 0.075f * level));
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

    // -------------------------------------------------------------------------
    //  Draw state. The clear takes the colour and the depth, and the background
    //  is the panel's own so the window reads as a recess rather than as a hole.
    // -------------------------------------------------------------------------
    juce::OpenGLHelpers::clear (background);

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

    const auto drawReel = [this, &scene, &body, &tape] (float centreX, float centreY, float centreZ,
                                                        float radius, float angle, float packOuter)
    {
        const auto model = scene
                         * glm::translate (glm::mat4 (1.0f), { centreX, centreY, centreZ })
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

    drawReel (supplyCentreX, supplyCentreY, 0.00f, supplyRadius, supplyAngle, supplyPackOuter);
    drawReel (takeUpCentreX, takeUpCentreY, 0.00f, takeUpRadius, takeUpAngle, takeUpPackOuter);

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
    const auto ribbonModel = scene;
    shaderProgram->setUniformMat4 ("uModel", glm::value_ptr (ribbonModel), 1, GL_FALSE);
    shaderProgram->setUniform ("uMode", static_cast<float> (ribbonMode));
    shaderProgram->setUniform ("uBaseColour", tape.getFloatRed() * 1.35f + 0.02f,
                               tape.getFloatGreen() * 1.35f + 0.02f,
                               tape.getFloatBlue() * 1.35f + 0.02f);
    drawMesh (ribbon);
}
    }

    if (programLinked.load() != 0)
    {
        uploadMesh (reel, buildReel());
        uploadMesh (tapePack, buildTapePack());
        uploadMesh (ribbon, buildRibbon());
        uploadMesh (head, buildHead());
    }
    else
    {
        releaseMesh (reel);
        releaseMesh (tapePack);
        releaseMesh (ribbon);
        releaseMesh (head);
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

    // -------------------------------------------------------------------------
    //  What the machine is doing, read once. Each of the five is a single atomic
    //  load, which is why this can be called on the render thread at all.
    // -------------------------------------------------------------------------
    const auto level = outputLevel.load (std::memory_order_relaxed);
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

    supplyAngle = std::fmod (supplyAngle + static_cast<float> (frameSeconds) * 2.6f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));
    takeUpAngle = std::fmod (takeUpAngle + static_cast<float> (frameSeconds) * 2.3f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));

    if (speed > 0.001f)
    {
        // Tape off the supply reel and onto the take-up one, faster when there
        // is signal to record. It is the one number in the scene that has to
        // remember anything, which is why it is a member and not a uniform.
        tapeTransfer = juce::jlimit (0.0f, 1.0f,
                                     tapeTransfer + static_cast<float> (frameSeconds)
                                       * (0.010f + 0.075f * level));
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

    // -------------------------------------------------------------------------
    //  Draw state. The clear takes the colour and the depth, and the background
    //  is the panel's own so the window reads as a recess rather than as a hole.
    // -------------------------------------------------------------------------
    juce::OpenGLHelpers::clear (background);

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

    const auto drawReel = [this, &scene, &body, &tape] (float centreX, float centreY, float centreZ,
                                                        float radius, float angle, float packOuter)
    {
        const auto model = scene
                         * glm::translate (glm::mat4 (1.0f), { centreX, centreY, centreZ })
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

    drawReel (supplyCentreX, supplyCentreY, 0.00f, supplyRadius, supplyAngle, supplyPackOuter);
    drawReel (takeUpCentreX, takeUpCentreY, 0.00f, takeUpRadius, takeUpAngle, takeUpPackOuter);

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
    const auto ribbonModel = scene;
    shaderProgram->setUniformMat4 ("uModel", glm::value_ptr (ribbonModel), 1, GL_FALSE);
    shaderProgram->setUniform ("uMode", static_cast<float> (ribbonMode));
    shaderProgram->setUniform ("uBaseColour", tape.getFloatRed() * 1.35f + 0.02f,
                               tape.getFloatGreen() * 1.35f + 0.02f,
                               tape.getFloatBlue() * 1.35f + 0.02f);
    drawMesh (ribbon);
}
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
    // more here than the handful of redundant calls a 66 px column costs.
    glBindBuffer (GL_ARRAY_BUFFER, mesh.vertexBuffer);
    glVertexAttribPointer (static_cast<GLuint> (positionLocation), 4, GL_FLOAT, GL_FALSE,
                           vertexStride, nullptr);
    glVertexAttribPointer (static_cast<GLuint> (normalLocation), 3, GL_FLOAT, GL_FALSE,
                           vertexStride, reinterpret_cast<const void*> (normalOffset));
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
    }
    else
    {
        releaseMesh (reel);
        releaseMesh (tapePack);
        releaseMesh (ribbon);
        releaseMesh (head);
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

    // -------------------------------------------------------------------------
    //  What the machine is doing, read once. Each of the five is a single atomic
    //  load, which is why this can be called on the render thread at all.
    // -------------------------------------------------------------------------
    const auto level = outputLevel.load (std::memory_order_relaxed);
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

    supplyAngle = std::fmod (supplyAngle + static_cast<float> (frameSeconds) * 2.6f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));
    takeUpAngle = std::fmod (takeUpAngle + static_cast<float> (frameSeconds) * 2.3f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));

    if (speed > 0.001f)
    {
        // Tape off the supply reel and onto the take-up one, faster when there
        // is signal to record. It is the one number in the scene that has to
        // remember anything, which is why it is a member and not a uniform.
        tapeTransfer = juce::jlimit (0.0f, 1.0f,
                                     tapeTransfer + static_cast<float> (frameSeconds)
                                       * (0.010f + 0.075f * level));
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

    // -------------------------------------------------------------------------
    //  Draw state. The clear takes the colour and the depth, and the background
    //  is the panel's own so the window reads as a recess rather than as a hole.
    // -------------------------------------------------------------------------
    juce::OpenGLHelpers::clear (background);

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

    const auto drawReel = [this, &scene, &body, &tape] (float centreX, float centreY, float centreZ,
                                                        float radius, float angle, float packOuter)
    {
        const auto model = scene
                         * glm::translate (glm::mat4 (1.0f), { centreX, centreY, centreZ })
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

    drawReel (supplyCentreX, supplyCentreY, 0.00f, supplyRadius, supplyAngle, supplyPackOuter);
    drawReel (takeUpCentreX, takeUpCentreY, 0.00f, takeUpRadius, takeUpAngle, takeUpPackOuter);

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
    const auto ribbonModel = scene;
    shaderProgram->setUniformMat4 ("uModel", glm::value_ptr (ribbonModel), 1, GL_FALSE);
    shaderProgram->setUniform ("uMode", static_cast<float> (ribbonMode));
    shaderProgram->setUniform ("uBaseColour", tape.getFloatRed() * 1.35f + 0.02f,
                               tape.getFloatGreen() * 1.35f + 0.02f,
                               tape.getFloatBlue() * 1.35f + 0.02f);
    drawMesh (ribbon);
}
    }

    if (programLinked.load() != 0)
    {
        uploadMesh (reel, buildReel());
        uploadMesh (tapePack, buildTapePack());
        uploadMesh (ribbon, buildRibbon());
        uploadMesh (head, buildHead());
    }
    else
    {
        releaseMesh (reel);
        releaseMesh (tapePack);
        releaseMesh (ribbon);
        releaseMesh (head);
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

    // -------------------------------------------------------------------------
    //  What the machine is doing, read once. Each of the five is a single atomic
    //  load, which is why this can be called on the render thread at all.
    // -------------------------------------------------------------------------
    const auto level = outputLevel.load (std::memory_order_relaxed);
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

    supplyAngle = std::fmod (supplyAngle + static_cast<float> (frameSeconds) * 2.6f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));
    takeUpAngle = std::fmod (takeUpAngle + static_cast<float> (frameSeconds) * 2.3f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));

    if (speed > 0.001f)
    {
        // Tape off the supply reel and onto the take-up one, faster when there
        // is signal to record. It is the one number in the scene that has to
        // remember anything, which is why it is a member and not a uniform.
        tapeTransfer = juce::jlimit (0.0f, 1.0f,
                                     tapeTransfer + static_cast<float> (frameSeconds)
                                       * (0.010f + 0.075f * level));
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

    // -------------------------------------------------------------------------
    //  Draw state. The clear takes the colour and the depth, and the background
    //  is the panel's own so the window reads as a recess rather than as a hole.
    // -------------------------------------------------------------------------
    juce::OpenGLHelpers::clear (background);

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

    const auto drawReel = [this, &scene, &body, &tape] (float centreX, float centreY, float centreZ,
                                                        float radius, float angle, float packOuter)
    {
        const auto model = scene
                         * glm::translate (glm::mat4 (1.0f), { centreX, centreY, centreZ })
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

    drawReel (supplyCentreX, supplyCentreY, 0.00f, supplyRadius, supplyAngle, supplyPackOuter);
    drawReel (takeUpCentreX, takeUpCentreY, 0.00f, takeUpRadius, takeUpAngle, takeUpPackOuter);

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
    const auto ribbonModel = scene;
    shaderProgram->setUniformMat4 ("uModel", glm::value_ptr (ribbonModel), 1, GL_FALSE);
    shaderProgram->setUniform ("uMode", static_cast<float> (ribbonMode));
    shaderProgram->setUniform ("uBaseColour", tape.getFloatRed() * 1.35f + 0.02f,
                               tape.getFloatGreen() * 1.35f + 0.02f,
                               tape.getFloatBlue() * 1.35f + 0.02f);
    drawMesh (ribbon);
}
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
    // more here than the handful of redundant calls a 66 px column costs.
    glBindBuffer (GL_ARRAY_BUFFER, mesh.vertexBuffer);
    glVertexAttribPointer (static_cast<GLuint> (positionLocation), 4, GL_FLOAT, GL_FALSE,
                           vertexStride, nullptr);
    glVertexAttribPointer (static_cast<GLuint> (normalLocation), 3, GL_FLOAT, GL_FALSE,
                           vertexStride, reinterpret_cast<const void*> (normalOffset));
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

    // -------------------------------------------------------------------------
    //  What the machine is doing, read once. Each of the five is a single atomic
    //  load, which is why this can be called on the render thread at all.
    // -------------------------------------------------------------------------
    const auto level = outputLevel.load (std::memory_order_relaxed);
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

    supplyAngle = std::fmod (supplyAngle + static_cast<float> (frameSeconds) * 2.6f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));
    takeUpAngle = std::fmod (takeUpAngle + static_cast<float> (frameSeconds) * 2.3f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));

    if (speed > 0.001f)
    {
        // Tape off the supply reel and onto the take-up one, faster when there
        // is signal to record. It is the one number in the scene that has to
        // remember anything, which is why it is a member and not a uniform.
        tapeTransfer = juce::jlimit (0.0f, 1.0f,
                                     tapeTransfer + static_cast<float> (frameSeconds)
                                       * (0.010f + 0.075f * level));
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

    // -------------------------------------------------------------------------
    //  Draw state. The clear takes the colour and the depth, and the background
    //  is the panel's own so the window reads as a recess rather than as a hole.
    // -------------------------------------------------------------------------
    juce::OpenGLHelpers::clear (background);

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

    const auto drawReel = [this, &scene, &body, &tape] (float centreX, float centreY, float centreZ,
                                                        float radius, float angle, float packOuter)
    {
        const auto model = scene
                         * glm::translate (glm::mat4 (1.0f), { centreX, centreY, centreZ })
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

    drawReel (supplyCentreX, supplyCentreY, 0.00f, supplyRadius, supplyAngle, supplyPackOuter);
    drawReel (takeUpCentreX, takeUpCentreY, 0.00f, takeUpRadius, takeUpAngle, takeUpPackOuter);

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
    const auto ribbonModel = scene;
    shaderProgram->setUniformMat4 ("uModel", glm::value_ptr (ribbonModel), 1, GL_FALSE);
    shaderProgram->setUniform ("uMode", static_cast<float> (ribbonMode));
    shaderProgram->setUniform ("uBaseColour", tape.getFloatRed() * 1.35f + 0.02f,
                               tape.getFloatGreen() * 1.35f + 0.02f,
                               tape.getFloatBlue() * 1.35f + 0.02f);
    drawMesh (ribbon);
}
    }

    if (programLinked.load() != 0)
    {
        uploadMesh (reel, buildReel());
        uploadMesh (tapePack, buildTapePack());
        uploadMesh (ribbon, buildRibbon());
        uploadMesh (head, buildHead());
    }
    else
    {
        releaseMesh (reel);
        releaseMesh (tapePack);
        releaseMesh (ribbon);
        releaseMesh (head);
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

    // -------------------------------------------------------------------------
    //  What the machine is doing, read once. Each of the five is a single atomic
    //  load, which is why this can be called on the render thread at all.
    // -------------------------------------------------------------------------
    const auto level = outputLevel.load (std::memory_order_relaxed);
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

    supplyAngle = std::fmod (supplyAngle + static_cast<float> (frameSeconds) * 2.6f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));
    takeUpAngle = std::fmod (takeUpAngle + static_cast<float> (frameSeconds) * 2.3f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));

    if (speed > 0.001f)
    {
        // Tape off the supply reel and onto the take-up one, faster when there
        // is signal to record. It is the one number in the scene that has to
        // remember anything, which is why it is a member and not a uniform.
        tapeTransfer = juce::jlimit (0.0f, 1.0f,
                                     tapeTransfer + static_cast<float> (frameSeconds)
                                       * (0.010f + 0.075f * level));
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

    // -------------------------------------------------------------------------
    //  Draw state. The clear takes the colour and the depth, and the background
    //  is the panel's own so the window reads as a recess rather than as a hole.
    // -------------------------------------------------------------------------
    juce::OpenGLHelpers::clear (background);

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

    const auto drawReel = [this, &scene, &body, &tape] (float centreX, float centreY, float centreZ,
                                                        float radius, float angle, float packOuter)
    {
        const auto model = scene
                         * glm::translate (glm::mat4 (1.0f), { centreX, centreY, centreZ })
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

    drawReel (supplyCentreX, supplyCentreY, 0.00f, supplyRadius, supplyAngle, supplyPackOuter);
    drawReel (takeUpCentreX, takeUpCentreY, 0.00f, takeUpRadius, takeUpAngle, takeUpPackOuter);

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
    const auto ribbonModel = scene;
    shaderProgram->setUniformMat4 ("uModel", glm::value_ptr (ribbonModel), 1, GL_FALSE);
    shaderProgram->setUniform ("uMode", static_cast<float> (ribbonMode));
    shaderProgram->setUniform ("uBaseColour", tape.getFloatRed() * 1.35f + 0.02f,
                               tape.getFloatGreen() * 1.35f + 0.02f,
                               tape.getFloatBlue() * 1.35f + 0.02f);
    drawMesh (ribbon);
}
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
    // more here than the handful of redundant calls a 66 px column costs.
    glBindBuffer (GL_ARRAY_BUFFER, mesh.vertexBuffer);
    glVertexAttribPointer (static_cast<GLuint> (positionLocation), 4, GL_FLOAT, GL_FALSE,
                           vertexStride, nullptr);
    glVertexAttribPointer (static_cast<GLuint> (normalLocation), 3, GL_FLOAT, GL_FALSE,
                           vertexStride, reinterpret_cast<const void*> (normalOffset));
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
    }
    else
    {
        releaseMesh (reel);
        releaseMesh (tapePack);
        releaseMesh (ribbon);
        releaseMesh (head);
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

    // -------------------------------------------------------------------------
    //  What the machine is doing, read once. Each of the five is a single atomic
    //  load, which is why this can be called on the render thread at all.
    // -------------------------------------------------------------------------
    const auto level = outputLevel.load (std::memory_order_relaxed);
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

    supplyAngle = std::fmod (supplyAngle + static_cast<float> (frameSeconds) * 2.6f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));
    takeUpAngle = std::fmod (takeUpAngle + static_cast<float> (frameSeconds) * 2.3f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));

    if (speed > 0.001f)
    {
        // Tape off the supply reel and onto the take-up one, faster when there
        // is signal to record. It is the one number in the scene that has to
        // remember anything, which is why it is a member and not a uniform.
        tapeTransfer = juce::jlimit (0.0f, 1.0f,
                                     tapeTransfer + static_cast<float> (frameSeconds)
                                       * (0.010f + 0.075f * level));
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

    // -------------------------------------------------------------------------
    //  Draw state. The clear takes the colour and the depth, and the background
    //  is the panel's own so the window reads as a recess rather than as a hole.
    // -------------------------------------------------------------------------
    juce::OpenGLHelpers::clear (background);

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

    const auto drawReel = [this, &scene, &body, &tape] (float centreX, float centreY, float centreZ,
                                                        float radius, float angle, float packOuter)
    {
        const auto model = scene
                         * glm::translate (glm::mat4 (1.0f), { centreX, centreY, centreZ })
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

    drawReel (supplyCentreX, supplyCentreY, 0.00f, supplyRadius, supplyAngle, supplyPackOuter);
    drawReel (takeUpCentreX, takeUpCentreY, 0.00f, takeUpRadius, takeUpAngle, takeUpPackOuter);

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
    const auto ribbonModel = scene;
    shaderProgram->setUniformMat4 ("uModel", glm::value_ptr (ribbonModel), 1, GL_FALSE);
    shaderProgram->setUniform ("uMode", static_cast<float> (ribbonMode));
    shaderProgram->setUniform ("uBaseColour", tape.getFloatRed() * 1.35f + 0.02f,
                               tape.getFloatGreen() * 1.35f + 0.02f,
                               tape.getFloatBlue() * 1.35f + 0.02f);
    drawMesh (ribbon);
}
    }

    if (programLinked.load() != 0)
    {
        uploadMesh (reel, buildReel());
        uploadMesh (tapePack, buildTapePack());
        uploadMesh (ribbon, buildRibbon());
        uploadMesh (head, buildHead());
    }
    else
    {
        releaseMesh (reel);
        releaseMesh (tapePack);
        releaseMesh (ribbon);
        releaseMesh (head);
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

    // -------------------------------------------------------------------------
    //  What the machine is doing, read once. Each of the five is a single atomic
    //  load, which is why this can be called on the render thread at all.
    // -------------------------------------------------------------------------
    const auto level = outputLevel.load (std::memory_order_relaxed);
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

    supplyAngle = std::fmod (supplyAngle + static_cast<float> (frameSeconds) * 2.6f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));
    takeUpAngle = std::fmod (takeUpAngle + static_cast<float> (frameSeconds) * 2.3f * speed,
                             static_cast<float> (juce::MathConstants<float>::twoPi));

    if (speed > 0.001f)
    {
        // Tape off the supply reel and onto the take-up one, faster when there
        // is signal to record. It is the one number in the scene that has to
        // remember anything, which is why it is a member and not a uniform.
        tapeTransfer = juce::jlimit (0.0f, 1.0f,
                                     tapeTransfer + static_cast<float> (frameSeconds)
                                       * (0.010f + 0.075f * level));
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

    // -------------------------------------------------------------------------
    //  Draw state. The clear takes the colour and the depth, and the background
    //  is the panel's own so the window reads as a recess rather than as a hole.
    // -------------------------------------------------------------------------
    juce::OpenGLHelpers::clear (background);

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

    const auto drawReel = [this, &scene, &body, &tape] (float centreX, float centreY, float centreZ,
                                                        float radius, float angle, float packOuter)
    {
        const auto model = scene
                         * glm::translate (glm::mat4 (1.0f), { centreX, centreY, centreZ })
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

    drawReel (supplyCentreX, supplyCentreY, 0.00f, supplyRadius, supplyAngle, supplyPackOuter);
    drawReel (takeUpCentreX, takeUpCentreY, 0.00f, takeUpRadius, takeUpAngle, takeUpPackOuter);

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
    const auto ribbonModel = scene;
    shaderProgram->setUniformMat4 ("uModel", glm::value_ptr (ribbonModel), 1, GL_FALSE);
    shaderProgram->setUniform ("uMode", static_cast<float> (ribbonMode));
    shaderProgram->setUniform ("uBaseColour", tape.getFloatRed() * 1.35f + 0.02f,
                               tape.getFloatGreen() * 1.35f + 0.02f,
                               tape.getFloatBlue() * 1.35f + 0.02f);
    drawMesh (ribbon);
}
