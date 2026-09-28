// Minimal stand-in for the handful of JUCE helpers the extracted DSP uses, so
// the harness compiles with nothing but a C++17 compiler. Only what the
// extracted code actually calls is here: jmin / jmax / jlimit, MathConstants,
// and the two Decibels conversions. Anything the plugin's DSP needs that is
// missing will fail to compile rather than quietly behaving differently.
#pragma once

#include <cmath>
#include <cstdint>
#include <string>

namespace juce
{
template <typename T>
constexpr T jmin (T a, T b) noexcept { return b < a ? b : a; }

template <typename T>
constexpr T jmax (T a, T b) noexcept { return a < b ? b : a; }

template <typename T>
constexpr T jlimit (T lo, T hi, T v) noexcept { return v < lo ? lo : (hi < v ? hi : v); }

template <typename T>
struct MathConstants
{
    static constexpr T pi = static_cast<T> (3.14159265358979323846);
    static constexpr T twoPi = static_cast<T> (2.0 * 3.14159265358979323846);
    static constexpr T halfPi = static_cast<T> (0.5 * 3.14159265358979323846);
};

struct Decibels
{
    static float decibelsToGain (float decibels) noexcept
    {
        return std::pow (10.0f, decibels / 20.0f);
    }

    static float gainToDecibels (float gain, float floorDb = -100.0f) noexcept
    {
        if (gain <= 1.0e-6f)
            return floorDb;
        return 20.0f * std::log10 (gain);
    }
};

// A minimal stand-in for the two juce::String members NeuralStage needs when it
// is compiled with J37_HAS_RTNEURAL 0: the empty query the loaded-model guard
// uses, plus toStdString/from-string construction so the JSON path still type
// checks. The struct's loadFromJson, under the harness, does nothing but reply
// false - so the type only has to exist and answer these, which is all the
// extracted text calls. It is deliberately NOT a real string: the harness must
// never be able to grow a dependency on JUCE's string semantics.
class String
{
public:
    String() = default;
    String (const char*) {}

    bool isEmpty() const noexcept { return true; }
    bool isNotEmpty() const noexcept { return false; }

    std::string toStdString() const { return {}; }
};

/** The harness compiles the extracted text with warnings as errors in places, so
    the innocuous ignoreUnused the shipping code uses must resolve here too. */
template <typename... Ts>
inline void ignoreUnused (Ts&&...) noexcept {}
} // namespace juce

