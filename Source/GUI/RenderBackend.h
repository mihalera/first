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
//
// hasNativeImplementation returns true only for backends that have been ported
// with a complete scene draw path. Until then, they fall through to CPU.
//
// Each backend's scene draw availability is gated by its build macro:
//
//   - J37_NATIVE_SOKOL  -> Sokol backend has scene draw
//   - J37_NATIVE_VULKAN -> Vulkan backend has scene draw
//   - J37_NATIVE_METAL  -> Metal backend has scene draw
//   - J37_NATIVE_D3D11  -> DirectX 11 has scene draw
//   - J37_NATIVE_D3D12  -> DirectX 12 has scene draw
//   - J37_NATIVE_BGFX   -> BGFX backend has scene draw
constexpr bool hasNativeImplementation(Backend backend) noexcept {
#if defined(J37_NATIVE_SOKOL)
    constexpr bool sokolDrawsScene = true;
#else
    constexpr bool sokolDrawsScene = false;
#endif
    return backend == Backend::sokol && sokolDrawsScene;
}

constexpr bool isNativeBackendPending(Backend backend) noexcept {
    // If the backend has scene draw, it is NOT pending - it is effective.
    if (hasNativeImplementation(backend))
        return false;

    // These backends can present a cleared surface but have no scene draw yet,
    // so they are pending until their scene draw is implemented.
    return backend == Backend::vulkan
        || backend == Backend::metal
        || backend == Backend::directX11
        || backend == Backend::directX12
        || backend == Backend::bgfx;
}

constexpr Backend effectiveBackend() noexcept {
    constexpr auto configured = configuredBackend();

    // Priority order for AUTO mode: try backends in sequence,
    // with CPU as the absolute last resort.
    //
    // This structure ensures that when native backends (Vulkan, Metal,
    // Sokol, BGFX) become portable with complete scene draw paths,
    // the priority order is already established rather than needing
    // a redesign later.
    //
    // Current state: hasNativeImplementation returns false for all
    // backends, so AUTO effectively falls through to CPU.
    // When native backends are ported, their checks will enable
    // actual use rather than falling through.
    //
    if (configured == Backend::autoDetect || configured == Backend::openGL)
        return Backend::openGL;

    // Try Metal if configured (will fall to CPU until scene draw is ported)
#if defined(J37_RENDER_BACKEND_METAL)
    if (configured == Backend::metal)
        return Backend::metal;
#endif

    // Try Vulkan if configured (will fall to CPU until scene draw is ported)
#if defined(J37_RENDER_BACKEND_VULKAN)
    if (configured == Backend::vulkan)
        return Backend::vulkan;
#endif

    // Try D3D12 if configured (will fall to CPU until scene draw is ported)
#if defined(J37_RENDER_BACKEND_D3D12)
    if (configured == Backend::directX12)
        return Backend::directX12;
#endif

    // Try D3D11 if configured (will fall to CPU until scene draw is ported)
#if defined(J37_RENDER_BACKEND_D3D11)
    if (configured == Backend::directX11)
        return Backend::directX11;
#endif

    // Try Sokol if configured (will fall to CPU until scene draw is ported)
#if defined(J37_RENDER_BACKEND_SOKOL)
    if (configured == Backend::sokol)
        return Backend::sokol;
#endif

    // Try BGFX if configured (will fall to CPU until scene draw is ported)
#if defined(J37_RENDER_BACKEND_BGFX)
    if (configured == Backend::bgfx)
        return Backend::bgfx;
#endif

    // CPU is the absolute last resort - always available
    return Backend::cpu;
}

constexpr bool usesOpenGL() noexcept { return effectiveBackend() == Backend::openGL; }

constexpr bool usesCpuFallback() noexcept { return effectiveBackend() == Backend::cpu; }

/** Whether SETTINGS may offer this backend at RUNTIME.

    A build configured for a native adapter still draws the scene with the CPU
    until that adapter's scene draw exists, so the choices that can be honoured
    now are AUTO (whatever the build selected), OPENGL (the one full scene path)
    and CPU (the software painter). Everything else is listed as PENDING and
    stays unselectable: an entry that changed nothing would be the panel claiming
    a capability the build does not have. */
constexpr bool isBackendSelectable (Backend backend) noexcept {
    return backend == Backend::autoDetect
        || backend == Backend::cpu
        || (backend == Backend::openGL && usesOpenGL())
        || hasNativeImplementation(backend);
}

/** The renderer a CHOSEN backend actually runs.

    CPU is always CPU and AUTO defers to the build. OPENGL resolves to the
    software path in a build whose scene has no GL path, and a native adapter is
    answered by its capability probe rather than by its name - so a selection
    made in one build can never make another build claim a renderer it cannot
    draw with. */
constexpr Backend runningBackend (Backend choice) noexcept {
    if (choice == Backend::autoDetect)
        return effectiveBackend();

    if (choice == Backend::openGL)
        return usesOpenGL() ? Backend::openGL : Backend::cpu;

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
    return isNativeBackendPending (configuredBackend())
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
    draws with the CPU, and saying otherwise would be the panel claiming a
    renderer it is not running. When the build's own adapter is still staged it is
    named too, as PENDING, which is the state RenderBackend.h is in until the tape
    scene is ported. */
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
