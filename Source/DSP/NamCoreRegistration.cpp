// =============================================================================
//  The anchor that keeps NAM Core's architectures in the plugin.
//
//  NAM Core registers an architecture from a file-scope static inside that
//  architecture's own translation unit: `_register_WaveNet` in
//  NAM/wavenet/model.cpp, `_register_LSTM` in NAM/lstm.cpp, and so on. Nothing
//  calls an architecture's factory BY NAME - `get_dsp` looks the name up in a
//  registry at run time - so an object file that only carries such a static has
//  NO symbol anyone references. A linker that is handed those objects inside an
//  archive (which is how CMake wires an OBJECT library into JUCE's plugin
//  target: every object goes into libMiBoX_SharedCode.a, and the final
//  VST3/CLAP/AU link scans that archive) therefore pulls none of them, the
//  registry comes up empty, and every `.nam` file is refused at run time with
//  "No config parser registered for architecture: WaveNet" - shown on the panel
//  as INVALID MODEL (BAD SHAPE) for a file that is perfectly valid.
//
//  The harnesses under tests/ link the same objects directly on the command
//  line, so the whole-program link keeps them and the fixtures pass; the plugin
//  is the only build where the pruning happens, and that is why it was the only
//  build that could not load a model.
//
//  This file makes one address per architecture a reference the linker has to
//  resolve. It is deliberately not a call to the factories: they are not ours to
//  call, the registrations are the only thing wanted, and taking the address is
//  enough to make the defining object get pulled. The table stays in this
//  translation unit and is read through a volatile pointer by the anchor, so
//  neither the optimiser nor link-time optimisation can fold the references
//  away - the reads are observable, and the addresses are what it is all for.
// =============================================================================
#include "OptionalLibraries.h"

#if J37_HAS_NAMCORE
    //  Every architecture NAM Core knows, by the header that declares its
    //  factory. The list is the format's, not this plugin's: an architecture
    //  missing here disappears from the registry exactly as if it had never been
    //  vendored, and a .nam of that kind reads as a corrupt file.
    #include <NAM/container.h>
    #include <NAM/convnet.h>
    #include <NAM/linear.h>
    #include <NAM/lstm.h>
    #include <NAM/sequential.h>
    #include <NAM/wavenet/model.h>

    #include <cstddef>
    #include <memory>
#endif

namespace j37dsp
{

#if J37_HAS_NAMCORE
namespace
{
    // The factories share one signature; naming it keeps the table readable and
    // makes a mismatch a compile error rather than a silent narrowing.
    using ConfigFactory = std::unique_ptr<nam::ModelConfig> (*) (const nlohmann::json&, double);
}

//  Nothing outside this file names the table; the anchor below is its one
//  reader. What matters is the relocations INSIDE it - one address per factory,
//  and therefore a reason for the linker to pull the object that defines it.
const ConfigFactory namCoreArchitectureFactories[]
{
    &nam::lstm::create_config,
    &nam::wavenet::create_config,
    &nam::convnet::create_config,
    &nam::linear::create_config,
    &nam::sequential::create_config,
    &nam::container::create_config,
};
#endif

void keepNamCoreArchitecturesLinked() noexcept
{
#if J37_HAS_NAMCORE
    //  Volatile, and every entry read: the VALUE is never used, and that is the
    //  point - the reads are what hold each factory's address (and therefore its
    //  translation unit) in the program. A plain loop would be dead code and
    //  would be removed, references and all.
    const volatile ConfigFactory* factories = namCoreArchitectureFactories;
    constexpr std::size_t count = sizeof (namCoreArchitectureFactories)
                                / sizeof (namCoreArchitectureFactories[0]);

    for (std::size_t i = 0; i < count; ++i)
        (void) factories[i];
#endif
}

} // namespace j37dsp
