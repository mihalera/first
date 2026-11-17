#!/usr/bin/env python3
"""
Checks the MIX and MACHINE pages against the chain they are supposed to drive.

    python3 tests/parameters/check_mix_wiring.py

Nine SOURCE knobs and one strip are the whole of the saturation UI, and every
piece of that has been moved, renamed or re-counted at least once: seven knobs
became nine, the principle list grew two entries in the middle rather than at the
end, and the MACHINE page took the type selectors off the deck. A mistake in any
of those is SILENT in the worst way this plugin has - the build is clean, the
panel looks right, and a knob either moves a parameter nobody reads, or the strip
paints a machine the core is not running, or the editor builds against a table
the audio thread does not share. None of it shows in a compile, a harness that
does not include the editor, or a screenshot.

So this reads the sources and asks one question per link in the chain, in the
order the signal travels:

  * PanelPaint's `principleParameterIds` - the ORDER. Everything else is
    checked against this list rather than against its own idea of the names.
  * PanelText's controlSpecs: the nine ids exist, sit together, and state the
    same defaults the parameter table does (a knob that double-clicks to a
    default the host disagrees with is a knob that moves on its own).
  * ParameterTable: each id is a 0..1 percentage parameter - the shares are
    normalised, so a 0..100 registration would silently read 50x too loud.
  * PluginProcessor: the audio thread fetches a raw pointer for every one of
    the nine, because the pointer IS the only route from the parameter to the
    smoother.
  * ProcessorSignalPath: `setWeights` is called with those nine smoothers in
    the same order the ids are declared in.
  * SaturationCore: the arguments are named in that order and each one is
    stored into the weight of its own name - a swapped pair here would still
    compile and would colour every machine with its neighbour's curve.
  * PanelTabs: the MIX page lists exactly those nine control indices, in that
    order; its subtitle and the strip's caption still SAY nine.
  * EditorTimer / EditorMeters: the strip reads the same ids in the same order
    and prints the same nine names.
  * EditorSetup / PluginEditor: the MACHINE page's seven selectors are all
    attached to parameters that exist and all placed under `machineTab`, so the
    page is a real page rather than seven widgets that stayed on the deck.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

PAINT = os.path.join(ROOT, "Source", "GUI", "PanelPaint.h")
TEXT = os.path.join(ROOT, "Source", "GUI", "PanelText.h")
TABS = os.path.join(ROOT, "Source", "GUI", "PanelTabs.h")
TABLE = os.path.join(ROOT, "Source", "ParameterTable.cpp")
PROCESSOR = os.path.join(ROOT, "Source", "PluginProcessor.cpp")
PATH = os.path.join(ROOT, "Source", "ProcessorSignalPath.cpp")
CORE = os.path.join(ROOT, "Source", "DSP", "SaturationCore.h")
METERS = os.path.join(ROOT, "Source", "GUI", "EditorMeters.cpp")
TIMER = os.path.join(ROOT, "Source", "GUI", "EditorTimer.cpp")
SETUP = os.path.join(ROOT, "Source", "GUI", "EditorSetup.cpp")
EDITOR = os.path.join(ROOT, "Source", "PluginEditor.cpp")

problems = []


def read(path):
    with open(path, encoding="utf-8") as handle:
        return handle.read()


def fail(what):
    problems.append(what)


paint = read(PAINT)
text = read(TEXT)
tabs = read(TABS)
table = read(TABLE)
processor = read(PROCESSOR)
path = read(PATH)
core = read(CORE)
meters = read(METERS)
timer = read(TIMER)
setup = read(SETUP)
editor = read(EDITOR)

# ---------------------------------------------------------------------------
# 1. The order, stated once.
# ---------------------------------------------------------------------------
count_match = re.search(r"principleCount\s*=\s*(\d+)", paint)
if not count_match:
    sys.exit("check_mix_wiring: principleCount not found in PanelPaint.h")

principle_count = int(count_match.group(1))

ids_match = re.search(r"principleParameterIds\s*\{(.*?)\}", paint, re.S)
if not ids_match:
    sys.exit("check_mix_wiring: principleParameterIds not found in PanelPaint.h")

principle_ids = re.findall(r'"([a-z_]+)"', ids_match.group(1))

if len(principle_ids) != principle_count:
    fail("principleParameterIds lists %d ids, principleCount says %d"
         % (len(principle_ids), principle_count))

if len(set(principle_ids)) != len(principle_ids):
    fail("principleParameterIds repeats an id")

machines = [name[: -len("_source")] if name.endswith("_source") else name
            for name in principle_ids]

if any(not name.endswith("_source") for name in principle_ids):
    fail("every principle id must be a *_source parameter")

# ---------------------------------------------------------------------------
# 2. The core takes them in that order, and stores each one where it says.
# ---------------------------------------------------------------------------
weights_match = re.search(r"void setWeights\s*\((.*?)\)\s*noexcept", core, re.S)
if not weights_match:
    sys.exit("check_mix_wiring: SaturationCore::setWeights not found")

weights_args = re.findall(r"float\s+([a-zA-Z]+)\s*Amount", weights_match.group(1))

if weights_args != machines:
    fail("setWeights takes (%s) but the principle order is (%s)"
         % (", ".join(weights_args), ", ".join(machines)))

for machine in machines:
    stored = re.search(r"%sWeight\s*=\s*juce::jlimit\s*\([^;]*?%sAmount" % (machine, machine),
                       core, re.S)
    if stored is None:
        fail("setWeights stores %sWeight from the wrong argument (or not at all)" % machine)

# ---------------------------------------------------------------------------
# 3. The audio thread fetches one raw pointer per id, and the signal path hands
#    the smoothers over in that same order.
# ---------------------------------------------------------------------------
for machine, parameter in zip(machines, principle_ids):
    if not re.search(r"%sSourceParam\s*=\s*parameters\.getRawParameterValue\s*\(\s*\"%s\"\s*\)"
                     % (machine, parameter), processor):
        fail("%sSourceParam is never fetched from \"%s\" - the knob would move nothing"
             % (machine, parameter))

call_match = re.search(r"saturation\.setWeights\s*\((.*?)\);", path, re.S)
if not call_match:
    sys.exit("check_mix_wiring: the signal path's setWeights call not found")

call_args = re.findall(r"([a-zA-Z]+)SourceSmoothed\.getCurrentValue\s*\(\s*\)",
                       call_match.group(1))

if call_args != machines:
    fail("ProcessorSignalPath passes (%s) to setWeights, the principle order is (%s)"
         % (", ".join(call_args), ", ".join(machines)))

# ---------------------------------------------------------------------------
# 4. The panel states the same defaults the parameter table does, in 0..1.
# ---------------------------------------------------------------------------
specs_block = re.search(r"controlSpecs\s*\{(.*?)\n    \} \};", text, re.S)
if not specs_block:
    sys.exit("check_mix_wiring: controlSpecs not found in PanelText.h")

specs = re.findall(r'\{\s*"([a-z_0-9]+)",\s*"([^"]*)",\s*(-?[0-9.]+)', specs_block.group(1))
spec_index = {spec[0]: index for index, spec in enumerate(specs)}

if len(spec_index) != len(specs):
    fail("controlSpecs repeats an id")

control_count_match = re.search(r"controlCount\s*=\s*(\d+)", text)
if control_count_match and int(control_count_match.group(1)) != len(specs):
    fail("controlSpecs holds %d rows, controlCount says %s"
         % (len(specs), control_count_match.group(1)))

defaults = {}

for parameter in principle_ids:
    if parameter not in spec_index:
        fail("PanelText has no control for %s" % parameter)
        continue

    defaults[parameter] = float(specs[spec_index[parameter]][2])

    registered = re.search(
        r'AudioParameterFloat>\s*\(\s*juce::ParameterID\s*\{\s*"%s"\s*,\s*\d+\s*\}'
        r'.*?percentageRange\s*\(\s*[0-9.]+f\s*\)\s*,\s*(-?[0-9.]+)f' % parameter,
        table, re.S)

    if registered is None:
        fail("\"%s\" is not registered as a percentageRange AudioParameterFloat" % parameter)
        continue

    if abs(float(registered.group(1)) - defaults[parameter]) > 1.0e-6:
        fail("\"%s\" defaults to %s on the panel and %s in the table"
             % (parameter, defaults[parameter], registered.group(1)))

# ---------------------------------------------------------------------------
# 5. The MIX page lists those nine control indices, in that order.
# ---------------------------------------------------------------------------
mix_entry = re.search(r'\{\s*"MIX"\s*,\s*"(.*?)"\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*\{([^}]*)\}',
                      tabs, re.S)
if not mix_entry:
    sys.exit("check_mix_wiring: the MIX page is not in PanelTabs.h")

mix_indices = [int(value) for value in re.findall(r"\d+", mix_entry.group(4))]
mix_expected = [spec_index[parameter] for parameter in principle_ids if parameter in spec_index]

if mix_indices != mix_expected:
    fail("the MIX page shows controls %s, the nine SOURCE knobs are %s"
         % (mix_indices, mix_expected))

if int(mix_entry.group(2)) != len(mix_expected):
    fail("the MIX page declares %s knobs but lists %d" % (mix_entry.group(2), len(mix_indices)))

mix_hint = mix_entry.group(1)

for retired in ("CASSETTE", "VINYL", "AMP"):
    if re.search(r"\b%s\b" % retired, mix_hint):
        fail("the MIX page's description still names the retired %s model" % retired)

if "seven" in mix_hint.lower():
    fail("the MIX page's description still says seven")

if "nine" not in mix_hint.lower():
    fail("the MIX page's description does not say nine")

# ---------------------------------------------------------------------------
# 6. The knobs on the page are the table's knobs: the editor builds its control
#    list from controlSpecs, attaches each slider to that id, and double-clicks
#    back to the spec's default.
# ---------------------------------------------------------------------------
if "controlIds.add(controlSpecs[i].id)" not in setup:
    fail("the editor's control ids are not built from controlSpecs")

if not re.search(r"SliderAttachment>\(\s*audioProcessor\.parameters,\s*controlIds", setup):
    fail("the editor's sliders are not attached to their controlSpecs ids")

if "setDoubleClickReturnValue(true, defaultValues[i])" not in setup:
    fail("double-click does not return a knob to the default the spec states")

# ---------------------------------------------------------------------------
# 7. The strip reads those ids and prints those names.
# ---------------------------------------------------------------------------
if "principleParameterIds" not in timer:
    fail("EditorTimer does not read the principle ids - the strip would guess")

names_match = re.search(r"principleNames\[principleCount\]\s*\{(.*?)\}", meters, re.S)
if not names_match:
    sys.exit("check_mix_wiring: the strip's principleNames not found in EditorMeters.cpp")

strip_names = [name.upper() for name in re.findall(r'"([^"]+)"', names_match.group(1))]
expected_names = [name.upper() for name in machines]

if strip_names != expected_names:
    fail("the strip paints (%s), the principle order is (%s)"
         % (", ".join(strip_names), ", ".join(expected_names)))

if "shares[static_cast<std::size_t> (p)]" not in meters:
    fail("the strip does not index its shares by principle")

if "nine machines' shares" not in meters:
    fail("the strip's note still counts the machines wrong")

# The whole strip is the same nine cells: one per principle, no fixed seven.
if "principleCount - 1" not in meters:
    fail("the strip's cell arithmetic does not span every principle")

# ---------------------------------------------------------------------------
# 8. The MACHINE page's seven selectors are attached, and they live there.
# ---------------------------------------------------------------------------
machine_page = re.search(r"\{\s*\"MACHINE\"\s*,\s*\"(.*?)\"\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*\{([^}]*)\}",
                         tabs, re.S)
if not machine_page:
    sys.exit("check_mix_wiring: the MACHINE page is not in PanelTabs.h")

selectors = {
    "tapeTypeBox": "tape_type",
    "valveTypeBox": "valve_type",
    "transformerTypeBox": "transformer_type",
    "digitalTypeBox": "digital_type",
    "vinylTypeBox": "vinyl_type",
    "speedBox": "speed",
    "vinylSpeedBox": "vinyl_speed",
}

for widget, parameter in selectors.items():
    if not re.search(r'audioProcessor\.parameters\s*,\s*"%s"\s*,\s*%s\s*\)' % (parameter, widget),
                     setup):
        fail("%s is not attached to \"%s\" - the MACHINE page's selector would do nothing"
             % (widget, parameter))

    if not re.search(r'juce::ParameterID\s*\{\s*"%s"\s*,\s*\d+\s*\}' % parameter, table):
        fail("\"%s\" is attached in the editor but never registered" % parameter)

    label = widget[: -len("Box")] + "Label"

    if not re.search(r"place\s*\(\s*%s\s*,\s*%s\s*," % (label, widget), editor):
        fail("%s is attached but never placed on the MACHINE page" % widget)

if len(re.findall(r"place\s*\(\s*\w+Label\s*,\s*\w+Box\s*,", editor)) < len(selectors):
    fail("the MACHINE page places fewer selectors than it has")

for widget in selectors:
    if not re.search(r"&%s\b" % widget, editor.split("geometry.machineTab")[1]):
        fail("%s is placed but not toggled with the MACHINE page" % widget)

if "--" in machine_page.group(4) or machine_page.group(4).strip():
    fail("the MACHINE page holds no knobs - its members are the seven selectors")

print("MIX page and MACHINE page wiring: %d principles, %d MIX knobs, %d MACHINE selectors"
      % (principle_count, len(mix_indices), len(selectors)))

if problems:
    print("")
    for problem in problems:
        print("  - " + problem)
    print("\nCHECK MIX WIRING FAILED (%d problem(s))" % len(problems))
    sys.exit(1)

print("CHECK MIX WIRING PASSED - the nine SOURCE knobs, the parameter table, the "
      "smoothers, the core's weights and the strip all name the same machines in "
      "the same order, and every MACHINE-page selector is attached and placed")
