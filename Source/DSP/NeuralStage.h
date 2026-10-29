// =============================================================================
//  NeuralStage - an optional neural model run as a per-channel nonlinearity.
//
//  Moved out of PluginProcessor.h, so the harnesses include the stage the plugin
//  compiles instead of having its text cut out of the plugin's header. It is
//  compiled in THREE shapes and all three are measured:
//
//    * tests/dsp/ includes it with J37_HAS_RTNEURAL 0 and J37_HAS_NAMCORE 0,
//      which is the bit-for-bit pass-through every build without either library
//      has;
//    * tests/neural/stage_test.cpp with J37_HAS_RTNEURAL 1, against a stand-in
//      for RTNeural's own two-method API - the only way the guarded half of the
//      stage is compiled on a machine that has no RTNeural;
//    * tests/neural/fixture_test.cpp with the REAL RTNeural and the REAL NAM
//      Core, which is where .nam files are loaded and compared with the
//      reference player's own output.
//
//  The conventions of Source/DSP/ are stated in J37Math.h. This is the one header
//  there that includes another one of the directory's files for a reason other
//  than a member's type: the stage's two shapes are chosen by a macro, so the file
//  that decides that macro has to be read before this one.
// =============================================================================
#pragma once

// Which optional libraries this build has. It is what defines J37_HAS_RTNEURAL
// and J37_HAS_NAMCORE, and a build may define either itself instead (see the
// note in that header).
#include "OptionalLibraries.h"

// The stage HOLDS a RateBridge by value, so its type has to be complete here.
#include "RateBridge.h"

// The second engine. Without J37_HAS_NAMCORE this header is empty by design, so
// including it unconditionally is what keeps every mention of it below behind
// one guard rather than two.
#include "NamBackend.h"

#if J37_HAS_RTNEURAL || J37_HAS_NAMCORE
    // The .nam converter below parses and rebuilds JSON - the type is part of this
    // header's own interface whenever either engine is compiled - so the header
    // includes it rather than relying on the probe having done so.
    #include <nlohmann/json.hpp>
#endif

#include <atomic>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

//==============================================================================
/**
    NeuralStage - an optional neural model run as a per-channel nonlinearity.

    The other stages in this plugin are hand-written curves, each chosen because
    it models a specific piece of hardware. A neural model is the opposite
    approach: instead of a curve being derived from physics, one is LEARNED from
    measurements of a real device, and the inference engine runs it fast enough to
    sit inside a per-sample loop.

    Why the integration is shaped the way it is:

      - It is OPTIONAL and INERT BY DEFAULT. No model is compiled in and none is
        loaded from disk by the engine, so a build without a model is exactly the
        plugin it was before this stage existed. The stage is a bit-for-bit
        pass-through until a model is installed, which is what keeps every
        existing preset, session and regression baseline valid.

      - It is DTYPE-FLOAT, matching the engine's own float path. RTNeural's model
        file decides its own topology (a Dense net, an LSTM, a GRU); RTNeural's
        JSON loader builds the right one. A .nam file is different: it is NAM's
        own format, and its model is built by NAM Core.

      - It has TWO ENGINES behind one seam, and the seam is the point. RTNeural
        runs the LSTM conversion below; NAM Core (`Source/DSP/NamBackend.h`)
        runs everything else - WaveNet, ConvNet, Linear, Sequential - and the
        LSTM too in a build that has NAM Core without RTNeural. Which engine
        builds a given file is `buildFromNam`'s one decision, and nothing else in
        the stage knows the difference: the rate bridge, the active pointer, the
        retirement list and the reset requests all talk to an `InferenceModel`,
        which is one `forward` and one `reset`.

      - LSTM stays with RTNeural even when both are in the build. That is not
        inertia: it is what keeps the two engines MEASURED against each other.
        tests/neural/fixture_test.cpp drives the same fixture through both and
        requires their outputs to agree to -100 dBFS; routing LSTM to NAM Core
        would delete the comparison and leave only the oracle.

      - It is guarded. J37_HAS_RTNEURAL and J37_HAS_NAMCORE are 0 under the DSP
        harness and where neither library was fetched, and every member below
        then reduces to the pass-through. The class compiles either way, so no
        call site needs a conditional guard around it.

    A 1-in/1-out model is deliberate: the stage sits IN the mono per-channel
    chain, so a stereo pair is two instances of the same model rather than one
    two-input model trying to model the inter-channel relationship. That keeps
    the stage's meaning the same as every other stage's - a mono nonlinearity
    applied per channel - and keeps the stereo image entirely in the engine's
    own hands.
*/
struct NeuralStage
{
#if J37_HAS_RTNEURAL || J37_HAS_NAMCORE
    // -----------------------------------------------------------------------
    //  What the stage asks of a model, whichever engine built it.
    //
    //  A virtual call per sample rather than a branch on "which engine". Both
    //  engines already dispatch virtually inside `forward` - NAM through its
    //  `DSP` hierarchy, RTNeural through `Model::forward` - so the seam adds one
    //  more dispatch to a loop that makes one anyway, and buys the retirement
    //  list, the active pointer and the rate bridge written once.
    // -----------------------------------------------------------------------
    struct InferenceModel
    {
        virtual ~InferenceModel() = default;

        /** One sample in, one sample out. */
        virtual float forward (float x) noexcept = 0;

        /** Drops the model's memory of the past. */
        virtual void reset() noexcept = 0;
    };

    #if J37_HAS_RTNEURAL
    /**
        RTNeural, as an `InferenceModel`.

        The frame member lives HERE rather than on the stage because it is the
        adapter's business to know how its engine wants a sample - RTNeural's
        `forward` takes a pointer to a float[inSize] and RETURNS the single
        output by value - and because a stage with no RTNeural should not carry
        an array it can never fill.
    */
    struct RtNeuralModel final : InferenceModel
    {
        explicit RtNeuralModel (std::unique_ptr<RTNeural::Model<float>> built)
            : model (std::move (built))
        {
        }

        float forward (float x) noexcept override
        {
            inputFrame[0] = x;
            return model->forward (inputFrame);
        }

        void reset() noexcept override { model->reset(); }

        std::unique_ptr<RTNeural::Model<float>> model;
        float inputFrame[1] { 0.0f };
    };
    #endif

    #if J37_HAS_NAMCORE
    /** NAM Core, as an `InferenceModel`. The backend owns everything NAM needs;
        this adapter only renames its calls to the seam's. */
    struct NamCoreModel final : InferenceModel
    {
        float forward (float x) noexcept override { return backend.process (x); }
        void reset() noexcept override { backend.reset(); }

        j37dsp::NamBackend backend;
    };
    #endif

    // -----------------------------------------------------------------------
    //  The model. Owned behind an `InferenceModel` pointer so the type does not
    //  have to be complete in the header (RTNeural's model types are templates
    //  over the layer list, NAM's are a class hierarchy), and so a stage with no
    //  model costs one pointer and one branch per sample.
    //
    //  Under the harness, or in a build without either engine, these members do
    //  not exist and every method below is the pass-through.
    // -----------------------------------------------------------------------
    std::unique_ptr<InferenceModel> model;
    std::unique_ptr<InferenceModel> pendingModel;
    std::unique_ptr<InferenceModel> retiredModel;
    std::mutex controlMutex;
    std::atomic<InferenceModel*> activeModel { nullptr };
    std::atomic<bool> clearRequested { false };
    std::atomic<bool> audioQuiescent { true };
    std::atomic<bool> resetRequested { false };

    //  True from the moment a built model has been PUBLISHED - which happens on
    //  the message thread, before the audio thread's next block adopts it. See
    //  hasModel() below for why the distinction matters to the panel.
    std::atomic<bool> modelInstalled { false };

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

    //  Why the last buildFromNam() produced nothing, in the engine's own words.
    //  Message thread only - the picker reads it straight after a refused load,
    //  on the same call, so it needs no guard and no lifetime of its own. Empty
    //  after a successful build and before the first attempt.
    std::string lastBuildError;
#endif

    /** The detail behind a refused .nam build - the engine's own sentence, or
        empty when the refusal came from somewhere that had nothing to add. */
    const std::string& lastBuildErrorText() const noexcept
    {
#if J37_HAS_RTNEURAL || J37_HAS_NAMCORE
        return lastBuildError;
#else
        static const std::string none;
        return none;
#endif
    }

    /** True when a model is installed and ready to run. */
    bool isActive() const noexcept
    {
#if J37_HAS_RTNEURAL || J37_HAS_NAMCORE
        return activeModel.load (std::memory_order_acquire) != nullptr
            && ! clearRequested.load (std::memory_order_acquire);
#else
        return false;
#endif
    }

    /** True from the moment a model has been INSTALLED, including the window
        between the message thread's load and the audio thread's handoff at the
        next block boundary.

        The panel needs this rather than isActive(). A host with nothing playing
        may not run a block for as long as the transport is stopped, and a picker
        that reported "NO MODEL" until audio moved read as a load that had failed
        - the user's own "NAM does not load", said about a model that was built
        and sitting in pendingModel. isActive() stays the audio thread's answer
        ("a model is in inference now"); this is the control thread's ("a model
        is installed"). */
    bool hasModel() const noexcept
    {
#if J37_HAS_RTNEURAL || J37_HAS_NAMCORE
        return modelInstalled.load (std::memory_order_acquire)
            && ! clearRequested.load (std::memory_order_acquire);
#else
        return false;
#endif
    }

    /**
        Loads a model from RTNeural's JSON format. Returns false - leaving any
        previously loaded model untouched but the stage inactive - when the text is
        empty, malformed, or RTNeural is not in the build.

        RTNeural's parser takes a parsed nlohmann::json (or an input stream), NOT a
        string, so the text is parsed here first. This is RTNeural's own wire
        format, which no .nam file uses; .nam goes through `buildFromNam` below.

        This is called from the MESSAGE thread (a preset load, or the editor's model
        picker), never from processBlock, so the parse allocation is safe. The audio
        thread only ever reads the resulting model through process().
    */
    bool loadFromJson (const juce::String& json)
    {
    #if J37_HAS_RTNEURAL
        if (json.isEmpty())
            return false;

        std::unique_ptr<RTNeural::Model<float>> parsedModel;
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

        publish (std::make_unique<RtNeuralModel> (std::move (parsedModel)));
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
#if J37_HAS_RTNEURAL || J37_HAS_NAMCORE
        wantedModelRate.store (modelRate, std::memory_order_relaxed);
        wantedEngineRate.store (engineRate, std::memory_order_relaxed);
        ratesPending.store (true, std::memory_order_release);
#else
        juce::ignoreUnused (modelRate, engineRate);
#endif
    }

    /** Releases the model, returning the stage to the pass-through. The control
        thread first closes the stage and waits for any in-flight sample to leave;
        the outgoing model is then retired by the next audio-thread handoff. */
    void clear() noexcept
    {
#if J37_HAS_RTNEURAL || J37_HAS_NAMCORE
        std::unique_ptr<InferenceModel> discard;
        {
            std::lock_guard<std::mutex> lock (controlMutex);
            discard = std::move (pendingModel);
            clearRequested.store (true, std::memory_order_release);
            activeModel.store (nullptr, std::memory_order_release);
        }
        modelInstalled.store (false, std::memory_order_release);
        discard.reset();
        reclaimRetiredModel();
#endif
    }

#if J37_HAS_RTNEURAL || J37_HAS_NAMCORE
    /**
        Builds the stage from a NeuralAmpModeler (.nam) file's parsed JSON.

        Which engine builds it is decided here and nowhere else:

          * an LSTM, in a build with RTNeural, goes through the converter below.
            That is the path this stage has always had, and keeping it is what
            makes the differential test possible - the converter and NAM Core
            render the same fixture and have to agree.
          * everything else - and an LSTM in a build with NAM Core but without
            RTNeural - goes to NAM Core, which is the reference implementation of
            every architecture in the format. Without it those files are still
            refused rather than half-loaded: "a model that loads and sounds wrong"
            remains worse than one that fails loudly.

        Returns false, leaving the previously loaded model untouched, when no
        engine can build the document.
    */
    bool buildFromNam (const nlohmann::json& namJson)
    {
        std::unique_ptr<InferenceModel> built;
        lastBuildError.clear();

    #if J37_HAS_RTNEURAL
        if (isLstm (namJson))
        {
            built = convertLstm (namJson);

            if (built == nullptr)
                lastBuildError = "the LSTM weights are not in a shape the converter reads";
        }
    #endif

    #if J37_HAS_NAMCORE
        if (built == nullptr)
            built = buildWithNamCore (namJson);
    #endif

        if (built == nullptr)
            return false;

        lastBuildError.clear();
        publish (std::move (built));
        return true;
    }

    #if J37_HAS_RTNEURAL
    /**
        Builds the stage from a NeuralAmpModeler (.nam) file's text, through
        RTNeural's layer JSON.

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

        One real transposition is involved: NAM puts the four gate blocks on the
        ROWS of each kernel (PyTorch's row-major [4H x (I+H)]) while
        RTNeural's loader indexes the same coefficients with the taps on the
        rows ([in][4H]), so the x- and h-parts of every kernel are read back
        column-wise. The gate order inside each block is untouched, and the
        hidden/cell seeds RTNeural's layers start at zero rather than the
        file's, which differs from the reference player only during the model's
        own prewarm window and nowhere after it. Any unexpected shape returns
        null, and the caller turns that into the same "no model" outcome a bad
        RTNeural file already produces.
    */
    std::unique_ptr<InferenceModel> convertLstm (const nlohmann::json& namJson)
    {
        std::unique_ptr<RTNeural::Model<float>> parsedModel;

        try
        {
            const auto& config = namJson.at ("config");
            const auto numLayers = config.at ("num_layers").get<int>();
            const auto inputSize = config.at ("input_size").get<int>();
            const auto hiddenSize = config.at ("hidden_size").get<int>();

            // Keep all subsequent dimensions and products bounded before doing any
            // size arithmetic on data supplied by the model file.
            constexpr int maxLayers = 8;
            constexpr int maxInputSize = 256;
            constexpr int maxHiddenSize = 512;
            if (numLayers < 1 || numLayers > maxLayers
                || inputSize < 1 || inputSize > maxInputSize
                || hiddenSize < 1 || hiddenSize > maxHiddenSize)
                return nullptr;

            // A .nam file's weights array is 1-D JSON. Anything else is not a
            // shape this converter understands.
            if (! namJson.contains ("weights") || ! namJson["weights"].is_array())
                return nullptr;

            const auto& weights = namJson["weights"];

            // Slice offsets, counted exactly as nam::lstm::LSTM consumes the flat
            // vector: every layer's block is its kernel stored row-major (4H rows
            // in PyTorch's i/f/g/o gate order by I+H columns of input taps then
            // recurrent taps), then the flat bias (4H), then the hidden and cell
            // seeds (H each). Only the first layer has I input taps; deeper ones
            // take the previous layer's H hidden outputs, so their stride is H+H.
            // The topmost block additionally carries the output head (out x H,
            // row-major) and its bias (out), which become the "dense" layer.
            using Size = std::size_t;
            const auto hidden = static_cast<Size> (hiddenSize);
            const auto input = static_cast<Size> (inputSize);
            const auto layers = static_cast<Size> (numLayers);

            auto checkedMultiply = [] (Size a, Size b, Size& result) noexcept
            {
                constexpr auto maxValue = std::numeric_limits<Size>::max();
                if (a != 0 && b > maxValue / a)
                    return false;
                result = a * b;
                return true;
            };
            auto checkedAdd = [] (Size a, Size b, Size& result) noexcept
            {
                constexpr auto maxValue = std::numeric_limits<Size>::max();
                if (b > maxValue - a)
                    return false;
                result = a + b;
                return true;
            };
            auto kernelElementsFor = [hidden, &checkedMultiply, &checkedAdd] (Size layerIn, Size& result)
            {
                Size taps = 0;
                if (! checkedAdd (layerIn, hidden, taps)
                    || ! checkedMultiply (4, hidden, result))
                    return false;
                return checkedMultiply (result, taps, result);
            };
            auto blockElementsFor = [hidden, &checkedMultiply, &checkedAdd, &kernelElementsFor]
                                    (Size layerIn, Size& result)
            {
                Size kernel = 0, bias = 0, states = 0, partial = 0;
                return kernelElementsFor (layerIn, kernel)
                    && checkedMultiply (4, hidden, bias)
                    && checkedMultiply (2, hidden, states)
                    && checkedAdd (kernel, bias, partial)
                    && checkedAdd (partial, states, result);
            };

            Size expectedElements = 0;
            if (! checkedAdd (hidden, 1, expectedElements))
                return nullptr;
            for (Size layer = 0; layer < layers; ++layer)
            {
                Size layerElements = 0;
                const auto layerInput = layer == 0 ? input : hidden;
                if (! blockElementsFor (layerInput, layerElements)
                    || ! checkedAdd (expectedElements, layerElements, expectedElements))
                    return nullptr;
            }

            if (weights.size() != expectedElements)
                return nullptr;

            auto readFlat = [&weights] (Size offset, Size count)
            {
                if (offset > weights.size() || count > weights.size() - offset)
                    throw std::out_of_range ("NAM tensor range is out of bounds");
                std::vector<float> slice;
                slice.reserve (count);
                for (Size i = 0; i < count; ++i)
                    slice.push_back (weights.at (offset + i).get<float>());
                return slice;
            };

            // The head, kept for the [rows][columns] reads the head needs.
            auto kernel2D = [&readFlat] (Size offset, Size rows, Size columns)
            {
                std::vector<std::vector<float>> rowsOut;
                rowsOut.reserve (rows);
                for (Size r = 0; r < rows; ++r)
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
            const auto biasElements = static_cast<Size> (4) * hidden;
            auto splitKernel = [&weights, hidden, biasElements] (Size offset, Size inputRows)
            {
                const auto stride = inputRows + hidden;
                std::vector<std::vector<float>> kernel, recurrent;
                kernel.reserve (inputRows);
                recurrent.reserve (hidden);
                for (Size i = 0; i < inputRows; ++i)
                {
                    std::vector<float> row;
                    row.reserve (biasElements);
                    for (Size k = 0; k < biasElements; ++k)
                        row.push_back (weights.at (offset + k * stride + i).get<float>());
                    kernel.push_back (std::move (row));
                }
                for (Size j = 0; j < hidden; ++j)
                {
                    std::vector<float> row;
                    row.reserve (biasElements);
                    for (Size k = 0; k < biasElements; ++k)
                        row.push_back (weights.at (offset + k * stride + inputRows + j).get<float>());
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
            Size cursor = 0;

            for (Size layer = 0; layer < layers; ++layer)
            {
                const auto layerIn = layer == 0 ? input : hidden;
                Size kernelElements = 0, blockElements = 0;
                if (! kernelElementsFor (layerIn, kernelElements)
                    || ! blockElementsFor (layerIn, blockElements)
                    || cursor > weights.size() || blockElements > weights.size() - cursor)
                    return nullptr;
                auto kernelPair = splitKernel (cursor, layerIn);
                const auto bias = readFlat (cursor + kernelElements, biasElements);
                cursor += blockElements;

                layerList.push_back ({ { "type", "lstm" },
                                       { "shape", nlohmann::json::array ({ hiddenSize }) },
                                       { "weights", nlohmann::json::array (
                                           { kernelPair.first, kernelPair.second, bias }) } });
            }

            // The head is stored row-major [out x H]; RTNeural's dense loader
            // expects [in][out] and transposes on the way in, so each hidden tap
            // becomes one single-element row.
            if (cursor > weights.size() || hidden > weights.size() - cursor
                || weights.size() - cursor - hidden != 1)
                return nullptr;
            const auto headWeight = kernel2D (cursor, hidden, 1);
            const auto headBias = readFlat (cursor + hidden, 1);
            layerList.push_back ({ { "type", "dense" },
                                   { "shape", nlohmann::json::array ({ 1 }) },
                                   { "weights", nlohmann::json::array ({ headWeight, headBias }) } });

            nlohmann::json rtJson { { "in_shape", nlohmann::json::array ({ inputSize }) },
                                    { "layers", layerList } };

            parsedModel = RTNeural::json_parser::parseJson<float> (rtJson, false);
            if (parsedModel == nullptr)
                return nullptr;
        }
        catch (...)
        {
            return nullptr;
        }

        return std::make_unique<RtNeuralModel> (std::move (parsedModel));
    }
    #endif

    #if J37_HAS_NAMCORE
    /** NAM Core's own loader, for every architecture RTNeural does not convert.

        `prepare` runs HERE rather than on the audio thread, because it is the
        call that sizes NAM's ring buffers: the backend is built and sized while
        the message thread is already allocating for the load, and what the audio
        thread meets afterwards is a model whose buffers are the size it asks
        for. The rate is the one the bridge will feed it - the model's own when
        the file declares one, and the rate it reports otherwise - so a 48 kHz
        profile is never sized for a 96 kHz engine. */
    std::unique_ptr<InferenceModel> buildWithNamCore (const nlohmann::json& namJson)
    {
        auto built = std::make_unique<NamCoreModel>();

        if (! built->backend.load (namJson))
        {
            //  NAM Core says exactly what was wrong with the document - an
            //  unsupported version, a missing key, a weights array of the wrong
            //  length - and its sentence is the only thing on this path that can
            //  tell a user WHICH download to look for. It used to be dropped on
            //  the floor here, which is why every refusal reached the panel as
            //  the same "BAD SHAPE".
            lastBuildError = built->backend.lastError();
            return nullptr;
        }

        built->backend.prepare (built->backend.modelSampleRate());

        // `prepare` catches a rate the model refuses and leaves itself unprepared,
        // which would make every `process` a pass-through behind an active stage.
        // Refusing the load is the honest outcome: the panel says the file did not
        // load rather than playing dry while NEURAL reads as engaged.
        if (! built->backend.isPrepared())
        {
            lastBuildError = "the model would not start at its own sample rate";
            return nullptr;
        }

        return built;
    }
    #endif

    /** Publishes a completed model for adoption at the next audio-thread block.
        It never replaces an object that process() may currently be using. */
    void publish (std::unique_ptr<InferenceModel> built)
    {
        std::unique_ptr<InferenceModel> displaced;
        {
            std::lock_guard<std::mutex> lock (controlMutex);
            displaced = std::move (pendingModel);
            pendingModel = std::move (built);
            modelInstalled.store (pendingModel != nullptr, std::memory_order_release);
            clearRequested.store (false, std::memory_order_release);
            resetRequested.store (false, std::memory_order_relaxed);
        }
        displaced.reset();
        reclaimRetiredModel();
    }

    /** Handoff called before a block. The outgoing model is not destroyed here:
        destruction can allocate/free or acquire library locks, so control-thread
        code reclaims it only after observing the audio thread's quiescent epoch. */
    void adoptPendingModel()
    {
        std::unique_ptr<InferenceModel> previous;
        bool adopt = false;
        bool clear = false;
        {
            std::lock_guard<std::mutex> lock (controlMutex);
            clear = clearRequested.load (std::memory_order_acquire);
            adopt = pendingModel != nullptr;
            if (! clear && ! adopt)
                return;

            activeModel.store (nullptr, std::memory_order_release);
            previous = std::move (model);
            if (adopt)
                model = std::move (pendingModel);
            clearRequested.store (false, std::memory_order_release);
        }

        if (adopt)
        {
            model->reset();
            bridge.reset();
            activeModel.store (model.get(), std::memory_order_release);
        }
        else
        {
            resetRequested.store (false, std::memory_order_relaxed);
            bridge.reset();
        }

        if (previous != nullptr)
        {
            std::lock_guard<std::mutex> lock (controlMutex);
            retiredModel = std::move (previous);
        }
    }

    /** Whether a .nam document declares the one architecture the converter above
        covers. Every other architecture is NAM Core's, and a build without NAM
        Core refuses it. */
    void reclaimRetiredModel() noexcept
    {
        std::unique_ptr<InferenceModel> retired;
        {
            std::lock_guard<std::mutex> lock (controlMutex);
            if (! audioQuiescent.load (std::memory_order_acquire))
                return;
            retired = std::move (retiredModel);
        }
    }

    static bool isLstm (const nlohmann::json& namJson)
    {
        return namJson.contains ("architecture")
            && namJson["architecture"].is_string()
            && namJson["architecture"].get<std::string>() == "LSTM";
    }
#endif

    /** The model's own recurrent state, let go of on a rate change or a model
        swap so neither can resume from a stale hidden state. */
    void reset() noexcept
    {
#if J37_HAS_RTNEURAL || J37_HAS_NAMCORE
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
#if J37_HAS_RTNEURAL || J37_HAS_NAMCORE
        if (amount <= 1.0e-5f)
            return x;

        auto* active = activeModel.load (std::memory_order_acquire);
        if (active == nullptr)
            return x;

        if (activeModel.load (std::memory_order_acquire) != active)
            return x;

        audioQuiescent.store (false, std::memory_order_release);
        struct QuiescentGuard
        {
            std::atomic<bool>& state;
            int exceptionsOnEntry = std::uncaught_exceptions();
            ~QuiescentGuard()
            {
                if (std::uncaught_exceptions() > exceptionsOnEntry)
                    state.store (true, std::memory_order_release);
            }
        } quiescentGuard { audioQuiescent };

        // The rates, latched here rather than written into the bridge by the
        // thread that knows them: see `setSampleRates`. Checked per sample because
        // this stage is called per sample.
        // Claim the pending update BEFORE reading the rates. If setSampleRates()
        // publishes another update during these loads, its store of true remains
        // pending for the next inference instead of being erased by a trailing clear.
        if (ratesPending.exchange (false, std::memory_order_acq_rel))
            bridge.prepare (wantedModelRate.load (std::memory_order_relaxed),
                            wantedEngineRate.load (std::memory_order_relaxed));

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
        const auto converted = bridge.process (x, [active] (float input) noexcept
        {
            return active->forward (input);
        });

        const auto output = converted.dry
                          + (converted.wet - converted.dry) * juce::jlimit (0.0f, 1.0f, amount);
        audioQuiescent.store (true, std::memory_order_release);
        quiescentGuard.exceptionsOnEntry = std::uncaught_exceptions();
        return output;
#else
        juce::ignoreUnused (amount);
        return x;
#endif
    }
};
