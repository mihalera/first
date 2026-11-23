// =============================================================================
//  The USER THEME window: one row per colour the panel is painted from.
//
//  WHY IT IS A SEPARATE WINDOW. The panel is 780 x 816 at its minimum and every
//  band of it is spoken for; a palette editor needs fifteen rows and a colour
//  space, and there is no page on the machine with that much room. A window of its
//  own is also the honest shape for the thing it edits: the USER palette is not a
//  control of the audio machine - it is not in the signal path and not in the
//  session - it is the colour of the furniture, and it is edited beside the plugin
//  rather than inside it.
//
//  WHAT IT EDITS. j37panel::UiPalette, field for field, through the table below.
//  The table is the ONE place the fields are named, so a field added to the palette
//  shows up here as a row the moment it is added there - and the palette struct's
//  own comment (PanelPaint.h) already asks for the literal to be appended in the
//  same order, which is what keeps the two lists in step.
//
//  WHERE YOU START. Fifteen colours and no suggestion is a blank sheet, and most
//  people pick a palette broadly before they pick it precisely. The window
//  therefore opens on a band of STARTING POINTS - the five front-panel themes and
//  seven rooms of their own (see userThemePresets in PanelPaint.h) - and a press on
//  one loads that whole palette through the same callback an edit uses. It is a
//  starting point, not a mode: the rows below still edit every colour, and RESET
//  still returns to the seed.
//
//  WHAT IT DOES NOT DO. It does not own the palettes and it does not select the
//  theme. It holds the palette it was handed and calls onPaletteChanged() after
//  every edit; the editor decides what that means - it copies the palette into
//  j37panel::userPalette and switches the panel to ThemeChoice::user, which is the
//  "change a colour and the theme becomes USER" behaviour in one line of the caller
//  rather than in this window. A window that switched the theme itself could not be
//  reused for the RESET path, and could not be checked without an editor.
//
//  NO TRANSLATION HERE, deliberately. The panel's own text goes through xlat();
//  this window's words are the palette's field names - BACKGROUND, KNOB SHEEN,
//  GAUGE INK - which are the names the paint kit, this header and the source all
//  use. Translating a field name would leave the swatch editor naming fields in one
//  language and Source/GUI/ in another; the tooltip on the SETTINGS key, which IS
//  translated, is where the user-facing explanation lives.
// =============================================================================
#pragma once

#include "GUI/PanelAssets.h"
#include "GUI/PanelPaint.h"

#include <functional>
#include <memory>

namespace j37panel {

/** One row of the palette: the name the source uses and the member it edits.

    The order of `themeColourFields` is the order the rows are drawn in, and it is
    the palette's own order - chassis first, status last - because that is the order
    a person reads a palette in: the chassis, then the plate, then the ink, then the
    one accent, then the window and the needle.
*/
struct PaletteField
{
    const char *name;
    juce::Colour UiPalette::*member;
};

inline constexpr PaletteField themeColourFields[] = {
    { "BACKGROUND", &UiPalette::background },
    { "PANEL", &UiPalette::panel },
    { "RAISED", &UiPalette::raised },
    { "BORDER", &UiPalette::border },
    { "ACCENT", &UiPalette::accent },
    { "TEXT", &UiPalette::text },
    { "SECONDARY", &UiPalette::secondary },
    { "KNOB FACE", &UiPalette::knobFace },
    { "KNOB SHEEN", &UiPalette::knobHighlight },
    { "KNOB EDGE", &UiPalette::knobEdge },
    { "READOUT", &UiPalette::readout },
    { "GAUGE FACE", &UiPalette::gaugeFace },
    { "GAUGE INK", &UiPalette::gaugeInk },
    { "NEEDLE", &UiPalette::needle },
    { "STATUS", &UiPalette::status },
};

inline constexpr int themeColourFieldCount =
    static_cast<int>(sizeof(themeColourFields) / sizeof(themeColourFields[0]));

/** The palette written as ONE preference string, a colour per field in the table's
    own order. juce::Colour::toString() writes eight hex digits (AARRGGBB) and
    juce::Colour::fromString() reads them back, so a remembered palette is exact and
    still readable in the settings file. The table is the one place the fields are
    named, so a colour added to UiPalette is stored and restored without a second
    list to keep in step - which is why the string is built by walking it rather than
    by naming fifteen members here. */
inline juce::String serializeUserPalette(const UiPalette &palette) {
    juce::StringArray parts;
    for (const auto &field : themeColourFields)
        parts.add((palette.*(field.member)).toString());

    return parts.joinIntoString(",");
}

/** The inverse of serializeUserPalette(). A string carrying fewer colours than the
    palette has fields - one written by an older build, before a field was added -
    leaves the missing ones at `fallback` rather than at transparent black, so an old
    settings file still opens on a whole palette. */
inline UiPalette deserializeUserPalette(const juce::String &text, const UiPalette &fallback) {
    auto palette = fallback;
    const auto parts = juce::StringArray::fromTokens(text, ",", "");

    for (int i = 0; i < themeColourFieldCount && i < parts.size(); ++i)
        if (parts[i].isNotEmpty())
            palette.*(themeColourFields[i].member) = juce::Colour::fromString(parts[i]);

    return palette;
}

/** The window's fixed geometry, in one place: the pane paints against these and the
    window sizes itself from them, so a row height change moves both. */
inline constexpr int userThemeHeaderHeight = 46;
inline constexpr int userThemeRowHeight = 24;
inline constexpr int userThemeFooterHeight = 42;
inline constexpr int userThemeWidth = 360;

// The preset band: three rows of four keys between the header and the first
// colour row. Four columns because the window is 360 px wide and a preset's
// name plus its three-colour chip needs about eighty of them; three rows because
// twelve starting points is the list, and twelve keys in one row would name
// nothing. The row count is derived from the table below, so adding a preset
// lengthens the window rather than overflowing the band.
inline constexpr int userThemePresetColumns = 4;
inline constexpr int userThemePresetRowHeight = 20;
inline constexpr int userThemePresetGap = 4;
inline constexpr int userThemePresetRows =
    (userThemePresetCount + userThemePresetColumns - 1) / userThemePresetColumns;
inline constexpr int userThemePresetBandHeight =
    userThemePresetRows * (userThemePresetRowHeight + userThemePresetGap) + 6;

// The backdrop band: which surface the USER panel is painted from. The table is
// seven entries now that the four stock materials are keys of their own, so the band
// wraps at four to a row exactly the way the preset band does - and the row count is
// derived from assets::backgroundChoices, so an entry added there lengthens the
// window instead of overflowing the row.
inline constexpr int userThemeBackdropLabelHeight = 13;
inline constexpr int userThemeBackdropRowHeight = 20;
inline constexpr int userThemeBackdropGap = 4;
inline constexpr int userThemeBackdropColumns = 4;
inline constexpr int userThemeBackdropRows =
    (j37panel::assets::backgroundChoiceCount + userThemeBackdropColumns - 1)
    / userThemeBackdropColumns;
inline constexpr int userThemeBackdropBandHeight =
    userThemeBackdropLabelHeight
    + userThemeBackdropRows * (userThemeBackdropRowHeight + userThemeBackdropGap) + 4;

inline constexpr int userThemePatinaLabelHeight = 13;
inline constexpr int userThemePatinaRowHeight = 20;
inline constexpr int userThemePatinaBandHeight =
    userThemePatinaLabelHeight + userThemePatinaRowHeight + 8;

/** Where the first colour row starts: below the header, the preset band and the
    backdrop and patina bands. */
inline constexpr int userThemeRowsTop = userThemeHeaderHeight + userThemePresetBandHeight +
                                        userThemeBackdropBandHeight + userThemePatinaBandHeight;

/** The window's content height, grown from those numbers rather than measured. */
inline constexpr int userThemePaneHeight =
    userThemeRowsTop + themeColourFieldCount * userThemeRowHeight + userThemeFooterHeight;

//==============================================================================
/**
    The window's content: a colour per row, and a selector per row.

    It paints itself from the palette it holds rather than from the panel's, which
    is the point of a colour editor - the swatch you are dragging is the colour you
    are choosing, and the row's own ink follows it on the same frame. The panel
    catches up through onPaletteChanged().

    The selector is a juce::ColourSelector in a CallOutBox rather than fifteen
    always-visible sliders: fifteen embedded selectors would be a window the height
    of a screen, and only one row is ever being edited. The call-out owns the
    selector and this pane is kept alive by a SafePointer, so dismissing the box -
    by choosing, by clicking away, by closing the window - cannot leave a callback
    pointing at a dead pane.
*/
class UserThemePane final : public juce::Component {
  public:
    /** Called after every edit, with the whole palette. */
    std::function<void(const UiPalette &)> onPaletteChanged;

    /** Called when the pane's own CLOSE key is pressed. The window is what hides
        itself; the pane only knows a key was pressed. */
    std::function<void()> onClose;

    /** Called with the NAME of the backdrop the user pressed. The pane does not
        rebuild the panel itself - the editor owns that - so the same key works in a
        window that is only ever one of several ways into the setting. */
    std::function<void(const juce::String&)> onBackgroundChanged;

    /** Called when the panel's optional low-opacity patina is toggled. */
    std::function<void(bool)> onPatinaChanged;

    explicit UserThemePane(UiPalette initial,
                           juce::String backgroundIn = "THEME",
                           bool patinaEnabledIn = false) :
        palette(initial), background(std::move(backgroundIn)), patinaEnabled(patinaEnabledIn) {
        // The pane paints its whole face from the palette (see paint()), so it is
        // opaque - which is what stops the window's own background colour from
        // showing through as a flat card before the first paint, or wherever a
        // row's bounds fall short of the edge.
        setOpaque(true);
    }

    /** Adopts a palette from outside. The editor hands over the live one each time
        the window opens, so a window kept from an earlier session cannot show the
        seed while the panel is painted from the edits. */
    void setPalette(const UiPalette &next) {
        palette = next;
        repaint();
    }

    const UiPalette &getPalette() const noexcept { return palette; }

    /** Adopts the backdrop the panel is painted from, so a window reopened after a
        RESET (or after the settings file was edited) marks the live one. */
    void setBackground(const juce::String &next) {
        background = next;
        repaint();
    }

    void setPatinaEnabled(bool enabled) {
        patinaEnabled = enabled;
        repaint();
    }

    /** The RESET key: back to the seed, through the same callback an edit uses, so
        the caller has one code path rather than two. */
    void resetToSeed() {
        palette = userThemeSeed;
        repaint();
        notify();
    }

    /** A STARTING POINT key: adopts that whole look - the palette AND the surface it
        was drawn on - then reports both the way an edit does, so the panel switches
        to USER and changes its chassis on the same frame. The seed is deliberately
        not moved: RESET means "the seed", not "whatever preset I touched last". */
    void applyPreset(int index) {
        if (index < 0 || index >= userThemePresetCount)
            return;

        palette = *userThemePresets[index].palette;

        // The background is part of the preset rather than something it leaves
        // behind: a brass palette on the charcoal bakelite is half a starting point.
        background = j37panel::assets::backgroundChoiceFor (userThemePresets[index].background).name;
        repaint();
        notify();

        if (onBackgroundChanged != nullptr)
            onBackgroundChanged(background);
    }

    void paint(juce::Graphics &g) override {
        // The window's own chassis is the user's `background`, so the editor is
        // framed in the colour being edited - a background that swallows the panel
        // is visible here before it is applied to the panel.
        g.fillAll(palette.background);

        const auto area = getLocalBounds();
        const auto header = area.withHeight(userThemeHeaderHeight);

        g.setColour(palette.accent);
        g.setFont (panelFont (13.0f, juce::Font::bold));
        g.drawText("USER THEME", header.reduced(14, 0).withTrimmedBottom(20),
                   juce::Justification::centredLeft, false);

        g.setColour(palette.secondary);
        g.setFont (panelFont (10.0f));
        g.drawText("start from a preset, then click any colour to edit it",
                   header.reduced(14, 0).withTrimmedTop(20), juce::Justification::centredLeft,
                   false);

        // The optional brand mark, right-aligned in the header. It is the one
        // place on this window with room for a signature, and a build without the
        // asset simply has the empty end of the band - the window never draws a
        // placeholder box, which is what a missing image looks like.
        if (const auto &mark = j37panel::assets::cachedImage("brand_mark_png"); mark.isValid()) {
            const auto side = juce::jmin(userThemeHeaderHeight - 12, 34);
            g.drawImageWithin(mark, getWidth() - 14 - side, 6, side, side,
                              juce::RectanglePlacement::centred, false);
        }

        paintPresets(g);
        paintBackdrop(g);
        paintPatina(g);

        for (int row = 0; row < themeColourFieldCount; ++row) {
            const auto bounds = rowBounds(row);
            const auto field = themeColourFields[row];
            const auto colour = palette.*(field.member);

            if (row == hoveredRow) {
                g.setColour(palette.raised);
                g.fillRoundedRectangle(bounds.toFloat(), 3.0f);
            }

            // The swatch is filled AND outlined: a `readout` swatch is nearly black on
            // every dark theme, and without the outline it is a hole in the row.
            const auto swatch = swatchBounds(row).toFloat();
            g.setColour(colour);
            g.fillRoundedRectangle(swatch, 3.0f);
            g.setColour(palette.border);
            g.drawRoundedRectangle(swatch.reduced(0.5f), 3.0f, 1.0f);

            g.setColour(palette.secondary);
            g.setFont (panelFont (10.0f, juce::Font::bold));
            g.drawText(field.name, bounds.getX() + 14, bounds.getY(),
                       swatchBounds(row).getX() - bounds.getX() - 22, bounds.getHeight(),
                       juce::Justification::centredLeft, false);

            g.setColour(palette.text);
            g.setFont (panelFont (10.0f));
            // One rounded reading of the swatch's edge, used by both the x and
            // the width: the swatch is a rectangle<float>, so writing its right
            // edge into drawText's int parameters truncated it silently in two
            // places - which Clang reports as -Wfloat-conversion and which left
            // the hex row a pixel adrift of its chip.
            const auto swatchRight = juce::roundToInt(swatch.getRight());
            g.drawText(colour.toDisplayString(false), swatchRight + 10, bounds.getY(),
                       bounds.getRight() - swatchRight - 12,
                       bounds.getHeight(), juce::Justification::centredLeft, false);
        }

        paintFooter(g);
    }

    void mouseMove(const juce::MouseEvent &event) override {
        const auto row = rowAt(event.y);
        const auto preset = presetAt(event.getPosition());
        const auto backdrop = backdropAt(event.getPosition());
        const auto patina = patinaBounds().contains(event.getPosition());
        if (row != hoveredRow || preset != hoveredPreset || backdrop != hoveredBackdrop ||
            patina != hoveredPatina) {
            hoveredRow = row;
            hoveredPreset = preset;
            hoveredBackdrop = backdrop;
            hoveredPatina = patina;
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent &) override {
        if (hoveredRow >= 0 || hoveredPreset >= 0 || hoveredBackdrop >= 0 || hoveredPatina) {
            hoveredRow = -1;
            hoveredPreset = -1;
            hoveredBackdrop = -1;
            hoveredPatina = false;
            repaint();
        }
    }

    void mouseDown(const juce::MouseEvent &event) override {
        if (resetBounds().contains(event.getPosition())) {
            resetToSeed();
            return;
        }

        if (closeBounds().contains(event.getPosition())) {
            if (onClose != nullptr)
                onClose();
            return;
        }

        if (patinaBounds().contains(event.getPosition())) {
            applyPatina(!patinaEnabled);
            return;
        }

        const auto preset = presetAt(event.getPosition());
        if (preset >= 0) {
            applyPreset(preset);
            return;
        }

        const auto backdrop = backdropAt(event.getPosition());
        if (backdrop >= 0) {
            applyBackdrop(backdrop);
            return;
        }

        const auto row = rowAt(event.y);
        if (row >= 0)
            openSelector(row);
    }

  private:
    static constexpr int swatchWidth = 54;
    static constexpr int keyWidth = 92;

    juce::Rectangle<int> rowBounds(int row) const {
        return { 8, userThemeRowsTop + row * userThemeRowHeight, getWidth() - 16,
                 userThemeRowHeight };
    }

    /** The band's keys, in the table's order: rows of four, left to right. */
    juce::Rectangle<int> presetBounds(int index) const {
        const auto row = index / userThemePresetColumns;
        const auto column = index % userThemePresetColumns;
        const auto bandLeft = 8;
        const auto bandWidth = getWidth() - 16;
        // presetKeyWidth, not keyWidth: the footer's RESET/CLOSE keys already own
        // that name as a member, and a local of the same name is a -Wshadow
        // warning in a class that has no business hiding its own constants.
        const auto presetKeyWidth =
            (bandWidth - (userThemePresetColumns - 1) * userThemePresetGap)
            / userThemePresetColumns;

        return { bandLeft + column * (presetKeyWidth + userThemePresetGap),
                 userThemeHeaderHeight + row * (userThemePresetRowHeight + userThemePresetGap),
                 presetKeyWidth, userThemePresetRowHeight };
    }

    /** Which preset, if any, the point is on. -1 when it is on none of them. */
    int presetAt(juce::Point<int> point) const {
        for (int index = 0; index < userThemePresetCount; ++index)
            if (presetBounds(index).contains(point))
                return index;

        return -1;
    }

    /** The backdrop key: four to a row, one per entry in the table. */
    juce::Rectangle<int> backdropBounds(int index) const {
        constexpr auto columns = userThemeBackdropColumns;
        const auto row = index / columns;
        const auto column = index % columns;
        const auto bandLeft = 8;
        const auto bandWidth = getWidth() - 16;
        const auto backdropKeyWidth =
            (bandWidth - (columns - 1) * userThemeBackdropGap) / columns;

        return { bandLeft + column * (backdropKeyWidth + userThemeBackdropGap),
                 userThemeHeaderHeight + userThemePresetBandHeight
                     + userThemeBackdropLabelHeight
                     + row * (userThemeBackdropRowHeight + userThemeBackdropGap),
                 backdropKeyWidth, userThemeBackdropRowHeight };
    }

    /** Which backdrop key, if any, the point is on. -1 when it is on none. */
    int backdropAt(juce::Point<int> point) const {
        for (int index = 0; index < j37panel::assets::backgroundChoiceCount; ++index)
            if (backdropBounds(index).contains(point))
                return index;

        return -1;
    }

    juce::Rectangle<int> patinaBounds() const {
        const auto top = userThemeHeaderHeight + userThemePresetBandHeight +
                         userThemeBackdropBandHeight + userThemePatinaLabelHeight;
        return {8, top, getWidth() - 16, userThemePatinaRowHeight};
    }

    juce::Rectangle<int> swatchBounds(int row) const {
        // The swatch sits left of the hex text, both right-aligned against the row's
        // end: the eye scans the names down the left and the chips down the right.
        const auto bounds = rowBounds(row);
        return bounds.withTrimmedLeft(bounds.getWidth() - swatchWidth - 66)
            .withSizeKeepingCentre(swatchWidth, userThemeRowHeight - 8);
    }

    juce::Rectangle<int> resetBounds() const {
        return { 14, getHeight() - userThemeFooterHeight + 6, keyWidth,
                 userThemeFooterHeight - 16 };
    }

    juce::Rectangle<int> closeBounds() const {
        return { getWidth() - 14 - keyWidth, getHeight() - userThemeFooterHeight + 6, keyWidth,
                 userThemeFooterHeight - 16 };
    }

    int rowAt(int y) const {
        if (y < userThemeRowsTop)
            return -1;

        const auto row = (y - userThemeRowsTop) / userThemeRowHeight;
        return row < themeColourFieldCount ? row : -1;
    }

    /** The starting-point band: one key per preset, each painted in the palette it
        would load, so the choice is a picture rather than a name. The three-colour
        chip is the preset's own chassis, plate and accent - the same three colours
        the eye reads first when it looks at the panel. */
    void paintPresets(juce::Graphics &g) const {
        for (int index = 0; index < userThemePresetCount; ++index) {
            const auto bounds = presetBounds(index);
            const auto &preset = userThemePresets[index];
            const auto &face = *preset.palette;

            if (index == hoveredPreset) {
                g.setColour(palette.raised.brighter(0.10f));
                g.fillRoundedRectangle(bounds.toFloat(), 3.0f);
            }

            // The chip: chassis, plate, accent, each a vertical bar. It is drawn
            // against the key's own left edge, and the NAME follows it in the
            // preset's own ink, so the key is legible on any palette.
            const auto chip = bounds.withWidth(18).reduced(0, 4);
            const auto barWidth = chip.getWidth() / 3;

            g.setColour(face.background);
            g.fillRect(chip.withWidth(barWidth));
            g.setColour(face.panel);
            g.fillRect(chip.withTrimmedLeft(barWidth).withWidth(barWidth));
            g.setColour(face.accent);
            g.fillRect(chip.withTrimmedLeft(2 * barWidth));

            g.setColour(face.text);
            g.setFont (panelFont (9.0f, juce::Font::bold));
            g.drawText(preset.name, bounds.withTrimmedLeft(22).reduced(2, 0),
                       juce::Justification::centredLeft, false);

            g.setColour(index == hoveredPreset ? palette.accent : palette.border);
            g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 3.0f, 1.0f);
        }
    }

    /** A backdrop key: adopt it here, then report the NAME. The panel is rebuilt by
        the editor, so this pane has no image of its own to refresh. */
    void applyBackdrop(int index) {
        if (index < 0 || index >= j37panel::assets::backgroundChoiceCount)
            return;

        background = j37panel::assets::backgroundChoices[index].name;
        repaint();

        if (onBackgroundChanged != nullptr)
            onBackgroundChanged(background);
    }

    void applyPatina(bool enabled) {
        patinaEnabled = enabled;
        repaint();
        if (onPatinaChanged != nullptr)
            onPatinaChanged(patinaEnabled);
    }

    /** The band: a caption and four keys, the live one filled in the accent. */
    void paintBackdrop(juce::Graphics &g) const {
        const auto label = juce::Rectangle<int>(
            14, userThemeHeaderHeight + userThemePresetBandHeight, getWidth() - 28,
            userThemeBackdropLabelHeight);

        g.setColour(palette.secondary);
        g.setFont (panelFont (9.0f, juce::Font::bold));
        g.drawText("BACKGROUND ASSET", label, juce::Justification::centredLeft, false);

        for (int index = 0; index < j37panel::assets::backgroundChoiceCount; ++index) {
            const auto bounds = backdropBounds(index);
            const auto &choice = j37panel::assets::backgroundChoices[index];
            const auto active = background.equalsIgnoreCase(choice.name);

            g.setColour(active ? palette.accent
                               : index == hoveredBackdrop ? palette.raised.brighter(0.10f)
                                                          : palette.raised);
            g.fillRoundedRectangle(bounds.toFloat(), 3.0f);
            g.setColour(active ? palette.accent : palette.border);
            g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 3.0f, 1.0f);

            g.setColour(active ? palette.readout : palette.text);
            g.setFont (panelFont (9.0f, juce::Font::bold));
            g.drawText(choice.name, bounds, juce::Justification::centred, false);
        }
    }

    void paintPatina(juce::Graphics &g) const {
        const auto labelTop =
            userThemeHeaderHeight + userThemePresetBandHeight + userThemeBackdropBandHeight;
        g.setColour(palette.secondary);
        g.setFont(panelFont(9.0f, juce::Font::bold));
        g.drawText("PANEL PATINA",
                   juce::Rectangle<int>(14, labelTop, getWidth() - 28, userThemePatinaLabelHeight),
                   juce::Justification::centredLeft, false);

        const auto bounds = patinaBounds();
        const auto hovered = hoveredPatina;
        g.setColour(patinaEnabled ? palette.accent
                    : hovered     ? palette.raised.brighter(0.10f)
                                  : palette.raised);
        g.fillRoundedRectangle(bounds.toFloat(), 3.0f);
        g.setColour(patinaEnabled ? palette.accent : palette.border);
        g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 3.0f, 1.0f);
        g.setColour(patinaEnabled ? palette.readout : palette.text);
        g.setFont(panelFont(9.0f, juce::Font::bold));
        g.drawText(patinaEnabled ? "ON  ·  subtle chassis wear" : "OFF  ·  original clean finish",
                   bounds, juce::Justification::centred, false);
    }

    void notify() {
        if (onPaletteChanged != nullptr)
            onPaletteChanged(palette);
    }

    /** The two footer keys are drawn rather than made juce::TextButtons: a
        TextButton takes its ink from a Look and Feel, and the only one available
        here is the panel's - which is painted from the palette the user is in the
        middle of changing. Two rectangles in a pane painted from that palette cannot
        disagree with it. */
    void paintFooter(juce::Graphics &g) const {
        const auto key = [this, &g](juce::Rectangle<int> bounds, const juce::String &caption,
                                    juce::Colour face, juce::Colour ink) {
            g.setColour(face);
            g.fillRoundedRectangle(bounds.toFloat(), 3.0f);
            g.setColour(palette.border);
            g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 3.0f, 1.0f);
            g.setColour(ink);
            g.setFont (panelFont (10.0f, juce::Font::bold));
            g.drawText(caption, bounds, juce::Justification::centred, false);
        };

        key(resetBounds(), "RESET", palette.raised, palette.text);
        key(closeBounds(), "CLOSE", palette.accent, palette.readout);
    }

    /** A ColourSelector that reports every drag, rather than only on release.

        juce::ColourSelector is a ChangeBroadcaster and nothing more, so a listener is
        the documented way in; subclassing it here keeps the listener, the callback and
        the selector in the one object the call-out owns - and stops the pane holding a
        listener per row that would outlive the box it was made for. */
    class LiveSelector final : public juce::ColourSelector, private juce::ChangeListener {
      public:
        std::function<void(juce::Colour)> onColour;

        explicit LiveSelector(juce::Colour start)
            : juce::ColourSelector(juce::ColourSelector::showColourAtTop
                                   | juce::ColourSelector::editableColour
                                   | juce::ColourSelector::showSliders
                                   | juce::ColourSelector::showColourspace) {
            setCurrentColour(start, juce::dontSendNotification);
            addChangeListener(this);
        }

        ~LiveSelector() override { removeChangeListener(this); }

      private:
        void changeListenerCallback(juce::ChangeBroadcaster *) override {
            if (onColour != nullptr)
                onColour(getCurrentColour());
        }
    };

    void openSelector(int row) {
        const auto field = themeColourFields[row];
        auto selector = std::make_unique<LiveSelector>(palette.*(field.member));

        juce::Component::SafePointer<UserThemePane> safeThis(this);
        selector->onColour = [safeThis, field](juce::Colour chosen) {
            // getComponent() rather than a comparison against nullptr: a SafePointer
            // converts to its pointer type, so `safeThis == nullptr` is two candidate
            // operators on some compilers and a question with one answer on none.
            auto* pane = safeThis.getComponent();
            if (pane == nullptr)
                return;

            pane->palette.*(field.member) = chosen;
            pane->repaint();
            pane->notify();
        };

        selector->setSize(320, 320);

        // Pointed at the swatch's SCREEN area, with no parent, so the box floats over
        // the desktop beside a window that is itself on the desktop.
        juce::CallOutBox::launchAsynchronously(std::move(selector),
                                               localAreaToGlobal(swatchBounds(row)), nullptr);
    }

    UiPalette palette;
    juce::String background;
    int hoveredRow = -1;
    int hoveredPreset = -1;
    int hoveredBackdrop = -1;
    bool patinaEnabled = false;
    bool hoveredPatina = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UserThemePane)
};

//==============================================================================
/**
    The window's icon, drawn when this build carries no brand mark.

    A DocumentWindow with no icon hands the title bar and the taskbar whatever the
    peer supplies, which on a bare JUCE peer is a blank square - the "white icon"
    this window used to open with. Drawing one costs a 32 px image and means the
    window has a face in every build, art pack or not; Assets/window_icon.png
    replaces it when it exists.
*/
inline juce::Image defaultWindowIcon (const UiPalette &palette, int side = 32)
{
    juce::Image icon (juce::Image::ARGB, side, side, true);
    juce::Graphics g (icon);

    const auto area = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (side),
                                              static_cast<float> (side)).reduced (1.0f);

    g.setColour (palette.panel);
    g.fillRoundedRectangle (area, static_cast<float> (side) * 0.18f);
    g.setColour (palette.border.withAlpha (0.9f));
    g.drawRoundedRectangle (area, static_cast<float> (side) * 0.18f, 1.0f);

    // The reel: an accent ring, a hub, and the three spokes that make it read as a
    // reel at 16 px rather than as a letter O.
    const auto circle = area.reduced (static_cast<float> (side) * 0.18f);
    g.setColour (palette.accent);
    g.drawEllipse (circle, juce::jmax (1.2f, static_cast<float> (side) * 0.07f));
    const auto hubOffset = static_cast<float> (side) * 0.06f;

    g.fillEllipse (circle.getCentreX() - hubOffset, circle.getCentreY() - hubOffset,
                   static_cast<float> (side) * 0.12f, static_cast<float> (side) * 0.12f);

    return icon;
}

//==============================================================================
/**
    The window itself: a title bar, the pane, and nothing else.

    A desktop window rather than a child of the editor, because a plugin editor is
    exactly as large as the host lets it be, and because a colour editor you can drag
    beside the panel and still see the panel is the whole point.

    Closing it HIDES it. The editor owns it and keeps it for the session, showing it
    again on the next press: rebuilding fifteen rows on every open would be work for
    nothing, and a window that deleted itself on close would have to be rebuilt from
    a pointer the editor had just been told was gone.

    OWNERSHIP. The pane is handed to juce::ResizableWindow with setContentOwned, so
    the window deletes it - the other way round (a member unique_ptr) destroys the
    content BEFORE the base class, and the DocumentWindow's own destructor would then
    touch a component that no longer exists. That ordering bug is the reason this
    class holds a raw pointer rather than a smart one.
*/
class UserThemeWindow final : public juce::DocumentWindow {
  public:
    /** "The palette changed" - set once by the editor. */
    std::function<void(const UiPalette &)> onPaletteChanged;

    /** "The backdrop changed", with the entry's name - set once by the editor. */
    std::function<void(const juce::String &)> onBackgroundChanged;

    /** "The patina setting changed" - set once by the editor. */
    std::function<void(bool)> onPatinaChanged;

    explicit UserThemeWindow(UiPalette start,
                             juce::String background = "THEME",
                             bool patinaEnabled = false) :
        juce::DocumentWindow(
            "USER THEME", start.background, juce::DocumentWindow::closeButton, true) {
        auto content = std::make_unique<UserThemePane>(start, std::move(background), patinaEnabled);
        pane = content.get();

        pane->onPaletteChanged = [this](const UiPalette &edited) {
            if (onPaletteChanged != nullptr)
                onPaletteChanged(edited);
        };
        pane->onBackgroundChanged = [this](const juce::String &edited) {
            if (onBackgroundChanged != nullptr)
                onBackgroundChanged(edited);
        };
        pane->onPatinaChanged = [this](bool enabled) {
            if (onPatinaChanged != nullptr)
                onPatinaChanged(enabled);
        };
        pane->onClose = [this] { setVisible(false); };

        pane->setSize(userThemeWidth, userThemePaneHeight);

        setUsingNativeTitleBar(true);

        // The window is sized EXPLICITLY from the pane, and not left to the
        // resize-to-fit path: a DocumentWindow exists on the desktop from the
        // moment it is constructed, and if the fit ever fails to run the user is
        // looking at the peer's empty content area - a small blank window with a
        // close button. That is the "it opens as a white icon" report. A window
        // that says its own size cannot be left blank by an ordering detail.
        setContentOwned(content.release(), false);
        setContentComponentSize(userThemeWidth, userThemePaneHeight);
        setResizable(false, false);
        // setIcon() covers JUCE-drawn title bars; this window uses the native title
        // bar, so also pass the image to the desktop peer for Windows' title bar and
        // taskbar. Without the peer call Windows shows its generic blank-file icon.
        if (const auto &art = j37panel::assets::cachedImage ("window_icon_png"); art.isValid())
            setWindowIcon (art);
        else
            setWindowIcon (defaultWindowIcon (start));
    }

    ~UserThemeWindow() override { pane = nullptr; }

    void setPalette(const UiPalette &next) { pane->setPalette(next); }

    void setBackground(const juce::String &next) { pane->setBackground(next); }

    void setPatinaEnabled(bool enabled) { pane->setPatinaEnabled(enabled); }

    void show() {
        setVisible(true);
        toFront(true);
    }

    void closeButtonPressed() override { setVisible(false); }

  private:
    void setWindowIcon (const juce::Image& icon)
    {
        setIcon (icon);

        if (auto* peer = getPeer())
            peer->setIcon (icon);
    }

    UserThemePane *pane = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UserThemeWindow)
};

} // namespace j37panel
