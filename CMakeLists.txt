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
#  The cmake/ modules this file includes. On the module path so include() names
#  files without a relative path, and so the set is one line to read.
#
#  Each subject that used to be a stretch of this one file lives in cmake/, as
#  its own file with its own comments:
#
#    cmake/J37Dependencies.cmake       every third-party fetch (CPM), and the
#                                      CPM configuration itself
#    cmake/J37VendorLibraries.cmake    Intel IPP / Apple vDSP discovery + wiring
#    cmake/J37BuildIdentity.cmake      the git hash printed by the editor
#    cmake/J37ThirdPartyWarnings.cmake warning suppression for vendored code
#    cmake/J37Tests.cmake              dev-only Catch2 / nanobench, opt-in
#    cmake/J37Platform.cmake           the Apple and Windows specifics
# ------------------------------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ------------------------------------------------------------------------------
#  JUCE
#
#  JUCE is a git submodule pinned at 9.0.2, checked out at JUCE/ next to this
#  file. Adding it as a subdirectory is all that is needed: there are no module
#  paths to configure and therefore none to get wrong per-platform, which was
#  the source of the absolute-path problems the Projucer setup had.
#
#  This comes BEFORE the dependency file, because chowdsp_utils and
#  foleys_gui_magic are JUCE modules and need JUCE on the CMake path.
# ------------------------------------------------------------------------------
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE submodule not found at ${CMAKE_CURRENT_SOURCE_DIR}/JUCE.\n"
        "Run: git submodule update --init --recursive")
endif()

add_subdirectory(JUCE)

# ------------------------------------------------------------------------------
#  Third-party libraries (CPM). See cmake/J37Dependencies.cmake.
# ------------------------------------------------------------------------------
include(J37Dependencies)

# ------------------------------------------------------------------------------
#  Vendor vector libraries (Intel IPP / Apple vDSP). Discovery happens here;
#  wiring to the target happens after the target exists, via
#  j37_apply_vendor_libraries(first) below.
# ------------------------------------------------------------------------------
include(J37VendorLibraries)

# ------------------------------------------------------------------------------
#  Dev-only test libraries. Off by default; see cmake/J37Tests.cmake.
# ------------------------------------------------------------------------------
include(J37Tests)

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
#  Third-party warning suppression. See cmake/J37ThirdPartyWarnings.cmake.
# ------------------------------------------------------------------------------
include(J37ThirdPartyWarnings)
j37_apply_thirdparty_warnings(first)

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
#  listed because foleys_gui_magic DECLARES them as module dependencies in its
#  own header, and juce_add_modules resolves a module's dependencies from the
#  linking target's module list - leaving them out fails the configure with a
#  missing module error rather than at link time.
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
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
#  Vendor vector libraries: wire what was found by cmake/J37VendorLibraries.cmake
#  to the target. Both are additive accelerators; see that file for the detail.
# ------------------------------------------------------------------------------
j37_apply_vendor_libraries(first)

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

# ------------------------------------------------------------------------------
#  Build identity. See cmake/J37BuildIdentity.cmake.
# ------------------------------------------------------------------------------
include(J37BuildIdentity)
j37_apply_build_identity(first)

# ------------------------------------------------------------------------------
#  Platform specifics. See cmake/J37Platform.cmake.
# ------------------------------------------------------------------------------
include(J37Platform)
j37_apply_macos_settings(first)
j37_apply_windows_settings(first)

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
