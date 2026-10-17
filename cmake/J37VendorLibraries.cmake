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
#            integration is the system framework on the link line.
#
#            This file used to define J37_HAVE_VDSP as well, "under which the
#            DSP takes its vDSP path". No file under Source/ has ever read that
#            macro, and the sentence named the wrong accelerator: the vDSP that
#            runs is JUCE's. juce_dsp declares OSXFrameworks: Accelerate, so
#            linking juce::juce_dsp brings the framework, and juce_dsp.cpp
#            compiles its vDSP FFT under JUCE_USE_VDSP_FRAMEWORK, which
#            defaults to 1. On Apple the acceleration needs no switch from here;
#            what is left for this file is the CHECK - the framework is looked
#            for, the configure log says whether it was found, and an Apple
#            machine without it is a loud failure rather than a silent slide
#            onto the portable path.
#
#    IPP   - Intel's oneAPI primitives, installed on the developer's machine
#            under a licence this project cannot vendor into its own configure,
#            so it is DISCOVERED rather than shipped. When the SDK is present,
#            find_package(ipp) hands over its imported targets; when it is not,
#            the build carries on without it and nothing below runs.
#            J37_ENABLE_IPP=OFF opts out for a machine that has the SDK but does
#            not want it.
#
#            The switch that makes IPP DO anything is not a "use IPP" flag, and
#            the one the internet names does not exist: JUCE 9.0.4 has no
#            JUCE_DSP_USE_INTEL_IPP anywhere in its tree (its only such flag is
#            JUCE_DSP_USE_INTEL_MKL, which is MKL). What juce_dsp.cpp and
#            juce_FFT.cpp gate their Intel Performance Primitives FFT engine on
#            is the IPP LINKING MODEL:
#
#                #if _IPP_SEQUENTIAL_STATIC || _IPP_SEQUENTIAL_DYNAMIC \
#                 || _IPP_PARALLEL_STATIC   || _IPP_PARALLEL_DYNAMIC
#
#            Define exactly one of those four and juce::dsp::FFT - the engine
#            under juce::dsp::Convolution, which the IR cabinet runs - is
#            Intel's, at priority 9, above MKL and above JUCE's own. Leave them
#            undefined and an IPP install changes nothing, however much of it is
#            on the link line. IPP's own headers are done with the four macros
#            on GCC and Clang (ippdefs.h says so in a #pragma message); JUCE is
#            the reader that still cares, and it is the reason this file throws
#            one.
#
#            Two corrections came out of verifying that: the probe looked for
#            the directory holding ipp.h, which is not the directory holding the
#            headers JUCE includes (<ippcore.h>, <ipps.h> sit one level down, in
#            include/ipp, in Intel's own wheels), and the link line was missing
#            the vector-math library JUCE's FFT init goes through. Between them,
#            IPP could not have built JUCE's engine even with the macro thrown.
#
#            Nothing under Source/ branches on either library. The plugin's own
#            FFT is pocketfft (Source/DSP/SpectrumAnalyser.h), so a build with
#            IPP accelerates JUCE's FFT and not this repository's.
#
#   Both are therefore ADDITIVE: they accelerate kernels that are also compiled
#   in portable form, so the engine keeps working identically on a platform that
#   has neither - which is precisely what chowdsp::chowdsp_simd already does
#   with its own accelerator backends, and what the portable FFTs (KISS FFT /
#   PocketFFT / PFFFT) are here for.
#
#   This file does three things, in this order:
#     1. discovers the libraries and sets J37_HAVE_VDSP / J37_HAVE_IPP;
#     2. decides J37_IPP_LINKING_MODEL - the switch JUCE reads - for a layout
#        whose whole recipe is known, and says why in the configure log when it
#        cannot;
#     3. wires what it found to the plugin target, through the function below.
#   The third step is a function rather than a bare block so the top-level
#   file only has to say j37_apply_vendor_libraries(first) and not know which
#   macro goes with which target.
#
#   HOW THE IPP LAYOUT WAS VERIFIED, and how to re-verify it
#   --------------------------------------------------------
#   On Linux with GCC, against Intel's own wheels - the same ones the CI jobs
#   install - because the oneAPI GUI installer cannot put itself on a runner:
#
#       python3 -m pip download ipp-static ipp-include --no-deps -d /tmp/ipp
#       # unzip both into one prefix: include/ipp/{ipps.h,ippcore.h,...} and
#       # lib/libipps.a, lib/libippcore.a, lib/libippvm.a. (pip does the same
#       # by merging each wheel's .data/data over the environment prefix.)
#
#   A translation unit that defines the model macro, includes the two headers
#   JUCE includes, and calls the sequence JUCE's engine calls -
#   ippsFFTGetSize_C_32fc, ippsFFTInit_C_32fc, ippsFFTFwd_CToC_32fc,
#   ippsFFTInv_CToC_32fc, ippsFFTGetSize_R_32f, ippsFFTInit_R_32f,
#   ippsFFTFwd_RToCCS_32f_I, ippsFFTInv_CCSToR_32f_I, ippsMalloc_8u, ippsFree -
#   compiles and links against
#
#       -I<prefix>/include/ipp -L<prefix>/lib -lipps -lippcore -lippvm
#
#   and a 32-point complex and a 32-point real forward/inverse pair round-trip
#   to 1.2e-7. WITHOUT ippvm the same link fails with
#       undefined reference to `y8_ippsSin_32f_A24'
#   because ippsFFTInit_* builds its twiddle tables through ippsSin/ippsCos, and
#   those dispatchers live in the vector-math library. That failure is the
#   reason J37_IPP_VM_LIBRARY exists.
#
#   The layout left unwired is Intel's Windows wheel, which ships the mt
#   libraries only (ippsmt.lib, ippcoremt.lib, ippvmmt.lib) and no
#   libiomp5md.lib, while MSVC's own headers answer _IPP_PARALLEL_STATIC with
#   #pragma comment (lib, "libiomp5md"). That has to be settled by someone with
#   a Windows linker in front of them rather than guessed at from Linux; until
#   then the switch is thrown only where the recipe above holds.
#
# ==============================================================================

option(J37_ENABLE_IPP "Use Intel IPP when the oneAPI SDK is found on this machine" ON)

set(J37_HAVE_VDSP OFF)
set(J37_HAVE_IPP OFF)
set(J37_IPP_HOW "")
set(J37_IPP_LINKING_MODEL "")
set(J37_IPP_UNWIRED "")
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
        #   The headers land in <prefix>/include/ipp/ and an aggregator at
        #   <prefix>/include/ipp.h; the manylinux wheel puts libipps.a,
        #   libippcore.a and libippvm.a in <prefix>/lib and the win_amd64 wheel
        #   puts ippsmt.lib, ippcoremt.lib and ippvmmt.lib in
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
        # The mt suffix is the multithreaded variant, which is what both Windows
        # distributions ship; the plain names are the single-threaded ones. Both
        # are tried so neither layout has to be special-cased at the call site.
        unset(J37_IPP_INCLUDE_DIR CACHE)
        unset(J37_IPP_IPPS_LIBRARY CACHE)
        unset(J37_IPP_CORE_LIBRARY CACHE)
        unset(J37_IPP_VM_LIBRARY CACHE)

        # ipps.h and ippcore.h are the two headers JUCE's IPP engine includes,
        # so the directory holding THEM is the include directory that matters -
        # not the one holding ipp.h, which is what this probe used to settle
        # for. In Intel's wheels <prefix>/include/ipp.h is a 940-byte aggregator
        # whose whole body is #include <ipp/ipp.h>, and the real headers are
        # beside it in <prefix>/include/ipp/; handing CMake the aggregator's
        # directory gave JUCE nothing to include. PATH_SUFFIXES tries the nested
        # layout first on every platform, and the existence check below refuses
        # a directory that cannot satisfy JUCE rather than accepting one that
        # merely contains a header of Intel's.
        find_path(J37_IPP_INCLUDE_DIR
            NAMES ipps.h ippcore.h ipp.h
            HINTS ${j37_ipp_roots}
            PATH_SUFFIXES include/ipp include build/native/include/ipp build/native/include)

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

        # The vector-math library. Nothing the plugin's own code calls needs it,
        # and IPP links without it, but JUCE's IPP FFT engine does the moment it
        # is compiled in: ippsFFTInit_* builds its twiddle tables through
        # ippsSin/ippsCos, whose objects live here. Linking without it reports
        # undefined `y8_ippsSin_32f_A24' and the like - see the verification
        # block at the top of this file for the command and the exact failure.
        find_library(J37_IPP_VM_LIBRARY
            NAMES ippvm ippvmmt
            HINTS ${j37_ipp_roots}
            PATH_SUFFIXES lib lib/intel64 build/native/win-x64 Library/lib)

        if(J37_IPP_INCLUDE_DIR AND J37_IPP_IPPS_LIBRARY AND J37_IPP_CORE_LIBRARY
           AND EXISTS "${J37_IPP_INCLUDE_DIR}/ipps.h"
           AND EXISTS "${J37_IPP_INCLUDE_DIR}/ippcore.h")
            add_library(IPP::ipps UNKNOWN IMPORTED)
            set_target_properties(IPP::ipps PROPERTIES
                IMPORTED_LOCATION "${J37_IPP_IPPS_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${J37_IPP_INCLUDE_DIR}")

            add_library(IPP::ippcore UNKNOWN IMPORTED)
            set_target_properties(IPP::ippcore PROPERTIES
                IMPORTED_LOCATION "${J37_IPP_CORE_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${J37_IPP_INCLUDE_DIR}")

            if(J37_IPP_VM_LIBRARY)
                add_library(IPP::ippvm UNKNOWN IMPORTED)
                set_target_properties(IPP::ippvm PROPERTIES
                    IMPORTED_LOCATION "${J37_IPP_VM_LIBRARY}"
                    INTERFACE_INCLUDE_DIRECTORIES "${J37_IPP_INCLUDE_DIR}")
            endif()

            # ippi (imaging) is deliberately NOT searched: a tape saturator has
            # no imaging in it, and the NuGet static package spells it ippimt.lib
            # for no benefit here.
            set(J37_HAVE_IPP ON)
            set(J37_IPP_HOW "headers + libraries found directly")

            # Which of the four linking-model macros this layout answers to, and
            # whether this file is entitled to throw it. The name of the library
            # that was actually found decides both: mt is Intel's multithreaded
            # variant (JUCE's PARALLEL names), and a .so/.dylib is the dynamic
            # model rather than the static one.
            get_filename_component(J37_IPP_LIBRARY_NAME "${J37_IPP_IPPS_LIBRARY}" NAME)

            if(J37_IPP_LIBRARY_NAME MATCHES "\\.(so|dylib)(\\.[0-9]+)*$")
                set(j37_ipp_model _IPP_SEQUENTIAL_DYNAMIC)
                if(J37_IPP_LIBRARY_NAME MATCHES "mt\\.")
                    set(j37_ipp_model _IPP_PARALLEL_DYNAMIC)
                endif()
            else()
                set(j37_ipp_model _IPP_SEQUENTIAL_STATIC)
                if(J37_IPP_LIBRARY_NAME MATCHES "mt\\.")
                    set(j37_ipp_model _IPP_PARALLEL_STATIC)
                endif()
            endif()

            # Only the single-threaded STATIC layout has a verified recipe (the
            # one at the top of this file): its libraries are the .a files this
            # was measured on. The other three are named here and left alone,
            # with the reason printed rather than a switch thrown on a guess.
            if(j37_ipp_model STREQUAL "_IPP_SEQUENTIAL_STATIC")
                set(J37_IPP_LINKING_MODEL "${j37_ipp_model}")
            else()
                if(J37_IPP_LIBRARY_NAME MATCHES "mt\\.")
                    set(J37_IPP_UNWIRED
                        "the multithreaded libraries Intel's Windows wheels ship; MSVC's own headers answer ${j37_ipp_model} with a libiomp5md it does not ship, which needs a Windows linker to settle")
                else()
                    set(J37_IPP_UNWIRED
                        "a dynamic ${j37_ipp_model} layout, whose recipe has not been measured; only the static single-threaded one has")
                endif()
            endif()
        elseif(J37_IPP_INCLUDE_DIR AND J37_IPP_IPPS_LIBRARY AND J37_IPP_CORE_LIBRARY)
            # A prefix WAS found and it cannot serve JUCE: no ipps.h/ippcore.h
            # where its headers are, which are the two headers JUCE's IPP FFT
            # engine includes. Saying so beats quietly compiling the portable
            # path - the same argument the vDSP block above makes about a damaged
            # Accelerate framework, and the state this probe was in for as long
            # as it looked one directory too high.
            message(WARNING
                "Intel IPP was found but not in a layout this build can use: "
                "${J37_IPP_INCLUDE_DIR} holds no ipps.h/ippcore.h, and Intel's wheels keep both one level down, "
                "in include/ipp. Building with the portable FFTs; no IPP library is linked.")
        endif()
    endif()
endif()

# ------------------------------------------------------------------------------
#  Wiring what was found to the plugin target.
#
#  Both are PRIVATE: an accelerator's link line and compile definitions are none
#  of a consumer of the target's business.
#
#  What each one accelerates is worth stating precisely, because an earlier
#  version of this comment said "this plugin's own DSP" about both and that is
#  not what happens. The framework and the library are JUCE's business: IPP is
#  wired by throwing the macro juce_dsp's own FFT engine is gated on, and vDSP
#  needs no wiring at all because juce_dsp already declares and uses it. Nothing
#  under Source/ takes either path - the plugin's own FFT is pocketfft.
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
        #
        # No macro goes with it. This block used to also define J37_HAVE_VDSP=1
        # for the plugin's own code, which read it nowhere; the vDSP path that
        # exists on an Apple build is JUCE's, inside juce_dsp, and it is on by
        # default there. A macro nothing tests is not a feature, it is a
        # statement about the build that nobody checks - so the statement is now
        # the configure log's, not the compiler's.
        target_link_libraries(${target} PRIVATE "${J37_ACCELERATE_FRAMEWORK}")
    endif()

    if(J37_HAVE_IPP)
        # ippvm is in this list because JUCE's FFT init needs it, ippi because
        # Intel's own config package may define it - both are checked by TARGET
        # so a missing one skips itself instead of failing the link.
        foreach(j37_ipp_target IPP::ipps IPP::ippvm IPP::ippi IPP::ippcore)
            if(TARGET ${j37_ipp_target})
                target_link_libraries(${target} PRIVATE ${j37_ipp_target})
            endif()
        endforeach()

        # The ONE thing that makes IPP accelerate anything here: the linking
        # model macro JUCE's own FFT engine is gated on. Defining it makes
        # juce::dsp::FFT - and so the Convolution behind the IR cabinet - Intel's
        # engine rather than JUCE's, which is the whole point of linking the
        # library in the first place.
        #
        # This replaces J37_HAVE_IPP=1, which was defined here and read by no
        # file under Source/ in any build since it was written.
        if(J37_IPP_LINKING_MODEL)
            target_compile_definitions(${target} PRIVATE ${J37_IPP_LINKING_MODEL})
            message(STATUS "Intel IPP: JUCE's FFT engine will be built for ${J37_IPP_LINKING_MODEL}")
        else()
            message(STATUS "Intel IPP: linked, but JUCE's IPP FFT engine is NOT wired - ${J37_IPP_UNWIRED}")
        endif()
    endif()
endfunction()