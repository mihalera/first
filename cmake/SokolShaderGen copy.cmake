# SokolShaderGen.cmake
#
# Phase 2.1 shader plumbing from docs/backend-implementation-plan.md:
# derive SPIR-V from the GLSL sources in tests/shaders/VulkanSceneShaders/ at
# build time with glslangValidator, and embed the .spv as generated C array
# headers the Vulkan renderer can include.
#
# The GLSL sources stay as the run-on string literals the OpenGL and Sokol paths
# read; this block only derives SPIR-V from the same text a validator or
# tests/shaders/check_shaders.py could read. The .spv headers are derived data,
# written by this block and never edited by hand.
#
# Configure-time fallback: if no validator is found, J37_NATIVE_VULKAN is removed
# from the build and the build warns. That is the same pattern the CMake platform
# guards already use, and it is better than a runtime shaderc dependency the
# plugin does not otherwise carry.

if(DEFINED J37_NATIVE_RENDER_DEFINITIONS AND
   "J37_NATIVE_VULKAN=1" IN_LIST J37_NATIVE_RENDER_DEFINITIONS)
    find_program(J37_VULKAN_GLSLANG_VALIDATOR glslangValidator
        HINTS ENV VK_SDK_PATH ENV VULKAN_SDK PATH)
endif()

if(DEFINED J37_NATIVE_RENDER_DEFINITIONS AND
   "J37_NATIVE_VULKAN=1" IN_LIST J37_NATIVE_RENDER_DEFINITIONS AND
   NOT J37_VULKAN_GLSLANG_VALIDATOR)
    message(WARNING
        "J37_RENDER_BACKEND=VULKAN requested but glslangValidator was not found. "
        "The Vulkan renderer will not be compiled in this configuration: "
        "J37_NATIVE_VULKAN has been removed from the build. Install the Vulkan SDK "
        "(glslang-tools) or keep J37_RENDER_BACKEND on a different selection.")
    list(REMOVE_ITEM J37_NATIVE_RENDER_DEFINITIONS "J37_NATIVE_VULKAN=1")
endif()

if(DEFINED J37_NATIVE_RENDER_DEFINITIONS AND
   "J37_NATIVE_VULKAN=1" IN_LIST J37_NATIVE_RENDER_DEFINITIONS)
    set(J37_VULKAN_SHADER_OUTDIR
        "${CMAKE_CURRENT_BINARY_DIR}/generated/vulkan")
    file(MAKE_DIRECTORY "${J37_VULKAN_SHADER_OUTDIR}")

    # The GLSL text the Vulkan shaders are derived from. The Vulkan GLSL sources
    # live under tests/shaders/VulkanSceneShaders/ so the shader text stays in the
    # tree tests/shaders/check_shaders.py can read; that is the contract, not the
    # build-private extraction below.
    set(J37_VULKAN_GLSL_VERTEX_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/shaders/VulkanSceneShaders/vert.glsl")
    set(J37_VULKAN_GLSL_FRAGMENT_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/shaders/VulkanSceneShaders/frag.glsl")

    # Derived SPIR-V, one .spv per stage.
    set(J37_VULKAN_VERTEX_SPIRV
        "${J37_VULKAN_SHADER_OUTDIR}/VulkanSceneShaders.vert.spv")
    set(J37_VULKAN_FRAGMENT_SPIRV
        "${J37_VULKAN_SHADER_OUTDIR}/VulkanSceneShaders.frag.spv")

    # glslangValidator takes a GLSL source file, so this block compiles the
    # sources that already live under tests/shaders/VulkanSceneShaders/ directly.
    # The extraction step still exists for the same text in case a build wants a
    # private copy from the header path, but the canonical sources are the .glsl
    # files the test can inspect.
    set(J37_VULKAN_GLSL_VERTEX_TMP
        "${J37_VULKAN_SHADER_OUTDIR}/VulkanSceneShaders.vert.glsl")
    set(J37_VULKAN_GLSL_FRAGMENT_TMP
        "${J37_VULKAN_SHADER_OUTDIR}/VulkanSceneShaders.frag.glsl")

    add_custom_command(
        OUTPUT "${J37_VULKAN_GLSL_VERTEX_TMP}"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${J37_VULKAN_GLSL_VERTEX_SOURCE}"
            "${J37_VULKAN_GLSL_VERTEX_TMP}"
        DEPENDS "${J37_VULKAN_GLSL_VERTEX_SOURCE}"
        COMMENT
            "VulkanShaderGen: copying vertex GLSL source for Vulkan compile"
        VERBATIM)
    add_custom_command(
        OUTPUT "${J37_VULKAN_GLSL_FRAGMENT_TMP}"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${J37_VULKAN_GLSL_FRAGMENT_SOURCE}"
            "${J37_VULKAN_GLSL_FRAGMENT_TMP}"
        DEPENDS "${J37_VULKAN_GLSL_FRAGMENT_SOURCE}"
        COMMENT
            "VulkanShaderGen: copying fragment GLSL source for Vulkan compile"
        VERBATIM)

    add_custom_command(
        OUTPUT "${J37_VULKAN_VERTEX_SPIRV}"
        COMMAND ${CMAKE_COMMAND} -E echo
            "VulkanShaderGen: compiling ${J37_VULKAN_GLSL_VERTEX_TMP} to "
            "${J37_VULKAN_VERTEX_SPIRV}"
        COMMAND ${J37_VULKAN_GLSLANG_VALIDATOR}
            -V
            -S vert
            -o "${J37_VULKAN_VERTEX_SPIRV}"
            "${J37_VULKAN_GLSL_VERTEX_TMP}"
        DEPENDS "${J37_VULKAN_GLSL_VERTEX_TMP}"
        COMMENT
            "Compiling Vulkan vertex GLSL to SPIR-V"
        VERBATIM)
    add_custom_command(
        OUTPUT "${J37_VULKAN_FRAGMENT_SPIRV}"
        COMMAND ${CMAKE_COMMAND} -E echo
            "VulkanShaderGen: compiling ${J37_VULKAN_GLSL_FRAGMENT_TMP} to "
            "${J37_VULKAN_FRAGMENT_SPIRV}"
        COMMAND ${J37_VULKAN_GLSLANG_VALIDATOR}
            -V
            -S frag
            -o "${J37_VULKAN_FRAGMENT_SPIRV}"
            "${J37_VULKAN_GLSL_FRAGMENT_TMP}"
        DEPENDS "${J37_VULKAN_GLSL_FRAGMENT_TMP}"
        COMMENT
            "Compiling Vulkan fragment GLSL to SPIR-V"
        VERBATIM)

    # Embed the .spv as a generated C array header the Vulkan renderer includes.
    set(J37_VULKAN_SHADER_EMBED_SOURCES "")
    foreach(SPIRV_FILE ${J37_VULKAN_VERTEX_SPIRV} ${J37_VULKAN_FRAGMENT_SPIRV})
        get_filename_component(SPIRV_BASE "${SPIRV_FILE}" NAME_WE)
        set(EMBED_HDR
            "${J37_VULKAN_SHADER_OUTDIR}/${SPIRV_BASE}_spirv.h")

        file(WRITE "${EMBED_HDR}"
            "// Generated by VulkanShaderGen.cmake from ${SPIRV_FILE}\n"
            "// Do not edit by hand. SPIR-V embedded as a C array.\n"
            "#pragma once\n"
            "#include <stddef.h>\n"
            "extern const unsigned char ${SPIRV_BASE}_spirv[];\n"
            "extern const unsigned int ${SPIRV_BASE}_spirv_words;\n")

        add_custom_command(
            OUTPUT "${EMBED_HDR}"
            COMMAND ${PYTHON_EXECUTABLE}
                "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VulkanEmbedSpv.py"
                --input "${SPIRV_FILE}"
                --header "${EMBED_HDR}"
                --symbol "${SPIRV_BASE}"
            DEPENDS "${SPIRV_FILE}"
            COMMENT
                "Embedding ${SPIRV_FILE} as ${SPIRV_BASE}_spirv[]"
            VERBATIM)
        list(APPEND J37_VULKAN_SHADER_EMBED_SOURCES "${EMBED_HDR}")
    endforeach()

    # VulkanSceneShaders.h is the umbrella the Vulkan renderer includes. It is
    # regenerated whenever the embedded SPIR-V headers are regenerated.
    set(J37_VULKAN_SHADER_UMBRELLA
        "${J37_VULKAN_SHADER_OUTDIR}/VulkanSceneShaders.h")
    add_custom_command(
        OUTPUT "${J37_VULKAN_SHADER_UMBRELLA}"
        COMMAND ${CMAKE_COMMAND} -E echo
            "VulkanShaderGen: writing VulkanSceneShaders.h"
        COMMAND ${CMAKE_COMMAND} -E echo
            "// Generated by VulkanShaderGen.cmake\n"
            "// Do not edit by hand.\n"
            "#pragma once\n"
            "#include \"VulkanSceneShaders.vert_spirv.h\"\n"
            "#include \"VulkanSceneShaders.frag_spirv.h\"\n"
            > "${J37_VULKAN_SHADER_UMBRELLA}"
        DEPENDS ${J37_VULKAN_SHADER_EMBED_SOURCES}
        COMMENT
            "Generating VulkanSceneShaders.h from embedded SPIR-V"
        VERBATIM)

    add_custom_target(J37_VULKAN_SHADERS ALL
        DEPENDS "${J37_VULKAN_SHADER_UMBRELLA}")
endif()
