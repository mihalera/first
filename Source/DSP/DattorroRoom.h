// =============================================================================
//  The room: Jon Dattorro's 1997 plate, as el-visio/dattorro-verb implements it.
//
//  Compiled and driven by tests/reverb/run.sh, which links the library's own
//  verb.c and renders impulses through this header. The conventions of
//  Source/DSP/ are stated in J37Math.h.
// =============================================================================
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <vector>

// The library is C, so its header has to be included as C. Without this the
// declarations below get C++ linkage, every symbol is mangled, and the failure
// arrives at link time in a platform build that never saw tests/reverb.
extern "C"
{
#include "verb.h"
}

//==============================================================================
/**
    DattorroRoom - the reverb stage, built on el-visio/dattorro-verb.

    WHY A LIBRARY, after the plugin had a reverb of its own.

    `Source/DSP/PlateReverb.h` was four Schroeder all-passes into two comb banks,
    written here, measured by nothing, and it sounded like what it is: a room
    sketched in eight delay lines. Dattorro's Effect Design Part 1 names the
    numbers a plate needs - the input diffuser lengths, the two tank halves that
    cross-feed, the modulated all-pass that keeps the tail from ringing on one
    pitch, the seven taps the stereo pair is summed from - and el-visio's library
    is those numbers as 400 lines of C. Re-tuning a hand-rolled room to match it
    would be transcribing the paper, which is a library by another name.

    WHAT THE LIBRARY DOES NOT DO, and this wrapper does.

      * **A rate.** A delay network is a set of DELAYS counted in samples, so a
        tank built from 48 kHz numbers is not a room at another rate: at 96 kHz
        every delay is half as long in time and the tail is half as long, at
        44.1 kHz it is 9% longer. The old plate scaled its lengths and got the
        size right that way; a third-party tank cannot be resized from here, so
        instead it is RUN at its own rate - 48 kHz, the rate its numbers were
        chosen at - and the signal is carried across, the same way `RateBridge`
        carries one to a model: the session's samples are interpolated at the
        tank's instants on the way in, the two wet outputs are interpolated at
        the session's instants on the way out, both with the four-tap cubic that
        struct uses. At 48 kHz the conversion is not merely skipped, it is
        ABSENT - the tank is stepped once per sample, straight through, which is
        what the library was written for.

        The tail is therefore the same LENGTH IN SECONDS at every session rate,
        which is the property `tests/reverb` measures at 44.1, 48 and 96 kHz.

      * **A stereo frame.** Dattorro's input is one signal and its output is two
        taps of the network, so the stage cannot be called per channel the way
        the plate it replaces was: it takes the frame's mono sum and returns the
        pair. A mono bus gets the pair summed back down, because a stereo room
        folded to one channel is what a mono track should hear.

      * **The controls.** SIZE sets the tail's length through the decay around
        the tank. DAMPING is the low-pass in that loop, and it is independent: a
        long tail can stay open, and a short one can be dark. The library's
        damping is a one-pole COEFFICIENT - 1 is no damping at all, which reads
        backwards from the way a plugin knob is named. PRE-DELAY is kept here at
        the session rate rather than in the tank, so it is a real time in
        milliseconds at every rate and a sweep is interpolated instead of
        stepping an integer tap. WIDTH is a mid/side trim on the two wet taps.

      * **A clear input.** A one-pole high-pass at 120 Hz feeds the tank. A room
        that keeps the sub-bass of the programme turns every low note into a long
        cloud, which is what makes a full-mix reverb sound muddy; the low cut is
        fixed because it is part of the room's voicing rather than another knob.

    The library is used exactly as published: this file includes it, calls it,
    and owns nothing of it. The one thing it adds is the rate conversion above,
    which is the wrapper's whole reason to exist.
*/
struct DattorroRoom
{
    /** The rate the tank's delay lengths were chosen at. Dattorro's paper states
        them against 48 kHz, and el-visio's `MAX_PREDELAY` comment says the same. */
    static constexpr double tankRate = 48000.0;

    /** Two rates this close are the same rate - the same tolerance `RateBridge`
        uses, for the same reason: one arrives from a host and one is a constant. */
    static constexpr double sameRate = 1.0e-6;

    /** Session samples the input kernel reads ahead of the tank instant it wants:
        the cubic is centred on its point, so two samples either side of it. */
    static constexpr int lookahead = 2;

    /** The session-rate history. It holds `lookahead` plus one tank spacing - at
        the widest ratio this build can see (48 kHz tank, 192 kHz session) that is
        six samples - and it is a fixed array because a sample-rate change arrives
        as a host parameter and nothing on that path may allocate. */
    static constexpr int xSize = 32;
    static constexpr int xMask = xSize - 1;

    /** The tank-rate history, one per wet output. The output kernel is centred on
        the tank's grid and reads two samples either side of its point, so five
        slots are the floor; eight is that with room for the rounding. */
    static constexpr int wSize = 8;
    static constexpr int wMask = wSize - 1;

    /** Tank samples the output kernel needs before it has anything to say. Below
        this the wet signal is silence - not the dry signal: a reverb whose tank
        has not run yet has no tail, and there is nothing to fade in. */
    static constexpr int ready = 5;

    /** Where the two wet taps sit against the dry, so that a MIX of 1 is a room
        at a comparable level rather than one that swamps the programme. Measured
        by tests/reverb: a 1 kHz tone at -20 dBFS through a SIZE of 0.5 leaves the
        wet at about the level of the dry. */
    static constexpr float wetGain = 0.35f;

    /** The five controls, in the units the parameters use. The defaults are a
        20 ms pre-delay, damping at 45 percent and a width of 100 percent. */
    struct Settings
    {
        float size = 0.4f;
        float mix = 0.0f;
        float preDelayMs = 20.0f;
        float damping = 0.45f;
        float width = 1.0f;
    };

    /** The longest pre-delay the control can ask for. The tank's own pre-delay is
        bypassed, so this is the whole of the delay and it is held in a buffer at
        the session rate rather than at the tank's fixed 48 kHz. */
    static constexpr float maxPreDelayMs = 100.0f;

    DattorroRoom() = default;

    ~DattorroRoom()
    {
        if (tank != nullptr)
            DattorroVerb_delete (tank);
    }

    DattorroRoom (const DattorroRoom&) = delete;
    DattorroRoom& operator= (const DattorroRoom&) = delete;

    /** Prepares for a session rate. Allocates the tank the first time - so this
        is a prepareToPlay call and not an audio-thread one - and re-creates it on
        a rate change, which clears it. */
    void prepare (double sampleRate)
    {
        sessionRate = sampleRate > 1.0 ? sampleRate : tankRate;
        step = tankRate / sessionRate;
        spacing = 1.0 / step;
        active = std::abs (step - 1.0) > sameRate;

        // The pre-delay is a session-rate ring, long enough for the control's
        // whole travel plus the two samples the linear read brackets. It is
        // allocated here, not on the audio thread, and reset() only fills it.
        const auto required = static_cast<std::size_t> (
            std::ceil (sessionRate * static_cast<double> (maxPreDelayMs) * 0.001)) + 8;
        preDelayBuffer.assign (juce::jmax<std::size_t> (8, required), 0.0f);
        preDelaySize = static_cast<int> (preDelayBuffer.size());

        // A 20 ms control ramp: fast enough to feel immediate, slow enough that a
        // dragged pre-delay glides through its fractional taps instead of making
        // the room jump. Size, damping and width use the same ramp.
        controlCoefficient = 1.0f - std::exp (-1.0f / (0.02f * static_cast<float> (sessionRate)));

        // The input's 120 Hz high-pass, in the standard one-pole DC-blocker form.
        lowCutR = juce::jlimit (0.5f, 0.9999f,
                                1.0f - (juce::MathConstants<float>::twoPi * 120.0f)
                                         / static_cast<float> (sessionRate));

        reset();
    }

    /** Clears the tank and the conversion's history.

        Re-creating the tank is how the library's own state is cleared: it exposes
        no reset, and memsetting its internals from here would be a wrapper that
        knows the thing it is wrapping. The allocation that comes with it is why
        this is a prepare-time call - the host's reset() and a session-rate change
        both arrive off the audio thread. */
    void reset()
    {
        if (tank != nullptr)
            DattorroVerb_delete (tank);

        tank = DattorroVerb_create();
        configureTank();

        std::fill (preDelayBuffer.begin(), preDelayBuffer.end(), 0.0f);
        preDelayWrite = 0;
        preDelaySamples = -1.0f;
        lowCutX = 0.0f;
        lowCutY = 0.0f;
        currentWidth = -1.0f;
        currentSize = -1.0f;
        currentDamping = -1.0f;
        lastSize = -1.0f;
        lastDamping = -1.0f;

        xHistory.fill (0.0f);
        wetLHistory.fill (0.0f);
        wetRHistory.fill (0.0f);
        xWrite = 0;
        wWrite = 0;
        sessionSamples = 0;
        tankSamples = 0;
        nextTankAt = 0.0;
        newestTankAt = 0.0;
    }

    /** Whether the stage can render at all - the tank exists and a rate has been
        prepared. A build whose tank failed to allocate is a bypass, not a crash. */
    bool isPrepared() const noexcept { return tank != nullptr; }

    /** How far behind the dry signal the tail arrives, in session samples, from
        the conversion's own read-ahead. At the tank's rate it is the library's
        own one-sample latency; at another rate it adds the interpolation's
        read-ahead. The pre-delay is separate and is in `Settings`. */
    double delay() const noexcept
    {
        return active ? static_cast<double> (lookahead) + 2.0 * spacing + 1.0 : 1.0;
    }

    /** One session frame through the room.

        `frame` is the whole frame - one sample per channel - and `channels` is how
        many of them the host handed over. `Settings` carries the five controls:
        SIZE sets the tail, MIX how much of the wet pair is added, and PRE-DELAY,
        DAMPING and WIDTH voice the room. At mix 0 the stage does nothing at all,
        and the frame is bit-for-bit what it was.

        Not const and not re-entrant: the tank's state belongs to the audio thread.
    */
    void processFrame (float* frame, int channels, const Settings& settings) noexcept
    {
        if (tank == nullptr || frame == nullptr || channels <= 0 || settings.mix <= 1.0e-5f)
            return;

        applySettings (settings);

        // One input, two taps: Dattorro's network is fed the mono sum, and the
        // width comes from the taps rather than from two rooms.
        auto in = channels > 1 ? 0.5f * (frame[0] + frame[1]) : frame[0];

        // The room hears a clear top and no sub-bass. This is before the tank's
        // own pre-filter, which is a low-pass, so the two form a band around the
        // range a plate actually reflects.
        lowCutY = in - lowCutX + lowCutR * lowCutY;
        lowCutX = in;
        in = lowCutY;

        // Pre-delay, in the session domain. It is interpolated because the
        // control is smoothed and its read point is almost never a whole sample.
        if (preDelaySize > 0)
        {
            const auto targetSamples = static_cast<float> (
                juce::jlimit (0.0f, maxPreDelayMs, settings.preDelayMs)
                * 0.001f * static_cast<float> (sessionRate));

            if (preDelaySamples < 0.0f)
                preDelaySamples = targetSamples;
            else
                preDelaySamples += (targetSamples - preDelaySamples) * controlCoefficient;

            preDelayWrite = (preDelayWrite + 1) % preDelaySize;
            preDelayBuffer[static_cast<std::size_t> (preDelayWrite)] = in;
            in = readPreDelay (preDelaySamples);
        }

        float wetLeft = 0.0f;
        float wetRight = 0.0f;

        if (! active)
        {
            // The session is at the tank's own rate: the library's own loop,
            // once per sample, which is the path it was written for.
            DattorroVerb_process (tank, static_cast<double> (in));
            wetLeft = static_cast<float> (DattorroVerb_getLeft (tank));
            wetRight = static_cast<float> (DattorroVerb_getRight (tank));
        }
        else
        {
            xWrite = (xWrite + 1) & xMask;
            xHistory[static_cast<std::size_t> (xWrite)] = in;

            const auto now = static_cast<double> (sessionSamples++);

            // The tank's own instants, in order. One is taken once its instant has
            // arrived AND its input kernel is fully behind the newest session
            // sample, which is the two samples `lookahead` names.
            while (nextTankAt <= now - static_cast<double> (lookahead))
            {
                DattorroVerb_process (tank, static_cast<double> (xAtAge (now - nextTankAt)));

                wWrite = (wWrite + 1) & wMask;
                wetLHistory[static_cast<std::size_t> (wWrite)]
                    = static_cast<float> (DattorroVerb_getLeft (tank));
                wetRHistory[static_cast<std::size_t> (wWrite)]
                    = static_cast<float> (DattorroVerb_getRight (tank));

                newestTankAt = nextTankAt;
                tankSamples = tankSamples < wSize ? tankSamples + 1 : wSize;
                nextTankAt += spacing;
            }

            if (tankSamples >= ready)
            {
                // Where the session instant `now - outputAge` falls on the tank's
                // grid, in TANK samples back from the newest one.
                const auto age = (newestTankAt - (now - outputAge())) * step;
                wetLeft = wetAt (wetLHistory, age);
                wetRight = wetAt (wetRHistory, age);
            }
        }

        const auto mix = juce::jlimit (0.0f, 1.0f, settings.mix);

        if (channels > 1)
        {
            const auto targetWidth = juce::jlimit (0.0f, 2.0f, settings.width);

            if (currentWidth < 0.0f)
                currentWidth = targetWidth;
            else
                currentWidth += (targetWidth - currentWidth) * controlCoefficient;

            const auto mid = 0.5f * (wetLeft + wetRight);
            const auto side = 0.5f * (wetLeft - wetRight) * currentWidth;
            frame[0] += (mid + side) * wetGain * mix;
            frame[1] += (mid - side) * wetGain * mix;
        }
        else
        {
            frame[0] += 0.5f * (wetLeft + wetRight) * wetGain * mix;
        }
    }

private:
    /** How far behind the newest session sample the wet pair is read: the input
        kernel's lookahead plus the two tank spacings the output kernel needs. */
    double outputAge() const noexcept
    {
        return static_cast<double> (lookahead) + 2.0 * spacing;
    }

    /** The tank's wet pair at `age` TANK samples back from the newest one, through
        the four-point cubic. The taps are the tank's grid, which is uniform, so
        the point is an offset in tank samples - and that offset is bounded by the
        two spacings the output delay carries, which is why eight slots are enough
        for it. */
    float wetAt (const std::array<float, wSize>& history, double age) const noexcept
    {
        const auto tap = static_cast<int> (std::floor (age));
        const auto fraction = static_cast<float> (age - static_cast<double> (tap));

        return cubic (historyAt (history, tap - 1), historyAt (history, tap),
                      historyAt (history, tap + 1), historyAt (history, tap + 2), fraction);
    }

    /** `age` tank samples ago, by ring index: the newest - at the write position -
        is age 0. */
    float historyAt (const std::array<float, wSize>& history, int age) const noexcept
    {
        return history[static_cast<std::size_t> ((wWrite - age) & wMask)];
    }

    /** The session signal `age` samples ago - the newest is age 0 - interpolated
        between the four samples that bracket it. */
    float xAtAge (double age) const noexcept
    {
        const auto whole = static_cast<int> (std::floor (age));
        const auto fraction = static_cast<float> (age - static_cast<double> (whole));

        return cubic (xAt (whole - 1), xAt (whole), xAt (whole + 1), xAt (whole + 2), fraction);
    }

    float xAt (int age) const noexcept
    {
        return xHistory[static_cast<std::size_t> ((xWrite - age) & xMask)];
    }

    /** The cubic through four samples at the fraction `u` past the second of them
        - Catmull-Rom, the same four-point interpolator `RateBridge` uses, and the
        reason the conversion is smooth rather than piecewise linear. */
    static float cubic (float y0, float y1, float y2, float y3, float u) noexcept
    {
        return 0.5f * (2.0f * y1
                       + (-y0 + y2) * u
                       + (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3) * u * u
                       + (-y0 + 3.0f * y1 - 3.0f * y2 + y3) * u * u * u);
    }

    /** The tank's fixed voicing. The library's pre-delay is deliberately zero:
        this wrapper owns the time in milliseconds, at the session rate. */
    void configureTank() noexcept
    {
        if (tank == nullptr)
            return;

        DattorroVerb_setPreDelay (tank, 0.0);
        DattorroVerb_setPreFilter (tank, 0.85);
        DattorroVerb_setInputDiffusion1 (tank, 0.75);
        DattorroVerb_setInputDiffusion2 (tank, 0.625);
        DattorroVerb_setDecayDiffusion (tank, 0.70);
    }

    /** The pre-delay ring at a fractional age. */
    float readPreDelay (float samples) const noexcept
    {
        auto readPosition = static_cast<float> (preDelayWrite) - samples;

        while (readPosition < 0.0f)
            readPosition += static_cast<float> (preDelaySize);

        const auto whole = static_cast<int> (std::floor (readPosition));
        const auto fraction = readPosition - static_cast<float> (whole);
        const auto a = preDelayBuffer[static_cast<std::size_t> (whole % preDelaySize)];
        const auto b = preDelayBuffer[static_cast<std::size_t> ((whole + 1) % preDelaySize)];

        return a + (b - a) * fraction;
    }

    /** SIZE and DAMPING to the library's settings. Called per frame; it only
        touches the tank when a control has actually moved. */
    void applySettings (const Settings& settings) noexcept
    {
        if (tank == nullptr)
            return;

        const auto targetSize = juce::jlimit (0.0f, 1.0f, settings.size);
        const auto targetDamping = juce::jlimit (0.0f, 1.0f, settings.damping);

        if (currentSize < 0.0f)
            currentSize = targetSize;
        else
            currentSize += (targetSize - currentSize) * controlCoefficient;

        if (currentDamping < 0.0f)
            currentDamping = targetDamping;
        else
            currentDamping += (targetDamping - currentDamping) * controlCoefficient;

        const auto size = currentSize;
        const auto damping = currentDamping;
        const auto s = static_cast<double> (size);

        if (std::abs (size - lastSize) >= 1.0e-4f)
        {
            lastSize = size;

            // The tail. These two numbers are measured rather than chosen: the
            // harness prints the T60 they produce, and the ends of the control
            // are a room that has died away before the next word (0.54 s) and a
            // plate with a long one (4.9 s). The shape between them is the
            // library's own exponential - a feedback loop's decay is logarithmic
            // in its gain - and that is why the control is worth having at all:
            // the useful part of a reverb is its LENGTH, and length doubles where
            // gain barely moves.
            DattorroVerb_setDecay (tank, 0.24 + 0.64 * s);
        }

        // DAMPING is a separate decision now. The library's damping is a one-pole
        // coefficient, so a SMALLER number is a darker tail. At the default 45
        // percent this is 0.448, the coefficient the old SIZE 0.4 produced; the
        // ends run from an open 0.70 to a dark 0.14.
        if (std::abs (damping - lastDamping) >= 1.0e-4f)
        {
            lastDamping = damping;
            DattorroVerb_setDamping (tank, 0.70 - 0.56 * static_cast<double> (damping));
        }
    }

    sDattorroVerb* tank = nullptr;

    double sessionRate = tankRate;
    double step = 1.0;      ///< tank samples per session sample
    double spacing = 1.0;   ///< session samples per tank sample
    bool active = false;    ///< the conversion is actually running
    float lastSize = -1.0f; ///< so the first applySettings always writes
    float lastDamping = -1.0f;
    float currentWidth = 1.0f;
    float currentSize = -1.0f;
    float currentDamping = -1.0f;
    float controlCoefficient = 1.0f;

    std::vector<float> preDelayBuffer;
    int preDelaySize = 0;
    int preDelayWrite = 0;
    float preDelaySamples = -1.0f;
    float lowCutX = 0.0f;
    float lowCutY = 0.0f;
    float lowCutR = 0.0f;

    std::array<float, xSize> xHistory {};
    std::array<float, wSize> wetLHistory {};
    std::array<float, wSize> wetRHistory {};
    int xWrite = 0;
    int wWrite = 0;
    long long sessionSamples = 0;
    int tankSamples = 0;
    double nextTankAt = 0.0;
    double newestTankAt = 0.0;
};
