# MiBoX asset plan

## Status

The asset plan is implemented. MiBoX now has 24 optional PNG slots, 18 indexed
atlases with 389 named cells, and one standalone signal-flow illustration. The
nine new images are generated as original CC0 project artwork by
[`Assets/generate_asset_pack.py`](../Assets/generate_asset_pack.py). Each runtime
draw site keeps its existing vector or palette-based rendering when its image is
missing.

The durable source of truth is:

- [`Source/GUI/PanelAssetSlots.h`](../Source/GUI/PanelAssetSlots.h) for names,
  symbols, dimensions, and use contracts;
- [`Assets/manifest.json`](../Assets/manifest.json) for provenance and licence;
- [`Assets/asset_catalog.json`](../Assets/asset_catalog.json) for atlas grids,
  keys, and stable tile order.

## System rules

1. **No asset is required.** A clean checkout or build without any PNG still has
   the complete functional interface.
2. **Art stays out of the audio callback.** `cachedImage()` is a message-thread
   cache; no processor or DSP code may call it.
3. **Dynamic state stays dynamic.** JUCE draws moving pointers, knob indicators,
   labels, transport state, and user text. Static images provide surface and
   material detail only.
4. **Source tables own order.** Tape swatches follow `tapeStockNames`, rack pedals
   follow the four RACK page names, parameter glyphs follow `controlSpecs`, and
   factory art follows CMake's `FACTORY_PRESET_FILES`.
5. **Theme and patina are machine preferences.** They do not change plugin
   parameters, host automation, presets, or saved processor state.
6. **All embedded artwork needs clear provenance.** The current art is original
   CC0-1.0 work. Third-party art needs an explicitly compatible licence before it
   can enter a plugin binary.

The slot loader decodes images on first use. If every declared image is decoded
as RGBA, the upper bound is about **30.94 MiB**. Actual use is lower because the
cache is lazy and several documentation and product images are not opened by the
editor.

## Shipped inventory

| Tier | Slot | Size | Cells | Runtime use |
| --- | --- | ---: | ---: | --- |
| 1 | `panel_texture` | 1024 × 512 | 1 | Generic chassis grain fallback in `EditorPaint.cpp`. |
| 1 | `panel_materials` | 3072 × 256 | 6 × 512 × 256 | Theme surface atlas in `ThemeChoice` order. |
| 1 | `brand_mark` | 256 × 256 | 1 | USER THEME header; drawn emblem fallback. |
| 1 | `window_icon` | 64 × 64 | 1 | USER THEME window and desktop peer; drawn emblem fallback. |
| 1 | `brand_lockups` | 512 × 128 | 4 × 128 × 128 | Brand variants for product surfaces and documentation. |
| 1 | `ui_icons` | 512 × 832 | 104 × 64 × 64 | Page, family, control, workflow, status and platform glyphs. |
| 1 | `principle_icons` | 576 × 64 | 9 × 64 × 64 | MIX bar source glyphs, tinted with the active palette. |
| 2 | `parameter_glyphs` | 512 × 832 | 100 × 64 × 64 | Named glyphs in current `controlSpecs` order. |
| 2 | `machine_models` | 512 × 512 | 52 × 64 × 64 | Tape, circuit, digital and vinyl model glyph library. |
| 2 | `meter_status` | 512 × 256 | 32 × 64 × 64 | Meter, signal, phase, band and health glyph library. |
| 2 | `hardware_details` | 256 × 256 | 16 × 64 × 64 | Small mechanical detail glyph library. |
| 3 | `factory_preset_art` | 2240 × 64 | 35 × 64 × 64 | Selected factory preset badge in CMake preset order. |
| 3 | `preset_categories` | 512 × 64 | 8 × 64 × 64 | Preset category glyph library. |
| 3 | `signal_path` | 1600 × 500 | Standalone | Full signal path diagram in `docs/signal-path.md`. |
| 4 | `help_illustrations` | 1024 × 576 | 4 × 512 × 288 | Signal, A/B, preset and file-loading help scenes. |
| 5 | `knob_caps` | 512 × 256 | 8 × 128 × 128 | Static face material for every rotary; pointer and ticks stay dynamic. |
| 5 | `level_scale` | 512 × 256 | 1 × 512 × 256 | Transparent -20 to +3 scale for the AVG-driven needle. |
| 5 | `vu_needle_profile` | 256 × 64 | 1 × 256 × 64 | Shadowed moving pointer; vector line fallback. |
| 5 | `deck_plate` | 2048 × 512 | 1 × 2048 × 512 | Transparent calibration detail over the transport deck. |
| 6 | `rack_pedals` | 2048 × 512 | 4 × 512 × 512 | Distinct low-contrast background for each RACK page. |
| 6 | `tape_stock_swatches` | 1014 × 128 | 13 × 78 × 128 | Selected tape label beside the live combo value. |
| 6 | `tube_glow_maps` | 512 × 256 | 4 × 128 × 256 | Decorative heater marks driven by the visible slider amount. |
| 6 | `wear_and_patina` | 1024 × 1024 | 1 × 1024 × 1024 | Seamless optional surface wear, enabled in USER THEME. |
| 6 | `spindown_blur` | 256 × 256 | 1 × 256 × 256 | Software reel motion overlay, faded by smoothed transport speed. |

## New art and integration contracts

### Knob caps

`knob_caps.png` is a four-column by two-row atlas of 128 px faces. The stable
order is bakelite, knurled aluminium, aged ivory, soft rubber, chrome, oxblood
pedal, cast steel, and clear acrylic. Stock themes select aged ivory, bakelite,
knurled aluminium, cast steel, chrome, and acrylic respectively. The four RACK
pages use oxblood, rubber, knurled aluminium, and acrylic.

`J37LookAndFeel::drawRotarySlider()` continues to draw the hardware collar,
camera squeeze, scale ticks, shadow, pointer, and tracer. The raster face replaces
only the static top surface. If the atlas is invalid, the existing full procedural
dome remains visible.

### Level scale and pointer

The LEVELS dial shows the existing equal-weight average of PEAK, RMS, LUFS, and
VU. Its pointer is therefore an **AVG / VU-style summary**, not a peak needle.
`level_scale.png` has a single transparent -20 to +3 scale. It deliberately has no
second -60 to 0 peak scale, because the component has no second peak pointer.
The four exact measurements remain in their own readout rows.

`vu_needle_profile.png` is rotated around its hub to the live displayed AVG value.
If it is missing, the current palette-coloured vector needle is drawn. If the
scale image is missing, the current adaptive tick and label drawing is used.

### Deck and RACK surfaces

`deck_plate.png` contains only transparent silk-screen details. It is drawn over
the deck's existing theme texture and clipped to `layout.deck`, before child
controls paint. The transportation buttons, labels, selectors, and readouts stay
in code.

`rack_pedals.png` has four 512 px cells ordered DRIVE, SHAPE, MOTION, PITCH. The
active cell is drawn behind controls in `layout.controls`. Low alpha keeps the
knobs, values, and headings readable. When the file is absent, the themed panel
background already in place is the fallback.

### Tape swatches

Swatches are 78 × 128 px and follow the live parameter table exactly:

`J37`, `Ampex 456`, `Studer A800`, `Chrome`, `Type 111`, `GP9`, `Quantegy 499`,
`RTM SM911`, `SM 468`, `888`, `815`, `811`, `Off`.

The selector's stable component ID and one-based selected item ID choose the
image tile. Off remains last so existing parameter and preset indices do not
change. The combo still displays its normal text, and a missing image only removes
the little label swatch.

### Tube glow and patina

The four tube drawings decorate the valve, pentode, triode, and DRIVE controls.
Their brightness comes from JUCE's visible normalized slider position. It does
not inspect audio envelope, processor internals, or DSP state, so the editor/DSP
boundary remains intact. Underlying cap art remains complete when the glow atlas is
missing.

The seamless wear texture is optional and starts disabled. USER THEME contains a
PANEL PATINA toggle. Its value lives in the editor's machine-wide preferences and
is tiled over panel background layers at 8% paint opacity. It does not enter host
state or presets. The panel's clean appearance is the fallback when the image is
missing.

### Spindown blur

The radial streak image is used only for the software transport reel when the
native tape scene is not active. It fades with `smoothedMachineSpeed`; existing
transport animation and invalidation already repaint the region. The native scene
renderer and audio engine do not use or depend on this image.

## Authoring and validation

`Assets/generate_asset_pack.py` uses Python and Pillow to create the 19 generated
PNG sheets and the asset catalog. It reads the live source tables where cell order
is a compatibility contract. Pillow is not a CMake, plugin, or runtime dependency.
The five hand-maintained identity sheets are not rewritten by the generator.

Run the narrow checks after changing assets or their draw sites:

```sh
python Assets/generate_asset_pack.py
python tests/assets/check_assets.py
python tests/dependencies/check_markdown_links.py
python tests/theme/check_theme_wiring.py
```

The asset check verifies the slot/manifest/catalog/file agreement, PNG dimensions
and alpha, license metadata, live tape and rack ordering, and the optional draw
sites and fallbacks. The build embeds every PNG through the existing
`Assets/*.png` CMake glob; no new dependency or platform install step is needed.

Before distribution, follow [`release-checklist.md`](release-checklist.md). A local
build does not grant permission to distribute artwork or binaries.

## Completion roadmap

- [x] **Phase 1 — identity and foundation:** existing materials, marks, icons,
  glyphs, preset artwork, help scenes, and signal path.
- [x] **Phase 2 — controls and metering:** knob caps, AVG/VU scale, moving needle.
- [x] **Phase 3 — machine surfaces:** deck plate, four RACK treatments, tape swatches.
- [x] **Phase 4 — atmosphere:** tube glow, optional patina, software reel blur.

The system is complete. Future visual work should add a new slot only when a real
draw site, a stable cell order where needed, and a missing-image fallback can be
specified together.
