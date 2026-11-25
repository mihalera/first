// Processor-level boundary checks against the real MiBoX processor and signal path.
#include "PluginProcessor.h"

#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
constexpr float guardValue = 12345.0f;

//  A guard sample is written as 12345.0f and has to come back untouched, so
//  the comparison is exact by construction. Comparing the representation says
//  that, and keeps -Wfloat-equal off a line that is doing precisely what it
//  warns about.
bool isGuard(float value) { return std::memcmp(&value, &guardValue, sizeof(value)) == 0; }

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
            if (!isGuard(storage[static_cast<std::size_t>(channel)]
                                [static_cast<std::size_t>(sample)]) ||
                !isGuard(storage[static_cast<std::size_t>(channel)]
                                [static_cast<std::size_t>(guardSamples + samples + sample)]))
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

bool setParameter(FirstAudioProcessor &processor, const char *id, float value) {
    auto *parameter = processor.parameters.getParameter(id);
    if (parameter == nullptr)
        return false;

    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    return true;
}

bool parameterEquals(FirstAudioProcessor &processor, const char *id, float expected) {
    const auto *value = processor.parameters.getRawParameterValue(id);
    return value != nullptr && std::abs(value->load() - expected) < 1.0e-4f;
}

bool processRestoredBypass(FirstAudioProcessor &processor) {
    constexpr std::size_t sampleCount = 16;
    std::array<float, sampleCount> expectedLeft{};
    std::array<float, sampleCount> expectedRight{};
    juce::AudioBuffer<float> buffer(2, static_cast<int>(sampleCount));

    for (std::size_t sample = 0; sample < sampleCount; ++sample) {
        const auto value = static_cast<float>(sample) * 0.03125f - 0.25f;
        expectedLeft[sample] = value;
        expectedRight[sample] = -value;
        buffer.setSample(0, static_cast<int>(sample), expectedLeft[sample]);
        buffer.setSample(1, static_cast<int>(sample), expectedRight[sample]);
    }

    juce::MidiBuffer midi;
    processor.processBlock(buffer, midi);
    return std::memcmp(buffer.getReadPointer(0), expectedLeft.data(), sizeof(expectedLeft)) == 0 &&
           std::memcmp(buffer.getReadPointer(1), expectedRight.data(), sizeof(expectedRight)) == 0;
}

bool checkStateAcrossLifecycle() {
    auto saved = std::make_unique<FirstAudioProcessor>();
    if (!setParameter(*saved, "mix", 37.0f) || !setParameter(*saved, "bypass", 1.0f))
        return false;

    juce::MemoryBlock state;
    saved->getStateInformation(state);
    if (state.getSize() == 0)
        return false;

    auto restored = std::make_unique<FirstAudioProcessor>();
    restored->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    if (!parameterEquals(*restored, "mix", 37.0f) || !parameterEquals(*restored, "bypass", 1.0f))
        return false;

    // A restored host state must remain effective when the processor prepares,
    // releases, and prepares again at a new sample rate.
    restored->prepareToPlay(48000.0, 127);
    const auto firstRunPassed = processRestoredBypass(*restored);
    restored->releaseResources();

    restored->prepareToPlay(96000.0, 263);
    const auto secondRunPassed = processRestoredBypass(*restored);
    restored->releaseResources();

    return firstRunPassed && secondRunPassed && parameterEquals(*restored, "mix", 37.0f) &&
           parameterEquals(*restored, "bypass", 1.0f);
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

    if (!checkStateAcrossLifecycle()) {
        std::cerr << "restored parameter state was lost or ineffective across prepare/release\n";
        return 1;
    }

    std::cout << "processor boundaries: empty, 1/257/509-frame blocks, 48->96 kHz, "
                 "prepare/release without playback, and restored state across lifecycle passed\n";
    return 0;
}
