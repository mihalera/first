# ==============================================================================
#
#   The CMake test harnesses.
#
#   Two executables that link the shipping plugin: `first` carries the
#   processor, and these drive its public host entry points - processBlock at
#   zero-, one- and prime-sized blocks, and one rendered-audio assertion per
#   parameter. They are described here rather than in the root CMakeLists
#   because they are built in a build of their own:
#
#     cmake -S . -B build-tests -DJ37_BUILD_TESTS=ON
#     cmake --build build-tests --target j37_processor_boundary_tests \
#                                 j37_parameter_audio_tests
#     ctest --test-dir build-tests --output-on-failure
#
#   A plugin build (build/, build-rel/, the cpp23 and cpp26 builds) never sets
#   J37_BUILD_TESTS, so it neither configures this directory nor builds these
#   targets: the plugin's build tree holds the plugin and nothing else.
#
#   Reached through add_subdirectory(tests) inside if(J37_BUILD_TESTS) in the
#   root. `enable_testing()` stays in the root, because CTest looks for its test
#   file in the build directory's top level.
#
# ==============================================================================

#  The ROOT binary directory, not this one: JUCE's generated header and the
#  generated factory-preset index are written there, and both are on the include
#  surface these harnesses need.
set(J37_TESTS_BINARY_DIR "${CMAKE_BINARY_DIR}")

get_target_property(J37_GENERATED_SOURCES_DIR first JUCE_GENERATED_SOURCES_DIRECTORY)

# ==============================================================================
#
#   LTO off for this build tree.
#
#   These two binaries exist to run assertions; they are never shipped, and the
#   plugin they link is built here only because it is their dependency. Link-time
#   code generation re-optimises the whole program at every link, and in CI that
#   is minutes spent on code nobody listens to: the harness link was the single
#   longest step of the job that builds it.
#
#   The flags arrive through the INTERFACE of juce_recommended_lto_flags as
#   generator expressions (see cmake/J37Platform.cmake, which clears it for
#   -DJ37_FAST_CI_BUILD=ON), so filtering these targets' own options finds
#   nothing; clearing that interface is what takes effect. This is scoped to this
#   directory: a plugin build never configures it, and a session that sets
#   J37_FAST_CI_BUILD=ON has already cleared the same interface for the whole
#   tree, which leaves this a no-op.
#
#   It cannot change what the tests assert. LTO is an optimisation pass, not a
#   semantic switch: the harnesses read rendered audio through tolerances orders
#   of magnitude wider than any instruction-selection difference, and CI already
#   builds the plugin itself without LTO.
#
# ==============================================================================
if(TARGET juce_recommended_lto_flags)
    set_property(TARGET juce_recommended_lto_flags PROPERTY
                 INTERFACE_COMPILE_OPTIONS "")
    set_property(TARGET juce_recommended_lto_flags PROPERTY
                 INTERFACE_LINK_LIBRARIES "")
    message(STATUS "Test harnesses: LTO disabled (assertions, not a shipped build)")
else()
    message(STATUS "Test harnesses: LTO disabled (juce_recommended_lto_flags absent)")
endif()

#  One description for both binaries: they link the same target and need the
#  same include surface, so the reason for each line is written once.
function(j37_add_harness_target target)
    add_executable(${target} ${ARGN})

    #  JUCE's generated plugin definitions include category strings with `|`; on
    #  Windows clang's module scanner passes those through cmd.exe as a pipe.
    #  These targets have no C++ modules, so do not invoke that scanner for them.
    set_property(TARGET ${target} PROPERTY CXX_SCAN_FOR_MODULES OFF)

    target_compile_definitions(${target} PRIVATE
        JucePlugin_Name="MiBoX"
        JucePlugin_IsSynth=0
        JucePlugin_IsMidiEffect=0
        JucePlugin_PreferredChannelConfigurations=0)

    #  PluginProcessor.h includes <JuceHeader.h> and the plugin's own DSP headers,
    #  so this target has to compile against the same include surface the plugin
    #  does: every JUCE module, the generated binary data, and the third-party
    #  headers between them. `first` already carries that whole list, so it is
    #  asked for the list rather than restated - and rather than linked, because a
    #  JUCE module keeps its sources on the same interface as its includes, so
    #  linking one here would compile every module a second time.
    target_include_directories(${target} PRIVATE
        "${CMAKE_SOURCE_DIR}/Source"
        "${J37_GENERATED_SOURCES_DIR}"
        "${J37_TESTS_BINARY_DIR}/generated"
        $<TARGET_PROPERTY:first,INCLUDE_DIRECTORIES>)

    target_link_libraries(${target} PRIVATE
        first
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
endfunction()

#  The processor boundary harness: the real processor called through its public
#  host entry points, without opening an editor or an audio device.
j37_add_harness_target(j37_processor_boundary_tests processor_boundaries.cpp)
add_test(NAME processor_boundaries COMMAND j37_processor_boundary_tests)

#  The parameter audio harness: one unique rendered-audio test per parameter,
#  driven through the real processor's processBlock the way a host calls it.
j37_add_harness_target(j37_parameter_audio_tests parameter_audio.cpp)
add_test(NAME parameter_audio COMMAND j37_parameter_audio_tests)

#  The factory-preset audit harness: the same structural checks the DEBUG
#  constructor audit runs, executed against the shipping plugin in every CI
#  build rather than only where DEBUG compiles them in.
j37_add_harness_target(j37_preset_audit_tests preset_audit.cpp)
add_test(NAME preset_audit COMMAND j37_preset_audit_tests)
