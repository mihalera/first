// Processor-level boundary checks against the real MiBoX processor and signal path.
#include "PluginProcessor.h"

#include <array>
#include <cmath>
#include <iostream>
#include <vector>

namespace {
constexpr float guardValue = 12345.0f;

bool processGuardedBlock(FirstAudioProcessor &processor,
                         juce::MidiBuffer &midi,
                         int samples,
                         double sampleRate) {
    constexpr int guardSamples = 4;
    std::array<std::vector<float>, 2> storage{
        std::vector<float>(static_cast<std::size_t>(samples + 2 * guardSamples), guardValue),
        std::vector<float>(static_cast<std::size_t>(samples + 2 * guardSamples), guardValue)};
    std::array<float *, 2> channels{storage[0].data() + guardSamples,
                                    storage[1].data() + guardSamples};

    for (int sample = 0; sample < samples; ++sample) {
        const auto phase = juce::MathConstants<float>::twoPi * 440.0f * static_cast<float>(sample) /
                           static_cast<float>(sampleRate);
        channels[0][sample] = 0.2f * std::sin(phase);
        channels[1][sample] = 0.15f * std::cos(phase);
    }

    juce::AudioBuffer<float> buffer(channels.data(), 2, samples);
    processor.processBlock(buffer, midi);

    for (int channel = 0; channel < 2; ++channel) {
        for (int sample = 0; sample < samples; ++sample)
            if (!std::isfinite(channels[static_cast<std::size_t>(channel)][sample]))
                return false;

        for (int sample = 0; sample < guardSamples; ++sample) {
            if (storage[static_cast<std::size_t>(channel)][static_cast<std::size_t>(sample)] !=
                    guardValue ||
                storage[static_cast<std::size_t>(channel)]
                       [static_cast<std::size_t>(guardSamples + samples + sample)] != guardValue)
                return false;
        }
    }

    return buffer.getNumChannels() == 2 && buffer.getNumSamples() == samples;
}

bool processEmptyBlock(FirstAudioProcessor &processor, juce::MidiBuffer &midi) {
    juce::AudioBuffer<float> empty(2, 0);
    processor.processBlock(empty, midi);
    return empty.getNumChannels() == 2 && empty.getNumSamples() == 0;
}
} // namespace

int main() {
    FirstAudioProcessor processor;
    juce::MidiBuffer midi;

    // Cover a zero-frame callback and non-power-of-two sizes with sentinels around
    // externally owned channel memory to detect writes past either end.
    processor.prepareToPlay(48000.0, 509);
    if (!processEmptyBlock(processor, midi)) {
        std::cerr << "zero-frame processBlock changed buffer dimensions\n";
        return 1;
    }

    for (const auto samples : {1, 257, 509}) {
        if (!processGuardedBlock(processor, midi, samples, 48000.0)) {
            std::cerr << "invalid output or buffer-boundary write at 48 kHz, block " << samples
                      << '\n';
            return 1;
        }
    }

    // Hosts renegotiate via prepareToPlay between callbacks. Re-prepare at a new
    // rate, then render blocks of both the old and a different prime size.
    processor.prepareToPlay(96000.0, 263);
    for (const auto samples : {1, 263}) {
        if (!processGuardedBlock(processor, midi, samples, 96000.0)) {
            std::cerr << "invalid output or buffer-boundary write at 96 kHz, block " << samples
                      << '\n';
            return 1;
        }
    }
    processor.releaseResources();

    // Preparation and teardown without a single audio callback is a valid host
    // lifecycle and must not depend on processing having initialized state.
    FirstAudioProcessor neverPlayed;
    neverPlayed.prepareToPlay(44100.0, 127);
    neverPlayed.releaseResources();

    std::cout << "processor boundaries: empty, 1/257/509-frame blocks, 48->96 kHz, and "
                 "prepare/release without playback passed\n";
    return 0;
}
