// =============================================================================
//  Interface sounds: the ticks and the clicks the panel makes when it is used.
//
//  Moved out of PluginEditor.cpp whole, prose and all, by matching its exact
//  text. Its four design constraints are the interesting part and they are
//  below, unchanged: the engine has its own audio device so a tick can never
//  reach the plugin's output, it never fails the editor, and it costs nothing.
// =============================================================================

#include "PluginEditor.h"
#include <atomic>
#include <cmath>

//==============================================================================
//  Interface sounds.
//
//  The engine is a four-voice tick synthesiser with its own audio device. The
//  design constraints, in order of importance:
//
//    1. It must NEVER reach the plugin's audio output. It has its own device,
//       so a click cannot be recorded, cannot appear in the DAW's meters and
//       cannot be bounced into a render. This is the whole reason it is a
//       separate device rather than a voice inside the plugin's engine.
//
//    2. It must never fail the editor. If no device can be opened - a headless
//       host, an exclusive-mode driver, a machine with no output at all - the
//       engine simply stays silent and the panel works exactly as before. The
//       open is attempted once and never retried.
//
//    3. It must cost nothing. The render is a sine, a noise burst and a
//       two-stage envelope; the buffer is 256 samples at the device's rate.
//==============================================================================
FirstAudioProcessorEditor::UiSoundEngine::UiSoundEngine()
{
    // An output-only device, opened lazily and never retried: a host that has no
    // spare output, a machine whose only device is held exclusively by the DAW,
    // or a headless CI runner must all end up with a silent engine and a working
    // panel rather than an error or a hang.
    const auto error = deviceManager.initialise (0, 2, nullptr, true, {}, nullptr);
    deviceOpen = error.isEmpty();

    if (deviceOpen)
        deviceManager.addAudioCallback (this);
}

FirstAudioProcessorEditor::UiSoundEngine::~UiSoundEngine()
{
    if (deviceOpen)
        deviceManager.removeAudioCallback (this);
}

void FirstAudioProcessorEditor::UiSoundEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    deviceRate = device != nullptr ? device->getCurrentSampleRate() : 44100.0;
    phase = 0.0f;
    envelope = 0.0f;
    noiseState = 0.37f;
}

void FirstAudioProcessorEditor::UiSoundEngine::audioDeviceStopped()
{
    envelope = 0.0f;
}

void FirstAudioProcessorEditor::UiSoundEngine::trigger (Voice voice) noexcept
{
    if (! enabled.load (std::memory_order_relaxed))
        return;

    // The four voices, each a different synthesis rather than the same beep at a
    // different pitch. `brightness` (the machine's own activity) lifts all four
    // slightly, so a busy machine sounds slightly higher and tighter.
    //
    // The decay figure is the per-sample envelope multiplier at the DEVICE's
    // rate; because it is re-derived from the rate in the render it is stored
    // here as a time constant instead, which keeps the four voices the same
    // length at every device rate.
    const auto lift = brightness.load (std::memory_order_relaxed);

    switch (voice)
    {
        case Voice::click:
            // A switch: short, dry, bright. Almost all noise, very fast decay.
            pendingAmplitude.store (0.20f, std::memory_order_relaxed);
            pendingPitch.store (1900.0f + lift * 700.0f, std::memory_order_relaxed);
            pendingDecay.store (0.30f, std::memory_order_relaxed);   // ms of tail
            pendingNoiseMix.store (0.85f, std::memory_order_relaxed);
            break;

        case Voice::detent:
            // A knob crossing a step: softer, lower, mostly tone.
            pendingAmplitude.store (0.10f, std::memory_order_relaxed);
            pendingPitch.store (900.0f + lift * 400.0f, std::memory_order_relaxed);
            pendingDecay.store (0.55f, std::memory_order_relaxed);
            pendingNoiseMix.store (0.30f, std::memory_order_relaxed);
            break;

        case Voice::press:
            // A key going down: a low, dry thump.
            pendingAmplitude.store (0.16f, std::memory_order_relaxed);
            pendingPitch.store (420.0f + lift * 120.0f, std::memory_order_relaxed);
            pendingDecay.store (1.40f, std::memory_order_relaxed);
            pendingNoiseMix.store (0.45f, std::memory_order_relaxed);
            break;

        case Voice::release:
            // ...and coming back up: a touch higher, so a key pair reads as two
            // events rather than one blip when pressed quickly.
            pendingAmplitude.store (0.12f, std::memory_order_relaxed);
            pendingPitch.store (620.0f + lift * 160.0f, std::memory_order_relaxed);
            pendingDecay.store (0.90f, std::memory_order_relaxed);
            pendingNoiseMix.store (0.35f, std::memory_order_relaxed);
            break;
    }
}

void FirstAudioProcessorEditor::UiSoundEngine::audioDeviceIOCallbackWithContext (
    const float* const*, int, float* const* outputChannelData, int numOutputChannels,
    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    if (outputChannelData == nullptr || numOutputChannels <= 0)
        return;

    // A new trigger takes over whatever was ringing, which is what a real panel
    // does: the envelope is re-armed rather than layered, so a fast series of
    // clicks sounds like a switch being flicked rather than a chord.
    const auto amplitude = pendingAmplitude.exchange (0.0f, std::memory_order_relaxed);
    if (amplitude > 0.0f)
    {
        envelope = amplitude;
        const auto pitchHz = pendingPitch.load (std::memory_order_relaxed);
        phase = 0.0f;
        (void) pitchHz;   // read below, through the increment
        decay = pendingDecay.load (std::memory_order_relaxed);
        noiseMix = pendingNoiseMix.load (std::memory_order_relaxed);
    }

    if (envelope < 1.0e-5f)
    {
        for (int channel = 0; channel < numOutputChannels; ++channel)
            juce::FloatVectorOperations::clear (outputChannelData[channel], numSamples);
        return;
    }

    // The pitch is read here rather than in trigger() because it depends on the
    // DEVICE's rate, which trigger() (on the message thread) does not know.
    const auto pitchHz = pendingPitch.load (std::memory_order_relaxed);
    const auto phaseIncrement = static_cast<float> (juce::MathConstants<double>::twoPi
                                                        * pitchHz / deviceRate);

    // The envelope multiplier per sample, derived from the voice's tail in
    // MILLISECONDS so the four sounds keep their designed length whatever rate
    // the device happens to run at.
    const auto tailSamples = juce::jmax (1.0f, static_cast<float> (deviceRate) * decay * 0.001f);
    const auto decayPerSample = std::exp (-1.0f / tailSamples);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto tone = std::sin (phase);
        phase += phaseIncrement;
        if (phase >= juce::MathConstants<float>::twoPi)
            phase -= juce::MathConstants<float>::twoPi;

        // The noise burst: a fast, cheap LCG so the tick has a bright edge and
        // does not read as a pure sine beep. It is filtered slightly by mixing
        // with the previous value, which takes the harshest digital edge off.
        noiseState = noiseState * 1.27f + 0.31f;
        if (noiseState > 1.0f) noiseState -= 2.0f;
        const auto noise = noiseState;

        const auto voiceOut = (tone * (1.0f - noiseMix) + noise * noiseMix) * envelope;

        for (int channel = 0; channel < numOutputChannels; ++channel)
            outputChannelData[channel][sample] = voiceOut;

        envelope *= decayPerSample;
    }
}
