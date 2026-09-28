# ==============================================================================
#
#   J37 third-party dependencies.
#
#   Everything the plugin fetches from the network lives here, and nowhere else.
#   The top-level CMakeLists.txt has already added JUCE and defined the target
#   name; this file is included AFTER that, so a package that needs JUCE on its
#   own cmake path (chowdsp_utils, for example) finds it.
#
#   Split out of the top-level file so that the dependency story is one file to
#   read instead of one 300-line stretch of a 1300-line file. The order of the
#   packages below is NOT arbitrary - see the note on each one - so this file is
#   meant to be read top to bottom.
#
#   CPM configuration
#   -----------------
#   Reproducibility first, network second. Everything here is about making a
#   configure work the same way twice, and about surviving a GitHub outage.
#
#    CPM_SOURCE_CACHE - where CPM keeps the checkouts it downloads. Without this
#    every configure starts from scratch in build/_deps, so a clean rebuild
#    re-clones every dependency and a machine without network cannot build at
#    all. Pointing it at a directory that survives a build wipe means the second
#    configure is instant and the build works offline. Set it from the
#    environment (CPM_SOURCE_CACHE) if you prefer it outside the tree.
#
#    CPM_USE_LOCAL_PACKAGES - off deliberately. A system-installed chowdsp or
#    xsimd of the wrong version would silently win over the pinned one, which is
#    exactly the "works on my machine" failure the pinning exists to prevent.
#
#    J37_OFFLINE - when ON, CPM is forbidden from reaching the network at all.
#    It then builds ONLY from what is already in CPM_SOURCE_CACHE, and a missing
#    package fails with a clear message instead of a timeout. This is the switch
#    for air-gapped machines and for CI runs that should not depend on GitHub.
#
#   On pinning: the tags below are tags, not hashes. A tag is a moving reference
#   - upstream can retag it - so the last word in reproducibility is to replace
#   each GIT_TAG with the 40-character commit SHA it currently points at. That is
#   a deliberate one-line change per package and is left to the reader rather
#   than done blindly here, because a wrong SHA breaks the build for everyone.
#   To get the SHA for a tag:
#
#       git ls-remote --tags <repo-url> <tag>
#
#   melatonin_inspector and foleys_gui_magic are already pinned that way (they
#   publish no usable tags).
#
# ==============================================================================

set(CPM_DOWNLOAD_VERSION 0.40.2)

if(NOT DEFINED CPM_SOURCE_CACHE AND DEFINED ENV{CPM_SOURCE_CACHE})
    set(CPM_SOURCE_CACHE "$ENV{CPM_SOURCE_CACHE}" CACHE PATH "CPM package cache")
endif()
if(NOT DEFINED CPM_SOURCE_CACHE)
    set(CPM_SOURCE_CACHE "${CMAKE_SOURCE_DIR}/.cpm-cache" CACHE PATH
        "CPM package cache - survives a build/ wipe, enables offline configures")
endif()
set(CPM_USE_LOCAL_PACKAGES OFF CACHE BOOL "Never prefer a system package over the pinned one")

option(J37_OFFLINE "Build only from CPM_SOURCE_CACHE; never touch the network" OFF)
if(J37_OFFLINE)
    set(CPM_LOCAL_PACKAGES_ONLY ON CACHE BOOL "" FORCE)
    message(STATUS "J37_OFFLINE=ON: CPM will use only cached packages")
endif()

if(NOT EXISTS "${CMAKE_BINARY_DIR}/cmake/CPM.cmake")
    if(J37_OFFLINE)
        message(FATAL_ERROR
            "J37_OFFLINE=ON but CPM.cmake itself is not present at "
            "${CMAKE_BINARY_DIR}/cmake/CPM.cmake.\n"
            "Run one online configure first, or copy CPM.cmake there by hand.")
    endif()
    file(DOWNLOAD
         "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake"
         "${CMAKE_BINARY_DIR}/cmake/CPM.cmake")
endif()
include("${CMAKE_BINARY_DIR}/cmake/CPM.cmake")

# ------------------------------------------------------------------------------
#  Header-only checkouts that ship no CMake at all are registered through this
#  one helper: an INTERFACE target carrying a SYSTEM include path, so the link
#  line in the top-level file reads like every other dependency and the
#  third-party headers stay out of the warning sweep.
#
#  Defined here because every use of it is in this file. The top-level file does
#  not need it and no longer defines it.
# ------------------------------------------------------------------------------
function(j37_declare_header_only_library name include_dir)
    if(NOT TARGET ${name})
        add_library(${name} INTERFACE)
        target_include_directories(${name} SYSTEM INTERFACE "${include_dir}")
    endif()
endfunction()

# ==============================================================================
#  Graphics and DSP toolboxes - configured packages
# ==============================================================================

# chowdsp_utils: linked as JUCE modules (chowdsp::chowdsp_dsp_utils,
# chowdsp::chowdsp_math) - the official integration is a plain add_subdirectory
# AFTER add_subdirectory(JUCE), which the include order in the top-level file
# guarantees.
CPMAddPackage(
    NAME chowdsp_utils
    GITHUB_REPOSITORY Chowdhury-DSP/chowdsp_utils
    GIT_TAG v2.4.0
    EXCLUDE_FROM_ALL YES
    SYSTEM YES
    OPTIONS
        "CHOWDSP_ENABLE_TESTING OFF"
        "CHOWDSP_ENABLE_BENCHMARKS OFF"
        "CHOWDSP_ENABLE_EXAMPLES OFF")

# melatonin_inspector: a JUCE module, fetched with a pinned commit hash (the
# repository publishes no version tags). Its CMake calls juce_add_module on its
# own directory, and JUCE derives the module name - and therefore the header it
# must find, melatonin_inspector.h - from the LAST PATH COMPONENT of that
# directory. CPM's default download folder is ".../melatonin_inspector-src",
# which would make JUCE look for "melatonin_inspector-src.h" and abort every
# configure; SOURCE_DIR pins the checkout to a properly named folder, exactly
# as the project's own README prescribes.
CPMAddPackage(
    NAME melatonin_inspector
    GITHUB_REPOSITORY sudara/melatonin_inspector
    GIT_TAG 9c483f865854d83fe9873e1c20e3fd772f695f80
    SOURCE_DIR "${CMAKE_BINARY_DIR}/deps/melatonin_inspector"
    EXCLUDE_FROM_ALL YES
    SYSTEM YES)

# melatonin_blur: same source family as the inspector, same SOURCE_DIR reason -
# it is a JUCE module, and JUCE derives the header name it must find
# (melatonin_blur.h) from the LAST PATH COMPONENT of the module directory, so
# CPM's default "-src" suffix would abort every configure.
CPMAddPackage(
    NAME melatonin_blur
    GITHUB_REPOSITORY sudara/melatonin_blur
    GIT_TAG v1.4
    SOURCE_DIR "${CMAKE_BINARY_DIR}/deps/melatonin_blur"
    EXCLUDE_FROM_ALL YES
    SYSTEM YES)

# foleys_gui_magic: added from the repository's modules/ subdirectory, NOT from
# its root. The root CMakeLists.txt is a standalone project: it calls
# FetchContent_MakeAvailable(juce) for its OWN copy of JUCE and sets global
# options (CMAKE_OSX_ARCHITECTURES, CMAKE_MSVC_RUNTIME_LIBRARY, CXX_VISIBILITY)
# that would fight this project's settings and drag in a second JUCE checkout.
# modules/ contains one line - juce_add_modules(foleys_gui_magic) - and nothing
# else, so SOURCE_SUBDIR points CPM there and the JUCE added above is the only
# one in the build. SOURCE_DIR is pinned for the same reason as
# melatonin_inspector: juce_add_modules derives the header name it must find
# (foleys_gui_magic.h) from the directory name.
# Pinned to a master commit rather than v1.4.0: the tagged release still calls
# juce::Font::getStringWidth, which JUCE 9 removed.
CPMAddPackage(
    NAME foleys_gui_magic
    GITHUB_REPOSITORY ffAudio/foleys_gui_magic
    GIT_TAG d53d998e2bc3fb3ca394f1ee5b0c2f3886a67331
    SOURCE_DIR "${CMAKE_BINARY_DIR}/deps/foleys_gui_magic"
    SOURCE_SUBDIR modules
    EXCLUDE_FROM_ALL YES
    SYSTEM YES)

# xsimd: header-only portable SIMD. Pinned by tag like everything else.
# BUILD_TESTS is already OFF by default upstream, but it is stated here anyway:
# it pulls in xsimd's own test suite, and a plugin build must not pay for that.
CPMAddPackage(
    NAME xsimd
    GITHUB_REPOSITORY xtensor-stack/xsimd
    GIT_TAG 14.3.0
    EXCLUDE_FROM_ALL YES
    SYSTEM YES
    OPTIONS
        "BUILD_TESTS OFF"
        "BUILD_BENCHMARK OFF"
        "BUILD_EXAMPLES OFF")

# ==============================================================================
#  Header-only libraries. Each one ships no CMake (or a CMake this project
#  cannot use), so it is downloaded and exposed as an include-only target.
# ==============================================================================

# Signalsmith DSP - header-only DSP toolkit (stretch, EQ and overlap-add
# conveniences sit beside chowdsp's).
#
# The NAME stays "signalsmith-dsp" because that is how the library reads in
# prose, but the repository is Signalsmith-Audio/signalsmith-stretch: no
# "signalsmith-dsp" repository exists on GitHub, and configuring against it
# fails with GitHub's "could not read Username" prompt. Every published tag of
# it carries a CMake file demanding CMake >= 3.24, which this project cannot
# raise, so it is downloaded WITHOUT being configured and registered as a
# header-only target instead - the library is one self-contained header.
CPMAddPackage(
    NAME signalsmith-dsp
    GITHUB_REPOSITORY Signalsmith-Audio/signalsmith-stretch
    GIT_TAG 1.4.0
    DOWNLOAD_ONLY YES)

j37_declare_header_only_library(signalsmith-stretch
    "${signalsmith-dsp_SOURCE_DIR}/include")

# PocketFFT - mreineck/pocketfft, the FFT behind NumPy: exact transforms for
# arbitrary sizes out of one header. The "cpp" branch is the header-only
# edition (pocketfft_hdronly.h at the repository root); the master branch is
# the C/Fortran hybrid, which is not what a plugin wants.
CPMAddPackage(
    NAME pocketfft
    GITHUB_REPOSITORY mreineck/pocketfft
    GIT_TAG c90e55b3d529f8efa40ed01a20de22405f45fc65
    DOWNLOAD_ONLY YES)

j37_declare_header_only_library(pocketfft "${pocketfft_SOURCE_DIR}")

# farbot - hogliux/farbot, "FAbian's Realtime Box o' Tricks": the
# realtime-safe patterns (RealtimeObject, fifo, AsyncCaller). The library part
# is HEADER-ONLY (its CMake only builds tests, and googletest is a submodule
# this build never initialises), so it is downloaded and exposed include-only.
CPMAddPackage(
    NAME farbot
    GITHUB_REPOSITORY hogliux/farbot
    GIT_TAG d8f132c2e2ac44b379632e452700abe71ca3a92e
    DOWNLOAD_ONLY YES)

j37_declare_header_only_library(farbot "${farbot_SOURCE_DIR}/include")

# Rack DSP - VCVRack/Rack, the VCV Rack SDK: the DSP utilities (SVF, filters,
# quantizers, envelopes, resamplers) plus the whole module engine. The DSP
# half is header-only and JUCE-independent, so its include directory is exposed
# for the engine to adopt piece by piece. The ENGINE half must never be linked
# - it owns the module runtime no other host can host.
CPMAddPackage(
    NAME rack
    GITHUB_REPOSITORY VCVRack/Rack
    GIT_TAG v2.6.6
    DOWNLOAD_ONLY YES)

j37_declare_header_only_library(rack_dsp "${rack_SOURCE_DIR}/include")

# DywaPitchTrack - the Dynamic Wavelet Algorithm pitch tracker (Antoine
# Schmitt's dywapitchtrack, MIT). The author's own repository is GONE from
# GitHub, so this fetches hzeller/pitch-hero, a checkout that vendors the two
# library files unmodified. There is nothing to compile until a caller includes
# dywapitchtrack.c, so it is exposed include-only.
CPMAddPackage(
    NAME dywapitchtrack
    GITHUB_REPOSITORY hzeller/pitch-hero
    GIT_TAG a05bef4b9941d19d4721ceaf2cb50ab12a764629
    DOWNLOAD_ONLY YES)

j37_declare_header_only_library(dywapitchtrack "${dywapitchtrack_SOURCE_DIR}")

# ==============================================================================
#  Libraries with translation units this project compiles itself
# ==============================================================================

# FFTConvolver (HiFi-LOFi) - the streaming partitioned convolution engine
# behind many long-reverb implementations.
#
# The checkout ships no CMakeLists.txt, so CPM only downloads it and its four
# translation units - all flat at the repository root - are compiled here into
# a static library: the two-stage convolver itself, the single-shot variant,
# the AudioFFT backend it delegates the transforms to, and its small Utilities.
# Includes are flat ("FFTConvolver.h"), so one include dir - the root.
CPMAddPackage(
    NAME fftconvolver
    GITHUB_REPOSITORY HiFi-LOFi/FFTConvolver
    GIT_TAG f2cdeb04c42141d2caec19ca4f137398b2a76b85
    DOWNLOAD_ONLY YES)

if(NOT TARGET fftconvolver)
    add_library(fftconvolver STATIC EXCLUDE_FROM_ALL
        "${fftconvolver_SOURCE_DIR}/TwoStageFFTConvolver.cpp"
        "${fftconvolver_SOURCE_DIR}/FFTConvolver.cpp"
        "${fftconvolver_SOURCE_DIR}/AudioFFT.cpp"
        "${fftconvolver_SOURCE_DIR}/Utilities.cpp")
    target_include_directories(fftconvolver SYSTEM PUBLIC
        "${fftconvolver_SOURCE_DIR}")
endif()

# Dear ImGui - immediate-mode GUI toolkit.
#
# ocornut/imgui deliberately ships no CMakeLists.txt at its root, so a plain
# CPMAddPackage would die at configure. It is downloaded only and the four core
# translation units are built here as a static library. The backends/ folder is
# NOT compiled: none of the platform backends can own a window a JUCE plugin
# already owns, and core ImGui compiles alone.
CPMAddPackage(
    NAME imgui
    GITHUB_REPOSITORY ocornut/imgui
    GIT_TAG v1.91.5
    DOWNLOAD_ONLY YES)

if(NOT TARGET imgui)
    add_library(imgui STATIC EXCLUDE_FROM_ALL
        "${imgui_SOURCE_DIR}/imgui.cpp"
        "${imgui_SOURCE_DIR}/imgui_draw.cpp"
        "${imgui_SOURCE_DIR}/imgui_tables.cpp"
        "${imgui_SOURCE_DIR}/imgui_widgets.cpp")
    target_include_directories(imgui SYSTEM PUBLIC "${imgui_SOURCE_DIR}")
endif()

# ==============================================================================
#  Source-only references. These are fetched so the code is on disk to read
#  and to lift from, but NO target of theirs is ever linked: either they are a
#  different plugin framework (iPlug2), a different host's plugin (VCV
#  Prototype), or they need generated files a bare checkout does not have
#  (Soundpipe, Aubio).
# ==============================================================================

# iPlug2 - a full C++ plugin framework with its own UI and wrapper layers.
# It cannot be LINKED into a JUCE plugin: both frameworks want to own the
# window and the audio wrapper.
CPMAddPackage(
    NAME iPlug2
    GITHUB_REPOSITORY iPlug2/iPlug2
    GIT_TAG b1aac7ecd6778b37599b90d1d32766545fc2010f
    DOWNLOAD_ONLY YES)

# ChowCentaur - Chowdhury's Centaur overdrive (the modelled Klon KS-12). A JUCE
# PLUGIN project driven by a JUCEPluginTemplate checkout, so it cannot simply
# be dropped into this build. Adopting a stage from it means adding its
# Source/*.cpp files to target_sources(first ...) deliberately.
CPMAddPackage(
    NAME chowcentaur
    GITHUB_REPOSITORY AviateAudio/ChowDSP_ChowCentaur
    GIT_TAG 2c0414f3c81089247f1c295b1b1be922410a1fb2
    DOWNLOAD_ONLY YES)

# VCV Prototype - VCVRack/VCV-Prototype, the source of VCV's scriptable
# prototype module (the Lua / Faust / Python embeds). A plugin for a different
# host (the Rack runtime) with a Rack-plugin Makefile.
CPMAddPackage(
    NAME vcv_prototype
    GITHUB_REPOSITORY VCVRack/VCV-Prototype
    GIT_TAG 7cb75fda2faffb5b2b469cec1cc6183fcd1d7af0
    DOWNLOAD_ONLY YES)

# Soundpipe - paulbatchelor/soundpipe, the C DSP module library behind Sporth
# (132 modules: filters, reverbs, envelopes, oscillators). Upstream BUILDS by
# generating h/soundpipe.h and config.h from its Makefile; the bare checkout
# does not compile, so it is SOURCE ONLY.
CPMAddPackage(
    NAME soundpipe
    GITHUB_REPOSITORY paulbatchelor/soundpipe
    GIT_TAG 3efb43bdabd0ed23b17c694292b5a79f1692a3ea
    DOWNLOAD_ONLY YES)

# Aubio - aubio/aubio, the C library for audio labelling: pitch (YIN/YINFFT),
# onset, tempo and beat tracking. Its sources #include a GENERATED config.h and
# expect waf/autoconf to produce it, so the bare checkout does not compile.
CPMAddPackage(
    NAME aubio
    GITHUB_REPOSITORY aubio/aubio
    GIT_TAG ad5cf975aed08cc4562dd008cf9f83b12b82ffb8
    DOWNLOAD_ONLY YES)

# ==============================================================================
#  FFT libraries. Three are linked in so the engine can adopt whichever the
#  profiling decides is worth it; today the tape path needs none of them, and
#  that is why they are pinned but not called.
# ==============================================================================

# KISS FFT - mborgerding/kissfft, the "keep it simple, stupid" FFT. A real
# CMake project, so it is configured like any other package: static, no
# OpenMP, no pkg-config files, no command-line tools, no tests.
CPMAddPackage(
    NAME kissfft
    GITHUB_REPOSITORY mborgerding/kissfft
    GIT_TAG 131.1.0
    EXCLUDE_FROM_ALL YES
    SYSTEM YES
    OPTIONS
        "KISSFFT_STATIC ON"
        "KISSFFT_OPENMP OFF"
        "KISSFFT_TEST OFF"
        "KISSFFT_TOOLS OFF"
        "KISSFFT_PKGCONFIG OFF")

# PFFFT - "Pretty Fast (and Furious) FFT", Julien Pommier's SIMD-friendly
# convolution-grade transform. His own GitHub repository is gone; the
# maintained CMake packaging is marton78/pffft. Float precision, SIMD on,
# double off: the tape engine works in float.
CPMAddPackage(
    NAME pffft
    GITHUB_REPOSITORY marton78/pffft
    GIT_TAG e1dbebc9fbf74247d12f094accbbc470aaee8715
    EXCLUDE_FROM_ALL YES
    SYSTEM YES
    OPTIONS
        "PFFFT_USE_TYPE_FLOAT ON"
        "PFFFT_USE_TYPE_DOUBLE OFF"
        "PFFFT_USE_SIMD ON"
        "INSTALL_PFFFT OFF"
        "INSTALL_PFDSP OFF"
        "INSTALL_PFFASTCONV OFF"
        "PFFFT_USE_BENCH_FFTW OFF"
        "PFFFT_USE_BENCH_GREEN OFF"
        "PFFFT_USE_BENCH_KISS OFF")

# ==============================================================================
#  Small utility libraries
# ==============================================================================

# moodycamel::ReaderWriterQueue - cameron314/readerwriterqueue, the lock-free
# single-producer/single-consumer queue. Its CMake target is INTERFACE, i.e.
# header-only.
CPMAddPackage(
    NAME readerwriterqueue
    GITHUB_REPOSITORY cameron314/readerwriterqueue
    GIT_TAG v1.0.7
    EXCLUDE_FROM_ALL YES
    SYSTEM YES)

# yasio - a compact asynchronous TCP/UDP/KCP socket library. A real CMake
# project: static, tests and examples off. YASIO_SSL_BACKEND 0 selects its "no
# TLS" build: the default backend would download a prebuilt OpenSSL archive
# from the network at CONFIGURE time, which a reproducible plugin build must
# not do (and mbedtls would drag a second crypto library in behind JUCE's).
CPMAddPackage(
    NAME yasio
    GITHUB_REPOSITORY yasio/yasio
    GIT_TAG v4.4.1
    EXCLUDE_FROM_ALL YES
    SYSTEM YES
    OPTIONS
        "YASIO_BUILD_SHARED_LIBS OFF"
        "YASIO_BUILD_TESTS OFF"
        "YASIO_BUILD_EXAMPLES OFF"
        "YASIO_SSL_BACKEND 0")

# ==============================================================================
#  RTNeural - jatinchowdhury18/RTNeural, the real-time neural inference engine
#  behind Chowdhury's amp and pedal models (LSTM / GRU / Dense / Conv1D).
#
#  Configured as a real CMake subproject, but with the backend choice taken
#  away from its root script: that script's include(SIMDExtensions.cmake) plus
#  its ChooseBackend default would demand the modules/Eigen submodule (which a
#  CPM checkout cannot carry - CPM downloads no submodules), and its AVX probe
#  would plant -mavx2 into a binary that has to run on machines without AVX.
#  So BUILD_TESTS/BUILD_BENCH/BUILD_EXAMPLES are off and RTNEURAL_STL=ON is
#  passed before the checkout is configured: RTNEURAL_STL is checked FIRST in
#  ChooseBackend.cmake, the Eigen default branch never runs, and no backend
#  macro is defined at all - the STL path compiles straight out of the checkout
#  (verified: RTNeural.cpp -> a 223 KB object with no external headers).
#  The engine already pins xsimd 14.3.0; switching RTNeural onto it is a
#  per-model decision for the day a model is actually adopted.
#  Pinned to the current master head (upstream publishes no tags).
# ==============================================================================
CPMAddPackage(
    NAME RTNeural
    GITHUB_REPOSITORY jatinchowdhury18/RTNeural
    GIT_TAG 95c3c0f987a6fe903e7eec71e797405dbed7caf7
    EXCLUDE_FROM_ALL YES
    SYSTEM YES
    OPTIONS
        "RTNEURAL_STL ON"
        "BUILD_TESTS OFF"
        "BUILD_BENCH OFF"
        "BUILD_EXAMPLES OFF")

# ==============================================================================
#  dr_libs - mackron/dr_libs, the single-header decoders dr_wav / dr_mp3 /
#  dr_flac (used by miniaudio, SoLoud and half the audio-tooling world).
#
#  Nothing here is compiled: the headers self-contain their implementation
#  behind DR_WAV_IMPLEMENTATION and friends, which exactly one translation unit
#  may define, so the target is include-only and the implementation units are
#  added by whoever adopts a decoder. Pinned to the wav-0.14.5 tag (upstream
#  tags per component, wav-* and mp3-*, and wav is the newest of the two).
# ==============================================================================
CPMAddPackage(
    NAME dr_libs
    GITHUB_REPOSITORY mackron/dr_libs
    GIT_TAG wav-0.14.5
    DOWNLOAD_ONLY YES)

j37_declare_header_only_library(dr_libs "${dr_libs_SOURCE_DIR}")

# ==============================================================================
#  libsndfile is deliberately NOT fetched. Its 1.2.2 CMakeLists opens with
#  cmake_minimum_required(VERSION 3.1..3.18), and CMake 4.x - the release the
#  current CI runners ship - removed compatibility with minimums below 3.5, so
#  configuring it aborts the whole build (surfaced on the macOS and Windows
#  runners). Re-add it pinned to a release whose minimum CMake is 3.5 or newer.
# ==============================================================================

# ==============================================================================
#  YIN pitch tracking - ashokfernandez/Yin-Pitch-Tracking, the embedded-minded
#  C port of the Yin fundamental-frequency estimator (de Cheveigne & Kawahara).
#
#  Two C files at the repository root and nothing else: Yin.h (stdint only -
#  verified) and Yin.c (compiles alone under plain gcc). There is no CMake
#  upstream, so the checkout is downloaded only and the pair is compiled here
#  into a static library. The repository's audioData.h and Test_Yin.c are a
#  test fixture, not library code, and stay out of the build. The upstream
#  YIN_SAMPLING_RATE define (44100) is NOT used by the algorithm - the caller
#  divides by the real sample rate - so no patching is needed for 48/96 kHz.
#  No tags exist upstream; pinned to the master head.
# ==============================================================================
CPMAddPackage(
    NAME yin_pitch
    GITHUB_REPOSITORY ashokfernandez/Yin-Pitch-Tracking
    GIT_TAG 69483b048bea0faac73a49e209577aebeb5e9680
    DOWNLOAD_ONLY YES)

if(NOT TARGET yin_pitch)
    add_library(yin_pitch STATIC EXCLUDE_FROM_ALL
        "${yin_pitch_SOURCE_DIR}/Yin.c")
    target_include_directories(yin_pitch SYSTEM PUBLIC
        "${yin_pitch_SOURCE_DIR}")
endif()

# ==============================================================================
#  MPM (McLeod Pitch Method) - adamski/pitch_detector, a JUCE module wrapping
#  the NSDF-based MPM tracker plus a YIN class, built on the same author's
#  audio_fft module (a JUCE module wrapping HiFi-LoFi's AudioFFT).
#
#  It is a JUCE module and is registered like the other JUCE modules in this
#  build, which is also the only way its dependencies resolve: the module
#  header declares juce_core + juce_audio_basics + audio_fft, and
#  juce_add_module wires those to whatever the linking target already links.
#  audio_fft is fetched separately because pitch_detector carries it as a GIT
#  SUBMODULE - CPM downloads no submodules, so the AudioFFT/ directory would
#  otherwise be an empty hole in the checkout. audio_fft in turn vendors
#  HiFi-LoFi/AudioFFT (its AudioFFT/ directory is a submodule too), so the
#  upstream HiFi-LoFi checkout is fetched directly and CPM's SOURCE_DIR pins
#  it into the exact path audio_fft's includes expect. One line -
#  audio_fft.cpp - is all audio_fft compiles: it includes AudioFFT.cpp, whose
#  backend is chosen by define, and on Apple PitchMPM.h already switches that
#  define to AUDIOFFT_APPLE_ACCELERATE, so the FFT rides the Accelerate
#  framework linked for vDSP. No tags exist on either repository;
#  both are pinned to the master heads.
# ==============================================================================
#  The wrapper module (adamski/audio_fft) is what juce_add_module must see: its
#  root carries audio_fft.h - the module header the declaration and the include
#  path both come from. It compiles exactly one unit, audio_fft.cpp, which
#  #includes "AudioFFT/AudioFFT.cpp" - the HiFi-LoFi library vendored as a GIT
#  SUBMODULE of the wrapper, and therefore pinned into that exact slot below.
#  (Two first-pass layouts were wrong and both surfaced in CI: registering the
#  HiFi-LoFi checkout as the module left <audio_fft/audio_fft.h> unresolvable,
#  and spelling the wrapper's folder with a capital A broke the same include on
#  case-sensitive filesystems.)
CPMAddPackage(
    NAME audio_fft_module
    GITHUB_REPOSITORY adamski/audio_fft
    GIT_TAG 922b30a8518c737ffad6ed4c7337704770e8c82c
    SOURCE_DIR "${CMAKE_BINARY_DIR}/deps/pitch_detector/audio_fft"
    DOWNLOAD_ONLY YES)

#  The library itself, into the submodule slot the wrapper's sources include
#  through. Ordered AFTER the wrapper above: git can clone into an existing
#  EMPTY directory, but not into a populated one, and the wrapper's checkout
#  creates the empty AudioFFT/ slot its .gitmodules describes.
CPMAddPackage(
    NAME AudioFFT
    GITHUB_REPOSITORY HiFi-LoFi/AudioFFT
    GIT_TAG 0893b532dd357c7270609425f5ae9d9b5ae7d725
    SOURCE_DIR "${CMAKE_BINARY_DIR}/deps/pitch_detector/audio_fft/AudioFFT"
    DOWNLOAD_ONLY YES)

CPMAddPackage(
    NAME pitch_detector
    GITHUB_REPOSITORY adamski/pitch_detector
    GIT_TAG d3b60970e1096889f7b2727f62179ccb3f832640
    SOURCE_DIR "${CMAKE_BINARY_DIR}/deps/pitch_detector/pitch_detector"
    DOWNLOAD_ONLY YES)

if(NOT TARGET audio_fft)
    juce_add_module("${CMAKE_BINARY_DIR}/deps/pitch_detector/audio_fft")
endif()
# (audio_fft_module's checkout IS the module; AudioFFT is its vendored engine.)

if(NOT TARGET pitch_detector)
    juce_add_module("${CMAKE_BINARY_DIR}/deps/pitch_detector/pitch_detector")
endif()

# ==============================================================================
#  Dattorro reverb - el-visio/dattorro-verb, a compact C implementation of
#  Jon Dattorro's 1997 plate reverb (the input diffusers, the two tank halves
#  with their damping and decay filters, and the stereo taps).
#
#  "DattorroReverb" names an algorithm, not a repository: GitHub search by that
#  name returns single-file throwaway projects, one of which #includes
#  <sndfile.h> in its library code - not vendorable. This checkout is plain C
#  (verb.h / verb_structs.h / verb.c, stdint/math/string only - verified to
#  compile alone) with an opaque-struct API, so it is compiled here into a
#  static library like YIN. No tags upstream; pinned to the master head.
# ==============================================================================
CPMAddPackage(
    NAME dattorro_verb
    GITHUB_REPOSITORY el-visio/dattorro-verb
    GIT_TAG 41e976a0228a2472156f5c010b3bfbb444e777d2
    DOWNLOAD_ONLY YES)

if(NOT TARGET dattorro_verb)
    add_library(dattorro_verb STATIC EXCLUDE_FROM_ALL
        "${dattorro_verb_SOURCE_DIR}/verb.c")
    target_include_directories(dattorro_verb SYSTEM PUBLIC
        "${dattorro_verb_SOURCE_DIR}")
endif()
