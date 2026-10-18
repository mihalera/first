// =============================================================================
//  NoiseDecoder - a WAV file, decoded into the loop the vinyl stage plays.
//
//  The first thing in Source/DSP/ that decodes a FILE, and it is deliberately
//  JUCE-free: a noise print is bytes and arithmetic, and the part of the feature
//  that can be wrong - which sample of the file is which sample of the loop, at
//  which rate, and what a malformed file does - is the part a harness can hold
//  without a plugin, a host or a sound card. Source/DSP/NoiseLoader.h is the
//  part that needs JUCE (a File, a background thread, an atomic hand-off), and
//  it does none of the arithmetic here.
//
//  The library is dr_wav, from mackron/dr_libs, vendored under
//  ThirdParty/dr_libs/ because a harness that has to build BOTH WAYS - with the
//  decoder and without it - cannot depend on a configure having run. Its
//  implementation is compiled in exactly one translation unit,
//  Source/DSP/NoiseDecoder.cpp, which defines DR_WAV_IMPLEMENTATION before
//  including this header; every other user gets declarations.
//
//  The conventions of Source/DSP/ are stated in J37Math.h. This header includes
//  what it needs and names nothing of JUCE, so tests/dsp compiles it against a
//  plain C++17 compiler.
// =============================================================================
#pragma once

#include "OptionalLibraries.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#if J37_HAS_DRWAV
 #include <dr_wav.h>
#endif

namespace j37dsp
{

//==============================================================================
/**
    A noise print, as the vinyl stage plays it: two channels of samples at the
    engine's rate, indexed modulo the length so the loop never runs off its end.

    Stereo because the consumer is a stereo stage; a mono file is duplicated to
    both sides, which is what a mono print on a turntable actually is - the same
    surface read by one stylus.
*/
struct NoisePrint
{
    std::vector<float> left;
    std::vector<float> right;

    /** Frames in the loop. Both vectors hold at least this many samples. */
    std::size_t length = 0;

    /** The rate the samples are at. Set by whatever produced this print. */
    double sampleRate = 0.0;

    /** The rate the FILE was at, which is what a user needs to know when a
        44.1 kHz print is playing in a 96 kHz session. Equal to `sampleRate`
        until something converts the loop, and preserved across the conversion. */
    double sourceRate = 0.0;

    /** Frames the file held before any rate conversion - the diagnostic a user
        reads when the loop sounds shorter than the file they picked. */
    std::size_t sourceFrames = 0;

    bool empty() const noexcept
    {
        return length == 0 || left.size() < length || right.size() < length;
    }

    /** One sample of the loop. `index` wraps, so the consumer owns no state but
        its own position. Callers check `empty()` first; this does not. */
    float sample (int channel, std::size_t index) const noexcept
    {
        const auto i = index % length;
        return channel == 0 ? left[i] : right[i];
    }
};

//==============================================================================
/** Why a decode did or did not produce a loop. The strings are what the panel
    and the harness print, so they are part of the interface rather than debug
    output dressed up as an enum. */
enum class NoiseDecodeStatus
{
    ok,
    absent,      // this build has no decoder at all (J37_HAS_DRWAV 0)
    unreadable,  // dr_wav refused the bytes
    noFrames,    // a valid header with nothing in it
    nonFinite,   // the file holds NaN or infinity
    tooLong      // over the cap below, and refused rather than truncated
};

struct NoiseDecodeOutcome
{
    NoiseDecodeStatus status = NoiseDecodeStatus::absent;
    const char* reason = "";
    std::size_t frames = 0;      // frames decoded, 0 unless status is ok
    int fileChannels = 0;        // what the FILE had, before folding to a pair
};

/** The longest print the loader will accept, in seconds at the engine's rate.
    A surface-noise loop is seconds long; anything near a minute is either a
    mistake or a whole track, and both are refused with a reason rather than
    turned into an allocation the audio thread will then read. */
inline constexpr double maxNoisePrintSeconds = 60.0;

/** Declared before `decodeWavMemory` because a decode that converts rates uses
    it; defined below, beside the arithmetic it describes. */
inline bool resamplePrint (const NoisePrint& in, double targetRate, NoisePrint& out);

//==============================================================================
/**
    Decode WAV bytes into a loop.

    `targetRate` is the rate the loop will be PLAYED at; pass 0 to keep the
    file's own rate (what the loader does, since a print decoded once can be
    resampled for any engine rate without re-reading the file). `out` is only
    modified on success.

    What is deliberately NOT done:

      * no normalisation and no clipping. A print is a recording of a surface,
        and its level is what the record's noise floor was; rescaling it would
        make two prints of the same surface at different levels sound identical,
        which is the opposite of what the control is for. Float files outside
        [-1, 1) are accepted as they are.
      * no channel mixing. A file with more than two channels keeps the first
        two, because a three-channel print has no meaning here and summing it
        would invent one.

    What IS refused, with a reason the caller can print: an empty buffer, bytes
    dr_wav will not parse, a header with no frames, a file containing NaN or
    infinity, and anything over `maxNoisePrintSeconds`.
*/
inline NoiseDecodeOutcome decodeWavMemory (const void* data, std::size_t size,
                                           NoisePrint& out, double targetRate = 0.0)
{
#if ! J37_HAS_DRWAV
    // The caller still has to be able to say WHY nothing loaded. A build that
    // pruned ThirdParty/ gets the same code path, one status earlier.
    (void) data;
    (void) size;
    (void) targetRate;
    (void) out;
    return { NoiseDecodeStatus::absent,
             "this build has no WAV decoder (dr_wav is not on the include path)", 0, 0 };
#else
    if (data == nullptr || size == 0)
        return { NoiseDecodeStatus::unreadable, "the file is empty", 0, 0 };

    drwav wav;

    if (! drwav_init_memory (&wav, data, size, nullptr))
        return { NoiseDecodeStatus::unreadable, "this is not a WAV file dr_wav can read", 0, 0 };

    struct Cleanup
    {
        drwav& w;
        ~Cleanup() { drwav_uninit (&w); }
    } cleanup { wav };

    const auto fileChannels = static_cast<int> (wav.channels);
    const auto sourceFrames = static_cast<std::size_t> (wav.totalPCMFrameCount);

    if (sourceFrames == 0 || fileChannels <= 0)
        return { NoiseDecodeStatus::noFrames, "the WAV file has no audio frames", 0, fileChannels };

    const auto sourceRate = wav.sampleRate > 0 ? static_cast<double> (wav.sampleRate) : 0.0;
    const auto rate = targetRate > 0.0 ? targetRate : sourceRate;

    if (rate <= 0.0)
        return { NoiseDecodeStatus::unreadable, "the WAV file declares no sample rate", 0, fileChannels };

    // The cap is checked BEFORE the allocation, and on the number of frames that
    // would be played rather than the number in the file, so a decimated file
    // cannot arrive under the limit and then be stretched past it.
    const auto playedFrames = static_cast<std::size_t> (
        std::llround (static_cast<double> (sourceFrames) * rate / sourceRate));

    if (static_cast<double> (playedFrames) > maxNoisePrintSeconds * rate)
        return { NoiseDecodeStatus::tooLong,
                 "the print is longer than a minute - a noise loop is seconds, not a track", 0, fileChannels };

    const auto usedChannels = std::min (2, fileChannels);
    std::vector<float> interleaved (sourceFrames * static_cast<std::size_t> (fileChannels), 0.0f);

    const auto framesRead = drwav_read_pcm_frames_f32 (&wav, sourceFrames, interleaved.data());

    if (framesRead == 0)
        return { NoiseDecodeStatus::noFrames, "the WAV file has a header but no samples", 0, fileChannels };

    for (const auto value : interleaved)
    {
        // Checked on the whole buffer, not just the frames that are kept: a NaN
        // in the third channel of a five-channel file still means the file is
        // not a noise print, and silently dropping it would hide a damaged one.
        if (! std::isfinite (value))
            return { NoiseDecodeStatus::nonFinite,
                     "the file contains samples that are not finite (NaN or infinity)", 0, fileChannels };
    }

    NoisePrint decoded;
    decoded.sampleRate = sourceRate;
    decoded.sourceRate = sourceRate;
    decoded.sourceFrames = static_cast<std::size_t> (framesRead);
    decoded.length = decoded.sourceFrames;
    decoded.left.assign (decoded.length, 0.0f);
    decoded.right.assign (decoded.length, 0.0f);

    const auto stride = static_cast<std::size_t> (fileChannels);

    for (std::size_t i = 0; i < decoded.length; ++i)
    {
        decoded.left[i] = interleaved[i * stride];

        // A mono file is read by both sides, because a mono print IS the same
        // surface on both sides - not two decorrelated prints, which is what
        // inventing the second channel would produce.
        decoded.right[i] = usedChannels > 1 ? interleaved[i * stride + 1] : decoded.left[i];
    }

    if (targetRate > 0.0 && std::abs (sourceRate - targetRate) > 1.0e-6)
    {
        NoisePrint converted;

        if (! resamplePrint (decoded, targetRate, converted))
            return { NoiseDecodeStatus::unreadable, "the print could not be converted to the engine rate", 0, fileChannels };

        converted.sourceFrames = decoded.sourceFrames;
        out = std::move (converted);
    }
    else
    {
        out = std::move (decoded);
    }

    return { NoiseDecodeStatus::ok, "", out.length, fileChannels };
#endif
}

//==============================================================================
/**
    Convert a loop to another rate, by linear interpolation with the loop's own
    end wrapping into its own start.

    The wrap matters: a print is played as a LOOP, so interpolating across the
    seam with a clamp would put a one-sample crack at every repeat - the one
    place in the plugin where a click is not wanted. Linear rather than a windowed
    sinc because the material is broadband noise: a 44.1 kHz print played at
    96 kHz loses its top octave above ~22 kHz, which is above what a surface
    noise print carries in the first place, and the cost of a sinc kernel on a
    loop of seconds would be paid at every prepare.

    `in` must not be empty; an empty input returns false and leaves `out`
    untouched.
*/
inline bool resamplePrint (const NoisePrint& in, double targetRate, NoisePrint& out)
{
    if (in.empty() || targetRate <= 0.0 || in.sampleRate <= 0.0)
        return false;

    if (std::abs (in.sampleRate - targetRate) <= 1.0e-6)
    {
        out = in;
        out.sampleRate = targetRate;
        return true;
    }

    const auto ratio = in.sampleRate / targetRate;   // source samples per output sample
    const auto outLength = static_cast<std::size_t> (
        std::max<long long> (1, std::llround (static_cast<double> (in.length) / ratio)));

    NoisePrint converted;
    converted.sampleRate = targetRate;
    // The file's own rate travels with the loop: a panel that says "44.1 kHz file,
    // playing in a 96 kHz session" is reading this field, not the one above.
    converted.sourceRate = in.sourceRate > 0.0 ? in.sourceRate : in.sampleRate;
    converted.sourceFrames = in.sourceFrames;
    converted.length = outLength;
    converted.left.resize (outLength);
    converted.right.resize (outLength);

    for (std::size_t i = 0; i < outLength; ++i)
    {
        const auto position = static_cast<double> (i) * ratio;
        const auto index = static_cast<std::size_t> (position);
        const auto next = (index + 1) % in.length;
        const auto fraction = static_cast<float> (position - static_cast<double> (index));

        for (int c = 0; c < 2; ++c)
        {
            const auto a = in.sample (c, index);
            const auto b = in.sample (c, next);
            (c == 0 ? converted.left : converted.right)[i] = a + (b - a) * fraction;
        }
    }

    out = std::move (converted);
    return true;
}

} // namespace j37dsp
