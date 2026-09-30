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

# ------------------------------------------------------------------------------
#  VENDORED THIRD-PARTY LIBRARIES (ThirdParty/).
#
#  The libraries the plugin's own sources INCLUDE are checked into the
#  repository under ThirdParty/ at the exact versions CPM used to pin, and
#  every block below prefers the vendored copy when it exists. CPM's fetch
#  becomes the FALLBACK, not the source of truth: a fresh clone configures
#  and builds offline, with no .cpm-cache and no network, and CI downloads
#  nothing but JUCE itself.
#
#  Version discipline: the vendored tree is the SAME pin as the CPM block
#  beside it. When re-pinning, change both in one commit or the two paths
#  silently build against different sources.
#
#    ThirdParty/farbot/              hogliux/farbot  @ d8f132c2 (include/)
#    ThirdParty/pocketfft/           mreineck/pocketfft @ c90e55b3 (root header)
#    ThirdParty/signalsmith-stretch/ Signalsmith-Audio/signalsmith-stretch @ 1.4.0
#                                    + sibling signalsmith-linear @ 0.6.4 inside
#    ThirdParty/nlohmann_json/       nlohmann/json @ v3.12.0 (single_include/)
#    ThirdParty/xsimd/               xtensor-stack/xsimd @ 14.3.0 (include/)
#    ThirdParty/glm/                 g-truc/glm @ 1.0.3 (repo root)
#    ThirdParty/RTNeural/            jatinchowdhury18/RTNeural @ 95c3c0f9 (STL backend)
#
#  All licences are permissive and ship inside their folders.
# ------------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../ThirdParty/farbot/include")
    j37_declare_header_only_library(farbot
        "${CMAKE_CURRENT_LIST_DIR}/../ThirdParty/farbot/include")
endif()

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../ThirdParty/pocketfft/pocketfft_hdronly.h")
    j37_declare_header_only_library(pocketfft
        "${CMAKE_CURRENT_LIST_DIR}/../ThirdParty/pocketfft")
endif()

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../ThirdParty/signalsmith-stretch/include/signalsmith-stretch/signalsmith-stretch.h")
    j37_declare_header_only_library(signalsmith-stretch
        "${CMAKE_CURRENT_LIST_DIR}/../ThirdParty/signalsmith-stretch/include")
endif()

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../ThirdParty/nlohmann_json/single_include/nlohmann/json.hpp")
    j37_declare_header_only_library(nlohmann_json
        "${CMAKE_CURRENT_LIST_DIR}/../ThirdParty/nlohmann_json/single_include")
endif()

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../ThirdParty/xsimd/include/xsimd/xsimd.hpp")
    j37_declare_header_only_library(xsimd
        "${CMAKE_CURRENT_LIST_DIR}/../ThirdParty/xsimd/include")
endif()

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../ThirdParty/glm/glm/glm.hpp")
    j37_declare_header_only_library(glm
        "${CMAKE_CURRENT_LIST_DIR}/../ThirdParty/glm")
endif()

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../ThirdParty/RTNeural/RTNeural/RTNeural.h")
    # The vendored checkout is already pruned to the STL backend's needs: no
    # Eigen submodule, xsimd optional. Its include path is the checkout root;
    # its bundled modules/json serves the model loader's json parse.
    j37_declare_header_only_library(RTNeural_headers
        "${CMAKE_CURRENT_LIST_DIR}/../ThirdParty/RTNeural")
endif()

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
#  Vendored copy wins; CPM is the fallback.
if(NOT TARGET xsimd)
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
endif ()

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
#  Vendored copy wins (block at the top of this file); CPM is the fallback.
if(NOT TARGET signalsmith-stretch)
CPMAddPackage(
    NAME signalsmith-dsp
    GITHUB_REPOSITORY Signalsmith-Audio/signalsmith-stretch
    GIT_TAG 1.4.0
    DOWNLOAD_ONLY YES)
endif ()

#  signalsmith-linear: since the plugin's sources started including the
#  stretch header directly, its sibling matters: signalsmith-stretch.h line 4
#  does #include "signalsmith-linear/stft.h", pointing at
#  Signalsmith-Audio/linear. The relative include means the sibling must sit
#  NEXT TO the stretch checkout under a directory literally named
#  "signalsmith-linear" - the include resolves against the includING file's
#  directory first, so that layout is the whole integration; exposing linear's
#  root on the include path would NOT work, because the include is namespaced
#  by the folder name. Pinned to the 0.6.4 tag.
CPMAddPackage(
    NAME signalsmith-linear
    GITHUB_REPOSITORY Signalsmith-Audio/linear
    GIT_TAG 0.6.4
    # ...inside the stretch checkout, because the stretch header sits at that
    # checkout's ROOT (not under include/), so the quoted include resolves
    # against the checkout root as the base directory.
    SOURCE_DIR "${signalsmith-dsp_SOURCE_DIR}/signalsmith-linear"
    DOWNLOAD_ONLY YES)

j37_declare_header_only_library(signalsmith-stretch
    "${signalsmith-dsp_SOURCE_DIR}/include")

# PocketFFT - mreineck/pocketfft, the FFT behind NumPy: exact transforms for
# arbitrary sizes out of one header. The "cpp" branch is the header-only
# edition (pocketfft_hdronly.h at the repository root); the master branch is
# the C/Fortran hybrid, which is not what a plugin wants.
#  Vendored copy wins; CPM is the fallback.
if(NOT TARGET pocketfft)
CPMAddPackage(
    NAME pocketfft
    GITHUB_REPOSITORY mreineck/pocketfft
    GIT_TAG c90e55b3d529f8efa40ed01a20de22405f45fc65
    DOWNLOAD_ONLY YES)

j37_declare_header_only_library(pocketfft "${pocketfft_SOURCE_DIR}")
endif ()

# ==============================================================================
#  nlohmann/json - JSON for Modern C++, a header-only DOM parser and
#  serialiser.
#
#  The newest release is 3.12.0 (2025-04-11), and it is fetched the same way
#  the other header-only boxes here are: DOWNLOAD_ONLY, so the repository's own
#  CMakeLists is never added as a subproject. That is not a shortcut - it is the
#  point. Upstream's top-level file opens with a cmake_minimum_required whose
#  floor has sat below 3.5 for years, and CMake 4.x removed compatibility with
#  minimums that low, so configuring it through add_subdirectory aborts the
#  whole build on the runners that ship CMake 4. That is exactly the failure
#  libsndfile caused here before it was pinned to a commit (see the note
#  further down), and it is why "fetch the sources, expose the include
#  directory" is the established pattern in this file for a single-header C++
#  library rather than a decision made twice by accident.
#
#  single_include/nlohmann/json.hpp is the one file upstream ships for exactly
#  this use - the amalgamated header, resolved and dependency-ordered - and it
#  is what every other distribution form is generated from. single_include over
#  include/ also means one translation unit's worth of header, and no way for a
#  call site to pick up half the library from here and half from somewhere else.
#
#  Note that RTNeural, fetched further down, bundles its own copy of JSON for
#  its model format at modules/json/json.hpp (3.11.1). It is a different path
#  and a different version, and both are header-only, so there is nothing to
#  link and nothing that collides at build time. Code in this project should
#  include the canonical <nlohmann/json.hpp> and let this target's include
#  directory resolve it, rather than reaching for RTNeural's copy.
# ==============================================================================
#  Vendored copy wins; CPM is the fallback.
if(NOT TARGET nlohmann_json)
CPMAddPackage(
    NAME nlohmann_json
    GITHUB_REPOSITORY nlohmann/json
    GIT_TAG v3.12.0
    DOWNLOAD_ONLY YES)

j37_declare_header_only_library(nlohmann_json "${nlohmann_json_SOURCE_DIR}/single_include")
endif ()

# farbot - hogliux/farbot, "FAbian's Realtime Box o' Tricks": the
# realtime-safe patterns (RealtimeObject, fifo, AsyncCaller). The library part
# is HEADER-ONLY (its CMake only builds tests, and googletest is a submodule
# this build never initialises), so it is downloaded and exposed include-only.
#  Vendored copy wins; CPM is the fallback.
if(NOT TARGET farbot)
CPMAddPackage(
    NAME farbot
    GITHUB_REPOSITORY hogliux/farbot
    GIT_TAG d8f132c2e2ac44b379632e452700abe71ca3a92e
    DOWNLOAD_ONLY YES)

j37_declare_header_only_library(farbot "${farbot_SOURCE_DIR}/include")
endif ()

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
#  Vendored copy wins (the vendored checkout is pruned to the STL backend and
#  registered as RTNeural_headers at the top of this file); CPM is the fallback.
if(NOT TARGET RTNeural AND NOT TARGET RTNeural_headers)
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
endif ()

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
#  libsndfile - the C library for reading and writing sampled audio.
#
#  It was left out of the first pass because its newest RELEASE, 1.2.2, opens
#  with cmake_minimum_required(VERSION 3.1..3.18) and CMake 4.x - the release
#  the current CI runners ship - dropped compatibility with minimums below 3.5,
#  so configuring it aborted the whole build on the macOS and Windows runners.
#  1.2.2 is still the newest release (checked: the tag list has not moved past
#  it), so waiting for a release would have meant waiting indefinitely.
#
#  The fix is the range form rather than a version bump: master opens with
#  cmake_minimum_required(VERSION 3.5...4.0). The ...4.0 upper bound is what
#  matters - it declares the policy range up to CMake 4, so a 4.x caller
#  configures it instead of refusing it. So the pin is a master commit, and the
#  note about re-pinning on the next release stays true but is no longer
#  blocking: when 1.2.3 ships, prefer the tag.
#
#  Options, all of them off upstream defaults, because this is vendored into a
#  plugin and the defaults pull in the world:
#    ENABLE_EXTERNAL_LIBS  FLAC / Vorbis / Opus, and the only path that would
#                          need a pkg-config find_package
#    ENABLE_MPEG           the same, via the system libmpeg
#    BUILD_PROGRAMS        sndfile-info, sndfile-convert and friends - command
#                          line tools, which have no place in an audio plugin
#                          and are what pulls in the man page machinery
#    BUILD_TESTING         upstream's own test programs
#    BUILD_EXAMPLES        ditto
#    BUILD_CMAKE_LIB_PACKAGE
#                          the export/install rules for a CMake package
#
#  The result is a self-contained static `sndfile` target: C only, no external
#  codec, no host tooling (verified by configuring and building just this
#  target - libsndfile.a links with nothing).
# ==============================================================================
CPMAddPackage(
    NAME sndfile
    GITHUB_REPOSITORY libsndfile/libsndfile
    GIT_TAG b9103bd48b6c8fb517ae737fe3baee0c718b804c
    EXCLUDE_FROM_ALL YES
    SYSTEM YES
    OPTIONS
        "ENABLE_EXTERNAL_LIBS OFF"
        "ENABLE_MPEG OFF"
        "BUILD_PROGRAMS OFF"
        "BUILD_TESTING OFF"
        "BUILD_EXAMPLES OFF"
        "BUILD_CMAKE_LIB_PACKAGE OFF")

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

# ==============================================================================
#  Transient shaping, DSP and graphics libraries.
#
#  Requested as a group. They are not one thing: three are libraries this build
#  can compile against today, three are SOURCE ONLY because the thing that was
#  asked for is not C++, and one does not exist. Each says which, below, rather
#  than being wrapped in a target that would fail to link or a note that would
#  have to be found later.
#
#  Five of the six carry a permissive licence - BSL-1.0, Zlib, MIT, BSD-3 and
#  MIT - so they are safe to vendor into a closed-source plugin. The sixth
#  (cycfi/infra, below) ships NO licence at all, which is a question for a
#  lawyer rather than something to paper over; it is fetched because Q cannot
#  compile without it, and it is called out again at its own block.
#
#  None of these is currently CALLED by the plugin. They are pinned and
#  available, which is what was asked for; adopting one is a separate decision
#  that the profiling should make.
# ==============================================================================

# ------------------------------------------------------------------------------
#  Q - cycfi/q, the Q Audio DSP Library. A header-only C++20 audio DSP library
#  (BSL-1.0, the Boost licence: permissive, and not MIT as one might assume from
#  the author's other projects): filters, oscillators, envelopes, pitch and
#  onset detection, written to run on small microcontrollers. Pin v1.0.1, the
#  1.0 release. C++20 matches this project, so the standard is not a barrier.
#
#  DOWNLOAD_ONLY and exposed include-only rather than added as a subproject. Q
#  DOES ship a CMakeLists - it declares an INTERFACE target called libq, so
#  add_subdirectory would work - but it is a build of Q's own examples, tests
#  and q_io (Q_BUILD_EXAMPLES / Q_BUILD_TEST / Q_BUILD_IO all default ON) and it
#  pulls a second dependency, cycfi/infra, through FetchContent at configure
#  time. Adding a plugin to this project should not silently start compiling
#  someone else's test suite, so the headers are taken and the build is not.
#
#  The include path is q_lib/include, NOT the repository root, and there is no
#  umbrella header: Q is included per module, so the canonical forms are
#      #include <q/fx/delay.hpp>        a delay line
#      #include <q/fx/biquad.hpp>       filter coefficients
#      #include <q/utility/...>         the small helpers
#  Passing the repository root would compile nothing at all, which is why the
#  path is written out here. q_io/ is deliberately left off the include path:
#  it is file and stream I/O, which a plugin has no use for.
#
#  Header-only means nothing to link - target_include_directories is the entire
#  integration. Note that Q is a fairly small-footprint embedded library, so it
#  is a better fit for per-block helper maths than for the tape engine; the
#  engine's own filters are hand-written and stay that way.
# ------------------------------------------------------------------------------
CPMAddPackage(
    NAME q_dsp
    GITHUB_REPOSITORY cycfi/q
    GIT_TAG v1.0.1
    DOWNLOAD_ONLY YES)

j37_declare_header_only_library(q_dsp "${q_dsp_SOURCE_DIR}/q_lib/include")

#  cycfi/infra - Q cannot compile without it. Q's headers include
#  <infra/support.hpp>, and the only place that header comes from is this
#  repository, which Q's own CMakeLists pulls in with FetchContent. Taking Q
#  as headers only therefore takes this too, and forgetting it is a confusing
#  failure: the error names infra/support.hpp, in a library the reader did not
#  ask about. It is pinned to the same 2024_MAY tag Q names.
#
#  LICENCE: there is none. The repository has no licence file, no SPDX metadata
#  and a four-line README that does not mention one, so there is no grant of
#  rights to rely on. It is fetched so that Q is not broken on arrival, and
#  because it is on the include path of anything that uses Q, it is worth
#  settling before any of it ships. Vendoring it into a closed-source plugin is
#  the case that needs an answer.
CPMAddPackage(
    NAME cycfi_infra
    GITHUB_REPOSITORY cycfi/infra
    GIT_TAG 2024_MAY
    DOWNLOAD_ONLY YES)

j37_declare_header_only_library(cycfi_infra "${cycfi_infra_SOURCE_DIR}/include")

#  infra needs four include paths, not one - its own CMakeLists names all four -
#  and the other three are the backports it falls back to when the standard
#  library is short of them. The helper above takes a single directory, so the
#  rest are added here, each only if it is actually present: these are git
#  submodules, and a checkout made without them should still configure rather
#  than fail on a -isystem pointing at nothing.
foreach(infra_extra IN ITEMS
        external/filesystem/include
        external/optional-lite/include
        external/string-view-lite/include)
    if(EXISTS "${cycfi_infra_SOURCE_DIR}/${infra_extra}")
        target_include_directories(cycfi_infra SYSTEM INTERFACE
            "${cycfi_infra_SOURCE_DIR}/${infra_extra}")
    endif()
endforeach()

# Transitive, so that linking q_dsp is enough: whoever includes Q gets infra's
# include path without having to know that Q has a dependency of its own.
target_link_libraries(q_dsp INTERFACE cycfi_infra)

# ------------------------------------------------------------------------------
#  Sokol GFX - floooh/sokol, the single-header immediate-mode 3D renderer
#  (Zlib). The name asked for is sokol_gfx.h, but it is not standalone: a
#  working integration is three headers, and which three is a decision, so all
#  of the useful ones are exposed rather than just the one that was named.
#
#    sokol_gfx.h       the renderer
#    sokol_glue.h      wires sokol_gfx to a windowing layer - INCLUDE THIS LAST
#    sokol_log.h       sokol_gfx.h includes it, so it must be reachable
#    sokol_time.h      likewise
#
#  There is no sokol_gpucam.h in this tree, which is worth saying because it is
#  the header sokol's own examples use for a camera and it used to exist: it was
#  folded into sokol_gfx upstream. The mat4 work it did is what GLM below is
#  for, and that pairing is the intended one - sokol for the renderer, GLM for
#  the maths, rather than both carrying a partial copy of it.
#
#  sokol_app.h is deliberately NOT exposed as a target of its own, because it is
#  sokol's own windowing and event layer and JUCE already is one. Two of them
#  fighting over the same window is the integration that does not work.
#
#  Two things whoever uses this needs to know, both of which are silent when
#  they are wrong: the backend must be selected by defining SOKOL_GLCORE (or
#  SOKOL_METAL / SOKOL_D3D11) before including sokol_gfx.h, and the
#  implementation must be compiled exactly once in the whole program by
#  defining SOKOL_IMPL in a single translation unit. The other headers are
#  declaration-only and including sokol_glue.h anywhere else just declares the
#  same functions again.
#
#  No tags upstream - the release tags are pre-* snapshots of upcoming API
#  changes, not versions - so this is pinned to the master head, the same
#  choice dattorro_verb makes.
# ------------------------------------------------------------------------------
CPMAddPackage(
    NAME sokol_gfx
    GITHUB_REPOSITORY floooh/sokol
    GIT_TAG 2e75443dbd4940b5aa8d76a8e479f8e4b270b9a3
    DOWNLOAD_ONLY YES)

j37_declare_header_only_library(sokol_gfx "${sokol_gfx_SOURCE_DIR}")

# ------------------------------------------------------------------------------
#  GLM - g-truc/glm, OpenGL Mathematics (MIT): the header-only C++ maths
#  library whose types and semantics follow the GLSL specification, so shader
#  maths and host maths read the same. Pin 1.0.3, the current release.
#
#  The include path is the repository root, which is what makes the canonical
#  `#include <glm/glm.hpp>` resolve. GLM needs no configuration, but it does
#  respond to it: the defaults are float-only and no extensions, so anything
#  expecting doubles or a particular packed layout should say so with
#  GLM_FORCE_* rather than discovering it at the call site.
#
#  This is the companion to sokol_gfx above rather than a duplicate of it:
#  sokol_gpucam.h carries the handful of mat4 helpers sokol itself needs, and
#  GLM is the general library for everything else.
# ------------------------------------------------------------------------------
#  Vendored copy wins; CPM is the fallback.
if(NOT TARGET glm)
CPMAddPackage(
    NAME glm
    GITHUB_REPOSITORY g-truc/glm
    GIT_TAG 1.0.3
    DOWNLOAD_ONLY YES)

j37_declare_header_only_library(glm "${glm_SOURCE_DIR}")
endif ()

# ------------------------------------------------------------------------------
#  SOURCE ONLY - three transient shapers, none of which is a C++ library.
#
#  All three were asked for by name as if they were dependencies. They are
#  vendored so the source is here and the algorithm is readable, and none of
#  them is compiled, because there is nothing here a C++ build could consume.
#  Stating that here is the point: a target that could not link would be worse
#  than an honest comment, and a silent no-op is worse than both.
# ------------------------------------------------------------------------------

#  TransDes - ylmrx/transdes (MIT, tag 0.0.3): a transient DESIGNER - an
#  envelope detector with separate attack and sustain sensitivity, a gain
#  computer, and a detector that can be split across two signals. A finished
#  JUCE plugin, not a library: Source/ is one plugin's code and TransDes.jucer
#  is its project file, so the DSP is reachable but the pieces are a processor,
#  not an API.
#  Its detector is the interesting part for this engine: a differentiated
#  envelope with a high-pass on the derivative, which is what separates a
#  transient from a sustained change in level.
CPMAddPackage(
    NAME transdes
    GITHUB_REPOSITORY ylmrx/transdes
    GIT_TAG 0.0.3
    DOWNLOAD_ONLY YES)

#  Whetstone - unicornsasfuel/whetstone (BSD-3): a band-limited transient
#  shaper - it extracts a band you choose with a low and a high cutoff, shapes
#  only that band, and mixes it back. Written in FAUST, and that is the whole
#  of the problem: whetstone.dsp is Faust source, and the juce/ folder is a
#  Projucer project wrapping a FaustPluginProcessor whose real DSP is a
#  Faust-generated C++ file that is not in the repository.
#  Consuming it means running the Faust compiler in the build and checking in
#  or generating that output, which is a toolchain decision rather than a
#  dependency one - so it is left as source. If the band-split behaviour is
#  wanted, the algorithm is legible in the .dsp and is a small amount of work
#  to write directly.
#  No tags upstream; pinned to the main head.
CPMAddPackage(
    NAME whetstone
    GITHUB_REPOSITORY unicornsasfuel/whetstone
    GIT_TAG 83d715a09357841c75ed12151b2061901b2a4625
    DOWNLOAD_ONLY YES)

#  Bark-scale multi-band transient shaper - sevagh/multiband-transient-shaper
#  (MIT): a Bark frequency filterbank feeding a differential-envelope
#  transient shaper - an implementation of the SPL design. This one is not
#  C++ at all: the repository is assets/, matlab/ and python/, with the last
#  commit in 2020. It is the ALGORITHM, written out in the language it was
#  developed in, and there is nothing here to compile into a plugin.
#  It is worth having for the read: a Bark filterbank is the standard answer
#  to "shape transients in one band without touching the rest", and the
#  filterbank design is more reusable than the shaper around it.
#  No tags upstream; pinned to the master head.
CPMAddPackage(
    NAME multiband_transient_shaper
    GITHUB_REPOSITORY sevagh/multiband-transient-shaper
    GIT_TAG 7c77e23c1708f4252ba04fc08dcad503f340bab4
    DOWNLOAD_ONLY YES)

# ------------------------------------------------------------------------------
#  juce_opengl_3d - NOT FETCHED, because it does not exist.
#
#  There is no JUCE module by that name. The module list in this checkout
#  contains juce_opengl and no 3D sibling of it, and the 3D drawing support in
#  JUCE is shader-level: you write a GLSL shader and hand it to
#  OpenGLShaderProgram, rather than being handed a scene graph. So there is
#  nothing to pin and no tag to record.
#
#  What already covers it:
#    juce::juce_opengl      OpenGLContext, OpenGLRenderer, OpenGLShaderProgram -
#                          ALREADY LINKED by this target for the panel's
#                          best-effort accelerated repaint. A 3D panel would be
#                          a shader on that existing context, not a new module.
#    sokol_gfx + glm       above in this file - the renderer and the maths, if
#                          the 3D work outgrows hand-written GLSL.
#  If a specific third-party JUCE module called juce_opengl_3d was meant, it is
#  a fork or a private module rather than something upstream publishes, and it
#  would have to be named by repository and tag before it could be added here.
# ------------------------------------------------------------------------------
