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

    //==========================================================================
    /** What the audio thread saw, counted rather than traced.

        The two reports this answers are "the sound is wrong" and "the machine
        stalled": a plugin that overruns its own blocks, or one that puts a
        non-finite sample into the host's buffer, sounds broken in ways no single
        log line describes, and neither is visible from the message thread. These
        are counters, not lines - a line per block would be a file per second and
        a lock on the audio thread - so the numbers are read once, when the
        processor is destroyed, and written to the trace as one sentence.

        Every call here is a few relaxed atomic adds: no allocation, no lock, no
        file, nothing that can block. Called from the callback on purpose. */
    struct AudioReport
    {
        unsigned long long blocks = 0;
        unsigned long long overBudget = 0;
        unsigned long long nonFinitePeaks = 0;
        float loudestPeak = 0.0f;

        // The stereo half. "The sound is strange" and "it is only on one side"
        // are different reports, and one peak cannot tell them apart: a plugin
        // that writes one channel, or writes the same channel twice, has a
        // perfectly healthy peak.
        unsigned long long stereoBlocks = 0;
        unsigned long long nonFiniteSides = 0;   // exactly one side broke
        float leftPeak = 0.0f;
        float rightPeak = 0.0f;
        float widestImbalance = 0.0f;            // |L - R| / max(L, R), 0 if a side is silent
    };

    /** One finished block: how much of the block's own duration it used (above
        1.0 is an overrun) and the peak of its output. A NaN or an infinity in the
        peak is counted and kept out of the loudest-peak figure, which is what
        makes "it clicked, then went silent" show up as one of the two. */
    void noteAudioBlock (double loadFraction, float peak) noexcept;

    /** One finished STEREO block: the same report as above, plus each side's own
        peak. This is what separates "the output is broken" from "the LEFT
        output is broken": a stage that stops writing one channel, writes both
        from one, or collapses the pair into mono leaves a single peak looking
        healthy, and the per-side numbers show which of the three it is. */
    void noteStereoBlock (double loadFraction, float peakLeft, float peakRight) noexcept;

    AudioReport audioReport() noexcept;

    /** Message-thread reporting: one line describing the audio report, or an empty
        string when the callback never ran in this process. */
    std::string audioReportText() noexcept;
} // namespace j37diag
