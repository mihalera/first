// =============================================================================
//  The tape machine's own rate and its four shaping curves: what the character
//  controls do to the signal's shape, what the SPEED control and the platter do to
//  the machine's rate, and the two LFO rates the WOW and FLUTTER controls sweep.
//
//  This is the second stage taken out of processTapeEngine() (see step F in
//  SPLITTING-BIG-FILES.md). What makes it a stage rather than a block is that it is a
//  PURE function of seven numbers - the four controls, the SPEED choice, the platter's
//  position and the host's tempo - so a harness can call it without an engine, a
//  parameter tree or a block. The claims its prose makes are checkable and are checked:
//  at speed the platter's scale IS the SPEED control's own number, the wow rate is
//  locked to the host tempo (and the flutter rate is not), and the tempo anchor is
//  clamped at both ends.
//
//  The conventions of the directory apply, and they are stated in J37Math.h: one
//  concern per header, the header includes what it needs, and it does not pull JUCE in.
//  It names `juce::jlimit` and `juce::MathConstants`, which its includer has already
//  declared - inside the plugin that is JuceHeader.h, under the harness it is
//  tests/dsp/shim.h, and in tests/dsp/check_headers_juce.sh it is the real
//  juce_audio_basics. The two other files in this directory that name a `juce::` scalar
//  helper do the same, for the same reason.
//
//  The prose came with the code, out of PluginProcessor.cpp and then out of
//  Source/ProcessorSignalPath.cpp by matching its exact text. What changed is where the
//  results are written - fields of the struct rather than the processor's own locals -
//  and nothing else.
// =============================================================================
#pragma once

#include <cmath>

namespace j37mod
{

/** One block's worth of the machine's shaping curves, its speed scale and its two
    wander rates. */
struct TapeModulation
{
    // The four character controls' shaping curves.
    float driveCurve = 0.0f;
    float biasCurve = 0.0f;
    float wowCurve = 0.0f;
    float flutterCurve = 0.0f;

    // The SPEED control and the platter, multiplied: the rate the machine is running
    // at, as a scale on everything that follows from its speed.
    float speedScale = 1.0f;

    // The two LFO rates.
    float wowFreq = 0.0f;
    float flutterFreq = 0.0f;
};

/** Builds a block's modulation from the four controls, the SPEED choice, the platter's
    position and the host's tempo. */
inline TapeModulation makeTapeModulation (float drive, float bias, float wow, float flutter,
                                          int speedChoice, float platter, double hostTempoBpm)
{
    TapeModulation modulation;

    // -------------------------------------------------------------------------
    //  Analogue transfer curves.
    //
    //  These are deliberately NONLINEAR and must stay that way: they are the model of
    //  the machine, not a control taper. A tape stage bends gently at low levels and
    //  enters saturation hard as it is pushed, and that progressive bending is what
    //  produces the harmonics. Linearising them (as they briefly were) removes the
    //  analogue character entirely.
    //
    //  The knob taper is handled by the skewed NormalisableRange in
    //  createParameterLayout, so these power curves are free to be the sound-shaping
    //  functions they should be.
    // -------------------------------------------------------------------------
    modulation.driveCurve = std::pow (drive, 1.45f);
    modulation.biasCurve = std::pow (bias, 1.30f);
    modulation.wowCurve = std::pow (wow, 1.55f);
    modulation.flutterCurve = std::pow (flutter, 1.45f);

    // The SPEED control's own character mapping, moved by the platter: a machine
    // that is winding down behaves the way a machine running slower does - less
    // band from its heads, slower wander, its bias following - which is what
    // makes a coast read as the transport losing speed rather than as a fade. At
    // speed this is exactly the SPEED control's number, so a running machine is
    // unchanged by it.
    const auto speedChoiceScale = (speedChoice == 0) ? 0.76f
                                   : (speedChoice == 1) ? 1.0f : 1.34f;
    modulation.speedScale = speedChoiceScale * (0.76f + 0.24f * platter);

    // The wow rate is tempo-locked. A machine's speed irregularities read as
    // musical when they land on the track's own timing, so the LFO is anchored to
    // the host tempo - one wander cycle per BAR at WOW's centre - and the WOW
    // control sweeps around that anchor instead of over an absolute rate. At 120
    // BPM the centre is 0.5 Hz; double the tempo and the machine wanders twice as
    // fast, which is what playing along with a faster track means for a deck.
    // Without a host tempo the 120 BPM default keeps the machine where it was.
    //
    // The anchor is clamped at both ends: 0.10 Hz is below anything a deck does and
    // 3.0 Hz is above it, so a tempo that a host reports absurdly - a session with
    // its tempo at 1 BPM, or a stray value at the top of the range - still leaves
    // the machine wandering rather than stopped or buzzing.
    const auto barsPerSecond = static_cast<float> (hostTempoBpm) / 240.0f;
    const auto tempoWowAnchor = juce::jlimit (0.10f, 3.0f, barsPerSecond);
    modulation.wowFreq = tempoWowAnchor
                       * (0.30f + modulation.wowCurve * 1.40f) * modulation.speedScale;
    // The flutter rate does NOT follow the tempo: it is the capstan and the pinch
    // roller, which are mechanical and run at their own rate whatever the track is
    // doing. Only its depth and its scaling come from the control and the platter.
    modulation.flutterFreq = (1.9f + modulation.flutterCurve * 5.4f)
                           * (1.0f + modulation.speedScale * 0.22f);

    return modulation;
}

} // namespace j37mod
