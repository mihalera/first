// =============================================================================
//  The OSC module's struck voice: a small modal bank, not a sine.
//
//  WHAT IT IS. A struck bar is not one frequency. It is a handful of modes -
//  the fundamental and the inharmonic partials above it - each ringing for its
//  own length of time. This class is that bank: six two-pole resonators, tuned
//  from the MIDI note the ring landed on, excited by one impulse per wall hit.
//  The result is a short pitched strike with the woody/metallic edge a sine
//  cannot give, and it costs one multiply-add chain per mode per sample.
//
//  WHY NOT STK. STK's ModalBar is the same idea with a larger surface: files,
//  a build target and a licensing notice for four modes. The processor only
//  needs the four arithmetic operations below, and the voice has to run on the
//  audio thread without allocation - so the algorithm lives here, where a
//  harness can drive it without a toolkit, and no dependency is added for it.
//
//  WHY THE MODES ARE INHARMONIC. A bar's partials are not integer multiples of
//  the fundamental (a string's are). The ratios below are the first six of a
//  free-free bar, close to STK's ModalBar set: 1, 2.756, 5.404, 8.933, 13.34,
//  18.64. They are what makes the attack read as a struck object rather than
//  as an organ stop.
//
//  WHY THE DECAYS SCALE WITH PITCH. A small bar radiates its energy faster
//  than a large one. Each mode's decay is shortened as the note rises, so the
//  top of the ring's three octaves is a shorter tick and the bottom is a longer
//  ring - the same instrument, not a sample pitched up.
//
//  REALTIME. prepare() and reset() are the only calls that touch more than a
//  handful of floats; trigger() runs once per wall hit, and renderSample() is
//  branch-free apart from the tiny denormal flush. No allocation, no lock, no
//  JUCE, no lookbehind.
// =============================================================================
#pragma once

#include <array>
#include <cmath>
#include <cstddef>

namespace j37dsp
{

class ModalVoice
{
public:
    static constexpr int modeCount = 6;

    /** Sets the rate the resonators are tuned at. Called from prepareToPlay();
        safe to call again when the host changes the session rate. */
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = (std::isfinite (newSampleRate) && newSampleRate >= 8000.0)
                        ? newSampleRate : 48000.0;
        reset();
    }

    /** The rate the bank was last prepared for. The processor compares this
        with the engine rate each block, because oversampling changes that rate
        without a new prepareToPlay(). */
    double sampleRateHz() const noexcept { return sampleRate; }

    /** Silences the bank without changing its tuning. */
    void reset() noexcept
    {
        for (auto& mode : modes)
            mode = ModeState {};

        excitation = 0.0;
    }

    /** Starts a new strike.

        `frequencyHz` is the note's fundamental (the ring hands over an absolute
        MIDI note already converted by the processor). `amplitude` is the
        impact's gain, normally 0.35..1.25 from OscBall's curve; it scales the
        whole bank and opens the higher modes a little more on a hard hit, so
        the timbre follows the velocity rather than only the level. */
    void trigger (float frequencyHz, float amplitude) noexcept
    {
        reset();

        if (! std::isfinite (frequencyHz) || ! std::isfinite (amplitude))
            return;

        const auto fundamental = static_cast<double> (frequencyHz);
        const auto level = static_cast<double> (std::fmin (2.0f, std::fmax (0.0f, amplitude)));

        if (fundamental < 20.0 || level <= 0.0)
            return;

        for (int index = 0; index < modeCount; ++index)
        {
            const auto& spec = modeSpecs[static_cast<std::size_t> (index)];
            auto& mode = modes[static_cast<std::size_t> (index)];

            // Above Nyquist there is no mode to ring. Skipping it is better
            // than folding it back down as an alias; the higher a note is, the
            // more of the bank is simply absent, which is also what a real
            // small bar does.
            const auto frequency = fundamental * spec.ratio;

            if (frequency >= sampleRate * 0.45)
                continue;

            const auto decay = decaySeconds (spec.decay, fundamental);
            const auto radius = std::exp (-1.0 / (decay * sampleRate));
            const auto omega = 2.0 * 3.14159265358979323846 * frequency / sampleRate;

            // The strike excites the high modes less at low velocity. The
            // fundamental is left alone so a soft hit is still the same note.
            const auto velocityTilt = index < 2
                                        ? 1.0
                                        : 0.60 + 0.40 * std::fmin (1.0, level);

            mode.b0 = std::sin (omega) * spec.gain * level * velocityTilt * outputTrim;
            mode.a1 = 2.0 * radius * std::cos (omega);
            mode.a2 = -radius * radius;
        }

        excitation = 1.0;
    }

    /** One sample of the sum of the ringing modes. */
    float renderSample() noexcept
    {
        const auto input = excitation;
        excitation = 0.0;

        auto sum = 0.0;

        for (auto& mode : modes)
        {
            const auto output = mode.b0 * input + mode.a1 * mode.y1 + mode.a2 * mode.y2;

            mode.y2 = mode.y1;
            mode.y1 = output;
            sum += output;

            // A mode that has rung down to the last few bits is silence, not
            // a denormal to carry through every later sample.
            if (std::fabs (mode.y1) < 1.0e-15 && std::fabs (mode.y2) < 1.0e-15)
            {
                mode.y1 = 0.0;
                mode.y2 = 0.0;
            }
        }

        return static_cast<float> (sum);
    }

    /** True once every mode has decayed below the audible floor. */
    bool isSilent() const noexcept
    {
        if (std::fabs (excitation) > 0.0)
            return false;

        for (const auto& mode : modes)
            if (std::fabs (mode.y1) >= 1.0e-9 || std::fabs (mode.y2) >= 1.0e-9)
                return false;

        return true;
    }

private:
    struct ModeSpec
    {
        double ratio;
        double gain;
        double decay; // seconds at 440 Hz, before the pitch-dependent scaling
    };

    struct ModeState
    {
        double b0 = 0.0;
        double a1 = 0.0;
        double a2 = 0.0;
        double y1 = 0.0;
        double y2 = 0.0;
    };

    static constexpr std::array<ModeSpec, modeCount> modeSpecs {{
        { 1.000,  0.58, 1.90 },
        { 2.756,  0.28, 1.25 },
        { 5.404,  0.16, 0.82 },
        { 8.933,  0.09, 0.52 },
        { 13.34,  0.05, 0.34 },
        { 18.64,  0.03, 0.24 }
    }};

    /** The bank's gains sum to a little over one; this keeps a full-velocity
        strike near unity before the processor applies its own low OSC level. */
    static constexpr double outputTrim = 0.82;

    static double decaySeconds (double base, double fundamental) noexcept
    {
        const auto scale = std::pow (440.0 / fundamental, 0.28);
        return std::fmin (4.0, std::fmax (0.08, base * scale));
    }

    std::array<ModeState, modeCount> modes {};
    double sampleRate = 48000.0;
    double excitation = 0.0;
};

} // namespace j37dsp
