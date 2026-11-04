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

    WHY PNG HERE: the panel materials are raster art; six surfaces share one
    atlas, seven small alpha glyphs share another, and the factory preset artwork
    shares a third. All use one cached loader. Art needing unlimited scale can use
    the Drawable loader from the README.
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
      "The generic chassis texture retained as a fallback when the theme-indexed "
      "panel_materials atlas is absent." },

    { "panel_materials", "panel_materials.png", "panel_materials_png", 3072, 256,
      "Six 512 px wide theme materials in ThemeChoice order: ivory paper, charcoal "
      "bakelite, brushed steel, anodized graphite, white enamel, user-neutral fibre." },

    { "brand_mark", "brand_mark.png", "brand_mark_png", 256, 256,
      "The product mark: a reel, a monogram, a drawn logo. Drawn in the USER THEME "
      "window's header today; it is the one slot meant to be read as a signature." },

    { "window_icon", "window_icon.png", "window_icon_png", 64, 64,
      "The title-bar and taskbar icon of the USER THEME window. Square, and it "
      "should read at 16 px." },

    { "principle_icons", "principle_icons.png", "principle_icons_png", 448, 64,
      "Seven monochrome alpha glyphs for the MIX bar, ordered TAPE, VALVE, "
      "CASSETTE, VINYL, AMP, TRANSFORMER, DIGITAL. Tinted by the active palette." },

    { "factory_preset_art", "factory_preset_art.png", "factory_preset_art_png", 1856, 64,
      "Twenty-nine 64 px preset artwork tiles in the exact FACTORY_PRESET_FILES "
      "order from CMakeLists.txt; selected in the deck's current-preset badge." },
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
