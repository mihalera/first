/*
  ==============================================================================
    TapeScene - the deck's transport, drawn as real geometry on the GPU.
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

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
#include <cstring>
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
    the message thread and does nothing but store five floats. Every one of them
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

    /** The values of the controls standing in front of the machine, 0..1 each, in
        the order of the scene's own placements - and NOT the plugin's parameter
        order, because the window shows five of a bank of fifty-eight and which
        five is a decision this class makes, not one the parameter tree does.

        Called from the editor's timer, on the message thread. The values cross to
        the render thread as atomics; nothing else does. */
    void setControlValues (const float* values, int count) noexcept;

    /** The captions under those controls, drawn in the window itself.

        Not text on a 2D overlay: a label painted by juce::Graphics would sit at a
        fixed screen position while the bank it belongs to moves with the camera,
        which is exactly the mismatch this whole component exists to avoid. The
        GLSL in here stamps each caption out of a procedural 5 x 7 alphabet, so a
        knob's name is part of the same picture as the knob.

        Message thread only, and it allocates: the editor calls it when the
        visible bank changes, never per frame. */
    void setKnobCaptions (const juce::String* captions, int count) noexcept;

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

    /** The editor's GL switch. False detaches the context and stops the scene;
        true starts the attach attempts again. */
    void setSceneEnabled (bool shouldBeEnabled);

    /** Whether the context is up AND the program linked - i.e. whether the
        editor should keep drawing its own flat transport behind this one. Read on
        the message thread, so the GL thread's half of the answer is an atomic. */
    [[nodiscard]] bool isSceneLive() const noexcept;

    /** Drives the attach retries, from the editor's timer.

        A component cannot have a context until the host has given it a native
        peer, and the editor may not have done that when this is constructed, so
        a first failure is not a final answer. The retry budget is bounded and
        then forgotten, for the same reason the editor's own context stops
        retrying: a driver or a VM that cannot create a context will not manage on
        the last attempt either, and retrying forever would burn a frame every
        33 ms for a picture that is never going to arrive.
    */
    void serviceContextAttachment();

    void paint (juce::Graphics&) override {}
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
    struct Geometry
    {
        std::vector<float> vertices;
        std::vector<juce::uint32> indices;
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

    /** The control bank in FRONT of the deck, the one piece of geometry that is not
        part of the machine: a strip of three machined knobs and two raised keys on
        a common face, each built round its own origin so drawKnob() can place and
        turn one with a single matrix. */
    struct ControlBank
    {
        Geometry body;      ///< the face they are set into
        Geometry knob;      ///< one knob with its own index mark, built round the origin
        Geometry key;       ///< one switch cap, built round the origin
        Geometry knobGlow;  ///< the disc a lit knob lays on the face
    };

    static ControlBank buildControlBank();

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

    static constexpr int controlBankCount = 5;

    /** The first controlBankCount of those are knobs; the rest are keys. A
        constant rather than a literal 3 because the GLSL's branch and the host's
        placement table both read it, and the two drifting apart would put a knob
        caption over a keycap. */
    static constexpr int bankKnobCount = 3;

    /** The control the editor puts a tooltip on, and the one a click on the bank
        is taken to mean. One control, not three: the bank is a picture of the
        machine rather than a second set of knobs, and a user who wants a knob
        uses the real one. */
    static constexpr int bankInteractiveIndex = 0;

    void drawControlBank (const glm::mat4& viewProjection,
                          const glm::mat4& scene,
                          const juce::Colour& bodyColour,
                          const juce::Colour& highlightColour);

    void uploadMesh (Mesh& mesh, const Geometry& geometry);
    void releaseMesh (Mesh& mesh) noexcept;
    void drawMesh (const Mesh& mesh);

    //==========================================================================
    //  OpenGLRenderer
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

    /** The GL thread's half of the answer to isSceneLive(). */
    std::atomic<int> programLinked { 0 };

    /** The bank's knob angles, 0..1, and the five captions under them. The
        captions are published as bytes plus a length rather than as a String
        because a juce::String allocates, and the render thread must not. */
    std::array<std::atomic<float>, bankKnobCount> knobValues {};
    std::array<std::array<char, 32>, bankKnobCount> captionBytes {};
    std::array<std::atomic<int>, bankKnobCount> captionLengths {};

    /** The caption the panel's plate is currently drawing, as the shader's own
        glyph indices. Written on the render thread and read only by it, so this
        is a plain member and not an atomic - it is this thread's own scratch. */
    std::array<int, 8> labelGlyphs { -1, -1, -1, -1 };

    // The bank's own presentation state: what the lit discs follow, and what the
    // two keycaps show. Lagged from the audio values in renderOpenGL(), because
    // a real knob does not jump and a real lamp does not step.
    float bankDrive = 0.0f;
    float bankPeak = 0.0f;
    float bankReduction = 0.0f;
    bool bankSpinning = false;
    bool bankRecording = false;

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
