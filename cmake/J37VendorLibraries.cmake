# ==============================================================================
#
#   J37 vendor vector libraries: Intel IPP and Apple vDSP.
#
#   Neither is a GitHub repository, so neither can be fetched through CPM, and
#   neither needs to be: each is already present on a machine that has it, and
#   each is integrated here the only way such a library can be.
#
#    vDSP  - part of Apple's Accelerate framework, shipped with the OS on every
#            Apple platform. There is nothing to download, so the whole
#            integration is the system framework on the link line plus
#            J37_HAVE_VDSP, under which the DSP takes its vDSP path. Everywhere
#            else vDSP does not exist, so everywhere else this is a no-op rather
#            than a fallback: that is the same arrangement JUCE itself uses.
#
#    IPP   - Intel's oneAPI primitives, installed on the developer's machine
#            under a licence this project cannot vendor into its own configure,
#            so it is DISCOVERED rather than shipped. When the SDK is present,
#            find_package(ipp) hands over its imported targets; when it is not,
#            the build carries on without it and J37_HAVE_IPP is never defined.
#            That asymmetry is the whole design: a contributor on Windows or
#            Linux has no IPP installed and their build must not need one, while
#            a contributor who has it gets the real library from the same
#            configure, with no extra step. J37_ENABLE_IPP=OFF opts out for a
#            machine that has the SDK but does not want it.
#
#   Both are therefore ADDITIVE: they accelerate kernels that are also compiled
#   in portable form, so the engine keeps working identically on a platform that
#   has neither - which is precisely what chowdsp::chowdsp_simd already does
#   with its own accelerator backends, and what the portable FFTs (KISS FFT /
#   PocketFFT / PFFFT) are here for.
#
#   This file does two things, in this order:
#     1. discovers the libraries and sets J37_HAVE_VDSP / J37_HAVE_IPP;
#     2. wires what it found to the plugin target, through the function below.
#   The second step is a function rather than a bare block so the top-level
#   file only has to say j37_apply_vendor_libraries(first) and not know which
#   macro goes with which target.
#
# ==============================================================================

option(J37_ENABLE_IPP "Use Intel IPP when the oneAPI SDK is found on this machine" ON)

set(J37_HAVE_VDSP OFF)
set(J37_HAVE_IPP OFF)
set(J37_IPP_HOW "")
set(J37_ACCELERATE_FRAMEWORK "")

# ------------------------------------------------------------------------------
#  Apple vDSP (Accelerate).
# ------------------------------------------------------------------------------
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

# ------------------------------------------------------------------------------
#  Intel IPP (oneAPI).
# ------------------------------------------------------------------------------
if(J37_ENABLE_IPP)
    # oneAPI installs into its own prefix and, unless its setvars.sh has been
    # run, nothing in the environment points at it. All three spellings are
    # honoured, so a developer who has installed it gets a build that finds it
    # without passing a single -D:
    #
    #   IPPROOT / IPP_ROOT   Intel's own environment variables
    #   the stock locations   /opt/intel/oneapi/ipp/latest on Linux,
    #                         C:/Program Files (x86)/Intel/oneAPI/ipp/latest on
    #                         Windows (the 2024+ layout)
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
    # name therefore CANNOT find it, and the same targets come back spelled
    # IPP::ipps, IPP::ippi, IPP::ippcore, which is what the link line uses.
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
        # from a command line, oneAPI is not in winget and Chocolatey has
        # nothing:
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
        # config names them, so the link line cannot tell the routes apart.
        # IPPROOT is what names the prefix: it is Intel's own variable for the
        # installed SDK, and the CI step exports it for the wheels too.
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
#  Wiring what was found to the plugin target.
#
#  Both are PRIVATE: they accelerate this plugin's own DSP and are none of a
#  consumer of the target's business.
#
#  The IPP targets are checked individually rather than named blindly, because
#  which of them an installed oneAPI ships varies by version and by the
#  components that were installed with it - a missing optional component must
#  skip itself instead of failing the link. They are IMPORTED by Intel's own
#  config file, so this works with GCC and Clang too, not only with the Intel
#  compiler.
# ------------------------------------------------------------------------------
function(j37_apply_vendor_libraries target)
    if(J37_HAVE_VDSP)
        # The full framework path, which CMake links directly. The alternative
        # spelling is the raw "-framework Accelerate" flag; the path is used
        # because it is the same string that was verified to exist above, so the
        # link and the check can never disagree about what was found.
        target_link_libraries(${target} PRIVATE "${J37_ACCELERATE_FRAMEWORK}")
        target_compile_definitions(${target} PRIVATE J37_HAVE_VDSP=1)
    endif()

    if(J37_HAVE_IPP)
        foreach(j37_ipp_target IPP::ipps IPP::ippi IPP::ippcore)
            if(TARGET ${j37_ipp_target})
                target_link_libraries(${target} PRIVATE ${j37_ipp_target})
            endif()
        endforeach()

        # The macro the DSP code tests. Undefined when the library is absent,
        # which is the point: one set of sources, three answers - vDSP on Apple,
        # IPP where the SDK is installed, the portable kernels everywhere else.
        target_compile_definitions(${target} PRIVATE J37_HAVE_IPP=1)
    endif()
endfunction()