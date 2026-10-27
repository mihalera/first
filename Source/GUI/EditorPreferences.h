/*
  ============================================================================
    EditorPreferences - the editor's MACHINE-WIDE preferences.

    Not the session. What lives here describes the COMPUTER rather than the mix:
    the folders the file pickers reopen in, the renderer the panel was last told
    to draw with, and the front panel it was left on - the theme choice and the
    USER palette the COLOURS window edits. Those have to outlive an editor (a host
    closes and re-creates one), they must not travel in a session to a machine
    that cannot honour them, and a preset must never be able to change them.

    The theme choice is what the USER panel needed to survive a close and reopen:
    the editor's own member defaults to IVORY, so a host that destroyed and
    re-created the editor dropped a desk that had been left on USER back onto
    IVORY. Remembering it here is the same answer the renderer already had.

    ONE PropertiesFile for all of them, deliberately. Two `static PropertiesFile`
    objects for the same file are two caches of one file, and each save writes its
    own cache over the other's keys - so a second accessor is not a convenience,
    it is a way to lose the first one's settings. The function-local static in an
    inline function is one instance across every translation unit.

    The renderer and the theme are stored as NAMES rather than enum ordinals so
    the file stays readable and an enum reordering cannot silently select a
    different backend or panel.
  ============================================================================
*/

#pragma once

#include "GUI/RenderBackend.h"

// ThemeChoice, UiPalette and the seed palette live in PanelPaint.h; the field table
// that (de)serialises a palette is UserThemeWindow.h's. The theme and the USER
// palette are preferences of the same kind as the renderer - which panel the desk is
// wearing, not what the mix is - so their store belongs in this file.
#include "GUI/UserThemeWindow.h"

#include <JuceHeader.h>

namespace j37panel
{
/** The editor's one machine-wide preferences file. */
inline juce::PropertiesFile& editorPreferences()
{
    static juce::PropertiesFile settings ([] {
        juce::PropertiesFile::Options options;
        options.applicationName      = "MiBoX";
        options.folderName           = "MiBoX";
        options.filenameSuffix       = "settings";
        options.osxLibrarySubFolder  = "Application Support";
        options.commonToAllUsers     = false;
        options.ignoreCaseOfKeyNames = true;
        return options;
    }());

    return settings;
}

constexpr const char* renderBackendKey = "render_backend";

/** The renderer last chosen in SETTINGS, or AUTO when nothing is stored, the
    stored name is unknown, or the backend is not selectable in THIS build. The
    last case matters: a machine updated to a build that stages a different
    adapter must fall back to AUTO rather than keep asking for a path the build
    no longer has. */
inline j37::render::Backend rememberedRenderBackend()
{
    const auto stored = editorPreferences().getValue (renderBackendKey);

    if (stored.isEmpty())
        return j37::render::Backend::autoDetect;

    for (const auto backend : {j37::render::Backend::autoDetect,
                               j37::render::Backend::openGL,
                               j37::render::Backend::cpu})
    {
        if (stored.equalsIgnoreCase (j37::render::backendName (backend))
            && j37::render::isBackendSelectable (backend))
            return backend;
    }

    return j37::render::Backend::autoDetect;
}

/** Stores a renderer the build can actually honour. A PENDING adapter is not
    written: the panel cannot run it, so remembering it would be remembering a
    wish and it would come back as a selection that does nothing. */
inline void rememberRenderBackend (j37::render::Backend backend)
{
    if (! j37::render::isBackendSelectable (backend))
        return;

    editorPreferences().setValue (renderBackendKey, j37::render::backendName (backend));
    editorPreferences().saveIfNeeded();
}

/** The front panel the desk was last left on. Stored by NAME, for the same reason the
    renderer is: the file stays readable and inserting a theme into ThemeChoice (as
    WHITE MONSTER once was) cannot silently select a different one. */
constexpr const char* panelThemeKey = "panel_theme";

inline const char* themeChoiceName (j37panel::ThemeChoice theme) noexcept
{
    switch (theme)
    {
        case j37panel::ThemeChoice::charcoal:     return "CHARCOAL";
        case j37panel::ThemeChoice::metal:        return "METAL";
        case j37panel::ThemeChoice::darkMetal:    return "DARK METAL";
        case j37panel::ThemeChoice::whiteMonster: return "WHITE MONSTER";
        case j37panel::ThemeChoice::user:         return "USER";
        case j37panel::ThemeChoice::ivory:
        default:                                  return "IVORY";
    }
}

inline j37panel::ThemeChoice themeChoiceFromName (const juce::String& name) noexcept
{
    // Nothing stored (a fresh install) and a name this build no longer knows (a theme
    // removed by a later version) mean the same thing: open on the default panel.
    for (const auto theme : {j37panel::ThemeChoice::ivory,
                             j37panel::ThemeChoice::charcoal,
                             j37panel::ThemeChoice::metal,
                             j37panel::ThemeChoice::darkMetal,
                             j37panel::ThemeChoice::whiteMonster,
                             j37panel::ThemeChoice::user})
    {
        if (name.equalsIgnoreCase (themeChoiceName (theme)))
            return theme;
    }

    return j37panel::ThemeChoice::ivory;
}

/** The panel the last editor was left on, or IVORY before anything is stored. */
inline j37panel::ThemeChoice rememberedThemeChoice()
{
    return themeChoiceFromName (editorPreferences().getValue (panelThemeKey));
}

/** Stores the panel the user just selected. */
inline void rememberThemeChoice (j37panel::ThemeChoice theme)
{
    editorPreferences().setValue (panelThemeKey, themeChoiceName (theme));
    editorPreferences().saveIfNeeded();
}

/** The USER palette, remembered so a desk left on USER comes back on the colours it
    was left on rather than on the seed. Its own key, so the theme choice and the
    colours cannot overwrite each other. */
constexpr const char* userPaletteKey = "user_palette";

inline j37panel::UiPalette rememberedUserPalette()
{
    const auto stored = editorPreferences().getValue (userPaletteKey);

    return stored.isEmpty() ? j37panel::userThemeSeed
                            : j37panel::deserializeUserPalette (stored, j37panel::userThemeSeed);
}

inline void rememberUserPalette (const j37panel::UiPalette& palette)
{
    editorPreferences().setValue (userPaletteKey, j37panel::serializeUserPalette (palette));
    editorPreferences().saveIfNeeded();
}

} // namespace j37panel
