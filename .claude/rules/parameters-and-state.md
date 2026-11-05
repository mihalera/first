---
paths:
  - "Source/ParameterTable.cpp"
  - "Source/ProcessorPresets.*"
  - "Source/ProcessorStateFormat.h"
  - "Source/PluginProcessor.*"
  - "tests/parameters/**"
  - "tests/presets/**"
  - "tests/state/**"
---

# Host parameter and session-state rules

- Parameter IDs, names, ranges, defaults, units, and conversions are part of the plugin's host-facing contract. Preserve them unless the request explicitly changes the control surface.
- Trace a parameter through the table, reads in the processor/signal path, editor attachment, factory presets, and the relevant structural check.
- Keep old serialized sessions loadable. Update the state version/migration and `tests/state/` when the serialized shape changes.
- Factory preset JSON and the embedded resource path must agree; run the focused preset check after changing either.

