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


def main() -> int:
    problems: list[str] = []
    rows = slot_table()

    if not MANIFEST.exists():
        raise SystemExit(f"{MANIFEST.relative_to(REPO)} is missing; the asset pipeline "
                         f"has a manifest even when it has no art")

    manifest = json.loads(MANIFEST.read_text())
    entries = {entry["name"]: entry for entry in manifest.get("slots", [])}

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
