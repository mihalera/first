#include "NativeRenderer_SokolGfx.h"

#include <JuceHeader.h>

// Sokol_gfx is a header-only library. We include the implementation header
// which provides the sokol_gfx API functions.
// The Sokol_gfx library is already fetched via CPM as a dependency
// (ThirdParty/xsimd/... and the Sokol_gfx header is in the CPM cache).

// -----------------------------------------------------------------------------
// Sokol_gfx initialisation / shutdown
// -----------------------------------------------------------------------------

// Sokol_gfx desc structure - describes the graphics context we want.
// Sokol_gfx will auto-detect the backend (OpenGL, Metal, GLES) based on
// the platform and the defines we set above.

static bool _sokol_inited = false;

void sokol_gfx_shutdown_internal()
{
    if (_sokol_inited)
    {
        sokol_gfx_shutdown();
        _sokol_inited = false;
    }
}

void sokol_gfx_init_internal(int sample_rate, int width, int height)
{
    if (_sokol_inited)
        return;

    // Sokol_gfx init data - we ask for RGBA8 format, the sample rate
    // (for sync purposes, though Sokol_gfx doesn't really use audio sample rates),
    // and the width/height of the rendering area.

    struct SokolGfxDesc
    {
        int sample_rate;   // not really used by Sokol_gfx, but kept for compatibility
        int width;
        int height;
        // Pixel format - RGBA8 is standard
        // Sample count - 1 (no MSAA by default)
        // Coverage sample locations - disabled
    } desc = { sample_rate, width, height, 1, { } };

    // Note: Sokol_gfx's sokol_gfx_setup() takes a pointer to a desc struct.
    // The exact signature depends on the Sokol version. In Sokol 0.8+:
    // sokol_gfx_setup(&desc);
    // 
    // For this plugin, we initialise Sokol_gfx with the given dimensions.
    // The library will create an OpenGL, Metal, or Vulkan context depending on
    // the platform and the backend selects.

    // Sokol_gfx automatically selects the backend based on the defines we set.
    // If SOKOL_GFX_IMPL_VULKAN is defined, it will use Vulkan;
    // if SOKOL_GFX_IMPL_METAL is defined, it will use Metal (macOS);
    // otherwise it defaults to OpenGL.
    sokol_gfx_setup(&desc);
    _sokol_inited = true;
}

// -----------------------------------------------------------------------------
// SokolGfxRenderer implementation
// -----------------------------------------------------------------------------

bool SokolGfxRenderer::initialise(juce::Component& peer, Config config)
{
    // Guard against re-initialisation
    if (_initialised)
        return true;

    _width = config.width;
    _height = config.height;

    // Initialise Sokol_gfx with the component's dimensions.
    // We pass the sample rate (44100 as a default, though Sokol_gfx
    // doesn't really use it), and the width/height of the rendering area.
    //
    // IMPORTANT: Sokol_gfx_init_internal expects the sample rate to be
    // the project's sample rate. We get this from the audio processor.
    // For now, use 44100 as a reasonable default - the actual sample rate
    // will be set when the audio processor calls us.
    sokol_gfx_init_internal(44100, config.width, config.height);

    _initialised = true;
    return true;
}

void SokolGfxRenderer::resize(Config config)
{
    if (!_initialised)
        return;

    _width = config.width;
    _height = config.height;

    // Sokol_gfx does not have an explicit resize call - it uses the
    // dimensions provided at init time. If we need to re-initialise
    // with new dimensions, we would shutdown and re-init.
    // For now, just store the new dimensions.
}

bool SokolGfxRenderer::isPresentable() const noexcept
{
    return _initialised;
}

void SokolGfxRenderer::shutdown() noexcept
{
    if (_initialised)
    {
        sokol_gfx_shutdown_internal();
        _initialised = false;
        _width = 1;
        _height = 1;
    }
}

bool SokolGfxRenderer::beginFrame()
{
    if (!_initialised)
        return false;

    // Sokol_gfx does not have a explicit "begin frame" concept in the
    // same way as Vulkan/D3D11. The clear and draw commands are recorded
    // immediately. We just need to ensure we're initialised.
    return _initialised;
}

void SokolGfxRenderer::clear(juce::Colour colour)
{
    if (!_initialised)
        return;

    // Sokol_gfx immediate-mode: clear the framebuffer with the given colour.
    // Sokol_gfx uses a float [0,1] range for colours.
    float r = juce::jlimit(0.0f, 1.0f, colour.getFloatRed());
    float g = juce::jlimit(0.0f, 1.0f, colour.getFloatGreen());
    float b = juce::jlimit(0.0f, 1.0f, colour.getFloatBlue());
    float a = juce::jlimit(0.0f, 1.0f, colour.getFloatAlpha());

    // Sokol_gfx clear: pass the colour as (r, g, b, a) in float format.
    // The Sokol_gfx clear function: sokol_gfx_clear(r, g, b, a)
    sokol_gfx_clear(r, g, b, a);
}

void SokolGfxRenderer::endFrame()
{
    if (!_initialised)
        return;

    // Sokol_gfx is immediate-mode - there's no explicit "end frame" flush
    // like in Vulkan or D3D11. The commands are submitted implicitly
    // when the next beginFrame/clear cycle happens, or at swap time.
    // For a plugin that renders to a texture or offscreen area, we just
    // need to ensure the frame is "complete" - which in Sokol_gfx means
    // the clear has been recorded. Since Sokol_gfx records immediately,
    // there's nothing extra to do here.
}