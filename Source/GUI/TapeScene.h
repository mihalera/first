/*
  ==============================================================================
    TapeScene - the deck's transport, drawn as real geometry on the GPU.
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "SceneFrame.h"
#include "SceneLayout.h"

// The GL typedefs (GLuint, GLint, GLsizei) and the GL commands this component
// calls live in the module's own header, which the generated JuceHeader also
// pulls in - but not when the module list changes, and this file should not
// depend on that.
#include <juce_opengl/juce_opengl.h>

// glm is linked by CMake (see cmake/J37Dependencies.cmake) and is used for the
// scene's matrices. It is a header-only library, so this is the whole of what it
// takes to have it here - and having it here rather than in the .cpp is what
// lets the bank's own draw signature and the editor's hit-test agree on what a
// matrix is instead of passing raw floats between them.
#include <glm/glm.hpp>

#include <atomic>
#include <array>
#include <memory>
#include <vector>

/**
    A window into the machine: two reels, the tape that runs between them, and the
    head it passes - every one of them built from maths at start-up and lit by a
    shader. There is no image, no vector asset and no byte of hand-drawn geometry
    in the binary; the whole thing is a few thousand triangles and two dozen
    lines of GLSL.

    It is a CHILD COMPONENT, not a mode of the editor. The editor's own context is
    a component-painting context - it draws the 2D panel through JUCE's normal
    paint path - and a context can be one or the other, never both, so this
    component owns a second context, attaches it to itself and renders into its
    own bounds. Everything the user actually touches stays an ordinary 2D JUCE
    widget on top of it: the mouse maths, the host automation and the parameter
    attachments are all untouched, and only the picture behind them changes.

    WHERE IT SITS is the deck's right-hand column - the strip the layout already
    reserves for the reel and the ribbon (`reelCorridor` in PluginEditor.cpp).
    That strip is the one region of the panel with no control in it, so the scene
    can be drawn over the panel without covering a single knob, and the window
    needs no margin of its own to stay legible. It is a tall narrow column, so the
    transport in it runs VERTICALLY - supply reel at the top, the head in the
    middle, take-up at the bottom - which is both what fits and what a tall
    window of a tape machine has always looked like.

    WHAT IT REACTS TO: setAudioState() is called from the editor's 30 Hz timer on
    the message thread and stores six floats. Every one of them
    is an atomic, read once per frame on the GL thread. Nothing on the audio
    thread and nothing on the message thread is touched from renderOpenGL() - the
    alternative, reading the parameter tree from the render thread, is a data
    race that would eventually be a crash rather than a wrong pixel.

    WITHOUT A CONTEXT - a driver, a remote session, a VM, or the user turning GL
    off - isSceneLive() is false and the editor draws its flat 2D reel and ribbon
    instead, so the panel is never left with a hole where the transport was.
*/
class TapeScene final : public juce::Component,
                        private juce::OpenGLRenderer
{
public:
    TapeScene();
    ~TapeScene() override;

    /** One frame of what the machine is doing. Every argument arrives already
        normalised to a range the shader can use directly, so the render thread
        never has to know what a dB is or where a parameter lives.

        @param outputLevel     output loudness, 0..1
        @param drive           the DRIVE control, 0..1
        @param gainReduction   both compressors' total reduction, 0..1
        @param wowFlutter      the transport's drift, signed, -1..1, zero is steady
        @param transportSpeed  the machine's speed, 0..1 - zero stops the reels
        @param outputPeak      output peak, 0..1 - the loudest sample in the frame
    */
    void setAudioState (float outputLevel,
                        float drive,
                        float gainReduction,
                        float wowFlutter,
                        float transportSpeed,
                        float outputPeak) noexcept;

    /** Values for the decorative controls in scene-placement order, normalised
        to 0..1. Missing values retain their neutral position. */
    void setControlValues (const float* values, int count) noexcept;

    /** Which control a point in this component falls on, or -1 for none.

        The editor owns the mouse: this component is set not to intercept clicks,
        because a 3D control that swallowed them would also swallow the drag on
        the 2D control underneath. So the bank is HIT-TESTED here and the result
        handed back, and the editor decides what a click on it means.

        The projection is recomputed from the same constants renderOpenGL() uses,
        so the answer and the picture cannot disagree about where a knob is. */
    int controlIndexAt (juce::Point<int> positionInThisComponent) const;

    /** The value a control is showing, 0..1, or 0.5 when it has none. */
    float getControlValue (int controlIndex) const noexcept;

    /** The window's colours, so it follows the panel's theme. Called from the
        editor whenever the theme changes and once at construction. */
    void setPalette (juce::Colour background,
                     juce::Colour body,
                     juce::Colour highlight,
                     juce::Colour tape) noexcept;

    /** The arithmetic texture's three doses (grain, tape shimmer, wear), as
        RGB, one call for both the scene's uniforms and the editor's 2D ink. */
    void setTextureWeights (float grain, float shimmer, float wear) noexcept
    {
        const auto packed = juce::Colour::fromFloatRGBA (juce::jlimit (0.0f, 1.0f, grain),
                                                         juce::jlimit (0.0f, 1.0f, shimmer),
                                                         juce::jlimit (0.0f, 1.0f, wear),
                                                         1.0f).getARGB();
        textureWeights.store (packed, std::memory_order_relaxed);
    }

    /** Controls the separate OpenGL transport context. A false scene uses the
        software-painted transport instead. */
    void setSceneEnabled (bool shouldBeEnabled);

    /** Retries attaching the scene context while its bounded retry budget remains. */
    void serviceContextAttachment ();

    /** Whether the context is up AND the program linked - i.e. whether the
        editor should keep drawing its own flat transport behind this one. Read on
        the message thread, so the GL thread's half of the answer is an atomic. */
    [[nodiscard]] bool isSceneLive() const noexcept;

    /** CPU fallback rendered when the OpenGL context or shader is unavailable. */
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    //==========================================================================
    //  Geometry
    //
    //  Every vertex is four floats of position and three of normal. The fourth
    //  position component is the payload that tells the vertex shader what KIND
    //  of vertex it is looking at, which is what lets one shader and one vertex
    //  format draw all four pieces of the transport:
    //
    //     solid  (uMode 0)  w is unused
    //     pack   (uMode 1)  x,y is a UNIT direction, z is the fraction of the
    //                      pack's width this vertex sits at, w is its height
    //     ribbon (uMode 2)  x,y,z is the point, w is how far along the tape it is
    //
    //  The pack is why the tape can be seen moving from one reel to the other
    //  without a single byte crossing the bus after start-up: the vertices know
    //  their DIRECTION and their FRACTION, and the shader turns that into a
    //  radius from one uniform. The same trick makes the ribbon flutter, with w
    //  as the parameter along the path.
    //==========================================================================
    /** Indexed mesh in the backend-neutral SceneFrame representation. */
    using Geometry = j37::render::SceneMesh;

    /** Control-bank topology shared by the OpenGL and native SceneFrame renderers. */
    struct ControlBank
    {
        Geometry body;
        Geometry knob;
        Geometry key;
        Geometry knobGlow;
    };

    struct Mesh
    {
        GLuint vertexBuffer = 0;
        GLuint indexBuffer = 0;
        GLsizei indexCount = 0;
    };

    static Geometry buildReel();
    static Geometry buildTapePack();
    static Geometry buildRibbon();
    static Geometry buildHead();
    static std::shared_ptr<const std::vector<Geometry>> sharedSceneMeshes();
    static ControlBank buildControlBank();
    j37::render::SceneFrame buildSceneFrame (int width, int height, double elapsedSeconds,
                                              const glm::mat4& viewProjection,
                                              const glm::mat4& scene,
                                              const juce::Colour& background,
                                              const juce::Colour& body,
                                              const juce::Colour& highlight,
                                              const juce::Colour& tape,
                                              float driveAmount,
                                              float flutter,
                                              float reduction,
                                              float elapsed) const;
    void appendDraw (j37::render::SceneFrame& frame,
                     std::uint32_t meshIndex,
                     j37::render::SceneMaterial material,
                     const glm::mat4& model,
                     const juce::Colour& baseColour,
                     std::array<float, 3> parameters,
                     const juce::Colour& highlightColour) const;

    /** One control in the bank, and where it sits on the face. */
    struct ControlPlacement
    {
        float x = 0.0f;
        float y = 0.0f;
        float radius = 0.0f;
        float angle = 0.0f;
        bool isKey = false;
        bool isLit = false;
        float colour[3] { 0.0f, 0.0f, 0.0f };
    };

    /** The bank's size, read from the layout header rather than restated: the
        scene, the frame builder and the harness count the same controls. */
    static constexpr int controlBankCount = j37::render::scene::controlBankCount;

    /** The first controlBankCount of those are knobs; the rest are keys. */
    static constexpr int bankKnobCount = j37::render::scene::bankKnobCount;

    /** The control the editor puts a tooltip on, and the one a click on the bank
        is taken to mean. One control, not three: the bank is a picture of the
        machine rather than a second set of knobs, and a user who wants a knob
        uses the real one. */
    static constexpr int bankInteractiveIndex = 0;

    void uploadMesh (Mesh& mesh, const Geometry& geometry);
    void releaseMesh (Mesh& mesh) noexcept;
    void drawMesh (const Mesh& mesh);
    void drawSceneFrameOpenGL (const j37::render::SceneFrame& frame);

    //==========================================================================
    //  OpenGL backend
    //
    //  This is kept behind the backend boundary above. Vulkan, Metal and D3D
    //  can replace these callbacks without changing scene state, geometry or
    //  hit-testing.
    //==========================================================================
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    juce::OpenGLContext openGLContext;
    std::unique_ptr<juce::OpenGLShaderProgram> shaderProgram;
    Mesh reel {};
    Mesh tapePack {};
    Mesh ribbon {};
    Mesh head {};

    Mesh bankBody {};
    Mesh bankKnob {};
    Mesh bankKey {};
    Mesh bankGlow {};

    GLint positionLocation = -1;
    GLint normalLocation = -1;

    /** Bound only where the context's version has vertex array objects. A 3.x
        core profile rejects a draw with none bound, and a 1.x/2.x context has
        nowhere to bind one - hence the pair of paths rather than one. */
    GLuint vertexArray = 0;

    //==========================================================================
    //  Message thread writes these, the GL thread reads them. Atomic because
    //  the two are genuinely concurrent: the editor's timer and the render
    //  thread are not steps of one loop.
    //==========================================================================
    std::atomic<float> outputLevel { 0.0f };
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<float> drive { 0.0f };
    std::atomic<float> gainReduction { 0.0f };
    std::atomic<float> wowFlutter { 0.0f };
    std::atomic<float> transportSpeed { 0.0f };

    std::atomic<juce::uint32> backgroundColour { 0xff101216 };
    std::atomic<juce::uint32> bodyColour { 0xff3a4048 };
    std::atomic<juce::uint32> highlightColour { 0xffd8a45a };
    std::atomic<juce::uint32> tapeColour { 0xff241a14 };

    /** The arithmetic texture's three doses, packed into a colour's channels
        (r = static grain, g = live tape shimmer, b = wear blotches). It rides
        setPalette like the four colours, so one call textures both the scene
        and the 2D panel with the same hand. Zeroed by default: until the
        editor dials the weights, the scene draws untextured rather than
        guessing. */
    std::atomic<juce::uint32> textureWeights { 0x00000000 };

    /** The GL thread's half of the answer to isSceneLive(). */
    std::atomic<int> programLinked { 0 };

    /** The bank's knob angles, 0..1. */
    std::array<std::atomic<float>, bankKnobCount> knobValues {};

    //==========================================================================
    //  Message thread only.
    //==========================================================================
    bool sceneEnabled = false;
    int attachAttemptsLeft = 0;

    //==========================================================================
    //  GL thread only. Nothing else may touch these.
    //==========================================================================
    double timeOriginSeconds = 0.0;
    double previousFrameSeconds = 0.0;
    float tapeTransfer = 0.5f;
    float supplyAngle = 0.0f;
    float takeUpAngle = 0.0f;

    // The control bank's own state, and the reason it is state rather than a
    // function of the audio: a real knob does not jump. Each value follows what
    // the machine is doing with a lag, so a transient moves the knob and then it
    // settles - which is what a knob with a hand on it looks like, and what a
    // parameter read once per frame does not.
    float bankDrive = 0.0f;
    float bankPeak = 0.0f;
    float bankTransfer = 0.5f;
    float bankReduction = 0.0f;
    bool bankSpinning = false;
    bool bankRecording = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TapeScene)
};
