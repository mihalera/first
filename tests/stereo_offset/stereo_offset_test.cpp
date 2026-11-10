#include "StereoOffsetBuffer.h"

#include <cmath>
#include <cstdio>

namespace
{
bool near (float actual, float expected) noexcept
{
    return std::abs (actual - expected) < 1.0e-6f;
}
}

int main()
{
    StereoOffsetBuffer buffer;
    buffer.write (0, 1.0f);
    buffer.write (1, 10.0f);
    if (! near (buffer.readInterpolated (0, 0.0f), 1.0f)
        || ! near (buffer.readInterpolated (1, 0.0f), 10.0f))
    {
        std::fprintf (stderr, "zero offset does not return the current stereo frame\n");
        return 1;
    }

    buffer.advanceFrame();
    buffer.write (0, 2.0f);
    buffer.write (1, 20.0f);
    if (! near (buffer.readInterpolated (0, 1.0f), 1.0f)
        || ! near (buffer.readInterpolated (1, 1.0f), 10.0f)
        || ! near (buffer.readInterpolated (0, 0.5f), 1.5f)
        || ! near (buffer.readInterpolated (1, 0.5f), 15.0f))
    {
        std::fprintf (stderr, "fractional delay crossed or mixed the stereo channels\n");
        return 1;
    }

    buffer.advanceFrame();
    buffer.write (0, 3.0f);
    buffer.write (1, 30.0f);
    if (! near (buffer.readInterpolated (0, 2.0f), 1.0f)
        || ! near (buffer.readInterpolated (1, 2.0f), 10.0f))
    {
        std::fprintf (stderr, "history changed when switching the delayed side\n");
        return 1;
    }

    buffer.write (0, 4.0f);
    buffer.write (1, 40.0f);
    if (! near (buffer.readInterpolated (0, 1000.0f), 0.0f)
        || ! near (buffer.readInterpolated (1, 1000.0f), 0.0f))
    {
        std::fprintf (stderr, "maximum delay was not clamped to buffer history\n");
        return 1;
    }

    buffer.reset();
    buffer.write (0, -0.25f);
    buffer.write (1, 0.75f);
    if (! near (buffer.readInterpolated (0, 0.0f), -0.25f)
        || ! near (buffer.readInterpolated (1, 0.0f), 0.75f))
    {
        std::fprintf (stderr, "reset did not clear both channel histories\n");
        return 1;
    }

    std::puts ("StereoOffsetBuffer: independent channel histories and fractional reads pass");
    return 0;
}
