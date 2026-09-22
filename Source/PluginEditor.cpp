#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const juce::Colour backgroundColour (0xff0d1418);
    const juce::Colour panelColour (0xff172127);
    const juce::Colour cardColour (0xff1c292f);
    const juce::Colour goldColour (0xffd5ad68);
    const juce::Colour textColour (0xfff2eee5);
    const juce::Colour secondaryTextColour (0xff95a3a6);

    juce::String formatDb (float value)
    {
        return juce::String (value, 1) + " dB";
    }
}

void J37LookAndFeel::drawRotarySlider (juce::Graphics& g,
                                       int x, int y, int width, int height,
                                       float sliderPos,
                                       const float rotaryStartAngle,
                                       const float rotaryEndAngle,
                                       juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> (static_cast<float> (x),
                                                 static_cast<float> (y),
                                                 static_cast<float> (width),
                                                 static_cast<float> (height)).reduced (7.0f);
    const auto centre = bounds.getCentre();
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.36f;
    const auto angle = juce::jmap (sliderPos, 0.0f, 1.0f, rotaryStartAngle, rotaryEndAngle);
    const auto outerRadius = radius + 8.0f;

    g.setColour (juce::Colours::black.withAlpha (0.24f));
    g.fillEllipse (centre.x - radius - 2.0f, centre.y - radius + 2.0f,
                   (radius + 2.0f) * 2.0f, (radius + 2.0f) * 2.0f);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, outerRadius, outerRadius, 0.0f,
                         rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (juce::Colour (0xff35454a));
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    juce::Path activeArc;
    activeArc.addCentredArc (centre.x, centre.y, outerRadius, outerRadius, 0.0f,
                             rotaryStartAngle, angle, true);
    g.setColour (goldColour);
    g.strokePath (activeArc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

    juce::ColourGradient face (juce::Colour (0xff344248), centre.x - radius, centre.y - radius,
                               juce::Colour (0xff1c282d), centre.x + radius, centre.y + radius, false);
    g.setGradientFill (face);
    g.fillEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);

    g.setColour (juce::Colour (0xff718087).withAlpha (0.55f));
    g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.0f);

    const auto pointerLength = radius * 0.64f;
    const auto pointerEnd = centre + juce::Point<float> (std::cos (angle - juce::MathConstants<float>::halfPi) * pointerLength,
                                                         std::sin (angle - juce::MathConstants<float>::halfPi) * pointerLength);
    g.setColour (juce::Colour (0xffffe7b6));
    g.drawLine (centre.x, centre.y, pointerEnd.x, pointerEnd.y, 3.0f);
    g.setColour (goldColour);
    g.fillEllipse (centre.x - 4.2f, centre.y - 4.2f, 8.4f, 8.4f);

    if (slider.hasKeyboardFocus (false))
    {
        g.setColour (juce::Colour (0xfff1d08a).withAlpha (0.8f));
        g.drawEllipse (bounds.reduced (2.0f), 1.0f);
    }
}

//==============================================================================
FirstAudioProcessorEditor::LevelMeter::LevelMeter (juce::String meterTitle)
    : title (std::move (meterTitle))
{
    setOpaque (false);
}

void FirstAudioProcessorEditor::LevelMeter::setLevels (float peakLinear, float rmsLinear)
{
    constexpr float floorDb = -60.0f;
    constexpr float frameSeconds = 1.0f / 30.0f;

    const auto toDb = [] (float level)
    {
        return juce::Decibels::gainToDecibels (juce::jmax (0.0f, level), -60.0f);
    };

    const auto targetPeakDb = juce::jlimit (floorDb, 6.0f, toDb (peakLinear));
    const auto targetRmsDb = juce::jlimit (floorDb, 6.0f, toDb (rmsLinear));

    const auto rmsCoefficient = targetRmsDb > rmsDb ? 0.58f : 0.16f;
    rmsDb += (targetRmsDb - rmsDb) * rmsCoefficient;

    if (targetPeakDb >= peakHoldDb)
    {
        peakHoldDb = targetPeakDb;
        peakHoldTime = 0.65f;
    }
    else if (peakHoldTime > 0.0f)
    {
        peakHoldTime = juce::jmax (0.0f, peakHoldTime - frameSeconds);
    }
    else
    {
        peakHoldDb = juce::jmax (targetPeakDb, peakHoldDb - 18.0f * frameSeconds);
    }

    repaint();
}

void FirstAudioProcessorEditor::LevelMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (juce::Colour (0xff142025));
    g.fillRoundedRectangle (bounds, 12.0f);
    g.setColour (juce::Colour (0xff34434a));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 12.0f, 1.0f);

    g.setColour (secondaryTextColour);
    g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    g.drawText (title, getLocalBounds().removeFromTop (28), juce::Justification::centred, false);

    const auto barTop = 38.0f;
    const auto barBottom = static_cast<float> (getHeight()) - 80.0f;
    const auto barHeight = juce::jmax (40.0f, barBottom - barTop);
    const auto barWidth = 27.0f;
    const auto barX = juce::jmin (static_cast<float> (getWidth()) - barWidth - 7.0f,
                                  static_cast<float> (getWidth()) * 0.57f);
    const auto dbToY = [barTop, barHeight] (float db)
    {
        const auto normalized = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        return barTop + (1.0f - normalized) * barHeight;
    };

    g.setColour (juce::Colour (0xff0c1317));
    g.fillRoundedRectangle (barX, barTop, barWidth, barHeight, 5.0f);

    const float ticks[] { 0.0f, -6.0f, -18.0f, -36.0f, -60.0f };
    g.setFont (juce::Font (juce::FontOptions (9.0f)));
    for (const auto tick : ticks)
    {
        const auto y = dbToY (tick);
        g.setColour (juce::Colour (0xff405057));
        g.drawHorizontalLine (juce::roundToInt (y), barX - 2.0f, barX + barWidth + 2.0f);
        g.setColour (secondaryTextColour);
        g.drawText (juce::String (static_cast<int> (tick)),
                    juce::Rectangle<int> (3, juce::roundToInt (y - 7.0f),
                                          juce::jmax (28, juce::roundToInt (barX - 8.0f)), 14),
                    juce::Justification::right, false);
    }

    const auto rmsY = dbToY (rmsDb);
    auto fill = juce::Rectangle<float> (barX + 3.0f, rmsY,
                                        barWidth - 6.0f, barBottom - rmsY);
    if (fill.getHeight() > 0.0f)
    {
        juce::ColourGradient levelGradient (juce::Colour (0xffe4594f), barX, barTop,
                                            juce::Colour (0xff54bd87), barX, barBottom, false);
        levelGradient.addColour (0.72, juce::Colour (0xffe1bd58));
        g.setGradientFill (levelGradient);
        g.fillRoundedRectangle (fill, 3.0f);
    }

    const auto holdY = dbToY (peakHoldDb);
    g.setColour (juce::Colour (0xfffff2d1));
    g.drawLine (barX - 2.0f, holdY, barX + barWidth + 2.0f, holdY, 2.0f);

    const auto readoutY = getHeight() - 39;
    g.setColour (textColour);
    g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
    g.drawText ("PK " + formatDb (peakHoldDb),
                juce::Rectangle<int> (4, readoutY, getWidth() - 8, 16),
                juce::Justification::centred, false);
    g.setColour (goldColour);
    g.drawText ("RMS " + formatDb (rmsDb),
                juce::Rectangle<int> (4, readoutY + 15, getWidth() - 8, 16),
                juce::Justification::centred, false);
}

//==============================================================================
FirstAudioProcessorEditor::FirstAudioProcessorEditor (FirstAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setResizable (true, true);
    setResizeLimits (920, 610, 1440, 900);
    setSize (1080, 700);
    createDecorativePhysics();

    const auto styleLabel = [] (juce::Label& label, const juce::String& text,
                                float size, juce::Colour colour,
                                bool bold, juce::Justification justification)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain)));
        label.setColour (juce::Label::textColourId, colour);
        label.setJustificationType (justification);
        label.setInterceptsMouseClicks (false, false);
    };

    styleLabel (brandLabel, "ANALOG TAPE", 9.0f, goldColour, true, juce::Justification::left);
    styleLabel (titleLabel, "J37", 27.0f, textColour, true, juce::Justification::left);
    styleLabel (subtitleLabel, "TAPE MACHINE  /  SATURATION", 10.0f, secondaryTextColour, true,
                juce::Justification::left);
    styleLabel (statusLabel, "STEREO  •  REAL-TIME", 9.0f, juce::Colour (0xffa9d6be), true,
                juce::Justification::centred);
    styleLabel (deckHeadingLabel, "TAPE DECK", 10.0f, goldColour, true, juce::Justification::left);
    styleLabel (tapeTypeLabel, "MODEL", 9.0f, secondaryTextColour, true, juce::Justification::left);
    styleLabel (speedLabel, "SPEED", 9.0f, secondaryTextColour, true, juce::Justification::left);
    styleLabel (deckHintLabel, "Choose a tape formula and transport speed.", 10.0f,
                secondaryTextColour, false, juce::Justification::centredLeft);
    styleLabel (controlsHeadingLabel, "TAPE CHARACTER", 10.0f, goldColour, true,
                juce::Justification::left);
    styleLabel (controlsHintLabel, "Double-click any control to reset it", 9.0f,
                secondaryTextColour, false, juce::Justification::right);
    styleLabel (metersHeadingLabel, "LEVELS", 10.0f, goldColour, true,
                juce::Justification::left);
    styleLabel (metersHintLabel, "dBFS  /  PEAK + RMS", 8.0f, secondaryTextColour, true,
                juce::Justification::left);

    addAndMakeVisible (brandLabel);
    addAndMakeVisible (titleLabel);
    addAndMakeVisible (subtitleLabel);
    addAndMakeVisible (statusLabel);
    addAndMakeVisible (deckHeadingLabel);
    addAndMakeVisible (tapeTypeLabel);
    addAndMakeVisible (speedLabel);
    addAndMakeVisible (deckHintLabel);
    addAndMakeVisible (controlsHeadingLabel);
    addAndMakeVisible (controlsHintLabel);
    addAndMakeVisible (metersHeadingLabel);
    addAndMakeVisible (metersHintLabel);

    const juce::StringArray controlIds { "input", "drive", "bias", "tone",
                                         "wow", "flutter", "mix", "output" };
    const juce::StringArray controlNames { "INPUT", "DRIVE", "BIAS", "TONE",
                                           "WOW", "FLUTTER", "MIX", "OUTPUT" };
    const std::array<double, controlCount> defaultValues { 0.0, 0.42, 0.36, 0.58,
                                                           0.14, 0.18, 0.62, 0.68 };

    for (std::size_t i = 0; i < controlCount; ++i)
    {
        auto& slider = controls[i];
        slider.setName (controlNames[static_cast<int> (i)]);
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 0.75f,
                                    juce::MathConstants<float>::pi * 2.25f, true);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 20);
        slider.setTextBoxIsEditable (true);
        slider.setVelocityBasedMode (false);
        slider.setMouseDragSensitivity (250);
        slider.setPopupDisplayEnabled (true, true, this);
        slider.setScrollWheelEnabled (true);
        slider.setDoubleClickReturnValue (true, defaultValues[i]);
        slider.setLookAndFeel (&customLookAndFeel);
        slider.setColour (juce::Slider::textBoxTextColourId, textColour);
        slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff121b20));
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff35454a));
        slider.setColour (juce::Slider::thumbColourId, goldColour);

        if (i == 0)
        {
            slider.setRange (-24.0, 24.0, 0.1);
            slider.setNumDecimalPlacesToDisplay (1);
            slider.setTextValueSuffix (" dB");
        }
        else
        {
            slider.setRange (0.0, 1.0, 0.001);
            slider.textFromValueFunction = [] (double value)
            {
                return juce::String (juce::roundToInt (value * 100.0)) + " %";
            };
            slider.valueFromTextFunction = [] (const juce::String& text)
            {
                return juce::jlimit (0.0, 1.0, text.getDoubleValue() / 100.0);
            };
        }

        controlLabels[i].setText (controlNames[static_cast<int> (i)], juce::dontSendNotification);
        controlLabels[i].setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
        controlLabels[i].setColour (juce::Label::textColourId, secondaryTextColour);
        controlLabels[i].setJustificationType (juce::Justification::centred);
        controlLabels[i].setInterceptsMouseClicks (false, false);

        addAndMakeVisible (controlLabels[i]);
        addAndMakeVisible (slider);
        controlAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
            (audioProcessor.parameters, controlIds[static_cast<int> (i)], slider);
    }

    tapeTypeBox.addItemList (juce::StringArray { "J37", "Ampex 456", "Studer A800", "Chrome" }, 1);
    speedBox.addItemList (juce::StringArray { "7.5 ips", "15 ips", "30 ips" }, 1);
    tapeTypeBox.setTextWhenNothingSelected ("Select tape");
    speedBox.setTextWhenNothingSelected ("Select speed");
    tapeTypeBox.setLookAndFeel (&customLookAndFeel);
    speedBox.setLookAndFeel (&customLookAndFeel);

    const auto styleCombo = [] (juce::ComboBox& box)
    {
        box.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff121b20));
        box.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff3a4b51));
        box.setColour (juce::ComboBox::textColourId, textColour);
        box.setColour (juce::ComboBox::arrowColourId, goldColour);
        box.setColour (juce::ComboBox::focusedOutlineColourId, goldColour);
    };
    styleCombo (tapeTypeBox);
    styleCombo (speedBox);
    addAndMakeVisible (tapeTypeBox);
    addAndMakeVisible (speedBox);
    tapeTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "tape_type", tapeTypeBox);
    speedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "speed", speedBox);

    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);

    openGLContext.setComponentPaintingEnabled (true);
    openGLContext.setContinuousRepainting (false);
    openGLContext.attachTo (*this);

    startTimerHz (30);
}

FirstAudioProcessorEditor::~FirstAudioProcessorEditor()
{
    stopTimer();
    openGLContext.detach();

    for (auto& slider : controls)
        slider.setLookAndFeel (nullptr);

    tapeTypeBox.setLookAndFeel (nullptr);
    speedBox.setLookAndFeel (nullptr);
}

FirstAudioProcessorEditor::EditorLayout FirstAudioProcessorEditor::getEditorLayout() const
{
    auto remaining = getLocalBounds().reduced (18);
    EditorLayout layout;

    layout.header = remaining.removeFromTop (78);
    remaining.removeFromTop (12);
    layout.deck = remaining.removeFromTop (84);
    remaining.removeFromTop (12);

    layout.meters = remaining.removeFromRight (276);
    remaining.removeFromRight (14);
    layout.controls = remaining;

    return layout;
}

//==============================================================================
void FirstAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (backgroundColour));

    const auto layout = getEditorLayout();
    const auto drawCard = [&g] (juce::Rectangle<int> area, juce::Colour base)
    {
        g.setGradientFill (juce::ColourGradient (base.brighter (0.045f),
                                                 static_cast<float> (area.getX()),
                                                 static_cast<float> (area.getY()),
                                                 base.darker (0.12f),
                                                 static_cast<float> (area.getRight()),
                                                 static_cast<float> (area.getBottom()),
                                                 false));
        g.fillRoundedRectangle (area.toFloat(), 15.0f);
        g.setColour (juce::Colour (0xff33434a).withAlpha (0.72f));
        g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 15.0f, 1.0f);
    };

    drawCard (getLocalBounds().reduced (8), juce::Colour (panelColour));
    drawCard (layout.header, juce::Colour (0xff202c31));
    drawCard (layout.deck, juce::Colour (cardColour));
    drawCard (layout.controls, juce::Colour (cardColour));
    drawCard (layout.meters, juce::Colour (cardColour));

    g.setColour (goldColour.withAlpha (0.8f));
    g.fillRoundedRectangle (juce::Rectangle<float> (
        static_cast<float> (layout.header.getX() + 18),
        static_cast<float> (layout.header.getY() + 17), 3.0f,
        static_cast<float> (layout.header.getHeight() - 34)), 2.0f);

    g.setColour (juce::Colour (0xff35454a));
    g.drawHorizontalLine (layout.controls.getY() + 42,
                          static_cast<float> (layout.controls.getX() + 18),
                          static_cast<float> (layout.controls.getRight() - 18));
    g.drawHorizontalLine (layout.meters.getY() + 48,
                          static_cast<float> (layout.meters.getX() + 18),
                          static_cast<float> (layout.meters.getRight() - 18));

    const auto badge = juce::Rectangle<float> (
        static_cast<float> (layout.header.getRight() - 205),
        static_cast<float> (layout.header.getY() + 24), 184.0f, 30.0f);
    g.setColour (juce::Colour (0xff182328));
    g.fillRoundedRectangle (badge, 15.0f);
    g.setColour (juce::Colour (0xff3c6653));
    g.fillEllipse (badge.getX() + 13.0f, badge.getCentreY() - 3.5f, 7.0f, 7.0f);

    // Subtle simulated reel details, kept clear of labels and controls.
    g.setColour (goldColour.withAlpha (0.10f));
    const auto reelCentre = juce::Point<float> (static_cast<float> (layout.deck.getRight() - 66),
                                                static_cast<float> (layout.deck.getCentreY() + 5));
    for (const auto radius : { 18.0f, 10.0f, 3.0f })
        g.drawEllipse (reelCentre.x - radius, reelCentre.y - radius,
                       radius * 2.0f, radius * 2.0f, 1.2f);
    g.drawLine (reelCentre.x - 25.0f, reelCentre.y, reelCentre.x + 25.0f, reelCentre.y, 1.0f);
    g.drawLine (reelCentre.x, reelCentre.y - 25.0f, reelCentre.x, reelCentre.y + 25.0f, 1.0f);

    std::array<RenderOrb, decorativeOrbCount> orbsToDraw;
    {
        const juce::SpinLock::ScopedLockType lock (renderOrbsLock);
        orbsToDraw = renderOrbs;
    }

    for (const auto& orb : orbsToDraw)
    {
        const auto x = juce::jmap (orb.position.x, 0.0f, 1.5f,
                                   static_cast<float> (layout.deck.getX()) + layout.deck.getWidth() * 0.68f,
                                   static_cast<float> (layout.deck.getRight()) - 24.0f);
        const auto y = juce::jmap (orb.position.y, 0.0f, 1.2f,
                                   static_cast<float> (layout.deck.getY()) + 15.0f,
                                   static_cast<float> (layout.deck.getBottom()) - 15.0f);
        const auto radius = juce::jmap (orb.radius, 0.045f, 0.057f, 3.0f, 5.0f);

        g.setColour (orb.colour.withAlpha (0.08f));
        g.fillEllipse (x - radius * 2.0f, y - radius * 2.0f, radius * 4.0f, radius * 4.0f);
        g.setColour (orb.colour.withAlpha (0.22f));
        g.fillEllipse (x - radius, y - radius, radius * 2.0f, radius * 2.0f);
    }
}

void FirstAudioProcessorEditor::createDecorativePhysics()
{
    physicsWorld = std::make_unique<b2World> (b2Vec2 (0.0f, 0.0f));

    b2BodyDef boundaryDefinition;
    auto* boundaryBody = physicsWorld->CreateBody (&boundaryDefinition);

    const auto addWall = [boundaryBody] (float centreX, float centreY,
                                         float halfWidth, float halfHeight)
    {
        b2PolygonShape wallShape;
        wallShape.SetAsBox (halfWidth, halfHeight, b2Vec2 (centreX, centreY), 0.0f);

        b2FixtureDef wallFixture;
        wallFixture.shape = &wallShape;
        wallFixture.friction = 0.05f;
        wallFixture.restitution = 0.82f;
        boundaryBody->CreateFixture (&wallFixture);
    };

    addWall (0.75f, 0.0f, 0.75f, 0.02f);
    addWall (0.75f, 1.2f, 0.75f, 0.02f);
    addWall (0.0f, 0.6f, 0.02f, 0.6f);
    addWall (1.5f, 0.6f, 0.02f, 0.6f);

    for (std::size_t i = 0; i < decorativeOrbCount; ++i)
    {
        const auto index = static_cast<int> (i);
        const auto radius = 0.045f + static_cast<float> (index % 2) * 0.012f;
        const auto position = b2Vec2 (0.30f + static_cast<float> (index) * 0.22f,
                                      0.28f + static_cast<float> (index % 2) * 0.18f);

        b2BodyDef bodyDefinition;
        bodyDefinition.type = b2_dynamicBody;
        bodyDefinition.position = position;
        bodyDefinition.linearVelocity.Set ((index % 2 == 0 ? 1.0f : -1.0f) * 0.34f,
                                           (index % 3 == 0 ? 0.5f : -0.35f) * 0.42f);
        bodyDefinition.linearDamping = 0.04f;
        auto* body = physicsWorld->CreateBody (&bodyDefinition);

        b2CircleShape circleShape;
        circleShape.m_radius = radius;

        b2FixtureDef orbFixture;
        orbFixture.shape = &circleShape;
        orbFixture.density = 0.7f;
        orbFixture.friction = 0.15f;
        orbFixture.restitution = 0.82f;
        body->CreateFixture (&orbFixture);

        const auto colour = juce::Colour (0xffd8b36b)
                                .withAlpha (0.45f + static_cast<float> (index) * 0.06f);
        physicsOrbs[i] = { body, radius, colour };
        renderOrbs[i] = { juce::Point<float> (position.x, position.y), radius, colour };
    }
}

void FirstAudioProcessorEditor::timerCallback()
{
    inputMeter.setLevels (audioProcessor.getInputPeakLevel(), audioProcessor.getInputRmsLevel());
    outputMeter.setLevels (audioProcessor.getOutputPeakLevel(), audioProcessor.getOutputRmsLevel());

    if (! isShowing() || physicsWorld == nullptr)
        return;

    physicsWorld->Step (1.0f / 30.0f, 6, 2);

    std::array<RenderOrb, decorativeOrbCount> nextFrame;
    for (std::size_t i = 0; i < decorativeOrbCount; ++i)
    {
        const auto position = physicsOrbs[i].body->GetPosition();
        nextFrame[i] = { juce::Point<float> (position.x, position.y),
                         physicsOrbs[i].radius,
                         physicsOrbs[i].colour };
    }

    {
        const juce::SpinLock::ScopedLockType lock (renderOrbsLock);
        renderOrbs = nextFrame;
    }

    repaint (getEditorLayout().deck);
}

void FirstAudioProcessorEditor::resized()
{
    const auto layout = getEditorLayout();

    brandLabel.setBounds (layout.header.getX() + 29, layout.header.getY() + 12, 150, 14);
    titleLabel.setBounds (layout.header.getX() + 27, layout.header.getY() + 25, 92, 42);
    subtitleLabel.setBounds (layout.header.getX() + 126, layout.header.getY() + 37, 280, 20);
    statusLabel.setBounds (layout.header.getRight() - 200, layout.header.getY() + 24, 177, 30);

    deckHeadingLabel.setBounds (layout.deck.getX() + 20, layout.deck.getY() + 9, 160, 16);
    tapeTypeLabel.setBounds (layout.deck.getX() + 20, layout.deck.getY() + 39, 45, 20);
    tapeTypeBox.setBounds (layout.deck.getX() + 69, layout.deck.getY() + 33, 206, 34);
    speedLabel.setBounds (layout.deck.getX() + 300, layout.deck.getY() + 39, 45, 20);
    speedBox.setBounds (layout.deck.getX() + 349, layout.deck.getY() + 33, 146, 34);
    deckHintLabel.setBounds (layout.deck.getX() + 515, layout.deck.getY() + 32,
                             juce::jmax (110, layout.deck.getWidth() - 600), 36);

    controlsHeadingLabel.setBounds (layout.controls.getX() + 19, layout.controls.getY() + 13, 200, 19);
    controlsHintLabel.setBounds (layout.controls.getRight() - 254, layout.controls.getY() + 13,
                                 235, 19);
    metersHeadingLabel.setBounds (layout.meters.getX() + 18, layout.meters.getY() + 10, 100, 16);
    metersHintLabel.setBounds (layout.meters.getX() + 18, layout.meters.getY() + 28, 150, 14);

    auto grid = layout.controls.reduced (13);
    grid.removeFromTop (42);
    grid.removeFromBottom (8);
    const auto cellWidth = grid.getWidth() / 4;
    const auto rowHeight = grid.getHeight() / 2;

    for (std::size_t i = 0; i < controlCount; ++i)
    {
        const auto row = static_cast<int> (i / 4);
        const auto column = static_cast<int> (i % 4);
        auto cell = juce::Rectangle<int> (grid.getX() + column * cellWidth,
                                          grid.getY() + row * rowHeight,
                                          column == 3 ? grid.getRight() - (grid.getX() + column * cellWidth)
                                                      : cellWidth,
                                          row == 1 ? grid.getBottom() - (grid.getY() + row * rowHeight)
                                                   : rowHeight);
        controlLabels[i].setBounds (cell.getX() + 3, cell.getY() + 1,
                                    cell.getWidth() - 6, 17);
        auto sliderBounds = cell.reduced (4);
        sliderBounds.removeFromTop (19);
        controls[i].setBounds (sliderBounds);
    }

    const auto meterWidth = (layout.meters.getWidth() - 44) / 2;
    const auto meterY = layout.meters.getY() + 54;
    const auto meterHeight = layout.meters.getHeight() - 68;
    inputMeter.setBounds (layout.meters.getX() + 14, meterY, meterWidth, meterHeight);
    outputMeter.setBounds (layout.meters.getRight() - 14 - meterWidth, meterY,
                           meterWidth, meterHeight);
}
