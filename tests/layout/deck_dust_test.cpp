// The deck's dust and the header's badge, measured without a renderer.
//
// Both of these were fixed by eye once already and neither could be proved: the
// motes' corridor was an 86 x 14 px strip at the right end of the badge line, the
// caption on the machine-state pill was pushed 26 px in by a left-only border and
// then left-justified, and no harness on this project could run either of them -
// the dust was six Box2D bodies (JUCE's Box2D allocates through juce_core, so it
// cannot be stepped without JUCE) and the badge's inset was written inline in the
// layout. So "the particles cross the deck" and "the caption is centred" were
// claims, not measurements.
//
// Now both are stated once in a JUCE-free header the shipping code uses -
// Source/GUI/DeckDust.h and Source/GUI/StatusBadge.h - and this file runs them the
// way the panel runs them:
//
//   * the deck's corridor is solved from a deck rectangle, exactly as placeDeck()
//     solves it, and the world is stepped at the editor's timer rate for thirty
//     seconds of animation. Every mote is mapped on screen with the same functions
//     paint() calls, and the spread is measured.
//   * every mote's invalidation rectangle is the one repaintDeckParticles()
//     dirties; it has to contain the mote's whole drawn footprint, halo included,
//     and has to stay inside the deck.
//   * the badge's caption box is the one placeHeaderStatus() sets, measured on the
//     pill the paint routine fills: symmetric margins, a centre on the pill's, and
//     a lamp the text clears.
//
// Every geometric claim is paired with a NEGATIVE CONTROL: the same measurement is
// run against the placement that shipped before the fix, and the check must reject
// it. A check that only ever sees the good case proves nothing - it would still
// pass if the fix were reverted, which is exactly the state this file exists to
// catch.
//
// What this file cannot see: pixels. It measures the geometry the paint routine is
// handed, not the antialiasing of the result.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "GUI/DeckDust.h"
#include "GUI/StatusBadge.h"

namespace dust = j37panel::dust;
namespace badge = j37panel::badge;

namespace {

int failures = 0;

void check(bool ok, const std::string &what) {
    if (!ok) {
        std::printf("  FAIL %s\n", what.c_str());
        ++failures;
    }
}

using Deck = dust::Corridor;

struct DeckRect {
    const char *name;
    float x, y, w, h;
};

// The deck's floor, its preferred size and a wide window. The floor is the size
// that made the strip look like a corner: 236 px of deck, 14 px of corridor.
const std::array<DeckRect, 3> decks {{
    {"floor 936x236", 12.0f, 96.0f, 936.0f, 236.0f},
    {"preferred 1440x278", 12.0f, 96.0f, 1440.0f, 278.0f},
    {"wide 1800x330", 12.0f, 96.0f, 1800.0f, 330.0f},
}};

constexpr float framesPerSecond = 30.0f;
constexpr int animatedFrames = 900; // thirty seconds of panel time

// The corridor the deck used to hand the motes: the right end of the badge line,
// 86 x 14 px. Kept here as the negative control, not as a configuration.
Deck legacyCorridor(const DeckRect &deck) {
    const auto right = deck.x + deck.w;
    const auto badgeTop = deck.y + deck.h - 14.0f;
    return {right - 110.0f, badgeTop + 1.0f, right - 24.0f, badgeTop + 13.0f};
}

struct Sample {
    float x, y;
};

struct Coverage {
    float minX = 1.0e30f, maxX = -1.0e30f;
    float minY = 1.0e30f, maxY = -1.0e30f;
    bool leftHalf = false, rightHalf = false, topHalf = false, bottomHalf = false;
    int samples = 0;

    void add(const Sample &s, const Deck &deck) {
        minX = std::min(minX, s.x);
        maxX = std::max(maxX, s.x);
        minY = std::min(minY, s.y);
        maxY = std::max(maxY, s.y);

        const auto midX = (deck.left + deck.right) * 0.5f;
        const auto midY = (deck.top + deck.bottom) * 0.5f;
        leftHalf = leftHalf || s.x < midX;
        rightHalf = rightHalf || s.x > midX;
        topHalf = topHalf || s.y < midY;
        bottomHalf = bottomHalf || s.y > midY;
        ++samples;
    }

    float spreadX() const { return maxX - minX; }
    float spreadY() const { return maxY - minY; }
};

// Thirty seconds of animation, mapped on screen the way paint() maps it.
Coverage runAnimation(const Deck &corridor, const Deck &deck) {
    dust::World world;
    Coverage coverage;

    for (int frame = 0; frame < animatedFrames; ++frame) {
        world.step(1.0f / framesPerSecond);
        for (const auto &orb : world.orbs())
            coverage.add({dust::screenX(corridor, orb.x), dust::screenY(corridor, orb.y)}, deck);
    }

    return coverage;
}

//  The whole point of the corridor: the motes have to reach the deck's far
//  quarters, not hover along one line. 85 % of the corridor's width and 70 % of
//  its height is the tolerance for "across the deck" - a full traversal is not
//  required, a corner cluster is excluded by a factor of ten.
bool coversCorridor(const Coverage &coverage, const Deck &corridor) {
    const auto needX = 0.85f * corridor.width();
    const auto needY = 0.70f * corridor.height();
    return coverage.spreadX() >= needX && coverage.spreadY() >= needY;
}

void checkDustCoverage() {
    std::printf("\n- the dust crosses the deck\n");

    for (const auto &deckRect : decks) {
        const Deck deck {deckRect.x, deckRect.y, deckRect.x + deckRect.w,
                         deckRect.y + deckRect.h};

        const auto corridor = dust::corridorForDeck(deckRect.x, deckRect.y, deckRect.w,
                                                    deckRect.h);
        const auto coverage = runAnimation(corridor, deck);

        std::printf("  %-18s corridor %.0fx%.0f px, motes span x %.0f..%.0f (%.0f px, "
                    "%.0f%% of corridor), y %.0f..%.0f (%.0f px, %.0f%%) over %d samples\n",
                    deckRect.name, corridor.width(), corridor.height(), coverage.minX,
                    coverage.maxX, coverage.spreadX(),
                    100.0f * coverage.spreadX() / std::max(1.0f, corridor.width()),
                    coverage.minY, coverage.maxY, coverage.spreadY(),
                    100.0f * coverage.spreadY() / std::max(1.0f, corridor.height()),
                    coverage.samples);

        check(corridor.width() >= 0.90f * deckRect.w,
              std::string(deckRect.name) + ": the corridor is the deck, not a strip");
        check(corridor.height() >= 0.90f * deckRect.h,
              std::string(deckRect.name) + ": the corridor is as tall as the deck");

        check(coversCorridor(coverage, corridor),
              std::string(deckRect.name) + ": the motes reach both edges of the corridor");
        check(coverage.leftHalf && coverage.rightHalf,
              std::string(deckRect.name) + ": the motes are in both halves across");
        check(coverage.topHalf && coverage.bottomHalf,
              std::string(deckRect.name) + ": the motes are in both halves down");

        // The negative control: the same animation, mapped into the strip that
        // shipped before the fix. It MUST be rejected, or the checks above are
        // measuring nothing.
        const auto legacy = legacyCorridor(deckRect);
        const auto legacyCoverage = runAnimation(legacy, deck);

        std::printf("  %-18s (old 86x14 strip: motes span %.0f x %.0f px, halves x %d/%d "
                    "y %d/%d)\n",
                    "", legacyCoverage.spreadX(), legacyCoverage.spreadY(),
                    legacyCoverage.leftHalf ? 1 : 0, legacyCoverage.rightHalf ? 1 : 0,
                    legacyCoverage.topHalf ? 1 : 0, legacyCoverage.bottomHalf ? 1 : 0);

        check(!coversCorridor(legacyCoverage, corridor),
              std::string(deckRect.name) +
                  ": the old strip corridor must FAIL the coverage check (negative control)");
        check(!(legacyCoverage.topHalf && legacyCoverage.bottomHalf),
              std::string(deckRect.name) +
                  ": the old strip must FAIL the top/bottom check (negative control)");
    }
}

void checkDustInvalidation() {
    std::printf("\n- what a mote dirties is what a mote draws\n");

    for (const auto &deckRect : decks) {
        const Deck deck {deckRect.x, deckRect.y, deckRect.x + deckRect.w,
                         deckRect.y + deckRect.h};
        const auto corridor = dust::corridorForDeck(deckRect.x, deckRect.y, deckRect.w,
                                                    deckRect.h);

        dust::World world;

        bool containsFootprint = true;
        bool insideDeck = true;
        float widest = 0.0f;
        float tallest = 0.0f;

        for (int frame = 0; frame < animatedFrames; ++frame) {
            world.step(1.0f / framesPerSecond);

            for (const auto &orb : world.orbs()) {
                const auto rect = dust::orbInvalidation(corridor, orb);
                const auto x = dust::screenX(corridor, orb.x);
                const auto y = dust::screenY(corridor, orb.y);
                const auto reach = dust::drawnReach(orb.radius);

                // The drawn circle plus its widest halo, to the pixel.
                if (rect.left > x - reach || rect.right < x + reach || rect.top > y - reach ||
                    rect.bottom < y + reach)
                    containsFootprint = false;

                if (rect.left < deck.left || rect.right > deck.right || rect.top < deck.top ||
                    rect.bottom > deck.bottom)
                    insideDeck = false;

                widest = std::max(widest, rect.right - rect.left);
                tallest = std::max(tallest, rect.bottom - rect.top);
            }
        }

        const auto deckArea = deckRect.w * deckRect.h;
        const auto dirtiedPerFrame = widest * tallest * static_cast<float>(dust::orbCount);

        std::printf("  %-18s invalidation %.0fx%.0f px each, %.2f%% of the deck per frame\n",
                    deckRect.name, widest, tallest, 100.0f * dirtiedPerFrame / deckArea);

        check(containsFootprint,
              std::string(deckRect.name) + ": the invalidation rect contains the drawn halo");
        check(insideDeck, std::string(deckRect.name) + ": the invalidation rect stays in the deck");
        check(dirtiedPerFrame <= 0.05f * deckArea,
              std::string(deckRect.name) +
                  ": six motes dirty a small part of the deck, not the whole corridor");

        // Negative control: a rect around the CENTRE only - what an invalidator
        // that forgot the halo would compute - must be rejected by the containment
        // check.
        const auto orb = world.orbs().front();
        const auto centreX = dust::screenX(corridor, orb.x);
        const auto radius = dust::screenRadius(orb.radius);
        const auto reach = dust::drawnReach(orb.radius);
        const bool centreOnlyContains = centreX - radius <= centreX - reach &&
                                        centreX + radius >= centreX + reach;
        check(!centreOnlyContains, std::string(deckRect.name) +
                                       ": a rect without the halo must FAIL the containment "
                                       "check (negative control)");
    }
}

void checkStatusBadge() {
    std::printf("\n- the state caption is centred on its pill\n");

    constexpr int pillHeights[] = {20, 24, 28};

    for (const auto height : pillHeights) {
        for (int width = 200; width <= 700; width += 50) {
            const badge::Box pill {100, 200, width, height};
            const auto text = badge::textArea(pill);

            const auto leftMargin = text.x - pill.x;
            const auto rightMargin = (pill.x + pill.w) - (text.x + text.w);
            const auto textCentre = text.x + text.w * 0.5f;
            const auto pillCentre = pill.x + pill.w * 0.5f;

            check(leftMargin == rightMargin,
                  "pill " + std::to_string(width) + "x" + std::to_string(height) +
                      ": the caption's margins are symmetric (" + std::to_string(leftMargin) +
                      " left, " + std::to_string(rightMargin) + " right)");
            check(std::abs(textCentre - pillCentre) <= 0.5f,
                  "pill " + std::to_string(width) + "x" + std::to_string(height) +
                      ": the caption's centre is the pill's centre");
            check(text.x >= pill.x + badge::lampRight() + badge::lampClearance,
                  "pill " + std::to_string(width) + "x" + std::to_string(height) +
                      ": the caption clears the lamp");
            check(text.w > 0, "pill " + std::to_string(width) + "x" + std::to_string(height) +
                                  ": the caption has room to be drawn");
        }
    }

    // The negative control: the placement that shipped before the fix - a label
    // spanning the whole pill with a 26 px LEFT-only border and nothing on the
    // right. Its effective text box is asymmetric by the whole right half of the
    // pill, which is the "crooked" the user reported.
    constexpr int legacyBorder = 26;
    const badge::Box pill {100, 200, 420, 24};
    const int legacyLeft = pill.x + badge::pillPadX + legacyBorder;
    const int legacyRight = pill.x + pill.w - badge::pillPadX;
    const auto legacyLeftMargin = legacyLeft - pill.x;
    const auto legacyRightMargin = (pill.x + pill.w) - legacyRight;
    const auto legacyCentre = legacyLeft + (legacyRight - legacyLeft) * 0.5f;

    std::printf("  legacy placement: margins %d/%d px, centre offset %.0f px\n",
                legacyLeftMargin, legacyRightMargin,
                legacyCentre - (pill.x + pill.w * 0.5f));

    check(legacyLeftMargin != legacyRightMargin,
          "the old left-only inset must FAIL the symmetry check (negative control)");
    check(std::abs(legacyCentre - (pill.x + pill.w * 0.5f)) > 0.5f,
          "the old left-only inset must FAIL the centre check (negative control)");

    std::printf("  current placement: margins %d/%d px on a %d px pill\n",
                badge::textArea(pill).x - pill.x,
                (pill.x + pill.w) - (badge::textArea(pill).x + badge::textArea(pill).w),
                pill.w);
}

} // namespace

int main() {
    std::printf("DECK DUST AND STATE BADGE\n");

    checkDustCoverage();
    checkDustInvalidation();
    checkStatusBadge();

    std::printf("\n%s (%d failures)\n", failures == 0 ? "DECK DUST CHECK PASSED"
                                                      : "DECK DUST CHECK FAILED",
                failures);
    return failures == 0 ? 0 : 1;
}
