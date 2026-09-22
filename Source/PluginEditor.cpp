#include "../Builds/VisualStudio2026/pch.h"

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Symmetric decibel range shared by the input and output trims, matching the
    // processor's parameter layout exactly.
    constexpr double minStageDb = -32.0;
    constexpr double maxStageDb = 32.0;

    struct UiPalette
    {
        juce::Colour background;
        juce::Colour panel;
        juce::Colour card;
        juce::Colour raised;
        juce::Colour border;
        juce::Colour accent;
        juce::Colour text;
        juce::Colour secondary;
        juce::Colour knobFace;
        juce::Colour knobHighlight;
        juce::Colour knobEdge;
        juce::Colour readout;
        juce::Colour gaugeFace;
        juce::Colour gaugeInk;
        juce::Colour needle;
        juce::Colour status;
    };

    const UiPalette ivoryPalette {
        juce::Colour::fromRGB (197, 184, 157),
        juce::Colour::fromRGB (222, 210, 187),
        juce::Colour::fromRGB (211, 197, 169),
        juce::Colour::fromRGB (231, 220, 197),
        juce::Colour::fromRGB (111, 97, 73),
        juce::Colour::fromRGB (145, 96, 42),
        juce::Colour::fromRGB (45, 38, 29),
        juce::Colour::fromRGB (102, 91, 72),
        juce::Colour::fromRGB (179, 162, 130),
        juce::Colour::fromRGB (217, 203, 176),
        juce::Colour::fromRGB (93, 78, 55),
        juce::Colour::fromRGB (245, 238, 222),
        juce::Colour::fromRGB (239, 229, 203),
        juce::Colour::fromRGB (49, 43, 33),
        juce::Colour::fromRGB (132, 47, 37),
        juce::Colour::fromRGB (62, 111, 72)
    };

    const UiPalette charcoalPalette {
        juce::Colour::fromRGB (28, 25, 21),
        juce::Colour::fromRGB (54, 46, 35),
        juce::Colour::fromRGB (68, 57, 42),
        juce::Colour::fromRGB (84, 70, 50),
        juce::Colour::fromRGB (144, 117, 72),
        juce::Colour::fromRGB (211, 166, 83),
        juce::Colour::fromRGB (244, 231, 204),
        juce::Colour::fromRGB (190, 173, 140),
        juce::Colour::fromRGB (89, 72, 51),
        juce::Colour::fromRGB (126, 105, 73),
        juce::Colour::fromRGB (209, 172, 103),
        juce::Colour::fromRGB (35, 30, 24),
        juce::Colour::fromRGB (226, 210, 173),
        juce::Colour::fromRGB (48, 39, 28),
        juce::Colour::fromRGB (132, 47, 37),
        juce::Colour::fromRGB (122, 174, 128)
    };

    const UiPalette& paletteFor (bool darkTheme)
    {
        return darkTheme ? charcoalPalette : ivoryPalette;
    }

    juce::String formatDb (float value)
    {
        return juce::String (value, 1) + " dB";
    }

    void drawScrew (juce::Graphics& g, float x, float y, const UiPalette& palette)
    {
        g.setColour (palette.border.withAlpha (0.65f));
        g.fillEllipse (x - 3.0f, y - 3.0f, 6.0f, 6.0f);
        g.setColour (palette.panel.brighter (0.22f));
        g.drawLine (x - 1.5f, y + 1.5f, x + 1.5f, y - 1.5f, 0.8f);
    }

    void drawPanel (juce::Graphics& g, juce::Rectangle<int> area,
                    const UiPalette& palette, float cornerSize)
    {
        const auto rect = area.toFloat();
        juce::ColourGradient fill (palette.panel.brighter (0.08f), rect.getX(), rect.getY(),
                                  palette.panel.darker (0.08f), rect.getRight(), rect.getBottom(), false);
        g.setGradientFill (fill);
        g.fillRoundedRectangle (rect, cornerSize);
        g.setColour (palette.border);
        g.drawRoundedRectangle (rect.reduced (0.5f), cornerSize, 1.0f);
        g.setColour (palette.panel.brighter (0.12f).withAlpha (0.8f));
        g.drawRoundedRectangle (rect.reduced (3.0f), juce::jmax (1.0f, cornerSize - 2.0f), 0.7f);
    }
}

void J37LookAndFeel::drawRotarySlider (juce::Graphics& g,
                                       int x, int y, int width, int height,
                                       float sliderPos,
                                       const float rotaryStartAngle,
                                       const float rotaryEndAngle,
                                       juce::Slider& slider)
{
    const auto& palette = paletteFor (darkTheme);
    const auto bounds = juce::Rectangle<float> (static_cast<float> (x),
                                                 static_cast<float> (y),
                                                 static_cast<float> (width),
                                                 static_cast<float> (height)).reduced (6.0f);
    const auto centre = bounds.getCentre();
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.34f;
    const auto outerRadius = radius + 8.0f;
    const auto angle = juce::jmap (sliderPos, 0.0f, 1.0f, rotaryStartAngle, rotaryEndAngle);

    //------------------------------------------------------------------
    //  Animation layer 1: a soft halo that breathes with the compressor
    //  activity, plus a slow pulse so the panel never looks frozen.
    //------------------------------------------------------------------
    const auto breath = 0.5f + 0.5f * std::sin (animationPhase);
    const auto haloAlpha = 0.05f + activity * 0.20f * (0.6f + 0.4f * breath);
    if (haloAlpha > 0.01f)
    {
        for (int ring = 3; ring >= 1; --ring)
        {
            const auto haloRadius = outerRadius + static_cast<float> (ring) * 5.0f
                                    + activity * 4.0f * breath;
            g.setColour (palette.accent.withAlpha (haloAlpha / static_cast<float> (ring)));
            g.drawEllipse (centre.x - haloRadius, centre.y - haloRadius,
                           haloRadius * 2.0f, haloRadius * 2.0f, 1.6f);
        }
    }

    g.setColour (palette.knobEdge.withAlpha (0.22f));
    g.fillEllipse (centre.x - radius - 3.0f, centre.y - radius + 2.0f,
                   (radius + 3.0f) * 2.0f, (radius + 3.0f) * 2.0f);

    juce::Path scaleTrack;
    scaleTrack.addCentredArc (centre.x, centre.y, outerRadius, outerRadius, 0.0f,
                              rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (palette.knobEdge.withAlpha (0.70f));
    g.strokePath (scaleTrack, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));

    juce::Path activeTrack;
    activeTrack.addCentredArc (centre.x, centre.y, outerRadius, outerRadius, 0.0f,
                               rotaryStartAngle, angle, true);
    g.setColour (palette.accent);
    g.strokePath (activeTrack, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));

    // Bright tracer dot riding the end of the active arc - the clearest "live" cue.
    const auto tracer = centre + juce::Point<float> (std::cos (angle) * outerRadius,
                                                     std::sin (angle) * outerRadius);
    const auto tracerPulse = 2.6f + 1.4f * breath + activity * 2.0f;
    g.setColour (palette.accent.withAlpha (0.35f));
    g.fillEllipse (tracer.x - tracerPulse * 1.9f, tracer.y - tracerPulse * 1.9f,
                   tracerPulse * 3.8f, tracerPulse * 3.8f);
    g.setColour (palette.readout);
    g.fillEllipse (tracer.x - tracerPulse, tracer.y - tracerPulse,
                   tracerPulse * 2.0f, tracerPulse * 2.0f);

    //------------------------------------------------------------------
    //  Animation layer 2: the scale ticks tremble with the transport
    //  drift, so wow and flutter are visible as well as audible.
    //------------------------------------------------------------------
    const auto driftWobble = (drift - 0.5f) * 2.0f;
    for (int tick = 0; tick <= 12; ++tick)
    {
        const auto tickAngle = juce::jmap (static_cast<float> (tick), 0.0f, 12.0f,
                                           rotaryStartAngle, rotaryEndAngle);
        const auto major = tick % 3 == 0;
        const auto tremble = std::sin (tickAngle * 3.0f + animationPhase * 1.7f)
                             * driftWobble * 1.3f;
        const auto innerRadius = outerRadius + (major ? 3.0f : 4.0f) + tremble;
        const auto outerTickRadius = innerRadius + (major ? 5.0f : 2.5f);
        const auto inner = centre + juce::Point<float> (std::cos (tickAngle) * innerRadius,
                                                        std::sin (tickAngle) * innerRadius);
        const auto outer = centre + juce::Point<float> (std::cos (tickAngle) * outerTickRadius,
                                                        std::sin (tickAngle) * outerTickRadius);
        g.setColour (palette.knobEdge.withAlpha (major ? 0.75f : 0.42f));
        g.drawLine (inner.x, inner.y, outer.x, outer.y, major ? 1.2f : 0.8f);
    }

    juce::ColourGradient face (palette.knobHighlight, centre.x - radius, centre.y - radius,
                               palette.knobFace, centre.x + radius, centre.y + radius, false);
    g.setGradientFill (face);
    g.fillEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
    g.setColour (palette.knobEdge.withAlpha (0.9f));
    g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.0f);
    g.setColour (palette.knobHighlight.withAlpha (0.48f));
    g.drawEllipse (centre.x - radius + 4.0f, centre.y - radius + 4.0f,
                   radius * 2.0f - 8.0f, radius * 2.0f - 8.0f, 0.8f);

    // Moving specular sweep across the knob face.
    const auto sweepAngle = animationPhase * 0.6f;
    const auto sweepCentre = centre + juce::Point<float> (std::cos (sweepAngle) * radius * 0.42f,
                                                          std::sin (sweepAngle) * radius * 0.42f);
    g.setColour (palette.knobHighlight.withAlpha (0.10f + 0.06f * breath));
    g.fillEllipse (sweepCentre.x - radius * 0.42f, sweepCentre.y - radius * 0.42f,
                   radius * 0.84f, radius * 0.84f);

    const auto pointerLength = radius * 0.62f;
    const auto pointerEnd = centre + juce::Point<float> (
        std::cos (angle - juce::MathConstants<float>::halfPi) * pointerLength,
        std::sin (angle - juce::MathConstants<float>::halfPi) * pointerLength);
    g.setColour (palette.accent.darker (0.15f));
    g.drawLine (centre.x + 1.0f, centre.y + 1.0f, pointerEnd.x + 1.0f, pointerEnd.y + 1.0f, 3.0f);
    g.setColour (palette.needle);
    g.drawLine (centre.x, centre.y, pointerEnd.x, pointerEnd.y, 2.2f);
    g.setColour (palette.accent);
    g.fillEllipse (centre.x - 3.5f, centre.y - 3.5f, 7.0f, 7.0f);

    if (slider.hasKeyboardFocus (false))
    {
        g.setColour (palette.accent.withAlpha (0.85f));
        g.drawEllipse (bounds.reduced (1.5f), 1.1f);
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
    const auto& palette = paletteFor (darkTheme);
    const auto bounds = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (palette.raised.darker (0.12f));
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (palette.border.withAlpha (0.9f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, 1.0f);

    g.setColour (palette.text);
    g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    g.drawText (title, getLocalBounds().removeFromTop (26), juce::Justification::centred, false);

    const auto centreX = static_cast<float> (getWidth()) * 0.5f;
    const auto centreY = 137.0f;
    const auto radius = juce::jmin (centreX - 12.0f, 48.0f);
    const auto startAngle = juce::MathConstants<float>::pi;
    const auto endAngle = juce::MathConstants<float>::twoPi;

    const auto dial = juce::Rectangle<float> (centreX - radius - 8.0f, 37.0f,
                                              (radius + 8.0f) * 2.0f, 185.0f);
    g.setColour (palette.knobEdge.withAlpha (0.55f));
    g.fillRoundedRectangle (dial, 4.0f);
    g.setColour (palette.gaugeFace);
    g.fillRoundedRectangle (dial.reduced (3.0f), 3.0f);
    g.setColour (palette.border);
    g.drawRoundedRectangle (dial.reduced (3.0f), 3.0f, 0.9f);

    juce::Path scaleArc;
    scaleArc.addCentredArc (centreX, centreY, radius, radius, 0.0f,
                            startAngle, endAngle, true);
    g.setColour (palette.gaugeInk.withAlpha (0.8f));
    g.strokePath (scaleArc, juce::PathStrokeType (1.1f));

    for (int tick = 0; tick <= 10; ++tick)
    {
        const auto fraction = static_cast<float> (tick) / 10.0f;
        const auto angle = juce::jmap (fraction, startAngle, endAngle);
        const auto major = tick % 2 == 0;
        const auto outerRadius = radius - 1.0f;
        const auto innerRadius = radius - (major ? 8.0f : 4.0f);
        const auto outer = juce::Point<float> (centreX + std::cos (angle) * outerRadius,
                                               centreY + std::sin (angle) * outerRadius);
        const auto inner = juce::Point<float> (centreX + std::cos (angle) * innerRadius,
                                               centreY + std::sin (angle) * innerRadius);
        g.setColour (palette.gaugeInk);
        g.drawLine (inner.x, inner.y, outer.x, outer.y, major ? 1.1f : 0.7f);

        if (major)
        {
            const auto db = -20 + juce::roundToInt (fraction * 23.0f);
            const auto labelRadius = radius - 18.0f;
            const auto labelPoint = juce::Point<float> (
                centreX + std::cos (angle) * labelRadius,
                centreY + std::sin (angle) * labelRadius);
            g.setFont (juce::Font (juce::FontOptions (8.0f)));
            g.drawText (juce::String (db > 0 ? "+" : "") + juce::String (db),
                        juce::Rectangle<int> (juce::roundToInt (labelPoint.x - 10.0f),
                                              juce::roundToInt (labelPoint.y - 5.0f), 20, 11),
                        juce::Justification::centred, false);
        }
    }

    const auto needleDb = juce::jlimit (-20.0f, 3.0f, rmsDb);
    const auto needleFraction = (needleDb + 20.0f) / 23.0f;
    const auto needleAngle = juce::jmap (needleFraction, startAngle, endAngle);
    const auto needleLength = radius - 13.0f;
    const auto needleEnd = juce::Point<float> (
        centreX + std::cos (needleAngle) * needleLength,
        centreY + std::sin (needleAngle) * needleLength);
    g.setColour (palette.needle.withAlpha (0.9f));
    g.drawLine (centreX + 0.8f, centreY + 0.8f, needleEnd.x + 0.8f, needleEnd.y + 0.8f, 2.0f);
    g.setColour (palette.needle);
    g.drawLine (centreX, centreY, needleEnd.x, needleEnd.y, 1.5f);
    g.setColour (palette.accent);
    g.fillEllipse (centreX - 4.0f, centreY - 4.0f, 8.0f, 8.0f);

    g.setColour (palette.gaugeInk.withAlpha (0.65f));
    g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
    g.drawText ("VU", juce::Rectangle<int> (juce::roundToInt (centreX - 19.0f),
                                            juce::roundToInt (centreY + 13.0f), 38, 13),
                juce::Justification::centred, false);

    const auto readoutY = getHeight() - 43;
    g.setColour (palette.text);
    g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
    g.drawText ("PEAK  " + formatDb (peakHoldDb),
                juce::Rectangle<int> (2, readoutY, getWidth() - 4, 17),
                juce::Justification::centred, false);
    g.setColour (palette.accent);
    g.drawText ("RMS   " + formatDb (rmsDb),
                juce::Rectangle<int> (2, readoutY + 17, getWidth() - 4, 17),
                juce::Justification::centred, false);
}

//==============================================================================
void FirstAudioProcessorEditor::CompressorMeter::setReduction (float reductionDb, float inputReductionDb,
                                                              float newActivity)
{
    // Smooth downwards instantly (so gain reduction is never under-reported) but
    // let the bar fall back gracefully, like a real VU-driven reduction needle.
    const auto clamped = juce::jlimit (-12.0f, 0.0f, reductionDb);
    const auto coefficient = clamped < displayedDb ? 0.55f : 0.10f;
    displayedDb += (clamped - displayedDb) * coefficient;

    const auto clampedInput = juce::jlimit (-12.0f, 0.0f, inputReductionDb);
    const auto inputCoefficient = clampedInput < displayedInputDb ? 0.55f : 0.10f;
    displayedInputDb += (clampedInput - displayedInputDb) * inputCoefficient;

    activity += (juce::jlimit (0.0f, 1.0f, newActivity) - activity) * 0.22f;

    repaint();
}

void FirstAudioProcessorEditor::CompressorMeter::paint (juce::Graphics& g)
{
    const auto& palette = paletteFor (darkTheme);
    const auto bounds = getLocalBounds().toFloat().reduced (2.0f);

    g.setColour (palette.raised.darker (0.12f));
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (palette.border.withAlpha (0.9f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, 1.0f);

    g.setColour (palette.text);
    g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    g.drawText ("COMP", getLocalBounds().removeFromTop (20), juce::Justification::centred, false);

    const auto trackTop = 26.0f;
    const auto trackBottom = static_cast<float> (getHeight()) - 46.0f;
    const auto trackLeft = 12.0f;
    const auto trackRight = static_cast<float> (getWidth()) - 12.0f;
    const auto trackHeight = juce::jmax (24.0f, trackBottom - trackTop);

    const auto centreX = 0.5f * (trackLeft + trackRight);
    const auto barWidth = juce::jmin (30.0f, (trackRight - trackLeft) * 0.44f);
    const auto barLeft = centreX - barWidth * 0.5f;

    // Segmented LED ladder: it reads as a classic hardware reduction display.
    constexpr int segments = 16;
    const auto segmentGap = 2.0f;
    const auto segmentHeight = (trackHeight - segmentGap * static_cast<float> (segments - 1))
                               / static_cast<float> (segments);
    const auto litFraction = juce::jlimit (0.0f, 1.0f, -displayedDb / 12.0f);
    const auto litSegments = static_cast<int> (std::ceil (litFraction * static_cast<float> (segments)));

    for (int segment = 0; segment < segments; ++segment)
    {
        const auto y = trackTop + static_cast<float> (segment) * (segmentHeight + segmentGap);
        const auto lit = segments - segment <= litSegments;
        const auto position = static_cast<float> (segment) / static_cast<float> (segments - 1);

        juce::Colour segmentColour = palette.status;
        if (position > 0.45f) segmentColour = palette.accent;
        if (position > 0.78f) segmentColour = juce::Colour::fromRGB (196, 74, 52);

        if (lit)
        {
            g.setColour (segmentColour.withAlpha (0.30f));
            g.fillRoundedRectangle (barLeft - 2.0f, y - 1.0f, barWidth + 4.0f,
                                    segmentHeight + 2.0f, 2.5f);
            g.setColour (segmentColour);
        }
        else
        {
            g.setColour (palette.border.withAlpha (0.35f));
        }

        g.fillRoundedRectangle (barLeft, y, barWidth, segmentHeight, 2.0f);
    }

    // Activity glow behind the ladder so the meter still shows the compressor
    // breathing during quiet passages where no reduction is happening.
    if (activity > 0.01f)
    {
        const auto glowHeight = trackHeight * juce::jlimit (0.0f, 1.0f, activity);
        g.setColour (palette.accent.withAlpha (0.10f + activity * 0.10f));
        g.fillRoundedRectangle (barLeft - 5.0f, trackBottom - glowHeight,
                                barWidth + 10.0f, glowHeight, 3.0f);
    }

    const auto readoutY = getHeight() - 42;
    const auto totalReduction = displayedDb < -0.05f;
    g.setColour (totalReduction ? palette.accent : palette.secondary);
    g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    g.drawText (juce::String (displayedDb, 1) + " dB",
                juce::Rectangle<int> (2, readoutY, getWidth() - 4, 16),
                juce::Justification::centred, false);

    // The two stages are labelled separately: IN is after the input trim, OUT is
    // the stage that sits immediately before the output trim.
    g.setColour (palette.secondary);
    g.setFont (juce::Font (juce::FontOptions (8.0f)));
    g.drawText ("IN " + juce::String (displayedInputDb, 1) + "  OUT "
                    + juce::String (totalReduction ? displayedDb - displayedInputDb : 0.0f, 1),
                juce::Rectangle<int> (2, readoutY + 16, getWidth() - 4, 13),
                juce::Justification::centred, false);
}

//==============================================================================
FirstAudioProcessorEditor::FirstAudioProcessorEditor (FirstAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setResizable (true, true);
    setResizeLimits (1000, 730, 1500, 1000);
    setSize (1180, 760);
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

    styleLabel (brandLabel, "ANALOG TAPE", 9.0f, paletteFor (false).accent, true, juce::Justification::left);
    titleLabel.setText ("J37", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions ("Georgia", "Bold", 30.0f)));
    titleLabel.setJustificationType (juce::Justification::left);
    titleLabel.setInterceptsMouseClicks (false, false);
    styleLabel (subtitleLabel, "TAPE MACHINE  /  SATURATION", 10.0f,
                paletteFor (false).secondary, true, juce::Justification::left);
    styleLabel (statusLabel, "STEREO / REAL TIME", 9.0f,
                paletteFor (false).status, true, juce::Justification::centred);    styleLabel (deckHeadingLabel, "TAPE DECK", 10.0f, paletteFor (false).accent,
                true, juce::Justification::left);
    styleLabel (tapeTypeLabel, "MODEL", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    styleLabel (speedLabel, "SPEED", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    styleLabel (deckHintLabel, "Select tape formula and transport speed.", 9.0f,
                paletteFor (false).secondary, false, juce::Justification::centredLeft);
    styleLabel (controlsHeadingLabel, "TAPE CHARACTER", 10.0f, paletteFor (false).accent,
                true, juce::Justification::left);
    styleLabel (controlsHintLabel, "Slow drag is precise. Hold Shift to fine tune.", 9.0f,
                paletteFor (false).secondary, false, juce::Justification::right);
    styleLabel (metersHeadingLabel, "LEVELS", 10.0f, paletteFor (false).accent,
                true, juce::Justification::left);
    styleLabel (metersHintLabel, "dBFS / PEAK + RMS", 8.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    styleLabel (compressorLabel, "TAPE GLUE x2", 10.0f, paletteFor (false).accent,
                true, juce::Justification::left);
    styleLabel (compressorReadout, "IN + OUT stage", 8.0f, paletteFor (false).secondary,
                false, juce::Justification::left);

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
    addAndMakeVisible (compressorLabel);
    addAndMakeVisible (compressorReadout);

    const juce::StringArray controlIds { "input", "drive", "bias",
                                         "tone", "wow", "flutter",
                                         "mix", "output", "stereo_width" };
    const juce::StringArray controlNames { "INPUT", "DRIVE", "BIAS",
                                           "TONE", "WOW", "FLUTTER",
                                           "MIX", "OUTPUT", "WIDTH" };
    const std::array<double, controlCount> defaultValues { 0.0, 0.42, 0.36,
                                                           0.58, 0.14, 0.18,
                                                           0.62, 0.0, 0.5 };

    for (std::size_t i = 0; i < controlCount; ++i)
    {
        auto& slider = controls[i];
        slider.setName (controlNames[static_cast<int> (i)]);
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 0.75f,
                                    juce::MathConstants<float>::pi * 2.25f, true);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 84, 20);
        slider.setTextBoxIsEditable (true);
        slider.setVelocityBasedMode (true);
        slider.setVelocityModeParameters (0.35, 1, 0.02, true, juce::ModifierKeys::shiftModifier);
        slider.setMouseDragSensitivity (600);
        slider.setPopupDisplayEnabled (true, true, this);
        slider.setScrollWheelEnabled (true);
        slider.setDoubleClickReturnValue (true, defaultValues[i]);
        slider.setLookAndFeel (&customLookAndFeel);
        slider.setColour (juce::Slider::textBoxOutlineColourId, paletteFor (false).border);
        slider.setTooltip ("Slow movement gives fine control. Hold Shift to adjust precisely. "
                           "Use the wheel for small steps. Double-click to reset.");

        if (i == 0 || i == 7)
        {
            // Input and Output are both calibrated decibel trims over the same range.
            slider.setRange (minStageDb, maxStageDb, 0.1);
            slider.setNumDecimalPlacesToDisplay (1);
            slider.setTextValueSuffix (" dB");

            if (i == 0)
            {
                slider.setTooltip ("Input trim. Drives the tape machine harder for more saturation. "
                                   "Range -32 to +32 dB. Double-click to reset to 0 dB.");
            }
            else
            {
                slider.setTooltip ("Output trim in decibels, matching the input control. "
                                   "Range -32 to +32 dB. Double-click to reset to 0 dB.");
            }
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
            if (i == 6)
            {
                slider.setTooltip ("Mix blends the dry signal with the tape path. "
                                   "0 % is fully dry, 100 % is fully through the tape. "
                                   "Double-click for the default 62 %.");
            }
            else if (i == controlCount - 1)
            {
                slider.setTooltip ("Stereo width: 0 percent is mono, 50 percent is natural stereo, "
                                   "100 percent is extra wide.");
            }
        }

        controlLabels[i].setText (controlNames[static_cast<int> (i)], juce::dontSendNotification);
        controlLabels[i].setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
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

    bypassButton.setClickingTogglesState (true);
    bypassButton.setButtonText ("BYPASS");
    bypassButton.setTooltip ("Hard bypass: the tape engine and both glue compressors are switched out. "
                             "The switch is ramped, so toggling it never clicks.");
    bypassButton.setLookAndFeel (&customLookAndFeel);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (audioProcessor.parameters, "bypass", bypassButton);

    themeButton.setButtonText ("DARK THEME");
    themeButton.setTooltip ("Switch between the ivory and charcoal front panels.");
    themeButton.setLookAndFeel (&customLookAndFeel);
    themeButton.onClick = [this]
    {
        darkTheme = ! darkTheme;
        applyTheme();
    };

    addAndMakeVisible (tapeTypeBox);
    addAndMakeVisible (speedBox);
    addAndMakeVisible (bypassButton);
    addAndMakeVisible (themeButton);
    tapeTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "tape_type", tapeTypeBox);
    speedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "speed", speedBox);

    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);
    addAndMakeVisible (compressorMeter);
    applyTheme();

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
    bypassButton.setLookAndFeel (nullptr);
    themeButton.setLookAndFeel (nullptr);
}

FirstAudioProcessorEditor::EditorLayout FirstAudioProcessorEditor::getEditorLayout() const
{
    auto remaining = getLocalBounds().reduced (18);
    EditorLayout layout;

    layout.header = remaining.removeFromTop (78);
    remaining.removeFromTop (12);
    layout.deck = remaining.removeFromTop (84);
    remaining.removeFromTop (12);

    layout.meters = remaining.removeFromRight (330);
    remaining.removeFromRight (14);
    layout.controls = remaining;

    return layout;
}

void FirstAudioProcessorEditor::applyTheme()
{
    const auto& palette = paletteFor (darkTheme);
    customLookAndFeel.setDarkTheme (darkTheme);
    inputMeter.setDarkTheme (darkTheme);
    outputMeter.setDarkTheme (darkTheme);
    {
        const juce::SpinLock::ScopedLockType lock (renderOrbsLock);
        for (std::size_t i = 0; i < decorativeOrbCount; ++i)
        {
            const auto orbColour = palette.accent.withAlpha (0.25f + static_cast<float> (i) * 0.04f);
            physicsOrbs[i].colour = orbColour;
            renderOrbs[i].colour = orbColour;
        }
    }

    brandLabel.setColour (juce::Label::textColourId, palette.accent);
    titleLabel.setColour (juce::Label::textColourId, palette.text);
    subtitleLabel.setColour (juce::Label::textColourId, palette.secondary);
    statusLabel.setColour (juce::Label::textColourId, palette.status);
    deckHeadingLabel.setColour (juce::Label::textColourId, palette.accent);
    tapeTypeLabel.setColour (juce::Label::textColourId, palette.secondary);
    speedLabel.setColour (juce::Label::textColourId, palette.secondary);
    deckHintLabel.setColour (juce::Label::textColourId, palette.secondary);
    controlsHeadingLabel.setColour (juce::Label::textColourId, palette.accent);
    controlsHintLabel.setColour (juce::Label::textColourId, palette.secondary);
    metersHeadingLabel.setColour (juce::Label::textColourId, palette.accent);
    metersHintLabel.setColour (juce::Label::textColourId, palette.secondary);

    for (auto& label : controlLabels)
        label.setColour (juce::Label::textColourId, palette.secondary);

    for (auto& slider : controls)
    {
        slider.setColour (juce::Slider::textBoxTextColourId, palette.text);
        slider.setColour (juce::Slider::textBoxBackgroundColourId, palette.readout);
        slider.setColour (juce::Slider::textBoxOutlineColourId, palette.border);
        slider.setColour (juce::Slider::thumbColourId, palette.accent);
    }

    const auto styleCombo = [&palette] (juce::ComboBox& box)
    {
        box.setColour (juce::ComboBox::backgroundColourId, palette.raised);
        box.setColour (juce::ComboBox::outlineColourId, palette.border);
        box.setColour (juce::ComboBox::textColourId, palette.text);
        box.setColour (juce::ComboBox::arrowColourId, palette.accent);
        box.setColour (juce::ComboBox::focusedOutlineColourId, palette.accent);
    };
    styleCombo (tapeTypeBox);
    styleCombo (speedBox);

    bypassButton.setColour (juce::TextButton::buttonColourId, palette.raised);
    bypassButton.setColour (juce::TextButton::buttonOnColourId, palette.needle);
    bypassButton.setColour (juce::TextButton::textColourOffId, palette.text);
    bypassButton.setColour (juce::TextButton::textColourOnId, palette.readout);
    themeButton.setColour (juce::TextButton::buttonColourId, palette.raised);
    themeButton.setColour (juce::TextButton::textColourOffId, palette.text);

    compressorLabel.setColour (juce::Label::textColourId, palette.accent);
    compressorReadout.setColour (juce::Label::textColourId, palette.secondary);
    compressorMeter.setDarkTheme (darkTheme);

    themeButton.setButtonText (darkTheme ? "LIGHT THEME" : "DARK THEME");
    repaint();
}

//==============================================================================
void FirstAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto& palette = paletteFor (darkTheme);
    g.fillAll (palette.background);

    const auto layout = getEditorLayout();
    drawPanel (g, getLocalBounds().reduced (8), palette, 7.0f);
    drawPanel (g, layout.header, palette, 5.0f);
    drawPanel (g, layout.deck, palette, 5.0f);
    drawPanel (g, layout.controls, palette, 5.0f);
    drawPanel (g, layout.meters, palette, 5.0f);

    const auto outer = getLocalBounds().reduced (8);
    const auto grainColour = palette.text.withAlpha (darkTheme ? 0.025f : 0.035f);
    g.setColour (grainColour);
    for (int line = 0; line < 18; ++line)
    {
        const auto y = outer.getY() + 9 + line * 4;
        g.drawHorizontalLine (y, static_cast<float> (outer.getX() + 8),
                              static_cast<float> (outer.getRight() - 8));
    }

    g.setColour (palette.accent.withAlpha (0.85f));
    g.fillRect (layout.header.getX() + 17, layout.header.getY() + 16, 3,
                layout.header.getHeight() - 32);

    g.setColour (palette.border.withAlpha (0.75f));
    g.drawHorizontalLine (layout.controls.getY() + 42,
                          static_cast<float> (layout.controls.getX() + 18),
                          static_cast<float> (layout.controls.getRight() - 18));
    g.drawHorizontalLine (layout.meters.getY() + 48,
                          static_cast<float> (layout.meters.getX() + 18),
                          static_cast<float> (layout.meters.getRight() - 18));
    g.drawHorizontalLine (layout.deck.getBottom() - 8,
                          static_cast<float> (layout.deck.getX() + 16),
                          static_cast<float> (layout.deck.getRight() - 16));

    const auto statusBadge = juce::Rectangle<float> (
        static_cast<float> (layout.header.getRight() - 205),
        static_cast<float> (layout.header.getY() + 24), 184.0f, 30.0f);
    g.setColour (palette.raised.darker (0.16f));
    g.fillRoundedRectangle (statusBadge, 4.0f);

    // Status lamp: pulses with the compressor, so the header shows that the
    // plugin is alive and working even when no gain reduction is happening.
    const auto lampCentre = juce::Point<float> (statusBadge.getX() + 16.0f,
                                                statusBadge.getCentreY());
    const auto lampPulse = 4.5f + 2.5f * glowAmount;
    g.setColour (palette.status.withAlpha (0.12f + 0.30f * glowAmount));
    g.fillEllipse (lampCentre.x - lampPulse * 2.2f, lampCentre.y - lampPulse * 2.2f,
                   lampPulse * 4.4f, lampPulse * 4.4f);
    g.setColour (palette.status);
    g.fillEllipse (lampCentre.x - 3.5f, lampCentre.y - 3.5f, 7.0f, 7.0f);

    for (const auto point : { juce::Point<float> (layout.header.getX() + 9.0f, layout.header.getY() + 9.0f),
                              juce::Point<float> (layout.header.getRight() - 9.0f, layout.header.getY() + 9.0f),
                              juce::Point<float> (layout.header.getX() + 9.0f, layout.header.getBottom() - 9.0f),
                              juce::Point<float> (layout.header.getRight() - 9.0f, layout.header.getBottom() - 9.0f) })
        drawScrew (g, point.x, point.y, palette);

    const auto reelCentre = juce::Point<float> (static_cast<float> (layout.deck.getRight() - 64),
                                                static_cast<float> (layout.deck.getCentreY() + 5));
    g.setColour (palette.accent.withAlpha (0.22f));
    for (const auto radius : { 19.0f, 12.0f, 3.0f })
        g.drawEllipse (reelCentre.x - radius, reelCentre.y - radius,
                       radius * 2.0f, radius * 2.0f, 1.1f);
    g.drawLine (reelCentre.x - 24.0f, reelCentre.y, reelCentre.x + 24.0f, reelCentre.y, 0.8f);
    g.drawLine (reelCentre.x, reelCentre.y - 24.0f, reelCentre.x, reelCentre.y + 24.0f, 0.8f);

    // Spokes: density tracks the selected tape speed, drift tracks the modulation.
    const auto spokeAlpha = 0.20f + 0.45f * glowAmount;
    for (int spoke = 0; spoke < 6; ++spoke)
    {
        const auto spokeAngle = reelAngle + static_cast<float> (spoke)
                                              * juce::MathConstants<float>::pi / 3.0f;
        const auto spokeInner = juce::Point<float> (reelCentre.x + std::cos (spokeAngle) * 4.0f,
                                                    reelCentre.y + std::sin (spokeAngle) * 4.0f);
        const auto spokeOuter = juce::Point<float> (reelCentre.x + std::cos (spokeAngle) * 18.5f,
                                                    reelCentre.y + std::sin (spokeAngle) * 18.5f);
        g.setColour (palette.accent.withAlpha (spokeAlpha));
        g.drawLine (spokeInner.x, spokeInner.y, spokeOuter.x, spokeOuter.y, 1.4f);

        g.setColour (palette.readout.withAlpha (0.10f + 0.25f * glowAmount));
        g.fillEllipse (spokeOuter.x - 1.6f, spokeOuter.y - 1.6f, 3.2f, 3.2f);
    }

    // Tape ribbon between the reel and the transport, drawn with a slight sag
    // that breathes with the wow/flutter drift.
    const auto sag = (driftAmount - 0.5f) * 5.0f;
    juce::Path tapeRibbon;
    tapeRibbon.startNewSubPath (reelCentre.x - 22.0f, reelCentre.y + 18.0f);
    tapeRibbon.quadraticTo (static_cast<float> (layout.deck.getX() + 6),
                            static_cast<float> (layout.deck.getBottom()) + 6.0f + sag,
                            static_cast<float> (layout.deck.getX() + 4),
                            static_cast<float> (layout.deck.getCentreY()) + sag);
    g.setColour (palette.knobEdge.withAlpha (0.35f));
    g.strokePath (tapeRibbon, juce::PathStrokeType (1.6f));

    std::array<RenderOrb, decorativeOrbCount> orbsToDraw;
    {
        const juce::SpinLock::ScopedLockType lock (renderOrbsLock);
        orbsToDraw = renderOrbs;
    }

    for (const auto& orb : orbsToDraw)
    {
        const auto x = juce::jmap (orb.position.x, 0.0f, 1.5f,
                                   static_cast<float> (layout.deck.getX()) + layout.deck.getWidth() * 0.69f,
                                   static_cast<float> (layout.deck.getRight()) - 28.0f);
        const auto y = juce::jmap (orb.position.y, 0.0f, 1.2f,
                                   static_cast<float> (layout.deck.getY()) + 14.0f,
                                   static_cast<float> (layout.deck.getBottom()) - 14.0f);
        const auto radius = juce::jmap (orb.radius, 0.045f, 0.057f, 2.5f, 4.5f);

        // Halos breathe so the orbs read as particles rather than static dots.
        const auto haloScale = 1.8f + 0.6f * std::sin (glowPhase + orb.position.x * 6.0f);
        g.setColour (orb.colour.withAlpha (0.07f + 0.05f * glowAmount));
        g.fillEllipse (x - radius * haloScale, y - radius * haloScale,
                       radius * haloScale * 2.0f, radius * haloScale * 2.0f);
        g.setColour (orb.colour.withAlpha (0.17f + 0.14f * glowAmount));
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
        const auto position = b2Vec2 (0.30f + static_cast<float> (index) * 0.18f,
                                      0.28f + static_cast<float> (index % 3) * 0.16f);

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

        const auto colour = paletteFor (false).accent.withAlpha (
            0.25f + static_cast<float> (index) * 0.04f);
        physicsOrbs[i] = { body, radius, colour };
        renderOrbs[i] = { juce::Point<float> (position.x, position.y), radius, colour };
    }
}

void FirstAudioProcessorEditor::timerCallback()
{
    inputMeter.setLevels (audioProcessor.getInputPeakLevel(), audioProcessor.getInputRmsLevel());
    outputMeter.setLevels (audioProcessor.getOutputPeakLevel(), audioProcessor.getOutputRmsLevel());

    // Compressor display: total gain reduction of the two glue stages plus the
    // live activity envelope, with the input and output stages shown separately.
    const auto reduction = audioProcessor.getGainReductionDb();
    const auto inputReduction = audioProcessor.getInputGainReductionDb();
    const auto activity = audioProcessor.getCompressorActivity();
    compressorMeter.setReduction (reduction, inputReduction, activity);

    // Animated presentation state. Everything here is derived from audio
    // telemetry, so the panel visibly reacts to what the plugin is doing.
    glowPhase += 0.13f;
    if (glowPhase > juce::MathConstants<float>::twoPi)
        glowPhase -= juce::MathConstants<float>::twoPi;

    const auto targetGlow = juce::jlimit (0.0f, 1.0f,
                                          activity * 0.7f + std::abs (reduction) / 6.0f);
    glowAmount += (targetGlow - glowAmount) * 0.18f;

    const auto drift = audioProcessor.getTransportDrift();
    driftAmount += (drift - driftAmount) * 0.25f;

    // The header status text follows the real bypass state of the processor.
    const auto bypassed = audioProcessor.isBypassed();
    if (bypassed != currentBypassDisplay)
    {
        currentBypassDisplay = bypassed;
        statusLabel.setText (bypassed ? "BYPASSED / DRY" : "STEREO / REAL TIME",
                             juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId,
                               bypassed ? paletteFor (darkTheme).secondary
                                        : paletteFor (darkTheme).status);
    }

    // The reel spins according to the selected tape speed and the live drift.
    const auto speedIndex = speedBox.getSelectedId() - 1;
    const auto speedScale = speedIndex == 0 ? 0.55f : (speedIndex == 1 ? 0.85f : 1.25f);
    reelSpeed += ((speedScale * (0.9f + driftAmount * 0.5f)) - reelSpeed) * 0.08f;
    reelAngle += reelSpeed * 0.09f;
    if (reelAngle > juce::MathConstants<float>::twoPi)
        reelAngle -= juce::MathConstants<float>::twoPi;

    customLookAndFeel.setActivity (glowAmount);
    customLookAndFeel.setDrift (driftAmount);
    customLookAndFeel.advanceFrame();

    // Repaint only what actually animates: the header lamp, the deck (reel +
    // particles) and the control area (knob halos and tracers).
    const auto layout = getEditorLayout();
    repaint (layout.header);
    repaint (layout.deck);
    repaint (layout.controls);

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
}

void FirstAudioProcessorEditor::resized()
{
    const auto layout = getEditorLayout();

    brandLabel.setBounds (layout.header.getX() + 28, layout.header.getY() + 12, 150, 14);
    titleLabel.setBounds (layout.header.getX() + 25, layout.header.getY() + 23, 96, 44);
    subtitleLabel.setBounds (layout.header.getX() + 126, layout.header.getY() + 39, 310, 20);
    themeButton.setBounds (layout.header.getRight() - 348, layout.header.getY() + 24, 126, 30);
    statusLabel.setBounds (layout.header.getRight() - 202, layout.header.getY() + 24, 181, 30);

    deckHeadingLabel.setBounds (layout.deck.getX() + 18, layout.deck.getY() + 8, 150, 17);
    tapeTypeLabel.setBounds (layout.deck.getX() + 18, layout.deck.getY() + 39, 45, 20);
    tapeTypeBox.setBounds (layout.deck.getX() + 68, layout.deck.getY() + 32, 208, 35);
    speedLabel.setBounds (layout.deck.getX() + 296, layout.deck.getY() + 39, 45, 20);
    speedBox.setBounds (layout.deck.getX() + 345, layout.deck.getY() + 32, 150, 35);
    bypassButton.setBounds (layout.deck.getX() + 515, layout.deck.getY() + 32, 118, 35);
    deckHintLabel.setBounds (layout.deck.getX() + 647, layout.deck.getY() + 31,
                             juce::jmax (130, layout.deck.getWidth() - 735), 36);

    controlsHeadingLabel.setBounds (layout.controls.getX() + 18, layout.controls.getY() + 12, 210, 19);
    controlsHintLabel.setBounds (layout.controls.getRight() - 360, layout.controls.getY() + 12,
                                 342, 19);
    metersHeadingLabel.setBounds (layout.meters.getX() + 18, layout.meters.getY() + 8, 100, 18);
    metersHintLabel.setBounds (layout.meters.getX() + 18, layout.meters.getY() + 27, 150, 14);
    compressorLabel.setBounds (layout.meters.getX() + 190, layout.meters.getY() + 8, 120, 18);
    compressorReadout.setBounds (layout.meters.getX() + 190, layout.meters.getY() + 27, 120, 14);

    auto grid = layout.controls.reduced (14);
    grid.removeFromTop (42);
    grid.removeFromBottom (8);
    const auto cellWidth = grid.getWidth() / controlColumns;
    const auto rowHeight = grid.getHeight() / 3;

    for (std::size_t i = 0; i < controlCount; ++i)
    {
        const auto row = static_cast<int> (i / controlColumns);
        const auto column = static_cast<int> (i % controlColumns);
        auto cell = juce::Rectangle<int> (grid.getX() + column * cellWidth,
                                          grid.getY() + row * rowHeight,
                                          column == controlColumns - 1
                                              ? grid.getRight() - (grid.getX() + column * cellWidth)
                                              : cellWidth,
                                          row == 2
                                              ? grid.getBottom() - (grid.getY() + row * rowHeight)
                                              : rowHeight);
        controlLabels[i].setBounds (cell.getX() + 5, cell.getY() + 1,
                                    cell.getWidth() - 10, 17);
        auto sliderBounds = cell.reduced (5);
        sliderBounds.removeFromTop (18);
        controls[i].setBounds (sliderBounds);
    }

    const auto meterWidth = (layout.meters.getWidth() - 58) / 2;
    const auto meterY = layout.meters.getY() + 53;
    const auto meterHeight = layout.meters.getHeight() - 67;
    inputMeter.setBounds (layout.meters.getX() + 14, meterY, meterWidth, meterHeight);
    outputMeter.setBounds (layout.meters.getRight() - 14 - meterWidth, meterY,
                           meterWidth, meterHeight);

    // Compressor reduction bar sits across the foot of the meters panel.
    const auto compressorHeight = juce::jlimit (54, 78, layout.meters.getHeight() / 4);
    compressorMeter.setBounds (layout.meters.getX() + 14,
                               layout.meters.getBottom() - compressorHeight - 6,
                               layout.meters.getWidth() - 28,
                               compressorHeight);
}
