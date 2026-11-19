/*  ============================================================================
    The optional art loader.

    One rule: NO ASSET IS REQUIRED. Every call site asks for an image and draws
    its own panel when the answer is invalid, so a checkout with an empty Assets/
    directory, a stripped binary, or a build where the resources target was not
    linked behaves exactly like the panel before this file existed. That is what
    makes the art additive rather than a new dependency.

    WHERE THE BYTES COME FROM. CMake globs every file in Assets/ into the same
    juce_add_binary_data target as the presets and the translation tables, so the
    files are inside the plugin - no install step, no search path, nothing for a
    user to lose. BinaryData::getNamedResource takes the generated SYMBOL name
    (the file name with '.' turned into '_'), which is why PanelAssetSlots.h has a
    `symbol` column next to the file name rather than deriving one at run time.

    THREADING. The cache is a plain map, deliberately: every draw site on this
    panel runs on the message thread, and the one thing that must not happen is
    for the audio thread to touch juce::Image. Nothing here is called from it.

    MEMORY. An absent slot costs one failed lookup and an invalid Image in the
    cache - there is no retry loop and no per-frame decode. A present slot is
    decoded once.
  ============================================================================ */
#pragma once

#include <JuceHeader.h>
#include <BinaryData.h>

#include "GUI/PanelAssetSlots.h"

#include <map>

namespace j37panel::assets {

/** Decodes a slot from the binary data. Empty when the file was never embedded. */
inline juce::Image loadImage (const char* symbol)
{
    int size = 0;
    const auto* data = BinaryData::getNamedResource (symbol, size);

    if (data == nullptr || size <= 0)
        return {};

    return juce::ImageFileFormat::loadFrom (data, static_cast<size_t> (size));
}

/** The slot's image, decoded once and kept for the session. Message thread only. */
inline const juce::Image& cachedImage (const char* symbol)
{
    static std::map<juce::String, juce::Image> cache;

    const auto key = juce::String (symbol);
    const auto found = cache.find (key);

    if (found != cache.end())
        return found->second;

    return cache.emplace (key, loadImage (symbol)).first->second;
}

/** True when this build carries the slot's file. */
inline bool available (const char* symbol)
{
    return cachedImage (symbol).isValid();
}

/** A vector/Drawable form for SVG-style art, when a slot is ever added for it.
    Kept beside loadImage so the two loaders stay one file rather than two. */
inline std::unique_ptr<juce::Drawable> loadDrawable (const char* symbol)
{
    int size = 0;
    const auto* data = BinaryData::getNamedResource (symbol, size);

    if (data == nullptr || size <= 0)
        return {};

    return juce::Drawable::createFromImageData (data, static_cast<size_t> (size));
}

//==============================================================================
/** Which art slot the chassis surface is painted from.

    "THEME" is the default and means what the panel always did: the theme-indexed
    tile out of panel_materials, then the generic texture, then the arithmetic
    grain. A named slot paints the whole face from that one image. "DRAWN" skips
    the art entirely and keeps the grain, which is what a build with no Assets/
    directory shows already - so the list can never offer a choice the panel
    cannot draw.

    A null `symbol` means "no asset" and an empty one means "the theme's own
    material": every reader goes through backgroundChoiceFor(), so the COLOURS
    window that offers the names and the paint path that honours one cannot
    disagree about what an entry means.
*/
struct BackgroundChoice
{
    const char* name;
    const char* symbol;
};

inline constexpr BackgroundChoice backgroundChoices[] = {
    { "THEME", "" },
    { "PANEL", "panel_texture_png" },
    { "DETAIL", "hardware_details_png" },
    { "DRAWN", nullptr },
};

inline constexpr int backgroundChoiceCount =
    static_cast<int> (sizeof (backgroundChoices) / sizeof (backgroundChoices[0]));

/** The entry with this name; an unknown name - including the empty string a
    settings file that never stored one yields - means THEME. */
inline const BackgroundChoice& backgroundChoiceFor (const juce::String& name)
{
    for (const auto& choice : backgroundChoices)
        if (name.equalsIgnoreCase (choice.name))
            return choice;

    return backgroundChoices[0];
}

/** The live choice. Read by rebuildPanelTextureLayers(); written by the COLOURS
    window through the editor, which also remembers it in the preferences file. */
inline juce::String activeBackground = "THEME";

} // namespace j37panel::assets
