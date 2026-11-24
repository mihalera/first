# MiBoX asset plan

## Purpose and current state

Artwork is an optional presentation layer over MiBoX's complete, code-drawn
interface. The original asset rollout is implemented: 24 optional PNG slots,
18 indexed atlases containing 389 named cells, one standalone signal-flow
illustration, and separately catalogued Android launcher resources. Missing PNGs
must leave every control and readout usable through the existing vector,
procedural, or palette-based fallback.

The next work is inventory consistency, reproducible generation, and fallback
coverage—not adding imagery without a named product need.

### Sources of truth
Keep each fact in its owning source; avoid a second manually maintained
inventory when the existing tables already describe it:

- [`Source/GUI/PanelAssetSlots.h`](../Source/GUI/PanelAssetSlots.h): runtime slot
  names, BinaryData symbols, dimensions, purpose, and fallback intent.
- [`Assets/manifest.json`](../Assets/manifest.json): file provenance, licence,
  author, dimensions, and runtime use.
- [`Assets/asset_catalog.json`](../Assets/asset_catalog.json): atlas grids,
  stable semantic keys, tile order, and standalone/platform asset metadata.
- [`Assets/generate_asset_pack.py`](../Assets/generate_asset_pack.py): generated
  art/catalog output and source tables for compatibility-sensitive ordering.
- [`Assets/README.md`](../Assets/README.md): concise contributor guide and slot
  inventory.

If these disagree, identify the owning source, update derived copies, and extend
focused checks. Never silently renumber an index tied to a parameter, preset,
theme, or UI control.

## Non-negotiable constraints

1. **Optional at runtime:** missing art must not disable a control, hide a value,
   or leave a blank panel. Every runtime slot needs a useful fallback.
2. **Never in the audio path:** image decoding, file access, and image caching stay
   outside processor/DSP code and the audio callback. Panel images use the existing
   message-thread `cachedImage()` path.
3. **Static art, live state:** images may provide material, texture, or a fixed
   glyph. JUCE continues to draw changing labels, meters, switches, knob pointers,
   transport state, and user text from live UI data.
4. **Preserve compatibility order:** derive order from live source tables whenever
   possible. For persisted indices, append instead of inserting, then update the
   catalog and checks with the source change.
5. **Keep preferences local:** theme, patina, renderer, and desk settings belong to
   machine-wide editor preferences, not host parameters, presets, or processor state.
6. **Record provenance before embedding:** current project art is documented as
   CC0-1.0. Do not add third-party art with unclear, non-commercial, or incompatible
   terms.
7. **Budget decoded memory:** images are decoded lazily. Decoding all declared
   images as RGBA would use about **30.94 MiB**; any expansion must state its
   decoded-memory cost, intended usage, and scaling behavior. Actual use is lower
   because documentation and product images are not all opened by the editor.

## Shipped inventory

| Tier | Slot | Size | Cells | Runtime use |
| --- | --- | ---: | ---: | --- |
| 1 | `panel_texture` | 1024 × 512 | 1 | Generic chassis grain fallback in `EditorPaint.cpp`. |
| 1 | `panel_materials` | 3072 × 256 | 6 × 512 × 256 | Theme surface atlas in `ThemeChoice` order. |
| 1 | `brand_mark` | 256 × 256 | 1 | USER THEME header; drawn emblem fallback. |
| 1 | `window_icon` | 64 × 64 | 1 | USER THEME window and desktop peer; drawn emblem fallback. |
| 1 | `brand_lockups` | 512 × 128 | 4 × 128 × 128 | Square lockup in the panel header; palette-aware horizontal/reversed lockup in the USER THEME header. |
| 1 | `ui_icons` | 512 × 832 | 104 × 64 × 64 | Page, family, control, workflow, status and platform glyphs. |
| 1 | `principle_icons` | 576 × 64 | 9 × 64 × 64 | MIX bar source glyphs, tinted with the active palette. |
| 2 | `parameter_glyphs` | 512 × 832 | 100 × 64 × 64 | Control caption glyphs in `controlSpecs` order, drawn in `J37LookAndFeel::drawLabel()`; fallback is the text-only caption. |
| 2 | `machine_models` | 512 × 512 | 52 × 64 × 64 | Model-selector glyphs for valve, pentode, triode, transistor, transformer, digital and vinyl boxes; tape keeps its swatch. |
| 2 | `meter_status` | 512 × 256 | 32 × 64 × 64 | Input/output glyph plus red clipping glyph in `LevelMeter::paint()`; fallback is text-only title. |
| 2 | `hardware_details` | 256 × 256 | 16 × 64 × 64 | `hardware.screw` tile in `drawScrew()`; full atlas also available as the USER PANEL "DETAIL" background. |
| 3 | `factory_preset_art` | 2240 × 64 | 35 × 64 × 64 | Selected factory preset badge in CMake preset order. |
| 3 | `preset_categories` | 512 × 64 | 8 × 64 × 64 | Instrument-profile chip in `PresetArtworkBadge::paint()` (Master Bus/mixing, Vocal/vocals, Bass/bass, Guitar/guitar, Piano/keys, Drums/drums). |
| 3 | `signal_path` | 1600 × 500 | Standalone | Full-width diagram in the GUIDE sheet; also referenced by `docs/signal-path.md`. |
| 4 | `help_illustrations` | 1024 × 576 | 4 × 512 × 288 | Signal path, A/B, preset workflow and load-model scenes in the GUIDE sheet. |
| 5 | `knob_caps` | 512 × 256 | 8 × 128 × 128 | Static face material for every rotary; pointer and ticks stay dynamic. |
| 5 | `level_scale` | 512 × 256 | 1 × 512 × 256 | Transparent -20 to +3 scale for the AVG-driven needle. |
| 5 | `vu_needle_profile` | 256 × 64 | 1 × 256 × 64 | Shadowed moving pointer; vector line fallback. |
| 5 | `deck_plate` | 2048 × 512 | 1 × 2048 × 512 | Transparent calibration detail over the transport deck. |
| 6 | `rack_pedals` | 2048 × 512 | 4 × 512 × 512 | Distinct low-contrast background for each RACK page. |
| 6 | `tape_stock_swatches` | 1014 × 128 | 13 × 78 × 128 | Selected tape label beside the live combo value. |
| 6 | `tube_glow_maps` | 512 × 256 | 4 × 128 × 256 | Decorative heater marks driven by the visible slider amount. |
| 6 | `wear_and_patina` | 1024 × 1024 | 1 × 1024 × 1024 | Seamless optional surface wear, enabled in USER THEME. |
| 6 | `spindown_blur` | 256 × 256 | 1 × 256 × 256 | Software reel motion overlay, faded by smoothed transport speed. |

## Planned assets

These are additional assets identified as useful for the plugin's UI but not yet
shipped. Each entry lists a proposed slot name, dimensions, decoded RGBA cost, and
intended consumer. They are candidates, not commitments: add one only when a
concrete draw site and fallback are defined.

| Proposed slot | Size | ~MiB | Intended consumer | Notes |
| --- | --- | ---: | --- | --- |
| `automation_icons` | 512 × 256 | 0.50 | Automation lane and parameter-modulation glyphs | Reuse `ui_icons` family if overlap. |
| `eq_curve_previews` | 1024 × 256 | 1.00 | IN/OUT EQ response preview behind the EQ knobs | Static; frequency axis from code. |
| `reverb_room_shapes` | 512 × 512 | 1.00 | Reverb page room preview | One tile per reverb size preset. |
| `ir_waveforms` | 1024 × 128 | 0.50 | IR MIX readout preview | Rendered from the loaded IR; optional. |
| `neural_model_previews` | 512 × 128 | 0.25 | Neural model badge thumbnail | Falls back to `model.neural` glyph. |
| `vinyl_label_art` | 512 × 512 | 1.00 | Vinyl page label preview | Optional decorative. |
| `tape_reel_faces` | 512 × 512 | 1.00 | Software reel face variants | Extends `knob_caps` family. |
| `cabinet_previews` | 512 × 256 | 0.50 | Cabinet selector preview | One tile per cabinet type. |
| `oscilloscope_grids` | 512 × 512 | 1.00 | Oscilloscope overlay grid | Reusable across scopes. |
| `spectrum_frames` | 1024 × 256 | 1.00 | Spectrum analyzer chrome | Low-contrast frame only. |
| `meter_frames` | 512 × 256 | 0.50 | Meter bezel variants | Extends `hardware_details`. |
| `knob_scale_rings` | 512 × 512 | 1.00 | Optional printed scale ring overlay | Fallback is code-drawn ticks. |
| `mod_matrix_diagram` | 1024 × 512 | 2.00 | Modulation routing diagram | Help-sheet candidate. |
| `preset_thumbnails` | 1024 × 64 | 0.25 | Preset browser row thumbnail | Extends `factory_preset_art`. |
| `transport_icons` | 256 × 256 | 0.25 | Play/stop/record transport glyphs | Reuse `ui_icons` if overlap. |

Adding all fifteen would cost about 12.75 MiB of decoded RGBA on top of the current
~30.94 MiB. Add them incrementally, one at a time, with a draw site and fallback
defined first.

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

### Parameter glyphs

`parameter_glyphs.png` carries 100 tintable glyphs in `controlSpecs` order. Each
control label (`controlLabels`) is given the custom look-and-feel and the control's
parameter id as its component id. `J37LookAndFeel::drawLabel()` resolves the id to a
`controlSpecs` index and draws the matching 64 px tile to the left of the caption,
tinted with the caption ink. The caption text area shrinks by the glyph width, so
long captions still fit through `drawFittedText()`. A missing atlas leaves the
text-only caption exactly as before.

### Machine model glyphs

`machine_models.png` feeds the model selectors. `EditorSetup.cpp` assigns each box a
stable id (`mibox.valve`, `mibox.pentode`, `mibox.triode`, `mibox.transistor`,
`mibox.transformer`, `mibox.digital`, `mibox.vinyl`); the tape box keeps its
existing `mibox.tape_stock` swatch. `modelGlyphTileFor()` maps box id plus selected
entry to a tile index, and `drawComboBox()` draws it beside the value while
`positionComboBoxText()` reserves the matching inset. Off entries map to the
family's `Off` tile (or the generic screw `Off` tile for principle families, which
have no per-model glyph). A missing atlas leaves the selector text-only.

### Meter status glyphs

`LevelMeter::paint()` draws the `meter.input` (tile 0) or `meter.output` (tile 1)
glyph to the left of the centred title, and the `meter.clipping` (tile 6) glyph on
the right in red when `clipping` is true. Both are tinted with the meter palette
and fall back to the text-only title when the atlas is absent.

### Preset category chips

`PresetArtworkBadge::paint()` overlays a small category chip in its bottom-right
corner, derived from the live `instrument` parameter: Master Bus -> mixing,
Vocal -> vocals, Bass -> bass, Guitar -> guitar, Piano -> keys, Drums -> drums.
The two remaining atlas categories (mastering, creative) are reserved for a future
explicit preset-category field, which the factory preset JSON does not yet carry.
A missing atlas leaves the badge as before.

### Brand lockups

The panel header draws the square lockup (tile 0) in its left margin, ahead of the
copper rule. The USER THEME window header draws the horizontal lockup (tile 1) on a
light palette and the reversed lockup (tile 3) on a dark palette, falling back to
`brand_mark` and then to nothing. The monochrome lockup (tile 2) is reserved for
future monochrome surfaces.

### GUIDE sheet

A new full-editor overlay, opened from the SETTINGS page's GUIDE row, displays
`signal_path.png` at full width followed by a 2 x 2 grid of the four
`help_illustrations` scenes with captions (SIGNAL PATH, A/B COMPARE, PRESET
WORKFLOW, LOAD A MODEL OR IR). It follows the panel palette, closes on Escape or
CLOSE, and rebuilds on a theme switch. Missing images leave the captions and a
text fallback, so the sheet is never blank.

## Prioritized follow-up plan

### P0 — Reconcile preset-art metadata
`PanelAssetSlots.h` describes `factory_preset_art` as **29** tiles, while its
2240 × 64 dimensions and the manifest/catalog describe **35** tiles. Correct the
stale source-table description and confirm that all copies agree with CMake's
`FACTORY_PRESET_FILES` order. Do not change preset identities or reorder tiles to
make the documentation pass.

**Done when:** the slot description, manifest, catalog, CMake list, and generated
atlas agree on count and order, and `tests/assets/check_assets.py` passes.

### P1 — Keep generated output reproducible
The generator uses Python and Pillow to produce generated PNGs and catalog data
from live source tables where order matters. Pillow is authoring-only, not a CMake,
plugin, or runtime dependency. Some identity/material sheets are hand-maintained
and must not be overwritten by the generator. Before editing a generated PNG by
hand, verify whether regeneration replaces it; change the generator or documented
source artwork instead.

**Done when:** generation is reproducible and catalog keys and required indices
are unique, contiguous, and source-aligned.

### P2 — Verify optional rendering paths
For each changed runtime slot or draw site, verify both valid-image rendering and
the missing/invalid-image fallback. Keep overlays clipped, text and hit targets
readable, and animation driven by UI state rather than DSP internals. For visual
changes, inspect compact, normal, and high-DPI sizes where practical; source-level
presence does not prove alignment or legibility.

**Done when:** absent art leaves a complete usable interface and focused asset and
editor checks pass.

### P3 — Control pack growth
Before adding a slot or increasing resolution, record decoded RGBA memory impact,
expected use frequency, scaling behavior, and fallback. Extend an existing atlas
only when ownership and tile ordering remain clear. Keep unused documentation art
out of runtime decoding where practical.

**Done when:** every new embedded runtime image has a concrete consumer and a
stated memory budget.

### P4 — Gate distribution on rights review
Before shipping, verify provenance and licence metadata for the exact embedded
files, then follow [`release-checklist.md`](release-checklist.md). A local build
or repository presence does not grant permission to distribute art or binaries.

**Done when:** release review confirms distribution rights for every shipped file.

## Authoring and validation workflow
After changing assets, source-derived lists, or draw sites, run from the repository
root:

```sh
python Assets/generate_asset_pack.py
python tests/assets/check_assets.py
python tests/dependencies/check_markdown_links.py
python tests/theme/check_theme_wiring.py
```

The asset check verifies slot/manifest/catalog/file agreement, dimensions and
alpha, licence metadata, source-derived tape/RACK/preset/parameter ordering,
optional draw-site wiring, and fallback contracts. Run
`tests/editor/check_editor_juce.sh` when changing C++ rendering integration; it
compiles editor translation units and GUI headers against JUCE. Report exactly
which checks ran: focused checks are not a full platform build or release review.

CMake embeds `Assets/*.png` through BinaryData; adding a PNG does not require a new
runtime dependency or platform install step. Android launcher resources are
packaging assets catalogued separately from the runtime slot table and must stay
aligned with their catalog entries and Android manifest.

## Runtime draw-site index

Every slot has exactly one or more message-thread consumers. Missing or invalid
images always fall back to the procedural/palette/vector path listed.

| Slot | Consumer | Fallback |
| --- | --- | --- |
| `panel_texture` | `EditorPaint.cpp` generic grain surface | Arithmetic grain + wear. |
| `panel_materials` | `EditorPaint.cpp` theme surface atlas | Generic texture, then grain. |
| `brand_mark` | `PluginEditor.cpp` window icon; `EditorMeters.cpp` preset badge; `UserThemeWindow.h` header | Drawn reel/monogram or empty margin. |
| `window_icon` | `UserThemeWindow.h` title-bar icon | Host/default icon. |
| `brand_lockups` | `EditorPaint.cpp` header square (tile 0); `UserThemeWindow.h` header lockup (tile 1/3) | `brand_mark`, then empty margin. |
| `ui_icons` | `J37LookAndFeel.cpp` family buttons (tiles 18-20) | Text-only button caption. |
| `principle_icons` | `EditorMeters.cpp` MIX bar source glyphs | Principle-coloured bars only. |
| `parameter_glyphs` | `J37LookAndFeel.cpp` control captions (`drawLabel`) | Text-only caption. |
| `machine_models` | `J37LookAndFeel.cpp` model combo boxes | Text-only selector value. |
| `meter_status` | `EditorMeters.cpp` `LevelMeter::paint()` | Text-only title. |
| `hardware_details` | `PanelPaint.h` `drawScrew()`; `PanelAssets.h` DETAIL background | Procedural screw / grain. |
| `factory_preset_art` | `EditorMeters.cpp` `PresetArtworkBadge` | `brand_mark`, then drawn reel. |
| `preset_categories` | `EditorMeters.cpp` preset badge chip | No chip (artwork only). |
| `signal_path` | `HelpSheet.h` guide diagram | Text fallback. |
| `help_illustrations` | `HelpSheet.h` scene grid | Captions only. |
| `knob_caps` | `J37LookAndFeel.cpp` cap face | Procedural dome. |
| `level_scale` | `EditorMeters.cpp` dial scale | Adaptive ticks and labels. |
| `vu_needle_profile` | `EditorMeters.cpp` needle | Vector needle. |
| `deck_plate` | `EditorPaint.cpp` deck overlay | Theme texture only. |
| `rack_pedals` | `EditorPaint.cpp` RACK page background | Themed panel background. |
| `tape_stock_swatches` | `J37LookAndFeel.cpp` tape combo | Text-only value. |
| `tube_glow_maps` | `J37LookAndFeel.cpp` source knobs | No glow. |
| `wear_and_patina` | `EditorPaint.cpp` panel layers | Clean surface. |
| `spindown_blur` | `EditorPaint.cpp` software reel | No blur overlay. |

## Fallback contract

1. `cachedImage(symbol)` returns an invalid `juce::Image` when the binary resource is
   absent; every call site tests `isValid()` (and usually dimensions) before drawing.
2. No call site hides a control, label, meter, or readout because an image is missing.
3. Procedural/palette/vector drawing always runs first or remains visible underneath;
   the image is an additive layer.
4. Tinted atlases use `g.setColour(...)` before `drawImage(..., true)`; untinted
   raster art sets white to avoid a black tint.
5. Missing art must not trigger allocations, file I/O, or audio-thread work.

## Adding a new asset

1. Add a row to `PanelAssetSlots.h` (name, file, symbol, size, purpose).
2. Add provenance, licence, author, and `used_by` to `Assets/manifest.json`.
3. Add atlas grid/keys to `Assets/asset_catalog.json` if it is an atlas.
4. Add the PNG to `Assets/` (or the generator) and run `python Assets/generate_asset_pack.py`.
5. Add exactly one draw site with an explicit fallback, and update this document.
6. Run `python tests/assets/check_assets.py`, `tests/editor/check_editor_juce.sh`,
   and the relevant harness; report exactly what ran.

## Decoded memory budget

Decoding every declared image as RGBA costs about 30.94 MiB; actual editor use is
lower because documentation/help images and some atlases are only touched on demand.
New runtime images must state their decoded RGBA cost, expected open frequency, and
scaling behaviour before being added. Prefer extending an existing atlas over adding
a standalone full-resolution image.

## Completion status

All 24 shipped slots are connected to live draw sites with procedural fallbacks.
Generation is unblocked: no outstanding item prevents producing or regenerating
the PNGs.

The artwork rollout is complete. Add new art only for a named product/UI need,
with a clear source of truth, stable ordering where relevant, optional loading, a
useful fallback, compatible licensing, and focused validation.
