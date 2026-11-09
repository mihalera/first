/*
  The shutdown watchdog.

  A host closing a project destroys the editor and then the processor. Somewhere
  in that teardown this build sometimes stops, and the only way to find out where
  without a debugger attached is to have been writing down each step and, when a
  step never returns, to ask the threads what they were doing.

  So: the teardown we own is bracketed with phase lines, a watchdog thread is
  started at the first one, and if the last line has not arrived within a couple
  of seconds it writes the phase trace and the stacks of the message, GL and
  audio threads beside the log. It does not abort, sleep less, or force anything:
  the process is left exactly as stuck as it was, so the state that is reported
  is the state that is real.
*/
#pragma once

#include <atomic>
#include <chrono>
#include <string>

namespace j37diag
{
    class ShutdownWatchdog
    {
    public:
        explicit ShutdownWatchdog (int limitMilliseconds = 2000) noexcept;

        /** Start watching. Idempotent: a second call inside the same teardown -
            the processor destructing after the editor - does not restart it. */
        void arm() noexcept;

        /** One step of the teardown, as it is reached. */
        void phase (const char* what) noexcept;

        /** The teardown finished. Stops the watchdog without writing anything. */
        void disarm() noexcept;

        int limitMilliseconds() const noexcept { return limit; }
        bool hasFired() const noexcept       { return fired.load (std::memory_order_relaxed); }
        std::string stacksPath() const;

        /** `phase()` on construction, `done` on destruction - so a step that
            returns is told apart from one that never does. */
        struct ScopedPhase
        {
            ScopedPhase (ShutdownWatchdog& ownerToUse, const char* what) : owner (ownerToUse)
            {
                owner.phase (what);
            }

            ~ScopedPhase() { owner.phase ("done"); }

            ShutdownWatchdog& owner;
        };

    private:
        void watch();
        void writeStacks();
        void appendPhaseTrace (std::string& destination);

        int limit;
        std::atomic<bool> armed { false };
        std::atomic<bool> finished { false };
        std::atomic<bool> fired { false };
        std::chrono::steady_clock::time_point began;
    };

    /** One watchdog per process, so the editor and the processor destructors
        agree about whether a teardown is already being watched. */
    ShutdownWatchdog& shutdownWatchdog();

    /** Covers a whole destruction: `begin()` starts the watch, the destructor
        ends it. Declare one as the FIRST member of a class so it is destroyed
        after every other member - that is what makes the trace cover the member
        teardown as well as the destructor body, which is where a window's
        children, look-and-feels and contexts go. */
    class TeardownScope
    {
    public:
        void begin() noexcept { shutdownWatchdog().arm(); }

        ~TeardownScope() { shutdownWatchdog().disarm(); }
    };
} // namespace j37diag
