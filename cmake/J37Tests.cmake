# ==============================================================================
#
#   J37 dev-only test libraries.
#
#   Fetched only when J37_BUILD_TESTS is ON, because a plugin build must not
#   drag a test framework into its dependency graph.
#
#    Catch2    - the C++ test framework. The DSP harness in tests/dsp/ compiles
#                the shipping code against a shim and checks RENDERED AUDIO;
#                that is the right tool for a signal-path regression. Catch2 is
#                for the unit-sized pieces underneath it - a single shaper, a
#                single loudness filter, a single compressor detector - where
#                the question is "does this function still return what it
#                returned yesterday", not "does the plugin still sound right".
#
#    nanobench - micro-benchmarks. The reason this is here at all: the shaper
#                runs two std::tanh calls per sample per channel (four with the
#                zero-point correction), and at 8x oversampling that is the hot
#                loop. Approximating tanh or vectorising it is a real
#                complexity cost, and it should only be paid once a benchmark
#                shows the shaper actually dominates.
#
#   Enable with -DJ37_BUILD_TESTS=ON.
#
#   A separate file because this is the one dependency block that is OFF by
#   default and whose whole point is its own switch - keeping it next to the
#   shipping dependencies made it look like one of them.
#
# ==============================================================================

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