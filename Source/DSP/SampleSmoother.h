// =============================================================================
//  The sample clock, and the smoother that advances from it.
//
//  This is the one header in Source/DSP/ that leans on JUCE for more than a
//  scalar helper: juce::SmoothedValue and its linear smoothing type are the
//  thing it adapts. It does not include JUCE itself - it relies on its includer
//  having declared them, which is what the plugin's JuceHeader.h and
//  tests/dsp/check_headers_juce.sh's juce_audio_basics both do, and what keeps
//  the directory's other convention (see J37Math.h) intact: a header here is
//  compiled by both the plugin and the check, and neither may be the only one
//  that has seen it.
// =============================================================================
#pragma once

#include <cstdint>

//==============================================================================
/**
    A sample-clock adapter for JUCE's SmoothedValue.

    The engine uses one coefficient pair for both channels, so every ramp must advance
    once per SAMPLE, not once per channel and not never. JUCE's getCurrentValue() is a
    peek; it does not advance the ramp. The first smoother in each frame advances the
    shared clock, and the other smoothers advance lazily on their first read in that
    frame. This preserves the existing call sites while making their old
    getCurrentValue() intent explicit and sample-accurate.
*/
struct SampleClock
{
    std::uint64_t sample = 0;
};

class SampleSmoother
{
public:
    // The two flag parameters are named `should...` rather than after the fields they
    // initialise: a constructor parameter with the same name as its field shadows it,
    // and Clang flags that on macOS (it is how a silent self-assignment hides here).
    explicit SampleSmoother (SampleClock& clockToUse, bool shouldStartAtSample = false,
                             bool shouldInvertOutput = false, float initialValue = 0.0f)
        : clock (clockToUse), startsSample (shouldStartAtSample),
          invertOutput (shouldInvertOutput), smoother (initialValue)
    {
    }

    void reset (double sampleRateToUse, double smoothingSeconds) noexcept
    {
        smoother.reset (sampleRateToUse, smoothingSeconds);
        lastSample = clock.sample;
        snapNextTarget = true;
    }

    void setCurrentAndTargetValue (float value) noexcept
    {
        smoother.setCurrentAndTargetValue (value);
        lastSample = clock.sample;
        snapNextTarget = false;
    }

    void setTargetValue (float value) noexcept
    {
        // The first target after a rate reset is seeded at its exact value. Without
        // this, a coefficient that starts at zero (notably the playback poles) can
        // leave the first wet frame silent while its 20 ms ramp is still in progress.
        if (snapNextTarget)
        {
            smoother.setCurrentAndTargetValue (value);
            snapNextTarget = false;
        }
        else
        {
            smoother.setTargetValue (value);
        }
    }

    float getNextValue() noexcept
    {
        if (startsSample)
        {
            ++clock.sample;
            lastSample = clock.sample;
        }
        else
        {
            lastSample = clock.sample;
        }

        const auto value = smoother.getNextValue();
        return invertOutput ? 1.0f - value : value;
    }

    float getCurrentValue() noexcept
    {
        if (lastSample != clock.sample)
        {
            lastSample = clock.sample;
            const auto value = smoother.getNextValue();
            return invertOutput ? 1.0f - value : value;
        }

        const auto value = smoother.getCurrentValue();
        return invertOutput ? 1.0f - value : value;
    }

    bool isSmoothing() const noexcept
    {
        return smoother.isSmoothing();
    }

    // The processing loop already consumed every sample. Advancing here would apply
    // the ramp a second time and turn it into a block-rate zipper.
    void skip (int) noexcept
    {
    }

private:
    using Smoother = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;

    SampleClock& clock;
    bool startsSample = false;
    bool invertOutput = false;
    Smoother smoother;
    std::uint64_t lastSample = 0;
    bool snapNextTarget = true;
};
