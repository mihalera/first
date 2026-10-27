// =============================================================================
//  The two glue compressors' operating points: where each stage's threshold, knee,
//  ratio and reduction limit sit, the attack and release the tape formula and the
//  SPEED control stretch them to, the block-rate detector coefficients built from
//  them, and the makeup each stage pays its reduction back with.
//
//  This is a stage taken out of processTapeEngine() (see step F in
//  SPLITTING-BIG-FILES.md), and it is a PURE function of the two trim controls, the
//  tape formula and the SPEED scale: a harness can call it without an engine, a
//  parameter tree or a block. Its prose is checkable and is checked - the two trims
//  move every figure the way their names say, a hotter formula tightens the glue, a
//  faster transport shortens it, and the makeup grows with the trim that feeds it.
//
//  The conventions of the directory apply, and they are stated in J37Math.h: one
//  concern per header, the header includes what it needs, and it does not pull JUCE
//  in. It names `juce::jlimit` (declared by its includer) and the shipping
//  GlueCompressor, whose coefficients it builds, so tests/dsp compiles the shipping
//  stage rather than a copy of it.
//
//  The prose came with the code, out of Source/ProcessorSignalPath.cpp by matching
//  its exact text. What changed is where each figure is written - fields of the
//  struct rather than the processor's own members - and nothing else.
// =============================================================================
#pragma once

#include "DSP/GlueCompressor.h"

namespace j37glueop
{

/** One block's worth of both glue stages' calibration. */
struct GlueOperatingPoints
{
    // The input stage: its threshold, knee and ratio, and how far its reduction is
    // allowed to go.
    float inputThresholdDb = 0.0f;
    float inputKneeDb = 0.0f;
    float inputCompressorRatio = 1.0f;
    float inputReductionLimitDb = 0.0f;

    // The output stage, calibrated the same way from the OUTPUT trim.
    float outputThresholdDb = 0.0f;
    float outputKneeDb = 0.0f;
    float outputCompressorRatio = 1.0f;
    float outputReductionLimitDb = 0.0f;

    // Makeup is part of each stage, and it is driven by the same trim control: a
    // boosted trim pays its reduction back and a trimmed-down one simply backs off,
    // so neither stage can quietly undo the balance the user dialled in.
    float inputMakeupFraction = 0.0f;
    float outputMakeupFraction = 0.0f;

    // The detectors' block-rate coefficients: everything they depend on is fixed for
    // the whole block, so they are built once rather than recomputed per sample.
    GlueCompressor::Coefficients inputCoefficients;
    GlueCompressor::Coefficients outputCoefficients;
};

/** Builds both stages' calibration from the trims, the tape formula, the SPEED scale
    and the engine rate. */
inline GlueOperatingPoints makeGlueOperatingPoints (float inputDb, float outputDb, int tapeType,
                                                    float speedScale, float engineSampleRate)
{
    GlueOperatingPoints points;

    // -------------------------------------------------------------------------
    //  Glue compressor operating points.
    //
    //  The two stages are independent processors, but each one is calibrated from
    //  the trim control that feeds it. Turning INPUT up pushes this scaled signal
    //  into the input stage, so that stage clamps down sooner and more firmly;
    //  OUTPUT works the same way on the output stage, which then spends the trimmed
    //  level on the output trim. A negative trim backs a stage off completely and a
    //  positive one leans on it hard, which is what makes the two controls feel
    //  like real gain staging rather than plain volume.
    // -------------------------------------------------------------------------
    const float inputDriveLoad = juce::jlimit (0.0f, 1.0f, (inputDb + 24.0f) / 48.0f);
    const float outputDriveLoad = juce::jlimit (0.0f, 1.0f, (outputDb + 24.0f) / 48.0f);

    points.inputThresholdDb = -17.0f + inputDriveLoad * 14.0f;    // -17 .. -3 dB
    points.inputKneeDb = 10.0f - inputDriveLoad * 3.0f;           // 10 .. 7 dB
    points.inputCompressorRatio = 1.15f + inputDriveLoad * 0.25f; // 1.15 .. 1.40 : 1
    points.inputReductionLimitDb = -(2.0f + inputDriveLoad * 5.0f);

    points.outputThresholdDb = -18.0f + outputDriveLoad * 15.0f;  // -18 .. -3 dB
    points.outputKneeDb = 7.0f - outputDriveLoad * 2.0f;          // 7 .. 5 dB
    points.outputCompressorRatio = 1.16f + outputDriveLoad * 0.18f;
    points.outputReductionLimitDb = -(2.5f + outputDriveLoad * 5.0f);

    // -----------------------------------------------------------------------
    //  Tape-stock and transport coupling into the glue time constants.
    //
    //  The compressors are part of the machine, so the FORMULA loaded on it and
    //  the SPEED it runs at change how the glue stages move - not just how the
    //  tape saturates:
    //
    //    Tape formula (0 = J37 ... 3 = Chrome). Oxide thickness and bias current
    //    set how quickly the detector can follow the programme: the soft, low-
    //    output formulas get slower constants (more head bump, more relaxed
    //    glue), the hot and chrome formulas get faster, tighter ones.
    //
    //    Transport speed. A slow 7.5 ips pass has more print-through and a
    //    lazier flux build-up, so the glue is stretched; 30 ips is tight and
    //    immediate, so the constants shorten. This rides the same speedScale
    //    used for the head damping, so SPEED keeps one coherent meaning across
    //    the whole machine.
    //
    //  Attack multipliers land roughly in 0.72..1.33, release in 0.72..1.40.
    //  Both stages share the multipliers, so the two stages still feel like one
    //  machine while remaining independent processors.
    // -----------------------------------------------------------------------
    // Eight formulas now index this scale: type 0 (J37, softest glue) through
    // type 7 (RTM SM911), linearly between. The jlimit keeps the ends exact.
    const float stockAttackScale = juce::jlimit (0.95f, 1.34f,
                                                 1.34f - 0.047f * static_cast<float> (tapeType));
    const float stockReleaseScale = juce::jlimit (0.95f, 1.40f,
                                                  1.40f - 0.064f * static_cast<float> (tapeType));
    const float transportAttackScale = 1.32f - 0.22f * speedScale;                  // 1.14 -> 1.03
    const float transportReleaseScale = 1.38f - 0.30f * speedScale;                 // 1.15 -> 0.98

    const float inputAttackSeconds = 0.16f * stockAttackScale * transportAttackScale;
    const float inputReleaseSeconds = 0.85f * stockReleaseScale * transportReleaseScale;
    const float outputAttackSeconds = 0.20f * stockAttackScale * transportAttackScale;
    const float outputReleaseSeconds = 1.00f * stockReleaseScale * transportReleaseScale;

    // The two glue detectors' block-rate coefficients. Everything they depend on -
    // the engine rate, the four base constants above and the trim-driven load - is
    // fixed for the whole block, so they are built once here rather than recomputed
    // per sample. Before this the detector cost two std::exp calls per sample per
    // channel for values that cannot change inside the block.
    points.inputCoefficients = GlueCompressor::makeCoefficients (
        engineSampleRate, inputAttackSeconds, inputReleaseSeconds, inputDriveLoad);
    points.outputCoefficients = GlueCompressor::makeCoefficients (
        engineSampleRate, outputAttackSeconds, outputReleaseSeconds, outputDriveLoad);

    // Makeup is part of each stage, and it is driven by the same trim control: a
    // boosted trim pays its reduction back and a trimmed-down one simply backs off,
    // so neither stage can quietly undo the balance the user dialled in.
    const float inputDegree = juce::jlimit (0.0f, 1.0f, (inputDb + 16.0f) / 32.0f);
    const float outputDegree = juce::jlimit (0.0f, 1.0f, (outputDb + 16.0f) / 32.0f);
    points.inputMakeupFraction = 0.30f + inputDegree * 0.35f;
    points.outputMakeupFraction = 0.35f + outputDegree * 0.35f;

    return points;
}

} // namespace j37glueop
