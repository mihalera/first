# ==============================================================================
#
#   Nonlin Analog Saturator - tape machine emulation (J37, Ampex 456, Studer A800,
#   Chrome, Type 111, GP9, Quantegy 499, RTM SM911)
#
#   This is the single source of truth for the build. The Projucer project
#   (first.jucer) and its generated Builds/ folder have been retired: every
#   setting that used to live there is reproduced below, but as text that the
#   build reads directly instead of as generated files that can drift.
#
#   Supported configurations
#     Windows : Visual Studio / x64                -> VST3
#     macOS   : Xcode / universal (arm64 + x86_64) -> VST3, AU, AUv3
#     Linux   : GCC / Clang x64                    -> VST3
#
#   AU and AUv3 are Apple-only formats. JUCE compiles them to nothing on
#   Windows and Linux, so the FORMATS list below is safe to share between platforms.
#
# ==============================================================================

cmake_minimum_required(VERSION 3.22)

project(first
    VERSION 1.0.0
    DESCRIPTION "Nonlin Analog Saturator"
    LANGUAGES C CXX)

# Keep the generated project files out of the source tree.
if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ------------------------------------------------------------------------------
# ------------------------------------------------------------------------------
#  JUCE
#
#  JUCE is a git submodule pinned at 9.0.2, checked out at JUCE/ next to this
#  file. Adding it as a subdirectory is all that is needed: there are no module
#  paths to configure and therefore none to get wrong per-platform, which was
#  the source of the absolute-path problems the Projucer setup had.
# ------------------------------------------------------------------------------
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE submodule not found at ${CMAKE_CURRENT_SOURCE_DIR}/JUCE.\n"
        "Run: git submodule update --init --recursive")
endif()

add_subdirectory(JUCE)

# ------------------------------------------------------------------------------
#  Third-party libraries (CPM.cmake)
#
#  Everything below JUCE itself is fetched by CPM at configure time and pinned
#  by tag, so a fresh clone builds the same thing forever:
#
#    chowdsp_utils       Chowdhury DSP's toolbox for JUCE plugins - cached DSP
#                        primitives, SIMD wrappers, smoother and parameter
#                        helpers a generation ahead of the hand-rolled
#                        equivalents. Linked into the plugin target so the
#                        engine can adopt its pieces incrementally.
#
#    melatonin_inspector Sudara's JUCE component inspector. The module is
#                        compiled and linked so a debug session can embed its
#                        component with no further setup; it stays passive in
#                        the shipped build.
#
#    xsimd               Portable SIMD wrappers (SSE/AVX/NEON). Linked into the
#                        plugin so the per-sample tape loop can be vectorised
#                        when profiling says it is worth it. chowdsp::chowdsp_simd
#                        already wraps xsimd, so this is the same code path the
#                        DSP helpers use rather than a second SIMD layer.
#
#    melatonin_blur      Fast shadow/gradient blurring for JUCE Components.
#                        Available to the editor's paint routines, which draw a
#                        lot of soft analog shading by hand today.
#
#    foleys_gui_magic    Daniel Walz's declarative GUI framework for JUCE. The
#                        editor can be described in XML and edited live, which is
#                        what the hand-written resized()/paint() code in
#                        PluginEditor.cpp is doing today. Fetched here as a
#                        MODULE ONLY - see the note on the CPMAddPackage call.
#
#  The tanh/SIMD advice from the library review is recorded where the tanh
#  stages live in PluginProcessor.cpp: approximating tanh only pays once
#  profiling shows the shaper is the hot loop, and chowdsp_utils' SIMD types
#  are the vehicle if that day comes.
#
#  NOTHING BELOW IS ADOPTED YET. These packages are fetched and linked so that
#  the pieces are available in the build; the engine still uses its own code.
#  Adopting them is a per-piece decision that has to be made against a profile
#  and a listening test, not against a library list.
# ------------------------------------------------------------------------------
# ------------------------------------------------------------------------------
#  CPM configuration
#
#  Reproducibility first, network second. Everything below is about making a
#  configure work the same way twice, and about surviving a GitHub outage.
#
#   CPM_SOURCE_CACHE - where CPM keeps the checkouts it downloads. Without this
#   every configure starts from scratch in build/_deps, so a clean rebuild
#   re-clones every dependency and a machine without network cannot build at all.
#   Pointing it at a directory that survives "rm -rf build" means the second
#   configure is instant and the build works offline. Set it from the
#   environment (CPM_SOURCE_CACHE) if you prefer it outside the tree.
#
#   CPM_USE_LOCAL_PACKAGES - off deliberately. A system-installed chowdsp or
#   xsimd of the wrong version would silently win over the pinned one, which is
#   exactly the "works on my machine" failure the pinning exists to prevent.
#
#   J37_OFFLINE - when ON, CPM is forbidden from reaching the network at all. It
#   then builds ONLY from what is already in CPM_SOURCE_CACHE, and a missing
#   package fails with a clear message instead of a timeout. This is the switch
#   for air-gapped machines and for CI runs that should not depend on GitHub.
#
#  On pinning: the tags below are tags, not hashes. A tag is a moving reference -
#  upstream can retag it - so the last word in reproducibility is to replace each
#  GIT_TAG with the 40-character commit SHA it currently points at. That is a
#  deliberate one-line change per package and is left to the reader rather than
#  done blindly here, because a wrong SHA breaks the build for everyone.
#  To get the SHA for a tag:
#
#     git ls-remote --tags <repo-url> <tag>
#
#  melatonin_inspector is already pinned that way (it publishes no tags).
# ------------------------------------------------------------------------------
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

# chowdsp_utils: linked as JUCE modules (chowdsp::chowdsp_dsp_utils,
# chowdsp::chowdsp_math) - the official integration is a plain add_subdirectory
# AFTER add_subdirectory(JUCE), which the order here guarantees.
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
# one in the build. FOLEYS_BUILD_EXAMPLES/TESTS belong to the root project and so
# are not options here at all; there is nothing to switch off.
#
# SOURCE_DIR is pinned for the same reason as melatonin_inspector: juce_add_modules
# derives the header name it must find (foleys_gui_magic.h) from the directory
# name, and CPM's default "-src" suffix would break that.
# Pinned to a master commit rather than v1.4.0: the tagged release still calls
# juce::Font::getStringWidth, which JUCE 9 removed (its own modules do not
# compile against this project's JUCE). Upstream fixed it on master via
# juce::GlyphArrangement::getStringWidth and has not tagged since; this commit
# is the fix. Re-pin to a tag when one appears.
CPMAddPackage(
    NAME foleys_gui_magic
    GITHUB_REPOSITORY ffAudio/foleys_gui_magic
    GIT_TAG d53d998e2bc3fb3ca394f1ee5b0c2f3886a67331
    SOURCE_DIR "${CMAKE_BINARY_DIR}/deps/foleys_gui_magic"
    SOURCE_SUBDIR modules
    EXCLUDE_FROM_ALL YES
    SYSTEM YES)

# xsimd: header-only portable SIMD. Pinned by tag like everything else, so a
# fresh clone resolves the same version forever. SYSTEM YES keeps its headers out
# of the warning sweep - it is third-party code and is not ours to fix.
#
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

# ------------------------------------------------------------------------------
#  Header-only checkouts that ship no CMake at all are registered through this
#  one helper: an INTERFACE target carrying a SYSTEM include path, so the link
#  line below reads like every other dependency and the third-party headers
#  stay out of the warning sweep.
# ------------------------------------------------------------------------------
function(j37_declare_header_only_library name include_dir)
    if(NOT TARGET ${name})
        add_library(${name} INTERFACE)
        target_include_directories(${name} SYSTEM INTERFACE "${include_dir}")
    endif()
endfunction()

# ------------------------------------------------------------------------------
#  Signalsmith DSP - header-only DSP toolkit (stretch, EQ and overlap-add
#  conveniences sit beside chowdsp's).
#
#  The NAME stays "signalsmith-dsp" because that is how the library reads in
#  prose, but the repository is Signalsmith-Audio/signalsmith-stretch: no
#  "signalsmith-dsp" repository exists on GitHub, and configuring against it
#  fails with GitHub's "could not read Username" prompt (its way of saying
#  404). Pinned to the 1.4.0 tag. Every published tag of it carries a CMake
#  file demanding CMake >= 3.24, which this project cannot raise without
#  breaking machines that ship 3.22, so it is downloaded WITHOUT being
#  configured and registered as a header-only target instead - the library is
#  one self-contained header (include/signalsmith-stretch.h) and needs nothing
#  else, not even the "linear" sibling its CMake would FetchContent.
# ------------------------------------------------------------------------------
CPMAddPackage(
    NAME signalsmith-dsp
    GITHUB_REPOSITORY Signalsmith-Audio/signalsmith-stretch
    GIT_TAG 1.4.0
    DOWNLOAD_ONLY YES)

j37_declare_header_only_library(signalsmith-stretch
    "${signalsmith-dsp_SOURCE_DIR}/include")

# ------------------------------------------------------------------------------
#  FFTConvolver (HiFi-LOFi) - the streaming partitioned convolution engine
#  behind many long-reverb implementations.
#
#  The checkout ships no CMakeLists.txt (the default branch is "non-uniform"
#  and there are no tags), so CPM only downloads it and its four translation
#  units - all flat at the repository root - are compiled here into a static
#  library: the two-stage convolver itself, the single-shot variant, the
#  AudioFFT backend it delegates the transforms to, and its small Utilities.
#  Includes are flat ("FFTConvolver.h"), so one include dir - the root.
#  Pinned to the commit the default branch pointed at when this was written
#  (the hash that sat here before was invented and never resolved).
# ------------------------------------------------------------------------------
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

# ------------------------------------------------------------------------------
#  Dear ImGui - immediate-mode GUI toolkit.
#
#  ocornut/imgui deliberately ships no CMakeLists.txt at its root, so a plain
#  CPMAddPackage would die at configure ("no CMakeLists.txt"). It is downloaded
#  only and the four core translation units are built here as a static library.
#  The backends/ folder is NOT compiled: none of the platform backends can own
#  a window a JUCE plugin already owns, and core ImGui compiles alone. Pinned
#  to the v1.91.5 tag.
# ------------------------------------------------------------------------------
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

# ------------------------------------------------------------------------------
#  iPlug2 - a full C++ plugin framework with its own UI and wrapper layers.
#
#  It cannot be LINKED into a JUCE plugin: both frameworks want to own the
#  window and the audio wrapper. None of its subdirectories is a stand-alone
#  CMake project either (its builds are generated projects), so it is fetched
#  as a source-only reference for its DSP modules and utility headers; no
#  target of it is ever linked. The newest tag upstream is v1.0.0-beta - the
#  "v2.1.0" that sat here before does not exist.
# ------------------------------------------------------------------------------
CPMAddPackage(
    NAME iPlug2
    GITHUB_REPOSITORY iPlug2/iPlug2
    GIT_TAG b1aac7ecd6778b37599b90d1d32766545fc2010f
    DOWNLOAD_ONLY YES)

# ------------------------------------------------------------------------------
#  Melatonin Vector Graphics - REQUESTED BUT DOES NOT EXIST.
#  Sudara's melatonin family on GitHub is exactly four repositories:
#  melatonin_inspector, melatonin_blur, melatonin_parameters and
#  melatonin_audio_sparklines. None of them is a vector-graphics library and
#  nobody else publishes one under that name (GitHub search returns nothing).
#  The two melatonin libraries that exist and fit this panel - inspector and
#  blur - are already fetched above; there is nothing further to add.
# ------------------------------------------------------------------------------

# ------------------------------------------------------------------------------
#  ChowCentaur - Chowdhury's Centaur overdrive (the modelled Klon KS-12).
#  The repository is a JUCE PLUGIN project driven by a JUCEPluginTemplate
#  checkout, not a stand-alone CMake library, so it cannot simply be dropped
#  into this build. It is fetched SOURCE ONLY: adopting a stage from it means
#  adding its Source/*.cpp files to target_sources(first ...) deliberately,
#  a per-stage decision against a listening test, not a blanket link. Pinned
#  to the current main head.
# ------------------------------------------------------------------------------
CPMAddPackage(
    NAME chowcentaur
    GITHUB_REPOSITORY AviateAudio/ChowDSP_ChowCentaur
    GIT_TAG 2c0414f3c81089247f1c295b1b1be922410a1fb2
    DOWNLOAD_ONLY YES)

# ------------------------------------------------------------------------------
#  VCV Prototype - VCVRack/VCV-Prototype, the source of VCV's scriptable
#  prototype module (the Lua / Faust / Python embeds). It is a plugin for a
#  different host (the Rack runtime) and its build is a Rack-plugin Makefile,
#  so none of it can link into a JUCE plugin; fetched SOURCE ONLY as reference
#  for the interpreter integrations it carries. Pinned to the v1 branch head.
# ------------------------------------------------------------------------------
CPMAddPackage(
    NAME vcv_prototype
    GITHUB_REPOSITORY VCVRack/VCV-Prototype
    GIT_TAG 7cb75fda2faffb5b2b469cec1cc6183fcd1d7af0
    DOWNLOAD_ONLY YES)

# ------------------------------------------------------------------------------
#  Rack DSP - VCVRack/Rack, the VCV Rack SDK: the DSP utilities (SVF, filters,
#  quantizers, envelopes, resamplers) plus the whole module engine. The DSP
#  half is header-only and JUCE-independent, so its include directory is
#  exposed for the engine to adopt piece by piece. The ENGINE half must never
#  be linked - it owns the module runtime no other host can host. Pinned to
#  the v2.6.6 tag.
# ------------------------------------------------------------------------------
CPMAddPackage(
    NAME rack
    GITHUB_REPOSITORY VCVRack/Rack
    GIT_TAG v2.6.6
    DOWNLOAD_ONLY YES)

j37_declare_header_only_library(rack_dsp "${rack_SOURCE_DIR}/include")

# ------------------------------------------------------------------------------
#  Soundpipe - paulbatchelor/soundpipe, the C DSP module library behind Sporth
#  (132 modules: filters, reverbs, envelopes, oscillators). Upstream BUILDS by
#  generating h/soundpipe.h and config.h from its Makefile; the bare checkout
#  does not compile, so it is SOURCE ONLY. Adopting a module means generating
#  those two files first and then adding modules/<name>.c with its
#  dependencies to the build.
# ------------------------------------------------------------------------------
CPMAddPackage(
    NAME soundpipe
    GITHUB_REPOSITORY paulbatchelor/soundpipe
    GIT_TAG 3efb43bdabd0ed23b17c694292b5a79f1692a3ea
    DOWNLOAD_ONLY YES)

# ------------------------------------------------------------------------------
#  Intel IPP and Apple vDSP - the two vendor vector libraries.
#
#  Neither is a GitHub repository, so neither can be fetched through CPM, and
#  neither needs to be: each is already present on a machine that has it, and
#  each is integrated here the only way such a library can be.
#
#  vDSP  - part of Apple's Accelerate framework, shipped with the OS on every
#          Apple platform. There is nothing to download, so the whole
#          integration is the system framework on the link line plus
#          J37_HAVE_VDSP, under which the DSP takes its vDSP path. Everywhere
#          else vDSP does not exist, so everywhere else this is a no-op rather
#          than a fallback: that is the same arrangement JUCE itself uses.
#
#  IPP  - Intel's oneAPI primitives, installed on the developer's machine under
#          a licence this project cannot vendor into its own configure, so it
#          is DISCOVERED rather than shipped. When the SDK is present,
#          find_package(ipp) hands over its imported targets; when it is not,
#          the build carries on without it and J37_HAVE_IPP is never defined.
#          That asymmetry is the whole design: a contributor on Windows or
#          Linux has no IPP installed and their build must not need one, while a
#          contributor who has it gets the real library from the same
#          configure, with no extra step. J37_ENABLE_IPP=OFF opts out for a
#          machine that has the SDK but does not want it.
#
#  Both are therefore ADDITIVE: they accelerate kernels that are also compiled
#  in portable form, so the engine keeps working identically on a platform that
#  has neither - which is precisely what chowdsp::chowdsp_simd already does
#  with its own accelerator backends, and what the portable FFTs below (KISS
#  FFT / PocketFFT / PFFFT) are here for.
# ------------------------------------------------------------------------------
# ------------------------------------------------------------------------------
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
# ------------------------------------------------------------------------------
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

# ------------------------------------------------------------------------------
#  dr_libs -mackron/dr_libs, the single-header decoders dr_wav / dr_mp3 /
#  dr_flac (used by miniaudio, SoLoud and half the audio-tooling world).
#
#  Nothing here is compiled: the headers self-contain their implementation
#  behind DR_WAV_IMPLEMENTATION and friends, which exactly one translation unit
#  may define, so the target is include-only and the implementation units are
#  added by whoever adopts a decoder. Pinned to the wav-0.14.5 tag (upstream
#  tags per component, wav-* and mp3-*, and wav is the newest of the two).
# ------------------------------------------------------------------------------
CPMAddPackage(
    NAME dr_libs
    GITHUB_REPOSITORY mackron/dr_libs
    GIT_TAG wav-0.14.5
    DOWNLOAD_ONLY YES)

j37_declare_header_only_library(dr_libs "${dr_libs_SOURCE_DIR}")

# ------------------------------------------------------------------------------
#  libsndfile - libsndfile/libsndfile, the reference C library for reading and
#  writing sound files (WAV/AIFF/FLAC and friends), for offline preset-render
#  and analysis tooling.
#
#  A real CMake project, configured like any other package, with its optional
#  codecs and programs switched off: ENABLE_EXTERNAL_LIBS would make CMake
#  demand Ogg/Vorbis/Opus/FLAC development packages before it would configure,
#  ENABLE_MPEG adds the LAME-based decoder path, BUILD_PROGRAMS/BUILD_EXAMPLES
#  build command-line tools a plugin never needs, and ENABLE_CPACK only
#  matters to libsndfile's own release process. The library builds STATIC by
#  default upstream; stated anyway so a future default flip cannot silently
#  drag a DLL into the plugin. The imported alias SndFile::sndfile is what the
#  link line below names. Pinned to the 1.2.2 tag.
# ------------------------------------------------------------------------------
CPMAddPackage(
    NAME sndfile
    GITHUB_REPOSITORY libsndfile/libsndfile
    GIT_TAG 1.2.2
    EXCLUDE_FROM_ALL YES
    SYSTEM YES
    OPTIONS
        "BUILD_SHARED_LIBS OFF"
        "BUILD_PROGRAMS OFF"
        "BUILD_EXAMPLES OFF"
        "BUILD_REGTEST OFF"
        "BUILD_TESTING OFF"
        "ENABLE_EXTERNAL_LIBS OFF"
        "ENABLE_MPEG OFF"
        "ENABLE_CPACK OFF")

# ------------------------------------------------------------------------------
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
# ------------------------------------------------------------------------------
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

# ------------------------------------------------------------------------------
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
#  framework linked for vDSP above. No tags exist on either repository;
#  both are pinned to the master heads.
# ------------------------------------------------------------------------------
CPMAddPackage(
    NAME AudioFFT
    GITHUB_REPOSITORY HiFi-LoFi/AudioFFT
    GIT_TAG 0893b532dd357c7270609425f5ae9d9b5ae7d725
    SOURCE_DIR "${CMAKE_BINARY_DIR}/deps/pitch_detector/AudioFFT"
    DOWNLOAD_ONLY YES)

CPMAddPackage(
    NAME pitch_detector
    GITHUB_REPOSITORY adamski/pitch_detector
    GIT_TAG d3b60970e1096889f7b2727f62179ccb3f832640
    SOURCE_DIR "${CMAKE_BINARY_DIR}/deps/pitch_detector/pitch_detector"
    DOWNLOAD_ONLY YES)

if(NOT TARGET audio_fft)
    juce_add_module("${CMAKE_BINARY_DIR}/deps/pitch_detector/AudioFFT")
endif()

if(NOT TARGET pitch_detector)
    juce_add_module("${CMAKE_BINARY_DIR}/deps/pitch_detector/pitch_detector")
endif()

# ------------------------------------------------------------------------------
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
# ------------------------------------------------------------------------------
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

option(J37_ENABLE_IPP "Use Intel IPP when the oneAPI SDK is found on this machine" ON)

set(J37_HAVE_VDSP OFF)
set(J37_HAVE_IPP OFF)
set(J37_IPP_HOW "")
set(J37_ACCELERATE_FRAMEWORK "")

if(APPLE)
    # find_library() is deliberately NOT used to locate this. It searches for
    # LIBRARY FILES - lib<name>.dylib and friends - and a system framework has
    # none: the framework is a directory, not a file. An earlier version of this
    # block used find_library and the macOS CI runner, which has Accelerate
    # installed like every Mac, reported "Apple vDSP NOT found" on it. A
    # framework is a real directory on every Apple platform, so that is what is
    # checked, and the framework path itself is what gets linked.
    if(IS_DIRECTORY "/System/Library/Frameworks/Accelerate.framework")
        set(J37_ACCELERATE_FRAMEWORK "/System/Library/Frameworks/Accelerate.framework")
        set(J37_HAVE_VDSP ON)
    else()
        # Accelerate is part of macOS and cannot be missing from a real install;
        # saying so loudly beats silently compiling the portable path and
        # leaving the developer to wonder why the vDSP path never runs.
        message(WARNING
            "Apple platform, but /System/Library/Frameworks/Accelerate.framework "
            "is missing. Building without vDSP - check the Xcode command line "
            "tools and that the SDK is not damaged.")
    endif()
endif()

if(J37_ENABLE_IPP)
    # oneAPI installs into its own prefix and, unless its setvars.sh has been
    # run, nothing in the environment points at it. All three spellings are
    # honoured, so a developer who has installed it gets a build that finds it
    # without passing a single -D:
    #
    #   IPPROOT / IPP_ROOT   Intel's own environment variables
    #   the stock locations   /opt/intel/oneapi/ipp/latest on Linux,
    #                         C:/Program Files (x86)/Intel/oneAPI/ipp/latest on
    #                         Windows (the 2024+ layout, the one the deb and
    #                         the Windows installer both use)
    #
    # macOS is deliberately absent: Intel stopped shipping oneAPI for macOS, so
    # there is no prefix to look for and the vDSP path is the one that exists.
    set(j37_ipp_roots "")

    foreach(j37_ipp_prefix
            "$ENV{IPPROOT}"
            "$ENV{IPP_ROOT}"
            "/opt/intel/oneapi/ipp/latest"
            # The parentheses in ProgramFiles(x86) have to be escaped or CMake
            # reads the '(' as the start of a variable name and fails to parse.
            "$ENV{ProgramFiles\(x86\)}/Intel/oneAPI/ipp/latest")
        if(j37_ipp_prefix AND IS_DIRECTORY "${j37_ipp_prefix}")
            list(APPEND j37_ipp_roots "${j37_ipp_prefix}")
            list(APPEND CMAKE_PREFIX_PATH
                "${j37_ipp_prefix}"
                "${j37_ipp_prefix}/lib/cmake")
        endif()
    endforeach()

    # The package name is IPP, and the case is load-bearing in both directions:
    # the file Intel ships is IPPConfig.cmake, and find_package(ipp) only ever
    # looks for ippConfig.cmake and ipp-config.cmake. Asking for the lower-case
    # name therefore CANNOT find it - it reported "not installed" on a machine
    # with IPP 2026.0 installed in /opt, and the same targets come back spelled
    # IPP::ipps, IPP::ippi, IPP::ippcore, which is what the link line below uses.
    #
    # QUIET and not REQUIRED: the library is genuinely optional, so a machine
    # without it must read as a normal outcome of the search, not an error.
    find_package(IPP CONFIG QUIET)

    if(IPP_FOUND)
        set(J37_HAVE_IPP ON)
        set(J37_IPP_HOW "oneAPI SDK config")
    else()
        # Fallback probe, for the layouts that ship NO CMake config at all.
        # There are two of them and they are the only practical way to put IPP
        # on a CI runner, because the oneAPI installer is a ~1.5 GB GUI
        # installer that Intel's own community threads report as not working
        # from a command line, oneAPI is not in winget (open request) and
        # Chocolatey has nothing:
        #
        #   Intel's PyPI wheels, ipp-static + ipp-include - the route CI uses.
        #   Headers land in <prefix>/include/ipp.h, the manylinux wheel puts
        #   libipps.a in <prefix>/lib and the win_amd64 wheel puts ippsmt.lib in
        #   <prefix>/Library/lib. There is no macOS wheel at all, which is the
        #   other half of why vDSP is the Apple path.
        #
        #   intelipp.static.win-x64 on NuGet - headers plus .lib files, nothing
        #   else, under build/native.
        #
        # The probe is a plain find_path/find_library, so it works with any
        # compiler, and the targets it defines are named exactly as Intel's own
        # config names them, so the link line below cannot tell the routes
        # apart. IPPROOT is what names the prefix: it is Intel's own variable
        # for the installed SDK, and the CI step exports it for the wheels too,
        # so one variable covers both.
        #
        # The mt suffix is the multithreaded static variant, which is what the
        # NuGet package ships and the only one there; the plain name is what
        # the oneAPI installer ships. Both are tried so neither layout has to
        # be special-cased at the call site.
        unset(J37_IPP_INCLUDE_DIR CACHE)
        unset(J37_IPP_IPPS_LIBRARY CACHE)
        unset(J37_IPP_CORE_LIBRARY CACHE)

        find_path(J37_IPP_INCLUDE_DIR
            NAMES ipp.h
            HINTS ${j37_ipp_roots}
            PATH_SUFFIXES include build/native/include)

        # The last suffix, Library/lib, is the one Intel's PyPI wheels use on
        # Windows: ipp_static-<ver>.data/data/Library/lib/ippsmt.lib. The Linux
        # wheel uses lib/libipps.a and the oneAPI install uses lib/intel64, and
        # all three have to be searched or the route silently finds nothing on
        # one platform and works on the others.
        find_library(J37_IPP_IPPS_LIBRARY
            NAMES ipps ippsmt
            HINTS ${j37_ipp_roots}
            PATH_SUFFIXES lib lib/intel64 build/native/win-x64 Library/lib)

        find_library(J37_IPP_CORE_LIBRARY
            NAMES ippcore ippcoremt
            HINTS ${j37_ipp_roots}
            PATH_SUFFIXES lib lib/intel64 build/native/win-x64 Library/lib)

        if(J37_IPP_INCLUDE_DIR AND J37_IPP_IPPS_LIBRARY AND J37_IPP_CORE_LIBRARY)
            add_library(IPP::ipps UNKNOWN IMPORTED)
            set_target_properties(IPP::ipps PROPERTIES
                IMPORTED_LOCATION "${J37_IPP_IPPS_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${J37_IPP_INCLUDE_DIR}")

            add_library(IPP::ippcore UNKNOWN IMPORTED)
            set_target_properties(IPP::ippcore PROPERTIES
                IMPORTED_LOCATION "${J37_IPP_CORE_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${J37_IPP_INCLUDE_DIR}")

            # ippi (imaging) is deliberately NOT searched: a tape saturator has
            # no imaging in it, and the NuGet static package spells it ippimt.lib
            # for no benefit here.
            set(J37_HAVE_IPP ON)
            set(J37_IPP_HOW "headers + libraries found directly")
        endif()
    endif()
endif()

# ------------------------------------------------------------------------------
#  farbot - hogliux/farbot, "FAbian's Realtime Box o' Tricks": the
#  realtime-safe patterns (RealtimeObject, fifo, AsyncCaller) presented at
#  Meeting C++. The library part is HEADER-ONLY (its CMake only builds tests,
#  and googletest is a submodule this build never initialises), so it is
#  downloaded and exposed as an include-only target. Pinned to the current
#  master head.
# ------------------------------------------------------------------------------
CPMAddPackage(
    NAME farbot
    GITHUB_REPOSITORY hogliux/farbot
    GIT_TAG d8f132c2e2ac44b379632e452700abe71ca3a92e
    DOWNLOAD_ONLY YES)

j37_declare_header_only_library(farbot "${farbot_SOURCE_DIR}/include")

# ------------------------------------------------------------------------------
#  moodycamel::ReaderWriterQueue - cameron314/readerwriterqueue, the lock-free
#  single-producer/single-consumer queue (the blocking sibling lives in the
#  same checkout). Its CMake target is INTERFACE, i.e. header-only. Pinned to
#  the v1.0.7 tag.
# ------------------------------------------------------------------------------
CPMAddPackage(
    NAME readerwriterqueue
    GITHUB_REPOSITORY cameron314/readerwriterqueue
    GIT_TAG v1.0.7
    EXCLUDE_FROM_ALL YES
    SYSTEM YES)

# ------------------------------------------------------------------------------
#  KISS FFT - mborgerding/kissfft, the "keep it simple, stupid" FFT. A real
#  CMake project, so it is configured like any other package: static, no
#  OpenMP, no pkg-config files, no command-line tools, no tests. Pinned to
#  the 131.1.0 tag.
# ------------------------------------------------------------------------------
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

# ------------------------------------------------------------------------------
#  PocketFFT - mreineck/pocketfft, the FFT behind NumPy: exact transforms for
#  arbitrary sizes out of one header. The "cpp" branch is the header-only
#  edition (pocketfft_hdronly.h at the repository root); the master branch is
#  the C/Fortran hybrid, which is not what a plugin wants. Pinned to the cpp
#  branch head.
# ------------------------------------------------------------------------------
CPMAddPackage(
    NAME pocketfft
    GITHUB_REPOSITORY mreineck/pocketfft
    GIT_TAG c90e55b3d529f8efa40ed01a20de22405f45fc65
    DOWNLOAD_ONLY YES)

j37_declare_header_only_library(pocketfft "${pocketfft_SOURCE_DIR}")

# ------------------------------------------------------------------------------
#  yasio - yasio/yasio, a compact asynchronous TCP/UDP/KCP socket library used
#  by game and audio-networking clients. A real CMake project: static, tests
#  and examples off. YASIO_SSL_BACKEND 0 selects its "no TLS" build: the
#  default backend would download a prebuilt OpenSSL archive from the network
#  at CONFIGURE time, which a reproducible plugin build must not do (and
#  mbedtls would drag a second crypto library in behind JUCE's). Re-enable a
#  backend if a feature ever needs TLS. Pinned to the v4.4.1 tag.
# ------------------------------------------------------------------------------
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

# ------------------------------------------------------------------------------
#  DywaPitchTrack - the Dynamic Wavelet Algorithm pitch tracker (Antoine
#  Schmitt's dywapitchtrack, MIT). The author's own repository is GONE from
#  GitHub (antoineschmitt/dywapitchtrack answers 404), so this fetches
#  hzeller/pitch-hero, a checkout that vendors the two library files
#  unmodified. There is nothing to compile until a caller includes
#  dywapitchtrack.c, so it is exposed include-only. Pinned to its master head.
# ------------------------------------------------------------------------------
CPMAddPackage(
    NAME dywapitchtrack
    GITHUB_REPOSITORY hzeller/pitch-hero
    GIT_TAG a05bef4b9941d19d4721ceaf2cb50ab12a764629
    DOWNLOAD_ONLY YES)

j37_declare_header_only_library(dywapitchtrack "${dywapitchtrack_SOURCE_DIR}")

# ------------------------------------------------------------------------------
#  Aubio - aubio/aubio, the C library for audio labelling: pitch (YIN/YINFFT),
#  onset, tempo and beat tracking. Its sources #include a GENERATED config.h
#  and expect waf/autoconf to produce it, so the bare checkout does not
#  compile; it is fetched SOURCE ONLY. Adopting it means generating config.h
#  once (vendoring the generated file is the usual shortcut) and building
#  src/*.c into a small static library here. Pinned to the current master head.
# ------------------------------------------------------------------------------
CPMAddPackage(
    NAME aubio
    GITHUB_REPOSITORY aubio/aubio
    GIT_TAG ad5cf975aed08cc4562dd008cf9f83b12b82ffb8
    DOWNLOAD_ONLY YES)

# ------------------------------------------------------------------------------
#  PFFFT - "Pretty Fast (and Furious) FFT", Julien Pommier's SIMD-friendly
#  convolution-grade transform. His own GitHub repository is gone; the
#  maintained CMake packaging is marton78/pffft. Float precision, SIMD on,
#  double off: the tape engine works in float and the double variant doubles
#  the compile for nothing yet. Pinned to the current master head.
# ------------------------------------------------------------------------------
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

# ------------------------------------------------------------------------------
#  Dev-only libraries - fetched only when J37_BUILD_TESTS is ON
#
#  Catch2    - the C++ test framework. The DSP harness in tests/dsp/ compiles
#              the shipping code against a shim and checks RENDERED AUDIO; that
#              is the right tool for a signal-path regression. Catch2 is for the
#              unit-sized pieces underneath it - a single shaper, a single
#              loudness filter, a single compressor detector - where the question
#              is "does this function still return what it returned yesterday",
#              not "does the plugin still sound right".
#
#  nanobench - micro-benchmarks. The reason this is here at all: the shaper runs
#              two std::tanh calls per sample per channel (four with the
#              zero-point correction), and at 8x oversampling that is the hot
#              loop. Approximating tanh or vectorising it is a real complexity
#              cost, and it should only be paid once a benchmark shows the shaper
#              actually dominates - not because it looks expensive on paper.
#
#  Both are OFF by default: a plugin build must not drag a test framework into
#  its dependency graph. Enable with -DJ37_BUILD_TESTS=ON.
# ------------------------------------------------------------------------------
option(J37_BUILD_TESTS "Fetch Catch2 and nanobench and build the DSP unit tests" OFF)

if(J37_BUILD_TESTS)
    CPMAddPackage(
        NAME Catch2
        GITHUB_REPOSITORY catchorg/Catch2
        GIT_TAG v3.9.1
        EXCLUDE_FROM_ALL YES
        SYSTEM YES)

    CPMAddPackage(
        NAME nanobench
        GITHUB_REPOSITORY martinus/nanobench
        GIT_TAG v4.6.0
        EXCLUDE_FROM_ALL YES
        SYSTEM YES)
endif()

# ------------------------------------------------------------------------------
#  Plugin target metadata
#
#  These values are the ones the Projucer project carried, preserved exactly so
#  that a plugin built with CMake is seen by hosts and by existing saved
#  sessions as the same plugin as before:
#
#    ManufacturerCode 0x4d616e75  ("Manu" - see the note below)
#    PluginCode       0x5a793539  ("Zy59")
#    Bundle ID        com.MCsmes.first
#    Product name     Nonlin Analog Saturator
#
#  Only PRODUCT_NAME and DESCRIPTION changed with the rename. The manufacturer
#  code, the plugin code and the bundle ID are the plugin's identity to hosts and
#  to saved sessions - they stay exactly as they are, so an existing session still
#  finds the plugin after the rename and does not end up with two entries.
# ------------------------------------------------------------------------------
# Set to TRUE to copy the built plugins into the user plugin folders as part of
# the build. Off by default so a CI build cannot silently write to the machine's
# plugin directories.
option(COPY_PLUGIN_AFTER_BUILD "Copy plugins to the user plugin folders after building" OFF)

# The bundle id lives in a variable rather than inline in the juce_add_plugin call
# because the AUv3 app extension needs it a second time, with ".internal" appended (see
# the Apple block below). Written twice, the two copies drift and Xcode starts warning
# that the Info.plist and the PRODUCT_BUNDLE_IDENTIFIER build setting disagree.
set(J37_BUNDLE_ID "com.MCsmes.first")

juce_add_plugin(first
    COMPANY_NAME                "MCsmes"
    COMPANY_WEBSITE             "https://www.MCsmes.com"
    COMPANY_EMAIL               ""
    BUNDLE_ID                   "${J37_BUNDLE_ID}"

    # These two codes are the plugin's identity to every host. They are
    # deliberately NOT "corrected" to match the company name:
    #
    #   ManufacturerCode 0x4d616e75  "Manu"
    #   PluginCode       0x5a793539  "Zy59"
    #
    # 0x4d616e75 is the ASCII for "Manu" - the default that JUCE's template
    # supplies, and the value the Projucer project has always carried. The plugin
    # code was customised but the manufacturer code never was.
    #
    # Hosts key their plugin registry and their saved sessions on this PAIR, so
    # changing either one makes the plugin look like a different product:
    # existing sessions would not find it, and users would end up with both old
    # and new entries installed side by side. "Manu" therefore stays exactly as
    # it is, however odd it looks next to the company name.
    PLUGIN_MANUFACTURER_CODE    Manu
    PLUGIN_CODE                 Zy59
    PRODUCT_NAME                "Nonlin Analog Saturator"
    DESCRIPTION                 "Nonlinear analog tape saturation and magnetic coloration"
    FORMATS                     AU AUv3 VST3
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     ${COPY_PLUGIN_AFTER_BUILD}
    VST3_CATEGORIES             Fx
    AU_MAIN_TYPE                kAudioUnitType_Effect)

# ------------------------------------------------------------------------------
#  Third-party warning suppression
#
#  foleys_gui_magic is a JUCE MODULE, not a separate library, so its 55 translation
#  units are compiled as part of `first` and its warnings land in this project's build
#  log on every platform. The SYSTEM YES on its CPMAddPackage covers its HEADERS, not
#  its .cpp files. What it produces is eleven warnings on Linux and rather more on
#  AppleClang: unused locals, shadowed members, and implicit int-to-float conversions
#  in code this project does not own and does not patch.
#
#  The suppression is attached PER SOURCE FILE, never to the target, so a new warning
#  of these kinds in the plugin's own Source/*.cpp is still reported and still worth
#  fixing. A glob that matches nothing is a no-op, so a CPM path change degrades to
#  "no suppression" rather than to a configure error. MSVC is skipped entirely: it
#  spells the same warnings as /wdNNNN numbers, not -W flags.
# ------------------------------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set(j37_thirdparty_warnings -Wno-unused-variable -Wno-unused-but-set-variable
                               -Wno-unused-function -Wno-missing-prototypes -Wno-shadow)

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # -Wshorten-64-to-32 and -Wimplicit-const-int-float-conversion are Clang-only
        # spellings; GCC rejects them. -Wimplicit-const-int-float-conversion is not on
        # by default in Clang, so silencing it costs this project nothing.
        list(APPEND j37_thirdparty_warnings -Wno-implicit-int-float-conversion
                                             -Wno-implicit-const-int-float-conversion
                                             -Wno-shorten-64-to-32)
    endif()

    file(GLOB j37_thirdparty_sources CONFIGURE_DEPENDS
         "${CMAKE_BINARY_DIR}/deps/foleys_gui_magic/modules/foleys_gui_magic/*.cpp"
         "${CMAKE_BINARY_DIR}/deps/foleys_gui_magic/modules/foleys_gui_magic/*/*.cpp")

    if(j37_thirdparty_sources)
        set_source_files_properties(${j37_thirdparty_sources}
            PROPERTIES COMPILE_OPTIONS "${j37_thirdparty_warnings}")
    endif()

    # The per-source flags above are not enough on their own, because most of these
    # headers are parsed as part of THIS project's translation units: PluginEditor.h
    # includes <foleys_gui_magic/foleys_gui_magic.h> behind a __has_include guard, and
    # PluginProcessor.h pulls in chowdsp, so their headers are diagnosed while compiling
    # Source/*.cpp. Marking those include directories SYSTEM is what actually silences
    # them: a diagnostic raised INSIDE a system header is dropped, while a diagnostic in
    # our own code is still reported. That is strictly more precise than widening the
    # -Wno- list for our own sources, which is why the lists above stay as they are.
    file(GLOB j37_thirdparty_include_dirs CONFIGURE_DEPENDS
         "${CMAKE_BINARY_DIR}/deps/foleys_gui_magic/modules"
         "${CMAKE_SOURCE_DIR}/.cpm-cache/chowdsp_utils/*/modules/*")

    foreach(j37_include_dir IN LISTS j37_thirdparty_include_dirs)
        if(IS_DIRECTORY "${j37_include_dir}")
            target_include_directories(first SYSTEM PRIVATE "${j37_include_dir}")
        endif()
    endforeach()
endif()

# ------------------------------------------------------------------------------
#  What is left, and why it stays
#
#  On macOS this build now prints sixteen warnings, all of them
#  -Wimplicit-const-int-float-conversion from JUCE's own
#  juce_FastMathApproximations.h, raised while building
#  `juce_vst3_manifest_helper`. That is a NESTED CMake project JUCE generates and
#  configures on its own during the build, so it inherits neither this file's
#  CMAKE_CXX_FLAGS nor any per-target or per-source option set here - every way of
#  passing the flag from this file was tried and none of them reached it. Silencing
#  it would mean patching JUCE's generated helper, so the sixteen are left standing
#  and documented instead. Nothing in Source/*.cpp or in the plugin's own targets
#  warns on any platform.
# ------------------------------------------------------------------------------


# ------------------------------------------------------------------------------
#  Generated JuceHeader.h
#
#  The plugin sources include <JuceHeader.h>, which is how the previous Projucer
#  project exposed JUCE. CMake does NOT create that header automatically for
#  plugin targets - the Projucer wrote it into JuceLibraryCode/, and that folder
#  is gone.
#
#  juce_generate_juce_header() writes it into the target's generated-sources
#  directory and puts that directory on the include path. It also supplies
#  ProjectInfo::companyName, ProjectInfo::projectName and the project version,
#  which the same header used to carry.
#
#  The alternative - changing the sources to include the individual
#  <juce_audio_processors/juce_audio_processors.h> headers instead - is the
#  modern JUCE idiom and builds faster, because it stops pulling every module
#  header into every translation unit. That is a larger change to the source
#  files, so it is left as a follow-up rather than folded into this migration.
# ------------------------------------------------------------------------------
juce_generate_juce_header(first)

# ------------------------------------------------------------------------------
#  Sources
#
#  Source/PluginProcessor.cpp and PluginEditor.cpp are the whole plugin: JUCE
#  supplies the plugin entry points itself, so there is no main() in any target.
# ------------------------------------------------------------------------------
target_sources(first PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(first PRIVATE Source)

# ------------------------------------------------------------------------------
#  Module set
#
#  Only the modules the plugin actually uses. The Projucer project enabled
#  twenty-four, of which most were unused; this list is what the source needs:
#
#    audio_processors  AudioProcessor, the parameter tree, format wrappers
#    audio_utils       shared plugin UI helpers
#    dsp               Oversampling, AudioBlock (used by processTapeEngine)
#    gui_extra         drawing extras used by the editor
#    opengl            OpenGLContext, a best-effort accelerator for the panel
#    box2d             the decorative physics particles on the tape deck
#
#  audio_devices and cryptography are not used by this plugin directly. They are
#  listed because foleys_gui_magic DECLARES them as module dependencies in its own
#  header, and juce_add_modules resolves a module's dependencies from the linking
#  target's module list - leaving them out fails the configure with a missing
#  module error rather than at link time.
# ------------------------------------------------------------------------------
target_link_libraries(first
    PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_cryptography
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_opengl
        juce::juce_box2d
        chowdsp::chowdsp_dsp_utils
        chowdsp::chowdsp_math
        chowdsp::chowdsp_simd
        melatonin_inspector
        melatonin_blur
        foleys::foleys_gui_magic
        xsimd
        signalsmith-stretch
        fftconvolver
        imgui
        farbot
        rack_dsp
        readerwriterqueue
        kissfft
        pocketfft
        yasio
        dywapitchtrack
        PFFFT
        RTNeural
        dr_libs
        SndFile::sndfile
        yin_pitch
        pitch_detector
        dattorro_verb
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
#  Wiring the vendor vector libraries to the target.
#
#  The search for them is up with the rest of the dependency story; this is
#  where the result reaches the build. Both are PRIVATE: they accelerate this
#  plugin's own DSP and are none of a consumer of the target's business.
#
#  The IPP targets are checked individually rather than named blindly, because
#  which of them an installed oneAPI ships varies by version and by the
#  components that were installed with it - a missing optional component must
#  skip itself instead of failing the link. They are IMPORTED by Intel's own
#  config file, so this works with GCC and Clang too, not only with the Intel
#  compiler: ippsAbs_32f was compiled, linked and run against this on GCC 11.
# ------------------------------------------------------------------------------
if(J37_HAVE_VDSP)
    # The full framework path, which CMake links directly. The alternative
    # spelling is the raw "-framework Accelerate" flag; the path is used because
    # it is the same string that was verified to exist above, so the link and
    # the check can never disagree about what was found.
    target_link_libraries(first PRIVATE "${J37_ACCELERATE_FRAMEWORK}")
endif()

if(J37_HAVE_IPP)
    foreach(j37_ipp_target IPP::ipps IPP::ippi IPP::ippcore)
        if(TARGET ${j37_ipp_target})
            target_link_libraries(first PRIVATE ${j37_ipp_target})
        endif()
    endforeach()
endif()

# The macros the DSP code tests. Undefined when the library is absent, which is
# the point: one set of sources, three answers - vDSP on Apple, IPP where the
# SDK is installed, the portable kernels everywhere else.
if(J37_HAVE_VDSP)
    target_compile_definitions(first PRIVATE J37_HAVE_VDSP=1)
endif()

if(J37_HAVE_IPP)
    target_compile_definitions(first PRIVATE J37_HAVE_IPP=1)
endif()

# ------------------------------------------------------------------------------
#  Compile definitions
#
#  JUCE_VST3_CAN_REPLACE_VST2 and JUCE_STRICT_REFCOUNTEDPOINTER were both set in
#  the Projucer project, so they are kept for behavioural parity.
#
#  The web browser and CURL integration are switched off deliberately: the
#  plugin does not use them, and leaving them on adds build dependencies for
#  nothing.
# ------------------------------------------------------------------------------
target_compile_definitions(first PUBLIC
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

# -----------------------------------------------------------------------------
#  Build identity
# -----------------------------------------------------------------------------
# The editor prints the commit it was built from, so a bug report can name an exact
# build instead of "the latest one". The hash is read once, here, and handed to the
# sources as J37_BUILD_COMMIT.
#
# Two rules keep this from ever breaking a build:
#   1. If the tree is not a git checkout (a tarball, a vendored copy, an export) the
#      build still succeeds and simply reports "unknown".
#   2. A dirty working tree gets a trailing "+", so a local build with uncommitted
#      edits can never be mistaken for a clean one.
#
# .git/HEAD is registered as a configure dependency so that switching branches or
# pulling a new commit re-runs CMake and refreshes the string; a CI checkout is
# detached at the exact SHA, so its .git/HEAD changes with every build anyway.
# A purely local build that only advances HEAD while staying on the same branch needs
# an explicit re-configure, which is why the summary line below prints the value.
# -----------------------------------------------------------------------------
find_package(Git QUIET)
set(J37_BUILD_COMMIT "unknown")

if (GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short=8 HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE J37_GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE J37_GIT_STATUS)

    if (J37_GIT_STATUS EQUAL 0 AND J37_GIT_HASH)
        set(J37_BUILD_COMMIT "${J37_GIT_HASH}")

        # --untracked-files=no matters: CI configures into build/ INSIDE the source
        # tree, so a plain --porcelain would list that directory on every single CI
        # build and stamp every released binary as dirty. Only a modification to a
        # TRACKED file means the binary does not match its commit; untracked build
        # output never does.
        execute_process(
            COMMAND ${GIT_EXECUTABLE} status --porcelain --untracked-files=no
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            OUTPUT_VARIABLE J37_GIT_DIRTY
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)

        if (J37_GIT_DIRTY)
            set(J37_BUILD_COMMIT "${J37_BUILD_COMMIT}+")
        endif()
    endif()
endif()

# .git/HEAD is a file in a normal checkout and a path in a worktree; only register it
# when it actually exists, so CMAKE_CONFIGURE_DEPENDS never points at a missing file
# (which would make every configure fail).
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git/HEAD")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 "${CMAKE_CURRENT_SOURCE_DIR}/.git/HEAD")
endif()

target_compile_definitions(first PRIVATE J37_BUILD_COMMIT="${J37_BUILD_COMMIT}")

# ------------------------------------------------------------------------------
#  macOS specifics
# ------------------------------------------------------------------------------
if(APPLE)
    # AUv3 is delivered as an app extension and must be sandboxed. JUCE ships a
    # default entitlements file for exactly this; without it the extension is
    # rejected when the host tries to load it.
    set(appex_support_target "${CMAKE_CURRENT_SOURCE_DIR}/Builds/AUv3Support")

    if(NOT EXISTS "${appex_support_target}/AUv3_AppExtension.entitlements")
        file(MAKE_DIRECTORY "${appex_support_target}")
        file(WRITE "${appex_support_target}/AUv3_AppExtension.entitlements"
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">
<plist version=\"1.0\">
<dict>
	<key>com.apple.security.app-sandbox</key>
	<true/>
</dict>
</plist>
")
    endif()

    # JUCE names the plugin targets ${target}_${kind}: the shared code is the bare
    # `first` target (a STATIC library) and each format gets its own suffix -
    # first_VST3, first_AU, first_AUv3. There is no `first_SharedCode` target; the
    # `_SharedCode` suffix only appears in the output file name.
    #
    # Only the AUv3 app extension is sandboxed. A plain AU component is loaded
    # directly by the host process and must NOT carry the app-sandbox entitlement:
    # an Audio Unit that declares com.apple.security.app-sandbox is rejected by the
    # AU validation step and silently disappears from Logic's plug-in list.
    # Xcode warns unless PRODUCT_BUNDLE_IDENTIFIER is byte-for-byte the
    # CFBundleIdentifier already in the target's Info.plist. AUv3 is TWO targets and
    # JUCE gives them two different ids, both derived from the plugin bundle id: the
    # extension itself gets ".firstAUv3" and the helper framework it embeds gets
    # ".internal". Setting only one of them just trades one warning for another, so both
    # are set here, from the same J37_BUNDLE_ID the Info.plists were generated from.
    if(TARGET first_AUv3)
        set_target_properties(first_AUv3 PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS
            "${appex_support_target}/AUv3_AppExtension.entitlements"
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER
            "${J37_BUNDLE_ID}.firstAUv3")
    endif()

    if(TARGET first_AUv3_Framework)
        set_target_properties(first_AUv3_Framework PROPERTIES
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER
            "${J37_BUNDLE_ID}.internal")
    endif()

    # (see the third-party warning block above, which now covers Apple too)

    # The -Wshorten-64-to-32 that used to be enabled here is gone. It was switched on
    # to keep one specific conversion visible when it was the only warning in the
    # build; Clang does not enable that group by default, so asking for it by name
    # only ever produced a warning nobody was reading. The conversion itself was fixed
    # at the source instead (getXmlFromBinary takes an int, so the size_t cast that
    # provoked it was simply wrong).
endif()

# ------------------------------------------------------------------------------
#  Windows specifics
# ------------------------------------------------------------------------------
if(WIN32)
    # The plugin sources use standard library calls that MSVC flags as unsafe
    # in its default mode. The Projucer project set this too.
    target_compile_definitions(first PRIVATE _CRT_SECURE_NO_WARNINGS)

    # Warning level and conformance only. The optimisation flags that used to be here
    # (/O2 /Ob3 /GL /LTCG) have been removed: juce_recommended_config_flags and
    # juce_recommended_lto_flags already set all four, so re-stating them only produced
    #     warning D9025: overriding '/Ob2' with '/Ob3'
    # on every single translation unit. JUCE's own settings are correct; the duplicate
    # was noise, and duplicating build flags is how configured and actual optimisation
    # silently drift apart.
    if(MSVC)
        target_compile_options(first PRIVATE /W4 /permissive-)

        # ------------------------------------------------------------------
        #  Why a Windows build is much slower than the macOS one, and what
        #  actually helps.
        #
        #  Three separate costs, only one of which is about compiling code:
        #
        #   1. juceaide. Windows runs juceaide DURING configure - to emit the
        #      .rc resource script and the .ico - so configure has to build
        #      juceaide.exe from source first. macOS produces its plists with
        #      plain configure_file() and never invokes juceaide at configure
        #      time. That is why `cmake -S . -B build` alone can cost as much
        #      as an entire Mac build. It cannot be avoided, but it is worth
        #      knowing that the time is spent before any of your code is
        #      touched, not in it.
        #
        #   2. /GL + /LTCG. Whole-program optimisation makes MSVC write and
        #      then re-read intermediate representation for every object, which
        #      roughly doubles the work. Clang on macOS does LTO with the same
        #      flag and markedly less overhead. This is by far the largest
        #      avoidable item, and it is only worth its cost on release
        #      binaries. Set J37_FAST_WINDOWS_BUILD=ON to drop it for
        #      iteration; a distributable build should leave it off.
        #
        #   3. Parallelism. MSBuild defaults to a small number of concurrent
        #      CL processes. /MP scales to the machine, and CMAKE_BUILD_PARALLEL_LEVEL
        #      controls MSBuild's own parallelism for the overall build.
        # ------------------------------------------------------------------

        option(J37_FAST_WINDOWS_BUILD "Windows: skip link-time code generation for faster iteration" ON)

        if(J37_FAST_WINDOWS_BUILD)
            # ------------------------------------------------------------------
            #  The LTO flags do not live on this target, so they cannot be removed
            #  from it. JUCE puts them on the INTERFACE of juce_recommended_lto_flags
            #  as generator expressions:
            #
            #     target_compile_options(... INTERFACE $<$<CONFIG:Release>:-GL>)
            #     target_link_libraries(... INTERFACE $<$<CONFIG:Release>:-LTCG>)
            #
            #  Three consequences, all of which an earlier attempt at this got wrong:
            #
            #   - The flags arrive through a linked library, so filtering this
            #     target's own COMPILE_OPTIONS/LINK_OPTIONS finds nothing at all.
            #   - They are generator expressions, so matching on the literal "-GL"
            #     never matches either.
            #   - The link flag is added with target_link_libraries, not
            #     target_link_options, so it is not in LINK_OPTIONS to begin with.
            #
            #  Clearing the interface is the only thing that takes effect. It is safe
            #  here because this target is the only consumer of that helper in this
            #  project, and it is scoped inside an opt-out the user can flip.
            # ------------------------------------------------------------------
            if(TARGET juce_recommended_lto_flags)
                set_property(TARGET juce_recommended_lto_flags PROPERTY
                             INTERFACE_COMPILE_OPTIONS "")
                set_property(TARGET juce_recommended_lto_flags PROPERTY
                             INTERFACE_LINK_LIBRARIES "")
            endif()

            message(STATUS "Windows fast build: LTO disabled (J37_FAST_WINDOWS_BUILD=ON)")
            message(STATUS "  Set -DJ37_FAST_WINDOWS_BUILD=OFF for a distributable build.")
        endif()

        # /MP: compile translation units in parallel within one project. Without it
        # MSBuild parallelises across projects, and this plugin is only a handful of
        # projects, so most cores sit idle.
        target_compile_options(first PRIVATE /MP)

        # /Zf: generate debug info in parallel. Harmless and noticeable on a build
        # with this many translation units.
        target_compile_options(first PRIVATE /Zf)
    endif()
endif()

# ------------------------------------------------------------------------------
#  Summary
# ------------------------------------------------------------------------------
message(STATUS "")
message(STATUS "Nonlin Analog Saturator (first) ${PROJECT_VERSION}")
message(STATUS "  JUCE        : ${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
message(STATUS "  System      : ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Generator   : ${CMAKE_GENERATOR}")
message(STATUS "  Build type  : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build commit: ${J37_BUILD_COMMIT}")
if(APPLE)
    message(STATUS "  Archs       : ${CMAKE_OSX_ARCHITECTURES}")
    message(STATUS "  Formats     : VST3, AU, AUv3")
else()
    message(STATUS "  Formats     : VST3")
endif()
message(STATUS "  Libraries   : chowdsp_utils, melatonin_inspector, melatonin_blur, foleys_gui_magic, xsimd")
message(STATUS "                signalsmith-stretch, fftconvolver, imgui, iPlug2 (source only)")
message(STATUS "                chowcentaur, vcv_prototype, rack, soundpipe, aubio (source only)")
message(STATUS "                farbot, readerwriterqueue, kissfft, pocketfft, yasio, dywapitchtrack, pffft")
if(J37_HAVE_VDSP)
    message(STATUS "  Vector libs : Apple vDSP (Accelerate)")
elseif(APPLE)
    message(STATUS "  Vector libs : Apple vDSP NOT found - portable paths only")
else()
    message(STATUS "  Vector libs : Apple vDSP (Apple only, not this platform)")
endif()
if(J37_HAVE_IPP)
    message(STATUS "                Intel IPP found: ${J37_IPP_HOW}")
elseif(J37_ENABLE_IPP)
    message(STATUS "                Intel IPP not installed - portable paths only")
else()
    message(STATUS "                Intel IPP disabled by J37_ENABLE_IPP=OFF")
endif()
if(J37_BUILD_TESTS)
    message(STATUS "  Dev tests   : ON  (Catch2, nanobench)")
else()
    message(STATUS "  Dev tests   : OFF (-DJ37_BUILD_TESTS=ON to enable Catch2 + nanobench)")
endif()
message(STATUS "")
message(STATUS "Artifacts land in: ${CMAKE_BINARY_DIR}")
message(STATUS "To install into the user plugin folders, configure with")
message(STATUS "  -DCOPY_PLUGIN_AFTER_BUILD=TRUE")
message(STATUS "")