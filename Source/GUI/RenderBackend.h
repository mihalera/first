/*
  ============================================================================
    RenderBackend - renderer selection and capability boundary.

    The scene currently has a complete JUCE/OpenGL implementation.  The other
    API names are deliberately represented here before their native device
    code is added: selecting one of them never leaves the editor without a
    picture.  TapeScene falls back to JUCE's CPU paint path until that backend
    is compiled in.
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

constexpr const char *name(Backend backend) noexcept {
    switch (backend) {
    case Backend::autoDetect:
        return "Auto";
    case Backend::openGL:
        return "OpenGL";
    case Backend::vulkan:
        return "Vulkan";
    case Backend::metal:
        return "Metal";
    case Backend::directX11:
        return "DirectX 11";
    case Backend::directX12:
        return "DirectX 12";
    case Backend::cpu:
        return "CPU";
    }

    return "Unknown";
}

// Native device adapters are compiled per-platform. OpenGL remains the
// established renderer, while Vulkan, Metal and D3D12 now have command
// resource initialization paths; unsupported platform/API combinations still
// fall back to CPU at runtime.
constexpr bool hasNativeImplementation(Backend backend) noexcept {
#if defined (J37_NATIVE_VULKAN)
    if (backend == Backend::vulkan) return true;
#endif
#if defined (J37_NATIVE_METAL)
    if (backend == Backend::metal) return true;
#endif
#if defined (J37_NATIVE_D3D11)
    if (backend == Backend::directX11) return true;
#endif
#if defined (J37_NATIVE_D3D12)
    if (backend == Backend::directX12) return true;
#endif
    return backend == Backend::openGL || backend == Backend::cpu;
}

constexpr bool isNativeBackendPending(Backend backend) noexcept {
    return backend == Backend::vulkan
        || backend == Backend::metal
        || backend == Backend::directX11
        || backend == Backend::directX12;
}

constexpr Backend effectiveBackend() noexcept {
    constexpr auto configured = configuredBackend();

    if (configured == Backend::autoDetect)
        return Backend::openGL;

    return hasNativeImplementation(configured) ? configured : Backend::cpu;
}

constexpr bool usesOpenGL() noexcept { return effectiveBackend() == Backend::openGL; }

constexpr bool usesCpuFallback() noexcept { return effectiveBackend() == Backend::cpu; }

constexpr bool usesNativeCommandRenderer() noexcept
{
    return effectiveBackend() == Backend::vulkan
        || effectiveBackend() == Backend::metal
        || effectiveBackend() == Backend::directX11
        || effectiveBackend() == Backend::directX12;
}

constexpr bool isFallbackActive() noexcept {
    return isNativeBackendPending (configuredBackend())
        && usesCpuFallback();
}

constexpr const char* effectiveName() noexcept {
    return name (effectiveBackend());
}

constexpr const char* statusText() noexcept {
    if (isFallbackActive())
        return "CPU fallback (native renderer unavailable)";

    if (usesOpenGL())
        return "OpenGL";

    return "CPU";
}
} // namespace j37::render
