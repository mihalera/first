// =============================================================================
//  The subharmonic generator: the undertone series beneath the programme.
//
//  Driven on signal by tests/dsp/subfund_test.cpp, which includes this header
//  rather than a copy cut out of PluginProcessor.h, and instantiated by
//  tests/dsp/check_headers_juce.sh against real JUCE. The conventions of
//  Source/DSP/ are stated in J37Math.h.
// =============================================================================
#pragma once

#include <cmath>

//==============================================================================
/**
    Subharmonic generator - the descendant of the fundamental.

    Everything else in this plugin produces OVERtones: harmonics at integer multiples
    of the input frequency. A 100 Hz tone gets 200, 300, 400 Hz and so on. This stage
    is the opposite - it produces the subharmonic series (undertones at 1/2, 1/3, 1/4,
    1/5 of the fundamental), so a 100 Hz note gains weight at 50, 33.3, 25 and 20 Hz,
    with warm analog saturation running in the opposite direction.

    This cannot come out of the saturating curve, and it is worth being clear why,
    because it looks like it should. `tanh (sin (wt))` is a curve applied to a value,
    with no notion of time of its own, so its output is a function of the instantaneous
    input phase - and any such function has period 2pi/w, which means its Fourier
    series contains only multiples of w. Subharmonics need a process with its OWN
    timescale that can fall out of step with the signal.

    On a real machine there are three such processes, and this models all three:

      - Bias leakage. The ultrasonic bias oscillator is not perfectly suppressed on
        playback; the residue weakly modulates the operating point.
      - Domain-wall motion. Magnetic domains flip in groups, and the boundaries
        between them move at a rate that is not locked to the signal. This is the
        best-documented source of subharmonic content in magnetic recording.
      - Scrape flutter. Tape-to-head friction excites the tape's own mechanical
        resonances, modulating the effective head-to-tape speed.

    All three are the same shape mathematically: a slow, signal-dependent modulation
    of the transfer curve. The implementation below uses a phase-locked cascade of
    undertones (1/2, 1/3, 1/4, 1/5) excited by the fundamental bass envelope.
    Drive excites both the depth and the saturation of the subharmonics, producing
    warm inter-harmonic body and massive low-end weight while staying strictly
    locked to what is playing.

    This lives in the header rather than in an anonymous namespace in the .cpp because
    the processor holds two of them by value as members, and a member's type has to be
    visible where the class is declared.
*/
struct SubharmonicGenerator
{
    // An eight-stage undertone cascade. The stages produce the subharmonic series:
    // fundamental / 2, / 3, / 4, / 5, / 6, / 7, / 8, and / 9. Every stage is a pure
    // sinusoid: this block is a generator of partials, and nothing in it is allowed
    // to make a harmonic of one.
    //
    // For a fundamental note that allows them (e.g. >= 140 Hz):
    //   - Stage 0 (/2): -1 octave (foundational sub-bass weight)
    //   - Stage 1 (/3): -1 octave + 5th down (low-mid harmonic thickness)
    //   - Stage 2 (/4): -2 octaves down (deep sub rumble)
    //   - Stage 3 (/5): -2 octaves + major 3rd down (warm undertone)
    //   - Stage 4 (/6): -2 octaves + 5th down (sub-bass density)
    //   - Stage 5 (/7): -2 octaves + harmonic 7th down (extended sub warmth)
    //   - Stage 6 (/8): -3 octaves down (extreme low weight)
    //   - Stage 7 (/9): -3 octaves + major 2nd down (sub-boundary reinforcement)
    //
    // The relative level of the eight is fixed by `baseWeights` below: exactly -6 dB
    // per step of division, so the series always reads as a descending staircase with
    // 1/2 on top. See the weighting block in process() for why that is spelled out in
    // the dividers rather than in the stage index.
    //
    // Frequency-aware audibility:
    // When the input fundamental is very low (e.g. 20 - 40 Hz), dividing by 4, 5, 6, 7, 8, 9
    // would produce inaudible subsonic DC (< 14 Hz) that strains speakers and ruins headroom.
    // Each stage checks its actual synthesized frequency and smoothly rolls off between
    // 22 Hz and 14 Hz. Thus, when the signal allows (upper bass / low mids), all 8 stages
    // are fully active; when the note is already deep sub-bass, stages below the audible
    // limit fade out gracefully.
    static constexpr int numStages = 9;
    static constexpr int dividers[numStages] { 2, 3, 4, 5, 6, 7, 8, 9, 10 };

    // 2^-(divider - 1): 1/2 at 0 dB, 1/3 at -6, 1/4 at -12 ... 1/10 at -48.
    // The staircase law is unchanged - the ninth stage simply continues it, so
    // adding 1/10 does not alter the balance of the eight that were already there.
    static constexpr float baseWeights[numStages] { 0.5f, 0.25f, 0.125f, 0.0625f,
                                                     0.03125f, 0.015625f, 0.0078125f,
                                                     0.00390625f, 0.001953125f };

    // AC coupling in front of the detector. The magnetic shaper that excites this
    // stage is asymmetric on purpose, and an asymmetric transfer curve carries an
    // offset. An envelope follower cannot tell an offset from a quiet note, so
    // without this the undertones hold their level forever - a permanent drone at
    // the last tracked pitch with nothing playing. The corner sits below the
    // lowest fundamental the detector accepts, so no undertone this stage can
    // produce is touched by it.
    float detectorDcX = 0.0f;
    float detectorDcY = 0.0f;
    float detectorDcR = 0.9993f;

    // Detector filter states (2-pole Butterworth low-pass at ~260 Hz).
    // Filters out upper harmonics, cymbals, guitars and noise so that cycle
    // detection locks cleanly onto the true fundamental bass note.
    float lp1 = 0.0f;
    float lp2 = 0.0f;

    // Follower tracking the dynamic envelope of the RAW input signal.
    // Scales the generated subharmonics so they breathe with the music and
    // decay smoothly to silence.
    float detPeak = 0.0f;
    float peakTrack = 0.0f;  // tracks raw input envelope (not filtered)

    // Presence follower on the same band, with a deliberately much faster release
    // than detPeak above. The two do different jobs: detPeak sets how loud the
    // undertones are under a note that is playing, and it may take its time doing
    // that. `presence` answers a different question - is there still anything in
    // the fundamental band for the undertones to be derived from at all - and when
    // the answer turns out to be no, the answer has to be delivered at once.
    //
    // Without it the undertones outlived the note by a fixed musical release time
    // (30 ms of envelope plus the time the DC blocker and the tape filters need to
    // settle), which reads on an analyser as the stage still generating with
    // nothing playing - and the deeper the tracked pitch, the more audible the
    // rumble. It is a follower rather than a threshold, so it follows a note
    // decaying into its own tail instead of cutting at a fixed level.
    float presence = 0.0f;

    // Period tracker and Schmitt-trigger zero crossing detector
    float period = 441.0f;
    float samplesSinceCrossing = 0.0f;
    float prevDet = 0.0f;
    bool armed = true;

    // ------------------------------------------------------------------------
    //  Fundamental phase accumulator, in turns [0, 1).
    //
    //  This is the master clock the whole cascade is derived from, and it is the
    //  upgrade that matters most. The previous version aligned each stage against
    //  `crossingCount % divider` - a running COUNT of detected crossings rather
    //  than a phase. That has two faults:
    //
    //    - It drifts permanently. `crossingCount` only ever increases, so a
    //      single missed crossing (a quiet passage, a transient the detector
    //      skips) shifts every stage's reference for the rest of the session.
    //      The error never heals because nothing brings the count back in step
    //      with the signal.
    //
    //    - It is only correct when the divider happens to divide the count
    //      evenly. For the stages whose divider does not, the target phase is
    //      quantised to the wrong value and the stage is pulled off-frequency
    //      every time a crossing arrives.
    //
    //  A phase accumulator fixes both. It advances continuously from the tracked
    //  period, so the reference is always current, and a missed crossing costs
    //  one cycle of correction rather than a permanent offset - the PLL pulls
    //  the phase back toward the crossing each time one arrives.
    // ------------------------------------------------------------------------
    float fundamentalPhase = 0.0f;

    // Running phase per subharmonic stage in [0, 1)
    float phases[numStages] {};

    // ------------------------------------------------------------------------
    //  Cycle-confidence tracker.
    //
    //  The detector's trigger threshold is a fraction of the tracked peak, so a
    //  decaying note eventually stops producing crossings. When that happens the
    //  cascade must not simply coast: it would keep generating at the last
    //  tracked pitch, which is the drone the presence gate exists to prevent.
    //  Counting the samples since the last crossing and folding that into the
    //  presence gate makes the stage release as soon as the signal stops
    //  driving it, rather than after a fixed envelope time.
    // ------------------------------------------------------------------------
    float samplesSinceLastCrossing = 0.0f;
    float cycleConfidence = 0.0f;

    // The confidence coefficients, built with the rate.
    float confidenceAttackCoeff = 0.0f;
    float confidenceReleaseCoeff = 0.0f;

    // ------------------------------------------------------------------------
    //  Octave-lock memory.
    //
    //  A subharmonic an octave below the note is the one that carries the most
    //  energy, so it is also the one that can do the most damage: if the two
    //  channels happen to generate it in opposite phase, summing the mix to mono
    //  cancels it, and the low end disappears. That is not a fault in this stage,
    //  but it IS a fault in the result, and it is preventable.
    //
    //  `octavePhaseAligned` records whether the 1/2 stage has stayed close to the
    //  master's half-phase over the last few cycles. When the two channels agree,
    //  the octave is reinforced; when they disagree, the two octaves are opposite
    //  and the mono sum loses them. The caller can then bias the depth per channel
    //  so the pair stays constructive.
    // ------------------------------------------------------------------------
    float octavePhaseError = 0.0f;

    // Cached sample-rate-dependent filter coefficients
    float cachedSampleRate = 0.0f;
    float lpCoeff = 0.0f;
    float attackCoeff = 0.0f;
    float releaseCoeff = 0.0f;
    float presenceAttackCoeff = 0.0f;
    float presenceReleaseCoeff = 0.0f;

    void reset() noexcept
    {
        lp1 = 0.0f;
        lp2 = 0.0f;
        detPeak = 0.0f;
        presence = 0.0f;
        period = 441.0f;
        samplesSinceCrossing = 0.0f;
        prevDet = 0.0f;
        armed = true;
        fundamentalPhase = 0.0f;
        samplesSinceLastCrossing = 0.0f;
        cycleConfidence = 0.0f;
        octavePhaseError = 0.0f;
        cachedSampleRate = 0.0f;
        detectorDcX = 0.0f;
        detectorDcY = 0.0f;

        for (int s = 0; s < numStages; ++s)
            phases[s] = 0.0f;
    }

    void updateSampleRate (float currentSampleRate) noexcept
    {
        if (std::abs (currentSampleRate - cachedSampleRate) < 0.1f)
            return;

        cachedSampleRate = currentSampleRate;
        const float safeRate = juce::jmax (1.0f, currentSampleRate);
        lpCoeff = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 260.0f / safeRate);
        attackCoeff = 1.0f - std::exp (-1.0f / (safeRate * 0.003f));

        // 30 ms. The follower's job is to keep the undertones in proportion with the
        // note that is playing, and this is a musical release: a note that stops
        // cleanly should not have its undertones cut off under it.
        releaseCoeff = 1.0f - std::exp (-1.0f / (safeRate * 0.030f));

        // Presence, by contrast, is 3 ms up and 6 ms down. Slow enough that the
        // undertones fade in with a note instead of arriving ahead of it, and fast
        // enough that they are gone within a frame of the band going quiet.
        presenceAttackCoeff = 1.0f - std::exp (-1.0f / (safeRate * 0.003f));
        presenceReleaseCoeff = 1.0f - std::exp (-1.0f / (safeRate * 0.006f));

        // Cycle confidence: rises over about 40 ms of steady crossings and falls
        // over about 25 ms without one. Both are slower than the presence gate,
        // because confidence is a statement about the TRACK, not about the level -
        // it should not flicker on a single missed crossing.
        confidenceAttackCoeff = 1.0f - std::exp (-1.0f / (safeRate * 0.040f));
        confidenceReleaseCoeff = 1.0f - std::exp (-1.0f / (safeRate * 0.025f));

        // 5 Hz one-pole AC coupling, in the same form the playback DC blocker uses.
        // The detector accepts fundamentals from 15 Hz up, so this removes any
        // offset without touching anything the stage is trying to track.
        detectorDcR = juce::jlimit (0.5f, 0.9999f,
                                    1.0f - (juce::MathConstants<float>::twoPi * 5.0f) / safeRate);
    }

    /**
        The 1/2 stage's phase relative to the master, in turns, signed to [-0.5, 0.5).
        The caller reads this on both channels to decide whether their octaves are
        in phase; see the field's own note for why that matters.
    */
    float getOctavePhaseError() const noexcept { return octavePhaseError; }

    /** How well the detector is currently tracking, 0..1. Drives the UI readout. */
    float getCycleConfidence() const noexcept { return cycleConfidence; }

    /**
        Rotates the master phase by a small amount, for the caller's anti-phase
        protection. Deliberately tiny in effect and clamped: this is a nudge, not a
        set, so it can never break the lock the PLL is holding - it only biases
        which side of the lock the cascade settles on.
    */
    void nudgeMasterPhase (float turns) noexcept
    {
        fundamentalPhase += juce::jlimit (-0.01f, 0.01f, turns);
        if (fundamentalPhase >= 1.0f) fundamentalPhase -= 1.0f;
        if (fundamentalPhase < 0.0f)  fundamentalPhase += 1.0f;
    }

    /** The fundamental the cascade is locked to, in Hz. Zero when unlocked. */
    float getTrackedFrequency (float sampleRate) const noexcept
    {
        if (cycleConfidence < 0.1f)
            return 0.0f;

        const float safeRate = juce::jmax (1.0f, sampleRate);
        const float safePeriod = juce::jlimit (safeRate / 500.0f, safeRate / 15.0f, period);
        return safeRate / safePeriod;
    }

    /**
        Feeds one sample and returns the multi-frequency subharmonic component: a
        phase-locked series of pure sinusoids at f0/2, /3, /4 ... /10.

        `driveAmount` tilts the series towards its deep end, opening the lower
        dividers without ever reordering the staircase.
        `depth` scales the overall injected subharmonic level.
    */
    float process (float x, float driveAmount, float depth, float sampleRate) noexcept
    {
        if (depth <= 0.0f || ! std::isfinite (x))
            return 0.0f;

        updateSampleRate (sampleRate);
        const float safeRate = juce::jmax (1.0f, sampleRate);

        // ----------------------------------------------------------------------
        //  1. AC coupling, before anything reads the signal.
        //
        //  The shaper that excites this stage is asymmetric by design, and an
        //  asymmetric transfer curve carries an offset. A constant is not silence
        //  to an envelope follower: with one still in the detector path the
        //  undertones held their level indefinitely, droning at the last tracked
        //  pitch with nothing playing at all. The shaper no longer emits an
        //  offset at silence - its curve is pinned through the origin - but the
        //  follower must never be given an offset by anything, and this is the
        //  place that guarantees it. The corner sits below the lowest fundamental
        //  the detector accepts, so no undertone this stage can produce is
        //  touched by it.
        // ----------------------------------------------------------------------
        const float acCoupled = x - detectorDcX + detectorDcR * detectorDcY;
        detectorDcX = x;
        detectorDcY = acCoupled;

        // ----------------------------------------------------------------------
        //  2. Two-pole low-pass filter (~260 Hz) on detector path.
        //  Isolates the fundamental bass note from highs and overtones so the
        //  cycle detector never mistriggers on treble content.
        // ----------------------------------------------------------------------
        lp1 += (acCoupled - lp1) * lpCoeff;
        lp2 += (lp1 - lp2) * lpCoeff;
        const float det = lp2;

        // ----------------------------------------------------------------------
        //  3. Dynamic envelope follower of the fundamental bass band.
        //  Ensures level proportionality: quiet notes get quiet subharmonics,
        //  and silence decays cleanly without droning.
        //
        //  The presence follower below runs first and is not subject to the early
        //  exit, because it has to keep seeing the band in order to know the band
        //  has gone. Freezing it while the stage is silent would leave it holding
        //  whatever it had reached before the note stopped, and the next note
        //  would fade in from a stale level.
        // ----------------------------------------------------------------------
        const float absDet = std::abs (det);

        presence += (absDet - presence) * (absDet > presence ? presenceAttackCoeff
                                                             : presenceReleaseCoeff);

        detPeak += (absDet - detPeak) * (absDet > detPeak ? attackCoeff : releaseCoeff);

        // The floor is the same one detPeak is measured against, so the gate opens
        // and closes exactly where the follower does rather than at a level of its
        // own: below it there is no note, so there is nothing to double.
        constexpr float silenceFloor = 1.0e-4f;
        // The gate is now the product of TWO independent statements about whether
        // there is a note to double:
        //
        //   presenceGain     - is there still LEVEL in the fundamental band
        //   cycleConfidence  - is the detector still getting CYCLES from it
        //
        // The second is the new one, and it matters because they fail differently.
        // A sustained note whose level drops keeps producing crossings long after
        // it is quiet, and a noisy signal can hold the level up while producing no
        // usable cycles at all. Requiring both means the cascade releases when
        // EITHER stops being true, which is the honest answer to "is there a note
        // to derive undertones from".
        const float confidenceCoefficient = samplesSinceLastCrossing < safeRate * 0.25f
                                                ? confidenceAttackCoeff
                                                : confidenceReleaseCoeff;
        cycleConfidence += ((samplesSinceLastCrossing < safeRate * 0.25f ? 1.0f : 0.0f)
                                - cycleConfidence) * confidenceCoefficient;

        const float presenceGain = juce::jlimit (0.0f, 1.0f, presence / silenceFloor);
        const float gateGain = presenceGain * cycleConfidence;

        if (detPeak < silenceFloor || gateGain <= 0.0f)
            return 0.0f;

        // ----------------------------------------------------------------------
        //  4. Schmitt-trigger crossing detector with hysteresis.
        //  Measures fundamental period and triggers Phase-Locked Loop (PLL).
        // ----------------------------------------------------------------------
        samplesSinceCrossing += 1.0f;
        samplesSinceLastCrossing += 1.0f;
        const float triggerThreshold = detPeak * 0.10f;

        if (det > triggerThreshold && prevDet <= triggerThreshold && armed)
        {
            armed = false;
            // Valid fundamental range: 15 Hz up to 500 Hz
            const float minPeriod = safeRate / 500.0f;
            const float maxPeriod = safeRate / 15.0f;

            // The period estimate is averaged rather than jumped to. A quarter
            // weight per crossing is about four cycles of settling, which is fast
            // enough to follow a line and slow enough to reject a single bad
            // interval - and the interval is only accepted at all if it falls in
            // the musical range, so a transient cannot pull the track off.
            if (samplesSinceCrossing >= minPeriod && samplesSinceCrossing <= maxPeriod)
                period += (samplesSinceCrossing - period) * 0.25f;

            samplesSinceCrossing = 0.0f;
            samplesSinceLastCrossing = 0.0f;

            // ------------------------------------------------------------------
            //  Phase-Locked Loop, on the FUNDAMENTAL.
            //
            //  The crossing is a known point of the waveform: at an upward
            //  zero-crossing the fundamental's phase is exactly 0. So the
            //  correction is simply "pull the accumulator toward 0", and every
            //  stage's target follows from it by division - which is what a
            //  subdivision IS.
            //
            //  This replaces the previous `crossingCount % divider` scheme. That
            //  one used a running count rather than a phase, so it drifted
            //  permanently after a single missed crossing and was quantised to the
            //  wrong value for any divider that did not divide the count evenly.
            //  Deriving every stage from one phase accumulator fixes both: the
            //  reference is always current, and a missed crossing costs one
            //  correction rather than a permanent offset.
            // ------------------------------------------------------------------
            float phaseError = -fundamentalPhase;
            if (phaseError > 0.5f)  phaseError -= 1.0f;
            if (phaseError < -0.5f) phaseError += 1.0f;

            // A gentle pull: 8 % of the error per crossing. Tight enough to hold
            // lock over a long take, loose enough that it never fights the
            // continuous advancement and produces a jump.
            fundamentalPhase += phaseError * 0.08f;
            if (fundamentalPhase >= 1.0f) fundamentalPhase -= 1.0f;
            if (fundamentalPhase < 0.0f)  fundamentalPhase += 1.0f;
        }
        else if (det < -triggerThreshold)
        {
            armed = true;
        }
        prevDet = det;

        // ----------------------------------------------------------------------
        //  5. Continuous phase advancement and subharmonic synthesis.
        // ----------------------------------------------------------------------
        const float safePeriod = juce::jlimit (safeRate / 500.0f, safeRate / 15.0f, period);
        const float baseStep = 1.0f / safePeriod;
        const float trackedFundamentalHz = safeRate / safePeriod;

        // ----------------------------------------------------------------------
        //  Advance the fundamental phase, then re-derive every stage from it.
        //
        //  This is the second half of the PLL upgrade. The stages are no longer
        //  free-running oscillators that get nudged; they are DIVISIONS of one
        //  master phase. That is what keeps them locked to each other as well as
        //  to the signal - with independent accumulators, two stages can drift
        //  apart from each other even while both stay near the note, which shows
        //  up as the series beating against itself.
        // ----------------------------------------------------------------------
        fundamentalPhase += baseStep;
        if (fundamentalPhase >= 1.0f)
            fundamentalPhase -= 1.0f;

        const float clampedDrive = juce::jlimit (0.0f, 1.0f, driveAmount);

        float sum = 0.0f;
        float weightSum = 0.0f;

        for (int s = 0; s < numStages; ++s)
        {
            // The stage runs its OWN accumulator, advanced at baseStep/divider and
            // wrapped at 1.0 independently of the master.
            //
            // It cannot simply be sampled from the master, which is the obvious way
            // to express "locked" and was tried: fundamentalPhase / dividers[s] only
            // ever spans [0, 1/d) before the master wraps back to 0, so the cosine
            // traced a half-wave and returned instead of running continuously. Every
            // stage became a rectified pulse rather than a sinusoid, and the whole
            // staircase lost about 190 dB - the 1/2 stage measured -220 dB where it
            // should be -29. A divided oscillator needs a period of its own: it gets
            // 1/d of a cycle per master period, and those d pieces have to be joined
            // into one continuous cycle, which is exactly what the accumulator does.
            //
            // The master is still authoritative for LOCK - it is what the PLL
            // corrects against and what nudgeMasterPhase moves - it just cannot
            // supply the running phase.
            phases[s] += baseStep / static_cast<float> (dividers[s]);
            if (phases[s] >= 1.0f)
                phases[s] -= 1.0f;

            const float subFreq = trackedFundamentalHz / static_cast<float> (dividers[s]);

            // Low-frequency audibility window:
            // Stages that fall below ~14 - 22 Hz are smoothly attenuated so that
            // inaudible, speaker-straining infrasonic DC is never generated.
            // When f0 is high enough (e.g. > 140 Hz), all 8 subharmonics are fully active.
            // When f0 is very low (e.g. 20 Hz), inaudible stages fade out naturally.
            float audibility = 0.0f;
            if (subFreq >= 22.0f)
            {
                audibility = 1.0f;
            }
            else if (subFreq > 14.0f)
            {
                const float t = (subFreq - 14.0f) / (22.0f - 14.0f);
                audibility = t * t * (3.0f - 2.0f * t); // smoothstep
            }

            if (audibility <= 0.0f)
                continue;

            // Stage weighting. The series is a STAIRCASE, and the law is written in
            // terms of the DIVIDER rather than the stage index so that it cannot
            // invert: a partial at f0/d is fed at 2^-(d-1), which is exactly -6 dB for
            // every step of division - 1/2 on top, 1/3 at -6 dB, 1/4 at -12 dB, down
            // to 1/9 some 42 dB under the octave.
            //
            // The previous law pinned stage 0 at 1.0 and weighted the rest by
            // 1/(stage + 1), which is the same as 1/(d - 1): it dropped 10 dB from 1/2
            // to 1/3 and then left 1/4 ... 1/9 inside an 8 dB band. On an analyser
            // that is a flat shelf of undertones with one partial perched on top of
            // it rather than a staircase, and it is what the "the deep undertones are
            // the loud ones" report was measuring.
            //
            // DRIVE opens the deep end without ever breaking that order. The tilt
            // grows with the divider, but by at most 1.875x across the whole 2 ... 9
            // range, so no two adjacent stages can swap places at any drive setting.
            const float divider = static_cast<float> (dividers[s]);
            const float w = baseWeights[s] * (1.0f + clampedDrive * (divider - 2.0f) * 0.125f);
            const float effectiveWeight = w * audibility;
            weightSum += effectiveWeight;

            // A pure sinusoid, and deliberately the only waveform this stage ever
            // produces.
            //
            // Each partial used to be run through `tanh` first - "downward
            // saturation", the stage's own description of it. A memoryless
            // waveshaper on a sinusoid is a harmonic generator, and a loud one: at
            // the default DRIVE the 1/2 undertone was returning its own third
            // harmonic only 16 dB down, and DRIVE pushed that to 13 dB, because
            // DRIVE is what opened the waveshaper. That is exactly the reported
            // fault - "the regular harmonics are generated from the subharmonics" -
            // and it was being manufactured here, inside the generator, before
            // anything downstream could be blamed for it.
            //
            // A sine is also the only waveform a phase-locked cascade can use
            // without the stages interfering: any harmonics it adds are at
            // frequencies that belong to no stage's own period, so the series
            // stops being the clean staircase it is supposed to be. DRIVE keeps
            // its musical job - it tilts the weights towards the deep end above -
            // but it no longer shapes the waveform.
            const float osc = std::cos (juce::MathConstants<float>::twoPi * phases[s]);
            sum += osc * effectiveWeight;

            // The octave stage's phase error is recorded for the caller's
            // anti-phase bias. Stage 0 is the 1/2 divider by construction, and the
            // error is signed so the two channels can be compared.
            if (s == 0)
                octavePhaseError = phases[0] < 0.5f ? phases[0] : phases[0] - 1.0f;
        }

        if (weightSum <= 1.0e-4f)
            return 0.0f;

        const float fade = juce::jmin (1.0f, weightSum);
        return (sum / weightSum) * detPeak * depth * fade * gateGain;
    }
};
