// =============================================================================
//  The two look-and-feels, and the shared arithmetic they place text with.
//
//  Moved out of PluginEditor.cpp whole, prose and all, by matching its exact
//  text: every drawing routine, every comment that says why a gradient is
//  where it is, and the combo-box inset the two classes solve together. The
//  class declarations and their inline bodies stayed in PluginEditor.h, where
//  they were - this file is the half that draws.
// =============================================================================

#include "PluginEditor.h"
#include <cmath>
#include <cstring>
#include "GUI/PanelPaint.h"

// The knob's radial budget, which drawRotarySlider solves the cap, the groove and
// the marks inside. It is Source/GUI/KnobMetrics.h: called from here and from the
// editor's own strip solver, so it could not stay file-local to PluginEditor.cpp.
#include "GUI/KnobMetrics.h"

using j37panel::KnobMetrics;
using j37panel::computeKnobMetrics;

// The panel's own tables and palette names, said unqualified below as they
// were said in PluginEditor.cpp:
using j37panel::fillCylinderBarrel;
using j37panel::fillLitDome;
using j37panel::paletteForTheme;
using j37panel::principleColour;
using j37panel::principleOf;
using j37panel::shrinkingFont;

//==============================================================================
void J37LookAndFeel::drawRotarySlider (juce::Graphics& g,
                                       int x, int y, int width, int height,
                                       float sliderPos,
                                       const float rotaryStartAngle,
                                       const float rotaryEndAngle,
                                       juce::Slider& slider)
{
    const auto& palette = paletteForTheme (theme);
    const auto bounds = juce::Rectangle<float> (static_cast<float> (x),
                                                 static_cast<float> (y),
                                                 static_cast<float> (width),
                                                 static_cast<float> (height)).reduced (6.0f);

    // Everything a knob is made of lives inside the rectangle the layout gave
    // it, six pixels in. The radius solve below is what keeps the SOLID parts
    // (cap, collar, skirt, wall) inside that rectangle - it takes the side wall
    // out of the same budget as the face for exactly this reason - and this
    // ceiling is what keeps the two parts that are NOT hardware inside it too:
    // the tick marks, which are the furthest thing the scale draws, and the soft
    // halos, which are light and were the one thing allowed to spill. At the
    // minimum panel size a knob's ticks reached seven pixels past its own
    // rectangle and its halo twenty - far enough to land on the value readout
    // under it and on the member band under that, which is where a knob's arc
    // was seen standing on the switch beside it. The number itself, and every
    // ceiling measured from it, is solved below by computeKnobMetrics - this
    // comment describes one budget rather than five local constants.

    // The camera. A knob's top face is a disc, and a disc seen from anywhere
    // other than directly overhead is an ELLIPSE - flatter the nearer the near
    // edge of the panel it sits. Every term below is drawn as a circle about
    // the knob's own centre and the whole assembly is then squashed by one
    // transform, so the cap, its collar, its grooves, its rings and its ticks
    // all foreshorten TOGETHER, at this knob's angle, instead of the cap alone
    // being scaled while the ring around it stayed round. That single fact is
    // the difference between a row of hardware and a row of discs.
    // The knob's bounds place its ring and its cap, but only its POSITION on the
    // panel decides the angle they are seen from - so the slider comes with them.
    const auto view = perspectiveFor (bounds, slider);
    const auto centre = view.centre;
    g.saveState();
    // Clipped to the knob's own rectangle, so the ceiling above is a guarantee
    // rather than a hope: whatever the halo, the tracer's glow or a later
    // addition does, nothing this routine paints can reach the next cell. The
    // clip is opened before the camera transform, so it is the control's
    // rectangle in the panel - not a rectangle that foreshortens with it.
    g.reduceClipRegion (bounds.toNearestInt());
    g.addTransform (juce::AffineTransform (
                        view.squashX, 0.0f,
                        centre.x * (1.0f - view.squashX),
                        0.0f, view.squashY,
                        centre.y * (1.0f - view.squashY)));
    // The track ring and the tick marks live OUTSIDE the knob face, and DRIVE's
    // cells are WIDER than they are tall: an unconstrained ring was taller than
    // the cell, so the rasteriser clipped its top and bottom and the surviving
    // left/right fragments read as broken crescents - the "crooked toggles" on the
    // DRIVE page. Every ring in the assembly is therefore solved against the
    // half-extent the cell can actually show (see computeKnobMetrics), and the cap
    // takes what is left, so the whole thing stays concentric and fully visible at
    // every cell shape.
    // Every ceiling, and the cap, the collar, the groove and the marks that are
    // measured from it, in one call: see computeKnobMetrics above for what the
    // numbers are and why the groove is measured from the collar.
    const auto metrics = computeKnobMetrics (bounds.getWidth(), bounds.getHeight(), view.wall);
    const auto paintCeiling = metrics.paintCeiling;
    const auto radius = metrics.radius;
    const auto outerRadius = metrics.ring;

    // The side wall, as a fraction of the cap: what the skirt below is drawn
    // from, and the same term the metrics solved the cap against.
    const auto wallFraction = 0.12f + 0.40f * view.wall;
    const auto angle = juce::jmap (sliderPos, 0.0f, 1.0f, rotaryStartAngle, rotaryEndAngle);
    // JUCE's Path::addCentredArc measures clockwise from 12 o'clock and places a
    // point at (sin(angle), -cos(angle)). Convert that same angle to ordinary screen
    // coordinates for the tracer, ticks and pointer; using cos(angle), sin(angle)
    // directly rotates every indicator by 90 degrees and makes the arc look crooked.
    const auto screenAngle = angle - juce::MathConstants<float>::halfPi;

    // Hover feedback, NAM-style: a knob under the pointer lights its halo and
    // its face rim BEFORE the compressor breathes - the panel answers the
    // mouse first and the audio second, which is what makes hardware feel
    // awake. The boost is small enough that the activity glow still dominates
    // when the machine is working.
    const auto hovered = slider.isMouseOver() || slider.isMouseButtonDown();

    // Which principle this knob IS, if it is one of the seven SOURCE controls.
    // The editor registers each SOURCE slider against its machine where the
    // knobs are built (see J37LookAndFeel::registerPrincipleKnob), so the knob
    // carries its own identity rather than this paint routine holding a second
    // table of which control is which: one place to change when a knob moves,
    // and none to change when a paint routine does.
    const auto principle = principleOf (slider);
    const auto accent = principle >= 0 ? principleColour (principle, isDarkTheme())
                                       : palette.accent;

    //------------------------------------------------------------------
    //  Animation layer 1: a soft halo that breathes with the compressor
    //  activity, plus a slow pulse so the panel never looks frozen.
    //------------------------------------------------------------------
    // The phase is this knob's OWN, derived from where it stands on the panel.
    // Every halo used to ride the one shared phase, so a page of twelve knobs
    // brightened and dimmed in lockstep - which the eye reads as a global
    // flicker, because it IS one. Offset by position, the same nine lines of
    // drawing read as a panel with current running through it.
    const auto knobPosition = offsetToTopLevel (slider);
    const auto breathPhase = animationPhase
                             + knobPosition.x * 0.031f + knobPosition.y * 0.017f;
    const auto breath = 0.5f + 0.5f * std::sin (breathPhase);
    const auto haloAlpha = 0.05f + (hovered ? 0.06f : 0.0f)
                               + activity * 0.20f * (0.6f + 0.4f * breath);
    if (haloAlpha > 0.01f)
    {
#if J37_HAS_MELATONIN_BLUR
        // melatonin_blur draws the halo as one real blurred shadow rather than a
        // stack of stroked rings. The API renders a Path, and the shadow is cached
        // against that path, so the glow costs one blur the first time the radius
        // changes and is reused on every frame after that.
        melatonin::DropShadow glow;
        glow.setColor (accent.withAlpha (haloAlpha));
        // The blur radius is what decides how far this light reaches past the
        // cap, so it is solved from the room left to the ceiling rather than
        // fixed: the halo is as big as its cell allows and no bigger.
        const auto glowRadius = 6.0f + (hovered ? 3.0f : 0.0f) + activity * 6.0f * breath;
        glow.setRadius (juce::jmax (2.0f, juce::jmin (glowRadius, paintCeiling - radius)));

        juce::Path haloPath;
        haloPath.addEllipse (centre.x - radius, centre.y - radius,
                             radius * 2.0f, radius * 2.0f);
        glow.render (g, haloPath);
#else
        for (int ring = 3; ring >= 1; --ring)
        {
            const auto haloRadius = juce::jmin (outerRadius + static_cast<float> (ring) * 5.0f
                                                    + activity * 4.0f * breath,
                                                paintCeiling);
            g.setColour (accent.withAlpha (haloAlpha / static_cast<float> (ring)));
            g.drawEllipse (centre.x - haloRadius, centre.y - haloRadius,
                           haloRadius * 2.0f, haloRadius * 2.0f, 1.6f);
        }
#endif
    }

    // The knob is let INTO the panel rather than sitting on it, so the plate
    // around it is milled down a step. A dark seat ring with a lit upper-left
    // lip is what says "recessed"; before this the cap stood on a flat disc and
    // looked stuck there rather than fitted into anything.
    const auto seatRadius = outerRadius + 2.5f;
    g.setColour (juce::Colours::black.withAlpha (0.22f));
    g.fillEllipse (centre.x - seatRadius, centre.y - seatRadius,
                   seatRadius * 2.0f, seatRadius * 2.0f);
    juce::Path seatLip;
    seatLip.addCentredArc (centre.x, centre.y, seatRadius - 0.7f, seatRadius - 0.7f, 0.0f,
                           5.30f, 2.05f, true);
    g.setColour (palette.panel.brighter (0.22f).withAlpha (0.30f));
    g.strokePath (seatLip, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

    // The scale is a GROOVE cut into that seat, not a line printed on it: a
    // dark channel with a lit floor along its upper lip, so the arc sits down
    // inside the metal instead of floating over it.
    juce::Path scaleTrack, scaleFloor;
    scaleTrack.addCentredArc (centre.x, centre.y, outerRadius, outerRadius, 0.0f,
                              rotaryStartAngle, rotaryEndAngle, true);
    scaleFloor.addCentredArc (centre.x, centre.y, outerRadius - 0.9f, outerRadius - 0.9f, 0.0f,
                              rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (juce::Colours::black.withAlpha (0.34f));
    g.strokePath (scaleTrack, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
    g.setColour (palette.knobEdge.withAlpha (0.85f));
    g.strokePath (scaleFloor, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

    // The live arc is a lit bar IN that groove, so it takes the same two terms
    // plus a bright core: a wide saturated shoulder for the bar's own shadowed
    // side, a narrower saturated core, and a near-white filament riding it -
    // three lines rather than one because one line is a highlighter and three
    // is an indicator lamp behind a diffuser.
    juce::Path activeTrack, activeCore;
    activeTrack.addCentredArc (centre.x, centre.y, outerRadius, outerRadius, 0.0f,
                               rotaryStartAngle, angle, true);
    activeCore.addCentredArc (centre.x, centre.y - 0.6f, outerRadius, outerRadius, 0.0f,
                              rotaryStartAngle, angle, true);
    g.setColour (accent.darker (0.50f).withAlpha (0.90f));
    g.strokePath (activeTrack, juce::PathStrokeType (4.2f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
    g.setColour (accent);
    g.strokePath (activeTrack, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
    g.setColour (accent.brighter (0.45f).withAlpha (0.80f));
    g.strokePath (activeCore, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));

    // The tracer dot - the lit bead riding the end of the active arc - is the
    // LAST thing the knob draws, over its own cap: see below, after the hub. It
    // used to be painted here, and the collar is drawn after this point, so the
    // bead's inner half was under the collar at every size - a light that is
    // three-quarters of a circle is a light that reads as clipped metal. It is
    // still drawn inside the camera transform (with the hub), so it squashes
    // with the groove it rides rather than staying round on a foreshortened
    // scale.

    //------------------------------------------------------------------
    //  Animation layer 2: the scale ticks tremble with the transport
    //  drift, so wow and flutter are visible as well as audible.
    //------------------------------------------------------------------
    const auto driftWobble = (drift - 0.5f) * 2.0f;
    const auto tickIntervals = metrics.tickIntervals;
    const auto majorEvery = tickIntervals / 12;

    for (int tick = 0; tick <= tickIntervals; ++tick)
    {
        const auto tickAngle = juce::jmap (static_cast<float> (tick), 0.0f,
                                           static_cast<float> (tickIntervals),
                                           rotaryStartAngle, rotaryEndAngle);
        const auto tickScreenAngle = tickAngle - juce::MathConstants<float>::halfPi;
        const auto major = tick % majorEvery == 0;
        const auto tremble = std::sin (tickScreenAngle * 3.0f + animationPhase * 1.7f)
                             * driftWobble * 1.3f;
        // The marks hang off the groove by the groove's own proportions and stop
        // at the ceiling, so the scale keeps its shape at every knob size instead
        // of being a comb on a big one and a stub on a small one (see
        // computeKnobMetrics). The two weights are also readable at last: the
        // minor weight used to be 0.42 alpha at 0.8 px, which measures 1.8:1
        // against the ivory panel - below the 3:1 a graphic needs, and the reason
        // a thirteen-mark scale read as five marks.
        const auto innerRadius = juce::jmin (outerRadius + (major ? metrics.tickGap : metrics.minorGap) + tremble,
                                             paintCeiling - (major ? metrics.tickClearance
                                                                   : metrics.minorClearance));
        const auto outerTickRadius = juce::jmin (innerRadius + (major ? metrics.tickLength
                                                                       : metrics.minorLength),
                                                 paintCeiling);
        const auto inner = centre + juce::Point<float> (std::cos (tickScreenAngle) * innerRadius,
                                                        std::sin (tickScreenAngle) * innerRadius);
        const auto outer = centre + juce::Point<float> (std::cos (tickScreenAngle) * outerTickRadius,
                                                        std::sin (tickScreenAngle) * outerTickRadius);
        g.setColour (palette.knobEdge.withAlpha (major ? 0.78f : 0.58f));
        g.drawLine (inner.x, inner.y, outer.x, outer.y, major ? 1.2f : 1.0f);
    }

    // The neutral mark. On a control whose range straddles zero - every trim,
    // every EQ band, the polarity of a side channel - the scale has no reference
    // point without it: every mark on it says "a division", and not one of them
    // says "this is zero". It sits at the range's mid angle (which on this rotary
    // convention is 9 o'clock, the same place the pointer stands at half travel)
    // and it is drawn in the knob's own accent, because it is the one mark that
    // means something other than a graduation.
    if (slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0)
    {
        const auto neutralAngle = juce::jmap (0.5f, 0.0f, 1.0f,
                                              rotaryStartAngle, rotaryEndAngle)
                                  - juce::MathConstants<float>::halfPi;
        const auto neutralCos = std::cos (neutralAngle), neutralSin = std::sin (neutralAngle);
        const auto neutralInner = juce::jmin (outerRadius + metrics.tickGap,
                                              paintCeiling - metrics.tickClearance);
        const auto neutralOuter = juce::jmin (neutralInner + metrics.tickLength, paintCeiling);

        // A dark line under it first, so the mark reads as cut into the plate on
        // the themes where the accent is close to the panel's own value.
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawLine (centre.x + neutralCos * neutralInner, centre.y + neutralSin * neutralInner,
                    centre.x + neutralCos * neutralOuter, centre.y + neutralSin * neutralOuter, 2.6f);
        g.setColour (accent.withAlpha (0.90f));
        g.drawLine (centre.x + neutralCos * neutralInner, centre.y + neutralSin * neutralInner,
                    centre.x + neutralCos * neutralOuter, centre.y + neutralSin * neutralOuter, 1.4f);
    }

    //----------------------------------------------------------------------
    //  The cap. This is the piece the panel is judged on, so it is built as a
    //  piece of hardware rather than as a shaded disc: a shadow it casts on the
    //  panel, a KNURLED collar - the fine teeth a machined knob is gripped by -
    //  a domed top lit from the same up-and-left source as the panels and the
    //  screws, a bright bevel arc where that dome turns into the light and a
    //  dark one where it turns away, and a tapered indicator that casts its own
    //  one-pixel shadow onto the dome.
    //
    //  Every term is drawn from the palette, so metal reads as steel, charcoal
    //  as bakelite and ivory as brass without a second drawing path - the only
    //  thing that is not palette-driven is the accent, and that is the one thing
    //  that is SUPPOSED to differ between knobs (the seven principles).
    //----------------------------------------------------------------------
    const auto collarRadius = metrics.collar;

    // The shadow first, so everything else sits on top of it. The light is up
    // and left (as everywhere else on this panel), so the shadow falls down and
    // to the right - a shadow that fell the other way read as a glow.
#if J37_HAS_MELATONIN_BLUR
    {
        melatonin::DropShadow capShadow;
        capShadow.setColor (juce::Colours::black.withAlpha (0.34f));
        capShadow.setRadius (radius * 0.5f + 2.0f);

        juce::Path capPath;
        capPath.addEllipse (centre.x - collarRadius + 1.5f, centre.y - collarRadius + 2.0f,
                            collarRadius * 2.0f, collarRadius * 2.0f);
        capShadow.render (g, capPath);
    }
#else
    for (int layer = 3; layer >= 1; --layer)
    {
        const auto spread = static_cast<float> (layer) * 1.1f;
        g.setColour (juce::Colours::black.withAlpha (0.040f));
        g.fillEllipse (centre.x - collarRadius + spread * 0.5f,
                       centre.y - collarRadius + spread,
                       collarRadius * 2.0f, collarRadius * 2.0f);
    }
#endif

    // The knob's own THICKNESS. Seen from above and a little to the front, a
    // real cap shows a sliver of its side wall below the top face, and that
    // sliver is most of what tells the eye the part has depth - without it the
    // cap and its shadow describe a disc floating over the panel. One ellipse
    // pushed down and shaded top-to-bottom, drawn BEFORE the collar so the
    // collar's rim overlaps its upper edge and only the lower crescent shows.
    const auto skirt = radius * wallFraction * 1.15f;
    juce::ColourGradient skirtShade (palette.knobEdge.darker (0.16f), centre.x,
                                     centre.y + collarRadius - skirt,
                                     palette.knobEdge.darker (0.62f), centre.x,
                                     centre.y + collarRadius + skirt * 2.4f, false);
    skirtShade.addColour (0.50f, palette.knobEdge.darker (0.38f));
    g.setGradientFill (skirtShade);
    g.fillEllipse (centre.x - collarRadius, centre.y - collarRadius + skirt,
                   collarRadius * 2.0f, collarRadius * 2.0f);

    // The collar: a ring of metal, shaded top-to-bottom like the switch barrels
    // (fillCylinderBarrel's profile) so the two families of hardware on this
    // panel are lit by the same lamp.
    juce::ColourGradient collar (palette.knobEdge.brighter (0.12f), centre.x,
                                 centre.y - collarRadius,
                                 palette.knobEdge.darker (0.38f), centre.x,
                                 centre.y + collarRadius, false);
    collar.addColour (0.40f, palette.knobEdge);
    g.setGradientFill (collar);
    g.fillEllipse (centre.x - collarRadius, centre.y - collarRadius,
                   collarRadius * 2.0f, collarRadius * 2.0f);

    // The knurl. Each tooth is one short radial line whose brightness comes from
    // the cosine between its own surface normal and the panel's light, so the
    // teeth catch the light on one side and fall into shadow on the other - the
    // same arithmetic the barrel shading uses, applied per tooth. Teeth scale
    // with the knob, because a fixed count on a 14 px knob is a solid ring and
    // on a 40 px knob is a comb.
    const auto teeth = juce::jlimit (16, 44, juce::roundToInt (collarRadius * 1.15f));
    for (int tooth = 0; tooth < teeth; ++tooth)
    {
        const auto a = static_cast<float> (tooth) / static_cast<float> (teeth)
                       * juce::MathConstants<float>::twoPi;
        const auto ca = std::cos (a), sa = std::sin (a);
        const auto facing = -0.55f * ca - 0.83f * sa;   // light from up and left
        const auto shade = juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * facing);

        g.setColour (palette.knobEdge.darker (0.55f)
                         .interpolatedWith (palette.knobHighlight.brighter (0.08f), shade)
                         .withAlpha (0.5f));
        g.drawLine (centre.x + ca * (radius + 0.5f), centre.y + sa * (radius + 0.5f),
                    centre.x + ca * (collarRadius + 0.5f), centre.y + sa * (collarRadius + 0.5f),
                    0.9f);
    }

    // The dome: body, terminator, specular and bounce in one call - see
    // fillLitDome for why all four, and why a radial gradient alone leaves the
    // cap looking like a sticker.
    fillLitDome (g, centre, radius, palette.knobFace, palette.knobHighlight,
                 palette.knobEdge.darker (0.26f), hovered ? 1.15f : 1.0f);

    // The turning grooves. A real cap is faced off on a lathe and the tool
    // leaves concentric ridges on it; they catch the lamp on the side facing
    // it and vanish on the other, which is most of the difference between
    // "metal" and "grey disc". Six rings, alternating light and dark, thinning
    // toward the rim where the face has turned away from the tool - a ring that
    // keeps its full strength all the way out flattens the dome again.
    for (int ring = 1; ring <= 6; ++ring)
    {
        const auto t = static_cast<float> (ring) / 7.0f;
        const auto ringRadius = radius * (0.22f + t * 0.76f);
        const auto fade = 1.0f - t * 0.55f;
        g.setColour ((ring % 2 == 0 ? palette.knobHighlight : palette.knobEdge)
                         .withAlpha ((ring % 2 == 0 ? 0.10f : 0.17f) * fade));
        g.drawEllipse (centre.x - ringRadius, centre.y - ringRadius,
                       ringRadius * 2.0f, ringRadius * 2.0f, 0.7f);
    }

    // The bevel: the dome's own rim, one pixel in, bright where it turns up into
    // the light and dark where it turns away. Two arcs rather than an outline,
    // because an outline would say "circle" where this says "turned edge".
    juce::Path bevelLit, bevelShade;
    bevelLit.addCentredArc (centre.x, centre.y, radius - 0.8f, radius - 0.8f, 0.0f,
                            5.25f, 1.05f, true);
    bevelShade.addCentredArc (centre.x, centre.y, radius - 0.8f, radius - 0.8f, 0.0f,
                              2.10f, 4.19f, true);
    g.setColour (palette.knobHighlight.withAlpha (hovered ? 0.80f : 0.58f));
    g.strokePath (bevelLit, juce::PathStrokeType (1.1f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    g.setColour (palette.readout.withAlpha (0.42f));
    g.strokePath (bevelShade, juce::PathStrokeType (1.1f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));

    // The bounce along the shadow-side rim. The shade the bevel draws is not the
    // last word on that arc: without a faint lit line at the very edge under it,
    // the rim dies to black and the cap loses the thickness the skirt gave it.
    juce::Path rimBounce;
    rimBounce.addCentredArc (centre.x, centre.y, radius - 0.9f, radius - 0.9f, 0.0f,
                             2.05f, 4.30f, true);
    g.setColour (palette.knobHighlight.withAlpha (0.18f));
    g.strokePath (rimBounce, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

    // The inner step: a second, smaller dome standing proud of the first. It is
    // what makes the cap read as turned from a blank with a step in it rather
    // than as one disc with a gradient on it - and it gets the same four
    // lighting terms as the face, because a step is a step, not a flat disc
    // pasted inside a flat one. The dark ring under it is the occlusion where
    // it rises out of the face, which is where its height comes from.
    const auto innerRadius = radius * 0.56f;
    g.setColour (juce::Colours::black.withAlpha (0.30f));
    g.drawEllipse (centre.x - innerRadius - 0.7f, centre.y - innerRadius - 0.5f,
                   (innerRadius + 0.7f) * 2.0f, (innerRadius + 0.7f) * 2.0f, 1.1f);
    fillLitDome (g, centre, innerRadius, palette.knobFace.brighter (0.05f),
                 palette.knobHighlight.brighter (0.06f),
                 palette.knobFace.darker (0.32f), hovered ? 1.15f : 1.0f);
    g.setColour (palette.knobEdge.withAlpha (0.55f));
    g.drawEllipse (centre.x - innerRadius, centre.y - innerRadius,
                   innerRadius * 2.0f, innerRadius * 2.0f, 0.9f);

    // The glint: a small bright lozenge on the dome's lit shoulder, and the slow
    // specular sweep that keeps the cap alive under the pointer. Both are low
    // alpha - a real highlight is a FEW percent of the surface, and the old 10-16
    // percent sweep was the panel's only thing that ever looked wet.
    g.setColour (palette.knobHighlight.withAlpha (0.26f + (hovered ? 0.10f : 0.0f)));
    g.fillEllipse (centre.x - radius * 0.62f, centre.y - radius * 0.70f,
                   radius * 0.42f, radius * 0.26f);

    const auto sweepAngle = animationPhase * 0.6f;
    const auto sweepCentre = centre + juce::Point<float> (std::cos (sweepAngle) * radius * 0.40f,
                                                          std::sin (sweepAngle) * radius * 0.40f);
    g.setColour (palette.knobHighlight.withAlpha (0.05f + 0.04f * breath));
    g.fillEllipse (sweepCentre.x - radius * 0.36f, sweepCentre.y - radius * 0.36f,
                   radius * 0.72f, radius * 0.72f);

    // The indicator: a tapered rib from the hub out across the face, in the
    // knob's own accent, with a dark copy of itself one pixel down and right.
    // The offset copy is what turns a line into something that stands up off the
    // dome - it is the same trick the panel's drop shadows use, at 3 px.
    // Its length stops WELL short of the rim, and the number is not taste: the
    // six turning grooves above sit at radius * (0.22 + t * 0.76) for t = 1/7
    // .. 6/7, so the outermost one - the last division the face is cut with - is
    // at 0.871 of the face. At 0.86 the rib's tip landed on that ring, which is
    // what put the pointer on the dial's divisions rather than in the face: on
    // the small caps the strip draws there was nothing but the tip and the
    // outermost ring in the same few pixels, and at every size the rib ran out
    // to the bevel. Between the fourth ring (0.654) and the fifth (0.763) it is
    // clear of both at every cell, and it still reads as a pointer because it
    // starts at the hub and is drawn from the same accent as the arc.
    const auto pointerLength = radius * 0.72f;
    const auto pointerHalfWidth = juce::jmax (1.5f, radius * 0.085f);
    const auto cosA = std::cos (screenAngle), sinA = std::sin (screenAngle);
    const auto tipX = centre.x + cosA * pointerLength;
    const auto tipY = centre.y + sinA * pointerLength;

    juce::Path pointer, pointerShadow;
    pointer.addTriangle (centre.x - sinA * pointerHalfWidth,
                         centre.y + cosA * pointerHalfWidth,
                         centre.x + sinA * pointerHalfWidth,
                         centre.y - cosA * pointerHalfWidth,
                         tipX, tipY);
    pointerShadow.addTriangle (centre.x - sinA * pointerHalfWidth + 1.0f,
                               centre.y + cosA * pointerHalfWidth + 1.2f,
                               centre.x + sinA * pointerHalfWidth + 1.0f,
                               centre.y - cosA * pointerHalfWidth + 1.2f,
                               tipX + 1.0f, tipY + 1.2f);
    // ...and it is a SHADOW, which is what this colour now says. The copy used to
    // be the near-white readout colour at 40 percent - a light source where the
    // lamp cannot reach, which made every pointer read as a groove cut into the
    // dome instead of a rib standing on it, on every theme.
    g.setColour (juce::Colours::black.withAlpha (0.34f));
    g.fillPath (pointerShadow);
    g.setColour (accent.darker (0.12f));
    g.fillPath (pointer);

    // The rib's own two edges: a bright one along the side the lamp reaches and
    // a dark one along the side it does not. Without them the pointer is a
    // flat triangle of colour lying ON the dome, and with them it is a raised
    // moulding standing OFF it - the same trick as the drop shadow above, run
    // along its length instead of under it.
    const auto edgeAlpha = juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * (-cosA * 0.55f - sinA * 0.83f));
    g.setColour (accent.brighter (0.42f).withAlpha (0.30f + 0.45f * edgeAlpha));
    g.drawLine (centre.x - sinA * pointerHalfWidth, centre.y + cosA * pointerHalfWidth,
                tipX, tipY, 0.8f);
    g.setColour (juce::Colours::black.withAlpha (0.30f * (1.0f - edgeAlpha) + 0.10f));
    g.drawLine (centre.x + sinA * pointerHalfWidth, centre.y - cosA * pointerHalfWidth,
                tipX, tipY, 0.8f);

    g.setColour (accent.brighter (0.30f).withAlpha (0.75f));
    g.drawLine (centre.x, centre.y, centre.x + cosA * pointerLength * 0.86f,
                centre.y + sinA * pointerLength * 0.86f, 0.9f);

    // The hub: the screw the rib is set into - the same lit dome as the face,
    // small enough that only its specular and its terminator survive, which is
    // exactly what a real screw head is: a bright crescent and a dark one.
    const auto hubRadius = juce::jmax (2.2f, radius * 0.15f);
    fillLitDome (g, centre, hubRadius, palette.knobEdge.darker (0.08f),
                 palette.knobHighlight, palette.knobEdge.darker (0.55f));
    g.setColour (juce::Colours::black.withAlpha (0.32f));
    g.drawEllipse (centre.x - hubRadius, centre.y - hubRadius,
                   hubRadius * 2.0f, hubRadius * 2.0f, 0.8f);
    g.setColour (palette.knobHighlight.withAlpha (0.45f));
    g.drawEllipse (centre.x - hubRadius, centre.y - hubRadius,
                   hubRadius * 2.0f, hubRadius * 2.0f, 0.7f);

    // The tracer bead, last so nothing on the knob can cover it. It rides the
    // scale groove's centre line at the live angle, and its own glow is a wider,
    // fainter copy of the same disc. Both are drawn AFTER the collar, the knurl
    // and the cap: the collar stands a few pixels inside the groove, where a
    // bead of this size always overlapped it, and a lit indicator is the one
    // piece of this widget that is meant to sit on top of the hardware rather
    // than inside it. Still inside the camera transform, so it foreshortens with
    // the groove it belongs to.
    const auto tracer = centre + juce::Point<float> (std::cos (screenAngle) * outerRadius,
                                                     std::sin (screenAngle) * outerRadius);
    const auto tracerPulse = 2.6f + 1.4f * breath + activity * 2.0f;
    g.setColour (accent.withAlpha (0.35f));
    g.fillEllipse (tracer.x - tracerPulse * 1.9f, tracer.y - tracerPulse * 1.9f,
                   tracerPulse * 3.8f, tracerPulse * 3.8f);
    g.setColour (palette.readout);
    g.fillEllipse (tracer.x - tracerPulse, tracer.y - tracerPulse,
                   tracerPulse * 2.0f, tracerPulse * 2.0f);

    // Out of the camera transform before the focus ring: a focus ring is the one
    // thing on the panel that belongs to the SCREEN rather than to the object,
    // so it must not foreshorten with the knob it is around.
    g.restoreState();

    if (slider.hasKeyboardFocus (false))
    {
        g.setColour (palette.accent.withAlpha (0.85f));
        g.drawEllipse (bounds.reduced (1.5f), 1.1f);
    }
}

//==============================================================================
//  THE CAMERA. See J37LookAndFeel::Perspective for why the panel has one.
//
//  The viewer stands above the panel's CENTRE and a little way BACK from its
//  near edge - which is where a person sitting at a desk is, and the only place
//  from which a control's top face and its side wall are visible at the same
//  time. Three numbers fall out of that position for any control on the panel:
//
//    tilt     how far off the viewer's own axis it sits, 0 at the centre of
//             the panel and 1 at the corners. This is what makes two knobs on
//             the SAME row read as two objects rather than one repeated symbol.
//
//    squashY  how much its top face is foreshortened down the panel. Everything
//             is seen from in front, so every top face is an ellipse rather
//             than a circle - but a control near the near edge is much closer
//             to the viewer than one near the far edge and is seen far more
//             edge-on, so ITS ellipse is far flatter. Flattening them all by
//             the same amount is what a faked perspective does, and the eye
//             reads the repetition as a graphic instead of as a surface.
//
//    wall     how much of the side wall that angle exposes. Seen straight down
//             there is none at all; seen edge-on the whole height of the part
//             shows below its top face. This is the term that makes a switch a
//             solid object standing on the panel rather than a shape painted on
//             it.
//==============================================================================
juce::Point<float> J37LookAndFeel::offsetToTopLevel (const juce::Component& component) noexcept
{
    juce::Point<int> offset;
    for (auto* c = &component; c != nullptr; c = c->getParentComponent())
        offset += c->getPosition();
    return offset.toFloat();
}

void J37LookAndFeel::setPanelCamera (const juce::Component& panel) noexcept
{
    // The camera is stored in TOP-LEVEL space, because that is the one space
    // every control can be measured in: a control's own bounds start at (0, 0),
    // so without this a knob in the tab grid and a switch in the header would
    // both be told they sit in the same corner of the world.
    const auto origin = offsetToTopLevel (panel);
    cameraPanel = panel.getLocalBounds().toFloat().translated (origin.x, origin.y);
    cameraRoot = panel.getTopLevelComponent();
}

J37LookAndFeel::Perspective J37LookAndFeel::perspectiveFor (juce::Rectangle<float> bounds,
                                                            const juce::Component& control) const noexcept
{
    Perspective view;
    view.centre = bounds.getCentre();

    if (cameraPanel.isEmpty() || bounds.isEmpty()
        || cameraRoot == nullptr || control.getTopLevelComponent() != cameraRoot)
        return view;

    // This is the one thing the first camera got wrong, and it is why the whole
    // update read as no update at all. A control's bounds say where its own
    // corner is, never where it stands on the panel: measured from the top left
    // of a 30 px switch, EVERY control sits at the same far corner of the room,
    // so every dx and dy clamps to the same value and the panel comes out as
    // flat as it went in. The control's own centre is added to its place in the
    // component tree, and the panel's centre is in that same space, so the
    // subtraction below means what it says.
    const auto controlCentre = view.centre + offsetToTopLevel (control);
    const auto panelCentre = cameraPanel.getCentre();
    const auto halfWidth  = juce::jmax (1.0f, cameraPanel.getWidth()  * 0.5f);
    const auto halfHeight = juce::jmax (1.0f, cameraPanel.getHeight() * 0.5f);

    const auto dx = juce::jlimit (-1.0f, 1.0f, (controlCentre.x - panelCentre.x) / halfWidth);
    const auto dy = juce::jlimit (-1.0f, 1.0f, (controlCentre.y - panelCentre.y) / halfHeight);

    view.tilt = juce::jlimit (0.0f, 1.0f,
                              std::sqrt (dx * dx + dy * dy) * 0.70710678f);

    // 0.88 rather than 1.0: the whole panel is seen from in front, so no top
    // face anywhere on it is ever a true circle. The near half of the panel
    // falls away much faster than the far half - a control by the near edge is
    // seen nearly edge-on - and the floors are what stop the largest window's
    // front corners from collapsing into slits. The coefficients are the
    // difference between a camera and a decoration: at 0.24 the spread between a
    // knob's top row and its bottom row was under a tenth of the knob, which the
    // eye reads as no depth at all.
    view.squashY = juce::jlimit (0.58f, 1.0f, 0.88f - 0.30f * juce::jmax (0.0f, dy));
    view.squashX = juce::jlimit (0.74f, 1.0f, 0.90f - 0.12f * std::abs (dx)
                                            - 0.06f * juce::jmax (0.0f, dy));

    // The side wall: none at the viewer's own axis, the full height of the part
    // at the near edge. 0.34 rather than more because the viewer is seated, not
    // kneeling on the panel - but 0.34 of a part's height IS visible on a 26 px
    // rocker, which is where the depth has to show.
    view.wall = juce::jlimit (0.0f, 1.0f, 0.08f + 0.34f * juce::jmax (0.0f, dy)
                                              + 0.12f * std::abs (dx));
    view.lean = dx;

    return view;
}

//==============================================================================
//  Toggle switches are drawn as small hardware rockers instead of the default
//  text-button look: a cylindrical metal body shaded by the cosine of its own
//  cross-section angle (see fillCylinderBarrel - the same profile the 3D scene
//  uses), engraved captions, a thumb that physically slides between the two
//  positions, and a status LED that lights when the switch is engaged. This is
//  what the panel's BYPASS / POLARITY / AUTO GAIN controls were missing - they
//  used to look like plain buttons that happened to hold state.
//==============================================================================
void J37LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                       bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    const auto& palette = paletteForTheme (theme);
    const auto view = perspectiveFor (button.getLocalBounds().toFloat(), button);
    // The body is inset four pixels at each side rather than two. The wall, the
    // caption, the groove and the thumb all live inside this rectangle, so the
    // inset is the only place a rocker's overall size is decided - and at two
    // pixels the body ran right up against the cell the layout gave it, which is
    // what made a row of them read as one strip of metal rather than as separate
    // switches.
    const auto bounds = button.getLocalBounds().toFloat().reduced (4.0f, 3.0f);
    const auto isOn = button.getToggleState();

    // The FRONT WALL: the part of the switch's side that the camera's angle
    // exposes below its top face. A switch drawn as one flat capsule lying on
    // the panel has no wall, and a row of those is what made the panel look
    // printed rather than built - and worse, every one of them was showing the
    // SAME absence, which is the eye's cue that they were all drawn at once
    // from straight above.
    const auto wallHeight = juce::jmin (3.0f, bounds.getHeight() * 0.34f * view.wall);

    // The body is a cylinder lying along the switch - fillCylinderBarrel
    // shades it by the cosine of its cross-section angle - not a flat slab, and
    // its specular slides along its own length by however far this switch sits
    // from the camera's axis. Engaging the switch darkens the barrel too, so the
    // state reads through the shading itself and not only through the thumb.
    fillCylinderBarrel (g, bounds,
                        palette.readout.darker (isOn ? 0.22f : 0.02f),
                        palette.readout.darker (0.42f),
                        view.lean * view.tilt);

    // The wall is drawn INSIDE the capsule rather than under it, so a switch is
    // never any taller than the rectangle the layout gave it. The first pass put
    // it BELOW the body, and every rocker in the panel stood two or three pixels
    // taller than the switch it had been - which is exactly what the user
    // reported as "the toggles got even bigger". A nearer rocker still shows
    // more of its side: that side is the bottom of its own body now, and the
    // clip keeps it inside the capsule's curve where the body narrows.
    if (wallHeight > 0.75f)
    {
        juce::Path capsule;
        capsule.addRoundedRectangle (bounds.getX(), bounds.getY(),
                                     bounds.getWidth(), bounds.getHeight(),
                                     bounds.getHeight() * 0.5f);

        g.saveState();
        g.reduceClipRegion (capsule, juce::AffineTransform());
        const auto wall = juce::Rectangle<float> (bounds.getX(), bounds.getBottom() - wallHeight,
                                                  bounds.getWidth(), wallHeight);
        juce::ColourGradient wallShade (palette.readout.darker (0.58f), wall.getX(), wall.getY(),
                                        palette.readout.darker (0.26f), wall.getX(), wall.getBottom(), false);
        wallShade.addColour (0.42f, palette.readout.darker (0.46f));
        g.setGradientFill (wallShade);
        g.fillRect (wall);
        g.setColour (palette.readout.darker (0.66f).withAlpha (0.75f));
        g.drawLine (wall.getX() + wall.getWidth() * 0.18f, wall.getBottom() - 0.5f,
                    wall.getRight() - wall.getWidth() * 0.18f, wall.getBottom() - 0.5f, 0.8f);
        g.restoreState();
    }

    g.setColour (palette.border.withAlpha (0.9f));
    g.drawRoundedRectangle (bounds, bounds.getHeight() * 0.5f, 1.0f);

    // Two bands sized from the switch itself, so nothing ever fights for width:
    //   top band    - the function name, engraved across the full switch width
    //   bottom band - a short recessed track with a sliding thumb that carries the
    //                 state text; the state is always readable and the label never
    //                 truncates. The band height is a proportion of the control
    //                 (was a hardcoded 11 px, which on a 32 px switch left a crowded
    //                 15 px track carrying THREE overlapping text pieces: OFF, ON and
    //                 the thumb caption).
    //
    //  The proportion itself was the remaining crookedness at the header's 22 px
    //  switch: 0.44 asked a 9.7 px caption to sit over a 3 px groove whose thumb
    //  (band minus 2 px, floored at 8 px) stood 10 px tall in a 16 px capsule and
    //  shot past the rounded ends. Height that three stacked pieces must share is
    //  given out as three shares - caption 45 % of the body, the groove whatever
    //  is left under a rim of 14 % of the body - so the bands always SUM to the
    //  control: the header's 22 px switch reads a 10 px caption over a 6 px
    //  groove, the deck's 30 px switch a 12 px caption over an 11 px groove, and
    //  no band is ever asked to draw taller than the body holds.
    const auto labelHeight = juce::jlimit (9.0f, 12.0f, bounds.getHeight() * 0.45f);
    const auto labelBand = bounds.withHeight (labelHeight);
    // The groove is inset deeper than before because the body is now a full
    // capsule: at its rounded bottom the silhouette pulls in fast, and a
    // groove that ran to the old 4 px inset would paint past the arc. Its own
    // inset is deeper than the wall's, so the front wall drawn along the bottom
    // of the body can never cover it.
    // The groove is inset by the body's own end radius, so the groove, the thumb
    // that travels its full length and the state word on that thumb all stay on
    // the body's STRAIGHT section. At a flat 7 px the groove began inside the
    // capsule's rounded end, which is the half circle a rocker is drawn as at
    // each side - and the thumb could be seen riding onto it.
    const auto endInset = juce::jmax (7.0f, bounds.getHeight() * 0.5f + 1.0f);
    const auto track = bounds.withTrimmedTop (labelHeight)
                            .withTrimmedLeft (endInset).withTrimmedRight (endInset)
                            .withTrimmedBottom (juce::jmax (2.0f, bounds.getHeight() * 0.14f));

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
    // The thumb GLIDES between its stops: noteToggle() recorded this switch's
    // target and updateToggleAnimations() eases the position every timer
    // frame, so a click sends the thumb sliding instead of teleporting - the
    // one mechanical motion a real rocker has, and the animation the page was
    // missing. A switch the L&F was never told about draws at its true state.
    noteToggle (&button, isOn);
    const auto glide = toggleGlide (&button, isOn);
    const auto thumbWidth = juce::jmax (9.0f, (track.getWidth() - 2.0f) * 0.5f);
    const auto thumbHeight = juce::jmax (5.0f, track.getHeight() - 2.0f);
    const auto travel = track.getWidth() - thumbWidth - 2.0f;
    const auto thumbX = track.getX() + 1.0f + glide * travel;
    const auto thumbY = track.getY() + 1.0f + (shouldDrawButtonAsDown ? 1.0f : 0.0f);
    const auto thumb = juce::Rectangle<float> (thumbX, thumbY, thumbWidth, thumbHeight);
    // The thumb is a small cylinder of its own riding the groove, pressed one
    // pixel into the body while the mouse is down (thumbY above already
    // shifts it): shading, not colour, carries the mechanics. The floors used
    // to be 12/8 px - taller than the header switch's whole groove, which is
    // how the thumb escaped the track there - and now scale down with it.
    fillCylinderBarrel (g, thumb, palette.knobFace, palette.knobEdge);
    g.setColour (palette.knobEdge.withAlpha (0.85f));
    g.drawRoundedRectangle (thumb, thumb.getHeight() * 0.5f, 1.0f);

    // State text on the thumb, fitted to the thumb. The thumb is the authoritative
    // state readout, so its text must never be the thing that gets clipped.
    g.setColour (isOn ? palette.gaugeInk.withAlpha (0.95f) : palette.secondary);
    g.setFont (shrinkingFont (isOn ? "ON" : "OFF", 7.0f, juce::Font::bold, thumb.getWidth()));
    g.drawText (isOn ? "ON" : "OFF", thumb, juce::Justification::centred, true);

    // Hover ring for mouse/keyboard focus feedback.
    if (shouldDrawButtonAsHighlighted || button.hasKeyboardFocus (false))
    {
        g.setColour (palette.accent.withAlpha (0.5f));
        g.drawRoundedRectangle (bounds.expanded (1.5f), 5.0f, 1.0f);
    }
}

//==============================================================================
//  Where a list's value text starts inside its own field.
//
//  It used to start one pixel from the field's left wall - the value sat against
//  the border, which is the "text touching the wall" these lists were reported
//  for, and one pixel is also what LookAndFeel_V2 (and so V4) hard-codes, which
//  is where the number came from. Two shares decide it now, added together:
//
//   * a share of the FIELD's own height, so the padding keeps its proportion on
//     a twenty-eight-pixel member-row box and on a thirty-pixel deck field alike,
//     instead of being one number that is right for neither;
//   * a share of the PANEL's height, which is the part that ties it to the
//     window: a bigger panel gives its lists the same extra air it gives
//     everything else on it, so the inset grows with the window the way the
//     knobs' own geometry does.
//
//  The three-pixel floor is the one number that is a REQUIREMENT rather than a
//  proportion - below it a digit's stem is back against the wall whatever the
//  shares say - and it is what keeps a small field readable at the panel's
//  minimum size.
//
//  Both list styles place their text through this one function (see the two
//  positionComboBoxText overrides below), because a panel that pads one kind of
//  list and walls the other reads as two panels. The deck's fields are a fixed
//  30 px tall at every window size, so on this panel the two shares land at seven
//  pixels at the smallest window and nine at the largest - measured, not asserted:
//  the layout harness at tests/layout prints that pair at every panel size.
//==============================================================================
namespace
{
    constexpr int comboTextMinimumInset = 3;

    int comboTextInset (int fieldHeight, int panelHeight) noexcept
    {
        const auto fieldShare = juce::roundToInt (static_cast<float> (fieldHeight) * 0.12f);
        const auto panelShare = juce::roundToInt (static_cast<float> (panelHeight) * 0.004f);

        return juce::jmax (comboTextMinimumInset, fieldShare + panelShare);
    }
}

//==============================================================================
//  The deck's combo boxes (MODEL, SPEED, the workflow lists) are painted by
//  LookAndFeel_V4::drawComboBox, which reserves a 30 px arrow zone on the right -
//  but V4's positionComboBoxText was never overridden, so the value Label sat in
//  geometry inherited from wherever the last Look and Feel left it. Any list whose
//  Label geometry drifted ended up printing its text twice, a few pixels apart -
//  text on text, unreadable. Pinning the Label here gives every deck list one
//  authoritative placement: inside the arrow zone, at the combo font V4 chooses.
//==============================================================================
void J37LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    // The panel's own height comes in through the camera (setPanelCamera, which
    // the editor sets from paint()); until it has been set the two shares add up
    // from zero, which is why they are added rather than multiplied - an unset
    // panel costs this list a few pixels of air, not its whole inset.
    const auto panelHeight = juce::roundToInt (cameraPanel.getHeight());
    const auto inset = comboTextInset (box.getHeight(), panelHeight);

    label.setBounds (inset, 1, juce::jmax (0, box.getWidth() - 30), box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

//==============================================================================
//  The combo value Label: juce::ComboBox paints its text with an internal
//  juce::Label through THIS virtual, so the Label's own paint must not add
//  anything the field does not want - an opaque fill would sit on top of the
//  drawn background (a flat white card over the deck's colours, exactly the
//  band the workflow lists arrived with), and a proportional face would make
//  the same entry change width between the box and the open menu.
//==============================================================================
void J37LookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    const auto& palette = paletteForTheme (theme);
    const auto ink = label.findColour (juce::Label::textColourId);

    // A knob's value is the one piece of type on this panel that is read WHILE it
    // changes, so it is the one piece allowed its own face: tabular figures - the
    // panel's numeric face, already used by the meters' ladders and the compressor
    // reduction readout - and a size taken from the band it sits in rather than
    // the toolkit's fixed fifteen pixels. Proportional digits shift the number
    // sideways by a pixel or two every time the value crosses a digit boundary
    // ("1" is narrower than "8"), which on a knob being dragged is the readout
    // equivalent of a wobbling needle; and a fixed 15 px in a 16 px band leaves
    // the fit to luck. Only a Label that IS a Slider's value box gets this:
    // captions, combo fields and the deck's own readouts are untouched.
    const auto isSliderValue = dynamic_cast<juce::Slider*> (label.getParentComponent()) != nullptr;
    g.setFont (isSliderValue
                   ? juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                                    juce::jlimit (11.0f, 13.5f,
                                                                  static_cast<float> (label.getHeight()) * 0.85f),
                                                    juce::Font::plain))
                   : label.getFont());

    auto area = label.getLocalBounds().toFloat();
    const auto maxLines = juce::jmax (1,
                                       static_cast<int> (area.getHeight() / g.getCurrentFont().getHeight()));

    // ENGRAVED, not printed. A caption milled into a plate has its upper-left
    // wall in shadow and its lower-right wall catching the lamp, so the text
    // carries a lit copy of itself one pixel down and right and the ink sits on
    // top of it. Printed text is a flat silhouette on a flat ground and is one
    // of the reasons a drawn panel still reads as a picture of a panel: nothing
    // on a real front plate is printed flat except the silkscreen on the parts
    // nobody looks at. The lit copy is a fraction of the palette's own face
    // rather than white, so it stays metal on all three themes.
    if (area.getHeight() >= 8.0f && area.getWidth() >= 8.0f)
    {
        g.setColour (palette.panel.brighter (0.55f).withAlpha (ink.getAlpha() * 0.45f));
        g.drawFittedText (label.getText(), (area.translated (0.9f, 0.9f)).toNearestInt(),
                          label.getJustificationType(), maxLines,
                          label.getMinimumHorizontalScale());
    }

    g.setColour (ink);
    if (! label.isEnabled())
        g.setOpacity (0.45f);

    g.drawFittedText (label.getText(), area.toNearestInt(),
                      label.getJustificationType(), maxLines,
                      label.getMinimumHorizontalScale());
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
    const auto& palette = paletteForTheme (theme);

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
//  The IN-TAB look and feel.
//
//  See the class comment in PluginEditor.h for WHY this exists as a second
//  design rather than a smaller copy of the deck's. This is the drawing.
//==============================================================================
void J37InlineLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height,
                                         bool isButtonDown,
                                         int, int, int, int,
                                         juce::ComboBox& box)
{
    const auto& palette = paletteForTheme (theme);
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f,
                                                 static_cast<float> (width),
                                                 static_cast<float> (height)).reduced (0.5f);

    // A flat, recessed field rather than the deck's heavy bezel. The only depth
    // is a single-pixel inset at the top edge, which is what makes it read as
    // "a field you type or choose in" rather than as "a button".
    g.setColour (isButtonDown ? palette.readout.brighter (0.06f) : palette.readout);
    g.fillRoundedRectangle (bounds, 3.0f);

    g.setColour (palette.border.withAlpha (0.85f));
    g.drawRoundedRectangle (bounds, 3.0f, 1.0f);

    // The inner top shadow: one hairline, which is the whole of the recess.
    g.setColour (palette.panel.darker (0.35f).withAlpha (0.5f));
    g.drawLine (bounds.getX() + 3.0f, bounds.getY() + 1.0f,
                bounds.getRight() - 3.0f, bounds.getY() + 1.0f, 0.8f);

    // The value is drawn LEFT-aligned, like a settings row, rather than centred
    // like a button caption - that is the single biggest thing that makes this
    // read as a list rather than as a switch. The text itself is NOT drawn here:
    // juce::ComboBox paints its own internal Label over this call, so printing
    // the value here as well drew every list twice, a few pixels apart - text on
    // text, which is the self-overlap the lists arrived with. positionComboBoxText
    // below places that Label; this override only paints the field around it.

    // A slim caret, drawn as two short strokes rather than a filled triangle:
    // at this size a filled triangle reads as a decoration, a chevron reads as
    // "there is a list here".
    const auto caretCentre = juce::Point<float> (bounds.getRight() - 9.0f, bounds.getCentreY());
    g.setColour (palette.accent);
    g.drawLine (caretCentre.x - 3.5f, caretCentre.y - 1.8f,
                caretCentre.x, caretCentre.y + 1.8f, 1.4f);
    g.drawLine (caretCentre.x, caretCentre.y + 1.8f,
                caretCentre.x + 3.5f, caretCentre.y - 1.8f, 1.4f);

    if (box.hasKeyboardFocus (false))
    {
        g.setColour (palette.accent.withAlpha (0.75f));
        g.drawRoundedRectangle (bounds.expanded (0.5f), 3.5f, 1.0f);
    }
}

juce::Font J37InlineLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    // Sized from the box's own height so the same style works in a tab cell and
    // on a larger panel without a second constant to keep in step.
    const auto height = juce::jlimit (9.0f, 14.0f, static_cast<float> (box.getHeight()) * 0.50f);
    return juce::Font (juce::FontOptions (height));
}

void J37InlineLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    // The value Label, placed for the in-tab style: left-aligned like a settings
    // row, and trimmed on the right so a long entry ellipsises before it reaches
    // the caret. Print-the-value lives ONLY here (see drawComboBox above): the base
    // class never runs for these lists, so without this override the Label kept
    // V4's geometry - centred, 30 px reserved for an arrow zone the flat field
    // does not draw.
    //
    // The inset is the SAME solved one the deck's lists use (comboTextInset): the
    // in-tab style used to carry its own hard-coded six pixels, which meant one
    // list could be padded for a small window and the list beside it for a large
    // one - two rules for one panel's padding, and neither of them moved when the
    // window did.
    const auto inset = comboTextInset (box.getHeight(), panelHeight);
    label.setBounds (inset, 1, juce::jmax (0, box.getWidth() - 26), box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
}

void J37InlineLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    // Same argument as the deck style's drawLabel: the internal Label paints
    // itself over the field, so its own paint must be ink only - no opaque
    // background card, and the same face the field's own drawing used.
    g.setColour (label.findColour (juce::Label::textColourId));
    g.setFont (label.getFont());

    auto area = label.getLocalBounds().toFloat();
    if (! label.isEnabled())
        g.setOpacity (0.45f);

    g.drawFittedText (label.getText(), area.toNearestInt(),
                      label.getJustificationType(),
                      juce::jmax (1, static_cast<int> (area.getHeight() / g.getCurrentFont().getHeight())),
                      label.getMinimumHorizontalScale());
}

juce::Font J37InlineLookAndFeel::getPopupMenuFont()
{
    // The popup is the one place the in-tab style cannot size itself from a
    // control, so it uses a fixed compact size - the same 12 px the shrinking
    // font settles on for the tab captions, so the menu matches the panel.
    return juce::Font (juce::FontOptions (12.0f));
}

void J37InlineLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                             bool shouldDrawButtonAsHighlighted,
                                             bool shouldDrawButtonAsDown)
{
    const auto& palette = paletteForTheme (theme);
    const auto bounds = button.getLocalBounds().toFloat().reduced (2.0f, 3.0f);
    const auto isOn = button.getToggleState();

    // The in-tab switch is a small PILL with a sliding dot and an ON/OFF word -
    // the shape every settings panel uses, and deliberately NOT the deck's
    // rocker. The pill is a share of the control's own height so it scales with
    // the cell, and it never grows taller than a comfortable touch target.
    // Smaller than it was (12..18 px tall at 72 % of the cell). A pill's end is a
    // HALF CIRCLE and the dot at the end of it is a circle of its own: at the old
    // proportions the dot was four pixels smaller than the pill was tall and rode
    // the end cap, so the two curves crossed on every one of these switches. The
    // pill is 11..16 px now and the dot below is sized against the STRAIGHT
    // section, so it never reaches the cap.
    const auto pillHeight = juce::jlimit (11.0f, 16.0f, bounds.getHeight() * 0.62f);
    const auto pillWidth = juce::jlimit (pillHeight * 1.8f, bounds.getWidth() * 0.44f,
                                          pillHeight * 2.4f);
    const auto pill = juce::Rectangle<float> (bounds.getRight() - pillWidth,
                                              bounds.getCentreY() - pillHeight * 0.5f,
                                              pillWidth, pillHeight);

    // The track is a cylinder too (same shading helper), with the accent as
    // its face when on: the two-colour fill keeps the state readable at a
    // glance, and the curvature is what makes it read as hardware rather
    // than as a settings-app pill.
    fillCylinderBarrel (g, pill,
                        isOn ? palette.accent.withAlpha (0.85f)
                             : palette.readout.darker (0.35f),
                        isOn ? palette.accent.darker (0.35f)
                             : palette.readout.darker (0.55f));
    g.setColour (palette.border.withAlpha (0.85f));
    g.drawRoundedRectangle (pill, pillHeight * 0.5f, 1.0f);

    // The dot. It rides the track's two ends; when on it is the readout colour
    // on the accent, and when off it is the panel colour on the recess. It sits
    // INSIDE the end radius (half the pill's height) plus a pixel, which is what
    // keeps a round dot off the pill's own rounded end - the two were crossing
    // at both ends before.
    const auto dotDiameter = juce::jmax (5.0f, pillHeight - 7.0f);
    const auto dotEndInset = pillHeight * 0.5f + 1.0f;
    const auto dotX = isOn ? pill.getRight() - dotEndInset - dotDiameter
                           : pill.getX() + dotEndInset;
    const auto dotY = pill.getCentreY() - dotDiameter * 0.5f + (shouldDrawButtonAsDown ? 0.7f : 0.0f);
    g.setColour (isOn ? palette.readout : palette.knobFace);
    g.fillEllipse (dotX, dotY, dotDiameter, dotDiameter);
    g.setColour (palette.knobEdge.withAlpha (0.7f));
    g.drawEllipse (dotX, dotY, dotDiameter, dotDiameter, 0.8f);

    // The caption sits LEFT of the pill and is fitted to the space that is
    // actually left, so a long name shrinks rather than running under the pill.
    const auto captionArea = bounds.withRight (pill.getX() - 4.0f);
    if (captionArea.getWidth() > 4.0f)
    {
        const auto caption = button.getButtonText().toUpperCase();
        g.setColour (palette.secondary);
        g.setFont (shrinkingFont (caption,
                                   juce::jlimit (8.0f, 11.0f, bounds.getHeight() * 0.42f),
                                   juce::Font::bold, captionArea.getWidth()));
        g.drawText (caption, captionArea, juce::Justification::centredLeft, true);
    }

    // The state word, on the track itself, so the control is unambiguous even
    // for a user who cannot tell the two track colours apart.
    g.setColour (isOn ? palette.readout : palette.secondary);
    g.setFont (juce::Font (juce::FontOptions (juce::jmax (6.5f, pillHeight * 0.42f),
                                               juce::Font::bold)));
    // The state word takes what is left once the dot's end (its inset, its own
    // diameter and the two pixels of air around it) is removed from the side the
    // dot is on.
    const auto dotFootprint = dotEndInset + dotDiameter + 2.0f;
    const auto wordArea = pill.withTrimmedLeft (isOn ? dotEndInset : dotFootprint)
                              .withTrimmedRight (isOn ? dotFootprint : dotEndInset);
    g.drawText (isOn ? "ON" : "OFF", wordArea, juce::Justification::centred, false);

    if (shouldDrawButtonAsHighlighted || button.hasKeyboardFocus (false))
    {
        g.setColour (palette.accent.withAlpha (0.45f));
        g.drawRoundedRectangle (bounds.expanded (1.0f), 4.0f, 1.0f);
    }
}
