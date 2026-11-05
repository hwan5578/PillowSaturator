#pragma once

// Static saturation curves with closed-form antiderivatives for first-order
// antiderivative anti-aliasing (ADAA, Parker et al. 2016 / Bilbao et al. 2017).
// All curves have slope 1 at the origin, so small signals pass at unity gain.

#include "DspCommon.h"

namespace pillow::dsp
{
enum class CurveId : int
{
    Tanh = 0, // firm, smooth knee           -> Tube, Transformer
    Soft,     // x / sqrt(1 + x^2), soft knee -> Tape
    Cubic     // cubic soft clip, ceiling at |x| = 1.5 -> Clean
};

struct TanhCurve
{
    static double f (double x) noexcept { return std::tanh (x); }

    // log(cosh(x)), overflow-free
    static double F (double x) noexcept
    {
        const double ax = std::abs (x);
        return ax + std::log1p (std::exp (-2.0 * ax)) - 0.69314718055994530942;
    }

    static double slope (double x) noexcept
    {
        const double t = std::tanh (x);
        return 1.0 - t * t;
    }
};

struct SoftCurve
{
    static double f (double x) noexcept { return x / std::sqrt (1.0 + x * x); }

    // sqrt(1 + x^2) - 1, written to avoid cancellation for small x
    static double F (double x) noexcept
    {
        const double x2 = x * x;
        return x2 / (std::sqrt (1.0 + x2) + 1.0);
    }

    static double slope (double x) noexcept
    {
        const double d = 1.0 + x * x;
        return 1.0 / (d * std::sqrt (d));
    }
};

struct CubicCurve
{
    static double f (double x) noexcept
    {
        if (x >= 1.5)
            return 1.0;
        if (x <= -1.5)
            return -1.0;
        return x - (4.0 / 27.0) * x * x * x;
    }

    static double F (double x) noexcept
    {
        const double ax = std::abs (x);
        if (ax >= 1.5)
            return ax - 0.5625;
        const double x2 = x * x;
        return 0.5 * x2 - x2 * x2 / 27.0;
    }

    static double slope (double x) noexcept
    {
        return std::abs (x) >= 1.5 ? 0.0 : 1.0 - (4.0 / 9.0) * x * x;
    }
};

// Runtime dispatch helpers (used off the hot path: tables, UI curve display, per-sample bias).
inline double curveF (CurveId c, double x) noexcept
{
    switch (c)
    {
        case CurveId::Tanh:  return TanhCurve::f (x);
        case CurveId::Soft:  return SoftCurve::f (x);
        case CurveId::Cubic: return CubicCurve::f (x);
    }
    return x;
}

inline double curveSlope (CurveId c, double x) noexcept
{
    switch (c)
    {
        case CurveId::Tanh:  return TanhCurve::slope (x);
        case CurveId::Soft:  return SoftCurve::slope (x);
        case CurveId::Cubic: return CubicCurve::slope (x);
    }
    return 1.0;
}

//==============================================================================
// Per-channel ADAA state. The curve input is w = v + b where v is the driven signal
// and b a (slowly varying) bias, held constant within one base-rate sample.
struct AdaaState
{
    double vPrev = 0.0;  // previous driven input (without bias)
    double FPrev = 0.0;  // F(vPrev + bPrev)
    float bPrev = 0.0f;

    void reset() noexcept
    {
        vPrev = 0.0;
        FPrev = 0.0;
        bPrev = 0.0f;
    }
};

// Processes `n` base-rate samples worth of oversampled data in place:
// y = norm * (ADAA_f(v + b) - f(b)) with b, norm, offset given per base-rate sample.
template <class Curve>
inline void processCurveBlock (float* data, int n, int factor, const float* bias, const float* norm,
                               const float* offset, AdaaState& st) noexcept
{
    constexpr double eps = 1.0e-6;
    double vPrev = st.vPrev, FPrev = st.FPrev;
    float bPrev = st.bPrev;

    for (int m = 0; m < n; ++m)
    {
        const float bf = bias[m];
        const double b = bf;
        const double nm = norm[m];
        const double off = offset[m];

        if (std::abs (bf - bPrev) > 0.0f) // bias changed: re-evaluate F at the previous input with the new bias
        {
            FPrev = Curve::F (vPrev + b);
            bPrev = bf;
        }

        float* p = data + m * factor;
        for (int k = 0; k < factor; ++k)
        {
            const double v = p[k];
            const double w = v + b;
            const double Fw = Curve::F (w);
            const double d = v - vPrev;
            const double y = (std::abs (d) > eps) ? (Fw - FPrev) / d
                                                  : Curve::f (0.5 * (w + vPrev + b));
            vPrev = v;
            FPrev = Fw;
            p[k] = (float) (nm * (y - off));
        }
    }

    st.vPrev = vPrev;
    st.FPrev = FPrev;
    st.bPrev = bPrev;
}

inline void processCurveBlock (CurveId c, float* data, int n, int factor, const float* bias, const float* norm,
                               const float* offset, AdaaState& st) noexcept
{
    switch (c)
    {
        case CurveId::Tanh:  processCurveBlock<TanhCurve>  (data, n, factor, bias, norm, offset, st); break;
        case CurveId::Soft:  processCurveBlock<SoftCurve>  (data, n, factor, bias, norm, offset, st); break;
        case CurveId::Cubic: processCurveBlock<CubicCurve> (data, n, factor, bias, norm, offset, st); break;
    }
}
} // namespace pillow::dsp
