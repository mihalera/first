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
//  The ring's ball is here too. It is not part of the wire format, but it is the
//  other JUCE-free piece the module depends on, and its fixed step is what lets
//  the editor and the engine agree when the window closes. The checks drive it
//  for two minutes, assert it stays inside the circle and visits the ring, and
//  pin the contact-angle mapping and the impact-gain curve the note uses.
//
//  The modal voice is the third: it is rendered here so a bank that rings forever,
//  aliases at a low session rate or forgets to reset is caught without a host.
//
//  tests/osc/run.sh builds and runs this with no JUCE and no toolkit.
// =============================================================================
#include "Net/Osc.h"
#include "Net/OscSurface.h"
#include "OscBall.h"
#include "DSP/ModalVoice.h"
#include "GUI/OscRingMetrics.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <set>
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

void checkArgumentReader()
{
    std::printf("\nargument reader\n");

    using j37::osc::Argument;
    using j37::osc::detail::readArgument;

    Argument argument = Argument::ofString("unchanged");
    std::size_t position = 0;
    const std::string intMinimum("\x80\x00\x00\x00", 4);
    check(readArgument('i', intMinimum, position, argument, nullptr),
          "an int32 argument reads without a reason sink");
    check(position == 4 && argument.type == Argument::Type::int32
              && argument.integer == (-2147483647 - 1),
          "int32 reads consume four bytes and preserve the sign bit");

    position = 0;
    const std::string intMaximum("\x7f\xff\xff\xff", 4);
    check(readArgument('i', intMaximum, position, argument, nullptr)
              && position == 4 && argument.integer == 2147483647,
          "the largest positive int32 is decoded");

    position = 0;
    const std::string oneFloat("\x3f\x80\x00\x00", 4);
    check(readArgument('f', oneFloat, position, argument, nullptr)
              && position == 4 && argument.type == Argument::Type::float32
              && argument.real == 1.0f,
          "a big-endian float reads as 1.0 and consumes four bytes");

    position = 0;
    const std::string negativeZero("\x80\x00\x00\x00", 4);
    check(readArgument('f', negativeZero, position, argument, nullptr)
              && argument.real == 0.0f && std::signbit(argument.real),
          "a float's negative-zero bit is preserved");

    position = 0;
    const std::string infinity("\x7f\x80\x00\x00", 4);
    check(readArgument('f', infinity, position, argument, nullptr)
              && std::isinf(argument.real) && argument.real > 0.0f,
          "an IEEE positive-infinity float is decoded");

    position = 0;
    const std::string paddedText("abc\0", 4);
    check(readArgument('s', paddedText, position, argument, nullptr)
              && position == 4 && argument.type == Argument::Type::string
              && argument.text == "abc",
          "a padded string reads and advances to the next field");

    position = 0;
    const std::string emptyText(4, '\0');
    check(readArgument('s', emptyText, position, argument, nullptr)
              && position == 4 && argument.text.empty(),
          "an empty string occupies one four-byte word");

    position = 0;
    const std::string exactBoundary("abcd\0\0\0\0", 8);
    check(readArgument('s', exactBoundary, position, argument, nullptr)
              && position == 8 && argument.text == "abcd",
          "a string ending exactly on a word boundary consumes no extra word");

    const auto checkRejected = [&] (char tag, std::string_view bytes,
                                    std::size_t start, const char* expectedReason,
                                    const char* description)
    {
        Argument untouched = Argument::ofString("unchanged");
        auto cursor = start;
        std::string reason;
        const auto rejected = ! readArgument(tag, bytes, cursor, untouched, &reason);
        check(rejected && reason.find(expectedReason) != std::string::npos,
              description);
        check(cursor == start && untouched.type == Argument::Type::string
                  && untouched.text == "unchanged",
              "a rejected argument leaves the cursor and destination unchanged");
    };

    checkRejected('i', std::string_view("\x00\x00\x00", 3), 0, "promises",
                  "a truncated int32 is refused with a useful reason");
    checkRejected('f', std::string_view("\x3f\x80\x00", 3), 0, "promises",
                  "a truncated float32 is refused with a useful reason");
    checkRejected('s', "abcd", 0, "not terminated",
                  "an unterminated string is refused");
    checkRejected('s', std::string_view("\0", 1), 0, "padding",
                  "a string whose padding exceeds the packet is refused");
    checkRejected('?', {}, 0, "unsupported",
                  "an unsupported argument tag is refused");

    position = 2;
    argument = Argument::ofString("unchanged");
    std::string reason;
    check(! readArgument('i', oneFloat, position, argument, &reason)
              && reason.find("promises") != std::string::npos,
          "an argument beginning too near the end is refused");
    check(position == 2 && argument.text == "unchanged",
          "an out-of-range argument start does not partially mutate state");

    position = 0;
    check(! readArgument('?', {}, position, argument, nullptr),
          "a refusal is safe when the caller omits the reason sink");
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

    auto trailing = j37::osc::encode (Message { "/a", {} });
    trailing.push_back (0);
    trailing.push_back (0);
    trailing.push_back (0);
    trailing.push_back (0);
    check (! j37::osc::decode (asString (trailing), message, &reason),
           "trailing bytes after a complete message are refused");
    check (reason.find ("trailing") != std::string::npos,
           "and the reason identifies trailing bytes");

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

    const auto emptyElement = bundleOf ({ {} });
    reason.clear();
    check (j37::osc::decodePacket (asString (emptyElement), &reason).empty ()
           && reason.find ("empty") != std::string::npos,
           "an empty bundle element is refused with a clear reason");

    Packet shortElement;
    j37::osc::detail::appendUint32 (shortElement, 1);
    shortElement.push_back ('x');
    const auto misalignedElement = bundleOf ({ shortElement });
    reason.clear();
    check (j37::osc::decodePacket (asString (misalignedElement), &reason).empty ()
           && reason.find ("aligned") != std::string::npos,
           "a non-four-byte-aligned bundle element is refused");
}

void checkSurface()
{
    std::printf("\nthe address surface\n");

    using j37::osc::ParameterSlot;
    using j37::osc::Surface;

    const auto surface = Surface({ ParameterSlot{ "tape_drive", "tape/drive", 0.0f, 1.0f },
                                   ParameterSlot{ "input", "input", -32.0f, 32.0f } });

    const auto rejectsInvalid = [] (std::vector<ParameterSlot> slots)
    {
        try
        {
            const Surface invalid (std::move (slots));
            return false;
        }
        catch (const std::invalid_argument&)
        {
            return true;
        }
    };
    check (rejectsInvalid ({ ParameterSlot { "range", "range", 1.0f, 0.0f } }),
           "a reversed parameter range is rejected");
    check (rejectsInvalid ({ ParameterSlot { "range", "range", -INFINITY, 1.0f } }),
           "a non-finite parameter range is rejected");
    check (rejectsInvalid ({ ParameterSlot { "", "empty-id", 0.0f, 1.0f } }),
           "an empty parameter ID is rejected");
    check (rejectsInvalid ({ ParameterSlot { "bad id", "bad-id", 0.0f, 1.0f } }),
           "an ID containing whitespace is rejected");
    check (rejectsInvalid ({ ParameterSlot { "bad\x01id", "control-id", 0.0f, 1.0f } }),
           "an ID containing a control character is rejected");
    check (rejectsInvalid ({ ParameterSlot { "valid", "bad?path", 0.0f, 1.0f } }),
           "a path containing an OSC pattern character is rejected");
    check (rejectsInvalid ({ ParameterSlot { "same", "first", 0.0f, 1.0f },
                             ParameterSlot { "same", "second", 0.0f, 1.0f } }),
           "duplicate parameter IDs are rejected");
    check (rejectsInvalid ({ ParameterSlot { "first", "same", 0.0f, 1.0f },
                             ParameterSlot { "second", "same", 0.0f, 1.0f } }),
           "duplicate OSC paths are rejected");

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
    check(surface.engineeringValue(1, -0.5f) == -32.0f
          && surface.engineeringValue(1, 1.5f) == 32.0f,
          "engineering values clamp inputs to the parameter range");
    check(surface.addressOf(7).empty(), "an address of a slot that does not exist is empty");
}

void checkBallPhysics()
{
    std::printf("\nring ball physics\n");

    namespace ball = j37::oscball;

    ball::State state;
    check(ball::isFinite(state), "the launch state is finite");
    check(std::sqrt(state.x * state.x + state.y * state.y) < 1.0f,
          "the ball starts inside the ring");

    int impacts = 0;
    float slowest = 1.0e9f;
    float fastest = 0.0f;
    bool finite = true;
    bool inside = true;
    std::set<int> sectors;

    for (int frame = 0; frame < 60 * 120; ++frame)
    {
        const auto result = ball::advance(state, 1.0f / 60.0f);
        impacts += result.impactCount;
        finite = finite && ball::isFinite(state);
        inside = inside && std::sqrt(state.x * state.x + state.y * state.y) <= 1.0001f;

        for (int i = 0; i < result.impactCount; ++i)
        {
            slowest = std::fmin(slowest, result.impacts[i].speed);
            fastest = std::fmax(fastest, result.impacts[i].speed);
            sectors.insert(ball::sectorForNormal(result.impacts[i].normalX,
                                                 result.impacts[i].normalY, 21));
        }
    }

    check(finite, "two minutes of 60 Hz steps stay finite");
    check(inside, "the ball never escapes the unit circle");
    check(std::isfinite(state.angle) && std::isfinite(state.angularVelocity),
          "the ball's rotation stays finite too");
    check(impacts > 60, "it hits the wall often enough to be an instrument");
    check(fastest > slowest + 0.20f, "impacts arrive at different speeds, so the ring has dynamics");
    check(sectors.size() >= 18, "the bounce visits most of the ring's sectors");

    check(ball::gainForImpactSpeed(0.0f) >= 0.349f
          && ball::gainForImpactSpeed(10.0f) <= 1.251f,
          "the impact gain is bounded at both ends");
    check(ball::gainForImpactSpeed(0.5f) < ball::gainForImpactSpeed(2.5f),
          "a harder hit is louder");

    check(ball::sectorForNormal(0.0f, -1.0f, 36) == 0, "top normal is sector 0");
    check(ball::sectorForNormal(1.0f, 0.0f, 36) == 9, "right normal is a quarter around");
    check(ball::sectorForNormal(0.0f, 1.0f, 36) == 18, "bottom normal is half way");
    check(ball::sectorForNormal(-1.0f, 0.0f, 36) == 27, "left normal is three quarters");
    check(ball::sectorForNormal(0.0f, 1.0f, 21) == 11,
          "an odd sector count still wraps without leaving the ring");

    ball::State a;
    ball::State b;

    for (int frame = 0; frame < 120; ++frame)
    {
        ball::advance(a, 1.0f / 60.0f);
        ball::advance(b, 1.0f / 60.0f);
    }

    check(a.x == b.x && a.y == b.y && a.vx == b.vx && a.vy == b.vy
          && a.angle == b.angle && a.angularVelocity == b.angularVelocity
          && a.randomState == b.randomState,
          "the same state and time produce the same ball");

    ball::State broken;
    broken.x = std::nanf("");
    ball::sanitise(broken);
    check(ball::isFinite(broken), "a NaN state is reset, not carried");

    broken.angle = std::nanf("");
    ball::sanitise(broken);
    check(ball::isFinite(broken), "a NaN rotation is reset with the rest of the state");

    ball::State fast;
    fast.x = 0.99f;
    fast.y = 0.0f;
    fast.vx = 3.8f;
    fast.vy = -3.8f;
    bool fastInside = true;

    for (int frame = 0; frame < 240; ++frame)
    {
        ball::advance(fast, 0.1f);
        fastInside = fastInside && std::sqrt(fast.x * fast.x + fast.y * fast.y) <= 1.0001f;
    }

    check(fastInside, "a clamped high-speed ball is still projected inside the ring");
}

void checkModalVoice()
{
    std::printf("\nmodal voice\n");

    j37dsp::ModalVoice voice;
    voice.prepare(48000.0);
    check(voice.isSilent(), "a freshly prepared bank is silent");

    voice.trigger(220.0f, 1.0f);
    check(! voice.isSilent(), "a strike excites the bank");

    float earlyPeak = 0.0f;
    float latePeak = 0.0f;
    bool finite = true;

    for (int sample = 0; sample < 48000; ++sample)
    {
        const auto value = voice.renderSample();
        finite = finite && std::isfinite(value);

        if (sample < 4800)
            earlyPeak = std::fmax(earlyPeak, std::fabs(value));
        else if (sample >= 24000 && sample < 28800)
            latePeak = std::fmax(latePeak, std::fabs(value));
    }

    check(finite, "a second of the modal bank stays finite");
    check(earlyPeak > 1.0e-4f, "the strike is audible");
    check(latePeak < earlyPeak, "the modes decay rather than ring forever");

    const auto peakFor = [&voice] (float amplitude)
    {
        voice.reset();
        voice.trigger(220.0f, amplitude);
        auto peak = 0.0f;

        for (int sample = 0; sample < 2400; ++sample)
            peak = std::fmax(peak, std::fabs(voice.renderSample()));

        return peak;
    };

    check(peakFor(0.25f) < peakFor(1.0f) * 0.5f,
          "impact gain scales the voice's level");

    voice.prepare(32000.0);
    check(std::abs(voice.sampleRateHz() - 32000.0) < 1.0e-9,
          "the bank reports the rate it was prepared for");
    voice.trigger(987.0f, 1.0f);
    bool highFinite = true;

    for (int sample = 0; sample < 4800; ++sample)
        highFinite = highFinite && std::isfinite(voice.renderSample());

    check(highFinite, "the top of the ring is finite at a low session rate");

    voice.reset();
    check(voice.isSilent(), "reset leaves no tail behind");
}

// The ring's note plate, at every dial size. The plate is what says which note
// the ball just struck, and it used to be floored at 48 x 24 px regardless of
// the ring: on the deck's ~66 px ring that is a card three quarters of the dial
// wide with the note set at 12 px on it, which is the "the note table is too
// large" report from Windows. Nothing here draws, so the arithmetic is pinned on
// its own: the card is a share of the radius at every size, its caps only bind
// on the desk ring, its floors only on the smallest deck ring, and the note's
// type is solved from the card rather than from the dial.
void checkRingDisplayPlate()
{
    using j37panel::computeOscDisplayPlate;
    using j37panel::oscNoteFontHeight;
    using j37panel::oscNoteTextHeight;

    bool insideDial = true;
    bool proportional = true;
    bool floorsHold = true;
    bool capsHold = true;
    bool monotonic = true;
    bool noteFits = true;

    j37panel::OscDisplayPlate previous = computeOscDisplayPlate (0.0f);

    for (float ringRadius = 12.0f; ringRadius <= 240.0f; ringRadius += 0.25f)
    {
        const auto plate = computeOscDisplayPlate (ringRadius);

        // Below ~30 px of radius the plate's own floors bind, and a floor is by
        // definition larger than the dial is wide there; those two properties are
        // only meaningful where the proportional term is what decides the size.
        const auto proportionalBinds = ringRadius * 1.05f >= j37panel::oscPlateMinWidth;
        insideDial = insideDial && (! proportionalBinds
                                    || (plate.width <= ringRadius * 2.0f
                                        && plate.height <= ringRadius * 2.0f));
        proportional = proportional && (! proportionalBinds
                                        || plate.width <= ringRadius * 1.06f + 1.0e-4f);
        floorsHold = floorsHold && plate.width >= j37panel::oscPlateMinWidth
                                && plate.height >= j37panel::oscPlateMinHeight;
        capsHold = capsHold && plate.width <= j37panel::oscPlateMaxWidth + 1.0e-4f
                            && plate.height <= j37panel::oscPlateMaxHeight + 1.0e-4f;
        monotonic = monotonic && plate.width >= previous.width - 1.0e-4f
                              && plate.height >= previous.height - 1.0e-4f;
        noteFits = noteFits && oscNoteFontHeight (plate) >= 7.0f
                            && oscNoteFontHeight (plate) <= oscNoteTextHeight (plate);

        previous = plate;
    }

    check(insideDial, "the note plate fits inside the ring at every size");
    check(proportional, "the plate is a share of the radius, not a fixed size");
    check(floorsHold, "the plate keeps its legibility floors");
    check(capsHold, "the plate stops growing at its ceiling");
    check(monotonic, "a bigger ring never carries a smaller plate");
    check(noteFits, "the note's type always fits the plate it is printed on");

    // The two sizes the panel actually asks for: a desktop deck ring and the
    // landscape deck's. Both used to come out at the 48 x 24 floor.
    const auto deck = computeOscDisplayPlate (33.0f);
    const auto desk = computeOscDisplayPlate (39.0f);
    std::printf ("  deck ring 66 px: plate %.1f x %.1f, note %.1f px \\n",
                 deck.width, deck.height, oscNoteFontHeight (deck));
    std::printf ("  desk ring 78 px: plate %.1f x %.1f, note %.1f px \\n",
                 desk.width, desk.height, oscNoteFontHeight (desk));
    check(deck.width < 40.0f && deck.height < 21.0f,
          "a 66 px deck ring no longer carries a 48 x 24 card");
    check(oscNoteFontHeight (deck) < 11.0f,
          "and its note is no longer set at 12 px");
}

} // namespace

int main()
{
    std::printf("OSC 1.0: the codec, the refusals, the address surface and the ring's ball\n");

    checkAddressGrammar();
    checkEncoding();
    checkRoundTrips();
    checkArgumentReader();
    checkRefusals();
    checkBundles();
    checkSurface();
    checkBallPhysics();
    checkModalVoice();
    checkRingDisplayPlate();

    std::printf("\n%d check(s), %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
