#include "NativeRenderer_Metal.h"

#if defined (J37_NATIVE_METAL) && JUCE_MAC
namespace j37::render
{
namespace
{
/** Metal is staged until a native child view clipped to the editor is available.
    Never attach a drawable to the DAW-owned ComponentPeer. */
class MetalRenderer final : public NativeRenderer
{
public:
    bool initialise (juce::Component& component, Config config) override
    {
        juce::ignoreUnused (component, config);
        failure = "Metal: editor-scoped native surface unavailable";
        initialised = false;
        return false;
    }
    void resize (Config config) override { juce::ignoreUnused (config); }
    bool isPresentable() const noexcept override { return false; }
    void shutdown() noexcept override { initialised = false; }
    bool beginFrame() override { return false; }
    void clear (juce::Colour colour) override { juce::ignoreUnused (colour); }
    bool drawScene (const SceneFrame&) override { return false; }
    void endFrame() override {}
    bool isInitialised() const noexcept override { return initialised; }
    juce::String status() const override { return failure; }
private:
    juce::String failure;
    bool initialised = false;
};
}
std::unique_ptr<NativeRenderer> createMetalRenderer()
{
    return std::make_unique<MetalRenderer>();
}
}
#else
namespace j37::render
{
std::unique_ptr<NativeRenderer> createMetalRenderer() { return {}; }
}
#endif
