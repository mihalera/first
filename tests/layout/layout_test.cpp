// The knob strip's layout, checked without a renderer.
//
// The strip lays a page's knobs out by arithmetic alone: the editor layout
// becomes a grid, the grid becomes a column count and a tile, and the tile
// becomes the disc drawRotarySlider is handed. Every one of those steps is a
// pure function of the panel size, so every one of them can be run and measured
// here - on the panel's four corner sizes, on all eighteen pages, against the
// toolkit's own geometry - without a window, a GPU or a JUCE.
//
// What it checks is what the panel actually shows: that every knob gets a tile
// that stands inside its page and above the member band, that no two tiles
// overlap, that the strip is centred in the space it has, that the disc is
// drawable at the editor's 780 x 664 floor, and - the two that catch a layout
// drifting away from the paint routine - that the tile's height is the solved
// ideal and that the disc the solver scored is the disc the paint routine draws.
//
// The toolkit geometry it measures against, from JUCE 9.0.3:
//   * LookAndFeel_V2::getSliderLayout keeps a 16 px value box at the slider's
//     foot (jmin (getTextBoxHeight(), height - 15)); the knob sliders ask for 16
//     and sit in a tile tall enough to give them the full 16.
//   * Rotaries are neither isHorizontal() nor isVertical() (juce_Slider.cpp), so
//     no thumb indent is taken off either axis.
//   * drawRotarySlider then keeps 6 px a side round itself, and the paint area
//     it solves the cap, the groove and the marks inside is what is left.
// So a tile of w x h becomes a paint area of (w - 22) x (h - 46): the caption
// band inside the cell (18 px) plus the value box (16) plus the 6 px a side (12)
// comes off the height, and 10 px of cell inset plus those 6 px come off the
// width. That is the 46 px of chrome and the 24 px difference the solver is
// written in terms of.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

// ---- the toolkit, stubbed down to what the extracted code touches -----------
namespace j37_stub
{
    template <typename T> T jmin (T a, T b) { return std::min (a, b); }
    template <typename T> T jmin (T a, T b, T c) { return jmin (jmin (a, b), c); }
    template <typename T> T jmax (T a, T b) { return std::max (a, b); }
    template <typename T> T jmax (T a, T b, T c) { return jmax (jmax (a, b), c); }
    template <typename T> T jlimit (T lo, T hi, T v) { return std::min (hi, std::max (lo, v)); }
    inline int roundToInt (float value) { return static_cast<int> (std::lround (value)); }

    template <typename T>
    struct Rect
    {
        T x = 0, y = 0, w = 0, h = 0;
        Rect() = default;
        Rect (T x_, T y_, T w_, T h_) : x (x_), y (y_), w (w_), h (h_) {}
        T getX() const { return x; }
        T getY() const { return y; }
        T getWidth() const { return w; }
        T getHeight() const { return h; }
        T getRight() const { return x + w; }
        T getBottom() const { return y + h; }
        Rect reduced (T d) const { return { x + d, y + d, w - 2 * d, h - 2 * d }; }
        Rect reduced (T dx, T dy) const { return { x + dx, y + dy, w - 2 * dx, h - 2 * dy }; }
        Rect removeFromTop (T d) { auto r = *this; r.h = d; y += d; h -= d; return r; }
        Rect removeFromBottom (T d) { auto r = *this; r.y = y + h - d; r.h = d; h -= d; return r; }
        Rect removeFromRight (T d) { auto r = *this; r.x = x + w - d; r.w = d; w -= d; return r; }
    };
}

namespace juce
{
    using j37_stub::jmin; using j37_stub::jmax; using j37_stub::jlimit;
    using j37_stub::roundToInt;
    template <typename T> using Rectangle = j37_stub::Rect<T>;
}

// ---- the plugin's radial budget, cut verbatim out of Source/ ---------------
namespace extracted
{
#include "metrics.inc"
}
using extracted::computeKnobMetrics;
using extracted::KnobMetrics;

#include "constants.inc"

struct EditorLayout
{
    juce::Rectangle<int> header, deck, controls, meters;
};

// The three things the layout writes its results into: a caption Label, a
// control, and the one field of a tab page the placement reads.
struct StubLabel
{
    juce::Rectangle<int> bounds;
    void setBounds (int x, int y, int w, int h) { bounds = { x, y, w, h }; }
};

struct StubSlider
{
    juce::Rectangle<int> bounds;
    void setBounds (juce::Rectangle<int> r) { bounds = r; }
};

constexpr int stubKnobCount = 8;
StubLabel controlLabels[stubKnobCount];
StubSlider controls[stubKnobCount];

struct ActiveTabStub
{
    std::array<int, stubKnobCount> controls {};
    int count = 0;
};
ActiveTabStub activeTab;

// ---- the plugin's layout chain, cut verbatim out of Source/ ----------------
juce::Rectangle<int> localBounds;
juce::Rectangle<int> getLocalBounds() { return localBounds; }

EditorLayout editorLayoutOf (int W, int H)
{
    localBounds = { 0, 0, W, H };
#include "editor_layout.inc"
}

struct Page { const char* name; int knobs; bool band; };

struct Built
{
    std::vector<juce::Rectangle<int>> cells;     // the tile the layout gave the knob
    std::vector<juce::Rectangle<int>> sliders;   // the rectangle the toolkit paints in
    juce::Rectangle<int> grid;
    int areaHeight = 0, columns = 0, rows = 0;
    float score = 0.0f;
};

Built buildPage (const juce::Rectangle<int>& controlsBand, const Page& page)
{
    const auto tabControlCount = page.knobs;
    const auto memberRows = page.band ? 1 : 0;
    EditorLayout layout;
    layout.controls = controlsBand;

#include "grid.inc"
#include "band.inc"

    activeTab.count = page.knobs;
    for (int i = 0; i < page.knobs; ++i)
        activeTab.controls[static_cast<std::size_t> (i)] = i;

#include "solver.inc"

    // The score the solver settled on, read straight out of its own scope: the
    // check below is that the paint routine draws the disc it promised.
    const auto solvedScore = knobScore;

#include "placement.inc"

    Built built;
    built.grid = grid;
    built.areaHeight = knobAreaHeight;
    built.columns = knobColumns;
    built.rows = knobRows;
    built.score = solvedScore;

    for (int i = 0; i < page.knobs; ++i)
    {
        const auto& slider = controls[i].bounds;
        built.sliders.push_back (slider);
        // Back from the rectangle the toolkit sizes to the TILE the layout placed:
        // the cell reduced by 5, then 8 px of caption clearance off its top.
        built.cells.push_back (juce::Rectangle<int> (slider.getX() - 5, slider.getY() - 13,
                                                     slider.getWidth() + 10,
                                                     slider.getHeight() + 18));
    }

    return built;
}

// The camera, from J37LookAndFeel::perspectiveFor / setPanelCamera(*this).
float wallFor (float absX, float absY, int W, int H)
{
    const float dx = juce::jlimit (-1.0f, 1.0f, (absX - W * 0.5f) / std::max (1.0f, W * 0.5f));
    const float dy = juce::jlimit (-1.0f, 1.0f, (absY - H * 0.5f) / std::max (1.0f, H * 0.5f));
    return juce::jlimit (0.0f, 1.0f, 0.08f + 0.34f * std::max (0.0f, dy) + 0.12f * std::abs (dx));
}

// What drawRotarySlider is handed for one knob: the slider's rectangle less the
// toolkit's value box, less the 6 px it keeps round itself.
KnobMetrics metricsFor (const juce::Rectangle<int>& slider, int W, int H)
{
    const int textBox = std::min (16, slider.getHeight() - 15);
    const float paintW = static_cast<float> (slider.getWidth()) - 12.0f;
    const float paintH = static_cast<float> (slider.getHeight() - textBox) - 12.0f;
    const float centreX = static_cast<float> (slider.getX())
                          + static_cast<float> (slider.getWidth()) * 0.5f;
    const float centreY = static_cast<float> (slider.getY())
                          + static_cast<float> (slider.getHeight() - textBox) * 0.5f;
    return computeKnobMetrics (paintW, paintH, wallFor (centreX, centreY, W, H));
}

// ---- the pages and the panel sizes ----------------------------------------
// Every page's knob count, in the order the tab table declares them, and whether
// the page reserves a member band under its knobs (see the memberRows list in
// resized()).
const Page pages[] = {
    { "MACHINE", 7, true }, { "FRONT END", 5, true }, { "SATURATION", 2, false },
    { "CHARACTER", 3, true }, { "NOISE", 6, false }, { "RECORD", 5, false },
    { "VINYL", 3, false }, { "MIX", 7, true }, { "SUB FUND", 3, false },
    { "DELAY", 4, true }, { "REVERB", 2, false }, { "DYN", 3, false },
    { "OUT EQ", 6, true }, { "IN EQ", 6, true }, { "SHAPERS", 2, true },
    { "SETTINGS", 0, true }, { "AUTOTUNE", 0, false }, { "COMP", 0, false },
};

constexpr int memberBandHeight = 50;

int failures = 0;

void check (bool ok, const char* what, const char* page, int W, int H)
{
    if (! ok)
    {
        std::printf ("  FAIL [%s %dx%d] %s\n", page, W, H, what);
        ++failures;
    }
}

// The strip the page used to ride: four fixed columns of the grid's full rows.
// Kept here to print what the solve bought, page by page - not asserted on.
struct Old { int rows; std::vector<juce::Rectangle<int>> cells; };

Old solveOld (const juce::Rectangle<int>& grid, const Page& page)
{
    constexpr int tabColumns = 4;
    Old old;
    const int rows = (page.knobs + tabColumns - 1) / tabColumns;
    const int area = page.knobs > 0 && page.band ? grid.getHeight() - memberBandHeight
                                                 : grid.getHeight();
    const int rowHeight = rows > 0 ? area / rows : 0;
    const int cellWidth = grid.getWidth() / tabColumns;
    old.rows = rows;

    for (int slot = 0; slot < page.knobs; ++slot)
    {
        const int row = slot / tabColumns, column = slot % tabColumns;
        old.cells.push_back (juce::Rectangle<int> (
            grid.getX() + column * cellWidth,
            grid.getY() + row * rowHeight,
            column == tabColumns - 1
                ? grid.getRight() - (grid.getX() + column * cellWidth) : cellWidth,
            row == rows - 1 && ! page.band
                ? grid.getBottom() - (grid.getY() + row * rowHeight) : rowHeight));
    }

    return old;
}

int main()
{
    // The editor's own resize limits, and two sizes inside them a session is
    // likely to sit at - wide, tall, and the two extremes of the floor.
    const int sizes[][2] = { { 780, 664 }, { 1060, 916 }, { 1200, 823 }, { 1500, 1180 } };

    for (const auto& size : sizes)
    {
        const int W = size[0], H = size[1];
        const auto layout = editorLayoutOf (W, H);
        std::printf ("\n=== %d x %d   (the editor's limits are 780x664 .. 1500x1180)\n", W, H);

        for (const auto& page : pages)
        {
            const auto built = buildPage (layout.controls, page);
            const int band = page.band && page.knobs > 0 ? memberBandHeight : 0;
            const int area = built.grid.getHeight() - band;
            const int areaBottom = built.grid.getBottom() - band;
            const int topAir = built.cells.empty() ? 0 : built.cells[0].getY() - built.grid.getY();
            const int bottomAir = built.cells.empty() ? 0 : areaBottom - built.cells.back().getBottom();

            if (page.knobs == 0)
            {
                std::printf ("  %-9s no knobs\n", page.name);
                continue;
            }

            float disc = 0.0f, smallestDisc = 1.0e9f;
            for (const auto& slider : built.sliders)
            {
                const auto metrics = metricsFor (slider, W, H);
                disc = std::max (disc, metrics.radius * 2.0f);
                smallestDisc = std::min (smallestDisc, metrics.radius * 2.0f);
            }

            const auto old = solveOld (built.grid, page);
            float oldDisc = 0.0f;
            for (const auto& cell : old.cells)
            {
                auto slider = cell.reduced (5);     // the old placement, as it was
                slider.removeFromTop (8);
                oldDisc = std::max (oldDisc, metricsFor (slider, W, H).radius * 2.0f);
            }

            const int rows = (page.knobs + built.columns - 1) / built.columns;
            const int rowHeight = (area - (rows - 1) * knobRowGap) / rows;
            const int tileW = built.cells[0].getWidth(), tileH = built.cells[0].getHeight();
            const float paintWidth = static_cast<float> (tileW) - 22.0f;
            const float painted = computeKnobMetrics (paintWidth,
                                                      static_cast<float> (tileH) - 46.0f, 0.4f).radius;

            std::printf ("  %-9s knobs %d  cols %d rows %d  tile %3dx%-3d  air %3d/%3d"
                         " | disc %5.1f (was %5.1f) %+5.0f%%\n",
                         page.name, page.knobs, built.columns, built.rows, tileW, tileH,
                         topAir, bottomAir, disc, oldDisc,
                         oldDisc > 0.0f ? 100.0f * (disc / oldDisc - 1.0f) : 0.0f);

            // 1. Every knob got a tile; every tile is drawable and stands inside
            //    its page, above the member band; no two of them overlap.
            check (static_cast<int> (built.cells.size()) == page.knobs,
                   "every knob got a tile", page.name, W, H);
            check (smallestDisc >= 2.0f, "every disc is drawable (>= 2 px)", page.name, W, H);
            check (topAir >= 0 && bottomAir >= 0,
                   "no tile stands outside the knob area", page.name, W, H);
            check (std::abs (topAir - bottomAir) <= 1,
                   "the strip is centred in the area", page.name, W, H);
            check (tileH >= tileW + 24 || tileH == rowHeight,
                   "the tile is never shorter than its square form (unless the row is)",
                   page.name, W, H);

            for (std::size_t i = 0; i < built.cells.size(); ++i)
                for (std::size_t j = i + 1; j < built.cells.size(); ++j)
                {
                    const auto& a = built.cells[i];
                    const auto& b = built.cells[j];
                    const bool disjoint = a.getRight() <= b.getX() || b.getRight() <= a.getX()
                                       || a.getBottom() <= b.getY() || b.getBottom() <= a.getY();
                    check (disjoint, "no two tiles overlap", page.name, W, H);
                }

            // 2. The tile's height is the SOLVED one: the smallest at which the
            //    cap stops growing, or the most the row can give. Never wasteful,
            //    never a pixel short of what the row was offering.
            if (tileH < rowHeight)
            {
                const auto taller = computeKnobMetrics (paintWidth,
                                                        static_cast<float> (tileH) - 45.0f, 0.4f).radius;
                check (taller <= painted, "the tile is no taller than its disc needs",
                       page.name, W, H);
            }

            if (tileH > 47)
            {
                const auto shorter = computeKnobMetrics (paintWidth,
                                                         static_cast<float> (tileH) - 47.0f, 0.4f).radius;
                check (shorter < painted || tileH == rowHeight,
                       "the tile is no shorter than its disc needs", page.name, W, H);
            }

            // 3. The disc the fast score promised is the disc the paint draws.
            check (std::abs (built.score - painted) < 0.001f,
                   "the scored disc equals the drawn one", page.name, W, H);
        }
    }

    std::printf ("\n%s (%d failures)\n",
                 failures == 0 ? "LAYOUT CHECK PASSED" : "LAYOUT CHECK FAILED", failures);
    return failures == 0 ? 0 : 1;
}
