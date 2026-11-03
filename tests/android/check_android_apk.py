#!/usr/bin/env python3
"""Check the Android APK's native loader contract."""
from __future__ import annotations

import struct
import sys
import zipfile
from pathlib import Path


APK_LIBRARY = "lib/arm64-v8a/libjuce_jni.so"
ANDROID_PAGE_SIZE = 16 * 1024
REQUIRED_SYMBOLS = {"JNI_OnLoad", "juce_CreateApplication"}


def fail(message: str) -> None:
    raise SystemExit(f"Android APK check failed: {message}")


def check_library(data: bytes) -> None:
    if len(data) < 64 or data[:6] != b"\x7fELF\x02\x01":
        fail("native library is not a 64-bit little-endian ELF")

    header = struct.unpack_from("<16sHHIQQQIHHHHHH", data)
    machine = header[2]
    program_offset, program_entry_size, program_count = header[5], header[9], header[10]
    section_offset, section_entry_size, section_count = header[6], header[11], header[12]
    if machine != 183:
        fail(f"native library has ELF machine {machine}, expected AArch64 (183)")

    load_alignments = []
    for index in range(program_count):
        offset = program_offset + index * program_entry_size
        if offset + 56 > len(data):
            fail("truncated ELF program header")
        kind, _, file_offset, virtual_address, _, _, _, alignment = struct.unpack_from(
            "<IIQQQQQQ", data, offset
        )
        if kind == 1:
            if alignment <= 0 or virtual_address % alignment != file_offset % alignment:
                fail("PT_LOAD segment has invalid ELF alignment")
            load_alignments.append(alignment)

    if not load_alignments or any(value < ANDROID_PAGE_SIZE for value in load_alignments):
        fail(f"PT_LOAD segments are not all aligned for 16 KB pages: {load_alignments}")

    sections = []
    for index in range(section_count):
        offset = section_offset + index * section_entry_size
        if offset + 64 > len(data):
            fail("truncated ELF section header")
        sections.append(struct.unpack_from("<IIQQQQIIQQ", data, offset))

    exported = set()
    for section in sections:
        if section[1] != 11:  # SHT_DYNSYM
            continue
        string_section_index = section[6]
        if string_section_index >= len(sections) or section[9] < 24:
            fail("invalid dynamic symbol table")
        strings_section = sections[string_section_index]
        strings = data[strings_section[4]:strings_section[4] + strings_section[5]]
        for offset in range(section[4], section[4] + section[5], section[9]):
            if offset + 24 > len(data):
                fail("truncated dynamic symbol")
            name_offset, info, visibility, section_index, _, _ = struct.unpack_from(
                "<IBBHQQ", data, offset
            )
            if not name_offset or section_index == 0:
                continue
            if name_offset >= len(strings):
                fail("invalid dynamic symbol name")
            end = strings.find(b"\0", name_offset)
            if end < 0:
                fail("unterminated dynamic symbol name")
            name = strings[name_offset:end].decode("ascii", errors="replace")
            if (name in REQUIRED_SYMBOLS
                    and info >> 4 == 1  # STB_GLOBAL
                    and visibility & 3 == 0):  # STV_DEFAULT
                exported.add(name)

    missing = REQUIRED_SYMBOLS - exported
    if missing:
        fail(f"native library does not export required loader symbols: {sorted(missing)}")


def main() -> None:
    if len(sys.argv) != 2:
        fail("usage: check_android_apk.py <APK>")

    apk = Path(sys.argv[1])
    try:
        with zipfile.ZipFile(apk) as archive:
            info = archive.getinfo(APK_LIBRARY)
            library = archive.read(info)
        if info.compress_type != zipfile.ZIP_STORED:
            fail("native library must be uncompressed in the APK")
        with apk.open("rb") as package:
            package.seek(info.header_offset + 26)
            name_length, extra_length = struct.unpack("<HH", package.read(4))
        data_offset = info.header_offset + 30 + name_length + extra_length
        if data_offset % ANDROID_PAGE_SIZE != 0:
            fail("native library is not 16 KB-aligned within the APK")
    except (OSError, zipfile.BadZipFile, KeyError) as error:
        fail(f"cannot read {APK_LIBRARY} from {apk}: {error}")

    check_library(library)
    print("Android APK loader checks passed (arm64-v8a, 16 KB pages, JNI exports)")


if __name__ == "__main__":
    main()
