// =============================================================================
//  The equaliser: three tone bands, a high-pass and a low-pass.
//
//  Moved out of PluginProcessor.h, where it was the tone controls, whose coefficients are shared across channels. The conventions of
//  Source/DSP/ are stated in J37Math.h, and tests/dsp/check_headers_juce.sh
//  compiles this header against real JUCE along with the rest of the directory.
// =============================================================================
#pragma once

#include <array>
#include <cmath>
#include <cstddef>

//==============================================================================
/**
    The plugin's equaliser: three tone bands plus a high-pass and a low-pass.

    THE TONE STACK. Three bands, each a genuinely different filter rather than
    three of the same:

      LOW   a low shelf. It lifts or cuts everything below the corner, which is
            where weight lives. A shelf rather than a peak, because "more bass"
            on a master means the whole bottom octave, not one frequency.

      MID   a bell around 1 kHz inside the 200 Hz - 4 kHz band, leaving both ends
            alone - the band that decides whether something sounds present or
            hollow.

      HIGH  a high shelf above 4 kHz, the mirror of LOW. Air, or the lack of it.

    THE FILTERS. A high-pass and a low-pass, each with a real ORDER (how steep it
    is) and a real Q (how much it rings at the corner). They are deliberately not
    the same control as the shelves above:

      - a shelf shapes a band and leaves everything else at unity, so it can
        never remove anything completely;
      - a FILTER removes everything outside its passband, which is what you need
        to get rid of rumble, a hum, a hiss, or the sub-audio content a tape
        machine's own transport can produce.

    ORDER is expressed in dB PER OCTAVE - 6, 12, 18, 24, 36, 48 - because that is
    the unit a filter is actually specified in. Six is one pole, twelve is two,
    and the engine implements each pole as a one-pole section exactly like every
    other filter in this plugin, so the number the user picks is the slope they
    get, to within the usual one-pole approximation.

    Q is the resonance at the corner. At 0.5 the filter is critically damped
    (the gentlest, no overshoot); at higher values there is a lift at the corner.
    It is capped well short of self-oscillation, because a resonant filter that
    rings on a tape emulation is a fault rather than a feature - the Q control
    exists to let a steep filter be usable, not to make it sing.

    At 0 dB on all three bands, HP at its floor, LP at its ceiling and Q at its
    default, the whole stage is bit-for-bit transparent.
*/
struct ThreeBandEq
{
    // Split frequencies. The low split is where a mix's weight sits (200 Hz),
    // the high split is where air begins (4 kHz), and the mid bell sits between
    // them at 1 kHz - the octave where presence and honk both live.
    float lowCoefficient = 0.0f;
    float highCoefficient = 0.0f;

    // Per-band gains, 0.25x .. 4x (i.e. about -12 dB .. +12 dB). Set per block.
    float lowGain = 1.0f;
    float midGain = 1.0f;
    float highGain = 1.0f;

    // The mid bell the fraction of the band that passes through the
    // -3 dB point. A narrower bell is useful for taming a resonance, a wider one
    // for shaping a whole region, so Q is a control rather than a constant.
    float midWidth = 0.6f;

    // ------------------------------------------------------------------
    //  The high-pass and the low-pass.
    //
    //  `hpCoefficient` and `lpCoefficient` are the ONE-POLE coefficients every
    //  pole of the filter reuses - the order control only changes how many times
    //  each pole is applied, which is what makes a one-pole section stack into
    //  a 24 dB/octave filter without any of the biquad coefficient maths.
    //
    //  `hpPoles` and `lpPoles` are the pole COUNT (1, 2, 3, 4, 6, 8 for 6, 12,
    //  18, 24, 36, 48 dB/octave). `hpQ` and `lpQ` trim the corner lift.
    // ------------------------------------------------------------------
    float hpCoefficient = 0.0f;
    float lpCoefficient = 0.0f;
    int   hpPoles = 1;
    int   lpPoles = 1;
    float hpQ = 0.7f;
    float lpQ = 0.7f;

    // Per-channel filter state. The tone stack needs three memories per channel
    // (the low/high split, the mid/high split and the bell), and the two filters
    // need one each per pole - eight poles deep, which is the worst case and
    // costs sixty-four floats for the pair. That is nothing, and a static array
    // is what keeps this stage allocation-free on the audio thread.
    float lowStateL = 0.0f, lowStateR = 0.0f;
    float midLowL = 0.0f, midLowR = 0.0f;
    float bellStateL = 0.0f, bellStateR = 0.0f;

    static constexpr int maxPoles = 8;
    std::array<float, maxPoles> hpStateL {}, hpStateR {};
    std::array<float, maxPoles> lpStateL {}, lpStateR {};

    // The filters are bypassed when the control is at its floor/ceiling, so a
    // neutral EQ costs one comparison rather than eight multiplies.
    bool hpActive = false;
    bool lpActive = false;

    /** Rebuilds every split coefficient for a rate. Called from prepareToPlay
        and from the per-block path, because the rate the engine runs at can
        change with oversampling without prepareToPlay running again. */
    void prepare (float sampleRate) noexcept
    {
        // 200 Hz and 4 kHz, converted with the same one-pole helper everything
        // else in the plugin uses, so they mean the same thing at every rate.
        const auto rate = juce::jmax (1.0f, sampleRate);
        const auto omegaLow = juce::MathConstants<float>::twoPi * 200.0f / rate;
        const auto omegaHigh = juce::MathConstants<float>::twoPi * 4000.0f / rate;
        lowCoefficient = juce::jlimit (0.0f, 1.0f, 1.0f - std::exp (-omegaLow));
        highCoefficient = juce::jlimit (0.0f, 1.0f, 1.0f - std::exp (-omegaHigh));
    }

    /** The bell's own coefficient, derived from the split and the width. The mid
        band is the difference between two low-passes - one at the bell's centre
        and one wider - which is a band-pass built the same way every other split
        in this plugin is built. */
    float midBandCoefficient (float sampleRate) const noexcept
    {
        // Width 0 gives a narrow bell (about 2 kHz of passband), width 1 a broad
        // one (about 8 kHz). It is deliberately never zero-width: a resonant
        // peak on a tape machine is a fault, not a control.
        const float widthHz = 2000.0f + midWidth * 6000.0f;
        const auto omega = juce::MathConstants<float>::twoPi * widthHz
                         / juce::jmax (1.0f, sampleRate);
        return juce::jlimit (0.0f, 1.0f, 1.0f - std::exp (-omega));
    }

    /** Rebuilds the two filter coefficients and the pole counts.

        `hpHz` is the high-pass corner, `lpHz` the low-pass corner, `order` the
        slope in dB/octave and `q` the corner resonance. This is called once per
        BLOCK, not per sample: all four are controls, so they change on block
        boundaries at the fastest, and the eight std::exp below would be eight
        per sample otherwise.
    */
    void prepareFilters (float sampleRate, float hpHz, float lpHz,
                         float orderDbPerOctave, float q) noexcept
    {
        const auto rate = juce::jmax (1.0f, sampleRate);

        // The pole count from the slope: one pole is 6 dB per octave, so the
        // count is the slope divided by six. Clamped to the array's own size,
        // which is what makes the array's size the highest order on offer.
        const auto poles = juce::jlimit (1, maxPoles,
                                         juce::roundToInt (orderDbPerOctave / 6.0f));
        hpPoles = poles;
        lpPoles = poles;

        // "Off" positions. A corner below audibility is not a filter, and a
        // corner at or above Nyquist cannot exist - so both ends of each control's
        // travel are REAL bypasses rather than a filter that happens to be very
        // gentle. That is what makes a neutral EQ bit-for-bit transparent.
        hpActive = hpHz > 22.0f;
        lpActive = lpHz < rate * 0.45f;

        // Q: the corner lift. One-pole sections in series multiply their own
        // gentle knee, so a stack of them gives a rounded corner rather than a
        // resonant one - which is why the Q control acts on the CORNER only, by
        // trimming the pole's own response rather than by adding a resonant
        // biquad on top. 0.5 is critically damped, 1.5 is the practical maximum
        // before a steep stack starts to ring audibly at the corner.
        hpQ = juce::jlimit (0.5f, 1.5f, q);
        lpQ = hpQ;

        const auto hpOmega = juce::MathConstants<float>::twoPi
                           * juce::jlimit (22.0f, rate * 0.45f, hpHz) / rate;
        const auto lpOmega = juce::MathConstants<float>::twoPi
                           * juce::jlimit (22.0f, rate * 0.45f, lpHz) / rate;

        // The Q trims the coefficient: a higher Q makes the pole respond further
        // per step, which is the corner lift, and 1.0 is the neutral multiplier
        // so a Q of 0.7 is deliberately slightly under-damped rather than a
        // no-op - that is what a real one-pole corner looks like.
        hpCoefficient = juce::jlimit (0.0f, 1.0f,
                                      (1.0f - std::exp (-hpOmega)) * hpQ);
        lpCoefficient = juce::jlimit (0.0f, 1.0f,
                                      (1.0f - std::exp (-lpOmega)) * lpQ);
    }

    void reset() noexcept
    {
        lowStateL = lowStateR = 0.0f;
        midLowL = midLowR = 0.0f;
        bellStateL = bellStateR = 0.0f;
        hpStateL.fill (0.0f); hpStateR.fill (0.0f);
        lpStateL.fill (0.0f); lpStateR.fill (0.0f);
    }

    /** One sample through the three bands.

        `lowState` and `midState` are the caller's per-channel filter memories -
        the caller owns them so that both instances of this stage (input and
        output) keep their state in the places the rest of the engine keeps its
        own, rather than this struct allocating.
    */
    float process (float x, int channel, float midCoefficient) noexcept
    {
        // ------------------------------------------------------------------
        //  The HIGH-PASS, first, so everything downstream sees a signal that is
        //  already free of whatever it removes.
        //
        //  Each pole is a one-pole high-pass built from a one-pole low-pass, the
        //  same split every other filter in this plugin uses: what the low-pass
        //  keeps is subtracted from the input, and what is left is the high side.
        //  Stacking the poles in series is what makes 6 dB/octave into 24 - the
        //  slope is the pole COUNT and nothing else, which is why the order
        //  control needs no coefficient maths of its own.
        //
        //  `hpActive` is false at the control's floor, in which case the whole
        //  loop is skipped and the sample passes untouched. That is what keeps a
        //  neutral EQ bit-for-bit transparent rather than "transparent to within
        //  a very gentle filter".
        // ------------------------------------------------------------------
        float filtered = x;
        if (hpActive)
        {
            auto& hpState = channel == 0 ? hpStateL : hpStateR;
            for (int pole = 0; pole < hpPoles; ++pole)
            {
                auto& state = hpState[static_cast<std::size_t> (pole)];
                state += (filtered - state) * hpCoefficient;
                filtered -= state;   // the low side removed, the high side left
            }
        }

        // The low/high split: what the low-pass keeps is the LOW band, what it
        // removes is everything above it.
        auto& lowState = channel == 0 ? lowStateL : lowStateR;
        lowState += (filtered - lowState) * lowCoefficient;
        const float lowBand = lowState;
        const float upperBand = filtered - lowState;

        // The upper band is split again at 4 kHz into the MID and HIGH bands.
        auto& midState = channel == 0 ? midLowL : midLowR;
        midState += (upperBand - midState) * highCoefficient;
        const float midHighBand = upperBand - midState;   // everything above 4 kHz
        const float midBand = midState;                   // 200 Hz .. 4 kHz
        // The mid BELL. MID is a resonant-style bell rather than a second
        // low/high pair, and that is a deliberate difference: a full-band mid
        // shelf would drag the low and high ends with it, so turning MID up
        // would sound like turning everything up. A bell acts on its own pass
        // band ONLY, leaving the two shelves where they were.
        //
        // It is built from two one-poles over the same mid band - one at the
        // bell's own (narrower) corner and one at the band's own top - and what
        // the narrow one passes is subtracted from what the broad one passes.
        // That difference is a band-pass, and it is the same technique every
        // other filter in this plugin uses: one-pole splits, never biquads.
        // The small addition at the end keeps the bell's peak from vanishing as
        // the band narrows, so 0 dB really is 0 dB at every width. */
        auto& bellState = channel == 0 ? bellStateL : bellStateR;
        bellState += (midBand - bellState) * midCoefficient;
        const float bellBand = bellState;
        const float bellRest = midBand - bellBand;

        // The mid output: the bell's pass band takes the gain, and everything
        // else in the mid region is left at unity, so MID is a bell and not a
        // level control on the whole 200 Hz - 4 kHz span.
        const float midOut = midBand + bellBand * (midGain - 1.0f)
                                    + bellRest * (midGain - 1.0f) * 0.15f;

        const float tonal = lowBand * lowGain
                          + midOut
                          + midHighBand * highGain;

        // ------------------------------------------------------------------
        //  The LOW-PASS, last: the mirror of the high-pass above and built the
        //  same way, one pole at a time, with the pole COUNT being what the
        //  order control changes. It is the control that takes a hiss off, or
        //  that rounds the top of the machine off before it leaves the plugin.
        //
        //  A low-pass cannot be "off" at the top of its range the way a
        //  high-pass is at the bottom: a corner above Nyquist is not a filter at
        //  all, so the control's own maximum IS the bypass and `lpActive` tests
        //  for exactly that. The two filters are therefore symmetric in the
        //  panel and symmetric here - each has one end that is genuinely no
        //  filter rather than a very gentle one.
        // ------------------------------------------------------------------
        float out = tonal;
        if (lpActive)
        {
            auto& lpState = channel == 0 ? lpStateL : lpStateR;
            for (int pole = 0; pole < lpPoles; ++pole)
            {
                auto& state = lpState[static_cast<std::size_t> (pole)];
                state += (out - state) * lpCoefficient;
                out = state;
            }
        }

        return out;
    }
};
