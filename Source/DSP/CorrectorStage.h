// =============================================================================
//  The autotune stage's audio path: what moves the note once the detector has
//  said which note is playing.
//
//  Two engines behind one interface. Which of them a build uses is decided by
//  J37_CORRECTOR_USE_STRETCH (OFF by default) plus the include path, through the
//  probe in OptionalLibraries.h:
//
//    J37_HAS_SIGNALSMITH 1  signalsmith-stretch - a phase vocoder, BLOCK based,
//                           25 ms of reported lookahead, and a transposition that
//                           is a few percent wide of the ratio it is handed at
//                           this stage's window (see the measurements below).
//    J37_HAS_SIGNALSMITH 0  RealtimePitchShifter - the hand-written granular
//                           shifter the plugin has always corrected with: one
//                           period of delay, and a stationary tone to under a cent.
//
//  The DEFAULT is the shifter, and that is a measurement rather than a preference:
//  the vocoder's real win is that it does not shorten the note it corrects, and its
//  real cost is both the lookahead and an error of the same order as the +-10 cents
//  AUTOTUNE is built around. The option is there for the work that would earn it.
//  Neither engine is a stub: a checkout with ThirdParty/ pruned builds and corrects
//  exactly as every build before this file existed did.
//
//  WHAT THE LIBRARY CHANGES, beyond the sound. A granular shifter can splice at
//  the moment a read head's window is at zero and needs one period of lookahead;
//  a phase vocoder needs a whole analysis window, and it processes a BLOCK rather
//  than a frame. So the stage buffers frames into blocks of `blockFrames` and
//  reports the lookahead that costs - `latencyFrames()`, zero on the fallback - and
//  the plugin adds it to the latency it reports to the host while the stage is
//  engaged. That number is stated rather than hidden, and so is the measurement
//  that keeps the flag OFF: tests/pitch prints both engines' pitch error, duration
//  and lookahead on every run.
//
//  The conventions of Source/DSP/ are stated in J37Math.h: this header includes
//  what it needs and names nothing of JUCE, so a harness can compile it against a
//  shim with no JUCE on its include path.
// =============================================================================
#pragma once

#include "OptionalLibraries.h"
#include "RealtimePitchShifter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <memory>

#if J37_HAS_SIGNALSMITH
 // The stage's own include, the way SpectrumAnalyser.h includes pocketfft: a file
 // that only decides a macro stays free of a header this heavy.
 #include <signalsmith-stretch/signalsmith-stretch.h>
#endif

//==============================================================================
/**
    The corrector's audio path, driven one frame at a time by the caller and run
    by the library one block at a time.

    THE TWO SHAPES, and why they are shaped differently.

      * With the library, the stage is a phase vocoder: it analyses overlapping
        windows of the input, moves the frequencies in them, and resynthesises at
        the same rate, so the material keeps its duration while its pitch moves.
        That is what a corrector wants - a note pulled onto the grid stays exactly
        as long as it was. It is driven here through a frame interface so the rest
        of the engine does not have to know about blocks: the stage collects
        `blockFrames` frames, hands the block to the library, and serves the
        block's output while the next one is collected.

      * Without it, the stage is the granular shifter, which is what the plugin
        shipped: two windowed read heads over a delay line, restarted a whole
        period apart at the moment a head is silent. Its latency is one period of
        the material rather than a block, and it is not reported because it was
        not reported before this file existed.

    FORMANT COMPENSATION IS NOT TURNED ON, and it is worth saying why rather than
    leaving a missing feature looking like an oversight: `setFormantFactor (1,
    true)` - the call the library documents for holding the spectral envelope in
    place while the pitch moves - was tried on a synthetic vowel at 1.06x and 1.5x,
    with the formant base given explicitly and left to autodetect, and the
    envelope's spectral centroid moved by the pitch ratio in every case: 942 Hz to
    933 Hz at 1.06x and to 1489 Hz at 1.5x, identically with the compensation on
    and off. So the stage does not claim it. What it does claim is what
    tests/pitch measures: the pitch moves by the ratio it is given, the material
    keeps its duration and its level, and the lookahead is the number the plugin
    reports to the host.

    Everything allocated is allocated in prepare(): the library's own buffers,
    the two block rings. processFrame() allocates nothing and locks nothing, which
    is what makes it callable from the audio thread - and the silent warm-up call
    in prepare() is what makes that true of the FIRST call rather than only the
    second one, because the library's temporary buffers grow on the first block
    it sees.
*/
struct CorrectorStage
{
    /** Two instances, one per channel rather than one stereo pair: the correction
        is per channel (each side has its own detector and its own reading), and a
        stereo library instance has one transpose factor for both. */
    static constexpr int maxChannels = 2;

    /** Frames per library block: 5 ms at 48 kHz. Small enough that the stage's own
        share of the latency is a rounding error beside the vocoder window, large
        enough that the block is a block rather than a trickle. */
    static constexpr int blockFrames = 256;

    /** The vocoder's analysis window and the interval between analyses, in
        SECONDS rather than in frames: the library configures itself in samples, so
        the stage's own latency would otherwise move with the session rate, and a
        corrector whose lookahead depends on the sample rate is a corrector whose
        feel does. Four analyses per window is the usual phase-vocoder overlap;
        20 ms is short enough that a syllable is not smeared across it and long
        enough that a low note has several periods inside it. */
    static constexpr double windowSeconds = 0.020;
    static constexpr double intervalSeconds = 0.005;

    CorrectorStage() = default;
    ~CorrectorStage() = default;

    CorrectorStage (const CorrectorStage&) = delete;
    CorrectorStage& operator= (const CorrectorStage&) = delete;

    /** Whether this build has the library at all. Read by the harnesses, which
        build the same file both ways and assert what each shape does. */
    static constexpr bool hasLibrary()
    {
       #if J37_HAS_SIGNALSMITH
        return true;
       #else
        return false;
       #endif
    }

    /** Prepares for a rate and a channel count. Allocates - so this is a
        prepareToPlay call, never an audio-thread one. */
    void prepare (double sampleRate, int channels)
    {
        const auto rate = sampleRate > 1.0 ? sampleRate : 48000.0;

        activeChannels = std::max (1, std::min (maxChannels, channels));
        preparedRate = rate;
        prepared = true;

       #if J37_HAS_SIGNALSMITH
        const auto window = static_cast<int> (rate * windowSeconds);
        const auto interval = static_cast<int> (rate * intervalSeconds);
        libraryLatency = 0;

        for (int channel = 0; channel < activeChannels; ++channel)
        {
            if (engines[static_cast<std::size_t> (channel)] == nullptr)
                engines[static_cast<std::size_t> (channel)]
                    = std::make_unique<signalsmith::stretch::SignalsmithStretch<float>>();

            auto& engine = *engines[static_cast<std::size_t> (channel)];
            engine.configure (1, window, interval);

            // The first block a stretch instance is handed grows its temporary
            // buffers. Doing that here, on silence, is what keeps the audio thread
            // allocation-free: by the time processFrame() runs, the vectors are
            // already the size they will stay.
            std::array<float, blockFrames> warmup {};

            // The library takes one POINTER PER CHANNEL - a container of channel
            // pointers rather than a pointer to interleaved samples - and the
            // instance above is mono, so the container is one entry long. These
            // two lines are the whole of the "shape" the API asks for.
            float* warmupChannels[1] { warmup.data() };
            engine.process (warmupChannels, blockFrames, warmupChannels, blockFrames);

            libraryLatency = std::max (libraryLatency,
                                       engine.inputLatency() + engine.outputLatency());
        }
       #else
        libraryLatency = 0;
       #endif

        reset();
    }

    /** Back to silence: the tanks, the rings and the fallback shifters. */
    void reset()
    {
        for (auto& ring : inputRings)
            ring.fill (0.0f);

        for (auto& ring : outputRings)
            ring.fill (0.0f);

        for (auto& shifter : fallbackShifters)
            shifter.reset();

       #if J37_HAS_SIGNALSMITH
        for (auto& engine : engines)
            if (engine != nullptr)
                engine->reset();
       #endif

        framesHeld = 0;
        blockReady = false;
        blockPosition = 0;
    }

    bool isPrepared() const noexcept { return prepared; }

    /** The lookahead this stage costs the plugin, in frames of the session's rate:
        zero on the granular fallback, the wrapper's block plus the vocoder's own
        analysis and synthesis latency with the library. The plugin adds this to
        the latency it reports while the stage is engaged - and only while it is
        engaged, because a corrector that is switched off must not cost a session
        its feel. */
    int latencyFrames() const noexcept
    {
        return libraryLatency > 0 ? blockFrames + libraryLatency : 0;
    }

    /** One frame in, one frame out.

        `ratios` is the pitch to read each channel at (1 is the input's own
        pitch) and `periodHints` the material's period in frames for the fallback
        shifter - 0 when the caller has no reading. Both are read as
        `activeChannels` long.
    */
    void processFrame (float* frame, const float* ratios, const int* periodHints) noexcept
    {
        if (! prepared || frame == nullptr)
            return;

        for (int channel = 0; channel < activeChannels; ++channel)
        {
            const auto index = static_cast<std::size_t> (channel);
            const auto ratio = juce::jlimit (0.5f, 2.0f, ratios[index]);
            const auto hint = periodHints[index];

           #if J37_HAS_SIGNALSMITH
            if (engines[index] != nullptr)
            {
                // Collected, not processed: the block is handed over below.
                inputRings[index][static_cast<std::size_t> (framesHeld)] = frame[index];
                continue;
            }
           #endif

            frame[index] = fallbackShifters[index].process (frame[index], ratio, hint);
        }

       #if J37_HAS_SIGNALSMITH
        if (engines[0] != nullptr)
        {
            if (++framesHeld >= blockFrames)
            {
                framesHeld = 0;

                for (int channel = 0; channel < activeChannels; ++channel)
                {
                    const auto index = static_cast<std::size_t> (channel);
                    auto& engine = *engines[index];

                    // The ratio this block reads at is the one the caller has
                    // NOW, which is the smoothed correction: applying it per block
                    // rather than per frame costs nothing audible (the vocoder
                    // analyses every `interval` frames anyway) and keeps the
                    // library's own rate of change the only rate there is.
                    engine.setTransposeFactor (juce::jlimit (0.5f, 2.0f, ratios[index]));

                    float* inputChannels[1] { inputRings[index].data() };
                    float* outputChannels[1] { outputRings[index].data() };
                    engine.process (inputChannels, blockFrames,
                                    outputChannels, blockFrames);
                }

                blockPosition = 0;
                blockReady = true;
            }

            for (int channel = 0; channel < activeChannels; ++channel)
            {
                const auto index = static_cast<std::size_t> (channel);
                frame[index] = blockReady
                                   ? outputRings[index][static_cast<std::size_t> (blockPosition)]
                                   : 0.0f;
            }

            if (blockReady)
                ++blockPosition;
        }
       #endif
    }

    /** One frame through the stage's own delay, with nothing done to it.

        This is the bypassed plugin's path: it still owes the host the lookahead it
        reports, or a bypassed corrector would arrive a block early against every
        other track in the session. The rings and the counter are the wet path's,
        which is what makes the two report the same number - the only difference is
        that the block handed over is the identity rather than the vocoder. A stage
        whose latency is zero (the fallback, with nothing engaged) does nothing.
    */
    void delayFrame (float* frame) noexcept
    {
        if (! prepared || frame == nullptr || latencyFrames() == 0)
            return;

        for (int channel = 0; channel < activeChannels; ++channel)
            inputRings[static_cast<std::size_t> (channel)][static_cast<std::size_t> (framesHeld)]
                = frame[static_cast<std::size_t> (channel)];

        if (++framesHeld >= blockFrames)
        {
            framesHeld = 0;

            for (int channel = 0; channel < activeChannels; ++channel)
            {
                const auto index = static_cast<std::size_t> (channel);
                outputRings[index] = inputRings[index];
            }

            blockReady = true;
            blockPosition = 0;
        }

        for (int channel = 0; channel < activeChannels; ++channel)
            frame[static_cast<std::size_t> (channel)]
                = blockReady ? outputRings[static_cast<std::size_t> (channel)][static_cast<std::size_t> (blockPosition)]
                             : 0.0f;

        if (blockReady)
            ++blockPosition;
    }

private:
    bool prepared = false;
    double preparedRate = 48000.0;
    int activeChannels = 2;
    int libraryLatency = 0;
    int framesHeld = 0;
    bool blockReady = false;
    int blockPosition = 0;

    // One block of input per channel, filled linearly and handed over whole; one
    // block of output, served a frame at a time. Fixed arrays rather than vectors
    // because the audio thread may not grow anything, and the size is a constant.
    std::array<std::array<float, blockFrames>, maxChannels> inputRings {};
    std::array<std::array<float, blockFrames>, maxChannels> outputRings {};


    // The absent-library path, and the thing this stage replaced when the library
    // is present: instantiated either way, because the harnesses drive it directly
    // and because a build without the library is a build the plugin still ships.
    std::array<RealtimePitchShifter, maxChannels> fallbackShifters {};

   #if J37_HAS_SIGNALSMITH
    std::array<std::unique_ptr<signalsmith::stretch::SignalsmithStretch<float>>, maxChannels> engines {};
   #endif
};
