#pragma once

#include "DspCommon.h"
#include <vector>

namespace pillow::dsp
{
//==============================================================================
// Topology-preserving (trapezoidal) state-variable filter, after A. Simper
// ("SvfLinearTrapOptimised2"). Stable under fast coefficient modulation.
struct SvfCoeffs
{
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    float m0 = 1.0f, m1 = 0.0f, m2 = 0.0f; // output mix: m0*in + m1*band + m2*low
};

namespace svf
{
    inline double prewarp (double hz, double fs) noexcept
    {
        return std::tan (kPi * clampValue (hz, 1.0, 0.49 * fs) / fs);
    }

    inline SvfCoeffs fromGK (double g, double k, double m0, double m1, double m2) noexcept
    {
        SvfCoeffs c;
        const double a1 = 1.0 / (1.0 + g * (g + k));
        const double a2 = g * a1;
        c.a1 = (float) a1;
        c.a2 = (float) a2;
        c.a3 = (float) (g * a2);
        c.m0 = (float) m0;
        c.m1 = (float) m1;
        c.m2 = (float) m2;
        return c;
    }

    inline SvfCoeffs lowpass (double hz, double q, double fs) noexcept
    {
        return fromGK (prewarp (hz, fs), 1.0 / q, 0.0, 0.0, 1.0);
    }

    inline SvfCoeffs highpass (double hz, double q, double fs) noexcept
    {
        const double k = 1.0 / q;
        return fromGK (prewarp (hz, fs), k, 1.0, -k, -1.0);
    }

    inline SvfCoeffs bell (double hz, double q, double db, double fs) noexcept
    {
        const double A = std::pow (10.0, db / 40.0);
        const double k = 1.0 / (q * A);
        return fromGK (prewarp (hz, fs), k, 1.0, k * (A * A - 1.0), 0.0);
    }

    inline SvfCoeffs lowShelf (double hz, double q, double db, double fs) noexcept
    {
        const double A = std::pow (10.0, db / 40.0);
        const double k = 1.0 / q;
        return fromGK (prewarp (hz, fs) / std::sqrt (A), k, 1.0, k * (A - 1.0), A * A - 1.0);
    }

    inline SvfCoeffs highShelf (double hz, double q, double db, double fs) noexcept
    {
        const double A = std::pow (10.0, db / 40.0);
        const double k = 1.0 / q;
        return fromGK (prewarp (hz, fs) * std::sqrt (A), k, A * A, k * (1.0 - A) * A, 1.0 - A * A);
    }
} // namespace svf

struct SvfState
{
    float ic1 = 0.0f, ic2 = 0.0f;

    void reset() noexcept { ic1 = ic2 = 0.0f; }

    float process (float v0, const SvfCoeffs& c) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = c.a1 * ic1 + c.a2 * v3;
        const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return c.m0 * v0 + c.m1 * v1 + c.m2 * v2;
    }
};

//==============================================================================
// One-pole TPT filter (6 dB/oct low/high pass).
struct OnePole
{
    float G = 0.0f, s = 0.0f;

    void setCutoff (double hz, double fs) noexcept
    {
        const double g = std::tan (kPi * clampValue (hz, 1.0, 0.49 * fs) / fs);
        G = (float) (g / (1.0 + g));
    }

    void reset() noexcept { s = 0.0f; }

    float lowpass (float x) noexcept
    {
        const float v = (x - s) * G;
        const float y = v + s;
        s = y + v;
        return y;
    }

    float highpass (float x) noexcept { return x - lowpass (x); }
};

//==============================================================================
struct DcBlocker
{
    float R = 0.9995f, x1 = 0.0f, y1 = 0.0f;

    void prepare (double fs, double hz) noexcept { R = (float) std::exp (-2.0 * kPi * hz / fs); }
    void reset() noexcept { x1 = y1 = 0.0f; }

    float process (float x) noexcept
    {
        const float y = x - x1 + R * y1;
        x1 = x;
        y1 = y;
        return y;
    }
};

//==============================================================================
// Fixed-size integer delay line (allocation happens in prepare only).
class DelayLine
{
public:
    void prepare (int maxDelaySamples)
    {
        size = std::max (1, maxDelaySamples + 1);
        buffer.assign ((size_t) size, 0.0f);
        pos = 0;
        delay = std::min (delay, size - 1);
    }

    void setDelay (int samples) noexcept { delay = clampValue (samples, 0, size - 1); }
    int getDelay() const noexcept { return delay; }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        pos = 0;
    }

    float process (float x) noexcept
    {
        if (delay == 0)
            return x;

        buffer[(size_t) pos] = x;
        int r = pos - delay;
        if (r < 0)
            r += size;
        const float y = buffer[(size_t) r];
        pos = (pos + 1 == size) ? 0 : pos + 1;
        return y;
    }

private:
    std::vector<float> buffer;
    int size = 1, pos = 0, delay = 0;
};

//==============================================================================
// RMS envelope: x^2 -> linear pre-smoothing (removes audio-rate ripple without creating new
// frequencies) -> attack/release -> sqrt. A rectifier (|x|) would generate harmonics that
// alias at the base rate and leak into the bias / filter modulation as sidebands.
// Steady state for a sine of amplitude A is A / sqrt(2).
struct EnvelopeFollower
{
    float pre = 0.0f, attack = 0.0f, release = 0.0f, ms1 = 0.0f, ms2 = 0.0f;

    void prepare (double fs, double attackMs, double releaseMs, double preSmoothMs = 2.0) noexcept
    {
        pre = (float) (1.0 - std::exp (-1.0 / (fs * preSmoothMs * 0.001)));
        attack = (float) (1.0 - std::exp (-1.0 / (fs * attackMs * 0.001)));
        release = (float) (1.0 - std::exp (-1.0 / (fs * releaseMs * 0.001)));
    }

    void reset() noexcept { ms1 = ms2 = 0.0f; }

    float process (float x) noexcept
    {
        ms1 += pre * (x * x - ms1);
        ms2 += (ms1 > ms2 ? attack : release) * (ms1 - ms2);
        return std::sqrt (ms2);
    }
};

//==============================================================================
// First-order Thiran allpass: fractional delay `d` (best for 0.5 <= d < 1.5), flat magnitude.
struct ThiranAllpass
{
    float a = 0.0f, s = 0.0f;

    void setDelay (double d) noexcept { a = (float) ((1.0 - d) / (1.0 + d)); }
    void reset() noexcept { s = 0.0f; }

    float process (float x) noexcept
    {
        const float y = a * x + s;
        s = x - a * y;
        return y;
    }
};
} // namespace pillow::dsp
