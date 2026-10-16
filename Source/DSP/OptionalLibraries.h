// =============================================================================
//  Which optional libraries this build actually has.
//
//  Everything the engine uses that may be absent - chowdsp's math and DSP utils,
//  RTNeural, xsimd, and the four real-time-safe third-party utilities - is probed
//  for HERE, once, and reported as a J37_HAS_* macro. The stages that consume
//  those macros live in headers of their own under Source/DSP/, and a harness
//  compiles those headers with neither JUCE nor the libraries on its include
//  path, so the probe cannot live in the plugin's header any more: a stage that
//  reads a macro has to be able to find the file that decides it.
//
//  It is therefore the FIRST include of Source/PluginProcessor.h, and it is
//  included directly by the stage headers that need one of its answers
//  (Source/DSP/NeuralStage.h is the first). It includes nothing itself beyond the
//  libraries it finds, and it pulls in no JUCE - the conventions of Source/DSP/
//  are stated in J37Math.h.
//
//  A build may also DECIDE one of these rather than probe it: tests/neural passes
//  -DJ37_HAS_RTNEURAL=1 and stands in for RTNeural's own two-method API, which is
//  the only way the guarded half of the neural stage is compiled on a machine
//  that has no RTNeural. Every probe below is written so a macro that is already
//  defined wins, and the library is then that build's concern rather than this
//  file's.
// =============================================================================
#pragma once

// chowdsp_utils (fetched by CPM in CMakeLists.txt). The approximation helpers
// the engine uses live in the header-only chowdsp_math module; the include is
// conditional so the DSP regression harness - which compiles these headers
// against a shim, without chowdsp on its include path - is never asked for it.
//
// chowdsp_dsp_utils is pulled in alongside it. It is a full JUCE module rather
// than a header, so it needs the plugin target's include path, which the harness
// does not provide; the same guard covers both. What the engine uses from it is
// named where it is used.
#if ! defined (J37_DSP_HARNESS) && ! defined (J37_HAS_CHOWDSP_MATH) \
    && __has_include (<chowdsp_math/chowdsp_math.h>)
 #include <chowdsp_math/chowdsp_math.h>
 #define J37_HAS_CHOWDSP_MATH 1
#elif ! defined (J37_HAS_CHOWDSP_MATH)
 #define J37_HAS_CHOWDSP_MATH 0
#endif

#if ! defined (J37_DSP_HARNESS) && ! defined (J37_HAS_CHOWDSP_DSP) \
    && __has_include (<chowdsp_dsp_utils/chowdsp_dsp_utils.h>)
 #include <chowdsp_dsp_utils/chowdsp_dsp_utils.h>
 #define J37_HAS_CHOWDSP_DSP 1
#elif ! defined (J37_HAS_CHOWDSP_DSP)
 #define J37_HAS_CHOWDSP_DSP 0
#endif

// RTNeural (fetched by CPM in CMakeLists.txt). The real-time neural inference
// engine behind Chowdhury's amp and pedal models. It is wired in here exactly
// like the other optional dependencies: behind a __has_include probe AND a
// harness guard, with a portable no-op substitution when it is absent, so the
// DSP regression harness - which never sees RTNeural - still compiles and still
// renders the same audio the engine produced before the model stage existed.
//
// What the engine uses it for is named at the use site: NeuralStage, in
// Source/DSP/NeuralStage.h, runs a small feed-forward model as a per-channel
// nonlinearity. When no model is loaded the stage is a bit-for-bit pass-through,
// so the plugin's sound does not depend on a model being present.
#if defined (J37_HAS_RTNEURAL)
 // A build has decided this for itself (tests/neural passes it on the command
 // line, with RTNeural's API stood in for), so there is nothing to probe and no
 // library to include - the stage's .nam converter asks for the JSON type it
 // needs by including it ITSELF when this macro is 1.
#elif ! defined (J37_DSP_HARNESS) && __has_include (<RTNeural/RTNeural.h>)
 #include <RTNeural/RTNeural.h>
 // RTNeural's own model loader header, which is what carries json_parser. It is
 // not pulled in by RTNeural.h, so a translation unit that wants to LOAD a model
 // has to ask for it explicitly. nlohmann/json is on the same include path (and
 // RTNeural bundles a copy), so the JSON type it parses into is already available.
 #include <RTNeural/model_loader.h>
 #include <nlohmann/json.hpp>
 #define J37_HAS_RTNEURAL 1
#else
 #define J37_HAS_RTNEURAL 0
#endif

// xsimd (fetched by CPM in CMakeLists.txt). Portable SIMD wrappers, and the
// vehicle for the one approximation the review flagged as worth having: a
// vectorised tanh. The scalar shaper calls std::tanh four times per sample per
// channel (two branches, each also evaluated at the bias-only point so the DC
// pedestal can be subtracted), and that is the hot loop at 8x oversampling.
//
// It is wired in as an OPT-IN, not a default: an approximation only earns its
// place once a benchmark shows the shaper dominates, and a measured difference
// in the rendered audio is a change to the sound. See the use site in
// PluginProcessor.cpp for what the flag actually switches.
#if ! defined (J37_DSP_HARNESS) && ! defined (J37_HAS_XSIMD) \
    && defined (J37_USE_SIMD_TANH) && __has_include (<xsimd/xsimd.hpp>)
 #include <xsimd/xsimd.hpp>
 #define J37_HAS_XSIMD 1
#elif ! defined (J37_HAS_XSIMD)
 #define J37_HAS_XSIMD 0
#endif

// ------------------------------------------------------------------------------
//  Real-time-safe third-party utilities.
//
//  Each of these is included behind a __has_include guard AND a harness guard,
//  exactly like chowdsp above, because the harnesses compile these stages against
//  a shim that has neither JUCE's full include path nor any of these libraries.
//  The engine must therefore stay compilable with all four macros 0, and every use
//  site must have a portable substitution rather than assuming the library is
//  present.
//
//    fatoml::ReaderWriterQueue  cameron314/readerwriterqueue - the lock-free
//                               single-producer/single-consumer queue. Used for
//                               the audio->UI telemetry stream, so the audio
//                               thread can publish a frame of metering and the
//                               editor can read it WITHOUT every field being its
//                               own atomic and without a lock on the audio thread.
//
//    farbot::RealtimeObject     hogliux/farbot - a realtime-safe object holder.
//                               Used for the machine-state hand-off, so the
//                               message thread can install a whole new engine
//                               configuration without the audio thread ever
//                               blocking on it.
//
//    signalsmith-stretch        Signalsmith Audio - a real-time phase-vocoder
//                               time stretcher: it changes how long material
//                               takes while keeping its PITCH.
//
//                               This entry used to claim the transport used it
//                               "so a speed change is a genuine TIME change (the
//                               tape slows down and the music slows with it)
//                               rather than a resample that also drops the pitch
//                               of everything". That claim was false twice over:
//                               no file under Source/ included the header, and
//                               J37_HAS_SIGNALSMITH was defined to 0 and never
//                               probed - so the macro reported "absent" in a
//                               build that had the library on its include path,
//                               and reported it to nobody.
//
//                               It was also backwards about the sound. The
//                               transport's speed change is
//                               Source/DSP/RepitchStage.h, which is deliberately
//                               a VARISPEED: a tape read at half speed comes out
//                               an octave down, and that falling pitch IS the
//                               effect the transport is asked for. A stretch that
//                               preserved the pitch would delete it.
//
//                               The one stage that wants this library is the
//                               CORRECTOR. Moving a note onto the grid without
//                               changing how long it lasts is a pitch shift at
//                               constant duration, which is exactly what this is,
//                               and AUTOTUNE does that today with the hand-written
//                               granular shifter in
//                               Source/DSP/RealtimePitchShifter.h. Adopting it
//                               there is a change to the sound and belongs to its
//                               own piece of work, not to a probe.
//
//    SoundTouch                 Olli Parviainen's SoundTouch Audio Processing
//                               Library - the WSOLA time-stretch and pitch-shift
//                               engine. Vendored under ThirdParty/soundtouch and
//                               compiled into the plugin by
//                               cmake/J37Dependencies.cmake. The one dependency
//                               in this list that is NOT permissively licensed:
//                               LGPL-2.1, with the licence text distributed at
//                               ThirdParty/soundtouch/COPYING.TXT and the
//                               attribution in THIRD_PARTY_NOTICES.md.
//
//                               Like signalsmith-stretch above it is probed and
//                               included here and READ BY NOTHING: it is the
//                               second engine in this file whose presence the
//                               build pays for and no stage calls. It is left in
//                               place rather than removed, and the probe below is
//                               left honest about what it finds - the same pair of
//                               statements the analyser stage makes about
//                               pocketfft by USING it.
// ------------------------------------------------------------------------------
#if ! defined (J37_DSP_HARNESS)

 // readerwriterqueue: the header sits at the repository ROOT and the CMake target
 // is INTERFACE with that root as its include directory, so it is spelled
 // <readerwriterqueue.h>, not <readerwriterqueue/readerwriterqueue.h>. Both
 // spellings are probed, because some packaging layouts nest it under the project
 // name and the guard must not depend on which of them the fetch produced.
 #if __has_include (<readerwriterqueue.h>)
  #include <readerwriterqueue.h>
  #define J37_HAS_RWQ 1
 #elif __has_include (<readerwriterqueue/readerwriterqueue.h>)
  #include <readerwriterqueue/readerwriterqueue.h>
  #define J37_HAS_RWQ 1
 #else
  #define J37_HAS_RWQ 0
 #endif

 // farbot: its headers live under include/farbot, and the CMake target adds
 // include/ to the path, so the spelling is <farbot/...>. Two of its headers are
 // useful here - the realtime-safe FIFO and the RealtimeObject that guards it.
 #if __has_include (<farbot/fifo.hpp>)
  #include <farbot/fifo.hpp>
  #define J37_HAS_FARBOT 1
 #else
  #define J37_HAS_FARBOT 0
 #endif

 // signalsmith-stretch: header-only, and its header includes its own
 // signalsmith-linear sibling by relative path (see cmake/J37Dependencies.cmake,
 // which fetches that sibling for exactly this reason), so ONE include directory
 // is what the CMake target exposes. The probe only DECIDES the macro - the
 // header is included by whatever uses it, which is nothing today, and the
 // corrected entry above says which stage that should be.
 //
 // It was missing until now. J37_HAS_SIGNALSMITH was written only in the harness
 // branch at the bottom of this file (as 0), so no build could ever say it had
 // the library whatever was on its include path - the macro's single caller is a
 // comment. A probe that is never written is not a conservative default; it is a
 // statement about the build that nobody checked.
 #if __has_include (<signalsmith-stretch/signalsmith-stretch.h>)
  #define J37_HAS_SIGNALSMITH 1
 #else
  #define J37_HAS_SIGNALSMITH 0
 #endif

 // SoundTouch: its public headers sit flat in include/, and that one directory
 // is what the CMake target exposes, so the spelling is the bare
 // <SoundTouch.h>. Probed like the rest, so a checkout with ThirdParty/ pruned
 // reports 0 instead of failing the compile.
 #if __has_include (<SoundTouch.h>)
  #include <SoundTouch.h>
  #define J37_HAS_SOUNDTOUCH 1
 #else
  #define J37_HAS_SOUNDTOUCH 0
 #endif

#else
 #define J37_HAS_RWQ 0
 #define J37_HAS_FARBOT 0
 #define J37_HAS_SIGNALSMITH 0
 #define J37_HAS_SOUNDTOUCH 0
#endif

// ------------------------------------------------------------------------------
//  pocketfft (mreineck/pocketfft, vendored under ThirdParty/pocketfft as the one
//  header pocketfft_hdronly.h).
//
//  Deliberately NOT behind the harness guard the utilities above sit behind, and
//  deliberately does not include anything. Source/DSP/SpectrumAnalyser.h includes
//  the header ITSELF when this macro is 1 - the way Source/DSP/NeuralStage.h
//  includes RTNeural's loader itself - so a file that only decides a macro stays
//  free of a four-thousand-line header no other stage wants.
//
//  Unguarded is what lets a harness opt IN by putting ThirdParty/pocketfft on its
//  include path and opt OUT by leaving it off.
//  tests/spectrum/run.sh compiles the same stage BOTH ways and asserts what each
//  build reports, which is the only way the absent-library branch of a header is
//  ever compiled at all.
//
//  pocketfft was on the link line long before anything included it. That is the
//  state the review flagged (sixteen entries with no caller) and the reason the
//  analyser stage exists: an FFT this build already carries is worth calling.
// ------------------------------------------------------------------------------
#if ! defined (J37_HAS_POCKETFFT)
 #if __has_include (<pocketfft_hdronly.h>)
  #define J37_HAS_POCKETFFT 1
 #else
  #define J37_HAS_POCKETFFT 0
 #endif
#endif
