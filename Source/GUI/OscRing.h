// =============================================================================
//  OSC on the deck: a circle of the key's notes and a ball that BOUNCES inside it.
//
//  WHAT THE WIDGET IS. A disc divided into one place per note of the selected
//  KEY - the degrees of TONIC and MODE (the AUTOTUNE tab's two selectors) across
//  three octaves, C3 to B5 - with a ball that falls under gravity and rebounds
//  off the edge. Where it HITS IS the note: the contact angle picks the place, and
//  the dot for it lights up so the mapping is visible rather than implied. How
//  hard it hits is how hard the note is played.
//
//  WHY THE NOTES FOLLOW THE KEY. The generator enters the machine at INPUT and is
//  heard through whatever the deck is set to, so it is a musical source rather
//  than a test tone: twelve chromatic sectors would put it in C whatever the user
//  is playing in, and every hit on an out-of-key semitone would be the one
//  thing the rest of the panel spends a whole tab avoiding. The ring's notes are
//  therefore the scale's own degrees - seven places per octave in MAJOR, five in
//  a pentatonic, twelve in CHROMATIC - and a change of TONIC or MODE re-derives
//  them and retunes the note under the ball's last contact. Source/ScaleDegrees.h is the
//  table the pitch corrector uses too, so the corrector and the generator cannot
//  end up in two different keys.
//
//  WHY THREE OCTAVES. One octave was a ring that could only say the same eleven
//  intervals in a circle; the ball now travels from C3 to B5, so a run of bounces
//  is a phrase with a register as well as a pitch, and the same key sounds
//  different depending on where in the range the ball happens to be.
//
//  WHY A REAL BOUNCE. The old motion was a timed hop from one landing spot to the
//  next: a different chord every time, but the same duration, and the note was
//  chosen before the ball moved - a selector being advanced with an animation on
//  top. A ball under gravity is not a scheduler. It crosses the disc, hits the rim
//  where its own momentum takes it, and the note changes because the contact point
//  changed. Gravity alone would settle at six o'clock and play one sector forever,
//  so every impact gets a tangential kick (Source/OscBall.h): the ball walks the
//  ring instead of piling up at the bottom. The note is still chosen by position,
//  but the position is earned by the bounce.
//
//  WHY THE PHYSICS IS A SHARED HEADER. The editor ticks at the panel's rate and the
//  engine once per audio block, so neither can own the integration. Both call the
//  same fixed-step code in Source/OscBall.h, and the state - position, velocity,
//  the unspent fraction of a step and the spin's generator - is handed over with
//  the clock. Closing the window therefore does not move the ball; it only changes
//  who is asking it to fall.
//
//  WHY A CALL FROM THE EDITOR'S TIMER RATHER THAN A TIMER OF ITS OWN. The
//  editor already ticks thirty times a second and repaints in a bounded way, so
//  the ring is advanced by that tick (tick()). A second juce::Timer would be a
//  second clock to keep in step with the first, for an animation that does not
//  deserve one.
//
//  WHAT HAPPENS WHEN THE WINDOW CLOSES. The ring's clock is the editor's timer,
//  so it disappears with the window - but the module must not. The processor keeps
//  stepping the same physics while no editor exists (advanceOscBall in
//  ProcessorSignalPath.cpp), from the state the editor hands over. A new editor
//  asks for that state (getOscBallState) and its sector (getOscSector), so
//  reopening the window shows the ball where it actually is rather than where the
//  previous window left it. The widget draws the editor's view; it is not the
//  machine's only clock.
//
//  WHAT THE SWITCH DOES TO IT. OSC off freezes the ball where it is and stops the
//  announcements: the module is not generating, so a ball bouncing to notes nobody
//  hears would be a widget out of step with the machine. The ball is not hidden and
//  the ring is not greyed out of existence - it rests on the note it last played,
//  which is also the note the engine starts on the moment the switch goes back on.
//
//  NO PALETTE OF ITS OWN. The colours come from j37panel::paletteFor() at paint
//  time, exactly as every other draw routine under Source/GUI does, so a theme
//  change recolours the ring on the next frame with nothing to be told. The
//  darkTheme flag only survives to say which of the two the meter ink should
//  follow, the same way the meters take it.
// =============================================================================
#pragma once

#include "GUI/PanelPaint.h"
#include "OscBall.h"
#include "ScaleDegrees.h"

#include <array>
#include <cmath>
#include <functional>

namespace j37panel
{

class OscRing final : public juce::Component,
                      public juce::SettableTooltipClient
{
public:
    OscRing() { rebuildNotes(); }

    /** Re-derives the ring's notes from the deck's key: `tonicSemitone` is 0..11
        (C..B) and `modeIndex` is 0..5, the same two selectors the corrector uses.
        Called from the editor's tick with the live parameter values, so a change
        of TONIC or MODE on the AUTOTUNE tab reaches the ring without a listener.

        When either value actually moves, the notes are rebuilt (7 places per
        octave in MAJOR, 5 in a pentatonic, 12 in CHROMATIC, over C3..B5) and a
        running module retriggers the note under the sector it last hit - the key
        changed, so the pitch has to change with it. The ball itself is not moved
        or thrown: a key change retunes the instrument, it does not interrupt the
        bounce. The next contact plays the new key's note at that angle. */
    void setKey (int tonicSemitone, int modeIndex)
    {
        const auto newTonic = ((tonicSemitone % 12) + 12) % 12;
        const auto newMode = juce::jlimit (0, j37::scale::modeCount - 1, modeIndex);

        if (newTonic == tonic && newMode == mode)
            return;

        tonic = newTonic;
        mode = newMode;
        rebuildNotes();

        currentSector = juce::jlimit (0, noteTotal - 1, currentSector);

        if (running)
            announceLanded();

        repaint();
    }

    /** The MIDI note a sector is, for the engine and for the wire. Clamped, so a
        caller holding a sector from before a key change still gets a note. */
    int noteAt (int sector) const noexcept
    {
        return notes[static_cast<std::size_t> (juce::jlimit (0, noteTotal - 1, sector))];
    }

    /** How many notes the ring currently holds. */
    int noteCount() const noexcept { return noteTotal; }

    /** Advances the ball by `seconds` and fires onLanded() at every wall hit. A
        no-op while the module is off, so an idle ring costs nothing but the call
        itself. The step is fixed inside OscBall, so a frame that arrives late
        does not make the ball move further per second than a frame that is on
        time. */
    void tick (double seconds)
    {
        if (! running)
            return;

        const auto step = (std::isfinite (seconds) && seconds > 0.0)
                            ? static_cast<float> (seconds) : 0.0f;
        const auto result = j37::oscball::advance (ball, step);

        for (int impact = 0; impact < result.impactCount; ++impact)
        {
            const auto& hit = result.impacts[impact];
            currentSector = j37::oscball::sectorForNormal (hit.normalX, hit.normalY,
                                                            juce::jmax (1, noteTotal));
            currentGain = j37::oscball::gainForImpactSpeed (hit.speed);
            announceLanded();
        }

        repaint();
    }

    /** Whether the module runs (the header's OSC switch). Turning it on gives a
        resting ball a shove and hands over the note under it; turning it off
        freezes the ball where it is and stops the announcements. */
    void setRunning (bool shouldRun)
    {
        if (running == shouldRun)
            return;

        running = shouldRun;

        if (running)
        {
            // The key can have moved while the module was off, in which case the
            // engine is still holding the previous note: the tone starts the
            // moment the switch is thrown, so the engine has to be told where the
            // ball is before the first new impact rather than after it.
            if (j37::oscball::speedOf (ball) < 0.05f)
                ball = j37::oscball::State {};

            currentGain = 0.80f;
            announceLanded();
        }

        repaint();
    }

    /** Selects the note the processor is holding without moving the ball. The
        editor calls this with the processor's sector when it is built: the
        engine's last impact is the note a client heard, even if the ball has
        travelled since. The ball's own position comes from setBallState(). */
    void setSector (int sector) noexcept
    {
        currentSector = juce::jlimit (0, juce::jmax (0, noteTotal - 1), sector);
        repaint();
    }

    /** Continues from the physics state the processor was left with. The editor
        calls this before its first frame, so a window opened while the engine was
        bouncing shows the ball where it is rather than at the previous window's
        last position. */
    void setBallState (const j37::oscball::State& state) noexcept
    {
        ball = state;
        j37::oscball::sanitise (ball);
        repaint();
    }

    /** The state to hand back to the processor when this editor goes away. */
    const j37::oscball::State& ballState() const noexcept { return ball; }

    /** Dark-or-light, from the editor's derived flag - the same thing the meters
        take, so the ring's ink matches the panel it stands on. */
    void setDarkTheme (bool isDark) noexcept { darkTheme = isDark; }

    /** The sector the ball last hit, 0..noteCount()-1. The editor seeds the
        processor with noteAt(sector()), so a reopened window and the engine agree. */
    int sector () const noexcept { return currentSector; }

    /** Called with the sector and the impact gain on every wall hit while
        running, and on the two events that move the note without a hit: the
        module being switched on (the engine starts on the note under the ball)
        and the key changing (that note is retuned). The editor's callback is what
        keeps the engine and the OSC wire on the note the ring is showing. */
    std::function<void (int, float)> onLanded;

    /** Called when a press actually throws the ball. The editor hangs the
        interface click on it, so the ring sounds like the switch beside it
        instead of being the one control on the row that answers in silence. */
    std::function<void()> onPress;

    void paint (juce::Graphics& g) override
    {
        const auto& palette = paletteFor (darkTheme);
        const auto bounds = getLocalBounds().toFloat();
        const auto centre = bounds.getCentre();

        // Three pixels of air inside the component, so a ball sitting on the rim
        // still fits inside its own bounds rather than being clipped by the row
        // next to it.
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 3.0f;

        if (radius <= 4.0f)
            return;

        // The disc: the room the ball bounces in. Filled very faintly so the
        // interior reads as a surface rather than as empty space inside a hoop.
        g.setColour (palette.raised.withAlpha (running ? 0.30f : 0.16f));
        g.fillEllipse (centre.x - radius, centre.y - radius,
                       radius * 2.0f, radius * 2.0f);

        // And its edge, which is the scale the notes are arranged on.
        g.setColour (palette.knobEdge.withAlpha (running ? 0.95f : 0.55f));
        g.drawEllipse (centre.x - radius, centre.y - radius,
                       radius * 2.0f, radius * 2.0f, 1.2f);

        // One dot per note of the key. The one the ball is over is drawn larger
        // and in the accent, which is what makes the ring readable as a note
        // SELECTOR rather than as a decorative circle: the highlighted dot is the
        // note.
        //
        // The dots are sized against the spacing rather than fixed, because the
        // count moves with the mode: CHROMATIC over three octaves is 36 dots and
        // a pentatonic is 15, and fixed radii that look right at 15 turn the 36
        // into a solid rim. A live dot keeps a floor so it still reads as the
        // selection at the densest end.
        const auto spacing = juce::MathConstants<float>::twoPi * radius
                           / static_cast<float> (juce::jmax (1, noteTotal));

        for (int sector = 0; sector < noteTotal; ++sector)
        {
            const auto angle = sectorAngle (sector);
            const auto x = centre.x + std::cos (angle) * radius;
            const auto y = centre.y + std::sin (angle) * radius;
            const auto live = running && sector == currentSector;
            const auto dot = live ? juce::jlimit (1.8f, 2.4f, spacing * 0.40f)
                                  : juce::jlimit (0.9f, 1.4f, spacing * 0.26f);

            g.setColour (live ? palette.accent
                              : palette.secondary.withAlpha (running ? 0.85f : 0.5f));
            g.fillEllipse (x - dot, y - dot, dot * 2.0f, dot * 2.0f);
        }

        // The spoke: centre to the live dot. It is the one line that says WHY the
        // ball is on that note - the contact angle is the sector, and the angle is
        // drawn rather than left to be inferred.
        if (running)
        {
            const auto liveAngle = sectorAngle (currentSector);
            g.setColour (palette.accent.withAlpha (0.40f));
            g.drawLine (centre.x, centre.y,
                        centre.x + std::cos (liveAngle) * (radius - 2.0f),
                        centre.y + std::sin (liveAngle) * (radius - 2.0f),
                        1.0f);
        }

        // The ball. Its position is in unit-disc coordinates and the physics keeps
        // its CENTRE inside the circle; the multiplier leaves room for the drawn
        // ball, so at the rim the ball's edge touches the ring rather than
        // crossing it.
        const auto ballRadius = juce::jmax (2.6f, radius * 0.13f);
        const auto ballCentre = centre + juce::Point<float> (ball.x, ball.y)
                                           * juce::jmax (0.0f, radius - ballRadius);
        const auto drawnRadius = ballRadius;

        // The note the ball last hit, so the ring can be read as a pitch and not
        // only as a pattern of dots. Dimmed while the module is off: that contact is
        // the note it last played, and the note the engine starts on when the switch
        // goes back on.
        // Drawn before the ball so the ball always wins the overlap.
        g.setFont (juce::Font (juce::FontOptions (
            juce::Font::getDefaultMonospacedFontName(),
            juce::jmax (7.0f, juce::jmin (11.0f, radius * 0.30f)),
            juce::Font::bold)));
        g.setColour (running ? palette.readout : palette.secondary);
        g.drawText (noteName (noteAt (currentSector)),
                    juce::Rectangle<float> (radius * 0.9f, radius * 0.55f)
                        .withCentre (centre),
                    juce::Justification::centred, false);

        // The shadow: a fixed offset, because the ball is drawn at the disc's own
        // scale rather than lifted towards the viewer.
        g.setColour (palette.knobEdge.withAlpha (0.32f));
        g.fillEllipse (ballCentre.x - drawnRadius + 1.0f,
                       ballCentre.y - drawnRadius + 1.5f,
                       drawnRadius * 2.0f, drawnRadius * 2.0f);

        g.setColour (running ? palette.accent : palette.secondary.withAlpha (0.7f));
        g.fillEllipse (ballCentre.x - drawnRadius, ballCentre.y - drawnRadius,
                       drawnRadius * 2.0f, drawnRadius * 2.0f);

        g.setColour (palette.readout.withAlpha (running ? 0.75f : 0.35f));
        g.fillEllipse (ballCentre.x - drawnRadius * 0.35f, ballCentre.y - drawnRadius * 0.55f,
                       drawnRadius * 0.6f, drawnRadius * 0.6f);
    }

    /** Pressing the ring shoves the ball back towards the middle, so the control
        is not a display the user can only watch. The note sounds at the next wall
        hit, which is what the bounce is for. A press while the module is off does
        nothing - there is no note to choose. */
    void mouseDown (const juce::MouseEvent& event) override
    {
        juce::ignoreUnused (event);

        if (! running)
            return;

        if (onPress)
            onPress();

        // Inward rather than upward: at the rim an upward shove would just be
        // another collision, while an inward one gives the ball a new chord across
        // the disc. A little tangent makes two presses from the same spot differ.
        const auto distance = std::sqrt (ball.x * ball.x + ball.y * ball.y);

        if (distance > 0.05f)
        {
            ball.vx += (-ball.x / distance) * 1.25f;
            ball.vy += (-ball.y / distance) * 1.25f;
        }
        else
        {
            ball.vy -= 1.05f;
        }

        ball.vx += (random.nextFloat() - 0.5f) * 0.60f;
        j37::oscball::sanitise (ball);
        repaint();
    }

private:
    // The register comes from Source/ScaleDegrees.h, which the processor reads
    // too: while the window is closed the engine steps the same ring on the same
    // physics, and the two have to agree note for note. The physics constants
    // themselves live in Source/OscBall.h.
    static constexpr int maxNotes = j37::scale::ringMaxNotes;

    /** Fills `notes` with every semitone in the register that the key contains:
        three octaves of the mode's degrees, ascending. CHROMATIC keeps all 36,
        MAJOR 21, a pentatonic 15 - the count IS the number of places on the ring. */
    void rebuildNotes() noexcept
    {
        noteTotal = j37::scale::ringNoteCount (mode);

        for (int sector = 0; sector < noteTotal; ++sector)
            notes[static_cast<std::size_t> (sector)] = j37::scale::ringNoteAt (tonic, mode, sector);
    }

    // Radians, sector 0 at the top, increasing clockwise - the direction the
    // sectors run, and the direction the spin usually sends the ball, so a walk
    // reads as movement up the scale rather than as a lottery.
    float sectorAngle (int index) const noexcept
    {
        return -juce::MathConstants<float>::halfPi
             + static_cast<float> (index) * (juce::MathConstants<float>::twoPi
                                             / static_cast<float> (juce::jmax (1, noteTotal)));
    }

    // The panel's name for a MIDI note: C, C#, ... B, then the octave number the
    // MIDI convention gives it (60 is C4).
    static juce::String noteName (int midi)
    {
        static constexpr const char* names[12] {
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
        };

        const auto bounded = juce::jlimit (0, 127, midi);
        return juce::String (names[bounded % 12]) + juce::String (bounded / 12 - 1);
    }

    /** Hands the held sector to the client. Only called while running: with the
        module off there is no note sounding, and the editor's callback publishes
        to the wire as well as to the engine. A callback that throws must not
        leave the widget half-updated, so it is contained here the way it used to
        be at each call site. */
    void announceLanded ()
    {
        if (! onLanded)
            return;

        try
        {
            onLanded (currentSector, currentGain);
        }
        catch (...)
        {
            // UI state is already committed; the client's failure is its own.
        }
    }

    // The key the ring is currently drawn for, and the notes it produced. The
    // array is fixed at the chromatic maximum so a mode change is a fill of the
    // same storage rather than an allocation on a timer.
    int tonic = 0;      // 0..11, C..B
    int mode = 0;       // 0..5, see ScaleDegrees.h
    std::array<int, maxNotes> notes {};
    int noteTotal = 0;

    bool running = false;
    bool darkTheme = false;

    int currentSector = 0;
    float currentGain = 0.80f;

    //  UNIT disc coordinates (length 0..1 from the centre) so a resize moves the
    //  ball with the ring instead of leaving it in old pixels. The state carries
    //  its own fixed-step remainder and spin generator; see Source/OscBall.h.
    j37::oscball::State ball;

    //  The press's tangent is the only randomness the widget owns - the bounce's
    //  own spin lives in the physics state, so the processor can reproduce it.
    mutable juce::Random random;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscRing)
};

} // namespace j37panel
