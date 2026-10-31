// =============================================================================
//  OSC on the deck: a circle of the key's notes and a ball that hops INSIDE it.
//
//  WHAT THE WIDGET IS. A disc divided into one place per note of the selected
//  KEY - the degrees of TONIC and MODE (the AUTOTUNE tab's two selectors) across
//  three octaves, C3 to B5 - with a ball bouncing from one landing spot to
//  another across the middle of the disc. Where it lands IS the note: the angle
//  of the landing point picks the place, and the dot for it lights up so the
//  mapping is visible rather than implied.
//
//  WHY THE NOTES FOLLOW THE KEY. The generator enters the machine at INPUT and is
//  heard through whatever the deck is set to, so it is a musical source rather
//  than a test tone: twelve chromatic sectors would put it in C whatever the user
//  is playing in, and every landing on an out-of-key semitone would be the one
//  thing the rest of the panel spends a whole tab avoiding. The ring's notes are
//  therefore the scale's own degrees - seven places per octave in MAJOR, five in
//  a pentatonic, twelve in CHROMATIC - and a change of TONIC or MODE re-derives
//  them and retunes the note the ball is resting on. Source/ScaleDegrees.h is the
//  table the pitch corrector uses too, so the corrector and the generator cannot
//  end up in two different keys.
//
//  WHY THREE OCTAVES. One octave was a ring that could only say the same eleven
//  intervals in a circle; the ball now travels from C3 to B5, so a run of hops is
//  a phrase with a register as well as a pitch, and the same key sounds different
//  depending on where in the range the ball happens to be.
//
//  WHY THE BALL MOVES THROUGH THE DISC RATHER THAN AROUND ITS RIM. A ball that
//  walks a circle is a clock: the same eighteen millimetres, the same way, at
//  the same speed, and the eye stops reading it after two hops. A ball that
//  lands somewhere inside the disc and is thrown to another point inside it
//  travels a different distance every time, crosses the middle, and reads as
//  something being bounced - which is what the module is: a note picked by where
//  the ball comes down, not a pointer being advanced. The landing RADIUS varies
//  as well as the angle, so the hops are chords of different lengths rather than
//  equal steps, and a hop from two o'clock to ten o'clock cuts straight through
//  the middle of the disc.
//
//  The landing point is still chosen by SECTOR, though - one to four sectors
//  clockwise from where the ball is, never backwards and never zero - because
//  that is what keeps the notes in a walking order instead of a lottery. Only the
//  point inside the sector's wedge is random.
//
//  WHY A CALL FROM THE EDITOR'S TIMER RATHER THAN A TIMER OF ITS OWN. The
//  editor already ticks thirty times a second and repaints in a bounded way, so
//  the ring is advanced by that tick (tick()). A second juce::Timer would be a
//  second clock to keep in step with the first, for an animation that does not
//  deserve one.
//
//  WHAT THE SWITCH DOES TO IT. OSC off parks the ball on the spot it is over and
//  stops the announcements: the module is not generating, so a ball hopping to
//  notes nobody hears would be a widget out of step with the machine. The ball
//  is not hidden and the ring is not greyed out of existence - it rests on the
//  note it last played, which is also the note the engine will start on the
//  moment the switch goes back on.
//
//  NO PALETTE OF ITS OWN. The colours come from j37panel::paletteFor() at paint
//  time, exactly as every other draw routine under Source/GUI does, so a theme
//  change recolours the ring on the next frame with nothing to be told. The
//  darkTheme flag only survives to say which of the two the meter ink should
//  follow, the same way the meters take it.
// =============================================================================
#pragma once

#include "GUI/PanelPaint.h"
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
        octave in MAJOR, 5 in a pentatonic, 12 in CHROMATIC, over C3..B5), the
        ball is re-seated on the sector it is holding, and a running module
        retriggers that note - the key changed, so the pitch underneath the held
        ball has to change with it. */
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
        hopToSector = juce::jlimit (0, noteTotal - 1, hopToSector);
        hopping = false;
        hopElapsed = 0.0f;
        ballAtRest = landingSpot (currentSector);

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

    /** Advances the hop by `seconds` and fires onLanded() the moment the ball
        arrives. A no-op while the module is off, so an idle ring costs nothing
        but the call itself. */
    void tick (double seconds)
    {
        const auto safeSeconds = std::isfinite (seconds) ? seconds : 0.0;
        const auto step = static_cast<float> (juce::jmax (0.0, juce::jmin (0.25, safeSeconds)));

        if (! running)
        {
            // Settle rather than freeze mid-air: a switch thrown during a hop
            // lands the ball where it was heading, so the panel stops on a real
            // sector instead of on a half-finished one.
            if (hopping)
                land();

            return;
        }

        if (hopping)
        {
            hopElapsed += step;

            if (hopElapsed >= static_cast<float> (hopSeconds))
            {
                land();
                restLeft = static_cast<float> (restSeconds);
                repaint();
            }
            else
            {
                repaint();
            }

            return;
        }

        restLeft -= step;

        if (restLeft <= 0.0f)
        {
            beginHop();
            repaint();
        }
    }

    /** Whether the module runs (the header's OSC switch). Turning it on hands
        over the note the ball is parked on and starts the next hop from there;
        turning it off settles it on the sector it is over and stops the
        announcements. */
    void setRunning (bool shouldRun)
    {
        if (running == shouldRun)
            return;

        running = shouldRun;

        if (! running && hopping)
            land();

        if (running)
        {
            // The key can have moved while the module was off, in which case the
            // engine is still holding the previous note: the tone starts the
            // moment the switch is thrown, so the engine has to be told where the
            // ball is parked before the first hop rather than after it.
            announceLanded();
            restLeft = 0.0f;   // the first hop comes straight away
        }

        repaint();
    }

    /** Dark-or-light, from the editor's derived flag - the same thing the meters
        take, so the ring's ink matches the panel it stands on. */
    void setDarkTheme (bool isDark) noexcept { darkTheme = isDark; }

    /** The sector the ball last landed on, 0..noteCount()-1. The editor seeds the
        processor with noteAt(sector()), so a reopened window and the engine agree. */
    int sector () const noexcept { return currentSector; }

    /** Called with the sector on every landing while running, and on the two
        events that move the note without a hop: the module being switched on
        (the engine starts on the parked note) and the key changing (the parked
        note is retuned). The editor's callback is what keeps the engine and the
        OSC wire on the sector the ring is showing. */
    std::function<void (int)> onLanded;

    /** Called when a press actually throws the ball. The editor hangs the
        interface click on it, so the ring sounds like the switch beside it
        instead of being the one control on the row that answers in silence. */
    std::function<void()> onPress;

    void paint (juce::Graphics& g) override
    {
        const auto& palette = paletteFor (darkTheme);
        const auto bounds = getLocalBounds().toFloat();
        const auto centre = bounds.getCentre();

        // Three pixels of air inside the component, so the ball at the top of its
        // hop - which grows as it rises - still fits inside its own bounds rather
        // than being clipped by the row next to it.
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
        // ball is on that note - the landing point's angle is the sector, and the
        // angle is drawn rather than left to be inferred.
        if (running)
        {
            const auto liveAngle = sectorAngle (currentSector);
            g.setColour (palette.accent.withAlpha (hopping ? 0.28f : 0.45f));
            g.drawLine (centre.x, centre.y,
                        centre.x + std::cos (liveAngle) * (radius - 2.0f),
                        centre.y + std::sin (liveAngle) * (radius - 2.0f),
                        1.0f);
        }

        // The ball's position. While it hops it travels a straight chord between
        // the two landing spots - through the middle of the disc, and by a
        // different distance every time - and LIFTS, which is drawn as the ball
        // growing with its shadow falling further away from it. That is what makes
        // the motion read as a bounce rather than as a token being slid.
        const auto ballRadius = juce::jmax (2.6f, radius * 0.13f);
        auto ballAt = hopping ? hopFrom : ballAtRest;
        float lift = 0.0f;

        if (hopping)
        {
            const auto t = juce::jlimit (0.0f, 1.0f,
                                         hopElapsed / static_cast<float> (hopSeconds));
            const auto eased = t * t * (3.0f - 2.0f * t);   // smoothstep: no jolt at either end
            ballAt = hopFrom + (hopTo - hopFrom) * eased;
            lift = std::sin (t * juce::MathConstants<float>::pi);
        }

        const auto ballCentre = centre + ballAt * radius;
        const auto drawnRadius = ballRadius * (1.0f + 0.45f * lift);

        // The note the ball is resting on, so the ring can be read as a pitch and
        // not only as a pattern of dots. Dimmed while the module is off: the ball
        // is parked on the note it last played, and that note is what the engine
        // starts on when the switch goes back on.
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

        // The shadow, offset further down the higher the ball is.
        g.setColour (palette.knobEdge.withAlpha (0.35f - 0.18f * lift));
        g.fillEllipse (ballCentre.x - drawnRadius + 1.0f,
                       ballCentre.y - drawnRadius + 1.5f + 3.0f * lift,
                       drawnRadius * 2.0f, drawnRadius * 2.0f);

        g.setColour (running ? palette.accent : palette.secondary.withAlpha (0.7f));
        g.fillEllipse (ballCentre.x - drawnRadius, ballCentre.y - drawnRadius,
                       drawnRadius * 2.0f, drawnRadius * 2.0f);

        g.setColour (palette.readout.withAlpha (running ? 0.75f : 0.35f));
        g.fillEllipse (ballCentre.x - drawnRadius * 0.35f, ballCentre.y - drawnRadius * 0.55f,
                       drawnRadius * 0.6f, drawnRadius * 0.6f);
    }

    /** Pressing the ring throws the ball straight into its next hop, so the
        control is not a display the user can only watch. A press while the
        module is off does nothing - there is no note to choose. */
    void mouseDown (const juce::MouseEvent& event) override
    {
        juce::ignoreUnused (event);

        if (! running)
            return;

        if (onPress)
            onPress();

        if (hopping)
            land();

        beginHop();
        repaint();
    }

private:
    // The ring's register: C3 to B5 inclusive. Fixed in absolute MIDI rather than
    // "three octaves up from the tonic", because a range that climbed with the key
    // would put B major's top note near 1.9 kHz and C major's near 1 kHz - the same
    // control sounding like two different instruments depending on the key.
    static constexpr int lowestMidi = 48;                                  // C3
    static constexpr int highestMidi = 83;                                 // B5
    static constexpr int maxNotes = highestMidi - lowestMidi + 1;          // 36

    static constexpr double hopSeconds = 0.30;   // one hop, spot to spot
    static constexpr double restSeconds = 0.55;  // the pause it lands into

    /** Fills `notes` with every semitone in the register that the key contains:
        three octaves of the mode's degrees, ascending. CHROMATIC keeps all 36,
        MAJOR 21, a pentatonic 15 - the count IS the number of places on the ring. */
    void rebuildNotes() noexcept
    {
        noteTotal = 0;

        for (int midi = lowestMidi; midi <= highestMidi; ++midi)
            if (j37::scale::contains (mode, midi - tonic))
                notes[static_cast<std::size_t> (noteTotal++)] = midi;

        // Every mode in the table has at least five degrees, so this cannot fire
        // with three octaves of room; it is the guard for a table edited down.
        if (noteTotal == 0)
        {
            notes[0] = lowestMidi;
            noteTotal = 1;
        }
    }

    // Radians, sector 0 at the top, increasing clockwise - which is the direction
    // the ball always travels, so a hop reads as movement up the scale and never
    // as the ball teleporting back behind itself.
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

    /** Where inside `sector` the ball comes down: within the sector's own wedge,
        at a radius that is always clear of the note readout in the middle. */
    juce::Point<float> landingSpot (int sector) const noexcept
    {
        // A spread over the middle third of the wedge keeps the landing point
        // unmistakably inside its sector while still moving from hop to hop.
        const auto step = juce::MathConstants<float>::twoPi
                        / static_cast<float> (juce::jmax (1, noteTotal));
        const auto angle = sectorAngle (sector) + random.nextFloat() * step * 0.33f
                                              - step * 0.165f;

        // 0.62 .. 0.84 of the radius: outside the text, inside the rim, and never
        // the same distance twice, so two hops to neighbouring sectors are not the
        // same short step drawn at different angles.
        const auto distance = 0.62f + 0.22f * random.nextFloat();

        return juce::Point<float> (std::cos (angle), std::sin (angle)) * distance;
    }

    void beginHop ()
    {
        // Between one and four degrees forward, never zero and never backwards:
        // the ball walks the scale the way a ball is bounced around a room, and a
        // random number of steps is what stops it looking like a metronome.
        hopFrom = ballAtRest;
        hopSteps = 1 + random.nextInt (4);
        hopToSector = (currentSector + hopSteps) % juce::jmax (1, noteTotal);
        hopTo = landingSpot (hopToSector);
        hopElapsed = 0.0f;
        hopping = true;
    }

    void land ()
    {
        currentSector = hopToSector;
        ballAtRest = hopTo;
        hopping = false;
        hopElapsed = 0.0f;

        if (running)
            announceLanded();

        repaint();
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
            onLanded (currentSector);
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
    bool hopping = false;

    int currentSector = 0;
    int hopSteps = 1;
    int hopToSector = 1;

    // Positions are in UNIT disc coordinates (length 0..1 from the centre) so a
    // resize moves the ball with the ring instead of leaving it in old pixels.
    juce::Point<float> ballAtRest { 0.0f, -0.74f };
    juce::Point<float> hopFrom { 0.0f, -0.74f };
    juce::Point<float> hopTo { 0.0f, -0.74f };

    float hopElapsed = 0.0f;
    float restLeft = 0.0f;

    //  mutable because landingSpot() is const and still has to draw the landing
    //  point's angle and radius from it - the widget's appearance is const, the
    //  spot it picks is not.
    mutable juce::Random random;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscRing)
};

} // namespace j37panel
