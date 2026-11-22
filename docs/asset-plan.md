# Asset plan: what to draw, and what each drawing buys

The panel is drawn from code. Every plate, knob, tick and grain line is arithmetic
and palette colour, which is what lets it scale, theme and stay sharp at any size.
The art in `Assets/` is the **additive** layer: surface texture, product marks and
small glyphs that sit on top of the drawing without replacing it. A checkout with
no art still builds and still looks like the panel.

This file is the **plan**, not the catalogue: the catalogue (every slot, its exact
size, the code that reads it) is `Assets/README.md` and the single source of truth
behind both is `Source/GUI/PanelAssetSlots.h`. What follows is ordered by what a
user sees first, so a free afternoon has an obvious starting point.

## Status

All 15 declared slots are now filled and `python tests/assets/check_assets.py`
reports `ASSET CHECK PASSED` (15 slots, 15 files). The remaining work is the
proposed slots in the last table, which need a row in `PanelAssetSlots.h`, a
call at the draw site and a file.

## How to add a drawing

1. Drop the file into `Assets/` under the name in the slot table.
2. Nothing else. The loader picks it up, and the draw site falls back to its own
   painting when the image is missing or invalid.
3. `python tests/assets/check_assets.py` reports a file whose size disagrees with
   its slot, a slot with no file, and a file with no slot.

PNG with alpha. White-on-transparent for anything the panel tints (it re-tints
glyphs to the active machine colour); full colour for marks and artwork that is
itself the colour. `panel_texture` and each tile of `panel_materials` should be low
contrast and roughly seamless: they are multiplied over a dark or light palette, so
a bright feature reads as a stain rather than as material.

---

## Tier 1 - seen on every session, worth the most

| Slot | Size | What to draw | Where it shows |
| --- | --- | --- | --- |
| `brand_mark.png` | 256 × 256 | The signature: a reel seen at a slight angle, a monogram, or a drawn wordmark. It is read as a maker's mark, so it should survive being 16 px wide. | The title bar of the standalone, the USER THEME window's header, the brand strip in the header |
| `window_icon.png` | 64 × 64 | The same mark reduced to a square icon. Two tones only. | Explorer, taskbar, Alt-Tab, the title bar of every product window |
| `panel_materials.png` | 3072 × 256 (6 × 512 × 256) | Six materials in `ThemeChoice` order: ivory paper, charcoal bakelite, brushed steel, anodized graphite, white enamel, neutral fibre. Directional grain, no features. | Under the whole panel, per theme |
| `ui_icons.png` | 512 × 832 (104 glyphs) | The page and workflow glyphs: BOX, FX, RACK, SETUP families, transport keys, preset workflow (SAVE, DEL, A/B, UNDO, REDO), file loaders. | The tab bar, the preset chain, the file buttons |
| `principle_icons.png` | 576 × 64 (9 glyphs) | TAPE, VALVE, PENTODE, TRIODE, TRANSISTOR, FOLD, GLASS, TRANSFORMER, DIGITAL - one idea each, one stroke weight. | The MIX bar's nine cells |

If only one row of that table gets drawn, draw `brand_mark` and `window_icon`: they
are the two the operating system shows when the plugin is not even open.

## Tier 2 - the panel's identity at working size

| Slot | Size | What to draw | Where it shows |
| --- | --- | --- | --- |
| `parameter_glyphs.png` | 512 × 576 (71 glyphs, `controlSpecs` order) | One mark per knob: a tape reel for SPEED, a waveform for DRIVE, a magnet for BIAS, a VU needle for MIX, an ear for WIDTH. Flat, 1.5 px stroke, 32 px grid. | Above every knob, replacing the text caption where an icon reads faster |
| `machine_models.png` | 512 × 512 (52 glyphs) | The model lists: one silhouetted object per tape stock, valve, transformer, digital converter and vinyl pressing, including the `Off` entries (an empty reel, a socket with nothing in it). | The type selectors' rows and their tooltips |
| `meter_status.png` | 512 × 256 (32 glyphs) | Small status marks for peak, RMS, LUFS, VU, phase, clipping, latency and CPU. | The LEVELS card, the status pill |
| `hardware_details.png` | 256 × 256 (16 details) | Screws, washers, vents, label plates, jacks, scales, seams, reels, lamps. | Panel corners, the deck's edges, the fixing points |

## Tier 3 - the deck and the rack, where a drawing replaces a drawing

| Slot | Size | What to draw | Where it shows |
| --- | --- | --- | --- |
| `factory_preset_art.png` | 1856 × 64 (29 tiles) | One 64 px illustration per factory preset, in `FACTORY_PRESET_FILES` order: a machine, a room, a room within a room. | The preset badge at the deck's foot |
| `preset_categories.png` | 512 × 64 (8 glyphs) | Mixing, mastering, drums, vocals, bass, guitar, keys, creative. | The preset list's category column |
| `signal_path.png` | 1600 × 500 | The full signal flow, drawn as a console strip rather than a block diagram. | `docs/signal-path.md`, the user guide |

The deck's own tape scene (reels, ribbon, dust) is **not** an asset slot: it is
drawn in 3D by the renderer and themed by the palette, and a PNG cannot replace it
without losing the motion that makes it read as a running machine.

## Tier 4 - documentation and store material

| Slot | Size | What to draw | Where it shows |
| --- | --- | --- | --- |
| `brand_lockups.png` | 512 × 128 (4 cells) | Square, horizontal, monochrome and reversed marks. | Product windows, the manual, the store page |
| `help_illustrations.png` | 1024 × 576 (4 scenes) | Signal flow, A/B comparison, preset workflow, model loading - each one screen of a manual. | The user guide and the quick start |

---

## Slots that do not exist yet, and would be worth adding

These need one row in `PanelAssetSlots.h`, one call at the draw site and a file -
the recipe is at the top of that header. They are listed because they are the
places where a drawing would replace arithmetic that is currently doing its best.

| Proposed slot | Size | Why |
| --- | --- | --- |
| `knob_caps.png` | 1024 × 256 (8 caps × 128) | The knobs are shaded domes today. Eight hand-painted caps (bakelite, aluminium, ivory, worn) would give the panel the one material it is missing, and the cap is the largest single surface on the page. |
| `deck_plate.png` | 2048 × 512 | The deck's metal face, with the scale arcs and the head-block engraving painted in. The rows are placed over it, so it is a background only. |
| `rack_pedals.png` | 2048 × 512 (4 × 512) | One painted face per RACK page - DRIVE, SHAPE, MOTION, PITCH - so the four knob rows read as four pedals on a board rather than as four pages of knobs. |
| `tape_stock_swatches.png` | 1024 × 128 (13 × 78) | A colour chip per tape stock, for the TAPE list's rows: the twelve stocks plus OFF. Cheap to draw, and it turns a list of names into a shelf. |
| `level_scale.png` | 512 × 128 | The LEVELS card's dial face at high resolution: a printed scale with dB marks and a needle rest. The card is the most-read instrument on the panel. |

## What to draw first, in one sentence

Draw `brand_mark` and `window_icon` (the two Windows shows without the plugin
open), then `panel_materials` (every pixel of the panel is over it), then the knob
caps, then the per-page glyphs. Everything else is polish on a machine that is
already finished.
