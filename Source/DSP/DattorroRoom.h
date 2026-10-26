// =============================================================================
//  The room: Jon Dattorro's 1997 plate, as el-visio/dattorro-verb implements it.
//
//  Compiled and driven by tests/reverb/run.sh, which links the library's own
//  verb.c and renders impulses through this header. The conventions of
//  Source/DSP/ are stated in J37Math.h.
// =============================================================================
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>

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

      * **A control.** The stage has one parameter, SIZE, and it is mapped to the
        two settings that set a tail's length and its colour: the decay, which is
        the feedback around the tank, and the damping, which is the low-pass in
        that loop. A bigger room is a longer tail and a darker one, so the decay
        rises with SIZE and the damping coefficient falls (the library's damping
        is a one-pole COEFFICIENT - 1 is no damping at all, which reads backwards
        from the way a plugin knob is named).

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
        if (tank == nullptr)
            tank = DattorroVerb_create();

        if (tank == nullptr)
            return;

        step = sampleRate > 1.0 ? tankRate / sampleRate : 1.0;
        spacing = 1.0 / step;
        active = std::abs (step - 1.0) > sameRate;

        applySize (lastSize);
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
        {
            DattorroVerb_delete (tank);
            tank = DattorroVerb_create();
        }

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
        the conversion's own read-ahead. Zero when the session runs at the tank's
        rate, where the room is the library's own latency (one sample, from the
        order the taps are read in) and nothing else. */
    double delay() const noexcept
    {
        return active ? static_cast<double> (lookahead) + 2.0 * spacing + 1.0 : 1.0;
    }

    /** One session frame through the room.

        `frame` is the whole frame - one sample per channel - and `channels` is how
        many of them the host handed over. `size` is 0..1 and sets the tail; `mix`
        is 0..1 and is how much of the wet pair is added to the dry. At mix 0 the
        stage does nothing at all, and the frame is bit-for-bit what it was.

        Not const and not re-entrant: the tank's state belongs to the audio thread.
    */
    void processFrame (float* frame, int channels, float size, float mix) noexcept
    {
        if (tank == nullptr || frame == nullptr || channels <= 0 || mix <= 1.0e-5f)
            return;

        applySize (size);

        // One input, two taps: Dattorro's network is fed the mono sum, and the
        // width comes from the taps rather than from two rooms.
        const auto in = channels > 1 ? 0.5f * (frame[0] + frame[1]) : frame[0];

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

        if (channels > 1)
        {
            frame[0] += wetLeft * wetGain * mix;
            frame[1] += wetRight * wetGain * mix;
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

    /** SIZE to the library's two settings. Called per frame; it only touches the
        tank when the control has actually moved, which is once per block in
        practice because the stage smooths SIZE before it gets here. */
    void applySize (float size) noexcept
    {
        const auto clamped = juce::jlimit (0.0f, 1.0f, size);

        if (tank == nullptr || std::abs (clamped - lastSize) < 1.0e-4f)
        {
            lastSize = clamped;
            return;
        }

        lastSize = clamped;
        const auto s = static_cast<double> (clamped);

        // The tail. These two numbers are measured rather than chosen: the harness
        // prints the T60 they produce, and the ends of the control are a room that
        // has died away before the next word (0.54 s) and a plate with a long one
        // (4.9 s). The shape between them is the library's own exponential - a
        // feedback loop's decay is logarithmic in its gain - and that is why the
        // control is worth having at all: the useful part of a reverb is its
        // LENGTH, and length doubles where gain barely moves.
        DattorroVerb_setDecay (tank, 0.24 + 0.64 * s);

        // The colour: a bigger room absorbs more top end per pass, and the
        // library's damping is a one-pole coefficient, so a LONGER tail is a
        // SMALLER number here. 0.55 is an open room, 0.30 a dark plate.
        DattorroVerb_setDamping (tank, 0.55 - 0.25 * s);

        // Everything else is the design's own starting point, stated here so the
        // sound is one file's worth of numbers rather than the library's defaults
        // half-overridden: a short pre-delay so the tail sits behind the note, a
        // bright pre-filter, the paper's diffuser gains.
        DattorroVerb_setPreDelay (tank, 0.02);
        DattorroVerb_setPreFilter (tank, 0.85);
        DattorroVerb_setInputDiffusion1 (tank, 0.75);
        DattorroVerb_setInputDiffusion2 (tank, 0.625);
        DattorroVerb_setDecayDiffusion (tank, 0.70);
    }

    sDattorroVerb* tank = nullptr;

    double step = 1.0;      ///< tank samples per session sample
    double spacing = 1.0;   ///< session samples per tank sample
    bool active = false;    ///< the conversion is actually running
    float lastSize = -1.0f; ///< so the first applySize always writes

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
