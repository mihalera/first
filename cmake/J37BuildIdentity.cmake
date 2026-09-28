# ==============================================================================
#
#   J37 build identity.
#
#   The editor prints the commit it was built from, so a bug report can name an
#   exact build instead of "the latest one". The hash is read once, here, and
#   handed to the sources as J37_BUILD_COMMIT.
#
#   Two rules keep this from ever breaking a build:
#     1. If the tree is not a git checkout (a tarball, a vendored copy, an
#        export) the build still succeeds and simply reports "unknown".
#     2. A dirty working tree gets a trailing "+", so a local build with
#        uncommitted edits can never be mistaken for a clean one.
#
#   .git/HEAD is registered as a configure dependency so that switching branches
#   or pulling a new commit re-runs CMake and refreshes the string; a CI
#   checkout is detached at the exact SHA, so its .git/HEAD changes with every
#   build anyway. A purely local build that only advances HEAD while staying on
#   the same branch needs an explicit re-configure, which is why the summary
#   line in the top-level file prints the value.
#
#   Included by the top-level CMakeLists.txt AFTER the plugin target exists, so
#   the definition can be attached to it directly.
#
# ==============================================================================

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

        # --untracked-files=no matters: CI configures into build/ INSIDE the
        # source tree, so a plain --porcelain would list that directory on every
        # single CI build and stamp every released binary as dirty. Only a
        # modification to a TRACKED file means the binary does not match its
        # commit; untracked build output never does.
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

# .git/HEAD is a file in a normal checkout and a path in a worktree; only
# register it when it actually exists, so CMAKE_CONFIGURE_DEPENDS never points
# at a missing file (which would make every configure fail).
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git/HEAD")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 "${CMAKE_CURRENT_SOURCE_DIR}/.git/HEAD")
endif()

# The function takes the target so this file does not have to know its name.
function(j37_apply_build_identity target)
    target_compile_definitions(${target} PRIVATE J37_BUILD_COMMIT="${J37_BUILD_COMMIT}")
endfunction()