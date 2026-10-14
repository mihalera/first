// =============================================================================
//  The panel's meters.
//
//  The input and output loudness meters, the two glue stages' compressor
//  meters, and the MIX bar that shows what each of the seven principles is
//  contributing. Moved out of PluginEditor.cpp whole, prose and all: they are
//  three component classes the editor declares, and none of them is part of
//  building the panel.
// =============================================================================

#include "PluginEditor.h"
#include <cmath>
#include <utility>
#include "GUI/PanelPaint.h"
#include "GUI/PanelTabs.h"
#include "GUI/PanelText.h"

// The panel's own tables and palette names, said unqualified below as they
// were said in PluginEditor.cpp:
using j37panel::formatDb;
using j37panel::paletteFor;
using j37panel::principleColour;
using j37panel::shrinkingFont;

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
    const auto rowInset = juce::jmax (5, juce::roundToInt (static_cast<float> (getWidth()) * 0.055f));
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
// The MIX page's saturation mix, as hardware rather than as a number.
//
// Seven knobs answer "how much did I ask for"; this answers "how much did the
// core get", and after normalisation those are different questions - turning
// TAPE up from 60 with nothing else up changes nothing audible, because the
// other shares are scaled to fit around it. So the bar reads the SHARES, not
// the knob positions, and it reads them the way the panel reads everything else:
// seven recessed cells, each filled from the bottom in that machine's colour,
// each still visible at zero so the row always shows all seven.
//
// The empty mix is drawn as pure tape, the same answer SaturationCore gives -
// so the bar and the sound can never disagree about what the engine is doing.
//==============================================================================
void FirstAudioProcessorEditor::MixBar::setShares (const std::array<float, principleCount>& newShares,
                                                     float newTotal)
{
    shares = newShares;
    total = juce::jlimit (0.0f, 1.0f, newTotal);
    repaint();
}

void FirstAudioProcessorEditor::MixBar::paint (juce::Graphics& g)
{
    const auto& palette = paletteFor (darkTheme);

    auto area = getLocalBounds().toFloat();
    if (area.getWidth() < 60.0f || area.getHeight() < 24.0f)
        return;

    // The heading row: what this strip IS on the left, what the numbers mean on
    // the right. Both are the panel's own two voices - the accent for the name
    // of the thing, the secondary for the note about it - so the strip reads as
    // part of the panel rather than as an overlay somebody dropped on it.
//
    // The right-hand note carries the AMOUNT as well as the share, because the
    // seven cells always add up to 100 % and a bar that always reads full is a
    // bar that cannot report that every SOURCE knob is at zero. The total is
    // the number that says how hard the stage is running; the shares are the
    // number that says which machines are doing it.
    auto heading = area.removeFromTop (13.0f);
    g.setColour (palette.accent);
    g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
    g.drawText ("SATURATION MIX", heading.reduced (1.0f, 0.0f),
                juce::Justification::centredLeft, false);
    g.setColour (total > 0.005f ? palette.readout.withAlpha (0.85f) : palette.accent);
    g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
    g.drawText ("AMOUNT " + juce::String (juce::roundToInt (total * 100.0f)) + " %",
                heading.reduced (1.0f, 0.0f), juce::Justification::centredRight, false);
    g.setColour (palette.secondary);
    g.setFont (juce::Font (juce::FontOptions (8.0f)));
    g.drawText ("seven machines' shares", heading.reduced (1.0f, 0.0f).reduced (96.0f, 0.0f),
                juce::Justification::centredRight, false);

    // The cells. A 3 px gap reads as the milled groove between two plates, and
    // the recess is drawn with the same three terms every other recess on this
    // panel uses: a dark floor, a darker seam at the top (the shadow the lip
    // casts INTO the slot) and a lit line at the bottom (the far wall catching
    // the lamp).
    static constexpr const char* principleNames[principleCount] {
        "TAPE", "VALVE", "CASSETTE", "VINYL", "AMP", "TRANSFORMER", "DIGITAL"
    };

    const auto strip = area.reduced (0.0f, 3.0f);
    constexpr float gap = 3.0f;
    const auto cellWidth = (strip.getWidth() - gap * static_cast<float> (principleCount - 1))
                           / static_cast<float> (principleCount);

    for (int p = 0; p < principleCount; ++p)
    {
        const auto share = juce::jlimit (0.0f, 1.0f, shares[static_cast<std::size_t> (p)]);
        const auto ink = principleColour (p, darkTheme);
        const auto cell = juce::Rectangle<float> (strip.getX()
                                                      + static_cast<float> (p) * (cellWidth + gap),
                                                  strip.getY(), cellWidth, strip.getHeight());
        const auto corner = juce::jmin (4.0f, cell.getWidth() * 0.12f);

        g.setColour (palette.readout.withAlpha (darkTheme ? 0.55f : 0.42f));
        g.fillRoundedRectangle (cell, corner);

        // The lip's shadow, thrown INTO the well along its top and left edges -
        // three lines down the top, darkest at the lip where the wall it falls
        // from is nearest, and two down the left at half the strength because
        // the lamp stands off to the left and so misses the wall on that side.
        // Drawn before the fill so the fill sits down inside the shadow.
        for (int line = 0; line < 3; ++line)
        {
            const auto step = static_cast<float> (line);
            g.setColour (juce::Colours::black.withAlpha (0.17f - 0.048f * step));
            g.drawLine (cell.getX() + corner - step * 0.5f, cell.getY() + 1.0f + step * 1.2f,
                        cell.getRight() - corner + step * 0.5f, cell.getY() + 1.0f + step * 1.2f,
                        1.0f);
        }
        for (int line = 0; line < 2; ++line)
        {
            const auto step = static_cast<float> (line);
            g.setColour (juce::Colours::black.withAlpha (0.12f - 0.045f * step));
            g.drawLine (cell.getX() + 1.0f + step * 1.2f, cell.getY() + corner - step * 0.5f,
                        cell.getX() + 1.0f + step * 1.2f, cell.getBottom() - corner + step * 0.5f,
                        1.0f);
        }

        // The fill: from the floor up, in the machine's colour, with a bright
        // meniscus on its surface so a full cell and a nearly-full one are told
        // apart at a glance rather than only by the printed percentage.
        if (share > 0.0005f)
        {
            const auto fillHeight = juce::jmax (2.0f, share * cell.getHeight());
            const auto fill = juce::Rectangle<float> (cell.getX(), cell.getBottom() - fillHeight,
                                                      cell.getWidth(), fillHeight);
            juce::ColourGradient fluid (ink.brighter (0.30f), fill.getX(), fill.getY(),
                                        ink.darker (0.34f), fill.getX(), fill.getBottom(), false);
            fluid.addColour (0.28f, ink.brighter (0.10f));
            fluid.addColour (0.62f, ink);
            g.setGradientFill (fluid);
            g.fillRoundedRectangle (fill, corner);

            // The meniscus catches the lamp along its top edge, and casts its
            // own short shadow down into the fluid below it - two lines, because
            // one reads as a border and the pair reads as a surface.
            g.setColour (ink.brighter (0.55f).withAlpha (0.90f));
            g.drawLine (cell.getX() + 2.0f, fill.getY() + 0.5f,
                        cell.getRight() - 2.0f, fill.getY() + 0.5f, 1.2f);
            g.setColour (juce::Colours::black.withAlpha (0.20f));
            g.drawLine (cell.getX() + 2.0f, fill.getY() + 1.8f,
                        cell.getRight() - 2.0f, fill.getY() + 1.8f, 1.0f);
        }

        // The far wall, lit on its INNER face - the only bright edge on a
        // carved shape, and the term that gives the cell depth rather than a
        // border. The same line down the right edge, dimmer, for the same
        // reason the knob's rim bounce is dimmer than its lit bevel.
        g.setColour (palette.knobEdge.withAlpha (darkTheme ? 0.45f : 0.32f));
        g.drawLine (cell.getX() + 2.0f, cell.getBottom() - 0.5f,
                    cell.getRight() - 2.0f, cell.getBottom() - 0.5f, 1.0f);
        g.setColour (palette.knobEdge.withAlpha (darkTheme ? 0.26f : 0.18f));
        g.drawLine (cell.getRight() - 0.5f, cell.getY() + 2.0f,
                    cell.getRight() - 0.5f, cell.getBottom() - 2.0f, 1.0f);

        // The name, in the machine's own colour, and the share underneath it.
        // Both shrink to the cell rather than clipping: at the minimum panel
        // width a cell is about 100 px, which TRANSFORMER at 9 px just fits,
        // and on a narrower host it has to fit anyway.
        auto nameRow = cell.reduced (3.0f, 2.0f).removeFromTop (cell.getHeight() * 0.42f);
        g.setColour (share > 0.0005f ? ink.brighter (0.30f)
                                     : palette.secondary.withAlpha (0.65f));
        g.setFont (shrinkingFont (principleNames[p], 8.5f, juce::Font::bold, nameRow.getWidth()));
        g.drawText (principleNames[p], nameRow, juce::Justification::centred, false);

        auto valueRow = cell.reduced (3.0f, 2.0f).removeFromBottom (cell.getHeight() * 0.40f);
        g.setColour (share > 0.0005f ? palette.readout.withAlpha (0.92f)
                                     : palette.secondary.withAlpha (0.45f));
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                                  8.0f, juce::Font::bold)));
        g.drawText (juce::String (juce::roundToInt (share * 100.0f)) + " %",
                    valueRow, juce::Justification::centred, false);
    }
}
