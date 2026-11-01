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

// -----------------------------------------------------------------------------
//  The OSC ring's register and its two timing constants.
//
//  They live here, beside the table that decides WHICH notes the ring holds,
//  because two clocks lay the ring out now: the editor's ball while the panel is
//  on screen, and the engine's own hop when the window is closed (the sequence
//  must not stop with the window - see setOscPanelOpen in PluginProcessor.h). If
//  either side owned the range or the timings, the note the panel shows and the
//  note the engine plays could disagree, which is the one thing a note selector
//  may never do.
//
//  C3 to B5, fixed in absolute MIDI rather than "three octaves up from the
//  tonic": a range that climbed with the key would put B major's top note near
//  1.9 kHz and C major's near 1 kHz - the same control sounding like two
//  different instruments depending on the key.
// -----------------------------------------------------------------------------
inline constexpr int ringLowestMidi = 48;                 // C3
inline constexpr int ringHighestMidi = 83;                // B5
inline constexpr int ringMaxNotes = ringHighestMidi - ringLowestMidi + 1;   // 36

inline constexpr double ringHopSeconds = 0.30;            // one hop, spot to spot
inline constexpr double ringRestSeconds = 0.55;           // the pause it lands into

/** How many places the ring holds for a mode: 36 in CHROMATIC, 21 in each of the
    seven-note modes (MAJOR, MINOR, DORIAN), 15 in either pentatonic. The register
    is exactly three octaves, so every pitch class appears three times and the
    count does not depend on the tonic. */
inline int ringNoteCount (int modeIndex) noexcept
{
    const auto& degrees = offsets (modeIndex);
    int degreesPerOctave = 0;

    for (int slot = 0; slot < 12; ++slot)
    {
        if (degrees[static_cast<std::size_t> (slot)] < 0)
            break;

        ++degreesPerOctave;
    }

    return degreesPerOctave * 3;
}

/** The ring's `sector`-th note, ascending from C3: sector 0 is the lowest note
    of the key in the register, not necessarily the tonic. Clamped, so a caller
    holding a sector from before a key change still gets a note. */
inline int ringNoteAt (int tonic, int modeIndex, int sector) noexcept
{
    const auto count = ringNoteCount (modeIndex);
    const auto wanted = sector < 0 ? 0 : (sector >= count ? count - 1 : sector);
    int index = 0;

    for (int midi = ringLowestMidi; midi <= ringHighestMidi; ++midi)
    {
        if (! contains (modeIndex, midi - tonic))
            continue;

        if (index == wanted)
            return midi;

        ++index;
    }

    return ringLowestMidi;
}

/** The sector holding `midiNote`, or -1 when the note is not one of the ring's
    notes for this key. Used to hand the ball's position from the editor to the
    engine, and to recover it when a new editor opens. */
inline int ringSectorOfNote (int tonic, int modeIndex, int midiNote) noexcept
{
    if (midiNote < ringLowestMidi || midiNote > ringHighestMidi)
        return -1;

    int index = 0;

    for (int midi = ringLowestMidi; midi <= ringHighestMidi; ++midi)
    {
        if (! contains (modeIndex, midi - tonic))
            continue;

        if (midi == midiNote)
            return index;

        ++index;
    }

    return -1;
}

} // namespace scale
} // namespace j37
