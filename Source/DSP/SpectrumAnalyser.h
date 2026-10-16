// =============================================================================
//  The spectrum analyser: the shaper's own output, measured as a curve rather
//  than as the two numbers Source/DSP/HarmonicAnalyser.h reports.
//
//  Driven by tests/spectrum/spectrum_test.cpp, which includes this header rather
//  than a copy of it, and compiled by tests/dsp/check_headers_juce.sh with the
//  rest of the directory. The conventions of Source/DSP/ are stated in J37Math.h.
//
//  It also SHIPS: Source/PluginProcessor.h holds one by value, the signal path
//  feeds it the magnetic shaper's own output at the channel-0 tap the harmonic
//  analyser beside it is fed from, and its bands travel in the block telemetry
//  frame the SHAPERS page draws its curve from. So the numbers below are the ones
//  on a user's screen, and this file is the first thing to read when the curve is
//  wrong - after SpectrumMetrics.h, which is where the DRAWING of them lives.
//
//  WHY A LIBRARY, AND WHICH ONE. The transform is pocketfft (mreineck/pocketfft,
//  vendored under ThirdParty/pocketfft). It was linked into the plugin by
//  cmake/J37Dependencies.cmake and included by NOTHING - one of the sixteen
//  entries the review found on the link line with no `#include` anywhere under
//  Source/ - so the choice to use it here is the choice to give it a caller
//  rather than to keep paying for it and never calling it.
//
//  The alternative was juce::dsp::FFT, which the plugin already has (Source/DSP/
//  IrStage.h convolves with JUCE's own engine). It was NOT taken, and the reason
//  is this directory's convention rather than taste: these headers do not pull
//  JUCE in, because the harnesses compile them with no JUCE on the include path.
//  A stage typed on juce::dsp::FFT therefore cannot be RUN by any harness - it can
//  only be parsed, which is exactly what J37Math.h warns about and what IrStage.h
//  reports about itself ("Nothing measures this stage - no harness renders it").
//  pocketfft is a single header with no dependencies, so the whole stage stays
//  inside the measured world: a build can drive it at every frequency the harness
//  asks for and assert on what it reports.
//
//  WHAT IT IS FOR. HarmonicAnalyser answers "is the distortion even or odd" in
//  two Goertzel bins and needs no FFT at all; that is the right cost for a
//  balance the panel prints as a percentage. What it cannot answer is where the
//  energy is - an analyser shows the shape of what the machine did, which is the
//  one thing a saturation plugin's own panel is uniquely placed to show. It reads
//  the signal and writes nothing to it: nothing downstream consumes it, so a wrong
//  reading here is a wrong PICTURE, never a wrong sound.
//
//  COST, AND WHY THE CALL IS PER SAMPLE. The engine calls this once per sample,
//  not once per block, because that is the shape the tap has: the shaper's output
//  exists inside the per-sample loop and nowhere else, so a block-sized call would
//  need a second buffer carrying the signal out of the loop and back. What that
//  costs is a call, a store and a comparison per sample, and NOT a transform: the
//  transform runs once every hopSize frames, so the work is amortised exactly
//  where it is cheap to reason about.
//
//  The amortised part was measured before it was wired anywhere: a single-threaded
//  1024-point r2c is 6.1 us and 12.2 us at 2048 on the machine this was written
//  on. One transform per 512 frames is one per 10.7 ms at 48 kHz, so a 48 kHz
//  session spends about 0.06 % of its time here - against a block budget of the
//  same 10.7 ms. The number is the reason the stage is viable at all; it is quoted
//  with how it was measured rather than asserted, which is the rule docs/ follows.
//
//  REAL-TIME SAFETY. prepare() sizes everything and is the only place that
//  computes the window; process() allocates nothing, locks nothing and calls no
//  library function that does. The buffers are std::array members rather than
//  vectors so that even prepare() cannot reallocate after construction - the
//  stage is the same size object for its whole life.
// =============================================================================
#pragma once

#include <array>
#include <cmath>
#include <complex>
#include <cstddef>

#include "OptionalLibraries.h"

// The probe in OptionalLibraries.h only DECIDES the macro; the header itself is
// included here, by the one stage that wants it, exactly as Source/DSP/
// NeuralStage.h includes RTNeural's loader itself for the same reason. That keeps
// the probe file free of a four-thousand-line header nothing else in the engine
// has a use for.
#if J37_HAS_POCKETFFT
 #include <pocketfft_hdronly.h>
#endif

//==============================================================================
/**
    A log-spaced magnitude spectrum of whatever is fed to it, in dBFS.

    One channel at a time. The caller passes the samples it wants measured and
    this class does not sum anything: whether a stereo frame is shown as its left
    channel, as the mid, or as the sum is a decision with a level consequence
    (summing two correlated channels reads 6 dB high), and the stage that owns
    the audio is the one that knows which reading it wants.

    Bands are PEAK amplitudes, not summed power. A tone at a band's centre reads
    the same number whether the band spans two bins or twenty, so the curve's
    shape is the signal's and not an artefact of where the band edges happened to
    land. A power sum would make every band's reading depend on its own width, and
    the curve would then slope upward at the top for a reason that has nothing to
    do with the audio.

    WHAT THE LADDER CANNOT RESOLVE. Twenty-four log-spaced bands from 20 Hz make the
    lowest of them NARROWER than the transform's own bin spacing: at 48 kHz a
    1024-point transform resolves 46.9 Hz, and the band around 130 Hz is 37 Hz wide.
    A band like that cannot be measured, only approached. A tone inside it leaks
    through the window's main lobe into the nearest bin, and that bin may belong to
    the band below - so a low tone can read one band low, and the bottom of the
    curve is indicative rather than exact. This is not a defect to be tuned out: no
    arrangement of edges can separate two frequencies that a 21 ms window cannot
    separate, and the alternative readings are worse (a power sum makes narrow
    bands read low for a reason that has nothing to do with the signal).
    tests/spectrum asserts BOTH halves of the claim rather than the flattering one:
    a tone in a band wider than a bin reads in exactly its own band, and a tone in a
    band narrower than one reads within a single band of it.

    With pocketfft absent from the build (J37_HAS_POCKETFFT 0) the stage still
    compiles, still prepares and still accepts samples, and reports every band at
    the floor: `compiledIn` is what a caller checks before it draws anything, so a
    build without the library shows no curve rather than a wrong one. That is the
    same shape as the engine's other optional stages (a build with no model loaded
    is a bit-for-bit pass-through) and it is what keeps the header compilable
    under every harness.
*/
struct SpectrumAnalyser
{
    /** Transform size. 1024 at 48 kHz is a 21 ms window: long enough to resolve
        a low band, short enough that the curve still moves with the programme
        rather than averaging it into a still image. */
    static constexpr int fftSize = 1024;

    /** Frames between transforms. Half the window, so consecutive transforms
        share half their samples and a steady tone does not flicker between
        updates. */
    static constexpr int hopSize = fftSize / 2;

    /** Bands drawn. Enough for the curve to have a shape at the width a panel
        readout has, few enough that no band is narrower than a bin. */
    static constexpr int bandCount = 24;

    /** What a band reads when nothing is there. -90 dBFS is below the noise
        floor the engine's own noise generators sit at, so the curve's own floor
        is never mistaken for programme. */
    static constexpr float floorDb = -90.0f;

    /** True when this TRANSLATION UNIT was built with pocketfft on its include
        path. Constant, not a member: it is a property of the build. */
    static constexpr bool compiledIn = J37_HAS_POCKETFFT != 0;

    /** The newest reading, one per band, in dBFS and never above 0. */
    std::array<float, bandCount> bandsDb {};

    /** Band edges in Hz: band b covers [edgesHz[b], edgesHz[b + 1]). The last
        edge is the highest frequency the transform can report, so a caller can
        label an axis without knowing the rate the stage was prepared at. */
    std::array<float, bandCount + 1> edgesHz {};

    /** True once prepare() has built the window. False means every reading below
        is the floor, which is also what an unprepared stage reports. */
    bool ready = false;

    /** Prepares the window for a sample rate. Allocation-free: every buffer is a
        member, so this only fills them. Called from prepareToPlay. */
    void prepare (double sampleRateIn) noexcept
    {
        sampleRate = sampleRateIn > 1.0 ? static_cast<float> (sampleRateIn) : 48000.0f;

        // Hann rather than a rectangular window: a transform of a chopped-out
        // span of programme is a discontinuity at both ends, and that spreads
        // energy from every partial into every bin. The window is what makes a
        // reading mean "this frequency" rather than "somewhere in the band".
        windowSum = 0.0f;

        for (int i = 0; i < fftSize; ++i)
        {
            const auto t = static_cast<float> (i) / static_cast<float> (fftSize - 1);
            window[static_cast<std::size_t> (i)] = 0.5f - 0.5f * std::cos (2.0f * pi * t);
            windowSum += window[static_cast<std::size_t> (i)];
        }

        buildBandEdges();

        // The window is what makes the stage able to read anything at all, so it
        // is the last thing built and it is what `ready` reports. It is set from
        // `compiledIn` rather than to true because it is a property of the BUILD:
        // a translation unit compiled without pocketfft has no transform to run,
        // and a stage that claimed to be ready there would be a caller's green
        // light over a curve that never moves.
        ready = compiledIn;

        reset();
    }

    /** Drops the pending samples and floors the reading, leaving the window in
        place - the same contract LoudnessMeter::reset() has and for the same
        reason: a cleared meter must not also be an unprepared one. */
    void reset() noexcept
    {
        pending.fill (0.0f);
        windowed.fill (0.0f);
        fill = 0;
        bandsDb.fill (floorDb);

        // A reset clears the READING, not the building: reset() is what the
        // engine calls after prepare() and on a transport restart, and a stage
        // that dropped `ready` here would be an unprepared one for the rest of
        // the session. Only a build with no library is unready for good, and
        // there prepare() is where it is decided.
        if (! compiledIn)
            ready = false;
    }

#if J37_HAS_POCKETFFT
    /** Feeds one block of the channel being measured.

        Length is arbitrary and need not divide the transform: the samples are
        accumulated across calls and the transform runs whenever a window's worth
        has arrived. */
    void process (const float* samples, int numSamples) noexcept
    {
        if (! ready || samples == nullptr || numSamples <= 0)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            pending[static_cast<std::size_t> (fill)] = samples[i];

            if (++fill < fftSize)
                continue;

            transform();

            // Keep the second half of what was just transformed and let the next
            // block fill the rest: the overlap that stops a steady tone flickering
            // between updates. std::copy rather than a rotate, because the source
            // and destination ranges are disjoint at these offsets.
            std::copy (pending.begin() + hopSize, pending.end(), pending.begin());
            fill = hopSize;
        }
    }
#else
    /** Feeds a block. A build without pocketfft has no transform to run, so the
        samples are dropped and the reading stays at the floor. Present rather
        than absent so that a caller needs no #if of its own. */
    void process (const float*, int) noexcept {}
#endif

    /** The last transform's band readings, ready to draw. Nothing is copied out:
        the caller reads bandsDb directly, as the harnesses do. */
    const std::array<float, bandCount>& bands() const noexcept { return bandsDb; }

    /** The number of samples pended since the last transform. Read by the
        harnesses to assert the boundary (a full window is needed before any
        reading is produced) and by nothing in the audio path. */
    int pendingSamples() const noexcept { return fill; }

private:
    static constexpr float pi = 3.14159265358979323846f;

    /** Where the band edges are, in Hz. Log-spaced, because pitch is: a linear
        split would spend most of the curve on the top two octaves and draw the
        bottom five - where a tape machine's whole character lives - in three
        pixels. */
    void buildBandEdges() noexcept
    {
        const auto low = 20.0f;
        const auto high = std::min (20000.0f, sampleRate * 0.5f);
        const auto ratio = high > low ? std::pow (high / low, 1.0f / static_cast<float> (bandCount))
                                      : 1.0f;

        edgesHz[0] = low;

        for (int b = 1; b <= bandCount; ++b)
            edgesHz[static_cast<std::size_t> (b)] = low * std::pow (ratio, static_cast<float> (b));

        // The top edge is written rather than multiplied into place: a rate low
        // enough to put Nyquist under 20 kHz makes the last step overshoot it,
        // and an edge above Nyquist is a band with no bins in it.
        edgesHz[static_cast<std::size_t> (bandCount)] = high;
    }

#if J37_HAS_POCKETFFT
    void transform() noexcept
    {
        for (int i = 0; i < fftSize; ++i)
            windowed[static_cast<std::size_t> (i)] =
                window[static_cast<std::size_t> (i)] * pending[static_cast<std::size_t> (i)];

        const pocketfft::shape_t shape { static_cast<std::size_t> (fftSize) };
        const pocketfft::stride_t strideIn { static_cast<std::ptrdiff_t> (sizeof (float)) };
        const pocketfft::stride_t strideOut { static_cast<std::ptrdiff_t> (sizeof (std::complex<float>)) };
        const pocketfft::shape_t axes { 0 };

        pocketfft::r2c (shape, strideIn, strideOut, axes, true,
                        windowed.data(), spectrum.data(), 1.0f, 1);

        const auto binHz = sampleRate / static_cast<float> (fftSize);
        const auto scale = 2.0f / windowSum;

        for (int b = 0; b < bandCount; ++b)
        {
            const auto lo = static_cast<int> (edgesHz[static_cast<std::size_t> (b)] / binHz);
            const auto hi = static_cast<int> (edgesHz[static_cast<std::size_t> (b) + 1] / binHz);

            auto peak = 0.0f;

            for (int bin = lo; bin <= hi; ++bin)
            {
                // Bin 0 is DC and the last bin is Nyquist; neither can be the
                // inside of a band and neither is doubled by the one-sided
                // transform, so both are skipped rather than scaled wrongly.
                if (bin <= 0 || bin >= fftSize / 2)
                    continue;

                const auto magnitude = std::abs (spectrum[static_cast<std::size_t> (bin)]);
                peak = std::max (peak, magnitude * scale);
            }

            // A band that contains no bin at all - the lowest ones, at a high
            // enough rate - reports the floor rather than the level of a neighbour
            // it does not cover. (A band that contains a bin but is narrower than
            // the bin SPACING is the different case the class comment describes:
            // it reports that bin's level, which may be a lobe of a tone that sits
            // in the band above.)
            bandsDb[static_cast<std::size_t> (b)] =
                peak > 0.0f ? std::max (floorDb, 20.0f * std::log10 (peak)) : floorDb;
        }
    }
#endif

    std::array<float, fftSize> pending {};
    std::array<float, fftSize> windowed {};
    std::array<float, fftSize> window {};
#if J37_HAS_POCKETFFT
    std::array<std::complex<float>, fftSize / 2 + 1> spectrum {};
#endif

    float sampleRate = 48000.0f;
    float windowSum = 0.0f;
    int fill = 0;
};
