/*
  ============================================================================
    RenderBackend - renderer selection and capability boundary.

    OpenGL is the only backend that renders the full scene. Other native
    adapters currently provide surface presentation only, so they must not be
    selected as effective scene renderers until they implement the geometry.
  ============================================================================
*/

#pragma once
#include <JuceHeader.h>

namespace j37::render {
enum class Backend { autoDetect, openGL, vulkan, metal, directX11, directX12, cpu };

constexpr Backend configuredBackend() noexcept {
#if defined(J37_RENDER_BACKEND_VULKAN)
    return Backend::vulkan;
#elif defined(J37_RENDER_BACKEND_METAL)
    return Backend::metal;
#elif defined(J37_RENDER_BACKEND_D3D11)
    return Backend::directX11;
#elif defined(J37_RENDER_BACKEND_D3D12)
    return Backend::directX12;
#elif defined(J37_RENDER_BACKEND_CPU)
    return Backend::cpu;
#elif defined(J37_RENDER_BACKEND_OPENGL)
    return Backend::openGL;
#else
    return Backend::autoDetect;
#endif
}

// Native adapters currently provide presentation only, not the TapeScene draw
// path. Do not select them as effective renderers until they render the scene.
constexpr bool hasNativeImplementation(Backend) noexcept {
    return false;
}

constexpr bool isNativeBackendPending(Backend backend) noexcept {
    return backend == Backend::vulkan
        || backend == Backend::metal
        || backend == Backend::directX11
        || backend == Backend::directX12;
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
