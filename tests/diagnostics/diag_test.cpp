/*
  Diagnostic harness.

  The shutdown watchdog and the trace sink are the only code in the plugin that
  exists purely to produce evidence from a machine we cannot reach, and a report
  that silently writes nothing is worse than no report at all. So this harness
  runs the three states the plugin can be in - switch off, switch on with a
  teardown that finishes, switch on with a teardown that stops - and checks the
  files that the plugin promises: none, a phase trace, and a phase trace plus the
  thread stacks.

  It links the two files under Source/Diagnostics as they ship, and mimics
  nothing.
*/
#include "Diagnostics/DiagCore.h"
#include "Diagnostics/ShutdownWatchdog.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
    int failures = 0;

    void check (bool condition, const std::string& what)
    {
        std::printf ("%s  %s\n", condition ? "ok  " : "FAIL", what.c_str());

        if (! condition)
            ++failures;
    }

    bool exists (const std::string& path)
    {
        std::ifstream in (path);
        return in.good();
    }

    std::string read (const std::string& path)
    {
        std::ifstream in (path);
        return std::string ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    }

    bool contains (const std::string& haystack, const std::string& needle)
    {
        return haystack.find (needle) != std::string::npos;
    }

    /** Each scenario starts from nothing, so a second run of the harness checks
        the run it just did rather than the one before it. */
    void removeFile (const std::string& path)
    {
        std::remove (path.c_str());
    }

    /** A thread that is still inside its work when the watchdog looks - the
        shape of a GL thread waiting for a swap or an audio thread waiting for a
        lock, which is what the teardown actually stalls on. */
    struct BlockedThread
    {
        BlockedThread (j37diag::ThreadSlot slot, const std::string& labelToUse) : label (labelToUse)
        {
            thread = std::thread ([this, slot]
            {
                j37diag::recordThread (slot);

                std::unique_lock<std::mutex> lock (mutex);
                released.wait (lock, [this] { return letGo; });
            });
        }

        ~BlockedThread()
        {
            {
                std::lock_guard<std::mutex> lock (mutex);
                letGo = true;
            }

            released.notify_all();

            if (thread.joinable())
                thread.join();
        }

        std::string label;
        std::mutex mutex;
        std::condition_variable released;
        bool letGo = false;
        std::thread thread;
    };
} // namespace

int main (int argc, char** argv)
{
    const std::string directory = argc > 1 ? argv[1] : ".";
    std::printf ("MiBoX diagnostics harness, files in %s\n\n", directory.c_str());

    // ---------------------------------------------------------------------
    // 1. Switch off: nothing at all is written, and the watchdog does not fire.
    // ---------------------------------------------------------------------
    {
        const auto path = directory + "/off.log";

        removeFile (path);
        j37diag::setEnabled (false);
        j37diag::setLogPath (path);
        j37diag::beginRun ("harness, switch off");
        j37diag::note ("this line must not reach a file");

        j37diag::ShutdownWatchdog guard (100);
        guard.arm();
        guard.phase ("teardown step");
        guard.disarm();

        check (! exists (path), "switch off: no log file");
        check (! exists (guard.stacksPath()), "switch off: no watchdog report");
        check (! guard.hasFired(), "switch off: watchdog never fires");
    }

    // ---------------------------------------------------------------------
    // 2. Switch on, teardown completes: a phase trace, and no stacks.
    // ---------------------------------------------------------------------
    {
        const auto path = directory + "/quick.log";

        removeFile (path);
        removeFile (path + ".stacks.txt");
        j37diag::setEnabled (true);
        j37diag::setLogPath (path);
        j37diag::beginRun ("harness, teardown that finishes");
        j37diag::note ("startup: harness began");

        j37diag::ShutdownWatchdog guard (2000);
        guard.arm();
        guard.phase ("teardown: stop the clock");
        guard.phase ("teardown: release the scene");
        guard.disarm();

        const auto log = read (path);

        check (exists (path), "switch on: the log file appears");
        check (contains (log, "startup: harness began"), "switch on: notes are recorded");
        check (contains (log, "phase: teardown: stop the clock"),
               "switch on: each phase is recorded");
        check (contains (log, "shutdown: complete"), "switch on: the end of the teardown is recorded");
        check (! guard.hasFired(), "switch on: a teardown that finishes does not fire the watchdog");
        check (! exists (guard.stacksPath()), "switch on: no stacks report for a clean teardown");
    }

    // ---------------------------------------------------------------------
    // 3. Switch on, teardown stops: the phase trace says how far it got, and the
    //    stacks say what the threads were doing when it stopped.
    // ---------------------------------------------------------------------
    {
        const auto path = directory + "/hang.log";

        removeFile (path);
        removeFile (path + ".stacks.txt");
        j37diag::setEnabled (true);
        j37diag::setLogPath (path);
        j37diag::beginRun ("harness, teardown that stops");

        j37diag::recordThread (j37diag::ThreadSlot::message);

        {
            BlockedThread glThread (j37diag::ThreadSlot::gl, "gl");
            BlockedThread audioThread (j37diag::ThreadSlot::audio, "audio");

            // Give the two threads a moment to register before the guard starts.
            std::this_thread::sleep_for (std::chrono::milliseconds (50));

            j37diag::ShutdownWatchdog guard (300);
            guard.arm();
            guard.phase ("teardown: stop the clock");
            guard.phase ("teardown: release the scene");

            // Never disarmed: this is the step that never returns.
            std::this_thread::sleep_for (std::chrono::milliseconds (900));

            check (guard.hasFired(), "stalled teardown: the watchdog fires");
            check (exists (guard.stacksPath()), "stalled teardown: the report appears");

            const auto report = read (guard.stacksPath());
            const auto log = read (path);

            check (contains (log, "phase: teardown: release the scene"),
                   "stalled teardown: the log holds the last phase reached");
            check (! contains (log, "shutdown: complete"),
                   "stalled teardown: the log does not claim the teardown finished");
            check (contains (report, "the teardown had not finished"),
                   "stalled teardown: the report says why it exists");
            check (contains (report, "phase trace"), "stalled teardown: the report carries the trace");
            check (contains (report, "phase: teardown: release the scene"),
                   "stalled teardown: the trace in the report names the step it stopped at");
            check (contains (report, "gl: tid"), "stalled teardown: the gl thread is listed");
            check (contains (report, "audio: tid"), "stalled teardown: the audio thread is listed");

           #if defined(_WIN32)
            check (contains (report, "---- message thread ----"), "stalled teardown: message stack asked for");
            check (contains (report, "---- gl thread ----"), "stalled teardown: gl stack asked for");
            check (contains (report, "---- audio thread ----"), "stalled teardown: audio stack asked for");
            check (contains (report, "!") || contains (report, "+0x"),
                   "stalled teardown: frames carry a symbol or an offset");
           #endif

            std::printf ("\n--- %s ---\n%s\n", guard.stacksPath().c_str(), report.c_str());

            guard.disarm();
        }
    }

    std::printf ("\n%s\n", failures == 0 ? "diagnostics harness passed"
                                         : "diagnostics harness FAILED");
    return failures == 0 ? 0 : 1;
}
