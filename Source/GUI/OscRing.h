// =============================================================================
//  OSC on the deck: a circle of twelve notes and a ball that hops INSIDE it.
//
//  WHAT THE WIDGET IS. A disc divided into twelve places - one per semitone of
//  an octave, C4 to B4 - with a ball bouncing from one landing spot to another
//  across the middle of the disc. Where it lands IS the note: the angle of the
//  landing point picks the sector, and the dot for that sector lights up so the
//  mapping is visible rather than implied.
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
//  that is what keeps the twelve notes in a walking order instead of a lottery.
//  Only the point inside the sector's wedge is random.
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

#include <cmath>
#include <functional>

namespace j37panel
{

class OscRing final : public juce::Component,
                      public juce::SettableTooltipClient
{
public:
    OscRing () = default;

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

    /** Whether the module runs (the header's OSC switch). Turning it on starts
        the next hop from wherever the ball is; turning it off settles it on the
        sector it is over and stops the announcements. */
    void setRunning (bool shouldRun)
    {
        if (running == shouldRun)
            return;

        running = shouldRun;

        if (! running && hopping)
            land();

        if (running)
            restLeft = 0.0f;   // the first hop comes straight away

        repaint();
    }

    /** Dark-or-light, from the editor's derived flag - the same thing the meters
        take, so the ring's ink matches the panel it stands on. */
    void setDarkTheme (bool isDark) noexcept { darkTheme = isDark; }

    /** The sector the ball last landed on, 0..11 (C..B). The editor seeds the
        processor with it, so a reopened window and the engine agree. */
    int sector () const noexcept { return currentSector; }

    /** Called with the sector on every landing - only while running. */
    std::function<void (int)> onLanded;

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

        const auto stepAngle = juce::MathConstants<float>::twoPi
                             / static_cast<float> (sectorCount);

        // The twelve sectors. The one the ball is over is drawn larger and in the
        // accent, which is what makes the ring readable as a note SELECTOR rather
        // than as a decorative circle: there are twelve of them, and the
        // highlighted one is the note.
        for (int sector = 0; sector < sectorCount; ++sector)
        {
            const auto angle = sectorAngle (sector);
            const auto x = centre.x + std::cos (angle) * radius;
            const auto y = centre.y + std::sin (angle) * radius;
            const auto live = running && sector == currentSector;
            const auto dot = live ? 2.4f : 1.4f;

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

        // Shown only when there is a note to name: with the module off the ball is
        // parked, and the readout would be claiming a note nothing is playing.
        // Drawn before the ball so the ball always wins the overlap.
        g.setFont (juce::Font (juce::FontOptions (
            juce::Font::getDefaultMonospacedFontName(),
            juce::jmax (7.0f, juce::jmin (11.0f, radius * 0.30f)),
            juce::Font::bold)));
        g.setColour (running ? palette.readout : palette.secondary);
        g.drawText (sectorName (currentSector),
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

        if (hopping)
            land();

        beginHop();
        repaint();
    }

private:
    static constexpr int sectorCount = 12;
    static constexpr double hopSeconds = 0.30;   // one hop, spot to spot
    static constexpr double restSeconds = 0.55;  // the pause it lands into

    // Radians, sector 0 at the top, increasing clockwise - which is the direction
    // the ball always travels, so a hop reads as movement forward through the
    // octave and never as the ball teleporting back behind itself.
    static float sectorAngle (int index) noexcept
    {
        return -juce::MathConstants<float>::halfPi
             + static_cast<float> (index) * (juce::MathConstants<float>::twoPi
                                             / static_cast<float> (sectorCount));
    }

    // The panel's name for a sector: C, C#, ... B. The octave is fixed at 4
    // because the twelve sectors ARE one octave, C4 to B4.
    static const char* sectorName (int index) noexcept
    {
        static constexpr const char* names[sectorCount] {
            "C4", "C#4", "D4", "D#4", "E4", "F4",
            "F#4", "G4", "G#4", "A4", "A#4", "B4"
        };
        return names[juce::jlimit (0, sectorCount - 1, index)];
    }

    /** Where inside `sector` the ball comes down: within the sector's own wedge,
        at a radius that is always clear of the note readout in the middle. */
    juce::Point<float> landingSpot (int sector) const noexcept
    {
        // The wedge is two steps wide, so a spread of a third of a step keeps the
        // landing point unmistakably inside it while still moving from hop to hop.
        const auto halfStep = juce::MathConstants<float>::pi / static_cast<float> (sectorCount);
        const auto angle = sectorAngle (sector) + random.nextFloat() * halfStep * 0.66f
                                              - halfStep * 0.33f;

        // 0.62 .. 0.84 of the radius: outside the text, inside the rim, and never
        // the same distance twice, so two hops to neighbouring sectors are not the
        // same short step drawn at different angles.
        const auto distance = 0.62f + 0.22f * random.nextFloat();

        return juce::Point<float> (std::cos (angle), std::sin (angle)) * distance;
    }

    void beginHop ()
    {
        // Between one and four sectors forward, never zero and never backwards:
        // the ball walks the octave the way a ball is bounced around a room, and a
        // random number of steps is what stops it looking like a metronome.
        hopFrom = ballAtRest;
        hopSteps = 1 + random.nextInt (4);
        hopToSector = (currentSector + hopSteps) % sectorCount;
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

        if (running && onLanded)
        {
            try
            {
                onLanded (currentSector);
            }
            catch (...)
            {
                // A client callback must not interrupt the component's landing
                // transition or its repaint; UI state is already committed above.
            }
        }

        repaint();
    }

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
