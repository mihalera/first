// =============================================================================
//  The tape formula and the instrument selector's voicing: what the TONE macro's
//  crossfade produces, what the TAPE TYPE switch biases on top of it, what the
//  INSTRUMENT selector adds, and the clamps that bound the pair.
//
//  This is a stage taken out of processTapeEngine() (see step F in
//  SPLITTING-BIG-FILES.md), and it is a PURE function of three numbers - the TONE
//  macro, the TAPE TYPE choice and the INSTRUMENT choice - so a harness can call it
//  without an engine, a parameter tree or a block. Every claim its prose makes is
//  checkable and is checked: the crossfade runs from the slow stock to the fast one,
//  TAPE TYPE = OFF removes the medium (no hiss, no damping, no asymmetry), the hotter
//  formulas bend harder and hiss less, and every figure lands inside the machine's
//  designed range.
//
//  The conventions of the directory apply, and they are stated in J37Math.h: one
//  concern per header, the header includes what it needs, and it does not pull JUCE
//  in. It names `juce::jlimit`, which its includer has already declared - inside the
//  plugin that is JuceHeader.h, under the harness it is tests/dsp/shim.h.
//
//  WHAT DID NOT TRAVEL. The switch's own side effect on the saturation cores
//  (`saturationL/R.tapeOff`) stays in processTapeEngine(): the cores are the
//  processor's members, not part of what this stage computes, and a header that
//  reached into the plugin's state could not be called by a harness. The processor
//  reads the same `tapeType == modelOffIndex` test for it, and the static_assert at
//  the call site pins the off index here to the one the tape table uses.
//
//  The prose came with the code, out of Source/ProcessorSignalPath.cpp by matching
//  its exact text. What changed is where each figure is written - fields of the
//  struct rather than the processor's own locals - and nothing else.
// =============================================================================
#pragma once

#include <cmath>

namespace j37voice
{

/** The tape stock table's OFF entry, mirrored from PluginProcessor.h's own
    `modelOffIndex` (which is `tapeStockCount - 1`). It is repeated here because a
    header under Source/DSP/ cannot include the processor's header, and a call-site
    static_assert is what keeps the two from drifting: see TapeVoicing's note. */
inline constexpr int tapeModelOffIndex = 12;

/** One block's worth of the tape medium's character: the saturation curve, the bias
    asymmetry, the noise floor, the head-gap damping corner in Hz, the magnetic
    hysteresis thickness, and the instrument selector's scale on the transport. */
struct TapeVoicing
{
    float tapeCurve = 1.0f;
    float tapeAsymmetry = 0.0f;
    float tapeHiss = 0.0f;
    float headDampingHz = 0.0f;
    float hysteresis = 0.0f;

    // How loose the transport runs under the INSTRUMENT selector: 1.0 is the
    // calibrated machine, below it is steadier (bass, piano, drums) and above it a
    // slightly loose vintage deck (guitar).
    float instrumentTransportScale = 1.0f;
};

/** Builds a block's tape voicing from the TONE macro, the formula and the instrument
    choice. */
inline TapeVoicing makeTapeVoicing (float character, int tapeType, int instrument)
{
    TapeVoicing voicing;

    // The TONE macro's blend curve: 0 % reads as the classic slow stock, 100 % as
    // the hot fast stock.
    const auto characterCurve = std::pow (juce::jlimit (0.0f, 1.0f, character), 1.20f);

    // Per-model tape character: saturation curve, bias asymmetry, tape noise floor,
    // head-gap damping and the magnetic hysteresis thickness. The damping is a real
    // corner FREQUENCY in Hz (the previous ms values landed at 4-9 Hz through the
    // time-constant filter - the same 2*pi unit error that hid the whole wet path).
    //
    // TONE (the character macro) is a crossfade BETWEEN MACHINE STATES, and the tape
    // formula is part of that state: at 0 % the machine behaves like the classic slow
    // stock (gentle curve, strong asymmetry, warm hiss, dark damping, thick hysteresis
    // blend) and at 100 % like the fast/hot stock (harder curve, less asymmetry, less
    // hiss, more open damping, leaner hysteresis). The selected TAPE TYPE shifts the
    // centre of that fade, so the knob still has its own meaning on top of the macro.
    // The slow-machine end is represented by the J37 numbers and the fast end by a
    // hotter generic stock, blended by the TONE curve.
    const float slowMachineCurve = 1.18f;      // J37 - soft magnetic bend
    const float slowMachineAsymmetry = 0.16f;  // strong even-harmonic warmth
    const float slowMachineHiss = 0.10f;       // quiet oxide floor (below audibility)
    const float slowMachineDampingHz = 12000.0f;
    const float slowMachineHysteresis = 0.30f;

    const float fastMachineCurve = 1.44f;      // hot-stud style dense saturation
    const float fastMachineAsymmetry = 0.10f;  // leaner, more symmetric bend
    const float fastMachineHiss = 0.06f;       // quieter still, faster stock
    const float fastMachineDampingHz = 20000.0f;
    const float fastMachineHysteresis = 0.44f;

    const auto machineBlend = juce::jlimit (0.0f, 1.0f, characterCurve);
    const auto blend = [machineBlend] (float slow, float fast)
    {
        return slow + (fast - slow) * machineBlend;
    };

    voicing.tapeCurve = blend (slowMachineCurve, fastMachineCurve);
    float tapeAsymmetry = blend (slowMachineAsymmetry, fastMachineAsymmetry);
    voicing.tapeHiss = blend (slowMachineHiss, fastMachineHiss);
    float headDampingHz = blend (slowMachineDampingHz, fastMachineDampingHz);
    float hysteresis = blend (slowMachineHysteresis, fastMachineHysteresis);

    // The TAPE TYPE switch keeps its own voice on top of the TONE macro: it biases the
    // blended state toward that formula's character (hotter formulas bend harder and
    // hiss less, the classic J37 stays soft) rather than replacing it.
    //
    // This switch is the third copy of the stock list and the only one the compiler
    // cannot check: C++ has no way to count case labels, so nothing here ties the
    // cases to tapeStockNames. The call site asserts the choice is in range against
    // tapeStockCount, and its static_assert pins the OFF index here to the table's, so
    // a stock added to the table and forgotten below is caught by that pin rather than
    // sounding like stock 0.
    // -------------------------------------------------------------------------
    //  TAPE TYPE = OFF: the magnetic medium is removed.
    //
    //  What is left is a solid-state amplifier: no hysteresis, no oxide floor,
    //  no head damping, no bias asymmetry - the machine's own electronics with
    //  no tape in it. It is the reference state for hearing what the MEDIUM
    //  contributes, because everything else in the chain is untouched by it.
    //
    //  It is implemented by (a) telling the saturation core that the tape
    //  principle is absent, so the blend redistributes its share among the
    //  principles still present, and (b) skipping the stock voicing switch
    //  entirely, which is what the early flag below does. BOTH are needed:
    //  the core alone would still have the stock's hiss and head damping riding
    //  on top of a tape-less blend.
    // -------------------------------------------------------------------------
    const bool tapeModelOff = (tapeType == tapeModelOffIndex);

    if (tapeModelOff)
    {
        // A solid-state machine: no medium, so no medium noise and no head-gap
        // damping. The transport still runs - the electronics are still moving
        // the tape past a head - so wow and flutter stay, which is exactly the
        // distinction that makes this a useful reference rather than a bypass.
        voicing.tapeHiss = 0.0f;
        headDampingHz = 26000.0f;
        hysteresis = 0.10f;
        tapeAsymmetry = 0.0f;
    }
    else
    switch (tapeType)
    {
        case 0: // J37 - the classic EMI reference sound
            break;
        case 1: // Ampex 456 - hotter, more low-order colour
            voicing.tapeCurve += 0.08f;
            tapeAsymmetry += 0.05f;
            voicing.tapeHiss += 0.03f;
            headDampingHz += 2500.0f;
            hysteresis += 0.06f;
            break;
        case 2: // Studer A800 - darkest, densest saturation
            voicing.tapeCurve += 0.16f;
            tapeAsymmetry += 0.08f;
            voicing.tapeHiss += 0.06f;
            headDampingHz += 5000.0f;
            hysteresis += 0.12f;
            break;
        case 3: // Chrome - clean and bright, low noise
            voicing.tapeCurve += 0.02f;
            tapeAsymmetry -= 0.03f;
            voicing.tapeHiss -= 0.02f;
            headDampingHz -= 2500.0f;
            hysteresis -= 0.04f;
            break;
        case 4: // Type 111 - gentle low-noise mastering stock, very quiet, soft top
            voicing.tapeCurve -= 0.05f;
            tapeAsymmetry -= 0.02f;
            voicing.tapeHiss -= 0.045f;
            headDampingHz -= 4000.0f;
            hysteresis -= 0.05f;
            break;
        case 5: // GP9 - hot modern mastering formula: dense low end, higher floor
            voicing.tapeCurve += 0.14f;
            tapeAsymmetry += 0.07f;
            voicing.tapeHiss += 0.075f;
            headDampingHz += 6000.0f;
            hysteresis += 0.11f;
            break;
        case 6: // Quantegy 499 - high-output studio workhorse: open top, firm glue
            voicing.tapeCurve += 0.10f;
            tapeAsymmetry += 0.04f;
            voicing.tapeHiss += 0.045f;
            headDampingHz += 8000.0f;
            hysteresis += 0.08f;
            break;
        case 7: // RTM SM911 - broadcast reference: balanced, smooth, low noise
            voicing.tapeCurve += 0.05f;
            tapeAsymmetry += 0.02f;
            voicing.tapeHiss -= 0.01f;
            headDampingHz += 3500.0f;
            hysteresis += 0.03f;
            break;
        case 8: // SM 468 - high-output low-noise studio stock. A firm bend with a
                // notably quiet floor and an open head, so it reads as clean density
                // rather than as colour.
            voicing.tapeCurve += 0.09f;
            tapeAsymmetry += 0.03f;
            voicing.tapeHiss -= 0.055f;
            headDampingHz += 7000.0f;
            hysteresis += 0.04f;
            break;
        case 9: // 888 - the hot, thick vintage stock. Strong bias asymmetry and the
                // thickest magnetic memory here, so it bends early and blooms hard:
                // the formula to reach for when the saturation IS the effect.
            voicing.tapeCurve += 0.19f;
            tapeAsymmetry += 0.10f;
            voicing.tapeHiss += 0.05f;
            headDampingHz -= 1500.0f;
            hysteresis += 0.14f;
            break;
        case 10: // 815 - dark, dense and quiet at the top. A soft head and a heavy
                 // low-mid bias make it the warmest of the new stocks without the
                 // extra hiss 888 brings: it thickens rather than drives.
            voicing.tapeCurve += 0.13f;
            tapeAsymmetry += 0.08f;
            voicing.tapeHiss -= 0.02f;
            headDampingHz -= 4500.0f;
            hysteresis += 0.12f;
            break;
        case 11: // 811 - the clean, open, low-noise mastering stock. The gentlest bend
                 // of the set with the most open head, so it stays transparent under
                 // level and keeps the top octave: a bus stock, not a colour.
            voicing.tapeCurve -= 0.02f;
            tapeAsymmetry += 0.01f;
            voicing.tapeHiss -= 0.05f;
            headDampingHz += 9000.0f;
            hysteresis -= 0.03f;
            break;
        default:
            // Unreachable while the call site's jassert and the OFF-index pin hold:
            // the choice parameter is clamped to tapeStockCount and the OFF entry is
            // handled above. Present so the switch is exhaustive and a future entry
            // cannot silently do nothing.
            break;
    }

    // The INSTRUMENT selector's voicing rides on top of the TAPE TYPE bias, and the
    // clamps below bound every figure, so any combination of the two selectors stays
    // inside the machine's designed operating range.
    switch (instrument)
    {
        case 1: // Vocal: gentler bend, more even warmth, quiet floor, steady transport
            voicing.tapeCurve -= 0.10f;
            tapeAsymmetry += 0.04f;
            voicing.tapeHiss -= 0.03f;
            headDampingHz -= 1000.0f;
            hysteresis += 0.05f;
            voicing.instrumentTransportScale = 0.80f;
            break;
        case 2: // Bass: the thickest bend and memory, the darkest head, the steadiest
                // transport - pitch wobble under a bass note is heard at once
            voicing.tapeCurve += 0.12f;
            tapeAsymmetry += 0.06f;
            voicing.tapeHiss += 0.02f;
            headDampingHz -= 2500.0f;
            hysteresis += 0.10f;
            voicing.instrumentTransportScale = 0.65f;
            break;
        case 3: // Guitar: cleaner bend, more bite, a slightly loose vintage transport
            voicing.tapeCurve -= 0.06f;
            tapeAsymmetry -= 0.02f;
            voicing.tapeHiss -= 0.01f;
            headDampingHz += 1500.0f;
            hysteresis -= 0.04f;
            voicing.instrumentTransportScale = 1.15f;
            break;
        case 4: // Piano: the cleanest bend so transients and decay stay honest, the
                // quietest floor, top just nudged open
            voicing.tapeCurve -= 0.12f;
            tapeAsymmetry += 0.02f;
            voicing.tapeHiss -= 0.04f;
            headDampingHz += 500.0f;
            hysteresis -= 0.06f;
            voicing.instrumentTransportScale = 0.85f;
            break;
        case 5: // Drums: the slam calibration - a harder bend and an open head for
                // the crack, tight magnetic memory so transients do not smear, and
                // a steady transport (wow under a kick drum reads as a fault)
            voicing.tapeCurve += 0.10f;
            tapeAsymmetry -= 0.04f;
            voicing.tapeHiss += 0.01f;
            headDampingHz += 3000.0f;
            hysteresis -= 0.08f;
            voicing.instrumentTransportScale = 0.90f;
            break;
        case 0: // Master Bus: the machine exactly as calibrated
        default:
            break;
    }

    voicing.tapeCurve = juce::jlimit (1.0f, 1.8f, voicing.tapeCurve);
    voicing.tapeAsymmetry = juce::jlimit (0.0f, 0.4f, tapeAsymmetry);
    voicing.tapeHiss = juce::jlimit (0.0f, 0.6f, voicing.tapeHiss);
    voicing.headDampingHz = juce::jlimit (4000.0f, 26000.0f, headDampingHz);
    voicing.hysteresis = juce::jlimit (0.1f, 0.7f, hysteresis);

    return voicing;
}

} // namespace j37voice
