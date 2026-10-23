#pragma once
#include "RenderBackend.h"
#include <JuceHeader.h>

#include <memory>

// Sokol_gfx headers - Sokol is a single-header library.
// The Sokol_gfx header provides the immediate-mode graphics API.
// We include it directly; the library is header-only and already fetched
// via CPM as a dependency with DOWNLOAD_ONLY YES.// Platform-specific Sokol_gfx backend selection.
// Sokol_gfx will auto-detect the backend based on the defined macros,
// but we set the appropriate defines so it picks the right one for each platform.

// On Apple (macOS/iOS): use Metal backend
// On Windows/Linux with Vulkan: use Vulkan backend
// On Windows/Linux without Vulkan: use OpenGL (core profile)

// We need to define the Sokol backend before including the Sokol header.
// Sokol_gfx.h will use these macros to select the right backend.
// Platform-specific init data for Sokol_gfx.

#if defined(__APPLE__) || defined(__MACOSX)
// macOS - Metal backend
#define SOKOL_GFX_IMPL_METAL
#else
// Windows/Linux - we check for Vulkan first
// If VK_* macros are defined, use Vulkan; otherwise OpenGL
#if defined(SOKOL_GFX_VULKAN)
#define SOKOL_GFX_IMPL_VULKAN
#else
#define SOKOL_GFX_IMPL_GL_CORE
#endif
#endif

#if defined(SOKOL_GFX_IMPL_METAL)
// Metal on macOS is already handled above
#elif defined(SOKOL_GFX_IMPL_VULKAN)
// Vulkan on Windows/Linux - user explicitly requested it
#elif defined(SOKOL_GFX_IMPL_GL_CORE)
// OpenGL core profile on Windows/Linux
#endif

// Sokol_gfx header - this brings in the full implementation.
// The library is header-only and already fetched via CPM.
#if !defined(SOKOL_GFX_H_INCLUDED)
#define SOKOL_GFX_H_INCLUDED

// Include Sokol_gfx - note: this is a large header, but since we're
// in a plugin build, it's fine. The header is already in the CPM cache.
#include "sokol_gfx.h"

// If using Metal on macOS, we need to include the Metal header too.
// Sokol_gfx handles this internally when SOKOL_GFX_IMPL_METAL is defined.
#if defined(SOKOL_GFX_IMPL_METAL)
#if !defined(SOKOL_GFX_IMPL_METAL)
#define SOKOL_GFX_IMPL_METAL
#endif
#endif

#if defined(SOKOL_GFX_IMPL_VULKAN)
// Vulkan requires the vk_struct type for the desc
#if !defined(SOKOL_GFX_IMPL_VULKAN)
#define SOKOL_GFX_IMPL_VULKAN
#endif
#endif

#if defined(SOKOL_GFX_IMPL_GL_CORE)
// OpenGL core profile - no extra includes needed
#if !defined(SOKOL_GFX_IMPL_GL_CORE)
#define SOKOL_GFX_IMPL_GL_CORE
#endif
#endif

#endif // SOKOL_GFX_H_INCLUDED

namespace j37::render {


// Sokol_gfx header - this brings in the full implementation.
// The library is header-only and already fetched via CPM.
#if !defined(SOKOL_GFX_H_INCLUDED)
#define SOKOL_GFX_H_INCLUDED

// Include Sokol_gfx - note: this is a large header, but since we're
// in a plugin build, it's fine. The header is already in the CPM cache.
#include "sokol_gfx.h"

// If using Metal on macOS, we need to include the Metal header too.
// Sokol_gfx handles this internally when SOKOL_GFX_IMPL_METAL is defined.

#endif // SOKOL_GFX_H_INCLUDED

namespace j37::render {

 /** 
  * Sokol_gfx-based native renderer.
  * 
  * Uses Sokol_gfx immediate-mode graphics library for 3D rendering.
  * Sokol_gfx is header-only, Zlib-licensed, and provides a cross-platform
  * graphics API that can run on OpenGL, Metal, or GLES backends.
  * 
  * In this plugin, Sokol_gfx will be used for 3D visualizations in the
  * TapeScene (waveforms, spectra, tape deck effects) while JUCE::Graphics
  * continues to handle the 2D UI controls.
  */
 class SokolGfxRenderer final : public NativeRenderer {
 public:
    struct Config {
        int width = 1;
        int height = 1;
        int framesInFlight = 2;
        bool validation = false;
        int positionX = 0;
        int positionY = 0;
        int vsyncInterval = 1;
    };

    SokolGfxRenderer() = default;
    ~SokolGfxRenderer() override { shutdown(); }

    // NativeRenderer interface implementation
    bool initialise(juce::Component& peer, Config config) override;
    void resize(Config config) override;
    bool isPresentable() const noexcept override { return true; }
    void shutdown() noexcept override;
    bool beginFrame() override;
    void clear(juce::Colour) override;
    void endFrame() override;
    bool isInitialised() const noexcept override { return _initialised; }
    juce::String status() const override;

 private:
    bool _initialised = false;
    int _width = 1;
    int _height = 1;
    // Sokol_gfx context state - we keep it simple since Sokol_gfx
    // manages its own internal state via static globals.
    // We just track our own dimensions and presentability.
 };

} // namespace j37::render