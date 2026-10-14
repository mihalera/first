// =============================================================================
//  The signal path: everything that runs between the host's block and the machine's
//  output.
//
//  Moved out of PluginProcessor.cpp whole - 3,870 lines of it, in order - because
//  that file is the processor's LIFECYCLE (what it is constructed with, what a
//  session saves, what the host asks it) and this is what it does to the audio. The
//  two are read for different reasons and by different people, and the one that
//  dominates the file by length was the one nobody could find anything in.
//
//  What is here, in the order the engine runs it:
//
//    processBlock()              the host entry point: pick the oversampling engine,
//                                hand the block to processTapeEngine()
//    setTransportState()         the public transport command, and the host sync it
//    flushPendingTransportHostSync()  flushes that the editor asks for
//    setSpindownHeld()           the platter brake, held from the panel
//    processBypassedBlock()      what the machine does in BYPASS and DELTA
//    processPostMachineStages()  the EQ pair, the IR and the neural stage after it
//    scaleOffsetsForMode()       the twelve scale degrees autotune snaps to
//    processAutoTuneSample()     the optional pitch correction, one frame at a time
//    publishBlockTelemetry()     what the panel's meters read, published per block
//    processTapeEngine()         the machine: 3,366 lines of the tape model
//
//  Nothing was retyped. The spans came out of PluginProcessor.cpp by matching their
//  exact bytes and went in unchanged, so every comment that explains why a stage is
//  shaped the way it is came along with it.
//
//  Two headers came out of the same move, because the helpers this file calls are
//  also called by code that stayed behind: Source/DSP/HotPathMath.h holds the five
//  inline shapers (the preparation code builds its one-pole coefficients with the
//  same two functions), and Source/HotPathProbe.h holds the probe counters the hot
//  loop increments and the processor's telemetry readout inspects. Both say in their
//  own banners why they had to stop being file-local.
//
//  processTapeEngine() is still one function, and that is deliberate rather than left
//  over: splitting it into named stages is the change that would most improve this
//  project's readability, and it is also the one no harness covers today - see
//  SPLITTING-BIG-FILES.md, section F. Moving it to a file of its own is the part that
//  needs no new measurement.
// =============================================================================
#include "PluginProcessor.h"

#include "DSP/HotPathMath.h"
#include "HotPathProbe.h"

void FirstAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // The oversampling switch is a host parameter; a change requires rebuilding
    // nothing (three fixed engines exist side by side) - only the routing picks
    // which one runs, which is a cheap branch taken once per block.
    if (oversamplingParam != nullptr)
    {
        const auto requested = static_cast<int> (oversamplingParam->load());
        if (requested != static_cast<int> (currentOversampling))
            setOversamplingFactor (static_cast<OversamplingFactor> (requested),
                                   juce::jmax (1, lastBlockSize));
    }

    switch (currentOversampling)
    {
        case OversamplingFactor::x2:
        case OversamplingFactor::x4:
        case OversamplingFactor::x8:
        {
            if (auto* oversampler = oversamplers.getUnchecked (static_cast<int> (currentOversampling)))
            {
                // The block must be non-const: processSamplesDown writes the result
                // back into it, in place over the host's audio.
                juce::dsp::AudioBlock<float> block (buffer);
                processTapeEngine (oversampler->processSamplesUp (block), midiMessages);
                oversampler->processSamplesDown (block);
                // Autotune is part of the engine's final sample path, so it is
                // already processed at the same rate as the tape stage before
                // processSamplesDown(). Do not run a second post-downsample path.
                return;
            }
            break;
        }
        case OversamplingFactor::off:
        default:
            break;
    }

    processTapeEngine (buffer, midiMessages);
}

//==============================================================================
void FirstAudioProcessor::setTransportState (int state)
{
    // This public command originates outside processBlock, so host notification is
    // safe here. Engine-originated transitions are published through an atomic and
    // flushed by the editor timer instead of notifying the host from the audio thread.
    if (auto* parameter = parameters.getParameter ("transport"))
    {
        const auto target = static_cast<float> (juce::jlimit (0, 2, state));
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (target));
    }
}

void FirstAudioProcessor::flushPendingTransportHostSync()
{
    if (! transportHostSyncRequested.exchange (false, std::memory_order_acq_rel))
        return;

    const auto state = static_cast<float> (juce::jlimit (
        0, 2, transportHostSyncState.load (std::memory_order_acquire)));
    if (auto* parameter = parameters.getParameter ("transport"))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (state));
}


void FirstAudioProcessor::setSpindownHeld (bool shouldHold)
{
    // Two things have to happen, and both are cheap:
    //
    //   1. The atomic the engine reads every block. That is what actually runs the
    //      platter down, and it is the only path that touches the audio thread -
    //      a relaxed store, no lock, no allocation.
    //   2. The `spindown` parameter, so a host that records automation sees the
    //      gesture and a saved session can hold it. It is written through the
    //      parameter rather than through the attachment in the editor so a button
    //      press and an automation lane are the same event.
    //
    // The engine treats a held spindown as a running transport, so a hold started
    // from STOP brings the platter up and then cuts it: the gesture always has
    // something to act on.
    spindownHeld.store (shouldHold, std::memory_order_relaxed);

    if (auto* parameter = parameters.getParameter ("spindown"))
        parameter->setValueNotifyingHost (shouldHold ? 1.0f : 0.0f);
}

bool FirstAudioProcessor::processBypassedBlock (juce::dsp::AudioBlock<float>& block,
                                                  const BlockContext& context)
{
    const auto bypassRequested = bypassParam != nullptr && bypassParam->load() >= 0.5f;
    const auto deltaRequested = deltaParam != nullptr && deltaParam->load() >= 0.5f;
    const bool deltaEngaged = deltaRequested || deltaListenSmoothed.isSmoothing()
                           || deltaListenSmoothed.getCurrentValue() > 0.0f;

    if (! bypassRequested || bypassSmoothed.isSmoothing()
        || bypassSmoothed.getCurrentValue() > 0.0f || deltaEngaged)
        return false;

    bypassActive.store (true, std::memory_order_relaxed);
    float peak = 0.0f;
    double squares = 0.0;
    for (int channel = 0; channel < context.channels; ++channel)
        for (int sample = 0; sample < context.samples; ++sample)
        {
            const auto value = block.getSample (channel, sample);
            peak = juce::jmax (peak, std::abs (value));
            squares += static_cast<double> (value) * value;
        }

    const auto sampleCount = static_cast<double> (context.samples)
                           * static_cast<double> (juce::jmax (1, context.channels));
    const auto rms = sampleCount > 0.0 ? static_cast<float> (std::sqrt (squares / sampleCount)) : 0.0f;
    const auto peakDb = juce::Decibels::gainToDecibels (peak, -70.0f);
    const auto rmsDb = juce::Decibels::gainToDecibels (rms, -70.0f);
    const auto combined = peakDb * 0.25f + rmsDb * 0.75f;

    inputPeakLevel.store (peak, std::memory_order_relaxed);
    outputPeakLevel.store (peak, std::memory_order_relaxed);
    inputRmsLevel.store (rms, std::memory_order_relaxed);
    outputRmsLevel.store (rms, std::memory_order_relaxed);
    inputPeakDb.store (peakDb, std::memory_order_relaxed);
    outputPeakDb.store (peakDb, std::memory_order_relaxed);
    inputRmsDb.store (rmsDb, std::memory_order_relaxed);
    outputRmsDb.store (rmsDb, std::memory_order_relaxed);
    inputLufs.store (rmsDb, std::memory_order_relaxed);
    outputLufs.store (rmsDb, std::memory_order_relaxed);
    inputVuDb.store (rmsDb, std::memory_order_relaxed);
    outputVuDb.store (rmsDb, std::memory_order_relaxed);
    inputCombinedDb.store (combined, std::memory_order_relaxed);
    outputCombinedDb.store (combined, std::memory_order_relaxed);
    inputClipping.store (peak > 1.0f, std::memory_order_relaxed);
    outputClipping.store (peak > 1.0f, std::memory_order_relaxed);
    inputGainReductionDb.store (0.0f, std::memory_order_relaxed);
    outputGainReductionDb.store (0.0f, std::memory_order_relaxed);
    inputCompressorActivity.store (0.0f, std::memory_order_relaxed);
    outputCompressorActivity.store (0.0f, std::memory_order_relaxed);
    compressorActivity.store (0.0f, std::memory_order_relaxed);
    return true;
}

void FirstAudioProcessor::processPostMachineStages (juce::dsp::AudioBlock<float>& block,
                                                    const BlockContext& context)
{
    const auto channels = context.channels;
    const auto samples = context.samples;
    const auto reverbMix = reverbMixSmoothed.getCurrentValue();
    const auto vinylMix = vinylSmoothed.getCurrentValue();
    const auto irMix = irMixSmoothed.getCurrentValue();
    if (channels <= 0 || samples <= 0) return;

    bool irActive = false;
    if (irMix > 1.0e-5f && irStage.hasIr
        && channels <= postFxScratch.getNumChannels()
        && samples <= postFxScratch.getNumSamples())
    {
        for (int c = 0; c < channels; ++c)
            postFxScratch.copyFrom (c, 0, block.getChannelPointer (c), samples);
        irStage.process (postFxScratch, channels, true);
        irActive = true;
    }

    if (reverbMix <= 1.0e-5f && vinylMix <= 1.0e-5f && ! irActive) return;
    const auto reverbSize = reverbSizeSmoothed.getCurrentValue();
    for (int i = 0; i < samples; ++i)
        for (int c = 0; c < channels; ++c)
        {
            auto* data = block.getChannelPointer (c);
            auto value = data[i];
            if (irActive) value += (postFxScratch.getSample (c, i) - value) * irMix;
            if (reverbMix > 1.0e-5f) value = reverb.process (value, c, reverbSize, reverbMix);
            if (vinylMix > 1.0e-5f)
            {
                auto& stage = c == 0 ? vinylL : vinylR;
                const auto noiseLevel = noiseLvlSmoothed.getCurrentValue() * noiseTrimSmoothed.getCurrentValue();
                value = stage.process (value, c, vinylCrackleSmoothed.getCurrentValue() * vinylMix * noiseLevel,
                                       vinylRumbleSmoothed.getCurrentValue() * vinylMix * noiseLevel, vinylMix,
                                       vinylRumbleCoefficient, vinylWarmthCoefficient, vinylNoiseState);
            }
            data[i] = value;
        }
}

const std::array<int, 12>& FirstAudioProcessor::scaleOffsetsForMode (int modeIndex) noexcept
{
    static constexpr std::array<std::array<int, 12>, 6> scales {{
        {{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }},
        {{ 0, 2, 4, 5, 7, 9, 11, -1, -1, -1, -1, -1 }},
        {{ 0, 2, 3, 5, 7, 8, 10, -1, -1, -1, -1, -1 }},
        {{ 0, 2, 3, 5, 7, 9, 10, -1, -1, -1, -1, -1 }},
        {{ 0, 2, 4, 7, 9, -1, -1, -1, -1, -1, -1, -1 }},
        {{ 0, 3, 5, 7, 10, -1, -1, -1, -1, -1, -1, -1 }}
    }};
    return scales[static_cast<std::size_t> (juce::jlimit (0, 5, modeIndex))];
}

/**
    The optional pitch correction, one frame at a time.

    Where the reading comes from, and what it is trusted for, is the whole of what
    changed here. The estimate is YIN's (see PitchTracker: a period detector, not
    a crossing counter), and the stage only corrects while that detector is
    actually CONFIDENT. Below the confidence gate the correction glides back to
    unity and the shifter is told to read at unity with a phase-locked splice, so
    the note passes through untouched and unclicked; the alternative - correcting
    towards a number the detector made up out of a cymbal or a breath - is the
    failure the panel used to call "does nothing", because what it did do was
    audible only as wrongness.

    The shifter is handed the detected PERIOD on every frame rather than a fixed
    grain. That is what makes the crossfade a crossfade: its two read heads land
    a whole period apart on a periodic signal, so the fading halves are the same
    waveform and the splice is inaudible. A smooth pad, a chord and a noisy
    fret-release all have readings the detector withholds, and those are exactly
    the frames where the fallback grain is used.

    TUNE AMOUNT is the gate's partner rather than its rival: it sets how much of
    the correction is asked for AND how fast the request travels - half the amount
    is half the pull at about twice the time constant, which is the difference
    between a note being nudged and a note being snapped onto the grid. At zero the
    stage is out of the path entirely, the detector with it, and the signal is
    bit-for-bit the one that arrived.
*/
void FirstAudioProcessor::processAutoTuneSample (float* samples, int channels, float rate) noexcept
{
    if (samples == nullptr || autoTuneParam == nullptr || autoTuneParam->load() < 0.5f)
        return;

    const auto amount = juce::jlimit (0.0f, 1.0f,
                                      autoTuneAmountParam != nullptr ? autoTuneAmountParam->load() : 0.0f);

    if (amount <= 1.0e-5f)
        return;

    const auto tonic = tuningTonicParam != nullptr ? static_cast<int> (tuningTonicParam->load()) : 0;
    const auto mode = tuningModeParam != nullptr ? static_cast<int> (tuningModeParam->load()) : 0;
    const auto& offsets = scaleOffsetsForMode (mode);
    const auto stageRate = rate > 1.0f ? rate : 48000.0f;

    // How fast the correction is allowed to travel, as the share of the way to the
    // target the smoothed ratio covers per frame. This is called once per FRAME,
    // so full amount is a time constant of 80 frames - 1.7 ms at 48 kHz, the snap
    // the control promises - and a third of the amount is 222 frames, 4.6 ms. Both
    // are one gesture rather than two notes, which is the point: what the control
    // mostly moves is the DEPTH of the pull below, and the glide rides along.
    const auto glide = 0.0005f + 0.012f * amount;

    for (int channel = 0; channel < juce::jlimit (0, 2, channels); ++channel)
    {
        const auto i = static_cast<std::size_t> (channel);
        const auto input = samples[i];
        auto& tracker = autoTuneTrackers[i];
        auto& shifter = autoTuneShifters[i];

        tracker.push (input);

        const auto detected = tracker.frequency();
        const auto confidence = tracker.confidence();

        // Below the gate the stage is transparent: unity ratio, and a splice
        // length of zero tells the shifter to use its fixed fallback grain.
        if (confidence < 0.5f || detected < 45.0f || detected > 1400.0f)
        {
            autoTuneCorrectionSmoothed[i] += (1.0f - autoTuneCorrectionSmoothed[i]) * glide;
            samples[i] = shifter.process (input, autoTuneCorrectionSmoothed[i], 0);
            continue;
        }

        // The note the reading lands closest to, and then the nearest note the
        // chosen key actually contains. CHROMATIC is every semitone, so the
        // second step is a no-op there and the first one is the whole of it.
        const auto midi = 69.0f + 12.0f * std::log2 (juce::jmax (1.0f, detected) / 440.0f);
        const auto nearest = juce::roundToInt (midi);
        auto target = nearest;

        if (mode != 0)
        {
            auto best = std::numeric_limits<int>::max();

            for (int candidate = nearest - 12; candidate <= nearest + 12; ++candidate)
            {
                const auto pitchClass = ((candidate - tonic) % 12 + 12) % 12;

                if (std::find (offsets.begin(), offsets.end(), pitchClass) != offsets.end()
                    && std::abs (candidate - nearest) < best)
                {
                    target = candidate;
                    best = std::abs (candidate - nearest);
                }
            }
        }

        const auto targetHz = 440.0f * std::pow (2.0f, (static_cast<float> (target) - 69.0f) / 12.0f);

        // A semitone either way: a corrector pulls a note ONTO the grid, it does
        // not transpose. A wider grip would chase a reading the detector is
        // reporting an octave away and take the performance with it.
        const auto ideal = juce::jlimit (0.94f, 1.06f, targetHz / juce::jmax (1.0f, detected));

        // TUNE AMOUNT is a DEPTH as well as a speed. Half the amount is half the
        // pull - a note a hair sharp lands, a note a quarter-tone flat is only
        // nudged - which is the setting a singer reaches for when they want the
        // corrector felt rather than heard. At full amount the pull is the whole
        // semitone and the glide is 8 ms, which is the snap the control promises.
        const auto ratio = 1.0f + (ideal - 1.0f) * amount;

        autoTuneCorrectionSmoothed[i] += (ratio - autoTuneCorrectionSmoothed[i]) * glide;

        // The splice length is the period of the material in frames of the
        // SHIFTER's rate. The tracker measured it at its own decimated rate, so
        // it is rescaled here - the one place that knows both rates.
        const auto stagePeriod = tracker.periodFrames() * stageRate / tracker.analysisRateHz();
        const auto periodHint = stagePeriod > 0.0f ? juce::roundToInt (stagePeriod) : 0;

        samples[i] = shifter.process (input, autoTuneCorrectionSmoothed[i], periodHint);
    }
}

void FirstAudioProcessor::publishBlockTelemetry (const BlockContext& context,
                                                float inputPeak, float inputRms,
                                                float outputPeak, float outputRms,
                                                float inputPeakReductionDb, float peakReductionDb,
                                                float inputEnvelopeActivity, float driftNow,
                                                bool clippingThisBlock, bool inputClippingThisBlock,
                                                float currentLufs, float currentInputLufs,
                                                float driveInto)
{
    // -------------------------------------------------------------------------
    //  Four-way loudness meter.
    //
    //  Each view is converted to dB and then averaged with equal weight, so no single
    //  scale can dominate the combined reading:
    //
    //    dB   - true peak of the block, the only view that reports clipping
    //    RMS  - electrical average of the whole block
    //    LUFS - K-weighted, so it tracks perceived loudness rather than voltage
    //    VU   - 300 ms ballistic average, the classic programme-level display
    //
    //  Averaging in dB (rather than linear) keeps the four views comparable, because
    //  all four are already level scales; converting to linear first would let a single
    //  silent view drag the result toward -infinity.
    // -------------------------------------------------------------------------
    const auto outputPeakDbValue = juce::Decibels::gainToDecibels (outputPeak, -70.0f);
    const auto outputRmsDbValue = juce::Decibels::gainToDecibels (outputRms, -70.0f);
    const auto outputVuDbValue = juce::Decibels::gainToDecibels (vuAverage, -70.0f);

    constexpr float viewWeight = 0.25f;
    const auto combinedDb = outputPeakDbValue * viewWeight
                          + outputRmsDbValue * viewWeight
                          + currentLufs * viewWeight
                          + outputVuDbValue * viewWeight;

    outputPeakDb.store (outputPeakDbValue, std::memory_order_relaxed);
    outputRmsDb.store (outputRmsDbValue, std::memory_order_relaxed);
    outputLufs.store (currentLufs, std::memory_order_relaxed);
    outputVuDb.store (outputVuDbValue, std::memory_order_relaxed);
    outputCombinedDb.store (combinedDb, std::memory_order_relaxed);
    outputClipping.store (clippingThisBlock, std::memory_order_relaxed);

    // Same four-way treatment for the input side, so the two meters are directly
    // comparable: the difference between them is what the plugin did to the level.
    const auto inputPeakDbValue = juce::Decibels::gainToDecibels (inputPeak, -70.0f);
    const auto inputRmsDbValue = juce::Decibels::gainToDecibels (inputRms, -70.0f);
    const auto inputVuDbValue = juce::Decibels::gainToDecibels (inputVuAverage, -70.0f);

    const auto inputCombinedValue = inputPeakDbValue * viewWeight
                                  + inputRmsDbValue * viewWeight
                                  + currentInputLufs * viewWeight
                                  + inputVuDbValue * viewWeight;

    inputPeakDb.store (inputPeakDbValue, std::memory_order_relaxed);
    inputRmsDb.store (inputRmsDbValue, std::memory_order_relaxed);
    inputLufs.store (currentInputLufs, std::memory_order_relaxed);
    inputVuDb.store (inputVuDbValue, std::memory_order_relaxed);
    inputCombinedDb.store (inputCombinedValue, std::memory_order_relaxed);
    inputClipping.store (inputClippingThisBlock, std::memory_order_relaxed);

    // The linear peak and RMS levels the scene's peak lamp drains each tick
    // (getOutputPeakLevel is an exchange), published here exactly as the bypass
    // path publishes its own pair in processBypassedBlock.
    inputPeakLevel.store (inputPeak, std::memory_order_relaxed);
    outputPeakLevel.store (outputPeak, std::memory_order_relaxed);
    inputRmsLevel.store (inputRms, std::memory_order_relaxed);
    outputRmsLevel.store (outputRms, std::memory_order_relaxed);

    // Subharmonic telemetry: what the undertone cascade is doing. Published once
    // per block from the LEFT generator, because the two are locked to the same
    // note by construction - the anti-phase protection keeps them together, and
    // reporting both would only invite the reader to compare two numbers that are
    // supposed to agree.
    subfundTrackedHz.store (subharmonicL.getTrackedFrequency (context.sampleRate),
                            std::memory_order_relaxed);
    subfundConfidence.store (subharmonicL.getCycleConfidence(), std::memory_order_relaxed);

    // Harmonic character, measured on the shaper earlier in the block. Published so the
    // panel can show the even/odd balance the tape stage is actually producing.
    evenHarmonicRatio.store (harmonicAnalyser.getEvenRatio(), std::memory_order_relaxed);
    oddHarmonicRatio.store (harmonicAnalyser.getOddRatio(), std::memory_order_relaxed);

    // Blocks of zero samples arrive during silence (and with some host buffer sizes),
    // and by then the per-sample smoothing would never have been advanced.
    inputGainSmoothed.skip (context.samples);
    outputGainSmoothed.skip (context.samples);
    mixSmoothed.skip (context.samples);
    widthSmoothed.skip (context.samples);
    bypassSmoothed.skip (context.samples);

    // Glue compressor telemetry: worst-case reduction this block plus an activity
    // envelope the UI can animate, both read without locking. Each stage publishes
    // its own reduction and activity so the editor can give it a dedicated meter.
    inputGainReductionDb.store (inputPeakReductionDb, std::memory_order_relaxed);
    outputGainReductionDb.store (peakReductionDb, std::memory_order_relaxed);
    inputCompressorActivity.store (juce::jlimit (0.0f, 1.0f, inputEnvelopeActivity),
                                   std::memory_order_relaxed);
    outputCompressorActivity.store (juce::jlimit (0.0f, 1.0f,
                                                  outputCompressor.getEnvelopeActivity()),
                                    std::memory_order_relaxed);
    compressorActivity.store (juce::jlimit (0.0f, 1.0f,
                                            juce::jmax (inputEnvelopeActivity,
                                                        outputCompressor.getEnvelopeActivity())),
                              std::memory_order_relaxed);

    // Transport drift mapped to 0..1 for the UI wobble, and a harmonic weight
    // derived from how hard the input is being driven into the tape curve.
    transportDrift.store (juce::jlimit (0.0f, 1.0f, 0.5f + driftNow * 2.0f),
                          std::memory_order_relaxed);
    harmonicCharacter.store (driveInto, std::memory_order_relaxed);

    // -------------------------------------------------------------------------
    //  Publish one complete telemetry frame.
    //
    //  This is the LAST thing the block does, so every value in the frame is the
    //  final value for this block - the meters, the drift, the harmonics, the
    //  subfund tracker and the anti-phase guard have all finished writing. With
    //  the lock-free queue present, this ONE push is what the editor reads: the
    //  fields cannot be read half-updated, because they are pushed together.
    //
    //  The individual atomics are still written, because the editor falls back to
    //  them when the queue is not compiled in and because they are what the DSP
    //  harness and any future non-UI consumer read. Keeping both costs a handful of
    //  relaxed stores per block and means neither path can be the stale one.
    // -------------------------------------------------------------------------
    {
        TelemetryFrame frame;
        frame.inputPeakDb = inputPeakDb.load (std::memory_order_relaxed);
        frame.inputRmsDb = inputRmsDb.load (std::memory_order_relaxed);
        frame.inputLufs = inputLufs.load (std::memory_order_relaxed);
        frame.inputVuDb = inputVuDb.load (std::memory_order_relaxed);
        frame.inputCombinedDb = inputCombinedDb.load (std::memory_order_relaxed);
        frame.inputClipping = inputClipping.load (std::memory_order_relaxed);

        frame.outputPeakDb = outputPeakDb.load (std::memory_order_relaxed);
        frame.outputRmsDb = outputRmsDb.load (std::memory_order_relaxed);
        frame.outputLufs = outputLufs.load (std::memory_order_relaxed);
        frame.outputVuDb = outputVuDb.load (std::memory_order_relaxed);
        frame.outputCombinedDb = outputCombinedDb.load (std::memory_order_relaxed);
        frame.outputClipping = outputClipping.load (std::memory_order_relaxed);

        frame.inputGainReductionDb = inputPeakReductionDb;
        frame.outputGainReductionDb = peakReductionDb;
        frame.inputCompressorActivity = inputEnvelopeActivity;
        frame.outputCompressorActivity = outputCompressor.getEnvelopeActivity();
        frame.compressorActivity = juce::jmax (inputEnvelopeActivity,
                                               outputCompressor.getEnvelopeActivity());

        frame.transportDrift = transportDrift.load (std::memory_order_relaxed);
        frame.harmonicCharacter = driveInto;
        frame.evenHarmonicRatio = evenHarmonicRatio.load (std::memory_order_relaxed);
        frame.oddHarmonicRatio = oddHarmonicRatio.load (std::memory_order_relaxed);
        frame.subfundTrackedHz = subfundTrackedHz.load (std::memory_order_relaxed);
        frame.subfundConfidence = subfundConfidence.load (std::memory_order_relaxed);
        frame.antiPhaseAmount = antiPhaseCorrection;
        frame.transportRamp = transportRampPublished.load (std::memory_order_relaxed);
        frame.spindownRamp = spindownRampPublished.load (std::memory_order_relaxed);
        frame.bypassActive = bypassActive.load (std::memory_order_relaxed);

#if J37_HAS_RWQ
        // Non-blocking: if the editor has not drained the ring, the OLDEST frame
        // is dropped rather than the audio thread waiting. An old meter reading is
        // worthless, so dropping is the right answer. pop() first is not needed -
        // ReaderWriterQueue's try_enqueue overwrites nothing and simply fails when
        // full, so a full ring stops being a problem the moment the editor reads.
        if (! telemetryQueue.try_enqueue (frame))
        {
            // Full ring: make room by discarding the oldest frame, then retry once.
            TelemetryFrame discarded;
            telemetryQueue.try_dequeue (discarded);
            telemetryQueue.try_enqueue (frame);
        }
#endif

        // The always-present mirror. These are the three the editor needs even when
        // the queue is absent, and the values a host-side meter would read.
        telemetryInputPeakDb.store (frame.inputPeakDb, std::memory_order_relaxed);
        telemetryOutputPeakDb.store (frame.outputPeakDb, std::memory_order_relaxed);
        telemetryInputRmsDb.store (frame.inputRmsDb, std::memory_order_relaxed);
        telemetryOutputRmsDb.store (frame.outputRmsDb, std::memory_order_relaxed);
        telemetryAntiPhase.store (frame.antiPhaseAmount, std::memory_order_relaxed);
        telemetryPlatter.store (frame.transportRamp, std::memory_order_relaxed);
    }
}

void FirstAudioProcessor::processTapeEngine (juce::dsp::AudioBlock<float> block,
                                             juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);

    const int numSamples = static_cast<int> (block.getNumSamples());

    // processBlock has already expanded oversampled blocks. Build all DSP time
    // constants against the rate of this block, not the host/base rate.
    const auto engineSampleRate = juce::jmax (1.0f, sampleRate * oversamplingRateFactor);

    // The engine works directly on the block: in the oversampled path the block
    // references the oversampler's internal storage, in the plain path it wraps
    // the host buffer. Either way processing is in place, so what arrives also
    // leaves through the same block.

    // Channels the engine actually works with: up to two, bounded by both what the
    // host delivers and what the oversampler's internal storage provides. Clearing
    // output-only channels (a mono-in/stereo-out host) must not write past the block.
    const int activeInputChannels = juce::jmin (2, juce::jmin (getTotalNumInputChannels(),
                                                               (int) block.getNumChannels()));

    // Clear any output-only channels (mono->stereo hosts) the host expects filled.
    // AudioBlock::clear() in JUCE 9 takes no arguments and clears EVERY channel,
    // so the per-channel primitive is FloatVectorOperations::clear directly -
    // exactly what AudioBlock's own clearInternal() calls.
    for (int channel = activeInputChannels;
         channel < juce::jmin (2, (int) block.getNumChannels());
         ++channel)
        juce::FloatVectorOperations::clear (
            block.getChannelPointer (static_cast<std::size_t> (channel)), numSamples);

    // Fast bypass exits before coefficient setup and the per-sample machine loop.
    // Delta Listen deliberately disables this shortcut until its fade reaches zero.
    BlockContext blockContext { numSamples, activeInputChannels, engineSampleRate };
    if (processBypassedBlock (block, blockContext))
        return;

    bypassActive.store (false, std::memory_order_relaxed);

    const auto tapeType = static_cast<int> (tapeTypeParam->load());
    const auto speed = static_cast<int> (speedParam->load());
    const auto instrument = (instrumentParam != nullptr)
                                ? static_cast<int> (instrumentParam->load()) : 0;
    const auto drive = driveParam->load();
    const auto bias = biasParam->load();
    const auto tone = toneParam->load();
    const auto character = characterParam != nullptr ? characterParam->load() : 0.5f;
    const auto wow = wowParam->load();
    const auto flutter = flutterParam->load();
    const auto mix = (mixParam != nullptr ? mixParam->load() : 50.0f) * 0.01f;
    const auto outputDb = outputDbParam->load();
    const auto inputDb = inputDbParam->load();
    // WIDTH is centred at 50%: 0 is mono, 50 is natural, 100 doubles SIDE.
    const auto stereoWidth = widthParam != nullptr
        ? juce::jlimit (0.0f, 2.0f, widthParam->load() * 2.0f) : 1.0f;
    const auto compressorThreshold = compressorThresholdParam != nullptr ? compressorThresholdParam->load() : -18.0f;
    const auto compressorRatio = compressorRatioParam != nullptr ? compressorRatioParam->load() : 2.0f;
    const auto compressorAttack = compressorAttackParam != nullptr ? compressorAttackParam->load() : 10.0f;
    const auto compressorRelease = compressorReleaseParam != nullptr ? compressorReleaseParam->load() : 120.0f;
    const auto compressorMakeup = compressorMakeupParam != nullptr ? compressorMakeupParam->load() : 0.0f;
    const auto compressorMix = compressorMixParam != nullptr ? compressorMixParam->load() : 0.0f;
    const auto compressorMixAmount = juce::jlimit (0.0f, 1.0f, compressorMix);
    const bool autoTuneEnabled = autoTuneParam != nullptr && autoTuneParam->load() >= 0.5f;
    const auto autoTuneAmount = autoTuneAmountParam != nullptr ? autoTuneAmountParam->load() : 0.0f;
    const auto autoTuneStrength = autoTuneEnabled ? juce::jlimit (0.0f, 1.0f, autoTuneAmount) : 0.0f;
    // The anti-phase guard's switch, read once per block like every other choice.
    // A missing parameter (a host that restored a session written before the
    // switch existed) means ON: the guard is protection, and the safe reading of
    // "not stated" is the state the plugin has always shipped in.
    const bool antiPhaseGuardOn = antiPhaseGuardParam == nullptr
                                 || antiPhaseGuardParam->load() >= 0.5f;

    // -------------------------------------------------------------------------
    //  MODELED TRACKS: the geometry of the tape, read once per block.
    //
    //  Three consequences of the layout, all of them geometry rather than taste:
    //
    //    widthScale   how WIDE each track is. A stereo deck's two tracks each
    //                 get half the tape; a three-track deck's get a third. A
    //                 narrower track reads less low end per channel and a
    //                 proportionally higher noise floor for the same tape, so
    //                 this is the same number driving both.
    //
    //    crosstalk    how much of the OTHER channel leaks into this one, from
    //                 the head's fringing field. The closer the tracks, the
    //                 more - so a three-track deck leaks most and the 2+3
    //                 layout, with a whole track between the two channels,
    //                 leaks least.
    //
    //    spacingDelay how long the leakage takes to arrive, in samples. The
    //                 fringing path is short but it is not instant, and on a
    //                 multitrack the spacing between the tracks is what sets
    //                 it - which is why the leak is a tiny DELAY rather than an
    //                 instantaneous blend. It is a fraction of a millisecond in
    //                 every real layout, so it reads as thickening rather than
    //                 as an echo, which is exactly what track-to-track bleed is.
    // -------------------------------------------------------------------------
    const auto tracksIndex = tracksParam != nullptr
                                 ? juce::jlimit (0, 2, static_cast<int> (tracksParam->load())) : 0;

    //       2-track        2+3 (4-track used as 2)     3-track
    //       widest/tightest spacing,       widest spacing,        narrowest tracks,
    //       least crosstalk                least crosstalk        most crosstalk
    static constexpr float tracksWidthScale[3]   { 1.00f, 0.94f, 0.82f };
    static constexpr float tracksCrosstalk[3]    { 0.020f, 0.010f, 0.055f };
    static constexpr float tracksSpacingMs[3]    { 0.018f, 0.042f, 0.009f };
    static constexpr float tracksNoiseScale[3]   { 1.00f, 1.06f, 1.22f };

    const float trackWidthScale = tracksWidthScale[tracksIndex];
    const float trackCrosstalk = tracksCrosstalk[tracksIndex];
    const float trackNoiseScale = tracksNoiseScale[tracksIndex];

    // The bleed's own delay, in samples. A one-sample floor keeps the line valid
    // at every rate, and at any normal rate the figure is tens of samples - far
    // below the ear's ability to hear it as a separate event, which is the
    // point: it is an infinitesimal smear, not an echo. It is a hard limit that
    // the maximum spacing is well under, so the wrap below never loops.
    const int trackBleedDelaySamples = juce::jlimit (
        1, tracksBleedBufferLength - 2,
        juce::roundToInt (tracksSpacingMs[tracksIndex] * 0.001f * engineSampleRate));

    // Output-stage switches are read once per block. Polarity is applied to each
    // final sample, after the clean-mono diagnostics, and auto gain gates the slow
    // programme-level compensator.
    const float polaritySign = (polarityParam != nullptr && polarityParam->load() >= 0.5f) ? -1.0f : 1.0f;
    const bool autoGainEnabled = autoGainParam == nullptr || autoGainParam->load() >= 0.5f;

    inputGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (inputDb));
    outputGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (outputDb));
    mixSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, mix));
    widthSmoothed.setTargetValue (stereoWidth);
    // BYPASS and DELTA are read here as plain flags for the smoother targets;
    // processBypassedBlock above read them for its own early-out.
    const auto bypassRequested = bypassParam != nullptr && bypassParam->load() >= 0.5f;
    const auto deltaRequested = deltaParam != nullptr && deltaParam->load() >= 0.5f;
    bypassSmoothed.setTargetValue (bypassRequested ? 0.0f : 1.0f);
    deltaListenSmoothed.setTargetValue (deltaRequested ? 1.0f : 0.0f);

    // Delay, stereo offset, noise trim and transport state. All read once per block
    // and fed to smoothers, so none of them can step the signal.
    const auto delayMs = delayTimeParam != nullptr ? delayTimeParam->load() : 0.0f;
    // The second head has ONE control and this is it: the tapped output's level
    // is its return amount, which is what a single LEVEL knob on a tape machine
    // means (TIME says where the head is, LEVEL says how loud its output is - see
    // the parameter's own note). It used to be two members bound to the SAME
    // parameter id under two different names, one called feedback and one called
    // level, which is an invitation to someone later "fixing" the level and
    // silently changing the decay instead.
    const auto delayFeedback = delayFeedbackParam != nullptr ? delayFeedbackParam->load() : 0.0f;
    const auto delayLevel = delayFeedback;
    const auto delayPingPong = delayPingPongParam != nullptr ? delayPingPongParam->load() : 0.0f;
    const auto stOffsetUs = stOffsetParam != nullptr ? stOffsetParam->load() : 0.0f;
    const auto noiseAmount = noiseParam != nullptr ? noiseParam->load() : 0.5f;
    const auto noiseLvlAmount = noiseLvlParam != nullptr ? noiseLvlParam->load() : 1.0f;
    // The transport state, read once per block from the parameter. The engine reads
    // it here and does not write it: the state it settles on by itself (START
    // arriving at speed, or a hold released on a stopped machine) leaves the rig as a
    // request, and the editor's timer is what turns that into a host notification.
    const auto transportState = transportParam != nullptr
                                    ? static_cast<int> (transportParam->load()) : 1;

    // -----------------------------------------------------------------------
    //  Tempo, from the host.
    //
    //  Read once per block from the playhead. `getPlayHead()` may return null (a
    //  host that does not offer one, or an offline render), and the position may
    //  be absent even when the playhead exists - so both are checked, and the
    //  last known tempo is kept when either is missing. Resetting to a default
    //  instead would make the delay jump every time the transport stopped.
    //
    //  getPosition() is called ONCE here rather than per parameter read, because
    //  it is a virtual call into the host and hosts are entitled to make it
    //  expensive.
    //
    //  The same call also answers the other question the playhead is asked -
    //  whether the host's transport is ROLLING - because START is armed on it
    //  (see the arm in TransportRig). It defaults to rolling, and it takes a
    //  POSITION for the host to be believed at all: JUCE carries the transport
    //  state as a flag inside this same struct and reports `false` for a host
    //  that never set one, so an unanswered playhead and a parked one are the
    //  same reading - and a key that refused to engage on a host that does not
    //  report would be a worse fault than the one the arm fixes.
    // -----------------------------------------------------------------------
    bool hostRolling = true;

    if (auto* hostPlayHead = getPlayHead())
    {
        if (auto position = hostPlayHead->getPosition())
        {
            if (auto bpm = position->getBpm())
            {
                // Guard against a host reporting nonsense during a seek or a
                // tempo ramp: a zero or negative tempo would divide by zero below.
                if (*bpm > 1.0 && *bpm < 1000.0)
                {
                    hostTempoBpm = *bpm;
                    hostTempoValid = true;
                }
            }

            // A position is what makes the playhead's own answer worth reading:
            // every format JUCE supports sets the transport state from the host's
            // own, so a host that supplies timing supplies this too, and a host
            // that supplies nothing keeps the rolling default above rather than
            // being read as parked.
            if (! position->getIsPlaying()
                && (position->getTimeInSamples().hasValue()
                    || position->getPpqPosition().hasValue()))
                hostRolling = false;
        }
    }

    // -----------------------------------------------------------------------
    //  The delay time, free-running or tempo-locked.
    //
    //  In free mode it is the DELAY control in milliseconds, converted to samples
    //  at the ENGINE rate, so a 100 ms head spacing is 100 ms of tape travel
    //  whether the engine runs at the session rate or at 8x it.
    //
    //  In sync mode the time is derived from the host's tempo and the note value
    //  RATE selects. A quarter note at 120 BPM is 500 ms, which is the whole
    //  calculation: one beat is 60000/tempo ms, and the ratio scales it.
    // -----------------------------------------------------------------------
    const bool delaySyncOn = delaySyncParam != nullptr && delaySyncParam->load() >= 0.5f;
    const auto delayRateIndex = delayRateParam != nullptr
                                    ? static_cast<int> (delayRateParam->load()) : 2;

    float effectiveDelayMs = delayMs;

    if (delaySyncOn)
    {
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
        constexpr int numNoteRatios = static_cast<int> (std::size (noteRatios));

        const auto ratio = noteRatios[juce::jlimit (0, numNoteRatios - 1, delayRateIndex)];

        // A whole note is four beats, so a whole note lasts 4 * 60000/tempo ms.
        const auto wholeNoteMs = 4.0 * 60000.0 / juce::jmax (1.0, hostTempoBpm);
        effectiveDelayMs = static_cast<float> (wholeNoteMs * static_cast<double> (ratio));
    }

    delaySamplesSmoothed.setTargetValue (
        juce::jlimit (0.0f, static_cast<float> (juce::jmax (0, delayBufferLength - 1)),
                      effectiveDelayMs * 0.001f * engineSampleRate));
    delayFeedbackSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, delayFeedback));
    delayLevelSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, delayLevel));
    pingPongSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, delayPingPong));

    // ST OFFSET is given in microseconds and converted to samples the same way. Only
    // the magnitude is the read offset; the sign decides WHICH channel is delayed, so
    // a negative value lags the left channel instead of the right.
    const float stOffsetSamples = std::abs (stOffsetUs) * 1.0e-6f * engineSampleRate;
    stOffsetSamplesSmoothed.setTargetValue (
        juce::jlimit (0.0f, static_cast<float> (stOffsetBufferLength - 2), stOffsetSamples));
    const bool offsetRightChannel = stOffsetUs >= 0.0f;

    // -----------------------------------------------------------------------
    //  The saturation mix and the amp voicing.
    //
    //  All seven SOURCE amounts are read once per block and fed to smoothers,
    //  so none of them can step the curve. The weights themselves are applied
    //  inside the core from the SMOOTHED values, so moving a SOURCE knob morphs
    //  the harmonics continuously rather than switching between seven curves.
    // -----------------------------------------------------------------------
    const auto vinylAmountSrc = vinylSourceParam != nullptr ? vinylSourceParam->load() : 0.0f;
    const auto tapeAmount = tapeSourceParam != nullptr ? tapeSourceParam->load() : 1.0f;
    const auto valveAmount = valveSourceParam != nullptr ? valveSourceParam->load() : 0.0f;
    const auto cassetteAmount = cassetteSourceParam != nullptr ? cassetteSourceParam->load() : 0.0f;
    const auto ampAmount = ampSourceParam != nullptr ? ampSourceParam->load() : 0.0f;
    const auto transformerAmount = transformerSourceParam != nullptr ? transformerSourceParam->load() : 0.0f;
    const auto digitalAmount = digitalSourceParam != nullptr ? digitalSourceParam->load() : 0.0f;
    const auto sagAmount = sagParam != nullptr ? sagParam->load() : 0.0f;
    const auto presenceAmount = presenceParam != nullptr ? presenceParam->load() : 0.5f;
    const auto cabinetAmount = cabinetParam != nullptr ? cabinetParam->load() : 0.0f;
    const auto ampBiasAmount = ampBiasParam != nullptr ? ampBiasParam->load() : 0.5f;

    tapeSourceSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, tapeAmount));
    vinylSourceSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, vinylAmountSrc));
    valveSourceSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, valveAmount));
    cassetteSourceSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, cassetteAmount));
    ampSourceSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, ampAmount));
    transformerSourceSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, transformerAmount));
    digitalSourceSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, digitalAmount));
    sagSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, sagAmount));
    presenceSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, presenceAmount));
    cabinetSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, cabinetAmount));
    ampBiasSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, ampBiasAmount));

    // -----------------------------------------------------------------------
    //  Transient shaper and neural stage.
    //
    //  Four controls, read once per block and fed to smoothers like the rest.
    //  The transient stage's two DETECTOR poles are the only rate-dependent
    //  numbers here, and they are block-rate constants for the same reason every
    //  other coefficient is: they cannot change within a block. The stage's own
    //  gain smoother is one of them, so it costs no exp() in the loop.
    // -----------------------------------------------------------------------
    const auto transientAttackAmount = transientAttackParam != nullptr
                                           ? transientAttackParam->load() : 0.0f;
    const auto transientSustainAmount = transientSustainParam != nullptr
                                            ? transientSustainParam->load() : 0.0f;
    const auto transientMixAmount = transientMixParam != nullptr
                                        ? transientMixParam->load() : 0.0f;
    const auto neuralMixAmount = neuralMixParam != nullptr ? neuralMixParam->load() : 0.0f;
    const auto irMixAmount = irMixParam != nullptr ? irMixParam->load() : 0.0f;

    transientAttackSmoothed.setTargetValue (juce::jlimit (-1.0f, 1.0f, transientAttackAmount));
    transientSustainSmoothed.setTargetValue (juce::jlimit (-1.0f, 1.0f, transientSustainAmount));
    transientMixSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, transientMixAmount));
    neuralMixSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, neuralMixAmount));
    irMixSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, irMixAmount));

    // ~2 ms transient window against a ~120 ms programme reference: the ratio of
    // the two is what the detector reads as an edge, so the gap between them is
    // the effect's whole time constant. 30 ms on the applied gain keeps the ride
    // a ride rather than a per-sample multiply of the envelope's own wobble.
    transientFastCoefficient = onePoleCoefficient (2.0f, engineSampleRate);
    transientSlowCoefficient = onePoleCoefficient (120.0f, engineSampleRate);
    transientGainCoefficient = onePoleCoefficient (30.0f, engineSampleRate);

    // -----------------------------------------------------------------------
    //  Preamp, distortion, tape condition, reverb, vinyl and the two modes.
    //
    //  All read once per block and fed to smoothers, so none of them can step
    //  the signal. The modes are read as plain bools because they change the
    //  MACHINE's voicing rather than a per-sample coefficient, and a mode is not
    //  something a user sweeps.
    // -----------------------------------------------------------------------
    const auto preampAmount = preampParam != nullptr ? preampParam->load() : 0.0f;
    const auto distortionAmount = distortionParam != nullptr ? distortionParam->load() : 0.0f;
    const auto fluxAmount = fluxParam != nullptr ? fluxParam->load() : 0.5f;
    const auto wearAmount = wearParam != nullptr ? wearParam->load() : 0.0f;
    const auto mechanicsAmount = mechanicsParam != nullptr ? mechanicsParam->load() : 0.0f;
    const auto reverbMix = reverbParam != nullptr ? reverbParam->load() : 0.0f;
    const auto reverbSize = reverbSizeParam != nullptr ? reverbSizeParam->load() : 0.4f;
    const auto vinylAmount = vinylParam != nullptr ? vinylParam->load() : 0.0f;
    const auto vinylCrackle = vinylCrackleParam != nullptr ? vinylCrackleParam->load() : 0.5f;
    const auto vinylRumble = vinylRumbleParam != nullptr ? vinylRumbleParam->load() : 0.35f;

    preampSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, preampAmount));
    distortionSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, distortionAmount));

    // -----------------------------------------------------------------------
    //  The DI box.
    //
    //  The pad is a CHOICE parameter (0 / -10 / -20 / -30 dB), so its raw value
    //  is the index and the dB it means is looked up in ONE place here. A pad is
    //  a switch, not a sweep, which is why it goes through no smoother - a
    //  smoothed pad would glide between two pads and briefly sit at a level that
    //  is neither.
    //
    //  The load and transformer coefficients are built from the engine rate, so
    //  the box behaves identically at every rate and at every oversampling
    //  factor - they are rate-dependent time constants like every other one in
    //  this file. The hum increment is the mains frequency the ground loop picks
    //  up (50 Hz), converted the same way.
    // -----------------------------------------------------------------------
    diSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, diParam != nullptr ? diParam->load() : 0.0f));
    diLoadSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f,
                                                 diLoadParam != nullptr ? diLoadParam->load() : 0.0f));
    diTransformerSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f,
                                                        diTransformerParam != nullptr
                                                            ? diTransformerParam->load() : 0.0f));

    {
        static constexpr float padDbValues[4] { 0.0f, -10.0f, -20.0f, -30.0f };
        const auto padIndex = diPadParam != nullptr
                                  ? juce::jlimit (0, 3, static_cast<int> (diPadParam->load())) : 0;
        diPadDb = padDbValues[padIndex];
    }

    // The load's own bandwidth: a very high input impedance is effectively
    // transparent (12 kHz at the neutral end), and a heavy load damps the
    // source's top octave. The LOAD control then scales this further inside the
    // stage, so the two together give the full range from "no load" to "heavy".
    diLoadCoefficient = onePoleCoefficientHz (12000.0f, engineSampleRate);
    // The DI transformer's own low split: small transformers bloom the bottom of
    // their band, and 200 Hz is where that starts being audible as "thickness".
    diTransformerCoefficient = onePoleCoefficientHz (200.0f, engineSampleRate);
    diHumIncrement = 50.0f / engineSampleRate;
    fluxSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, fluxAmount));
    wearSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, wearAmount));
    mechanicsSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, mechanicsAmount));
    reverbMixSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, reverbMix));
    reverbSizeSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, reverbSize));
    vinylSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, vinylAmount));
    vinylCrackleSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, vinylCrackle));
    vinylRumbleSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, vinylRumble));

    // The two mode switches. They are mutually exclusive BY DESIGN - a machine
    // cannot be both a modern deck and a deliberately degraded one - so MODERN
    // wins when both are set, which is the safer of the two to be wrong about.
    // The raw value of a bool parameter is a float, so it is tested against 0.5 -
    // exactly how `bypassParam` and `polarityParam` are read. Using it as a bool
    // directly would compile only because of an implicit conversion and would
    // read as "not zero", which happens to work here but hides the type.
    const bool modernRequested = modernModeParam != nullptr && modernModeParam->load() >= 0.5f;
    const bool lofiRequested = lofiModeParam != nullptr && lofiModeParam->load() >= 0.5f;
    modernMode = modernRequested;
    lofiMode = ! modernRequested && lofiRequested;

    // The delay character, cached as an int so the per-sample loop branches on a
    // register rather than reading an atomic.
    delayTypeCached = delayTypeParam != nullptr
                          ? static_cast<int> (delayTypeParam->load()) : 0;

    // The five type switches, cached for the same reason. Each is clamped to its
    // own list before it reaches the engine: a choice parameter should never
    // exceed its list, but a restored state that predates the parameter could
    // hand back anything, and every one of these indexes a static table.
    valveTypeCached = juce::jlimit (0, valveTypeCount - 1,
                          valveTypeParam != nullptr
                              ? static_cast<int> (valveTypeParam->load()) : 0);
    ampTypeCached = juce::jlimit (0, ampTypeCount - 1,
                        ampTypeParam != nullptr
                            ? static_cast<int> (ampTypeParam->load()) : 0);
    transformerTypeCached = juce::jlimit (0, transformerTypeCount - 1,
                                transformerTypeParam != nullptr
                                    ? static_cast<int> (transformerTypeParam->load()) : 0);
    digitalTypeCached = juce::jlimit (0, digitalTypeCount - 1,
                            digitalTypeParam != nullptr
                                ? static_cast<int> (digitalTypeParam->load()) : 0);
    vinylTypeCached = juce::jlimit (0, vinylTypeCount - 1,
                          vinylTypeParam != nullptr
                              ? static_cast<int> (vinylTypeParam->load()) : 0);

    // The vinyl type re-voices the two VinylStages, once per block. RIAA depth,
    // crackle and rumble multipliers, tick decay and groove hiss all belong to
    // the disc rather than to the controls, so they are written here and only
    // read in the loop.
    {
        // riaa, rumble, crackle, decay, surface
        static constexpr float voices[6][5] =
        {
            // Standard LP: the reference pressing. Full RIAA, quiet surface.
            { 0.45f, 1.00f, 0.60f, 0.99850f, 0.00f },
            // Single: less play per side, wider grooves, a little more top. The
            // rumble is lower - a single spins faster and the bearing sees less
            // time under the stylus.
            { 0.42f, 0.80f, 0.70f, 0.99820f, 0.00f },
            // Shellac 78: pre-vinyl. Shellac is abrasive, so the surface is
            // LOUD between every note, the groove hiss is continuous, the
            // rumble is a wind-up motor's, and there is no RIAA to undo - 78s
            // predate the curve, so the tilt here is the rougher blunt one.
            { 0.18f, 1.60f, 1.35f, 0.99700f, 1.00f },
            // Worn Classic: a well-loved record. The ticks multiply and ring,
            // the surface hiss rises, the top end has been played off.
            { 0.40f, 1.15f, 1.60f, 0.99880f, 0.35f },
            // Dubplate: soft lacquer, loud cut, played minutes after the cut.
            // Crackle is nearly absent and the RIAA is hot - the lathe was
            // pushed for a sound system.
            { 0.55f, 0.90f, 0.25f, 0.99900f, 0.05f },
            // Half-Speed Master: the cleanest pressing. The lathe tracked at
            // half speed, so the RIAA is deep and the surface is almost silent.
            { 0.55f, 0.70f, 0.30f, 0.99920f, 0.02f },
        };

        // VINYL TYPE = OFF: no DISC at all.
        //
        // The record is removed from the turntable, so what is left is the
        // PLAYBACK CHAIN with nothing in the groove: no RIAA, no crackle, no
        // rumble, no groove hiss, no surface character. The stage's own controls
        // (DUST, SCRATCH, WARP, CLICKS, ELECTRICAL) still work, because they
        // describe the record and the deck modelling rather than the pressing -
        // but with the disc off they act on a silent groove, so the faults that
        // need programme (dust, which follows the level) fall silent with it.
        //
        // It is the reference state for hearing what the DISC contributes.
        const bool vinylTypeOff = (vinylTypeCached == modelOffIndex);
        const auto& v = voices[juce::jlimit (0, 5, vinylTypeCached)];
        for (auto* stage : { &vinylL, &vinylR })
        {
            stage->riaaAmount    = vinylTypeOff ? 0.0f : v[0];
            stage->rumbleAmount  = vinylTypeOff ? 0.0f : v[1];
            stage->crackleAmount = vinylTypeOff ? 0.0f : v[2];
            stage->crackleDecay  = v[3];
            stage->surfaceNoise  = v[4];
        }

        // VINYL SPEED sets how much the disc's own speed error modulates the
        // stage - a slow wow on a 33, a tighter shimmer on a 45, and the wind-up
        // motor's wobble on a 78. Zero is a perfectly steady turntable. The
        // wander the type voices apply is folded on top of it, so a shellac at
        // speed 0 is steady but still shellac.
        const auto vinylSpeedNow = vinylSpeedParam != nullptr
            ? static_cast<int> (vinylSpeedParam->load()) : 0;
        static constexpr float wanderRate[3] = { 0.62f, 1.10f, 3.30f };   // Hz
        static constexpr float wanderDepth[3] = { 0.010f, 0.016f, 0.028f };
        for (auto* stage : { &vinylL, &vinylR })
        {
            stage->vinylWowIncrement = wanderRate[vinylSpeedNow] / engineSampleRate;
            // The turntable scales the depth (see the voicing below, which runs
            // after this and overwrites it with the scale applied) - the deck's
            // stability is what decides how much of the disc's wander you hear.
            stage->speedModulation = wanderDepth[vinylSpeedNow];

            // Every one of the four faults' rates is derived from the SAME platter
            // speed the wander uses, because they are all consequences of how fast
            // the disc turns: a scratch closes on the stylus once per revolution,
            // a warp lifts it once per revolution, and a 78's motor hums over a
            // winding that is not synchronised to a 33's at all. Deriving them from
            // one number is what makes the faults belong to the SAME record instead
            // of being four unrelated generators.
            const float revolutionHz = wanderRate[vinylSpeedNow];

            stage->scratchIncrement = revolutionHz / engineSampleRate;
            stage->warpIncrement = revolutionHz / engineSampleRate;

            // The dust's bandwidth is a real corner frequency - the grit sits above
            // the programme's own top end - converted the same way every other
            // coefficient here is. It also opens slightly with the platter speed,
            // because a faster stylus excites finer grit.
            stage->dustCoefficient = onePoleCoefficientHz (
                6000.0f + 2000.0f * static_cast<float> (vinylSpeedNow), engineSampleRate);

            // Mains hum is the one rate that is NOT the disc's: it is the supply's.
            // 50 Hz is the standard outside North America and 60 Hz inside it; the
            // engine uses the host's declared sample rate to place it and defaults
            // to 50 Hz, which is the more common supply and the lower, more audible
            // hum. The second harmonic is set from the same base so the two can
            // never lose their 2:1 relationship.
            constexpr float humHz = 50.0f;
            stage->humIncrement = humHz / engineSampleRate;
            stage->humIncrement2 = humHz * 2.0f / engineSampleRate;

            // The four amounts, read once per block like every other control. They
            // are squared inside the stage, so the control's travel is gentle at the
            // bottom (a little dust is a little dust) and the top end is where the
            // damage actually sounds like damage.
            stage->dustAmount = vinylDustParam != nullptr ? vinylDustParam->load() : 0.0f;
            stage->scratchAmount = vinylScratchParam != nullptr ? vinylScratchParam->load() : 0.0f;
            stage->warpAmount = vinylWarpParam != nullptr ? vinylWarpParam->load() : 0.0f;
            stage->electricalAmount = vinylElectricalParam != nullptr ? vinylElectricalParam->load() : 0.0f;

            // CLICKS rides the same platter-locked rate as the scratch and the
            // warp, because the periodic half of it IS a once-per-revolution
            // event: a pressing fault crossed by the stylus every turn.
            stage->clickAmount = vinylClicksParam != nullptr ? vinylClicksParam->load() : 0.0f;
            stage->clickIncrement = revolutionHz / engineSampleRate;

            // The split the GENERATION and CARTRIDGE tilts both act around: a
            // real playback-band boundary at 1.2 kHz, converted the same way
            // every other coefficient here is. One coefficient, two users, so
            // the two controls cannot disagree about where the boundary is.
            stage->cartridgeCoefficient = onePoleCoefficientHz (1200.0f, engineSampleRate);
        }

        // ------------------------------------------------------------------
        //  GENERATION, TURNTABLE and CARTRIDGE - the voicing of the three
        //  selectors. They are read once per block here, and the stage applies
        //  them per sample, exactly like the type voices above.
        // ------------------------------------------------------------------
        const auto generationIndex = vinylGenerationParam != nullptr
            ? juce::jlimit (0, 2, static_cast<int> (vinylGenerationParam->load())) : 0;
        const auto turntableIndex = vinylTurntableParam != nullptr
            ? juce::jlimit (0, 2, static_cast<int> (vinylTurntableParam->load())) : 0;
        const auto cartridgeIndex = vinylCartridgeParam != nullptr
            ? juce::jlimit (0, 2, static_cast<int> (vinylCartridgeParam->load())) : 0;

        // GENERATION: how much top end the cut and the pressing took, how much
        // bottom was left, and how much noise the process added. The three are
        // ordered from the reference cut to the copy of a copy.
        struct GenerationVoice { float topLoss; float bottomLift; float noise; };
        static constexpr GenerationVoice generationVoices[3]
        {
            // Laquer: the reference cut. Almost no loss, no extra noise.
            { 0.94f, 1.02f, 1.00f },
            // Direct: cut straight to metal. The most open top and the
            // quietest surface of the three - cleaner than the lacquer itself,
            // because one whole generation is missing.
            { 1.03f, 1.00f, 0.90f },
            // Printed: the pressing you actually buy. Every generation's loss,
            // a noticeably darker top, a fuller bottom and a noisier surface.
            { 0.80f, 1.08f, 1.35f },
        };

        // TURNTABLE: how much the drive wanders and how steadily it holds. A
        // belt is smooth but elastic, a direct drive is locked solid, an idler
        // couples the motor's own rumble into the platter.
        struct TurntableVoice { float wowScale; float stability; };
        static constexpr TurntableVoice turntableVoices[3]
        {
            { 1.10f, 1.15f },   // Belt: a little more wander, less steady
            { 0.55f, 0.70f },   // Direct: locked, the steadiest of the three
            { 1.35f, 1.55f },   // Idler: the least stable, coupling the drive
        };

        // CARTRIDGE: a top/bottom gain pair plus its own noise multipliers. The
        // moving coil is the bright, detailed one and carries the most hiss
        // because of its lower output; the DJ cart is the hot, heavier one with
        // the most surface noise.
        struct CartridgeVoice { float top; float bottom; float noise; float hiss; };
        static constexpr CartridgeVoice cartridgeVoices[3]
        {
            { 0.94f, 1.04f, 1.00f, 1.00f },   // MM: soft top, broad bottom
            { 1.12f, 0.96f, 1.05f, 1.30f },   // MC: bright, tight, more hiss
            { 1.05f, 1.12f, 1.30f, 1.10f },   // DJ: hot and heavy, more surface
        };

        const auto& generation = generationVoices[generationIndex];
        const auto& turntable = turntableVoices[turntableIndex];
        const auto& cartridge = cartridgeVoices[cartridgeIndex];

        for (auto* stage : { &vinylL, &vinylR })
        {
            stage->generationTopLoss = generation.topLoss;
            stage->generationBottomLift = generation.bottomLift;
            stage->generationNoiseScale = generation.noise;

            // The turntable scales the stage's own wander depth, so the same
            // VINYL SPEED setting wanders differently on each deck - which is
            // exactly what the control is for.
            stage->turntableWowScale = turntable.wowScale;
            stage->turntableStability = turntable.stability;

            stage->cartridgeTopGain = cartridge.top;
            stage->cartridgeBottomGain = cartridge.bottom;
            stage->cartridgeNoiseGain = cartridge.noise * generation.noise;
            stage->cartridgeHissGain = cartridge.hiss * generation.noise;

            // The turntable scales the wander the VINYL SPEED switch set, so the
            // deck's stability is what decides how much of it you actually hear.
            // Applied here rather than at the speed block because the voice is
            // only known once the selector has been read.
            stage->speedModulation *= turntable.stability;
            // And the WANDER RATE separately: a belt-driven platter does not
            // merely wander further, it wanders SLOWER, because the elastic belt
            // filters the motor's own speed variations. An idler is the opposite
            // - the wheel couples them through. Scaling the increment as well as
            // the depth is what makes the three decks three decks rather than
            // three amounts of the same wobble.
            stage->vinylWowIncrement *= turntable.wowScale;
        }
    }

    // The stage coefficients. Every frequency is converted with
    // onePoleCoefficientHz, so they mean the same thing at every sample rate.
    //   preamp low-cut  - the input transformer's roll-off, 40 Hz
    //   flux shelf      - the split the FLUX control lifts, 220 Hz
    //   wear loss       - the top-end the WEAR control removes, 6 kHz
    //   vinyl rumble    - the turntable's low-frequency floor, 30 Hz
    //   vinyl warmth    - the RIAA playback tilt's pivot, 400 Hz
    preampLowCutCoefficient = onePoleCoefficientHz (40.0f, engineSampleRate);
    fluxShelfCoefficient = onePoleCoefficientHz (220.0f, engineSampleRate);
    wearLossCoefficient = onePoleCoefficientHz (6000.0f, engineSampleRate);
    vinylRumbleCoefficient = onePoleCoefficientHz (30.0f, engineSampleRate);
    vinylWarmthCoefficient = onePoleCoefficientHz (400.0f, engineSampleRate);

    // The LO-FI mode's two constants. 3.2 kHz is the band limit - roughly a cheap
    // radio, and the most recognisable part of the mode - and the hold is four
    // samples, which is a 4x sample-rate reduction at any rate (it is expressed in
    // samples rather than in Hz so it stays the same reduction as the rate moves).
    lofiLowCoefficient = onePoleCoefficientHz (3200.0f, engineSampleRate);
    lofiHoldSamples = 4;

    // -------------------------------------------------------------------------
    //  The two equalisers.
    //
    //  The split coefficients are rebuilt here per block rather than only in
    //  prepareToPlay, because the engine's rate changes with the oversampling
    //  switch WITHOUT prepareToPlay running again - so an EQ prepared only once
    //  would have its 200 Hz and 4 kHz corners at the wrong frequencies the
    //  moment 4x was selected. One std::exp each per block is nothing.
    //
    //  The band gains come from the parameters, converted from dB to linear,
    //  and are fed to the smoothies so a drag glides. 0 dB converts to exactly
    //  1.0, which is what keeps a neutral EQ bit-for-bit transparent.
    // -------------------------------------------------------------------------
    inputEq.prepare (engineSampleRate);
    outputEq.prepare (engineSampleRate);

    // The two corners, orders and Q values, read once per block.
    //
    // The ORDER is a choice parameter, so its raw value is the INDEX into the
    // dB/octave list (0 = 6, 5 = 48), and the conversion to a slope happens in
    // ONE place here rather than in the filter - the filter wants a slope, the
    // panel wants a name, and this is the join between them.
    const auto eqOrderSlope = [] (std::atomic<float>* parameter) -> float
    {
        // Six entries: 6, 12, 18, 24, 36, 48. The list is not an arithmetic
        // series because 6, 12, 18, 24 are the useful everyday slopes and 36 and
        // 48 are the "get it out of the way" end - a straight 6..48 line would
        // spend three of its six positions on 30, 36 and 42, which nobody asks
        // for.
        static constexpr float slopes[6] { 6.0f, 12.0f, 18.0f, 24.0f, 36.0f, 48.0f };
        const auto index = parameter != nullptr
                               ? juce::jlimit (0, 5, static_cast<int> (parameter->load())) : 0;
        return slopes[index];
    };

    inputEq.prepareFilters (engineSampleRate,
                            inputEqHpFreqParam != nullptr ? inputEqHpFreqParam->load() : 20.0f,
                            inputEqLpFreqParam != nullptr ? inputEqLpFreqParam->load() : 20000.0f,
                            eqOrderSlope (inputEqOrderParam),
                            inputEqQParam != nullptr ? inputEqQParam->load() : 0.7f);
    // An explicit OFF on the switch wins over the corner's own travel-bypass.
    if (inputEqHpOnParam != nullptr && inputEqHpOnParam->load() < 0.5f)
        inputEq.hpActive = false;
    if (inputEqLpOnParam != nullptr && inputEqLpOnParam->load() < 0.5f)
        inputEq.lpActive = false;

    outputEq.prepareFilters (engineSampleRate,
                             outputEqHpFreqParam != nullptr ? outputEqHpFreqParam->load() : 20.0f,
                             outputEqLpFreqParam != nullptr ? outputEqLpFreqParam->load() : 20000.0f,
                             eqOrderSlope (outputEqOrderParam),
                             outputEqQParam != nullptr ? outputEqQParam->load() : 0.7f);
    if (outputEqHpOnParam != nullptr && outputEqHpOnParam->load() < 0.5f)
        outputEq.hpActive = false;
    if (outputEqLpOnParam != nullptr && outputEqLpOnParam->load() < 0.5f)
        outputEq.lpActive = false;

    const float inputEqMidCoefficient = inputEq.midBandCoefficient (engineSampleRate);
    const float outputEqMidCoefficient = outputEq.midBandCoefficient (engineSampleRate);

    inputEqLowSmoothed.setTargetValue (
        juce::Decibels::decibelsToGain (inputEqLowParam != nullptr ? inputEqLowParam->load() : 0.0f));
    inputEqMidSmoothed.setTargetValue (
        juce::Decibels::decibelsToGain (inputEqMidParam != nullptr ? inputEqMidParam->load() : 0.0f));
    inputEqHighSmoothed.setTargetValue (
        juce::Decibels::decibelsToGain (inputEqHighParam != nullptr ? inputEqHighParam->load() : 0.0f));
    outputEqLowSmoothed.setTargetValue (
        juce::Decibels::decibelsToGain (outputEqLowParam != nullptr ? outputEqLowParam->load() : 0.0f));
    outputEqMidSmoothed.setTargetValue (
        juce::Decibels::decibelsToGain (outputEqMidParam != nullptr ? outputEqMidParam->load() : 0.0f));
    outputEqHighSmoothed.setTargetValue (
        juce::Decibels::decibelsToGain (outputEqHighParam != nullptr ? outputEqHighParam->load() : 0.0f));

    // -----------------------------------------------------------------------
    //  MODERN mode: the machine re-voiced for a well-maintained 1990s deck.
    //
    //  It moves the COEFFICIENTS rather than the signal, because that is what
    //  actually differs about a modern machine: its head losses sit further out of
    //  the audio band, its floor is lower, and its magnetic memory is thinner
    //  because the tape is better. Applying these as block-rate scales means the
    //  mode changes the machine's calibration, not its level, so nothing else has
    //  to be recalibrated around it.
    // -----------------------------------------------------------------------
    modernHeadGapScale = modernMode ? 1.35f : 1.0f;
    modernHissScale = modernMode ? 0.35f : 1.0f;
    modernHysteresisScale = modernMode ? 0.75f : 1.0f;

    // The cabinet's poles. The roll-off sits where a 12-inch speaker's cone mass
    // puts it (about 5 kHz at the default cabinet setting, opening toward 9 kHz as
    // CABINET is turned down) and the resonance tracks the cabinet's own tuning,
    // which is the 80-120 Hz thump a closed box adds.
    const float cabinetHz = 9000.0f - cabinetAmount * 4000.0f;
    cabinetLowCoefficient = onePoleCoefficientHz (cabinetHz, engineSampleRate);
    // The resonance pole is deliberately much slower than the roll-off, so the
    // peak it adds is a broad lift rather than a narrow ring.
    cabinetPeakCoefficient = onePoleCoefficientHz (110.0f, engineSampleRate);
    // Presence is a high-pass split at 2.2 kHz: everything above it is the band the
    // feedback network lifts.
    presenceCoefficient = onePoleCoefficientHz (2200.0f, engineSampleRate);

    // The SAG envelope's two coefficients. Functions of the rate alone, so they are
    // block-rate constants like every other coefficient here - see the per-sample
    // loop, where rebuilding them was two std::exp per sample per channel.
    // 60 ms attack (the supply takes time to droop) and 240 ms release (it takes
    // longer to recover), which is the asymmetry that makes sag audible as "give"
    // rather than as a slow compressor.
    sagAttackCoefficient = onePoleCoefficient (60.0f, engineSampleRate);
    sagReleaseCoefficient = onePoleCoefficient (240.0f, engineSampleRate);

    // The safety limiter's four coefficients and its ceiling. All functions of the
    // rate alone - see the per-sample loop, where rebuilding them was four std::exp
    // per sample per channel.
    limiterDetectorAttack = onePoleCoefficient (0.5f, engineSampleRate);
    limiterDetectorRelease = onePoleCoefficient (80.0f, engineSampleRate);
    limiterCatchCoefficient = onePoleCoefficient (0.4f, engineSampleRate);
    limiterRecoveryCoefficient = onePoleCoefficient (120.0f, engineSampleRate);
    // The ceiling is a level, not a time constant, so it does not belong with the
    // four one-pole coefficients above - but it is set here so the hand-off point
    // and the coefficients that act on it are read from one place. It mirrors
    // softClip's knee, which is the only value that makes this stage's own comment
    // true, so that knee and this number have to move together: softClip declares
    // its knee as a function-local constexpr, so a shared constant would have to
    // be lifted out of a function the DSP harness cuts up on its own.
    limiterCeiling = 0.70f;

    // -------------------------------------------------------------------------
    //  Anti-phase guard coefficients.
    //
    //  Two windows, and the asymmetry between them is the whole behaviour:
    //
    //    - `antiPhaseCoefficient` is DELIBERATELY SLOW (about 400 ms). The guard
    //      must never react to a moment of genuine stereo - a hard-panned
    //      transient, a wide reverb tail - because those are real programme, not
    //      a fault. Only a SUSTAINED opposition is a fault, and 400 ms is long
    //      enough that a transient cannot trigger it and short enough that a
    //      genuinely inverted channel is corrected within a bar.
    //
    //    - the correction itself is applied through the same pole, so the rotation
    //      glides in and out rather than switching. A polarity flip that snapped
    //      would be a click, which is exactly the fault the guard exists to
    //      prevent.
    //
    // 400 ms, stated as a duration rather than as a reciprocal.
    antiPhaseCoefficient = onePoleCoefficient (400.0f, engineSampleRate);

    // The normaliser: the correlation is divided by a slow measure of the frame's
    // own energy, so the guard reads the RATIO of opposition to total energy rather
    // than an absolute number that would depend on the programme's level. It is
    // seeded here and tracked per frame inside the loop; the floor keeps a silent
    // passage from dividing by nothing.
    antiPhaseProductMagnitude = juce::jmax (1.0e-3f, antiPhaseProductMagnitude);

    // -------------------------------------------------------------------------
    //  Transport: handed to the rig.
    //
    //  Everything about the gesture - the three states, their durations, the
    //  deck's own settings that scale them, the momentary hold, and the
    //  auto-advance out of START - lives in TransportRig, because that is the
    //  part worth exercising on its own. What the engine does with it is follow
    //  `platter()`: the machine's level, its modulation depth and its speed
    //  response are all functions of that one number, and the gate below is
    //  where the machine's output stops.
    //
    //  The rig is handed SPEED (the tape speed), VINYL SPEED (the turntable) and
    //  the host tempo every block, because the three of them scale the gestures:
    //  a 30 ips machine takes longer to come up and to coast, a 78 rpm platter
    //  runs down for longer, and a take at 60 BPM moves twice as slowly as one at
    //  120. A host that offers no tempo leaves the rig on its 120 BPM anchor.
    //
    //  It is handed the host's TRANSPORT STATE as well. That is the one input
    //  to the transport that is not the deck's own: START pressed on a parked
    //  DAW arms the key rather than spinning the machine up, so a take begins
    //  with the machine engaging as the host rolls instead of a second and a
    //  half before it.
    //
    //  The state the rig settles on (START arriving at speed, or a hold released
    //  on a stopped machine) comes back as a request rather than as a host call:
    //  the atomics here are flushed to the parameter by the editor's timer, so no
    //  host notification ever happens on the audio thread.
    // -------------------------------------------------------------------------
    const bool spindownNow = spindownHeld.load (std::memory_order_relaxed);
    const auto vinylSpeedChoice = vinylSpeedParam != nullptr
                                      ? static_cast<int> (vinylSpeedParam->load()) : 0;
    transport.beginBlock (transportState, spindownNow, hostRolling, engineSampleRate,
                          speed, vinylSpeedChoice, hostTempoBpm);

    // The arm is published with the request rather than inside the per-frame loop:
    // it is block-granular state (the host rolls or it does not, for the whole
    // block), and the panel reads it to tell a START that is WAITING FOR THE DAW
    // from the two ways the machine can be at rest. Relaxed store, like the ramps
    // beside it: this is an advisory UI value.
    transportArmedPublished.store (transport.armedForHost, std::memory_order_relaxed);

    if (const auto requestedTransportState = transport.takeRequestedState();
        requestedTransportState >= 0)
    {
        transportHostSyncState.store (requestedTransportState, std::memory_order_relaxed);
        transportHostSyncRequested.store (true, std::memory_order_release);
    }

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
    const auto driveCurve = std::pow (drive, 1.45f);
    const auto biasCurve = std::pow (bias, 1.30f);
    const auto wowCurve = std::pow (wow, 1.55f);
    const auto flutterCurve = std::pow (flutter, 1.45f);

    // MIX carries no shaping curve of its own: its smoothed value IS the blend
    // position, converted to the raised-cosine dry/wet gains per sample below. Any
    // extra curve on it would only make the blend disagree with its own readout.
    const auto twoPi = juce::MathConstants<float>::twoPi;
    // The SPEED control's own character mapping, moved by the platter: a machine
    // that is winding down behaves the way a machine running slower does - less
    // band from its heads, slower wander, its bias following - which is what
    // makes a coast read as the transport losing speed rather than as a fade. At
    // speed this is exactly the SPEED control's number, so a running machine is
    // unchanged by it.
    const auto speedChoiceScale = (speed == 0) ? 0.76f : (speed == 1) ? 1.0f : 1.34f;
    const auto speedScale = speedChoiceScale * (0.76f + 0.24f * transport.platter());

    // The wow rate is tempo-locked. A machine's speed irregularities read as
    // musical when they land on the track's own timing, so the LFO is anchored to
    // the host tempo - one wander cycle per BAR at WOW's centre - and the WOW
    // control sweeps around that anchor instead of over an absolute rate. At 120
    // BPM the centre is 0.5 Hz; double the tempo and the machine wanders twice as
    // fast, which is what playing along with a faster track means for a deck.
    // Without a host tempo the 120 BPM default keeps the machine where it was.
    const auto barsPerSecond = static_cast<float> (hostTempoBpm) / 240.0f;
    const auto tempoWowAnchor = juce::jlimit (0.10f, 3.0f, barsPerSecond);
    const auto wowFreq = tempoWowAnchor * (0.30f + wowCurve * 1.40f) * speedScale;
    const auto flutterFreq = (1.9f + flutterCurve * 5.4f) * (1.0f + speedScale * 0.22f);

    // The TONE macro's blend curve, computed once per block. Declared BEFORE the tape
    // character section because the machine-state crossfade below is built from it:
    // 0 % reads as the classic slow stock, 100 % as the hot fast stock.
    const auto characterCurve = std::pow (
        juce::jlimit (0.0f, 1.0f, character), 1.20f);

    // Per-model tape character: saturation curve, bias asymmetry, tape noise floor,
    // head-gap damping and the magnetic hysteresis thickness. The damping is a real
    // corner FREQUENCY in Hz (the previous ms values landed at 4-9 Hz through the
    // time-constant filter - the same 2*pi unit error that hid the whole wet path).
    //
    // TONE (the character macro) is a crossfade BETWEEN MACHINE STATES, and the tape
    // formula is part of that state: at 0 % the machine behaves like the classic slow
    // stock (gentle curve, strong asymmetry, warm hiss, dark damping, thick hysteresis
    // blend) and at 100 % like the fast/hot stock (harder curve, less asymmetry, less
    // hiss, more open damping, leaner hysteresis). The selected TAPE TYPE shifts the
    // centre of that fade, so the knob still has its own meaning on top of the macro.
    // The slow-machine end is represented by the J37 numbers and the fast end by a
    // hotter generic stock, blended by the TONE curve.
    const float slowMachineCurve = 1.18f;      // J37 - soft magnetic bend
    const float slowMachineAsymmetry = 0.16f;  // strong even-harmonic warmth
    const float slowMachineHiss = 0.10f;       // quiet oxide floor (below audibility)
    const float slowMachineDampingHz = 12000.0f;
    const float slowMachineHysteresis = 0.30f;

    const float fastMachineCurve = 1.44f;      // hot-stud style dense saturation
    const float fastMachineAsymmetry = 0.10f;  // leaner, more symmetric bend
    const float fastMachineHiss = 0.06f;       // quieter still, faster stock
    const float fastMachineDampingHz = 20000.0f;
    const float fastMachineHysteresis = 0.44f;

    const auto machineBlend = juce::jlimit (0.0f, 1.0f, characterCurve);
    const auto blend = [machineBlend] (float slow, float fast)
    {
        return slow + (fast - slow) * machineBlend;
    };

    float tapeCurve = blend (slowMachineCurve, fastMachineCurve);
    float tapeAsymmetry = blend (slowMachineAsymmetry, fastMachineAsymmetry);
    float tapeHiss = blend (slowMachineHiss, fastMachineHiss);
    float headDampingHz = blend (slowMachineDampingHz, fastMachineDampingHz);
    float hysteresis = blend (slowMachineHysteresis, fastMachineHysteresis);

    // The TAPE TYPE switch keeps its own voice on top of the TONE macro: it biases the
    // blended state toward that formula's character (hotter formulas bend harder and
    // hiss less, the classic J37 stays soft) rather than replacing it.
    //
    // This switch is the third copy of the stock list and the only one the compiler
    // cannot check: C++ has no way to count case labels, so nothing here ties the
    // cases to tapeStockNames. Without an assert, a stock added to the table and
    // forgotten here would fall through to default and sound exactly like stock 0 -
    // a wrong tape rather than an error, which is the worst kind. tapeStockCount is
    // the same constant the parameter and the panel read, so this fires in a debug
    // build on the first block whenever the three stop agreeing.
    jassert (tapeType >= 0 && tapeType < tapeStockCount);

    // -------------------------------------------------------------------------
    //  TAPE TYPE = OFF: the magnetic medium is removed.
    //
    //  What is left is a solid-state amplifier: no hysteresis, no oxide floor,
    //  no head damping, no bias asymmetry - the machine's own electronics with
    //  no tape in it. It is the reference state for hearing what the MEDIUM
    //  contributes, because everything else in the chain is untouched by it.
    //
    //  It is implemented by (a) telling the saturation core that the tape
    //  principle is absent, so the blend redistributes its share among the
    //  principles still present, and (b) skipping the stock voicing switch
    //  entirely, which is what the early flag below does. BOTH are needed:
    //  the core alone would still have the stock's hiss and head damping riding
    //  on top of a tape-less blend.
    // -------------------------------------------------------------------------
    const bool tapeModelOff = (tapeType == modelOffIndex);
    saturationL.tapeOff = tapeModelOff;
    saturationR.tapeOff = tapeModelOff;

    if (tapeModelOff)
    {
        // A solid-state machine: no medium, so no medium noise and no head-gap
        // damping. The transport still runs - the electronics are still moving
        // the tape past a head - so wow and flutter stay, which is exactly the
        // distinction that makes this a useful reference rather than a bypass.
        tapeHiss = 0.0f;
        headDampingHz = 26000.0f;
        hysteresis = 0.10f;
        tapeAsymmetry = 0.0f;
    }
    else
    switch (tapeType)
    {
        case 0: // J37 - the classic EMI reference sound
            break;
        case 1: // Ampex 456 - hotter, more low-order colour
            tapeCurve += 0.08f;
            tapeAsymmetry += 0.05f;
            tapeHiss += 0.03f;
            headDampingHz += 2500.0f;
            hysteresis += 0.06f;
            break;
        case 2: // Studer A800 - darkest, densest saturation
            tapeCurve += 0.16f;
            tapeAsymmetry += 0.08f;
            tapeHiss += 0.06f;
            headDampingHz += 5000.0f;
            hysteresis += 0.12f;
            break;
        case 3: // Chrome - clean and bright, low noise
            tapeCurve += 0.02f;
            tapeAsymmetry -= 0.03f;
            tapeHiss -= 0.02f;
            headDampingHz -= 2500.0f;
            hysteresis -= 0.04f;
            break;
        case 4: // Type 111 - gentle low-noise mastering stock, very quiet, soft top
            tapeCurve -= 0.05f;
            tapeAsymmetry -= 0.02f;
            tapeHiss -= 0.045f;
            headDampingHz -= 4000.0f;
            hysteresis -= 0.05f;
            break;
        case 5: // GP9 - hot modern mastering formula: dense low end, higher floor
            tapeCurve += 0.14f;
            tapeAsymmetry += 0.07f;
            tapeHiss += 0.075f;
            headDampingHz += 6000.0f;
            hysteresis += 0.11f;
            break;
        case 6: // Quantegy 499 - high-output studio workhorse: open top, firm glue
            tapeCurve += 0.10f;
            tapeAsymmetry += 0.04f;
            tapeHiss += 0.045f;
            headDampingHz += 8000.0f;
            hysteresis += 0.08f;
            break;
        case 7: // RTM SM911 - broadcast reference: balanced, smooth, low noise
            tapeCurve += 0.05f;
            tapeAsymmetry += 0.02f;
            tapeHiss -= 0.01f;
            headDampingHz += 3500.0f;
            hysteresis += 0.03f;
            break;
        case 8: // SM 468 - high-output low-noise studio stock. A firm bend with a
                // notably quiet floor and an open head, so it reads as clean density
                // rather than as colour.
            tapeCurve += 0.09f;
            tapeAsymmetry += 0.03f;
            tapeHiss -= 0.055f;
            headDampingHz += 7000.0f;
            hysteresis += 0.04f;
            break;
        case 9: // 888 - the hot, thick vintage stock. Strong bias asymmetry and the
                // thickest magnetic memory here, so it bends early and blooms hard:
                // the formula to reach for when the saturation IS the effect.
            tapeCurve += 0.19f;
            tapeAsymmetry += 0.10f;
            tapeHiss += 0.05f;
            headDampingHz -= 1500.0f;
            hysteresis += 0.14f;
            break;
        case 10: // 815 - dark, dense and quiet at the top. A soft head and a heavy
                 // low-mid bias make it the warmest of the new stocks without the
                 // extra hiss 888 brings: it thickens rather than drives.
            tapeCurve += 0.13f;
            tapeAsymmetry += 0.08f;
            tapeHiss -= 0.02f;
            headDampingHz -= 4500.0f;
            hysteresis += 0.12f;
            break;
        case 11: // 811 - the clean, open, low-noise mastering stock. The gentlest bend
                 // of the set with the most open head, so it stays transparent under
                 // level and keeps the top octave: a bus stock, not a colour.
            tapeCurve -= 0.02f;
            tapeAsymmetry += 0.01f;
            tapeHiss -= 0.05f;
            headDampingHz += 9000.0f;
            hysteresis -= 0.03f;
            break;
        default:
            // Unreachable while the assert above holds: the choice parameter is
            // clamped to tapeStockCount, and the switch covers every index below it.
            // Present so the switch is exhaustive and a future entry cannot silently
            // do nothing.
            jassertfalse;
            break;
    }

    // The INSTRUMENT selector's voicing rides on top of the TAPE TYPE bias, and the
    // clamps below bound every figure, so any combination of the two selectors stays
    // inside the machine's designed operating range.
    float instrumentTransportScale = 1.0f;   // how loose the transport runs

    switch (instrument)
    {
        case 1: // Vocal: gentler bend, more even warmth, quiet floor, steady transport
            tapeCurve -= 0.10f;
            tapeAsymmetry += 0.04f;
            tapeHiss -= 0.03f;
            headDampingHz -= 1000.0f;
            hysteresis += 0.05f;
            instrumentTransportScale = 0.80f;
            break;
        case 2: // Bass: the thickest bend and memory, the darkest head, the steadiest
                // transport - pitch wobble under a bass note is heard at once
            tapeCurve += 0.12f;
            tapeAsymmetry += 0.06f;
            tapeHiss += 0.02f;
            headDampingHz -= 2500.0f;
            hysteresis += 0.10f;
            instrumentTransportScale = 0.65f;
            break;
        case 3: // Guitar: cleaner bend, more bite, a slightly loose vintage transport
            tapeCurve -= 0.06f;
            tapeAsymmetry -= 0.02f;
            tapeHiss -= 0.01f;
            headDampingHz += 1500.0f;
            hysteresis -= 0.04f;
            instrumentTransportScale = 1.15f;
            break;
        case 4: // Piano: the cleanest bend so transients and decay stay honest, the
                // quietest floor, top just nudged open
            tapeCurve -= 0.12f;
            tapeAsymmetry += 0.02f;
            tapeHiss -= 0.04f;
            headDampingHz += 500.0f;
            hysteresis -= 0.06f;
            instrumentTransportScale = 0.85f;
            break;
        case 5: // Drums: the slam calibration - a harder bend and an open head for
                // the crack, tight magnetic memory so transients do not smear, and
                // a steady transport (wow under a kick drum reads as a fault)
            tapeCurve += 0.10f;
            tapeAsymmetry -= 0.04f;
            tapeHiss += 0.01f;
            headDampingHz += 3000.0f;
            hysteresis -= 0.08f;
            instrumentTransportScale = 0.90f;
            break;
        case 0: // Master Bus: the machine exactly as calibrated
        default:
            break;
    }

    tapeCurve = juce::jlimit (1.0f, 1.8f, tapeCurve);
    tapeAsymmetry = juce::jlimit (0.0f, 0.4f, tapeAsymmetry);
    tapeHiss = juce::jlimit (0.0f, 0.6f, tapeHiss);
    headDampingHz = juce::jlimit (4000.0f, 26000.0f, headDampingHz);
    hysteresis = juce::jlimit (0.1f, 0.7f, hysteresis);

    // -------------------------------------------------------------------------
    //  Tape-type change.
    //
    //  Detected once per switch so the shaper's memory can be released at exactly
    //  that moment, and so the head-damping pole can be moved on the slow ramp instead
    //  of the 20 ms control ramp. See the note on activeTapeType in the header for why
    //  ramping the coefficients alone cannot make this continuous: the hysteresis term
    //  feeds the previous output back through a non-linear function, so stale memory
    //  inside a new curve is a step no amount of coefficient smoothing can remove.
    //
    //  The memory is faded rather than zeroed. Writing zeros would itself be a step -
    //  the curve would be evaluated against silence for one frame. Halving the three
    //  history slots lets the state unwind into the new curve over a few samples
    //  instead of jumping, which is inaudible at any level.
    // -------------------------------------------------------------------------
    const bool tapeTypeChanged = (activeTapeType != tapeType);

    if (tapeTypeChanged)
    {
        activeTapeType = tapeType;

        for (auto& memory : hystL) memory *= 0.5f;
        for (auto& memory : hystR) memory *= 0.5f;

        // Hold the slow ramp open long enough to cover the whole travel. The countdown
        // is in samples, so it scales with the rate like every other time constant.
        tapeTypeChangeCountdown = juce::roundToInt (engineSampleRate * 0.15f);
    }

    // The switch ramp is only consulted while its countdown is running. Outside that
    // window the ordinary control ramp owns the pole, so moving a knob keeps its 20 ms
    // response and only a formula change pays for the slower one.
    const bool tapeTypeSwitching = tapeTypeChangeCountdown > 0;

    if (tapeTypeSwitching)
        tapeTypeChangeCountdown -= numSamples;

    // DRIVE has no floor. It used to start at 0.28, which meant the signal was pushed
    // 38 % harder into the saturator even with the control at zero - a large part of why
    //    the plugin sounded overdriven at every setting. Now zero drive means unity gain
    // into the record head, so the machine is clean until the control asks it not to be.
    //
    // Compressor-coupled saturation: the smoothed squeeze of both glue stages (updated
    // once per block, see the end of processBlock) adds drive on top of the DRIVE
    // control. The harder the compressors work, the hotter the record head is run and
    // the harder the tape saturates - how a compressed signal hits a real machine.
    const float driveAmount = driveCurve * 1.9f + squeezeSaturationDrive * 1.15f;
    const float biasAmount = 0.18f + biasCurve * 1.55f;

    // Dry/wet blend. MIX is a genuine crossfade: at 0 % the signal is untouched dry
    // and at 100 % it is fully through the tape path. Previously the wet side had a
    // 12 % floor and the dry side was never fully removed, so MIX could not reach a
    // clean bypass or a fully saturated signal and its travel felt dead at the ends.
    // The gains themselves are derived per sample further down, from the smoothed MIX.
    const float wowDepth = wowCurve * (0.05f + speedScale * 0.08f)
                           * instrumentTransportScale;
    const float flutterDepth = flutterCurve * (0.08f + speedScale * 0.09f)
                               * instrumentTransportScale;
    const float speedBias = 0.84f + speedScale * 0.30f;

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
    const auto mechanismWear = juce::jlimit (0.0f, 1.0f,
                                             mechanicsSmoothed.getCurrentValue()
                                                 + wearSmoothed.getCurrentValue());

    // WEAR and MECHANICS deepen the transport's own wander. Both are already folded
    // into the per-sample modulation; this is the part they own on their own, so a
    // worn machine wanders even with WOW and FLUTTER closed - which is the one case
    // where the transport gate below is wrong to silence it, and is why this term is
    // added AFTER the gate is taken.
    const float wearWowCoupling = mechanismWear * 0.006f;

    // A hot record head is a noisier head: the hiss floor rises with the drive that
    // is actually reaching the tape, not with the DRIVE control alone, so it follows
    // the compressor-coupled driveAmount computed above.
    const float driveHissCoupling = juce::jlimit (0.0f, 1.0f, driveAmount * 0.35f);

    // A dark head gap hides the fine modulation before it hides anything else, so
    // the top of the TONE travel rolls the grain and the flutter off together. It
    // reads the TONE control directly rather than the cached toneLpAc coefficient:
    // that coefficient is only recomputed when tone or character actually move, so
    // on the first block it can still be 0 and would dampen the modulation before
    // the knob had been touched. The 0.55 floor means TONE can never silence the
    // transport's own movement, only soften it - which is what a biased head does
    // to pitch shimmer.
    const float toneNorm = juce::jlimit (0.0f, 1.0f, tone);
    const float headGapModulationMask = juce::jlimit (0.55f, 1.0f, 0.55f + toneNorm * 0.45f);

    // A plunger that is slowing loses speed before it loses signal. The whole point
    // of SPINDOWN is that the pitch goes first, so the platter speed is mapped
    // through a curve that stays near 1 until the platter is well below speed and
    // then falls away quickly: an exponent above 1 does that, and an exponent below
    // it would do the opposite - it would spend the modulation at the very start of
    // the run-down, where a machine is still moving and nothing should be happening
    // to it yet. The value is applied in the per-sample loop, where the platter
    // speed actually lives.
    constexpr float platterPitchExponent = 1.35f;

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
    constexpr float platterGateExponent = 2.5f;

    // A hard-driven machine also runs its transport more loosely: pushing a deck
    // that hard is a physical load, and the speed error is the same term the ramp
    // uses, read here as a small extra wander.
    const float loadInducedSpeedError = driveAmount * 0.004f;

    // The transport activity gate: 0 when both Wow and Flutter are closed, 1 from
    // roughly 50 % on either. The machine only makes a sound of any kind while its
    // transport moves, so EVERYTHING the transport does is gated by this one factor:
    // the tape-surface grain in the loop below (which used to run unconditionally
    // and read as a mystery noise generator whenever both controls were closed),
    // and - since the "hiss while paused" report - the noise floor as well, whose
    // gain is multiplied by it further down. Nothing in the engine is always-on.
    const float transportActivityGate = juce::jlimit (0.0f, 1.0f, (wowCurve + flutterCurve) * 2.0f);

    // Tone tilt: 0 = warm/soft, 1 = open/bright. The coefficients are cached by
    // updateToneCoefficients, which also derives the TONE-macro machine-state scalars
    // (head gap, pre-bias, flutter scale). They are rebuilt whenever either control
    // moves, and also whenever the sample rate changes (see
    // resetSampleRateDependentState).
    if (std::abs (tone - previousTone) > 1.0e-5f
        || std::abs (character - previousCharacter) > 1.0e-5f)
        updateToneCoefficients (tone, engineSampleRate);

    // Feed the ramps from the cached coefficients once per block; the per-sample loop
    // then reads getCurrentValue(), which advances the ramp one sample at a time. Only
    // these two gains are ramped - every other control either feeds a memoryless curve
    // (DRIVE, BIAS), an oscillator (WOW, FLUTTER) or an already-smoothed value
    // (INPUT, OUTPUT, MIX, WIDTH, BYPASS), none of which can step the waveform.
    toneShelfGainSmoothed.setTargetValue (toneShelfGain);
    toneShelfBoostSmoothed.setTargetValue (toneShelfBoost);
    preDriveGainSmoothed.setTargetValue (preDriveGain);

    // The magnetic curve's own two arguments ramp for the same reason: BIAS shifts the
    // whole transfer curve, so a per-block step is a hard discontinuity, not a zipper.
    const auto shaperDriveTarget = driveCurve * tapeCurve + hysteresis * 0.25f
                                     + squeezeSaturationDrive * 0.30f;
    const auto shaperAsymmetryTarget = tapeAsymmetry * (biasAmount * 0.42f);
    shaperDriveSmoothed.setTargetValue (shaperDriveTarget);
    shaperAsymmetrySmoothed.setTargetValue (shaperAsymmetryTarget);

    // The TONE curve used by the tilt stage below is the one computed above the tape
    // character section, so the head poles, the machine crossfade and the tilt all
    // read the same value.

    // The playback head poles are constants for the whole block (they only depend on
    // the controls and the rate), so they are built here once rather than per sample.
    // The head-gap pole is the TONE macro's own crossfade - slow/soft machine (4.3 kHz)
    // to fast/open machine (24 kHz) - divided by the selected speed's damping, so SPEED
    // and TONE keep their independent meaning on the same head.
    // MODERN mode opens the head losses up: the gap and damping corners move
    // further out of the audio band, which is what a better-maintained machine
    // actually does. The scale is 1.0 when the mode is off, so the tape machine is
    // bit-for-bit unchanged.
    const float headGapCoefficient = onePoleCoefficientHz (headGapHz * speedScale * modernHeadGapScale,
                                                           engineSampleRate);
    const float hfPostCoefficient = onePoleCoefficientHz (headDampingHz * speedScale * modernHeadGapScale,
                                                          engineSampleRate);

    // Tape hiss is a continuous noise floor, so its density is expressed per sample and
    // therefore scales with the sample rate.
    //
    // Two things are needed for this to stay correct up to 192 kHz:
    //
    //   1. The noise is BAND-LIMITED by the coefficient below rather than running white all
    //      the way to Nyquist. Real tape hiss comes from the medium and stops well short of
    //      the top octave; left white it would spread over 96 kHz at a 192 kHz rate, which
    //      sounds like a bright digital hiss instead of tape. Filtering it also means the
    //      noise occupies a fixed bandwidth at every rate, which is what makes point 2 work.
    //
    //   2. Because the bandwidth is now fixed, the gain no longer needs the sqrt(rate)
    //      compensation - that term existed only to keep total white-noise energy constant
    //      as more samples were added per second. Adding it on top of the band limit would
    //      double-compensate and the hiss would get louder as the rate went up, which is
    //      exactly the bug this replaces.
    // MODERN mode lowers the floor, which is the single most obvious difference
    // between an old machine and a well-kept one. 1.0 when the mode is off.
    // NOISE LVL is the trim that acts on the floors themselves. NOISE is the
    // mix of every noise source; this is the level both floors share. It rides
    // the same smoother family, so a drag glides rather than steps.
    const float hissGain = tapeHiss * 0.00042f * modernHissScale
                         * noiseLvlSmoothed.getCurrentValue()
                         // A narrower track has a proportionally higher noise
                         // floor for the same tape, because the signal it carries
                         // is smaller and the medium's noise is not. This is the
                         // same geometric fact that costs the low end below -
                         // one cause, two consequences.
                         * trackNoiseScale;

    // The floor is gated by the transport. Tape hiss exists only while the tape is
    // actually MOVING across the head: a machine at rest is silent, because the
    // oxide never passes the playback gap. When both Wow and Flutter are closed the
    // machine is at rest and the floor coasts down to silence; opening either
    // control spins it back up. The fade itself is the hiss ramp's own slow window
    // (750 ms, see resetSampleRateDependentState), so a stop between takes reads as
    // the machine coasting to rest rather than a mute-switch drop. This is the fix
    // for the "noise while paused with Wow and Flutter at zero" report: the floor
    // was the only ungated always-on source left in the engine.
    //
    // driveHissCoupling rides on top of that: a head driven harder is a noisier head,
    // so the floor rises with the drive actually reaching the tape. It is added to the
    // gate rather than to the gain, so a machine whose transport is at rest still
    // cannot hiss - the coupling only makes a moving machine louder, which is the
    // physical thing it models.
    const float gatedHissGain = hissGain * juce::jlimit (0.0f, 1.0f,
                                                        transportActivityGate + driveHissCoupling * 0.6f
                                                            * transportActivityGate);

    // The two slow random sources - the MECHANICS drift and the WEAR contact
    // noise - are advanced ONCE per block, not per sample. Both are sub-audio
    // (the drift targets change every 120 ms, the contact noise follows over about
    // half a second), so a block-rate update is not an approximation, it is the
    // correct rate: a per-sample update would compute the same number a thousand
    // times and then use it once.
    //
    // They share the tape noise generator rather than carrying their own, because
    // a second LCG would double the state for no benefit - the two sources are
    // already decorrelated by being sampled at different moments.
    const int blockSamples = juce::jmax (1, numSamples);
    tapeConditionL.updateSlowSources (mechanicsSmoothed.getCurrentValue(),
                                      wearSmoothed.getCurrentValue(),
                                      blockSamples, noiseState);
    tapeConditionR.updateSlowSources (mechanicsSmoothed.getCurrentValue(),
                                      wearSmoothed.getCurrentValue(),
                                      blockSamples, noiseState);

    // The NOISE control trims the floor on top of the formula's own figure: 0.5 is
    // unity, so the knob starts neutral and the stock's own character is unchanged
    // unless the user asks for a different floor.
    noiseTrimSmoothed.setTargetValue (juce::jlimit (0.0f, 2.0f, noiseAmount * 2.0f));
    noiseLvlSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, noiseLvlAmount));

    // ST LINK: 1.0 is fully linked (one shared detector per stage), 0.0 is fully
    // unlinked (two independent detectors). Ramped, because it scales the gain
    // difference between the two paths and a step there is a step in the waveform.
    const auto stLinkAmount = stLinkParam != nullptr ? stLinkParam->load() : 1.0f;
    stLinkSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, stLinkAmount));

    // Every control-derived coefficient the block needs is in scope by now, so the
    // ramps are fed once here and read per sample with getCurrentValue(): no
    // multiplier and no pole in the wet path can step from one block to the next.
    driveAmountSmoothed.setTargetValue (driveAmount);
    toneLpSmoothed.setTargetValue (toneLpAc);
    hfPostSmoothed.setTargetValue (hfPostCoefficient);
    headGapSmoothed.setTargetValue (headGapCoefficient);
    flutterScaleSmoothed.setTargetValue (flutterScale);

    // The switch ramp tracks the same pole but over a much longer window. It is only
    // read while tapeTypeSwitching is true, so feeding it every block costs nothing
    // when no formula change is happening.
    headDampingSwitchSmoothed.setTargetValue (hfPostCoefficient);
    hissGainSmoothed.setTargetValue (gatedHissGain);
    subFundamentalSmoothed.setTargetValue (subFundamentalParam != nullptr
                                               ? subFundamentalParam->load() : 0.0f);

    // -----------------------------------------------------------------------
    //  Noise floor.
    //
    //  The hiss is a constant, band-limited floor - the sound of the medium. It is
    //  deliberately NOT programme-dependent: tracking the signal would make the
    //  noise breathe with the music, and the hiss must sound the same in a pause as
    //  under the programme.
    // -----------------------------------------------------------------------

    // onePoleCoefficient takes MILLISECONDS, so the 16 kHz corner is converted to the
    // equivalent time constant first: 1 / (2*pi*f). Passing 16000 here would be read as a
    // 16-second time constant, which would all but remove the hiss instead of shaping it.
    const float hissBandLimit = onePoleCoefficient (1000.0f / (juce::MathConstants<float>::twoPi * 16000.0f),
                                                    engineSampleRate);

    // Playback AC coupling: an 8 Hz one-pole DC blocker, rebuilt per block from the
    // rate. The standard form is y = x - x1 + R*y; R = 1 - 2*pi*fc/rate puts the
    // corner exactly at fc Hz, and clamping keeps the arithmetic safe at any rate.
    const float dcBlockR = juce::jlimit (0.5f, 0.9999f,
                                         1.0f - (juce::MathConstants<float>::twoPi * 8.0f)
                                             / engineSampleRate);

    // Output staging: the loudness the model adds is balanced out here, so OUTPUT
    // is a clean, calibrated +/- dB trim rather than an extra hidden gain stage.
    //
    // This is the STATIC calibration only: it compensates for the fixed gain the
    // saturation curve adds at a nominal level. The level that is actually lost inside
    // the shaper as it clamps is tracked per sample in the tape loop (driveCompensation),
    // so the two do not fight each other - one sets the operating level, the other keeps
    // the stage gain-neutral as DRIVE and the signal level move.
    //
    // Recalibrated for the corrected shaper. The old constants assumed a stage that
    // saturated at every setting, so they were fighting a permanent loss; now that the
    // shaper is near-unity at zero drive and only compresses when pushed, the static trim
    // has to be close to unity as well or the plugin ends up quiet instead of clean.
    const float driveGainCompensation = 1.0f - driveCurve * 0.16f;

    // -----------------------------------------------------------------------
    //  Compressor-coupled saturation.
    //
    //  The two glue stages are part of the machine, so how hard THEY work changes
    //  how hard the tape saturates - the way pushing an already-compressed signal
    //  into a real record head makes it saturate sooner and bloom harder. The
    //  drive term below grows smoothly (200 ms smoothing, so it follows the
    //  programme's density rather than individual transients) with the total gain
    //  reduction both stages are currently applying.
    //
    //  The tape stage runs BEFORE the output compressor in the chain, so it cannot
    //  see that stage's current-block reduction; it uses the previous block's
    //  value instead, which at one-block latency is indistinguishable musically.
    // -----------------------------------------------------------------------
    // 200 ms, stated as a duration rather than as a reciprocal.
    const float squeezeDriveSmoothing = onePoleCoefficient (200.0f, engineSampleRate);

    // The static calibration stays as designed: the slow programme compensator after
    // the output stage already restores any residual broadband loss against the
    // post-INPUT reference, so no extra static boost is wanted here - with the tape
    // poles now in the audio range the passband is close to unity and the wet path
    // level-tracks the dry one at any MIX setting.
    const float finalOutputGain = 0.78f * driveGainCompensation * (0.94f + speedScale * 0.08f);

    // Continuous pseudo-random tape noise: a 32-bit LCG held per instance, so the
    // hiss is uncorrelated between instances and reproducible for a given one.
    // The floor itself is constant - see the note in the tape loop for why there is
    // no programme-dependent levelling.
    const auto nextNoise = [] (std::uint32_t& state) -> float
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<float> ((state >> 8) & 0x00ffffffu) * (1.0f / 8388608.0f) - 1.0f;
    };

    const int activeChannels = activeInputChannels;

    // Scratch storage for the IR stage's wet capture. It is reserved in
    // prepareToPlay; do not resize a thread_local AudioBuffer in processBlock,
    // because a larger host block would allocate on the audio thread.
    //
    // The stage processes the WET signal only: in DELTA listen the difference
    // is taken against the dry reference further down, so an IR blended in at
    // MIX 100 still shows as "what the machine adds" rather than as the room
    // the dry signal never passed through.
    auto& scratch = postFxScratch;

    // Working pointers into the block's own storage. In the plain path these are
    // the host buffer's channels; in the oversampled path they are the oversampler's
    // internal buffer. The engine is written against these pointers only.
    std::array<float*, 2> channelData {};
    for (int channel = 0; channel < activeChannels; ++channel)
        channelData[static_cast<std::size_t> (channel)] = block.getChannelPointer (static_cast<std::size_t> (channel));

    float inputPeak = 0.0f;
    float outputPeak = 0.0f;
    double inputSquares = 0.0;
    double outputSquares = 0.0;
    float peakReductionDb = 0.0f;
    float inputPeakReductionDb = 0.0f;
    float inputEnvelopeActivity = 0.0f;
    float driftAccumulator = 0.0f;

    // Reference power for the final gain compensation: the power of the signal straight
    // after the INPUT trim, before the first glue compressor touches it. That is the
    // level the user dialled in with INPUT, so it is what the plugin's output should
    // still be tracking once the tape stage and both compressors have had their way
    // with the signal. Accumulated across this block only - the correction it drives is
    // itself smoothed, so no block-to-block history is needed here.
    float referenceBlockPower = 0.0f;

    // Loudness metering accumulators for this block. currentLufs is carried out of the
    // per-sample loop because the K-weighted follower is stateful across the whole block.
    float currentLufs = -70.0f;
    bool clippingThisBlock = false;
    float currentInputLufs = -70.0f;
    bool inputClippingThisBlock = false;

    // The input side of the K-weighted meter needs the raw signal, but the buffer is
    // processed in place, so the dry input of the current sample is stashed here before
    // the channel loop overwrites it. Two slots are enough for a stereo frame.
    std::array<float, 2> inputChainHistory {};

    // Slow one-pole coefficient for the compensation itself. Deliberately far slower
    // than either compressor, so the correction settles on the programme level instead
    // of pumping along with the transients.
    // 450 ms, stated as a duration rather than as a reciprocal.
    const float compensationCoefficient = onePoleCoefficient (450.0f, engineSampleRate);

    // VU ballistics (300 ms) for the input and output meters. The coefficient is a
    // constant for the whole block - it depends only on the sample rate - so it is
    // computed once here instead of re-evaluating an exp() twice per sample inside
    // the loop below.
    // 300 ms, stated as a duration rather than as a reciprocal.
    const float vuBallisticCoefficient = onePoleCoefficient (300.0f, engineSampleRate);

    // -------------------------------------------------------------------------
    //  Glue compressor operating points.
    //
    //  The two stages are independent processors, but each one is calibrated from
    //  the trim control that feeds it. Turning INPUT up pushes this scaled signal
    //  into the input stage, so that stage clamps down sooner and more firmly;
    //  OUTPUT works the same way on the output stage, which then spends the trimmed
    //  level on the output trim. A negative trim backs a stage off completely and a
    //  positive one leans on it hard, which is what makes the two controls feel
    //  like real gain staging rather than plain volume.
    // -------------------------------------------------------------------------
    const float inputDriveLoad = juce::jlimit (0.0f, 1.0f, (inputDb + 24.0f) / 48.0f);
    const float outputDriveLoad = juce::jlimit (0.0f, 1.0f, (outputDb + 24.0f) / 48.0f);

    const float inputThresholdDb = -17.0f + inputDriveLoad * 14.0f;    // -17 .. -3 dB
    const float inputKneeDb = 10.0f - inputDriveLoad * 3.0f;           // 10 .. 7 dB
    const float inputCompressorRatio = 1.15f + inputDriveLoad * 0.25f; // 1.15 .. 1.40 : 1
    const float inputReductionLimitDb = -(2.0f + inputDriveLoad * 5.0f);

    const float outputThresholdDb = -18.0f + outputDriveLoad * 15.0f;  // -18 .. -3 dB
    const float outputKneeDb = 7.0f - outputDriveLoad * 2.0f;          // 7 .. 5 dB
    const float outputCompressorRatio = 1.16f + outputDriveLoad * 0.18f;
    const float outputReductionLimitDb = -(2.5f + outputDriveLoad * 5.0f);

    // -----------------------------------------------------------------------
    //  Tape-stock and transport coupling into the glue time constants.
    //
    //  The compressors are part of the machine, so the FORMULA loaded on it and
    //  the SPEED it runs at change how the glue stages move - not just how the
    //  tape saturates:
    //
    //    Tape formula (0 = J37 ... 3 = Chrome). Oxide thickness and bias current
    //    set how quickly the detector can follow the programme: the soft, low-
    //    output formulas get slower constants (more head bump, more relaxed
    //    glue), the hot and chrome formulas get faster, tighter ones.
    //
    //    Transport speed. A slow 7.5 ips pass has more print-through and a
    //    lazier flux build-up, so the glue is stretched; 30 ips is tight and
    //    immediate, so the constants shorten. This rides the same speedScale
    //    used for the head damping, so SPEED keeps one coherent meaning across
    //    the whole machine.
    //
    //  Attack multipliers land roughly in 0.72..1.33, release in 0.72..1.40.
    //  Both stages share the multipliers, so the two stages still feel like one
    //  machine while remaining independent processors.
    // -----------------------------------------------------------------------
    // Eight formulas now index this scale: type 0 (J37, softest glue) through
    // type 7 (RTM SM911), linearly between. The jlimit keeps the ends exact.
    const float stockAttackScale = juce::jlimit (0.95f, 1.34f,
                                                 1.34f - 0.047f * static_cast<float> (tapeType));
    const float stockReleaseScale = juce::jlimit (0.95f, 1.40f,
                                                  1.40f - 0.064f * static_cast<float> (tapeType));
    const float transportAttackScale = 1.32f - 0.22f * speedScale;                  // 1.14 -> 1.03
    const float transportReleaseScale = 1.38f - 0.30f * speedScale;                 // 1.15 -> 0.98

    const float inputAttackSeconds = 0.16f * stockAttackScale * transportAttackScale;
    const float inputReleaseSeconds = 0.85f * stockReleaseScale * transportReleaseScale;
    const float outputAttackSeconds = 0.20f * stockAttackScale * transportAttackScale;
    const float outputReleaseSeconds = 1.00f * stockReleaseScale * transportReleaseScale;

    // The two glue detectors' block-rate coefficients. Everything they depend on -
    // the engine rate, the four base constants above and the trim-driven load - is
    // fixed for the whole block, so they are built once here rather than recomputed
    // per sample. Before this the detector cost two std::exp calls per sample per
    // channel for values that cannot change inside the block.
    const auto inputCompressorCoefficients = GlueCompressor::makeCoefficients (
        engineSampleRate, inputAttackSeconds, inputReleaseSeconds, inputDriveLoad);
    const auto outputCompressorCoefficients = GlueCompressor::makeCoefficients (
        engineSampleRate, outputAttackSeconds, outputReleaseSeconds, outputDriveLoad);

    // The K-weighted meters' 400 ms window coefficients, same reasoning: a function
    // of the rate alone, so two std::exp calls per frame per meter become two per
    // block.
    const auto outputLoudnessCoefficient = LoudnessMeter::makeWindowCoefficient (engineSampleRate);
    const auto inputLoudnessCoefficient = LoudnessMeter::makeWindowCoefficient (engineSampleRate);

    // Makeup is part of each stage, and it is driven by the same trim control: a
    // boosted trim pays its reduction back and a trimmed-down one simply backs off,
    // so neither stage can quietly undo the balance the user dialled in.
    const float inputDegree = juce::jlimit (0.0f, 1.0f, (inputDb + 16.0f) / 32.0f);
    const float outputDegree = juce::jlimit (0.0f, 1.0f, (outputDb + 16.0f) / 32.0f);
    const float inputMakeupFraction = 0.30f + inputDegree * 0.35f;
    const float outputMakeupFraction = 0.35f + outputDegree * 0.35f;

    const auto processOneSample = [&] (int sample) noexcept
    {
        // -----------------------------------------------------------------
        //  SAMPLE-LEVEL CONTROL READS.
        //
        //  Every control that the hot path depends on is read ONCE per sample
        //  from a single advancing sample clock, in the order below. The block-
        //  rate feed points live above this lambda; this lambda only READS, it
        //  never feeds a target. That split is the rule: block rate sets up the
        //  ramp, sample rate consumes it. If a control is read here it must be
        //  read from the same per-sample value everywhere else in the frame.
        //
        // INPUT owns the shared sample-clock advance; the following reads advance
        // their individual ramps lazily for this same frame.
        const float inputGain = inputGainSmoothed.getNextValue();
        const float outputGain = outputGainSmoothed.getCurrentValue();
        const float currentWidth = widthSmoothed.getCurrentValue();
        const float mixNow = juce::jlimit (0.0f, 1.0f, mixSmoothed.getCurrentValue());
        const float mixAngle = mixNow * juce::MathConstants<float>::halfPi;
        const float dryGain = std::cos (mixAngle);
        const float wetGain = std::sin (mixAngle);
        const float bypassMix = bypassSmoothed.getCurrentValue();
        const float deltaMix = deltaListenSmoothed.getCurrentValue();
        const float driveNow = shaperDriveSmoothed.getCurrentValue();
        const float asymmetryNow = shaperAsymmetrySmoothed.getCurrentValue();
        const float diNow = diSmoothed.getCurrentValue();
        const float diLoadNow = diLoadSmoothed.getCurrentValue();
        const float diTransformerNow = diTransformerSmoothed.getCurrentValue();
        const float distortionNow = distortionSmoothed.getCurrentValue();
        const float preampNow = preampSmoothed.getCurrentValue();
        referenceBlockPower = 0.0f;
        // Optional hot-path activity probe. Behind the DSP harness guard so the
        // release and the harness both compile, and behind an explicit enable so
        // it never runs in a normal build. The probe is deliberately read-only and
        // allocation-free: it exists to make the hot loop observable, not to change
        // it. It sits after the sample clock has advanced and after MIX has been
        // read, so its reads are peeks and no smoother can advance twice a sample.
#if ! defined (J37_DSP_HARNESS) && defined (J37_ENABLE_HOTPATH_PROBE)
        ++hotPathSampleCount;
        hotPathThisSampleActive = (mixNow > 1.0e-5f)
                                 || (driveNow > 1.0e-5f)
                                 || (subFundamentalSmoothed.getCurrentValue() > 1.0e-5f);
#endif

        // ------------------------------------------------------------------
        //  The transport's own frame.
        //
        //  One advance per FRAME and one value used for the whole frame, so both
        //  channels ride the same capstan - advancing a ramp per channel would put
        //  the sides a sample apart, which is a channel skew, not a transport. The
        //  rig owns the arithmetic and the gesture; this is the only place the
        //  engine touches it inside the loop.
        // ------------------------------------------------------------------
        transport.advanceFrame();

        // The platter's actual speed, which is what the machine's level, its
        // modulation depth and its reels all follow. It is the PRODUCT of the
        // capstan ramp and the run-down, which is what makes a spindown from a
        // stopped transport and a stop during a spindown land in the same place.
        const float platterSpeed = transport.platter();

        // How much of the machine's own motion reaches the signal: a transport
        // below speed runs flat, which is what makes the ramp read as a machine
        // engaging rather than as a fade-in. A spindown drives the same term the
        // other way, so the machine goes slack as the platter coasts.
        //
        // The curve is raised to platterPitchExponent so the term holds near speed
        // until the platter is genuinely slow, then falls away quickly. That is what
        // makes a coast read as a machine losing its drive rather than as a
        // fade-out, and it is why the exponent has to be ABOVE 1: below 1 the
        // machine would go slack the instant the button went down.
        //
        // What this deliberately does NOT do is move the programme's PITCH. That is
        // not this block's job and never was: everything here is a GAIN on one
        // number, the platter's speed, and a multiplier cannot bend a pitch - it
        // can only make the signal smaller. The bend belongs to the repitch stage
        // at the end of the chain, which reads the finished frame back at that
        // same number and is the one place the FORMAT of the output changes rather
        // than its level. Keeping the two apart is what lets this gate stop the
        // sound outright without the bend having to be undone first.
        const float pitchedPlatter = std::pow (platterSpeed, platterPitchExponent);
        const float speedError = (1.0f - (1.0f - pitchedPlatter) * 0.7f)
                               * (1.0f - loadInducedSpeedError);

        // What the machine passes while it is turning, 0 at rest. See
        // platterGateExponent: this is the multiplier the output stage's two legs
        // are both gated by, and it is why STOP is silence rather than a fade.
        const float platterGate = std::pow (platterSpeed, platterGateExponent);

        // Publish the platter speed and the run-down alone for the editor's reels
        // and momentary button. Relaxed stores: these are advisory UI values, and
        // the UI reads them with relaxed loads, so there is nothing to synchronise.
        transportRampPublished.store (platterSpeed, std::memory_order_relaxed);
        spindownRampPublished.store (transport.spindown, std::memory_order_relaxed);

        std::array<float, 2> tapeOutput {};

        // The machine's DRY leg, for DELTA listen: the signal after the input trim
        // and the input glue compressor and nothing else - the same `x` the raised-
        // cosine MIX crossfade feeds its dry side further down. Held per channel so
        // it can be scaled by stageGain once that gain is known, which it is not at
        // this point in the frame.
        std::array<float, 2> machineDryInput {};

        // The SUBFUND undertones, one per channel. They are generated from the
        // shaper's output down in the tape loop but deliberately NOT summed into
        // the tape path: everything between there and here is a nonlinearity that
        // would distort them into harmonics of themselves. They join the finished
        // signal further down instead, where the remaining stages are linear or
        // gain-only.
        std::array<float, 2> undertoneOutput {};

        // ------------------------------------------------------------------
        //  Input stage glue compressor, detected ONCE per frame.
        //
        //  It sits straight after the input trim, so the signal that reaches the
        //  tape is always the controlled one and the INPUT control is what drives
        //  this stage - exactly like hitting a recorder input harder. Fully
        //  independent of the output stage: this detector only ever sees the
        //  trimmed input.
        //
        //  It used to run inside the per-channel loop, once per channel, on that
        //  channel's own power. In stereo that is a detector advancing twice per
        //  frame, so every attack and release constant silently halved, and each
        //  channel taking its gain from an envelope the other channel had already
        //  moved - the two sides compressed at slightly different instants, which
        //  is where stereo image wander on a transient comes from. The gain is now
        //  computed once, from the power averaged over the channels, and both
        //  sides are given the same one. This is the arrangement the OUTPUT stage
        //  compressor below has always used.
        // ------------------------------------------------------------------
        std::array<float, 2> inputTrimmedByChannel {};
        float inputDetectorPower = 0.0f;
        for (int channel = 0; channel < activeChannels; ++channel)
        {
            const auto raw = channelData[static_cast<std::size_t> (channel)][sample];
            const auto trimmed = raw * inputGain;
            inputTrimmedByChannel[static_cast<std::size_t> (channel)] = trimmed;
            inputDetectorPower += trimmed * trimmed;
        }
        inputDetectorPower /= static_cast<float> (juce::jmax (1, activeChannels));

        // ------------------------------------------------------------------
        //  Input stage glue, LINKED or per-channel.
        //
        //  The shared detector always runs: it is the linked answer, and it is the
        //  path that has to stay warm so moving ST LINK cannot resume it from a
        //  stale envelope. When ST LINK is below 100 % each channel's own detector
        //  is advanced too, on that channel's own power, and the control blends
        //  the two gains.
        //
        //  Both paths use the same coefficients, so the only thing that differs is
        //  WHICH signal the detector sees: the channel average, or one side. That
        //  is exactly the difference between a bus compressor and a dual-mono one.
        //
        //  The blend happens per channel below, where each side's gain is applied,
        //  because the unlinked answer is different for L and R.
        // ------------------------------------------------------------------
        const float inputEnvelopeDb = inputCompressor.processDetection (
            inputDetectorPower, engineSampleRate, inputCompressorCoefficients);
        const float linkedReductionDb = juce::jmax (inputReductionLimitDb,
                                                    softKneeReductionDb (inputEnvelopeDb,
                                                                         inputThresholdDb,
                                                                         inputKneeDb,
                                                                         inputCompressorRatio));
        const float inputReductionDb = linkedReductionDb;
        const float linkNow = stLinkSmoothed.getCurrentValue();

        // The per-channel reductions, valid only when the control is actually
        // asking for them. At full link this array is unused and the detectors are
        // left alone, so the default path costs nothing extra.
        std::array<float, 2> inputPerChannelReductionDb {};
        if (linkNow < 0.999f)
        {
            for (int channel = 0; channel < activeChannels; ++channel)
            {
                const auto trimmed = inputTrimmedByChannel[static_cast<std::size_t> (channel)];
                const auto channelEnvelopeDb = inputCompressorChannels[static_cast<std::size_t> (channel)]
                    .processDetection (trimmed * trimmed, engineSampleRate,
                                       inputCompressorCoefficients);
                inputPerChannelReductionDb[static_cast<std::size_t> (channel)] =
                    juce::jmax (inputReductionLimitDb,
                                softKneeReductionDb (channelEnvelopeDb, inputThresholdDb,
                                                     inputKneeDb, inputCompressorRatio));
            }
        }

        const float inputCompressionGain = juce::Decibels::decibelsToGain (linkedReductionDb);

        inputPeakReductionDb = juce::jmin (inputPeakReductionDb, inputReductionDb);
        inputEnvelopeActivity = juce::jmax (inputEnvelopeActivity,
                                            inputCompressor.getEnvelopeActivity());

        for (int channel = 0; channel < activeChannels; ++channel)
        {
            auto& wowPhase = channel == 0 ? wowPhaseL : wowPhaseR;
            auto& flutterPhase = channel == 0 ? flutterPhaseL : flutterPhaseR;
            auto& hysteresisMemory = channel == 0 ? hystL : hystR;
            auto& highFreqMemory = channel == 0 ? highFreqL : highFreqR;

            const float rawInput = channelData[static_cast<std::size_t> (channel)][sample];
            inputPeak = juce::jmax (inputPeak, std::abs (rawInput));
            inputSquares += static_cast<double> (rawInput) * rawInput;
            inputChainHistory[static_cast<std::size_t> (channel)] = rawInput;

            // Input-side VU ballistic and clipping, measured on the raw signal arriving
            // at the plugin so the INPUT meter shows what the host is actually sending.
            inputVuAverage += (std::abs (rawInput) - inputVuAverage) * vuBallisticCoefficient;
            if (std::abs (rawInput) > 1.0f)
                inputClippingThisBlock = true;

            // The trimmed input for this channel, and this frame's compressor gain
            // for both of them, as computed once above the loop.
            const float inputTrimmed = inputTrimmedByChannel[static_cast<std::size_t> (channel)];

            // Track the power of the post-INPUT, pre-first-compressor signal, summed
            // over every active channel and averaged right after the loop. The old
            // first-channel-only reference was a different scale from the averaged
            // post-compressor power it is compared against, so a hard-panned right
            // channel could hide a genuine level loss and switch the compensation off.
            referenceBlockPower += inputTrimmed * inputTrimmed;

            // The glue gain for THIS channel. At full link it is the shared
            // reduction, as it always was; below that it is a blend toward this
            // channel's own reduction, so a loud left side ducks only the left.
            float inputCompressionGainThisChannel = inputCompressionGain;
            if (linkNow < 0.999f)
            {
                const auto unlinkedGain = juce::Decibels::decibelsToGain (
                    inputPerChannelReductionDb[static_cast<std::size_t> (channel)]);
                inputCompressionGainThisChannel = inputCompressionGain
                                                + (unlinkedGain - inputCompressionGain)
                                                      * (1.0f - linkNow);
            }

            float x = inputTrimmed * inputCompressionGainThisChannel;

            // ------------------------------------------------------------------
            //  Preamp and distortion - the two gain stages in FRONT of the machine.
            //
            //  They run here, on the trimmed and glue-compressed input, so the tape
            //  stage hears what they produced. That ordering is the whole point:
            //  a distorted signal recorded to tape sounds like a record rather than
            //  a pedal precisely because the machine smooths what the pedal did.
            //
            //  Both are level-matched internally (see InputStage), so neither
            //  changes the operating level - the INPUT control remains the thing
            //  that sets it, and these two only change the character.
            // ------------------------------------------------------------------
            auto& inputStage = channel == 0 ? inputStageL : inputStageR;

            // ------------------------------------------------------------------
            //  THE DI BOX, first in the chain.
            //
            //  First for a physical reason, not an arbitrary one: a DI box is
            //  what the instrument is plugged INTO, so everything after it - the
            //  distortion, the preamp, the saturation curve, the glue stages -
            //  hears a signal the box has already loaded, padded and coloured.
            //  That ordering is what makes the DI a real part of the chain
            //  rather than a tone control: a padded signal drives the tape curve
            //  differently from an unpadded one at the same level.
            //
            //  It runs before the IN EQ as well, because on the hardware the
            //  box is physically between the instrument and everything else.
            // ------------------------------------------------------------------
            if (diNow > 1.0e-5f)
            {
                x = inputStage.processDiBox (x, diNow, diLoadNow,
                                             diPadDb, diTransformerNow,
                                             diLoadCoefficient, diTransformerCoefficient,
                                             diHumIncrement);
            }

            if (distortionNow > 1.0e-5f)
                x = inputStage.processDistortion (x, distortionNow);

            if (preampNow > 1.0e-5f)
                x = inputStage.processPreamp (x, preampNow, preampLowCutCoefficient);
            // ------------------------------------------------------------------
            //  IN EQ - the input equaliser, and the position is the point.
            //
            //  It sits here, after the trim and the two input stages and BEFORE
            //  the tape, so what it shapes is what the machine HEARS. That makes
            //  it a different control from the output EQ: lifting the low end
            //  here pushes more low end into the saturation curve and the glue
            //  compressors, so the result is not merely a bass boost - the machine
            //  processes that bass, saturates on it and compresses it, and the
            //  character of the whole chain changes with it. This is the EQ an
            //  engineer uses to feed the recorder what it wants.
            //
            //  `machineDryInput` is captured BEFORE the EQ so that DELTA still
            //  subtracts the machine's own dry reference and not the EQ'd version
            //  of it - otherwise a neutral EQ would be invisible in delta while a
            //  boosted one would read as "character", which it is not.
            // ------------------------------------------------------------------
            machineDryInput[static_cast<std::size_t> (channel)] = x;

            inputEq.lowGain = inputEqLowSmoothed.getCurrentValue();
            inputEq.midGain = inputEqMidSmoothed.getCurrentValue();
            inputEq.highGain = inputEqHighSmoothed.getCurrentValue();
            x = inputEq.process (x, channel, inputEqMidCoefficient);

            const float wowLfo = std::sin (wowPhase);
            const float flutterLfo = std::sin (flutterPhase);
            const float grainLfo = std::sin (wowPhase * 0.8f + flutterPhase * 1.3f
                                             + static_cast<float> (channel) * 2.4f);

            wowPhase += (twoPi * wowFreq) / engineSampleRate;
            flutterPhase += (twoPi * flutterFreq) / engineSampleRate;
            // Wrap rather than a single subtraction: at very low rates, or with a
            // high wow/flutter setting, one increment can exceed a full turn and a
            // lone `-= twoPi` would leave the phase running away unbounded.
            wowPhase = std::fmod (wowPhase, twoPi);
            flutterPhase = std::fmod (flutterPhase, twoPi);

            if (channel == 0)
                driftAccumulator = wowLfo * wowDepth + flutterLfo * flutterDepth;

            // Transport speed modulation: wow is a slow pitch wander, flutter a fast
            // shimmer, and the grain term adds the fine tape-surface texture. The grain
            // is gated by the transport's actual activity: with Wow AND Flutter both at
            // zero the machine is mathematically still, so no modulation of any kind
            // reaches the signal.
            // Every transport term is scaled by speedError, which is below 1 while
            // START spins the capstan up. That is what turns the transport ramp into a
            // PITCH ramp rather than a level fade: the modulation deepens and the whole
            // wet path runs flat until the machine reaches speed.
            // MECHANICS and WEAR both add irregularity on top of the periodic wow
            // and flutter, and they are different mechanisms: MECHANICS is the
            // transport (dry bearings, a slack belt), WEAR is the medium (patchy
            // contact against the head). Both are folded in as extra depth rather
            // than as separate oscillators, because that is what they do to the
            // modulation that already exists - they make it less even, not more.
            const float mechanicsNow = mechanicsSmoothed.getCurrentValue();
            const float wearNowMod = wearSmoothed.getCurrentValue();
            // Two couplings meet here:
            //   - wearWowCoupling, the part WEAR/MECHANICS own on their own, so a
            //     worn machine wanders even with WOW and FLUTTER closed;
            //   - headGapModulationMask, the dark-head damping that softens the
            //     pitch shimmer at the top of the TONE travel before it softens
            //     anything else. Both are block-rate constants.
            const float irregularDepth = wowDepth
                                       + mechanicsNow * 0.006f
                                       + wearNowMod * 0.003f
                                       + wearWowCoupling;
            const float irregularOffset = (channel == 0 ? tapeConditionL : tapeConditionR).driftState
                                        * mechanicsNow * 0.010f;

            const float wowMod = (1.0f + wowLfo * irregularDepth + irregularOffset)
                                 * headGapModulationMask * speedError;
            const float flutterMod = (1.0f + flutterLfo * flutterDepth
                                        * flutterScaleSmoothed.getCurrentValue())
                                     * headGapModulationMask * speedError;
            const float grainMod = 1.0f + tapeHiss * 0.10f * transportActivityGate * grainLfo;

            // Record head: pre-emphasis, tape bias offset and drive. With DRIVE at zero
            // this is exactly unity, so the saturator sees the signal at the level the
            // user dialled in rather than a pre-boosted version of it. The TONE macro
            // adds the slow-machine pre-bias on top, scaled by the speed's own bias so
            // the two controls multiply naturally instead of fighting.
            const float preDrive = x * (1.0f + driveAmountSmoothed.getCurrentValue() * 1.2f
                                          * speedBias * preDriveGainSmoothed.getCurrentValue());
            // Magnetic hysteresis with memory - the core of the tape sound. The
            // squeeze term in the slope is the compressor coupling: dense, compressed
            // programme literally thickens the magnetic curve, not just its level.
            // Both arguments come from the ramps, so DRIVE and BIAS morph the curve
            // continuously instead of stepping it on every block boundary - BIAS was
            // the loudest of all, because its asymmetry term shifts the whole curve
            // rather than merely scaling it.
            // ------------------------------------------------------------------
            //  The saturation core.
            //
            //  This replaces the single magneticHysteresis call. It runs the same
            //  DRIVE and BIAS arguments the old shaper took - so both controls keep
            //  their meaning and their existing calibration - but weights seven
            //  distinct curve shapes together: tape, valve, cassette, vinyl, amp,
            //  transformer and digital. See SaturationCore for what each one is
            //  and why they are genuinely different mechanisms rather than seven
            //  settings of one.
            //
            //  The weights come from the SMOOTHED SOURCE controls, so moving a
            //  SOURCE knob morphs the harmonics continuously instead of stepping
            //  between seven curves on a block boundary.
            //
            //  SAG acts on the DRIVE, not on the output, because that is what the
            //  supply does: it is the gain that droops under sustained demand. The
            //  sag envelope follows the signal's own power, so a held chord gives
            //  and then recovers while a short transient never moves it - which is
            //  exactly how a real amplifier behaves.
            // ------------------------------------------------------------------
            auto& saturation = channel == 0 ? saturationL : saturationR;
            auto& amp = channel == 0 ? ampL : ampR;

            saturation.setWeights (tapeSourceSmoothed.getCurrentValue(),
                                   valveSourceSmoothed.getCurrentValue(),
                                   cassetteSourceSmoothed.getCurrentValue(),
                                   vinylSourceSmoothed.getCurrentValue(),
                                   ampSourceSmoothed.getCurrentValue(),
                                   transformerSourceSmoothed.getCurrentValue(),
                                   digitalSourceSmoothed.getCurrentValue());

            // The type switches re-voice their own curve. Four of the five live
            // here, because four of the five are voices on SaturationCore's
            // curves; the vinyl type re-voices the VinylStage at the end of the
            // chain instead and is applied once per block.
            //
            // Each switch is cheap - a table read into three floats - but it is
            // still per sample here rather than per block so that sweeping a type
            // control mid-note lands the same way the curve does: sample-locked
            // to the block the host handed us, with no extra state to unwind.
            saturation.setValveVoice (valveTypeCached);
            saturation.setAmpVoice (ampTypeCached);
            saturation.setTransformerVoice (transformerTypeCached);
            saturation.setDigitalVoice (digitalTypeCached);

            // Sag: the supply envelope. The two coefficients are BLOCK-RATE
            // constants (see where they are built, above the loop) - rebuilding
            // them here cost two std::exp per sample per channel for values that
            // cannot change within the block.
            const float sagAmountNow = sagSmoothed.getCurrentValue();
            // The demand the supply sees is the signal ARRIVING at the stage, not
            // what leaves it: a real supply droops in proportion to how hard it is
            // being asked to work, which is the input to the gain stage.
            const float demand = std::abs (preDrive);
            const float sagCoefficient = demand > amp.sagEnvelope ? sagAttackCoefficient
                                                                  : sagReleaseCoefficient;
            amp.sagEnvelope += (demand - amp.sagEnvelope) * sagCoefficient;
            amp.sagGain = 1.0f - sagAmountNow * 0.35f
                              * juce::jlimit (0.0f, 1.0f, amp.sagEnvelope * 3.0f);

            const float driveWithSag = driveNow * amp.sagGain;

            const float shapedCore = saturation.process (preDrive, driveWithSag,
                                                         asymmetryNow);

            // The amp's post-curve voicing: cabinet, then presence. It is applied
            // only in proportion to how much amp is in the blend, so a pure tape
            // setting is bit-for-bit the machine it always was.
            const float ampShare = juce::jlimit (0.0f, 1.0f, saturation.ampWeight);
            float voicedCore = shapedCore;
            if (ampShare > 1.0e-4f)
            {
                const float voiced = amp.process (shapedCore,
                                                  cabinetLowCoefficient,
                                                  cabinetPeakCoefficient,
                                                  presenceCoefficient,
                                                  (presenceSmoothed.getCurrentValue() - 0.5f) * 2.0f
                                                      * 0.6f);
                // Crossfade rather than switch, so the voicing fades in with the
                // amp share and there is no step at any BLEND position.
                voicedCore = shapedCore + (voiced - shapedCore)
                                          * ampShare * cabinetSmoothed.getCurrentValue();
            }

            hysteresisMemory[2] = hysteresisMemory[1];
            hysteresisMemory[1] = hysteresisMemory[0];
            hysteresisMemory[0] = voicedCore;

            // ------------------------------------------------------------------
            //  Sub-Fundamental: the multi-stage undertone series with downward saturation.
            //
            //  Generated here, from the SHAPER's output rather than the raw input, so
            //  it is excited by the signal that actually reached the magnetic domain,
            //  and per channel: one generator on the mono sum would collapse the
            //  stereo image at precisely the octave the control is meant to thicken.
            //
            //  The result is parked in undertoneOutput[] rather than summed into the
            //  tape path. Everything between here and the output is a nonlinearity -
            //  the bias-compression curve, the wow and flutter modulation, the output
            //  glue compressor - and each of them will happily turn the undertones
            //  into harmonics of themselves. That is the "regular harmonics are
            //  generated from the subharmonic" report: the undertones went in as
            //  partials and came out as a full harmonic series built on each of them.
            //  They are added back where the rest of the chain is linear or gain-only.
            // ------------------------------------------------------------------
            auto& subharmonic = channel == 0 ? subharmonicL : subharmonicR;
            undertoneOutput[static_cast<std::size_t> (channel)] = subharmonic.process (
                voicedCore, driveAmountSmoothed.getCurrentValue(),
                subFundamentalSmoothed.getCurrentValue(), engineSampleRate);

            // A parallel addition, not a blend: the control reads as adding weight
            // below the note instead of crossfading the tape away. The depth ramp keeps
            // the level change from stepping the phase-locked oscillator.
            // The voiced core is what continues down the tape path: the cabinet and
            // presence stages are part of the amp, so discarding them here would
            // compute the whole voicing and then throw it away.
            const float withSubharmonic = voicedCore;

            // Measure what the shaper actually produced, comparing its input against its
            // output. Harmonics are the reason this plugin exists, so the character is
            // observed rather than assumed: the analyser separates the even content
            // (warmth, from the bias asymmetry) from the odd content (edge, from the
            // symmetric tanh). Only the left channel is measured, since the two are driven
            // identically and doubling the analyser would cost twice as much for the same
            // reading.
            if (channel == 0)
                harmonicAnalyser.analyse (preDrive, voicedCore, engineSampleRate);

            // Tape is a low-pass medium: the faster the tape and the brighter the
            // tone setting, the more top end survives. The undertones are NOT part of
            // this - they join below, after the last nonlinearity.
            highFreqMemory[1] += (withSubharmonic - highFreqMemory[1]) * toneLpSmoothed.getCurrentValue();
            const float afterTapeLoss = highFreqMemory[1];

            // Per-model head damping (the headDampingHz each TAPE TYPE sets, scaled
            // by the transport speed). This pole used to be computed and then never
            // applied - the formulas' damping figures were a no-op.
            //
            // Two ramps share this target: the ordinary 20 ms control ramp, and the
            // slow one used while a tape formula is being switched. A formula change
            // moves this pole by up to 8 kHz, and 20 ms of that travel is an audible
            // sweep rather than a crossfade, which is why the slow ramp exists.
            const float dampingCoefficient = tapeTypeSwitching
                ? headDampingSwitchSmoothed.getCurrentValue()
                : hfPostSmoothed.getCurrentValue();

            // Both ramps must advance every sample even when one is idle, or the unused
            // one would resume from a stale value on the next switch. Reading advances
            // it lazily through the shared sample clock, so touching the other one here
            // is enough.
            if (tapeTypeSwitching)
                hfPostSmoothed.getCurrentValue();
            else
                headDampingSwitchSmoothed.getCurrentValue();

            highFreqMemory[2] += (afterTapeLoss - highFreqMemory[2]) * dampingCoefficient;
            const float dampedLoss = highFreqMemory[2];

            // Playback head gap loss: the TONE macro's crossfade of the head itself.
            highFreqMemory[0] += (dampedLoss - highFreqMemory[0]) * headGapSmoothed.getCurrentValue();
            const float headLoss = highFreqMemory[0];
            highFreqMemory[1] = afterTapeLoss;

            // ------------------------------------------------------------------
            //  MODELED TRACKS: the track WIDTH's two consequences.
            //
            //  A narrower track reads less low end per channel, because the head
            //  gap sees less of the recorded wavelength at the bottom of the
            //  band. That is the same geometry that raised the noise floor above,
            //  and it is applied here as a gentle bottom-end tilt rather than as
            //  a level change: the machine's overall level is the OUTPUT
            //  control's job, and a layout should not be a volume knob.
            //
            // The tilt is taken from the same split the tape condition stage
            // uses, so a narrower track's difference lands in the same place a
            // FLUX change does - which is correct, because both are statements
            // about how much of the medium's depth is being used. At the
            // two-track default the scale is exactly 1.0, so the machine is
            // bit-for-bit what it always was.
            // ------------------------------------------------------------------
            const float widthTilt = juce::jlimit (0.75f, 1.15f,
                                                  1.0f + (trackWidthScale - 1.0f) * 0.65f);

            // Scale compensation, gentle level-dependent bias compression and the
            // tape noise floor ride on the modulated signal.
            const float compensation = tapeCurve / 1.30f * widthTilt;

            // Nonlinearity costs level, but only when the shaper is actually working. The
            // corrected shaper is near-unity at low drive, so this correction scales with
            // the drive amount rather than applying a large standing boost - the old 2.2x
            // ceiling was compensating for a stage that saturated at every setting and is
            // no longer appropriate, which is part of why the plugin came out harsh and
            // loud. It now recovers a modest amount of the level the clamps removed and
            // stays close to unity when the machine is running clean.
            const float shapedLevel = std::abs (voicedCore);
            const float shaperLoss = juce::jlimit (0.0f, 1.0f,
                                                   driveCurve * 0.22f * (1.0f - shapedLevel * 0.7f));
            const float driveCompensation = 1.0f + shaperLoss;

            const float compressedBias = headLoss * (1.0f - 0.18f * headLoss * headLoss)
                                       / juce::jmax (0.35f, compensation)
                                       * juce::jlimit (0.8f, 1.35f, driveCompensation);

            // The hiss is band-limited rather than white, so its spectrum is the same at
            // 44.1 kHz and 192 kHz and it reads as tape noise instead of digital hiss. The
            // filter state is per channel so the two sides stay uncorrelated.
            auto& hissLowPass = channel == 0 ? hissLowPassL : hissLowPassR;
            const float rawHiss = nextNoise (noiseState) * hissGainSmoothed.getCurrentValue();
            hissLowPass += (rawHiss - hissLowPass) * hissBandLimit;

            // The band limit costs most of the noise power, so the gain is compensated by
            // the inverse of the filter's RMS response. Deriving it from the coefficient
            // rather than a fixed number keeps the perceived level flat at every rate.
            //
            // NOTE: there is deliberately no programme-dependent levelling here. An
            // earlier version divided the floor back up by the amount the glue stages had
            // ducked, with the intent of holding the level constant between pause and
            // signal. The maths was inverted - the compensation ran from 1.0 in a pause up
            // to a 1.05 ceiling under signal - so the hiss was loudest when nothing was
            // playing and slightly louder still when the compressors clamped down. Tape
            // hiss is simply a constant floor; tracking the programme makes it breathe
            // with the music, which is the one thing a noise floor must not do.
            const float bandLimitCompensation = 1.0f / std::sqrt (juce::jmax (0.05f, hissBandLimit));
            // The NOISE control trims the floor on top of the formula's own figure:
            // 0.5 is unity, so the knob starts neutral and the stock's own character
            // is unchanged unless the user asks for a different floor. The multiplier
            // is carried by a smoother, so dragging the knob glides the hiss rather
            // than stepping it.
            const float noiseFloor = hissLowPass * bandLimitCompensation
                                   * noiseTrimSmoothed.getCurrentValue();

            const float motioned = (compressedBias + noiseFloor) * wowMod * flutterMod * grainMod;

            // Playback EQ: the BRIGHTNESS tilt. A one-pole low-pass at the fixed
            // 1.6 kHz pivot splits the wet signal ITSELF into low and high bands
            // (the old code split it against its own filtered copy, so the "high
            // band" was hiss residue and the control was inaudible on an
            // analyser). The matched gain pair then moves the two bands in
            // opposite directions - removals capped near 4 dB, boosts to +15 dB,
            // exactly unity at the 50 percent pivot. Both smoothers advance every sample, so the
            // tilt can never step the waveform.
            auto& pivotLow = toneShelfSplit[static_cast<std::size_t> (channel)];
            pivotLow += (motioned - pivotLow) * toneShelfCoefficient;
            const float highBand = motioned - pivotLow;
            const float deEmphasised = pivotLow * toneShelfGainSmoothed.getCurrentValue()
                                     + highBand * toneShelfBoostSmoothed.getCurrentValue();

            // ------------------------------------------------------------------
            //  Tape condition: FLUX and WEAR.
            //
            //  FLUX is the record head's depth into the oxide - more flux is more
            //  low end and a stronger hysteresis memory, less is thin and bright.
            //  WEAR is the state of the heads and the tape: a rounded gap and
            //  patchy oxide lose top end.
            //
            //  They run here, after the playback tilt, because both are properties
            //  of the RECORDED signal rather than of the electronics - the tilt is
            //  the playback EQ, and these two are what the medium did before it.
            // ------------------------------------------------------------------
            auto& condition = channel == 0 ? tapeConditionL : tapeConditionR;
            const float fluxNow = fluxSmoothed.getCurrentValue();
            const float wearNow = wearSmoothed.getCurrentValue();

            float conditioned = deEmphasised;
            if (std::abs (fluxNow - 0.5f) > 1.0e-4f || wearNow > 1.0e-5f)
                conditioned = condition.processTone (deEmphasised, channel,
                                                     fluxNow, wearNow,
                                                     fluxShelfCoefficient, wearLossCoefficient);

            // -------------------------------------------------------------------
            //  Playback AC coupling (DC blocker) - the fix for "MIX at maximum
            //  produces garbage instead of a warm signal".
            //
            //  The shaper is deliberately asymmetric (that asymmetry is what creates
            //  the even harmonics), which leaves a small DC component and a slightly
            //  lopsided envelope on the tape signal. On a real machine the playback
            //  electronics are AC-coupled, so that offset never reaches the output;
            //  here it used to ride all the way into the glue compressor, the safety
            //  limiter and the soft clipper. The clipper then worked on an off-centre
            //  waveform: one half clipped much earlier than the other, the safety
            //  limiter sat permanently pulled down by the DC, and the result was a
            //  harsh, congested mess exactly when MIX was at 100 %.
            //
            //  A gentle one-pole DC blocker (about 8 Hz corner) removes the offset
            //  without touching the bass the way a steep high-pass would, which is
            //  precisely what the coupling capacitors in the playback chain do.
            // -------------------------------------------------------------------
            auto& dcX = dcBlockXState[static_cast<std::size_t> (channel)];
            auto& dcY = dcBlockYState[static_cast<std::size_t> (channel)];
            const float dcBlocked = conditioned - dcX + dcBlockR * dcY;
            dcX = conditioned;
            dcY = dcBlocked;

            // -------------------------------------------------------------------
            //  Playback head delay.
            //
            //  The delayed signal is taken from the DC-blocked wet path, which is
            //  the signal as it actually leaves the playback head - so the repeats
            //  inherit the tape's own bandwidth and saturation rather than being a
            //  clean digital echo of the input. That is the whole difference between
            //  a second tape head and a delay unit.
            //
            //  The read offset is the smoothed delay in samples, read with linear
            //  interpolation: the smoothing means the offset is almost never whole,
            //  and a delay that snapped to whole samples would zipper as the knob
            //  moved.
            // -------------------------------------------------------------------
            float delayed = 0.0f;
            if (delayBufferLength > 0)
            {
                const float delaySamples = juce::jlimit (
                    0.0f, static_cast<float> (delayBufferLength - 1),
                    delaySamplesSmoothed.getCurrentValue());

                float readPosition = static_cast<float> (delayWritePosition) - delaySamples;
                if (readPosition < 0.0f)
                    readPosition += static_cast<float> (delayBufferLength);

                const int readIndex = static_cast<int> (readPosition);
                const float fraction = readPosition - static_cast<float> (readIndex);
                const int nextIndex = (readIndex + 1) % delayBufferLength;

                const auto* delayRead = delayBuffer.getReadPointer (channel);
                delayed = delayRead[readIndex]
                        + (delayRead[nextIndex] - delayRead[readIndex]) * fraction;
            }

            // ------------------------------------------------------------------
            //  The repeat's character: TAPE / BBD / MODERN.
            //
            //  DELAY and DLY LVL set the time and the level; this sets what the
            //  repeats SOUND like, and the three are genuinely different machines
            //  rather than three amounts of the same loss:
            //
            //    TAPE   - the original behaviour. Each pass round the loop loses
            //             top end, because the repeat is recorded onto the tape and
            //             played back through the same losses the main path has.
            //    BBD    - a bucket-brigade chip, the analogue delay of the era.
            //             Darker still, and its bandwidth narrows as the delay
            //             lengthens - which is what a BBD physically does, because
            //             the same number of buckets is being clocked more slowly.
            //    MODERN - a clean digital delay: full bandwidth, no loss.
            // ------------------------------------------------------------------
            auto& damp = delayDampState[static_cast<std::size_t> (channel)];
            float delayCharacter = delayed;

            if (delayTypeCached == 0)
            {
                // TAPE: the original damping, unchanged.
                damp += (delayed - damp) * delayDampCoefficient;
                delayCharacter = damp;
            }
            else if (delayTypeCached == 1)
            {
                // BBD: darker still, and the loss rises with the delay time - a
                // long setting on a bucket brigade is a dull one, because the same
                // buckets are being clocked more slowly and the anti-alias filter
                // tracks the clock.
                auto& bbdLow = channel == 0 ? bbdLowL : bbdLowR;
                auto& bbdNoise = channel == 0 ? bbdNoiseStateL : bbdNoiseStateR;

                // The BBD's bandwidth is a fraction of its clock, which is the
                // defining limitation of the technology - and the clock rate is
                // set by the delay time, so this coefficient DOES have to track a
                // smoothed value and cannot be hoisted to the block.
                //
                // It is refreshed on a stride instead. The delay time ramps over
                // 20 ms, so recomputing the coefficient every 32 samples still
                // follows that ramp with 750 points at 48 kHz - indistinguishable
                // - and it costs one std::exp per 32 samples rather than one per
                // sample. The counter is advanced once per frame, not per channel,
                // so both sides refresh on the same instants.
                if (channel == 0 && --bbdCoefficientCountdown <= 0)
                {
                    bbdCoefficientCountdown = bbdCoefficientStride;

                    const float delaySamplesNow = juce::jmax (1.0f, delaySamplesSmoothed.getCurrentValue());
                    const float clockRate = engineSampleRate / delaySamplesNow;
                    const float bbdBandwidthHz = juce::jlimit (800.0f, 6000.0f, clockRate * 0.22f);
                    bbdLowCoefficient = onePoleCoefficientHz (bbdBandwidthHz, engineSampleRate);
                }

                bbdLow += (delayed - bbdLow) * bbdLowCoefficient;

                // The clock's own noise: a small, high-frequency buzz riding on the
                // repeats, which is the audible signature of a BBD.
                bbdNoise = bbdNoise * 1664525u + 1013904223u;
                const float clockNoise = (static_cast<float> ((bbdNoise >> 8) & 0x00ffffffu)
                                            * (1.0f / 8388608.0f) - 1.0f) * 0.006f;

                delayCharacter = bbdLow + clockNoise;
                damp = bbdLow;
            }
            else
            {
                // MODERN: full bandwidth. The damping state is still advanced so
                // switching back to TAPE does not resume from a stale value.
                damp += (delayed - damp) * delayDampCoefficient;
                delayCharacter = delayed;
            }

            const float delayTap = delayCharacter * delayLevelSmoothed.getCurrentValue();

            // ------------------------------------------------------------------
            //  What is written back into the line.
            //
            //  Two things matter here, and both are what make this a head on the
            //  machine rather than a parallel effect:
            //
            //    - the wet signal is written, so the echo is recorded onto the
            //      tape and saturates on the way round;
            //
            //    - the FEEDBACK goes into the line as well, and which line it
            //      goes into is what PING-PONG changes. At 0 the repeat is written
            //      back into its own channel, so the echoes stay where they started
            //      - a normal tape slap. At 1 it is written into the OTHER channel,
            //      so each repeat arrives on the opposite side and the echoes
            //      alternate left, right, left - the classic ping-pong.
            //
            //  The delay line itself is one stereo buffer indexed by channel, so
            //  "the other channel" is a one-bit difference in this index. That is
            //  the whole of the implementation: ping-pong is not a second delay
            //  unit, it is the SAME head read on the other side of the machine,
            //  which is what it physically is when two heads are wired across a
            //  stereo pair.
            // ------------------------------------------------------------------
            if (delayBufferLength > 0)
            {
                // The channel the feedback is written into. At a full ping-pong
                // this is the other side; in between it is neither, and the two
                // writes below distribute the feedback across both lines so the
                // control sweeps continuously rather than switching.
                const int feedbackChannel = 1 - channel;
                const float feedbackAmount = delayFeedbackSmoothed.getCurrentValue()
                                           * damp;
                const float pingPongNow = pingPongSmoothed.getCurrentValue();

                auto* delayWrite = delayBuffer.getWritePointer (channel);

                // The dry-ish wet signal always goes into its OWN line: the head
                // is recording what it hears, and that does not move when the
                // feedback is re-routed.
                delayWrite[delayWritePosition] = dcBlocked;

                // The feedback: partly into this channel's line and partly into
                // the other's, split by the control. The sum of the two partial
                // writes is the same total energy at every position, so sweeping
                // PING-PONG moves the echoes across the image rather than fading
                // them.
                delayWrite[delayWritePosition] += feedbackAmount * (1.0f - pingPongNow);

                if (feedbackChannel != channel)
                    delayBuffer.getWritePointer (feedbackChannel)[delayWritePosition]
                        += feedbackAmount * pingPongNow;
            }

            const float withDelay = dcBlocked + delayTap;

            // -------------------------------------------------------------------
            //  Stereo tape offset.
            //
            //  A fractional-sample delay on ONE channel, set by ST OFFSET. It is
            //  deliberately tiny - a few tens of microseconds - and it is what makes
            //  a tape bounce sit wide instead of merely being equalised wide.
            //
            //  A linear-interpolating buffer rather than an all-pass: an all-pass
            //  gives the same group delay for less memory but colours the phase
            //  differently across the band, and the point here is that the two
            //  channels differ by TIME, not by filter shape.
            // -------------------------------------------------------------------
            float aligned = withDelay;
            const bool offsetThisChannel = (channel == 1) == offsetRightChannel;
            if (offsetThisChannel && activeChannels > 1)
            {
                const float offsetSamples = juce::jlimit (
                    0.0f, static_cast<float> (stOffsetBufferLength - 2),
                    stOffsetSamplesSmoothed.getCurrentValue());

                float readPosition = static_cast<float> (stOffsetWritePosition) - offsetSamples;
                while (readPosition < 0.0f)
                    readPosition += static_cast<float> (stOffsetBufferLength);

                const int readIndex = static_cast<int> (readPosition);
                const float fraction = readPosition - static_cast<float> (readIndex);
                const int nextIndex = (readIndex + 1) % stOffsetBufferLength;

                aligned = stOffsetBuffer[static_cast<std::size_t> (readIndex)]
                        + (stOffsetBuffer[static_cast<std::size_t> (nextIndex)]
                           - stOffsetBuffer[static_cast<std::size_t> (readIndex)]) * fraction;
            }

            // The offset buffer is written for the channel being delayed AND for the
            // other one, so whichever side the control points at has a current sample
            // to read. Both writes happen every frame, so the buffer never holds a
            // stale side.
            if (channel == 1)
                stOffsetBuffer[static_cast<std::size_t> (stOffsetWritePosition)] = withDelay;

            // Raised-cosine crossfade between the DRY INPUT and the fully processed
            // tape signal, driven by the smoothed MIX. 0 % is a transparent dry signal
            // and 100 % is all tape, both at unity, with the level held across the
            // middle of the travel.
            //
            // The dry leg is `machineDryInput` - the sample captured right after the
            // input trim and glue compressor, BEFORE the DI, DISTORT, PREAMP and the
            // IN EQ - and not `x`, which by this point has been through every one of
            // those stages. The dry leg used to ride `x`, so MIX 0 was never the
            // untouched input: with a DI engaged the dry side carried the box's
            // colouring, and the whole output then multiplied by stageGain (the
            // compressors' gain, both makeups and the static calibration) left MIX 0
            // both coloured AND several dB off the input. `machineDryInput` is the
            // same capture DELTA subtracts its reference from, so both controls now
            // agree on what "dry" means - and because the dry leg no longer rides
            // the processed signal, MIX 0 IS the input, whatever the front-end
            // stages are doing.
            //
            //  The transport gates BOTH legs. A stopped machine passes nothing, so
            //  STOP is silence rather than "the wet part went away and the input is
            //  still there" - which is exactly what a stop on a deck does, and
            //  what the panel's STOP key and its tooltip promise. With MIX at 0 the
            //  machine is the input's path, so STOP there is a stop too. The gate's
            //  SHAPE is `platterGate` and not the platter speed itself: a linear
            //  gate is a fader, and a fader is what the report heard. tests/transport
            //  reads both of these legs as text precisely so neither can lose it.
            const float wetMix = aligned * wetGain * platterGate;
            const float dryMix = machineDryInput[static_cast<std::size_t> (channel)]
                               * dryGain * platterGate;
            tapeOutput[static_cast<std::size_t> (channel)] = dryMix + wetMix;
        }

        // ------------------------------------------------------------------
        //  THE REPITCH STAGE - the pitch of the transport.
        //
        //  Everything above this line reads the platter's speed as a GAIN: the
        //  level, the modulation depth, the heads' speed response and the gate
        //  that shapes STOP. None of those can bend a pitch, and a deck slowing
        //  down is first of all a pitch. This stage is the one place the format
        //  of the machine's output changes rather than its level: the finished
        //  frame is recorded into a delay line and read back out at the
        //  platter's own speed, so a coast walks the programme down through the
        //  octaves and a spin-up walks it back.
        //
        //  It sits AFTER the crossfade, on the frame both legs made, because the
        //  transport governs both legs - a stopped deck passes nothing, whatever
        //  its MIX is set to - and it sits BEFORE every gain that follows, so the
        //  machine's own level, the compensator and the limiter all see the
        //  pitch-bent signal rather than the other way round. One call per FRAME,
        //  like the transport's own advance: the delay line's two heads move once
        //  per frame and the two channels are one tape.
        // ------------------------------------------------------------------
        repitchStage.processFrame (tapeOutput, activeChannels, platterSpeed);

        // ------------------------------------------------------------------
        //  Subharmonic anti-phase protection.
        //
        //  Runs once per FRAME, after both channels have generated their
        //  undertones, because it is a statement about the PAIR - the channel loop
        //  cannot see both sides at once.
        //
        //  The 1/2 stage carries the most energy of the whole cascade, so it is
        //  the one that matters if the two sides disagree: two octaves in opposite
        //  phase cancel when the mix is summed to mono, and the low end the
        //  control was asked for simply disappears. That is not a fault in either
        //  generator - each is correctly locked to its own channel's waveform -
        //  but it is a fault in the result.
        //
        //  The fix is to pull the two octaves back together. The correction is a
        //  small rotation of the master phase toward the other channel's, applied
        //  only while the two are actually disagreeing and only in proportion to
        //  how much they disagree, so an already-aligned pair is untouched.
        // ------------------------------------------------------------------
        if (activeChannels == 2)
        {
            const float errorL = subharmonicL.getOctavePhaseError();
            const float errorR = subharmonicR.getOctavePhaseError();

            // The signed difference, wrapped to [-0.5, 0.5): zero means the two
            // octaves are in phase, +/-0.5 means they are exactly opposite.
            float octaveError = errorL - errorR;
            if (octaveError > 0.5f)  octaveError -= 1.0f;
            if (octaveError < -0.5f) octaveError += 1.0f;

            // Only correct a genuine disagreement, and only gently. A 0.05
            // threshold means small tracking differences are left alone - they are
            // not audible and correcting them would fight the per-channel lock -
            // while a real opposition is pulled back over a few cycles.
            if (std::abs (octaveError) > 0.05f)
            {
                const float correction = -octaveError * 0.02f;
                subharmonicR.nudgeMasterPhase (correction);
            }
        }

        // One advance of the delay line's write head per FRAME, not per channel:
        // advancing it inside the channel loop would move the line twice per stereo
        // frame, halving every delay time and putting the two channels a sample
        // apart. The ST OFFSET buffer advances with it so the two stay aligned.
        if (delayBufferLength > 0)
            delayWritePosition = (delayWritePosition + 1) % delayBufferLength;
        stOffsetWritePosition = (stOffsetWritePosition + 1) % stOffsetBufferLength;

        // The track-bleed ring advances once per FRAME for the same reason the
        // other two do: advancing it per channel would move it twice per stereo
        // frame and halve the spacing between the two heads.
        tracksBleedWritePosition = (tracksBleedWritePosition + 1) % tracksBleedBufferLength;

        // The reference is a per-channel average now, the same scale the
        // post-compressor power below is measured on.
        referenceBlockPower /= static_cast<float> (juce::jmax (1, activeChannels));

        // ---------------------------------------------------------------------
        //  Output stage glue compressor. It is immediately before the output trim,
        //  so the headroom it creates is spent directly on the OUTPUT control and
        //  the level leaving the plugin stays calibrated. Independent of the input
        //  stage: this detector only sees the tape output, and its operating point
        //  follows the OUTPUT trim.
        // ---------------------------------------------------------------------
        float detectorPower = 0.0f;
        if (activeChannels > 0)
        {
            for (int channel = 0; channel < activeChannels; ++channel)
            {
                const auto signal = tapeOutput[static_cast<std::size_t> (channel)];
                detectorPower += signal * signal;
            }
            detectorPower /= static_cast<float> (activeChannels);
        }

        const float envelopeDb = outputCompressor.processDetection (detectorPower, engineSampleRate,
                                                                    outputCompressorCoefficients);
        const float reductionDb = juce::jmax (outputReductionLimitDb,
                                              softKneeReductionDb (envelopeDb,
                                                                   outputThresholdDb,
                                                                   outputKneeDb,
                                                                   outputCompressorRatio));

        // The output stage's per-channel detectors, on the same terms as the input
        // stage's: advanced only when ST LINK is actually asking for them, so the
        // default linked path costs nothing extra. They see the TAPE output, which
        // is what this stage has always detected.
        std::array<float, 2> outputPerChannelReductionDb {};
        if (linkNow < 0.999f)
        {
            for (int channel = 0; channel < activeChannels; ++channel)
            {
                const auto signal = tapeOutput[static_cast<std::size_t> (channel)];
                const auto channelEnvelopeDb = outputCompressorChannels[static_cast<std::size_t> (channel)]
                    .processDetection (signal * signal, engineSampleRate,
                                       outputCompressorCoefficients);
                outputPerChannelReductionDb[static_cast<std::size_t> (channel)] =
                    juce::jmax (outputReductionLimitDb,
                                softKneeReductionDb (channelEnvelopeDb, outputThresholdDb,
                                                     outputKneeDb, outputCompressorRatio));
            }
        }
        // The output stage's three dB-to-gain conversions are also per sample. They
        // are grouped behind one guard rather than three, because the branch is
        // compile-time and the three always travel together.
#if J37_HAS_CHOWDSP_MATH
        const float compressionGain = chowdsp::DecibelsApprox::decibelsToGain (reductionDb);
#else
        const float compressionGain = juce::Decibels::decibelsToGain (reductionDb);
#endif
        peakReductionDb = juce::jmin (peakReductionDb, reductionDb);

        // Each stage pays back part of the reduction it applied, weighted by how hard
        // its trim control is driving it, so both together cannot leave the machine
        // quieter than it arrived.
#if J37_HAS_CHOWDSP_MATH
        const float inputMakeup = chowdsp::DecibelsApprox::decibelsToGain (-inputPeakReductionDb
                                                                            * inputMakeupFraction);
        const float outputMakeup = chowdsp::DecibelsApprox::decibelsToGain (-reductionDb
                                                                             * outputMakeupFraction);
#else
        const float inputMakeup = juce::Decibels::decibelsToGain (-inputPeakReductionDb
                                                                  * inputMakeupFraction);
        const float outputMakeup = juce::Decibels::decibelsToGain (-reductionDb
                                                                   * outputMakeupFraction);
#endif
        const float stageGain = compressionGain * outputMakeup * finalOutputGain * inputMakeup;

        // The same blend for the output stage's gain, per channel. Held as an
        // array so the loop below applies each side's own answer.
        std::array<float, 2> stageGainPerChannel { stageGain, stageGain };
        if (linkNow < 0.999f)
        {
            for (int channel = 0; channel < activeChannels; ++channel)
            {
                const auto unlinkedCompressionGain = juce::Decibels::decibelsToGain (
                    outputPerChannelReductionDb[static_cast<std::size_t> (channel)]);
                const auto blended = compressionGain
                                   + (unlinkedCompressionGain - compressionGain) * (1.0f - linkNow);
                stageGainPerChannel[static_cast<std::size_t> (channel)] =
                    blended * outputMakeup * finalOutputGain * inputMakeup;
            }
        }

        // ---------------------------------------------------------------------
        //  Final gain compensation.
        //
        //  The tape stage, the two glue compressors and their makeup all change the
        //  level, so what leaves the plugin is not necessarily the level the user
        //  dialled in with INPUT. This compares the finished signal against the
        //  reference taken straight after the input trim and before the first
        //  compressor, and pays back whatever was lost or gained, so the plugin holds
        //  its output level as DRIVE, TAPE TYPE, SPEED and MIX are changed instead of
        //  drifting louder or quieter with every edit.
        //
        //  The correction is smoothed with a slow one-pole, so it tracks the overall
        //  programme level rather than fighting the moment-to-moment compression. That
        //  keeps the compressors' punch intact: it corrects the average, not transients.
        // ---------------------------------------------------------------------
        float compensationGain = 1.0f;
        //
        //  The compensation measures the MACHINE's output against the input, so it
        //  must not try to correct a machine that is not running: with STOP gating
        //  that output the measurement would collapse, the compensator would ramp
        //  to its +12 dB limit while the machine sat still, and START would then
        //  come back loud before the correction relaxed. While the platter is not
        //  at speed the last correction is held instead - which is also the honest
        //  reading, because a stopped machine has no output to measure.
        if (autoGainEnabled && transport.atSpeed() && referenceBlockPower > 0.0f)
        {
            // Power of the signal as it leaves the compressors and tape stage, but
            // BEFORE this compensation is folded in - measuring the compensated output
            // would make the correction chase its own tail.
            float postCompressorPower = 0.0f;
            for (int channel = 0; channel < activeChannels; ++channel)
            {
                const auto signal = tapeOutput[static_cast<std::size_t> (channel)]
                                  * stageGainPerChannel[static_cast<std::size_t> (channel)];
                postCompressorPower += signal * signal;
            }

            if (postCompressorPower > 1.0e-12f)
            {
                // How far the finished signal has fallen below the reference taken after
                // the input trim. Positive means level was lost and needs paying back.
                // chowdsp's polynomial dB approximation replaces libm's exp2/log2
                // chain on the auto-gain hot path; the JUCE fallback below keeps any
                // build without the chowdsp header identical in behaviour.
#if J37_HAS_CHOWDSP_MATH
                const auto levelRatioDb = chowdsp::DecibelsApprox::gainToDecibels (
                    std::sqrt (referenceBlockPower / postCompressorPower), 0.0f);
#else
                const auto levelRatioDb = juce::Decibels::gainToDecibels (
                    std::sqrt (referenceBlockPower / postCompressorPower), 0.0f);
#endif

                // Only ever restore, never exaggerate: the correction may recover a loss
                // but must not become an extra boost stage of its own.
                const auto targetCompensationDb = juce::jlimit (0.0f, 12.0f, levelRatioDb);
                smoothedCompensationDb += (targetCompensationDb - smoothedCompensationDb)
                                        * compensationCoefficient;
                compensationGain = juce::Decibels::decibelsToGain (smoothedCompensationDb);
            }
        }

        std::array<float, 2> outputSignal {};
        std::array<float, 2> machineDry {};
        for (int channel = 0; channel < activeChannels; ++channel)
        {
            const auto index = static_cast<std::size_t> (channel);
            outputSignal[index] = tapeOutput[index]
                                * stageGainPerChannel[index] * compensationGain;

            // The same signal at MIX 0. Because it carries the identical
            // stageGain * compensationGain, everything that is gain rather than
            // character cancels in the subtraction below and DELTA shows only what
            // the machine ADDS. Subtracting the raw host sample instead left all of
            // it in the difference: the input trim, both compressors, both makeups,
            // the compensator and the output trim all read as "character", so a
            // perfectly dry MIX 0 still monitored as a loud level change rather than
            // silence.
            // Gated with the machine, for the same reason the dry leg is: DELTA is
            // the difference between what the machine made and the same signal at
            // MIX 0, and at rest both are nothing. Ungated, DELTA on a stopped
            // machine would monitor the input on its own - the opposite of silence.
            machineDry[index] = machineDryInput[index]
                              * stageGainPerChannel[index] * compensationGain * platterSpeed;
        }

        // -------------------------------------------------------------------
        //  SUBFUND joins here: the one point in the chain where the undertones
        //  can enter without anything downstream turning them into harmonics of
        //  themselves.
        //
        //  Upstream of here sit the three stages that caused the fault - the
        //  bias-compression curve, the wow and flutter modulation and the output
        //  glue compressor - which is why the sum was held back rather than added
        //  at the shaper. Downstream of here every stage is linear or gain-only:
        //  the stereo width, the OUTPUT trim, the safety limiter and the bypass
        //  crossfade. The limiter still sees the undertones, so a genuine
        //  overshoot is caught by it and the soft clipper stays a safety net
        //  rather than a shaper of them.
        //
        //  Two scalings keep the control meaning what it says:
        //
        //    - `wetGain`, so MIX 0 is the untouched input and the undertones go
        //      with it, exactly like the rest of the tape path;
        //    - `finalOutputGain`, the machine's static output calibration, so the
        //      knob holds the same level in the mix that it held when the sum ran
        //      through the whole chain. The DYNAMIC part of the glue stage is
        //      deliberately left out: a per-sample gain riding on the undertones
        //      would pump them at the note rate, which is the same fault as letting
        //      the compressor distort them.
        // -------------------------------------------------------------------
        for (int channel = 0; channel < activeChannels; ++channel)
            outputSignal[static_cast<std::size_t> (channel)] +=
                undertoneOutput[static_cast<std::size_t> (channel)] * wetGain * finalOutputGain
                * platterSpeed;

        // -------------------------------------------------------------------
        //  OUT EQ - the output equaliser, at the other end of the machine.
        //
        //  It sits after everything the machine does - the tape, both glue
        //  stages, the SUBFUND undertones - and before the width, the output
        //  trim, the anti-phase guard and the limiter. That means it corrects
        //  the RESULT rather than the input: nothing downstream responds to what
        //  it does, so it is the neutral, predictable EQ - the one you use to
        //  place the finished sound. The input EQ is the one that changes what
        //  the machine does; this is the one that changes what comes out.
        //
        //  It runs before the limiter deliberately: a boost here is caught by
        //  the protection chain like any other level, rather than being applied
        //  after the ceiling and forcing the clipper to work.
        // -------------------------------------------------------------------
        for (int channel = 0; channel < activeChannels; ++channel)
        {
            outputEq.lowGain = outputEqLowSmoothed.getCurrentValue();
            outputEq.midGain = outputEqMidSmoothed.getCurrentValue();
            outputEq.highGain = outputEqHighSmoothed.getCurrentValue();
            outputSignal[static_cast<std::size_t> (channel)] =
                outputEq.process (outputSignal[static_cast<std::size_t> (channel)],
                                  channel, outputEqMidCoefficient);
        }

        // -------------------------------------------------------------------
        //  NEURAL stage.
        //
        //  A learned model run as a per-channel nonlinearity, in the wet path
        //  and after the machine's own curve. It is deliberately placed AFTER
        //  the tape and BEFORE the transient shaper: a model that has been
        //  trained on a piece of hardware is itself a saturation curve, so it
        //  belongs beside the hand-written one rather than after the envelope
        //  stage - and running it before the shaper means the shaper can still
        //  sharpen whatever the model produced, which is the order an engineer
        //  would use the two.
        //
        //  At NEURAL MIX 0, or with no model loaded, this is a bit-for-bit
        //  pass-through, so a build without a model is exactly the plugin it was
        //  before this stage existed. The model is read-only at run time; the
        //  per-channel state lives in each stage.
        // -------------------------------------------------------------------
        const float neuralMixNow = neuralMixSmoothed.getCurrentValue();
        if (neuralMixNow > 1.0e-5f)
        {
            for (int channel = 0; channel < activeChannels; ++channel)
            {
                auto& neural = channel == 0 ? neuralL : neuralR;
                outputSignal[static_cast<std::size_t> (channel)] =
                    neural.process (outputSignal[static_cast<std::size_t> (channel)], neuralMixNow);
            }
        }

        // -------------------------------------------------------------------
        //  TRANSIENT SHAPER.
        //
        //  Attack and sustain, applied to the finished wet signal. It is the
        //  only stage in the chain that changes the ENVELOPE rather than the
        //  waveform, which is why it sits here: it moves the level of events
        //  without adding a harmonic, so the character the machine and the model
        //  just produced survives intact.
        //
        //  At both amounts zero the stage is transparent - the envelopes still
        //  run so the controls do not restart from rest, but the applied gain
        //  stays exactly 1. TRANSIENT MIX crossfades the shaped signal against
        //  the unshaped one, so the stage can be blended rather than switched.
        // -------------------------------------------------------------------
        const float transientMixNow = transientMixSmoothed.getCurrentValue();
        const float transientAttackNow = transientAttackSmoothed.getCurrentValue();
        const float transientSustainNow = transientSustainSmoothed.getCurrentValue();

        if (transientMixNow > 1.0e-5f
            && (std::abs (transientAttackNow) > 1.0e-5f || std::abs (transientSustainNow) > 1.0e-5f))
        {
            for (int channel = 0; channel < activeChannels; ++channel)
            {
                auto& shaper = channel == 0 ? transientL : transientR;
                const auto index = static_cast<std::size_t> (channel);

                const float shaped = shaper.process (outputSignal[index],
                                                     transientAttackNow, transientSustainNow,
                                                     transientFastCoefficient,
                                                     transientSlowCoefficient,
                                                     transientGainCoefficient);

                // The wet/dry crossfade is the MIX control: the shaper is the
                // wet leg, the untouched signal the dry one, so MIX 0 is exactly
                // the plugin without the stage.
                outputSignal[index] += (shaped - outputSignal[index]) * transientMixNow;
            }
        }

        // ------------------------------------------------------------------
        //  AUTOTUNE PLACEMENT / RATE DECISION.
        //
        //  The current arrangement runs autotune on the DOWN-SAMPLED signal AFTER
        //  processBlock has downsampled the oversampled block. That is a deliberate
        //  placement decision, not an accidental one, but it is the one place in the
        //  chain where the rate the processor sees and the rate the stage is handed
        //  can disagree if oversampling is on.
        //
        //  Review question (leave as a flag until decided one way):
        //    - Keep as-is if autotune is meant to operate at the host session rate
        //      regardless of oversampling, and its internal pitch state is rate-
        //      correct at that rate.
        //    - Move it earlier / re-rate it if the stage is meant to see the engine
        //      rate (session rate * oversampling factor) and the state is built for
        //      that.
        //
        //  Do not change the placement here without confirming which of those two is
        //  the intended behavior. Until then this block preserves the existing audio
        //  path exactly.
        // ------------------------------------------------------------------
        if (autoTuneStrength > 1.0e-5f)
        {
            std::array<float, 2> tuneFrame { outputSignal[0], outputSignal[1] };
            processAutoTuneSample (tuneFrame.data(), activeChannels, engineSampleRate);
            for (int channel = 0; channel < activeChannels; ++channel)
            {
                const auto index = static_cast<std::size_t> (channel);
                outputSignal[index] = tuneFrame[index];
            }
        }
        // Conventional compressor: post-tape, pre-width, independent of both
        // glue stages. It is a transparent bypass at MIX 0 and uses the same
        // detector settings for both channels without linking their gain states.
        if (compressorMixAmount > 1.0e-5f)
            for (int channel = 0; channel < activeChannels; ++channel)
            {
                const auto index = static_cast<std::size_t> (channel);
                const auto compressed = standardCompressors[index].process (
                    outputSignal[index], engineSampleRate, compressorThreshold,
                    compressorRatio, compressorAttack, compressorRelease, 6.0f,
                    compressorMakeup);
                outputSignal[index] += (compressed - outputSignal[index])
                                     * compressorMixAmount;
            }

        if (activeChannels == 2)
        {
            // ------------------------------------------------------------------
            //  MODELED TRACKS: the bleed between the two heads.
            //
            //  Each channel reads what the OTHER wrote a few samples ago and
            //  adds a little of it to itself - the fringing field reaching the
            //  neighbouring track. It runs BEFORE the width stage because it is
            //  part of what the machine put on the tape, not part of how the
            //  image is presented: the width control is the user's choice, the
            //  bleed is the layout's consequence.
            //
            //  The writes happen FIRST, for both channels, so the read below
            //  always sees the other channel's CURRENT frame rather than a frame
            //  that has already been overwritten by this one. That is the same
            //  discipline the ST OFFSET buffer uses and for the same reason.
            //
            //  At a two-track layout with the crosstalk figures above the leak
            //  is 0.010 to 0.055 - about -40 to -25 dB, which is the range real
            //  track-to-track bleed sits in. It is a small number on purpose: it
            //  should be felt as the image sitting slightly differently, not
            //  heard as anything at all.
            // ------------------------------------------------------------------
            tracksBleedBuffer[0][static_cast<std::size_t> (tracksBleedWritePosition)]
                = outputSignal[0];
            tracksBleedBuffer[1][static_cast<std::size_t> (tracksBleedWritePosition)]
                = outputSignal[1];

            // The read position, wrapped rather than clamped: the ring is a
            // circle, and the delay is always well under its length.
            int bleedReadPosition = tracksBleedWritePosition - trackBleedDelaySamples;
            if (bleedReadPosition < 0)
                bleedReadPosition += tracksBleedBufferLength;

            // Each channel takes the OTHER channel's delayed sample. This is what
            // makes the two sides of a three-track deck sound slightly closer
            // together: the leak is symmetric, so the image narrows a touch
            // without either side gaining on the other.
            const float bleedToLeft  = tracksBleedBuffer[1][static_cast<std::size_t> (bleedReadPosition)];
            const float bleedToRight = tracksBleedBuffer[0][static_cast<std::size_t> (bleedReadPosition)];

            outputSignal[0] += (bleedToLeft  - outputSignal[0]) * trackCrosstalk;
            outputSignal[1] += (bleedToRight - outputSignal[1]) * trackCrosstalk;

            // Keep the transport's inter-track bleed here. The user WIDTH control
            // is applied once at the final output stage, after the anti-phase guard.

            // ------------------------------------------------------------------
            //  Anti-phase prevention.
            //
            //  A stereo signal whose two sides are largely in OPPOSITE polarity
            //  cancels when the mix is folded to mono - the classic cause being an
            //  inverted source, a mis-wired cable, or the plugin's own modulation
            //  and undertone generators landing out of step across the pair. It is
            //  inaudible in stereo and then the low end and the centre of the image
            //  simply disappear the moment anything downstream sums the channels:
            //  a broadcast mono fold-down, a club PA, a phone speaker, a mono
            //  mastering check.
            //
            //  The guard measures how much of the frame's energy sits in the SIDE
            //  (the difference) relative to the MID (the sum), using the smoothed
            //  correlation of the two channels. When the two are genuinely in
            //  anti-phase the correlation goes negative and the guard ROTATES the
            //  pair's mid/side plane toward mono - in proportion to how wrong it
            //  is, over a slow ramp, so a real stereo image is never touched and a
            //  genuinely inverted channel is recovered over tens of milliseconds
            //  rather than switched.
            //
            //  ROTATION rather than attenuation, and the difference is the whole
            //  point of the guard. Attenuating the SIDE does not make a folding
            //  mix quieter in the way that matters: the mono sum is 2 x MID and
            //  scaling SIDE leaves MID exactly where it was, so the null it exists
            //  to prevent survives untouched - and on a fully inverted pair, where
            //  MID is zero and every bit of the programme is in SIDE, it fades the
            //  whole signal to silence instead of recovering it. Rotating the pair
            //  moves what is in SIDE into MID, which is what makes the fold-down
            //  audible again: at full engagement an inverted pair comes out as the
            //  (correct, full-level) mono signal it actually is. The rotation is
            //  orthogonal, so it preserves the pair's energy exactly - no pumping,
            //  no fading, nothing to hear except the image coming back.
            //
            //  It deliberately does NOT touch the width control: WIDTH can widen
            //  the image as far as the user likes, because a wide image is a
            //  legitimate stereo choice. What this prevents is the OTHER thing -
            //  the sides being in OPPOSITE polarity, which is not width at all.
            //
            //  antiPhaseGuardOn is the user's switch. With the guard off the
            //  correction is not merely skipped, it is RELAXED through the same
            //  pole the engagement uses, so turning the switch off unwinds the
            //  rotation smoothly instead of switching the image back in one
            //  sample - a click, which is the one thing this stage must never
            //  make. The DETECTOR keeps running while it is off, because the
            //  readout is the panel's warning that the sides are in opposition.
            // ------------------------------------------------------------------
            const float frameMid = outputSignal[0] + outputSignal[1];
            const float frameSide = outputSignal[0] - outputSignal[1];

            // The correlation envelope: a slow one-pole on the difference between
            // the mid power and the side power. Positive means the frame's energy
            // sits in the SUM (the sides agree - normal stereo), negative means it
            // sits in the DIFFERENCE (the sides oppose - the fault).
            const float frameEnergy = frameMid * frameMid - frameSide * frameSide;
            antiPhaseProduct += (frameEnergy - antiPhaseProduct) * antiPhaseCoefficient;

            // The normaliser tracks the frame's total energy on the same pole, so
            // the guard measures a RATIO and cannot be fooled by the programme
            // simply being louder or quieter.
            const float frameMagnitude = frameMid * frameMid + frameSide * frameSide;
            antiPhaseProductMagnitude += (frameMagnitude - antiPhaseProductMagnitude)
                                       * antiPhaseCoefficient;
            antiPhaseProductMagnitude = juce::jmax (1.0e-9f, antiPhaseProductMagnitude);

            // A bounded correction: only an actual negative correlation pulls the
            // guard into action, and it can only ever REDUCE the opposition, never
            // invert a healthy image.
            const float opposition = antiPhaseProduct < 0.0f
                ? juce::jlimit (0.0f, 1.0f, -antiPhaseProduct / antiPhaseProductMagnitude)
                : 0.0f;
            antiPhaseCorrection += ((antiPhaseGuardOn ? opposition : 0.0f) - antiPhaseCorrection)
                                 * antiPhaseCoefficient;

            // What the panel shows: the amount the guard is actually working by,
            // so a switch that is off reads as 0 rather than as the detector's
            // opinion. The readout's own text distinguishes "off" from "clean".
            antiPhaseAmount.store (antiPhaseGuardOn ? antiPhaseCorrection : 0.0f,
                                   std::memory_order_relaxed);

            if (antiPhaseCorrection > 1.0e-5f)
            {
                const float guardMid = 0.5f * (outputSignal[0] + outputSignal[1]);
                const float guardSide = 0.5f * (outputSignal[0] - outputSignal[1]);

                // The correction is the ANGLE, as a fraction of a quarter turn:
                // 0 leaves the pair exactly as it arrived, 1 is the quarter turn
                // that recovers an inverted pair into mono, and every level in
                // between is a proportional rotation of the pair.
                //
                // Stating it as the angle rather than as its SINE is the one
                // thing here that is not a style choice. Keeping sin(angle) in
                // the correction and recovering cos as sqrt (1 - sin^2) is
                // arithmetically exact, but its derivative is not: d(angle)/
                // d(sin) = 1/cos, which is unbounded at the quarter turn. A guard
                // sitting at full engagement then multiplies every wobble of the
                // detector - and the whole of a switch-off, which unwinds through
                // this same pole - into an unbounded step in the angle, and so
                // into a step in the signal. The angle form moves the pair by at
                // most a quarter turn per unit of correction at every level, and
                // the two trig calls ride the same rare branch the rest of the
                // rotation does (the guard is engaged only while a pair really is
                // opposed, which is why a slow 400 ms pole is enough to catch it).
                const float rotation = juce::jlimit (0.0f, 1.0f, antiPhaseCorrection)
                                       * juce::MathConstants<float>::halfPi;
                const float sinRotation = std::sin (rotation);
                const float cosRotation = std::cos (rotation);
                const float rotatedMid = guardMid * cosRotation + guardSide * sinRotation;
                const float rotatedSide = guardSide * cosRotation - guardMid * sinRotation;

                outputSignal[0] = rotatedMid + rotatedSide;
                outputSignal[1] = rotatedMid - rotatedSide;
            }
        }
        else
        {
            // Mono: there is no pair to be out of phase, so the guard's state is
            // allowed to relax rather than freezing at whatever the last stereo
            // block left it at.
            antiPhaseCorrection += (0.0f - antiPhaseCorrection) * antiPhaseCoefficient;
            antiPhaseAmount.store (0.0f, std::memory_order_relaxed);
        }

        // Final stereo width matrix. It runs immediately before output trim/limiting,
        // so the protection sees the widened peaks. 50% is unity Side, 0% is mono,
        // 100% is 2x Side; the Mid component remains unchanged.
        if (activeChannels == 2)
        {
            const float mid = 0.5f * (outputSignal[0] + outputSignal[1]);
            const float side = 0.5f * (outputSignal[0] - outputSignal[1]);
            outputSignal[0] = mid + side * currentWidth;
            outputSignal[1] = mid - side * currentWidth;
        }

        // Apply output trim before the limiter so boosts remain protected.
        for (int channel = 0; channel < activeChannels; ++channel)
            outputSignal[static_cast<std::size_t> (channel)] *= outputGain;

        // ---------------------------------------------------------------------
        //  Program-dependent safety limiter.
        //
        //  This exists so the soft clipper downstream stays idle. It watches the loudest
        //  channel of the frame and pulls the gain back whenever the signal is heading for
        //  the ceiling, then releases slowly enough to stay musical.
        //
        //  It is a GAIN stage rather than a shaper, so it adds no harmonics of its own:
        //  the only distortion in the output is the tape stage's, which is intentional.
        //  Normal material is limited transparently and only genuinely excessive level
        //  ever reaches the clipper.
        // ---------------------------------------------------------------------
        float framePeak = 0.0f;
        for (int channel = 0; channel < activeChannels; ++channel)
            framePeak = juce::jmax (framePeak, std::abs (outputSignal[static_cast<std::size_t> (channel)]));

        // A very fast attack catches transients before they overshoot; the release is
        // short enough to recover between events without pumping on sustained material.
        //
        // All four coefficients are BLOCK-RATE constants, built above the loop. They
        // used to be rebuilt here, which cost four std::exp per sample per channel -
        // and because the gain smoothing is a ternary, BOTH of its branches were
        // evaluated every sample and one thrown away.
        const auto detectorCoefficient = framePeak > preLimiterDetector ? limiterDetectorAttack
                                                                        : limiterDetectorRelease;
        preLimiterDetector += (framePeak - preLimiterDetector) * detectorCoefficient;

        // Gain required to bring the peak back to the ceiling. It is smoothed separately
        // from the detector so the correction is continuous and cannot click.
        const auto requiredGain = preLimiterDetector > limiterCeiling
                                    ? limiterCeiling / preLimiterDetector
                                    : 1.0f;
        // 0.4 ms catch, 120 ms recovery. The asymmetry is the point: a limiter
        // must catch instantly and let go slowly, or it pumps.
        const auto gainSmoothing = requiredGain < limiterGain ? limiterCatchCoefficient
                                                              : limiterRecoveryCoefficient;
        limiterGain += (requiredGain - limiterGain) * gainSmoothing;

        for (int channel = 0; channel < activeChannels; ++channel)
            outputSignal[static_cast<std::size_t> (channel)] *= limiterGain;

        for (int channel = 0; channel < activeChannels; ++channel)
        {
            const auto index = static_cast<std::size_t> (channel);
            auto& destination = channelData[index][sample];

            // DELTA listen: the finished signal minus the machine's own dry
            // reference. Both legs are computed on this frame, in this loop, from
            // the same input sample, so the subtraction is phase-perfect at every
            // oversampling factor with no compensation delay of its own. The
            // reference is the signal the machine WOULD have produced with the tape
            // stage silent - same input trim, same input compressor, same static
            // output gain - so what is left is character, not level.
            const auto delta = outputSignal[index] - machineDry[index];

            // Normal monitoring: the bypass crossfade, from the untouched host sample
            // to the finished signal. DELTA applies the SAME crossfade to the
            // difference rather than replacing it, so engaging BYPASS while DELTA
            // listens ramps the difference to silence instead of snapping to the dry
            // signal - and the early return above is held off until that ramp has
            // finished, so there is no step at the end of it either.
            const auto monitored = destination + (outputSignal[index] - destination) * bypassMix;
            const auto deltaMonitored = delta * bypassMix;

            // The two monitor positions themselves are crossfaded by the smoothed
            // DELTA ramp, so switching the button in or out is a 20 ms fade between
            // two continuous signals rather than a step. The meters keep reading the
            // finished output path below, so in delta mode they show the level of what
            // the machine adds.
            const auto blended = monitored + (deltaMonitored - monitored) * deltaMix;

            // Protection for the output, in two stages:
            //
            //   1. The safety limiter above keeps normal programme below the ceiling.
            //   2. The soft clipper only bends what still overshoots - a peak faster than
            //      the limiter's attack - so it is the last resort, not the mechanism that
            //      keeps the level in range.
            //
            // The flag therefore means "the soft clipper had to act", not merely "the
            // signal was loud", so the meter warns about real distortion.
            if (std::abs (blended) > 0.985f)
                clippingThisBlock = true;

            // ------------------------------------------------------------------
            //  LO-FI mode: the deliberate degradation.
            //
            //  Where MODERN opens the machine up, LO-FI closes it down: a hard
            //  bandwidth limit, a sample-and-hold quantisation (the bit-crush that
            //  gives the mode its grit) and a raised noise floor. It runs on the
            //  finished sample, after the protection chain, so the quantisation
            //  cannot be smoothed away by the limiter - the point of the mode is
            //  that it is a fault, and a fault should survive to the output.
            // ------------------------------------------------------------------
            float modeProcessed = blended;

            if (lofiMode)
            {
                auto& lofiLow = channel == 0 ? lofiLowL : lofiLowR;
                auto& lofiHold = channel == 0 ? lofiHoldL : lofiHoldR;

                // The bandwidth limit: a 3.2 kHz one-pole, which is the telephone/
                // cheap-radio band and the single most recognisable part of the
                // mode.
                lofiLow += (modeProcessed - lofiLow) * lofiLowCoefficient;

                // The sample-and-hold: the signal is held for a few samples, which
                // is a sample-rate reduction rather than a bit-depth one - it is
                // the aliasing that gives the mode its edge. The hold counter is
                // advanced once per frame by the caller, so both channels hold the
                // same instants and the image does not smear.
                // The counter is shared so both channels hold on the SAME instants -
                // per-channel counters would sample the two sides at different times
                // and smear the image. Each channel keeps its own held VALUE.
                if (channel == 0 && --lofiCounter <= 0)
                    lofiCounter = juce::jmax (1, lofiHoldSamples);

                if (lofiCounter >= lofiHoldSamples || channel == 0)
                    lofiHold = lofiLow;

                modeProcessed = lofiHold;
            }

            // The polarity switch flips the finished sample after the clipper, so a
            // 180-degree source mis-wiring is corrected at the very last stage.
            const auto limitedOut = softClip (modeProcessed) * polaritySign;
            destination = limitedOut;
            outputPeak = juce::jmax (outputPeak, std::abs (limitedOut));
            outputSquares += static_cast<double> (limitedOut) * limitedOut;

            // VU ballistics read the magnitude, so a polarity flip cannot make the
            // meter lie about the programme level.
            vuAverage += (std::abs (limitedOut) - vuAverage) * vuBallisticCoefficient;
        }

        // K-weighted loudness runs on the final stereo frame, after the width stage, so
        // it reports what actually leaves the plugin.
        //
        // It reads the channels back out of the buffer rather than off outputSignal,
        // for two reasons. The OUTPUT trim was already applied to outputSignal above,
        // so metering `outputSignal * outputGain` - which is what this used to do -
        // applied that trim a second time: the meter read 6 dB high for every decibel
        // of boost on OUTPUT, on a control that has nothing to do with loudness. And
        // the buffer holds the finished sample - the limiter's gain, the soft clipper
        // and the bypass crossfade have all run by this point - so this is the only
        // one of the three reads that is the signal the host actually receives. The
        // INPUT meter below reads its channels the same way.
        if (activeChannels == 2)
            currentLufs = outputLoudness.processFrame (channelData[0][sample],
                                                       channelData[1][sample],
                                                       outputLoudnessCoefficient);
        else if (activeChannels == 1)
            currentLufs = outputLoudness.processFrame (channelData[0][sample],
                                                       channelData[0][sample],
                                                       outputLoudnessCoefficient);

        // The INPUT meter runs the same K-weighting on the raw signal at the plugin's
        // own input, so the two meters can be compared directly. The channels are read
        // back from the buffer because the dry input was overwritten in place.
        if (activeChannels == 2)
            currentInputLufs = inputLoudness.processFrame (inputChainHistory[0], inputChainHistory[1],
                                                           inputLoudnessCoefficient);
        else if (activeChannels == 1)
            currentInputLufs = inputLoudness.processFrame (inputChainHistory[0], inputChainHistory[0],
                                                           inputLoudnessCoefficient);
    };

    for (int sample = 0; sample < numSamples; ++sample)
        processOneSample (sample);

    processPostMachineStages (block, blockContext);

    const auto measuredSamples = static_cast<double> (numSamples)
                               * static_cast<double> (juce::jmax (1, activeChannels));
    const float inputRms = measuredSamples > 0.0
        ? static_cast<float> (std::sqrt (inputSquares / measuredSamples)) : 0.0f;
    const float outputRms = measuredSamples > 0.0
        ? static_cast<float> (std::sqrt (outputSquares / measuredSamples)) : 0.0f;

    // Compressor-coupled saturation drive update (see the note at driveAmount): the
    // total gain reduction both stages applied this block is smoothed into
    // squeezeSaturationDrive over about 200 ms, so the tape stage reads programme
    // density rather than individual transients. Zero squeeze leaves the shaper
    // exactly as the DRIVE control set it; a slammed programme grows it smoothly.
    {
        const auto totalSqueezeDb = juce::jlimit (0.0f, 12.0f,
                                                  -(inputPeakReductionDb + peakReductionDb));
        const auto targetSqueeze = 1.0f - std::exp (-totalSqueezeDb * 0.20f);
        squeezeSaturationDrive += (targetSqueeze - squeezeSaturationDrive)
                                * squeezeDriveSmoothing;
    }

    // Noise-floor levelling is deliberately absent. See the note in the tape loop:
    // the hiss is a constant band-limited floor, not a programme-tracking one.

    const float driveInto = juce::jlimit (0.0f, 1.0f, inputRms * inputGainSmoothed.getCurrentValue()
                                                          * (0.5f + driveCurve));
    publishBlockTelemetry (blockContext, inputPeak, inputRms, outputPeak, outputRms,
                           inputPeakReductionDb, peakReductionDb, inputEnvelopeActivity,
                           driftAccumulator, clippingThisBlock, inputClippingThisBlock,
                           currentLufs, currentInputLufs, driveInto);

#if ! defined (J37_DSP_HARNESS) && defined (J37_ENABLE_HOTPATH_PROBE)
    // Flush the optional hot-path probe for this block. The probe in processOneSample
    // only increments counters; this is the one place the block-level summary is read
    // out, so the probe does not affect the audio path and does not allocate.
    hotPathThisBlockActive.store (hotPathThisSampleActive.load());
    if (hotPathThisSampleActive.load())
        ++hotPathActiveSampleCount;
#endif
}