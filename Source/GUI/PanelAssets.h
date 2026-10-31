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

} // namespace j37panel::assets
