// =============================================================================
//  A short, independent history for each side of the stereo tape offset.
// =============================================================================
#pragma once
#include <array>
#include <cstddef>

/** Fixed-capacity fractional delay used by ST OFFSET. */
struct StereoOffsetBuffer
{
    static constexpr int length = 64;

    void reset() noexcept
    {
        samples = {};
        writePosition = 0;
    }
    void write (int channel, float sample) noexcept
    {
        samples[static_cast<std::size_t> (channel)][static_cast<std::size_t> (writePosition)] = sample;
    }

    float readInterpolated (int channel, float offsetSamples) const noexcept
    {
        const auto offset = offsetSamples < 0.0f ? 0.0f
            : (offsetSamples > static_cast<float> (length - 2)
                ? static_cast<float> (length - 2) : offsetSamples);
        auto readPosition = static_cast<float> (writePosition) - offset;
        while (readPosition < 0.0f)
            readPosition += static_cast<float> (length);

        const auto readIndex = static_cast<int> (readPosition);
        const auto fraction = readPosition - static_cast<float> (readIndex);
        const auto nextIndex = (readIndex + 1) % length;
        const auto& channelSamples = samples[static_cast<std::size_t> (channel)];
        const auto first = channelSamples[static_cast<std::size_t> (readIndex)];
        return first + (channelSamples[static_cast<std::size_t> (nextIndex)] - first) * fraction;
    }

    void advanceFrame() noexcept
    {
        writePosition = (writePosition + 1) % length;
    }

private:
    std::array<std::array<float, length>, 2> samples {};
    int writePosition = 0;
};
