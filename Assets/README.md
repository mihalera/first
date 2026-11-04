# Optional panel art

The panel is **drawn from code**: every plate, knob, screw, tick and grain line is
arithmetic and palette colour. That is what makes it scale, theme and stay sharp on
any display. The files in this directory are the **additive** layer — surface art,
product marks and small glyphs that can be added without replacing the drawing.

**Nothing here is required.** A checkout with this directory empty builds and looks
exactly like the current panel: every draw site asks for its slot and falls back to
its own drawing when the image is missing (`Source/GUI/PanelAssets.h`).

## The slots

The single source of truth is the table in
[`Source/GUI/PanelAssetSlots.h`](../Source/GUI/PanelAssetSlots.h); the manifest below
mirrors it for the check and for whoever is sourcing the art.

| File | Size (px) | What it is | Where it is drawn |
| --- | --- | --- | --- |
| `panel_texture.png` | 1024 × 512 | Generic chassis grain retained as a fallback when the theme atlas is missing. Half-resolution layer under the controls. | `rebuildPanelTextureLayers()`, `Source/GUI/EditorPaint.cpp` |
| `panel_materials.png` | 3072 × 256 | Six subtle material tiles, each 512 × 256, in `ThemeChoice` order: ivory paper, charcoal bakelite, brushed steel, anodized graphite, white enamel, neutral user fibre. | Selected by `ThemeChoice` in `rebuildPanelTextureLayers()`, `Source/GUI/EditorPaint.cpp` |
| `brand_mark.png` | 256 × 256 | The product mark: a reel, a monogram, a drawn logo. Read as a signature, not a control. | The USER THEME window's header |
| `window_icon.png` | 64 × 64 | Title-bar / taskbar icon for the USER THEME window. Must read at 16 px. Without it the window draws its own reel mark (`defaultWindowIcon` in the same header), so the corner is never blank. | `UserThemeWindow`, `DocumentWindow::setIcon` |
| `principle_icons.png` | 448 × 64 | Seven 64 px white-on-transparent glyphs, ordered TAPE, VALVE, CASSETTE, VINYL, AMP, TRANSFORMER, DIGITAL. The panel tints each glyph to the active machine colour. | The seven cells in the MIX bar, `Source/GUI/EditorMeters.cpp` |
| `factory_preset_art.png` | 1856 × 64 | Twenty-nine 64 px illustrated tiles, one per factory preset. Its order exactly follows `FACTORY_PRESET_FILES` in `CMakeLists.txt`. | The selected preset's artwork badge, `PresetArtworkBadge` in `Source/GUI/EditorMeters.cpp` |
| `ui_icons.png` | 512 × 832 | 99 named glyphs for the 18 pages, controls, workflows, files, statuses, audio and supported platforms. White strokes and alpha support palette tinting; the three family glyphs appear in the TAPE / FX / SETUP keys. | `J37LookAndFeel.cpp`; remaining sprite indices in `asset_catalog.json` |
| `parameter_glyphs.png` | 512 × 576 | 71 glyphs in `controlSpecs` order, one for every plugin control. | Sprite indices in `asset_catalog.json` |
| `machine_models.png` | 512 × 512 | 57 glyphs from the processor's tape, valve, amp, transformer, digital and vinyl model tables, including OFF entries. | Sprite indices in `asset_catalog.json` |
| `meter_status.png` | 512 × 256 | 32 glyphs for input/output, peak/RMS/LUFS/VU, phase, frequency bands, clipping, latency, CPU and signal states. | Sprite indices in `asset_catalog.json` |
| `preset_categories.png` | 512 × 64 | Mixing, mastering, drums, vocals, bass, guitar, keys and creative category glyphs. | Sprite indices in `asset_catalog.json` |
| `hardware_details.png` | 256 × 256 | 16 small panel details: screws, washers, vents, label plates, jacks, scales, seams, reels and lamps. | Sprite indices in `asset_catalog.json` |
| `brand_lockups.png` | 512 × 128 | Square, horizontal, monochrome and reversed MiBoX marks, each in a 128 px cell. | Product windows and documentation |
| `help_illustrations.png` | 1024 × 576 | Four 512 × 288 scenes for signal flow, A/B comparison, preset workflow and model loading. | User guide and quick-start material |
| `signal_path.png` | 1600 × 500 | Full-width signal-flow diagram showing the major processing stages. | [`docs/signal-path.md`](../docs/signal-path.md) |

PNG with alpha. `panel_texture` and each tile in `panel_materials` should be
**seamless-ish and low contrast**: they are overlaid on a dark or light palette, so
bright features can read as stains. The material tile order is locked to
`ThemeChoice` in `Source/GUI/PanelPaint.h`; keep the enum and the atlas in step.
The marks are artwork; the principle sprite sheet is a white alpha mask, so JUCE
can tint the same seven shapes for every theme. The new icon atlases are indexed
by stable names in [`asset_catalog.json`](asset_catalog.json); all their cells can
be tinted from the alpha channel for normal, active, disabled or high-contrast
states.
The preset artwork atlas has one tile for every entry in CMake's factory preset
list. That list also generates the preset index used by the processor and combo
box, so the badge, selector, and embedded presets share one order. When no factory
preset is active (for example, a fresh or user-edited state), the badge falls back
to `brand_mark.png` and then to a drawn reel symbol.

## Rebuilding the generated pack

`generate_asset_pack.py` recreates the icon atlases, the 29 preset tiles, brand
lockups and help illustrations. It reads the current tab and control names, model
name arrays, and `FACTORY_PRESET_FILES`, then writes their cell names and indices
to `asset_catalog.json`. This keeps the icon order attached to the plugin's own
tables instead of maintaining a second handwritten list. Pillow 10.1 or newer is
needed only to regenerate these PNGs; it is not a build or runtime dependency.

The same catalog records the Android launcher set maintained under
`android/app/src/main/res`: its five density PNGs, adaptive icon definitions,
foreground vector, monochrome vector and package icon reference. Gradle reads
those files directly; CMake embeds only the PNGs in this `Assets/` folder.

```sh
python Assets/generate_asset_pack.py
python tests/assets/check_assets.py
```
=======
=======

PNG with alpha. `panel_texture` and each tile in `panel_materials` should be
**seamless-ish and low contrast**: they are overlaid on a dark or light palette, so
bright features can read as stains. The material tile order is locked to
`ThemeChoice` in `Source/GUI/PanelPaint.h`; keep the enum and the atlas in step.
The marks are artwork; the principle sprite sheet is a white alpha mask, so JUCE
can tint the same seven shapes for every theme.
The preset artwork atlas has one tile for every entry in CMake's factory preset
list. That list also generates the preset index used by the processor and combo
box, so the badge, selector, and embedded presets share one order. When no factory
preset is active (for example, a fresh or user-edited state), the badge falls back
to `brand_mark.png` and then to a drawn reel symbol.

## Licences — read this before committing anything

- **CC0-1.0 or public domain only.** That is the same bar the rest of the tree
  holds itself to; a texture is not worth a licence question in a binary release
  (`docs/release-checklist.md` is the gate for shipping).
- Do **not** commit a file whose licence is missing, "free for personal use",
  non-commercial, or unclear. A file with no provenance is a file that cannot ship.
- Fill in `licence`, `source` and `author` for the slot in
  [`manifest.json`](manifest.json). The check fails on an embedded file with an
  empty licence.
- Inspiration sources that usually clear this bar: Poly Haven (CC0 textures),
  ambientCG (CC0), Kenney (CC0), game-icons.net (CC BY 3.0 — **attribution
  required**, so either credit it in `THIRD_PARTY_NOTICES.md` or redraw), and
  your own drawings.

## Adding a file — the whole workflow

1. Drop the PNG in this directory under the manifest's file name. That is the only
   required step: `CMakeLists.txt` globs `Assets/*.png` into the plugin's binary
   data (`CONFIGURE_DEPENDS`, so a reconfigure picks it up).
2. Fill in `licence`, `source`, `author` for that slot in `manifest.json`.
3. Run the check:

   ```sh
   python3 tests/assets/check_assets.py
   ```

   It compares the manifest, the slot table and the files, and reads the PNG
   header to confirm the dimensions. It passes with an empty directory.
4. Build as usual. The loader caches the image on first use; a missing file costs
   one failed lookup and nothing else.

Want a new slot (a knob face, a per-tab illustration, a preset-badge plate)? Add a
row to `PanelAssetSlots.h`, add the same row to `manifest.json`, and call
`j37panel::assets::cachedImage(slot.symbol)` where it should be drawn — falling back
to the current drawing when the result is invalid. That is the whole extension
point, and it is why "add the art" is one prompt and not a refactor.

## Why this pipeline and not a theme pack on disk

The plugin is a binary that a host loads; a folder next to it gets lost, and a
search path is a support question. The art is embedded in the binary data target
that already carries the presets and the translation tables, and loaded through one
function. There is nothing to install, nothing to lose, and a build with no art is
still a complete plugin.
