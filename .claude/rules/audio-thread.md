---
paths:
  - "Source/PluginProcessor.*"
  - "Source/ProcessorSignalPath.cpp"
  - "Source/DSP/**/*.h"
  - "Source/DSP/**/*.cpp"
---

# Audio-thread rules

- Treat `processBlock` and every function it calls as real-time audio-thread code unless the call site proves otherwise.
- Do not allocate, lock, wait, perform I/O, log, or call editor/UI code from the audio callback.
- Preserve state ownership and coefficient update timing. Prefer setup in `prepareToPlay` and the existing block-rate/sample-rate split.
- Use the existing lock-free parameter, atomic, or queue pathways for cross-thread values; do not add a new shared mutable object without tracing both sides.
- When moving a stage, keep the shipping implementation self-contained and update a harness that compiles the real header where possible.
- Read the relevant section of `docs/signal-path.md` before changing stage order, oversampling, latency, or rate-domain behavior.

