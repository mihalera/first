#include "GUI/RenderBackend.h"

using j37::render::Backend;

static_assert (j37::render::allBackends.size() == 9);
static_assert (j37::render::backendChoiceId (Backend::autoDetect) == 1);
static_assert (j37::render::backendChoiceId (Backend::openGL) == 2);
static_assert (j37::render::backendChoiceId (Backend::vulkan) == 3);
static_assert (j37::render::backendChoiceId (Backend::metal) == 4);
static_assert (j37::render::backendChoiceId (Backend::directX11) == 5);
static_assert (j37::render::backendChoiceId (Backend::directX12) == 6);
static_assert (j37::render::backendChoiceId (Backend::sokol) == 7);
static_assert (j37::render::backendChoiceId (Backend::bgfx) == 8);
static_assert (j37::render::backendChoiceId (Backend::cpu) == 9);

static_assert (j37::render::backendFromChoiceId (1) == Backend::autoDetect);
static_assert (j37::render::backendFromChoiceId (2) == Backend::openGL);
static_assert (j37::render::backendFromChoiceId (3) == Backend::vulkan);
static_assert (j37::render::backendFromChoiceId (4) == Backend::metal);
static_assert (j37::render::backendFromChoiceId (5) == Backend::directX11);
static_assert (j37::render::backendFromChoiceId (6) == Backend::directX12);
static_assert (j37::render::backendFromChoiceId (7) == Backend::sokol);
static_assert (j37::render::backendFromChoiceId (8) == Backend::bgfx);
static_assert (j37::render::backendFromChoiceId (9) == Backend::cpu);
static_assert (j37::render::backendFromChoiceId (0) == Backend::autoDetect);
static_assert (j37::render::backendFromChoiceId (10) == Backend::autoDetect);
static_assert (j37::render::backendFromChoiceId (-1) == Backend::autoDetect);

int main()
{
    if (! j37::render::isBackendSelectable (Backend::autoDetect)
        || ! j37::render::isBackendSelectable (Backend::cpu)
        || j37::render::runningBackend (Backend::cpu) != Backend::cpu)
        return 1;

#if defined (J37_RENDER_BACKEND_SOKOL) && defined (J37_NATIVE_SOKOL)
    if (j37::render::configuredBackend() != Backend::sokol
        || j37::render::effectiveBackend() != Backend::sokol
        || ! j37::render::isBackendSelectable (Backend::sokol)
        || j37::render::runningBackend (Backend::sokol) != Backend::sokol
        || j37::render::runningBackend (Backend::autoDetect) != Backend::sokol)
        return 2;
    constexpr auto expectedNative = Backend::sokol;
#elif defined (J37_RENDER_BACKEND_OPENGL)
    if (j37::render::configuredBackend() != Backend::openGL
        || j37::render::effectiveBackend() != Backend::openGL
        || ! j37::render::isBackendSelectable (Backend::openGL)
        || j37::render::runningBackend (Backend::openGL) != Backend::openGL)
        return 3;
    constexpr auto expectedNative = Backend::openGL;
#else
    if (j37::render::configuredBackend() != Backend::autoDetect
        || j37::render::effectiveBackend() != Backend::openGL
        || ! j37::render::isBackendSelectable (Backend::openGL)
        || j37::render::runningBackend (Backend::autoDetect) != Backend::openGL)
        return 4;
    constexpr auto expectedNative = Backend::openGL;
#endif

    for (const auto backend : j37::render::allBackends)
    {
        const auto expected = backend == Backend::autoDetect
                           || backend == Backend::cpu
                           || backend == expectedNative;

        if (j37::render::isBackendSelectable (backend) != expected
            || j37::render::backendFromChoiceId (
                   j37::render::backendChoiceId (backend)) != backend)
            return 5;
    }

    return 0;
}
