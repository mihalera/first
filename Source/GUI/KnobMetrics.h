// =============================================================================
//  The knob's radial budget: how much of a cell a knob's rings may take.
//
//  Moved out of PluginEditor.cpp - the prose below and the arithmetic with it -
//  because it is called from two files that no longer share one: the editor's own
//  strip solver in resized(), and J37LookAndFeel::drawRotarySlider, which is
//  Source/GUI/J37LookAndFeel.cpp. A file-local function could not be called from
//  both, and a copy of this one in each would be the drift the layout harness
//  exists to catch - tests/layout now compiles THIS header rather than the text it
//  used to cut out of the editor.
//
//  It does not include JUCE, deliberately, and that is what makes the harness
//  possible: what it uses is juce::jmin, juce::jmax and juce::jlimit, which the
//  plugin's own JuceHeader.h declares and the harness's stub declares in its own
//  words. It is the same arrangement Source/DSP/J37Math.h states for the DSP
//  headers - a header that pulls JUCE in could not be compiled there at all, and a
//  header that can only be compiled by the plugin cannot be measured by a harness.
// =============================================================================
#pragma once

namespace j37panel
{
    struct KnobMetrics
    {
        float paintCeiling = 0.0f;    // half the paint area's short side
        float faceCeiling = 0.0f;     // the largest the cap's face may be
        float radius = 0.0f;          // the cap's face
        float collar = 0.0f;          // the knurled collar around it
        float ring = 0.0f;            // the scale groove's centre line
        float tickGap = 0.0f;         // groove -> a major mark's inner end
        float tickLength = 0.0f;
        float tickClearance = 0.0f;   // what a mark keeps clear of the ceiling
        float minorGap = 0.0f;
        float minorLength = 0.0f;
        float minorClearance = 0.0f;
        int   tickIntervals = 12;
    };

    inline KnobMetrics computeKnobMetrics (float width, float height, float wall) noexcept
    {
        KnobMetrics m;

        const auto shortSide = juce::jmin (width, height);
        const auto halfWidth  = width  * 0.5f - 2.0f;
        const auto halfHeight = height * 0.5f - 2.0f;

        m.paintCeiling = shortSide * 0.5f;
        m.faceCeiling  = shortSide * 0.34f;

        // The marks' own weights, and the clearance each keeps from the ceiling.
        m.tickLength = 5.0f;
        m.tickClearance = 4.0f;
        m.minorLength = 2.5f;
        m.minorClearance = 2.0f;

        // The room a major mark needs, taken off the groove's ceiling BEFORE the
        // groove is solved from it - otherwise the groove grows into the only
        // space the scale has and the marks come out as stubs against it.
        const auto scaleBand = juce::jlimit (3.5f, 6.0f, shortSide * 0.12f);

        // The side wall comes out of the SAME budget as the face: a knob nearer
        // the viewer shows more of its own side and a SMALLER top face, so the cap
        // is what gives way and no knob row marches out of its cells.
        const auto wallFraction = 0.12f + 0.40f * wall;
        const auto collarAllowance = juce::jmax (2.2f, m.faceCeiling * 0.10f);
        const auto ringCeiling = juce::jmin (m.faceCeiling + 8.0f, halfWidth, halfHeight - scaleBand);

        m.radius = juce::jmin (m.faceCeiling,
            juce::jmax (2.0f, (ringCeiling - 1.5f - collarAllowance) / (1.0f + 1.15f * wallFraction)));
        m.collar = m.radius + juce::jmax (2.2f, m.radius * 0.10f);
        m.ring = juce::jmin (m.collar + juce::jmax (3.2f, m.collar * 0.064f), halfWidth, halfHeight);

        // The marks hang off the groove by a share of the groove's own radius, so
        // the scale keeps its shape at every size.
        m.tickGap  = juce::jmax (2.5f, m.ring * 0.08f);
        m.minorGap = m.tickGap + juce::jmax (1.0f, m.ring * 0.02f);

        m.tickIntervals = m.radius > 26.0f ? 24 : 12;

        return m;
    }
}
