// =============================================================================
//  The hot-path probe: four counters the per-sample loop increments, and the
//  readout that turns them into a snapshot.
//
//  Moved out of PluginProcessor.cpp, prose and all, when the signal path left for
//  Source/ProcessorSignalPath.cpp. It is a header because the probe is one mechanism
//  split across two translation units: the loop that increments the counters is in
//  the signal path, and the snapshot is called from the processor's telemetry
//  readout. A file-local copy in each would be two different probes.
//
//  Everything here is behind `J37_ENABLE_HOTPATH_PROBE`, which no build defines -
//  the probe is instrumentation for a benchmark or an audit, never a shipping path,
//  and the guard is what keeps it out of the audio code when it is off. What the move
//  cost is two spellings and one sentence:
//
//    * The four counters are `inline` VARIABLES rather than file-local objects. That
//      is the whole reason they could not stay in the .cpp: one translation unit
//      writes them and another reads them, and `inline` is what makes those the same
//      four objects instead of one set per unit. The shapers in
//      Source/DSP/HotPathMath.h are `inline` FUNCTIONS by contrast - pure, so copies
//      of them would be harmless, while copies of these would silently read zero.
//    * The two blocks' own `#if` lines are gone in favour of ONE guard around the
//      whole body. In the .cpp they carried identical guards of their own, far apart
//      from each other in a 5,000 line file; here they are the two halves of one
//      mechanism in one header, and a second guard inside the first is a line a
//      reader has to check for no reason. The readout's comment about not having a
//      condition of its own is still true and still here.
//    * The comment above the counters said the readout sits "at the bottom of this
//      file". It says "header" now, which is where it is.
//
//  The indentation is the .cpp's, unchanged: the counters sat at four spaces under
//  the .cpp's guard and do the same under this one, and the readout was already at
//  file scope.
//
//  ONE CODE CHANGE came out of the move, and it is the sort this project keeps
//  looking for: with the probe's own flag defined (`-DJ37_ENABLE_HOTPATH_PROBE=1`,
//  which no build defines and no CI job sets), `hotPathThisBlockActive =
//  hotPathThisSampleActive;` is a copy assignment between two atomics - a deleted
//  function. The probe was declared so that turning the documented flag on would
//  compile, and it still did not. Both loads and the store spell out what they mean
//  now, and the flag's own branch is compiled on every change to these two files:
//  it is one -fsyntax-only pass with the macro defined, and it is what would have
//  caught this in the first place.
// =============================================================================
#pragma once

#include <atomic>

// One guard for the whole probe. In PluginProcessor.cpp the counters and the readout
// carried identical guards of their own, written apart from each other in a 5,000
// line file; here they are the two halves of one mechanism in one header, and a
// build that has the flag has both.
#if ! defined (J37_DSP_HARNESS) && defined (J37_ENABLE_HOTPATH_PROBE)

    // -------------------------------------------------------------------
    //  Hot-path probe counters (J37_ENABLE_HOTPATH_PROBE).
    //
    //  Four counters, incremented inside the per-sample loop and read out by
    //  hotPathProbeSnapshot() at the bottom of this header. They live at FILE
    //  scope rather than as members of the processor because that readout is a
    //  free function: it has no instance to reach a member through, so a member
    //  would have to be static - and a static member still has to be DEFINED
    //  somewhere, which is a second place for the probe to drift out of step
    //  with itself. Everything here is behind the same two guards as the code
    //  that touches it, so a build without the flag has no trace of any of it.
    //
    //  They are atomics rather than plain ints for the reason the probe exists:
    //  the counters are written on the AUDIO thread and read by whatever audit or
    //  benchmark code pulls the snapshot, and a plain counter read across those
    //  two threads is a data race with a torn-read failure mode.
    //
    //  They are DECLARED here because they were referenced for two revisions
    //  without ever existing: the flag has never been set in CI, so the one build
    //  that would have caught it is the one nobody ran, and turning on the
    //  documented probe was a guaranteed compile error. Nothing defines the flag
    //  today, which is why the breakage was invisible rather than fatal.
    // -------------------------------------------------------------------
    inline std::atomic<int> hotPathSampleCount { 0 };
    inline std::atomic<int> hotPathActiveSampleCount { 0 };
    inline std::atomic<bool> hotPathThisSampleActive { false };
    inline std::atomic<bool> hotPathThisBlockActive { false };

// ===========================================================================
//  HOT-PATH PROBE READOUT.
//  Deliberately outside the audio path's hot inner loop: this is only ever called
//  from debug / audit code paths. It exists so the counters above can be inspected
//  without polluting the audio function with queries.
// ===========================================================================
namespace
{
    struct HotPathProbeStats
    {
        int totalSamples = 0;
        int activeSamples = 0;
        float lastBlockActive = 0.0f;
    };

    HotPathProbeStats hotPathProbeSnapshot() noexcept
    {
        // No condition of its own: this whole namespace is already inside the
        // probe's guard, and a second, subtly different one here (it used to test
        // J37_ENABLE_HOTPATH_PROBE alone, without the harness exclusion) is how a
        // build that defines the flag for the DSP harness would reach for
        // counters that guard had just compiled away.
        HotPathProbeStats s;
        s.totalSamples = hotPathSampleCount;
        s.activeSamples = hotPathActiveSampleCount;
        s.lastBlockActive = hotPathThisBlockActive ? 1.0f : 0.0f;
        return s;
    }
}

#endif  // J37_ENABLE_HOTPATH_PROBE
