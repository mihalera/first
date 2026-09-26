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
        // Round to one decimal BEFORE formatting, so a value like -0.04 (which is just
        // the meter's jitter floor) prints as "0.0 dB" instead of the crooked-looking
        // "-0.0 dB".
        const auto rounded = std::round (value * 10.0f) * 0.1f + 0.0f;
        return juce::String (rounded, 1) + " dB";
    }

    // Returns the largest font up to `maxHeight` at which `text` still fits inside
    // `availableWidth`. Captions in a switch or a fixed-width label are laid out in
    // code, not by a layout engine, so a caption that grows (or a host that swaps in
    // a wider default font) silently truncates or ellipsises - and a truncated
    // caption is what made the rocker switches look broken. Measuring the string
    // against the space it actually has keeps the text whole instead.
    juce::Font shrinkingFont (const juce::String& text, float maxHeight,
                              int fontStyle, float availableWidth)
    {
        const auto makeFont = [fontStyle] (float height)
        {
            return juce::Font (juce::FontOptions (height, fontStyle));
        };

        if (availableWidth <= 0.0f || text.isEmpty())
            return makeFont (maxHeight);

        auto font = makeFont (maxHeight);

        // A safety margin for side bearing. A flat 1 px was not enough: on the narrow
        // workflow buttons (SAVE / DEL / UNDO / REDO, 38-44 px wide) a caption measured
        // as "just fitting" and then still came out as "SA..." / "D..." / "UN...",
        // because the real rendered string is a little wider than the arrangement
        // reports. The margin is now a share of the available width with a 3 px floor.
        const auto available = juce::jmax (1.0f, availableWidth
                                             - juce::jmax (3.0f, availableWidth * 0.08f));
        const auto measured = juce::GlyphArrangement::getStringWidth (font, text);

        if (measured > available)
        {
            // Scale once by the measured ratio rather than stepping down in fixed
            // decrements: one measurement, and the result is the exact size that
            // just fits instead of the next coarser size down.
            const auto ratio = available / measured;
            const auto scaled = maxHeight * juce::jlimit (0.55f, 1.0f, ratio);
            font = makeFont (scaled);
        }

        return font;
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
    // JUCE's Path::addCentredArc measures clockwise from 12 o'clock and places a
    // point at (sin(angle), -cos(angle)). Convert that same angle to ordinary screen
    // coordinates for the tracer, ticks and pointer; using cos(angle), sin(angle)
    // directly rotates every indicator by 90 degrees and makes the arc look crooked.
    const auto screenAngle = angle - juce::MathConstants<float>::halfPi;

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
    const auto tracer = centre + juce::Point<float> (std::cos (screenAngle) * outerRadius,
                                                     std::sin (screenAngle) * outerRadius);
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
        const auto tickScreenAngle = tickAngle - juce::MathConstants<float>::halfPi;
        const auto major = tick % 3 == 0;
        const auto tremble = std::sin (tickScreenAngle * 3.0f + animationPhase * 1.7f)
                             * driftWobble * 1.3f;
        const auto innerRadius = outerRadius + (major ? 3.0f : 4.0f) + tremble;
        const auto outerTickRadius = innerRadius + (major ? 5.0f : 2.5f);
        const auto inner = centre + juce::Point<float> (std::cos (tickScreenAngle) * innerRadius,
                                                        std::sin (tickScreenAngle) * innerRadius);
        const auto outer = centre + juce::Point<float> (std::cos (tickScreenAngle) * outerTickRadius,
                                                        std::sin (tickScreenAngle) * outerTickRadius);
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
    // The pointer uses the same converted screen angle as the active arc, tracer and
    // ticks, so its tip is exactly radial rather than 90 degrees away from the arc.
    const auto pointerEnd = centre + juce::Point<float> (
        std::cos (screenAngle) * pointerLength,
        std::sin (screenAngle) * pointerLength);
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
//  Toggle switches are drawn as small hardware rockers instead of the default
//  text-button look: a recessed body with engraved OFF / ON captions, a thumb
//  that physically slides between the two positions, and a status LED that
//  lights when the switch is engaged. This is what the panel's BYPASS /
//  POLARITY / AUTO GAIN controls were missing - they used to look like plain
//  buttons that happened to hold state.
//==============================================================================
void J37LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                       bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    const auto& palette = paletteFor (darkTheme);
    const auto bounds = button.getLocalBounds().toFloat().reduced (2.0f, 3.0f);
    const auto isOn = button.getToggleState();

    // Recessed body: darker fill with a vertical sheen, bevel on the inside.
    juce::ColourGradient body (palette.readout.darker (isOn ? 0.25f : 0.05f),
                               bounds.getX(), bounds.getY(),
                               palette.readout.darker (0.45f),
                               bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill (body);
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (palette.border.withAlpha (0.9f));
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
    g.setColour (palette.panel.brighter (0.15f).withAlpha (0.5f));
    g.drawRoundedRectangle (bounds.reduced (2.0f), 3.0f, 0.7f);

    // Two bands sized from the switch itself, so nothing ever fights for width:
    //   top band    - the function name, engraved across the full switch width
    //   bottom band - a short recessed track with a sliding thumb that carries the
    //                 state text; the state is always readable and the label never
    //                 truncates. The band height is a proportion of the control
    //                 (was a hardcoded 11 px, which on a 32 px switch left a crowded
    //                 15 px track carrying THREE overlapping text pieces: OFF, ON and
    //                 the thumb caption).
    const auto labelHeight = juce::jlimit (10.0f, 14.0f, bounds.getHeight() * 0.44f);
    const auto labelBand = bounds.withHeight (labelHeight);
    const auto track = bounds.withTrimmedTop (labelHeight)
                            .withTrimmedLeft (4.0f).withTrimmedRight (4.0f);

    // Status lamp: one explicit circular footprint, concentric at every scale. The
    // previous square slot made the lamp's visual centre depend on the band height,
    // which read as a curved/crooked LED at the smaller toggle sizes. All three layers
    // below use the same centre and only change diameter, so there is no offset bezel,
    // crescent, or elliptical edge to line up.
    const auto ledDiameter = juce::jlimit (6.0f, 8.0f, labelBand.getHeight() * 0.55f);
    const auto ledCentre = juce::Point<float> (
        labelBand.getRight() - 4.0f - ledDiameter * 0.5f,
        labelBand.getCentreY());
    const auto ledBounds = juce::Rectangle<float> (ledCentre.x - ledDiameter * 0.5f,
                                                     ledCentre.y - ledDiameter * 0.5f,
                                                     ledDiameter, ledDiameter);
    g.setColour (palette.readout.darker (0.40f));
    g.fillEllipse (ledBounds.getX(), ledBounds.getY(),
                   ledBounds.getWidth(), ledBounds.getHeight());
    if (isOn)
    {
        g.setColour (palette.status.withAlpha (0.24f));
        const auto glowBounds = ledBounds.expanded (1.5f);
        g.fillEllipse (glowBounds.getX(), glowBounds.getY(),
                       glowBounds.getWidth(), glowBounds.getHeight());
    }
    g.setColour (isOn ? palette.status : palette.knobEdge.withAlpha (0.45f));
    const auto coreBounds = ledBounds.reduced (ledDiameter * 0.28f);
    g.fillEllipse (coreBounds.getX(), coreBounds.getY(),
                   coreBounds.getWidth(), coreBounds.getHeight());

    // The function name is centred in the WHOLE label band, not in a band with the
    // lamp's footprint sliced off one side. Trimming the right edge left the caption
    // sitting about 6 px left of the switch's true centre, which is what made the label
    // and the lamp look crookedly placed. At the three real captions (BYPASS, POLARITY,
    // AUTO GAIN) the centred text clears the lamp by at least 15 px, so centring the
    // caption cannot make them collide.
    const auto captionBand = labelBand;
    g.setColour (palette.text.withAlpha (0.92f));
    g.setFont (shrinkingFont (button.getButtonText().toUpperCase(), 8.0f,
                               juce::Font::bold, captionBand.getWidth()));
    g.drawText (button.getButtonText().toUpperCase(), captionBand,
                juce::Justification::centred, true);

    // Track bed: a recessed groove with the thumb sliding between its two ends. The
    // OFF/ON stop captions were removed - the thumb's own caption is the single
    // authoritative state readout, so there is only ever one state word on screen.
    g.setColour (palette.readout.darker (0.55f));
    g.fillRoundedRectangle (track, 3.0f);

    // Thumb sized to fully cover its half of the track (was a fixed 23 px on a track
    // whose half-width changed with the control, so it could overhang the right edge).
    const auto thumbWidth = juce::jmax (12.0f, (track.getWidth() - 2.0f) * 0.5f);
    const auto thumbHeight = juce::jmax (8.0f, track.getHeight() - 2.0f);
    const auto thumbX = isOn ? track.getRight() - thumbWidth - 1.0f : track.getX() + 1.0f;
    const auto thumbY = track.getY() + 1.0f + (shouldDrawButtonAsDown ? 1.0f : 0.0f);
    const auto thumb = juce::Rectangle<float> (thumbX, thumbY, thumbWidth, thumbHeight);
    juce::ColourGradient thumbFill (palette.knobHighlight, thumb.getX(), thumb.getY(),
                                    palette.knobFace, thumb.getX(), thumb.getBottom(), false);
    g.setGradientFill (thumbFill);
    g.fillRoundedRectangle (thumb, 3.0f);
    g.setColour (palette.knobEdge.withAlpha (0.85f));
    g.drawRoundedRectangle (thumb, 3.0f, 1.0f);

    // State text on the thumb, fitted to the thumb. The thumb is the authoritative
    // state readout, so its text must never be the thing that gets clipped.
    g.setColour (isOn ? palette.gaugeInk.withAlpha (0.95f) : palette.secondary);
    g.setFont (shrinkingFont (isOn ? "ON" : "OFF", 7.5f, juce::Font::bold, thumb.getWidth()));
    g.drawText (isOn ? "ON" : "OFF", thumb, juce::Justification::centred, true);

    // Hover ring for mouse/keyboard focus feedback.
    if (shouldDrawButtonAsHighlighted || button.hasKeyboardFocus (false))
    {
        g.setColour (palette.accent.withAlpha (0.5f));
        g.drawRoundedRectangle (bounds.expanded (1.5f), 5.0f, 1.0f);
    }
}

//==============================================================================
//  Text-button captions are measured against each button's own width rather than
//  drawn at whatever size the default Look and Feel happens to choose. The
//  preset / A/B row is width-constrained at the minimum panel size, so a caption
//  that grows ("A (LIVE) *", "A/B: B") used to be ellipsised; scaling the font to
//  the button keeps the whole caption readable at any editor size.
//==============================================================================
void J37LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                     bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    const auto& palette = paletteFor (darkTheme);

    auto colour = button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId
                                                             : juce::TextButton::textColourOffId);
    if (! button.isEnabled())
        colour = colour.withMultipliedAlpha (0.45f);
    if (shouldDrawButtonAsHighlighted)
        colour = colour.brighter (0.12f);
    if (shouldDrawButtonAsDown)
        colour = colour.darker (0.3f);

    // Side bearing so the fitted caption is never clipped by the rounded corners. 3 px
    // rather than 5: the workflow buttons are only 38-44 px wide, so every pixel here is
    // the difference between a full "SAVE" and an ellipsised "SA...".
    const auto bounds = button.getLocalBounds().toFloat().reduced (3.0f, 1.0f);
    const auto text = button.getButtonText();

    g.setColour (colour);
    g.setFont (shrinkingFont (text, 12.0f, juce::Font::bold, bounds.getWidth()));
    g.drawText (text, bounds, juce::Justification::centred, true);

    if (button.hasKeyboardFocus (false))
    {
        g.setColour (palette.accent.withAlpha (0.7f));
        g.drawRoundedRectangle (button.getLocalBounds().toFloat().reduced (1.0f), 4.0f, 1.0f);
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
    //
    // Re-tuned after the "meters look crooked" report: the raw DSP values arrive at
    // 30 Hz with block-to-block jitter of several dB, which made the numbers twitch in
    // a lopsided way. Each rise/fall pair is now an order of magnitude apart (a clean
    // exponential envelope shape) and the RMS/LUFS rows share the same rise so the
    // two "honest average" rows move as one steady pair.
    const auto smoothTowards = [] (float current, float target, float rise, float fall)
    {
        const auto coefficient = target > current ? rise : fall;
        return current + (target - current) * coefficient;
    };

    displayedRms = smoothTowards (displayedRms, rmsDb, 0.50f, 0.05f);
    displayedLufs = smoothTowards (displayedLufs, lufs, 0.50f, 0.04f);
    displayedVu = smoothTowards (displayedVu, vuDb, 0.15f, 0.08f);
    displayedCombined = smoothTowards (displayedCombined, combinedDb, 0.35f, 0.07f);

    // Peak hold, so a fast transient stays readable instead of flashing past. The
    // release is a fixed dB-per-second slope rather than an exponential decay, so the
    // hold marker falls in a straight, predictable line.
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
        peakHoldDb = juce::jmax (peakDb, peakHoldDb - 12.0f * frameSeconds);
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

    // The title and subtitle get two NON-OVERLAPPING bands carved out of one
    // proportional top block. The old code removed 20*scale for the title, then
    // removed 32*scale and trimmed 18*scale off that for the subtitle, so the
    // subtitle actually started at 14*scale - six pixels (scaled) BEFORE the title
    // ended and the two lines printed on top of each other.
    const auto topBlock = juce::roundToInt (32.0f * textScale);
    const auto titleHeight = juce::roundToInt (18.0f * textScale);
    auto localBounds = getLocalBounds();
    const auto titleBand = localBounds.removeFromTop (titleHeight);
    const auto subtitleBand = localBounds.removeFromTop (topBlock - titleHeight);

    g.setColour (palette.text);
    g.setFont (juce::Font (juce::FontOptions (10.0f * textScale, juce::Font::bold)));
    g.drawText (title, titleBand, juce::Justification::centred, false);

    g.setColour (palette.secondary);
    g.setFont (juce::Font (juce::FontOptions (8.0f * textScale)));
    g.drawText (subtitle, subtitleBand, juce::Justification::centred, false);

    // Everything below is derived from this component's own bounds rather than from
    // fixed pixel offsets, so the dial stays centred and correctly scaled whether the
    // meter is a tall single-column VU or one cell of the 2 x 2 grid, and at any
    // display scale factor. `bounds` is the same rectangle the frame above was drawn
    // from, so it is reused rather than recomputed.
    //
    // The vertical budget is now PROPORTIONAL rather than a stack of measured fixed
    // slices: the dial face gets a fixed share of what is left after the title block,
    // and the five readout rows share the remainder EVENLY. A previous scheme reserved
    // per-row heights that scaled with textScale, so on short, narrow cells the rows
    // bunched against the bottom edge while the dial sat high - the lopsided look that
    // made the input/output meters read as crooked next to the compressor bars.
    constexpr int readoutRowCount = 5;

    // The dial takes a fixed share of the body, and the readout rows then take
    // whatever is genuinely LEFT below it. The old code computed the row block from a
    // fixed share of the body and only afterwards clamped it upwards to fit, which had
    // two consequences: the clamp pushed the first row back up against the dial, and
    // the last row still ended up ONE pixel from the rounded bottom border at every
    // editor size - the loudness numbers were literally touching the panel edge.
    // Taking the row height from the space that actually remains below the dial keeps
    // the dial, the rows and the border from fighting over the same pixels.
    const auto bodyHeight = juce::jmax (72, getHeight() - topBlock);
    const auto faceHeight = juce::roundToInt (static_cast<float> (bodyHeight) * 0.48f);
    const auto face = juce::Rectangle<float> (bounds.getX(),
                                              bounds.getY() + static_cast<float> (topBlock),
                                              bounds.getWidth(), static_cast<float> (faceHeight));

    constexpr float rowGapAbove = 4.0f;
    constexpr float rowBottomMargin = 3.0f;
    const auto rowsTop = face.getBottom() + rowGapAbove;
    const auto availableForRows = juce::jmax (static_cast<float> (readoutRowCount * 10),
                                              bounds.getBottom() - rowsTop - rowBottomMargin);
    // floor, not round: rounding UP would make the block taller than the space that
    // was actually measured, and the clamp below would then drag the whole block back
    // up on top of the dial. Flooring guarantees the rows start below the dial and
    // still leave the bottom margin.
    const auto readoutRowHeight = juce::jmax (10, static_cast<int> (availableForRows
                                                                    / readoutRowCount));
    const auto usedReadout = readoutRowHeight * readoutRowCount;
    const auto readoutTop = juce::jmin (rowsTop,
                                        bounds.getBottom() - static_cast<float> (usedReadout)
                                          - rowBottomMargin);
    const auto rowHeight = readoutRowHeight;

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

    // The whole dial is clipped to the face it belongs to. This is the guarantee that
    // nothing drawn here can ever continue past the dial box and into the readout rows
    // below: whatever the gauge geometry does, the PEAK/RMS/LUFS/VU/AVG rows are simply
    // unreachable. The box itself ends exactly on the face's bottom edge, so the clip
    // never cuts the visible instrument. (JUCE 9 removed GraphicsStateSaver; saveState /
    // restoreState are the pair it wrapped, and the restore is called explicitly below.)
    g.saveState();
    // 1 px of slack so rounding the float face to an integer clip cannot shave the
    // bottom border off the dial's own rounded box. The readout rows start 4 px below
    // the face, so this still leaves 3 px of guaranteed clearance.
    g.reduceClipRegion (face.toNearestInt().reduced (1, 1));

    g.setColour (palette.knobEdge.withAlpha (0.55f));
    g.fillRoundedRectangle (dial, 4.0f);
    g.setColour (palette.gaugeFace);
    g.fillRoundedRectangle (dial.reduced (3.0f), 3.0f);
    g.setColour (palette.border);
    g.drawRoundedRectangle (dial.reduced (3.0f), 3.0f, 0.9f);

    // The tick marks and their labels only make sense once the dial is big enough to
    // separate them, so they are drawn from an adaptive count rather than always ten.
    const auto tickCount = radius > 34.0f ? 10 : 6;

    // The gauge arc, and the reason its angles are NOT the startAngle/endAngle the tick
    // loop below uses. JUCE's arc convention is 0 radians = 12 o'clock with the angle
    // increasing clockwise - Point::getPointOnCircumference is (x + r*sin a, y - r*cos a)
    // - so the top half, left over the top to the right, is 3/2*pi to 5/2*pi. Handing
    // addCentredArc the tick loop's pi..2*pi put the sweep on the WRONG half: it began
    // at the BOTTOM of the dial, bulged out to the left and ended at the top, which is
    // the black half-circle that hung below the meter and ran through the readout rows.
    // 3/2*pi -> 5/2*pi traces the same left -> top -> right path that the ticks and the
    // needle describe with (cos a, sin a) over pi..2*pi, so all three agree exactly.
    juce::Path scaleArc;
    scaleArc.addCentredArc (centreX, centreY, radius, radius, 0.0f,
                            juce::MathConstants<float>::pi * 1.5f,
                            juce::MathConstants<float>::pi * 2.5f,
                            true);
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

    // (The old standalone "VU" caption was removed: it sat at centreY + 13, exactly
    // where the readout rows begin, and printed over the PEAK and RMS rows. The scale
    // is already named by the meter subtitle and by the VU row itself.)
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
    g.restoreState();   // end of the dial clip - the readout rows below draw unclipped

    // -------------------------------------------------------------------
    //  Four-way readout.
    //
    //  Each scale gets its own row because they answer different questions and their
    //  spread is meaningful: PEAK against LUFS shows how dynamic the material is, and
    //  the gap between VU and RMS shows how much transient content is present.
    //  The combined row is the equal-weighted average of the four, which is what the
    //  needle and the dial are driven from.
    // -------------------------------------------------------------------
    // The readout rows are inset from the meter's rounded border by a share of the
    // cell's own width instead of a hardcoded 2 px. At the minimum panel size the
    // left-aligned label started and the right-aligned value ended two pixels from the
    // border, which is what made the numbers look pasted onto the panel edge and the
    // column read as crooked.
    const auto rowInset = juce::jmax (5, juce::roundToInt (getWidth() * 0.055f));
    const auto rowWidth = juce::jmax (1, getWidth() - 2 * rowInset);

    const auto drawReadoutRow = [&] (const juce::String& label, float value,
                                     juce::Colour valueColour, int row)
    {
        const auto rowTop = juce::roundToInt (readoutTop) + row * rowHeight;
        const auto rowArea = juce::Rectangle<int> (rowInset, rowTop, rowWidth, rowHeight);
        g.setColour (palette.secondary);
        g.setFont (juce::Font (juce::FontOptions (8.0f * textScale)));
        g.drawText (label, rowArea, juce::Justification::centredLeft, false);

        // The value column uses a monospaced face so digits are all the same width:
        // with a proportional font the right-aligned numbers kept shifting sideways
        // frame to frame ("1" is narrower than "8"), which read as the meter being
        // crooked. With fixed-width digits the column has a clean, stable right edge.
        g.setColour (valueColour);
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                                  8.5f * textScale, juce::Font::bold)));
        g.drawText (formatDb (value), rowArea, juce::Justification::centredRight, false);
    };

    // The clipping lamp: only the peak view can clip, so it is flagged next to that row.
    const auto peakColour = clipping ? juce::Colour::fromRGB (208, 82, 58) : palette.text;
    drawReadoutRow ("PEAK dB", peakHoldDb, peakColour, 0);
    drawReadoutRow ("RMS", displayedRms, palette.text, 1);
    drawReadoutRow ("LUFS", displayedLufs, palette.accent, 2);
    drawReadoutRow ("VU", displayedVu, palette.text, 3);

    // Hairline on the exact boundary between the four measurement rows and the
    // summary row (neither row's centred text reaches its own edge), so the average
    // reads as a summary.
    g.setColour (palette.border.withAlpha (0.45f));
    g.drawHorizontalLine (juce::roundToInt (readoutTop) + 4 * rowHeight,
                          static_cast<float> (rowInset),
                          static_cast<float> (getWidth() - rowInset));

    // The summary row drives the needle: it is the equal-weighted average of the four
    // views above (the old "MIX 25%" caption wrongly suggested it had something to do
    // with the MIX control).
    drawReadoutRow ("AVG", displayedCombined, palette.accent, 4);
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
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                              10.0f * textScale, juce::Font::bold)));
    // Same rounding-then-printing rule as the level meters' formatDb: near-zero
    // reductions must read "0.0 dB", never "-0.0 dB".
    g.drawText (formatDb (displayedDb),
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
    //
    // Re-tuned again after a second "interface is still too large" report. Two things
    // shrank the real footprint: the 1024 x 640 opening size itself, and the outer
    // furniture - getEditorLayout() previously kept 18 px on every edge of the window
    // and a 78 px header, which alone cost more than a hundred rows of dead panel
    // before any control appeared. The opening size is now 960 x 600 - the same
    // proportion, one notch smaller - and the minimum is 780 x 540 so small hosts
    // keep the knob grid and the 2 x 2 meters usable. The maximum is pulled in to
    // 1400 x 900: beyond that the analogue panel stops gaining legibility and only
    // looks sparse.
    setResizeLimits (780, 640, 1400, 960);
    setSize (980, 690);

    // Tooltips appear after a third of a second of hover: quick enough to be
    // discoverable, slow enough not to flash while the user sweeps the panel.
    tooltipWindow->setMillisecondsBeforeTipAppears (350);
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
                paletteFor (false).status, true, juce::Justification::centred);
    styleLabel (deckHeadingLabel, "TAPE DECK", 10.0f, paletteFor (false).accent,
                true, juce::Justification::left);

    // Build identity, printed on the deck's heading strip. CMake resolves
    // J37_BUILD_COMMIT from `git rev-parse --short=8 HEAD` at configure time and marks
    // a dirty tree with a trailing "+"; when the sources are built outside a git
    // checkout the definition still exists and reads "unknown", so this line can never
    // be empty and the two cases are always distinguishable at a glance.
#ifdef J37_BUILD_COMMIT
    const juce::String buildId (J37_BUILD_COMMIT);
#else
    const juce::String buildId ("unknown");
#endif
    styleLabel (buildLabel, "BUILD " + buildId, 8.0f, paletteFor (false).secondary,
                true, juce::Justification::centredRight);
    styleLabel (tapeTypeLabel, "MODEL", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    styleLabel (speedLabel, "SPEED", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    styleLabel (deckHintLabel, "Tape formula and speed.", 9.0f,
                paletteFor (false).secondary, false, juce::Justification::centredLeft);
    styleLabel (controlsHeadingLabel, "TAPE CHARACTER", 10.0f, paletteFor (false).accent,
                true, juce::Justification::left);
    styleLabel (controlsHintLabel, "Shift = fine tune", 9.0f,
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
    addAndMakeVisible (buildLabel);
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
                                         "tone", "character", "wow",
                                         "flutter", "mix", "output",
                                         "stereo_width", "subfund" };
    const juce::StringArray controlNames { "INPUT", "DRIVE", "BIAS",
                                           "BRIGHT", "TONE", "WOW",
                                           "FLUTTER", "MIX", "OUTPUT",
                                           "WIDTH", "SUBFUND" };
    const std::array<double, controlCount> defaultValues { 0.0, 0.42, 0.36,
                                                           0.58, 0.5, 0.14,
                                                           0.18, 0.5, 0.0,
                                                           0.5, 0.0 };

    for (std::size_t i = 0; i < controlCount; ++i)
    {
        auto& slider = controls[i];
        slider.setName (controlNames[static_cast<int> (i)]);
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 0.75f,
                                    juce::MathConstants<float>::pi * 2.25f, true);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 18);
        slider.setTextBoxIsEditable (true);
        // Drag response tuned by ear: velocity-based mode throttled the first pixels
        // of every drag behind an acceleration ramp, which read as "slow knobs".
        // Direct movement plus a high sensitivity gives a full 0..1 sweep in about a
        // third of the panel width; Shift still engages JUCE's low-velocity mode for
        // fine control.
        slider.setMouseDragSensitivity (1400);
        slider.setVelocityBasedMode (true);
        slider.setVelocityModeParameters (1.0, 2, 0.06, true, juce::ModifierKeys::shiftModifier);
        slider.setPopupDisplayEnabled (true, true, this);
        slider.setScrollWheelEnabled (true);
        slider.setDoubleClickReturnValue (true, defaultValues[i]);
        slider.setLookAndFeel (&customLookAndFeel);
        slider.setColour (juce::Slider::textBoxOutlineColourId, paletteFor (false).border);

        // A tooltip for EVERY parameter - this is what shows when the user hovers.
        // The per-name text explains what the control does and its default, and the
        // generic interaction hints ride along on every knob.
        const auto parameterTooltip = [&] (const juce::String& id) -> juce::String
        {
            // juce::String, not a char pointer: the + below must concatenate strings,
            // not pointers (which does not compile).
            const juce::String hints = " Hold Shift for fine control, mouse wheel for small "
                                       "steps, double-click to reset.";
            if (id == "input")
                return juce::String ("INPUT - output-stages the signal into the machine before the "
                       "tape. Positive pushes the tape harder for more saturation, "
                       "negative cleans up. Range -32 to +32 dB, default 0 dB.") + hints;
            if (id == "drive")
                return juce::String ("DRIVE - the amount of magnetic saturation. At 0 percent the "
                       "machine is clean; higher settings bend the signal like tape "
                       "and add harmonics. Default 42 percent.") + hints;
            if (id == "bias")
                return juce::String ("BIAS - the record head's ultra-sonic offset. It shapes the "
                       "even harmonics: low bias is edgy and thin, higher bias is "
                       "warmer and fuller. Default 36 percent.") + hints;
            if (id == "tone")
                return juce::String ("BRIGHTNESS - the record top-end and the playback "
                       "high-shelf above 8 kHz. Low is warm and rounded, high is open "
                       "and airy. Default 58 percent.") + hints;
            if (id == "character")
                return juce::String ("TONE - the machine-state macro. It crossfades the whole deck "
                       "between the classic slow machine (soft head gap, relaxed "
                       "flutter) and the fast hot machine (open top end, tight "
                       "flutter). Default 50 percent.") + hints;
            if (id == "wow")
                return juce::String ("WOW - slow pitch wander of the transport, like a slightly "
                       "loose capstan. 0 percent is a perfectly steady machine. "
                       "Default 14 percent.") + hints;
            if (id == "flutter")
                return juce::String ("FLUTTER - fast shimmer of the transport, like the tape "
                       "brushing the heads. 0 percent is perfectly steady. "
                       "Default 18 percent.") + hints;
            if (id == "mix")
                return juce::String ("MIX - dry/wet crossfade. 0 percent is the untouched signal, "
                       "100 percent is fully through the tape. Default 50 percent.") + hints;
            if (id == "output")
                return juce::String ("OUTPUT - calibrated output trim after the whole chain. "
                       "Range -32 to +32 dB, default 0 dB.") + hints;
            if (id == "stereo_width")
                return juce::String ("WIDTH - stereo image after the tape. 0 percent is mono, "
                       "50 percent is the natural stereo width, 100 percent is extra "
                       "wide. Default 50 percent.") + hints;
            if (id == "subfund")
                return juce::String ("SUBFUND - subharmonics. "
                       "Generates up to 8 undertones (1/2 through 1/9) "
                       "below the note when signal frequency permits, adding deep multi-layered "
                       "weight and warmth that regular saturation cannot reach. They fall away in a "
                       "staircase as they divide further, so the octave below is the strongest and "
                       "1/9 is 42 dB under it. Stages falling "
                       "below audible sub-bass (< 14-22 Hz) are smoothly attenuated to prevent DC "
                       "rumble, and the whole series stops with the note rather than continuing to "
                       "sound under it. They are added to the signal after the tape's own "
                       "saturation, so they stay clean partials instead of feeding it and coming "
                       "back out as a harmonic series. Default 0 percent - "
                       "it is a colour, not a correction.") + hints;
            return hints;
        };
        slider.setTooltip (parameterTooltip (controlIds[static_cast<int> (i)]));

        // Index 8 is OUTPUT, not 7: the control order is INPUT, DRIVE, BIAS, BRIGHT,
        // TONE, WOW, FLUTTER, MIX, OUTPUT, WIDTH. The old check (i == 7) handed MIX
        // the -32..+32 dB range and suffix - which is why the Mix knob displayed dB -
        // and left OUTPUT stuck in the 0..1 percentage branch, where its slider range
        // clipped the real +/-32 dB parameter down to 0..1.
        if (i == 0 || i == 8)
        {
            // Input and Output are both calibrated decibel trims over the same range.
            slider.setRange (minStageDb, maxStageDb, 0.1);
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
        controlLabels[i].setJustificationType (juce::Justification::centred);
        controlLabels[i].setInterceptsMouseClicks (false, false);

        addAndMakeVisible (controlLabels[i]);
        addAndMakeVisible (slider);
        controlAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
            (audioProcessor.parameters, controlIds[static_cast<int> (i)], slider);
    }

    tapeTypeBox.addItemList (juce::StringArray { "J37", "Ampex 456", "Studer A800", "Chrome",
                                                 "Type 111", "GP9", "Quantegy 499", "RTM SM911" }, 1);
    speedBox.addItemList (juce::StringArray { "7.5 ips", "15 ips", "30 ips" }, 1);
    tapeTypeBox.setTextWhenNothingSelected ("Select tape");
    speedBox.setTextWhenNothingSelected ("Select speed");
    tapeTypeBox.setTooltip ("Tape formula. Each stock bends the sound differently: "
                            "J37 is soft and classic, Ampex and Studer are hotter, "
                            "Chrome and Type 111 are cleaner, GP9 and 499 are dense "
                            "modern formulas, SM911 is the broadcast reference. The "
                            "formula also shapes the glue compressors' timing.");
    speedBox.setTooltip ("Transport speed. 7.5 ips is dark and loose, 15 ips is the "
                         "classic studio speed, 30 ips keeps the most top end and "
                         "the tightest glue. Speed also shapes the glue timing.");
    tapeTypeBox.setLookAndFeel (&customLookAndFeel);
    speedBox.setLookAndFeel (&customLookAndFeel);

    bypassButton.setClickingTogglesState (true);
    bypassButton.setButtonText ("BYPASS");
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

    // ---------------------------------------------------------------
    //  Output-stage switches: polarity invert and auto gain.
    // ---------------------------------------------------------------
    bypassButton.setTooltip ("Hard bypass: the tape engine and both glue compressors are "
                             "switched out. The switch is ramped, so toggling it never clicks.");
    polarityButton.setClickingTogglesState (true);
    polarityButton.setTooltip ("Inverts the output polarity (180-degree phase flip). "
                               "Use it to correct an inverted source or to align two "
                               "machines feeding the same bus.");
    polarityButton.setLookAndFeel (&customLookAndFeel);
    polarityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (audioProcessor.parameters, "polarity", polarityButton);
    addAndMakeVisible (polarityButton);

    autoGainButton.setClickingTogglesState (true);
    autoGainButton.setTooltip ("Auto gain lets the slow programme compensator restore "
                               "the level the INPUT trim dialled in. Switch it off to "
                               "keep the output exactly at the level the chain produced.");
    autoGainButton.setLookAndFeel (&customLookAndFeel);
    autoGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (audioProcessor.parameters, "auto_gain", autoGainButton);
    addAndMakeVisible (autoGainButton);

    // ---------------------------------------------------------------
    //  Oversampling switch on the panel. The parameter has always been
    //  host-visible; without a control it could only be reached from the
    //  DAW's own parameter list, which is not how a premium plugin works.
    // ---------------------------------------------------------------
    styleLabel (oversamplingLabel, "OVER", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    addAndMakeVisible (oversamplingLabel);
    oversamplingBox.addItemList (juce::StringArray { "Off", "2x", "4x", "8x" }, 1);
    oversamplingBox.setTooltip ("Internal rate of the tape engine. 2x and 4x reduce the "
                                "aliasing of the magnetic shaper; the added filter delay "
                                "is reported to the host, so DAW PDC compensates.");
    oversamplingBox.setLookAndFeel (&customLookAndFeel);
    oversamplingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "oversampling", oversamplingBox);
    addAndMakeVisible (oversamplingBox);

    // ---------------------------------------------------------------
    //  Instrument voicing selector, on the deck's second row.
    // ---------------------------------------------------------------
    styleLabel (instrumentLabel, "INSTRUMENT", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::left);
    addAndMakeVisible (instrumentLabel);
    instrumentBox.addItemList (juce::StringArray { "Master Bus", "Vocal", "Bass",
                                                   "Guitar", "Piano" }, 1);
    instrumentBox.setTooltip ("Re-voices the machine for what is being recorded: how hard "
                              "the tape bends, how much top end survives, how loud the "
                              "floor sits and how steady the transport runs. Master Bus "
                              "is the neutral calibration.");
    instrumentBox.setLookAndFeel (&customLookAndFeel);
    instrumentAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        (audioProcessor.parameters, "instrument", instrumentBox);
    addAndMakeVisible (instrumentBox);

    // ---------------------------------------------------------------
    //  Premium workflow bar.
    // ---------------------------------------------------------------
    styleLabel (presetHeadingLabel, "PRESET", 9.0f, paletteFor (false).secondary,
                true, juce::Justification::centredLeft);
    addAndMakeVisible (presetHeadingLabel);

    refreshPresetList();
    presetBox.setTooltip ("Factory presets. Loading one replaces the whole machine state "
                          "in a single undoable step.");
    presetBox.setLookAndFeel (&customLookAndFeel);
    presetBox.onChange = [this]
    {
        const auto selectedId = presetBox.getSelectedId();
        if (selectedId <= 0)
            return;
        const auto presetIndex = selectedId - 1;
        if (presetIndex == audioProcessor.getLastPresetIndex())
            return; // re-selecting the displayed entry is not a state change
        audioProcessor.applyFactoryPreset (presetIndex);
        lastShownPreset = presetIndex;
    };
    addAndMakeVisible (presetBox);

    const auto workflowButtonSetup = [&] (juce::TextButton& button, const juce::String& tip)
    {
        button.setTooltip (tip);
        button.setLookAndFeel (&customLookAndFeel);
        addAndMakeVisible (button);
    };

    // ---------------------------------------------------------------
    //  User presets: the combo lists what is on disk, SAVE opens a name
    //  dialog and DEL removes the selected file. Every action refreshes
    //  the list so two instances of the plugin stay honest with each
    //  other about what exists.
    // ---------------------------------------------------------------
    refreshUserPresetList();
    userPresetBox.setTooltip ("User presets - your own saved machine states. "
                              "Selecting one recalls it as a single undoable step.");
    userPresetBox.setLookAndFeel (&customLookAndFeel);
    userPresetBox.setTextWhenNothingSelected ("USER...");
    userPresetBox.onChange = [this]
    {
        const auto selectedId = userPresetBox.getSelectedId();
        if (selectedId <= 0)
            return;
        const auto name = userPresetBox.getItemText (selectedId - 1);
        if (name.isEmpty() || name == audioProcessor.getCurrentPresetName())
            return;
        if (audioProcessor.applyUserPreset (name))
        {
            lastShownUserPreset = name;
            lastShownPreset = -1;
            // A user preset clears the factory selection: ID 0 is "no selection".
            presetBox.setSelectedId (audioProcessor.getLastPresetIndex() + 1,
                                     juce::dontSendNotification);
            updateWorkflowButtons();
        }
        else
        {
            refreshUserPresetList(); // the file vanished; rebuild the list
        }
    };
    addAndMakeVisible (userPresetBox);

    workflowButtonSetup (savePresetButton, "Store the whole machine state as a user "
                                           "preset you can recall in any session.");
    workflowButtonSetup (deletePresetButton, "Delete the selected user preset file.");
    savePresetButton.onClick = [this]
    {
        if (savePresetWindow != nullptr)
        {
            savePresetWindow->exitModalState (0);
            savePresetWindow.reset();
        }

        auto* window = new juce::AlertWindow ("Save user preset",
                                              "Name this machine state:",
                                              juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("preset_name", audioProcessor.getCurrentPresetName(), "Name:");
        window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        savePresetWindow.reset (window);

        window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window] (int result)
        {
            // Guard on the pointer: an older dialog's asynchronous callback must
            // never dismiss a newer one.
            if (savePresetWindow.get() != window)
                return;
            const auto chosen = window->getTextEditorContents ("preset_name");
            savePresetWindow.reset();
            if (result == 1 && chosen.trim().isNotEmpty())
            {
                if (audioProcessor.saveUserPreset (chosen))
                    refreshUserPresetList();
            }
        }), false);
    };
    deletePresetButton.onClick = [this]
    {
        const auto selectedId = userPresetBox.getSelectedId();
        if (selectedId <= 0)
            return;
        const auto name = userPresetBox.getItemText (selectedId - 1);
        if (name.isNotEmpty() && audioProcessor.deleteUserPreset (name))
            refreshUserPresetList();
    };

    styleLabel (presetBadgeLabel, "", 8.0f, paletteFor (false).secondary,
                false, juce::Justification::centredLeft);
    addAndMakeVisible (presetBadgeLabel);

    workflowButtonSetup (copyAButton, "Store the current settings in slot A. "
                                      "A is the side the plugin starts on.");
    workflowButtonSetup (copyBButton, "Store the current settings in slot B.");
    workflowButtonSetup (compareButton, "Toggle between slots A and B to compare settings "
                                        "with identical level, bypass state and oversampling.");
    workflowButtonSetup (undoButton, "Undo the last preset or A/B change (Ctrl+Z).");
    workflowButtonSetup (redoButton, "Redo an undone preset or A/B change (Ctrl+Y).");

    copyAButton.onClick = [this]
    {
        audioProcessor.copyToCompareSlot (0);
        updateWorkflowButtons();
    };
    copyBButton.onClick = [this]
    {
        audioProcessor.copyToCompareSlot (1);
        updateWorkflowButtons();
    };
    compareButton.onClick = [this]
    {
        audioProcessor.toggleCompare();
        updateWorkflowButtons();
    };
    undoButton.onClick = [this]
    {
        audioProcessor.getUndoManager().undo();
        updateWorkflowButtons();
    };
    redoButton.onClick = [this]
    {
        audioProcessor.getUndoManager().redo();
        updateWorkflowButtons();
    };

    styleLabel (compareBadgeLabel, "", 8.5f, paletteFor (false).accent,
                true, juce::Justification::centred);
    addAndMakeVisible (compareBadgeLabel);

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
    presetBox.setLookAndFeel (nullptr);
    copyAButton.setLookAndFeel (nullptr);
    copyBButton.setLookAndFeel (nullptr);
    compareButton.setLookAndFeel (nullptr);
    undoButton.setLookAndFeel (nullptr);
    redoButton.setLookAndFeel (nullptr);
    polarityButton.setLookAndFeel (nullptr);
    autoGainButton.setLookAndFeel (nullptr);
    oversamplingBox.setLookAndFeel (nullptr);
    instrumentBox.setLookAndFeel (nullptr);
    savePresetButton.setLookAndFeel (nullptr);
    deletePresetButton.setLookAndFeel (nullptr);

    if (savePresetWindow != nullptr)
        savePresetWindow->exitModalState (0);
    savePresetWindow.reset();
}

void FirstAudioProcessorEditor::refreshUserPresetList()
{
    userPresetBox.clear (juce::dontSendNotification);
    const auto names = audioProcessor.getUserPresetNames();
    for (auto i = 0; i < names.size(); ++i)
        userPresetBox.addItem (names[i], i + 1);

    const auto loaded = audioProcessor.getCurrentPresetName();
    const auto index = names.indexOf (loaded);
    userPresetBox.setSelectedItemIndex (index >= 0 ? index : -1, juce::dontSendNotification);
    lastShownUserPreset = index >= 0 ? loaded : juce::String();
}

void FirstAudioProcessorEditor::refreshPresetList()
{
    presetBox.clear (juce::dontSendNotification);
    const auto names = FirstAudioProcessor::getPresetNames();
    for (auto i = 0; i < names.size(); ++i)
        presetBox.addItem (names[i], i + 1);
    presetBox.setSelectedItemIndex (juce::jmax (0, audioProcessor.getLastPresetIndex()),
                                    juce::dontSendNotification);
    lastShownPreset = audioProcessor.getLastPresetIndex();
}

void FirstAudioProcessorEditor::updateWorkflowButtons()
{
    // The A/B buttons carry the active side on their text, so the panel always says
    // which slot is live without needing a separate readout.
    const auto activeSlot = audioProcessor.getActiveCompareSlot();
    copyAButton.setButtonText (activeSlot == 0 ? "A (LIVE)" : "COPY A");
    copyBButton.setButtonText (activeSlot == 1 ? "B (LIVE)" : "COPY B");
    compareButton.setButtonText (activeSlot == 0 ? "A/B: B" : "A/B: A");

    // An edited dot on the inactive slot's caption: when the two stored slots differ,
    // the side you are NOT hearing has something different on it.
    const auto dirty = audioProcessor.isCompareDirty();
    copyAButton.setButtonText (copyAButton.getButtonText()
                               + (dirty && activeSlot != 0 ? " *" : ""));
    copyBButton.setButtonText (copyBButton.getButtonText()
                               + (dirty && activeSlot != 1 ? " *" : ""));


    auto& undoManager = audioProcessor.getUndoManager();
    undoButton.setEnabled (undoManager.canUndo());
    redoButton.setEnabled (undoManager.canRedo());

    // The toggles draw themselves through J37LookAndFeel::drawToggleButton, which
    // reads the palette live, so theme and state changes recolour on the same frame
    // without any per-colour bookkeeping here.

    // Short captions: the badge sits at the right end of the preset row, where the
    // old "A/B MATCHED" could push into the redo button on a narrow panel.
    compareBadgeLabel.setText (dirty ? "EDITED" : "MATCHED",
                               juce::dontSendNotification);

    const auto presetName = audioProcessor.getCurrentPresetName();
    const auto presetIsDirty = audioProcessor.isPresetDirty();
    const auto presetBadgeText = presetName.isEmpty()
                                ? "FACTORY STATE"
                                : (presetIsDirty ? "PRESET: " + presetName + " (EDITED)"
                                                 : "PRESET: " + presetName);
    presetBadgeLabel.setText (presetBadgeText, juce::dontSendNotification);
    // Re-fit on every caption change, not just on resize: a user can name a preset
    // anything at all, and the badge must shrink to fit rather than clip at the edge.
    presetBadgeLabel.setFont (shrinkingFont (presetBadgeText, 8.0f, juce::Font::plain,
                                             static_cast<float> (presetBadgeLabel.getWidth()) - 4.0f));
    presetBadgeLabel.setColour (juce::Label::textColourId,
                                presetIsDirty ? paletteFor (darkTheme).accent
                                              : paletteFor (darkTheme).secondary);
}

bool FirstAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    // Ctrl/Cmd+Z and Ctrl/Cmd+Y mirror the DAW convention inside the plugin's own
    // preset/A/B history. The Shift+Z (redo) test comes FIRST: a bare Ctrl+Z also
    // matches the Shift variant, so testing undo first would swallow the redo chord.
    const auto commandDown = key.getModifiers().isCommandDown();
    if (commandDown && key.isKeyCode ('Y'))
    {
        audioProcessor.getUndoManager().redo();
        updateWorkflowButtons();
        return true;
    }
    if (commandDown && key.isKeyCode ('Z'))
    {
        if (key.getModifiers().isShiftDown())
            audioProcessor.getUndoManager().redo();
        else
            audioProcessor.getUndoManager().undo();
        updateWorkflowButtons();
        return true;
    }
    return Component::keyPressed (key);
}

FirstAudioProcessorEditor::EditorLayout FirstAudioProcessorEditor::getEditorLayout() const
{
    // Outer padding of 14 px instead of 18: the screws and the panel border only need
    // that much clearance, and every pixel saved here goes to the working areas. The
    // header is 70 px and the deck 80 - both are still comfortably above their fixed
    // furniture (30 px badge, 35 px combo boxes) plus the deck's second workflow line,
    // but no longer bankroll dead space.
    auto remaining = getLocalBounds().reduced (14);
    EditorLayout layout;

    // Header and deck are fixed-height, so they keep their proportions at small panel
    // sizes and on high-DPI displays. The deck is three control lines tall - transport,
    // then switches / oversampling, then presets and the A/B cluster - plus a badge
    // band at the bottom, so no two groups ever share a row and nothing can overlap
    // even at the minimum panel size (752 px of deck width fits every line exactly).
    layout.header = remaining.removeFromTop (70);
    remaining.removeFromTop (8);
    layout.deck = remaining.removeFromTop (164);
    remaining.removeFromTop (8);

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
    buildLabel.setColour (juce::Label::textColourId, palette.secondary);
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
    styleCombo (presetBox);

    const auto styleWorkflowButton = [&palette] (juce::TextButton& button, bool emphasised)
    {
        button.setColour (juce::TextButton::buttonColourId, palette.raised);
        button.setColour (juce::TextButton::buttonOnColourId, palette.accent);
        button.setColour (juce::TextButton::textColourOffId, emphasised ? palette.text : palette.secondary);
        button.setColour (juce::TextButton::textColourOnId, palette.readout);
    };

    // bypassButton is a ToggleButton drawn by J37LookAndFeel::drawToggleButton.
    themeButton.setColour (juce::TextButton::buttonColourId, palette.raised);
    themeButton.setColour (juce::TextButton::textColourOffId, palette.text);

    styleCombo (oversamplingBox);
    styleCombo (instrumentBox);
    styleWorkflowButton (copyAButton, audioProcessor.getActiveCompareSlot() == 0);
    styleWorkflowButton (copyBButton, audioProcessor.getActiveCompareSlot() == 1);
    styleWorkflowButton (compareButton, false);
    styleWorkflowButton (undoButton, false);
    styleWorkflowButton (redoButton, false);
    styleWorkflowButton (savePresetButton, false);
    styleWorkflowButton (deletePresetButton, false);
    // polarityButton and autoGainButton are drawn by J37LookAndFeel::drawToggleButton
    // and need no per-theme colour calls here.
    presetHeadingLabel.setColour (juce::Label::textColourId, palette.secondary);
    oversamplingLabel.setColour (juce::Label::textColourId, palette.secondary);
    instrumentLabel.setColour (juce::Label::textColourId, palette.secondary);
    compareBadgeLabel.setColour (juce::Label::textColourId,
                                 audioProcessor.isCompareDirty() ? palette.accent : palette.secondary);

    compressorLabel.setColour (juce::Label::textColourId, palette.accent);
    compressorReadout.setColour (juce::Label::textColourId, palette.secondary);
    harmonicsLabel.setColour (juce::Label::textColourId, palette.accent);
    harmonicsReadout.setColour (juce::Label::textColourId, palette.secondary);
    compressorMeterIn.setDarkTheme (darkTheme);
    compressorMeterOut.setDarkTheme (darkTheme);

    themeButton.setButtonText (darkTheme ? "LIGHT THEME" : "DARK THEME");

    updateWorkflowButtons();
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
    g.fillRect (layout.header.getX() + 17, layout.header.getY() + 14, 3,
                layout.header.getHeight() - 28);

    g.setColour (palette.border.withAlpha (0.75f));
    g.drawHorizontalLine (layout.controls.getY() + 42,
                          static_cast<float> (layout.controls.getX() + 18),
                          static_cast<float> (layout.controls.getRight() - 18));
    g.drawHorizontalLine (layout.meters.getY() + 48,
                          static_cast<float> (layout.meters.getX() + 18),
                          static_cast<float> (layout.meters.getRight() - 18));
    // The deck divider sits one pixel above the panel's bottom edge, BELOW the preset
    // badge band. It used to be eight pixels above the bottom, which put it straight
    // through the middle of the badge text at the minimum editor size.
    g.drawHorizontalLine (layout.deck.getBottom() - 1,
                          static_cast<float> (layout.deck.getX() + 16),
                          static_cast<float> (layout.deck.getRight() - 16));

    const auto statusBadge = juce::Rectangle<float> (
        static_cast<float> (layout.header.getRight() - 205),
        static_cast<float> (layout.header.getY() + 20), 184.0f, 30.0f);
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

    // getX()/getRight() return int, so the 9.0f insets are added *after* the cast.
    // `getX() + 9.0f` promotes the int to float implicitly, which is exactly what
    // the int-to-float conversion warnings on macOS flag. Reading the edges into
    // float locals once also keeps the four screw positions consistent with each
    // other instead of each one repeating its own mixed int/float arithmetic.
    const auto headerX = static_cast<float> (layout.header.getX());
    const auto headerRight = static_cast<float> (layout.header.getRight());
    const auto headerY = static_cast<float> (layout.header.getY());
    const auto headerBottom = static_cast<float> (layout.header.getBottom());

    for (const auto point : { juce::Point<float> (headerX + 9.0f, headerY + 9.0f),
                              juce::Point<float> (headerRight - 9.0f, headerY + 9.0f),
                              juce::Point<float> (headerX + 9.0f, headerBottom - 9.0f),
                              juce::Point<float> (headerRight - 9.0f, headerBottom - 9.0f) })
        drawScrew (g, point.x, point.y, palette);

    // The decorative reel lives in the deck's TOP-RIGHT corner, in the heading band
    // (y + 6..22) where no control sits, and clear of the harmonics readout, which is
    // right-aligned below it on the switches line. It used to sit ON the readout text.
    const auto reelCentre = juce::Point<float> (static_cast<float> (layout.deck.getRight() - 42),
                                                static_cast<float> (layout.deck.getY() + 14));
    g.setColour (palette.accent.withAlpha (0.22f));
    for (const auto radius : { 11.0f, 7.0f, 2.0f })
        g.drawEllipse (reelCentre.x - radius, reelCentre.y - radius,
                       radius * 2.0f, radius * 2.0f, 1.0f);
    g.drawLine (reelCentre.x - 14.0f, reelCentre.y, reelCentre.x + 14.0f, reelCentre.y, 0.8f);
    g.drawLine (reelCentre.x, reelCentre.y - 14.0f, reelCentre.x, reelCentre.y + 14.0f, 0.8f);

    // Spokes: density tracks the selected tape speed, drift tracks the modulation.
    const auto spokeAlpha = 0.20f + 0.45f * glowAmount;
    for (int spoke = 0; spoke < 6; ++spoke)
    {
        const auto spokeAngle = reelAngle + static_cast<float> (spoke)
                                              * juce::MathConstants<float>::pi / 3.0f;
        const auto spokeInner = juce::Point<float> (reelCentre.x + std::cos (spokeAngle) * 2.0f,
                                                    reelCentre.y + std::sin (spokeAngle) * 2.0f);
        const auto spokeOuter = juce::Point<float> (reelCentre.x + std::cos (spokeAngle) * 10.5f,
                                                    reelCentre.y + std::sin (spokeAngle) * 10.5f);
        g.setColour (palette.accent.withAlpha (spokeAlpha));
        g.drawLine (spokeInner.x, spokeInner.y, spokeOuter.x, spokeOuter.y, 1.4f);

        g.setColour (palette.readout.withAlpha (0.10f + 0.25f * glowAmount));
        g.fillEllipse (spokeOuter.x - 1.6f, spokeOuter.y - 1.6f, 3.2f, 3.2f);
    }

    // Tape ribbon from the small reel down the deck's right edge, sagging with the
    // wow/flutter drift. It stays inside the clear corridor between the oversampling
    // box (ends x + 382 of the deck) and the harmonics readout (starts x + 608), so
    // it can never cross a control or a caption.
    const auto sag = (driftAmount - 0.5f) * 5.0f;
    juce::Path tapeRibbon;
    tapeRibbon.startNewSubPath (reelCentre.x, reelCentre.y + 12.0f);
    const auto deckY = static_cast<float> (layout.deck.getY());
    tapeRibbon.quadraticTo (reelCentre.x + 30.0f + sag,
                            deckY + 56.0f,
                            reelCentre.x + 6.0f + sag,
                            deckY + 60.0f);
    g.setColour (palette.knobEdge.withAlpha (0.35f));
    g.strokePath (tapeRibbon, juce::PathStrokeType (1.4f));

    std::array<RenderOrb, decorativeOrbCount> orbsToDraw;
    {
        const juce::SpinLock::ScopedLockType lock (renderOrbsLock);
        orbsToDraw = renderOrbs;
    }

    for (const auto& orb : orbsToDraw)
    {
        // The orbs drift only inside the deck's free corridor on the SWITCHES line,
        // between the oversampling box (ends x + 382) and the harmonics readout
        // (starts x + 608). The old vertical band was the heading + switches rows
        // (y + 14..62), which crossed the BYPASS switch and the deck hint on the
        // transport row above.
        const auto x = juce::jmap (orb.position.x, 0.0f, 1.5f,
                                   static_cast<float> (layout.deck.getX()) + 400.0f,
                                   static_cast<float> (layout.deck.getX()) + 590.0f);
        const auto y = juce::jmap (orb.position.y, 0.0f, 1.2f,
                                   static_cast<float> (layout.deck.getY()) + 74.0f,
                                   static_cast<float> (layout.deck.getY()) + 104.0f);
        const auto radius = juce::jmap (orb.radius, 0.045f, 0.057f, 2.0f, 3.5f);

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

    // Workflow state is cheap to poll at 30 Hz and makes the panel self-healing:
    // if the host, a session load or a preset change moves anything, the buttons,
    // badge and preset display catch up on the next frame instead of lying.
    const auto canUndoNow = audioProcessor.getUndoManager().canUndo();
    const auto canRedoNow = audioProcessor.getUndoManager().canRedo();
    const auto slotNow = audioProcessor.getActiveCompareSlot();
    const auto dirtyNow = audioProcessor.isCompareDirty();
    const auto presetNow = audioProcessor.getLastPresetIndex();
    if (slotNow != lastShownSlot || dirtyNow != lastShownDirty
        || canUndoNow != lastShownCanUndo || canRedoNow != lastShownCanRedo)
    {
        lastShownSlot = slotNow;
        lastShownDirty = dirtyNow;
        lastShownCanUndo = canUndoNow;
        lastShownCanRedo = canRedoNow;
        updateWorkflowButtons();
    }
    if (presetNow != lastShownPreset)
    {
        lastShownPreset = presetNow;
        // Item IDs are index + 1, so 0 means "no selection" - exactly what a user
        // preset (index -1) should show in the FACTORY combo.
        presetBox.setSelectedId (presetNow + 1, juce::dontSendNotification);
        refreshUserPresetList(); // a factory load clears the user-preset selection
    }

    const auto userPresetNow = audioProcessor.getCurrentPresetName();
    const auto presetDirtyNow = audioProcessor.isPresetDirty();
    if (userPresetNow != lastShownUserPreset || presetDirtyNow != lastShownPresetDirty)
    {
        lastShownUserPreset = userPresetNow;
        lastShownPresetDirty = presetDirtyNow;
        updateWorkflowButtons();
    }

    // While the user turns knobs, the live state drifts away from the stored side, so
    // the active A/B slot is re-mirrored periodically and COPY A / COPY B always capture
    // the machine as it is right now. This used to run on EVERY timer frame: each call
    // deep-copies the whole parameter tree twice, so at 30 Hz that was sixty whole-tree
    // copies a second landing on the message thread in the middle of a knob drag - on top
    // of the repaint work. That is exactly when the GUI thread is busiest and the audio
    // thread has least headroom, and the resulting dropouts are heard as clicks. It now
    // runs at ~5 Hz and only while the editor is actually on screen, which is far faster
    // than a person can hear the EDITED badge appear.
    if (isShowing() && ++compareMirrorTick >= 6)
    {
        compareMirrorTick = 0;
        audioProcessor.updateActiveCompareSlot();
    }

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

    // Header captions: the small brand strip sits ABOVE the big title (they used to
    // share the same band and printed over each other), the subtitle starts right of
    // the title box, and all three stay clear of the theme button / status badge.
    brandLabel.setBounds (layout.header.getX() + 26, layout.header.getY() + 8, 120, 12);
    titleLabel.setBounds (layout.header.getX() + 25, layout.header.getY() + 22, 96, 42);
    subtitleLabel.setBounds (layout.header.getX() + 126, layout.header.getY() + 40, 220, 18);
    themeButton.setBounds (layout.header.getRight() - 348, layout.header.getY() + 20, 126, 30);
    statusLabel.setBounds (layout.header.getRight() - 202, layout.header.getY() + 20, 181, 30);

    // The deck has three dedicated lines, each chain widths are tuned to fit the
    // minimum deck width (752 px at a 780 px window) without any overlap:
    //   line 1 (y + 32) - transport: MODEL | tape box | SPEED | speed box | BYPASS | hint
    //                     chain ends at x + 664 of 752.
    //   line 2 (y + 74) - POLARITY | AUTO GAIN | OVER | oversampling box (ends x + 324);
    //                     the harmonics readout takes the right end (x + 608..x + 738).
    //   line 3 (y + 110)- PRESET | factory box | USER box | SAVE | DEL | A/B | undo | redo
    //                     | badge (ends x + 751 of 752).
    // Line 3 sits at y + 110 rather than y + 116 so the preset badge is not jammed
    // against the controls, and the badge itself sits at y + 146 with clear air both
    // above and below it. The deck divider is drawn at y + 163 (just above the panel
    // edge) so the divider never crosses the badge text either.
    deckHeadingLabel.setBounds (layout.deck.getX() + 18, layout.deck.getY() + 6, 150, 16);

    // The build id rides the deck's heading strip, right-aligned and stopping short of
    // the reel (whose left edge is deck.right - 56). This strip is the only full-width
    // band near the top of the panel that is guaranteed empty at the 780 px minimum:
    // the header's equivalent gap shrinks to about 58 px there, and the switches row
    // is reserved for the drifting particles.
    buildLabel.setBounds (layout.deck.getX() + 190, layout.deck.getY() + 6,
                          layout.deck.getWidth() - 280, 16);

    tapeTypeLabel.setBounds (layout.deck.getX() + 18, layout.deck.getY() + 41, 45, 16);
    tapeTypeBox.setBounds (layout.deck.getX() + 66, layout.deck.getY() + 32, 148, 32);
    speedLabel.setBounds (tapeTypeBox.getRight() + 16, layout.deck.getY() + 41, 45, 16);
    speedBox.setBounds (tapeTypeBox.getRight() + 62, layout.deck.getY() + 32, 104, 32);
    bypassButton.setBounds (speedBox.getRight() + 18, layout.deck.getY() + 32, 92, 32);
    deckHintLabel.setBounds (bypassButton.getRight() + 14, layout.deck.getY() + 34, 160, 28);

    polarityButton.setBounds (layout.deck.getX() + 18, layout.deck.getY() + 74, 92, 32);
    autoGainButton.setBounds (polarityButton.getRight() + 6, layout.deck.getY() + 74, 100, 32);
    oversamplingLabel.setBounds (autoGainButton.getRight() + 16, layout.deck.getY() + 83, 40, 16);
    oversamplingBox.setBounds (autoGainButton.getRight() + 58, layout.deck.getY() + 74, 72, 32);
    instrumentLabel.setBounds (oversamplingBox.getRight() + 16, layout.deck.getY() + 83, 68, 16);
    instrumentBox.setBounds (oversamplingBox.getRight() + 16, layout.deck.getY() + 74, 112, 32);

    const auto harmonicsWidth = 130;
    harmonicsLabel.setBounds (layout.deck.getRight() - harmonicsWidth - 14,
                              layout.deck.getY() + 70, harmonicsWidth, 15);
    harmonicsReadout.setBounds (layout.deck.getRight() - harmonicsWidth - 14,
                                layout.deck.getY() + 88, harmonicsWidth, 14);

    presetHeadingLabel.setBounds (layout.deck.getX() + 18, layout.deck.getY() + 119, 46, 16);
    presetBox.setBounds (layout.deck.getX() + 66, layout.deck.getY() + 110, 128, 32);
    userPresetBox.setBounds (presetBox.getRight() + 6, layout.deck.getY() + 110, 100, 32);
    savePresetButton.setBounds (userPresetBox.getRight() + 5, layout.deck.getY() + 110, 40, 32);
    deletePresetButton.setBounds (savePresetButton.getRight() + 4, layout.deck.getY() + 110, 38, 32);
    copyAButton.setBounds (deletePresetButton.getRight() + 10, layout.deck.getY() + 110, 64, 32);
    copyBButton.setBounds (copyAButton.getRight() + 5, layout.deck.getY() + 110, 64, 32);
    compareButton.setBounds (copyBButton.getRight() + 5, layout.deck.getY() + 110, 54, 32);
    undoButton.setBounds (compareButton.getRight() + 5, layout.deck.getY() + 110, 44, 32);
    redoButton.setBounds (undoButton.getRight() + 5, layout.deck.getY() + 110, 44, 32);
    compareBadgeLabel.setBounds (redoButton.getRight() + 8, layout.deck.getY() + 110, 54, 32);

    // The badge band is inset further than the other deck text (22 px instead of 18)
    // and its caption is fitted to the width it actually has, so neither "FACTORY
    // STATE" nor a long user-preset name can run into the panel edges or be clipped.
    // y + 146 (not y + 149) so the badge has clear air BOTH above and below: the old
    // position left it hugging the deck's bottom border with the divider right under
    // it, which read as a caption that had fallen off the panel. 4 px above, 3 px
    // below, 2 px taller for the text.
    presetBadgeLabel.setBounds (layout.deck.getX() + 22, layout.deck.getY() + 146,
                                layout.deck.getWidth() - 44, 14);
    presetBadgeLabel.setFont (shrinkingFont (presetBadgeLabel.getText(), 8.0f,
                                             juce::Font::plain,
                                             static_cast<float> (presetBadgeLabel.getWidth()) - 4.0f));

    controlsHeadingLabel.setBounds (layout.controls.getX() + 18, layout.controls.getY() + 10, 210, 19);
    const auto controlsHintRight = layout.controls.getRight() - 12;
    const auto controlsHintLeft = juce::jmax (layout.controls.getX() + 240,
                                              controlsHeadingLabel.getRight() + 24);
    controlsHintLabel.setBounds (controlsHintLeft, layout.controls.getY() + 10,
                                 juce::jmax (0, controlsHintRight - controlsHintLeft), 19);
    metersHeadingLabel.setBounds (layout.meters.getX() + 16, layout.meters.getY() + 8, 130, 18);
    metersHintLabel.setBounds (layout.meters.getX() + 16, layout.meters.getY() + 27, 150, 14);
    const auto compressorLabelWidth = 120;
    compressorLabel.setBounds (layout.meters.getRight() - compressorLabelWidth - 14,
                               layout.meters.getY() + 8, compressorLabelWidth, 18);
    compressorReadout.setBounds (layout.meters.getRight() - compressorLabelWidth - 14,
                                 layout.meters.getY() + 27, compressorLabelWidth, 14);

    auto grid = layout.controls.reduced (14);
    grid.removeFromTop (42);
    grid.removeFromBottom (8);
    const auto cellWidth = grid.getWidth() / controlColumns;
    // Three rows, to match controlColumns: 4 x 3 = 12 cells for 11 controls.
    const auto rowHeight = grid.getHeight() / 3;

    // The number of rows is derived from the control count and the column count rather
    // than written out. Hardcoding it is how the previous "row == 1" became wrong the
    // moment a tenth control turned the grid from two rows into three: the last row
    // would then have been sized as if it were the bottom row and overflowed the panel.
    const auto gridRows = (static_cast<int> (controlCount) + controlColumns - 1) / controlColumns;

    for (std::size_t i = 0; i < controlCount; ++i)
    {
        const auto row = static_cast<int> (i / controlColumns);
        const auto column = static_cast<int> (i % controlColumns);
        auto cell = juce::Rectangle<int> (grid.getX() + column * cellWidth,
                                          grid.getY() + row * rowHeight,
                                          column == controlColumns - 1
                                              ? grid.getRight() - (grid.getX() + column * cellWidth)
                                              : cellWidth,
                                          row == gridRows - 1
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
    auto meterArea = layout.meters.reduced (12, 0);
    meterArea.removeFromTop (46);           // heading + hint labels, with 4 px clearance
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
