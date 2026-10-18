// =============================================================================
//  Which optional libraries this build actually has.
//
//  Everything the engine uses that may be absent - chowdsp's math and DSP utils,
//  RTNeural, xsimd, readerwriterqueue, pocketfft and signalsmith-stretch - is
//  probed for HERE, once, and reported as a J37_HAS_* macro. Every one of those macros is read by a
//  stage, which is now checked: tests/linkline/check_linkline.py fails when a
//  probe has no reader, and when the link line carries a library no include
//  names. The stages that consume
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
// place once a benchmark shows what it buys, and a measured difference in the
// rendered audio is a change to the sound. See the use site - j37Tanh in
// Source/DSP/HotPathMath.h, which Source/DSP/SaturationCore.h's eleven tanh calls
// all go through - for what the flag actually switches, the J37_USE_SIMD_TANH
// option in CMakeLists.txt for how it is thrown, and tests/dsp/simd_tanh_test.cpp
// for what each of the three shapes renders.
//
// Two things about this probe were wrong until it was measured, and they are the
// reason the harness next door exists:
//
//   - the macro was read by a header and thrown by no build, so J37_HAS_XSIMD was
//     0 everywhere and the branch below was never compiled by anything;
//   - the branch it guards called xsimd::tanh's SCALAR entry point, which xsimd
//     defines as `using std::tanh;` - i.e. the flag, once thrown, would have
//     switched nothing. The approximation exists only in the batch kernel, which
//     is what HotPathMath.h now calls.
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
//  It is included behind a __has_include guard AND a harness guard, exactly like
//  chowdsp above, because the harnesses compile these stages against a shim that
//  has neither JUCE's full include path nor any of these libraries. The engine
//  must therefore stay compilable with J37_HAS_RWQ 0, and the use site must have
//  a portable substitution rather than assuming the library is present.
//
//    fatoml::ReaderWriterQueue  cameron314/readerwriterqueue - the lock-free
//                               single-producer/single-consumer queue. Used for
//                               the audio->UI telemetry stream, so the audio
//                               thread can publish a frame of metering and the
//                               editor can read it WITHOUT every field being its
//                               own atomic and without a lock on the audio thread.
//                               It is the ONLY one of the four probed in this
//                               block: every use site reads J37_HAS_RWQ and falls
//                               back to plain atomics without it.
//
//    The other three left this block, and two of them left the hard way: they
//    were LINKED, INCLUDED and probed here, and read by nothing.
//
//    farbot::RealtimeObject     The entry used to claim the machine-state
//                               hand-off used it, so the message thread could
//                               install a whole engine configuration without the
//                               audio thread blocking. No such hand-off exists:
//                               the engine's state is written by the audio
//                               thread from atomic parameters, and the only
//                               cross-thread object is the telemetry frame,
//                               which readerwriterqueue already carries. With no
//                               site to adopt it, farbot came off the link line
//                               and out of this file; cmake/J37Dependencies.cmake
//                               still fetches it, so a stage that needs a
//                               realtime-safe object is one line and one probe
//                               away.
//
//    signalsmith-stretch        A phase-vocoder time stretcher: it changes how
//                               long material takes while keeping its PITCH.
//                               This block used to say two things about it, and
//                               both were false. The transport does not use it -
//                               the transport's speed change is
//                               Source/DSP/RepitchStage.h, which is deliberately
//                               a VARISPEED, because a tape at half speed is
//                               supposed to come out an octave down. And the
//                               macro that said whether the build had it was
//                               never probed, so it read 0 everywhere and said
//                               it to nobody.
//
//                               It has since come back as a stage: the CORRECTOR
//                               moves a note onto the grid without changing how
//                               long it lasts, which is a pitch shift at
//                               constant duration, and Source/DSP/CorrectorStage.h
//                               is that stage - with this library as an OPT-IN
//                               engine and the shifter above as its default. Its
//                               probe is at the bottom of this file rather than
//                               in this block, because what it needs is an
//                               include path, a flag and a fallback rather than a
//                               real-time utility with a JUCE-laden substitute.
//
//    SoundTouch                 The WSOLA time-stretch engine, LGPL-2.1 - the one
//                               dependency in this file that was not
//                               permissively licensed. It was probed, included
//                               and compiled into the plugin, and no stage
//                               called it, so it was contributing nothing but a
//                               static LGPL link to the binary. That link is
//                               gone; THIRD_PARTY_NOTICES.md records the
//                               library, the fetch stays for the same reason
//                               signalsmith's does, and the licence question it
//                               raised with it.
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

#else
 #define J37_HAS_RWQ 0
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

// ------------------------------------------------------------------------------
//  signalsmith-stretch (vendored under ThirdParty/signalsmith-stretch, with the
//  sibling signalsmith-linear checkout inside it that its own header includes by
//  a relative path).
//
//  OPT-IN, exactly like xsimd above and for the same kind of reason: the two
//  engines do not render the same audio, so which one the build uses is a
//  listening decision that belongs in CMakeLists.txt rather than in a probe. The
//  flag is J37_CORRECTOR_USE_STRETCH (OFF by default), and the numbers behind
//  "off by default" are in the option's own comment: the vocoder costs 1216
//  frames of lookahead at 48 kHz against the granular shifter's one period, and
//  this library's transposition measured 10 cents wide of the ratio it was given
//  at 1.06x and 6 cents at 0.94x (tests/pitch prints both), which is the same
//  order as the +-10 cent dead zone the control is built around.
//
//  NOT behind the harness guard, because that guard exists so a stage can be
//  compiled against a shim with no optional library on the include path - and the
//  way to compile the library shape is to ask for it, which is what
//  tests/pitch/run.sh's second binary and the CMake option both do. A guard that
//  forced the macro to 0 would make that impossible.
//
//  And like pocketfft, the file that only decides the macro includes nothing:
//  Source/DSP/CorrectorStage.h includes the library ITSELF when this is 1, so a
//  checkout that has pruned ThirdParty/ carries the fallback engine and none of
//  the four-thousand-line header.
// ------------------------------------------------------------------------------
#if ! defined (J37_HAS_SIGNALSMITH)
 #if defined (J37_CORRECTOR_USE_STRETCH) \
     && __has_include (<signalsmith-stretch/signalsmith-stretch.h>)
  #define J37_HAS_SIGNALSMITH 1
 #else
  #define J37_HAS_SIGNALSMITH 0
 #endif
#endif
