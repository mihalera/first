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

## Completion status

- [x] Identity, materials, icons, preset art, help scenes, and signal-flow diagram.
- [x] Knob caps and AVG/VU-style meter scale and needle.
- [x] Deck plate, RACK surfaces, and tape-stock swatches.
- [x] Tube glow, optional patina, and software-reel motion overlay.
- [ ] Reconcile the stale factory-preset tile count in `PanelAssetSlots.h`.
- [ ] Keep generated catalog data aligned with source tables and checks.

The artwork rollout is complete. Add new art only for a named product/UI need,
with a clear source of truth, stable ordering where relevant, optional loading, a
useful fallback, compatible licensing, and focused validation.
