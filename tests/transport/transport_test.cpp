// =============================================================================
//  Regression harness for the TRANSPORT: STOP, PLAY, START and SPINDOWN.
//
//  What is checked here is the GESTURE: what each key does to the machine, how
//  long it takes, and what the platter speed is while it does it - because the
//  gestures cannot be read out of the source. The machine as compiled looks
//  reasonable either way, and two defects were reported against it that were
//  invisible there:
//
//    1. STOP only made the machine QUIETER. Its ramp was a one-pole whose
//       coefficient was built from a number that read like a duration, so the
//       platter approached rest asymptotically, never actually arrived, and the
//       machine's output - which is gated by the platter - stayed audible.
//    2. The gestures did not follow the deck. TAPE SPEED, VINYL SPEED and the
//       session's tempo all moved what the machine SOUNDS like and none of them
//       moved what it DOES, so a 78 rpm platter ran down exactly like a 33 and a
//       30 ips deck came up exactly as fast as a 7.5.
//
//  A third reading is checked as an absence: START is a SPIN-UP and never a
//  brake. It was briefly made to re-cue (a short brake, then the spin-up) so that
//  the key would do something on a machine that was already running, and a key
//  that stops the deck before starting it is a different gesture wearing START's
//  name.
//
//  All of it is a property of the gesture over time, so it is checked by driving
//  the real rig frame by frame and watching `platter()` - the one number the
//  engine follows, and the multiplier on the machine's output. The struct is cut
//  verbatim out of Source/PluginProcessor.h by tests/transport/extract.py and
//  #included below, so this harness cannot drift from what the plugin compiles.
//
//  What a harness like this canNOT see: the JUCE half (the parameter, the host
//  notification, the panel's buttons). Those are checked by the build. What it
//  can see is everything that was wrong.
// =============================================================================

#include <algorithm>
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

/** A rig plus the bookkeeping a run needs: the rate, how far it has been
    advanced, and the DECK'S OWN SETTINGS the gestures are scaled by - the TAPE
    SPEED choice, the VINYL SPEED choice and the tempo. The defaults are the
    machine every documented duration is stated for (15 ips, 33 rpm, 120 BPM),
    where all three scales are exactly 1. The state constants are spelled out here
    rather than taken from the enum so that a renumbered enum is a compile error
    here, not a silent pass. */
struct Drive
{
    static constexpr int stop  = 0;
    static constexpr int play  = 1;
    static constexpr int start = 2;

    static constexpr int tape15    = 1;
    static constexpr int vinyl33   = 0;
    static constexpr double tempo120 = 120.0;

    TransportRig rig;
    double rate = kRate;
    int frames = 0;
    int tape = tape15;
    int vinyl = vinyl33;
    double tempo = tempo120;

    explicit Drive (int state, bool held = false, double sampleRate = kRate,
                    int tapeSpeed = tape15, int vinylSpeed = vinyl33,
                    double bpm = tempo120)
    {
        rate = sampleRate;
        tape = tapeSpeed;
        vinyl = vinylSpeed;
        tempo = bpm;
        rig.prepare (sampleRate);
        block (state, held);
    }

    void block (int state, bool held = false)
    { rig.beginBlock (state, held, rate, tape, vinyl, tempo); }

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
//  START is a SPIN-UP. It is never a brake, and it is never a no-op the machine
//  can be left sitting in.
// -----------------------------------------------------------------------------
void testStartOnRunningMachine()
{
    std::printf ("START on a running machine engages nothing and settles into PLAY\n");

    Drive drive (Drive::play);
    drive.advanceSeconds (1.0);
    drive.block (Drive::start);

    // The gesture drives the capstan UP, so a machine already at speed has
    // nothing left to do. What must NOT happen is the re-cue this replaced: a
    // short brake to rest and then the spin-up, which turned the key into a
    // different gesture. Nor may the state be left sitting on START.
    float lowest = drive.rig.platter();

    for (int frame = 0; frame < static_cast<int> (0.5 * kRate); ++frame)
    {
        drive.rig.advanceFrame();
        ++drive.frames;
        lowest = std::min (lowest, drive.rig.platter());
    }

    check (drive.rig.platter() == 1.0f, "START leaves a running machine running");
    check (lowest == 1.0f, "and it does not brake it first: there is no re-cue");
    check (drive.rig.state == Drive::play, "the state settles into PLAY, it does not sit on START");
    check (drive.rig.takeRequestedState() == Drive::play, "and that is published to the host");
    check (drive.rig.takeRequestedState() == -1, "exactly once");
}

void testStartCatchesACoast()
{
    std::printf ("START during a STOP coast brings the machine back UP\n");

    Drive drive (Drive::play);
    drive.advanceSeconds (0.5);
    drive.block (Drive::stop);
    drive.advanceSeconds (0.3);

    const float platterAtPress = drive.rig.platter();
    check (platterAtPress > 0.0f && platterAtPress < 1.0f,
           "the test starts mid-coast, with the platter partway down");

    drive.block (Drive::start);

    // From here the platter only goes one way: UP. The time it takes is the
    // distance it has left at the spin-up's own rate, which is what makes the
    // spin-up and the catch the SAME gesture rather than two special cases.
    float lowest = platterAtPress;
    int recovered = -1;

    for (int frame = 0; frame <= static_cast<int> (2.0 * kRate); ++frame)
    {
        if (drive.rig.platter() >= 1.0f)
        {
            recovered = frame;
            break;
        }

        drive.rig.advanceFrame();
        ++drive.frames;
        lowest = std::min (lowest, drive.rig.platter());
    }

    check (recovered >= 0, "START brings the coasting machine back to speed");
    check (lowest >= platterAtPress - kEpsilon,
           "and never brakes on the way: the platter only rises");
    check (std::abs (recovered / kRate - (1.0f - platterAtPress) * TransportRig::startSpinUpSeconds) <= 0.01,
           "it comes back at the spin-up's documented rate: " + seconds (recovered)
               + " from " + std::to_string (platterAtPress) + " platter, against "
               + std::to_string ((1.0f - platterAtPress) * TransportRig::startSpinUpSeconds) + " s");
    check (drive.rig.state == Drive::play, "and the state arrives at PLAY");
    check (drive.rig.takeRequestedState() == Drive::play, "with the host told once");
}

// -----------------------------------------------------------------------------
//  The deck's own settings move the gestures.
//
//  One machine per set of settings, measured the same way every other gesture in
//  this file is: drive it and count the frames. The expected time is the
//  documented second times the scale table's entry, so a scale that stops being
//  applied - or one applied to the wrong gesture - fails here rather than being
//  heard as "the 78 does not feel longer than the 33".
// -----------------------------------------------------------------------------
constexpr int kTapeSpeeds[3] { 0, 1, 2 };        // 7.5 / 15 / 30 ips
constexpr int kVinylSpeeds[3] { 0, 1, 2 };       // 33 / 45 / 78 rpm
const char* const kTapeNames[3] { "7.5 ips", "15 ips", "30 ips" };
const char* const kVinylNames[3] { "33 rpm", "45 rpm", "78 rpm" };

std::string scaleName (const char* const label, const char* const name, float scale)
{
    char text[64];
    std::snprintf (text, sizeof (text), "%.2f x (%s)", scale, name);
    return std::string (label) + " " + text;
}

/** Seconds a STOP coast takes on the machine these settings describe, or -1. */
double stopCoastOn (int tapeSpeed, int vinylSpeed, double bpm)
{
    Drive drive (Drive::play, false, kRate, tapeSpeed, vinylSpeed, bpm);
    drive.advanceSeconds (0.5);
    drive.block (Drive::stop);

    const int coasted = drive.advanceUntil (8.0, [] (const TransportRig& rig)
                                            { return rig.platter() <= 0.0f; });
    return coasted < 0 ? -1.0 : coasted / kRate;
}

/** Frames a START from rest takes on the machine these settings describe. */
int spinUpOn (int tapeSpeed, int vinylSpeed, double bpm)
{
    Drive drive (Drive::play, false, kRate, tapeSpeed, vinylSpeed, bpm);
    drive.block (Drive::stop);
    drive.advanceUntil (8.0, [] (const TransportRig& rig) { return rig.platter() <= 0.0f; });
    drive.block (Drive::start);

    return drive.advanceUntil (8.0, [] (const TransportRig& rig) { return rig.platter() >= 1.0f; });
}

/** Frames a PLAY re-lock takes from rest on the machine these settings describe. */
int relockOn (int tapeSpeed, int vinylSpeed, double bpm)
{
    Drive drive (Drive::play, false, kRate, tapeSpeed, vinylSpeed, bpm);
    drive.block (Drive::stop);
    drive.advanceUntil (8.0, [] (const TransportRig& rig) { return rig.platter() <= 0.0f; });
    drive.block (Drive::play);

    return drive.advanceUntil (8.0, [] (const TransportRig& rig) { return rig.platter() >= 1.0f; });
}

/** Seconds a SPINDOWN run-down takes on the machine these settings describe. */
double spindownCoastOn (int tapeSpeed, int vinylSpeed, double bpm)
{
    Drive drive (Drive::play, false, kRate, tapeSpeed, vinylSpeed, bpm);
    drive.advanceSeconds (0.5);
    drive.block (Drive::play, true);

    const int down = drive.advanceUntil (8.0, [] (const TransportRig& rig)
                                         { return rig.platter() <= 0.0f; });
    return down < 0 ? -1.0 : down / kRate;
}

/** Seconds a SPINDOWN release takes to bring the platter back. */
double spindownReturnOn (int tapeSpeed, int vinylSpeed, double bpm)
{
    Drive drive (Drive::play, false, kRate, tapeSpeed, vinylSpeed, bpm);
    drive.advanceSeconds (0.5);
    drive.block (Drive::play, true);
    drive.advanceUntil (8.0, [] (const TransportRig& rig) { return rig.platter() <= 0.0f; });
    drive.block (Drive::play, false);

    const int back = drive.advanceUntil (8.0, [] (const TransportRig& rig)
                                         { return rig.platter() >= 1.0f; });
    return back < 0 ? -1.0 : back / kRate;
}

void testTapeSpeedScalesTheCapstan()
{
    std::printf ("TAPE SPEED scales START, STOP and PLAY, and not the platter\n");

    for (const int tape : kTapeSpeeds)
    {
        const float scale = TransportRig::tapeSpeedScales[tape];
        const std::string at = scaleName ("", kTapeNames[tape], scale);

        const double coast = stopCoastOn (tape, Drive::vinyl33, Drive::tempo120);
        check (coast > 0.0 && std::abs (coast - TransportRig::stopCoastSeconds * scale) <= 0.01,
               "STOP coasts in " + std::to_string (TransportRig::stopCoastSeconds * scale)
                   + " s at" + at + ": " + std::to_string (coast) + " s");

        const int spinUp = spinUpOn (tape, Drive::vinyl33, Drive::tempo120);
        check (isGesture (spinUp, TransportRig::startSpinUpSeconds * scale),
               "START comes up in " + std::to_string (TransportRig::startSpinUpSeconds * scale)
                   + " s at" + at + ": " + seconds (spinUp));

        const int relock = relockOn (tape, Drive::vinyl33, Drive::tempo120);
        check (relock >= 0 && std::abs (relock / kRate - TransportRig::playRelockSeconds * scale) <= 0.01,
               "PLAY re-locks in " + std::to_string (TransportRig::playRelockSeconds * scale)
                   + " s at" + at + ": " + seconds (relock));
    }

    // The two selectors scale different halves of the machine. A tape speed that
    // also moved the platter (or a turntable that moved the capstan) would make
    // the couple inseparable, and the panel's two rate selectors would be one.
    const double defaultDown = spindownCoastOn (Drive::tape15, Drive::vinyl33, Drive::tempo120);
    const double slowTapeDown = spindownCoastOn (2, Drive::vinyl33, Drive::tempo120);
    check (std::abs (defaultDown - TransportRig::spindownCoastSeconds) <= 0.01
               && std::abs (slowTapeDown - defaultDown) <= 0.01,
           "the tape speed leaves SPINDOWN alone: it is the platter's gesture");
}

void testVinylSpeedScalesThePlatter()
{
    std::printf ("VINYL SPEED scales SPINDOWN, and not the capstan\n");

    for (const int vinyl : kVinylSpeeds)
    {
        const float scale = TransportRig::vinylSpeedScales[vinyl];
        const std::string at = scaleName ("", kVinylNames[vinyl], scale);

        const double down = spindownCoastOn (Drive::tape15, vinyl, Drive::tempo120);
        check (down > 0.0 && std::abs (down - TransportRig::spindownCoastSeconds * scale) <= 0.01,
               "the run-down takes " + std::to_string (TransportRig::spindownCoastSeconds * scale)
                   + " s at" + at + ": " + std::to_string (down) + " s");

        const double back = spindownReturnOn (Drive::tape15, vinyl, Drive::tempo120);
        check (back > 0.0 && std::abs (back - TransportRig::spindownReturnSeconds * scale) <= 0.01,
               "and the power comes back in " + std::to_string (TransportRig::spindownReturnSeconds * scale)
                   + " s at" + at + ": " + std::to_string (back) + " s");
    }

    const double defaultCoast = stopCoastOn (Drive::tape15, Drive::vinyl33, Drive::tempo120);
    const double shellacCoast = stopCoastOn (Drive::tape15, 2, Drive::tempo120);
    check (std::abs (defaultCoast - TransportRig::stopCoastSeconds) <= 0.01
               && std::abs (shellacCoast - defaultCoast) <= 0.01,
           "the turntable speed leaves STOP alone: it is the capstan's gesture");
}

void testTempoScalesEveryGesture()
{
    std::printf ("the tempo scales every gesture, anchored at 120 BPM\n");

    struct Case { double bpm; float scale; const char* what; };
    const Case cases[] {
        { 120.0, 1.0f, "the anchor" },
        { 60.0, 2.0f, "half the tempo, twice the gesture" },
        { 240.0, 0.5f, "double the tempo, half the gesture" },
        { 90.0, 120.0 / 90.0, "in between" },
        { 20.0, 2.0f, "clamped at the slow end" },
        { 480.0, 0.5f, "clamped at the fast end" },
        { 0.0, 1.0f, "no host tempo at all" }
    };

    for (const auto& testCase : cases)
    {
        char label[32];
        std::snprintf (label, sizeof (label), " at %.0f BPM", testCase.bpm);
        const std::string at = label;

        check (std::abs (TransportRig::tempoScaleFor (testCase.bpm) - testCase.scale) <= 1.0e-6f,
               std::string ("the tempo scale is ") + std::to_string (testCase.scale) + at
                   + " - " + testCase.what);

        if (testCase.bpm <= 1.0)
            continue;                  // a tempo a host did not supply is the anchor only

        const double coast = stopCoastOn (Drive::tape15, Drive::vinyl33, testCase.bpm);
        check (coast > 0.0 && std::abs (coast - TransportRig::stopCoastSeconds * testCase.scale) <= 0.01,
               "STOP coasts in " + std::to_string (TransportRig::stopCoastSeconds * testCase.scale)
                   + " s" + at + ": " + std::to_string (coast) + " s");

        const int spinUp = spinUpOn (Drive::tape15, Drive::vinyl33, testCase.bpm);
        check (isGesture (spinUp, TransportRig::startSpinUpSeconds * testCase.scale),
               "START comes up in " + std::to_string (TransportRig::startSpinUpSeconds * testCase.scale)
                   + " s" + at + ": " + seconds (spinUp));

        const double down = spindownCoastOn (Drive::tape15, Drive::vinyl33, testCase.bpm);
        check (down > 0.0 && std::abs (down - TransportRig::spindownCoastSeconds * testCase.scale) <= 0.01,
               "and the power cut runs down in " + std::to_string (TransportRig::spindownCoastSeconds * testCase.scale)
                   + " s" + at + ": " + std::to_string (down) + " s");
    }
}

void testTheSettingsCompose()
{
    std::printf ("the three settings compose on the one machine\n");

    // The corner the panel can actually be put in: the slowest tempo of the range
    // and the heaviest machine on the shelf, all at once. The gesture has to be
    // the product of the two scales and stay a gesture.
    const float tempoScale = TransportRig::tempoScaleFor (60.0);
    const float capstan = TransportRig::tapeSpeedScales[2] * tempoScale;
    const float platter = TransportRig::vinylSpeedScales[2] * tempoScale;

    const double coast = stopCoastOn (2, 2, 60.0);
    check (coast > 0.0 && std::abs (coast - TransportRig::stopCoastSeconds * capstan) <= 0.01,
           "a 30 ips machine at 60 BPM coasts in " + std::to_string (TransportRig::stopCoastSeconds * capstan)
               + " s: " + std::to_string (coast) + " s");

    const double down = spindownCoastOn (2, 2, 60.0);
    check (down > 0.0 && std::abs (down - TransportRig::spindownCoastSeconds * platter) <= 0.01,
           "a 78 at 60 BPM runs down in " + std::to_string (TransportRig::spindownCoastSeconds * platter)
               + " s: " + std::to_string (down) + " s");
    check (down <= 6.0, "and the longest corner is still a gesture rather than a stall");
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

void testDurationsAndScalesAreSane()
{
    std::printf ("the documented gestures, and the scales that move them, are sane\n");

    const float gestures[] = { TransportRig::stopCoastSeconds, TransportRig::startSpinUpSeconds,
                               TransportRig::playRelockSeconds,  TransportRig::spindownCoastSeconds,
                               TransportRig::spindownReturnSeconds };

    for (const float duration : gestures)
        check (duration >= 0.05f && duration <= 3.0f,
               "a default-machine gesture is between 50 ms and 3 s: " + std::to_string (duration));

    check (TransportRig::spindownCoastSeconds > TransportRig::stopCoastSeconds,
           "the power cut coasts longer than STOP does - the tail IS the effect");
    check (TransportRig::spindownReturnSeconds < TransportRig::spindownCoastSeconds,
           "and it comes back up faster than it ran down");

    // The anchors are what make the seconds above the truth for a fresh instance -
    // 15 ips, 33 rpm, 120 BPM - so they are checked as identities, not as ranges:
    // on the default machine every scale is exactly 1, and the default machine is
    // the one the tips, the parameter's comment and the README quote.
    check (TransportRig::tapeSpeedScales[1] == 1.0f, "15 ips is the tape anchor: scale 1");
    check (TransportRig::vinylSpeedScales[0] == 1.0f, "33 rpm is the platter anchor: scale 1");
    check (TransportRig::tempoScaleFor (120.0) == 1.0f, "120 BPM is the tempo anchor: scale 1");
    check (TransportRig::tempoScaleFor (0.0) == 1.0f,
           "a host that supplies no tempo leaves the anchor alone");
    check (TransportRig::tempoScaleFor (-20.0) == 1.0f, "and so does a nonsense one");

    // Both tables climb with the speed, and the slowest machine is genuinely
    // lighter and quicker than the anchor.
    for (int index = 1; index < 3; ++index)
    {
        check (TransportRig::tapeSpeedScales[index] > TransportRig::tapeSpeedScales[index - 1],
               "a faster tape is a heavier transport, not a lighter one");
        check (TransportRig::vinylSpeedScales[index] > TransportRig::vinylSpeedScales[index - 1],
               "a faster platter coasts longer, not shorter");
    }

    check (TransportRig::tapeSpeedScales[0] < 1.0f && TransportRig::tapeSpeedScales[2] > 1.0f,
           "the tape table straddles its anchor");
    check (TransportRig::vinylSpeedScales[2] > 1.0f,
           "and the platter table climbs away from its own");

    // The corners of the three selectors together, which is what a user can put
    // the panel into: the longest gesture is the 78's run-down at a slow tempo on
    // a heavy deck, the shortest a re-lock at a fast one. Neither may leave the
    // range a transport is playable in - a gesture is not a stall, and it is not
    // a click.
    const float slow = TransportRig::tempoScaleFor (60.0);
    const float fast = TransportRig::tempoScaleFor (240.0);
    const float longest = std::max (TransportRig::startSpinUpSeconds * TransportRig::tapeSpeedScales[2] * slow,
                                    TransportRig::spindownCoastSeconds * TransportRig::vinylSpeedScales[2] * slow);
    const float shortest = std::min (TransportRig::playRelockSeconds * TransportRig::tapeSpeedScales[0] * fast,
                                     TransportRig::spindownReturnSeconds * TransportRig::vinylSpeedScales[0] * fast);

    check (longest <= 6.0f, "the heaviest machine at a slow tempo is still a gesture: "
                                + std::to_string (longest) + " s");
    check (shortest >= 0.05f, "and the lightest at a fast one is not a click: "
                                  + std::to_string (shortest) + " s");

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
    testStartCatchesACoast();
    testSpindownRunsThePlatterDown();
    testSpindownFromStopBringsItUp();
    testTapeSpeedScalesTheCapstan();
    testVinylSpeedScalesThePlatter();
    testTempoScalesEveryGesture();
    testTheSettingsCompose();
    testEngineFacingProperties();
    testDurationsAndScalesAreSane();

    if (failures == 0)
        std::printf ("TRANSPORT CHECK PASSED (0 failures)\n");
    else
        std::printf ("TRANSPORT CHECK FAILED (%d)\n", failures);

    return failures == 0 ? 0 : 1;
}
} // namespace

int main() { return run(); }
