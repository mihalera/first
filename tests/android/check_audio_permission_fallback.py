#!/usr/bin/env python3
"""Verify that denying Android microphone access keeps standalone audio output available."""
from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
STANDALONE_H = (
    ROOT / "JUCE" / "modules" / "juce_audio_plugin_client"
    / "Standalone" / "juce_StandaloneFilterWindow.h"
)


def fail(message: str) -> None:
    raise SystemExit(f"Android audio permission fallback check failed: {message}")


def method_body(source: str, signature: str) -> str:
    match = re.search(re.escape(signature), source)
    if match is None:
        fail(f"missing {signature}")

    open_brace = source.find("{", match.end())
    if open_brace < 0:
        fail(f"missing body for {signature}")

    start = open_brace + 1
    depth = 1
    position = start
    while position < len(source) and depth:
        if source[position] == "{":
            depth += 1
        elif source[position] == "}":
            depth -= 1
        position += 1

    if depth:
        fail(f"unterminated {signature}")
    return source[start:position - 1]


def main() -> None:
    source = STANDALONE_H.read_text(encoding="utf-8")
    reload_body = method_body(source, "void reloadAudioDeviceState")

    permission_fallback = re.search(
        r"if\s*\(\s*!\s*enableAudioInput\s*&&\s*savedState\s*!=\s*nullptr\s*\)"
        r"\s*savedState->removeAttribute\s*\(\s*\"audioDeviceInChans\"\s*\)\s*;",
        reload_body,
    )
    if permission_fallback is None:
        fail("saved input-channel settings are not disabled when microphone access is denied")

    if not re.search(
        r"deviceManager\.initialise\s*\(\s*enableAudioInput\s*\?\s*inputChannels\s*:\s*0"
        r"\s*,\s*outputChannels",
        reload_body,
    ):
        fail("device initialization does not preserve output while disabling input")

    print("Android audio permission fallback check passed (output remains enabled, input stays disabled)")


if __name__ == "__main__":
    main()
