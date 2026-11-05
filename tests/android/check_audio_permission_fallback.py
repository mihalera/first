#!/usr/bin/env python3
"""Verify that denying Android microphone access keeps standalone audio output available.

JUCE 9.0.5 no longer carries this repository's old explicit removal of the saved
input channels, and its output-only retry is not enough on its own: initialiseFromXML
restores audioDeviceInChans from the saved setup and setAudioDeviceSetup overwrites
numInputChansNeeded with that saved mask, so the retry asks for the microphone again.
The plugin therefore removes the saved input mask before the holder reloads its
device, while the holder's settings are already live in createPluginFilter(). This
checks that project-side guard together with the upstream retry path.
"""
=======
"""Verify that denying Android microphone access keeps standalone audio output available."""
=======
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
PROCESSOR_CPP = ROOT / "Source" / "PluginProcessor.cpp"
=======
DEVICE_MANAGER_CPP = (
    ROOT / "JUCE" / "modules" / "juce_audio_devices"
    / "audio_io" / "juce_AudioDeviceManager.cpp"
)
=======
=======


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
=======
    permission_fallback = re.search(
        r"if\s*\(\s*!\s*enableAudioInput\s*&&\s*savedState\s*!=\s*nullptr\s*\)"
        r"\s*savedState->removeAttribute\s*\(\s*\"audioDeviceInChans\"\s*\)\s*;",
        reload_body,
    )
    if permission_fallback is None:
        fail("saved input-channel settings are not disabled when microphone access is denied")

=======
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

    setup_body = method_body(device_manager, "String AudioDeviceManager::setAudioDeviceSetup (")
    if not re.search(r"numInputChansNeeded\s*=\s*currentSetup\.inputChannels\.countNumberOfSetBits\s*\(\)", setup_body):
        fail("the saved input mask is no longer known to override the disabled input count")

=======
    xml_body = method_body(device_manager, "String AudioDeviceManager::initialiseFromXML (")
    if not re.search(
        r"if\s*\(\s*error\.isNotEmpty\(\)\s*&&\s*selectDefaultDeviceOnFailure\s*\)"
        r"\s*error\s*=\s*initialise\s*\(\s*numInputChansNeeded\s*,\s*numOutputChansNeeded"
        r"\s*,\s*nullptr\s*,\s*false\s*,",
        xml_body,
    ):
        fail("XML setup failure does not retry with output-only initialization")

    processor = PROCESSOR_CPP.read_text(encoding="utf-8")
    filter_body = method_body(processor, "juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()")
    if "dropSavedInputChannelsWithoutPermission()" not in filter_body:
        fail("createPluginFilter does not drop the saved input mask without microphone access")

    guard_body = method_body(processor, "void dropSavedInputChannelsWithoutPermission()")
    if not re.search(r"RuntimePermissions::isGranted\s*\(\s*(?:juce::)?RuntimePermissions::recordAudio\s*\)",
                     guard_body):
        fail("the microphone permission is not checked before the saved mask is used")

    if not re.search(r"StandalonePluginHolder::getInstance\s*\(\s*\)", guard_body):
        fail("the project guard does not reach the live standalone settings")

    if not re.search(r"removeAttribute\s*\(\s*\"audioDeviceInChans\"\s*\)", guard_body):
        fail("the saved input-channel mask is not removed when microphone access is denied")

=======
=======
    print("Android audio permission fallback check passed (output remains enabled, input stays disabled)")


if __name__ == "__main__":
    main()
