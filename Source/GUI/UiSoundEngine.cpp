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
//  The engine is a one-voice tick synthesiser with its own audio device. The
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
//    3. It must cost nothing. The render is two sines, a filtered noise term and
//       two envelopes; the buffer is 256 samples at the device's rate.
//
//  ONE SOUND, NOT FOUR. The old engine had four tiny synthesisers and each was
//  built from a bare sine and an unfiltered white burst, faded out from a step.
//  That is the recipe for the short, brittle "tick" people describe as an
//  unpleasant click: a full-amplitude onset, a sine with nothing around it and a
//  noise edge that runs to Nyquist. What is here instead is ONE tick - a damped
//  body, a short ping and a low-passed noise transient, with a sub-millisecond
//  attack ramp - and the four voices are four positions in it: the switch is the
//  brightest and shortest, a detent is quieter and lower, a key going down is the
//  lowest body, a key coming up is a touch above it. The panel therefore sounds
//  like one panel rather than like four different gadgets.
//
//  ...AND NEVER QUITE THE SAME TWICE. Every hit draws a small random offset for
//  pitch (about a semitone either way at most), body length, transient length,
//  level and stereo placement. The result is the psychoacoustic trick a real
//  mechanical keyboard plays: the ear hears one sound, but a repeated press does
//  not become a machine-gun tone, because no two hits are identical. The offsets
//  are deliberately narrow - a wide randomisation would read as a broken control
//  rather than as a hand on one.
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
    bodyPhase = 0.0f;
    pingPhase = 0.0f;
    bodyEnvelope = 0.0f;
    clickEnvelope = 0.0f;
    attack = 0.0f;
    noiseLow = 0.0f;
    noiseState = 0.0f;

    // The transient's noise is band-limited to about 4 kHz. Left white it runs to
    // Nyquist, which is where the brittle part of the old click lived: the ear
    // reads a full-bandwidth burst as a digital edge, and a low-passed one as a
    // tap. The corner is a real frequency, converted once here, so the sound is
    // the same at every device rate.
    const auto rate = static_cast<float> (juce::jmax (8000.0, deviceRate));
    noiseCoefficient = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 4000.0f / rate);

    // The attack ramp: 0.7 ms from silence to full. Short enough that the hit
    // still reads as immediate, long enough that it is not a step - and a step is
    // exactly what the ear hears as a click.
    attackStep = 1.0f / (0.0007f * rate);
}

void FirstAudioProcessorEditor::UiSoundEngine::audioDeviceStopped()
{
    bodyEnvelope = 0.0f;
    clickEnvelope = 0.0f;
}

void FirstAudioProcessorEditor::UiSoundEngine::trigger (Voice voice) noexcept
{
    if (! enabled.load (std::memory_order_relaxed))
        return;

    // The one timbre, and the four places in it. `click` here is the level of the
    // transient relative to the body, not a separate sound; `clickTail` is how
    // long that transient lasts, and it is always a fraction of the body's own
    // tail. The noise share is what part of the transient is grit rather than
    // ping - the switch has the most, because a switch has a leaf spring in it.
    float bodyHz = 300.0f;
    float tailMs = 12.0f;
    float click = 0.55f;
    float clickTailMs = 2.6f;
    float noise = 0.35f;
    float amplitude = 0.105f;

    switch (voice)
    {
        case Voice::click:
            bodyHz = 310.0f;  tailMs = 12.0f; click = 0.55f; clickTailMs = 2.6f;
            noise = 0.35f;    amplitude = 0.105f;
            break;

        case Voice::detent:
            bodyHz = 245.0f;  tailMs = 16.0f; click = 0.40f; clickTailMs = 2.2f;
            noise = 0.28f;    amplitude = 0.078f;
            break;

        case Voice::press:
            bodyHz = 165.0f;  tailMs = 20.0f; click = 0.30f; clickTailMs = 1.9f;
            noise = 0.22f;    amplitude = 0.095f;
            break;

        case Voice::release:
            bodyHz = 195.0f;  tailMs = 18.0f; click = 0.34f; clickTailMs = 1.7f;
            noise = 0.22f;    amplitude = 0.080f;
            break;
    }

    // The per-hit variation. Pitch moves by up to about a semitone, the tails by
    // a sixth, the level by half a decibel, and the hit lands slightly off centre
    // in the pair - enough that a rapid series of presses sounds played rather
    // than sequenced, small enough that the voice never changes identity.
    bodyHz *= 0.955f + random.nextFloat() * 0.09f;
    tailMs *= 0.90f + random.nextFloat() * 0.20f;
    clickTailMs *= 0.86f + random.nextFloat() * 0.28f;
    amplitude *= 0.945f + random.nextFloat() * 0.11f;
    noise = juce::jlimit (0.0f, 1.0f, noise + (random.nextFloat() - 0.5f) * 0.08f);

    // A busy machine ticks a touch higher, which is what `brightness` is for: the
    // panel's own activity feeds it, and the tick follows the machine rather than
    // sitting at one fixed pitch all session.
    const auto lift = brightness.load (std::memory_order_relaxed);
    bodyHz *= 1.0f + lift * 0.10f;

    pendingAmplitude.store (amplitude, std::memory_order_relaxed);
    pendingPitch.store (bodyHz, std::memory_order_relaxed);
    pendingDecay.store (tailMs, std::memory_order_relaxed);
    pendingClick.store (click, std::memory_order_relaxed);
    pendingClickDecay.store (clickTailMs, std::memory_order_relaxed);
    pendingNoise.store (noise, std::memory_order_relaxed);
    pendingPan.store ((random.nextFloat() - 0.5f) * 0.22f, std::memory_order_relaxed);
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
        bodyEnvelope = amplitude;
        clickEnvelope = 1.0f;
        attack = 0.0f;
        bodyPhase = 0.0f;
        pingPhase = 0.0f;

        const auto rate = static_cast<float> (juce::jmax (8000.0, deviceRate));
        const auto twoPi = juce::MathConstants<float>::twoPi;

        // The ping sits 4.3 times above the body, so the two are never a musical
        // interval that would read as a note. The body's phase increment is read
        // here rather than in trigger() because it needs the DEVICE's rate, which
        // the message thread does not know.
        bodyIncrement = twoPi * pendingPitch.load (std::memory_order_relaxed) / rate;
        pingIncrement = bodyIncrement * 4.3f;

        const auto decayMs = juce::jmax (0.5f, pendingDecay.load (std::memory_order_relaxed));
        const auto clickMs = juce::jmax (0.3f, pendingClickDecay.load (std::memory_order_relaxed));
        bodyDecayPerSample = std::exp (-1.0f / (0.001f * decayMs * rate));
        clickDecayPerSample = std::exp (-1.0f / (0.001f * clickMs * rate));

        clickLevel = pendingClick.load (std::memory_order_relaxed);
        noiseShare = pendingNoise.load (std::memory_order_relaxed);
        pan = pendingPan.load (std::memory_order_relaxed);
    }

    if (bodyEnvelope < 1.0e-5f && clickEnvelope < 1.0e-5f)
    {
        for (int channel = 0; channel < numOutputChannels; ++channel)
            juce::FloatVectorOperations::clear (outputChannelData[channel], numSamples);
        return;
    }

    for (int sample = 0; sample < numSamples; ++sample)
    {
        attack = juce::jmin (1.0f, attack + attackStep);

        const auto body = std::sin (bodyPhase);
        bodyPhase += bodyIncrement;
        if (bodyPhase >= juce::MathConstants<float>::twoPi)
            bodyPhase -= juce::MathConstants<float>::twoPi;

        const auto ping = std::sin (pingPhase);
        pingPhase += pingIncrement;
        if (pingPhase >= juce::MathConstants<float>::twoPi)
            pingPhase -= juce::MathConstants<float>::twoPi;

        // The transient's grit: cheap noise, low-passed once. The state is shared
        // across hits on purpose - it means a second press starts from whatever
        // the filter was holding rather than from a hard zero, which is one less
        // step in the waveform.
        noiseState = noiseState * 1.27f + 0.31f;
        if (noiseState > 1.0f) noiseState -= 2.0f;
        noiseLow += (noiseState - noiseLow) * noiseCoefficient;

        const auto transient = (ping * (1.0f - noiseShare) + noiseLow * noiseShare)
                             * clickLevel * clickEnvelope;

        const auto value = (body + transient) * bodyEnvelope * attack;

        // A small pan, so two presses in a row are not an identical point in the
        // stereo field. Beyond the pair the value is written unpanned.
        for (int channel = 0; channel < numOutputChannels; ++channel)
            outputChannelData[channel][sample] = channel == 0 ? value * (1.0f - pan)
                                                : channel == 1 ? value * (1.0f + pan)
                                                               : value;

        bodyEnvelope *= bodyDecayPerSample;
        clickEnvelope *= clickDecayPerSample;
    }
}
