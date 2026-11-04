/*
  ============================================================================
    RenderBackend - renderer selection and capability boundary.

    OpenGL, Metal and SOKOL draw the complete TapeScene today. The other native
    values are still staged: their adapters may create a device and present a
    cleared surface, but they do not draw the scene in this build. They must be
    advertised as PENDING and resolved to the CPU fallback, never selected as if
    the scene path existed.
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
// a cleared surface. J37_NATIVE_* is set by CMake only for an adapter that has
// the complete SceneFrame draw path; the staged adapters are deliberately left
// without it, so their configured value stays PENDING and falls through to the
// CPU fallback rather than showing a blank deck.
//
// The parameter is read in every macro branch that exists; the `(void)` keeps
// the function well-formed in builds with no J37_NATIVE_* definitions, where
// none of the branches below are compiled.
constexpr bool hasNativeImplementation(Backend backend) noexcept {
    (void) backend;
#if defined(J37_NATIVE_SOKOL)
    if (backend == Backend::sokol)
        return true;
#endif
#if defined(J37_NATIVE_METAL)
    if (backend == Backend::metal)
        return true;
#endif
#if defined(J37_NATIVE_VULKAN)
    if (backend == Backend::vulkan)
        return true;
#endif
#if defined(J37_NATIVE_D3D11)
    if (backend == Backend::directX11)
        return true;
#endif
#if defined(J37_NATIVE_D3D12)
    if (backend == Backend::directX12)
        return true;
#endif
#if defined(J37_NATIVE_BGFX)
    if (backend == Backend::bgfx)
        return true;
#endif
    return false;
}

/** True for a native value whose scene draw is not part of this build. */
constexpr bool isNativeBackendPending(Backend backend) noexcept {
    if (hasNativeImplementation(backend))
        return false;

    return backend == Backend::vulkan
        || backend == Backend::metal
        || backend == Backend::directX11
        || backend == Backend::directX12
        || backend == Backend::bgfx;
}

constexpr Backend effectiveBackend() noexcept {
    constexpr auto configured = configuredBackend();

    // AUTO and a plain OpenGL build both use JUCE's OpenGL path.
    if (configured == Backend::autoDetect || configured == Backend::openGL)
        return Backend::openGL;

    // A configured native value is effective ONLY when its adapter can draw the
    // complete frame. Otherwise the editor keeps the CPU painter: an incomplete
    // adapter must never become the running backend just because its platform
    // SDK was found.
    if (hasNativeImplementation(configured))
        return configured;

    return Backend::cpu;
}

constexpr bool usesOpenGL() noexcept { return effectiveBackend() == Backend::openGL; }

constexpr bool usesCpuFallback() noexcept { return effectiveBackend() == Backend::cpu; }

/** Whether SETTINGS may offer this backend at RUNTIME.

    A build configured for a staged native adapter still draws the scene with the
    CPU, so the choices that can be honoured now are AUTO (whatever the build
    selected), OPENGL (JUCE's always-available scene path) and CPU (the software
    painter). Everything else is listed as PENDING and stays unselectable: an
    entry that changed nothing would be the panel claiming a capability the
    build does not have. */
constexpr bool isBackendSelectable (Backend backend) noexcept {
    // OPENGL is JUCE's own context and is always available at runtime, even in
    // a build configured for a native adapter: the panel's GL context and the
    // deck's are separate, and a staged native value must not remove the one
    // complete GPU scene path the user can fall back to.
    return backend == Backend::autoDetect
        || backend == Backend::cpu
        || backend == Backend::openGL
        || hasNativeImplementation(backend);
}

/** The renderer a CHOSEN backend actually runs.

    CPU is always CPU, AUTO defers to the build, and OPENGL is JUCE's own path in
    every build. A native adapter is answered by its capability probe rather than
    by its name - so a selection made in one build can never make another build
    claim a renderer it cannot draw with. */
constexpr Backend runningBackend (Backend choice) noexcept {
    if (choice == Backend::autoDetect)
        return effectiveBackend();

    if (choice == Backend::openGL)
        return Backend::openGL;

    if (choice == Backend::cpu)
        return Backend::cpu;

    return hasNativeImplementation (choice) ? choice : Backend::cpu;
}

constexpr bool runsOnOpenGL (Backend choice) noexcept {
    return runningBackend (choice) == Backend::openGL;
}

constexpr bool usesNativeCommandRenderer() noexcept
{
    return false;
}

constexpr bool isFallbackActive() noexcept {
    return isNativeBackendPending(configuredBackend())
        && usesCpuFallback();
}

/** The API's name in the spelling the build option uses, so the panel and
    `-DJ37_RENDER_BACKEND=` say the same word for the same thing. */
constexpr const char* backendName (Backend backend) noexcept {
    switch (backend) {
        case Backend::autoDetect: return "AUTO";
        case Backend::openGL:     return "OPENGL";
        case Backend::vulkan:     return "VULKAN";
        case Backend::metal:      return "METAL";
        case Backend::directX11:  return "D3D11";
        case Backend::directX12:  return "D3D12";
        case Backend::sokol:      return "SOKOL";
        case Backend::bgfx:       return "BGFX";
        case Backend::cpu:        return "CPU";
    }

    return "UNKNOWN";
}

/** What the panel's header prints: the API the frame is actually drawn with.

    `choice` is the renderer selected in SETTINGS (AUTO when there is no choice),
    and `runningBackend()` is what that choice actually runs - not what it asks
    for - because a build configured for an adapter that only clears its surface
    draws with the CPU under AUTO, and saying otherwise would be the panel
    claiming a renderer it is not running. When the build's own adapter is still
    staged it is named too, as PENDING. */
inline juce::String backendCaption (Backend choice = Backend::autoDetect) {
    constexpr auto configured = configuredBackend();
    const auto running = runningBackend (choice);

    juce::String caption ("BACKEND  ");
    caption << backendName (running);

    if (isNativeBackendPending (configured) && running != configured)
        caption << " (" << backendName (configured) << " PENDING)";

    return caption;
}

} // namespace j37::render
