# ==============================================================================
#
#   J37 platform specifics.
#
#   Everything that is Apple-only or Windows-only, in one file, so the
#   top-level CMakeLists.txt reads as the build's shape rather than as its
#   platform patchwork.
#
#   Each block is a function taking the target name, so this file never has to
#   know what the plugin target is called; the top-level file calls them once.
#
# ==============================================================================

# ------------------------------------------------------------------------------
#  macOS
# ------------------------------------------------------------------------------
function(j37_apply_macos_settings target)
    if(NOT APPLE)
        return()
    endif()

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

    # JUCE names the plugin targets ${target}_${kind}: the shared code is the
    # bare `first` target (a STATIC library) and each format gets its own suffix
    # - first_VST3, first_AU, first_AUv3. There is no `first_SharedCode` target;
    # the `_SharedCode` suffix only appears in the output file name.
    #
    # Only the AUv3 app extension is sandboxed. A plain AU component is loaded
    # directly by the host process and must NOT carry the app-sandbox
    # entitlement: an Audio Unit that declares com.apple.security.app-sandbox is
    # rejected by the AU validation step and silently disappears from Logic's
    # plug-in list.
    # Xcode warns unless PRODUCT_BUNDLE_IDENTIFIER is byte-for-byte the
    # CFBundleIdentifier already in the target's Info.plist. AUv3 is TWO targets
    # and JUCE gives them two different ids, both derived from the plugin bundle
    # id: the extension itself gets ".firstAUv3" and the helper framework it
    # embeds gets ".internal". Setting only one just trades one warning for
    # another, so both are set here, from the same J37_BUNDLE_ID the Info.plists
    # were generated from.
    if(TARGET ${target}_AUv3)
        set_target_properties(${target}_AUv3 PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS
            "${appex_support_target}/AUv3_AppExtension.entitlements"
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER
            "${J37_BUNDLE_ID}.firstAUv3")
    endif()

    if(TARGET ${target}_AUv3_Framework)
        set_target_properties(${target}_AUv3_Framework PROPERTIES
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER
            "${J37_BUNDLE_ID}.internal")
    endif()
endfunction()

# ------------------------------------------------------------------------------
#  Windows
# ------------------------------------------------------------------------------
function(j37_apply_windows_settings target)
    if(NOT WIN32)
        return()
    endif()

    # The plugin sources use standard library calls that MSVC flags as unsafe
    # in its default mode. The Projucer project set this too.
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)

    # Warning level and conformance only. The optimisation flags that used to be
    # here (/O2 /Ob3 /GL /LTCG) have been removed: juce_recommended_config_flags
    # and juce_recommended_lto_flags already set all four, so re-stating them
    # only produced
    #     warning D9025: overriding '/Ob2' with '/Ob3'
    # on every single translation unit. JUCE's own settings are correct; the
    # duplicate was noise, and duplicating build flags is how configured and
    # actual optimisation silently drift apart.
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)

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
        #      time. That is why `cmake -S . -B build` alone can cost as much as
        #      an entire Mac build. It cannot be avoided, but it is worth knowing
        #      that the time is spent before any of your code is touched.
        #
        #   2. /GL + /LTCG. Whole-program optimisation makes MSVC write and then
        #      re-read intermediate representation for every object, which
        #      roughly doubles the work. Clang on macOS does LTO with the same
        #      flag and markedly less overhead. This is by far the largest
        #      avoidable item, and it is only worth its cost on release
        #      binaries. Set J37_FAST_WINDOWS_BUILD=ON to drop it for iteration;
        #      a distributable build should leave it off.
        #
        #   3. Parallelism. MSBuild defaults to a small number of concurrent CL
        #      processes. /MP scales to the machine.
        # ------------------------------------------------------------------

        option(J37_FAST_WINDOWS_BUILD "Windows: skip link-time code generation for faster iteration" ON)

        if(J37_FAST_WINDOWS_BUILD)
            # ------------------------------------------------------------------
            #  The LTO flags do not live on this target, so they cannot be
            #  removed from it. JUCE puts them on the INTERFACE of
            #  juce_recommended_lto_flags as generator expressions:
            #
            #     target_compile_options(... INTERFACE $<$<CONFIG:Release>:-GL>)
            #     target_link_libraries(... INTERFACE $<$<CONFIG:Release>:-LTCG>)
            #
            #  Three consequences, all of which an earlier attempt at this got
            #  wrong:
            #
            #   - The flags arrive through a linked library, so filtering this
            #     target's own COMPILE_OPTIONS/LINK_OPTIONS finds nothing at all.
            #   - They are generator expressions, so matching on the literal
            #     "-GL" never matches either.
            #   - The link flag is added with target_link_libraries, not
            #     target_link_options, so it is not in LINK_OPTIONS to begin with.
            #
            #  Clearing the interface is the only thing that takes effect. It is
            #  safe here because this target is the only consumer of that helper
            #  in this project, and it is scoped inside an opt-out the user can
            #  flip.
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

        # /MP: compile translation units in parallel within one project.
        # Without it MSBuild parallelises across projects, and this plugin is
        # only a handful of projects, so most cores sit idle.
        target_compile_options(${target} PRIVATE /MP)

        # /Zf: generate debug info in parallel. Harmless and noticeable on a
        # build with this many translation units.
        target_compile_options(${target} PRIVATE /Zf)
    endif()
endfunction()# ------------------------------------------------------------------------------
#  Android
# ------------------------------------------------------------------------------
#  What "building for Android" can mean here, given what JUCE's CMake support
#  actually is. JUCE's CMake layer compiles plugin formats from the platform
#  kind list, and that list reduces to Standalone alone on Android - VST3 and
#  AU are explicitly excluded when CMAKE_SYSTEM_NAME is Android
#  (JUCEModuleSupport.cmake, _juce_get_platform_plugin_kinds). There is also no
#  Gradle exporter: the Projucer wrote an Android Studio project, but CMake
#  ships nothing equivalent, and the mobile hosts an APK could contain do not
#  exist for desktop plugin formats anyway.
#
#  So Android support is the Standalone wrapper built as a shared library for
#  the NDK (that is what juce_add_plugin produces for every format target when
#  CMAKE_SYSTEM_NAME is Android - _juce_link_plugin_wrapper), and a Gradle
#  project skeleton that loads it as an external native build. The activity
#  classes, manifest, resources, icons and signing all live on the Gradle side
#  because an APK cannot be produced from CMake: this is the same division
#  every NDK consumer has, not a limitation of this project.
#
#  Required toolchain: the Android NDK (r23+), either via Android Studio's SDK
#  Manager or commandlinetools, and CMake 3.22+ inside that SDK. Configure
#  through Gradle (recommended - it drives the NDK for you) or by hand:
#
#      cmake -S . -B build-android \
#          -DCMAKE_SYSTEM_NAME=Android \
#          -DCMAKE_ANDROID_API=28 \
#          -DCMAKE_ANDROID_NDK=<ndk path> \
#          -DCMAKE_ANDROID_ARCH_ABI=arm64-v8a
#
#  API 28 is the floor, and the reason is a hard dependency rather than taste:
#  chowdsp_core's aligned allocator calls std::aligned_alloc, and bionic has
#  had that function only since API 28 - building against anything lower fails
#  inside the chowdsp headers with "reference to unresolved using
#  declaration". ARMEABI-V7A can be added as a second ABI but doubles build
#  time for the shrinking 32-bit device base.
function(j37_apply_android_settings target)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
        return()
    endif()

    # Every format target JUCE created for this plugin (SharedCode plus one per
    # format kind) is a SHARED library on Android and is what the APK's
    # PackageManager loads. They all carry the same rpath-less conventions
    # Gradle expects, so nothing per-target is needed beyond what JUCE set;
    # this block exists to state the project's choices and to fail early on
    # settings that only make sense on the desktop.

    # The audio backends: JUCE bundles Oboe and its OpenSL ES fallback inside
    # juce_audio_devices, so there is nothing to link from the NDK. The GL ES
    # version for the best-effort OpenGL panel renderer is selected by the
    # module layer (GL ES 3.0 where available); no extra link flags are needed.

    # The desktop-only juce_webbrowser backend is already off project-wide
    # (JUCE_WEB_BROWSER=0 in the top-level file), which is also what Android
    # wants - JUCE's Android webview path needs the activity helpers a
    # generated Gradle project would provide.

    # Android requires every shared library to declare its STL. JUCE's module
    # layer uses the C++ standard library throughout, so c++_static is the
    # correct choice for a single .so that owns its whole process (the Standalone
    # app); a plugin collection sharing one process would switch to c++_shared.
    set(CMAKE_ANDROID_STL_TYPE "c++_static" PARENT_SCOPE)

    message(STATUS "Android: Standalone wrapper as a shared library for the NDK")
    message(STATUS "  min API     : ${CMAKE_ANDROID_API}")
    message(STATUS "  ABI         : ${CMAKE_ANDROID_ARCH_ABI}")
    message(STATUS "  STL         : c++_static (set CMAKE_ANDROID_STL_TYPE to change)")
    message(STATUS "  Next step   : point a Gradle project's externalNativeBuild at "
                   "this CMakeLists (see README, the Android section)")
endfunction()
