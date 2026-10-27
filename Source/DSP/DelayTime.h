// =============================================================================
//  The delay head's time, free-running or tempo-locked.
//
//  This is a stage taken out of processTapeEngine() (see step F in
//  SPLITTING-BIG-FILES.md), and it is a PURE function of four numbers - the DELAY
//  control in free mode, the sync switch, the RATE note choice and the host tempo -
//  so a harness can call it without an engine, a parameter tree or a block. The note
//  table it reads is the part worth checking: a quarter at 120 BPM has to be exactly
//  500 ms, a dotted quarter 750, a quarter triplet a third of a second, and the
//  choice has to be clamped rather than indexing off the end.
//
//  The conventions of the directory apply, and they are stated in J37Math.h: one
//  concern per header, the header includes what it needs, and it does not pull JUCE
//  in. It names `juce::jlimit` and `juce::jmax`, which its includer has already
//  declared.
//
//  The prose came with the code, out of Source/ProcessorSignalPath.cpp by matching
//  its exact text. The conversion to samples at the engine rate stays at the call
//  site, because it needs the engine rate and the delay buffer's own length - the
//  two facts this stage does not own.
// =============================================================================
#pragma once

namespace j37delay
{

/** The delay head's time in milliseconds: the free control when sync is off, or the
    host tempo scaled by the RATE note choice when it is on. */
inline float effectiveDelayMs (float freeMs, bool syncOn, int delayRateIndex, double hostTempoBpm)
{
    if (! syncOn)
        return freeMs;

    // The note value as a fraction of a whole note. A quarter is 1/4, a
    // dotted quarter is 1/4 * 1.5, a triplet quarter is 1/4 * 2/3.
    //
    //  1/1 = 1.0      1/2 = 0.5      1/4 = 0.25     1/8 = 0.125   1/16 = 0.0625
    //  1/4 T (triplet) = 0.25 * 2/3  1/8 T = 0.125 * 2/3
    //  1/4 D (dotted)  = 0.25 * 1.5
    static constexpr float noteRatios[] { 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f,
                                          0.25f * 2.0f / 3.0f,
                                          0.125f * 2.0f / 3.0f,
                                          0.25f * 1.5f };
    constexpr int numNoteRatios = static_cast<int> (sizeof (noteRatios) / sizeof (noteRatios[0]));

    const auto ratio = noteRatios[juce::jlimit (0, numNoteRatios - 1, delayRateIndex)];

    // A whole note is four beats, so a whole note lasts 4 * 60000/tempo ms.
    const auto wholeNoteMs = 4.0 * 60000.0 / juce::jmax (1.0, hostTempoBpm);
    return static_cast<float> (wholeNoteMs * static_cast<double> (ratio));
}

} // namespace j37delay
