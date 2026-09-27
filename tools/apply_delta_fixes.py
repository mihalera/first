#!/usr/bin/env python3
"""One-shot patcher for the DELTA listen fixes.

Every replacement asserts that its anchor occurs EXACTLY once in the target file,
so a mistyped or ambiguous anchor aborts the run instead of editing the wrong
place. Run from the project root:  python3 tools/apply_delta_fixes.py
"""

import sys
from pathlib import Path

HEADER = Path("Source/PluginProcessor.h")
PROCESSOR = Path("Source/PluginProcessor.cpp")

HEADER_EDITS = [
    (
        """    /** True when the last processed block was fully bypassed. */
    bool isBypassed() const noexcept { return bypassActive.load (std::memory_order_relaxed); }
""",
        """    /** True when the last processed block was fully bypassed. */
    bool isBypassed() const noexcept { return bypassActive.load (std::memory_order_relaxed); }

    /**
        DELTA listen's switch, as the 0..1 target its ramp is fed.

        Three places need the same answer - the per-block ramp feed, the bypass
        early-return guard and the rate-change seed - so it is named rather than
        written out three times. Read through the getter, never the raw pointer,
        like every other switch, so a host-side change cannot be seen half-applied.
    */
    float deltaListenTarget() const noexcept
    {
        return (deltaParam != nullptr && deltaParam->load() >= 0.5f) ? 1.0f : 0.0f;
    }
""",
    ),
    (
        """    SampleSmoother widthSmoothed { sampleClock };
    SampleSmoother bypassSmoothed { sampleClock };
""",
        """    SampleSmoother widthSmoothed { sampleClock };
    SampleSmoother bypassSmoothed { sampleClock };
    // DELTA listen is the one switch whose output step is as large as the programme
    // itself: switching it on replaces the finished signal with (machine - the
    // machine's own dry), so a bare boolean would move the output by that much
    // inside a single sample and click. It therefore crossfades on the same 10 ms
    // window the bypass switch uses, and - like every other switch here - it is
    // seeded from the parameter in prepareToPlay, so a session that restores DELTA
    // on does not spend its first block fading into it.
    SampleSmoother deltaSmoothed { sampleClock };
""",
    ),
]

PROCESSOR_EDITS = [
    # 1. The parameter comment now describes what the code actually does.
    (
        """    // DELTA listen: when on, the output becomes wet minus dry - only what the
    // machine itself adds (harmonics, glue, transport wander) is heard. Both
    // legs of the subtraction live on the SAME timeline (the dry signal is the
    // host buffer being processed in place, the wet signal is finished further
    // up in this very loop), so the difference is phase-perfect at every
    // oversampling factor with no compensation delay of its own. The blend
    // below ignores the bypass crossfade while the mode is on: delta of a
    // bypassed machine is exactly zero, which is its own sanity check.
""",
        """    // DELTA listen: when on, the output becomes wet minus dry - only what the
    // machine itself adds (harmonics, glue, transport wander) is heard. Both
    // legs of the subtraction live on the SAME timeline (the dry leg is carried
    // alongside the wet one and finished further up in this very loop), so the
    // difference is phase-perfect at every oversampling factor with no
    // compensation delay of its own. The dry leg is the machine's OWN dry - the
    // INPUT trim and the input-stage compressor, carried through the same static
    // gain and the same width/trim/limiter stages as the wet leg - so MIX 0
    // cancels to digital silence and the trims do not show up in the difference
    // as level. The difference is scaled by the bypass ramp, so a bypassed
    // machine's delta is exactly zero, which is its own sanity check. The switch
    // is ramped rather than stepped, because the distance between the two signals
    // is the programme itself.
""",
    ),
    # 2. Dirty-tracking: DELTA is a host parameter, so it belongs in both lists.
    (
        """                                     "character" })
        parameters.addParameterListener (parameterID, this);
""",
        """                                     "character", "delta" })
        parameters.addParameterListener (parameterID, this);
""",
    ),
    (
        """                                     "character" })
        parameters.removeParameterListener (parameterID, this);
""",
        """                                     "character", "delta" })
        parameters.removeParameterListener (parameterID, this);
""",
    ),
    # 3. Seed the ramp from the restored parameter, exactly like the bypass switch.
    (
        """    bypassSmoothed.setCurrentAndTargetValue (bypassParam != nullptr && bypassParam->load() >= 0.5f ? 0.0f : 1.0f);
""",
        """    bypassSmoothed.setCurrentAndTargetValue (bypassParam != nullptr && bypassParam->load() >= 0.5f ? 0.0f : 1.0f);
    // DELTA listen starts where the parameter says it should, for the same reason:
    // a session saved with DELTA on must not spend its first 10 ms crossfading in.
    deltaSmoothed.reset (sampleRateToUse, 0.01);
    deltaSmoothed.setCurrentAndTargetValue (deltaListenTarget());
""",
    ),
    # 4. Feed the ramp ABOVE the bypass early return, and make that return delta-aware.
    (
        """    if (bypassRequested && ! bypassSmoothed.isSmoothing() && bypassSmoothed.getCurrentValue() <= 0.0f)
    {
        bypassActive.store (true, std::memory_order_relaxed);
""",
        """    // DELTA listen is read and its ramp fed HERE, above the bypass early return
    // below, because that return skips the whole engine: a switch whose ramp lived
    // downstream of it would never advance on the blocks that take it.
    deltaSmoothed.setTargetValue (deltaListenTarget());

    // The early return is only safe while DELTA is out of the picture. It hands the
    // host buffer back untouched, so with DELTA engaged it would replace the
    // difference signal with the full dry signal - and it would do it at the instant
    // the crossfade finished, a step the size of the whole programme. So the engine
    // keeps running, and keeps advancing the DELTA ramp, until the switch is off AND
    // its ramp has actually arrived at the dry position. Releasing BYPASS then ramps
    // back out through the engine instead of cutting to the dry buffer, and the
    // DELTA crossfade covers the hand-over in both directions.
    const auto deltaFullyOff = deltaListenTarget() <= 0.0f
                            && ! deltaSmoothed.isSmoothing()
                            && deltaSmoothed.getCurrentValue() <= 0.0f;

    if (bypassRequested && ! bypassSmoothed.isSmoothing()
        && bypassSmoothed.getCurrentValue() <= 0.0f && deltaFullyOff)
    {
        bypassActive.store (true, std::memory_order_relaxed);
""",
    ),
    # 5. The per-block boolean is gone; the ramp is read per sample instead.
    (
        """    const bool autoGainEnabled = autoGainParam == nullptr || autoGainParam->load() >= 0.5f;
    const bool deltaListen = deltaParam != nullptr && deltaParam->load() >= 0.5f;
""",
        """    const bool autoGainEnabled = autoGainParam == nullptr || autoGainParam->load() >= 0.5f;
""",
    ),
    (
        """        const float currentWidth = widthSmoothed.getNextValue();
        const float bypassMix = bypassSmoothed.getNextValue();
""",
        """        const float currentWidth = widthSmoothed.getNextValue();
        const float bypassMix = bypassSmoothed.getNextValue();
        // DELTA listen's own ramp position for this sample. Read here beside the
        // bypass ramp because the output crossfade below needs both, every sample.
        const float deltaMix = deltaSmoothed.getNextValue();
""",
    ),
    # 6. Carry the machine's own dry leg out of the tape loop.
    (
        """        std::array<float, 2> undertoneOutput {};
""",
        """        std::array<float, 2> undertoneOutput {};

        // The machine's own dry leg, per channel: exactly the signal the tape path
        // carries at MIX 0 - the INPUT trim and the input-stage compressor, and
        // nothing else. DELTA listen subtracts THIS instead of the raw host buffer,
        // which is what makes the two things the control promises actually true:
        // MIX 0 has to come out as digital silence, and the difference has to hold
        // the machine's character rather than its gain staging. Subtracting the raw
        // buffer put the whole INPUT trim, the input compressor and the machine's
        // static output calibration into the difference, so an otherwise untouched
        // MIX 0 read as a quiet inverted copy of the programme instead of silence.
        std::array<float, 2> machineDry {};
""",
    ),
    (
        """            referenceBlockPower += inputTrimmed * inputTrimmed;
            const float x = inputTrimmed * inputCompressionGain;
""",
        """            referenceBlockPower += inputTrimmed * inputTrimmed;
            const float x = inputTrimmed * inputCompressionGain;
            machineDry[static_cast<std::size_t> (channel)] = x;
""",
    ),
    # 7. Build the difference alongside the finished output, cancelling the static gain.
    (
        """        std::array<float, 2> outputSignal {};
        for (int channel = 0; channel < activeChannels; ++channel)
            outputSignal[static_cast<std::size_t> (channel)] =
                tapeOutput[static_cast<std::size_t> (channel)] * stageGain * compensationGain;
""",
        """        // Two signals leave the machine's gain stage and they differ only in which
        // leg carries the character: outputSignal is the finished wet path, and
        // deltaSignal is that same path with the machine's own dry removed. The dry
        // leg is scaled by the SAME stageGain and compensationGain, which is what
        // cancels the static output calibration and the auto-gain compensator out of
        // the difference instead of leaving them sitting in it.
        std::array<float, 2> outputSignal {};
        std::array<float, 2> deltaSignal {};
        const auto machineGain = stageGain * compensationGain;
        for (int channel = 0; channel < activeChannels; ++channel)
        {
            const auto index = static_cast<std::size_t> (channel);
            outputSignal[index] = tapeOutput[index] * machineGain;
            deltaSignal[index] = (tapeOutput[index] - machineDry[index]) * machineGain;
        }
""",
    ),
    # 8. Undertones and the width stage belong to the difference too.
    (
        """        for (int channel = 0; channel < activeChannels; ++channel)
            outputSignal[static_cast<std::size_t> (channel)] +=
                undertoneOutput[static_cast<std::size_t> (channel)] * wetGain * finalOutputGain;

        if (activeChannels == 2)
        {
            const float mid = 0.5f * (outputSignal[0] + outputSignal[1]);
            const float side = 0.5f * (outputSignal[0] - outputSignal[1]) * currentWidth;
            outputSignal[0] = mid + side;
            outputSignal[1] = mid - side;
        }
""",
        """        for (int channel = 0; channel < activeChannels; ++channel)
        {
            const auto index = static_cast<std::size_t> (channel);
            const auto undertones = undertoneOutput[index] * wetGain * finalOutputGain;
            outputSignal[index] += undertones;
            // The undertones are something the machine ADDS, so all of them belongs
            // in the difference: the dry leg has no counterpart to cancel them with.
            deltaSignal[index] += undertones;
        }

        // The width stage runs on both legs, so the difference holds the width
        // change the machine made rather than losing it against the dry side.
        const auto applyWidth = [] (std::array<float, 2>& signal, float width)
        {
            const float mid = 0.5f * (signal[0] + signal[1]);
            const float side = 0.5f * (signal[0] - signal[1]) * width;
            signal[0] = mid + side;
            signal[1] = mid - side;
        };

        if (activeChannels == 2)
        {
            applyWidth (outputSignal, currentWidth);
            applyWidth (deltaSignal, currentWidth);
        }
""",
    ),
    (
        """        for (int channel = 0; channel < activeChannels; ++channel)
            outputSignal[static_cast<std::size_t> (channel)] *= outputGain;
""",
        """        for (int channel = 0; channel < activeChannels; ++channel)
        {
            const auto index = static_cast<std::size_t> (channel);
            outputSignal[index] *= outputGain;
            deltaSignal[index] *= outputGain;
        }
""",
    ),
    (
        """        for (int channel = 0; channel < activeChannels; ++channel)
            outputSignal[static_cast<std::size_t> (channel)] *= limiterGain;
""",
        """        // The limiter's gain rides on both legs. It is one output-stage gain with
        // one detector, and the difference is that same stage's contribution, so
        // sharing the gain keeps the two legs in step and means the difference can
        // never be pushed past what the wet path itself already passed.
        for (int channel = 0; channel < activeChannels; ++channel)
        {
            const auto index = static_cast<std::size_t> (channel);
            outputSignal[index] *= limiterGain;
            deltaSignal[index] *= limiterGain;
        }
""",
    ),
    # 9. The output crossfade itself.
    (
        """            auto& destination = channelData[static_cast<std::size_t> (channel)][sample];
            const auto difference = outputSignal[static_cast<std::size_t> (channel)] - destination;
            // DELTA listen replaces the bypass crossfade with the difference
            // itself; the meters keep reading the finished output path, so in
            // this mode they show the level of what the machine adds.
            const auto blended = deltaListen ? difference
                                             : destination + difference * bypassMix;
""",
        """            const auto index = static_cast<std::size_t> (channel);
            auto& destination = channelData[index][sample];
            const auto difference = outputSignal[index] - destination;

            // The ordinary output: the finished path crossfaded against the dry
            // buffer the host handed over. This is what BYPASS rides.
            const auto normalOut = destination + difference * bypassMix;

            // DELTA listen: what the machine adds, and nothing else. Its dry leg is
            // the machine's OWN dry (see machineDry above), so MIX 0 cancels to
            // digital silence and the INPUT/OUTPUT trims and the auto-gain
            // compensator do not appear in the difference as level.
            //
            // It is scaled by the bypass ramp for the same reason the ordinary path
            // is: a bypassed machine adds nothing, so its difference is zero, and
            // scaling by the ramp makes that true continuously rather than only at
            // the two ends of the crossfade.
            const auto deltaOut = deltaSignal[index] * bypassMix;

            // Crossfaded on deltaSmoothed's 10 ms ramp instead of switched, because
            // the step between these two signals is the programme itself - a bare
            // boolean here is a guaranteed click on every toggle. At deltaMix 0 this
            // is exactly normalOut, so the non-DELTA path is bit-for-bit unchanged.
            const auto blended = normalOut + (deltaOut - normalOut) * deltaMix;
""",
    ),
    # 10. Zero-sample blocks, with every other ramp.
    (
        """    widthSmoothed.skip (numSamples);
    bypassSmoothed.skip (numSamples);
""",
        """    widthSmoothed.skip (numSamples);
    bypassSmoothed.skip (numSamples);
    deltaSmoothed.skip (numSamples);
""",
    ),
]


def apply(path, edits):
    text = path.read_text()
    for number, (old, new) in enumerate(edits, start=1):
        found = text.count(old)
        if found != 1:
            sys.exit(f"{path}: edit {number} matched {found} times, expected 1")
        text = text.replace(old, new, 1)
    path.write_text(text)
    print(f"{path}: applied {len(edits)} edits")


apply(HEADER, HEADER_EDITS)
apply(PROCESSOR, PROCESSOR_EDITS)
print("ok")
