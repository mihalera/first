# ==============================================================================
#
#   Analog Saturator - J37 tape machine emulation
#
#   This is the single source of truth for the build. The Projucer project
#   (first.jucer) and its generated Builds/ folder have been retired: every
#   setting that used to live there is reproduced below, but as text that the
#   build reads directly instead of as generated files that can drift.
#
#   Supported configurations
#     Windows : Visual Studio / x64        -> VST3
#     macOS   : Xcode / universal (arm64 + x86_64) -> VST3, AU, AUv3
#
#   AU and AUv3 are Apple-only formats. JUCE compiles them to nothing on
#   Windows, so the FORMATS list below is safe to share between platforms.
#
# ==============================================================================

cmake_minimum_required(VERSION 3.22)

project(first
    VERSION 1.0.0
    DESCRIPTION "Analog Saturator"
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
#  Plugin target metadata
#
#  These values are the ones the Projucer project carried, preserved exactly so
#  that a plugin built with CMake is seen by hosts and by existing saved
#  sessions as the same plugin as before:
#
#    ManufacturerCode 0x4d616e75  ("Manu" - see the note below)
#    PluginCode       0x5a793539  ("Zy59")
#    Bundle ID        com.MCsmes.first
#    Product name     Analog Saturator
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
    PRODUCT_NAME                "Analog Saturator"
    DESCRIPTION                 "Tape machine saturation and J37-style coloration"
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
#  Source/PluginProcessor.cpp and PluginEditor.cpp are the plugin. Main.cpp and
#  MainComponent.cpp are the standalone-application entry point and are NOT
#  part of any plugin target: JUCE supplies the plugin entry points itself.
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
# ------------------------------------------------------------------------------
target_link_libraries(first
    PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_opengl
        juce::juce_box2d
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
    endif()
endif()

# ------------------------------------------------------------------------------
#  Summary
# ------------------------------------------------------------------------------
message(STATUS "")
message(STATUS "Analog Saturator (first) ${PROJECT_VERSION}")
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
message(STATUS "")
message(STATUS "Artifacts land in: ${CMAKE_BINARY_DIR}")
message(STATUS "To install into the user plugin folders, configure with")
message(STATUS "  -DCOPY_PLUGIN_AFTER_BUILD=TRUE")
message(STATUS "")