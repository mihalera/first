#!/usr/bin/env python3
"""Build the MiBoX icon atlases and product/help illustrations.

Requires Pillow. The script reads the live tab, parameter and model tables, along
with the ordered factory preset list, so the art cannot drift from the plugin.
"""

from __future__ import annotations

import json
import math
import re
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
ASSETS = Path(__file__).resolve().parent
TILE = 64
SCALE = 3
INK = (246, 238, 219, 255)
ACCENT = (237, 173, 61, 255)
TEAL = (72, 204, 182, 255)
PINK = (246, 94, 151, 255)
BLUE = (93, 177, 248, 255)
PURPLE = (171, 137, 248, 255)
MUTED = (130, 137, 137, 255)


def slug(value: str) -> str:
    value = value.strip().lower()
    return re.sub(r"[^a-z0-9]+", "_", value).strip("_")


def cpp_names(path: Path, array_name: str) -> list[str]:
    source = path.read_text(encoding="utf-8")
    match = re.search(rf"\b{re.escape(array_name)}\s*\{{(.*?)\}}", source, re.S)
    if match is None:
        raise SystemExit(f"Could not read {array_name} from {path.relative_to(ROOT)}")
    body = re.sub(r"//[^\n]*", "", match.group(1))
    return re.findall(r'"([^"\n]+)"', body)


def tab_names() -> list[str]:
    source = (ROOT / "Source/GUI/PanelTabs.h").read_text(encoding="utf-8")
    block = re.search(r"tabSpecs\s*\{\s*\{(.*?)\}\s*\};", source, re.S)
    if block is None:
        raise SystemExit("Could not read tabSpecs in Source/GUI/PanelTabs.h")
    return re.findall(r'^\s*\{\s*"([A-Z][A-Z0-9 /-]+)"\s*,', block.group(1), re.M)


def control_ids() -> list[str]:
    source = (ROOT / "Source/GUI/PanelText.h").read_text(encoding="utf-8")
    block = re.search(r"controlSpecs\s*\{\s*\{(.*?)\}\s*\};", source, re.S)
    if block is None:
        raise SystemExit("Could not read controlSpecs in Source/GUI/PanelText.h")
    return re.findall(r'^\s*\{\s*"([a-z0-9_]+)"\s*,', block.group(1), re.M)


def add_defs(target: list[dict], family: str, definitions: list[tuple[str, str, str]]) -> None:
    for key, label, shape in definitions:
        target.append({"key": key, "label": label, "family": family, "shape": shape})


def ui_definitions() -> list[dict]:
    items: list[dict] = []
    tab_shapes = {
        "MACHINE": "reel", "FRONT END": "input", "SATURATION": "wave",
        "CHARACTER": "knob", "NOISE": "wobble", "RECORD": "vinyl",
        "VINYL": "vinyl", "MIX": "sliders", "SUBFUND": "octave",
        "DELAY": "echo", "REVERB": "room", "DYN": "envelope",
        "OUT EQ": "eq", "IN EQ": "eq", "MODELS": "chip",
        "SETTINGS": "gear", "AUTOTUNE": "note", "COMP": "compressor",
        # The creative effects rack's four pages: harmonics, filters, modulators,
        # pitch. Each borrows the glyph its subject already uses elsewhere rather
        # than introducing four new drawings for a bar of navigation.
        "DRIVE": "wave", "SHAPE": "filter", "MOTION": "sine", "PITCH": "note",
=======
    }
    for tab in tab_names():
        if tab not in tab_shapes:
            raise SystemExit(f"No tab icon drawing is assigned for {tab!r}")
        items.append({"key": f"tab.{slug(tab)}", "label": tab,
                      "family": "navigation", "shape": tab_shapes[tab]})

    groups = [
        ("family", "navigation", [
            ("tape", "Tape family", "reel"), ("fx", "Effects family", "echo"),
            ("setup", "Setup family", "gear"), ("menu", "Menu", "menu"),
            ("back", "Back", "back"), ("close", "Close", "close"),
            ("expand", "Expand", "expand"), ("collapse", "Collapse", "collapse"),
        ]),
        ("switch", "controls", [
            ("bypass", "Bypass", "switch"), ("delta", "Delta monitor", "delta"),
            ("polarity", "Polarity", "phase"), ("auto_gain", "Auto gain", "level"),
            ("sync", "Tempo sync", "sync"), ("tempo", "Tempo", "metronome"),
            ("oversampling", "Oversampling", "steps"), ("lofi", "Lo-Fi", "cassette"),
            ("modern", "Modern", "digital"), ("theme", "Theme", "sunmoon"),
            ("language", "Language", "globe"), ("renderer", "Renderer", "screen"),
        ]),
        ("workflow", "presets", [
            ("preset", "Preset", "file"), ("save", "Save", "save"),
            ("delete", "Delete", "trash"), ("import", "Import", "import"),
            ("export", "Export", "export"), ("undo", "Undo", "undo"),
            ("redo", "Redo", "redo"), ("compare", "Compare", "compare"),
            ("copy_a", "Copy to A", "copy"), ("copy_b", "Copy to B", "copy"),
            ("slot_a", "Slot A", "slot_a"), ("slot_b", "Slot B", "slot_b"),
            ("favorite", "Favorite", "star"), ("search", "Search", "search"),
            ("randomize", "Randomize", "dice"), ("reset", "Reset", "reset"),
        ]),
        ("files", "files", [
            ("model", "Neural model", "chip"), ("impulse", "Impulse response", "wave_file"),
            ("browse", "Browse", "folder"), ("load", "Load", "import"),
            ("unload", "Unload", "eject"), ("drop", "Drop file", "download"),
            ("ready", "File ready", "check"), ("missing", "File missing", "missing"),
        ]),
        ("status", "status", [
            ("active", "Active", "check"), ("inactive", "Inactive", "circle"),
            ("warning", "Warning", "warning"), ("error", "Error", "error"),
            ("success", "Success", "check"), ("clipped", "Clipped", "clip"),
            ("silence", "No signal", "silence"), ("signal", "Signal", "wave"),
            ("automation", "Automated", "automation"), ("modified", "Modified", "asterisk"),
            ("default", "Default", "reset"), ("locked", "Locked", "lock"),
            ("cpu", "CPU", "cpu"), ("latency", "Latency", "latency"),
            ("mono", "Mono", "mono"), ("stereo", "Stereo", "stereo"),
        ]),
        ("audio", "audio", [
            ("input", "Input", "input"), ("output", "Output", "output"),
            ("left", "Left channel", "left"), ("right", "Right channel", "right"),
            ("phase_normal", "Normal phase", "phase"),
            ("phase_inverted", "Inverted phase", "phase_inverted"),
            ("peak", "Peak", "peak"), ("rms", "RMS", "rms"),
            ("lufs", "LUFS", "lufs"), ("vu", "VU", "vu"),
            ("limiter", "Limiter", "limiter"), ("width", "Stereo width", "stereo"),
        ]),
        ("platform", "platform", [
            ("windows", "Windows", "windows"), ("macos", "macOS", "macos"),
            ("linux", "Linux", "linux"), ("android", "Android", "android"),
            ("vst3", "VST3", "plugin"), ("au", "Audio Unit", "plugin"),
            ("auv3", "Audio Unit v3", "plugin"),
            ("standalone", "Standalone", "screen"), ("help", "Help", "help"),
        ]),
    ]
    for prefix, family, definitions in groups:
        add_defs(items, family, [(f"{prefix}.{key}", label, shape)
                                 for key, label, shape in definitions])
    return items


def parameter_definitions() -> list[dict]:
    shapes = {
        "input": "input", "drive": "wave", "bias": "magnet", "tone": "eq",
        "character": "knob", "wow": "wobble", "flutter": "wobble", "mix": "sliders",
        "output": "output", "stereo_width": "stereo", "tape_source": "reel",
        "valve_source": "valve", "cassette_source": "cassette", "vinyl_source": "vinyl",
        "amp_source": "amp", "transformer_source": "transformer", "digital_source": "digital",
        "amp_bias": "magnet", "sag": "envelope", "presence": "spark", "cabinet": "speaker",
        "delay_time": "clock", "delay_feedback": "echo", "st_offset": "stereo",
        "noise": "noise", "subfund": "octave", "preamp": "input", "distortion": "wave",
        "flux": "magnet", "wear": "scratch", "mechanics": "gear", "reverb": "room",
        "reverb_size": "room", "vinyl": "vinyl", "vinyl_crackle": "noise",
        "vinyl_rumble": "wobble", "noise_lvl": "level", "st_link": "link",
        "vinyl_dust": "dust", "vinyl_scratch": "scratch", "vinyl_warp": "wobble",
        "vinyl_electrical": "spark", "vinyl_clicks": "pulse", "in_low": "eq_low",
        "in_mid": "eq_mid", "in_high": "eq_high", "out_low": "eq_low",
        "out_mid": "eq_mid", "out_high": "eq_high", "di": "input", "di_load": "coil",
        "di_transformer": "transformer", "delay_pingpong": "pingpong",
        "transient_attack": "envelope", "transient_sustain": "envelope",
        "transient_mix": "sliders", "neural_mix": "chip", "ir_mix": "wave_file",
        "reverb_predelay": "clock", "reverb_damp": "filter", "reverb_width": "stereo",
        "delay_tone": "eq", "delay_lowcut": "filter", "delay_damp": "filter",
        "reverb_lowcut": "filter", "in_hp_freq": "filter", "in_lp_freq": "filter",
        "in_eq_q": "eq_mid", "out_hp_freq": "filter", "out_lp_freq": "filter",
        "out_eq_q": "eq_mid", "in_eq_order": "steps", "out_eq_order": "steps",
        # The effects rack, one glyph per stage, in controlSpecs order.
        "fx_wah": "filter", "fx_anti_spike": "pulse", "fx_gate": "silence",
        "fx_overdrive": "wave", "fx_fuzz": "wave", "fx_wavefolder": "wave",
        "fx_bitcrusher": "steps", "fx_rate_crusher": "pulse",
        "fx_ring_mod": "sine", "fx_octaver": "octave", "fx_pitch": "note",
        "fx_comb": "filter", "fx_formant": "eq_mid", "fx_dynamic_eq": "eq",
        "fx_de_esser": "filter", "fx_chorus": "stereo", "fx_flanger": "wobble",
        "fx_phaser": "phase", "fx_tremolo": "envelope", "fx_vibrato": "wobble",
        "fx_rotary": "reel", "fx_vocoder": "chip", "fx_granular": "spark",
        "fx_spectral": "eq_high", "fx_speed": "clock", "comp_upward": "compressor",
=======
    }
    result = []
    for key in control_ids():
        if key not in shapes:
            raise SystemExit(f"No parameter glyph assigned for {key}")
        result.append({"key": f"parameter.{key}", "label": key.replace("_", " ").upper(),
                       "family": "parameter", "shape": shapes[key]})
    return result


def model_definitions() -> list[dict]:
    sources = [
        ("tape", ROOT / "Source/PluginProcessor.h", "tapeStockNames", "reel"),
        ("valve", ROOT / "Source/DSP/SaturationCore.h", "valveTypeNames", "valve"),
        ("amp", ROOT / "Source/DSP/SaturationCore.h", "ampTypeNames", "amp"),
        ("transformer", ROOT / "Source/DSP/SaturationCore.h", "transformerTypeNames", "transformer"),
        ("digital", ROOT / "Source/DSP/SaturationCore.h", "digitalTypeNames", "digital"),
        ("vinyl", ROOT / "Source/PluginProcessor.h", "vinylTypeNames", "vinyl"),
    ]
    result = []
    for family, path, array_name, shape in sources:
        for index, label in enumerate(cpp_names(path, array_name)):
            result.append({"key": f"model.{family}.{slug(label)}", "label": label,
                           "family": family, "shape": "off" if label.lower() == "off" else shape,
                           "variant": index})
    for key, label, shape in [
        ("model.principle.tape", "Tape", "reel"),
        ("model.principle.valve", "Valve", "valve"),
        ("model.principle.cassette", "Cassette", "cassette"),
        ("model.principle.vinyl", "Vinyl", "vinyl"),
        ("model.principle.amp", "Amplifier", "amp"),
        ("model.principle.transformer", "Transformer", "transformer"),
        ("model.principle.digital", "Digital", "digital"),
        ("model.neural", "Neural model", "chip"),
        ("model.impulse_response", "Impulse response", "wave_file"),
        ("model.cabinet", "Cabinet", "speaker"),
        ("model.room", "Room", "room"),
    ]:
        result.append({"key": key, "label": label, "family": "model", "shape": shape})
    return result


def draw_icon(name: str, shape: str, variant: int = 0, tile: int = TILE) -> Image.Image:
    size = tile * SCALE
    image = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(image)
    s = SCALE
    cx = cy = tile / 2
    color = INK
    width = 2.5 * s

    def xy(x: float, y: float) -> tuple[int, int]:
        return round(x * s), round(y * s)

    def line(points: list[tuple[float, float]], fill=color, w=width, joint="curve") -> None:
        d.line([xy(x, y) for x, y in points], fill=fill, width=max(1, round(w)), joint=joint)

    def ellipse(box: tuple[float, float, float, float], outline=color, w=width, fill=None) -> None:
        d.ellipse(tuple(round(v * s) for v in box), outline=outline,
                  width=max(1, round(w)), fill=fill)

    def rect(box: tuple[float, float, float, float], outline=color, w=width,
             radius=5, fill=None) -> None:
        d.rounded_rectangle(tuple(round(v * s) for v in box), radius=round(radius * s),
                            outline=outline, width=max(1, round(w)), fill=fill)

    def arc(box: tuple[float, float, float, float], start: float, end: float,
            fill=color, w=width) -> None:
        d.arc(tuple(round(v * s) for v in box), start, end, fill=fill, width=max(1, round(w)))

    def dots(points: list[tuple[float, float]], radius=1.7, fill=color) -> None:
        for x, y in points:
            ellipse((x-radius, y-radius, x+radius, y+radius), outline=fill, w=0, fill=fill)

    if shape in {"reel", "vinyl"}:
        ellipse((13, 13, 51, 51), w=width)
        ellipse((21, 21, 43, 43), w=2 * s)
        ellipse((28, 28, 36, 36), w=0, fill=ACCENT)
        if shape == "vinyl":
            for inset in range(1 + variant % 3):
                ellipse((16 + inset*2, 16 + inset*2, 48 - inset*2, 48 - inset*2), w=s)
            arc((20, 20, 44, 44), 195, 345, w=s)
        elif shape == "reel":
            hole_sets = [
                [(32, 24)], [(24.5, 36), (39.5, 36)],
                [(32, 24), (24.5, 36), (39.5, 36)],
                [(25, 27), (39, 27), (25, 38), (39, 38)],
                [(32, 23), (24, 30), (40, 30), (27, 41), (37, 41)],
            ]
            dots(hole_sets[variant % len(hole_sets)], radius=2)
    elif shape == "knob":
        arc((13, 13, 51, 51), 135, 405, fill=INK, w=3*s)
        ellipse((21, 21, 43, 43), w=2*s)
        line([(32, 32), (43, 22)], fill=ACCENT, w=3*s)
        ellipse((29, 29, 35, 35), w=0, fill=ACCENT)
    elif shape == "magnet":
        line([(20, 15), (20, 34), (24, 42), (40, 42), (44, 34), (44, 15)], w=4*s)
        line([(17, 15), (25, 15)], fill=TEAL, w=4*s)
        line([(39, 15), (47, 15)], fill=PINK, w=4*s)
        line([(24, 42), (40, 42)], fill=ACCENT, w=3*s)
    elif shape == "off":
        ellipse((13, 13, 51, 51), outline=MUTED, w=2*s)
        line([(18, 46), (46, 18)], fill=PINK, w=3*s)
    elif shape in {"cassette", "file", "wave_file", "plugin", "screen", "slot_a", "slot_b"}:
        rect((10, 13, 54, 51), radius=5)
        if shape == "cassette":
            ellipse((18, 23, 29, 34), w=2*s)
            ellipse((35, 23, 46, 34), w=2*s)
            line([(18, 40), (46, 40)], fill=ACCENT, w=2*s)
            rect((24, 35, 40, 43), radius=2, w=s)
        elif shape == "wave_file":
            line([(15, 33), (20, 33), (24, 23), (28, 42), (32, 27),
                  (36, 38), (41, 30), (48, 30)], fill=TEAL, w=2.2*s)
            line([(44, 13), (44, 22), (53, 22)], w=2*s)
        elif shape == "plugin":
            for y in (22, 32, 42):
                line([(16, y), (48, y)], w=s)
            dots([(20, 22), (37, 32), (28, 42)], 2.2)
        elif shape == "slot_a":
            d.text(xy(18, 20), "A", fill=ACCENT, stroke_width=0)
            line([(19, 42), (45, 42)], w=2*s)
        elif shape == "slot_b":
            d.text(xy(18, 20), "B", fill=TEAL, stroke_width=0)
            line([(19, 42), (45, 42)], w=2*s)
        else:
            line([(18, 23), (46, 23)], w=2*s)
            line([(18, 32), (42, 32)], w=2*s)
            line([(18, 41), (38, 41)], w=2*s)
            if shape == "screen":
                line([(24, 55), (40, 55), (32, 51), (32, 55)], fill=ACCENT, w=2*s)
    elif shape in {"amp", "speaker", "room"}:
        rect((14, 10, 50, 54), radius=3)
        if shape == "room":
            arc((20, 20, 44, 44), 190, 350, fill=TEAL, w=2*s)
            arc((16, 16, 48, 48), 195, 345, fill=MUTED, w=s)
            line([(20, 45), (44, 45)], w=2*s)
        else:
            if variant % 2 == 0:
                ellipse((21, 19, 43, 41), w=2*s)
                ellipse((27, 25, 37, 35), w=s)
            else:
                ellipse((18, 20, 30, 34), w=2*s)
                ellipse((34, 20, 46, 34), w=2*s)
                ellipse((22, 37, 42, 48), w=s)
            line([(20, 47), (44, 47)], fill=ACCENT, w=2*s)
    elif shape in {"valve", "transformer", "coil"}:
        if shape == "valve":
            rect((21, 12, 43, 48), radius=10)
            line([(27, 48), (27, 54)], w=2*s)
            line([(37, 48), (37, 54)], w=2*s)
            for row in range(1 + variant % 3):
                y = 22 + row * 8
                line([(26, y), (38, y)], fill=ACCENT if row == 0 else INK, w=2*s)
        else:
            line([(26, 12), (26, 52)], fill=ACCENT, w=2*s)
            line([(38, 12), (38, 52)], fill=ACCENT, w=2*s)
            loop_count = 2 + variant % 4
            for i in range(loop_count):
                y = 16 + i * (32 / max(1, loop_count - 1))
                arc((16, y-5, 36, y+5), 275, 85, w=2*s)
                arc((28, y-5, 48, y+5), 95, 265, w=2*s)
    elif shape in {"digital", "steps", "octave", "pulse", "latency", "cpu", "chip"}:
        if shape in {"cpu", "chip"}:
            rect((19, 19, 45, 45), radius=3)
            rect((25, 25, 39, 39), radius=2, w=s)
            for pos in (23, 32, 41):
                line([(pos, 11), (pos, 19)], w=2*s)
                line([(pos, 45), (pos, 53)], w=2*s)
                line([(11, pos), (19, pos)], w=2*s)
                line([(45, pos), (53, pos)], w=2*s)
            if shape == "chip":
                dots([(32, 32)], 2.2, TEAL)
        else:
            steps_count = 2 + variant % 4
            points = [(13, 45)]
            for step in range(steps_count):
                x = 13 + (step + 1) * (32 / steps_count)
                y = 45 - (step + 1) * (26 / steps_count)
                points.extend([(x, points[-1][1]), (x, y)])
            points.append((51, points[-1][1]))
            line(points, fill=BLUE if shape == "digital" else INK, w=3*s)
            if shape == "octave":
                line([(16, 51), (26, 51), (26, 45), (36, 45), (36, 39), (49, 39)],
                     fill=TEAL, w=2*s)
            elif shape == "pulse":
                dots([(21, 43), (29, 34), (37, 25)], 2, ACCENT)
            elif shape == "latency":
                ellipse((42, 35, 52, 45), w=2*s)
    elif shape in {"eq", "eq_low", "eq_mid", "eq_high", "sliders", "filter"}:
        for x, y in [(19, 22), (32, 32), (45, 24)]:
            line([(x, 13), (x, 51)], w=2*s)
            ellipse((x-4, y-4, x+4, y+4), outline=ACCENT if x == 32 else INK, w=2*s)
        if shape == "eq_low":
            line([(15, 43), (27, 43)], fill=TEAL, w=2*s)
        elif shape == "eq_high":
            line([(39, 17), (51, 17)], fill=TEAL, w=2*s)
        elif shape == "filter":
            line([(13, 47), (24, 47), (31, 32), (38, 20), (51, 20)], fill=TEAL, w=2*s)
    elif shape in {"wave", "wobble", "noise", "delta", "rms", "peak", "lufs", "vu", "clip", "limiter", "envelope", "silence", "echo"}:
        if shape == "delta":
            line([(13, 32), (23, 32), (29, 20), (35, 44), (41, 28), (51, 28)], fill=PINK, w=2.5*s)
            line([(13, 45), (51, 45)], fill=MUTED, w=s)
        elif shape == "silence":
            line([(13, 32), (25, 32)], fill=MUTED, w=2*s)
            line([(39, 32), (51, 32)], fill=MUTED, w=2*s)
            line([(28, 27), (36, 37)], fill=PINK, w=2*s)
        elif shape == "echo":
            line([(13, 25), (20, 25), (25, 39), (31, 15), (38, 25), (44, 25),
                  (48, 34), (52, 20)], fill=TEAL, w=2.2*s)
            line([(16, 47), (48, 47)], fill=MUTED, w=s)
        elif shape == "envelope":
            line([(13, 44), (20, 42), (25, 14), (30, 27), (37, 33), (51, 39)], fill=TEAL, w=3*s)
        else:
            amp = 5 if shape in {"rms", "vu"} else 14
            points = []
            for x in range(12, 53, 2):
                phase = (x-12) / 40 * math.pi * (2.0 + (variant % 3) * 0.5)
                factor = 0.56 + 0.44 * math.sin(x * 0.35 + variant)
                points.append((x, 32 + math.sin(phase) * amp * factor))
            line(points, fill=PINK if shape in {"clip", "limiter"} else INK, w=2.4*s)
            if shape in {"peak", "lufs", "vu", "clip", "rms"}:
                line([(12, 52), (52, 52)], fill=MUTED, w=s)
                if shape == "clip":
                    rect((43, 11, 52, 19), outline=PINK, w=s, radius=2, fill=PINK)
                else:
                    dots([(18, 32), (32, 32), (46, 32)], 1.5, ACCENT)
    elif shape in {"input", "output", "left", "right", "export", "import", "download", "eject", "back", "undo", "redo", "copy", "link", "pingpong"}:
        line([(12, 32), (49, 32)], w=3*s)
        direction = "left" if shape in {"input", "import", "back", "undo", "right"} else "right"
        if shape in {"left", "right"}:
            direction = shape
        tip = 15 if direction == "left" else 49
        sign = 1 if direction == "left" else -1
        line([(tip-sign*10, 23), (tip, 32), (tip-sign*10, 41)], fill=ACCENT, w=3*s)
        if shape in {"undo", "redo", "copy", "link", "pingpong"}:
            arc((20, 17, 46, 45), 190, 350, fill=TEAL if shape == "redo" else INK, w=2*s)
        if shape == "copy":
            rect((19, 19, 39, 43), w=s, radius=2)
            rect((27, 23, 47, 47), w=2*s, radius=2)
    elif shape in {"switch", "phase", "phase_inverted", "level", "automation", "compare", "sync", "metronome", "clock", "warning", "error", "check", "missing", "circle", "asterisk", "star", "search", "dice", "reset", "lock", "globe", "sunmoon", "gear", "settings", "menu", "expand", "collapse", "help", "trash", "save", "folder", "mono", "stereo", "magnet", "spark", "scratch", "dust", "sine", "note", "close", "screw_slot", "screw_head", "washer", "compressor", "windows", "macos", "linux", "android"}:
        if shape in {"switch", "level", "automation", "lock"}:
            rect((13, 21, 51, 43), radius=11)
            x = 39 if shape != "lock" else 32
            ellipse((x-7, 25, x+7, 39), outline=ACCENT if shape == "switch" else INK, w=2*s)
            if shape == "lock":
                arc((24, 12, 40, 34), 180, 360, w=2*s)
        elif shape in {"clock", "sync", "metronome"}:
            ellipse((13, 13, 51, 51), w=2*s)
            line([(32, 19), (32, 32), (43, 38)], fill=ACCENT, w=2*s)
            if shape == "metronome":
                line([(21, 49), (32, 15), (43, 49)], w=2*s)
        elif shape in {"compare", "stereo", "mono", "phase", "phase_inverted", "link", "pingpong"}:
            if shape == "mono":
                line([(32, 14), (32, 50)], w=3*s)
                line([(20, 20), (44, 20)], fill=ACCENT, w=2*s)
            else:
                arc((12, 15, 45, 47), 285, 75, w=2*s)
                arc((19, 15, 52, 47), 105, 255, fill=TEAL, w=2*s)
                if shape == "phase_inverted":
                    line([(15, 49), (49, 15)], fill=PINK, w=3*s)
        elif shape in {"check", "error", "warning", "missing"}:
            color = TEAL if shape == "check" else PINK if shape == "error" else ACCENT
            ellipse((12, 12, 52, 52), outline=color, w=2*s)
            if shape == "check":
                line([(21, 32), (29, 40), (44, 23)], fill=color, w=3*s)
            elif shape == "warning":
                line([(32, 21), (32, 35)], fill=color, w=3*s)
                dots([(32, 43)], 1.5, color)
            else:
                line([(23, 23), (41, 41)], fill=color, w=3*s)
                line([(41, 23), (23, 41)], fill=color, w=3*s)
        elif shape in {"gear", "settings", "menu", "expand", "collapse", "trash", "save", "folder", "search", "reset", "star", "dice", "help", "globe", "sunmoon", "asterisk", "spark", "dust", "scratch", "sine", "magnet", "note", "close", "screw_slot", "screw_head", "washer", "compressor", "windows", "macos", "linux", "android"}:
            if shape in {"gear", "settings"}:
                ellipse((20, 20, 44, 44), w=3*s)
                ellipse((28, 28, 36, 36), w=2*s)
                for angle in range(0, 360, 45):
                    a = math.radians(angle)
                    line([(32+13*math.cos(a), 32+13*math.sin(a)),
                          (32+21*math.cos(a), 32+21*math.sin(a))], w=3*s)
            elif shape == "note":
                line([(36, 16), (36, 40), (27, 43)], fill=TEAL, w=3*s)
                line([(36, 16), (49, 13), (49, 36)], fill=TEAL, w=3*s)
                ellipse((20, 39, 30, 48), outline=TEAL, w=2*s, fill=TEAL)
                ellipse((42, 34, 52, 43), outline=TEAL, w=2*s, fill=TEAL)
            elif shape == "close":
                line([(19, 19), (45, 45)], fill=PINK, w=4*s)
                line([(45, 19), (19, 45)], fill=PINK, w=4*s)
            elif shape == "washer":
                ellipse((12, 12, 52, 52), w=3*s)
                ellipse((25, 25, 39, 39), outline=ACCENT, w=2*s)
            elif shape in {"screw_slot", "screw_head"}:
                ellipse((14, 14, 50, 50), w=2*s)
                if shape == "screw_slot":
                    line([(21, 43), (43, 21)], fill=ACCENT, w=3*s)
                else:
                    line([(22, 22), (42, 42)], fill=ACCENT, w=3*s)
                    line([(42, 22), (22, 42)], fill=ACCENT, w=3*s)
            elif shape == "compressor":
                line([(15, 47), (24, 45), (29, 39), (32, 25), (39, 19), (50, 17)], fill=TEAL, w=3*s)
                line([(14, 50), (50, 50)], fill=MUTED, w=s)
                line([(14, 50), (14, 14)], fill=MUTED, w=s)
            elif shape in {"windows", "macos", "linux", "android"}:
                if shape == "windows":
                    rect((14, 16, 50, 48), w=2*s, radius=2)
                    line([(32, 16), (32, 48)], w=2*s)
                    line([(14, 32), (50, 32)], w=2*s)
                elif shape == "macos":
                    ellipse((15, 16, 49, 49), outline=INK, w=2*s)
                    ellipse((28, 18, 45, 38), outline=ACCENT, w=2*s)
                    line([(34, 14), (40, 9)], fill=ACCENT, w=2*s)
                elif shape == "linux":
                    ellipse((22, 15, 42, 43), outline=TEAL, w=2*s)
                    ellipse((26, 24, 29, 27), outline=INK, w=0, fill=INK)
                    ellipse((35, 24, 38, 27), outline=INK, w=0, fill=INK)
                    line([(29, 31), (32, 34), (35, 31)], fill=ACCENT, w=2*s)
                    line([(25, 43), (20, 51)], w=2*s)
                    line([(39, 43), (44, 51)], w=2*s)
                else:
                    arc((17, 18, 47, 48), 180, 360, fill=TEAL, w=2*s)
                    line([(17, 33), (17, 48), (47, 48), (47, 33)], fill=TEAL, w=2*s)
                    line([(23, 19), (19, 12)], w=2*s)
                    line([(41, 19), (45, 12)], w=2*s)
            elif shape in {"expand", "collapse"}:
                if shape == "expand":
                    line([(25, 25), (14, 14)], fill=ACCENT, w=3*s)
                    line([(14, 14), (14, 23)], fill=ACCENT, w=3*s)
                    line([(14, 14), (23, 14)], fill=ACCENT, w=3*s)
                    line([(39, 25), (50, 14)], fill=ACCENT, w=3*s)
                    line([(50, 14), (41, 14)], fill=ACCENT, w=3*s)
                    line([(50, 14), (50, 23)], fill=ACCENT, w=3*s)
                    line([(25, 39), (14, 50)], fill=ACCENT, w=3*s)
                    line([(14, 50), (14, 41)], fill=ACCENT, w=3*s)
                    line([(14, 50), (23, 50)], fill=ACCENT, w=3*s)
                    line([(39, 39), (50, 50)], fill=ACCENT, w=3*s)
                    line([(50, 50), (41, 50)], fill=ACCENT, w=3*s)
                    line([(50, 50), (50, 41)], fill=ACCENT, w=3*s)
                else:
                    line([(14, 14), (25, 25)], fill=TEAL, w=3*s)
                    line([(14, 14), (23, 14)], fill=TEAL, w=3*s)
                    line([(14, 14), (14, 23)], fill=TEAL, w=3*s)
                    line([(50, 14), (39, 25)], fill=TEAL, w=3*s)
                    line([(50, 14), (41, 14)], fill=TEAL, w=3*s)
                    line([(50, 14), (50, 23)], fill=TEAL, w=3*s)
                    line([(14, 50), (25, 39)], fill=TEAL, w=3*s)
                    line([(14, 50), (23, 50)], fill=TEAL, w=3*s)
                    line([(14, 50), (14, 41)], fill=TEAL, w=3*s)
                    line([(50, 50), (39, 39)], fill=TEAL, w=3*s)
                    line([(50, 50), (41, 50)], fill=TEAL, w=3*s)
                    line([(50, 50), (50, 41)], fill=TEAL, w=3*s)
            elif shape in {"asterisk", "spark"}:
                ray_count = 8 if shape == "asterisk" else 4
                for ray in range(ray_count):
                    angle = -math.pi/2 + ray * 2*math.pi/ray_count
                    inner = 8 if shape == "asterisk" else 5
                    outer = 18 if shape == "asterisk" or ray % 2 == 0 else 11
                    line([(32+inner*math.cos(angle),32+inner*math.sin(angle)),
                          (32+outer*math.cos(angle),32+outer*math.sin(angle))],
                         fill=ACCENT if ray % 2 == 0 else TEAL, w=2.5*s)
            elif shape == "scratch":
                arc((14, 14, 50, 50), 195, 345, fill=MUTED, w=2*s)
                line([(20, 40), (27, 34), (31, 38), (38, 25)], fill=PINK, w=2.5*s)
                line([(24, 45), (43, 20)], fill=ACCENT, w=2*s)
            elif shape == "dust":
                dots([(18, 22), (29, 15), (43, 24), (22, 36), (37, 43), (48, 37)],
                     2.2, MUTED)
                dots([(31, 29)], 1.8, ACCENT)
            elif shape == "sine":
                points = [(x, 32 + 13*math.sin((x-12)*math.pi/10)) for x in range(12, 53)]
                line(points, fill=TEAL, w=2.5*s)
            elif shape == "folder":
                line([(11, 21), (27, 21), (31, 26), (53, 26), (49, 47), (14, 47), (11, 21)])
            elif shape == "search":
                ellipse((13, 12, 41, 40), w=3*s)
                line([(36, 37), (51, 52)], w=4*s)
            elif shape == "star":
                pts=[]
                for i in range(10):
                    a=-math.pi/2+i*math.pi/5
                    r=19 if i%2==0 else 8
                    pts.append((32+r*math.cos(a),32+r*math.sin(a)))
                line(pts+[pts[0]], fill=ACCENT, w=2*s)
            elif shape == "menu":
                for y in (20,32,44): line([(15,y),(49,y)], w=3*s)
            elif shape == "trash":
                line([(18,21),(46,21)], w=3*s); rect((21,24,43,51),w=2*s,radius=2)
                line([(26,15),(38,15)],w=3*s)
            elif shape == "save":
                rect((14,12,50,52),w=2*s,radius=4)
                rect((22,13,42,26),w=2*s,radius=1)
                rect((22,36,42,49),w=2*s,radius=2)
            elif shape == "reset":
                arc((13,13,51,51),45,310,fill=ACCENT,w=3*s)
                line([(14,19),(14,31),(25,31)],fill=ACCENT,w=3*s)
            elif shape == "dice":
                rect((13,13,51,51),w=2*s,radius=5)
                dots([(22,22),(42,22),(32,32),(22,42),(42,42)],2.2)
            elif shape == "help":
                ellipse((13,13,51,51),w=2*s)
                arc((22,20,42,39),190,350,fill=ACCENT,w=3*s)
                line([(32,38),(32,40)],w=3*s); dots([(32,46)],1.4)
            elif shape == "sunmoon":
                ellipse((14,18,42,46),outline=ACCENT,w=2*s)
                arc((25,12,52,48),80,280,fill=TEAL,w=3*s)
            elif shape == "globe":
                ellipse((13,13,51,51),w=2*s); ellipse((24,13,40,51),w=s)
                line([(14,25),(50,25)],w=s); line([(14,39),(50,39)],w=s)
            else:
                raise ValueError(f"No semantic vector drawing is defined for icon shape {shape!r} ({name})")
        elif shape == "circle":
            ellipse((13,13,51,51), w=2*s)
            dots([(32,32)],3,ACCENT)
        else:
            raise ValueError(f"No semantic vector drawing is defined for icon shape {shape!r} ({name})")
    else:
        raise ValueError(f"No vector drawing is defined for icon shape {shape!r} ({name})")

    image = image.resize((tile, tile), Image.Resampling.LANCZOS)
    return image


def atlas_image(items: list[dict], filename: str, columns: int = 8,
                tile: int = TILE) -> tuple[dict, Image.Image]:
    rows = math.ceil(len(items) / columns)
    image = Image.new("RGBA", (columns * tile, rows * tile), (0, 0, 0, 0))
    for index, item in enumerate(items):
        item["tile"] = index
        cell = draw_icon(item["key"], item["shape"], item.get("variant", 0), tile)
        image.alpha_composite(cell, ((index % columns) * tile, (index // columns) * tile))
    entry = {"file": filename, "columns": columns, "rows": rows,
             "tile_size": tile, "width": image.width, "height": image.height,
             "items": items, "tintable": True}
    return entry, image


def preset_art_atlas() -> tuple[dict, Image.Image]:
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    block = re.search(r"set\(FACTORY_PRESET_FILES\s*(.*?)\)", cmake, re.S)
    if block is None:
        raise SystemExit("Could not read FACTORY_PRESET_FILES from CMakeLists.txt")
    order = re.findall(r"\$\{FACTORY_PRESET_DIR\}/([a-z0-9_]+)\.json", block.group(1))
    if not order:
        raise SystemExit("FACTORY_PRESET_FILES is empty")

    preset_dir = ROOT / "Source/Presets/Factory"
    source_names = ["tape", "valve", "cassette", "vinyl", "amp", "transformer", "digital"]
    source_ids = [f"{name}_source" for name in source_names]
    shapes = {"tape": "reel", "valve": "valve", "cassette": "cassette",
              "vinyl": "vinyl", "amp": "amp", "transformer": "transformer",
              "digital": "digital"}
    colors = {"tape": ACCENT, "valve": (245, 118, 73, 255),
              "cassette": TEAL, "vinyl": PURPLE, "amp": PINK,
              "transformer": (178, 207, 82, 255), "digital": BLUE}
    atlas = Image.new("RGBA", (len(order) * TILE, TILE), (0, 0, 0, 0))
    items=[]

    for index, name in enumerate(order):
        path = preset_dir / f"{name}.json"
        if not path.is_file():
            raise SystemExit(f"Factory preset is missing: {path.relative_to(ROOT)}")
        values = json.loads(path.read_text(encoding="utf-8"))
        mix = [max(0.0, float(values.get(key, 0.0))) for key in source_ids]
        dominant = max(range(len(mix)), key=lambda i: mix[i]) if any(mix) else 0
        machine = source_names[dominant]
        accent = colors[machine]
        size = TILE * SCALE
        tile = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        d = ImageDraw.Draw(tile)
        s = SCALE
        d.rounded_rectangle((3*s, 3*s, 61*s, 61*s), radius=7*s,
                            fill=(19, 23, 25, 255), outline=accent, width=2*s)
        d.rounded_rectangle((6*s, 6*s, 58*s, 58*s), radius=5*s,
                            outline=(76, 83, 83, 255), width=s)

        # Two lamps reveal drive and stereo width without requiring any text.
        for lamp, x in enumerate((48, 55)):
            amount = float(values.get("drive" if lamp == 0 else "stereo_width", 0.0))
            lamp_color = accent if amount > (0.30 if lamp == 0 else 0.55) else MUTED
            r = 1.7 * s
            d.ellipse(((x-r)*s, 9*s-r*s, (x+r)*s, 9*s+r*s), fill=lamp_color)

        # A slim waveform at the top shifts with drive, wow and flutter.
        drive = max(0.0, min(1.0, float(values.get("drive", 0.0))))
        wow = max(0.0, min(1.0, float(values.get("wow", 0.0))))
        flutter = max(0.0, min(1.0, float(values.get("flutter", 0.0))))
        tone = max(0.0, min(1.0, float(values.get("tone", 0.5))))
        wave=[]
        for x in range(8, 46):
            phase=(x-8) / 38 * math.pi * (2.5 + drive * 3.5 + (tone-0.5)*0.8)
            modulation=1.0 + wow*0.55*math.sin(x*0.31) + flutter*0.25*math.sin(x*0.93)
            amplitude=2.5 + drive*5.2
            wave.append((x*s, (16 + math.sin(phase)*amplitude*modulation)*s))
        d.line(wave, fill=accent, width=2*s, joint="curve")

        glyph = draw_icon(name, shapes[machine], variant=index).resize((33*s, 33*s),
                                                                       Image.Resampling.LANCZOS)
        tile.alpha_composite(glyph, (15*s, 19*s))
        d.line([(9*s, 53*s), (55*s, 53*s)], fill=(62, 69, 70, 255), width=s)

        total = sum(mix)
        if total <= 0.0001:
            mix = [1.0] + [0.0] * (len(source_ids)-1)
            total = 1.0
        for machine_index, amount in enumerate(mix):
            x = (10 + machine_index*7) * s
            d.rounded_rectangle((x, 55*s, x+4*s, 59*s), radius=s,
                                fill=(44, 50, 51, 255), outline=(99, 105, 102, 255), width=s)
            height = max(1, round(3 * amount / total * 4))
            d.rectangle((x+s, 59*s-height, x+3*s, 59*s), fill=colors[source_names[machine_index]])

        tile = tile.resize((TILE, TILE), Image.Resampling.LANCZOS)
        atlas.alpha_composite(tile, (index*TILE, 0))
        items.append({"key": f"preset.{name}", "label": values.get("name", name),
                      "family": "factory_preset", "tile": index})

    return {"file":"factory_preset_art.png", "columns":len(order), "rows":1,
            "tile_size":TILE, "width":atlas.width, "height":atlas.height,
            "items":items, "tintable":False,
            "tile_order":order}, atlas


def category_definitions() -> list[dict]:
    entries = [
        ("mixing", "Mixing", "sliders"), ("mastering", "Mastering", "limiter"),
        ("drums", "Drums", "pulse"), ("vocals", "Vocals", "note"),
        ("bass", "Bass", "wobble"), ("guitar", "Guitar", "amp"),
        ("keys", "Keys", "digital"), ("creative", "Creative", "spark"),
    ]
    return [{"key": f"category.{key}", "label": label, "family": "preset_category",
             "shape": shape} for key, label, shape in entries]


def meter_definitions() -> list[dict]:
    entries = [
        ("meter.input", "Input", "input"), ("meter.output", "Output", "output"),
        ("meter.peak", "Peak", "peak"), ("meter.rms", "RMS", "rms"),
        ("meter.lufs", "LUFS", "lufs"), ("meter.vu", "VU", "vu"),
        ("meter.clipping", "Clipping", "clip"), ("meter.limiter", "Limiter", "limiter"),
        ("meter.silence", "Silence", "silence"), ("meter.signal", "Signal", "wave"),
        ("meter.mono", "Mono", "mono"), ("meter.stereo", "Stereo", "stereo"),
        ("meter.phase", "Phase", "phase"), ("meter.width", "Width", "stereo"),
        ("meter.cpu", "CPU", "cpu"), ("meter.latency", "Latency", "latency"),
        ("meter.warning", "Warning", "warning"), ("meter.error", "Error", "error"),
        ("meter.ok", "Healthy", "check"), ("meter.automation", "Automation", "automation"),
        ("meter.noise", "Noise", "noise"), ("meter.transient", "Transient", "pulse"),
        ("meter.low", "Low band", "eq_low"), ("meter.mid", "Mid band", "eq_mid"),
        ("meter.high", "High band", "eq_high"), ("meter.left", "Left", "left"),
        ("meter.right", "Right", "right"), ("meter.delta", "Delta", "delta"),
        ("meter.bypass", "Bypass", "switch"), ("meter.saturation", "Saturation", "wave"),
        ("meter.delay", "Delay", "echo"), ("meter.room", "Room", "room"),
    ]
    return [{"key": key, "label": label, "family": "meter_status", "shape": shape}
            for key, label, shape in entries]


def hardware_definitions() -> list[dict]:
    entries = [
        ("screw", "Cross-head screw", "screw_head"), ("screw_slot", "Slotted screw", "screw_slot"),
        ("washer", "Washer", "washer"), ("rivet", "Rivet", "circle"),
        ("vent", "Vent", "sliders"), ("label_plate", "Label plate", "file"),
        ("jack", "Jack", "output"), ("toggle", "Toggle", "switch"),
        ("knob_tick", "Knob scale", "knob"), ("dial_face", "Dial face", "clock"),
        ("meter_frame", "Meter frame", "vu"), ("safety_mark", "Safety mark", "warning"),
        ("edge_seam", "Panel seam", "link"), ("reel_hub", "Reel hub", "reel"),
        ("badge_plate", "Badge plate", "plugin"), ("status_lamp", "Status lamp", "spark"),
    ]
    return [{"key": f"hardware.{key}", "label": label, "family": "panel_hardware",
             "shape": shape} for key, label, shape in entries]


def brand_atlas() -> tuple[dict, Image.Image]:
    source = Image.open(ASSETS / "brand_mark.png").convert("RGBA")
    items = [
        {"key": "brand.square", "label": "Square brand mark", "tile": 0},
        {"key": "brand.horizontal", "label": "Horizontal wordmark", "tile": 1},
        {"key": "brand.monochrome", "label": "Monochrome mark", "tile": 2},
        {"key": "brand.reversed", "label": "Reversed mark", "tile": 3},
    ]
    tile = 128
    image = Image.new("RGBA", (tile * 4, tile), (0, 0, 0, 0))
    mark = source.resize((tile, tile), Image.Resampling.LANCZOS)
    image.alpha_composite(mark, (0, 0))

    horizontal = Image.new("RGBA", (tile, tile), (0, 0, 0, 0))
    mini = source.resize((72, 72), Image.Resampling.LANCZOS)
    horizontal.alpha_composite(mini, (0, 28))
    d = ImageDraw.Draw(horizontal)
    d.text((72, 52), "MiBoX", fill=INK, font=ImageFont.load_default(size=20))
    image.alpha_composite(horizontal, (tile, 0))

    alpha = source.getchannel("A")
    mono = Image.new("RGBA", source.size, (246, 238, 219, 0))
    mono.putalpha(alpha)
    mono = mono.resize((tile, tile), Image.Resampling.LANCZOS)
    image.alpha_composite(mono, (tile * 2, 0))
    reverse = Image.new("RGBA", source.size, (42, 47, 49, 0))
    reverse.putalpha(alpha)
    reverse = reverse.resize((tile, tile), Image.Resampling.LANCZOS)
    image.alpha_composite(reverse, (tile * 3, 0))
    return {"file": "brand_lockups.png", "columns": 4, "rows": 1,
            "tile_size": tile, "width": image.width, "height": image.height,
            "items": items, "tintable": False}, image


def draw_help_card(title: str, kind: str, width: int = 512, height: int = 288) -> Image.Image:
    scale = 2
    w, h = width * scale, height * scale
    image = Image.new("RGBA", (w, h), (25, 29, 31, 255))
    d = ImageDraw.Draw(image)
    s = scale
    d.rounded_rectangle((8*s, 8*s, (width-8)*s, (height-8)*s), radius=12*s,
                        fill=(30, 35, 37, 255), outline=(78, 87, 88, 255), width=2*s)
    d.text((24*s, 18*s), title.upper(), fill=INK, font=ImageFont.load_default(size=42))

    if kind == "signal":
        boxes = [(18, 112, 104, 68, "INPUT + EQ", TEAL),
                 (140, 112, 104, 68, "TAPE + MIX", ACCENT),
                 (262, 112, 104, 68, "PROTECTION", PINK),
                 (384, 112, 104, 68, "IR / ROOM", TEAL)]
        for x, y, bw, bh, label, color in boxes:
            d.rounded_rectangle((x*s,y*s,(x+bw)*s,(y+bh)*s),radius=6*s,
                                fill=(40,47,49,255),outline=color,width=2*s)
            d.text(((x+8)*s,(y+24)*s),label,fill=color,font=ImageFont.load_default(size=24))
        for x in (126, 248, 370):
            d.line((x*s,146*s,(x+18)*s,146*s),fill=INK,width=3*s)
            d.line(((x+12)*s,140*s,(x+18)*s,146*s,(x+12)*s,152*s),fill=INK,width=3*s)
        d.text((32*s,218*s),"INPUT → MACHINE → PROTECTION → IR / REVERB / VINYL",
               fill=MUTED,font=ImageFont.load_default(size=20))
    elif kind == "ab":
        for x, label, color in ((95,"A",ACCENT),(310,"B",TEAL)):
            d.rounded_rectangle((x*s,95*s,(x+105)*s,196*s),radius=8*s,
                                fill=(42,49,51,255),outline=color,width=3*s)
            d.text(((x+48)*s,126*s),label,fill=color,font=ImageFont.load_default(size=48))
        d.line((210*s,146*s,300*s,146*s),fill=INK,width=4*s)
        d.polygon([(300*s,146*s),(284*s,136*s),(284*s,156*s)],fill=INK)
        d.text((145*s,220*s),"MATCH LEVELS, SWITCH, LISTEN",fill=MUTED,font=ImageFont.load_default(size=22))
    elif kind == "preset":
        labels = [(35,"BROWSE",BLUE),(160,"LOAD",ACCENT),(285,"TWEAK",PINK),(410,"SAVE",TEAL)]
        for x, label, color in labels:
            d.rounded_rectangle((x*s,105*s,(x+78)*s,177*s),radius=7*s,
                                fill=(42,49,51,255),outline=color,width=2*s)
            d.text(((x+13)*s,135*s),label,fill=color,font=ImageFont.load_default(size=20))
        for x in (120,245,370):
            d.line((x*s,141*s,(x+28)*s,141*s),fill=INK,width=3*s)
            d.polygon([((x+28)*s,141*s),((x+20)*s,135*s),((x+20)*s,147*s)],fill=INK)
    else:
        d.rounded_rectangle((42*s,108*s,150*s,181*s),radius=6*s,
                            fill=(42,49,51,255),outline=BLUE,width=2*s)
        d.text((72*s,137*s),"FILE",fill=BLUE,font=ImageFont.load_default(size=24))
        d.line((166*s,145*s,246*s,145*s),fill=INK,width=3*s)
        d.polygon([(246*s,145*s),(235*s,138*s),(235*s,152*s)],fill=INK)
        d.rounded_rectangle((266*s,93*s,402*s,205*s),radius=8*s,
                            fill=(42,49,51,255),outline=ACCENT,width=2*s)
        for x in (288,320,352,384):
            d.line((x*s,109*s,x*s,124*s),fill=ACCENT,width=2*s)
        d.line((284*s,161*s,302*s,161*s,312*s,140*s,326*s,178*s,
                341*s,149*s,356*s,163*s,383*s,163*s),fill=TEAL,width=3*s)
    return image.resize((width, height), Image.Resampling.LANCZOS)


def help_atlas() -> tuple[dict, Image.Image]:
    cards = [
        ("help.signal_path", "Signal path", "signal"),
        ("help.ab_compare", "A/B comparison", "ab"),
        ("help.preset_workflow", "Preset workflow", "preset"),
        ("help.load_model", "Load a model or impulse response", "model"),
    ]
    card_w, card_h = 512, 288
    image = Image.new("RGBA", (card_w*2, card_h*2), (0,0,0,0))
    items=[]
    for index,(key,label,kind) in enumerate(cards):
        image.alpha_composite(draw_help_card(label,kind),(index%2*card_w,index//2*card_h))
        items.append({"key":key,"label":label,"tile":index})
    return {"file":"help_illustrations.png","columns":2,"rows":2,
            "tile_size":[card_w,card_h],"width":image.width,"height":image.height,
            "items":items,"tintable":False}, image


def signal_path_image() -> Image.Image:
    width, height = 1600, 500
    scale = 2
    image = Image.new("RGBA", (width*scale,height*scale), (23,27,29,255))
    d=ImageDraw.Draw(image)
    d.text((44*scale,24*scale),"MIBOX · SIGNAL PATH",fill=INK,font=ImageFont.load_default(size=40))
    stages = [
        (40,("INPUT +", "FRONT END"),TEAL),
        (230,("INPUT EQ", "TAPE HEAD"),BLUE),
        (420,("TRANSPORT", "SATURATION"),ACCENT),
        (610,("OUT EQ +", "NEURAL + DYN"),PINK),
        (800,("AUTOTUNE +", "COMPRESSOR"),PURPLE),
        (990,("WIDTH +", "PROTECTION"),BLUE),
        (1180,("PHASE / LO-FI", "BYPASS + DELTA"),TEAL),
        (1370,("IR / REVERB /", "VINYL"),ACCENT),
    ]
    for x,labels,color in stages:
        d.rounded_rectangle((x*scale,185*scale,(x+150)*scale,275*scale),radius=10*scale,
                            fill=(36,43,45,255),outline=color,width=3*scale)
        d.text(((x+12)*scale,211*scale),labels[0],fill=color,font=ImageFont.load_default(size=22))
        d.text(((x+12)*scale,237*scale),labels[1],fill=color,font=ImageFont.load_default(size=22))
        d.line(((x+12)*scale,299*scale,(x+137)*scale,299*scale),fill=(97,109,110,255),width=2*scale)
        d.line(((x+12)*scale,306*scale,(x+137)*scale,306*scale),fill=(60,70,72,255),width=2*scale)
    for x in (195,385,575,765,955,1145,1335):
        d.line((x*scale,230*scale,(x+28)*scale,230*scale),fill=INK,width=4*scale)
        d.polygon([((x+28)*scale,230*scale),((x+16)*scale,222*scale),((x+16)*scale,238*scale)],fill=INK)
    d.text((48*scale,385*scale),"ENGINE-RATE PROCESSING",fill=TEAL,font=ImageFont.load_default(size=24))
    d.text((650*scale,385*scale),"SAMPLE + FRAME STAGES",fill=ACCENT,font=ImageFont.load_default(size=24))
    d.text((1215*scale,385*scale),"BLOCK EFFECTS",fill=PURPLE,font=ImageFont.load_default(size=24))
    return image.resize((width,height),Image.Resampling.LANCZOS)


def platform_asset_inventory() -> list[dict]:
    res = ROOT / "android/app/src/main/res"
    density_order = {"mdpi": 0, "hdpi": 1, "xhdpi": 2, "xxhdpi": 3, "xxxhdpi": 4}
    density_images=[]
    for path in res.glob("mipmap-*/ic_launcher.png"):
        density = path.parent.name.removeprefix("mipmap-")
        if density in density_order:
            with Image.open(path) as image:
                width, height = image.size
            density_images.append({"density":density,"file":path.relative_to(ROOT).as_posix(),
                                   "width":width,"height":height})
    density_images.sort(key=lambda entry:density_order[entry["density"]])
    return [{
        "platform":"Android",
        "package":"com.mcsmes.mibox",
        "manifest":"android/app/src/main/AndroidManifest.xml",
        "manifest_icon":"@mipmap/ic_launcher",
        "density_icons":density_images,
        "adaptive_icons":[
            "android/app/src/main/res/mipmap-anydpi-v26/ic_launcher.xml",
            "android/app/src/main/res/mipmap-anydpi-v33/ic_launcher.xml",
        ],
        "foreground_vector":"android/app/src/main/res/drawable/ic_launcher_foreground.xml",
        "monochrome_vector":"android/app/src/main/res/drawable/ic_launcher_monochrome.xml",
    }]


def main() -> int:
    output_images: dict[str, Image.Image] = {}
    atlases=[]

    specs = [
        ("ui_icons.png", ui_definitions(), 8),
        ("parameter_glyphs.png", parameter_definitions(), 8),
        ("machine_models.png", model_definitions(), 8),
        ("meter_status.png", meter_definitions(), 8),
        ("preset_categories.png", category_definitions(), 8),
        ("hardware_details.png", hardware_definitions(), 4),
    ]
    for filename, items, columns in specs:
        atlas, image = atlas_image(items, filename, columns)
        atlases.append(atlas)
        output_images[filename] = image

    preset_entry, preset_image = preset_art_atlas()
    atlases.append(preset_entry)
    output_images[preset_entry["file"]] = preset_image

    brand, brand_image = brand_atlas()
    atlases.append(brand)
    output_images[brand["file"]] = brand_image
    help_entry, help_image = help_atlas()
    atlases.append(help_entry)
    output_images[help_entry["file"]] = help_image
    output_images["signal_path.png"] = signal_path_image()

    for filename, image in output_images.items():
        image.save(ASSETS / filename, optimize=True)

    catalog = {
        "about": "Generated MiBoX visual library. Sprite keys and indices are stable within each atlas; white glyph atlases are tintable for normal, active, disabled and high-contrast states.",
        "generator": "Assets/generate_asset_pack.py (Pillow required)",
        "atlases": atlases,
        "platform_assets": platform_asset_inventory(),
        "standalone": [{"key":"help.signal_path_full","file":"signal_path.png",
                        "width":1600,"height":500,"description":"Full signal path overview for the manual."}],
    }
    (ASSETS / "asset_catalog.json").write_text(
        json.dumps(catalog, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    print(f"generated {len(output_images)} PNG assets and {sum(len(a['items']) for a in atlases)} named atlas cells")
    for atlas in atlases:
        print(f"  {atlas['file']}: {len(atlas['items'])} cells, {atlas['width']}x{atlas['height']}")
    print("  signal_path.png: 1600x500")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
