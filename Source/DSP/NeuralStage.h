// =============================================================================
//  NeuralStage - an optional RTNeural model run as a per-channel nonlinearity.
//
//  Moved out of PluginProcessor.h, so the harnesses include the stage the plugin
//  compiles instead of having its text cut out of the plugin's header. It is
//  compiled in two shapes and BOTH are measured: tests/dsp/ includes it with
//  J37_HAS_RTNEURAL 0, which is the bit-for-bit pass-through every build without
//  the library has, and tests/neural/stage_test.cpp with it 1, against a stand-in
//  for RTNeural's own two-method API - the only way the guarded half of the stage
//  is compiled on a machine that has no RTNeural.
//
//  The conventions of Source/DSP/ are stated in J37Math.h. This is the one header
//  there that includes another one of the directory's files for a reason other
//  than a member's type: the stage's two shapes are chosen by a macro, so the file
//  that decides that macro has to be read before this one.
// =============================================================================
#pragma once

// Which optional libraries this build has. It is what defines J37_HAS_RTNEURAL,
// and a build may define that itself instead (see the note in that header).
#include "OptionalLibraries.h"

// The stage HOLDS a RateBridge by value, so its type has to be complete here.
#include "RateBridge.h"

#if J37_HAS_RTNEURAL
    // The .nam converter below parses and rebuilds JSON - the type is part of this
    // header's own interface whenever the guarded half of it is compiled - so the
    // header includes it rather than relying on the probe having done so.
    #include <nlohmann/json.hpp>
#endif

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

//==============================================================================
/**
    NeuralStage - an optional RTNeural model run as a per-channel nonlinearity.

    The other stages in this plugin are hand-written curves, each chosen because
    it models a specific piece of hardware. A neural model is the opposite
    approach: instead of a curve being derived from physics, one is LEARNED from
    measurements of a real device, and RTNeural runs it fast enough to sit inside
    a per-sample loop.

    Why the integration is shaped the way it is:

      - It is OPTIONAL and INERT BY DEFAULT. No model is compiled in and none is
        loaded from disk by the engine, so a build without a model is exactly the
        plugin it was before this stage existed. The stage is a bit-for-bit
        pass-through until a model is installed, which is what keeps every
        existing preset, session and regression baseline valid.

      - It is DTYPE-FLOAT, matching the engine's own float path. The model file
        decides its own topology (a Dense net, an LSTM, a GRU); RTNeural's JSON
        loader builds the right one. This wrapper only owns the frame plumbing -
        one input, one output, both carried per channel.

      - It is guarded. J37_HAS_RTNEURAL is 0 under the DSP harness and where the
        library was not fetched, and every member below then reduces to the
        pass-through. The class compiles either way, so no call site needs an
        conditional guards around it.

    A 1-in/1-out model is deliberate: the stage sits IN the mono per-channel
    chain, so a stereo pair is two instances of the same model rather than one
    two-input model trying to model the inter-channel relationship. That keeps
    the stage's meaning the same as every other stage's - a mono nonlinearity
    applied per channel - and keeps the stereo image entirely in the engine's
    own hands.
*/
struct NeuralStage
{
    // -----------------------------------------------------------------------
    //  The model. Owned behind a unique_ptr so the type does not have to be
    //  complete in the header (RTNeural's model types are templates over the
    //  layer list), and so a stage with no model costs one pointer and one
    //  branch per sample.
    //
    //  Under the harness, or in a build without RTNeural, the member does not
    //  exist and every method below is the pass-through.
    // -----------------------------------------------------------------------
#if J37_HAS_RTNEURAL
    using ModelType = RTNeural::Model<float>;

    std::unique_ptr<ModelType> model;
    // Old model objects are kept alive until stage destruction. Model replacement
    // is rare; retaining them avoids deallocation while an audio callback may be
    // finishing inference with a previously loaded pointer.
    std::vector<std::unique_ptr<ModelType>> retiredModels;
    std::mutex controlMutex;
    std::atomic<ModelType*> activeModel { nullptr };
    std::atomic<bool> clearRequested { false };
    std::atomic<bool> resetRequested { false };

    // The conversion, for a model whose own rate is not the engine's (see
    // RateBridge). With the two rates equal the bridge is a single branch and
    // the stage is exactly what it was before it existed.
    RateBridge bridge;

    // The two rates, and whether the bridge has been told about them yet. They
    // arrive from whoever knows: a model load on the MESSAGE thread, a session
    // rate or an oversampling change from either thread - and the oversampling
    // switch really is made from the audio thread, because it is a host
    // parameter. The bridge itself is audio-thread state, so it is prepared on
    // the audio thread's next inference instead - the same discipline the model
    // pointer above follows.
    std::atomic<double> wantedModelRate { 0.0 };
    std::atomic<double> wantedEngineRate { 0.0 };
    std::atomic<bool> ratesPending { false };

    /** The model's input frame. RTNeural's forward() takes a pointer to a
        float[inSize] and RETURNS the single output sample by value, so one
        element is all this stage needs - it is 1-in/1-out. */
    float inputFrame[1] { 0.0f };
#endif

    /** True when a model is installed and ready to run. */
    bool isActive() const noexcept
    {
#if J37_HAS_RTNEURAL
        return activeModel.load (std::memory_order_acquire) != nullptr
            && ! clearRequested.load (std::memory_order_acquire);
#else
        return false;
#endif
    }

    /**
        Loads a model from RTNeural's JSON format. Returns false - leaving any
        previously loaded model untouched but the stage inactive - when the text is
        empty, malformed, or the library is not in the build.

        RTNeural's parser takes a parsed nlohmann::json (or an input stream), NOT a
        string, so the text is parsed here first. nlohmann::json is already fetched
        by CMakeLists (and RTNeural bundles its own copy), so this adds no dependency.

        This is called from the MESSAGE thread (a preset load, or the editor's model
        picker), never from processBlock, so the parse allocation is safe. The audio
        thread only ever reads the resulting model through process().
    */
    bool loadFromJson (const juce::String& json)
    {
    #if J37_HAS_RTNEURAL
        if (json.isEmpty())
            return false;

        std::unique_ptr<ModelType> parsedModel;
        try
        {
            // nlohmann::json::parse throws on malformed input, which the catch below
            // turns into a clean "no model" rather than a crash or a diagnostic the
            // user cannot act on.
            const auto parsed = nlohmann::json::parse (json.toStdString());
            parsedModel = RTNeural::json_parser::parseJson<float> (parsed, false);
            if (parsedModel == nullptr)
                return false;
        }
        catch (...)
        {
            return false;
        }

        std::lock_guard<std::mutex> lock (controlMutex);
        // Retain the previous model until teardown. Audio threads may still hold
        // the pointer they loaded before publication of the replacement.
        clearRequested.store (true, std::memory_order_release);
        if (model != nullptr)
            retiredModels.push_back (std::move (model));
        model = std::move (parsedModel);
        activeModel.store (model.get(), std::memory_order_release);
        clearRequested.store (false, std::memory_order_release);
        resetRequested.store (true, std::memory_order_release);
        return true;
    #else
        juce::ignoreUnused (json);
        return false;
    #endif
    }

    /** Tells the stage the rate the MODEL was trained at - 0 means unknown, which
        keeps the model on the engine's own samples, exactly the behaviour every
        build had before the bridge existed - and the rate the ENGINE is running
        at, so a model trained at 48 kHz is run at 48 kHz whatever the engine is
        doing (see RateBridge).

        Called whenever either rate can have changed: a model load, a session rate
        change, or a change of oversampling factor. Realtime-safe - it writes three
        atomics and nothing else - because the oversampling switch arrives on the
        audio thread. */
    void setSampleRates (double modelRate, double engineRate) noexcept
    {
#if J37_HAS_RTNEURAL
        wantedModelRate.store (modelRate, std::memory_order_relaxed);
        wantedEngineRate.store (engineRate, std::memory_order_relaxed);
        ratesPending.store (true, std::memory_order_release);
#else
        juce::ignoreUnused (modelRate, engineRate);
#endif
    }

    /** Releases the model, returning the stage to the pass-through. */
    void clear() noexcept
    {
#if J37_HAS_RTNEURAL
        std::lock_guard<std::mutex> lock (controlMutex);
        clearRequested.store (true, std::memory_order_release);
        activeModel.store (nullptr, std::memory_order_release);
        if (model != nullptr)
            retiredModels.push_back (std::move (model));
        resetRequested.store (true, std::memory_order_release);
#endif
    }

#if J37_HAS_RTNEURAL
    /**
        Builds the stage from a NeuralAmpModeler (.nam) file's text.

        A .nam file is a JSON document the NAM core describes in terms of
        PyTorch's LSTM layout: one flat `weights` vector holding the input
        kernel, the recurrent kernel and the bias of every layer back to back
        (each row-major, each in the i / f / c / o gate order PyTorch trains
        in), then the layer's initial hidden and cell states, and - for the
        top layer - a linear output head and its bias. RTNeural's JSON
        describes the SAME network as a stack of named layers, and its LSTMLayer
        consumes the gates in PyTorch's own order (its setters shuffle them
        into TensorFlow's layout internally), so the conversion is a re-shape:
        slice the flat vector by the config's sizes and hand the slices to the
        loader as "lstm" and "dense" layers.

        One real transposition is involved: NAM puts the four gate blocks on
        the ROWS of each kernel (PyTorch's row-major [4H x (I+H)]) while
        RTNeural's loader indexes the same coefficients with the taps on the
        rows ([in][4H]), so the x- and h-parts of every kernel are read back
        column-wise. The gate order inside each block is untouched, and the
        hidden/cell seeds RTNeural's layers start at zero rather than the
        file's, which differs from the reference player only during the model's
        own prewarm window and nowhere after it. Any unexpected shape throws,
        and the caller turns a throw into the same "no model" outcome a bad
        RTNeural file already produces.
    */
    bool buildFromNam (const nlohmann::json& namJson)
    {
        // Only the LSTM architecture is converted. NAM's WaveNet and the
        // convolutional "Feather" nets have their own weight layouts that no
        // single re-shape covers, and pretending otherwise would produce a
        // model that loads and sounds wrong rather than one that fails loudly.
        if (! namJson.contains ("architecture")
            || ! namJson["architecture"].is_string()
            || namJson["architecture"].get<std::string>() != "LSTM")
            return false;

        std::unique_ptr<ModelType> parsedModel;
        try
        {
            const auto& config = namJson.at ("config");
            const auto numLayers = config.at ("num_layers").get<int>();
            const auto inputSize = config.at ("input_size").get<int>();
            const auto hiddenSize = config.at ("hidden_size").get<int>();

        // A .nam file's weights array is 1-D JSON. Anything else is not a
        // shape this converter understands.
        if (! namJson.contains ("weights") || ! namJson["weights"].is_array())
            return false;

        const auto& weights = namJson["weights"];

        // Slice offsets, counted exactly as nam::lstm::LSTM consumes the flat
        // vector: every layer's block is its kernel stored row-major (4H rows
        // in PyTorch's i/f/g/o gate order by I+H columns of input taps then
        // recurrent taps), then the flat bias (4H), then the hidden and cell
        // seeds (H each). Only the first layer has I input taps; deeper ones
        // take the previous layer's H hidden outputs, so their stride is H+H.
        // The topmost block additionally carries the output head (out x H,
        // row-major) and its bias (out), which become the "dense" layer.
        const auto biasElements = 4 * hiddenSize;
        auto kernelElementsFor = [hiddenSize] (int layerIn)
        {
            return 4 * hiddenSize * (layerIn + hiddenSize);
        };
        auto blockElementsFor = [kernelElementsFor, biasElements, hiddenSize] (int layerIn)
        {
            return kernelElementsFor (layerIn) + biasElements + 2 * hiddenSize;
        };
        auto expectedElements = hiddenSize + 1;
        for (int layer = 0; layer < numLayers; ++layer)
            expectedElements += blockElementsFor (layer == 0 ? inputSize : hiddenSize);

        if (numLayers < 1 || inputSize < 1 || hiddenSize < 1
            || static_cast<int> (weights.size()) != expectedElements)
            return false;

        auto readFlat = [&weights] (int offset, int count)
        {
            std::vector<float> slice;
            slice.reserve (static_cast<std::size_t> (count));
            for (int i = 0; i < count; ++i)
                slice.push_back (weights.at (static_cast<std::size_t> (offset + i)).get<float>());
            return slice;
        };

        // The head, kept for the [rows][columns] reads the head needs.
        auto kernel2D = [&readFlat] (int offset, int rows, int columns)
        {
            std::vector<std::vector<float>> rowsOut;
            rowsOut.reserve (static_cast<std::size_t> (rows));
            for (int r = 0; r < rows; ++r)
                rowsOut.push_back (readFlat (offset + r * columns, columns));
            return rowsOut;
        };

        // One layer's kernel block reshaped into the two matrices RTNeural's
        // loader indexes by: an [inputRows][4H] kernel for the input taps and
        // an [H][4H] one for the recurrent taps. NAM stores the whole matrix
        // row-major with the gates on the rows (PyTorch's i/f/g/o order), so
        // reading it back column-wise moves the taps onto the rows and the
        // gates into the columns without touching the gate order - exactly
        // the mapping RTNeural's own setters then regroup per gate.
        auto splitKernel = [&weights, hiddenSize, biasElements] (int offset, int inputRows)
        {
            const auto stride = inputRows + hiddenSize;
            std::vector<std::vector<float>> kernel, recurrent;
            kernel.reserve (static_cast<std::size_t> (inputRows));
            recurrent.reserve (static_cast<std::size_t> (hiddenSize));
            for (int i = 0; i < inputRows; ++i)
            {
                std::vector<float> row;
                row.reserve (static_cast<std::size_t> (biasElements));
                for (int k = 0; k < biasElements; ++k)
                    row.push_back (weights.at (static_cast<std::size_t> (offset + k * stride + i)).get<float>());
                kernel.push_back (std::move (row));
            }
            for (int j = 0; j < hiddenSize; ++j)
            {
                std::vector<float> row;
                row.reserve (static_cast<std::size_t> (biasElements));
                for (int k = 0; k < biasElements; ++k)
                    row.push_back (weights.at (static_cast<std::size_t> (offset + k * stride + inputRows + j)).get<float>());
                recurrent.push_back (std::move (row));
            }
            return std::make_pair (std::move (kernel), std::move (recurrent));
        };

        // The RTNeural document this re-shape produces: `inputSize` inputs,
        // one "lstm" layer per NAM layer, then the head as a "dense" layer.
        // The gate order inside each 4H column block stays exactly as
        // PyTorch trained it - RTNeural's own setters do that regrouping, so
        // a second permutation here would be a second bug.
        nlohmann::json layerList = nlohmann::json::array();
        int cursor = 0;

        for (int layer = 0; layer < numLayers; ++layer)
        {
            const auto layerIn = layer == 0 ? inputSize : hiddenSize;
            auto kernelPair = splitKernel (cursor, layerIn);
            const auto bias = readFlat (cursor + kernelElementsFor (layerIn), biasElements);
            cursor += blockElementsFor (layerIn);

            layerList.push_back ({ { "type", "lstm" },
                                   { "shape", nlohmann::json::array ({ hiddenSize }) },
                                   { "weights", nlohmann::json::array (
                                       { kernelPair.first, kernelPair.second, bias }) } });
        }

        // The head is stored row-major [out x H]; RTNeural's dense loader
        // expects [in][out] and transposes on the way in, so each hidden tap
        // becomes one single-element row.
        const auto headWeight = kernel2D (cursor, hiddenSize, 1);
        const auto headBias = readFlat (cursor + hiddenSize, 1);
        layerList.push_back ({ { "type", "dense" },
                               { "shape", nlohmann::json::array ({ 1 }) },
                               { "weights", nlohmann::json::array ({ headWeight, headBias }) } });

        nlohmann::json rtJson { { "in_shape", nlohmann::json::array ({ inputSize }) },
                                { "layers", layerList } };

        try
        {
            parsedModel = RTNeural::json_parser::parseJson<float> (rtJson, false);
            if (parsedModel == nullptr)
                return false;
        }
        catch (...)
        {
            return false;
        }

        std::lock_guard<std::mutex> lock (controlMutex);
        clearRequested.store (true, std::memory_order_release);
        if (model != nullptr)
            retiredModels.push_back (std::move (model));
        model = std::move (parsedModel);
        activeModel.store (model.get(), std::memory_order_release);
        clearRequested.store (false, std::memory_order_release);
        resetRequested.store (true, std::memory_order_release);
        return true;
        }
        catch (...)
        {
            return false;
        }
    }
#endif
    /** The model's own recurrent state, let go of on a rate change or a model
        swap so neither can resume from a stale hidden state. */
    void reset() noexcept
    {
#if J37_HAS_RTNEURAL
        // Recurrent state is touched only by the audio thread. Defer reset to its
        // next inference, avoiding concurrent reset during model swaps/rate changes.
        resetRequested.store (true, std::memory_order_release);
#endif
    }

    /**
        One sample through the model, or straight through when there is none.

        `amount` is the dry/wet position: 0 is the untouched input, 1 is the
        model's own output, and the crossfade is linear so the stage can be
        blended in and out without a step. A model that is present but at
        amount 0 therefore costs the inference but changes nothing audibly -
        which is the correct reading of a wet/dry control rather than a bug.
    */
    float process (float x, float amount) noexcept
    {
#if J37_HAS_RTNEURAL
        if (amount <= 1.0e-5f)
            return x;

        auto* active = activeModel.load (std::memory_order_acquire);
        if (active == nullptr || clearRequested.load (std::memory_order_acquire))
            return x;

        if (activeModel.load (std::memory_order_acquire) != active
            || clearRequested.load (std::memory_order_acquire))
            return x;

        // The rates, latched here rather than written into the bridge by the
        // thread that knows them: see `setSampleRates`. Checked per sample, which
        // is one relaxed load against the two the guards above already make, and
        // it has to be here rather than per block because this stage is called
        // per sample.
        if (ratesPending.load (std::memory_order_acquire))
        {
            bridge.prepare (wantedModelRate.load (std::memory_order_relaxed),
                            wantedEngineRate.load (std::memory_order_relaxed));
            ratesPending.store (false, std::memory_order_release);
        }

        if (resetRequested.exchange (false, std::memory_order_acq_rel))
        {
            active->reset();

            // The model's own memory is gone, so the conversion's is too: what the
            // bridge is holding are that model's answers.
            bridge.reset();
        }

        // The frame the model is handed: the sample itself when the two rates
        // agree, and the signal carried to the model's own rate when they do not.
        // Either way the stage's crossfade runs between the bridge's two legs,
        // which are in phase with each other.
        const auto converted = bridge.process (x, [active, this] (float input) noexcept
        {
            inputFrame[0] = input;

            // RTNeural's Model<T>::forward(const T*) returns the single output
            // value.
            return active->forward (inputFrame);
        });

        return converted.dry
             + (converted.wet - converted.dry) * juce::jlimit (0.0f, 1.0f, amount);
#else
        juce::ignoreUnused (amount);
        return x;
#endif
    }
};
