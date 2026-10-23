// =============================================================================
//  The OSC codec and the address surface, checked against the wire format itself.
//
//  Source/Net/Osc.h is staged: no build constructs a Surface and the plugin opens no
//  socket, so nothing in the shipping product can prove this code right. That is
//  exactly why the checks below are byte-level rather than "encode then decode and
//  see if it comes back": a codec that pads wrongly and decodes its own output
//  symmetrically would round-trip perfectly and be unusable by every real client.
//  So the expected bytes are written out where the spec fixes them, and the
//  conversation with a client is simulated in both directions - a packet built by
//  hand, byte by byte, is fed to the decoder, and a packet the encoder produced is
//  compared against the bytes a client would have sent.
//
//  tests/osc/run.sh builds and runs this with no JUCE and no toolkit.
// =============================================================================
#include "Net/Osc.h"
#include "Net/OscSurface.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char* name)
{
    ++checks;
    std::printf("  %s  %s\n", condition ? "ok  " : "FAIL", name);

    if (! condition)
        ++failures;
}

using j37::osc::Argument;
using j37::osc::Message;
using j37::osc::Packet;

std::string asString(const Packet& packet)
{
    return std::string(packet.begin(), packet.end());
}

/** `#bundle`, an immediate timetag, then the elements - what a client sends. */
Packet bundleOf(const std::vector<Packet>& elements)
{
    Packet packet;
    const std::string header = "#bundle";
    packet.insert(packet.end(), header.begin(), header.end());
    packet.push_back(0);

    for (int byte = 0; byte < 8; ++byte) // the timetag: 1 (immediate) as an NTP fixed point
        packet.push_back(byte < 7 ? 0 : 1);

    for (const auto& element : elements) {
        j37::osc::detail::appendUint32(packet, static_cast<std::uint32_t>(element.size()));
        packet.insert(packet.end(), element.begin(), element.end());
    }

    return packet;
}

void checkAddressGrammar()
{
    std::printf("\naddress grammar\n");

    check(j37::osc::addressesAreValid("/mibox/tape/drive"), "an ordinary container address is valid");
    check(j37::osc::addressesAreValid("/a"), "a one-segment address is valid");
    check(j37::osc::addressesAreValid("/mibox/meter/1"), "digits are valid in a segment");
    check(! j37::osc::addressesAreValid(""), "an empty address is refused");
    check(! j37::osc::addressesAreValid("mibox/tape"), "an address has to start with a slash");
    check(! j37::osc::addressesAreValid("/tape drive"), "a space is reserved and refused");
    check(! j37::osc::addressesAreValid("/tape/*"), "'*' is a pattern character and refused");
    check(! j37::osc::addressesAreValid("/tape/#"), "'#' is reserved and refused");
    check(! j37::osc::addressesAreValid("/tape/[0]"), "'[' is a pattern character and refused");
    check(! j37::osc::addressesAreValid(std::string("/tape/\x01")), "a control character is refused");
}

void checkEncoding()
{
    std::printf("\nencoding, against the bytes the specification fixes\n");

    Message bare;
    bare.address = "/a";

    // Four bytes of address ("/a", NUL, one pad) then four of type tag (",", NUL, two
    // pads). A message with no arguments is exactly eight bytes.
    const auto barePacket = j37::osc::encode(bare);
    check(barePacket == Packet{ '/', 'a', 0, 0, ',', 0, 0, 0 },
          "an eight-byte message with no arguments");

    Message one;
    one.address = "/a";
    one.arguments.push_back(Argument::ofFloat(0.25f));
    const auto onePacket = j37::osc::encode(one);
    // Four bytes of address, four of type tag, four of argument.
    check(onePacket.size() == 12, "a one-float message is twelve bytes");
    check(onePacket.size() == 12 && onePacket[4] == ',' && onePacket[5] == 'f',
          "the type tag string is ',f'");
    check(onePacket.size() == 12 && onePacket[8] == 0x3e && onePacket[9] == 0x80
              && onePacket[10] == 0 && onePacket[11] == 0,
          "0.25f is big-endian IEEE-754 (0x3e800000)");

    Message text;
    text.address = "/s";
    text.arguments.push_back(Argument::ofString("abcde"));
    const auto textPacket = j37::osc::encode(text);
    // "/s" + NUL + pad = 4; ",s" + NUL + pad = 4; "abcde" + NUL = 6 -> padded to 8.
    check(textPacket.size() == 16, "a five-character string occupies eight bytes, padded");
    check(textPacket.size() == 16 && textPacket[4] == ',' && textPacket[5] == 's',
          "the type tag string is ',s'");
    // "abcde" starts at byte 8 (after the address and the type tag); its terminator
    // and padding fill the rest of the second four-byte chunk, bytes 13 to 15.
    check(textPacket.size() == 16 && textPacket[8] == 'a' && textPacket[12] == 'e'
              && textPacket[13] == 0 && textPacket[15] == 0,
          "the string's second four-byte chunk ends in NULs");

    Message invalid;
    invalid.address = "/a b";
    check(j37::osc::encode(invalid).empty(), "an invalid address encodes to nothing");
}

void checkRoundTrips()
{
    std::printf("\nround trips\n");

    Message original;
    original.address = "/mibox/tape/drive";
    original.arguments.push_back(Argument::ofFloat(-0.5f));
    original.arguments.push_back(Argument::ofInt(-7));
    original.arguments.push_back(Argument::ofString("a string longer than one word"));

    const auto packet = j37::osc::encode(original);

    Message decoded;
    std::string reason;
    const auto ok = j37::osc::decode(asString(packet), decoded, &reason);

    check(ok, "a message the encoder produced decodes");
    check(decoded.address == original.address, "the address survives");
    check(decoded.arguments.size() == 3, "all three arguments survive");
    check(decoded.arguments.size() == 3
              && decoded.arguments[0].type == Argument::Type::float32
              && decoded.arguments[0].real == -0.5f,
          "a negative float survives bit for bit");
    check(decoded.arguments.size() == 3
              && decoded.arguments[1].type == Argument::Type::int32
              && decoded.arguments[1].integer == -7,
          "a negative int32 survives");
    check(decoded.arguments.size() == 3
              && decoded.arguments[2].type == Argument::Type::string
              && decoded.arguments[2].text == "a string longer than one word",
          "a multi-word string survives");

    // The packet a CLIENT would send, built by hand: this is the direction that
    // catches a decoder which only agrees with its own encoder.
    const std::string fromAClient =
        std::string("/gain\0\0\0,f\0\0", 12) + std::string("\x3f\x00\x00\x00", 4);
    Message client;
    check(j37::osc::decode(fromAClient, client, &reason), "a hand-built client packet decodes");
    check(client.address == "/gain" && client.arguments.size() == 1
              && client.arguments[0].real == 0.5f,
          "the hand-built packet's address and 0.5f are read");
}

void checkRefusals()
{
    std::printf("\nrefusals\n");

    Message message;
    std::string reason;

    check(! j37::osc::decode(std::string("/a\0\0,f\0\0\x3f\0\0", 11), message, &reason),
          "a packet that is not a whole number of four-byte words is refused");
    check(reason.find("four-byte") != std::string::npos, "and the reason names the alignment");

    check(! j37::osc::decode(std::string("/abcdefghijk", 12), message, &reason),
          "an unterminated address is refused");

    // The type tag promises a float, the packet ends after the tag.
    check(! j37::osc::decode(std::string("/a\0\0,f\0\0", 8), message, &reason),
          "a type tag that promises an argument the packet does not hold is refused");
    check(reason.find("promises") != std::string::npos, "and the reason says so");

    // The type tag is missing entirely: an address chunk, then four NULs.
    check(! j37::osc::decode(std::string("/a\0\0\0\0\0\0\0", 8), message, &reason),
          "a message with no type tag string is refused");

    // 'd' is a double: valid OSC 1.1, deliberately not carried here.
    check(! j37::osc::decode(std::string("/a\0\0,d\0\0\0", 8), message, &reason),
          "an unsupported type tag is refused");
    check(reason.find("unsupported") != std::string::npos, "and the reason names the tag");

    check(! j37::osc::decode(std::string("#bundle\0", 8) + std::string(8, '\0'), message, &reason),
          "a bundle is not a message, and decode() says so");
    check(reason.find("bundle") != std::string::npos, "and points at decodePacket()");
}

void checkBundles()
{
    std::printf("\nbundles\n");

    Message first;
    first.address = "/one";
    first.arguments.push_back(Argument::ofFloat(1.0f));

    Message second;
    second.address = "/two";
    second.arguments.push_back(Argument::ofInt(2));

    Message third;
    third.address = "/three";

    const auto bundle = bundleOf({ j37::osc::encode(first), j37::osc::encode(second),
                                   j37::osc::encode(third) });

    std::string reason;
    const auto messages = j37::osc::decodePacket(asString(bundle), &reason);

    check(messages.size() == 3, "a bundle of three messages decodes to three messages");
    check(messages.size() == 3 && messages[0].address == "/one" && messages[1].address == "/two"
              && messages[2].address == "/three",
          "and they keep the sender's order");

    // A length field is the one number a peer sets arbitrarily: it has to be refused.
    Packet broken = bundleOf({ j37::osc::encode(first) });
    const auto lengthAt = broken.size() - (j37::osc::encode(first).size() + 4);
    broken[lengthAt] = 0x7f; // 127 bytes claimed, 40 or so present

    reason.clear();
    const auto refused = j37::osc::decodePacket(asString(broken), &reason);
    check(refused.empty(), "a bundle element longer than the packet is refused");
    check(reason.find("longer") != std::string::npos, "and the reason says the length is wrong");
}

void checkSurface()
{
    std::printf("\nthe address surface\n");

    using j37::osc::ParameterSlot;
    using j37::osc::Surface;

    const auto surface = Surface({ ParameterSlot{ "tape_drive", "tape/drive", 0.0f, 1.0f },
                                   ParameterSlot{ "input", "input", -32.0f, 32.0f } });

    Message message;
    message.address = "/mibox/tape/drive";
    message.arguments.push_back(Argument::ofFloat(0.42f));

    const auto hit = surface.route(message);
    check(hit.has_value(), "a known address routes");
    check(hit.has_value() && hit->slot == 0, "to the slot it names");
    check(hit.has_value() && std::abs(hit->normalised - 0.42f) < 1.0e-6f, "with its value");

    message.arguments.front().real = 1.7f;
    const auto high = surface.route(message);
    check(high.has_value() && high->normalised == 1.0f, "a value above one is clamped, not refused");

    message.arguments.front().real = -0.2f;
    const auto low = surface.route(message);
    check(low.has_value() && low->normalised == 0.0f, "and below zero is clamped too");

    message.arguments.front().real = std::nanf("");
    check(! surface.route(message).has_value(), "NaN is refused - it is not a knob position");

    message.arguments.front().real = 0.5f;
    message.arguments.push_back(Argument::ofFloat(0.5f));
    check(! surface.route(message).has_value(), "two arguments at one address are refused");
    message.arguments.pop_back();

    message.arguments.front() = Argument::ofInt(1);
    check(! surface.route(message).has_value(), "an int32 is refused rather than guessed");

    message.arguments.front() = Argument::ofString("up");
    check(! surface.route(message).has_value(), "a string is refused rather than guessed");

    Message unknown;
    unknown.address = "/mibox/tape/nothing";
    unknown.arguments.push_back(Argument::ofFloat(0.5f));
    check(! surface.route(unknown).has_value(), "an address not in the map is refused");

    unknown.address = "/mibox/tape/*";
    check(! surface.route(unknown).has_value(), "a pattern address is refused, not expanded");

    // Feedback is the other direction, and it has to survive a trip through the codec.
    const auto feedback = surface.feedback(1, 0.75f);
    check(feedback.address == "/mibox/input" && feedback.arguments.size() == 1,
          "feedback addresses the slot");

    Message redecoded;
    check(j37::osc::decode(asString(j37::osc::encode(feedback)), redecoded)
              && surface.route(redecoded).has_value()
              && std::abs(surface.route(redecoded)->normalised - 0.75f) < 1.0e-6f,
          "and a client can send it straight back");

    check(std::abs(surface.engineeringValue(1, 0.75f) - 16.0f) < 1.0e-4f,
          "the engineering value maps the parameter's own range (-32..32 at 0.75 is 16)");
    check(surface.addressOf(7).empty(), "an address of a slot that does not exist is empty");
}

} // namespace

int main()
{
    std::printf("OSC 1.0: the codec, the refusals and the address surface\n");

    checkAddressGrammar();
    checkEncoding();
    checkRoundTrips();
    checkRefusals();
    checkBundles();
    checkSurface();

    std::printf("\n%d check(s), %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
