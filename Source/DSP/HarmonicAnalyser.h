// =============================================================================
//  The harmonic analyser: live readout of a nonlinear stage.
//
//  Moved out of PluginProcessor.h, where it was the display's numbers, measured on the shaper's own input and output. The conventions of
//  Source/DSP/ are stated in J37Math.h, and tests/dsp/check_headers_juce.sh
//  compiles this header against real JUCE along with the rest of the directory.
// =============================================================================
#pragma once

#include <cmath>

//==============================================================================
/**
    Live harmonic analysis of a nonlinear stage.

    The analogue character of this plugin lives in the harmonics its shaper adds, so rather
    than trusting the curve on paper, the stage is measured: Goertzel filters run at the 2nd
    and 3rd harmonic of a tracked fundamental and report how much energy is even (2nd)
    against odd (3rd) relative to it.

    Those two bins are enough to characterise the stage because the split they show is the
    one that matters: even content is what the bias asymmetry contributes and reads as
    warmth, odd content is what the symmetric tanh contributes and reads as edge. Measuring
    more bins would cost more for no extra insight into that balance.

    Two things make this usable as a live meter: it runs only every N samples so the cost is
    negligible, and it uses the shaper's own input and output, so what it reports is the
    distortion that was actually produced, not a prediction.

    This lives in the header rather than in an anonymous namespace in the .cpp because the
    processor holds one by value as a member, and a member's type has to be visible where
    the class is declared.
*/
struct HarmonicAnalyser
{
    void reset() noexcept
    {
        evenRatio = 0.0f;
        oddRatio = 0.0f;
        fundamentalLevel = 0.0f;
        trackedFrequency = 220.0f;
        samplesSinceCrossing = 1;
        sampleCounter = 0;
        previousPositive = true;
        previous1 = 0.0f;
        previous2 = 0.0f;
        lastMagnitude = 0.0f;
        windowCounter = 0;
    }

    /**
        Feeds one sample of the shaper's input and output.

        Return value: true once a fresh harmonic reading has been produced this call, false
        while the measurement is still accumulating or while there is too little signal to
        measure. Callers that only want the running values can use the getters instead and
        ignore the return entirely.
    */
    bool analyse (float shaperInput, float shaperOutput, float sampleRate)
    {
        // The fundamental is taken from the zero-crossing rate of the input, which is cheap
        // and needs no FFT. The estimate is heavily smoothed because it only has to be in
        // the right region for the harmonic bins to line up.
        const auto absInput = std::abs (shaperInput);
        if ((shaperInput >= 0.0f) != previousPositive && absInput > 1.0e-4f)
        {
            // samplesSinceCrossing is a period in SAMPLES, so it is converted to a frequency
            // by dividing the rate. It used to be passed through a jlimit with frequency
            // bounds, which was dimensionally wrong: clamping a sample count against a Hz
            // range silently picked the wrong branch and the estimate only worked because
            // the bounds happened to be wide. The period itself is what needs guarding, so
            // it is clamped to a sane sample range and the resulting frequency is bounded
            // separately below.
            const auto periodSamples = static_cast<float> (
                juce::jlimit (2, juce::jmax (2, static_cast<int> (sampleRate)), samplesSinceCrossing));
            const auto instantFrequency = sampleRate / periodSamples;
            trackedFrequency += (instantFrequency - trackedFrequency) * 0.05f;
            samplesSinceCrossing = 0;
        }

        previousPositive = shaperInput >= 0.0f;
        ++samplesSinceCrossing;

        // Only measure while there is real signal, and only occasionally.
        if (absInput < 1.0e-3f)
        {
            fundamentalLevel += (0.0f - fundamentalLevel) * 0.05f;
            return false;
        }

        // Measure at a fixed RATE rather than every fixed number of samples, so the update
        // frequency of the readout is the same at 44.1 kHz and 192 kHz. At a fixed stride
        // the analyser would run four times more often per second on a 192 kHz session,
        // costing four times as much for a display that updates at 30 Hz regardless.
        const auto stride = juce::jmax (16, juce::roundToInt (sampleRate / 700.0f));

        if (++sampleCounter < stride)
            return true;

        sampleCounter = 0;

        // Bound the tracked frequency to a range that is valid at any sample rate. The lower
        // edge is a musical floor and the upper edge is kept clear of Nyquist, and the
        // maximum is taken with jmax so the two can never cross over - passing inverted
        // bounds to jlimit would return an undefined value rather than the nearest limit.
        const auto maxFrequency = juce::jmax (60.0f, sampleRate * 0.45f);
        const auto frequency = juce::jlimit (30.0f, maxFrequency, trackedFrequency);

        // The output is captured explicitly: a lambda has no access to the enclosing
        // function's parameters unless they are named in the capture list.
        const auto ratioAt = [this, shaperOutput, frequency, sampleRate, maxFrequency] (float bin)
        {
            if (bin * frequency >= maxFrequency)
                return 0.0f;

            return std::abs (goertzel (shaperOutput, bin * frequency, sampleRate));
        };

        const auto fundamental = juce::jmax (1.0e-6f, std::abs (goertzel (shaperInput,
                                                                         frequency, sampleRate)));
        const auto second = ratioAt (2.0f);
        const auto third = ratioAt (3.0f);

        // Relative to the fundamental, so the reading is meaningful at any level: this is a
        // distortion ratio, not an absolute power.
        const auto even = second / fundamental;
        const auto odd = third / fundamental;

        const auto smoothing = 0.15f;
        evenRatio += (even - evenRatio) * smoothing;
        oddRatio += (odd - oddRatio) * smoothing;
        fundamentalLevel += (fundamental - fundamentalLevel) * smoothing;

        return true;
    }

    /** Second-harmonic content relative to the fundamental - warmth and body. */
    float getEvenRatio() const noexcept { return evenRatio; }

    /** Third-harmonic content relative to the fundamental - edge and density. */
    float getOddRatio() const noexcept { return oddRatio; }

    /** How much level the shaper saw, so the display can dim when there is no signal. */
    float getFundamentalLevel() const noexcept { return fundamentalLevel; }

private:
    /**
        Single-bin magnitude estimate, evaluated over the most recent window so it is
        independent of the block size. Used instead of an FFT because only a couple of bins
        are needed and this costs a fraction of a full transform.
    */
    float goertzel (float sample, float frequency, float sampleRate) noexcept
    {
        const auto omega = juce::MathConstants<float>::twoPi * frequency / sampleRate;
        const auto coefficient = 2.0f * std::cos (omega);

        // The window length is a fixed TIME, not a fixed number of samples, so the bin width
        // stays the same at every supported rate. A fixed sample count would make the window
        // shrink with the sample rate - at 192 kHz 512 samples is only 2.7 ms and the bin
        // width balloons to 375 Hz, which is wider than the 1 kHz gap between harmonics and
        // makes the even/odd split meaningless.
        //
        // 11.6 ms is the window that 512 samples gives at 44.1 kHz, so the behaviour at the
        // lower rates is unchanged and the higher ones now match it.
        const auto samplesPerWindow = juce::jmax (64, juce::roundToInt (0.0116 * sampleRate));

        const auto current = sample + coefficient * previous1 - previous2;
        previous2 = previous1;
        previous1 = current;

        // The window is reset periodically rather than run forever, which keeps the
        // recurrence from accumulating numerical error over a long session.
        if (++windowCounter >= samplesPerWindow)
        {
            // Power at the bin, from the final two states of the recurrence.
            const auto power = previous1 * previous1 + previous2 * previous2
                             - coefficient * previous1 * previous2;

            previous1 = 0.0f;
            previous2 = 0.0f;
            windowCounter = 0;

            lastMagnitude = std::sqrt (juce::jmax (0.0f, power))
                          / static_cast<float> (samplesPerWindow);
            lastMagnitude = juce::jlimit (0.0f, 4.0f, lastMagnitude);
        }

        return lastMagnitude;
    }

    float trackedFrequency = 220.0f;
    float evenRatio = 0.0f;
    float oddRatio = 0.0f;
    float fundamentalLevel = 0.0f;
    int samplesSinceCrossing = 1;
    int sampleCounter = 0;
    bool previousPositive = true;

    // Goertzel recurrence state and its measurement window.
    float previous1 = 0.0f;
    float previous2 = 0.0f;
    float lastMagnitude = 0.0f;
    int windowCounter = 0;
};
