#pragma once

#include <cmath>

// The transport's per-frame arithmetic, lifted out of the tape loop.
//
// processOneSample used to work this out inline, between the capstan advance and
// the input glue compressor: the platter speed raised to two different exponents,
// the 0.7 that makes a coast read as a machine losing its drive, and the load
// term that pulls the tape slower under load - three numbers with no name inside
// a 1,700-line function, and therefore invisible to every harness.
//
// What this deliberately does NOT do is move the programme's PITCH. Everything
// here is a GAIN on one number, the platter's speed, and a multiplier cannot bend
// a pitch - it can only make the signal smaller. The bend belongs to the repitch
// stage at the end of the chain, which reads the finished frame back at that same
// number and is the one place the FORMAT of the output changes rather than its
// level. Keeping the two apart is what lets the gate stop the sound outright
// without the bend having to be undone first.

namespace j37dsp
{

// How much of the machine's own motion still reaches the signal once the platter
// is slow: the term falls away with the speed, but not all the way with it, so a
// coast reads as a machine losing its drive rather than as a fade-out.
inline constexpr float kTransportLoadReach = 0.7f;

struct TapeTransportFrame
{
    float platterSpeed = 1.0f;  // what the machine's level, depth and reels follow
    float speedError = 1.0f;    // the tape running slow; 1 is at speed
    float platterGate = 1.0f;   // what the machine passes while turning; 0 is silence
    float spindown = 0.0f;      // the run-down, for the reels and the panel's button
};

// platterSpeed is the PRODUCT of the capstan ramp and the run-down, which is what
// makes a spindown from a stopped transport and a stop during a spindown land in
// the same place.
//
// platterPitchExponent holds the machine term near speed until the platter is
// genuinely slow and then lets it fall away: it has to be ABOVE 1, or the machine
// goes slack the instant the button goes down. platterGateExponent does the same
// job for the output gate, and it is why STOP is silence rather than a fade.
inline TapeTransportFrame tapeTransportFrame (float platterSpeed,
                                              float spindown,
                                              float platterPitchExponent,
                                              float platterGateExponent,
                                              float loadInducedSpeedError) noexcept
{
    const auto pitchedPlatter = std::pow (platterSpeed, platterPitchExponent);
    const auto speedError = (1.0f - (1.0f - pitchedPlatter) * kTransportLoadReach)
                          * (1.0f - loadInducedSpeedError);
    const auto platterGate = std::pow (platterSpeed, platterGateExponent);

    return { platterSpeed, speedError, platterGate, spindown };
}

} // namespace j37dsp
