// =============================================================================
//  The transport rig: one capstan, three keys and a power cut.
//
//  Driven by tests/transport/transport_test.cpp, which includes this header rather
//  than a copy cut out of PluginProcessor.h, and instantiated by
//  tests/dsp/check_headers_juce.sh against real JUCE. The conventions of
//  Source/DSP/ are stated in J37Math.h.
// =============================================================================
#pragma once

//==============================================================================
/**
    The transport: one capstan, three keys, and a power cut.

    Both the engine and the panel read the same state, so it lives here as one
    small object rather than as a handful of floats in the processor:

      * `ramp`     - the capstan: 0 at rest, 1 at speed.
      * `spindown` - the momentary run-down: 1 while powered, 0 while held.

    `platter()` is the two multiplied, and that one number is what the machine
    follows - the level it passes, the depth of its wow and flutter, how much
    band its heads can put out (see `speedScale` at the call site), and the value
    the panel spins its reels at.

    The three states are the ones a tape deck has:

      STOP   brakes the capstan to rest and leaves it there. That is what makes a
             stopped machine SILENT rather than quiet: a deck that is not moving
             does not pass a faded copy of its input, it passes nothing.
      PLAY   normal running.
      START  the engagement gesture. The capstan comes up to speed and the state
             then advances to PLAY by itself; the engine publishes that
             transition for the message thread to hand to the host.

    START is a SPIN-UP and nothing else: the capstan is driven up from wherever
    it is - from rest, or from partway down after a coast the key has caught - and
    on a machine that is already at speed there is nothing left to drive, so the
    key settles the state straight into PLAY. It does NOT brake first. An earlier
    version "re-cued" the machine instead (a short brake, then the spin-up) to
    give the key something to do on a running machine, and that is a different
    gesture wearing START's name: a deck that stops before it starts is not what
    the key means, and the brake is the one thing it must not do.

    The gestures are the deck's OWN, so they move with the deck's settings, and
    `beginBlock()` is handed all three:

      * TAPE SPEED scales the CAPSTAN gestures - STOP's coast, START's spin-up and
        PLAY's re-lock. The tape travels further per second, so the flywheel is
        carrying more energy: it takes longer to bring up and longer to hold
        back.
      * VINYL SPEED scales the PLATTER gesture - SPINDOWN's run-down and its
        return. A 78 turns more than twice as fast as a 33 and spends the energy
        a power cut leaves it for longer.
      * the TEMPO scales everything. The deck's motions are part of the take and
        the take is at this tempo - the same argument that tempo-locks the wow
        rate and the delay - so a gesture at 240 BPM takes half the time it takes
        at 120.

    The durations below are SECONDS, and the ramps are linear in time so that they
    mean what they say. They replaced one-pole coefficients built from numbers
    that read like durations: an exponential ramp reaches 0.999 only after 6.9
    time constants, so the old "about a second" spin-up was really a seven-second
    creep that never settled into PLAY during a take, and the old "350 ms" stop
    needed about four seconds to reach rest. A transport gesture is a duration.

    One more thing the deck answers to, and it is not its own: the HOST. START
    pressed on a DAW whose transport is parked ARMS the key instead of performing
    it - see `armedForHost` - so a take begins with the machine engaging as the
    DAW rolls rather than a second and a half before it. `beginBlock()` is handed
    that one bit along with everything else.
*/
struct TransportRig
{
    enum class State : int
    {
        stop  = 0,
        play  = 1,
        start = 2
    };

    // The gestures, in seconds AT THE DEFAULT MACHINE - 15 ips, 33 rpm, 120 BPM.
    // Every scale below is exactly 1 there, so these are the numbers a fresh
    // instance performs: what the panel's tips, the parameter's own comment and
    // the README quote. STOP is a COAST rather than a clamp: a platter with mass
    // takes a moment to stop, and that moment is the gesture. The spindown hold
    // coasts longer than STOP does because the long tail IS the effect, and it
    // comes back up faster so releasing the button feels like power returning.
    static constexpr float stopCoastSeconds      = 0.75f;
    static constexpr float startSpinUpSeconds    = 1.10f;
    static constexpr float playRelockSeconds     = 0.25f;
    static constexpr float spindownCoastSeconds  = 1.60f;
    static constexpr float spindownReturnSeconds = 0.90f;

    // The two rate selectors as SCALES on those seconds, anchored on the default
    // machine so that selecting the default is choosing the documented gesture.
    //
    // Each is the square root of the speed's ratio to its own anchor: a 30 ips
    // deck runs twice the tape of a 15 and coasts 1.41 as long, a 78 turns 2.36
    // times as fast as a 33 and coasts 1.54 as long. The square root rather than
    // the ratio because only part of what a transport fights - the flywheel's
    // stored energy - grows with the speed; the servo's own rate does not, and
    // the ratio would turn the 30 ips spin-up into a two-second wait that reads
    // as a fault rather than as a machine.
    static constexpr float tapeSpeedScales[3]  { 0.71f, 1.00f, 1.41f };   // 7.5 / 15 / 30 ips
    static constexpr float vinylSpeedScales[3] { 1.00f, 1.17f, 1.54f };   // 33 / 45 / 78 rpm

    /** The tempo's scale on every gesture: half the tempo, twice the gesture,
        anchored at 120 BPM - the same anchor the wow rate uses, and the tempo a
        host that offers no playhead is assumed to be running at. Clamped to half
        and double, so a 5 BPM session cannot stretch a gesture into a stall and
        a 400 BPM one cannot squeeze it into a click. */
    static float tempoScaleFor (double bpm) noexcept
    {
        if (! (bpm > 1.0))
            return 1.0f;

        const auto ratio = 120.0 / bpm;
        return static_cast<float> (ratio < 0.5 ? 0.5 : (ratio > 2.0 ? 2.0 : ratio));
    }

    // The capstan and the momentary run-down. Public because the engine reads
    // them for more than the product (the telemetry and the panel's reels want
    // the run-down on its own), not because anything writes them.
    float ramp = 1.0f;
    float spindown = 1.0f;

    // The state, engine side. START is advanced by `advanceFrame()` and the
    // parameter catches up when the editor's timer writes it back.
    int state = static_cast<int> (State::play);

    /** True while START is being HELD FOR THE HOST - the key was pressed on a DAW
        whose transport is not rolling - so the machine is at rest waiting for it
        rather than spinning up. Public because the panel reads it (that is how it
        can say "waiting for the DAW" rather than "stopped" while the parameter
        still says START), and because the harness reads it to tell the two
        rest-states apart. */
    bool armedForHost = false;

    /** 0..1 - what the rest of the machine follows. */
    float platter() const noexcept { return ramp * spindown; }

    /** True while the platter is at full speed. The level compensator measures
        the machine's output against the input, so it must not try to correct a
        machine that is not running. */
    bool atSpeed() const noexcept { return platter() >= 0.999f; }

    /** The state the engine has asked the host to follow - START settling into
        PLAY, or a spindown released on a stopped machine - or -1 for nothing to
        publish. Taken once: the engine copies it into the atomic the editor's
        timer flushes to the parameter. */
    int takeRequestedState() noexcept
    {
        const auto requested = requestedState;
        requestedState = -1;
        return requested;
    }

    void prepare (double sampleRateIn) noexcept
    {
        sampleRate = sampleRateIn > 1.0 ? sampleRateIn : 48000.0;
        ramp = 1.0f;
        spindown = 1.0f;
        rampStep = 0.0f;
        spindownStep = 0.0f;
        capstanScale = 1.0f;
        platterScale = 1.0f;
        state = static_cast<int> (State::play);
        lastState = -1;
        spinningUp = false;
        advancedToPlay = false;
        lastHeld = false;
        armedForHost = false;
        requestedState = -1;
    }

    /** Once per block: the `transport` parameter's own value, the momentary hold
        (an atomic written by the panel), whether the HOST'S transport is rolling
        (read from the playhead by the engine, and true when the host will not
        say), the rate the ENGINE is running at - the oversampled rate, because
        the ramps advance once per engine frame and the durations above are
        wall-clock seconds - and the deck's own settings: the TAPE SPEED and VINYL
        SPEED choice indices, which scale the gestures (see the tables above), and
        the tempo they are scaled to.
    */
    void beginBlock (int stateIndex, bool held, bool hostRolling, double engineRate,
                     int tapeSpeed, int vinylSpeed, double tempoBpm) noexcept
    {
        if (engineRate > 1.0)
            sampleRate = engineRate;

        // The deck's settings, as scales on the durations. The indices are clamped
        // because a choice parameter is a small integer and a stray one must not
        // index outside the tables; they are re-derived every block rather than
        // read when a gesture starts, but the STEPS are stored when a gesture
        // starts, so a selector moved mid-coast cannot step the ramp that is
        // running - the next gesture is the one that follows the change.
        const auto tapeIndex = tapeSpeed < 0 ? 0 : (tapeSpeed > 2 ? 2 : tapeSpeed);
        const auto vinylIndex = vinylSpeed < 0 ? 0 : (vinylSpeed > 2 ? 2 : vinylSpeed);
        const auto tempoScale = tempoScaleFor (tempoBpm);

        capstanScale = tapeSpeedScales[tapeIndex] * tempoScale;
        platterScale = vinylSpeedScales[vinylIndex] * tempoScale;

        const auto parameterState = stateIndex < 0 ? 1 : (stateIndex > 2 ? 2 : stateIndex);
        auto requested = parameterState;

        // START is a transient and the engine advances it to PLAY itself. Until
        // the host has written that back, the parameter still reads START, and
        // treating each of those blocks as a fresh press would restart the
        // gesture once per block - so the advance is remembered here and a
        // parameter that still says START is read as the PLAY it is on its way
        // to. A parameter that says anything else clears it.
        //
        // The clear has to read the PARAMETER's value, not the rewritten one: the
        // rewritten value is PLAY whenever the guard fires, so clearing on it
        // would clear the guard on the very next block and the machine would
        // restart once per block after all. (tests/transport drives exactly this: a
        // parameter that stays on START for two seconds of blocks.)
        if (advancedToPlay && parameterState == static_cast<int> (State::start))
            requested = static_cast<int> (State::play);
        if (parameterState != static_cast<int> (State::start))
            advancedToPlay = false;

        // -------------------------------------------------------------------
        //  THE ARM: START waits for the HOST.
        //
        //  START is a request to the MACHINE, and the machine's take belongs to
        //  the DAW. Pressing START while the host's transport is parked therefore
        //  ARMS the key instead of running it: the machine coasts to rest and
        //  waits - nothing turns and nothing sounds - and the state follows the
        //  host, so the moment it rolls, the parameter (which still reads START)
        //  drives the ordinary spin-up below, from rest or from wherever a coast
        //  the key caught had left the capstan.
        //
        //  What the machine does while it waits is STOP's, because that is what
        //  is being done to it: the capstan is braked to rest. What differs is
        //  the REASON, and the reason is what `armedForHost` publishes - the
        //  panel reads it and says the key is waiting for the DAW rather than
        //  that the machine is stopped.
        //
        //  A host that will not answer - no playhead, or a playhead with no
        //  position - counts as ROLLING. The other reading, unknown means parked,
        //  would leave the key dead on every host that cannot answer, which is a
        //  worse fault than the one this exists to fix. A held SPINDOWN also
        //  counts as rolling: the hold is a hand on the machine rather than a
        //  transport request, and a manual power cut keeps its gesture whether or
        //  not the DAW is running.
        // -------------------------------------------------------------------
        armedForHost = requested == static_cast<int> (State::start) && ! hostRolling && ! held;

        if (armedForHost)
            requested = static_cast<int> (State::stop);

        state = requested;

        // The two hold rules, and neither runs while the key is armed: the arm
        // yields to the hold entirely (see `! held` above) rather than trading
        // one gesture against the other, and a waiting machine that its own
        // released hold could spin up would not be waiting for anything.
        if (! armedForHost)
        {
            // The release edge of a spindown hold. A hold forces the platter to run,
            // so releasing it on a stopped machine leaves the machine RUNNING: the
            // gesture put the platter in motion and cutting the power does not brake
            // it. The state follows, through the same request the auto-advance uses.
            if (lastHeld && ! held && state == static_cast<int> (State::stop))
            {
                state = static_cast<int> (State::play);
                lastState = -1;                 // re-arm: the PLAY case spins it up
                requestedState = state;
            }

            // A hold pressed on a STOPPED machine brings the platter up first - the
            // cut has to have something to cut. The engine side only: the parameter
            // follows on release, which is when the gesture has an answer.
            if (held && state == static_cast<int> (State::stop))
            {
                state = static_cast<int> (State::play);
                lastState = -1;
            }
        }

        lastHeld = held;

        // The run-down, and where it is allowed to start.
        //
        // A hold CUTS THE POWER, and a machine whose power is cut has to have been
        // running: a hold on a machine whose platter is not yet up is the gesture
        // of bringing it up and THEN cutting - so the run-down is held at 1 until
        // the capstan arrives and falls from there. Letting the two ramps overlap
        // instead (as they did) peaks the platter short of speed and cuts it from
        // there: the machine is never actually running at the moment the power
        // goes, which is not a thing a turntable does.
        if (! held)
            spindownStep = stepFor (spindownReturnSeconds * platterScale);   // the power comes back
        else if (ramp >= 1.0f)
            spindownStep = -stepFor (spindownCoastSeconds * platterScale);
        else
            spindownStep = 0.0f;                               // the cut waits for the platter

        if (state != lastState)
        {
            switch (state)
            {
                case static_cast<int> (State::stop):
                    rampStep = -stepFor (stopCoastSeconds * capstanScale);
                    spinningUp = false;
                    break;

                case static_cast<int> (State::start):
                    // START drives the capstan UP from wherever it is, at the
                    // spin-up's rate: from rest, or from partway down after a
                    // coast the key has caught. Never DOWN - the brake is STOP's.
                    // The one machine with nothing left to engage is one already
                    // at speed, and that settles straight into PLAY rather than
                    // inventing a gesture to perform.
                    if (ramp < 1.0f)
                    {
                        spinningUp = true;
                        rampStep = stepFor (startSpinUpSeconds * capstanScale);
                    }
                    else
                    {
                        spinningUp = false;
                        rampStep = 0.0f;
                        state = static_cast<int> (State::play);
                        requestedState = state;
                        advancedToPlay = true;
                    }
                    break;

                case static_cast<int> (State::play):
                default:
                    // Arriving here FROM the spin-up the ramp is already running
                    // on its own timing; PLAY selected directly is a re-lock.
                    if (! spinningUp)
                        rampStep = ramp < 1.0f ? stepFor (playRelockSeconds * capstanScale) : 0.0f;
                    break;
            }

            lastState = state;
        }
    }

    /** Once per sample frame: advance the capstan and the run-down. */
    void advanceFrame() noexcept
    {
        // "The capstan is moving", spelled as the two exact comparisons that mean
        // it rather than as `!= 0.0f`: the step is a sentinel here (zero is the
        // stopped state the transport sets itself), so the test has to stay
        // exact, and -Wfloat-equal objects to the spelling, not to the intent.
        if (rampStep > 0.0f || rampStep < 0.0f)
        {
            ramp += rampStep;

            if (rampStep > 0.0f)
            {
                if (ramp >= 1.0f)
                {
                    ramp = 1.0f;
                    rampStep = 0.0f;

                    // START is done the moment the capstan arrives, and the state
                    // says so: the machine has no "starting" position.
                    if (spinningUp)
                    {
                        spinningUp = false;

                        if (state == static_cast<int> (State::start))
                        {
                            state = static_cast<int> (State::play);
                            lastState = state;
                            advancedToPlay = true;
                            requestedState = state;
                        }
                    }
                }
            }
            else if (ramp <= 0.0f)
            {
                ramp = 0.0f;                      // at rest, and it stays there
                rampStep = 0.0f;
            }
        }

        spindown += spindownStep;
        spindown = spindown < 0.0f ? 0.0f : (spindown > 1.0f ? 1.0f : spindown);
    }

private:
    float stepFor (float seconds) const noexcept
    {
        return 1.0f / (seconds > 0.0001f ? seconds * static_cast<float> (sampleRate) : 1.0f);
    }

    double sampleRate = 48000.0;
    float rampStep = 0.0f;
    float spindownStep = 0.0f;
    float capstanScale = 1.0f;      // TAPE SPEED * tempo: on the capstan's gestures
    float platterScale = 1.0f;      // VINYL SPEED * tempo: on the platter's
    int lastState = -1;
    int requestedState = -1;
    bool spinningUp = false;
    bool advancedToPlay = false;
    bool lastHeld = false;
};
