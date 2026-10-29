# Optional panel art

The panel is **drawn from code**: every plate, knob, screw, tick and grain line is
arithmetic and palette colour. That is what makes it scale, theme and stay sharp on
any display. The files in this directory are the **additive** layer — surfaces and
marks that a designer can drop in later without touching the drawing.

**Nothing here is required.** A checkout with this directory empty builds and looks
exactly like the current panel: every draw site asks for its slot and falls back to
its own drawing when the image is missing (`Source/GUI/PanelAssets.h`).

## The slots

The single source of truth is the table in
[`Source/GUI/PanelAssetSlots.h`](../Source/GUI/PanelAssetSlots.h); the manifest below
mirrors it for the check and for whoever is sourcing the art.

| File | Size (px) | What it is | Where it is drawn |
| --- | --- | --- | --- |
| `panel_texture.png` | 1024 × 512 | The chassis surface: grain, wear, faint print marks. Half-resolution layer stretched over the whole editor, under the controls. | `rebuildPanelTextureLayers()`, `Source/GUI/EditorPaint.cpp` |
| `brand_mark.png` | 256 × 256 | The product mark: a reel, a monogram, a drawn logo. Read as a signature, not a control. | The USER THEME window's header |
| `window_icon.png` | 64 × 64 | Title-bar / taskbar icon for the USER THEME window. Must read at 16 px. Without it the window draws its own reel mark (`defaultWindowIcon` in the same header), so the corner is never blank. | `UserThemeWindow`, `DocumentWindow::setIcon` |

PNG with alpha. `panel_texture` should be **seamless-ish and low contrast**: it is
multiplied into a dark (or ivory) panel at a low dose, so any single bright feature
will read as a stain. The other two are spots of colour and can be as bold as the
brand allows.

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
