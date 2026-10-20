// =============================================================================
//  The panel's paint kit: four palettes, the drawing helpers that shape the
//  vintage furniture, and the two formatters the meters read.
//
//  Moved out of PluginEditor.cpp whole, prose and all, by matching its exact text
//  - so a typo would have failed the move rather than changed the panel. Several
//  things had to travel with it or change shape, and each is a fact about headers
//  rather than about painting:
//
//    * Every definition below is `inline`. In the .cpp these names were in an
//      anonymous namespace - one copy per translation unit, internal linkage -
//      and in a header that would leave `liveThemeChoice` a different object in
//      every one of them: the theme the look-and-feel sets would not be the one
//      the panel reads.
//    * `ThemeChoice` came out of J37LookAndFeel to sit beside the palettes that
//      select on it. A palette cannot be nested inside the class that paints with
//      it, and every caller was already reaching through the class to name it.
//    * `principleCount` and the melatonin_blur probe came with the kit: the seven
//      principles are the colours here before they are anything else, and the
//      paint routines are what use that module.
//
//  What the paint routines are HANDED - the panel's page table - is
//  Source/GUI/PanelTabs.h, a header of its own: nothing in either needs the other,
//  which is what made them two files instead of one.
// =============================================================================
//
//  The kit does not include JUCE itself: it names juce::Colour, juce::Graphics,
//  juce::Rectangle and juce::Font, which its includer has already declared. The
//  plugin's own JuceHeader.h is the one that does (see PluginEditor.h), which is
//  also what keeps this header free of the plugin's build settings.
// =============================================================================
#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <cstddef>

// melatonin_blur (fetched by CPM in CMakeLists.txt). Fast shadow and gradient
// blurring for JUCE Components. Available to the paint routines below, which
// draw the panel's soft analog shading by hand today; the include is conditional
// so a build without the module on its include path still compiles.
#if __has_include(<melatonin_blur/melatonin_blur.h>)
#include <melatonin_blur/melatonin_blur.h>
#define J37_HAS_MELATONIN_BLUR 1
#else
#define J37_HAS_MELATONIN_BLUR 0
#endif

// The seven saturation principles, in the order the SOURCE knobs, the colours
// and the MIX page's bar all use them: tape, valve, cassette, vinyl, amp,
// transformer, digital - the same order SaturationCore::setWeights takes them
// in. Declared once here, ABOVE everything that needs it, because three
// separate pieces of the panel count the same seven: the LookAndFeel, which
// paints a knob in its machine's colour and has to hold one pointer per
// principle; the editor's MixBar, whose strip is a fixed seven cells; and the
// seven SOURCE registrations themselves. One number, so none of the three can
// drift from the others.
inline constexpr int principleCount = 7;

namespace j37panel {
/** The four front-panel choices, moved here from J37LookAndFeel. */
enum class ThemeChoice { ivory = 0, charcoal = 1, metal = 2, darkMetal = 3 };

// Symmetric decibel range shared by the input and output trims, matching the
// processor's parameter layout exactly.
inline constexpr double minStageDb = -32.0;
inline constexpr double maxStageDb = 32.0;

struct UiPalette {
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

inline const UiPalette ivoryPalette{
    juce::Colour::fromRGB(197, 184, 157), // background
    juce::Colour::fromRGB(222, 210, 187), // panel
    juce::Colour::fromRGB(231, 220, 197), // raised
    juce::Colour::fromRGB(111, 97, 73),   // border
    juce::Colour::fromRGB(145, 96, 42),   // accent
    juce::Colour::fromRGB(45, 38, 29),    // text
    juce::Colour::fromRGB(102, 91, 72),   // secondary
    juce::Colour::fromRGB(179, 162, 130), // knobFace
    juce::Colour::fromRGB(217, 203, 176), // knobHighlight
    juce::Colour::fromRGB(93, 78, 55),    // knobEdge
    juce::Colour::fromRGB(245, 238, 222), // readout
    juce::Colour::fromRGB(239, 229, 203), // gaugeFace
    juce::Colour::fromRGB(49, 43, 33),    // gaugeInk
    juce::Colour::fromRGB(132, 47, 37),   // needle
    juce::Colour::fromRGB(62, 111, 72)    // status
};

inline const UiPalette charcoalPalette{
    juce::Colour::fromRGB(28, 25, 21),    // background
    juce::Colour::fromRGB(54, 46, 35),    // panel
    juce::Colour::fromRGB(84, 70, 50),    // raised
    juce::Colour::fromRGB(144, 117, 72),  // border
    juce::Colour::fromRGB(211, 166, 83),  // accent
    juce::Colour::fromRGB(244, 231, 204), // text
    juce::Colour::fromRGB(190, 173, 140), // secondary
    juce::Colour::fromRGB(89, 72, 51),    // knobFace
    juce::Colour::fromRGB(126, 105, 73),  // knobHighlight
    juce::Colour::fromRGB(209, 172, 103), // knobEdge
    juce::Colour::fromRGB(35, 30, 24),    // readout
    juce::Colour::fromRGB(226, 210, 173), // gaugeFace
    juce::Colour::fromRGB(48, 39, 28),    // gaugeInk
    juce::Colour::fromRGB(132, 47, 37),   // needle
    juce::Colour::fromRGB(122, 174, 128)  // status
};

// ------------------------------------------------------------------
//  The METAL theme: brushed steel rather than wood or paper. It remains
//  separate from the added DARK METAL palette below.
//
//  The palette is built around that: the chassis is a NEUTRAL grey rather
//  than a warm one, the accent is a cool cyan rather than gold or amber,
//  and the "metal" itself comes from the contrast between the raised
//  surfaces and the recesses rather than from a warm colour. Every colour
//  is deliberately desaturated - a metal panel gets its depth from its
//  lighting, not from its pigment - which is also what keeps the accent
//  reading as an indicator lamp rather than as a decoration.
//
//  Its dark palette feeds the same dark/light drawing paths as charcoal;
//  the exact colours are still selected by ThemeChoice.
// ------------------------------------------------------------------
//  The METAL palette evokes a rack unit: a graphite chassis, machined
//  aluminium lettering plates, a signal-green indicator lamp, and a pale
//  gauge face set into the dark panel.
inline const UiPalette metalPalette{
    juce::Colour::fromRGB(16, 17, 19),    // background - graphite chassis
    juce::Colour::fromRGB(37, 39, 43),    // panel - brushed dark steel
    juce::Colour::fromRGB(58, 62, 68),    // raised - machined plate
    juce::Colour::fromRGB(105, 112, 121), // border - bright machined edge
    juce::Colour::fromRGB(64, 224, 148),  // accent - signal green, the panel's one saturated voice
    juce::Colour::fromRGB(232, 236, 240), // text - etched white
    juce::Colour::fromRGB(148, 156, 164), // secondary - engraved grey
    juce::Colour::fromRGB(46, 49, 54),    // knobFace
    juce::Colour::fromRGB(96, 102, 110),  // knobHighlight - the knurl's sheen
    juce::Colour::fromRGB(196, 202, 210), // knobEdge - bright machined ring
    juce::Colour::fromRGB(12, 13, 15),    // readout - dead black window
    juce::Colour::fromRGB(214, 220, 226), // gaugeFace - the pale ballistic window
    juce::Colour::fromRGB(24, 26, 29),    // gaugeInk
    juce::Colour::fromRGB(255, 120, 60),  // needle - hot orange, distinct from the green
    juce::Colour::fromRGB(110, 220, 160)  // status - the same green family, dimmer
};

// A new dark theme, distinct from both the warm charcoal console and the
// cool graphite Metal rack panel. This uses deep indigo and restrained
// electric violet accents, while preserving high-contrast readable text.
inline const UiPalette darkMetalPalette{
    juce::Colour::fromRGB(17, 16, 25),    // background - midnight blue-black
    juce::Colour::fromRGB(34, 31, 48),    // panel - dark indigo
    juce::Colour::fromRGB(55, 49, 75),    // raised - violet slate
    juce::Colour::fromRGB(113, 97, 157),  // border
    juce::Colour::fromRGB(177, 133, 255), // accent - electric violet
    juce::Colour::fromRGB(242, 237, 255), // text
    juce::Colour::fromRGB(184, 174, 211), // secondary
    juce::Colour::fromRGB(49, 43, 69),    // knobFace
    juce::Colour::fromRGB(105, 91, 143),  // knobHighlight
    juce::Colour::fromRGB(199, 180, 244), // knobEdge
    juce::Colour::fromRGB(13, 12, 20),    // readout
    juce::Colour::fromRGB(216, 208, 239), // gaugeFace
    juce::Colour::fromRGB(29, 25, 40),    // gaugeInk
    juce::Colour::fromRGB(255, 125, 174), // needle - rose
    juce::Colour::fromRGB(163, 224, 191)  // status - mint
};

// The live choice, kept in step with the editor's by applyTheme(). It lives
// in the anonymous namespace beside the palettes so both lookups share it.
inline j37panel::ThemeChoice liveThemeChoice = j37panel::ThemeChoice::ivory;

//  The four-theme lookup. `paletteFor(bool)` below is kept for existing
//  paint calls; this is the one place each theme selects its palette.
// The parameter is j37panel::ThemeChoice itself: a second, local enum
// with the same enumerators compiled on its own and then could not accept
// the look-and-feel's member (two distinct types), which is what broke
// drawToggleButton. One enum, declared once in the header.
//
// This one is DEFINED FIRST, and the order is load-bearing: both of these
// are free functions in the anonymous namespace, so each may only use what
// is declared above it. Written the other way round, paletteFor could not
// see the lookup or the live choice, and the failure reads as if the
// functions did not exist at all.
inline const UiPalette &paletteForTheme(j37panel::ThemeChoice theme) {
    switch (theme) {
    case j37panel::ThemeChoice::darkMetal:
        return darkMetalPalette;
    case j37panel::ThemeChoice::metal:
        return metalPalette;
    case j37panel::ThemeChoice::charcoal:
        return charcoalPalette;
    case j37panel::ThemeChoice::ivory:
    default:
        return ivoryPalette;
    }
}

// The bool overload remains the funnel for legacy draw helpers; it consults
// the full live choice so all four themes resolve to their own palette.
//
// No `const` on this one: it is a free function, not a member, and a cv-
// qualifier on a non-member function is a hard error ("cannot have
// cv-qualifier") rather than something to be ignored.
inline const UiPalette &paletteFor(bool darkTheme) noexcept {
    juce::ignoreUnused(darkTheme);
    return paletteForTheme(liveThemeChoice);
}

// -----------------------------------------------------------------------
//  The seven principles, each with its own colour.
//
//  The SOURCE knobs replaced one sweep and one spread with seven NAMED
//  machines, and a row of seven identical knobs reads as seven copies of
//  one control. A hue per principle fixes that for the price of a table: the
//  arc, the tracer and the MIX page's bar all speak it, so the colour IS
//  the name - the user learns "coral is the valve" once, and the seven
//  knobs stop being seven strangers.
//
//  The hues are spread around the wheel but held to one family: warm brass
//  and coral for the two machines that are mostly about heat, cool teal and
//  violet for the two that are about a surface and a groove, the rest as the
//  mid-tones between them. Each is desaturated enough to sit on charcoal or
//  metal without glowing and dark enough to read on ivory, so the same
//  seven colours work on all three themes - the dark ones get a small lift
//  and the light one a small sink, because one pigment needs opposite
//  treatment on opposite grounds.
// -----------------------------------------------------------------------
inline juce::Colour principleColour(int principle, bool darkTheme) noexcept {
    static constexpr juce::uint32 hues[principleCount]{
        0xffe0a53c, // TAPE        - brass
        0xffe0603f, // VALVE       - hot coral
        0xff3fae9e, // CASSETTE    - teal
        0xff8a74e0, // VINYL       - violet
        0xffd9527f, // AMP         - rose
        0xffa8b84a, // TRANSFORMER - olive
        0xff3f9fe0  // DIGITAL     - blue
    };

    const auto base = juce::Colour(hues[juce::jlimit(0, principleCount - 1, principle)]);
    return darkTheme ? base.brighter(0.16f) : base.darker(0.08f);
}

// The parameter ids the seven SOURCE knobs bind to, in principleColour's
// order. The MIX page's bar reads them in this order too, so a re-order
// here moves a colour with its machine rather than orphaning one.
inline constexpr std::array<const char *, principleCount> principleParameterIds{
    "tape_source", "valve_source",       "cassette_source", "vinyl_source",
    "amp_source",  "transformer_source", "digital_source"};

/** The principle index of a parameter id, or -1 if it is not one of the
    seven SOURCE knobs. Used once, where the sliders are built, to stamp the
    index onto each of them as a component property. */
inline int principleOf(const juce::String &id) noexcept {
    for (int p = 0; p < principleCount; ++p)
        if (id == principleParameterIds[static_cast<std::size_t>(p)])
            return p;

    return -1;
}

// Shading for the cylindrical switch bodies (drawToggleButton, both look
// and feels). The switch is a cylinder lying along its own length, lit
// from above: a point on the projected face has its surface normal tilted
// by the cross-section angle a, so the light it catches is cos(a) - the
// brightness peaks just ABOVE the centreline (where the curvature faces
// the lamp), falls to the rim on top, and dies into shadow underneath.
// That profile is painted twice: once as the barrel's gradient, once as a
// thin specular line riding the same falloff - which is what turns a
// filled shape into something that reads as machined metal.
inline void fillCylinderBarrel(juce::Graphics &g,
                               const juce::Rectangle<float> &barrel,
                               juce::Colour face,
                               juce::Colour edge,
                               float lean = 0.0f) {
    if (barrel.getWidth() < 2.0f || barrel.getHeight() < 2.0f)
        return;

    // The barrel: cosine shading down the height, one-sided because the
    // light sits above the machine. The silhouette is the full capsule
    // (radius = half the height), which IS the cylinder's projection.
    juce::ColourGradient shading(edge, barrel.getCentreX(), barrel.getY(), edge.darker(0.35f),
                                 barrel.getCentreX(), barrel.getBottom(), false);
    shading.addColour(0.30, face.brighter(0.30f));
    shading.addColour(0.62, face);
    g.setGradientFill(shading);
    g.fillRoundedRectangle(barrel, barrel.getHeight() * 0.5f);

    // The specular: a thin bright line along the barrel's length, riding
    // above the centreline. It is the single strongest cue that the
    // surface is curved rather than flat. `lean` slides it along the
    // barrel's length, which is what the camera's angle asks for: a
    // control the viewer is looking at from its left catches its lamp
    // nearer the middle, and one seen from its right nearer the far end.
    const auto streak =
        juce::Rectangle<float>(barrel.getX() + barrel.getWidth() * (0.06f + 0.10f * lean),
                               barrel.getY() + barrel.getHeight() * 0.12f,
                               barrel.getWidth() * (0.88f - 0.20f * std::abs(lean)),
                               juce::jmin(barrel.getHeight() * 0.16f, 3.5f));
    juce::ColourGradient specular(face.brighter(0.60f).withAlpha(0.65f), streak.getX(),
                                  streak.getY(), face.brighter(0.60f).withAlpha(0.0f),
                                  streak.getX(), streak.getBottom(), false);
    g.setGradientFill(specular);
    g.fillRoundedRectangle(streak, streak.getHeight() * 0.5f);
}

// -----------------------------------------------------------------------
//  A cap lit by the panel's one lamp.
//
//  Radial shading on its own is what makes a knob read as a PRINTED DISC.
//  The falloff says "round", but the TERMINATOR - the band where the surface
//  turns away from the light and stops catching it - is missing, and without
//  that band the eye has no second edge to read and the form goes flat. So
//  this fills the same ellipse in four terms a photograph of a machined part
//  would show, each doing something a radial gradient cannot:
//
//    body       radial, centred ABOVE AND LEFT of the cap's middle, because
//               that is where the lamp is; a gradient centred on the cap
//               itself lights it evenly and reads as paint
//    terminator LINEAR across the cap, falling into shadow down and right.
//               A linear gradient filled into an ellipse is clipped BY the
//               ellipse, so the two models combine with no mask and no
//               second path - the whole trick is one fill call
//    specular   a hard bright core inside a soft one, where the dome faces
//               the lamp square on. The soft blob alone is a smudge; the
//               core inside it is the reflection of the light source
//    bounce     the light the panel throws back up into the cap's underside.
//               This is the term that sells a part as SOLID rather than as
//               a circle: a shadow side that dies to pure black is the
//               single most common reason a rendered knob looks drawn
//
//  `gloss` scales the three lighting terms and nothing else, so a hovered
//  knob gets the same shading brighter rather than a second drawing path.
// -----------------------------------------------------------------------
inline void fillLitDome(juce::Graphics &g,
                        juce::Point<float> centre,
                        float radius,
                        juce::Colour body,
                        juce::Colour highlight,
                        juce::Colour shadow,
                        float gloss = 1.0f) {
    if (radius < 0.75f)
        return;

    juce::ColourGradient dome(highlight, centre.x - radius * 0.32f, centre.y - radius * 0.38f,
                              shadow, centre.x + radius * 0.86f, centre.y + radius * 0.86f, true);
    dome.addColour(0.34f, body.brighter(0.10f));
    dome.addColour(0.70f, body);
    dome.addColour(0.94f, body.darker(0.20f));
    g.setGradientFill(dome);
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);

    // The terminator: transparent across the lit half, then two darkening
    // stops down the shadow half. Two rather than one, because a single
    // ramp reads as a vignette painted on the cap and a two-stage ramp
    // reads as the curve of a surface.
    juce::ColourGradient turn(juce::Colours::transparentBlack, centre.x - radius * 0.85f,
                              centre.y - radius * 0.85f, shadow.withAlpha(0.46f * gloss),
                              centre.x + radius * 0.95f, centre.y + radius * 0.95f, false);
    turn.addColour(0.48f, juce::Colours::transparentBlack);
    turn.addColour(0.74f, shadow.withAlpha(0.14f * gloss));
    g.setGradientFill(turn);
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);

    // The specular core.
    const auto hotRadius = juce::jmax(0.6f, radius * 0.28f);
    const auto hotX = centre.x - radius * 0.38f;
    const auto hotY = centre.y - radius * 0.44f;
    juce::ColourGradient spec(highlight.withAlpha(0.50f * gloss), hotX, hotY,
                              highlight.withAlpha(0.0f), hotX + hotRadius, hotY + hotRadius, true);
    g.setGradientFill(spec);
    g.fillEllipse(hotX - hotRadius, hotY - hotRadius, hotRadius * 2.0f, hotRadius * 2.0f);

    // The bounce, low and to the right - the one term that must NOT sit
    // where the light does, or it stops reading as reflected light and
    // starts reading as a second highlight.
    const auto bounceRadius = juce::jmax(0.6f, radius * 0.54f);
    const auto bounceX = centre.x + radius * 0.34f;
    const auto bounceY = centre.y + radius * 0.44f;
    juce::ColourGradient bounce(highlight.withAlpha(0.20f * gloss), bounceX, bounceY,
                                highlight.withAlpha(0.0f), bounceX - bounceRadius,
                                bounceY - bounceRadius, true);
    g.setGradientFill(bounce);
    g.fillEllipse(bounceX - bounceRadius, bounceY - bounceRadius, bounceRadius * 2.0f,
                  bounceRadius * 2.0f);
}

inline juce::String formatDb(float value) {
    // Round to one decimal BEFORE formatting, so a value like -0.04 (which is just
    // the meter's jitter floor) prints as "0.0 dB" instead of the crooked-looking
    // "-0.0 dB".
    const auto rounded = std::round(value * 10.0f) * 0.1f + 0.0f;
    return juce::String(rounded, 1) + " dB";
}

// Returns the largest font up to `maxHeight` at which `text` still fits inside
// `availableWidth`. Captions in a switch or a fixed-width label are laid out in
// code, not by a layout engine, so a caption that grows (or a host that swaps in
// a wider default font) silently truncates or ellipsises - and a truncated
// caption is what made the rocker switches look broken. Measuring the string
// against the space it actually has keeps the text whole instead.
inline juce::Font shrinkingFont(const juce::String &text,
                                float maxHeight,
                                int fontStyle,
                                float availableWidth) {
    const auto makeFont = [fontStyle](float height) {
        return juce::Font(juce::FontOptions(height, fontStyle));
    };

    if (availableWidth <= 0.0f || text.isEmpty())
        return makeFont(maxHeight);

    auto font = makeFont(maxHeight);

    // A safety margin for side bearing. A flat 1 px was not enough: on the narrow
    // workflow buttons (SAVE / DEL / UNDO / REDO, 38-44 px wide) a caption measured
    // as "just fitting" and then still came out as "SA..." / "D..." / "UN...",
    // because the real rendered string is a little wider than the arrangement
    // reports. The margin is now a share of the available width with a 3 px floor.
    const auto available =
        juce::jmax(1.0f, availableWidth - juce::jmax(3.0f, availableWidth * 0.08f));
    const auto measured = juce::GlyphArrangement::getStringWidth(font, text);

    if (measured > available) {
        // Scale once by the measured ratio rather than stepping down in fixed
        // decrements: one measurement, and the result is the exact size that
        // just fits instead of the next coarser size down.
        const auto ratio = available / measured;
        const auto scaled = maxHeight * juce::jlimit(0.55f, 1.0f, ratio);
        font = makeFont(scaled);
    }

    return font;
}

// A screw head, countersunk into the plate. It used to be a dot with a
// diagonal through it, and a dot with a line through it reads as a HOLE -
// which is the opposite of what a screw is. This is four terms: the dark
// countersink the head sits down in, the head lit from the same up-and-left
// as every other surface here, the rim pair that closes it into a cylinder,
// and a cross whose flutes each carry a lit lip, because a groove with a lit
// lip has depth and a groove painted flat does not.
inline void drawScrew(juce::Graphics &g, float x, float y, const UiPalette &palette) {
    const auto r = 3.2f;
    const auto reach = r * 0.66f;

    g.setColour(juce::Colours::black.withAlpha(0.26f));
    g.fillEllipse(x - r - 1.1f, y - r - 1.1f, (r + 1.1f) * 2.0f, (r + 1.1f) * 2.0f);

    juce::ColourGradient head(palette.panel.brighter(0.32f), x, y - r, palette.panel.darker(0.48f),
                              x, y + r, false);
    head.addColour(0.42f, palette.panel);
    g.setGradientFill(head);
    g.fillEllipse(x - r, y - r, r * 2.0f, r * 2.0f);

    g.setColour(palette.border.withAlpha(0.50f));
    g.drawLine(x - reach, y - r + 0.5f, x + reach, y - r + 0.5f, 0.8f);
    g.setColour(palette.panel.darker(0.55f).withAlpha(0.70f));
    g.drawLine(x - reach, y + r - 0.5f, x + reach, y + r - 0.5f, 0.8f);

    g.setColour(palette.panel.darker(0.62f).withAlpha(0.90f));
    g.drawLine(x - reach, y - reach, x + reach, y + reach, 1.2f);
    g.drawLine(x - reach, y + reach, x + reach, y - reach, 1.2f);
    g.setColour(palette.panel.brighter(0.40f).withAlpha(0.45f));
    g.drawLine(x - reach, y - reach - 0.9f, x + reach, y + reach - 0.9f, 0.7f);
}

inline void drawPanel(juce::Graphics &g,
                      juce::Rectangle<int> area,
                      const UiPalette &palette,
                      float cornerSize) {
    const auto rect = area.toFloat();

    // Skip a panel the clip cannot reach. Without this every repaint rebuilds
    // all five gradients and re-fills all five panels however small the dirty
    // rectangle was, because the geometry and the gradient setup are paid for
    // whether or not the rasteriser then clips the result away. That fixed cost
    // per repaint is what the panel's 30 Hz animation pays, and attaching a
    // context makes every one of those repaints a render of this component.
    if (!rect.intersects(g.getClipBounds().toFloat()))
        return;

    // ------------------------------------------------------------------
    //  Drop shadow. The light source is up-and-left, consistently with the
    //  knob highlights and the screw slots, so the shadow falls down-and-
    //  right.
    //
    //  melatonin_blur draws this as one real Gaussian-ish shadow rather than a
    //  stack of three flat rounded rectangles, which is what makes the panel
    //  read as sitting ABOVE the chassis instead of being outlined on it. The
    //  library caches the blurred mask against its path and radius, so the
    //  cost is one blur the first time and a blit on every frame after - which
    //  is why it is affordable on a panel repainted at 30 Hz. Without the
    //  module the hand-drawn approximation below is unchanged.
    // ------------------------------------------------------------------
#if J37_HAS_MELATONIN_BLUR
    {
        // Same API the knob halo uses: setters, then render against a Path. A
        // rounded rectangle is built as a path so the proven render(g, Path)
        // overload is the one called, and the shadow is cached against it.
        melatonin::DropShadow panelShadow;
        panelShadow.setColor(juce::Colours::black.withAlpha(0.30f));
        panelShadow.setRadius(cornerSize + 2.0f);

        juce::Path panelPath;
        panelPath.addRoundedRectangle(rect.getX(), rect.getY(), rect.getWidth(), rect.getHeight(),
                                      cornerSize);
        panelShadow.render(g, panelPath);
    }
#else
    for (int layer = 3; layer >= 1; --layer) {
        // Shadows fall DOWN only. A previous pass biased the offset upward
        // (spread * 0.6 on x with spread on y was fine, but callers placed
        // captions flush with panel bottoms), so the layer stack printed as a
        // displaced halo AROUND text sitting near the edge - the "shifted
        // shadow" the panel showed under labels. Small, strictly downward,
        // tight alpha: a suggestion of depth, never a second object.
        const auto spread = static_cast<float>(layer) * 1.2f;
        g.setColour(juce::Colours::black.withAlpha(0.035f));
        g.fillRoundedRectangle(rect.translated(0.0f, spread).expanded(spread * 0.4f),
                               cornerSize + spread * 0.4f);
    }
#endif

    // The face: a subtle vertical gradient, brighter at the top because that
    // is where the light lands.
    juce::ColourGradient fill(palette.panel.brighter(0.10f), rect.getX(), rect.getY(),
                              palette.panel.darker(0.10f), rect.getX(), rect.getBottom(), false);
    g.setGradientFill(fill);
    g.fillRoundedRectangle(rect, cornerSize);

    // The corner sheen. The lamp is up and LEFT, so the face is brightest at
    // its top-left corner and falls away to the bottom-right; the vertical
    // gradient above only says "top", so this one term is what stops a panel
    // reading as a flat sheet of colour. It is one translucent ellipse clipped
    // to the panel's own rounded shape, drawn after the fill and before the
    // edges, so the sheen sits UNDER the lip rather than on it. (saveState /
    // restoreState are the pair JUCE 9's removed GraphicsStateSaver wrapped.)
    g.saveState();
    g.reduceClipRegion(rect.toNearestInt());
    g.setColour(palette.panel.brighter(0.18f).withAlpha(0.16f));
    g.fillEllipse(rect.getX() - rect.getWidth() * 0.35f, rect.getY() - rect.getHeight() * 0.85f,
                  rect.getWidth() * 1.7f, rect.getHeight() * 1.5f);
    g.restoreState();

    // The top-edge highlight: one pixel, inset past the corners so it does not
    // fight the rounded edge. This is what makes the panel read as RAISED.
    g.setColour(palette.panel.brighter(0.30f).withAlpha(0.55f));
    g.drawLine(rect.getX() + cornerSize, rect.getY() + 0.5f, rect.getRight() - cornerSize,
               rect.getY() + 0.5f, 1.0f);

    // ...and its partner on the LEFT edge, dimmer and running only the upper
    // two thirds: the same lamp catches both, and a panel lit only from above
    // looks like a printed card rather than a milled plate.
    g.setColour(palette.panel.brighter(0.26f).withAlpha(0.34f));
    g.drawLine(rect.getX() + 0.5f, rect.getY() + cornerSize, rect.getX() + 0.5f,
               rect.getY() + rect.getHeight() * 0.68f, 1.0f);

    // The occlusion along the bottom inside edge: the panel is proud of the
    // chassis, so its own lower lip casts a short shadow onto its own face.
    // Three one-pixel lines at falling alpha, because a single hard line at
    // this weight reads as a border rather than as shade.
    for (int line = 0; line < 3; ++line) {
        g.setColour(juce::Colours::black.withAlpha(0.05f - 0.014f * static_cast<float>(line)));
        g.drawLine(rect.getX() + cornerSize,
                   rect.getBottom() - 1.5f - static_cast<float>(line) * 1.2f,
                   rect.getRight() - cornerSize,
                   rect.getBottom() - 1.5f - static_cast<float>(line) * 1.2f, 1.0f);
    }

    // The outline, and an inner seam. The seam is darker than the face rather
    // than lighter - it is the shadow the panel casts on the chassis, so it
    // belongs on the inside of the border.
    g.setColour(palette.border);
    g.drawRoundedRectangle(rect.reduced(0.5f), cornerSize, 1.0f);
    g.setColour(palette.panel.darker(0.35f).withAlpha(0.7f));
    g.drawRoundedRectangle(rect.reduced(3.0f), juce::jmax(1.0f, cornerSize - 2.0f), 0.7f);
}
} // namespace j37panel
