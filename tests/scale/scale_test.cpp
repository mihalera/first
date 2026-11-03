#include "ScaleDegrees.h"

#include <cstdio>

namespace
{

int failures = 0;
int checks = 0;

void check (bool condition, const char* message)
{
    ++checks;

    if (! condition)
    {
        ++failures;
        std::printf ("FAIL: %s\n", message);
    }
}

void checkMode (int mode, int expectedDegreeCount)
{
    const auto& degrees = j37::scale::offsets (mode);
    int degreeCount = 0;
    int previous = -1;
    bool reachedSentinel = false;

    for (const auto degree : degrees)
    {
        if (degree < 0)
        {
            reachedSentinel = true;
            continue;
        }

        check (! reachedSentinel, "scale degrees must precede all sentinels");
        check (degree > previous && degree < 12,
               "scale degrees must be ascending pitch classes");
        check (j37::scale::contains (mode, degree),
               "every listed degree must be contained in its mode");
        previous = degree;
        ++degreeCount;
    }

    check (degreeCount == expectedDegreeCount, "mode must contain the expected degrees");
    check (j37::scale::ringNoteCount (mode) == expectedDegreeCount * 3,
           "the three-octave register must contain three copies of each degree");

    for (int semitone = -48; semitone <= 48; ++semitone)
    {
        const auto pitchClass = ((semitone % 12) + 12) % 12;
        bool expected = false;

        for (int degree = 0; degree < degreeCount; ++degree)
            expected = expected || degrees[static_cast<std::size_t> (degree)] == pitchClass;

        check (j37::scale::contains (mode, semitone) == expected,
               "membership must normalize positive and negative octaves");
    }

    for (int tonic = 0; tonic < 12; ++tonic)
    {
        int previousMidi = j37::scale::ringLowestMidi - 1;
        const auto count = j37::scale::ringNoteCount (mode);

        for (int sector = 0; sector < count; ++sector)
        {
            const auto midi = j37::scale::ringNoteAt (tonic, mode, sector);
            check (midi > previousMidi, "ring notes must be strictly ascending");
            check (j37::scale::contains (mode, midi - tonic),
                   "every ring note must match the selected key and mode");
            check (j37::scale::ringSectorOfNote (tonic, mode, midi) == sector,
                   "ring note and sector mappings must round-trip");
            previousMidi = midi;
        }

        check (j37::scale::ringNoteAt (tonic, mode, -1)
                   == j37::scale::ringNoteAt (tonic, mode, 0),
               "negative sectors must clamp to the first note");
        check (j37::scale::ringNoteAt (tonic, mode, count)
                   == j37::scale::ringNoteAt (tonic, mode, count - 1),
               "past-end sectors must clamp to the last note");
    }

    check (j37::scale::ringSectorOfNote (0, mode, j37::scale::ringLowestMidi - 1) == -1,
           "notes below the ring register must have no sector");
    check (j37::scale::ringSectorOfNote (0, mode, j37::scale::ringHighestMidi + 1) == -1,
           "notes above the ring register must have no sector");
}

} // namespace

int main()
{
    constexpr int expectedDegrees[j37::scale::modeCount] { 12, 7, 7, 7, 5, 5 };

    for (int mode = 0; mode < j37::scale::modeCount; ++mode)
        checkMode (mode, expectedDegrees[mode]);

    check (&j37::scale::offsets (-1) == &j37::scale::table[0],
           "negative modes must clamp to chromatic");
    check (&j37::scale::offsets (j37::scale::modeCount) == &j37::scale::table.back(),
           "past-end modes must clamp to the last scale");
    check (j37::scale::ringNoteCount (-1) == 36,
           "invalid low modes must use the chromatic ring");
    check (j37::scale::ringNoteCount (j37::scale::modeCount) == 15,
           "invalid high modes must use the final pentatonic ring");

    if (failures != 0)
    {
        std::printf ("scale tests: %d/%d checks failed\n", failures, checks);
        return 1;
    }

    std::printf ("scale tests passed (%d checks)\n", checks);
    return 0;
}
