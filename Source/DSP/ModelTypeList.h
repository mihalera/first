// =============================================================================
//  A compile-time helper for the model-type lists the saturation core, the tape
//  stock and the vinyl stage carry.
//
//  Every model family follows the same pattern: a count, a name array sized to
//  the count, a constexpr completeness check, a static_assert, and a
//  juce::StringArray conversion.  That pattern was copy-pasted six times across
//  the tape stock and the five voiced families, which meant six places to forget
//  the assert or to miscount.  This header
//  captures the three-step check (names are complete, count matches, convert) as
//  one template so each family supplies the two things that vary - the count and
//  the names - and nothing else.
//
//  It is JUCE-free except for the StringArray conversion, which relies on the
//  includer having declared juce::StringArray (which is true in both the plugin
//  and in the JUCE-linked test check).
// =============================================================================
#pragma once

#include <array>

namespace j37model
{

/** True when every slot of a name array was actually given a value.
    std::array catches too MANY initialisers, but not too FEW: elements past the
    initialiser list are value-initialised to nullptr, so a raised count without
    a matching name compiles clean and quietly hands the list a null pointer. */
template <std::size_t N>
constexpr bool namesAreComplete (const std::array<const char*, N>& names)
{
    for (const auto* name : names)
        if (name == nullptr)
            return false;
    return true;
}

/** Converts a constexpr name array to the juce::StringArray the parameter and
    the combo box both need.  A function rather than a constant because
    juce::StringArray is not a literal type. */
template <std::size_t N>
inline juce::StringArray toStringArray (const std::array<const char*, N>& names)
{
    juce::StringArray result;
    for (const auto* name : names)
        result.add (juce::String (name));
    return result;
}

} // namespace j37model
