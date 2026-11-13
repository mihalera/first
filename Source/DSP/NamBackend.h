// =============================================================================
//  NamBackend - the second inference engine: NeuralAmpModelerCore.
//
//  Source/DSP/NeuralStage.h reads .nam files by CONVERTING them to RTNeural's
//  JSON, which is a re-shape of the flat weight vector and only covers the LSTM
//  architecture: NAM's WaveNet (and ConvNet, Linear, Sequential) have their own
//  weight layouts that no single re-shape covers. This header is the other half
//  of that story - the reference implementation itself, called through NAM
//  Core's own loader and per-sample loop.
//
//  Why it is a separate file with its own guarded half:
//
//    * OptionalLibraries.h probes J37_HAS_NAMCORE, and this header is the only
//      caller. Without the library (or without the option) the class below does
//      not exist at all, and NeuralStage's NAM member disappears with it - so a
//      build without NAM Core compiles the same code it always did.
//    * It is JUCE-free, like every header under Source/DSP/ (see J37Math.h): what
//      it uses is NAM Core's own API, nlohmann::json, and the C++ standard
//      library. That is what lets tests/neural compile it against the vendored
//      tree with no configure and no plugin build.
//    * It owns the whole lifetime of one model, and the AUDIO thread only ever
//      sees `process`, which is one call into NAM's per-sample loop with
//      pre-allocated buffers. NAM's models allocate on construction and in
//      `Reset` while resizing - both of which is exactly why this object is
//      built on the message thread and published by pointer in NeuralStage's
//      retirement list rather than swapped in place.
//
//  Loading is by JSON, not by path: NeuralStage already has the file's text in
//  hand (the picker reads the bytes for its own error reporting), and NAM Core
//  exposes `get_dsp(const nlohmann::json&)` for exactly that. Nothing here
//  touches the filesystem, so this header stays as testable as the rest of the
//  DSP headers.
//
//  The sample type is NAM Core's own `NAM_SAMPLE`, not float: the library
//  defaults to double and only switches to float when its build defines
//  NAM_SAMPLE_FLOAT, so spelling `float` here would either fail to compile
//  against a default build or silently disagree with one that set the macro.
//  The model's own arithmetic is float either way (Eigen::MatrixXf), so what the
//  conversion costs is one cast at the seam - `process` takes and returns float
//  because the rest of the engine does.
// =============================================================================
#pragma once

#include "OptionalLibraries.h"

#if J37_HAS_NAMCORE
    // NAM Core's loader and the DSP base class. Its own headers are spelled
    // <NAM/...> because ThirdParty/namcore is the include root, exactly like the
    // upstream project's own layout.
    #include <NAM/dsp.h>
    #include <NAM/get_dsp.h>

    #include <nlohmann/json.hpp>

    #include <memory>
    #include <stdexcept>
    #include <string>
#endif

namespace j37dsp
{

#if J37_HAS_NAMCORE
/**
    One NAM Core model, in the shape NeuralStage wants to drive it in.

    The interface is deliberately the same four calls the RTNeural path has -
    load, prepare, process, reset - so the stage's own bookkeeping (rate bridge,
    active pointer, retirement list, reset requests) does not learn a second
    vocabulary to talk to the second engine.

    Threading, stated once, because it is the whole reason the class is this
    small:

      * `load` and `prepare` are MESSAGE thread calls (a model load, a session
        rate or oversampling change). Both allocate; neither is real-time safe.
      * `process` and `reset` are AUDIO thread calls. `process` writes one sample
        into a pre-allocated frame, calls NAM's per-frame loop, and copies one
        sample out: no allocation, no lock, no filesystem. `reset` re-runs NAM's
        own `Reset` at the arguments `prepare` established - and that is the one
        place the library's "Reset may allocate" contract needs care, so the
        reasoning is written out at the call site below.
*/
class NamBackend
{
public:
    NamBackend() = default;

    NamBackend (const NamBackend&) = delete;
    NamBackend& operator= (const NamBackend&) = delete;

    /** Builds a model from a .nam file's parsed JSON. Returns false - leaving the
        backend with no model - when NAM Core refuses the document, which it does
        loudly: an unknown architecture, a weights array of the wrong length, or
        a version outside the range the library supports.

        `prewarm` is turned OFF deliberately, twice over. NAM Core's prewarm runs
        the model over silence to fill its ring buffers, which is the right thing
        for a block-based host that wants the first block to be steady state and
        the wrong thing here: the first samples of a take are what the player
        heard, and the reference player's own comparison window starts after the
        model's receptive field rather than after a silence run nobody asked for.

        `DspLoadOptions` only decides what happens DURING get_dsp(); the returned
        model is restored to the caller's previous thread-local default, which is
        ON. So the instance's own switch is turned off as well, or the first
        `Reset` would prewarm anyway. */
    bool load (const nlohmann::json& namFile)
    {
        nam::DspLoadOptions options;
        options.prewarm = false;

        std::unique_ptr<nam::DSP> built;

        try
        {
            built = nam::get_dsp (namFile, options);
        }
        catch (const std::exception& error)
        {
            lastErrorMessage = error.what();
            return false;
        }
        catch (...)
        {
            lastErrorMessage = "the model could not be built";
            return false;
        }

        if (built == nullptr)
        {
            lastErrorMessage = "the model could not be built";
            return false;
        }

        if (built->NumInputChannels() != 1 || built->NumOutputChannels() != 1)
        {
            // The stage sits in the mono per-channel chain: a stereo model would
            // need two input frames and its own channel policy, which is a
            // different feature rather than a bigger buffer.
            lastErrorMessage = "the model is not one input, one output";
            return false;
        }

        built->SetPrewarmOnReset (false);

        model = std::move (built);
        modelRate = model->GetExpectedSampleRate() > 0.0 ? model->GetExpectedSampleRate() : 48000.0;
        preparedRate = 0.0;
        lastErrorMessage = {};
        return true;
    }

    /** Sizes the model's own buffers for a sample rate and clears its state.
        Allocates, so it runs where `load` does - never on the audio thread.

        NAM Core sizes its ring buffers lazily: the first `process` call grows
        them, the output vectors are resized per call, and `Reset` resizes the
        condition matrices. One zero frame is therefore pushed through here,
        while allocating is allowed, and the state is dropped immediately after.
        What the audio thread meets afterwards is a model whose buffers are
        already the size it asked for. */
    void prepare (double sampleRate)
    {
        if (model == nullptr)
            return;

        const auto rate = sampleRate > 0.0 ? sampleRate : modelRate;

        try
        {
            model->Reset (rate, maxFrames);
            preparedRate = rate;

            inputFrame[0] = (NAM_SAMPLE) 0.0;
            outputFrame[0] = (NAM_SAMPLE) 0.0;
            model->process (inputPointers, outputPointers, maxFrames);

            model->Reset (rate, maxFrames);
        }
        catch (...)
        {
            // A rate the model cannot accept leaves the model where it was rather
            // than half-resized: the stage's bridge keeps feeding the old rate,
            // which is what it did before this call.
            preparedRate = 0.0;
        }
    }

    /** One sample in, one sample out. NAM Core's loop is written for blocks, so
        this stage feeds it blocks of ONE - which is what a per-sample stage in a
        plugin's inner loop can do without a lookahead it does not have. NAM
        copies its input into a ring buffer per frame, and the ring buffer's
        contents at reset decide the transient, not the block size. */
    float process (float x) noexcept
    {
        if (model == nullptr || preparedRate <= 0.0)
            return x;

        inputFrame[0] = (NAM_SAMPLE) x;
        outputFrame[0] = (NAM_SAMPLE) x;

        model->process (inputPointers, outputPointers, maxFrames);

        return (float) outputFrame[0];
    }

    /** Drops the model's own memory of the past. Called when the stage is told to
        reset, on a model swap, or on a rate change.

        This is an AUDIO thread call, so it must not allocate - and NAM Core's
        contract for `Reset` says it MAY. What makes it safe here is that the two
        arguments are the ones `prepare` already established: the frame count is
        the compile-time constant one and the rate is the one the model was
        prepared at, and NAM Core only resizes when a dimension actually CHANGES
        (`std::vector::resize` to the same size and `Eigen::MatrixXf::resize` to
        the same shape both keep their storage). The buffers are therefore the
        size this call asks for, and what is left is NAM's own zero-fill.

        A backend that was never prepared has nothing to clear, so the call is a
        no-op rather than a Reset against a size the buffers were not grown for. */
    void reset() noexcept
    {
        if (model == nullptr || preparedRate <= 0.0)
            return;

        try
        {
            model->Reset (preparedRate, maxFrames);
        }
        catch (...)
        {
            // Reset is documented to be able to throw on a rate a model cannot
            // run at; there is no better answer on this thread than leaving the
            // state where it is, and the next prepare() will settle it.
        }
    }

    bool isLoaded() const noexcept { return model != nullptr; }

    /** The rate the model was trained at, which is the rate the stage's bridge
        should feed it at. */
    double modelSampleRate() const noexcept { return modelRate; }

    /** True once `prepare` has sized the model - which is what tells a caller the
        audio thread can actually run it. */
    bool isPrepared() const noexcept { return model != nullptr && preparedRate > 0.0; }

    const std::string& lastError() const noexcept { return lastErrorMessage; }

private:
    // One frame at a time: the buffer size the model is prepared for, and the
    // frame this class hands to it.
    static constexpr int maxFrames = 1;

    std::unique_ptr<nam::DSP> model;
    double modelRate = 48000.0;
    double preparedRate = 0.0;
    std::string lastErrorMessage;

    NAM_SAMPLE inputFrame[maxFrames] { (NAM_SAMPLE) 0.0 };
    NAM_SAMPLE outputFrame[maxFrames] { (NAM_SAMPLE) 0.0 };
    NAM_SAMPLE* inputPointers[maxFrames] { inputFrame };
    NAM_SAMPLE* outputPointers[maxFrames] { outputFrame };
};
#endif

} // namespace j37dsp
