// =============================================================================
//  OSC 1.0: the wire format, and nothing else.
//
//  STAGED, NOT WIRED. Nothing under Source/ constructs a Surface or opens a socket
//  yet, so this header changes no behaviour at all: it is here so that the day OSC
//  is switched on, the part that decodes a packet is already written, tested and
//  reviewed on its own - rather than arriving in the same commit as a socket, a
//  thread and a parameter map, where a padding bug and a routing bug look the same.
//
//  WHY HAND-WRITTEN RATHER THAN A LIBRARY. OSC 1.0 is four paragraphs of
//  specification: big-endian numbers, NUL-terminated strings, every field padded to
//  four bytes, and a bundle that is a count-free sequence of length-prefixed
//  messages. The two C libraries that exist for it (liblo, oscpack) each add a
//  transport, a discovery layer and a build system, and the plugin's own transport
//  is juce::DatagramSocket - already in the binary, already tested by JUCE. That is
//  the same argument docs/library-candidates.md makes for every other list here: a
//  library has to buy something the standard library and JUCE do not.
//
//  JUCE-FREE ON PURPOSE. The codec touches no juce:: name, so tests/osc compiles it
//  with a bare C++20 compiler and no toolkit - the same shape as Source/DSP/ and for
//  the same reason: a format that can only be tested inside the plugin is a format
//  that is tested by opening the plugin.
//
//  WHAT IS REFUSED RATHER THAN GUESSED. A packet arrives from the network, so every
//  field is checked: a size that is not a multiple of four, a string with no NUL in
//  its four-byte chunk, a type tag with no argument behind it, an address with a
//  character OSC reserves for patterns. Each refusal names the reason, because
//  "ignored" and "malformed" are different answers to the caller.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace j37::osc {

/** One OSC argument. The three types this codec carries; the rest of OSC 1.0's
    tag set (blob, int64, timetag, double, ...) is deliberately absent: a plugin
    parameter is a float, a meter is a float, and a type this surface cannot use is
    better refused at the decoder than carried to a caller that will drop it. */
struct Argument
{
    enum class Type { int32, float32, string };

    Type type = Type::float32;
    std::int32_t integer = 0;
    float real = 0.0f;
    std::string text;

    static Argument ofInt (std::int32_t value) { Argument a; a.type = Type::int32; a.integer = value; return a; }
    static Argument ofFloat (float value)       { Argument a; a.type = Type::float32; a.real = value; return a; }
    static Argument ofString (std::string value) { Argument a; a.type = Type::string; a.text = std::move (value); return a; }
};

/** An address and its arguments. */
struct Message
{
    std::string address;
    std::vector<Argument> arguments;
};

/** Bytes on the wire: a message, or a `#bundle` of them. */
using Packet = std::vector<std::uint8_t>;

/** Every field in the format starts on a four-byte boundary: an address, a type tag
    string and a string argument are NUL-terminated and then padded up to this. */
inline constexpr std::size_t argumentAlignment = 4;

/** OSC's address grammar, which is stricter than "a URL path": printable ASCII with
    no space, and none of `#*,?[]{}` - those mean pattern matching, and a surface that
    accepted them as literal addresses would promise its clients a query protocol it
    does not have. */
bool addressesAreValid (std::string_view address);

/** Encodes one message. The caller's address is assumed valid - encode() checks it
    anyway and returns an empty packet rather than emitting something a conforming
    receiver would reject. */
Packet encode (const Message& message);

/** Decodes exactly one message. `reason` (when given) receives why it was refused. */
bool decode (std::string_view bytes, Message& out, std::string* reason = nullptr);

/** Decodes a packet: one message, or a bundle's worth in their own order.

    Bundles are FLATTENED - the timetag is not honoured, because the consumer this
    is staged for (a control surface setting knob values) has no schedule to put a
    future-dated message on, and silently dropping one would be worse than applying
    it now. A caller that ever needs the schedule gets it from the same decoder. */
std::vector<Message> decodePacket (std::string_view bytes, std::string* reason = nullptr);

// -----------------------------------------------------------------------------
//  The implementation. Everything below is `inline` and lives in this header
//  because the whole format is two hundred lines and a .cpp would exist only to be
//  added to three build systems.
// -----------------------------------------------------------------------------
namespace detail {

inline bool paddedSize (std::size_t size, std::size_t& result) noexcept
{
    constexpr auto padding = argumentAlignment - 1;
    if (size > static_cast<std::size_t> (-1) - padding)
        return false;

    result = (size + padding) & ~padding;
    return true;
}

inline void appendUint32 (Packet& packet, std::uint32_t value)
{
    packet.push_back (static_cast<std::uint8_t> ((value >> 24) & 0xffu));
    packet.push_back (static_cast<std::uint8_t> ((value >> 16) & 0xffu));
    packet.push_back (static_cast<std::uint8_t> ((value >> 8) & 0xffu));
    packet.push_back (static_cast<std::uint8_t> (value & 0xffu));
}

inline void appendString (Packet& packet, std::string_view text)
{
    packet.insert (packet.end(), text.begin(), text.end());
    packet.push_back (0);

    while (packet.size() % argumentAlignment != 0)
        packet.push_back (0);
}

inline bool readUint32 (std::string_view bytes, std::size_t& position, std::uint32_t& value)
{
    if (position > bytes.size() || bytes.size() - position < 4)
        return false;

    value = (static_cast<std::uint32_t> (static_cast<std::uint8_t> (bytes[position])) << 24)
          | (static_cast<std::uint32_t> (static_cast<std::uint8_t> (bytes[position + 1])) << 16)
          | (static_cast<std::uint32_t> (static_cast<std::uint8_t> (bytes[position + 2])) << 8)
          | static_cast<std::uint32_t> (static_cast<std::uint8_t> (bytes[position + 3]));
    position += 4;
    return true;
}

/** A NUL-terminated string whose terminator has to be inside the four-byte chunk it
    is padded to: that is the only reading of "padded to a multiple of four" that
    cannot run past the end of a truncated packet. */
inline bool readString (std::string_view bytes, std::size_t& position, std::string& value,
                       std::string* reason)
{
    if (position > bytes.size())
    {
        if (reason != nullptr)
            *reason = "a string starts past the end of the packet";
        return false;
    }

    const auto terminator = bytes.find ('\0', position);

    if (terminator == std::string_view::npos)
    {
        if (reason != nullptr)
            *reason = "a string is not terminated inside the packet";
        return false;
    }

    // The chunk the terminator falls in, padded - so a string that ends at byte 6
    // still occupies 8 and the next field starts there.
    std::size_t chunk = 0;
    if (! paddedSize (terminator - position + 1, chunk)
        || chunk > bytes.size() - position)
    {
        if (reason != nullptr)
            *reason = "a string's padding runs past the end of the packet";
        return false;
    }

    value.assign (bytes.substr (position, terminator - position));
    position += chunk;
    return true;
}

inline bool isReservedAddressCharacter (char c) noexcept
{
    switch (c)
    {
        case ' ': case '#': case '*': case ',': case '?':
        case '[': case ']': case '{': case '}':
            return true;
        default:
            return false;
    }
}

} // namespace detail

inline bool addressesAreValid (std::string_view address)
{
    if (address.empty() || address.front() != '/')
        return false;

    for (const auto c : address)
    {
        const auto value = static_cast<unsigned char> (c);

        if (value < 0x20 || value > 0x7e)
            return false;

        if (detail::isReservedAddressCharacter (c))
            return false;
    }

    return true;
}

inline Packet encode (const Message& message)
{
    Packet packet;

    if (! addressesAreValid (message.address))
        return packet;

    detail::appendString (packet, message.address);

    // The type tag string, assembled first: it has to precede the arguments, and it
    // says how many of them there are - which is why a message with no arguments
    // still carries the lone comma.
    std::string tags = ",";

    for (const auto& argument : message.arguments)
    {
        switch (argument.type)
        {
            case Argument::Type::int32:   tags += 'i'; break;
            case Argument::Type::float32: tags += 'f'; break;
            case Argument::Type::string:  tags += 's'; break;
        }
    }

    detail::appendString (packet, tags);

    for (const auto& argument : message.arguments)
    {
        switch (argument.type)
        {
            case Argument::Type::int32:
                detail::appendUint32 (packet, static_cast<std::uint32_t> (argument.integer));
                break;

            case Argument::Type::float32:
            {
                // The bits, not the value: OSC sends IEEE-754 big-endian, and a
                // memcpy through uint32 is the only spelling of that with no
                // strict-aliasing question.
                std::uint32_t bits = 0;
                static_assert (sizeof (float) == sizeof (bits), "OSC floats are 32-bit");
                std::memcpy (&bits, &argument.real, sizeof (bits));
                detail::appendUint32 (packet, bits);
                break;
            }

            case Argument::Type::string:
                detail::appendString (packet, argument.text);
                break;
        }
    }

    return packet;
}

inline bool decode (std::string_view bytes, Message& out, std::string* reason)
{
    out = {};

    if (bytes.size() % j37::osc::argumentAlignment != 0)
    {
        if (reason != nullptr)
            *reason = "a packet is a whole number of four-byte words";
        return false;
    }

    std::size_t position = 0;

    if (! detail::readString (bytes, position, out.address, reason))
        return false;

    // A bundle's own header is not a message, and decoding one as a message would
    // silently produce an address of "#bundle" and a timetag as a typetag.
    if (out.address == "#bundle")
    {
        if (reason != nullptr)
            *reason = "this is a bundle; decodePacket() unpacks it";
        return false;
    }

    if (! addressesAreValid (out.address))
    {
        if (reason != nullptr)
            *reason = "the address is not a valid OSC address";
        return false;
    }

    std::string tags;
    if (! detail::readString (bytes, position, tags, reason))
        return false;

    if (tags.empty() || tags.front() != ',')
    {
        if (reason != nullptr)
            *reason = "a message's arguments begin with a type tag string";
        return false;
    }

    for (std::size_t index = 1; index < tags.size(); ++index)
    {
        switch (tags[index])
        {
            case 'i':
            {
                std::uint32_t raw = 0;
                if (! detail::readUint32 (bytes, position, raw))
                {
                    if (reason != nullptr)
                        *reason = "the type tag string promises more arguments than the packet holds";
                    return false;
                }

                out.arguments.push_back (Argument::ofInt (static_cast<std::int32_t> (raw)));
                break;
            }

            case 'f':
            {
                std::uint32_t raw = 0;
                if (! detail::readUint32 (bytes, position, raw))
                {
                    if (reason != nullptr)
                        *reason = "the type tag string promises more arguments than the packet holds";
                    return false;
                }

                float value = 0.0f;
                std::memcpy (&value, &raw, sizeof (value));
                out.arguments.push_back (Argument::ofFloat (value));
                break;
            }

            case 's':
            {
                std::string text;
                if (! detail::readString (bytes, position, text, reason))
                    return false;

                out.arguments.push_back (Argument::ofString (std::move (text)));
                break;
            }

            default:
                if (reason != nullptr)
                    *reason = std::string ("unsupported type tag '") + tags[index] + "'";
                return false;
        }
    }

    if (position != bytes.size())
    {
        if (reason != nullptr)
            *reason = "a message has trailing bytes after its arguments";
        return false;
    }

    return true;
}

inline std::vector<Message> decodePacket (std::string_view bytes, std::string* reason)
{
    std::vector<Message> messages;

    Message single;
    if (decode (bytes, single, reason))
    {
        messages.push_back (std::move (single));
        return messages;
    }

    // Not a message: try the bundle shape. `#bundle\0` is eight bytes, then the
    // timetag is eight more, then each element is a big-endian int32 length
    // followed by that many bytes - which is what has to be walked carefully,
    // because a length is the one number in the format a peer sets arbitrarily.
    if (bytes.size() < 16 || bytes.compare (0, 8, "#bundle\0", 8) != 0)
        return messages;

    std::size_t position = 16;

    while (position < bytes.size())
    {
        std::uint32_t length = 0;
        if (! detail::readUint32 (bytes, position, length))
        {
            if (reason != nullptr)
                *reason = "a bundle element has no length";
            return {};
        }

        if (position > bytes.size() || length > bytes.size() - position)
        {
            if (reason != nullptr)
                *reason = "a bundle element is longer than the packet";
            return {};
        }

        // An element is a whole OSC packet padded to a four-byte boundary, so a
        // zero length and a length that is not a multiple of four cannot be one:
        // they are a peer's arithmetic that has gone wrong, and decoding either
        // as a truncated packet would report the wrong fault.
        if (length == 0)
        {
            if (reason != nullptr)
                *reason = "a bundle element is empty";
            return {};
        }

        if (length % 4 != 0)
        {
            if (reason != nullptr)
                *reason = "a bundle element is not four-byte aligned";
            return {};
        }

        Message element;
        if (! decode (bytes.substr (position, length), element, reason))
        {
            // One unreadable element fails the packet rather than being skipped: a
            // bundle is one unit of intent from the sender, and applying half of it
            // would leave a control surface in a state nobody asked for.
            return {};
        }

        messages.push_back (std::move (element));
        position += length;
    }

    return messages;
}

} // namespace j37::osc
