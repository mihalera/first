/*  ============================================================================
    The deck's drifting dust: where it lives, how it moves, and what a frame of
    it dirties.

    WHY THIS IS A HEADER OF ITS OWN, AND WHY IT OWNS THE MOTION. This used to be
    three separate numbers in three files - a corridor set in placeDeck(), a
    world in Box2D bodies created in EditorSetup.cpp, and a box-to-screen map
    retyped in the paint routine and again in the invalidation helper - and the
    one thing none of them could be measured for is the claim that the motes
    actually cross the deck. They could not: the corridor was an 86 x 14 px strip
    at the right end of the badge line, and the velocities were tuned for a
    twelve-pixel band ("the vertical component is deliberately tiny: the band is
    twelve pixels tall"), so six dots drifted along one horizontal line in one
    corner and nothing on the panel said otherwise.

    Everything those three files disagreed about now lives here, once:
      * the corridor a deck rectangle gives the motes;
      * the world they drift in, and the step that moves them;
      * the mapping on screen, which paint() and the invalidator both call - so
        the rectangle that gets dirtied is the rectangle that gets drawn.

    It is JUCE-free and dependency-free on purpose. Box2D could not be used for
    this: JUCE's copy allocates through juce_core, so a harness without JUCE
    cannot step it, and the animation was therefore untestable. Six bodies in a
    box with elastic walls and a drift speed are a page of arithmetic, they run
    identically on every platform, and tests/layout/deck_dust_test.cpp steps
    this exact class frame by frame and measures where the motes land.
  ============================================================================ */

#pragma once

#include <array>
#include <cmath>

namespace j37panel {
namespace dust {

// The deck's dust is a fixed population: six bodies, each with its own radius
// and colour. The count is a layout fact (six read as dust, twelve as noise).
inline constexpr int orbCount = 6;

// The world the motes drift in, in its own units. It is NOT the panel's aspect:
// the mapping below stretches it into whatever corridor the deck has, so the
// world only has to be big enough for the walls to be met.
inline constexpr float worldWidth = 1.5f;
inline constexpr float worldHeight = 1.2f;

// World units per second. 0.045 crosses 1.5 units in about half a minute, which
// is dust settling on a machine rather than a list scrolling past.
inline constexpr float driftSpeed = 0.045f;

// A wall keeps 82 % of the speed it is hit with. The dispenser re-normalises
// below, so a mote never coasts to a stop.
inline constexpr float wallBounce = 0.82f;

// The halo breathes up to 2.4x the mote's radius (see the paint routine); the
// invalidator uses the same widest value, so a halo cannot be drawn outside the
// rectangle that was dirtied for it.
inline constexpr float haloScale = 2.4f;
inline constexpr float smallestRadius = 0.045f;
inline constexpr float radiusStep = 0.012f;
inline constexpr float smallestScreenRadius = 2.0f;
inline constexpr float screenRadiusStep = 1.5f;

// The corridor is the deck's own face, inset by the frame's padding: ten pixels
// a side and six top and bottom. It used to be an 86 x 14 px strip pinned to the
// right end of the badge line, which is what a cluster in one corner looks like.
inline constexpr float corridorSideInset = 10.0f;
inline constexpr float corridorVerticalInset = 6.0f;

// The map keeps the motes' centres this far inside the corridor, so a mote at a
// wall is still a whole dot rather than half an eclipse on the frame.
inline constexpr float corridorEdgeInset = 6.0f;

struct Corridor {
    float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;

    float width() const noexcept { return right - left; }
    float height() const noexcept { return bottom - top; }
};

inline Corridor corridorForDeck(float deckX, float deckY, float deckWidth,
                                float deckHeight) noexcept {
    return {deckX + corridorSideInset, deckY + corridorVerticalInset,
            deckX + deckWidth - corridorSideInset, deckY + deckHeight - corridorVerticalInset};
}

struct Orb {
    float x = 0.0f, y = 0.0f;
    float vx = 0.0f, vy = 0.0f;
    float radius = smallestRadius;
};

struct Rect {
    float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
};

inline float screenX(const Corridor &corridor, float worldX) noexcept {
    const auto left = corridor.left + corridorEdgeInset;
    const auto right = corridor.right - corridorEdgeInset;
    return left + (right - left) * (worldX / worldWidth);
}

inline float screenY(const Corridor &corridor, float worldY) noexcept {
    return corridor.top + (corridor.bottom - corridor.top) * (worldY / worldHeight);
}

inline float screenRadius(float worldRadius) noexcept {
    const auto t = (worldRadius - smallestRadius) / radiusStep;
    const auto clamped = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return smallestScreenRadius + screenRadiusStep * clamped;
}

/** How far from its centre a mote is DRAWN, halo included. */
inline float drawnReach(float worldRadius) noexcept {
    return screenRadius(worldRadius) * haloScale;
}

/** The rectangle one mote dirties: its whole drawn footprint, halo included. */
inline Rect orbInvalidation(const Corridor &corridor, Orb orb) noexcept {
    const auto x = screenX(corridor, orb.x);
    const auto y = screenY(corridor, orb.y);
    const auto reach = drawnReach(orb.radius);
    return {x - reach, y - reach, x + reach, y + reach};
}

/** The six motes and the step that moves them.

    The walls are elastic and the speed is re-normalised to `driftSpeed` whenever
    a bounce has taken it below that, so the motion is constant for as long as the
    panel is open and no wall can quietly stop a mote in a corner. The directions
    are fanned out rather than nearly horizontal: the vertical component was
    tuned to a twelve-pixel band, and in a corridor that is now the whole deck a
    near-zero vy would draw the dust as one horizontal line across the panel.
*/
class World {
  public:
    World() { reset(); }

    void reset() noexcept {
        for (int index = 0; index < orbCount; ++index) {
            const auto i = static_cast<float>(index);
            auto &orb = orbs_[static_cast<std::size_t>(index)];

            // Spread over the world rather than in a row: with the directions
            // below, this is what gets a mote into each corner inside the first
            // few seconds instead of after a lap.
            orb.x = 0.25f + 0.20f * i;
            orb.y = 0.22f + 0.19f * static_cast<float>(index % 3);
            orb.radius = smallestRadius + radiusStep * static_cast<float>(index % 2);

            // Six directions, sixty degrees apart, so both axes move.
            const auto angle = i * (3.14159265358979323846f / 3.0f);
            orb.vx = std::cos(angle) * driftSpeed;
            orb.vy = std::sin(angle) * driftSpeed;
        }
    }

    void step(float seconds) noexcept {
        for (auto &orb : orbs_) {
            orb.x += orb.vx * seconds;
            orb.y += orb.vy * seconds;

            bounce(orb.x, orb.vx, orb.radius, worldWidth);
            bounce(orb.y, orb.vy, orb.radius, worldHeight);

            const auto speed = std::sqrt(orb.vx * orb.vx + orb.vy * orb.vy);
            if (speed > 1.0e-5f && speed < driftSpeed) {
                const auto gain = driftSpeed / speed;
                orb.vx *= gain;
                orb.vy *= gain;
            }
        }
    }

    const std::array<Orb, orbCount> &orbs() const noexcept { return orbs_; }

  private:
    // A wall reverses the component that hit it, and always sends the mote back
    // inside: a reflection that kept the sign would pin it to the wall and the
    // re-normalisation below would then push it straight back out, every frame.
    static void bounce(float &position, float &velocity, float radius,
                       float extent) noexcept {
        if (position - radius < 0.0f) {
            position = radius;
            velocity = std::fabs(velocity) * wallBounce;
        } else if (position + radius > extent) {
            position = extent - radius;
            velocity = -std::fabs(velocity) * wallBounce;
        }
    }

    std::array<Orb, orbCount> orbs_{};
};

} // namespace dust
} // namespace j37panel
