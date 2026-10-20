// =============================================================================
//  The .nam weight LAYOUTS, in one place, for the two harnesses that need them.
//
//      tests/neural/fixture_test.cpp   proves a hand-authored fixture is
//                                      internally consistent: the flat weights
//                                      array a document carries has to be
//                                      exactly as long as its own config implies,
//                                      which is the classic silent fixture bug.
//      tests/neural/bench_nam.cpp      GENERATES a model of a realistic size,
//                                      which needs the same rules the other way
//                                      round - one number per weight, in the order
//                                      the loader consumes them.
//
//  The rules are written from the loaders' own order rather than from a
//  remembered count. For the LSTM that is `NAM/lstm.cpp`; for the WaveNet it is
//  the rechannel, per-layer dilated conv + input mixin + layer1x1, and head
//  rechannel, with the trailing head_scale read out of the weights themselves.
//  A generator that disagrees with the loader is not silent: NAM Core refuses a
//  weights array of the wrong length ("Weight mismatch: provided N weights, but
//  the model expects M"), so the benchmark's model either loads or says why.
//
//  Why the synthetic models exist at all. tests/neural/fixtures/ holds two
//  deliberately tiny models - 109 weights for the LSTM, 41 for the WaveNet -
//  because the gate there compares RENDERED samples with the reference player
//  and a small model makes the oracle table readable. Cost is a different
//  question: a 2-channel WaveNet with two dilations says nothing about a 16
//  channel, ten-dilation profile, and the 4-cell LSTM says nothing about the
//  20-cell one a real capture uses. So the benchmark builds its models here, at
//  the shapes the format actually ships, with weights from a deterministic
//  generator - a synthetic profile with the real shape's arithmetic, which is
//  all a cost measurement needs and is honest about being that.
// =============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <vector>

//------------------------------------------------------------------------------
//  How many numbers each config's layout implies.
//------------------------------------------------------------------------------

/** One block per layer - the kernel (4H rows of I+H taps, PyTorch's i/f/c/o gate
    order), the bias (4H), the hidden and cell seeds (H each) - then, after the
    last layer, the output head (H taps) and its bias (1). */
inline long long lstmWeightCount (const nlohmann::json& config)
{
    const auto input = config.at ("input_size").get<long long>();
    const auto hidden = config.at ("hidden_size").get<long long>();
    const auto layers = config.at ("num_layers").get<long long>();

    long long total = hidden + 1;   // the output head and its bias, read last

    for (long long layer = 0; layer < layers; ++layer)
    {
        const auto layerInput = layer == 0 ? input : hidden;
        total += 4 * hidden * (layerInput + hidden)   // kernel, gate rows
               + 4 * hidden                           // bias
               + 2 * hidden;                          // hidden and cell seeds
    }

    return total;
}

/** The head rechannel of ONE layer array, in either spelling the format uses:
    the legacy `head_size` + `head_bias` (an implicit 1-tap kernel) and the
    trainer's nested `head` object (out_channels, kernel_size, bias). Both
    consume the same kind of block, so the count and the generator read the
    shape through this rather than assuming one spelling. */
struct HeadShape
{
    long long size = 1;
    long long kernel = 1;
    bool bias = false;
};

inline HeadShape headShape (const nlohmann::json& array)
{
    if (array.contains ("head_size"))
        return { array.at ("head_size").get<long long>(), 1, array.value ("head_bias", false) };

    const auto& head = array.at ("head");

    return { head.at ("out_channels").get<long long>(),
             head.value ("kernel_size", 1),
             head.value ("bias", false) };
}

/** The rechannel (input_size -> channels, no bias), then per layer the dilated
    conv (channels -> channels, bias), the input mixin (condition_size ->
    channels, no bias) and the layer1x1 (channels -> channels, bias), then the
    head rechannel (channels -> head_size, bias per head_bias), then - when the
    document has one - the POST-STACK head (a stack of Conv1Ds from head_size to
    out_channels, each with its bias), then the trailing head_scale, which the
    reference player READS FROM THE WEIGHTS, so the file's last number must equal
    the config's and this function counts it. */
inline long long wavenetWeightCount (const nlohmann::json& config)
{
    long long total = 0;

    for (const auto& array : config.at ("layers"))
    {
        const auto channels = array.at ("channels").get<long long>();
        const auto condition = array.at ("condition_size").get<long long>();
        const auto inputSize = array.at ("input_size").get<long long>();
        const auto head = headShape (array);
        const auto kernel = array.at ("kernel_size").get<long long>();
        const auto gated = array.value ("gated", false);
        const auto bottleneck = array.value ("bottleneck", channels);
        const auto convOut = gated ? 2 * bottleneck : bottleneck;

        total += inputSize * channels;                                  // rechannel, no bias
        total += static_cast<long long> (array.at ("dilations").size())
                 * (convOut * channels * kernel + convOut              // dilated conv and bias
                    + convOut * condition                              // input mixin, no bias
                    + channels * bottleneck + channels);               // layer1x1 and bias
        total += head.kernel * head.size * channels                    // head rechannel
                 + (head.bias ? head.size : 0);
    }

    // The post-stack head sits between the layer arrays and the head scale in
    // the stream. A document with `"head": null` has none - its head rechannel
    // is the whole output stage - which is how both fixtures are written.
    if (config.contains ("head") && ! config.at ("head").is_null())
    {
        const auto& postStack = config.at ("head");
        const auto headChannels = postStack.at ("channels").get<long long>();
        const auto headOut = postStack.at ("out_channels").get<long long>();
        const auto& kernels = postStack.at ("kernel_sizes");

        auto in = headShape (config.at ("layers").back()).size;

        for (std::size_t i = 0; i < kernels.size(); ++i)
        {
            const auto out = i + 1 == kernels.size() ? headOut : headChannels;
            total += in * out * kernels.at (i).get<long long>() + out;   // conv, then its bias
            in = out;
        }
    }

    return total + 1;   // head_scale, the last value in the stream
}

//------------------------------------------------------------------------------
//  The block sizes, in the order the loaders consume them.
//
//  `weightCount` above is the sum of these, and the generators below fill one
//  block at a time - so the count a fixture is checked against and the order a
//  generated model is written in cannot drift apart. A block is one contiguous
//  run of numbers; which matrix it is reshaped into afterwards does not matter
//  for a length check or for a cost measurement.
//------------------------------------------------------------------------------
inline std::vector<long long> lstmBlocks (const nlohmann::json& config)
{
    const auto input = config.at ("input_size").get<long long>();
    const auto hidden = config.at ("hidden_size").get<long long>();
    const auto layers = config.at ("num_layers").get<long long>();

    std::vector<long long> blocks;

    for (long long layer = 0; layer < layers; ++layer)
    {
        const auto layerInput = layer == 0 ? input : hidden;
        blocks.push_back (4 * hidden * (layerInput + hidden));
        blocks.push_back (4 * hidden);
        blocks.push_back (hidden);
        blocks.push_back (hidden);
    }

    blocks.push_back (hidden);   // output head
    blocks.push_back (1);        // its bias

    return blocks;
}

inline std::vector<long long> wavenetBlocks (const nlohmann::json& config)
{
    std::vector<long long> blocks;

    for (const auto& array : config.at ("layers"))
    {
        const auto channels = array.at ("channels").get<long long>();
        const auto condition = array.at ("condition_size").get<long long>();
        const auto inputSize = array.at ("input_size").get<long long>();
        const auto head = headShape (array);
        const auto kernel = array.at ("kernel_size").get<long long>();
        const auto gated = array.value ("gated", false);
        const auto bottleneck = array.value ("bottleneck", channels);
        const auto convOut = gated ? 2 * bottleneck : bottleneck;

        blocks.push_back (inputSize * channels);

        for (std::size_t layer = 0; layer < array.at ("dilations").size(); ++layer)
        {
            blocks.push_back (convOut * channels * kernel);
            blocks.push_back (convOut);
            blocks.push_back (convOut * condition);
            blocks.push_back (channels * bottleneck);
            blocks.push_back (channels);
        }

        blocks.push_back (head.kernel * head.size * channels);

        if (head.bias)
            blocks.push_back (head.size);
    }

    if (config.contains ("head") && ! config.at ("head").is_null())
    {
        const auto& postStack = config.at ("head");
        const auto headChannels = postStack.at ("channels").get<long long>();
        const auto headOut = postStack.at ("out_channels").get<long long>();
        const auto& kernels = postStack.at ("kernel_sizes");

        auto in = headShape (config.at ("layers").back()).size;

        for (std::size_t i = 0; i < kernels.size(); ++i)
        {
            const auto out = i + 1 == kernels.size() ? headOut : headChannels;
            blocks.push_back (in * out * kernels.at (i).get<long long>() + out);
            in = out;
        }
    }

    blocks.push_back (1);   // head_scale

    return blocks;
}

namespace j37nam
{

/** A deterministic weight source: xorshift32, so a generated model is the same
    model on every machine and every run - the benchmark's numbers have to be
    comparable between two builds, and a model that changed shape or scale
    between runs would make them not.

    The magnitudes are small on purpose. A profile is not a random matrix, and
    what the measurement wants is the shape's arithmetic rather than its
    training: weights of this size keep every activation in its responsive range,
    so the timed loop is not measuring saturation or denormal handling, and the
    benchmark asserts the output is finite before it reports a time. */
struct WeightSource
{
    unsigned int state = 0x2545F491u;

    float next (float scale) noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;

        const auto unit = static_cast<float> (state & 0xFFFFFFu) / 16777215.0f;
        return (unit * 2.0f - 1.0f) * scale;
    }
};

/** Fills one block of `count` weights, so the block sizes above are the only
    description of the layout in this file. */
inline void fill (std::vector<float>& weights, long long count, WeightSource& source, float scale)
{
    weights.reserve (weights.size() + static_cast<std::size_t> (count));

    for (long long i = 0; i < count; ++i)
        weights.push_back (source.next (scale));
}

/** A WaveNet profile of the shape the format actually ships: one layer array,
    sixteen channels, a 3-tap kernel, ten dilations from 1 to 512, an eight
    channel head rechannel, a 1-tap post-stack head that reduces those eight to
    the one output channel the stage needs, and the standard 0.02 head scale -
    10 874 weights.

    The post-stack head is not decoration. Without it the model's output width
    is the layer array's `head_size` (eight), and NeuralStage refuses a model
    that is not one-in/one-out - which is what the first run of this benchmark
    found, NAM's constructor having accepted the document perfectly well. The
    arrangement here is the one a real capture uses. */
inline nlohmann::json standardWaveNet()
{
    nlohmann::json file;

    file["version"] = "0.5.4";
    file["architecture"] = "WaveNet";
    file["sample_rate"] = 48000;
    file["metadata"] = { { "name", "J37 synthetic 16-channel WaveNet" },
                         { "modeled_by", "tests/neural/nam_shapes.h - generated for the process-cost benchmark" },
                         { "loudness", -20.0 },
                         { "gain", 1.0 } };

    auto& config = file["config"];

    nlohmann::json array;
    array["input_size"] = 1;
    array["condition_size"] = 1;
    array["head_size"] = 8;
    array["channels"] = 16;
    array["kernel_size"] = 3;
    array["dilations"] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512 };
    array["activation"] = "LeakyReLU";
    array["gated"] = false;
    array["head_bias"] = false;

    config["layers"] = nlohmann::json::array();
    config["layers"].push_back (array);

    nlohmann::json head;
    head["channels"] = 8;
    head["out_channels"] = 1;
    head["kernel_sizes"] = { 1 };
    head["activation"] = "Tanh";

    config["head"] = head;
    config["head_scale"] = 0.02;

    WeightSource source;
    std::vector<float> weights;

    for (const auto count : wavenetBlocks (config))
        fill (weights, count, source, count == 1 ? 0.0f : 0.02f);

    // The last number IS the head scale in this format, so the generator writes
    // the config's own value there rather than a random one.
    weights.back() = config.at ("head_scale").get<float>();

    file["weights"] = weights;
    return file;
}

/** An LSTM profile of a realistic size: one layer, twenty hidden cells, mono -
    1 821 weights, against the 109 of the fixture. */
inline nlohmann::json standardLstm (int hiddenSize = 20)
{
    nlohmann::json file;

    file["version"] = "0.5.4";
    file["architecture"] = "LSTM";
    file["sample_rate"] = 48000;
    file["metadata"] = { { "name", "J37 synthetic 20-cell LSTM" },
                         { "modeled_by", "tests/neural/nam_shapes.h - generated for the process-cost benchmark" },
                         { "loudness", -20.0 },
                         { "gain", 1.0 } };

    auto& config = file["config"];
    config["input_size"] = 1;
    config["hidden_size"] = hiddenSize;
    config["num_layers"] = 1;

    WeightSource source;
    std::vector<float> weights;

    for (const auto count : lstmBlocks (config))
        fill (weights, count, source, count == 1 ? 0.0f : 0.08f);

    file["weights"] = weights;
    return file;
}

} // namespace j37nam
