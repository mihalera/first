# ==============================================================================
#
#   J37 third-party warning suppression.
#
#   foleys_gui_magic is a JUCE MODULE, not a separate library, so its 55
#   translation units are compiled as part of the plugin and its warnings land
#   in this project's build log on every platform. The SYSTEM YES on its
#   CPMAddPackage covers its HEADERS, not its .cpp files. What it produces is
#   eleven warnings on Linux and rather more on AppleClang: unused locals,
#   shadowed members, and implicit int-to-float conversions in code this project
#   does not own and does not patch.
#
#   The suppression is attached PER SOURCE FILE, never to the target, so a new
#   warning of these kinds in the plugin's own Source/*.cpp is still reported
#   and still worth fixing. A glob that matches nothing is a no-op, so a CPM
#   path change degrades to "no suppression" rather than to a configure error.
#   MSVC is skipped entirely: it spells the same warnings as /wdNNNN numbers,
#   not -W flags.
#
#   A separate file because this is build hygiene, not build behaviour: nothing
#   here changes what is compiled or how it behaves, and keeping it out of the
#   top-level file keeps that file about the plugin.
#
# ==============================================================================

function(j37_apply_thirdparty_warnings target)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        return()
    endif()

    set(j37_thirdparty_warnings -Wno-unused-variable -Wno-unused-but-set-variable
                               -Wno-unused-function -Wno-missing-prototypes -Wno-shadow)

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # -Wshorten-64-to-32 and -Wimplicit-const-int-float-conversion are
        # Clang-only spellings; GCC rejects them. -Wimplicit-const-int-float-
        # conversion is not on by default in Clang, so silencing it costs this
        # project nothing.
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

    # The per-source flags above are not enough on their own, because most of
    # these headers are parsed as part of THIS project's translation units:
    # PluginEditor.h includes <foleys_gui_magic/foleys_gui_magic.h> behind a
    # __has_include guard, and PluginProcessor.h pulls in chowdsp, so their
    # headers are diagnosed while compiling Source/*.cpp. Marking those include
    # directories SYSTEM is what actually silences them: a diagnostic raised
    # INSIDE a system header is dropped, while a diagnostic in our own code is
    # still reported. That is strictly more precise than widening the -Wno-
    # list for our own sources, which is why the lists above stay as they are.
    file(GLOB j37_thirdparty_include_dirs CONFIGURE_DEPENDS
         "${CMAKE_BINARY_DIR}/deps/foleys_gui_magic/modules"
         "${CMAKE_SOURCE_DIR}/.cpm-cache/chowdsp_utils/*/modules/*")

    foreach(j37_include_dir IN LISTS j37_thirdparty_include_dirs)
        if(IS_DIRECTORY "${j37_include_dir}")
            target_include_directories(${target} SYSTEM PRIVATE "${j37_include_dir}")
        endif()
    endforeach()
endfunction()

# ------------------------------------------------------------------------------
#  What is left, and why it stays
#
#  On macOS this build now prints sixteen warnings, all of them
#  -Wimplicit-const-int-float-conversion from JUCE's own
#  juce_FastMathApproximations.h, raised while building
#  `juce_vst3_manifest_helper`. That is a NESTED CMake project JUCE generates
#  and configures on its own during the build, so it inherits neither this
#  project's CMAKE_CXX_FLAGS nor any per-target or per-source option set here -
#  every way of passing the flag from this project was tried and none of them
#  reached it. Silencing it would mean patching JUCE's generated helper, so the
#  sixteen are left standing and documented instead. Nothing in Source/*.cpp or
#  in the plugin's own targets warns on any platform.
# ------------------------------------------------------------------------------