/*
  ============================================================================
    RenderBackend - renderer selection and capability boundary.

    OpenGL is the only backend that currently renders the full scene. Native
    API work is staged behind this capability boundary and must not be selected
    until the scene draw path is complete.
  ============================================================================
*/

#pragma once
#include <JuceHeader.h>

namespace j37::render {
/**
    The renderer a build was configured for.

    `sokol` and `bgfx` are the two GPU abstraction layers this plugin can be
    built against. They are ALTERNATIVES rather than additions: each one owns the
    device, the surface and the frame, and two of them in one binary would be two
    owners of the same window. The build option selects one, exactly as it
    selects Vulkan or Metal, which is also what keeps them from interfering.
*/
enum class Backend { autoDetect, openGL, vulkan, metal, directX11, directX12, sokol, bgfx, cpu };

constexpr Backend configuredBackend() noexcept {
#if defined(J37_RENDER_BACKEND_VULKAN)
    return Backend::vulkan;
#elif defined(J37_RENDER_BACKEND_METAL)
    return Backend::metal;
#elif defined(J37_RENDER_BACKEND_D3D11)
    return Backend::directX11;
#elif defined(J37_RENDER_BACKEND_D3D12)
    return Backend::directX12;
#elif defined(J37_RENDER_BACKEND_SOKOL)
    return Backend::sokol;
#elif defined(J37_RENDER_BACKEND_BGFX)
    return Backend::bgfx;
#elif defined(J37_RENDER_BACKEND_CPU)
    return Backend::cpu;
#elif defined(J37_RENDER_BACKEND_OPENGL)
    return Backend::openGL;
#else
    return Backend::autoDetect;
#endif
}

// A native adapter is not a usable scene backend merely because it can present
// a cleared surface. Keep every native selection on the established fallback
// until it can draw the complete TapeScene frame.
//
// The parameter is unnamed on purpose: juce::ignoreUnused is not constexpr, so
// calling it here made this function ill-formed in any constant expression and
// clang rejected the header outright while gcc happened to let it through.
constexpr bool hasNativeImplementation(Backend) noexcept {
    return false;
}

constexpr bool isNativeBackendPending(Backend backend) noexcept {
    return backend == Backend::vulkan
        || backend == Backend::metal
        || backend == Backend::directX11
        || backend == Backend::directX12
        || backend == Backend::sokol
        || backend == Backend::bgfx;
}

constexpr Backend effectiveBackend() noexcept {
    constexpr auto configured = configuredBackend();

    if (configured == Backend::autoDetect || configured == Backend::openGL)
        return Backend::openGL;

    return Backend::cpu;
}

constexpr bool usesOpenGL() noexcept { return effectiveBackend() == Backend::openGL; }

constexpr bool usesCpuFallback() noexcept { return effectiveBackend() == Backend::cpu; }

constexpr bool usesNativeCommandRenderer() noexcept
{
    return false;
}

constexpr bool isFallbackActive() noexcept {
    return isNativeBackendPending (configuredBackend())
        && usesCpuFallback();
}

} // namespace j37::render
