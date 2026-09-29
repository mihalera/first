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

#include <atomic>
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
    */
    void setAudioState (float outputLevel,
                        float drive,
                        float gainReduction,
                        float wowFlutter,
                        float transportSpeed) noexcept;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TapeScene)
};
