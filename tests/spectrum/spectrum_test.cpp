// =============================================================================
//  Regression harness for the spectrum analyser.
//
//  The analyser is a MEASUREMENT: it reads the signal and writes nothing to it,
//  so a wrong reading here is a wrong PICTURE and never a wrong sound, and no
//  stage in the audio path depends on its output. That is exactly why the claims
//  below are worth pinning rather than eyeballing later: a spectrum that is 12 dB
//  high, or that puts 6 kHz in the band labelled 2 kHz, looks entirely plausible
//  on a panel, and the person who notices is the one who already trusted it.
//
//  So the properties the stage's own doc comment makes are measured, on signal:
//
//    1. CALIBRATION. A full-scale sine sitting ON a bin centre reads 0.0 dBFS.
//       That is the anchor every number below is relative to, and it is what makes
//       the vertical axis mean something: the reading is an amplitude, so a
//       full-scale programme reads full scale rather than "some big number".
//    2. LINEARITY. The same tone 20 dB down reads 20 dB down - so the reading is
//       a LEVEL and not a shape. A curve that is compressed at the top is a curve
//       that lies about how hard the machine is being driven.
//    3. SCALLOPING, DECLARED. A tone BETWEEN two bin centres reads low, because a
//       Hann window's worst-case scalloping loss is about 1.4 dB. This is asserted
//       rather than tolerated: the bound belongs to the window this stage chose,
//       so what is checked is that the loss stays inside it. A reading that was
//       3 dB down between bins would be a window that does not overlap-add.
//    4. THE BANDS ARE IN ORDER. A tone an octave up lands in a HIGHER band, not
//       merely in a different one - the failure that puts a curve on screen upside
//       down and that no single-tone test would catch.
//    5. THE EDGES. Log-spaced from 20 Hz up to the lower of 20 kHz and Nyquist,
//       strictly increasing, with the top edge written rather than multiplied into
//       place - a band whose edges cross is a band that reads a neighbour's bins.
//       Checked at 48 kHz, where the top edge is 20 kHz, and at 8 kHz, where
//       Nyquist is below it and the whole ladder has to move.
//    6. THE WINDOW IS A WINDOW. Fewer than fftSize samples produce NO reading -
//       the bands stay at the floor - and the fftSize-th produces one. This is the
//       boundary that a caller drawing a curve runs into on the first frames after
//       a start, when a reading that "arrives early" would be a transform of a
//       half-filled buffer.
//    7. SILENCE. A machine at rest measures the floor in every band, and comes
//       back to it after a tone has been through. The same claim the loudness
//       meter's harness makes about its own window, for the same reason: a curve
//       that keeps a faded copy of the last thing played is a curve that lies
//       about a stopped transport.
//
//  THE TWO BUILDS. This file is compiled twice by run.sh: once with
//  ThirdParty/pocketfft on the include path (J37_HAS_POCKETFFT 1, the real
//  transform) and once with it off (0, the branch a build without the library
//  compiles). The second build is the only thing in this suite that ever compiles
//  the absent-library half of a Source/DSP/ header at all, so it asserts what that
//  half promises: it compiles, it prepares, it accepts samples and it reports the
//  floor - and `compiledIn` is false, which is what a caller checks before drawing
//  anything. Without this pass, the fallback would be written and never built.
//
//  No shim and no JUCE: the header is JUCE-free by design (see its own comment for
//  why that was the deciding argument for pocketfft over juce::dsp::FFT), so this
//  harness needs nothing but a C++17 compiler.
// =============================================================================

#include "DSP/SpectrumAnalyser.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
constexpr double kTwoPi = 6.283185307179586;
constexpr int kFftSize = SpectrumAnalyser::fftSize;

int gFailures = 0;

void check (bool condition, const std::string& what)
{
    std::printf ("  %-4s %s\n", condition ? "PASS" : "FAIL", what.c_str());

    if (! condition)
        ++gFailures;
}

std::string format (const char* pattern, double a)
{
    std::array<char, 256> buffer {};
    std::snprintf (buffer.data(), buffer.size(), pattern, a);
    return std::string (buffer.data());
}

std::string format2 (const char* pattern, double a, double b)
{
    std::array<char, 256> buffer {};
    std::snprintf (buffer.data(), buffer.size(), pattern, a, b);
    return std::string (buffer.data());
}

/** Sample rate at which 1 kHz is a bin centre: bin width is rate/1024, so at
    48 kHz bin 21 is 984.375 Hz. A tone ON a bin centre is the only one that can
    be asserted tightly, because a Hann window of a tone BETWEEN bins reads low by
    up to 1.42 dB (claim 3), and a test that used a round 1000 Hz would be
    measuring the window's scalloping rather than the stage's calibration. */
constexpr double kSampleRate = 48000.0;
constexpr double kBinWidth = kSampleRate / kFftSize;
constexpr double kCentreHz = 21.0 * kBinWidth;          // 984.375 Hz

/** The band a frequency falls in, by the edges the stage published. */
int bandOf (const SpectrumAnalyser& analyser, double hz)
{
    for (int b = 0; b < SpectrumAnalyser::bandCount; ++b)
        if (hz >= analyser.edgesHz[static_cast<std::size_t> (b)]
            && hz < analyser.edgesHz[static_cast<std::size_t> (b) + 1])
            return b;

    return -1;
}

/** The band the reading is loudest in - what a curve's peak is drawn at. */
int peakBand (const SpectrumAnalyser& analyser)
{
    auto best = 0;

    for (int b = 1; b < SpectrumAnalyser::bandCount; ++b)
        if (analyser.bandsDb[static_cast<std::size_t> (b)]
            > analyser.bandsDb[static_cast<std::size_t> (best)])
            best = b;

    return best;
}

/** A tone of `amplitude` at `frequency`, fed to the analyser in blocks of
    `blockSize` - the way the engine hands over audio rather than one sample at a
    time, so a stage that only worked on whole transforms would fail here. */
void feedTone (SpectrumAnalyser& analyser, double frequency, double amplitude,
               int totalSamples, int blockSize)
{
    std::vector<float> block (static_cast<std::size_t> (blockSize));

    for (int done = 0; done < totalSamples; done += blockSize)
    {
        const auto count = std::min (blockSize, totalSamples - done);

        for (int i = 0; i < count; ++i)
            block[static_cast<std::size_t> (i)] = static_cast<float> (
                amplitude * std::sin (kTwoPi * frequency
                                      * static_cast<double> (done + i) / kSampleRate));

        analyser.process (block.data(), count);
    }
}

/** A fresh analyser, prepared exactly the way the engine prepares a stage. */
SpectrumAnalyser prepared (double sampleRate)
{
    SpectrumAnalyser analyser;
    analyser.prepare (sampleRate);
    return analyser;
}

//==============================================================================
//  The build WITH the library: the transform, the bands and the levels.
//==============================================================================
void runLibraryBuild()
{
    // ---------------------------------------------------------------- 1. CALIBRATION
    {
        auto analyser = prepared (kSampleRate);
        feedTone (analyser, kCentreHz, 1.0, kFftSize, 256);

        const auto band = bandOf (analyser, kCentreHz);
        const auto reading = band >= 0 ? analyser.bandsDb[static_cast<std::size_t> (band)]
                                       : -200.0f;

        check (band >= 0, "984.375 Hz falls inside a band the stage published an edge for");

        check (std::abs (reading - 0.0f) < 0.2f,
               format2 ("a full-scale tone on a bin centre reads %.3f dBFS, 0.000 expected (tol 0.2) for band %.0f",
                        reading, static_cast<double> (band)));
    }

    // ---------------------------------------------------------------- 2. LINEARITY
    {
        auto analyser = prepared (kSampleRate);
        feedTone (analyser, kCentreHz, 0.1, kFftSize, 128);   // -20 dBFS

        const auto band = bandOf (analyser, kCentreHz);
        const auto reading = analyser.bandsDb[static_cast<std::size_t> (band)];

        check (std::abs (reading - (-20.0f)) < 0.3f,
               format ("a -20 dBFS tone reads %.3f dBFS, -20.000 expected (tol 0.3)", reading));
    }

    // ---------------------------------------------------------------- 3. SCALLOPING
    {
        // 1000 Hz at 48 kHz sits 15.6 Hz off bin 21, which is not the worst case;
        // the worst case is a half-bin offset, so that is what is measured.
        const auto betweenBins = kCentreHz + 0.5 * kBinWidth;

        auto analyser = prepared (kSampleRate);
        feedTone (analyser, betweenBins, 1.0, kFftSize, 256);

        const auto reading = analyser.bandsDb[static_cast<std::size_t> (bandOf (analyser, betweenBins))];

        check (reading <= 0.05f && reading > -1.6f,
               format ("a full-scale tone half a bin off centre reads %.3f dBFS - low, as a Hann window's 1.42 dB scalloping loss allows", reading));
    }

    // ---------------------------------------------------------------- 4. BAND ORDER
    {
        auto low = prepared (kSampleRate);
        feedTone (low, 150.0, 1.0, kFftSize, 256);

        auto high = prepared (kSampleRate);
        feedTone (high, 6000.0, 1.0, kFftSize, 256);

        check (peakBand (high) > peakBand (low),
               format2 ("6 kHz peaks in band %.0f and 150 Hz in band %.0f - the curve is the right way up",
                        static_cast<double> (peakBand (high)), static_cast<double> (peakBand (low))));

        // The EXACT half of the claim, stated where it can hold. 6 kHz sits in a
        // band thousands of hertz wide - 34 bins at this transform - so its energy
        // is unambiguously in its own band, and a curve that put it anywhere else
        // would be mislabelled rather than merely coarse.
        check (bandOf (high, 6000.0) == peakBand (high),
               format ("6 kHz peaks in exactly the band 6 kHz maps to (%.0f), where that band is 34 bins wide",
                       static_cast<double> (bandOf (high, 6000.0))));

        // The COARSE half, stated where it cannot. At 48 kHz the ladder's band
        // around 130 Hz is 37 Hz wide while the transform resolves 46.9 Hz, so the
        // 150 Hz tone's main lobe reaches a bin that belongs to the band below and
        // the reading lands one band low. Declared here rather than tuned away,
        // because no edges can resolve what a 21 ms window cannot separate - see
        // the class comment in the header.
        check (std::abs (peakBand (low) - bandOf (low, 150.0)) <= 1,
               format2 ("150 Hz peaks in band %.0f against its own band %.0f - within one band, because that band is narrower than a bin",
                        static_cast<double> (peakBand (low)), static_cast<double> (bandOf (low, 150.0))));
    }

    // ---------------------------------------------------------------- 5. THE EDGES
    {
        auto analyser = prepared (kSampleRate);
        auto increasing = true;

        for (int b = 0; b < SpectrumAnalyser::bandCount; ++b)
            if (analyser.edgesHz[static_cast<std::size_t> (b) + 1]
                <= analyser.edgesHz[static_cast<std::size_t> (b)])
                increasing = false;

        check (increasing, "every band edge is above the one below it, so no band can read a neighbour's bins");
        check (analyser.edgesHz[0] == 20.0f, "the ladder starts at 20 Hz");
        check (analyser.edgesHz[SpectrumAnalyser::bandCount] == 20000.0f,
               "the top edge at 48 kHz is 20 kHz");

        // Nyquist below 20 kHz: the ladder's top has to move, or the last bands
        // read bins that a 1024-point transform at this rate does not have.
        auto narrow = prepared (8000.0);
        check (narrow.edgesHz[SpectrumAnalyser::bandCount] == 4000.0f,
               "at 8 kHz the top edge is Nyquist (4 kHz), not 20 kHz");

        auto stillIncreasing = true;
        for (int b = 0; b < SpectrumAnalyser::bandCount; ++b)
            if (narrow.edgesHz[static_cast<std::size_t> (b) + 1]
                <= narrow.edgesHz[static_cast<std::size_t> (b)])
                stillIncreasing = false;

        check (stillIncreasing, "the ladder is still strictly increasing after moving its top edge");
    }

    // ---------------------------------------------------------------- 6. THE WINDOW
    {
        auto analyser = prepared (kSampleRate);
        std::vector<float> tone (static_cast<std::size_t> (kFftSize));
        for (int i = 0; i < kFftSize; ++i)
            tone[static_cast<std::size_t> (i)] = static_cast<float> (
                std::sin (kTwoPi * kCentreHz * static_cast<double> (i) / kSampleRate));

        analyser.process (tone.data(), kFftSize - 1);

        auto allFloor = true;
        for (auto db : analyser.bandsDb)
            if (db > SpectrumAnalyser::floorDb)
                allFloor = false;

        check (allFloor, "1023 samples of a full-scale tone produce no reading at all");
        check (analyser.pendingSamples() == kFftSize - 1,
               format ("1023 samples are pended, waiting for the window to fill (%.0f)",
                       static_cast<double> (analyser.pendingSamples())));

        analyser.process (tone.data() + (kFftSize - 1), 1);

        const auto band = bandOf (analyser, kCentreHz);
        check (analyser.bandsDb[static_cast<std::size_t> (band)] > -1.0f,
               format ("the 1024th sample completes the window and the tone appears in band %.0f",
                       static_cast<double> (band)));
        check (analyser.pendingSamples() == SpectrumAnalyser::hopSize,
               format ("half the window is kept for the next transform (%.0f samples)",
                       static_cast<double> (analyser.pendingSamples())));
    }

    // ---------------------------------------------------------------- 7. SILENCE
    {
        auto analyser = prepared (kSampleRate);
        std::vector<float> silence (static_cast<std::size_t> (kFftSize * 2), 0.0f);
        analyser.process (silence.data(), static_cast<int> (silence.size()));

        auto atFloor = true;
        for (auto db : analyser.bandsDb)
            if (db != SpectrumAnalyser::floorDb)
                atFloor = false;

        check (atFloor, "silence reads the floor in every band");

        feedTone (analyser, kCentreHz, 1.0, kFftSize, 256);
        const auto loud = analyser.bandsDb[static_cast<std::size_t> (bandOf (analyser, kCentreHz))];
        analyser.reset();

        auto backToFloor = true;
        for (auto db : analyser.bandsDb)
            if (db != SpectrumAnalyser::floorDb)
                backToFloor = false;

        check (loud > -1.0f && backToFloor,
               format ("a tone (%.3f dBFS) followed by reset() comes back to the floor", (double) loud));
    }
}

//==============================================================================
//  The build WITHOUT the library: everything above is absent, and the stage is
//  still a stage. This is the half that no other harness in the suite compiles.
//==============================================================================
void runAbsentLibraryBuild()
{
    auto analyser = prepared (kSampleRate);

    check (! SpectrumAnalyser::compiledIn,
           "a build with pocketfft off the include path reports compiledIn == false");
    check (! analyser.ready,
           "and prepare() leaves the stage unready rather than half-built");

    // The engine's own driver shape: prepare, feed a block, read. It must be safe
    // and it must report the floor rather than a stale or arbitrary number.
    feedTone (analyser, kCentreHz, 1.0, kFftSize * 4, 256);

    auto atFloor = true;
    for (auto db : analyser.bandsDb)
    {
        if (db != SpectrumAnalyser::floorDb)
            atFloor = false;
    }

    check (atFloor, "feeding a full-scale tone reads the floor in every band, so no curve is drawn");

    // The edges are computed from the rate alone, so they are still published -
    // a caller can label an axis even in a build that draws nothing.
    check (analyser.edgesHz[0] == 20.0f && analyser.edgesHz[SpectrumAnalyser::bandCount] == 20000.0f,
           "the band ladder is still published, from the rate, with no library present");
    check (analyser.pendingSamples() == 0, "and nothing is being pended towards a transform that cannot run");

    // Null and zero-length feeds are what a host hands over on a stopped
    // transport; neither may be a crash or a reading.
    analyser.process (nullptr, 64);
    analyser.process (analyser.bands().data(), 0);
    check (analyser.pendingSamples() == 0, "a null and a zero-length feed are both ignored");
}
} // namespace

int main()
{
    std::printf ("SpectrumAnalyser harness - %s build (J37_HAS_POCKETFFT=%d, %d-point transform)\n\n",
                 SpectrumAnalyser::compiledIn ? "LIBRARY" : "ABSENT-LIBRARY",
                 J37_HAS_POCKETFFT, kFftSize);

    if (SpectrumAnalyser::compiledIn)
        runLibraryBuild();
    else
        runAbsentLibraryBuild();

    std::printf ("\n");

    if (gFailures == 0)
    {
        std::printf ("ALL CHECKS PASSED (0 failures)\n");
        return 0;
    }

    std::printf ("%d CHECK(S) FAILED\n", gFailures);
    return 1;
}
