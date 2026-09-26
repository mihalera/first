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
    GIT_TAG 13.0.0
    EXCLUDE_FROM_ALL YES
    SYSTEM YES
    OPTIONS
        "BUILD_TESTS OFF"
        "BUILD_BENCHMARK OFF"
        "BUILD_EXAMPLES OFF")

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
        GIT_TAG v3.7.1
        EXCLUDE_FROM_ALL YES
        SYSTEM YES)

    CPMAddPackage(
        NAME nanobench
        GITHUB_REPOSITORY martinus/nanobench
        GIT_TAG v4.3.11
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

juce_add_plugin(first
    COMPANY_NAME                "MCsmes"
    COMPANY_WEBSITE             "https://www.MCsmes.com"
    COMPANY_EMAIL               ""
    BUNDLE_ID                   "com.MCsmes.first"

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
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

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
    if(TARGET first_AUv3)
        set_target_properties(first_AUv3 PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS
            "${appex_support_target}/AUv3_AppExtension.entitlements")
    endif()

    # Keep the size_t -> int conversion warning visible. It is harmless (the state
    # blob is never near 2 GB) but it was the only warning in the whole build, so
    # it is worth not losing.
    target_compile_options(first PRIVATE -Wshorten-64-to-32)
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