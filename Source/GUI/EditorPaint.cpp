// =============================================================================
//  What the panel draws: paint(), and the two functions that prepare the texture
//  layers it blits.
//
//  Moved out of PluginEditor.cpp, prose and all. The texture layers are here rather
//  than with the setup because they are re-built on a resize and read on every
//  frame: their whole reason for existing is the drawing below them.
// =============================================================================

// The editor's own header: the class, the members and the palette type.
#include "PluginEditor.h"

// The panel vocabulary this file reads through, from the headers that hold
// it - the paint kit, the page table and the knob table.
#include "GUI/PanelTabs.h"
#include "GUI/PanelPaint.h"

// Only the names this file's own text uses: a header member that is not
// reachable from here should not look reachable.
using j37panel::paletteFor;
using j37panel::controlsDividerOffset;
using j37panel::drawScrew;
using j37panel::drawPanel;

#include <utility>

// The arithmetic texture's doses, one call for both renderers (see paint() and
// TapeScene::panelTexture): grain = static tooth, shimmer = live tape grain,
// wear = the blotches. The shimmer term carries the drive and the gain
// reduction, so the surface is ALIVE where the machine pushes signal - the
// same weights the fragment shader reads.
void FirstAudioProcessorEditor::configureTextureWeights (float drive, float gainReduction)
{
    constexpr float grainWeight  = 0.040f;
    constexpr float shimmerBase  = 0.015f;
    constexpr float shimmerDrive = 0.030f;
    constexpr float wearWeight   = 0.030f;

    // The doses above were tuned on the dark panel, where a faint light ink
    // reads against a near-black face. On the light panel the SAME alpha of a
    // DARK ink sits on a bright face and mostly vanishes - the user's own
    // report: the texture simply was not there. So light-theme doses are
    // multiplied up: both renderers receive the boosted weights (this feeds
    // the 2D image AND the scene's uTextureWeights), so GPU and software
    // panels keep showing the same surface.
    const auto doseBoost = darkTheme ? 1.0f : 2.2f;

    panelTextureDrive = juce::jlimit (0.0f, 1.0f, drive);
    panelTextureGainReduction = juce::jlimit (0.0f, 1.0f, gainReduction);

    const auto shimmer = (shimmerBase + shimmerDrive * panelTextureDrive) * doseBoost;
    panelTextureWeights = { grainWeight * doseBoost, shimmer, wearWeight * doseBoost };

    tapeScene.setTextureWeights (grainWeight * doseBoost, shimmer, wearWeight * doseBoost);
}

// One image per resize: the static grain (hash of screen pixels) and the wear
// blotches (value noise over a coarse grid), both at the design doses. Built at
// half resolution and scaled on draw, which is what keeps the one-time cost at
// a fraction of a second and the per-frame cost at a single blit.
void FirstAudioProcessorEditor::rebuildPanelTextureLayers()
{
    const auto bounds = getLocalBounds();
    if (bounds.isEmpty())
    {
        panelTextureImage = {};
        return;
    }

    constexpr float grainWeight = 0.040f;
    constexpr float wearWeight  = 0.030f;

    // Light theme prints DARK ink on a bright face, where the same alpha
    // nearly disappears - so the static layers are boosted by the same
    // factor configureTextureWeights uses for the live ones. The two dose
    // sets have to agree or the static grain and the shimmer read as two
    // different materials.
    const auto doseBoost = darkTheme ? 1.0f : 2.2f;
    const auto grainDose = grainWeight * doseBoost;
    const auto wearDose  = wearWeight * doseBoost;

    const auto w = juce::jmax (1, bounds.getWidth() / 2);
    const auto h = juce::jmax (1, bounds.getHeight() / 2);
    auto image = juce::Image (juce::Image::ARGB, w, h, true);

    const auto hashOf = [] (float x)
    {
        return std::fmod (std::abs (std::sin (x) * 43758.5453123f), 1.0f);
    };

    // Grain: the shader's hash(face * 7.5) against half-res pixel coordinates,
    // one centered ink dot per cell, centered on the panel colour so the layer
    // only adds texture, never tint.
    const auto grainInk = paletteFor (darkTheme).text;
    const auto grainEdge = paletteFor (darkTheme).knobEdge;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const auto n = hashOf (static_cast<float> (x * y) * 7.5f
                                   + static_cast<float> (x) * 1.7f
                                   + static_cast<float> (y) * 2.3f);
            const auto bright = n > 0.5f;
            const auto alpha = grainDose * std::abs (n - 0.5f) * 2.0f;
            image.setPixelAt (x, y, (bright ? grainInk : grainEdge)
                                        .withAlpha (juce::jlimit (0.0f, 1.0f, alpha)));
        }

    // Wear: big soft blotches from the shader's value-noise grid, drawn as
    // translucent ellipses over the grain.
    juce::Graphics imageGraphics (image);
    for (int gy = 0; gy < 5; ++gy)
        for (int gx = 0; gx < 9; ++gx)
        {
            const auto fi = static_cast<float> (gy * 9 + gx);
            const auto n = hashOf (fi * 4.19f + 2.17f);
            if (n < 0.55f)
                continue;

            const auto cx = (static_cast<float> (gx) + hashOf (fi * 1.61f)) / 9.0f;
            const auto cy = (static_cast<float> (gy) + hashOf (fi * 2.71f)) / 5.0f;
            const auto size = 30.0f + 40.0f * hashOf (fi * 3.37f);
            const auto alpha = wearDose * (n - 0.5f) * 2.0f;

            imageGraphics.setColour (grainEdge.withAlpha (
                juce::jlimit (0.0f, 1.0f, alpha)));
            imageGraphics.fillEllipse (cx * static_cast<float> (w) - size * 0.5f,
                                       cy * static_cast<float> (h) - size * 0.5f,
                                       size, size);
        }

    panelTextureImage = std::move (image);
}

//==============================================================================
void FirstAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto& palette = paletteFor (darkTheme);
    // The band this paint actually has to cover. The panel is repainted in bands
    // (see timerCallback) and most of what this function draws never changes, so
    // the clip is used to skip whole sections instead of drawing them and letting
    // the rasteriser discard the pixels - the draw calls, the geometry and the
    // trigonometry are all still paid for if they are issued.
    const auto clip = g.getClipBounds();
    const auto inClip = [&clip] (const juce::Rectangle<int>& area) { return area.intersects (clip); };

    g.fillAll (palette.background);

    const auto layout = getEditorLayout();
    // The neural card sits behind the SHAPERS page's picker cell, and its
    // geometry MIRRORS resized()'s: the grid is the controls band reduced by
    // 14, cut 78 from the top (controlsDividerOffset 92 - 14 + 8) and 8 from
    // the bottom; the member band is a fixed 50 px strip at the grid's bottom.
    // The picker rides the member band's FIRST column - its cell x is grid.x,
    // width cellWidth (cellWidth = gridW / 4).
    constexpr int tabColumnsNow = 4;
    constexpr int memberBandHeightNow = 50;
    auto gridNow = layout.controls.reduced (14);
    gridNow.removeFromTop (controlsDividerOffset - 14 + 8);
    gridNow.removeFromBottom (8);
    const auto memberBandY = gridNow.getBottom() - memberBandHeightNow;
    const auto cellWidthNow = gridNow.getWidth() / tabColumnsNow;

    // ------------------------------------------------------------------
    //  Chassis grain.
    //
    //  Drawn BEFORE the section panels so it sits on the chassis rather than over
    //  the controls - the old version painted its 18 rules across the whole
    //  window, which put lines through every panel and read as scratches rather
    //  than as a surface.
    //
    //  Two passes: a fine 3 px weave for the texture, and a much fainter 12 px
    //  one to break up the regularity. A single period reads as a pattern; two
    //  beating against each other read as brushed metal.
    // ------------------------------------------------------------------
    const auto chassis = getLocalBounds().reduced (8).toFloat();
    const auto grainAlpha = darkTheme ? 0.016f : 0.022f;
    const auto grainColour = palette.text.withAlpha (grainAlpha);

    // Two passes over the whole chassis is a few hundred rules, and the panels are
    // drawn over the top of them immediately afterwards - so most of them are
    // covered within the same paint. Skipping the ones the clip cannot see is what
    // keeps that affordable at 30 Hz with a context attached, where every one of
    // these repaints is a render of this component.
    // Float, to match the float y the loops below count with. getY()/getBottom()
    // are int, and the implicit conversions warned on every Clang build.
    const auto grainTop = static_cast<float> (clip.getY());
    const auto grainBottom = static_cast<float> (clip.getBottom());

    g.setColour (grainColour);
    for (float y = chassis.getY() + 2.0f; y < chassis.getBottom(); y += 3.0f)
        if (y >= grainTop && y <= grainBottom)
            g.drawHorizontalLine (juce::roundToInt (y),
                                  chassis.getX() + 6.0f, chassis.getRight() - 6.0f);

    g.setColour (palette.text.withAlpha (grainAlpha * 0.6f));
    for (float y = chassis.getY() + 7.0f; y < chassis.getBottom(); y += 12.0f)
        if (y >= grainTop && y <= grainBottom)
            g.drawHorizontalLine (juce::roundToInt (y),
                                  chassis.getX() + 6.0f, chassis.getRight() - 6.0f);

    // (The arithmetic texture is blitted further down, AFTER the panels, so the
    // panels' own faces sit over the chassis grain rather than over the texture.)


    drawPanel (g, getLocalBounds().reduced (8), palette, 7.0f);

    // The camera is set here rather than in the constructor because it depends
    // on the panel's own size: the viewer stands over the CENTRE of whatever
    // the panel currently is, so the same drawing code answers "how edge-on is
    // this knob" correctly at the minimum window size and at four times it.
    // The panel ITSELF, not a rectangle: the L&F converts it into the same
    // top-level space every control is measured in, which is what it needs to
    // know where each control stands relative to the viewer.
    customLookAndFeel.setPanelCamera (*this);
    // The in-tab lists' inset is solved from the same panel height the camera is
    // measured in (comboTextInset), so the second look-and-feel is told the same
    // thing here rather than reading a size of its own.
    inlineLookAndFeel.setPanelBounds (*this);
    drawPanel (g, layout.header, palette, 5.0f);
    drawPanel (g, layout.deck, palette, 5.0f);
    drawPanel (g, layout.controls, palette, 5.0f);
    drawPanel (g, layout.meters, palette, 5.0f);

    // ----------------------------------------------------------------------
    //  The panel's texture, 2D and mathematical - the same three terms the GPU
    //  scene shades, in the same doses (uTextureWeights there):
    //
    //   - static grain + wear blotches: ONE image, built once per resize in
    //     rebuildPanelTextureLayers() from the same hash spaces the shader
    //     uses (grain = fract(sin(p*7.5)), wear = value noise), blitted here
    //     in one call - AFTER the panels, so their faces carry it rather than
    //     hiding it, and BEFORE the child controls, which paint on top of
    //     everything here by being children;
    //   - live shimmer: a sparse drift of dots whose positions are the shader's
    //     hash(face*40 + t*(1+GR)) evaluated at a walking time offset - so the
    //     surface visibly responds to DRIVE and to the compressors. THIS is
    //     what the GL switch switches between: with the context attached the
    //     scene's fragment shader draws the identical texture definition on
    //     the GPU; without it, this ink does.
    //  Every dose is centered, so no branch can shift the panel's colour.
    // ----------------------------------------------------------------------
    if (panelTextureImage.isValid())
    {
        // Two rules keep the surface from reading as "square blocks":
        //
        //  1. RESAMPLING QUALITY. The ten-argument drawImage carries no quality
        //     of its own - the blit runs at whatever Graphics::setImageResamplingQuality
        //     last set, and Graphics' default is LOW, i.e. nearest-neighbour:
        //     a half-res layer drawn through it arrives as 2x2 blocks, which is
        //     precisely the "quadrats" the user saw on alternating frames. The
        //     quality is set explicitly for this one blit and restored after, so
        //     every other image in the panel keeps its old speed.
        //
        //  2. OPACITY-EXACT. The layer was built with per-pixel alpha already
        //     carrying the dose, so multiplying it again would brighten the grain
        //     whenever a repaint had to re-blend the layer, and the texture would
        //     visibly jump between "normal" and "too strong" as frames
        //     alternated. The alpha in the image IS the final amount.
        g.setOpacity (1.0f);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (panelTextureImage,
                     0, 0, getWidth(), getHeight(),
                     0, 0, panelTextureImage.getWidth(), panelTextureImage.getHeight(),
                     false /* fillAlphaChannelWithCurrentBrush: NO - the alpha in
                              the image is the final dose, re-multiplying it with
                              a brush would double the ink */);
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    }

    if (panelTextureWeights.shimmer > 0.0f)
    {
        const auto timeNow = static_cast<float> (juce::Time::getMillisecondCounterHiRes()) * 0.001f;
        // The shimmer drifts across the WHOLE panel: the timer now repaints
        // everything every tick, and the dots' hash chains are keyed to the
        // area they land in, so pinning them to the header+deck union while
        // the grain layer covered the whole editor read as two different
        // materials on one surface.
        const auto area = getLocalBounds().toFloat();
        const auto hashOf = [] (float x)
        {
            return std::fmod (std::abs (std::sin (x) * 43758.5453123f), 1.0f);
        };
        const auto timeStep = std::floor (timeNow * 8.0f);
        const auto drift = timeStep * 0.35f * (1.0f + panelTextureGainReduction);

        for (int i = 0; i < panelShimmerDotCount; ++i)
        {
            const auto fi = static_cast<float> (i);
            const auto u = hashOf (fi * 1.37f + drift);
            const auto v = hashOf (fi * 3.11f + drift * 0.73f + 19.7f);
            const auto a = hashOf (fi * 7.77f - drift * 1.31f + 5.3f);
            const auto bright = a > 0.5f;
            const auto ink = bright ? palette.text : palette.knobEdge;
            const auto alpha = panelTextureWeights.shimmer * (0.35f + 0.65f * std::abs (a - 0.5f) * 2.0f);

            g.setColour (ink.withAlpha (juce::jlimit (0.0f, 1.0f, alpha)));
            g.fillEllipse (area.getX() + u * area.getWidth(),
                           area.getY() + v * area.getHeight(), 1.7f, 1.7f);
        }
    }

    g.setColour (palette.accent.withAlpha (0.85f));
    g.fillRect (layout.header.getX() + 17, layout.header.getY() + 14, 3,
                layout.header.getHeight() - 28);

    g.setColour (palette.border.withAlpha (0.75f));
    // Under the tab bar, not through it: the divider used to sit 42 px down, which is
    // where the first tab button now starts.
    g.drawHorizontalLine (layout.controls.getY() + controlsDividerOffset,
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

    // The machine-state badge: a raised pill across the header grid's THIRD row,
    // wrapped around the statusLabel that lives there. The text moved to the
    // deck in an earlier pass but the drawn pill stayed in the header, so the
    // header switches printed over an empty badge plate - the user saw controls
    // on a design that had lost its text. The design now lives with its text
    // again, on a row nothing else occupies: the pill cannot be collided with.
    // The pill is the header grid's own width - four columns and three gaps,
    // less the 18 px the lamp takes - read from the SAME constants resized()
    // places the cells with (see PluginEditor.h). The first pass left a second
    // copy of the arithmetic here with the old 92 px cell in it, so the pill ran
    // 72 px wide of its band and sat off to the left of the statusLabel it wraps.
    // The pill's own rectangle comes from statusPillBounds(), which the timer
    // invalidates: one definition, so the lamp inside it cannot be drawn here and
    // repainted there.
    const auto statusPill = statusPillBounds().toFloat();
    g.setColour (palette.raised.darker (0.16f));
    g.fillRoundedRectangle (statusPill, 4.0f);

    // Status lamp: pulses with the compressor, so the panel shows that the
    // plugin is alive and working even when no gain reduction is happening.
    const auto lampCentre = juce::Point<float> (statusPill.getX() + 15.0f,
                                                statusPill.getCentreY());
    const auto lampPulse = 4.5f + 2.5f * glowAmount;
    g.setColour (palette.status.withAlpha (0.12f + 0.30f * glowAmount));
    g.fillEllipse (lampCentre.x - lampPulse * 2.2f, lampCentre.y - lampPulse * 2.2f,
                   lampPulse * 4.4f, lampPulse * 4.4f);
    g.setColour (palette.status);
    g.fillEllipse (lampCentre.x - 3.5f, lampCentre.y - 3.5f, 7.0f, 7.0f);

    // The neural model picker's card: the moved picker reads as a device with a
    // face, not as three unstyled widgets on the page - "it's black" was the
    // unstyled defaults speaking. The card is drawn behind the picker's member
    // cell only when SHAPERS is the visible tab - the page whose NEURAL knob it
    // belongs to.
    if (activeTabIsShapers)
    {
        const auto card = juce::Rectangle<float> (
            static_cast<float> (gridNow.getX()),
            static_cast<float> (memberBandY),
            static_cast<float> (cellWidthNow),
            static_cast<float> (memberBandHeightNow));
        g.setColour (palette.raised.brighter (0.06f));
        g.fillRoundedRectangle (card, 4.0f);
        g.setColour (palette.border.withAlpha (0.55f));
        g.drawRoundedRectangle (card, 4.0f, 1.0f);
    }

    // A hairline rule under the header strip separates the title block from the
    // controls; it runs the full width again - the badge it used to dodge lives
    // on the grid's third row now.
    g.setColour (palette.border.withAlpha (0.5f));
    g.drawHorizontalLine (layout.header.getBottom() - 4,
                          static_cast<float> (layout.header.getX() + 18),
                          static_cast<float> (layout.header.getRight() - 18));

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

    // The reel, the spokes, the tape ribbon and the orbs are the deck's, and they
    // are the most expensive thing in here: six spokes' worth of cos/sin, a path
    // stroke, and a copy of the particle array out from under its spin lock. They
    // are also the last thing this function draws, so when the clip is the header
    // band there is nothing after them to skip and this can simply stop.
    if (! inClip (layout.deck))
        return;

    // The reel and the ribbon below are the deck's SOFTWARE transport, and they are
    // drawn only when the 3D one is not. tapeScene.isSceneLive() is true only once
    // its context is up AND its shader linked, so a failed context, a driver that
    // will not oblige, and the user turning GL off all land back here - the panel
    // is never left with a hole where the transport was, and never shows two
    // transports at once. The particles further down are NOT gated: they live in
    // the gap the switches row left, a different part of the deck from the 3D
    // transport's window, so the two never overlap.
    //
    // The decorative reel lives in the deck's TOP-RIGHT corner, in the heading band
    // (y + 6..22) where no control sits, and clear of the harmonics readout, which is
    // right-aligned below it on the switches line. It used to sit ON the readout text.
    //
    // The reel is now a real transport indicator rather than decoration: its hub
    // dims as the platter slows, so a STOP empties it and a held SPINDOWN is
    // visible as the ring going grey before the spokes stop.
    if (! tapeScene.isSceneLive())
    {
        const auto reelCentre = juce::Point<float> (static_cast<float> (layout.deck.getRight() - 42),
                                                    static_cast<float> (layout.deck.getY() + 14));
        const auto platterNow = juce::jlimit (0.0f, 1.0f, smoothedMachineSpeed);
        g.setColour (palette.accent.withAlpha (0.10f + 0.30f * platterNow));
        for (const auto radius : { 11.0f, 7.0f, 2.0f })
            g.drawEllipse (reelCentre.x - radius, reelCentre.y - radius,
                           radius * 2.0f, radius * 2.0f, 1.0f);
        g.drawLine (reelCentre.x - 14.0f, reelCentre.y, reelCentre.x + 14.0f, reelCentre.y, 0.8f);
        g.drawLine (reelCentre.x, reelCentre.y - 14.0f, reelCentre.x, reelCentre.y + 14.0f, 0.8f);

        // Spokes: density tracks the selected tape speed, drift tracks the modulation,
        // and the whole set fades out as the platter coasts down - which is what makes a
        // spindown read as a turntable losing its drive rather than as a paused picture.
        const auto spokeAlpha = (0.20f + 0.45f * glowAmount) * (0.25f + 0.75f * platterNow);
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
        // wow/flutter drift. It hangs from the reel at deck.right - 42, so the only row
        // that has to stay clear of it is the model row, and that row stops at the
        // standard right inset (deck.right - 14) - twenty-eight pixels to the right of
        // the ribbon's leftmost point. That is a fact about the ribbon, not a number
        // that has to be kept in step with the row's width by hand.
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
    } // ! tapeScene.isSceneLive()

    std::array<RenderOrb, decorativeOrbCount> orbsToDraw;
    {
        const juce::SpinLock::ScopedLockType lock (renderOrbsLock);
        orbsToDraw = renderOrbs;
    }

    // The corridor the switches row left free, which resized() measured rather
    // than assumed - see deckParticleCorridorX. An empty range means the row had
    // no gap wide enough to be worth drawing into (a very narrow window), and
    // the right answer then is no particles at all: they are decoration, and
    // decoration on top of a control is the failure this whole pass is about.
    if (deckParticleCorridorX.getLength() < 20)
        return;

    for (const auto& orb : orbsToDraw)
    {
        const auto x = juce::jmap (orb.position.x, 0.0f, 1.5f,
                                   static_cast<float> (deckParticleCorridorX.getStart()) + 6.0f,
                                   static_cast<float> (deckParticleCorridorX.getEnd()) - 6.0f);
        const auto y = juce::jmap (orb.position.y, 0.0f, 1.2f,
                                   static_cast<float> (deckParticleCorridorY.getStart()),
                                   static_cast<float> (deckParticleCorridorY.getEnd()));
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
