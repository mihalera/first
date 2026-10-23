// =============================================================================
//  The OSC surface: an address per plugin parameter, and the values in between.
//
//  STAGED, NOT WIRED. Nothing constructs a Surface today - Source/PluginProcessor.cpp
//  includes this header so the code is compiled and reviewed, and then does nothing
//  with it. The plugin has no socket and no OSC thread, and this file adds neither:
//  it is the mapping half, which is the half that can be got wrong silently.
//
//  THE TWO DECISIONS THIS FILE MAKES, so that the commit that switches it on does
//  not have to make them in a hurry:
//
//  1. FLOATS ONLY, NORMALISED. A parameter arrives as one float32 in [0, 1] - the
//     same convention every DAW, plugin and control surface already speaks, and the
//     same range the parameter tree stores. An int32 or a string addressed at a knob
//     is REFUSED rather than interpreted: an int means "index" to one client and
//     "raw value" to another, and guessing would give a user a control that moves to
//     the wrong place one client later.
//
//  2. EXACT ADDRESSES, NO PATTERNS. `/mibox/tape/drive` sets the drive; `/mibox/tape/*`
//     is refused. OSC's pattern characters are reserved by the codec
//     (j37::osc::addressesAreValid), and a surface that honoured them would be
//     promising a query protocol this one does not have. A client finds the map with
//     Surface::slots(), which is what the plugin would answer on a `/mibox/describe`
//     request when the transport lands.
//
//  WHAT A SLOT CARRIES. The parameter id, the path under the root, and the range the
//  caller resolved from the parameter tree. The range is here rather than looked up
//  because this header does not know about juce::AudioProcessorParameter - keeping it
//  JUCE-free is what lets tests/osc compile it with no toolkit, and what keeps the
//  mapping checkable without an editor.
// =============================================================================
#pragma once

#include "Net/Osc.h"

#include <optional>
#include <string>
#include <vector>

namespace j37::osc {

/** One addressable parameter. */
struct ParameterSlot
{
    std::string id;   ///< the plugin's parameter id, e.g. "tape_drive"
    std::string path; ///< its address below the surface's root, e.g. "tape/drive"

    float minimum = 0.0f; ///< the parameter's own range, for feedback and for scaling
    float maximum = 1.0f;
};

/**
    The address map, and the two conversions on either side of it.

    Routing is a linear scan over the slot table. A parameter tree is a hundred-odd
    entries and an incoming packet is one address, so a hash map would be a second
    data structure to keep in step with the table for no measurable gain - and the
    scan touches no allocator: the address it compares against is assembled on the
    stack by length and prefix rather than by "root + '/' + path", which is the
    difference that matters on the day this is called from a receiver thread.
*/
class Surface
{
public:
    struct Hit
    {
        std::size_t slot = 0;
        float normalised = 0.0f;
    };

    Surface (std::vector<ParameterSlot> parameterSlots, std::string addressRoot = "/mibox")
        : slots (std::move (parameterSlots)), root (std::move (addressRoot))
    {
    }

    /** The whole map, in the order it was given. What a client asks for. */
    const std::vector<ParameterSlot>& getSlots() const noexcept { return slots; }
    const std::string& getRoot() const noexcept { return root; }

    /** The address a slot answers on: the root, then the path. */
    std::string addressOf (std::size_t slot) const
    {
        if (slot >= slots.size())
            return {};

        return root + "/" + slots[slot].path;
    }

    /**
        The parameter a message addresses, and the value to set it to.

        Refused (nullopt) when: the address is not in the map, the message does not
        carry exactly one argument, the argument is not a float32, or the float is
        NaN. Anything else is CLAMPED into [0, 1] rather than refused - a surface
        that rejects 1.0000001 because a client scaled by 1.0000001 is a surface that
        drops a knob movement for a rounding error.
    */
    std::optional<Hit> route (const Message& message) const
    {
        for (std::size_t slot = 0; slot < slots.size(); ++slot)
        {
            if (! matches (slots[slot], message.address))
                continue;

            if (message.arguments.size() != 1)
                return std::nullopt;

            const auto& argument = message.arguments.front();
            if (argument.type != Argument::Type::float32)
                return std::nullopt;

            // NaN fails both comparisons, which is the cheapest spelling of "this
            // float is not a number and therefore not a knob position".
            if (! (argument.real >= -1.0e6f && argument.real <= 1.0e6f))
                return std::nullopt;

            Hit hit;
            hit.slot = slot;
            hit.normalised = argument.real < 0.0f ? 0.0f
                           : argument.real > 1.0f ? 1.0f
                                                  : argument.real;
            return hit;
        }

        return std::nullopt;
    }

    /** The other direction: a parameter's value as an outgoing message.

        `normalised` is what the tree stores; the slot's range is what a client that
        wants engineering units gets, so a meter or a readout can be wired without a
        second convention. The value is sent as the NORMALISED float for the same
        reason route() reads one: both ends of this surface speak one scale.
    */
    Message feedback (std::size_t slot, float normalised) const
    {
        Message message;

        if (slot >= slots.size())
            return message;

        message.address = addressOf (slot);
        message.arguments.push_back (Argument::ofFloat (normalised < 0.0f ? 0.0f
                                                     : normalised > 1.0f ? 1.0f
                                                                         : normalised));
        return message;
    }

    /** The slot's value in its own units - the number a client displays rather than
        the one it sets. Kept beside feedback() so the two conversions are one file. */
    float engineeringValue (std::size_t slot, float normalised) const noexcept
    {
        if (slot >= slots.size())
            return 0.0f;

        const auto& entry = slots[slot];
        return entry.minimum + (entry.maximum - entry.minimum) * normalised;
    }

private:
    /** `root/path`, compared without building the string: the length first, then the
        root, then the separator, then the path. One read of the address either way,
        and nothing allocated for a packet that matches nothing. */
    bool matches (const ParameterSlot& slot, std::string_view address) const noexcept
    {
        if (address.size() != root.size() + 1 + slot.path.size())
            return false;

        if (address.compare (0, root.size(), root) != 0)
            return false;

        if (address[root.size()] != '/')
            return false;

        return address.compare (root.size() + 1, slot.path.size(), slot.path) == 0;
    }

    std::vector<ParameterSlot> slots;
    std::string root;
};

} // namespace j37::osc
