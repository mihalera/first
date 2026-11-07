// =============================================================================
//  The effects rack: twenty-five creative stages between the machine and the
//  protection chain.
//
//  WHERE IT SITS
//
//  The rack is called from the engine's per-sample loop, after the transient
//  shaper and before the conventional compressor, at the ENGINE rate - inside
//  the oversampled region, so the stages that make harmonics (the overdrive, the
//  fuzz, the folder, the two crushers) are already running above the session's
//  bandwidth before their own antialiasing has to do anything. That is the same
//  argument the record head's own curvature is placed under, and it is why this
//  is a per-sample stage rather than one of the block stages at the end of the
//  chain: a limiter that has already run cannot undo a fuzz.
//
//  ORDER
//
//  The order is a pedalboard's, not a list: the dynamics and the pitch stages sit
//  in front of the modulation, and the modulation in front of the time stages,
//  because that is the order in which each stage's decisions still mean something
//  - a gate in front of a delay can close between notes, a gate behind it can
//  only chop the tail.
//
//      wah -> anti-spike -> gate -> overdrive -> fuzz -> folder ->
//      bitcrusher -> rate crusher -> ring mod -> octaver -> pitch ->
//      comb -> formant -> dynamic EQ -> de-esser -> chorus -> flanger ->
//      phaser -> tremolo -> vibrato -> rotary -> vocoder -> granular ->
//      spectral gate -> speed
//
//  THE CONTROL
//
//  Each stage has ONE control, its AMOUNT, and zero means the stage is out of
//  the path - not "bypassed in code", but not called at all, so an untouched
//  rack is bit-for-bit the signal it was handed. The amounts arrive at BLOCK
//  rate (the signal path reads its parameters once per block, like every other
//  control it owns) and are smoothed here at audio rate, so a host writing a
//  parameter between two blocks cannot step the gain of a fuzz.
//
//  The one exception is SPEED, which is a rate and not an amount: its middle is
//  1x, so it is out of the path at the middle rather than at zero, and it reads
//  its target directly. See the note at its call site.
// =============================================================================
#pragma once

#include "Fx/FxDrive.h"
#include "Fx/FxDynamics.h"
#include "Fx/FxFilter.h"
#include "Fx/FxModulation.h"
#include "Fx/FxPitch.h"
#include "Fx/FxPrimitives.h"

#include <algorithm>
#include <cmath>

namespace j37dsp
{

/** The twenty-five amounts, as the signal path reads them. A plain struct rather
    than an array so a stage added here cannot silently take another's control. */
struct FxTargets
{
    float wah = 0.0f;
    float antiSpike = 0.0f;
    float gate = 0.0f;
    float overdrive = 0.0f;
    float fuzz = 0.0f;
    float wavefolder = 0.0f;
    float bitcrusher = 0.0f;
    float rateCrusher = 0.0f;
    float ringMod = 0.0f;
    float octaver = 0.0f;
    float pitch = 0.0f;
    float comb = 0.0f;
    float formant = 0.0f;
    float dynamicEq = 0.0f;
    float deEsser = 0.0f;
    float chorus = 0.0f;
    float flanger = 0.0f;
    float phaser = 0.0f;
    float tremolo = 0.0f;
    float vibrato = 0.0f;
    float rotary = 0.0f;
    float vocoder = 0.0f;
    float granular = 0.0f;
    float spectral = 0.0f;
    float speed = 0.0f;
};

class FxRack
{
public:
    /** The smoothed amounts. Declared before its uses because a member
        function's return type is looked up where it stands, not at the end of
        the class. */
    struct Amounts
    {
        float wah = 0.0f, antiSpike = 0.0f, gate = 0.0f, overdrive = 0.0f, fuzz = 0.0f;
        float wavefolder = 0.0f, bitcrusher = 0.0f, rateCrusher = 0.0f, ringMod = 0.0f;
        float octaver = 0.0f, pitch = 0.0f, comb = 0.0f, formant = 0.0f, dynamicEq = 0.0f;
        float deEsser = 0.0f, chorus = 0.0f, flanger = 0.0f, phaser = 0.0f, tremolo = 0.0f;
        float vibrato = 0.0f, rotary = 0.0f, vocoder = 0.0f, granular = 0.0f, spectral = 0.0f;
    };

    /** Called from prepareToPlay with the ENGINE rate - session rate times the
        oversampling factor - because that is the rate the loop runs at. */
    void prepare (double engineSampleRate)
    {
        rate = engineSampleRate > 0.0 ? engineSampleRate : 48000.0;

        wah.prepare (rate);
        antiSpike.prepare (rate);
        gate.prepare (rate);
        overdrive.prepare (rate);
        fuzz.prepare (rate);
        wavefolder.prepare (rate);
        bitcrusher.prepare (rate);
        rateCrusher.prepare (rate);
        ringModulator.prepare (rate);
        octaver.prepare (rate);
        pitchShifter.prepare (rate);
        comb.prepare (rate);
        formant.prepare (rate);
        dynamicEqualiser.prepare (rate);
        deEsser.prepare (rate);
        chorus.prepare (rate);
        flanger.prepare (rate);
        phaser.prepare (rate);
        tremolo.prepare (rate);
        vibrato.prepare (rate);
        rotary.prepare (rate);
        vocoder.prepare (rate);
        granular.prepare (rate);
        spectralGate.prepare (rate);
        speed.prepare (rate);

        // 15 ms of smoothing: fast enough that a knob feels immediate, slow
        // enough that a host's parameter step is a fade rather than a click.
        smoothingCoefficient = 1.0f - std::exp (-1.0f
                                                / (0.015f * static_cast<float> (rate)));

        reset();
    }

    void reset()
    {
        wah.reset();
        antiSpike.reset();
        gate.reset();
        overdrive.reset();
        fuzz.reset();
        wavefolder.reset();
        bitcrusher.reset();
        rateCrusher.reset();
        ringModulator.reset();
        octaver.reset();
        pitchShifter.reset();
        comb.reset();
        formant.reset();
        dynamicEqualiser.reset();
        deEsser.reset();
        chorus.reset();
        flanger.reset();
        phaser.reset();
        tremolo.reset();
        vibrato.reset();
        rotary.reset();
        vocoder.reset();
        granular.reset();
        spectralGate.reset();
        speed.reset();

        amounts = Amounts {};
    }

    /** Block rate: what the parameters say now. */
    void setTargets (const FxTargets& targets) noexcept { this->targets = targets; }

    /** The smoothed amounts, for the panel and for the harness. */
    const Amounts& currentAmounts() const noexcept { return amounts; }

    /** Audio rate: one frame. Both channels, because the chorus, the rotary and
        the vocoder put the two sides out of step. */
    void processSample (float& left, float& right) noexcept
    {
        step (amounts.wah, targets.wah);
        step (amounts.antiSpike, targets.antiSpike);
        step (amounts.gate, targets.gate);
        step (amounts.overdrive, targets.overdrive);
        step (amounts.fuzz, targets.fuzz);
        step (amounts.wavefolder, targets.wavefolder);
        step (amounts.bitcrusher, targets.bitcrusher);
        step (amounts.rateCrusher, targets.rateCrusher);
        step (amounts.ringMod, targets.ringMod);
        step (amounts.octaver, targets.octaver);
        step (amounts.pitch, targets.pitch);
        step (amounts.comb, targets.comb);
        step (amounts.formant, targets.formant);
        step (amounts.dynamicEq, targets.dynamicEq);
        step (amounts.deEsser, targets.deEsser);
        step (amounts.chorus, targets.chorus);
        step (amounts.flanger, targets.flanger);
        step (amounts.phaser, targets.phaser);
        step (amounts.tremolo, targets.tremolo);
        step (amounts.vibrato, targets.vibrato);
        step (amounts.rotary, targets.rotary);
        step (amounts.vocoder, targets.vocoder);
        step (amounts.granular, targets.granular);
        step (amounts.spectral, targets.spectral);

        if (amounts.wah > threshold)             wah.process (left, right, amounts.wah);
        if (amounts.antiSpike > threshold)       antiSpike.process (left, right, amounts.antiSpike);
        if (amounts.gate > threshold)            gate.process (left, right, amounts.gate);
        if (amounts.overdrive > threshold)       overdrive.process (left, right, amounts.overdrive);
        if (amounts.fuzz > threshold)            fuzz.process (left, right, amounts.fuzz);
        if (amounts.wavefolder > threshold)      wavefolder.process (left, right, amounts.wavefolder);
        if (amounts.bitcrusher > threshold)      bitcrusher.process (left, right, amounts.bitcrusher);
        if (amounts.rateCrusher > threshold)     rateCrusher.process (left, right, amounts.rateCrusher);
        if (amounts.ringMod > threshold)         ringModulator.process (left, right, amounts.ringMod);
        if (amounts.octaver > threshold)         octaver.process (left, right, amounts.octaver);

        if (amounts.pitch > threshold)
        {
            pitchShifter.setSemitones (amounts.pitch * 12.0f);
            pitchShifter.process (left, right);
        }

        if (amounts.comb > threshold)            comb.process (left, right, amounts.comb);
        if (amounts.formant > threshold)         formant.process (left, right, amounts.formant);
        if (amounts.dynamicEq > threshold)       dynamicEqualiser.process (left, right, amounts.dynamicEq);
        if (amounts.deEsser > threshold)         deEsser.process (left, right, amounts.deEsser);
        if (amounts.chorus > threshold)          chorus.process (left, right, amounts.chorus);
        if (amounts.flanger > threshold)         flanger.process (left, right, amounts.flanger);
        if (amounts.phaser > threshold)          phaser.process (left, right, amounts.phaser);
        if (amounts.tremolo > threshold)         tremolo.process (left, right, amounts.tremolo);
        if (amounts.vibrato > threshold)         vibrato.process (left, right, amounts.vibrato);
        if (amounts.rotary > threshold)          rotary.process (left, right, amounts.rotary);
        if (amounts.vocoder > threshold)         vocoder.process (left, right, amounts.vocoder);
        if (amounts.granular > threshold)        granular.process (left, right, amounts.granular);
        if (amounts.spectral > threshold)        spectralGate.process (left, right, amounts.spectral);

        // SPEED is the one control on this panel that is a RATE, not an amount:
        // its middle is the identity (1x) and its zero is half speed, not
        // silence. So it is read from the target rather than from a smoothed
        // amount - a tape does not fade between two speeds, it jumps, and the
        // splice is the sound of the knob - and it is called whenever the switch
        // sits away from the middle, including at zero.
        if (std::abs (targets.speed - 0.5f) > threshold)
            speed.process (left, right, targets.speed);
    }

    /** True while any stage is in the path. The signal path uses it to skip the
        loop's own bookkeeping when the rack is untouched. */
    bool isActive() const noexcept
    {
        return amounts.wah > threshold || amounts.antiSpike > threshold
            || amounts.gate > threshold || amounts.overdrive > threshold
            || amounts.fuzz > threshold || amounts.wavefolder > threshold
            || amounts.bitcrusher > threshold || amounts.rateCrusher > threshold
            || amounts.ringMod > threshold || amounts.octaver > threshold
            || amounts.pitch > threshold || amounts.comb > threshold
            || amounts.formant > threshold || amounts.dynamicEq > threshold
            || amounts.deEsser > threshold || amounts.chorus > threshold
            || amounts.flanger > threshold || amounts.phaser > threshold
            || amounts.tremolo > threshold || amounts.vibrato > threshold
            || amounts.rotary > threshold || amounts.vocoder > threshold
            || amounts.granular > threshold || amounts.spectral > threshold
            || std::abs (targets.speed - 0.5f) > threshold;
    }

private:
    static constexpr float threshold = 1.0e-4f;

    void step (float& value, float target) noexcept
    {
        value += (target - value) * smoothingCoefficient;

        if (value < threshold)
            value = 0.0f;
    }

    j37dsp::fx::EnvelopeWah wah;
    j37dsp::fx::AntiSpike antiSpike;
    j37dsp::fx::Gate gate;
    j37dsp::fx::Overdrive overdrive;
    j37dsp::fx::Fuzz fuzz;
    j37dsp::fx::Wavefolder wavefolder;
    j37dsp::fx::Bitcrusher bitcrusher;
    j37dsp::fx::RateCrusher rateCrusher;
    j37dsp::fx::RingModulator ringModulator;
    j37dsp::fx::Octaver octaver;
    j37dsp::fx::PitchShifter pitchShifter;
    j37dsp::fx::CombFilter comb;
    j37dsp::fx::FormantFilter formant;
    j37dsp::fx::DynamicEqualizer dynamicEqualiser;
    j37dsp::fx::DeEsser deEsser;
    j37dsp::fx::Chorus chorus;
    j37dsp::fx::Flanger flanger;
    j37dsp::fx::Phaser phaser;
    j37dsp::fx::Tremolo tremolo;
    j37dsp::fx::Vibrato vibrato;
    j37dsp::fx::Rotary rotary;
    j37dsp::fx::Vocoder vocoder;
    j37dsp::fx::Granular granular;
    j37dsp::fx::SpectralGate spectralGate;
    j37dsp::fx::Speed speed;

    FxTargets targets;
    Amounts amounts;
    double rate = 48000.0;
    float smoothingCoefficient = 0.001f;
};

} // namespace j37dsp
