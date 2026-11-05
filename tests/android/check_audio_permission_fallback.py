#!/usr/bin/env python3
"""Verify that denying Android microphone access keeps standalone audio output available.

JUCE 9.0.5 no longer carries this repository's old explicit removal of the saved
input channels. Its AudioDeviceManager still has the same fallback: the standalone
holder asks for zero inputs when access is denied, and initialiseFromXML retries
with those zero needed inputs if opening the saved setup fails. This checks that
both halves of that path are present, so a JUCE update cannot silently leave
Android without output when the microphone prompt is declined.
"""
from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
STANDALONE_H = (
    ROOT / "JUCE" / "modules" / "juce_audio_plugin_client"
    / "Standalone" / "juce_StandaloneFilterWindow.h"
)
DEVICE_MANAGER_CPP = (
    ROOT / "JUCE" / "modules" / "juce_audio_devices"
    / "audio_io" / "juce_AudioDeviceManager.cpp"
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

    if not re.search(
        r"deviceManager\.initialise\s*\(\s*enableAudioInput\s*\?\s*inputChannels\s*:\s*0"
        r"\s*,\s*outputChannels\s*,\s*savedState\.get\(\)\s*,\s*true\s*,",
        reload_body,
    ):
        fail("device initialization does not preserve output while disabling input")

    device_manager = DEVICE_MANAGER_CPP.read_text(encoding="utf-8")
    initialise_body = method_body(device_manager, "String AudioDeviceManager::initialise (")
    if not re.search(r"numInputChansNeeded\s*=\s*numInputChannelsNeeded\s*;", initialise_body):
        fail("AudioDeviceManager does not carry the disabled input count into XML setup")

    xml_body = method_body(device_manager, "String AudioDeviceManager::initialiseFromXML (")
    if not re.search(
        r"if\s*\(\s*error\.isNotEmpty\(\)\s*&&\s*selectDefaultDeviceOnFailure\s*\)"
        r"\s*error\s*=\s*initialise\s*\(\s*numInputChansNeeded\s*,\s*numOutputChansNeeded"
        r"\s*,\s*nullptr\s*,\s*false\s*,",
        xml_body,
    ):
        fail("XML setup failure does not retry with output-only initialization")

    print("Android audio permission fallback check passed (output remains enabled, input stays disabled)")


if __name__ == "__main__":
    main()
