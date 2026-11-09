#include "ShutdownWatchdog.h"

#include "DiagCore.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <thread>

#if defined(_WIN32)
 #include <windows.h>

 #include <dbghelp.h>

 #if defined(__MINGW32__)
  // MinGW has no #pragma comment support; CMake and the harness link dbghelp
  // explicitly. Nothing to do here, but the branch documents why.
 #else
  #pragma comment(lib, "dbghelp.lib")
 #endif
#endif

namespace j37diag
{
namespace
{
   #if defined(_WIN32)
    /** Symbol handler for the process, alive only while a dump is being written.
        Deferred loads are on, so a dump does not pay for the modules it never
        looks at. Without PDBs beside the plugin the names fall back to
        module+offset, which still identifies the frame. */
    struct SymbolSession
    {
        SymbolSession()
        {
            SymSetOptions (SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
            ready = SymInitialize (GetCurrentProcess(), nullptr, FALSE) != FALSE;

            if (ready)
                SymRefreshModuleList (GetCurrentProcess());
        }

        ~SymbolSession()
        {
            if (ready)
                SymCleanup (GetCurrentProcess());
        }

        bool ready = false;
    };

    std::string describeAddress (DWORD64 address) noexcept
    {
        std::string text;

        IMAGEHLP_MODULE64 module {};
        module.SizeOfStruct = sizeof (module);
        const auto namedModule = SymGetModuleInfo64 (GetCurrentProcess(), address, &module) != FALSE;
        const auto* moduleName = namedModule ? module.ModuleName : "?";

        char stackBuffer[sizeof (SYMBOL_INFO) + 512] {};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*> (stackBuffer);
        symbol->SizeOfStruct = sizeof (SYMBOL_INFO);
        symbol->MaxNameLen = 500;

        DWORD64 displacement = 0;
        char line[768] {};

        if (SymFromAddr (GetCurrentProcess(), address, &displacement, symbol) != FALSE)
            std::snprintf (line, sizeof (line), "%s!%s+0x%llx", moduleName, symbol->Name,
                           static_cast<unsigned long long> (displacement));
        else
            std::snprintf (line, sizeof (line), "%s+0x%llx", moduleName,
                           static_cast<unsigned long long> (namedModule ? address - module.BaseOfImage
                                                                       : address));

        text = line;

        IMAGEHLP_LINE64 location {};
        location.SizeOfStruct = sizeof (location);
        DWORD lineDisplacement = 0;

        if (SymGetLineFromAddr64 (GetCurrentProcess(), address, &lineDisplacement, &location) != FALSE)
            text += std::string (" (") + location.FileName + ":" + std::to_string (location.LineNumber)
                    + ")";

        return text;
    }

    void dumpThread (std::ostream& out, const char* name, unsigned long long threadId) noexcept
    {
        if (threadId == 0)
        {
            out << "  " << name << ": the thread was never seen this run\n";
            return;
        }

        const auto id = static_cast<DWORD> (threadId);
        HANDLE thread = OpenThread (THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME
                                        | THREAD_QUERY_INFORMATION,
                                    FALSE, id);

        if (thread == nullptr)
        {
            out << "  " << name << ": could not open thread " << id << " (error " << GetLastError()
                << ")\n";
            return;
        }

        if (SuspendThread (thread) == static_cast<DWORD> (-1))
        {
            out << "  " << name << ": could not suspend thread " << id << " (error "
                << GetLastError() << ")\n";
            CloseHandle (thread);
            return;
        }

        CONTEXT context {};
        context.ContextFlags = CONTEXT_FULL;

        if (GetThreadContext (thread, &context) == FALSE)
        {
            out << "  " << name << ": could not read the context of thread " << id << " (error "
                << GetLastError() << ")\n";
            ResumeThread (thread);
            CloseHandle (thread);
            return;
        }

        STACKFRAME64 frame {};
        DWORD machineType = IMAGE_FILE_MACHINE_AMD64;

       #if defined(_M_ARM64)
        machineType = IMAGE_FILE_MACHINE_ARM64;
        frame.AddrPC.Offset = context.Pc;
        frame.AddrStack.Offset = context.Sp;
        frame.AddrFrame.Offset = context.Fp;
       #elif defined(_M_IX86)
        machineType = IMAGE_FILE_MACHINE_I386;
        frame.AddrPC.Offset = context.Eip;
        frame.AddrStack.Offset = context.Esp;
        frame.AddrFrame.Offset = context.Ebp;
       #else
        frame.AddrPC.Offset = context.Rip;
        frame.AddrStack.Offset = context.Rsp;
        frame.AddrFrame.Offset = context.Rbp;
       #endif

        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrStack.Mode = AddrModeFlat;
        frame.AddrFrame.Mode = AddrModeFlat;

        out << "  " << name << " (tid " << id << "):\n";

        for (int depth = 0; depth < 32; ++depth)
        {
            if (StackWalk64 (machineType, GetCurrentProcess(), thread, &frame, &context, nullptr,
                             SymFunctionTableAccess64, SymGetModuleBase64, nullptr)
                == FALSE)
                break;

            if (frame.AddrPC.Offset == 0)
                break;

            out << "    " << describeAddress (frame.AddrPC.Offset) << '\n';
        }

        ResumeThread (thread);
        CloseHandle (thread);
    }
   #endif
} // namespace

ShutdownWatchdog::ShutdownWatchdog (int limitMilliseconds) noexcept
    : limit (limitMilliseconds > 0 ? limitMilliseconds : 2000)
{
}

std::string ShutdownWatchdog::stacksPath() const
{
    return logPath() + ".stacks.txt";
}

void ShutdownWatchdog::arm() noexcept
{
    if (! enabled())
        return;

    if (armed.exchange (true))
        return;

    finished.store (false, std::memory_order_relaxed);
    fired.store (false, std::memory_order_relaxed);
    began = std::chrono::steady_clock::now();

    phase ("shutdown: watchdog armed");

    // Detached on purpose: if the main thread never returns, nothing here is
    // going to join it, and the process is about to be killed by the user.
    std::thread (&ShutdownWatchdog::watch, this).detach();
}

void ShutdownWatchdog::phase (const char* what) noexcept
{
    if (what == nullptr)
        return;

    note ((std::string ("phase: ") + what).c_str());
}

void ShutdownWatchdog::disarm() noexcept
{
    finished.store (true, std::memory_order_release);

    if (armed.exchange (false))
        phase ("shutdown: complete");
}

void ShutdownWatchdog::watch()
{
    for (;;)
    {
        if (finished.load (std::memory_order_acquire))
            return;

        if (std::chrono::steady_clock::now() - began
            >= std::chrono::milliseconds (limit))
            break;

        std::this_thread::sleep_for (std::chrono::milliseconds (20));
    }

    if (finished.load (std::memory_order_acquire))
        return;

    fired.store (true, std::memory_order_relaxed);
    writeStacks();
}

void ShutdownWatchdog::appendPhaseTrace (std::string& destination)
{
    destination += "\nphase trace (the guard's own log, oldest first):\n";

    std::ifstream in (logPath(), std::ios::binary);
    std::string text ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());

    // The guard only reports; it does not read the whole of a runaway file.
    constexpr std::size_t cap = 262144;
    const auto first = text.size() > cap ? text.size() - cap : std::size_t { 0 };
    destination += text.substr (first);
}

void ShutdownWatchdog::writeStacks()
{
    try
    {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds> (
                                 std::chrono::steady_clock::now() - began)
                                 .count();

        std::ostringstream report;
        report << "===== MiBoX shutdown watchdog =====\n";
        report << "the teardown had not finished " << elapsed << " ms after it started (limit "
               << limit << " ms)\n";
        report << "the process is left exactly as it was: nothing was aborted or forced\n\n";

        report << "threads:\n";
        for (int slot = 0; slot < static_cast<int> (ThreadSlot::count); ++slot)
        {
            const auto which = static_cast<ThreadSlot> (slot);
            report << "  " << threadSlotName (which) << ": tid "
                   << recordedThreadId (which) << '\n';
        }

        report << "\nstacks:\n";

       #if defined(_WIN32)
        SymbolSession symbols;

        if (! symbols.ready)
            report << "  (the symbol handler could not be initialised, error " << GetLastError()
                   << ")\n";

        report << "---- message thread ----\n";
        dumpThread (report, "message", recordedThreadId (ThreadSlot::message));
        report << "---- gl thread ----\n";
        dumpThread (report, "gl", recordedThreadId (ThreadSlot::gl));
        report << "---- audio thread ----\n";
        dumpThread (report, "audio", recordedThreadId (ThreadSlot::audio));
       #else
        report << "  thread stacks are captured on Windows only in this build; the thread ids\n"
                  "  and the phase trace above still say which step the teardown stopped at.\n";
       #endif

        std::string finalReport = report.str();
        appendPhaseTrace (finalReport);

        std::ofstream out (stacksPath(), std::ios::trunc);
        out << finalReport;
    }
    catch (...)
    {
        // A report that cannot be written is a missing report, never a reason for
        // the watchdog thread to throw.
    }
}

ShutdownWatchdog& shutdownWatchdog()
{
    static ShutdownWatchdog guard;
    return guard;
}
} // namespace j37diag
