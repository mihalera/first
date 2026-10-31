// =============================================================================
//  The six scales the panel offers, stated once.
//
//  Two parts of the plugin read this table: the pitch corrector, which snaps a
//  reading to the nearest degree, and the OSC ring, which lays those degrees out
//  as the notes its ball can land on. It used to live inside
//  ProcessorSignalPath.cpp, where the ring could not reach it - and a second copy
//  is how the corrector and the generator end up in different keys while both
//  claim the same TONIC and MODE.
//
//  A mode is twelve slots: the semitone offsets from the tonic, ascending, with
//  -1 marking the end of the scale. CHROMATIC fills all twelve, which is why the
//  sentinel exists rather than a count: the shape of the table is the same for
//  every mode, and a caller walking it stops on the first -1.
//
//  No JUCE here on purpose: this is an array and a few loops, so a harness can
//  include it without a toolkit.
// =============================================================================
#pragma once

#include <array>
#include <cstddef>

namespace j37
{
namespace scale
{

inline constexpr int modeCount = 6;

/** Semitone offsets from the tonic, ascending, -1 past the last degree. */
inline constexpr std::array<std::array<int, 12>, modeCount> table {{
    {{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }},      // chromatic
    {{ 0, 2, 4, 5, 7, 9, 11, -1, -1, -1, -1, -1 }},   // major
    {{ 0, 2, 3, 5, 7, 8, 10, -1, -1, -1, -1, -1 }},   // minor
    {{ 0, 2, 3, 5, 7, 9, 10, -1, -1, -1, -1, -1 }},   // dorian
    {{ 0, 2, 4, 7, 9, -1, -1, -1, -1, -1, -1, -1 }},  // pentatonic major
    {{ 0, 3, 5, 7, 10, -1, -1, -1, -1, -1, -1, -1 }}  // pentatonic minor
}};

/** The mode's twelve slots, clamped to a mode the table actually holds. */
inline const std::array<int, 12>& offsets (int modeIndex) noexcept
{
    const auto clamped = modeIndex < 0 ? 0
                       : modeIndex >= modeCount ? modeCount - 1
                                                : modeIndex;
    return table[static_cast<std::size_t> (clamped)];
}

/** Whether `semitoneFromTonic` (any integer) is one of the mode's degrees. */
inline bool contains (int modeIndex, int semitoneFromTonic) noexcept
{
    const auto normalised = ((semitoneFromTonic % 12) + 12) % 12;
    const auto& degrees = offsets (modeIndex);

    for (int slot = 0; slot < 12; ++slot)
    {
        const auto degree = degrees[static_cast<std::size_t> (slot)];

        if (degree < 0)
            break;

        if (degree == normalised)
            return true;
    }

    return false;
}

} // namespace scale
} // namespace j37
