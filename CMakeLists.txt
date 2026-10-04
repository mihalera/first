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
#     Android : NDK (arm64-v8a)                    -> Standalone (the only format
#                                                    JUCE's CMake layer produces there)
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

# Android: everything that ends up inside the Standalone wrapper's shared
# library - the JUCE modules, the Oboe runtime they bundle, and every CPM
# static library on the link line - must be position-independent. A shared
# object cannot reference a non-PIC static archive, and the link fails with
# "relocation R_AARCH64_... cannot be used against symbol ... recompile with
# -fPIC". Set before add_subdirectory(JUCE) so every target in the build
# inherits it. Desktop builds are untouched.
if(CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
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

# Renderer selection is compile-time because a plugin editor must create its
# graphics device on the host's UI thread. OpenGL is the complete backend today;
# the other values select an honest CPU fallback until their native integrations
# are enabled. This keeps the build matrix ready without pretending that a
# Vulkan/Metal/D3D device can be created by JUCE's OpenGL module.
set(J37_RENDER_BACKEND "AUTO" CACHE STRING
    "Editor renderer: AUTO, OPENGL, VULKAN, METAL, D3D11, D3D12 or CPU")
set_property(CACHE J37_RENDER_BACKEND PROPERTY STRINGS
    AUTO OPENGL VULKAN METAL D3D11 D3D12 CPU)
string(TOUPPER "${J37_RENDER_BACKEND}" J37_RENDER_BACKEND)
set(J37_RENDER_DEFINITIONS)
set(J37_NATIVE_RENDER_DEFINITIONS)
if(J37_RENDER_BACKEND STREQUAL "OPENGL")
    list(APPEND J37_RENDER_DEFINITIONS J37_RENDER_BACKEND_OPENGL=1)
elseif(J37_RENDER_BACKEND STREQUAL "VULKAN")
    list(APPEND J37_RENDER_DEFINITIONS J37_RENDER_BACKEND_VULKAN=1)
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        list(APPEND J37_NATIVE_RENDER_DEFINITIONS J37_NATIVE_VULKAN=1)
    endif()
elseif(J37_RENDER_BACKEND STREQUAL "METAL")
    list(APPEND J37_RENDER_DEFINITIONS J37_RENDER_BACKEND_METAL=1)
    if(APPLE)
        list(APPEND J37_NATIVE_RENDER_DEFINITIONS J37_NATIVE_METAL=1)
    endif()
elseif(J37_RENDER_BACKEND STREQUAL "D3D11")
    list(APPEND J37_RENDER_DEFINITIONS J37_RENDER_BACKEND_D3D11=1)
elseif(J37_RENDER_BACKEND STREQUAL "D3D12")
    list(APPEND J37_RENDER_DEFINITIONS J37_RENDER_BACKEND_D3D12=1)
    if(WIN32)
        list(APPEND J37_NATIVE_RENDER_DEFINITIONS J37_NATIVE_D3D12=1)
    endif()
elseif(J37_RENDER_BACKEND STREQUAL "CPU")
    list(APPEND J37_RENDER_DEFINITIONS J37_RENDER_BACKEND_CPU=1)
elseif(NOT J37_RENDER_BACKEND STREQUAL "AUTO")
    message(FATAL_ERROR "Unknown J37_RENDER_BACKEND='${J37_RENDER_BACKEND}'")
endif()

# Keep platform mistakes visible at configure time. These checks do not claim
# that the native backend is implemented; they only prevent a Metal build on
# Windows or a D3D build on macOS from looking like a supported configuration.
if(J37_RENDER_BACKEND STREQUAL "METAL" AND NOT APPLE)
    message(WARNING "J37_RENDER_BACKEND=METAL requested on a non-Apple platform; CPU fallback will be used")
elseif((J37_RENDER_BACKEND STREQUAL "D3D11" OR J37_RENDER_BACKEND STREQUAL "D3D12") AND NOT WIN32)
    message(WARNING "${J37_RENDER_BACKEND} requested on a non-Windows platform; CPU fallback will be used")
elseif(J37_RENDER_BACKEND STREQUAL "VULKAN")
    find_package(Vulkan QUIET)
    if(NOT Vulkan_FOUND)
        message(WARNING "Vulkan SDK not found; CPU fallback will be used")
    endif()
endif()

# The bundle id lives in a variable rather than inline in the juce_add_plugin call
# because the AUv3 app extension needs it a second time, with ".internal" appended (see
# the Apple block below). Written twice, the two copies drift and Xcode starts warning
# that the Info.plist and the PRODUCT_BUNDLE_IDENTIFIER build setting disagree.
set(J37_BUNDLE_ID "com.MCsmes.first")

# The format list is platform-conditional for one reason: JUCE's platform kind
# list reduces to Standalone alone on Android, and a FORMATS entry JUCE cannot
# produce there is simply intersected away - leaving an empty active-format set
# and a build with no wrapper to link. On desktop this stays exactly what it
# always was.
set(J37_PLUGIN_FORMATS AU AUv3 VST3)
if(CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(J37_PLUGIN_FORMATS Standalone)
endif()

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
    FORMATS                     ${J37_PLUGIN_FORMATS}
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
    Source/PluginEditor.cpp
    Source/GUI/TapeScene.cpp
    Source/GUI/NativeRenderer.cpp
    Source/GUI/NativeRenderer_D3D12.cpp
    Source/GUI/NativeRenderer_Vulkan.cpp
    Source/GUI/RenderBackend.h)

target_include_directories(first PRIVATE Source)

if(APPLE)
    target_sources(first PRIVATE Source/GUI/NativeRenderer_Metal.mm)
endif()

if(TARGET Vulkan::Vulkan)
    target_link_libraries(first PRIVATE Vulkan::Vulkan)
endif()
if(WIN32 AND J37_RENDER_BACKEND STREQUAL "D3D12")
    target_link_libraries(first PRIVATE d3d12 dxgi dxguid)
elseif(APPLE AND J37_RENDER_BACKEND STREQUAL "METAL")
    target_link_libraries(first PRIVATE "-framework Metal" "-framework QuartzCore")
endif()

# ------------------------------------------------------------------------------
#  Embedded resources - the factory presets and the translation tables.
#
#  Both used to live in the C++ source: the twenty-six factory presets were a
#  160-field struct plus twenty-six rows of hand-written initialisers (about 290
#  lines of numbers), and every string was a literal. Neither belongs there. A
#  preset is DATA, and data that lives in code can only be changed by a
#  programmer, reviewed as a diff of numbers, and rebuilt to take effect.
#
#  So each preset is a JSON file of its own under Source/Presets/Factory, and
#  each language is a JUCE .txt mapping table under Source/Translations. Both
#  are compiled into the binary, so the factory presets cannot be deleted by the
#  user and the translations need no files beside the plugin.
#
#  The ORDER of the presets is a list, and the list is the only place it is
#  written: FactoryPresetIndex.h is generated from this same variable, so the
#  C++ cannot disagree with the build about which file is preset 3. Adding a
#  preset is one line here and one new file - the loader, the count and the
#  preset box all derive themselves.
# ------------------------------------------------------------------------------
set(FACTORY_PRESET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Source/Presets/Factory")

set(FACTORY_PRESET_FILES
    ${FACTORY_PRESET_DIR}/default.json
    ${FACTORY_PRESET_DIR}/minimum.json
    ${FACTORY_PRESET_DIR}/maximum.json
    ${FACTORY_PRESET_DIR}/gentle_warmth.json
    ${FACTORY_PRESET_DIR}/bus_glue_tape.json
    ${FACTORY_PRESET_DIR}/drum_slam.json
    ${FACTORY_PRESET_DIR}/vintage_lo_fi.json
    ${FACTORY_PRESET_DIR}/wide_master.json
    ${FACTORY_PRESET_DIR}/clean_glue.json
    ${FACTORY_PRESET_DIR}/saturated_crunch.json
    ${FACTORY_PRESET_DIR}/wobbly_cassette.json
    ${FACTORY_PRESET_DIR}/bright_air_tape.json
    ${FACTORY_PRESET_DIR}/mix_saturation.json
    ${FACTORY_PRESET_DIR}/master_bounce.json
    ${FACTORY_PRESET_DIR}/vocal_rail.json
    ${FACTORY_PRESET_DIR}/drum_room_warm.json
    ${FACTORY_PRESET_DIR}/bass_weight.json
    ${FACTORY_PRESET_DIR}/master_safety.json
    ${FACTORY_PRESET_DIR}/lo_fi_radio.json
    ${FACTORY_PRESET_DIR}/ferric_master.json
    ${FACTORY_PRESET_DIR}/british_valve.json
    ${FACTORY_PRESET_DIR}/plexi_bite.json
    ${FACTORY_PRESET_DIR}/iron_bloom.json
    ${FACTORY_PRESET_DIR}/twelve_bit_bounce.json
    ${FACTORY_PRESET_DIR}/dubplate_dub.json
    ${FACTORY_PRESET_DIR}/shellac_radio.json
    ${FACTORY_PRESET_DIR}/transient_punch.json
    ${FACTORY_PRESET_DIR}/soft_touch.json
    ${FACTORY_PRESET_DIR}/tight_bus.json)

# A guard, not a convention: a path that does not exist is a typo, and CMake
# would otherwise pass it to the binary-data builder and fail with a message
# about a generated C++ symbol rather than about the file you meant to name.
foreach(preset_file IN LISTS FACTORY_PRESET_FILES)
    if(NOT EXISTS "${preset_file}")
        message(FATAL_ERROR "Factory preset not found: ${preset_file}")
    endif()
endforeach()

set(TRANSLATION_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Source/Translations")
set(TRANSLATION_FILES
    ${TRANSLATION_DIR}/en.txt
    ${TRANSLATION_DIR}/uk.txt)

foreach(translation_file IN LISTS TRANSLATION_FILES)
    if(NOT EXISTS "${translation_file}")
        message(FATAL_ERROR "Translation table not found: ${translation_file}")
    endif()
endforeach()

juce_add_binary_data(PluginResources
    HEADER_NAME BinaryData.h
    NAMESPACE BinaryData
    SOURCES ${FACTORY_PRESET_FILES} ${TRANSLATION_FILES})

# The index header: the preset list, in order, as data. Written from the same
# CMake variable that feeds juce_add_binary_data, so the two cannot drift.
# juceaide names a resource's symbol after its file name with '.' and ' ' turned
# into '_'; every preset file is named with [a-z0-9_] precisely so that rule is
# the identity here, and the two lines that derive one from the other can be
# read at a glance instead of having to be trusted.
set(FACTORY_PRESET_INDEX "")
foreach(preset_file IN LISTS FACTORY_PRESET_FILES)
    get_filename_component(preset_name "${preset_file}" NAME)
    string(REGEX REPLACE "\\.json$" "" preset_symbol "${preset_name}")
    string(APPEND FACTORY_PRESET_INDEX
           "    { BinaryData::${preset_symbol}_json, BinaryData::${preset_symbol}_jsonSize },\n")
endforeach()

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/FactoryPresetIndex.h"
"// ==============================================================================
//  GENERATED - do not edit.
//
//  Written by CMakeLists.txt from FACTORY_PRESET_FILES, the same list that
//  feeds juce_add_binary_data. The order here is the order of the preset box,
//  the undo label and the badge, all of which index by POSITION - so this file
//  existing at all is the point: there is no second list of preset names in the
//  source left to fall out of step with the files.
//
//  Add a preset: drop the JSON in Source/Presets/Factory and add one line to
//  FACTORY_PRESET_FILES. Nothing else changes.
// ==============================================================================
#pragma once

namespace FactoryPresetIndex
{
    struct Entry { const char* data; int size; };

    // const, NOT constexpr: juce_add_binary_data declares each resource as
    // `extern const char name[]`, and an extern array of unknown size is not a
    // constant expression - so `constexpr` here is a hard compile error, in
    // every translation unit that includes this header, on every platform. The
    // table is still initialised once and lives in the header; it just is not
    // a compile-time constant, which nothing here needs it to be.
    inline const Entry entries[] {
${FACTORY_PRESET_INDEX}    };
}
")

# target_sources on a binary-data target is what adds its generated directory to
# this target's include path and makes the build depend on it being regenerated.
# The generated index is written above, not by juce_add_binary_data, so it needs
# its own include directory. FactoryPresetIndex.h is the only header the plugin
# includes from here.
target_include_directories(first PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

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
        SoundTouch
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
        yin_pitch
        pitch_detector
        audio_fft
        dattorro_verb
        sndfile
        nlohmann_json
        q_dsp
        sokol_gfx
        glm
        PluginResources
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
    ${J37_RENDER_DEFINITIONS}
    ${J37_NATIVE_RENDER_DEFINITIONS}
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
#  Android. See cmake/J37Platform.cmake (j37_apply_android_settings) for the
#  detail; the short version is that CMake cannot host an Android app - Gradle
#  owns the APK, the manifest, the resources and the signing - so CMake builds
#  the plugin's Standalone wrapper as a shared library into a Gradle project
#  skeleton, and Gradle does everything a phone needs around it.
# ------------------------------------------------------------------------------
j37_apply_android_settings(first)

# ------------------------------------------------------------------------------
#  Summary
# ------------------------------------------------------------------------------
message(STATUS "")
message(STATUS "Nonlin Analog Saturator (first) ${PROJECT_VERSION}")
message(STATUS "  JUCE        : ${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
message(STATUS "  System      : ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Generator   : ${CMAKE_GENERATOR}")
message(STATUS "  Build type  : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Renderer    : requested ${J37_RENDER_BACKEND}; native command resources enabled per platform")
message(STATUS "  Build commit: ${J37_BUILD_COMMIT}")
if(APPLE)
    message(STATUS "  Archs       : ${CMAKE_OSX_ARCHITECTURES}")
    message(STATUS "  Formats     : VST3, AU, AUv3")
else()
    message(STATUS "  Formats     : VST3")
endif()
message(STATUS "  Libraries   : chowdsp_utils, melatonin_inspector, melatonin_blur, foleys_gui_magic, xsimd")
message(STATUS "                signalsmith-stretch, SoundTouch (LGPL-2.1), fftconvolver,")
message(STATUS "                imgui, iPlug2 (source only)")
message(STATUS "                chowcentaur, vcv_prototype, rack, soundpipe, aubio (source only)")
message(STATUS "                farbot, readerwriterqueue, kissfft, pocketfft, yasio, dywapitchtrack, pffft")
message(STATUS "                RTNeural (neural inference, used by the DYNAMICS tab), dr_libs,")
message(STATUS "                yin_pitch, pitch_detector (MPM), dattorro_verb, sndfile")
message(STATUS "                nlohmann_json (header only)")
message(STATUS "                q_dsp, sokol_gfx, glm (header only)")
message(STATUS "                transdes, whetstone, multiband-transient-shaper (source only)")
message(STATUS "                juce_opengl_3d: no such JUCE module, see cmake/J37Dependencies.cmake")
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
