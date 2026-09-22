/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>

//==============================================================================
/**
*/
class FirstAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    FirstAudioProcessor();
    ~FirstAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRateToUse, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState parameters;

    //==============================================================================
    //  Live telemetry published by the audio thread and consumed by the editor.
    //  Peaks are exchanged (consumed) by the meter; the rest are plain readings.
    float getInputPeakLevel() noexcept { return inputPeakLevel.exchange (0.0f, std::memory_order_relaxed); }
    float getInputRmsLevel() const noexcept { return inputRmsLevel.load (std::memory_order_relaxed); }
    float getOutputPeakLevel() noexcept { return outputPeakLevel.exchange (0.0f, std::memory_order_relaxed); }
    float getOutputRmsLevel() const noexcept { return outputRmsLevel.load (std::memory_order_relaxed); }

    /** Gain reduction of the input stage compressor in dB (always <= 0). */
    float getInputGainReductionDb() const noexcept { return inputGainReductionDb.load (std::memory_order_relaxed); }

    /** Total gain reduction of both glue stages in dB (always <= 0). */
    float getGainReductionDb() const noexcept
    {
        return juce::jlimit (-24.0f, 0.0f,
                             inputGainReductionDb.load (std::memory_order_relaxed)
                             + gainReductionDb.load (std::memory_order_relaxed));
    }

    /** Envelope of the tape glue compressors as a 0..1 linear activity value. */
    float getCompressorActivity() const noexcept { return compressorActivity.load (std::memory_order_relaxed); }

    /** Instantaneous transport drift (wow/flutter), normalised to 0..1 around 0.5. */
    float getTransportDrift() const noexcept { return transportDrift.load (std::memory_order_relaxed); }

    /** Harmonic weight of the last block, 0..1, used for UI colour animation. */
    float getHarmonicCharacter() const noexcept { return harmonicCharacter.load (std::memory_order_relaxed); }

    /** True when the last processed block was fully bypassed. */
    bool isBypassed() const noexcept { return bypassActive.load (std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Cached parameter pointers: avoids repeated string lookups on the audio thread.
    std::atomic<float>* inputDbParam = nullptr;
    std::atomic<float>* driveParam = nullptr;
    std::atomic<float>* biasParam = nullptr;
    std::atomic<float>* toneParam = nullptr;
    std::atomic<float>* wowParam = nullptr;
    std::atomic<float>* flutterParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* outputDbParam = nullptr;
    std::atomic<float>* widthParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* tapeTypeParam = nullptr;
    std::atomic<float>* speedParam = nullptr;

    float sampleRate = 44100.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> inputGainSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputGainSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> widthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> bypassSmoothed;

    std::atomic<float> inputPeakLevel { 0.0f };
    std::atomic<float> inputRmsLevel { 0.0f };
    std::atomic<float> outputPeakLevel { 0.0f };
    std::atomic<float> outputRmsLevel { 0.0f };
    std::atomic<float> inputGainReductionDb { 0.0f };
    std::atomic<float> gainReductionDb { 0.0f };
    std::atomic<float> compressorActivity { 0.0f };
    std::atomic<float> transportDrift { 0.5f };
    std::atomic<float> harmonicCharacter { 0.0f };
    std::atomic<bool> bypassActive { false };

    // Per-channel tape state: 3-element hysteresis memory (current, previous, older)
    // plus a 2-element high-frequency post-emphasis memory.
    std::array<float, 3> hystL {};
    std::array<float, 3> hystR {};
    std::array<float, 2> highFreqL {};
    std::array<float, 2> highFreqR {};

    float wowPhaseL = 0.0f;
    float wowPhaseR = 0.0f;
    float flutterPhaseL = 0.0f;
    float flutterPhaseR = 0.0f;

    float previousTone = -1.0f;
    float toneLpAc = 0.0f;
    float toneLpBc = 0.0f;

    // Two coupled glue stages: the input compressor runs straight after the input
    // trim, the output compressor straight before the output trim.
    float inputCompressorEnvelope = 0.0f;
    float outputCompressorEnvelope = 0.0f;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FirstAudioProcessor)
};
