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
MANIFEST = REPO / "Assets" / "manifest.json"
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


def main() -> int:
    problems: list[str] = []
    rows = slot_table()
    theme_window = THEME_WINDOW.read_text()
    editor_paint = EDITOR_PAINT.read_text()
    editor_meters = EDITOR_METERS.read_text()
    editor_theme = EDITOR_THEME.read_text()
    panel_paint = PANEL_PAINT.read_text()

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
