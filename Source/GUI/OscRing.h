// =============================================================================
//  OSC on the deck: a ring of twelve sectors and a ball that hops around it.
//
//  WHAT THE WIDGET IS. A circle drawn as a ring, divided into twelve places by
//  twelve dots, with a ball travelling clockwise from dot to dot in little hops.
//  Where it lands IS the note: sector 0 is C, sector 11 is B, all twelve inside
//  one octave starting at C4. The landing is the only event this class produces
//  - `onLanded` fires with the sector - and everything else on the panel (the
//  oscillator's pitch, the envelope's retrigger, the packet that goes out over
//  OSC) hangs off that one call.
//
//  WHY A CALL FROM THE EDITOR'S TIMER RATHER THAN A TIMER OF ITS OWN. The
//  editor already ticks thirty times a second and repaints in a bounded way, so
//  the ring is advanced by that tick (tick()). A second juce::Timer would be a
//  second clock to keep in step with the first, for an animation that does not
//  deserve one.
//
//  WHAT THE SWITCH DOES TO IT. OSC off parks the ball on the sector it is over
//  and stops the announcements: the module is not generating, so a ball hopping
//  to notes nobody hears would be a widget out of step with the machine. The
//  ball is not hidden and the ring is not greyed out of existence - it rests on
//  the note it last played, which is also the note the engine will start on the
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
        // hop - which lifts it OFF the ring - still lands inside its own bounds
        // rather than being clipped by the row next to it.
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 3.0f;

        if (radius <= 4.0f)
            return;

        // The ring itself: the track the ball walks. Dimmer while the module is
        // off, but never invisible - it is a dial on a front panel, not a hidden
        // state.
        g.setColour (palette.knobEdge.withAlpha (running ? 0.95f : 0.55f));
        g.drawEllipse (centre.x - radius, centre.y - radius,
                       radius * 2.0f, radius * 2.0f, 1.2f);

        // The twelve sectors. The one the ball rests on is drawn larger and in
        // the accent, which is what makes the ring readable as a note SELECTOR
        // rather than as a decorative circle: there are twelve of them, and the
        // highlighted one is the note.
        for (int sector = 0; sector < sectorCount; ++sector)
        {
            const auto angle = sectorAngle (sector);
            const auto x = centre.x + std::cos (angle) * radius;
            const auto y = centre.y + std::sin (angle) * radius;
            const auto live = running && sector == currentSector && ! hopping;
            const auto dot = live ? 2.6f : 1.5f;

            g.setColour (live ? palette.accent
                              : palette.secondary.withAlpha (running ? 0.85f : 0.5f));
            g.fillEllipse (x - dot, y - dot, dot * 2.0f, dot * 2.0f);
        }

        // The note the ring is currently on, in the middle where there is room
        // for three characters and nothing else. Monospaced because it changes
        // while the panel is watched and a proportional "C#4" -> "D4" would make
        // the text shuffle sideways on every landing.
        g.setFont (juce::Font (juce::FontOptions (
            juce::Font::getDefaultMonospacedFontName(),
            juce::jmax (7.0f, juce::jmin (12.0f, radius * 0.34f)),
            juce::Font::bold)));
        g.setColour (running ? palette.readout : palette.secondary);
        g.drawText (sectorName (currentSector),
                    juce::Rectangle<float> (radius * 1.1f, radius * 0.7f)
                        .withCentre (centre),
                    juce::Justification::centred, false);

        // The ball. While it hops it rides the ring and lifts off it by a tenth
        // of the radius at the top of the arc - which is the whole difference
        // between "hopping along the circle" and "sliding around it".
        const auto ballRadius = juce::jmax (3.0f, radius * 0.14f);
        float angle = sectorAngle (currentSector);
        float lift = 0.0f;

        if (hopping)
        {
            const auto t = juce::jlimit (0.0f, 1.0f,
                                         hopElapsed / static_cast<float> (hopSeconds));
            angle = sectorAngle (hopFrom)
                  + static_cast<float> (hopSteps) * (juce::MathConstants<float>::twoPi
                                                     / static_cast<float> (sectorCount)) * t;
            lift = std::sin (t * juce::MathConstants<float>::pi) * radius * 0.10f;
        }

        const auto ballCentre = juce::Point<float> (
            centre.x + std::cos (angle) * (radius + lift),
            centre.y + std::sin (angle) * (radius + lift));

        // A shadow under the ball first, so it reads as standing above the ring
        // rather than printed on it - the same cue the panel's knobs use for
        // their caps.
        g.setColour (palette.knobEdge.withAlpha (0.35f));
        g.fillEllipse (ballCentre.x - ballRadius + 1.0f, ballCentre.y - ballRadius + 1.5f,
                       ballRadius * 2.0f, ballRadius * 2.0f);

        g.setColour (running ? palette.accent : palette.secondary.withAlpha (0.7f));
        g.fillEllipse (ballCentre.x - ballRadius, ballCentre.y - ballRadius,
                       ballRadius * 2.0f, ballRadius * 2.0f);

        g.setColour (palette.readout.withAlpha (running ? 0.75f : 0.35f));
        g.fillEllipse (ballCentre.x - ballRadius * 0.35f, ballCentre.y - ballRadius * 0.55f,
                       ballRadius * 0.6f, ballRadius * 0.6f);
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
    static constexpr double hopSeconds = 0.30;   // one hop, dot to dot
    static constexpr double restSeconds = 0.55;  // the pause it lands into

    // Radians, sector 0 at the top, increasing clockwise - which is the direction
    // the ball always travels, so a hop reads as movement around the ring and
    // never as the ball teleporting across it.
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

    void beginHop ()
    {
        // Between one and four sectors forward, never zero and never backwards:
        // the ball walks the ring the way a ball is bounced around a rim, and a
        // random number of steps is what stops it looking like a metronome.
        hopFrom = currentSector;
        hopSteps = 1 + random.nextInt (4);
        hopElapsed = 0.0f;
        hopping = true;
    }

    void land ()
    {
        currentSector = (hopFrom + hopSteps) % sectorCount;
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
    int hopFrom = 0;
    int hopSteps = 1;

    float hopElapsed = 0.0f;
    float restLeft = 0.0f;

    juce::Random random;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscRing)
};

} // namespace j37panel
