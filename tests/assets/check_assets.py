#!/usr/bin/env python3
"""Checks the optional panel art against the slot table and the manifest.

The panel's art is OPTIONAL, so an empty Assets/ directory is a pass with a
summary. What this file exists for is the moment somebody does add a file: then
the three lists that have to agree - the slot table the code reads, the manifest a
person fills in, and the files on disk - are compared, and a mismatch is reported
before it becomes a texture nobody can licence or a slot that draws nothing.

What is checked:
  * the manifest's slots are exactly the table's slots (name, file, size);
  * every image in Assets/ is a known slot, is a real PNG, and has the slot's
    dimensions (read out of the IHDR chunk, so no Pillow is needed);
  * every embedded file has its licence, source and author recorded;
  * the slot table's symbol matches juce_add_binary_data's naming rule
    ('.' becomes '_'), because a wrong symbol is a file that builds and never
    loads.

Usage:  python3 tests/assets/check_assets.py
"""

from __future__ import annotations

import json
import pathlib
import re
import struct
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
SLOTS = REPO / "Source" / "GUI" / "PanelAssetSlots.h"
THEME_WINDOW = REPO / "Source" / "GUI" / "UserThemeWindow.h"
EDITOR_PAINT = REPO / "Source" / "GUI" / "EditorPaint.cpp"
EDITOR_METERS = REPO / "Source" / "GUI" / "EditorMeters.cpp"
EDITOR_THEME = REPO / "Source" / "GUI" / "EditorTheme.cpp"
PANEL_PAINT = REPO / "Source" / "GUI" / "PanelPaint.h"
CMAKE = REPO / "CMakeLists.txt"
LOOK_AND_FEEL = REPO / "Source" / "GUI" / "J37LookAndFeel.cpp"
EDITOR_SETUP = REPO / "Source" / "GUI" / "EditorSetup.cpp"
MANIFEST = REPO / "Assets" / "manifest.json"
CATALOG = REPO / "Assets" / "asset_catalog.json"
ASSET_DIR = REPO / "Assets"

ROW = re.compile(
    r'\{\s*"(?P<name>[^"]+)"\s*,\s*"(?P<file>[^"]+)"\s*,\s*"(?P<symbol>[^"]+)"\s*,'
    r'\s*(?P<width>\d+)\s*,\s*(?P<height>\d+)')


def slot_table() -> list[dict]:
    text = SLOTS.read_text()
    rows = [match.groupdict() for match in ROW.finditer(text)]
    if not rows:
        raise SystemExit(f"{SLOTS.relative_to(REPO)}: no slot rows found - the table "
                         f"this check reads has changed shape")
    return rows


def png_size(path: pathlib.Path) -> tuple[int, int] | None:
    """The IHDR width and height, without decoding the image."""
    with path.open("rb") as handle:
        header = handle.read(24)
    if header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        return None
    width, height = struct.unpack(">II", header[16:24])
    return width, height


def png_has_alpha(path: pathlib.Path) -> bool:
    """True for PNG colour types that carry an alpha channel."""
    with path.open("rb") as handle:
        header = handle.read(26)
    return (header[:8] == b"\x89PNG\r\n\x1a\n"
            and header[12:16] == b"IHDR"
            and header[25] in {4, 6})


def cpp_string_array(path: pathlib.Path, name: str) -> list[str]:
    text = path.read_text()
    match = re.search(rf"\b{re.escape(name)}\s*\{{(.*?)\}}", text, re.DOTALL)
    if match is None:
        return []
    body = re.sub(r"//[^\n]*", "", match.group(1))
    return re.findall(r'"([^"\n]+)"', body)


def slug(value: str) -> str:
    return re.sub(r"[^a-z0-9]+", "_", value.strip().lower()).strip("_")


def main() -> int:
    problems: list[str] = []
    rows = slot_table()
    theme_window = THEME_WINDOW.read_text()
    editor_paint = EDITOR_PAINT.read_text()
    editor_meters = EDITOR_METERS.read_text()
    editor_theme = EDITOR_THEME.read_text()
    panel_paint = PANEL_PAINT.read_text()
    cmake = CMAKE.read_text()
    panel_tabs = (REPO / "Source" / "GUI" / "PanelTabs.h").read_text()
    panel_text = (REPO / "Source" / "GUI" / "PanelText.h").read_text()
    look_and_feel = LOOK_AND_FEEL.read_text()
    editor_setup = (REPO / "Source" / "GUI" / "EditorSetup.cpp").read_text()
    asset_generator = (ASSET_DIR / "generate_asset_pack.py").read_text()

    if "setWindowIcon (art);" not in theme_window:
        problems.append("UserThemeWindow must apply the window_icon asset when present")
    if "setWindowIcon (defaultWindowIcon (start));" not in theme_window:
        problems.append("UserThemeWindow must apply its drawn fallback icon when the asset is absent")
    if "peer->setIcon (icon);" not in theme_window:
        problems.append("UserThemeWindow must pass its icon to the native desktop peer")

    if ("panel_materials_png" not in editor_paint
            or "getClippedImage" not in editor_paint
            or "j37panel::liveThemeChoice" not in editor_paint):
        problems.append("EditorPaint must select a panel-material atlas tile from the live theme")
    theme_enum = re.search(r"enum class ThemeChoice\s*\{([^}]*)\}", panel_paint)
    theme_choices = ([] if theme_enum is None else
                     [(name, int(value)) for name, value in
                      re.findall(r"\b(\w+)\s*=\s*(-?\d+)", theme_enum.group(1))])
    theme_values = [value for _, value in theme_choices]
    if not theme_values or theme_values != list(range(len(theme_values))):
        problems.append("ThemeChoice values must remain contiguous from zero for the material atlas")
    if theme_choices and theme_choices[-1][0] != "user":
        problems.append("ThemeChoice::user must remain last because it selects the final material tile")
    if "static_cast<int> (j37panel::ThemeChoice::user) + 1" not in editor_paint:
        problems.append("EditorPaint must derive the atlas tile count from the final ThemeChoice")
    if "liveThemeChoice = themeChoice;" not in editor_theme:
        problems.append("applyTheme must publish ThemeChoice before rebuilding its material surface")
    elif (editor_theme.find("liveThemeChoice = themeChoice;")
          > editor_theme.find("rebuildPanelTextureLayers();")):
        problems.append("applyTheme must set the live theme before rebuilding the material surface")

    if ("principle_icons_png" not in editor_meters
            or not re.search(r"drawImage\s*\(\s*icons,.*?p\s*\*\s*64,\s*0,\s*64,\s*64,\s*true",
                             editor_meters, re.DOTALL)):
        problems.append("MixBar must draw and tint the matching 64 px principle icon cell")

    preset_block = re.search(r"set\(FACTORY_PRESET_FILES\s*(.*?)\)", cmake, re.DOTALL)
    preset_order = ([] if preset_block is None else
                    re.findall(r"\$\{FACTORY_PRESET_DIR\}/([a-z0-9_]+)\.json",
                               preset_block.group(1)))
    if not preset_order:
        problems.append("CMakeLists.txt must define the ordered FACTORY_PRESET_FILES list")

    if ("factory_preset_art_png" not in editor_meters
            or "processor.getLastPresetIndex()" not in editor_meters
            or not re.search(r"drawImage\s*\(\s*atlas,.*?index\s*\*\s*64,\s*0,\s*64,\s*64",
                             editor_meters, re.DOTALL)):
        problems.append("PresetArtworkBadge must crop the selected 64 px tile by processor preset index")
    if "brand_mark_png" not in editor_meters:
        problems.append("PresetArtworkBadge must provide a product-mark fallback for non-factory states")
    if ("ui_icons_png" not in look_and_feel
            or "mibox.family." not in look_and_feel
            or "button.setComponentID (\"mibox.family.\"" not in editor_setup):
        problems.append("the TAPE / FX / SETUP family controls must draw their indexed UI atlas glyphs")

    if not MANIFEST.exists():
        raise SystemExit(f"{MANIFEST.relative_to(REPO)} is missing; the asset pipeline "
                         f"has a manifest even when it has no art")

    manifest = json.loads(MANIFEST.read_text())
    entries = {entry["name"]: entry for entry in manifest.get("slots", [])}
    manifest_order = [entry["name"] for entry in manifest.get("slots", [])]
    table_order = [row["name"] for row in rows]
    if manifest_order != table_order:
        problems.append("manifest slot order must mirror PanelAssetSlots.h, including atlas tile order")

    material_entry = entries.get("panel_materials")
    if material_entry is not None and theme_values:
        if material_entry.get("width", 0) % len(theme_values) != 0:
            problems.append("panel_materials width must divide evenly across every ThemeChoice")
    principle_count = re.search(r"inline constexpr int principleCount = (\d+);", panel_paint)
    icon_entry = entries.get("principle_icons")
    if icon_entry is not None and principle_count is not None:
        expected_width = int(principle_count.group(1)) * 64
        if icon_entry.get("width") != expected_width or icon_entry.get("height") != 64:
            problems.append("principle_icons must contain one 64x64 cell per saturation principle")

    preset_art_entry = entries.get("factory_preset_art")
    if preset_art_entry is not None:
        if preset_art_entry.get("tile_order") != preset_order:
            problems.append("factory_preset_art tile_order must exactly match CMake's factory preset order")
        if (preset_art_entry.get("tile_size") != 64
                or preset_art_entry.get("width") != len(preset_order) * 64
                or preset_art_entry.get("height") != 64):
            problems.append("factory_preset_art must have one 64x64 tile per ordered factory preset")

    if not CATALOG.exists():
        problems.append("asset_catalog.json is missing; sprite atlases need stable names and indices")
    else:
        catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
        atlases = catalog.get("atlases", [])
        catalog_keys: set[str] = set()
        atlas_by_file = {atlas.get("file"): atlas for atlas in atlases}
        if len(atlas_by_file) != len(atlases):
            problems.append("asset_catalog.json contains duplicate atlas file entries")

        for atlas in atlases:
            filename = atlas.get("file", "")
            columns, atlas_rows = atlas.get("columns", 0), atlas.get("rows", 0)
            tile_size = atlas.get("tile_size", 0)
            if isinstance(tile_size, list) and len(tile_size) == 2:
                tile_width, tile_height = tile_size
            else:
                tile_width = tile_height = tile_size
            if (not columns or not atlas_rows or not tile_width or not tile_height
                    or atlas.get("width") != columns * tile_width
                    or atlas.get("height") != atlas_rows * tile_height):
                problems.append(f"{filename}: catalog dimensions must match its grid and tile size")
            items = atlas.get("items", [])
            if len(items) > columns * atlas_rows:
                problems.append(f"{filename}: more named items than atlas cells")
            for tile, item in enumerate(items):
                key = item.get("key")
                if item.get("tile") != tile:
                    problems.append(f"{filename}: tile indices must be contiguous and match item order")
                if not key or key in catalog_keys:
                    problems.append(f"{filename}: missing or duplicate atlas key {key!r}")
                if key:
                    catalog_keys.add(key)

            slot_name = pathlib.Path(filename).stem
            entry = entries.get(slot_name)
            if entry is None:
                problems.append(f"{filename}: catalog atlas has no slot in manifest.json")
            elif (entry.get("width"), entry.get("height")) != (atlas.get("width"), atlas.get("height")):
                problems.append(f"{filename}: catalog and manifest dimensions disagree")

        catalog_preset = atlas_by_file.get("factory_preset_art.png", {})
        catalog_preset_order = [item.get("key", "").removeprefix("preset.")
                                for item in catalog_preset.get("items", [])]
        if catalog_preset_order != preset_order:
            problems.append("factory preset atlas cells must follow CMake's ordered preset list")

        tab_block = re.search(r"tabSpecs\s*\{\s*\{(.*?)\}\s*\};", panel_tabs, re.DOTALL)
        tab_names = ([] if tab_block is None else
                     re.findall(r'^\s*\{\s*"([A-Z][A-Z0-9 /-]+)"\s*,',
                                tab_block.group(1), re.MULTILINE))
        ui_atlas = atlas_by_file.get("ui_icons.png", {})
        ui_tab_order = [item.get("key") for item in ui_atlas.get("items", [])[:len(tab_names)]]
        if ui_tab_order != [f"tab.{slug(name)}" for name in tab_names]:
            problems.append("ui_icons tab glyphs must remain in PanelTabs.h order")
        ui_items = ui_atlas.get("items", [])
        family_keys = [item.get("key") for item in ui_items[18:21]]
        if family_keys != ["family.tape", "family.fx", "family.setup"]:
            problems.append("family icon cells 18-20 must remain TAPE, FX, SETUP")

        control_block = re.search(r"controlSpecs\s*\{\s*\{(.*?)\}\s*\};",
                                  panel_text, re.DOTALL)
        control_ids = ([] if control_block is None else
                       re.findall(r'^\s*\{\s*"([a-z0-9_]+)"\s*,',
                                  control_block.group(1), re.MULTILINE))
        parameter_atlas = atlas_by_file.get("parameter_glyphs.png", {})
        parameter_order = [item.get("key") for item in parameter_atlas.get("items", [])]
        if parameter_order != [f"parameter.{key}" for key in control_ids]:
            problems.append("parameter glyphs must remain in controlSpecs order")

        model_sets = [
            ("tape", REPO / "Source" / "PluginProcessor.h", "tapeStockNames"),
            ("valve", REPO / "Source" / "DSP" / "SaturationCore.h", "valveTypeNames"),
            ("amp", REPO / "Source" / "DSP" / "SaturationCore.h", "ampTypeNames"),
            ("transformer", REPO / "Source" / "DSP" / "SaturationCore.h", "transformerTypeNames"),
            ("digital", REPO / "Source" / "DSP" / "SaturationCore.h", "digitalTypeNames"),
            ("vinyl", REPO / "Source" / "PluginProcessor.h", "vinylTypeNames"),
        ]
        model_order = [f"model.{family}.{slug(name)}"
                       for family, source, array in model_sets
                       for name in cpp_string_array(source, array)]
        model_atlas = atlas_by_file.get("machine_models.png", {})
        actual_model_order = [item.get("key") for item in model_atlas.get("items", [])]
        if actual_model_order[:len(model_order)] != model_order:
            problems.append("machine model glyphs must follow the processor's live type-name arrays")

        standalone = {item.get("file"): item for item in catalog.get("standalone", [])}
        signal = standalone.get("signal_path.png")
        if signal is None or (signal.get("width"), signal.get("height")) != (1600, 500):
            problems.append("asset_catalog.json must describe the 1600x500 signal_path illustration")
        elif entries.get("signal_path", {}).get("width") != signal.get("width"):
            problems.append("signal_path standalone catalog and manifest dimensions disagree")
        signal_doc = (REPO / "docs" / "signal-path.md").read_text()
        if "../Assets/signal_path.png" not in signal_doc:
            problems.append("docs/signal-path.md must display the generated signal path illustration")
        if "FACTORY_PRESET_FILES" not in asset_generator or "controlSpecs" not in asset_generator:
            problems.append("asset generator must derive preset and parameter order from project sources")

        android_assets = [item for item in catalog.get("platform_assets", [])
                          if item.get("platform") == "Android"]
        if len(android_assets) != 1:
            problems.append("asset_catalog.json must describe the Android launcher resource set")
        else:
            android = android_assets[0]
            expected_densities = {"mdpi": (48, 48), "hdpi": (72, 72),
                                  "xhdpi": (96, 96), "xxhdpi": (144, 144),
                                  "xxxhdpi": (192, 192)}
            actual_densities = {entry.get("density"): entry for entry in android.get("density_icons", [])}
            if set(actual_densities) != set(expected_densities):
                problems.append("Android launcher must have all five standard density PNGs")
            for density, dimensions in expected_densities.items():
                entry = actual_densities.get(density)
                if entry is None:
                    continue
                path = REPO / entry.get("file", "")
                if not path.is_file() or png_size(path) != dimensions:
                    problems.append(f"Android {density} launcher must be a {dimensions[0]}x{dimensions[1]} PNG")
            android_manifest = REPO / android.get("manifest", "")
            if (not android_manifest.is_file()
                    or android.get("manifest_icon") != "@mipmap/ic_launcher"
                    or "android:icon=\"@mipmap/ic_launcher\"" not in android_manifest.read_text()):
                problems.append("Android manifest must reference the catalogued launcher icon")
            for key in ("adaptive_icons", "foreground_vector", "monochrome_vector"):
                paths = android.get(key, []) if isinstance(android.get(key), list) else [android.get(key)]
                if any(not (REPO / path).is_file() for path in paths if path):
                    problems.append(f"Android launcher catalog references a missing {key} resource")

    by_name = {row["name"]: row for row in rows}

    if set(entries) != set(by_name):
        problems.append("manifest slots and PanelAssetSlots.h disagree: "
                        f"only in manifest {sorted(set(entries) - set(by_name))}, "
                        f"only in the table {sorted(set(by_name) - set(entries))}")

    for name, row in by_name.items():
        entry = entries.get(name)
        if entry is None:
            continue
        if entry.get("file") != row["file"]:
            problems.append(f"{name}: manifest file {entry.get('file')!r} != table "
                            f"{row['file']!r}")
        if (entry.get("width"), entry.get("height")) != (int(row["width"]), int(row["height"])):
            problems.append(f"{name}: manifest size {entry.get('width')}x{entry.get('height')} "
                            f"!= table {row['width']}x{row['height']}")

        expected_symbol = row["file"].replace(".", "_")
        if row["symbol"] != expected_symbol:
            problems.append(f"{name}: symbol {row['symbol']!r} is not the binary-data "
                            f"symbol for {row['file']!r} ({expected_symbol!r}) - the file "
                            f"would build and never load")

    present = sorted(path for path in ASSET_DIR.glob("*")
                     if path.is_file() and path.suffix.lower() in {".png", ".jpg", ".jpeg"})

    found_files = {path.name: path for path in present}

    for name, path in found_files.items():
        row = next((r for r in rows if r["file"] == name), None)
        if row is None:
            problems.append(f"{path.name}: not a slot in PanelAssetSlots.h - an embedded "
                            f"file nothing can draw")
            continue

        entry = entries.get(row["name"], {})
        for field in ("licence", "source", "author"):
            if not entry.get(field):
                problems.append(f"{path.name}: manifest.{field} is empty - the licence rule "
                                f"in Assets/README.md is CC0/public domain with provenance")

        if path.suffix.lower() == ".png":
            size = png_size(path)
            if size is None:
                problems.append(f"{path.name}: not a PNG this check can read")
            elif size != (int(row["width"]), int(row["height"])):
                problems.append(f"{path.name}: {size[0]}x{size[1]} on disk, but the slot "
                                f"asks for {row['width']}x{row['height']} (see Assets/README.md)")
            if not png_has_alpha(path):
                problems.append(f"{path.name}: PNG needs an alpha channel for the panel asset pipeline")

    print(f"asset slots: {len(rows)} declared, {len(present)} file(s) in Assets/")

    if problems:
        print("\nASSET CHECK FAILED:")
        for problem in problems:
            print(f"  - {problem}")
        return 1

    print("ASSET CHECK PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
