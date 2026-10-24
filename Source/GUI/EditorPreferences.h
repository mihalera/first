/*
  ============================================================================
    EditorPreferences - the editor's MACHINE-WIDE preferences.

    Not the session. What lives here describes the COMPUTER rather than the mix:
    the folders the file pickers reopen in, and the renderer the panel was last
    told to draw with. Those have to outlive an editor (a host closes and
    re-creates one), they must not travel in a session to a machine that cannot
    honour them, and a preset must never be able to change them.

    ONE PropertiesFile for all of them, deliberately. Two `static PropertiesFile`
    objects for the same file are two caches of one file, and each save writes its
    own cache over the other's keys - so a second accessor is not a convenience,
    it is a way to lose the first one's settings. The function-local static in an
    inline function is one instance across every translation unit.

    The renderer choice is a string rather than an enum ordinal so the file stays
    readable and an enum reordering cannot silently select a different backend.
  ============================================================================
*/

#pragma once

#include "GUI/RenderBackend.h"
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

} // namespace j37panel
