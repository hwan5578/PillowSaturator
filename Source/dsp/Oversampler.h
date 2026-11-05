#pragma once

// Cascaded 2x half-band oversampler (up to 8x) with two filter families:
//  - Minimum phase: polyphase IIR allpass pairs (elliptic half-band design), ~5-6 samples latency
//  - Linear phase : Kaiser-windowed half-band FIR, ~66-73 samples latency
// The wet path is padded at the high rate (integer delay + Thiran fractional delay) so that
// filters + the ADAA half-sample add up to an exact integer number of base-rate samples.
// This keeps the dry/wet Mix phase-aligned. All filters are designed once (static);
// per-channel state is allocation-free at run time.

#include "DspCommon.h"
#include "Filters.h"
#include <array>
#include <complex>
#include <vector>

namespace pillow::dsp
{
namespace halfband
{
    //--------------------------------------------------------------------------
    // Polyphase IIR half-band designer (elliptic, two allpass branches).
    // `transition` is the normalised transition width relative to the low sample rate:
    // passband edge = (0.5 - transition) * fsLow.
    inline std::vector<double> designIIR (int numCoefs, double transition)
    {
        double k = std::tan ((1.0 - transition * 2.0) * kPi / 4.0);
        k *= k;
        const double kksqrt = std::pow (1.0 - k * k, 0.25);
        const double e = 0.5 * (1.0 - kksqrt) / (1.0 + kksqrt);
        const double e2 = e * e;
        const double e4 = e2 * e2;
        const double q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));
        const int order = numCoefs * 2 + 1;

        std::vector<double> coefs ((size_t) numCoefs);
        for (int index = 0; index < numCoefs; ++index)
        {
            const int c = index + 1;
            double num = 0.0;
            {
                int i = 0, sign = 1;
                double term = 0.0;
                do
                {
                    term = std::pow (q, i * (i + 1)) * std::sin ((i * 2 + 1) * c * kPi / order) * sign;
                    num += term;
                    sign = -sign;
                    ++i;
                } while (std::abs (term) > 1.0e-100 && i < 1000);
            }
            double den = 0.0;
            {
                int i = 1, sign = -1;
                double term = 0.0;
                do
                {
                    term = std::pow (q, i * i) * std::cos (i * 2 * c * kPi / order) * sign;
                    den += term;
                    sign = -sign;
                    ++i;
                } while (std::abs (term) > 1.0e-100 && i < 1000);
            }
            const double ww = num * std::pow (q, 0.25) / (den + 0.5);
            const double wwsq = ww * ww;
            const double x = std::sqrt ((1.0 - wwsq * k) * (1.0 - wwsq / k)) / (1.0 + wwsq);
            coefs[(size_t) index] = (1.0 - x) / (1.0 + x);
        }
        return coefs;
    }

    //--------------------------------------------------------------------------
    inline double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        const double q = 0.25 * x * x;
        for (int k = 1; k < 500; ++k)
        {
            term *= q / ((double) k * (double) k);
            sum += term;
            if (term < sum * 1.0e-17)
                break;
        }
        return sum;
    }

    // Kaiser-windowed half-band FIR, N = 4M + 3 taps (centre tap = 0.5, DC gain exactly 1).
    inline std::vector<double> designFIR (int M, double attenuationDb)
    {
        const int N = 4 * M + 3;
        const int c = (N - 1) / 2;
        const double beta = attenuationDb > 50.0 ? 0.1102 * (attenuationDb - 8.7)
                                                 : 0.5842 * std::pow (attenuationDb - 21.0, 0.4) + 0.07886 * (attenuationDb - 21.0);
        const double i0beta = besselI0 (beta);

        std::vector<double> h ((size_t) N, 0.0);
        double oddSum = 0.0;
        for (int n = 0; n < N; ++n)
        {
            const int d = n - c;
            if (d == 0 || (d % 2) == 0)
                continue;
            const double x = 0.5 * kPi * d;
            const double r = (2.0 * n) / (double) (N - 1) - 1.0;
            const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0beta;
            h[(size_t) n] = 0.5 * (std::sin (x) / x) * w;
            oddSum += h[(size_t) n];
        }
        for (int n = 0; n < N; ++n)
            if (n != c)
                h[(size_t) n] *= 0.5 / oddSum;
        h[(size_t) c] = 0.5;
        return h;
    }

    //--------------------------------------------------------------------------
    // Phase delay of H(z) = 0.5 * (A0(z^2) + z^-1 A1(z^2)) at a low frequency, in high-rate samples.
    inline double iirLowFrequencyDelay (const std::vector<double>& coefs)
    {
        const double w = 1.0e-3;
        const std::complex<double> zinv = std::polar (1.0, -w);
        const std::complex<double> z2inv = zinv * zinv;
        std::complex<double> a0 (1.0, 0.0), a1 (1.0, 0.0);
        for (size_t i = 0; i < coefs.size(); ++i)
        {
            const auto sec = (coefs[i] + z2inv) / (1.0 + coefs[i] * z2inv);
            if ((i % 2) == 0)
                a0 *= sec;
            else
                a1 *= sec;
        }
        const auto h = 0.5 * (a0 + zinv * a1);
        return -std::arg (h) / w;
    }

    //--------------------------------------------------------------------------
    // Shared filter designs. Stage 1 is base<->2x, stage 2 is 2x<->4x, stage 3 is 4x<->8x.
    // Specs keep a 0..20 kHz passband and >= 90 dB rejection of anything that would fold
    // into it, worst case at a 44.1 kHz base rate.
    struct Designs
    {
        static constexpr int numStages = 3;
        std::array<std::vector<double>, numStages> fir;
        std::array<std::vector<double>, numStages> iir;
        std::array<double, numStages> iirDelay {}; // per filter, high-rate samples

        Designs()
        {
            const int firM[numStages] = { 32, 5, 4 };         // N = 131, 23, 19
            const int iirCoefs[numStages] = { 8, 4, 3 };      // ~104, 125, 140 dB
            for (int s = 0; s < numStages; ++s)
            {
                const double fsLow = 44100.0 * (double) (1 << s);
                const double transition = (0.5 * fsLow - 20000.0) / fsLow;
                fir[(size_t) s] = designFIR (firM[s], 95.0);
                iir[(size_t) s] = designIIR (iirCoefs[s], transition);
                iirDelay[(size_t) s] = iirLowFrequencyDelay (iir[(size_t) s]);
            }
        }

        static const Designs& get()
        {
            static const Designs designs;
            return designs;
        }
    };
} // namespace halfband

//==============================================================================
class FirHalfbandStage
{
public:
    void setup (const std::vector<double>& h)
    {
        const int N = (int) h.size();
        M = (N - 3) / 4;
        K = (N + 1) / 2;
        taps.resize ((size_t) K);
        for (int k = 0; k < K; ++k)
            taps[(size_t) k] = (float) h[(size_t) (2 * k)];
        upHist.assign ((size_t) (2 * K), 0.0f);
        dnHist.assign ((size_t) (2 * K), 0.0f);
        dnOdd.assign ((size_t) (M + 1), 0.0f);
        reset();
    }

    void reset() noexcept
    {
        std::fill (upHist.begin(), upHist.end(), 0.0f);
        std::fill (dnHist.begin(), dnHist.end(), 0.0f);
        std::fill (dnOdd.begin(), dnOdd.end(), 0.0f);
        upPos = dnPos = oddPos = 0;
    }

    int numTaps() const noexcept { return 4 * M + 3; }

    // n low-rate samples in, 2n high-rate samples out
    void up (const float* in, float* out, int n) noexcept
    {
        const float* t = taps.data();
        for (int i = 0; i < n; ++i)
        {
            const float x = in[i];
            upPos = (upPos == 0) ? K - 1 : upPos - 1;
            upHist[(size_t) upPos] = x;
            upHist[(size_t) (upPos + K)] = x;
            const float* hp = upHist.data() + upPos; // hp[k] = x[m - k]

            float acc = 0.0f;
            for (int k = 0; k < K; ++k)
                acc += t[k] * hp[k];

            out[2 * i] = 2.0f * acc;
            out[2 * i + 1] = hp[M]; // centre-tap branch: x[m - M]
        }
    }

    // 2n high-rate samples in, n low-rate samples out
    void down (const float* in, float* out, int n) noexcept
    {
        const float* t = taps.data();
        const int oddLen = M + 1;
        for (int i = 0; i < n; ++i)
        {
            const float e = in[2 * i];
            const float o = in[2 * i + 1];

            dnPos = (dnPos == 0) ? K - 1 : dnPos - 1;
            dnHist[(size_t) dnPos] = e;
            dnHist[(size_t) (dnPos + K)] = e;
            const float* hp = dnHist.data() + dnPos;

            float acc = 0.0f;
            for (int k = 0; k < K; ++k)
                acc += t[k] * hp[k];

            acc += 0.5f * dnOdd[(size_t) oddPos]; // odd sample from M + 1 steps ago
            dnOdd[(size_t) oddPos] = o;
            oddPos = (oddPos + 1 == oddLen) ? 0 : oddPos + 1;

            out[i] = acc;
        }
    }

private:
    std::vector<float> taps, upHist, dnHist, dnOdd;
    int M = 0, K = 1, upPos = 0, dnPos = 0, oddPos = 0;
};

//==============================================================================
class IirHalfbandStage
{
public:
    static constexpr int maxPerPath = 8;

    void setup (const std::vector<double>& coefs)
    {
        n0 = n1 = 0;
        for (size_t i = 0; i < coefs.size(); ++i)
        {
            if ((i % 2) == 0)
            {
                if (n0 < maxPerPath)
                    c0[n0++] = (float) coefs[i];
            }
            else if (n1 < maxPerPath)
            {
                c1[n1++] = (float) coefs[i];
            }
        }
        reset();
    }

    void reset() noexcept
    {
        for (int k = 0; k < maxPerPath; ++k)
            su0[k] = su1[k] = sd0[k] = sd1[k] = 0.0f;
        prevOdd = 0.0f;
    }

    void up (const float* in, float* out, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            out[2 * i] = run (c0, su0, n0, in[i]);
            out[2 * i + 1] = run (c1, su1, n1, in[i]);
        }
    }

    void down (const float* in, float* out, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const float e = in[2 * i];
            const float o = in[2 * i + 1];
            out[i] = 0.5f * (run (c0, sd0, n0, e) + run (c1, sd1, n1, prevOdd));
            prevOdd = o;
        }
    }

private:
    static float run (const float* c, float* s, int count, float x) noexcept
    {
        for (int k = 0; k < count; ++k)
        {
            const float y = c[k] * x + s[k];
            s[k] = x - c[k] * y;
            x = y;
        }
        return x;
    }

    float c0[maxPerPath] {}, c1[maxPerPath] {};
    float su0[maxPerPath] {}, su1[maxPerPath] {}, sd0[maxPerPath] {}, sd1[maxPerPath] {};
    int n0 = 0, n1 = 0;
    float prevOdd = 0.0f;
};

//==============================================================================
class Oversampler
{
public:
    static constexpr int maxStages = halfband::Designs::numStages;
    static constexpr int maxFactor = 1 << maxStages;

    // Half a high-rate sample of delay introduced by first-order ADAA in the curve.
    static constexpr double coreDelayHighRate = 0.5;

    // Natural delay of up + down filtering plus the curve, in highest-rate samples.
    // Linear phase: exact. Minimum phase: low-frequency phase delay.
    static double naturalHighRateDelay (int numStages, bool linearPhase)
    {
        const auto& d = halfband::Designs::get();
        numStages = clampValue (numStages, 0, maxStages);
        double total = coreDelayHighRate;
        for (int s = 0; s < numStages; ++s)
        {
            const double stageDelay = linearPhase ? (double) (d.fir[(size_t) s].size() - 1)
                                                  : 2.0 * d.iirDelay[(size_t) s];
            total += stageDelay * (double) (1 << (numStages - 1 - s));
        }
        return total;
    }

    // Wet-path latency in base-rate samples after padding (always an integer).
    static int latencyFor (int numStages, bool linearPhase)
    {
        numStages = clampValue (numStages, 0, maxStages);
        const double factor = (double) (1 << numStages);
        // leave room for >= 0.5 high-rate samples of fractional padding
        return (int) std::ceil ((naturalHighRateDelay (numStages, linearPhase) + 0.5) / factor - 1.0e-9);
    }

    void prepare (int maxBlockLowRate)
    {
        const auto& d = halfband::Designs::get();
        for (int s = 0; s < maxStages; ++s)
        {
            fir[(size_t) s].setup (d.fir[(size_t) s]);
            iir[(size_t) s].setup (d.iir[(size_t) s]);
            buffers[(size_t) s].assign ((size_t) (maxBlockLowRate << (s + 1)), 0.0f);
        }
        maxBlock = maxBlockLowRate;
        pad.fill (0.0f);
        configure (numStages, linear);
    }

    void configure (int stages, bool linearPhase) noexcept
    {
        numStages = clampValue (stages, 0, maxStages);
        linear = linearPhase;

        const double factor = (double) (1 << numStages);
        const double padHigh = (double) latencyFor (numStages, linear) * factor
                             - naturalHighRateDelay (numStages, linear); // in [0.5, factor + 0.5)
        padLength = clampValue ((int) std::floor (padHigh - 0.5), 0, (int) pad.size() - 1);
        fractional.setDelay (padHigh - (double) padLength);               // in [0.5, 1.5)
        reset();
    }

    void reset() noexcept
    {
        for (auto& s : fir)
            s.reset();
        for (auto& s : iir)
            s.reset();
        pad.fill (0.0f);
        padPos = 0;
        fractional.reset();
    }

    int getFactor() const noexcept { return 1 << numStages; }
    int getNumStages() const noexcept { return numStages; }
    bool isLinearPhase() const noexcept { return linear; }

    // Upsamples n base-rate samples; returns the internal high-rate buffer (n * factor samples)
    // which the caller may process in place before calling processDown().
    float* processUp (const float* in, int n) noexcept
    {
        if (numStages == 0)
        {
            std::copy (in, in + n, buffers[0].data());
            return buffers[0].data();
        }

        const float* src = in;
        for (int s = 0; s < numStages; ++s)
        {
            float* dst = buffers[(size_t) s].data();
            const int len = n << s;
            if (linear)
                fir[(size_t) s].up (src, dst, len);
            else
                iir[(size_t) s].up (src, dst, len);
            src = dst;
        }
        return buffers[(size_t) (numStages - 1)].data();
    }

    void processDown (float* out, int n) noexcept
    {
        if (numStages == 0)
        {
            std::copy (buffers[0].data(), buffers[0].data() + n, out);
            return;
        }

        float* top = buffers[(size_t) (numStages - 1)].data();
        const int len = n << numStages;
        for (int i = 0; i < len; ++i)
        {
            float v = fractional.process (top[i]);
            if (padLength > 0)
            {
                const float delayed = pad[(size_t) padPos];
                pad[(size_t) padPos] = v;
                v = delayed;
                padPos = (padPos + 1 == padLength) ? 0 : padPos + 1;
            }
            top[i] = v;
        }

        for (int s = numStages - 1; s >= 0; --s)
        {
            const float* src = buffers[(size_t) s].data();
            float* dst = (s == 0) ? out : buffers[(size_t) (s - 1)].data();
            const int lenOut = n << s;
            if (linear)
                fir[(size_t) s].down (src, dst, lenOut);
            else
                iir[(size_t) s].down (src, dst, lenOut);
        }
    }

private:
    std::array<FirHalfbandStage, maxStages> fir;
    std::array<IirHalfbandStage, maxStages> iir;
    std::array<std::vector<float>, maxStages> buffers;
    std::array<float, 2 * maxFactor> pad {};
    ThiranAllpass fractional;
    int padLength = 0, padPos = 0;
    int numStages = 1, maxBlock = 0;
    bool linear = false;
};
} // namespace pillow::dsp
