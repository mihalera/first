// =============================================================================
//  Regression harness for the TRANSPORT: STOP, PLAY, START and SPINDOWN.
//
//  Two defects were reported against it, and neither is visible in the source:
//  the machine as compiled looked reasonable, and the gestures it produced were
//  not the gestures its own keys promise.
//
//    1. START did nothing on a machine that was already running. The capstan was
//       already up, so "spin the capstan up" was a no-op - the key had no reading
//       under which it acted on the machine it was pressed on.
//    2. STOP only made the machine QUIETER. Its ramp was a one-pole whose
//       coefficient was built from a number that read like a duration, so the
//       platter approached rest asymptotically, never actually arrived, and the
//       machine's output - which is gated by the platter - stayed audible.
//
//  Both are properties of the GESTURE over time, so they are checked here by
//  driving the real rig frame by frame and watching `platter()` - the one number
//  the engine follows, and the multiplier on the machine's output. The struct is
//  cut verbatim out of Source/PluginProcessor.h by tests/transport/extract.py and
//  #included below, so this harness cannot drift from what the plugin compiles.
//
//  What a harness like this canNOT see: the JUCE half (the parameter, the host
//  notification, the panel's buttons). Those are checked by the build. What it
//  can see is everything that was wrong.
// =============================================================================

#include <cmath>
#include <cstdio>
#include <string>

#include "extracted_transport.inc"

namespace
{
constexpr double kRate = 48000.0;

/** Adding one to a float many times drifts; the ramps are compared with this. */
constexpr float kEpsilon = 1.0e-4f;

int failures = 0;

void check (bool condition, const std::string& what)
{
    if (! condition)
    {
        std::printf ("  FAIL  %s\n", what.c_str());
        ++failures;
    }
}

std::string seconds (int frames, double rate = kRate)
{
    char text[64];
    std::snprintf (text, sizeof (text), "%d frames (%.3f s)", frames,
                   rate > 0.0 ? frames / rate : 0.0);
    return text;
}

/** A rig plus the bookkeeping a run needs: the rate, and how far it has been
    advanced. The state constants are spelled out here rather than taken from the
    enum so that a renumbered enum is a compile error here, not a silent pass. */
struct Drive
{
    static constexpr int stop  = 0;
    static constexpr int play  = 1;
    static constexpr int start = 2;

    TransportRig rig;
    double rate = kRate;
    int frames = 0;

    explicit Drive (int state, bool held = false, double sampleRate = kRate)
    {
        rate = sampleRate;
        rig.prepare (sampleRate);
        block (state, held);
    }

    void block (int state, bool held = false) { rig.beginBlock (state, held, rate); }

    void advance (int count)
    {
        for (int i = 0; i < count; ++i)
        {
            rig.advanceFrame();
            ++frames;
        }
    }

    void advanceSeconds (double value) { advance (static_cast<int> (value * rate)); }

    /** Frames advanced before `done` holds, or -1 if it never did within `limit`
        seconds. Checked before each advance, so a predicate already true costs
        nothing. */
    template <typename Predicate>
    int advanceUntil (double limitSeconds, Predicate done)
    {
        const int limit = static_cast<int> (limitSeconds * rate);

        for (int i = 0; i <= limit; ++i)
        {
            if (done (rig))
                return i;

            rig.advanceFrame();
            ++frames;
        }

        return -1;
    }
};

/** True when `frames` is the number of frames `gestureSeconds` should take.

    The tolerance is 10 ms because the ramps are single-precision: adding a step of
    about 1e-5 to a float near 1.0 rounds on every frame, and over the tens of
    thousands of frames a gesture lasts that accumulates to a millisecond or two
    either side. Which is fine - a gesture is a duration a performer feels, not a
    sample count - but it is not fine to pretend the arithmetic is exact, because
    then the harness fails on its own rounding rather than on the machine. What
    10 ms will not tolerate is a ramp of the wrong SHAPE: the one-pole this
    replaced took roughly 4 s to reach rest from a documented 350 ms, and the
    spin-up "about a second" was a seven-second creep.
*/
bool isGesture (int frames, float gestureSeconds)
{
    const double actual = frames / kRate;
    return frames >= 0 && std::abs (actual - gestureSeconds) <= 0.01;
}

// -----------------------------------------------------------------------------
//  PLAY is the steady state.
// -----------------------------------------------------------------------------
void testPlayIsSteady()
{
    std::printf ("PLAY is a steady state\n");

    Drive drive (Drive::play);
    drive.advanceSeconds (2.0);

    check (std::abs (drive.rig.platter() - 1.0f) < kEpsilon,
           "PLAY leaves the platter at speed");
    check (drive.rig.atSpeed(), "PLAY is at speed, so the level compensator measures it");
    check (drive.rig.state == Drive::play, "PLAY leaves the state where it was put");
    check (drive.rig.takeRequestedState() == -1,
           "PLAY asks the host for nothing - it is already the state");
}

// -----------------------------------------------------------------------------
//  STOP is the report. It has to REACH rest, and it has to STAY there.
// -----------------------------------------------------------------------------
void testStopCoastsToRestAndStays()
{
    std::printf ("STOP coasts to rest, and stays there\n");

    Drive drive (Drive::play);
    drive.advanceSeconds (1.0);
    drive.block (Drive::stop);

    const int coasted = drive.advanceUntil (2.0, [] (const TransportRig& rig)
                                            { return rig.platter() <= 0.0f; });

    check (coasted >= 0, "STOP reaches rest instead of approaching it");
    check (isGesture (coasted, TransportRig::stopCoastSeconds),
           "STOP takes the " + std::to_string (TransportRig::stopCoastSeconds)
               + " s it documents, not a time constant: " + seconds (coasted));
    check (drive.rig.ramp == 0.0f, "the capstan lands on exactly 0, not on 0.0003");
    check (drive.rig.platter() == 0.0f, "the platter is exactly at rest");

    // The engine's output is gated by this number - the MIX crossfade multiplies
    // BOTH its legs by it - so an exactly-zero platter is what makes STOP silence
    // rather than a quieter machine. If this ever drifts off zero, the bug is back.
    drive.advanceSeconds (4.0);
    check (drive.rig.platter() == 0.0f, "STOP leaves the machine at rest: it does not creep back up");
    check (drive.rig.takeRequestedState() == -1, "STOP asks the host for nothing - it IS the state");

    // Held at rest: a stopped machine that is stopped again is still stopped.
    drive.block (Drive::stop);
    drive.advanceSeconds (1.0);
    check (drive.rig.platter() == 0.0f, "STOP on a stopped machine is a no-op, not a second bump");
}

void testStopIsLinearInTime()
{
    std::printf ("the coast is linear in time, so the duration means what it says\n");

    Drive drive (Drive::play);
    drive.advanceSeconds (0.5);
    drive.block (Drive::stop);

    const int half = drive.advanceUntil (2.0, [] (const TransportRig& rig)
                                         { return rig.platter() <= 0.5f; });

    // A one-pole - the shape that shipped, and the shape the doc comment above the
    // durations warns about - reaches half speed after 0.69 time constants and then
    // crawls. A linear ramp reaches it exactly halfway through.
    check (half >= 0 && isGesture (half, TransportRig::stopCoastSeconds * 0.5f),
           "half speed arrives halfway through the coast: " + seconds (half));
}

// -----------------------------------------------------------------------------
//  START on a machine at rest: the engagement gesture.
// -----------------------------------------------------------------------------
void testStartFromRest()
{
    std::printf ("START from rest spins up and settles into PLAY\n");

    Drive drive (Drive::play);
    drive.block (Drive::stop);
    drive.advanceUntil (2.0, [] (const TransportRig& rig) { return rig.platter() <= 0.0f; });
    check (drive.rig.platter() == 0.0f, "the machine is genuinely at rest before START");

    drive.block (Drive::start);

    const int spinUp = drive.advanceUntil (3.0, [] (const TransportRig& rig)
                                          { return rig.platter() >= 1.0f; });

    check (spinUp >= 0, "START brings the platter back to speed");
    check (isGesture (spinUp, TransportRig::startSpinUpSeconds),
           "the spin-up takes the " + std::to_string (TransportRig::startSpinUpSeconds)
               + " s it documents: " + seconds (spinUp));
    check (drive.rig.state == Drive::play, "the state says PLAY once the capstan arrives");
    check (drive.rig.takeRequestedState() == Drive::play,
           "START asks the host to follow it into PLAY");
    check (drive.rig.takeRequestedState() == -1, "that request is taken exactly once");

    // The parameter still reads START until the editor's timer writes the new
    // state back. Those blocks must NOT be read as a fresh press, or the machine
    // re-cues every block and never settles.
    for (int i = 0; i < 400; ++i)
    {
        drive.block (Drive::start);
        drive.advanceSeconds (0.05);
    }

    check (drive.rig.platter() == 1.0f, "a parameter still reading START does not re-cue the machine");
    check (drive.rig.state == Drive::play, "and it does not ask the host again");
    check (drive.rig.takeRequestedState() == -1, "no second request is published");
}

// -----------------------------------------------------------------------------
//  START on a machine already running: the report. It re-cues.
// -----------------------------------------------------------------------------
void testStartOnRunningMachine()
{
    std::printf ("START on a running machine re-cues and spins back up\n");

    Drive drive (Drive::play);
    drive.advanceSeconds (1.0);
    drive.block (Drive::start);

    const int braked = drive.advanceUntil (2.0, [] (const TransportRig& rig)
                                           { return rig.platter() <= 0.0f; });

    check (braked >= 0, "START acts on a running machine: it does not leave it turning");
    check (isGesture (braked, TransportRig::startRecueSeconds),
           "the re-cue brake takes the " + std::to_string (TransportRig::startRecueSeconds)
               + " s it documents: " + seconds (braked));

    const int spunUp = drive.advanceUntil (3.0, [] (const TransportRig& rig)
                                           { return rig.platter() >= 1.0f; });

    check (isGesture (spunUp, TransportRig::startSpinUpSeconds),
           "and the spin-up that follows takes its own documented time: " + seconds (spunUp));
    check (drive.rig.state == Drive::play, "the re-cue still lands in PLAY");
    check (drive.rig.takeRequestedState() == Drive::play, "and publishes that once");
}

void testStartInterruptsAStop()
{
    std::printf ("START during a STOP coast catches the machine and brings it back\n");

    Drive drive (Drive::play);
    drive.advanceSeconds (0.5);
    drive.block (Drive::stop);
    drive.advanceSeconds (0.3);

    check (drive.rig.platter() > 0.0f && drive.rig.platter() < 1.0f,
           "the test starts mid-coast, with the platter partway down");

    const float platterAtPress = drive.rig.platter();
    drive.block (Drive::start);

    const int rest = drive.advanceUntil (2.0, [] (const TransportRig& rig)
                                         { return rig.platter() <= 0.0f; });

    // The brake is a RATE, not a duration: catching a machine that is only partway
    // up takes it proportionally less time to bring down. That is what a brake does
    // - and it is the property that makes the gesture read as one physical action
    // whatever speed the key is pressed at, so it is checked as the rate it is.
    check (rest >= 0, "START from a coasting machine completes the brake it asked for");
    check (std::abs (rest / kRate - platterAtPress * TransportRig::startRecueSeconds) <= 0.01,
           "and it brakes at the documented rate: " + seconds (rest) + " from "
               + std::to_string (platterAtPress) + " platter");

    check (drive.advanceUntil (3.0, [] (const TransportRig& rig)
                               { return rig.platter() >= 1.0f; }) >= 0,
           "then the machine comes back up to speed");
    check (drive.rig.state == Drive::play, "and the state arrives at PLAY");
}

// -----------------------------------------------------------------------------
//  SPINDOWN: the momentary hold.
// -----------------------------------------------------------------------------
void testSpindownRunsThePlatterDown()
{
    std::printf ("SPINDOWN runs the platter down and releases it back\n");

    Drive drive (Drive::play);
    drive.advanceSeconds (0.5);
    drive.block (Drive::play, true);                    // the button goes down

    const int down = drive.advanceUntil (3.0, [] (const TransportRig& rig)
                                         { return rig.platter() <= 0.0f; });

    check (down >= 0, "holding SPINDOWN takes the platter all the way down");
    check (isGesture (down, TransportRig::spindownCoastSeconds),
           "the run-down takes the " + std::to_string (TransportRig::spindownCoastSeconds)
               + " s it documents: " + seconds (down));
    check (drive.rig.spindown == 0.0f, "the run-down lands on exactly 0");
    check (drive.rig.state == Drive::play,
           "a hold is an effect ON the transport, not a fourth state: the state is untouched");
    check (drive.rig.takeRequestedState() == -1,
           "and a hold on a running machine asks the host for nothing");

    drive.block (Drive::play, false);                   // release
    const int back = drive.advanceUntil (3.0, [] (const TransportRig& rig)
                                         { return rig.platter() >= 1.0f; });

    check (isGesture (back, TransportRig::spindownReturnSeconds),
           "releasing it brings the platter back in " + std::to_string (TransportRig::spindownReturnSeconds)
               + " s: " + seconds (back));
    check (drive.rig.ramp == 1.0f,
           "and it comes back by restoring the RUN-DOWN, not by restarting the capstan");
}

void testSpindownFromStopBringsItUp()
{
    std::printf ("SPINDOWN on a stopped machine brings the platter up, then cuts it\n");

    Drive drive (Drive::play);
    drive.block (Drive::stop);
    drive.advanceUntil (2.0, [] (const TransportRig& rig) { return rig.platter() <= 0.0f; });

    drive.block (Drive::stop, true);                    // the button goes down

    const int rose = drive.advanceUntil (1.5, [] (const TransportRig& rig)
                                         { return rig.platter() >= 1.0f; });

    check (rose >= 0, "the held button puts the platter in motion: the gesture has something to cut");
    check (isGesture (rose, TransportRig::playRelockSeconds),
           "it comes up in the " + std::to_string (TransportRig::playRelockSeconds)
               + " s a re-lock takes: " + seconds (rose));
    check (drive.rig.platter() == 1.0f,
           "and it arrives AT SPEED before the power goes - a cut is only a cut on a machine that runs");
    check (drive.rig.takeRequestedState() == -1,
           "the machine is still STOP while the button is held: the parameter follows on release");

    // The engine re-reads the rig every block, so the next block is where the cut
    // starts - the run-down is held at 1 while the capstan is still coming up.
    drive.block (Drive::stop, true);

    const int cut = drive.advanceUntil (3.0, [] (const TransportRig& rig)
                                        { return rig.platter() <= 0.0f; });
    check (isGesture (cut, TransportRig::spindownCoastSeconds),
           "and the cut itself is the documented run-down: " + seconds (cut));

    drive.block (Drive::stop, false);                   // release

    check (drive.rig.state == Drive::play,
           "releasing the hold on a stopped machine leaves the machine RUNNING");
    check (drive.rig.takeRequestedState() == Drive::play,
           "the machine tells the host about that, once");
    check (drive.advanceUntil (3.0, [] (const TransportRig& rig)
                               { return rig.platter() >= 1.0f; }) >= 0,
           "and the platter stays up afterwards");
}

// -----------------------------------------------------------------------------
//  The invariants the engine leans on.
// -----------------------------------------------------------------------------
void testEngineFacingProperties()
{
    std::printf ("the properties the engine reads the rig through\n");

    // `atSpeed()` is what keeps the level compensator from measuring a machine
    // that is not running. Every state that is not full speed has to answer false.
    Drive hold (Drive::play);
    hold.advanceSeconds (0.25);
    hold.block (Drive::play, true);
    hold.advanceSeconds (0.23);

    check (std::abs (hold.rig.platter() - hold.rig.ramp * hold.rig.spindown) < kEpsilon,
           "the platter is the capstan times the run-down, and nothing else");
    check (! hold.rig.atSpeed(),
           "a machine running down is not at speed: the compensator leaves it alone");

    Drive coast (Drive::play);
    coast.advanceSeconds (0.25);
    coast.block (Drive::stop);
    coast.advanceSeconds (0.25);

    check (! coast.rig.atSpeed(), "neither is a machine coasting to a stop");
    check (coast.rig.platter() > 0.0f, "and it is still moving at this point");

    // The platter only ever goes one way while a gesture is running: a machine
    // that stepped back up mid-coast would read as a stutter, not as a stop.
    float previous = coast.rig.platter();
    bool monotone = true;

    for (int frame = 0; frame < 40000 && coast.rig.platter() > 0.0f; ++frame)
    {
        coast.rig.advanceFrame();

        if (coast.rig.platter() > previous)
            monotone = false;

        previous = coast.rig.platter();
    }

    check (monotone, "a STOP coast never steps back up");
    check (coast.rig.atSpeed() == false && coast.rig.platter() == 0.0f,
           "and it ends at rest, which is not at speed");

    // A machine brought back up has to be at speed again, or the compensator would
    // never resume once the take restarts.
    Drive running (Drive::play);
    running.advanceSeconds (1.0);
    check (running.rig.atSpeed(), "a machine at speed says so, so the compensator resumes");
}

void testDurationsAreSane()
{
    std::printf ("the documented gestures are durations a user could play\n");

    const float gestures[] = { TransportRig::stopCoastSeconds, TransportRig::startSpinUpSeconds,
                               TransportRig::startRecueSeconds,  TransportRig::playRelockSeconds,
                               TransportRig::spindownCoastSeconds, TransportRig::spindownReturnSeconds };

    for (const float seconds : gestures)
        check (seconds >= 0.05f && seconds <= 3.0f,
               "a gesture is between 50 ms and 3 s: " + std::to_string (seconds));

    check (TransportRig::startSpinUpSeconds > TransportRig::startRecueSeconds,
           "the spin-up is the slow half of START, as its own note says");
    check (TransportRig::spindownCoastSeconds > TransportRig::stopCoastSeconds,
           "the power cut coasts longer than STOP does - the tail IS the effect");
    check (TransportRig::spindownReturnSeconds < TransportRig::spindownCoastSeconds,
           "and it comes back up faster than it ran down");

    // The ramps are per-frame at the ENGINE's rate, so the gestures have to land in
    // the same wall-clock time at any supported rate.
    for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        Drive drive (Drive::play, false, rate);
        drive.advanceSeconds (0.25);
        drive.block (Drive::stop);

        const int coasted = drive.advanceUntil (2.0, [] (const TransportRig& rig)
                                                { return rig.platter() <= 0.0f; });
        const double actual = coasted / rate;

        check (coasted >= 0 && std::abs (actual - TransportRig::stopCoastSeconds) <= 0.01,
               "STOP takes the same wall-clock time at " + std::to_string (static_cast<int> (rate))
                   + " Hz: " + seconds (coasted, rate));
    }
}

int run()
{
    testPlayIsSteady();
    testStopCoastsToRestAndStays();
    testStopIsLinearInTime();
    testStartFromRest();
    testStartOnRunningMachine();
    testStartInterruptsAStop();
    testSpindownRunsThePlatterDown();
    testSpindownFromStopBringsItUp();
    testEngineFacingProperties();
    testDurationsAreSane();

    if (failures == 0)
        std::printf ("TRANSPORT CHECK PASSED (0 failures)\n");
    else
        std::printf ("TRANSPORT CHECK FAILED (%d)\n", failures);

    return failures == 0 ? 0 : 1;
}
} // namespace

int main() { return run(); }
