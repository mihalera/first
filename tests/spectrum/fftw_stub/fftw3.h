/*
  ============================================================================
    A stand-in for the fftw3.h Arm Performance Libraries ship.

    WHY THIS EXISTS. Source/DSP/SpectrumAnalyser.h has an ArmPL branch that no
    other harness in this repository can compile: ArmPL is a downloaded AArch64
    library, so a developer on x86 and the Linux x64 CI jobs have no copy of it.
    Leaving the branch uncompiled is the state the rest of this suite refuses to
    accept - tests/neural stands in for RTNeural's API rather than skip it, and
    tests/spectrum already compiles the stage both with and without pocketfft.

    WHAT IT IS. Not a mock: a real, unnormalised forward real-to-complex DFT,
    computed in double and cast to float. That is the same transform ArmPL's
    FFTW interface computes, so the harness's calibration, linearity, band-order
    and edge assertions are meaningful against it rather than trivially true.
    The declarations are FFTW3's own - `fftwf_complex` is `float[2]`,
    `fftwf_plan` is an opaque pointer, and the three calls below are the ones
    the analyser makes - so the branch compiled here is the branch the ARM
    builds compile.

    WHAT IT IS NOT. Proof that the real library links. That is what the
    linux-arm64 and windows-arm64 CI jobs are for: they install ArmPL, configure
    with J37_ARMPL_REQUIRED=ON and build the plugin against it. This header only
    keeps the C++ half of the integration honest on machines that cannot.
  ============================================================================
*/
#pragma once

#include <cmath>
#include <cstddef>

// The two flags the analyser passes. The values are FFTW3's own, so a mistake
// in the analyser's argument list would be a mistake here too - and this file
// is read by a human, not by FFTW.
#define FFTW_UNALIGNED (1U << 5)
#define FFTW_ESTIMATE  (1U << 6)

typedef float fftwf_complex[2];

struct fftwf_plan_s
{
    int size;
};

typedef struct fftwf_plan_s* fftwf_plan;

// One plan is enough here for the reason a stub is allowed to be simple: the
// harness prepares one analyser at a time, and the size is a compile-time
// constant of the stage. The plan carries only that size, so it is independent
// of the arrays it was created against - which is exactly the property the
// analyser relies on when it executes with the new-array variant.
inline fftwf_plan fftwf_plan_dft_r2c_1d (int n, float*, fftwf_complex*, unsigned)
{
    static fftwf_plan_s plan { 0 };
    plan.size = n;
    return &plan;
}

inline void fftwf_destroy_plan (fftwf_plan)
{
}

inline void fftwf_execute_dft_r2c (fftwf_plan plan, float* in, fftwf_complex* out)
{
    const int n = plan->size;
    const double twoPi = 6.283185307179586476925286766559;

    for (int k = 0; k <= n / 2; ++k)
    {
        double real = 0.0;
        double imaginary = 0.0;

        for (int t = 0; t < n; ++t)
        {
            const double angle = -twoPi * static_cast<double> (k)
                                        * static_cast<double> (t) / static_cast<double> (n);
            real += static_cast<double> (in[t]) * std::cos (angle);
            imaginary += static_cast<double> (in[t]) * std::sin (angle);
        }

        out[k][0] = static_cast<float> (real);
        out[k][1] = static_cast<float> (imaginary);
    }
}
