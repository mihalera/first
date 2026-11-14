#include "DiagCore.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <mutex>
#include <vector>

#if defined(_WIN32)
 #include <process.h>
 #include <windows.h>
#else
 #include <functional>
 #include <thread>
 #include <unistd.h>
#endif

namespace j37diag
{
namespace
{
    std::mutex& logLock()
    {
        static std::mutex lock;
        return lock;
    }

    std::string& mutableLogPath()
    {
        static std::string path { "mibox-diagnostics.log" };
        return path;
    }

    Sink sinkFunction = nullptr;
    void* sinkContext = nullptr;

    std::atomic<bool> switchedOn { false };
    std::atomic<bool> switchRead { false };
    std::atomic<bool> headerWritten { false };

    std::atomic<unsigned long long> audioBlocks { 0 };
    std::atomic<unsigned long long> audioOverBudget { 0 };
    std::atomic<unsigned long long> audioNonFinite { 0 };
    std::atomic<float> audioLoudest { 0.0f };
    std::atomic<unsigned long long> audioStereoBlocks { 0 };
    std::atomic<unsigned long long> audioNonFiniteSides { 0 };
    std::atomic<float> audioLeftPeak { 0.0f };
    std::atomic<float> audioRightPeak { 0.0f };
    std::atomic<float> audioImbalance { 0.0f };

    std::atomic<unsigned long long>& slotWord (ThreadSlot slot) noexcept
    {
        static std::atomic<unsigned long long> words[static_cast<int> (ThreadSlot::count)] {};
        return words[static_cast<int> (slot)];
    }

    bool environmentSaysOn() noexcept
    {
       #if defined(_WIN32)
        char* value = nullptr;
        size_t length = 0;

        if (_dupenv_s (&value, &length, "MIBOX_DIAG") != 0 || value == nullptr)
            return false;

        const auto on = value[0] == '1' || value[0] == 'y' || value[0] == 'Y'
                        || value[0] == 't' || value[0] == 'T';
        std::free (value);
        return on;
       #else
        const auto* value = std::getenv ("MIBOX_DIAG");

        if (value == nullptr)
            return false;

        return value[0] == '1' || value[0] == 'y' || value[0] == 'Y'
               || value[0] == 't' || value[0] == 'T';
       #endif
    }

    unsigned long long currentThreadId() noexcept
    {
       #if defined(_WIN32)
        return static_cast<unsigned long long> (GetCurrentThreadId());
       #else
        // pthread_t is an integer on Linux and a pointer on macOS, so casting it
        // is not portable; the hash of the C++ thread id is a number that is
        // stable for a thread on both, and that is all the report needs.
        return static_cast<unsigned long long> (
            std::hash<std::thread::id> {} (std::this_thread::get_id()));
       #endif
    }

    int currentProcessId() noexcept
    {
       #if defined(_WIN32)
        return static_cast<int> (_getpid());
       #else
        return static_cast<int> (getpid());
       #endif
    }

    std::string startedAt;

    std::string& runStartText()
    {
        if (startedAt.empty())
            startedAt = timestamp();
        return startedAt;
    }
} // namespace

std::string timestamp()
{
    using namespace std::chrono;

    const auto now = system_clock::now();
    const auto seconds = system_clock::to_time_t (now);
    const auto millis = duration_cast<milliseconds> (now.time_since_epoch()).count() % 1000;

    std::tm parts {};
   #if defined(_WIN32)
    localtime_s (&parts, &seconds);
   #else
    localtime_r (&seconds, &parts);
   #endif

    char text[40] {};
    std::snprintf (text, sizeof (text), "%02d:%02d:%02d.%03d", parts.tm_hour, parts.tm_min,
                   parts.tm_sec, static_cast<int> (millis));
    return text;
}

bool enabled() noexcept
{
    if (! switchRead.load (std::memory_order_acquire))
    {
        switchedOn.store (environmentSaysOn(), std::memory_order_relaxed);
        switchRead.store (true, std::memory_order_release);
    }

    return switchedOn.load (std::memory_order_relaxed);
}

void setEnabled (bool shouldBeEnabled) noexcept
{
    switchedOn.store (shouldBeEnabled, std::memory_order_relaxed);
    switchRead.store (true, std::memory_order_release);
}

void setLogPath (const std::string& path) noexcept
{
    if (path.empty())
        return;

    try
    {
        std::lock_guard<std::mutex> lock (logLock());
        mutableLogPath() = path;
    }
    catch (...)
    {
    }
}

std::string logPath()
{
    try
    {
        std::lock_guard<std::mutex> lock (logLock());
        return mutableLogPath();
    }
    catch (...)
    {
        return {};
    }
}

void setSink (Sink sink, void* context) noexcept
{
    std::lock_guard<std::mutex> lock (logLock());
    sinkFunction = sink;
    sinkContext = context;
}

void note (const char* text) noexcept
{
    if (text == nullptr || ! enabled())
        return;

    try
    {
        std::lock_guard<std::mutex> lock (logLock());

        if (sinkFunction != nullptr)
        {
            sinkFunction (text, sinkContext);
            return;
        }

        std::ofstream out (mutableLogPath(), std::ios::app);

        if (! out)
            return;

        out << timestamp() << " tid " << currentThreadId() << " " << text << '\n';
    }
    catch (...)
    {
        // Diagnostics must never be the reason a plugin throws. A full disk or a
        // read-only path means the trace is missing, not that the audio is.
    }
}

void beginRun (const char* where) noexcept
{
    if (! enabled())
        return;

    if (headerWritten.exchange (true, std::memory_order_acq_rel))
        return;

    try
    {
        std::lock_guard<std::mutex> lock (logLock());

        if (sinkFunction != nullptr)
        {
            sinkFunction ("MiBoX diagnostics: this run", sinkContext);
            return;
        }

        std::ofstream out (mutableLogPath(), std::ios::app);

        if (! out)
            return;

        out << "\n===== MiBoX diagnostics, run started " << runStartText()
            << " (pid " << currentProcessId() << ") =====\n";

        if (where != nullptr)
            out << timestamp() << " where: " << where << '\n';
    }
    catch (...)
    {
    }
}

void noteFirstFrame (const char* what) noexcept
{
    static std::atomic<bool> noted { false };

    if (! enabled() || noted.exchange (true, std::memory_order_acq_rel))
        return;

    note (what != nullptr ? what : "first frame");
}

void recordThread (ThreadSlot slot) noexcept
{
    if (slot >= ThreadSlot::count)
        return;

    slotWord (slot).store (currentThreadId(), std::memory_order_relaxed);
}

unsigned long long recordedThreadId (ThreadSlot slot) noexcept
{
    if (slot >= ThreadSlot::count)
        return 0;

    return slotWord (slot).load (std::memory_order_relaxed);
}

namespace
{
    // A relaxed "raise to this value" for the peak counters: the same loop
    // noteAudioBlock has always used for its own, factored so the two sides and
    // the loudest overall cannot drift apart in how they treat a NaN.
    void raiseTo (std::atomic<float>& target, float value) noexcept
    {
        auto current = target.load (std::memory_order_relaxed);

        while (value > current
               && ! target.compare_exchange_weak (current, value,
                                                  std::memory_order_relaxed,
                                                  std::memory_order_relaxed))
        {
        }
    }
}

void noteAudioBlock (double loadFraction, float peak) noexcept
{
    audioBlocks.fetch_add (1, std::memory_order_relaxed);

    // Written as a NEGATED comparison so a NaN load counts as an overrun rather
    // than silently passing: a callback that timed out is exactly the case where
    // the arithmetic downstream of the clock is least trustworthy.
    if (! (loadFraction <= 1.0))
        audioOverBudget.fetch_add (1, std::memory_order_relaxed);

    if (! std::isfinite (peak))
    {
        audioNonFinite.fetch_add (1, std::memory_order_relaxed);
        return;
    }

    auto current = audioLoudest.load (std::memory_order_relaxed);

    while (peak > current
           && ! audioLoudest.compare_exchange_weak (current, peak,
                                                    std::memory_order_relaxed,
                                                    std::memory_order_relaxed))
    {
    }
}

void noteStereoBlock (double loadFraction, float peakLeft, float peakRight) noexcept
{
    const auto leftFinite = std::isfinite (peakLeft);
    const auto rightFinite = std::isfinite (peakRight);

    // The peak the existing report keeps is the louder of the two sides, so a
    // stereo block is still counted exactly the way a mono one always was and
    // the two numbers stay comparable.
    noteAudioBlock (loadFraction,
                    leftFinite && rightFinite ? (peakLeft > peakRight ? peakLeft : peakRight)
                                              : (leftFinite ? peakLeft : peakRight));

    audioStereoBlocks.fetch_add (1, std::memory_order_relaxed);

    // The case this exists for: one side non-finite, the other not. Both broken
    // is the peak counter's business; one is a channel that stopped carrying
    // signal while its neighbour kept going.
    if (leftFinite != rightFinite)
        audioNonFiniteSides.fetch_add (1, std::memory_order_relaxed);

    if (leftFinite)
        raiseTo (audioLeftPeak, peakLeft);
    if (rightFinite)
        raiseTo (audioRightPeak, peakRight);

    // Imbalance only means something when both sides carry something: a silent
    // channel is the other counter's report, not a 100 % imbalance on every
    // block of a quiet passage.
    if (leftFinite && rightFinite && peakLeft > 0.0f && peakRight > 0.0f)
    {
        const auto louder = peakLeft > peakRight ? peakLeft : peakRight;
        const auto quieter = peakLeft > peakRight ? peakRight : peakLeft;
        raiseTo (audioImbalance, (louder - quieter) / louder);
    }
}

AudioReport audioReport() noexcept
{
    AudioReport report;
    report.blocks = audioBlocks.load (std::memory_order_relaxed);
    report.overBudget = audioOverBudget.load (std::memory_order_relaxed);
    report.nonFinitePeaks = audioNonFinite.load (std::memory_order_relaxed);
    report.loudestPeak = audioLoudest.load (std::memory_order_relaxed);
    report.stereoBlocks = audioStereoBlocks.load (std::memory_order_relaxed);
    report.nonFiniteSides = audioNonFiniteSides.load (std::memory_order_relaxed);
    report.leftPeak = audioLeftPeak.load (std::memory_order_relaxed);
    report.rightPeak = audioRightPeak.load (std::memory_order_relaxed);
    report.widestImbalance = audioImbalance.load (std::memory_order_relaxed);
    return report;
}

std::string audioReportText() noexcept
{
    const auto report = audioReport();

    if (report.blocks == 0)
        return {};

    const auto asDb = [] (float peak)
    {
        return peak > 0.0f ? 20.0 * std::log10 (static_cast<double> (peak)) : -240.0;
    };

    const auto peakDb = asDb (report.loudestPeak);

    char text[320] {};
    std::snprintf (text, sizeof (text),
                   "audio: %llu blocks, %llu over budget, %llu non-finite peak(s), "
                   "loudest %.1f dBFS",
                   report.blocks, report.overBudget, report.nonFinitePeaks, peakDb);

    // The stereo half is only written when stereo blocks were actually counted,
    // so a mono host's line reads exactly as it always did.
    if (report.stereoBlocks > 0)
    {
        char stereo[224] {};
        std::snprintf (stereo, sizeof (stereo),
                       "; %llu stereo block(s), L %.1f / R %.1f dBFS, "
                       "widest L/R imbalance %.0f %%, %llu one-sided non-finite",
                       report.stereoBlocks, asDb (report.leftPeak),
                       asDb (report.rightPeak),
                       static_cast<double> (report.widestImbalance) * 100.0,
                       report.nonFiniteSides);
        strncat (text, stereo, sizeof (text) - std::strlen (text) - 1);
    }

    return text;
}

const char* threadSlotName (ThreadSlot slot) noexcept
{
    switch (slot)
    {
        case ThreadSlot::message: return "message";
        case ThreadSlot::gl:      return "gl";
        case ThreadSlot::audio:   return "audio";

            //  Spelled out rather than left to `default`: -Wswitch-enum wants
            //  every enumerator named, and a value added later should be a
            //  warning at the switch rather than a silent "unknown" here.
        case ThreadSlot::count:   break;
    }

    return "unknown";
}
} // namespace j37diag
