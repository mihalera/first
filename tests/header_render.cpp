/*  ============================================================================
    The header's offscreen render: what the panel actually puts on the pixels.

    WHY THIS EXISTS. The machine-state badge - the lamp and the words STEREO /
    REAL TIME, or BYPASSED / DRY - has been wrong on screen three times while every
    geometric harness was green: a pill wider than the header, a pill a third of the
    header wide with the caption pushed into it, and a caption on a plate bigger than
    the caption. tests/layout/deck_dust_test.cpp measures the numbers those reports
    were about - the badge is the words plus the lamp's reach - and numbers were never
    the whole story: "the sign is crooked" is a claim about the rendering.

    So this harness renders. JUCE paints a component into a juce::Image through an
    ordinary Graphics with no window and no display, which is why the check can be an
    ordinary test binary: the editor is built the way a host builds it, its whole
    surface is painted into an image at three real window widths, and every claim
    below is made against the pixels that came out. There is no new UI framework here
    and no second implementation of the header to drift from the first.

    THE ONE THING THAT MAKES "NO PLATE" A PIXEL FACT. The header has a gradient, a
    grain layer and a live shimmer, so "compare it with the panel colour" is a
    guess with a tolerance. Instead the harness paints the SAME editor a second time
    in the same frame with the caption collapsed to nothing (Probe::collapseCaption)
    and subtracts: every pixel of the header's second row that changes between the
    two renders is a pixel the badge drew. The claim is that those pixels are the
    lamp's disc and bloom and the caption's own glyphs - and nothing else, at zero
    tolerance, anywhere in the row. A plate behind the words changes thousands of
    pixels that are neither; so does a pill wider than the sign.

    The claims are made per width on the main page - and per PALETTE and per
    TRANSLATION at the panel's minimum width, because the badge is measured from its
    words: a longer translation is a layout case, and a different palette is different
    ink on the same geometry. And on SETTINGS, where the badge used to stand down and
    now keeps the row, with the language selector below it on the page.

      plate  nothing is drawn in the badge's row but the lamp and the glyphs. Every
             pixel that differs between the two renders is inside the glyph ink or
             inside the lamp's disc and bloom - so there is no plate, no pill and no
             shadow, neither around the words nor in the gaps between them.
      fit    the caption's ink is inside its box on every side, so no glyph is
             clipped by the edge the words are drawn against, and the ink is tall
             enough to be glyphs rather than a smear.
      lamp   the lamp's disc is drawn and the gap between it and the first glyph is
             empty: the lamp does not sit on the words.
      row    the lamp starts after LEVELS ON/OFF ends and the badge ends inside the
             header, so the second row is shared rather than overdrawn.
      state  BYPASSED / DRY and STEREO / REAL TIME give the same lamp pixel and boxes
             measured from their own words, and switching back restores the first
             state's box exactly - no leftover of the previous width.

    Every claim has a negative control at the foot of this file: the same analysis is
    run against an image this harness deliberately damages - the plate painted back
    behind the words, a pill wider than the sign, a glyph clipped by the box, the lamp
    moved onto the first letter, the badge widened over LEVELS, the previous state's
    width left behind - and each control must FAIL the claim it targets. A check that
    cannot fail is not a check, and these are the reports the user actually made.

    Reads the panel through the friend declaration in PluginEditor.h
    (`J37HeaderRenderProbe`): the badge's box, the LEVELS switch, the caption's own
    text and the page on screen are facts the panel already uses, and a render test
    that re-derived them could agree with itself while disagreeing with the paint
    routine it is supposed to be checking.
   ============================================================================ */

#include <juce_gui_basics/juce_gui_basics.h>

#include "GUI/PanelTabs.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <array>
#include <cstdio>
#include <vector>

//==============================================================================
/*  The panel's own members, through the friend declaration in PluginEditor.h.

    Global scope on purpose: that is where the friend declaration names it, and the
    helpers below only alias it.
*/
struct J37HeaderRenderProbe
{
    explicit J37HeaderRenderProbe (FirstAudioProcessorEditor& e) : editor (e) {}

    juce::Rectangle<int> badge() const { return editor.statusBadgeBounds(); }
    juce::Rectangle<int> levels() const { return editor.levelsButton.getBounds(); }
    /** Where the SETTINGS page put its LANGUAGE selector, so a claim can say the
        badge's row and the selector do not share a pixel. */
    juce::Rectangle<int> language() const { return editor.languageBox.getBounds(); }

    /**  The MACHINE page's ten type selectors in layout order, each with the caption
        label that names it. The labels are ordinary members of the editor, so this
        reads exactly the components the page paints - including whether they were
        ever ADDED to it, which is not something a placed rectangle can tell you. */
    struct TypeCell
    {
        juce::Label& label;
        juce::ComboBox& box;
    };

    std::array<TypeCell, 10> machineTypeCells() const
    {
        return {{{ editor.tapeTypeLabel, editor.tapeTypeBox },
                 { editor.valveTypeLabel, editor.valveTypeBox },
                 { editor.pentodeTypeLabel, editor.pentodeTypeBox },
                 { editor.triodeTypeLabel, editor.triodeTypeBox },
                 { editor.transistorTypeLabel, editor.transistorTypeBox },
                 { editor.transformerTypeLabel, editor.transformerTypeBox },
                 { editor.digitalTypeLabel, editor.digitalTypeBox },
                 { editor.vinylTypeLabel, editor.vinylTypeBox },
                 { editor.speedLabel, editor.speedBox },
                 { editor.vinylSpeedLabel, editor.vinylSpeedBox }}};
    }
    juce::Rectangle<int> header() const { return editor.getEditorLayout().header; }
    const j37panel::UiPalette& palette() const { return j37panel::paletteFor (editor.darkTheme); }
    bool badgeShown() const { return editor.statusLabel.isVisible(); }
    juce::String caption() const { return editor.statusLabel.getText(); }
    juce::Font captionFont() const { return editor.statusLabel.getFont(); }
    int tab() const { return editor.currentTab; }
    void openPage (int page) { editor.setCurrentTab (page); }

    /**  The caption, through the two calls the panel's timer makes for it. The timer
        itself cannot be used here: it returns immediately unless the editor is on a
        desktop (`isShowing()`), and this harness deliberately has no window. These
        are the same two lines the timer runs - set the words, re-measure the badge -
        so the harness cannot produce a box the panel would not. */
    void showCaption (const juce::String& text)
    {
        editor.statusLabel.setText (text, juce::dontSendNotification);
        editor.placeStatusBadge();
    }

    /**  The caption collapsed to nothing, for the second render: the same frame with
        no badge in it. Its text is left alone, so a later placeStatusBadge() puts the
        badge back exactly where it was. */
    void collapseCaption() { editor.statusLabel.setBounds (0, 0, 0, 0); }
    void restoreCaption() { editor.placeStatusBadge(); }

    /**  A palette and a language, applied the way the panel applies them. Both are
        live properties of the editor rather than of its construction, and the badge's
        geometry comes from MEASURED text - so a translation is a layout case (a
        longer caption in the same row) and a palette is different ink on the same
        geometry. Neither writes a preference: persisting belongs to the handlers the
        user drives, not to these two. */
    j37panel::ThemeChoice theme() const { return editor.themeChoice; }

    void useTheme (j37panel::ThemeChoice choice)
    {
        editor.themeChoice = choice;
        editor.applyTheme();
    }

    void useLanguage (int languageIndex) { editor.applyLanguage (languageIndex); }

    /**  The USER palette is a global the editor copies at applyTheme() time, so a
        harness that does not set it renders whatever palette this machine's owner
        saved. The matrix sets its own, and the run puts this one back. */
    void useUserPalette (const j37panel::UiPalette& palette) { j37panel::userPalette = palette; }

    /**  The panel's live shimmer - the dots that drift eight times a second over the
        surface. It is a clock, not the badge, and two paints in different steps differ
        in those dots and in nothing else, so it is read and written here to take the
        clock out of the measurement. */
    float shimmer() const { return editor.panelTextureWeights.shimmer; }
    void setShimmer (float value) { editor.panelTextureWeights.shimmer = value; }

    FirstAudioProcessorEditor& editor;
};

//==============================================================================
namespace
{

using Probe = J37HeaderRenderProbe;

int failures = 0;
int checks = 0;

void expect (bool ok, const juce::String& what)
{
    ++checks;

    if (! ok)
    {
        ++failures;
        std::printf ("FAIL  %s\n", what.toRawUTF8());
    }
}

juce::Colour pixel (const juce::Image& image, int x, int y)
{
    if (! image.getBounds().contains (x, y))
        return {};

    return image.getPixelAt (x, y);
}

int luma (juce::Colour c) noexcept
{
    return (c.getRed() * 299 + c.getGreen() * 587 + c.getBlue() * 114) / 1000;
}

int levelDifference (juce::Colour a, juce::Colour b) noexcept
{
    return juce::jmax (std::abs (a.getRed() - b.getRed()),
                       std::abs (a.getGreen() - b.getGreen()),
                       std::abs (a.getBlue() - b.getBlue()));
}

//==============================================================================
/*  One rendered state of the panel: the image plus everything the claims need to
    name a rectangle in it. */
struct Shot
{
    juce::Image image;
    juce::Rectangle<int> header;
    juce::Rectangle<int> badge;
    juce::Rectangle<int> caption;
    juce::Rectangle<int> levels;
    juce::String text;
    bool shown = false;
};

juce::Image paint (juce::Component& editor)
{
    juce::Image image (juce::Image::ARGB, juce::jmax (1, editor.getWidth()),
                       juce::jmax (1, editor.getHeight()), true);

    juce::Graphics g (image);
    editor.paintEntireComponent (g, true);

    return image;
}

Shot takeShot (juce::Component& editor, const Probe& probe)
{
    Shot shot;
    shot.image = paint (editor);
    shot.header = probe.header();
    shot.levels = probe.levels();
    shot.badge = probe.badge();
    shot.text = probe.caption();
    shot.shown = probe.badgeShown() && ! shot.badge.isEmpty();

    if (shot.shown)
        shot.caption = shot.badge.withTrimmedLeft (j37panel::badge::lampReach);

    return shot;
}

/** The badge's row: LEVELS ON/OFF ends on its left, the header's right edge on its
    right, and it is two pixels taller than the badge on each side, so a plate taller
    than the words cannot hide outside it. */
juce::Rectangle<int> badgeRow (const Shot& shot)
{
    const auto left = shot.levels.getRight() + 1;
    const auto right = shot.header.getRight() - 6;

    return {left, shot.badge.getY() - 2, juce::jmax (0, right - left), shot.badge.getHeight() + 4};
}

/** The caption's ink: pixels whose luminance is far from the box's dominant one. The
    box's own two levels are measured from the box, so this works on a light panel and
    on a dark one without a palette. */
juce::Rectangle<int> inkBounds (const juce::Image& image, juce::Rectangle<int> box)
{
    std::vector<int> histogram (256, 0);

    for (int y = box.getY(); y < box.getBottom(); ++y)
        for (int x = box.getX(); x < box.getRight(); ++x)
            ++histogram[static_cast<std::size_t> (luma (pixel (image, x, y)))];

    int background = 0;
    for (int i = 1; i < 256; ++i)
        if (histogram[static_cast<std::size_t> (i)]
            > histogram[static_cast<std::size_t> (background)])
            background = i;

    //  `far` is a macro in the Windows headers, so the other end of the box's
    //  contrast is named for what it is.
    int extreme = background;
    int offset = 0;
    for (int i = 0; i < 256; ++i)
        if (std::abs (i - background) > offset && histogram[static_cast<std::size_t> (i)] > 0)
        {
            offset = std::abs (i - background);
            extreme = i;
        }

    const auto threshold = juce::jmax (12, std::abs (extreme - background) / 3);

    juce::Rectangle<int> bounds;
    for (int y = box.getY(); y < box.getBottom(); ++y)
        for (int x = box.getX(); x < box.getRight(); ++x)
            if (std::abs (luma (pixel (image, x, y)) - background) > threshold)
                bounds = bounds.isEmpty()
                             ? juce::Rectangle<int> (x, y, 1, 1)
                             : bounds.getUnion (juce::Rectangle<int> (x, y, 1, 1));

    return bounds;
}

int inkCount (const juce::Image& image, juce::Rectangle<int> box, int background)
{
    int count = 0;

    for (int y = box.getY(); y < box.getBottom(); ++y)
        for (int x = box.getX(); x < box.getRight(); ++x)
            if (std::abs (luma (pixel (image, x, y)) - background) > 40)
                ++count;

    return count;
}

//==============================================================================
/*  The claims, each one a predicate so the negative controls can ask the same
    question of a damaged image. */

/** The badge's box in the form StatusBadge.h speaks: the shipped helpers below take
    this, so the test does not re-do the lamp's arithmetic. */
j37panel::badge::Box badgeBox (const Shot& shot)
{
    return {shot.badge.getX(), shot.badge.getY(), shot.badge.getWidth(), shot.badge.getHeight()};
}

/** Where the lamp is allowed to be: its disc and the bloom around it, with a pixel
    of air for the anti-aliasing on the ellipse's edge. lampReach is what the badge's
    width already reserves for exactly this. */
juce::Rectangle<int> lampBox (const Shot& shot)
{
    const auto radius = j37panel::badge::lampRadius + 2 * j37panel::badge::lampGlow + 1;
    const auto centreX = j37panel::badge::lampCentreX (badgeBox (shot));

    return {centreX - radius, shot.badge.getCentreY() - radius, radius * 2, radius * 2};
}

/** Everything the badge is allowed to paint in its row: its glyphs and its lamp. */
juce::Rectangle<int> drawnByBadge (const Shot& shot)
{
    return inkBounds (shot.image, shot.caption).expanded (1).getUnion (lampBox (shot).expanded (1));
}

/**  Plate: the badge's row contains the lamp and the glyphs and NOTHING else.

     `bare` is the same editor painted in the same frame with the caption collapsed,
     so every pixel that differs between the two is a pixel the badge put there. Each
     of them has to be inside the glyph ink or inside the lamp - a plate behind the
     words would change the whole row, and the gaps between the letters would be the
     first thing to give it away.

     `allowed` comes from the undamaged render: the negative controls pass the same
     rectangle, so a plate that covers the glyphs is measured against where the glyphs
     were. Returns the number of pixels drawn outside those two places. */
int strayDrawing (const Shot& shot, const juce::Image& bare, juce::Rectangle<int> allowed)
{
    if (! shot.shown)
        return -1;

    int drawn = 0;
    int stray = 0;
    juce::Rectangle<int> where;

    for (int y = badgeRow (shot).getY(); y < badgeRow (shot).getBottom(); ++y)
        for (int x = badgeRow (shot).getX(); x < badgeRow (shot).getRight(); ++x)
        {
            if (levelDifference (pixel (shot.image, x, y), pixel (bare, x, y)) == 0)
                continue;

            ++drawn;

            if (allowed.contains (x, y))
                continue;

            ++stray;
            where = where.isEmpty() ? juce::Rectangle<int> (x, y, 1, 1)
                                    : where.getUnion (juce::Rectangle<int> (x, y, 1, 1));
        }

    std::printf ("      the badge drew %d px in its row: %d of them outside the glyphs and the "
                 "lamp",
                 drawn, stray);

    if (stray > 0)
        std::printf (", from %d,%d to %d,%d", where.getX(), where.getY(), where.getRight(),
                     where.getBottom());

    std::printf ("\n");

    return stray;
}

/** Fit: glyphs on the pixels, inside the box, with air on every side. A caption too
    wide for its box is squeezed by drawFittedText and its ink runs into the right
    edge - which is what "the sign is cut off" looked like. The left edge is not
    required to have air: the pen starts there and a glyph's left bearing is allowed
    to be zero. */
bool glyphsFit (const Shot& shot)
{
    if (! shot.shown || shot.caption.isEmpty())
        return false;

    const auto ink = inkBounds (shot.image, shot.caption);

    if (ink.isEmpty() || ink.getWidth() <= 8 || ink.getHeight() < 5)
        return false;

    return ink.getX() >= shot.caption.getX() && ink.getRight() < shot.caption.getRight()
        && ink.getY() > shot.caption.getY() && ink.getBottom() < shot.caption.getBottom();
}

/** The box IS the measured text: placeStatusBadge() gives the label the width the
    panel measures for its own words, clamped to the room the row has. Comparing the
    box with that measurement - rather than with the ink - is what makes this hold on
    Windows and on Linux: JUCE's fitted text shrinks a caption into a box too narrow
    for it, so a squeezed caption can still have ink that fills the box (the ink check
    below) while the box has stopped being its words. */
bool captionIsMeasuredFromItsWords (const Shot& shot, const juce::Font& captionFont)
{
    if (! shot.shown || shot.caption.isEmpty())
        return false;

    const auto measured = juce::roundToInt (
        juce::GlyphArrangement::getStringWidth (captionFont, shot.text) + 1.0f);
    const auto ink = inkBounds (shot.image, shot.caption);

    return shot.caption.getWidth() == measured && ! ink.isEmpty()
        && measured - ink.getWidth() <= 4;
}

/** Lamp: a disc on the panel, and an empty gap between it and the first glyph. */
bool lampClearOfWords (const Shot& shot)
{
    if (! shot.shown)
        return false;

    const auto centre = lampBox (shot).getCentre();
    const auto panel = pixel (shot.image, shot.header.getRight() - 4, centre.y);

    if (levelDifference (pixel (shot.image, centre.x, centre.y), panel) <= 24)
        return false;

    const auto ink = inkBounds (shot.image, shot.caption);

    if (ink.isEmpty())
        return false;

    const auto discRight = j37panel::badge::lampDiscRight (badgeBox (shot));
    const auto gap = juce::Rectangle<int> (discRight, shot.badge.getY(), ink.getX() - discRight,
                                           shot.badge.getHeight());

    return ! gap.isEmpty() && inkCount (shot.image, gap, luma (panel)) == 0;
}

/** Row: after LEVELS, and inside the header. */
bool badgeIsAfterLevels (const Shot& shot)
{
    if (! shot.shown)
        return false;

    const auto lampLeft = shot.badge.getX() + j37panel::badge::lampGlow;

    return lampLeft > shot.levels.getRight() && shot.badge.getRight() <= shot.header.getRight() - 6;
}

//==============================================================================
/** Everything a rendered badge has to be, given its own text and box. */
void checkBadge (const Shot& shot, const juce::Image& bare, const juce::Font& captionFont,
                 const juce::String& where)
{
    const auto name = [&where] (const char* claim) { return juce::String (where) + ": " + claim; };

    expect (shot.shown, name ("the badge is on screen on the main page"));

    if (! shot.shown || shot.caption.isEmpty())
        return;

    const auto ink = inkBounds (shot.image, shot.caption);

    std::printf ("      %s: badge %dx%d at %d,%d, caption %dx%d, ink %dx%d at %d,%d\n",
                 where.toRawUTF8(), shot.badge.getWidth(), shot.badge.getHeight(),
                 shot.badge.getX(), shot.badge.getY(), shot.caption.getWidth(),
                 shot.caption.getHeight(), ink.getWidth(), ink.getHeight(), ink.getX(),
                 ink.getY());

    //  The caption is the badge, less the lamp's reach: StatusBadge.h is the one
    //  definition of that reach and the panel paints with the same header.
    expect (shot.badge.getWidth() == shot.caption.getWidth() + j37panel::badge::lampReach,
            name ("the badge is the caption plus the lamp's reach and nothing else"));

    expect (glyphsFit (shot), name ("every glyph is inside its box, none clipped"));
    expect (captionIsMeasuredFromItsWords (shot, captionFont),
            name ("the caption box is measured from this state's own words"));
    expect (lampClearOfWords (shot), name ("the lamp does not overlap the first glyph"));
    expect (badgeIsAfterLevels (shot),
            name ("the badge starts after LEVELS ON/OFF and ends inside the header"));
    expect (strayDrawing (shot, bare, drawnByBadge (shot)) == 0,
            name ("nothing is drawn in the badge's row but the lamp and the glyphs"));
}

//==============================================================================
/*  Damage, exactly as the panel looked when the user reported it: the plate this
    replaced was `palette.raised.darker (0.16f)` filled as a rounded rectangle over the
    whole of the badge's row - once a pill a third of the header wide. */
void paintPlate (Shot& shot, const j37panel::UiPalette& palette, int extraWidth)
{
    juce::Graphics g (shot.image);
    g.setColour (palette.raised.darker (0.16f));

    const auto plate = shot.badge.withWidth (shot.badge.getWidth() + extraWidth);
    g.fillRoundedRectangle (plate.toFloat(), 4.0f);
}

/*  Damage: the caption's last glyph pushed into the box's right edge - what a caption
    too wide for its box looks like after drawFittedText squeezes it. */
void clipCaption (Shot& shot)
{
    juce::Graphics g (shot.image);
    g.setColour (juce::Colours::white);
    g.fillRect (juce::Rectangle<int> (shot.caption.getRight() - 1, shot.caption.getY(), 1,
                                      shot.caption.getHeight()));
}

/*  Damage: the lamp drawn on top of the first letter. */
void lampOntoWords (Shot& shot)
{
    const auto ink = inkBounds (shot.image, shot.caption);

    juce::Graphics g (shot.image);
    g.setColour (juce::Colours::white);
    g.fillEllipse (static_cast<float> (ink.getX()) - 3.0f,
                   static_cast<float> (shot.badge.getCentreY()) - 3.0f, 7.0f, 7.0f);
}

/*  The panel's timer, without a window: the editor is never on the desktop here, so
    nothing pumps the message queue - and `runDispatchLoopUntil` is compiled out of a
    JUCE build without modal loops. Sleeping and then dispatching the timers that came
    due is what the message queue would have done, and it is enough for the layout,
    which resized() has already done. */
void pumpMessages (int milliseconds)
{
    for (int elapsed = 0; elapsed < milliseconds; elapsed += 16)
    {
        juce::Thread::sleep (16);
        juce::Timer::callPendingTimersSynchronously();
    }
}

struct WidthUnderTest
{
    const char* name;
    int width;
    int height;
};

/** A palette by the name the panel's own selector uses, for the log. */
juce::String themeName (j37panel::ThemeChoice choice)
{
    switch (choice)
    {
        case j37panel::ThemeChoice::ivory: return "ivory";
        case j37panel::ThemeChoice::charcoal: return "charcoal";
        case j37panel::ThemeChoice::metal: return "metal";
        case j37panel::ThemeChoice::darkMetal: return "darkMetal";
        case j37panel::ThemeChoice::whiteMonster: return "whiteMonster";
        case j37panel::ThemeChoice::user: return "user";
    }

    return "unknown";
}

} // namespace

//==============================================================================
int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    FirstAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> owned (processor.createEditor());

    if (owned == nullptr)
    {
        std::printf ("FAIL  the processor built no editor\n");
        return 1;
    }

    auto* editor = dynamic_cast<FirstAudioProcessorEditor*> (owned.get());

    if (editor == nullptr)
    {
        std::printf ("FAIL  the processor's editor is not the panel\n");
        return 1;
    }

    Probe probe (*editor);

    /*  What this machine had before the run. The editor's DESTRUCTOR writes the theme
        choice, the USER palette and its own size into the machine-wide preferences (see
        ~FirstAudioProcessorEditor), so a harness that ends on `user` at the 780 x 664
        floor would edit the desk it ran on. These three are put back before the editor
        goes away. */
    const auto originalTheme = probe.theme();
    const auto originalWidth = editor->getWidth();
    const auto originalHeight = editor->getHeight();
    const auto originalUserPalette = j37panel::userPalette;

    /*  One state of the panel, rendered twice: with the badge, and in the same frame
        with the caption collapsed. Both images come from the same editor, the same size
        and the same texture, so the panel's gradient, grain and material cancel out
        exactly and what is left is the badge. The live shimmer is switched off for the
        pair - it is a clock rather than the badge, and catching one 125 ms step with two
        whole-panel paints is a race this measurement does not need to run. */
    const auto renderState = [&]() {
        const auto shimmer = probe.shimmer();
        probe.setShimmer (0.0f);

        const auto withBadge = takeShot (*editor, probe);
        probe.collapseCaption();
        const auto bare = paint (*editor);
        probe.restoreCaption();

        probe.setShimmer (shimmer);

        return std::pair<Shot, juce::Image> {withBadge, bare};
    };

    /*  One caption, measured and checked: the two renders, the claims against them,
        and the shot back for a caller that wants to compare two states. */
    const auto shotFor = [&] (const juce::String& caption, const juce::String& where) {
        probe.showCaption (caption);
        auto [shot, bare] = renderState();
        checkBadge (shot, bare, probe.captionFont(), where);

        return shot;
    };

    //  The panel's minimum, the size it opens at, and a wide window: the badge sits in
    //  one row of all three, and the row is what the user reported on.
    const WidthUnderTest widths[] = {{"minimum", 780, 664}, {"typical", 1060, 916},
                                     {"wide", 1440, 1000}};

    Shot lastShot;
    juce::Image lastBare;

    for (const auto& size : widths)
    {
        editor->setSize (size.width, size.height);
        pumpMessages (200);

        //  The caption the panel opens on, stated rather than inherited: the timer is
        //  the panel's only writer and it returns while the editor is not on a desktop,
        //  so a render taken here would otherwise depend on what the constructor
        //  happened to leave in the label.
        probe.showCaption (xlat ("STEREO / REAL TIME"));

        std::printf ("\n%s %dx%d  badge shown %d  text \"%s\"\n", size.name, editor->getWidth(),
                     editor->getHeight(), static_cast<int> (probe.badgeShown()),
                     probe.caption().toRawUTF8());

        auto [shot, bare] = renderState();

        std::printf ("      levels %d,%d %dx%d  header %dx%d  row %d,%d %dx%d\n",
                     shot.levels.getX(), shot.levels.getY(), shot.levels.getWidth(),
                     shot.levels.getHeight(), shot.header.getWidth(), shot.header.getHeight(),
                     badgeRow (shot).getX(), badgeRow (shot).getY(), badgeRow (shot).getWidth(),
                     badgeRow (shot).getHeight());

        checkBadge (shot, bare, probe.captionFont(), size.name);

        lastShot = shot;
        lastBare = bare;
    }

    //  The two machine states, at the width the panel opens at. The words are the
    //  panel's own two captions from its own translation table, and they go through the
    //  timer's own two calls (see Probe::showCaption).
    editor->setSize (1060, 916);
    pumpMessages (200);

    const auto realTimeText = xlat ("STEREO / REAL TIME");
    const auto bypassText = xlat ("BYPASSED / DRY");

    const auto realTime = shotFor (realTimeText, "state: real time");
    const auto bypassed = shotFor (bypassText, "state: bypass");

    std::printf ("\nstate: bypass  text \"%s\"  badge %d,%d %dx%d\n", bypassed.text.toRawUTF8(),
                 bypassed.badge.getX(), bypassed.badge.getY(), bypassed.badge.getWidth(),
                 bypassed.badge.getHeight());

    expect (bypassed.text != realTime.text, "state: the caption follows the machine");
    expect (bypassed.caption.getWidth() != realTime.caption.getWidth(),
            "state: the two captions are different widths, so a leftover would show");
    expect (bypassed.badge.getX() == realTime.badge.getX()
                && bypassed.badge.getY() == realTime.badge.getY(),
            "state: both states put the lamp on the same pixel");

    const auto returned = shotFor (realTimeText, "state: back to real time");

    expect (returned.badge == realTime.badge,
            "state: switching back restores the first state's box, to the pixel");

    //  SETTINGS: the page the badge used to stand down on, because the language
    //  selector was placed in the same row. The pair lives in the page's own member
    //  band now, so the claims are the opposite ones: the badge is UP, it still ends
    //  inside the header, and its row neither contains nor overlaps the selector.
    const auto mainPage = probe.tab();

    probe.openPage (j37panel::index_of_tab_named ("SETTINGS"));
    pumpMessages (200);

    {
        const auto settings = takeShot (*editor, probe);

        std::printf ("\nsettings  badge shown %d  text \"%s\"\n",
                     static_cast<int> (probe.badgeShown()), probe.caption().toRawUTF8());

        expect (probe.tab() == j37panel::index_of_tab_named ("SETTINGS"),
                "settings: the page opened");
        expect (probe.badgeShown(),
                "settings: the badge stays up on the page that used to hide it");

        const auto row = juce::Rectangle<int> (settings.levels.getRight(), settings.levels.getY(),
                                               settings.header.getRight()
                                                   - settings.levels.getRight(),
                                               settings.levels.getHeight());
        expect (inkCount (settings.image, row,
                          luma (pixel (settings.image, settings.header.getRight() - 4,
                                       row.getCentreY())))
                    > 0,
                "settings: the badge paints its row, it is not an empty gap");
        expect (! row.intersects (probe.language()),
                "settings: the language selector is below the badge's row, not in it");
        expect (probe.badge().getRight() <= settings.header.getRight(),
                "settings: the badge still ends inside the header");
    }

    //==========================================================================
    //  The MACHINE page's TYPE row: a caption over every list.
    //
    //  The list and its caption are placed by one call, so the geometry cannot leave
    //  a list unnamed - but a caption that is never given a parent can still be laid
    //  out, given its words and made visible with every call succeeding, and the page
    //  then paints ten lists under seven names. That is what the user's render showed.
    //  The claim is therefore made of the pixels: every one of the ten captions has to
    //  be a child of the panel, visible, and leaving ink in its own strip above its
    //  list.
    //==========================================================================
    probe.openPage (j37panel::index_of_tab_named ("MACHINE"));
    editor->setSize (1060, 916);
    pumpMessages (200);

    {
        const auto machinePage = paint (*editor);
        const auto cells = probe.machineTypeCells();

        std::printf ("\nmachine  %dx%d\n", editor->getWidth(), editor->getHeight());
        expect (static_cast<int> (cells.size()) == 10,
                "machine: the page has ten type selectors");

        for (const auto& cell : cells)
        {
            auto& label = cell.label;
            auto& box = cell.box;
            const auto name = "machine: \"" + label.getText() + "\" ";

            //  A pixel of the cell that is outside the caption's own box, on the
            //  caption's row: the panel behind it, in whatever theme is loaded.
            const auto background = luma (pixel (machinePage, label.getX() - 3,
                                                 label.getBounds().getCentreY()));
            const auto ink = inkCount (machinePage, label.getBounds(), background);

            std::printf ("      %-16s label %dx%d at %d,%d  ink %d\n",
                         label.getText().toRawUTF8(), label.getWidth(), label.getHeight(),
                         label.getX(), label.getY(), ink);

            expect (label.getParentComponent() == editor,
                    name + "is a child of the panel, not an orphan");
            expect (label.isVisible(), name + "is visible on its page");
            expect (! label.getText().isEmpty(), name + "has words of its own");
            expect (label.getBottom() <= box.getY(), name + "sits above its list");
            expect (ink > 0, name + "paints in the strip above its list");
        }
    }

    probe.openPage (mainPage);
    pumpMessages (50);

    //  The palette the width loop rendered in. The controls below damage THAT shot,
    //  so their plate is drawn in the ink it already carries.
    const j37panel::UiPalette controlPalette = probe.palette();

    //==========================================================================
    //  EVERY palette and EVERY translation, at the panel's minimum width.
    //
    //  The badge is measured from its words, so a translation is a LAYOUT case - the
    //  longest caption in the tightest row - and a palette is different ink on the
    //  same geometry. The minimum width is where a caption that no longer fits is
    //  squeezed first, which is what the user saw as "the sign is cut off", so the
    //  matrix runs there rather than at a size that would flatter it.
    //==========================================================================
    const j37panel::ThemeChoice palettes[] = {
        j37panel::ThemeChoice::ivory,        j37panel::ThemeChoice::charcoal,
        j37panel::ThemeChoice::metal,        j37panel::ThemeChoice::darkMetal,
        j37panel::ThemeChoice::whiteMonster, j37panel::ThemeChoice::user};

    //  Back to the page the badge lives on, so the matrix below measures the same
    //  arrangement the first claims did.
    probe.openPage (mainPage);
    pumpMessages (50);

    //  A TEST-OWNED USER palette. The real one is whatever this machine's owner saved,
    //  and a valid but low-contrast choice would make the lamp claim measure their taste
    //  rather than the panel's geometry. This one is light on purpose (the stock USER
    //  default is not), with a status ink that is nothing like the ivory panel's.
    auto testUserPalette = j37panel::paletteForTheme (j37panel::ThemeChoice::ivory);
    testUserPalette.status = juce::Colour (0xff2f7f5f);
    probe.useUserPalette (testUserPalette);

    int matrixCases = 0;

    for (const auto choice : palettes)
        for (int language = 0; language < 2; ++language)
        {
            probe.useTheme (choice);
            probe.useLanguage (language);
            editor->setSize (780, 664);
            pumpMessages (50);

            const auto where = themeName (choice) + (language == 0 ? "/en" : "/uk");

            std::printf ("\n%s  %dx%d\n", where.toRawUTF8(), editor->getWidth(),
                         editor->getHeight());

            const auto realTimeHere = shotFor (xlat ("STEREO / REAL TIME"), where + " real time");
            const auto bypassHere = shotFor (xlat ("BYPASSED / DRY"), where + " bypass");

            expect (realTimeHere.badge.getX() == bypassHere.badge.getX()
                        && realTimeHere.badge.getY() == bypassHere.badge.getY(),
                    where + ": both states put the lamp on the same pixel");

            //  How much row the caption left over, printed so a translation that grows
            //  towards the edge is visible in the log before it ever fails.
            std::printf ("      captions %d px and %d px wide, %d px of row to the header edge\n",
                         realTimeHere.caption.getWidth(), bypassHere.caption.getWidth(),
                         realTimeHere.header.getRight() - 6 - realTimeHere.badge.getRight());

            ++matrixCases;
        }

    expect (matrixCases == 12, "the matrix covered every palette in both translations");

    //==========================================================================
    //  The negative controls. Each one damages a real render the way the panel
    //  looked when the report was made, and its claim has to REFUSE it.
    //==========================================================================
    std::printf ("\nnegative controls\n");

    {
        auto damaged = lastShot;
        paintPlate (damaged, controlPalette, 0);
        expect (strayDrawing (damaged, lastBare, drawnByBadge (lastShot)) > 0,
                "control: a plate behind the words is refused by the plate claim");
    }

    {
        auto damaged = lastShot;
        paintPlate (damaged, controlPalette, 40);
        expect (strayDrawing (damaged, lastBare, drawnByBadge (lastShot)) > 0,
                "control: a pill wider than the sign is refused by the plate claim");
    }

    {
        auto damaged = lastShot;
        clipCaption (damaged);
        expect (! glyphsFit (damaged), "control: a clipped glyph is refused by the fit claim");
    }

    {
        auto damaged = lastShot;
        lampOntoWords (damaged);
        expect (! lampClearOfWords (damaged),
                "control: a lamp on the first letter is refused by the lamp claim");
    }

    {
        auto damaged = lastShot;
        damaged.badge = damaged.badge.withX (damaged.levels.getRight() - 10);
        expect (! badgeIsAfterLevels (damaged),
                "control: a badge over LEVELS is refused by the row claim");
    }

    {
        auto damaged = lastShot;
        damaged.badge = damaged.badge.withWidth (damaged.badge.getWidth() + 40);
        expect (! captionIsMeasuredFromItsWords (damaged, probe.captionFont()),
                "control: a leftover width is refused by the measurement claim");
    }

    {
        //  The matrix above lives one step from this: the LONGEST translation in the
        //  TIGHTEST row. A caption longer than that row still fits nothing, so the
        //  fit claim has to refuse it there too - otherwise the matrix could be green
        //  in a case it cannot actually fail.
        const auto longest = xlat ("STEREO / REAL TIME");
        probe.showCaption (longest + " " + juce::String::repeatedString ("W", 30));

        const auto crowded = takeShot (*editor, probe);

        //  JUCE does not clip an over-long caption here, it SHRINKS it into the box the
        //  row left - so the failure the user saw is not "a cut glyph" but a box that no
        //  longer hugs its words, and that is the claim which has to refuse this.
        std::printf ("\ncontrol: a %d px box for a caption the row had to squeeze\n",
                     crowded.caption.getWidth());

        expect (! captionIsMeasuredFromItsWords (crowded, probe.captionFont()),
                "control: a caption wider than the tight row is refused by the measurement "
                "claim");

        probe.showCaption (longest);
    }

    //  The desk is put back as it was found: the destructor below writes the theme, the
    //  USER palette and the size to machine-wide preferences, so these are returned to
    //  the values this machine had before the run.
    probe.useTheme (originalTheme);
    probe.useUserPalette (originalUserPalette);
    editor->setSize (originalWidth, originalHeight);

    std::printf ("\nHEADER RENDER  %d checks, %d failures\n", checks, failures);

    return failures == 0 ? 0 : 1;
}
