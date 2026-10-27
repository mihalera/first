// =============================================================================
//  The machine's device couplings: where one control changes what another one
//  MEANS.
//
//  This is a stage taken out of processTapeEngine() (see step F in
//  SPLITTING-BIG-FILES.md), and it is a PURE function of the signals it couples -
//  the two slow sources, the drive actually reaching the tape, TONE, and the two
//  wander curves - so a harness can call it without an engine, a parameter tree or a
//  block. The claims its prose makes are checkable and are checked: a control at zero
//  means zero, every coupling is bounded, and a worn machine wanders even with WOW
//  and FLUTTER closed.
//
//  The conventions of the directory apply, and they are stated in J37Math.h: one
//  concern per header, the header includes what it needs, and it does not pull JUCE
//  in. It names `juce::jlimit`, which its includer has already declared.
//
//  The prose came with the code, out of Source/ProcessorSignalPath.cpp by matching
//  its exact text. What changed is where each figure is written - fields of the
//  struct rather than the processor's own state - and nothing else.
// =============================================================================
#pragma once

namespace j37coupling
{

/** One block's worth of the couplings. */
struct TapeCouplings
{
    // WEAR and MECHANICS deepen the transport's own wander.
    float wearWowCoupling = 0.0f;

    // A hot record head is a noisier head: how much the drive that is actually
    // reaching the tape lifts the noise floor.
    float driveHissCoupling = 0.0f;

    // A dark head gap hides the fine modulation before it hides anything else.
    float headGapModulationMask = 1.0f;

    // A plunger that is slowing loses speed before it loses signal: the two
    // exponents that shape the platter's pitch and its output level.
    float platterPitchExponent = 1.0f;
    float platterGateExponent = 1.0f;

    // A hard-driven machine runs its transport more loosely.
    float loadInducedSpeedError = 0.0f;

    // The transport activity gate: 0 when both Wow and Flutter are closed, 1 from
    // roughly 50 % on either.
    float transportActivityGate = 0.0f;
};

/** Builds the block's couplings from the two slow sources, the drive, TONE and the
    two wander curves. */
inline TapeCouplings makeTapeCouplings (float mechanics, float wear, float driveAmount,
                                        float tone, float wowCurve, float flutterCurve)
{
    TapeCouplings couplings;

    // -------------------------------------------------------------------------
    //  Device couplings: where one control changes what another one MEANS.
    //
    //  A machine is not a list of independent knobs. A worn capstan makes the wow
    //  deeper at the same WOW setting; a harder-driven head makes its own hiss
    //  louder; a dark head gap hides modulation; a spent platter loses speed before
    //  it loses signal. These are the interactions that make a tape model feel like
    //  one object instead of a chain of processors, and every one of them is a
    //  plain product of two already-computed signals - no extra oscillator, no
    //  extra state, nothing to keep in sync.
    //
    //  Each coupling is deliberately ASYMMETRIC and bounded: the couplings can only
    //  add, never invert, so a control at zero always means zero and the machine
    //  cannot be talked into a state its design does not have.
    // -------------------------------------------------------------------------
    const auto mechanismWear = juce::jlimit (0.0f, 1.0f, mechanics + wear);

    // WEAR and MECHANICS deepen the transport's own wander. Both are already folded
    // into the per-sample modulation; this is the part they own on their own, so a
    // worn machine wanders even with WOW and FLUTTER closed - which is the one case
    // where the transport gate is wrong to silence it, and is why this term is added
    // AFTER the gate is taken.
    couplings.wearWowCoupling = mechanismWear * 0.006f;

    // A hot record head is a noisier head: the hiss floor rises with the drive that
    // is actually reaching the tape, not with the DRIVE control alone.
    couplings.driveHissCoupling = juce::jlimit (0.0f, 1.0f, driveAmount * 0.35f);

    // A dark head gap hides the fine modulation before it hides anything else, so
    // the top of the TONE travel rolls the grain and the flutter off together. It
    // reads the TONE control directly rather than the cached toneLpAc coefficient:
    // that coefficient is only recomputed when tone or character actually move, so
    // on the first block it can still be 0 and would dampen the modulation before
    // the knob had been touched. The 0.55 floor means TONE can never silence the
    // transport's own movement, only soften it - which is what a biased head does
    // to pitch shimmer.
    const float toneNorm = juce::jlimit (0.0f, 1.0f, tone);
    couplings.headGapModulationMask = juce::jlimit (0.55f, 1.0f, 0.55f + toneNorm * 0.45f);

    // A plunger that is slowing loses speed before it loses signal. The whole point
    // of SPINDOWN is that the pitch goes first, so the platter speed is mapped
    // through a curve that stays near 1 until the platter is well below speed and
    // then falls away quickly: an exponent above 1 does that, and an exponent below
    // it would do the opposite - it would spend the modulation at the very start of
    // the run-down, where a machine is still moving and nothing should be happening
    // to it yet. The value is applied in the per-sample loop, where the platter
    // speed actually lives.
    couplings.platterPitchExponent = 1.35f;

    // The machine's OUTPUT gate, and a different shape from the modulation term
    // above on purpose. A tape that is still moving is still being read: its level
    // holds until the travel is nearly gone and then collapses, which is what makes
    // a stop read as the machine going away rather than as someone pulling a fader
    // down. The first version of this gate was linear (the platter speed straight),
    // which is 6 dB down halfway through a coast and exactly the sound that was
    // reported as "STOP just makes it quieter". At this exponent the level is 0.77
    // at nine tenths of speed, 0.18 at half speed and -35 dB at a fifth of it, so
    // the audible part of a coast is its last third. The trade the shape makes is on
    // the way back up: a spin-up brings the machine's sound in concentrated in its
    // last third instead of fading it in early, which is the honest reading of a
    // machine engaging - and it is only a trade in LEVEL now, because the repitch
    // stage underneath it glides the pitch back up through the whole ramp rather
    // than letting it snap.
    couplings.platterGateExponent = 2.5f;

    // A hard-driven machine also runs its transport more loosely: pushing a deck
    // that hard is a physical load, and the speed error is the same term the ramp
    // uses, read here as a small extra wander.
    couplings.loadInducedSpeedError = driveAmount * 0.004f;

    // The transport activity gate: 0 when both Wow and Flutter are closed, 1 from
    // roughly 50 % on either. The machine only makes a sound of any kind while its
    // transport moves, so EVERYTHING the transport does is gated by this one factor:
    // the tape-surface grain in the loop below (which used to run unconditionally
    // and read as a mystery noise generator whenever both controls were closed),
    // and - since the "hiss while paused" report - the noise floor as well, whose
    // gain is multiplied by it further down. Nothing in the engine is always-on.
    couplings.transportActivityGate = juce::jlimit (0.0f, 1.0f, (wowCurve + flutterCurve) * 2.0f);

    return couplings;
}

} // namespace j37coupling
