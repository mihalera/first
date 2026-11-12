// =============================================================================
//  The OSC ring's note plate: the small inset card that carries the note name
//  and LIVE / HOLD, and the type size on it.
//
//  Moved out of OscRing.h because it is ARITHMETIC rather than drawing, and the
//  arithmetic is what went wrong. The plate used to be solved from the ring's
//  radius with two independent floors - 48 x 24 - so every ring smaller than
//  that floor carried a plate bigger than the dial it sat in: the deck's ring on
//  Windows is about 66 px across, and a 48 px plate covered nearly three
//  quarters of it, with a note printed at 12 px on it. On the wider desk ring the
//  same numbers read as a normal inset, which is why the size was never noticed
//  on the platforms the panel was laid out on.
//
//  The plate is now proportional at every size and its floors are low enough to
//  be floors rather than the normal case, so the same ratio holds from a phone's
//  deck ring to the desk. The note's type is solved from the plate rather than
//  from the ring, so the label always fits the card it is printed on.
//
//  JUCE-free on purpose: tests/osc compiles this header and pins the geometry at
//  every ring size, which is the only way to catch a floor that only binds on
//  one platform's ring.
// =============================================================================
#pragma once

#include <algorithm>

namespace j37panel
{
    struct OscDisplayPlate
    {
        float width = 48.0f;
        float height = 24.0f;
    };

    // A plate that is proportional to the dial: 1.05 of the radius across and
    // 0.52 of it down, which is a little over half the ring at every size. The
    // caps are where the desk plate stops growing - past a 96 x 44 card the note
    // reads as a label that happens to have a ring around it, not as an inset in
    // one - and the floors keep a small deck ring's plate legible at all.
    inline constexpr float oscPlateMaxWidth = 72.0f;
    inline constexpr float oscPlateMaxHeight = 32.0f;
    inline constexpr float oscPlateMinWidth = 24.0f;
    inline constexpr float oscPlateMinHeight = 16.0f;

    inline constexpr OscDisplayPlate computeOscDisplayPlate (float radius) noexcept
    {
        OscDisplayPlate plate;
        plate.width = std::clamp (radius * 0.85f, oscPlateMinWidth, oscPlateMaxWidth);
        plate.height = std::clamp (radius * 0.42f, oscPlateMinHeight, oscPlateMaxHeight);
        return plate;
    }

    // The status band is the lower quarter of the plate (see OscRing.h); the note
    // gets everything above it, and its type is a little over half of that with
    // the plate's own 2 px inset taken off. Solved from the plate, so the note can
    // never outgrow the card at a ring size nobody looked at.
    inline constexpr float oscStatusBandShare = 0.25f;
    inline constexpr float oscNoteInset = 2.0f;

    inline constexpr float oscNoteTextHeight (const OscDisplayPlate& plate) noexcept
    {
        return plate.height - 2.0f * oscNoteInset
               - plate.height * oscStatusBandShare;
    }

    inline constexpr float oscNoteFontHeight (const OscDisplayPlate& plate) noexcept
    {
        return std::clamp (oscNoteTextHeight (plate) * 0.72f, 7.0f, 12.0f);
    }
}
