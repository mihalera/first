// GENERATED FILE - do not edit.
// Cut verbatim out of Source/ by tests/dsp/extract.py.

#define SUBFUND_MIX_INVERTS 0

// ---- GlueCompressor (PluginProcessor.h) ----------------------------------------
struct GlueCompressor
{
    float envelope = 0.0f;

    // A second, much slower follower used only to tell transients apart from sustained
    // programme. Keeping it separate from `envelope` means the adaptation never feeds
    // back into the gain computation itself.
    float slowEnvelope = 0.0f;

    void reset() noexcept
    {
        envelope = 0.0f;
        slowEnvelope = 0.0f;
    }

    /** Envelope level of the most recent block, as 0..1 linear activity. */
    float getEnvelopeActivity() const noexcept
    {
        return juce::jlimit (0.0f, 1.0f, std::sqrt (juce::jmax (0.0f, envelope)));
    }

    /** 0 = sustained programme, 1 = sharp transient. Used by the UI to show activity. */
    float getTransientAmount() const noexcept
    {
        const auto fast = std::sqrt (juce::jmax (0.0f, envelope));
        const auto slow = std::sqrt (juce::jmax (0.0f, slowEnvelope));
        if (slow <= 1.0e-6f)
            return 0.0f;

        return juce::jlimit (0.0f, 1.0f, (fast - slow) / slow * 1.6f);
    }

    /**
        Pushes signal power through the detector; call once per sample.

        attackBaseSeconds and releaseBaseSeconds are the nominal constants. They are
        then adapted by the three axes described above, so the caller never has to
        schedule attack or release by hand - that is the semi-automatic behaviour.
    */
    float processDetection (float detectorPower, float sampleRate,
                            float attackBaseSeconds, float releaseBaseSeconds,
                            float loadFactor) noexcept
    {
        const float safeRate = juce::jmax (1.0f, sampleRate);

        // -- Axis 3: how full the detector already is --------------------------
        const float envelopeLevel = getEnvelopeActivity();

        // -- Axis 2: transient or sustained? ----------------------------------
        // A time constant roughly a hundred times slower than the detector tracks the
        // running programme level. Comparing the two is enough to tell a drum hit
        // (fast spikes above the slow average) from a sustained pad or vocal line.
        const float slowTimeConstant = juce::jmax (0.05f, releaseBaseSeconds * 3.5f);
        const float slowCoefficient = std::exp (-1.0f / (safeRate * slowTimeConstant));
        slowEnvelope = slowCoefficient * slowEnvelope
                     + (1.0f - slowCoefficient) * detectorPower;

        const float transientAmount = getTransientAmount();

        // -- Axis 1: how hard the stage is being driven -----------------------
        // Load lengthens both constants, so a stage being leaned on turns slow and
        // dense while an idle one stays quick and transparent.
        const float loadStretch = 1.0f + loadFactor * 1.8f;

        // Attack: quick on transients so nothing is missed, relaxed on sustained
        // material so the stage does not clamp the body of the sound. The transient
        // term dominates the level term, because catching a peak matters more.
        const float transientSpeedUp = 1.0f - transientAmount * 0.72f;
        const float attackSeconds = attackBaseSeconds * loadStretch
                                  * juce::jlimit (0.25f, 1.6f, transientSpeedUp)
                                  * (1.0f + envelopeLevel * 0.9f);

        // Release: long on sustained programme and when the detector is full, short on
        // isolated transients so the stage reopens before the next event. This is what
        // gives the classic auto-release feel - dense passages stay together, sparse
        // ones breathe.
        const float releaseStretch = 1.0f
                                   + (1.0f - transientAmount) * 1.15f
                                   + envelopeLevel * 2.4f;
        const float releaseSeconds = releaseBaseSeconds * (1.0f + loadFactor * 2.2f)
                                   * releaseStretch;

        const float timeConstant = detectorPower > envelope ? attackSeconds : releaseSeconds;
        const float coefficient = std::exp (-1.0f / (safeRate * timeConstant));
        envelope = coefficient * envelope + (1.0f - coefficient) * detectorPower;

        return juce::Decibels::gainToDecibels (std::sqrt (juce::jmax (0.0f, envelope)), -100.0f);
    }
};

// ---- SubharmonicGenerator (PluginProcessor.h) ----------------------------------------
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
    static constexpr int numStages = 8;
    static constexpr int dividers[numStages] { 2, 3, 4, 5, 6, 7, 8, 9 };

    // 2^-(divider - 1): 1/2 at 0 dB, 1/3 at -6, 1/4 at -12 ... 1/9 at -42.
    static constexpr float baseWeights[numStages] { 0.5f, 0.25f, 0.125f, 0.0625f,
                                                     0.03125f, 0.015625f, 0.0078125f,
                                                     0.00390625f };

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
    int crossingCount = 0;

    // Running phase per subharmonic stage in [0, 1)
    float phases[numStages] {};

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
        crossingCount = 0;
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

        // 5 Hz one-pole AC coupling, in the same form the playback DC blocker uses.
        // The detector accepts fundamentals from 15 Hz up, so this removes any
        // offset without touching anything the stage is trying to track.
        detectorDcR = juce::jlimit (0.5f, 0.9999f,
                                    1.0f - (juce::MathConstants<float>::twoPi * 5.0f) / safeRate);
    }

    /**
        Feeds one sample and returns the multi-frequency subharmonic component: a
        phase-locked series of pure sinusoids at f0/2, /3, /4 ... /9.

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
        const float presenceGain = juce::jlimit (0.0f, 1.0f, presence / silenceFloor);

        if (detPeak < silenceFloor || presenceGain <= 0.0f)
            return 0.0f;

        // ----------------------------------------------------------------------
        //  4. Schmitt-trigger crossing detector with hysteresis.
        //  Measures fundamental period and triggers Phase-Locked Loop (PLL).
        // ----------------------------------------------------------------------
        samplesSinceCrossing += 1.0f;
        const float triggerThreshold = detPeak * 0.10f;

        if (det > triggerThreshold && prevDet <= triggerThreshold && armed)
        {
            armed = false;
            // Valid fundamental range: 15 Hz up to 500 Hz
            const float minPeriod = safeRate / 500.0f;
            const float maxPeriod = safeRate / 15.0f;

            if (samplesSinceCrossing >= minPeriod && samplesSinceCrossing <= maxPeriod)
                period += (samplesSinceCrossing - period) * 0.25f;

            samplesSinceCrossing = 0.0f;
            ++crossingCount;

            // Phase-Locked Loop (PLL): gently align phase to the crossing boundary
            // to ensure zero phase drift over extended playback.
            for (int s = 0; s < numStages; ++s)
            {
                const int d = dividers[s];
                const float targetPhase = static_cast<float> (crossingCount % d) / static_cast<float> (d);
                float phaseError = targetPhase - phases[s];
                if (phaseError > 0.5f)  phaseError -= 1.0f;
                if (phaseError < -0.5f) phaseError += 1.0f;
                phases[s] += phaseError * 0.10f;
                if (phases[s] >= 1.0f) phases[s] -= 1.0f;
                if (phases[s] < 0.0f)  phases[s] += 1.0f;
            }
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

        const float clampedDrive = juce::jlimit (0.0f, 1.0f, driveAmount);

        float sum = 0.0f;
        float weightSum = 0.0f;

        for (int s = 0; s < numStages; ++s)
        {
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
        }

        if (weightSum <= 1.0e-4f)
            return 0.0f;

        const float fade = juce::jmin (1.0f, weightSum);
        return (sum / weightSum) * detPeak * depth * fade * presenceGain;
    }
};

// ---- magneticHysteresis (PluginProcessor.cpp) --------------------------------------
inline float magneticHysteresis (float x, float drive, float asymmetry, float memory)
    {
        const float biased = x + asymmetry;

        // ------------------------------------------------------------------
        //  One saturating branch, normalised so it is unity-slope at the origin.
        //
        //  The previous version summed three tanh terms whose weights added to 1.0. Each
        //  term saturated independently, so the curves stacked and the transfer compressed
        //  a full-scale input to 0.67 even with DRIVE at zero - the plugin saturated at
        //  every setting, which is what made it sound overdriven no matter what.
        //
        //  `tanh (s*x) / s` is the right shape: its slope at zero is exactly 1, so quiet
        //  signals pass through untouched, and its asymptote is 1/s, so the amount of
        //  compression at the top is set purely by s - which is what DRIVE controls. With
        //  DRIVE at zero, s is 1 and the curve is a gentle, single-tanh tape bend rather
        //  than three stacked ones.
        // ------------------------------------------------------------------
        const float slope = 1.0f + drive * 2.6f;
        const float hard = j37Tanh (biased * slope) / slope;

        // The delayed image gives tape its "sticky" transient behaviour. It shares the
        // same unity-slope normalisation so it contributes character without costing
        // level, and it is scaled by the drive amount so it cannot bend at zero drive.
        const float lagged = memory * 0.62f;
        const float delayedSlope = 0.55f + drive * 1.0f;
        const float delayed = j37Tanh ((biased * delayedSlope) + lagged) / delayedSlope;

        // A blend, not a sum: the weights add to one so the two branches average rather
        // than stacking their compression.
        constexpr float hardWeight = 0.70f;
        constexpr float delayedWeight = 0.30f;

        const float blended = hard * hardWeight + delayed * delayedWeight;

        // Remove the bias offset asymmetrically so the effect adds even harmonics instead
        // of merely shifting the signal. The squared term makes the asymmetry
        // level-dependent, mirroring how real bias interacts with signal amplitude.
        const float asymmetryTerm = asymmetry * 0.45f * hard * hard;

        // The bias is an OFFSET on this transfer curve, and an offset on a curve is
        // also a DC pedestal coming out of it: at digital silence this function
        // returned a constant of roughly -30 dBFS rather than zero. That pedestal is
        // the last always-on source in the engine - the hiss is gated by the
        // transport, but this was not - and it is a rumble, not a click: it arrives
        // the instant the input goes quiet, sits there, and is then taken away
        // again by the playback DC blocker some 20 ms later. On a pause that reads
        // as the machine still making a sound with nothing playing.
        //
        // Subtracting the curve's own value at zero input removes the pedestal and
        // nothing else. The even-harmonic asymmetry survives intact because only its
        // AC part is kept - the difference between the asymmetry term now and the
        // same term evaluated at zero - and the lagged branch is evaluated at the
        // same memory it was given, so the subtraction is exact rather than an
        // approximation that drifts. Zero in, zero out, at every drive and bias, on
        // every sample.
        const float hardAtZero = j37Tanh (asymmetry * slope) / slope;
        const float delayedAtZero = j37Tanh ((asymmetry * delayedSlope) + lagged) / delayedSlope;
        const float blendedAtZero = hardAtZero * hardWeight + delayedAtZero * delayedWeight;
        const float asymmetryAtZero = asymmetry * 0.45f * hardAtZero * hardAtZero;

        return (blended - blendedAtZero) - (asymmetryTerm - asymmetryAtZero);
    }


// ---- softClip (PluginProcessor.cpp) --------------------------------------
inline float softClip (float x) noexcept
    {
        constexpr float ceiling = 0.985f;

        // Below this the curve is 1:1, so quiet and normal-level material is transparent.
        constexpr float knee = 0.70f;

        const auto magnitude = std::abs (x);
        if (magnitude <= knee)
            return x;

        const auto sign = x < 0.0f ? -1.0f : 1.0f;
        const auto excess = magnitude - knee;

        // The value is continuous at the knee (it equals `knee` there) and the slope only
        // changes gradually, so there is no audible corner where saturation begins. The
        // remaining headroom above the knee is what the exponential curve gets to spend.
        const auto compressed = knee + (1.0f - std::exp (-excess)) * (ceiling - knee);
        return sign * juce::jmin (ceiling, compressed);
    }


// ---- softKneeReductionDb (PluginProcessor.cpp) --------------------------------------
inline float softKneeReductionDb (float envelopeDb, float thresholdDb, float kneeDb, float ratio) noexcept
    {
        const float halfKnee = kneeDb * 0.5f;
        const float overshootDb = envelopeDb - thresholdDb;

        if (overshootDb <= -halfKnee)
            return 0.0f;

        if (overshootDb < halfKnee)
        {
            const auto kneeProgress = overshootDb + halfKnee;
            return -(1.0f - 1.0f / ratio) * kneeProgress * kneeProgress / (2.0f * kneeDb);
        }

        return -(1.0f - 1.0f / ratio) * overshootDb;
    }


// ---- onePoleCoefficient (PluginProcessor.cpp) --------------------------------------
inline float onePoleCoefficient (float milliseconds, float sampleRate)
    {
        const float seconds = juce::jmax (0.01f, milliseconds) * 0.001f;
        return 1.0f - std::exp (-1.0f / (seconds * juce::jmax (1.0f, sampleRate)));
    }


// ---- onePoleCoefficientHz (PluginProcessor.cpp) --------------------------------------
inline float onePoleCoefficientHz (float cornerHz, float sampleRate)
    {
        const float omega = juce::MathConstants<float>::twoPi
                          * juce::jmax (1.0f, cornerHz)
                          / juce::jmax (1.0f, sampleRate);
        return juce::jlimit (0.0f, 1.0f, 1.0f - std::exp (-omega));
    }

