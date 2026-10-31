/*  ============================================================================
    The panel's optional art slots, in one table.

    WHY THIS EXISTS. The panel is drawn from code: every knob, plate, screw and
    grain line is arithmetic and palette colours. That is what makes it scale and
    theme, and it is also why a set of hand-drawn textures and marks can be added
    later without touching any of the drawing - as long as the build, the loader
    and the documentation all agree on WHAT a file is called and WHERE it is used.

    This header is that agreement. It is JUCE-free so tests/assets/check_assets.py
    can read it without a compiler, and Source/GUI/PanelAssets.h is the only code
    that turns a slot into a juce::Image.

    ADDING A SLOT is three things:
      1. a row in the table below (name, file, symbol, size, purpose);
      2. one call at the place that draws it - assets::cachedImage (slot.symbol);
      3. the file dropped into Assets/. Nothing is fetched, nothing is generated,
         and a build with no Assets/ directory behaves exactly as before, because
         every call site falls back to the drawn panel when the image is invalid.

    WHY PNG AND NOT SVG HERE: these are surfaces (grain, wear, a mark), not icons
    that must stay crisp at any size. Vector art belongs in Assets/ as .svg
    files, loaded with juce::Drawable, which is a different loader and row type; the
    pipeline for it is described in Assets/README.md rather than half-built here.
  ============================================================================ */
#pragma once

#include <cstring>

namespace j37panel::assets {

struct Slot
{
    const char* name;    // what the documentation and the manifest call it
    const char* file;    // the file name in Assets/
    const char* symbol;  // the BinaryData symbol name (dots become underscores)
    int width;           // recommended pixel size; not enforced, but checked when present
    int height;
    const char* purpose;
};

inline constexpr Slot slots[] = {
    { "panel_texture", "panel_texture.png", "panel_texture_png", 1024, 512,
      "The chassis surface: grain, wear, faint print marks. Drawn over the whole "
      "editor at half resolution, UNDER the controls and OVER the palette." },

    { "brand_mark", "brand_mark.png", "brand_mark_png", 256, 256,
      "The product mark: a reel, a monogram, a drawn logo. Drawn in the USER THEME "
      "window's header today; it is the one slot meant to be read as a signature." },

    { "window_icon", "window_icon.png", "window_icon_png", 64, 64,
      "The title-bar and taskbar icon of the USER THEME window. Square, and it "
      "should read at 16 px." },
};

inline constexpr int slotCount = static_cast<int> (sizeof (slots) / sizeof (slots[0]));

/** The slot with this name, or nullptr. Used by the loader's assertion and by
    the assets check; the draw sites name their slot by its symbol directly. */
inline const Slot* findSlot (const char* name)
{
    for (int i = 0; i < slotCount; ++i)
        if (std::strcmp (slots[i].name, name) == 0 || std::strcmp (slots[i].file, name) == 0)
            return &slots[i];

    return nullptr;
}

} // namespace j37panel::assets
