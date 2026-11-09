/*
  MiBoX diagnostics: the switch, the sink, and the thread table.

  The two reports this exists for - "the app never opens on Android" and "the DAW
  hangs when the project closes" - both need evidence from a machine we cannot
  reach. So the plugin can be told, without being rebuilt, to write down what it
  did: a startup trace on Android and a shutdown phase trace plus thread stacks on
  the desktop. Everything here is inert unless that switch is on.

  Deliberately JUCE-free, so the harness under tests/diagnostics can compile and
  run it with any C++17 compiler instead of only inside the plugin build.
*/
#pragma once

#include <string>

namespace j37diag
{
    /** The switch. Read once: MIBOX_DIAG=1 (or y/t) in the environment turns the
        desktop trace on; the Android launcher sets it explicitly from the intent
        before anything else runs. */
    bool enabled() noexcept;

    /** For the Android bootstrap and for the harness. Never called by shipping
        code paths. */
    void setEnabled (bool shouldBeEnabled) noexcept;

    /** Where the lines go. The plugin passes a path resolved by JUCE (see
        diagnosticsLogFile()), the harness passes its own temporary one. */
    void setLogPath (const std::string& path) noexcept;
    std::string logPath();

    /** Send lines somewhere other than the file - the Android side routes them
        through Diag.java, which owns the file there. The context is passed
        through untouched. */
    using Sink = void (*) (const char* line, void* context);
    void setSink (Sink sink, void* context) noexcept;

    /** One timestamped line. No-op when the switch is off, so an off build pays
        one relaxed load and nothing else. */
    void note (const char* text) noexcept;
    inline void note (const std::string& text) noexcept { note (text.c_str()); }

    /** Separator, header and the previous run's separator. Called once, when the
        process is far enough along to know what it is. */
    void beginRun (const char* where) noexcept;

    /** Noted once per process, however often it is called: the first painted
        frame. "Nothing after the permission dialog" and "nothing after the
        first paint" are different bugs, and this is the line that separates
        them. */
    void noteFirstFrame (const char* what) noexcept;

    /** Thread identities for the shutdown watchdog. Recording is a single relaxed
        store, which is what makes it usable from the audio and GL threads; the
        watchdog reads them from its own thread after a stall. */
    enum class ThreadSlot { message = 0, gl = 1, audio = 2, count = 3 };

    void recordThread (ThreadSlot slot) noexcept;
    unsigned long long recordedThreadId (ThreadSlot slot) noexcept;
    const char* threadSlotName (ThreadSlot slot) noexcept;

    /** Wall-clock stamp with milliseconds, for lines nobody is watching live. */
    std::string timestamp();
} // namespace j37diag
