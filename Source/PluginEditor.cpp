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

    // NOTE: this struct is aggregate-initialised positionally below, so the field order here
    // and the order of the colour literals in each palette must stay in step. `card` used to
    // sit third and was never read, which meant every colour after it was one field adrift of
    // its name; it has been removed and the literals below follow this order exactly.
    // If you add a field, add it at the end and append its literal to both palettes.

    const UiPalette ivoryPalette {
        juce::Colour::fromRGB (197, 184, 157),   // background
        juce::Colour::fromRGB (222, 210, 187),   // panel
        juce::Colour::fromRGB (231, 220, 197),   // raised
        juce::Colour::fromRGB (111, 97, 73),     // border
        juce::Colour::fromRGB (145, 96, 42),     // accent
        juce::Colour::fromRGB (45, 38, 29),      // text
        juce::Colour::fromRGB (102, 91, 72),     // secondary
        juce::Colour::fromRGB (179, 162, 130),   // knobFace
        juce::Colour::fromRGB (217, 203, 176),   // knobHighlight
        juce::Colour::fromRGB (93, 78, 55),      // knobEdge
        juce::Colour::fromRGB (245, 238, 222),   // readout
        juce::Colour::fromRGB (239, 229, 203),   // gaugeFace
        juce::Colour::fromRGB (49, 43, 33),      // gaugeInk
        juce::Colour::fromRGB (132, 47, 37),     // needle
        juce::Colour::fromRGB (62, 111, 72)      // status
    };

    const UiPalette charcoalPalette {
        juce::Colour::fromRGB (28, 25, 21),      // background
        juce::Colour::fromRGB (54, 46, 35),      // panel
        juce::Colour::fromRGB (84, 70, 50),      // raised
        juce::Colour::fromRGB (144, 117, 72),    // border
        juce::Colour::fromRGB (211, 166, 83),    // accent
        juce::Colour::fromRGB (244, 231, 204),   // text
        juce::Colour::fromRGB (190, 173, 140),   // secondary
        juce::Colour::fromRGB (89, 72, 51),      // knobFace
        juce::Colour::fromRGB (126, 105, 73),    // knobHighlight
        juce::Colour::fromRGB (209, 172, 103),   // knobEdge
        juce::Colour::fromRGB (35, 30, 24),      // readout
        juce::Colour::fromRGB (226, 210, 173),   // gaugeFace
        juce::Colour::fromRGB (48, 39, 28),      // gaugeInk
        juce::Colour::fromRGB (132, 47, 37),     // needle
        juce::Colour::fromRGB (122, 174, 128)    // status
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
FirstAudioProcessorEditor::LevelMeter::LevelMeter (juce::String meterTitle, juce::String meterSubtitle)
    : title (std::move (meterTitle)), subtitle (std::move (meterSubtitle))
{
    setOpaque (false);
}

void FirstAudioProcessorEditor::LevelMeter::setLoudness (float peakDbIn, float rmsDbIn,
                                                        float lufsIn, float vuDbIn,
                                                        float combinedDbIn, bool clippingIn)
{
    constexpr float floorDb = -70.0f;
    constexpr float frameSeconds = 1.0f / 30.0f;

    peakDb = juce::jlimit (floorDb, 6.0f, peakDbIn);
    rmsDb = juce::jlimit (floorDb, 6.0f, rmsDbIn);
    lufs = juce::jlimit (floorDb, 6.0f, lufsIn);
    vuDb = juce::jlimit (floorDb, 6.0f, vuDbIn);
    combinedDb = juce::jlimit (floorDb, 6.0f, combinedDbIn);
    clipping = clippingIn;

    // Each view gets a ballistic matched to what it represents: the combined reading and
    // the RMS rise quickly and fall slowly, the VU is left slow because that is its whole
    // point, and the K-weighted LUFS is already time-averaged in the DSP.
    const auto smoothTowards = [] (float current, float target, float rise, float fall)
    {
        const auto coefficient = target > current ? rise : fall;
        return current + (target - current) * coefficient;
    };

    displayedRms = smoothTowards (displayedRms, rmsDb, 0.55f, 0.15f);
    displayedLufs = smoothTowards (displayedLufs, lufs, 0.30f, 0.10f);
    displayedVu = smoothTowards (displayedVu, vuDb, 0.18f, 0.18f);
    displayedCombined = smoothTowards (displayedCombined, combinedDb, 0.45f, 0.14f);

    // Peak hold, so a fast transient stays readable instead of flashing past.
    if (peakDb >= peakHoldDb)
    {
        peakHoldDb = peakDb;
        peakHoldTime = 0.65f;
    }
    else if (peakHoldTime > 0.0f)
    {
        peakHoldTime = juce::jmax (0.0f, peakHoldTime - frameSeconds);
    }
    else
    {
        peakHoldDb = juce::jmax (peakDb, peakHoldDb - 18.0f * frameSeconds);
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
    // A local text scale derived from the meter's own size (not from the display), so
    // the labels grow with the dial instead of staying at a fixed 8-10 px and becoming
    // unreadable in a large cell or oversized in a small one.
    const auto textScale = juce::jlimit (0.85f, 1.6f,
                                         static_cast<float> (getWidth()) / 190.0f * 1.0f);

    g.setColour (palette.text);
    g.setFont (juce::Font (juce::FontOptions (10.0f * textScale, juce::Font::bold)));
    g.drawText (title, getLocalBounds().removeFromTop (juce::roundToInt (20.0f * textScale)),
                juce::Justification::centred, false);

    g.setColour (palette.secondary);
    g.setFont (juce::Font (juce::FontOptions (8.0f * textScale)));
    g.drawText (subtitle,
                getLocalBounds().removeFromTop (juce::roundToInt (32.0f * textScale))
                                .withTrimmedTop (juce::roundToInt (18.0f * textScale)),
                juce::Justification::centred, false);

    // Everything below is derived from this component's own bounds rather than from
    // fixed pixel offsets, so the dial stays centred and correctly scaled whether the
    // meter is a tall single-column VU or one cell of the 2 x 2 grid, and at any
    // display scale factor. `bounds` is the same rectangle the frame above was drawn
    // from, so it is reused rather than recomputed.
    const auto labelArea = 44.0f;
    const auto readoutArea = 46.0f;
    const auto face = bounds.withTrimmedTop (labelArea).withTrimmedBottom (readoutArea);

    const auto centreX = face.getCentreX();
    // The dial is a half circle sitting on the lower edge of the face area.
    const auto radius = juce::jmin (face.getWidth() * 0.5f - 10.0f, face.getHeight() * 0.72f);
    const auto centreY = face.getBottom() - 6.0f;
    const auto startAngle = juce::MathConstants<float>::pi;
    const auto endAngle = juce::MathConstants<float>::twoPi;

    const auto dial = juce::Rectangle<float> (centreX - radius - 8.0f,
                                              centreY - radius - 10.0f,
                                              (radius + 8.0f) * 2.0f,
                                              radius + 16.0f);
    g.setColour (palette.knobEdge.withAlpha (0.55f));
    g.fillRoundedRectangle (dial, 4.0f);
    g.setColour (palette.gaugeFace);
    g.fillRoundedRectangle (dial.reduced (3.0f), 3.0f);
    g.setColour (palette.border);
    g.drawRoundedRectangle (dial.reduced (3.0f), 3.0f, 0.9f);

    // The tick marks and their labels only make sense once the dial is big enough to
    // separate them, so they are drawn from an adaptive count rather than always ten.
    const auto tickCount = radius > 34.0f ? 10 : 6;

    juce::Path scaleArc;
    scaleArc.addCentredArc (centreX, centreY, radius, radius, 0.0f,
                            startAngle, endAngle, true);
    g.setColour (palette.gaugeInk.withAlpha (0.8f));
    g.strokePath (scaleArc, juce::PathStrokeType (1.1f));

    for (int tick = 0; tick <= tickCount; ++tick)
    {
        const auto fraction = static_cast<float> (tick) / static_cast<float> (tickCount);
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
            const auto labelRadius = juce::jmax (4.0f, radius - 18.0f);
            const auto labelPoint = juce::Point<float> (
                centreX + std::cos (angle) * labelRadius,
                centreY + std::sin (angle) * labelRadius);
            g.setFont (juce::Font (juce::FontOptions (8.0f * textScale)));
            g.drawText (juce::String (db > 0 ? "+" : "") + juce::String (db),
                        juce::Rectangle<int> (juce::roundToInt (labelPoint.x - 10.0f),
                                              juce::roundToInt (labelPoint.y - 5.0f), 20, 11),
                        juce::Justification::centred, false);
        }
    }

    g.setColour (palette.gaugeInk.withAlpha (0.65f));
    g.setFont (juce::Font (juce::FontOptions (9.0f * textScale, juce::Font::bold)));
    g.drawText ("VU", juce::Rectangle<int> (juce::roundToInt (centreX - 19.0f),
                                            juce::roundToInt (centreY + 13.0f), 38, 13),
                juce::Justification::centred, false);

    // The needle rides the combined 25 %-each reading, so the dial shows one trustworthy
    // number while the text block below shows exactly how that number was arrived at.
    const auto needleDb = juce::jlimit (-20.0f, 3.0f, displayedCombined);
    const auto needleFraction = (needleDb + 20.0f) / 23.0f;
    const auto needleAngle = juce::jmap (needleFraction, startAngle, endAngle);
    const auto needleLength = juce::jmax (4.0f, radius - 13.0f);
    const auto needleEnd = juce::Point<float> (
        centreX + std::cos (needleAngle) * needleLength,
        centreY + std::sin (needleAngle) * needleLength);
    g.setColour (palette.needle.withAlpha (0.9f));
    g.drawLine (centreX + 0.8f, centreY + 0.8f, needleEnd.x + 0.8f, needleEnd.y + 0.8f, 2.0f);
    g.setColour (palette.needle);
    g.drawLine (centreX, centreY, needleEnd.x, needleEnd.y, 1.5f);
    g.setColour (palette.accent);
    g.fillEllipse (centreX - 4.0f, centreY - 4.0f, 8.0f, 8.0f);

    // -------------------------------------------------------------------
    //  Four-way readout.
    //
    //  Each scale gets its own row because they answer different questions and their
    //  spread is meaningful: PEAK against LUFS shows how dynamic the material is, and
    //  the gap between VU and RMS shows how much transient content is present.
    //  The combined row is the equal-weighted average of the four, which is what the
    //  needle and the dial are driven from.
    // -------------------------------------------------------------------
    const auto readoutTop = getHeight() - juce::roundToInt (62.0f * textScale);
    const auto rowHeight = juce::roundToInt (15.0f * textScale);

    const auto drawReadoutRow = [&] (const juce::String& label, float value,
                                     juce::Colour valueColour, int row)
    {
        const auto rowArea = juce::Rectangle<int> (2, readoutTop + row * rowHeight,
                                                   getWidth() - 4, rowHeight);
        g.setColour (palette.secondary);
        g.setFont (juce::Font (juce::FontOptions (8.0f * textScale)));
        g.drawText (label, rowArea, juce::Justification::centredLeft, false);

        g.setColour (valueColour);
        g.setFont (juce::Font (juce::FontOptions (8.5f * textScale, juce::Font::bold)));
        g.drawText (formatDb (value), rowArea, juce::Justification::centredRight, false);
    };

    // The clipping lamp: only the peak view can clip, so it is flagged next to that row.
    const auto peakColour = clipping ? juce::Colour::fromRGB (208, 82, 58) : palette.text;
    drawReadoutRow ("PEAK dB", peakHoldDb, peakColour, 0);
    drawReadoutRow ("RMS", displayedRms, palette.text, 1);
    drawReadoutRow ("LUFS", displayedLufs, palette.accent, 2);
    drawReadoutRow ("VU", displayedVu, palette.text, 3);
    drawReadoutRow ("MIX 25%", displayedCombined, palette.accent, 4);
}
//==============================================================================
FirstAudioProcessorEditor::CompressorMeter::CompressorMeter (juce::String meterTitle,
                                                             juce::String stageCaption)
    : title (std::move (meterTitle)), caption (std::move (stageCaption))
{
    setInterceptsMouseClicks (false, false);
}

void FirstAudioProcessorEditor::CompressorMeter::setReduction (float reductionDb, float newActivity)
{
    // Smooth downwards instantly (so gain reduction is never under-reported) but
    // let the bar fall back gracefully, like a real VU-driven reduction needle.
    const auto clamped = juce::jlimit (-12.0f, 0.0f, reductionDb);
    const auto coefficient = clamped < displayedDb ? 0.55f : 0.10f;
    displayedDb += (clamped - displayedDb) * coefficient;

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

    // Text and ladder scale with this meter's own size, so the reduction display stays
    // legible whether it is one cell of the 2 x 2 grid or a large panel, without any
    // dependence on the host's display scale.
    const auto textScale = juce::jlimit (0.85f, 1.6f, static_cast<float> (getWidth()) / 190.0f);

    g.setColour (palette.text);
    g.setFont (juce::Font (juce::FontOptions (10.0f * textScale, juce::Font::bold)));
    g.drawText (title, getLocalBounds().removeFromTop (juce::roundToInt (22.0f * textScale)),
                juce::Justification::centred, false);

    const auto trackTop = 26.0f * textScale;
    const auto trackBottom = static_cast<float> (getHeight()) - 46.0f * textScale;
    const auto trackLeft = 12.0f;
    const auto trackRight = static_cast<float> (getWidth()) - 12.0f;
    const auto trackHeight = juce::jmax (24.0f, trackBottom - trackTop);

    const auto centreX = 0.5f * (trackLeft + trackRight);
    const auto barWidth = juce::jmin (30.0f * textScale, (trackRight - trackLeft) * 0.44f);
    const auto barLeft = centreX - barWidth * 0.5f;

    // Segmented LED ladder: it reads as a classic hardware reduction display. The
    // segment count is chosen from the available height so the ladder fills the cell
    // instead of leaving gaps in a tall layout or overlapping in a short one.
    const auto segments = juce::jlimit (8, 24, juce::roundToInt (trackHeight / (7.0f * textScale)));
    const auto segmentGap = 2.0f * textScale;
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

    const auto readoutY = getHeight() - juce::roundToInt (42.0f * textScale);
    const auto reducing = displayedDb < -0.05f;
    g.setColour (reducing ? palette.accent : palette.secondary);
    g.setFont (juce::Font (juce::FontOptions (10.0f * textScale, juce::Font::bold)));
    g.drawText (juce::String (displayedDb, 1) + " dB",
                juce::Rectangle<int> (2, readoutY, getWidth() - 4, 16),
                juce::Justification::centred, false);

    // Each meter now belongs to exactly one stage, so the caption names the point in
    // the chain that stage sits at rather than splitting one readout into IN and OUT.
    g.setColour (palette.secondary);
    g.setFont (juce::Font (juce::FontOptions (8.0f * textScale)));
    g.drawText (caption,
                juce::Rectangle<int> (2, readoutY + 16, getWidth() - 4, 13),
                juce::Justification::centred, false);
}

//==============================================================================
FirstAudioProcessorEditor::FirstAudioProcessorEditor (FirstAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setResizable (true, true);

    // The editor is deliberately sized in LOGICAL units and the DPI scale is NOT applied
    // here. JUCE already handles display scaling for plugin editors: component coordinates
    // are logical, and the host (or JUCE on the desktop) multiplies them by the display
    // scale when it maps them to physical pixels. Multiplying setSize() by the display
    // scale as well applies it a second time, which is why the window opened far too large
    // - on a 150 % display this asked for 1860 x 1230 physical pixels for a 1240 x 820
    // panel. The layout code below is all relative to getLocalBounds(), so it needs no
    // knowledge of DPI at all.
    setResizeLimits (1000, 760, 1700, 1100);
    setSize (1240, 820);
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

    // Serif display face with a cross-platform fallback chain. "Georgia" only exists on
    // Windows, so naming it alone made the title fall back to an arbitrary face (and an
    // unpredictable width) on macOS and Linux. FontOptions accepts a comma-separated
    // list and uses the first family that resolves, ending in a generic fallback.
    titleLabel.setFont (juce::Font (juce::FontOptions ("Georgia, Times New Roman, Times, serif",
                                                       30.0f, juce::Font::bold)));
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
    styleLabel (compressorLabel, "GLUE x2", 10.0f, paletteFor (false).accent,
                true, juce::Justification::right);
    styleLabel (compressorReadout, "one meter per stage", 8.0f, paletteFor (false).secondary,
                false, juce::Justification::right);
    styleLabel (harmonicsLabel, "HARMONICS", 10.0f, paletteFor (false).accent,
                true, juce::Justification::centredRight);
    styleLabel (harmonicsReadout, "even / odd", 8.0f, paletteFor (false).secondary,
                false, juce::Justification::centredRight);

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
    addAndMakeVisible (harmonicsLabel);
    addAndMakeVisible (harmonicsReadout);

    const juce::StringArray controlIds { "input", "drive", "bias",
                                         "tone", "wow", "flutter",
                                         "mix", "output", "stereo_width" };
    const juce::StringArray controlNames { "INPUT", "DRIVE", "BIAS",
                                           "BRIGHT", "WOW", "FLUTTER",
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
            if (i == 3)
            {
                slider.setTooltip ("Brightness sets how much top end survives the tape. "
                                   "Low is warm, soft and rolled off; high is open and airy. "
                                   "A faster tape speed keeps more top end at the same setting. "
                                   "Double-click for the default 58 %.");
            }
            else if (i == 6)
            {
                slider.setTooltip ("Mix blends the dry signal with the tape path. "
                                   "0 % is fully dry, 100 % is fully through the tape. "
                                   "Double-click for the default 50 %.");
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
    addAndMakeVisible (compressorMeterIn);
    addAndMakeVisible (compressorMeterOut);
    applyTheme();

    // OpenGL is only an optimisation for the animated panel, and it is the least
    // portable part of the UI: some Windows drivers, remote sessions, virtual machines
    // and headless hosts cannot create a context at all. Attaching unconditionally
    // means those setups get a broken or blank editor, so the context is treated as a
    // best-effort accelerator. If it cannot attach, the component renderer draws the
    // same panel through the software path with no visual difference.
    openGLContext.setComponentPaintingEnabled (true);
    openGLContext.setContinuousRepainting (false);

    // attachTo() returns void, so it cannot be tested directly - it would read as
    // `if (!void)`, which is not a valid expression. The documented way to find out whether
    // the context came up is to ask afterwards, and detach if it did not so the component
    // renderer draws the panel instead.
    openGLContext.attachTo (*this);

    if (! openGLContext.isAttached())
        openGLContext.detach();

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

    // Header and deck are fixed-height, so they keep their proportions at small panel
    // sizes and on high-DPI displays. The rest of the height goes to controls + meters.
    layout.header = remaining.removeFromTop (78);
    remaining.removeFromTop (12);
    layout.deck = remaining.removeFromTop (84);
    remaining.removeFromTop (12);

    // The meters panel has to hold a 2 x 2 grid of dials, so it claims a share of the
    // width rather than a fixed pixel count. That keeps both rows legible whether the
    // editor is at its minimum size or opened large.
    const auto metersWidth = juce::jlimit (320, 460, juce::roundToInt (static_cast<float> (remaining.getWidth()) * 0.30f));
    layout.meters = remaining.removeFromRight (metersWidth);
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
    harmonicsLabel.setColour (juce::Label::textColourId, palette.accent);
    harmonicsReadout.setColour (juce::Label::textColourId, palette.secondary);
    compressorMeterIn.setDarkTheme (darkTheme);
    compressorMeterOut.setDarkTheme (darkTheme);

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
    // Four meters: the two level meters each show the full four-way loudness reading
    // (peak dB, RMS, LUFS, VU and their equal-weighted combination), and each glue
    // compressor stage gets its own reduction meter fed from its own telemetry, so the
    // panel shows both what the signal level is doing and where the work is being done.
    inputMeter.setLoudness (audioProcessor.getInputPeakDb(),
                            audioProcessor.getInputRmsDb(),
                            audioProcessor.getInputLufs(),
                            audioProcessor.getInputVuDb(),
                            audioProcessor.getInputCombinedDb(),
                            audioProcessor.isInputClipping());
    outputMeter.setLoudness (audioProcessor.getOutputPeakDb(),
                             audioProcessor.getOutputRmsDb(),
                             audioProcessor.getOutputLufs(),
                             audioProcessor.getOutputVuDb(),
                             audioProcessor.getOutputCombinedDb(),
                             audioProcessor.isOutputClipping());

    compressorMeterIn.setReduction (audioProcessor.getInputGainReductionDb(),
                                    audioProcessor.getInputCompressorActivity());
    compressorMeterOut.setReduction (audioProcessor.getOutputGainReductionDb(),
                                     audioProcessor.getOutputCompressorActivity());

    const auto reduction = audioProcessor.getGainReductionDb();
    const auto activity = audioProcessor.getCompressorActivity();

    // Report the harmonic balance the tape stage is producing. Even and odd are shown
    // side by side because the ratio between them is the character: even-dominant reads as
    // warm and full, odd-dominant as hard and edgy, and real tape has both.
    const auto evenRatio = audioProcessor.getEvenHarmonicRatio();
    const auto oddRatio = audioProcessor.getOddHarmonicRatio();
    harmonicsReadout.setText ("E " + juce::String (evenRatio * 100.0f, 1) + " %"
                                  + "   O " + juce::String (oddRatio * 100.0f, 1) + " %",
                              juce::dontSendNotification);

    // Colour the readout by which family dominates, so the character is readable at a
    // glance without needing to compare the numbers.
    const auto harmonicPalette = paletteFor (darkTheme);
    const auto totalHarmonics = evenRatio + oddRatio;
    harmonicsReadout.setColour (juce::Label::textColourId,
                                totalHarmonics < 0.001f ? harmonicPalette.secondary
                                : evenRatio >= oddRatio ? harmonicPalette.status
                                                        : harmonicPalette.needle);

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
    // getSelectedId() returns 0 while nothing is selected, which would index the
    // combo's item list at -1, so the index is clamped before it is used.
    const auto speedIndex = juce::jlimit (0, 2, speedBox.getSelectedId() - 1);
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
    metersHeadingLabel.setBounds (layout.meters.getX() + 16, layout.meters.getY() + 8, 130, 18);
    metersHintLabel.setBounds (layout.meters.getX() + 16, layout.meters.getY() + 27, 150, 14);
    compressorLabel.setBounds (layout.meters.getRight() - 176, layout.meters.getY() + 8, 160, 18);
    compressorReadout.setBounds (layout.meters.getRight() - 176, layout.meters.getY() + 27, 160, 14);

    // The harmonic readout sits in the free space at the right of the deck row, which is
    // where a real machine would print its meter calibration.
    const auto harmonicsWidth = 150;
    harmonicsLabel.setBounds (layout.deck.getRight() - harmonicsWidth - 18,
                              layout.deck.getY() + 8, harmonicsWidth, 18);
    harmonicsReadout.setBounds (layout.deck.getRight() - harmonicsWidth - 18,
                                layout.deck.getY() + 27, harmonicsWidth, 16);

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

    // Four meters in a 2 x 2 grid inside the meters panel:
    //   row 1 - INPUT and OUTPUT level VU meters
    //   row 2 - COMP IN and COMP OUT gain-reduction meters
    // The rows share the available height evenly, so the arrangement (and the
    // input-left / output-right reading order) holds at any panel size.
    auto meterArea = layout.meters.reduced (14, 0);
    meterArea.removeFromTop (44);           // heading + hint labels
    meterArea.removeFromBottom (8);

    const auto columnGap = 8;
    const auto rowGap = 8;
    const auto columnWidth = (meterArea.getWidth() - columnGap) / 2;

    // Note the separate name: `rowHeight` is already taken by the control grid above, so
    // reusing it here would shadow it through the rest of the function and trip the
    // redefinition error rather than silently picking the wrong cell size.
    const auto meterRowHeight = (meterArea.getHeight() - rowGap) / 2;

    const auto leftColumn = meterArea.getX();
    const auto rightColumn = meterArea.getRight() - columnWidth;
    const auto topRow = meterArea.getY();
    const auto bottomRow = meterArea.getBottom() - meterRowHeight;

    inputMeter.setBounds (leftColumn, topRow, columnWidth, meterRowHeight);
    outputMeter.setBounds (rightColumn, topRow, columnWidth, meterRowHeight);
    compressorMeterIn.setBounds (leftColumn, bottomRow, columnWidth, meterRowHeight);
    compressorMeterOut.setBounds (rightColumn, bottomRow, columnWidth, meterRowHeight);
}
