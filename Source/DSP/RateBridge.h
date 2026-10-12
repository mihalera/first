// =============================================================================
//  The rate bridge: carries a signal between the engine's rate and a model's.
//
//  Compiled and driven by tests/neural/rate_test.cpp (and by the DSP harness
//  through it), which include this header rather than a copy cut out of
//  PluginProcessor.h. The conventions of Source/DSP/ are stated in J37Math.h.
// =============================================================================
#pragma once

#include <array>
#include <cmath>

//==============================================================================
/**
    RateBridge - carries a signal from the ENGINE's rate to a MODEL's own rate and
    back, so a model is run at the rate it was trained at rather than the one the
    engine happens to be running at.

    A .nam file is trained at one rate - almost always 48 kHz - and the engine runs
    at the session's rate times its oversampling factor, so a 48 kHz session with
    4x oversampling hands the model 192 000 samples a second. A neural net is not a
    set of coefficients that mean the same thing at every rate the way a filter's
    do: its layers hold DELAYS and recurrences counted in samples, so running it
    four times too fast does not make it four times brighter, it makes it a
    different network - every cell's feedback four times tighter, every state four
    times shorter. Before this existed the stage ran the model at whatever rate the
    engine was at and said nothing about it.

    So the signal is carried across: the engine's samples are interpolated at the
    MODEL's own instants on the way in, and the model's output samples are
    interpolated at the ENGINE's instants on the way out, both with a four-tap
    cubic (Catmull-Rom) kernel. That kernel is a fractional-delay interpolator and
    not an antialias filter - the engine's own oversampling is what band-limits the
    programme, and this bridge moves it between two rates of the same signal rather
    than protecting either - which is the honest reading of what a rate conversion
    here can claim.

    A fractional delay is not causal, so the bridge reads a little way into the
    past: a model sample is taken two ENGINE samples after its own instant, and the
    engine's output is reconstructed two engine samples plus two model spacings
    behind. Every sample the bridge emits is therefore `delay()` engine samples
    late, and the DRY leg is read through the same interpolation at the same age -
    so the wet/dry crossfade the stage applies stays in phase and the conversion
    costs the stage nothing but its own delay. At 4x oversampling of a 48 kHz
    session that delay is ten engine samples, about fifty microseconds.

    When the two rates agree the bridge is not merely skipped, it is ABSENT: the
    model is handed the sample it was given, the dry leg is that same sample, and
    the stage is bit-for-bit what it was before the bridge existed. That is the
    path a session takes whenever its model was trained at the rate the engine is
    running at.
*/
struct RateBridge
{
    /** Two rates this close are the same rate. They arrive as doubles - one from a
        file, one from a host's buffer size - so the test cannot be equality; a
        millionth of a rate is a hundredth of a cent over a five-minute take. */
    static constexpr double sameRate = 1.0e-6;

    /** Engine samples the input kernel reads AHEAD of the instant it wants: the
        cubic is centred on its point, so two samples either side of it. */
    static constexpr int lookahead = 2;

    /** The engine-side history. It has to hold the whole of the delay the bridge
        works at - `lookahead` plus two model spacings - and a model spacing is
        the engine's rate divided by the model's: at the widest ratio this build
        can produce (8x oversampling of a 192 kHz session against a 44.1 kHz
        model) that is 34.8 engine samples, so the delay runs to about 72 and 128
        slots cover it with room to spare. A fixed array rather than a vector
        because a rate change can arrive from the audio thread - the oversampling
        switch is a host parameter - and nothing on that path may allocate. */
    static constexpr int xSize = 128;
    static constexpr int xMask = xSize - 1;

    /** The model-side history. The output kernel is centred on the model's grid,
        so it reads two samples either side of its point; eight slots are five
        with room for the rounding. */
    static constexpr int vSize = 8;
    static constexpr int vMask = vSize - 1;

    /** Model samples the output kernel needs before it has anything to say. */
    static constexpr int ready = 5;

    /** Model samples per engine sample. 1 means the two rates agree. */
    double step = 1.0;

    /** Engine samples per model sample - the reciprocal, kept because it is what
        the conversion advances by and a division per model sample is not free. */
    double spacing = 1.0;

    /** True when the two rates differ and the conversion is actually running. */
    bool active = false;

    /** What one engine sample of conversion comes to: the model's answer, and the
        dry signal delayed to sit beside it. */
    struct Converted
    {
        float wet = 0.0f;
        float dry = 0.0f;
    };

    /** Prepares for a model's rate and the engine's. Called whenever either can
        have changed - a model load, a session rate change, or a change of
        oversampling factor - and never per block. Allocates nothing, so the audio
        thread may call it. */
    void prepare (double modelRate, double engineRate) noexcept
    {
        const auto known = modelRate > 1.0 && engineRate > 1.0;
        step = known ? modelRate / engineRate : 1.0;
        spacing = 1.0 / step;
        active = known && std::abs (step - 1.0) > sameRate;
        reset();
    }

    void reset() noexcept
    {
        xHistory.fill (0.0f);
        vHistory.fill (0.0f);
        xWrite = 0;
        vWrite = 0;
        engineSamples = 0;
        modelSamples = 0;
        nextModelAt = 0.0;
        newestModelAt = 0.0;
    }

    /** How far behind the sample it was handed the bridge emits, in ENGINE
        samples: the input kernel's lookahead plus the two model spacings the
        output kernel needs. Zero when the two rates agree. */
    double delay() const noexcept
    {
        return active ? static_cast<double> (lookahead) + 2.0 * spacing : 0.0;
    }

    /** One engine sample in, one out, with the model stepped as many times as
        falls due at its own rate.

        `model` is called with the model's own input sample and must return the
        model's output for it. The bridge knows nothing about what is behind it,
        which is what lets it be exercised on its own - and the caller is the one
        that owns the model's lifetime guards and its reset.

        Not const and not re-entrant: the history is the conversion's own state,
        and it belongs to the audio thread. */
    template <typename Model>
    Converted process (float x, Model&& model) noexcept
    {
        // Rates that agree are not converted at all. This is the path every
        // session whose model matches the engine takes, and it is why a model
        // loaded at the engine's own rate is bit-for-bit what it was before this
        // struct existed.
        if (! active)
            return { model (x), x };

        xWrite = (xWrite + 1) & xMask;
        xHistory[static_cast<std::size_t> (xWrite)] = x;

        const auto now = static_cast<double> (engineSamples++);

        // The model's own instants, in order. One is taken once its instant has
        // arrived AND its kernel is fully behind the newest engine sample, which
        // is the two samples `lookahead` names.
        while (nextModelAt <= now - static_cast<double> (lookahead))
        {
            vWrite = (vWrite + 1) & vMask;
            vHistory[static_cast<std::size_t> (vWrite)] = model (xAtAge (now - nextModelAt));

            newestModelAt = nextModelAt;
            modelSamples = juce::jmin (modelSamples + 1, vSize);
            nextModelAt += spacing;
        }

        // Everything the bridge emits is this far behind the sample it was
        // handed, the dry leg included: a crossfade between a converted wet leg
        // and an unconverted dry one would be two signals a fraction of a
        // millisecond apart, which is a comb rather than a blend.
        const auto outputAge = delay();
        const auto dry = xAtAge (outputAge);

        // Until the model has answered enough times for the output kernel's
        // window there is nothing to reconstruct, and what the bridge owes is the
        // delayed dry signal - a pass-through with the right phase, so the model
        // fades in rather than arriving as a step.
        if (modelSamples < ready)
            return { dry, dry };

        // The model's output, reconstructed at the engine's own instants. The
        // taps are the model's grid, which is uniform, so the point is an offset
        // in MODEL samples rather than a position in the engine's - and that
        // offset is bounded by the two model spacings the output delay carries,
        // so the taps always sit inside the (much shorter) model-side history.
        const auto f = juce::jlimit (1.0, static_cast<double> (vSize - 3),
                                     (newestModelAt - (now - outputAge)) * step);
        const auto tap = static_cast<int> (std::floor (f));
        const auto fraction = static_cast<float> (f - static_cast<double> (tap));

        return { cubic (vAt (tap - 1), vAt (tap), vAt (tap + 1), vAt (tap + 2), fraction),
                 dry };
    }

private:
    /** The cubic through four samples at the fraction `u` past the second of
        them - Catmull-Rom, which is the four-point interpolator whose slope at
        each sample is the one the samples either side imply, and the reason the
        conversion is smooth rather than piecewise linear. */
    static float cubic (float y0, float y1, float y2, float y3, float u) noexcept
    {
        return 0.5f * (2.0f * y1
                       + (-y0 + y2) * u
                       + (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3) * u * u
                       + (-y0 + 3.0f * y1 - 3.0f * y2 + y3) * u * u * u);
    }

    /** The engine signal `age` samples ago - the newest sample is age 0 - 
        interpolated between the four samples that bracket it. The whole of the
        bridge's delay arithmetic is in the ages its callers ask for, all of which
        are inside the history by construction (see `xSize`). */
    float xAtAge (double age) const noexcept
    {
        const auto whole = static_cast<int> (std::floor (age));
        const auto fraction = static_cast<float> (age - static_cast<double> (whole));
        return cubic (xAt (whole - 1), xAt (whole),
                      xAt (whole + 1), xAt (whole + 2), fraction);
    }

    /** `age` samples ago, by ring index: the newest sample is age 0, and the
        buffer is its own ring so a push costs one store rather than a shift. */
    float xAt (int age) const noexcept
    {
        return xHistory[static_cast<std::size_t> ((xWrite - age) & xMask)];
    }

    float vAt (int age) const noexcept
    {
        return vHistory[static_cast<std::size_t> ((vWrite - age) & vMask)];
    }

    std::array<float, xSize> xHistory {};
    std::array<float, vSize> vHistory {};
    int xWrite = 0;
    int vWrite = 0;
    long long engineSamples = 0;    ///< engine samples handed in so far
    int modelSamples = 0;           ///< model samples taken so far, saturated at vSize
    double nextModelAt = 0.0;       ///< engine instant of the next model sample
    double newestModelAt = 0.0;     ///< ... and of the one taken most recently
};
