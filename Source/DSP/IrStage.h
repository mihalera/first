// =============================================================================
//  The IR cabinet stage: a per-channel convolution against a user-loaded impulse
//  response, blended by IR MIX exactly the way the neural stage is blended by
//  NEURAL.
//
//  Moved out of PluginProcessor.h, prose and all. Nothing measures this stage - no
//  harness renders it - so what it earns is the file, the convention, and the one
//  automated look it does get: tests/dsp/check_headers_juce.sh compiles every header
//  in Source/DSP/ against the real JUCE modules, which for this file meant the check
//  had to grow two of them (see below).
//
//  The conventions of Source/DSP/ are stated in J37Math.h, and this is the header
//  that leans hardest on the second of them: these headers do not include JUCE
//  themselves, because a harness compiles them against a stand-in with no JUCE on
//  its include path. What this file uses is therefore what its INCLUDER has to have
//  declared, named here so the requirement is visible rather than implied:
//
//    juce_dsp             - Convolution, AudioBlock, ProcessSpec,
//                           ProcessContextReplacing
//    juce_audio_formats   - AudioFormatManager, AudioFormatReader
//    juce_core            - File, AudioBuffer
//
//  Inside the plugin that includer is JuceHeader.h, which carries all three. Under
//  tests/dsp/check_headers_juce.sh it is the module headers the script puts in front
//  of every header it compiles - which is why that script, not this file, is where
//  the two extra modules are named. A build that gets this wrong fails loudly on the
//  first declaration below, and that is the intended outcome: there is no stand-in
//  anywhere that could quietly answer for juce::dsp::Convolution.
// =============================================================================
#pragma once

//==============================================================================
/**
    The IR cabinet stage: a per-channel convolution against a user-loaded
    impulse response, blended by IR MIX exactly the way the neural stage is
    blended by NEURAL.

    One juce::dsp::Convolution per channel rather than one shared stereo one
    for the same reason the neural stage is two instances: the file the user
    picks is often a MONO cab or room capture, and loading it as stereo would
    correlate the two sides' reverberation tails. A mono IR is loaded once and
    both channels convolve against it independently, which is the reading a
    single-miked cabinet actually deserves. A stereo IR loads both sides and
    each channel keeps its own.

    The convolver is JUCE's zero-latency uniform FFT engine, so the stage adds
    NO latency of its own and never has to report any: the oversamplers' report
    stays the plugin's whole truth, and the stage never has to race the
    oversampling switch for the one latency number.

    The convolution runs OUTSIDE the per-sample loop, on the finished block,
    for the same reason the reverb does: it is a STEREO-tailed effect whose
    input must stay continuous with what the model and the machine produced,
    and it cannot live inside a loop that advances shared smoother state per
    frame.
*/
struct IrStage
{
    /** One convolver per channel, prepared in prepareToPlay. */
    juce::dsp::Convolution convolverL;
    juce::dsp::Convolution convolverR;

    /** True from the moment a file has been HANDED to the loader. The load
        itself finishes on JUCE's background thread; until it does the
        convolvers pass audio through untouched, which is the correct reading
        of "still loading" rather than a drop to dry. */
    bool hasIr = false;

    /** Stereo flag of the last load, so the scratch buffer is only resized
        when it has to be. */
    bool lastLoadWasStereo = false;

    /** The rate and block maximum the convolvers were last prepared for. The
        engine re-prepares them whenever the rate they actually see changes
        (the oversampling factor multiplies it), because the FFT plans and the
        internal buffers are per-rate state. */
    double preparedSampleRate = 0.0;
    int preparedMaxBlock = 0;

    void clear() noexcept { hasIr = false; }

    /** Reads a WAV/AIFF (anything JUCE's basic formats decode) and hands it
        to the channel convolvers. Returns false without touching the previous
        IR when the file has no readable audio. Called on the MESSAGE thread:
        loadImpulseResponse is documented as wait-free and does the decoding on
        its own background thread. */
    bool loadFromFile (const juce::File& file)
    {
        if (! file.existsAsFile())
            return false;

        // A first probe read decides mono vs stereo handling (and rejects
        // undecodable files) without allocating the audio.
        juce::AudioFormatManager manager;
        manager.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> probe (manager.createReaderFor (file));
        if (probe == nullptr || probe->lengthInSamples <= 0)
            return false;

        const bool stereo = probe->numChannels >= 2;

        // Stereo::no would MIX a stereo file down; a stereo IR must stay a
        // stereo IR, so the flag follows the file and the TRIM/normalise
        // defaults are the ones a cab/room capture wants.
        convolverL.loadImpulseResponse (file,
                                        stereo ? juce::dsp::Convolution::Stereo::yes
                                               : juce::dsp::Convolution::Stereo::no,
                                        juce::dsp::Convolution::Trim::yes, 0,
                                        juce::dsp::Convolution::Normalise::yes);
        convolverR.loadImpulseResponse (file,
                                        stereo ? juce::dsp::Convolution::Stereo::yes
                                               : juce::dsp::Convolution::Stereo::no,
                                        juce::dsp::Convolution::Trim::yes, 0,
                                        juce::dsp::Convolution::Normalise::yes);

        lastLoadWasStereo = stereo;
        hasIr = true;
        return true;
    }

    /** Prepares both convolvers for a rate/block change. Safe before any IR
        exists: the convolver treats a pre-prepare load as the initial state. */
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        convolverL.prepare (spec);
        convolverR.prepare (spec);
        preparedSampleRate = spec.sampleRate;
        preparedMaxBlock = static_cast<int> (spec.maximumBlockSize);
    }

    void reset() noexcept
    {
        convolverL.reset();
        convolverR.reset();
    }

    /** Convolves one in-place block of `numChannels` channels. Dry/wet is the
        CALLER's business (the IR MIX control crossfades against the un-IR'd
        signal, which the stage does not hold), so this is all-or-nothing and
        the pass-through case is decided before any processing is paid for. */
    void process (juce::AudioBuffer<float>& buffer, int numChannels, bool wet)
    {
        if (! hasIr || ! wet || numChannels <= 0)
            return;

        juce::dsp::AudioBlock<float> block (buffer);
        auto left = block.getSingleChannelBlock (0);

        if (numChannels == 1 || ! lastLoadWasStereo)
        {
            // Mono IR: each channel convolves against the SAME response, which
            // is one cabinet heard by two microphones rather than two
            // different rooms.
            convolverL.process (juce::dsp::ProcessContextReplacing<float> (left));
            if (numChannels == 2)
            {
                auto right = block.getSingleChannelBlock (1);
                convolverR.process (juce::dsp::ProcessContextReplacing<float> (right));
            }
        }
        else
        {
            // Stereo IR: the file's own two sides belong to the two channels,
            // so they are processed as the stereo pair they were captured as.
            auto pair = block.getSubsetChannelBlock (0, 2);
            convolverL.process (juce::dsp::ProcessContextReplacing<float> (pair));
        }
    }
};
