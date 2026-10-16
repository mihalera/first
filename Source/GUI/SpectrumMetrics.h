// =============================================================================
//  The arithmetic the SHAPERS page's spectrum curve is drawn with, and nothing
//  else: no JUCE, no component, no colour.
//
//  It is a header of its own - rather than twenty lines inside paint() - for the
//  reason Source/GUI/KnobMetrics.h is one: the numbers a panel draws are the part
//  of a panel that can be CHECKED without a window. tests/layout compiles this
//  file with no JUCE at all and asserts the properties below, because the failure
//  this file can have is not a crash and not a compiler error - it is a curve that
//  is smooth, plausible and upside down.
//
//  The three claims, and why each one is worth pinning:
//
//    1. THE VERTICAL AXIS IS A LEVEL. floorDb maps to the BOTTOM of the box and
//       ceilingDb to the TOP, so a quiet band is drawn low and a loud one high.
//       Inverted, the curve reads as energy where there is none - which looks
//       exactly as convincing as the right way up.
//    2. IT IS CLAMPED, NOT SCALED. A reading below the floor is drawn ON the floor
//       and one above the ceiling on the ceiling, so a single very loud band
//       cannot squash every other band into the bottom pixel row. That is the
//       difference between a display and a normaliser.
//    3. THE POINTS ARE INSIDE THE BOX, IN ORDER. x rises with the band index and
//       every point lies strictly within the box, so the polyline cannot draw
//       outside its cell however narrow the panel gets - and the half-step inset
//       is what keeps the stroke's own width from being clipped at both edges.
//
//  It is deliberately called a metric rather than a view: what to draw is here,
//  what colour to draw it in is the editor's palette, and when to repaint is the
//  timer's business.
// =============================================================================
#pragma once

#include <array>
#include <cstddef>

namespace j37spectrum
{
/** The bottom of the drawn axis, in dBFS. Below the engine's own noise floor, so
    the curve's floor is never reached by programme and reads as "nothing here". */
inline constexpr float floorDb = -72.0f;

/** The top of the drawn axis: full scale, which is as loud as the frame's own
    readings can be. */
inline constexpr float ceilingDb = 0.0f;

struct Point
{
    float x = 0.0f;
    float y = 0.0f;
};

/** Where a level sits in a box `height` tall: the ceiling at the top edge, the
    floor at the bottom, and anything outside the two drawn ON an edge rather than
    past it (claim 2). */
inline float levelToY (float db, float height) noexcept
{
    const auto clamped = db < floorDb ? floorDb : (db > ceilingDb ? ceilingDb : db);
    return height * (ceilingDb - clamped) / (ceilingDb - floorDb);
}

/**
    One point per band, inside a box `width` x `height` whose top-left corner is
    the origin.

    The bands are already log-spaced (Source/DSP/SpectrumAnalyser.h builds its
    ladder as a geometric series), so EVEN x steps are correct: equal steps in the
    band index are equal frequency ratios, which is what a spectrum axis means. A
    second log mapping here would double the curve's own compression and pile the
    top half of the panel into the last few pixels.

    x is inset by half a step at both ends rather than running edge to edge: a
    stroke has width, and a point 0.5 px inside the edge still overhangs it once
    the line is drawn through it.
*/
template <std::size_t N>
inline std::array<Point, N> curve (const std::array<float, N>& bandsDb,
                                   float width, float height) noexcept
{
    std::array<Point, N> points {};

    if (N == 0)
        return points;

    const auto step = width / static_cast<float> (N);

    for (std::size_t i = 0; i < N; ++i)
        points[i] = { (static_cast<float> (i) + 0.5f) * step, levelToY (bandsDb[i], height) };

    return points;
}
} // namespace j37spectrum
