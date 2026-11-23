# MiBoX optional artwork

The panel stays complete when this directory is empty. JUCE decodes an image only
when its draw site asks for it, caches it on the message thread, and keeps the
current vector or palette drawing when the image is absent. No asset is used by the
audio callback.

The slot names, dimensions, and fallback contracts live in
[`Source/GUI/PanelAssetSlots.h`](../Source/GUI/PanelAssetSlots.h). Licensing and
provenance live in [`manifest.json`](manifest.json). Stable atlas keys and tile
indices live in [`asset_catalog.json`](asset_catalog.json). The full integration
plan is [`docs/asset-plan.md`](../docs/asset-plan.md).

## Shipped slots

| File | Size | Cells | Use |
| --- | ---: | ---: | --- |
| `panel_texture.png` | 1024 × 512 | 1 | Generic chassis grain if the material atlas is missing. |
| `panel_materials.png` | 3072 × 256 | 6 × 512 × 256 | Theme surfaces in `ThemeChoice` order. |
| `brand_mark.png` | 256 × 256 | 1 | USER THEME window signature; the drawn reel remains the fallback. |
| `window_icon.png` | 64 × 64 | 1 | USER THEME native window icon; the pane can draw its own icon. |
| `principle_icons.png` | 576 × 64 | 9 × 64 × 64 | MIX source-family glyphs. |
| `factory_preset_art.png` | 2240 × 64 | 35 × 64 × 64 | Current factory preset badge, ordered by CMake's preset list. |
| `ui_icons.png` | 512 × 832 | 104 × 64 × 64 | Navigation, family and workflow glyph atlas. |
| `parameter_glyphs.png` | 512 × 832 | 100 × 64 × 64 | Parameter glyphs in `controlSpecs` order. |
| `machine_models.png` | 512 × 512 | 52 × 64 × 64 | Model glyphs generated from live processor name tables. |
| `meter_status.png` | 512 × 256 | 32 × 64 × 64 | Meter, signal and health glyphs. |
| `preset_categories.png` | 512 × 64 | 8 × 64 × 64 | Preset browser categories. |
| `hardware_details.png` | 256 × 256 | 16 × 64 × 64 | Small panel hardware glyphs. |
| `brand_lockups.png` | 512 × 128 | 4 × 128 × 128 | Product mark arrangements. |
| `help_illustrations.png` | 1024 × 576 | 4 × 512 × 288 | Help and quick-start scenes. |
| `signal_path.png` | 1600 × 500 | Standalone | Full signal-flow illustration for `docs/signal-path.md`. |
| `knob_caps.png` | 512 × 256 | 8 × 128 × 128 | Static knob faces; JUCE draws the camera, shadow, ticks and pointer. |
| `level_scale.png` | 512 × 256 | 1 × 512 × 256 | Transparent -20 to +3 dB scale for the AVG-driven dial needle. |
| `deck_plate.png` | 2048 × 512 | 1 × 2048 × 512 | Transparent calibration silkscreen inside the transport deck. |
| `rack_pedals.png` | 2048 × 512 | 4 × 512 × 512 | Low-contrast backgrounds in DRIVE, SHAPE, MOTION, PITCH order. |
| `tape_stock_swatches.png` | 1014 × 128 | 13 × 78 × 128 | Selected tape label in `tapeStockNames` order; Off is last. |
| `tube_glow_maps.png` | 512 × 256 | 4 × 128 × 256 | Decorative heater marks on valve source knobs. Brightness follows the visible control amount. |
| `vu_needle_profile.png` | 256 × 64 | 1 × 256 × 64 | Rotated AVG/VU needle and shadow; the vector needle is the fallback. |
| `wear_and_patina.png` | 1024 × 1024 | 1 × 1024 × 1024 | Seamless sparse wear texture; enabled in USER THEME and remembered locally. |
| `spindown_blur.png` | 256 × 256 | 1 × 256 × 256 | Motion streak overlay for the software reel; it fades with transport speed. |

The 18 indexed atlases contain 389 named cells. `asset_catalog.json` records their
grid sizes, stable keys, and source ordering. The current source-derived dimensions
are intentional: for example, eight 128 px knob caps in four columns require a
512 × 256 image, and thirteen 78 px tape swatches require 1014 px of width.

## Generation and checks

`generate_asset_pack.py` creates the source-indexed glyph, preset and help atlases,
the nine new hardware and meter images, and `signal_path.png`. It reads live tab,
parameter, model, tape-stock, and factory-preset tables where order matters. Pillow
is used only by this authoring script; it is not a build or runtime dependency. The
five original identity/material sheets are kept as source artwork and are not
overwritten by this generator.

```sh
python Assets/generate_asset_pack.py
python tests/assets/check_assets.py
python tests/dependencies/check_markdown_links.py
```

CMake embeds `Assets/*.png` as binary data. If new files are added to an existing
build tree, CMake's `CONFIGURE_DEPENDS` glob causes the project to reconfigure.
Every slot is CC0-1.0 original project artwork, as recorded in `manifest.json`.
Do not add third-party art without clear compatible licensing and provenance.
