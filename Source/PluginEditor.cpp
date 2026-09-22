/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <juce_box2d/juce_box2d.h>
#include <juce_opengl/juce_opengl.h>

namespace detail
{
    class J37OpenGLPanel final : public juce::OpenGLAppComponent,
                               private juce::Timer
    {
    public:
        J37OpenGLPanel()
        {
            setOpaque (false);
            startTimerHz (30);

            b2Vec2 gravity (0.0f, 0.0f);
            physicsWorld = std::make_unique<b2World> (gravity);

            for (int i = 0; i < 4; ++i)
            {
                b2BodyDef bodyDef;
                bodyDef.type = b2_dynamicBody;
                bodyDef.position.Set (0.22f + static_cast<float> (i) * 0.16f,
                                      0.24f + static_cast<float> (i % 2) * 0.20f);
                bodyDef.linearVelocity.Set ((i % 2 == 0 ? 1.0f : -1.0f) * 0.34f,
                                           (i % 3 == 0 ? 0.5f : -0.35f) * 0.42f);

                auto* body = physicsWorld->CreateBody (&bodyDef);

                b2CircleShape circleShape;
                circleShape.m_radius = 0.045f + static_cast<float> (i % 2) * 0.012f;

                b2FixtureDef fixtureDef;
                fixtureDef.shape = &circleShape;
                fixtureDef.density = 0.7f;
                fixtureDef.friction = 0.18f;
                body->CreateFixture (&fixtureDef);

                orbs.push_back ({ body, circleShape.m_radius, juce::Colour (0xffd8b36b).withAlpha (0.05f + i * 0.012f) });
            }
        }

        ~J37OpenGLPanel() override
        {
            stopTimer();
            physicsWorld.reset();
        }

        void initialise() override {}

        void render() override
        {
            if (physicsWorld != nullptr)
                physicsWorld->Step (1.0f / 30.0f, 6, 2);

            const auto width = static_cast<double> (getWidth());
            const auto height = static_cast<double> (getHeight());

            juce::gl::glClearColor (0.96f, 0.95f, 0.94f, 0.0f);
            juce::gl::glClear (juce::gl::GL_COLOR_BUFFER_BIT);

            juce::gl::glMatrixMode (juce::gl::GL_PROJECTION);
            juce::gl::glLoadIdentity();
            juce::gl::glOrtho (0.0, width, height, 0.0, -1.0, 1.0);
            juce::gl::glMatrixMode (juce::gl::GL_MODELVIEW);
            juce::gl::glLoadIdentity();

            juce::gl::glEnable (juce::gl::GL_BLEND);
            juce::gl::glBlendFunc (juce::gl::GL_SRC_ALPHA,
                                  juce::gl::GL_ONE_MINUS_SRC_ALPHA);

            for (int i = 0; i < 12; ++i)
            {
                const auto y = 40.0f + static_cast<float> (i) * 30.0f;
                const auto alpha = 0.016f + static_cast<float> (i) * 0.004f;
                juce::gl::glBegin (juce::gl::GL_LINES);
                juce::gl::glColor4f (0.76f, 0.69f, 0.53f, alpha);
                juce::gl::glVertex2f (18.0f, y);
                juce::gl::glVertex2f (static_cast<float> (width) - 18.0f, y + 8.0f);
                juce::gl::glEnd();
            }

            juce::gl::glBegin (juce::gl::GL_LINES);
            juce::gl::glColor4f (0.62f, 0.55f, 0.42f, 0.04f);
            juce::gl::glVertex2f (42.0f, 18.0f);
            juce::gl::glVertex2f (42.0f, static_cast<float> (height) - 18.0f);
            juce::gl::glVertex2f (static_cast<float> (width) - 42.0f, 18.0f);
            juce::gl::glVertex2f (static_cast<float> (width) - 42.0f, static_cast<float> (height) - 18.0f);
            juce::gl::glEnd();

            for (const auto& orb : orbs)
            {
                if (orb.body == nullptr)
                    continue;

                const auto position = orb.body->GetPosition();
                const auto x = juce::jmap (position.x, 0.0f, 1.5f, 0.0f, static_cast<float> (width));
                const auto y = juce::jmap (position.y, 0.0f, 1.2f, 0.0f, static_cast<float> (height));
                const auto radius = juce::jmap (orb.radius, 0.04f, 0.10f, 6.0f, 12.0f);

                const auto r = orb.colour.getFloatRed();
                const auto g = orb.colour.getFloatGreen();
                const auto b = orb.colour.getFloatBlue();
                const auto a = orb.colour.getFloatAlpha();

                juce::gl::glBegin (juce::gl::GL_TRIANGLE_FAN);
                juce::gl::glColor4f (r, g, b, a);
                juce::gl::glVertex2f (x, y);

                for (int i = 0; i <= 28; ++i)
                {
                    const auto theta = juce::MathConstants<float>::twoPi * static_cast<float> (i) / 28.0f;
                    const auto px = x + std::cos (theta) * radius;
                    const auto py = y + std::sin (theta) * radius;
                    juce::gl::glVertex2f (px, py);
                }
                juce::gl::glEnd();
            }

            juce::gl::glDisable (juce::gl::GL_BLEND);
        }

        void resized() override
        {
            // OpenGLAppComponent handles the viewport size itself.
        }

        void timerCallback() override
        {
            if (isShowing())
                repaint();
        }

    private:
        struct Orb
        {
            b2Body* body = nullptr;
            float radius = 0.0f;
            juce::Colour colour;
        };

        std::unique_ptr<b2World> physicsWorld;
        std::vector<Orb> orbs;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (J37OpenGLPanel)
    };
}

namespace
{
    struct J37LookAndFeel : juce::LookAndFeel_V4
    {
        void drawRotarySlider (juce::Graphics& g,
                              int x, int y, int width, int height,
                              float sliderPos,
                              const float rotaryStartAngle,
                              const float rotaryEndAngle,
                              juce::Slider& slider) override
        {
            auto bounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                                static_cast<float> (width), static_cast<float> (height)).reduced (8.0f);
            const auto centre = bounds.getCentre();
            const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.43f;
            const auto angle = juce::jmap (sliderPos, 0.0f, 1.0f, rotaryStartAngle, rotaryEndAngle);

            g.setColour (juce::Colour (0xff171d22));
            g.fillEllipse (bounds);

            g.setColour (juce::Colour (0xffd0a95a));
            g.drawEllipse (bounds.reduced (1.0f), 1.6f);

            for (int i = 0; i < 24; ++i)
            {
                const auto tickAngle = juce::jmap (static_cast<float> (i), 0.0f, 24.0f, rotaryStartAngle, rotaryEndAngle);
                const auto tickRadius = radius * 0.9f;
                const auto inner = centre + juce::Point<float> (std::cos (tickAngle) * tickRadius,
                                                             std::sin (tickAngle) * tickRadius);
                const auto outer = centre + juce::Point<float> (std::cos (tickAngle) * (tickRadius + 7.0f),
                                                             std::sin (tickAngle) * (tickRadius + 7.0f));
                g.setColour (juce::Colour (0xffd4b06c).withAlpha (0.20f + (i % 2) * 0.18f));
                g.drawLine (inner.x, inner.y, outer.x, outer.y, 1.0f);
            }

            g.setColour (juce::Colour (0xfff3dcc0));
            g.fillEllipse (centre.x - radius * 0.38f, centre.y - radius * 0.38f,
                           radius * 0.76f, radius * 0.76f);

            g.setColour (juce::Colour (0xffcc9d4a));
            juce::Path pointer;
            pointer.addRectangle (0.0f, -radius * 0.62f, 3.5f, radius * 0.78f);
            const auto transform = juce::AffineTransform().translated (centre.x, centre.y)
                .rotated (angle - juce::MathConstants<float>::halfPi)
                .translated (-1.75f, 0.0f);
            g.fillPath (pointer, transform);

            g.setColour (juce::Colour (0xfff6d58d));
            g.fillEllipse (centre.x - 5.0f, centre.y - 5.0f, 10.0f, 10.0f);

            if (slider.hasKeyboardFocus (false))
            {
                g.setColour (juce::Colour (0xffd8b868));
                g.drawEllipse (bounds.reduced (1.5f), 1.2f);
            }
        }

        void drawLinearSlider (juce::Graphics& g,
                              int x, int y, int width, int height,
                              float sliderPos,
                              float minPos, float maxPos,
                              const juce::Slider::SliderStyle style,
                              juce::Slider& slider) override
        {
            auto bounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                                static_cast<float> (width), static_cast<float> (height));
            auto track = bounds.reduced (4.0f);

            g.setColour (juce::Colour (0xffece8df));
            g.fillRoundedRectangle (track, 8.0f);

            g.setColour (juce::Colour (0xffd7b15f));
            g.drawRoundedRectangle (track.reduced (1.0f), 8.0f, 1.2f);

            juce::Rectangle<float> fill;
            if (style == juce::Slider::LinearVertical || style == juce::Slider::LinearBarVertical)
            {
                const auto activeTop = juce::jmap (sliderPos, 0.0f, 1.0f, track.getBottom(), track.getY());
                fill = juce::Rectangle<float> (track.getX() + 4.0f, activeTop, track.getWidth() - 8.0f, track.getBottom() - activeTop);
            }
            else
            {
                const auto activeLeft = juce::jmap (sliderPos, 0.0f, 1.0f, track.getX(), track.getRight());
                fill = juce::Rectangle<float> (track.getX(), track.getY() + 4.0f, activeLeft - track.getX(), track.getHeight() - 8.0f);
            }

            g.setColour (juce::Colour (0xffd8b868).withAlpha (0.9f));
            g.fillRoundedRectangle (fill, 6.0f);

            if (slider.isEnabled ())
            {
                const auto thumbX = (style == juce::Slider::LinearVertical || style == juce::Slider::LinearBarVertical)
                    ? track.getCentreX()
                    : juce::jmap (sliderPos, 0.0f, 1.0f, track.getX(), track.getRight());
                const auto thumbY = (style == juce::Slider::LinearVertical || style == juce::Slider::LinearBarVertical)
                    ? juce::jmap (sliderPos, 0.0f, 1.0f, track.getBottom(), track.getY())
                    : track.getCentreY();

                g.setColour (juce::Colour (0xfff5f3ef));
                g.fillEllipse (thumbX - 7.5f, thumbY - 7.5f, 15.0f, 15.0f);
                g.setColour (juce::Colour (0xffc89b49));
                g.drawEllipse (thumbX - 7.5f, thumbY - 7.5f, 15.0f, 15.0f, 1.5f);
            }

            juce::ignoreUnused (minPos, maxPos);
        }
    };
}

//==============================================================================
FirstAudioProcessorEditor::FirstAudioProcessorEditor (FirstAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setSize (760, 460);

    auto configureRotary = [this] (juce::Slider& slider, float startValue)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 54, 18);
        slider.setTextBoxIsEditable (true);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 0.75f,
                                   juce::MathConstants<float>::pi * 2.25f,
                                   0.0f);
        slider.setVelocityBasedMode (false);
        slider.setMouseDragSensitivity (130);
        slider.setSkewFactorFromMidPoint (0.62f);
        slider.setPopupDisplayEnabled (true, true, nullptr);
        slider.setScrollWheelEnabled (true);
        slider.setDoubleClickReturnValue (true, 0.5f);
        slider.setLookAndFeel (&customLookAndFeel);
        slider.setRange (0.0, 1.0);
        slider.setValue (startValue);
    };

    auto configureLinear = [this] (juce::Slider& slider, juce::Slider::SliderStyle style, float startValue)
    {
        slider.setSliderStyle (style);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, true, 52, 18);
        slider.setTextBoxIsEditable (true);
        slider.setVelocityBasedMode (false);
        slider.setMouseDragSensitivity (110);
        slider.setSkewFactorFromMidPoint (0.55f);
        slider.setPopupDisplayEnabled (true, true, nullptr);
        slider.setScrollWheelEnabled (true);
        slider.setDoubleClickReturnValue (true, 0.5f);
        slider.setLookAndFeel (&customLookAndFeel);
        slider.setRange (0.0, 1.0);
        slider.setValue (startValue);
    };

    configureRotary (driveSlider, 0.42f);
    configureRotary (biasSlider, 0.36f);
    configureRotary (toneSlider, 0.58f);
    configureRotary (wowSlider, 0.14f);
    configureRotary (flutterSlider, 0.18f);

    configureLinear (mixSlider, juce::Slider::LinearHorizontal, 0.62f);
    configureLinear (outputSlider, juce::Slider::LinearVertical, 0.68f);

    tapeTypeBox.addItemList (juce::StringArray { "J37", "Ampex 456", "Studer A800", "Chrome" }, 1);
    speedBox.addItemList (juce::StringArray { "7.5 ips", "15 ips", "30 ips" }, 1);
    tapeTypeBox.setSelectedId (1);
    speedBox.setSelectedId (2);
    tapeTypeBox.setLookAndFeel (&customLookAndFeel);
    speedBox.setLookAndFeel (&customLookAndFeel);
    tapeTypeBox.setTextWhenNothingSelected ("TAPE");
    speedBox.setTextWhenNothingSelected ("SPEED");
    tapeTypeBox.setEditableText (true);
    speedBox.setEditableText (true);

    driveAttachment = std::make_unique<SliderAttachment> (audioProcessor.parameters, "drive", driveSlider);
    biasAttachment = std::make_unique<SliderAttachment> (audioProcessor.parameters, "bias", biasSlider);
    toneAttachment = std::make_unique<SliderAttachment> (audioProcessor.parameters, "tone", toneSlider);
    wowAttachment = std::make_unique<SliderAttachment> (audioProcessor.parameters, "wow", wowSlider);
    flutterAttachment = std::make_unique<SliderAttachment> (audioProcessor.parameters, "flutter", flutterSlider);
    mixAttachment = std::make_unique<SliderAttachment> (audioProcessor.parameters, "mix", mixSlider);
    outputAttachment = std::make_unique<SliderAttachment> (audioProcessor.parameters, "output", outputSlider);
    tapeTypeAttachment = std::make_unique<ComboBoxAttachment> (audioProcessor.parameters, "tape_type", tapeTypeBox);
    speedAttachment = std::make_unique<ComboBoxAttachment> (audioProcessor.parameters, "speed", speedBox);

    openGLPanel = std::make_unique<detail::J37OpenGLPanel>();
    addAndMakeVisible (*openGLPanel);
    openGLPanel->setBounds (0, 0, getWidth(), getHeight());

    addAndMakeVisible (driveSlider);
    addAndMakeVisible (biasSlider);
    addAndMakeVisible (toneSlider);
    addAndMakeVisible (wowSlider);
    addAndMakeVisible (flutterSlider);
    addAndMakeVisible (mixSlider);
    addAndMakeVisible (outputSlider);
    addAndMakeVisible (tapeTypeBox);
    addAndMakeVisible (speedBox);
}

FirstAudioProcessorEditor::~FirstAudioProcessorEditor()
{
}

//==============================================================================
void FirstAudioProcessorEditor::paint (juce::Graphics& g)
{
    auto bg = getLocalBounds().toFloat();

    juce::ColourGradient lightGrad (
        juce::Colour (0xfff6f3ee),
        bg.getX(),
        bg.getY(),
        juce::Colour (0xffdfe3e8),
        bg.getRight(),
        bg.getBottom(),
        false);
    g.setGradientFill (lightGrad);
    g.fillRoundedRectangle (bg.reduced (6.0f), 28.0f);

    const auto panel = getLocalBounds().reduced (12);

    g.setGradientFill (juce::ColourGradient (
        juce::Colour (0xfff8f3ed),
        static_cast<float> (panel.getX()),
        static_cast<float> (panel.getY()),
        juce::Colour (0xffe4e9ee),
        static_cast<float> (panel.getRight()),
        static_cast<float> (panel.getBottom()),
        false));
    g.fillRoundedRectangle (panel.toFloat(), 24.0f);

    g.setColour (juce::Colour (0xffc7a55f).withAlpha (0.12f));
    for (int i = 0; i < 14; ++i)
    {
        const auto y = panel.getY() + 18 + i * 18;
        g.drawLine (panel.getX() + 18.0f, static_cast<float> (y), panel.getRight() - 18.0f, static_cast<float> (y) + 8.0f, 1.0f);
    }

    g.setColour (juce::Colour (0xff2a2d2f));
    g.drawRoundedRectangle (panel.toFloat().reduced (2.0f), 24.0f, 1.8f);

    g.setColour (juce::Colour (0xffd5ad68).withAlpha (0.18f));
    g.fillRoundedRectangle (juce::Rectangle<float> (panel.getX() + 8.0f, panel.getY() + 8.0f,
                                                  panel.getWidth() - 16.0f, 72.0f), 18.0f);

    g.setColour (juce::Colour (0xff7e5929));
    g.setFont (juce::Font (25.0f, juce::Font::bold));
    g.drawText ("J37", juce::Rectangle<int> (panel.getX() + 24, panel.getY() + 18, 88, 30), juce::Justification::left, false);

    g.setColour (juce::Colour (0xff2c2c2c));
    g.setFont (juce::Font (15.0f, juce::Font::bold));
    g.drawText ("TAPE SATURATOR", juce::Rectangle<int> (panel.getX() + 118, panel.getY() + 24, 220, 20), juce::Justification::left, false);

    g.setColour (juce::Colour (0xffc49a56));
    g.fillRect (panel.getX() + 436, panel.getY() + 20, 186, 2);
    g.fillRect (panel.getX() + 436, panel.getY() + 34, 186, 2);

    g.setColour (juce::Colour (0xffd9ba7e));
    g.fillEllipse (panel.getX() + 646, panel.getY() + 18, 18, 18);
    g.setColour (juce::Colour (0xff3fb476));
    g.fillEllipse (panel.getX() + 646, panel.getY() + 18, 8, 8);

    g.setColour (juce::Colour (0xff6f675f));
    g.setFont (juce::Font (10.0f, juce::Font::bold));
    g.drawText ("INPUT", juce::Rectangle<int> (panel.getX() + 572, panel.getY() + 20, 60, 18), juce::Justification::centred, false);

    g.setColour (juce::Colour (0xfff2f3f6));
    g.fillRoundedRectangle (juce::Rectangle<float> (panel.getX() + 30.0f, panel.getY() + 92.0f,
                                                  panel.getWidth() - 60.0f, 32.0f), 9.0f);
    g.setColour (juce::Colour (0xffbb9048));
    g.drawRoundedRectangle (juce::Rectangle<float> (panel.getX() + 30.0f, panel.getY() + 92.0f,
                                                  panel.getWidth() - 60.0f, 32.0f), 9.0f, 1.1f);

    g.setColour (juce::Colour (0xff564f49));
    g.setFont (juce::Font (11.0f, juce::Font::bold));
    g.drawText ("REEL / TAPE / LOSS / WARMTH", juce::Rectangle<int> (panel.getX() + 44, panel.getY() + 100, 242, 18), juce::Justification::left, false);

    g.setColour (juce::Colour (0xffa9823d));
    g.setFont (juce::Font (11.0f, juce::Font::bold));

    const auto knobRowY = panel.getY() + 146;
    const auto knobRowX = panel.getX() + 34;
    const auto labelW = 82;
    const auto labelH = 18;

    g.drawText ("TAPE", juce::Rectangle<int> (knobRowX, knobRowY + 112, labelW, labelH), juce::Justification::centred, false);
    g.drawText ("SPEED", juce::Rectangle<int> (knobRowX + 96, knobRowY + 112, labelW + 22, labelH), juce::Justification::centred, false);
    g.drawText ("DRIVE", juce::Rectangle<int> (knobRowX + 254, knobRowY + 112, 82, labelH), juce::Justification::centred, false);
    g.drawText ("BIAS", juce::Rectangle<int> (knobRowX + 348, knobRowY + 112, 82, labelH), juce::Justification::centred, false);
    g.drawText ("TONE", juce::Rectangle<int> (knobRowX + 444, knobRowY + 112, 82, labelH), juce::Justification::centred, false);
    g.drawText ("WOW", juce::Rectangle<int> (knobRowX + 16, knobRowY + 260, 82, labelH), juce::Justification::centred, false);
    g.drawText ("FLUTTER", juce::Rectangle<int> (knobRowX + 112, knobRowY + 260, 92, labelH), juce::Justification::centred, false);
    g.drawText ("MIX", juce::Rectangle<int> (knobRowX + 220, knobRowY + 260, 82, labelH), juce::Justification::centred, false);
    g.drawText ("OUT", juce::Rectangle<int> (knobRowX + 308, knobRowY + 260, 82, labelH), juce::Justification::centred, false);

    g.setColour (juce::Colour (0xffcaa566).withAlpha (0.2f));
    g.fillRoundedRectangle (juce::Rectangle<float> (panel.getX() + 18.0f, panel.getY() + 138.0f,
                                                  panel.getWidth() - 36.0f, 3.0f), 2.0f);

    for (int i = 0; i < 26; ++i)
    {
        const auto x = static_cast<float> (panel.getX()) + 30.0f + static_cast<float> ((i * 19) % (panel.getWidth() - 80));
        const auto y = static_cast<float> (panel.getY()) + 18.0f + static_cast<float> ((i * 11) % 58);
        g.setColour (juce::Colour (0xffd9caa3).withAlpha (0.04f + static_cast<float> (i % 5) * 0.012f));
        g.fillEllipse (x, y, 2.0f, 2.0f);
    }
}

void FirstAudioProcessorEditor::resized()
{
    if (openGLPanel != nullptr)
        openGLPanel->setBounds (getLocalBounds());

    const auto bounds = getLocalBounds().reduced (20, 22);

    tapeTypeBox.setBounds (bounds.getX() + 34, bounds.getY() + 84, 110, 30);
    speedBox.setBounds (bounds.getX() + 158, bounds.getY() + 84, 120, 30);

    driveSlider.setBounds (bounds.getX() + 284, bounds.getY() + 138, 92, 92);
    biasSlider.setBounds (bounds.getX() + 380, bounds.getY() + 138, 92, 92);
    toneSlider.setBounds (bounds.getX() + 476, bounds.getY() + 138, 92, 92);

    wowSlider.setBounds (bounds.getX() + 42, bounds.getY() + 248, 92, 92);
    flutterSlider.setBounds (bounds.getX() + 138, bounds.getY() + 248, 92, 92);
    mixSlider.setBounds (bounds.getX() + 236, bounds.getY() + 250, 125, 22);
    outputSlider.setBounds (bounds.getX() + 240, bounds.getY() + 330, 18, 82);

    driveSlider.setRotaryParameters (juce::MathConstants<float>::pi * 0.75f,
                                    juce::MathConstants<float>::pi * 2.25f,
                                    0.0f);
    biasSlider.setRotaryParameters (juce::MathConstants<float>::pi * 0.75f,
                                  juce::MathConstants<float>::pi * 2.25f,
                                  0.0f);
    toneSlider.setRotaryParameters (juce::MathConstants<float>::pi * 0.75f,
                                   juce::MathConstants<float>::pi * 2.25f,
                                   0.0f);
    wowSlider.setRotaryParameters (juce::MathConstants<float>::pi * 0.75f,
                                 juce::MathConstants<float>::pi * 2.25f,
                                 0.0f);
    flutterSlider.setRotaryParameters (juce::MathConstants<float>::pi * 0.75f,
                                     juce::MathConstants<float>::pi * 2.25f,
                                     0.0f);
}
