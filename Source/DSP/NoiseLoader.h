// =============================================================================
//  NoiseLoader - a user WAV file, decoded off the audio thread and handed to it
//  without a lock.
//
//  The same shape as Source/DSP/IrStage.h, and for the same reason: the file is
//  bytes, the decode allocates, and a plugin's audio thread may do neither. So
//
//    * the FILE is read and decoded on the loader's own thread, which exists for
//      nothing else and is started when the user picks a file;
//    * the RESULT is published by a single atomic pointer swap;
//    * the OLD print is not freed at that moment. It is moved to a retirement
//      list that the audio thread never touches and that is emptied only in
//      NoiseLoader's destructor, because a reader that loaded the old pointer a
//      microsecond before the swap is still reading it. This is the argument
//      Source/DSP/NeuralStage.h's `retiredModels` makes for models, and it is
//      the same argument here;
//    * the audio thread's whole interface is one acquire load per BLOCK
//      (`active()`), one pointer per channel, and `NoisePrint::sample`, which is
//      an index and a wrap - no allocation, no lock, no system call.
//
//  A rate change is NOT a re-read of the file: `prepare` keeps the print at the
//  rate it was decoded at and converts a copy, so a session that changes from
//  48 kHz to 96 kHz costs arithmetic rather than disk. That conversion allocates,
//  and it happens in prepareToPlay and on the loader thread - never on the audio
//  thread.
//
//  The conventions of Source/DSP/ are stated in J37Math.h, and this header leans
//  on the second of them, exactly as IrStage.h does: these headers do not
//  include JUCE themselves, because a harness compiles some of them against a
//  stand-in with no JUCE on its include path. What this file uses is therefore
//  what its INCLUDER has to have declared, named here so the requirement is
//  visible rather than implied:
//
//    juce_core            - File, String, Thread, MemoryBlock, CriticalSection
//
//  Inside the plugin that includer is JuceHeader.h, which carries all of them.
//  Under tests/dsp/check_headers_juce.sh it is the module headers the script
//  puts in front of every header it compiles.
// =============================================================================
#pragma once

#include "NoiseDecoder.h"

#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace j37dsp
{

//==============================================================================
/**
    The vinyl stage's user noise print: a file, decoded in the background, played
    as a loop by the audio thread.

    Lifetime and threading, stated once so the three rules below are not
    folklore:

      * `requestLoad`, `clear`, `prepare` and the getters are MESSAGE THREAD
        calls. `requestLoad` and `clear` do not read the file; they tell the
        worker what to do next and return.
      * `active()` is the AUDIO THREAD call: an acquire load of one atomic
        pointer. It may be called once per block and the result used for the
        block's whole length.
      * the object is not safe to destroy while audio is running, exactly like
        every other stage in this plugin: `prepareToPlay` and the destructor run
        under the host's lock with the audio thread stopped.
*/
class NoiseLoader
{
public:
    NoiseLoader() = default;

    ~NoiseLoader()
    {
        // The worker is joined before anything it could be writing to is freed.
        stopWorker();
        retireAll();
    }

    //==========================================================================
    /** Called from prepareToPlay. Converts the loaded print to the new engine
        rate if one is loaded and the rate moved; a no-op otherwise, so a host
        that prepares repeatedly costs nothing. */
    void prepare (double sampleRate)
    {
        if (! (sampleRate > 0.0))
            return;

        const juce::ScopedLock lock (controlLock);

        if (std::abs (engineRate - sampleRate) < 1.0e-6 && engineRate > 0.0 && prepared)
            return;

        engineRate = sampleRate;
        prepared = true;

        if (sourcePrint != nullptr)
        {
            if (! rebuildAtEngineRate())
            {
                lastErrorMessage = "the print could not be converted to the engine rate";
            }
            else
            {
                lastErrorMessage = {};
            }
        }
    }

    //==========================================================================
    /** Ask for a file to be decoded. Returns immediately: the read and the
        decode happen on the worker. A request that arrives while another is in
        flight REPLACES it, because the user's last choice is the one they
        meant. */
    void requestLoad (const juce::File& file)
    {
        stopWorker();

        std::uint64_t requestGeneration = 0;
        {
            const juce::ScopedLock lock (controlLock);
            pendingFile = file;
            requestGeneration = ++generation;
            loadingFlag.store (true, std::memory_order_relaxed);
            lastErrorMessage = {};
        }

        startWorker (requestGeneration);
    }

    /** Stop using the print. The loop stops on the next block, and the samples
        are freed when the loader is destroyed - not on the audio thread. */
    void clear()
    {
        stopWorker();

        const juce::ScopedLock lock (controlLock);
        ++generation;
        pendingFile = juce::File();
        lastErrorMessage = {};
        loadingFlag.store (false, std::memory_order_relaxed);

        publish (nullptr);
        sourcePrint.reset();
        loadedFileNameText = {};
    }

    //==========================================================================
    /** The loop the audio thread plays, or nullptr for "no print loaded" - in
        which case the vinyl stage synthesises its own surface, which is what
        every build did before this file existed. */
    const NoisePrint* active() const noexcept
    {
        return activePrint.load (std::memory_order_acquire);
    }

    bool isLoading() const noexcept
    {
        return loadingFlag.load (std::memory_order_relaxed);
    }

    /** The file the loaded print came from, for the panel to show. Empty when
        nothing is loaded. */
    juce::String loadedFileName() const
    {
        const juce::ScopedLock lock (controlLock);
        return loadedFileNameText;
    }

    /** Why the last request produced no loop. Empty when the last request
        succeeded or when none has been made. */
    juce::String lastError() const
    {
        const juce::ScopedLock lock (controlLock);
        return lastErrorMessage;
    }

    /** Frames in the loaded loop, and the rate of the file it came from - the
        two numbers a user needs to understand what they are hearing. */
    std::size_t loadedFrames() const noexcept
    {
        const auto* print = active();
        return print != nullptr ? print->length : 0;
    }

    double loadedSourceRate() const noexcept
    {
        const auto* print = active();
        return print != nullptr ? print->sourceRate : 0.0;
    }

private:
    //==========================================================================
    /** The background worker. One exists at a time, created per request and
        joined when the next request arrives or the loader dies, so a long
        decode never blocks the message thread and a superseded request never
        writes over the newer one. */
    struct Worker final : public juce::Thread
    {
        Worker (NoiseLoader& ownerIn, juce::File fileIn, std::uint64_t generationIn)
            : juce::Thread ("J37 noise print"), owner (ownerIn), file (std::move (fileIn)),
              requestGeneration (generationIn) {}

        void run() override
        {
            if (threadShouldExit())
                return;

            juce::MemoryBlock bytes;

            if (! file.existsAsFile() || ! file.loadFileAsData (bytes))
            {
                owner.finishWithError ("the file could not be read", requestGeneration);
                return;
            }

            NoisePrint decoded;
            const auto outcome = decodeWavMemory (bytes.getData(), bytes.getSize(), decoded, 0.0);

            if (outcome.status != NoiseDecodeStatus::ok)
            {
                owner.finishWithError (outcome.reason, requestGeneration);
                return;
            }

            if (threadShouldExit())
                return;

            owner.finishWithPrint (std::move (decoded), file, requestGeneration);
        }

        NoiseLoader& owner;
        juce::File file;
        std::uint64_t requestGeneration = 0;
    };

    friend struct Worker;

    //==========================================================================
    void startWorker (std::uint64_t requestGeneration)
    {
        // The file copy is taken here rather than read later, so the worker
        // never touches `pendingFile` while the message thread may be writing it.
        juce::File file;

        {
            const juce::ScopedLock lock (controlLock);
            file = pendingFile;
        }

        worker = std::make_unique<Worker> (*this, file, requestGeneration);
        worker->startThread();
    }

    void stopWorker()
    {
        if (worker != nullptr)
        {
            // The wait is unbounded on purpose. A bounded one that expires would
            // destroy a Thread object that is still running, and the decode it is
            // inside is bounded by the minute-long cap on a print - a file the
            // size of an album side reads in tens of milliseconds. Waiting is the
            // honest cost of the one-worker-at-a-time rule; letting it return and
            // keeping the object alive would need a second list of zombies for no
            // gain the user could hear.
            worker->signalThreadShouldExit();
            worker->stopThread (-1);
            worker.reset();
        }
    }

    /** Worker thread. Publishes a decoded print, converted to the engine rate. */
    void finishWithPrint (NoisePrint&& decoded, const juce::File& file,
                          std::uint64_t requestGeneration)
    {
        auto preparedPrint = std::make_unique<NoisePrint>();

        {
            const juce::ScopedLock lock (controlLock);
            if (requestGeneration != generation)
                return;

            decoded.sourceRate = decoded.sampleRate;

            if (prepared && engineRate > 0.0
                && std::abs (decoded.sampleRate - engineRate) > 1.0e-6)
            {
                if (! resamplePrint (decoded, engineRate, *preparedPrint))
                {
                    lastErrorMessage = "the print could not be converted to the engine rate";
                    loadingFlag.store (false, std::memory_order_relaxed);
                    return;
                }

                preparedPrint->sourceRate = decoded.sampleRate;
            }
            else
            {
                *preparedPrint = std::move (decoded);
                preparedPrint->sourceRate = preparedPrint->sampleRate;
            }

            sourcePrint = std::make_unique<NoisePrint> (*preparedPrint);
            loadedFileNameText = file.getFileName();
            lastErrorMessage = {};
            loadingFlag.store (false, std::memory_order_relaxed);
        }

        publish (preparedPrint.release());
    }

    /** Worker thread. Records why nothing loaded and clears the busy flag. */
    void finishWithError (const char* reason, std::uint64_t requestGeneration)
    {
        const juce::ScopedLock lock (controlLock);
        if (requestGeneration != generation)
            return;

        lastErrorMessage = reason != nullptr ? reason : "the file could not be decoded";
        loadingFlag.store (false, std::memory_order_relaxed);
    }

    /** Swap the audio thread's pointer and retire what it was reading. Message
        thread only: the atomic store is what makes the swap visible, and the
        old print is kept alive rather than freed. */
    void publish (const NoisePrint* next)
    {
        const auto* previous = activePrint.exchange (next, std::memory_order_acq_rel);

        if (previous != nullptr)
        {
            const juce::ScopedLock lock (retireLock);
            retired.push_back (previous);
        }
    }

    /** Only ever called with the audio thread stopped: prepareToPlay's lock or
        the destructor. A print is deleted here and nowhere else. */
    void retireAll()
    {
        const juce::ScopedLock lock (retireLock);

        for (const auto* print : retired)
            delete print;

        retired.clear();
    }

    /** Message thread, with `controlLock` held. Rebuilds the engine-rate loop
        from the decoded one after a rate change. */
    bool rebuildAtEngineRate()
    {
        auto converted = std::make_unique<NoisePrint>();

        if (! resamplePrint (*sourcePrint, engineRate, *converted))
            return false;

        converted->sourceRate = sourcePrint->sampleRate;
        publish (converted.release());
        return true;
    }

    //==========================================================================
    mutable juce::CriticalSection controlLock;

    /** The file's own rate. Kept so a rate change does not re-read the file. */
    std::unique_ptr<NoisePrint> sourcePrint;

    /** At the engine's rate. What the audio thread reads. */
    std::atomic<const NoisePrint*> activePrint { nullptr };

    /** Prints the audio thread may still be inside. Owned here and deleted in
        the destructor - not at the swap that retired them. */
    mutable juce::CriticalSection retireLock;
    std::vector<const NoisePrint*> retired;

    juce::File pendingFile;

    // Named `...Text`/`...Message` rather than after the getters above them: a
    // member called `lastError` beside a method called `lastError()` is a
    // compile error the first time one of them is used, which is what the JUCE
    // header check found on the first run of this file.
    juce::String loadedFileNameText;
    juce::String lastErrorMessage;
    std::atomic<bool> loadingFlag { false };

    double engineRate = 0.0;
    bool prepared = false;
    std::uint64_t generation = 0;

    std::unique_ptr<Worker> worker;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoiseLoader)
};

} // namespace j37dsp
