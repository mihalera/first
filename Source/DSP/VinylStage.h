// =============================================================================
//  The vinyl stage: the record's surface, its rumble and the RIAA curve.
//
//  Included by tests/dsp/run.sh's harness through extract.py's SOURCE_HEADERS, and
//  instantiated by tests/dsp/check_headers_juce.sh against real JUCE, rather than
//  cut out of PluginProcessor.h by text. The conventions of Source/DSP/ are stated
//  in J37Math.h.
// =============================================================================
#pragma once

#include <cmath>
#include <cstdint>

//==============================================================================
/**
    The vinyl stage: surface noise, rumble and the RIAA playback curve.

    These three are what "vinyl" means as a sound, and none of them is a tape
    effect:

      CRACKLE - impulse noise. A record surface is not hiss: it is ticks, caused by
                dust and by the stylus crossing the groove's imperfections. So the
                generator produces SPARSE IMPULSES rather than continuous noise,
                which is the difference between a record and a noisy tape.
      RUMBLE  - a low-frequency thump from the turntable's bearing and motor,
                which is why vinyl has a bottom-end floor that a CD does not.
      WARMTH  - the RIAA playback curve. The curve itself is a strong low-frequency
                roll-off with a high-frequency boost (it is the inverse of the
                cutting curve), and a playback stage that is not perfectly
                complementary leaves the characteristic low-end lift and top-end
                softness. That is the "warm" part, and it is a filter, not a colour.

    Placed at the very end of the chain, after the protection stages, because a
    turntable is the last thing in the signal path.
*/
struct VinylStage
{
    // The voice the VINYL TYPE switch sets. RIAA is the cutting standard's own
    // tilt, so it belongs to the DISC, not to the playback chain's error - a 78
    // was not cut to the RIAA curve at all, and a half-speed master has more of
    // it because the lathe tracked properly.
    float riaaAmount = 0.45f;
    float rumbleAmount = 1.0f;       // multiplier on the caller's rumble level
    float crackleAmount = 1.0f;      // multiplier on the caller's crackle level
    float crackleDecay = 0.9985f;    // per-sample: how long a tick rings
    float surfaceNoise = 0.0f;       // groove hiss between the ticks
    float speedModulation = 0.0f;    // vinyl SPEED: wow/flutter depth on the stage

    // ------------------------------------------------------------------
    //  The four physical faults of a record and a turntable.
    //
    //  CRACKLE is the needle finding a scratch; these four are the rest of
    //  what goes wrong when a record is played. Each is a genuinely different
    //  mechanism rather than another amount of noise:
    //
    //    DUST       - fine particulate in the groove. A continuous, granular
    //                 high-frequency texture that follows the groove's own
    //                 modulation (a louder passage sounds dirtier) and lifts
    //                 off whenever the stylus has signal to grind through.
    //    SCRATCH    - a deep groove wound, not a dust tick. It comes back once
    //                 per revolution, so it is PERIODIC: a short burst of
    //                 noise at a fixed rate derived from the platter speed.
    //    WARP       - the record is not flat. The vertical warp makes the
    //                 stylus ride up and down once per turn, so the LEVEL (and
    //                 slightly the pitch) breathes at the platter rate. It is
    //                 the slow, cyclic throb a warped record has.
    //    ELECTRICAL - the cartridge, the cable and the earth loop. Mains hum
    //                 at the supply frequency plus its harmonics, and the
    //                 broadband static that comes with a bad earth.
    //
    //  All four are set per block from the controls and read only in process().
    // ------------------------------------------------------------------
    float dustAmount = 0.0f;         // 0..1 fine particulate in the groove
    float scratchAmount = 0.0f;      // 0..1 deep periodic groove damage
    float warpAmount = 0.0f;         // 0..1 vertical warp, level breathing
    float electricalAmount = 0.0f;   // 0..1 mains hum + earth static

    // ------------------------------------------------------------------
    //  CLICKS - the sharp, discrete groove faults: the cut, not the surface.
    //
    //  CRACKLE is the fine surface texture and DUST is the grit in the groove;
    //  CLICKS is the third and loudest class of record damage: an actual ridge
    //  or pit in the lacquer, or a stamping fault, that the stylus hits as a
    //  single hard transient. Where a crackle tick is a few milliseconds of
    //  noise, a click is a fast BIPOLAR impulse - a sharp full-bandwidth spike
    //  with almost no ringing - because it is a mechanical impact rather than
    //  a contact noise. That is why it reads as "a click" and not as "more
    //  crackle", and why it is a control of its own.
    //
    //  A second, lower-state effect rides with it and is what makes a badly
    //  pressed record sound BROKEN rather than merely dirty: the same fault
    //  repeats because the stylus crosses it once per revolution, so a fraction
    //  of the clicks line up with the platter instead of falling at random.
    // ------------------------------------------------------------------
    float clickAmount = 0.0f;        // 0..1 sharp groove faults
    float clickEnvelopeL = 0.0f;
    float clickEnvelopeR = 0.0f;
    float clickPhaseL = 0.0f;
    float clickPhaseR = 0.0f;
    float clickIncrement = 0.0f;     // one periodic click per revolution

    // ------------------------------------------------------------------
    //  GENERATION, TURNTABLE and CARTRIDGE - the three stages of getting a
    //  record to play, and each one changes the sound in its own way.
    //
    //  They are the same idea as TAPE TYPE and VINYL TYPE, one level up: the
    //  record TYPE says what was pressed, these say what was CUT, what it is
    //  played ON, and what is reading it.
    //
    //    GENERATION  how the lacquer was cut. A modern direct-metal master has
    //                almost none of the cutting-head's own colour and plays
    //                loud and clean; a vintage lacquer master is darker, its
    //                high end rolled off by the cutter head's own limits; a
    //                PRINTED pressing is a copy of a copy, so it carries the
    //                accumulated loss of every generation before it - duller
    //                still, noisier, and with a little extra surface character.
    //                ("PRINTED" is the pressing made from a stamper, as opposed
    //                to the reference lacquer the engineer hears.)
    //
    //    TURNTABLE   what drives the platter. A high-torque direct-drive DJ deck
    //                holds a rock-steady 33 and stops in a quarter turn; a
    //                belt-drive audiophile deck has a slow, elastic belt, so it
    //                is marginally less steady but smoother, and takes a moment
    //                to settle. The DJ deck is what chasing and scratching needs;
    //                the belt deck is what a quiet pressing wants.
    //
    //    CARTRIDGE   what reads the groove, and the largest single difference of
    //                the three. A moving-MAGNET cartridge has a warm, slightly
    //                soft top and a broad, gentle response; a moving-COIL has
    //                more detail and a brighter, tighter top with a lower output
    //                (so more gain, and more hiss); a DJ CONCORDE-style cart is
    //                heavier and hotter, tracks harder, and carries a bit more
    //                surface noise with it.
    //
    //  The three are INDEPENDENT: each sets coefficients of its own, and the
    //  engine applies whichever combination is selected. A funky modern cut on a
    //  belt-drive with a moving coil is a different machine from a vintage
    //  printed pressing on a DJ deck, and both are reachable.
    // ------------------------------------------------------------------
    float generationTiltL = 0.0f;   // per-channel cutting-generation tilt state
    float generationTiltR = 0.0f;
    float generationTopLoss = 1.0f; // >0, multiplier on the top band
    float generationBottomLift = 1.0f; // >0, multiplier on the bottom band
    float generationNoiseScale = 1.0f; // multiplier on this stage's own noise

    float turntableWowScale = 1.0f;    // multiplier on the speed wander
    float turntableStability = 1.0f;   // flutter irregularity

    float cartridgeTopGain = 1.0f;     // >0, top-band gain of the reading
    float cartridgeBottomGain = 1.0f;  // >0, bottom-band gain
    float cartridgeNoiseGain = 1.0f;   // multiplier on crackle/dust/click
    float cartridgeHissGain = 1.0f;    // multiplier on groove hiss
    float cartridgeCoefficient = 0.0f; // the split the two gains act around

    // Mains hum's frequency and the per-revolution rates are given as
    // INCREMENTS (fraction of a cycle per sample) so the stage is rate-agnostic
    // for the same reason every other increment here is: the caller converts a
    // frequency in Hz using the engine rate, and the phase maths below is then
    // one multiply and one wrap.
    float humIncrement = 0.0f;       // mains hum, usually 50 or 60 Hz
    float humIncrement2 = 0.0f;      // its second harmonic (100 / 120 Hz)
    float scratchIncrement = 0.0f;   // one scratch burst per platter revolution
    float warpIncrement = 0.0f;      // one warp cycle per platter revolution

    // Rumble: a low-frequency one-pole, per channel.
    float rumbleL = 0.0f;
    float rumbleR = 0.0f;

    // The RIAA-ish playback tilt, per channel.
    float warmthLowL = 0.0f;
    float warmthLowR = 0.0f;

    // Crackle: a short decaying envelope per impulse, per channel.
    float crackleEnvelopeL = 0.0f;
    float crackleEnvelopeR = 0.0f;

    // DUST: the granular high-frequency texture's own one-pole, per channel -
    // its bandwidth is set by the caller so it stays the same at every rate.
    float dustLowL = 0.0f;
    float dustLowR = 0.0f;
    float dustCoefficient = 0.0f;

    // SCRATCH and WARP phases, per channel. The scratch phase drives the burst
    // gate; the warp phase drives the level breath. They run independently of
    // the speed-wander phase above, because a warp and a speed error are two
    // different faults that happen to share a rotation rate.
    float scratchPhaseL = 0.0f;
    float scratchPhaseR = 0.0f;
    float warpPhaseL = 0.0f;
    float warpPhaseR = 0.0f;

    // ELECTRICAL: the hum's own two phases (fundamental and second harmonic),
    // per channel, so a cartridge wired with the two sides out of balance hums
    // differently on each - which is exactly how a real earth loop behaves.
    float humPhaseL = 0.0f;
    float humPhaseR = 0.0f;
    float humHarmonicPhaseL = 0.0f;
    float humHarmonicPhaseR = 0.0f;

    // The disc's own wow phase, per channel. The increment is set per block from
    // the speed the VINYL SPEED control selects, exactly as the transport's phases
    // are - a 33 RPM disc wanders at a different rate than a 45.
    float vinylWowPhaseL = 0.0f;
    float vinylWowPhaseR = 0.0f;
    float vinylWowIncrement = 0.02f;

    void reset() noexcept
    {
        rumbleL = rumbleR = 0.0f;
        warmthLowL = warmthLowR = 0.0f;
        crackleEnvelopeL = crackleEnvelopeR = 0.0f;
        vinylWowPhaseL = vinylWowPhaseR = 0.0f;
        dustLowL = dustLowR = 0.0f;
        scratchPhaseL = scratchPhaseR = 0.0f;
        warpPhaseL = warpPhaseR = 0.0f;
        humPhaseL = humPhaseR = 0.0f;
        humHarmonicPhaseL = humHarmonicPhaseR = 0.0f;
        clickEnvelopeL = clickEnvelopeR = 0.0f;
        clickPhaseL = clickPhaseR = 0.0f;
        generationTiltL = generationTiltR = 0.0f;
    }

    /**
        One sample of surface noise and colour.

        `crackleAmount` and `rumbleAmount` are 0..1 and scale the two noise
        sources independently; `warmth` is 0..1 and sets how much of the RIAA
        playback character is applied. `random` is the caller's noise source.
    */
    float process (float x, int channel,
                   float crackleControl, float rumbleControl, float warmth,
                   float rumbleCoefficient, float warmthCoefficient,
                   std::uint32_t& random) noexcept
    {
        // -- CRACKLE: sparse impulses, not continuous noise ---------------------
        // A tick happens when the stylus hits something, so it is an impulse with a
        // fast decay rather than a steady hiss. The probability per sample is low
        // and the amplitude is high, which is what makes it read as a record.
        auto& crackleEnvelope = channel == 0 ? crackleEnvelopeL : crackleEnvelopeR;

        random = random * 1664525u + 1013904223u;
        const float unit = static_cast<float> ((random >> 8) & 0x00ffffffu)
                         * (1.0f / 8388608.0f) - 1.0f;

        // Roughly one impulse every few thousand samples at full crackle, so the
        // ticks are distinct events rather than a buzz.
        const float probability = 0.00008f * crackleAmount;
        if (std::abs (unit) < probability)
        {
            // The impulse's own amplitude, also random, so the ticks are not all
            // the same size.
            random = random * 1664525u + 1013904223u;
            const float amplitude = static_cast<float> ((random >> 8) & 0x00ffffffu)
                                  * (1.0f / 8388608.0f) - 1.0f;
            crackleEnvelope += amplitude * 0.35f;
        }

        // A tick is over in a millisecond or two, unless the type says otherwise -
        // a worn shellac rings longer than a fresh half-speed master.
        crackleEnvelope *= crackleDecay;

        // -- RUMBLE: the turntable's low-frequency floor ------------------------
        auto& rumble = channel == 0 ? rumbleL : rumbleR;
        random = random * 1664525u + 1013904223u;
        const float rumbleNoise = static_cast<float> ((random >> 8) & 0x00ffffffu)
                                * (1.0f / 8388608.0f) - 1.0f;
        // A one-pole low-pass on white noise gives the thump; the coefficient is
        // built by the caller so it is the same at every rate. The type's own
        // rumble multiplier sits on top of the control.
        rumble += (rumbleNoise * rumbleControl * rumbleAmount * rumbleAmount * 0.020f - rumble) * rumbleCoefficient;

        // Groove surface hiss - continuous, unlike the ticks. A shellac 78 has
        // real surface between every transient; a half-speed master almost none.
        float groove = 0.0f;
        if (surfaceNoise > 0.0f)
        {
            random = random * 1664525u + 1013904223u;
            const float hiss = static_cast<float> ((random >> 8) & 0x00ffffffu)
                             * (1.0f / 8388608.0f) - 1.0f;
            groove = hiss * surfaceNoise * 0.0080f;
        }

        // ------------------------------------------------------------------
        //  DUST: fine particulate in the groove.
        //
        //  Unlike the ticks above, dust is a CONTINUOUS granular texture - the
        //  sound of the stylus grinding through fine grit. Two things make it
        //  read as dust rather than as another hiss:
        //
        //    - it is band-limited HIGH (a one-pole above the programme's own
        //      top end), so it sits on top of the music as grit;
        //    - it is scaled by the programme's own level, so a loud passage
        //      sounds dirtier than a quiet one. Real dust only makes a sound
        //      when there is modulation in the groove to disturb it.
        // ------------------------------------------------------------------
        float dust = 0.0f;
        if (dustAmount > 0.0f)
        {
            auto& dustLow = channel == 0 ? dustLowL : dustLowR;
            random = random * 1664525u + 1013904223u;
            const float grit = static_cast<float> ((random >> 8) & 0x00ffffffu)
                             * (1.0f / 8388608.0f) - 1.0f;

            // The high-pass is the complement of the one-pole: what the filter
            // removes is what is left.
            dustLow += (grit - dustLow) * dustCoefficient;
            const float highGrit = grit - dustLow;

            // The programme gating: `x` is the signal entering the stage, so a
            // quiet groove stays quiet even under a lot of dust.
            const float grooveEnergy = juce::jlimit (0.0f, 1.0f, std::abs (x) * 3.0f);
            dust = highGrit * dustAmount * dustAmount * 0.020f
                 * (0.25f + 0.75f * grooveEnergy);
        }

        // ------------------------------------------------------------------
        //  SCRATCH: a deep groove wound, once per revolution.
        //
        //  Where a dust tick is random, a SCRATCH is PERIODIC: the stylus crosses
        //  the same wound every turn, so the damage arrives at the rotation rate
        //  and is heard as a repeating thud rather than as a hiss. The phase
        //  wraps once per platter revolution, and the burst fires over a short
        //  window at the wrap, so the fault is a distinct event in time.
        // ------------------------------------------------------------------
        float scratch = 0.0f;
        if (scratchAmount > 0.0f && scratchIncrement > 0.0f)
        {
            float& phase = channel == 0 ? scratchPhaseL : scratchPhaseR;
            phase += scratchIncrement;
            if (phase >= 1.0f)
                phase -= 1.0f;

            // The burst window: the first few percent of the revolution. A short
            // raised-cosine so the event fades in and out instead of clicking -
            // a step at either end of the window would be a glitch of its own.
            constexpr float burstWindow = 0.04f;
            if (phase < burstWindow)
            {
                const float burstProgress = phase / burstWindow;
                const float window = 0.5f - 0.5f * std::cos (burstProgress * 6.2831853f);

                random = random * 1664525u + 1013904223u;
                const float wound = static_cast<float> ((random >> 8) & 0x00ffffffu)
                                  * (1.0f / 8388608.0f) - 1.0f;

                scratch = wound * scratchAmount * scratchAmount * 0.10f * window;
            }
        }

        // ------------------------------------------------------------------
        //  CLICKS: the sharp, discrete groove faults.
        //
        //  The third and loudest class of record damage. A crackle tick is a
        //  short burst of contact noise; a CLICK is a mechanical IMPACT - the
        //  stylus dropping off a ridge or hitting a pit - so it is a fast,
        //  bipolar, full-bandwidth spike with almost no ring. Two things make it
        //  read as a click rather than as more crackle:
        //
        //    - the impulse is shaped as an alternating pair of samples (a i, -i),
        //      which is the shortest transient a sampled signal can carry and
        //      therefore the brightest thing the stage can produce;
        //    - a fraction of the clicks is PERIODIC, locked to the platter, so a
        //      bad pressing ticks in time rather than at random - the same
        //      wound crossed once per revolution.
        // ------------------------------------------------------------------
        float click = 0.0f;
        if (clickAmount > 0.0f)
        {
            auto& clickEnvelope = channel == 0 ? clickEnvelopeL : clickEnvelopeR;

            random = random * 1664525u + 1013904223u;
            const float clickDraw = static_cast<float> ((random >> 8) & 0x00ffffffu)
                                  * (1.0f / 8388608.0f) - 1.0f;

            // The random half of the clicks: roughly one every few thousand
            // samples at full CLICKS, which is a stream of distinct events.
            const float randomProbability = 0.00012f * clickAmount;

            // The periodic half: one click per revolution, fired through a very
            // short window so it is a discrete impact and not a rumble. Only the
            // part of the CLICKS control ABOVE half the travel brings the
            // periodic behaviour in, so the low half of the knob is a clean
            // random-click stream and the top half is a record that ticks in time.
            bool periodicClick = false;
            if (clickIncrement > 0.0f && clickAmount > 0.5f)
            {
                float& phase = channel == 0 ? clickPhaseL : clickPhaseR;
                phase += clickIncrement;
                if (phase >= 1.0f)
                    phase -= 1.0f;

                constexpr float clickWindow = 0.004f;
                if (phase < clickWindow)
                    periodicClick = true;
            }

            if (std::abs (clickDraw) < randomProbability || periodicClick)
            {
                random = random * 1664525u + 1013904223u;
                const float amplitude = static_cast<float> ((random >> 8) & 0x00ffffffu)
                                      * (1.0f / 8388608.0f) - 1.0f;

                // A click is an IMPACT, so it is much louder than a tick - and it
                // is bipolar, alternating sign between events, which is what a
                // mechanical impact produces when the stylus is knocked both ways.
                const float sign = clickDraw < 0.0f ? -1.0f : 1.0f;
                clickEnvelope += amplitude * sign * 0.55f;
            }

            // The decay is FAST - a couple of samples - because a click has no
            // ring. This is the other half of what separates it from the crackle
            // envelope above, which is deliberately slow enough to ring.
            clickEnvelope *= 0.62f;

            click = clickEnvelope * clickAmount * clickAmount;
        }

        // ------------------------------------------------------------------
        //  WARP: the record is not flat.
        //
        //  A warped record makes the stylus ride up and down once per turn, so
        //  the tracking force - and therefore the output level - breathes at the
        //  platter rate. It is a slow, cyclic throb, and it is what makes a
        //  warped record sound like it is struggling rather than merely noisy.
        //
        //  It modulates what the stage PASSES rather than what it adds, so at
        //  WARP 0 it is exactly unity and the stage is untouched.
        // ------------------------------------------------------------------
        float warp = 1.0f;
        if (warpAmount > 0.0f && warpIncrement > 0.0f)
        {
            float& phase = channel == 0 ? warpPhaseL : warpPhaseR;
            phase += warpIncrement;
            if (phase >= 1.0f)
                phase -= 1.0f;

            // Depth is deliberately small: a warp is a wobble in level, not a
            // tremolo. At 100 % it is about +-1.5 dB, which is enough to hear
            // the record struggling without becoming an effect.
            warp = 1.0f + std::sin (phase * 6.2831853f) * warpAmount * 0.175f;
        }

        // ------------------------------------------------------------------
        //  ELECTRICAL: the cartridge, the cable and the earth loop.
        //
        //  Two separate faults, and both are what "electrical" means on a
        //  turntable:
        //
        //    - MAINS HUM at the supply frequency plus its second harmonic,
        //      which is what an unearthed cartridge picks up from the motor and
        //      the transformer;
        //    - EARTH STATIC, the broadband crackle of a bad ground, continuous
        //      and independent of the groove.
        //
        //  The hum's two components have independent phases per channel, so a
        //  cartridge whose two sides pick the field up differently hums
        //  asymmetrically - which is exactly how a real earth loop behaves.
        // ------------------------------------------------------------------
        float electrical = 0.0f;
        if (electricalAmount > 0.0f)
        {
            // Two phases per channel: the fundamental and the second harmonic.
            // Both are advanced by their own increment, set by the caller from the
            // ONE supply frequency, so the 2:1 relationship between them is
            // established in the voicing block and never re-derived here.
            float& humPhase = channel == 0 ? humPhaseL : humPhaseR;
            float& harmonicPhase = channel == 0 ? humHarmonicPhaseL : humHarmonicPhaseR;
            humPhase += humIncrement;
            harmonicPhase += humIncrement2;
            if (humPhase >= 1.0f) humPhase -= 1.0f;
            if (harmonicPhase >= 1.0f) harmonicPhase -= 1.0f;

            const float fundamental = std::sin (humPhase * 6.2831853f);
            const float second = std::sin (harmonicPhase * 6.2831853f);

            // The right channel's earthing is slightly different from the left's:
            // the second harmonic is in anti-phase across the pair while the
            // fundamental is not - the classic asymmetric hum of a cartridge whose
            // two sides pick the field up differently.
            const float harmonicSign = channel == 0 ? 1.0f : -1.0f;
            const float hum = (fundamental + 0.35f * harmonicSign * second)
                            * electricalAmount * electricalAmount * 0.006f;

            random = random * 1664525u + 1013904223u;
            const float staticNoise = static_cast<float> ((random >> 8) & 0x00ffffffu)
                                    * (1.0f / 8388608.0f) - 1.0f;
            const float earthStatic = staticNoise * electricalAmount * electricalAmount * 0.004f;

            electrical = hum + earthStatic;
        }

        // -- WARMTH: the RIAA playback character --------------------------------
        // A one-pole split: the low band is lifted and the high band is left, which
        // is the low-end lift and relative top-end softness of a playback stage
        // that is not perfectly complementary to the cutting curve.
        auto& warmthLow = channel == 0 ? warmthLowL : warmthLowR;
        warmthLow += (x - warmthLow) * warmthCoefficient;
        const float warmthHigh = x - warmthLow;
        const float warmed = warmthLow * (1.0f + warmth * riaaAmount)
                           + warmthHigh * (1.0f - warmth * riaaAmount * 0.4f);

        // The disc's own speed error, as a slow amplitude wander. A record that is
        // running off speed does not merely hiss - the whole groove's output rises
        // and falls with the stylus's tracking error, which is what this term is.
        // It is folded onto the stage's output rather than onto the dry path so
        // that at VINYL 0 none of it exists.
        float speedWander = 0.0f;
        if (speedModulation > 0.0f)
        {
            float& phase = channel == 0 ? vinylWowPhaseL : vinylWowPhaseR;
            phase += vinylWowIncrement;
            if (phase >= 1.0f)
                phase -= 1.0f;
            speedWander = std::sin (phase * 6.2831853f) * speedModulation;
        }

        // ------------------------------------------------------------------
        //  GENERATION and CARTRIDGE: the cut, and the thing reading it.
        //
        //  Both are a two-band tilt around one split, but for opposite reasons,
        //  and that is why they are two controls rather than one:
        //
        //    GENERATION is a LOSS. A cut master has the cutter head's own colour
        //    and a printed pressing has every generation's loss on top of it, so
        //    the top band is what suffers and the bottom is what remains. The
        //    tilt is applied to the stage's OWN output, before the cartridge gains,
        //    because it is part of what was pressed onto the disc rather than part
        //    of what reads it.
        //
        //    CARTRIDGE is a RESHAPE. A moving coil is bright and tight, a moving
        //    magnet is soft and broad, a DJ cart is hot and heavier. That is a gain
        //    PAIR on the two bands, applied after, because it is what the reading
        //    does to a record that is already the way it is.
        //
        //  Both already default to a flat pair (gains of 1.0), so a stage whose
        //  switches sit on their neutral entries is bit-for-bit untouched.
        // ------------------------------------------------------------------
        const float generationOut = [&]
        {
            if (std::abs (generationTopLoss - 1.0f) < 1.0e-5f
                && std::abs (generationBottomLift - 1.0f) < 1.0e-5f)
                return warmed;

            auto& tilt = channel == 0 ? generationTiltL : generationTiltR;
            tilt += (warmed - tilt) * cartridgeCoefficient;
            const float bottom = tilt;
            const float top = warmed - tilt;
            return bottom * generationBottomLift + top * generationTopLoss;
        }();

        const float cartridgeOut = [&]
        {
            if (std::abs (cartridgeTopGain - 1.0f) < 1.0e-5f
                && std::abs (cartridgeBottomGain - 1.0f) < 1.0e-5f)
                return generationOut;

            // The same split the generation tilt uses, taken from the same state -
            // one filter, two users, so the two controls cannot disagree about where
            // the band boundary is.
            auto& tilt = channel == 0 ? generationTiltL : generationTiltR;
            const float bottom = tilt;
            const float top = generationOut - tilt;
            return bottom * cartridgeBottomGain + top * cartridgeTopGain;
        }();

        const float grooveNoiseGain = cartridgeNoiseGain;
        const float hissGainNow = cartridgeHissGain;

        return (cartridgeOut
                     + groove * hissGainNow
                     + crackleEnvelope * crackleControl * crackleAmount * crackleAmount * 0.6f
                         * grooveNoiseGain
                     + rumble
                     + dust * grooveNoiseGain
                     + scratch * grooveNoiseGain
                     + click * grooveNoiseGain
                     + electrical) * (1.0f + speedWander) * warp;
    }
};
