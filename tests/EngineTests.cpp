// Pillow Saturator - engine tests and measurements (no JUCE needed).
//   g++ -std=c++17 -O2 -I../Source tests/EngineTests.cpp -o engine_tests && ./engine_tests
// Prints measurements and exits non-zero if any check fails.

#include "dsp/SaturatorEngine.h"

#include <chrono>
#include <complex>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
 #include <xmmintrin.h>
 #include <pmmintrin.h>
 #define PILLOW_HAS_SSE 1
#endif

using namespace pillow::dsp;

namespace
{
int failures = 0;

void check (bool ok, const std::string& what)
{
    if (! ok)
    {
        ++failures;
        std::printf ("  [FAIL] %s\n", what.c_str());
    }
}

void enableFlushToZero()
{
   #if PILLOW_HAS_SSE
    _MM_SET_FLUSH_ZERO_MODE (_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE (_MM_DENORMALS_ZERO_ON);
   #endif
}

//==============================================================================
void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * kPi / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t j = 0; j < len / 2; ++j)
            {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

std::vector<double> magnitudeSpectrum (const std::vector<float>& x)
{
    std::vector<std::complex<double>> a (x.begin(), x.end());
    fft (a);
    std::vector<double> m (a.size() / 2);
    for (size_t i = 0; i < m.size(); ++i)
        m[i] = std::abs (a[i]);
    return m;
}

double db (double v) { return 20.0 * std::log10 (std::max (v, 1.0e-15)); }

EngineParams cleanParams()
{
    EngineParams p;
    p.inputDb = 0.0f;
    p.driveDb = 0.0f;
    p.character = 0.0f;
    p.tone = 0.0f;
    p.lowCutHz = kMinLowCutHz;
    p.highCutHz = kMaxHighCutHz;
    p.mix = 1.0f;
    p.outputDb = 0.0f;
    p.autoGain = true;
    p.style = (int) StyleId::Clean;
    p.quality = 1;
    p.linearPhase = false;
    p.stereoMode = 0;
    p.bypass = false;
    return p;
}

// Runs a periodic tone (exactly `bin` cycles per `N` samples) through a fresh engine and
// returns the steady-state N-sample capture of channel 0.
std::vector<float> runTone (const EngineParams& p, double fs, int bin, int N, double amplitude, int stereo = 2)
{
    SaturatorEngine eng;
    eng.prepare (fs, stereo);
    eng.setParameters (p, true);
    eng.reset();

    const int warm = (int) (fs * 0.5);
    const int total = warm + N;
    std::vector<float> l ((size_t) total), r ((size_t) total);
    for (int n = 0; n < total; ++n)
    {
        const double ph = 2.0 * kPi * (double) bin * (double) (n % N) / (double) N;
        l[(size_t) n] = (float) (amplitude * std::sin (ph));
        r[(size_t) n] = (float) (amplitude * std::sin (ph + 0.3));
    }
    float* ptrs[2] = { l.data(), r.data() };
    int pos = 0;
    while (pos < total)
    {
        const int len = std::min (512, total - pos);
        float* p2[2] = { ptrs[0] + pos, ptrs[1] + pos };
        eng.process (p2, stereo, len);
        pos += len;
    }
    return std::vector<float> (l.begin() + warm, l.end());
}

struct Harmonics
{
    double h[8] {}; // dB re fundamental, h[1] = H2 ...
    double thd = 0; // %
    double fundamentalDb = 0;
};

Harmonics analyseHarmonics (const std::vector<float>& y, int bin)
{
    const auto m = magnitudeSpectrum (y);
    Harmonics r;
    const double f1 = m[(size_t) bin];
    r.fundamentalDb = db (f1 * 2.0 / (double) y.size());
    double sum = 0.0;
    for (int k = 2; k <= 9; ++k)
    {
        const size_t b = (size_t) (bin * k);
        const double a = b < m.size() ? m[b] : 0.0;
        if (k <= 8)
            r.h[k - 1] = db (a / f1);
        sum += a * a;
    }
    r.thd = 100.0 * std::sqrt (sum) / f1;
    return r;
}

double worstAliasDb (const std::vector<float>& y, int bin, double fs)
{
    const auto m = magnitudeSpectrum (y);
    const double f1 = m[(size_t) bin];
    double worst = 0.0;
    const size_t maxBin = (size_t) (20000.0 / fs * (double) y.size());
    for (size_t b = 1; b < std::min (maxBin, m.size()); ++b)
        if ((b % (size_t) bin) != 0)
            worst = std::max (worst, m[b]);
    return db (worst / f1);
}

double rms (const std::vector<float>& v)
{
    double s = 0.0;
    for (float x : v)
        s += (double) x * x;
    return std::sqrt (s / (double) v.size());
}

const char* styleName (int s) { return getStyleSpec ((StyleId) s).name; }

//==============================================================================
void testFilters()
{
    std::printf ("\n== Filters: pre/post shelf pairs are inverses ==\n");
    const double fs = 48000.0;
    std::mt19937 rng (1);
    std::normal_distribution<float> nd (0.0f, 0.3f);
    struct Pair { SvfCoeffs a, b; const char* name; };
    Pair pairs[] = {
        { svf::highShelf (3000, 0.5, 7, fs), svf::highShelf (3000, 0.5, -7, fs), "high shelf +/-7 dB" },
        { svf::lowShelf (160, 0.5, 9, fs), svf::lowShelf (160, 0.5, -9, fs), "low shelf +/-9 dB" },
    };
    for (auto& pr : pairs)
    {
        SvfState s1, s2;
        double err = 0.0, sig = 0.0;
        for (int n = 0; n < 48000; ++n)
        {
            const float x = nd (rng);
            const float y = s2.process (s1.process (x, pr.a), pr.b);
            if (n > 1000)
            {
                err += (double) (y - x) * (y - x);
                sig += (double) x * x;
            }
        }
        const double residual = 10.0 * std::log10 (err / sig);
        std::printf ("  %-20s residual %.1f dB\n", pr.name, residual);
        check (residual < -100.0, std::string ("shelf inverse ") + pr.name);
    }
}

void testOversampler()
{
    std::printf ("\n== Oversampler: passband, latency ==\n");
    for (int linear = 0; linear < 2; ++linear)
    {
        for (int stages = 1; stages <= 3; ++stages)
        {
            Oversampler os;
            os.prepare (SaturatorEngine::kChunk);
            os.configure (stages, linear == 1);
            const double fs = 44100.0;
            std::printf ("  %s %dx  natural %.3f -> padded %d samples |", linear ? "linear " : "minimum", 1 << stages,
                         Oversampler::naturalHighRateDelay (stages, linear == 1) / (double) (1 << stages),
                         Oversampler::latencyFor (stages, linear == 1));
            for (double f : { 100.0, 1000.0, 10000.0, 18000.0, 20000.0 })
            {
                os.reset();
                const int total = 16384;
                std::vector<float> x ((size_t) total), y ((size_t) total);
                for (int n = 0; n < total; ++n)
                    x[(size_t) n] = (float) std::sin (2.0 * kPi * f * n / fs);
                for (int pos = 0; pos < total; pos += SaturatorEngine::kChunk)
                {
                    os.processUp (x.data() + pos, SaturatorEngine::kChunk);
                    os.processDown (y.data() + pos, SaturatorEngine::kChunk);
                }
                double peak = 0.0;
                for (int n = total / 2; n < total; ++n)
                    peak = std::max (peak, (double) std::abs (y[(size_t) n]));
                std::printf (" %.0f Hz %+.3f dB", f, db (peak));
                check (std::abs (db (peak)) < 0.05, "oversampler passband flat");
            }
            std::printf ("\n");
        }
    }
}

// Phase (degrees) of b relative to a at an FFT bin.
double phaseDiffDeg (const std::vector<float>& a, const std::vector<float>& b, int bin)
{
    std::vector<std::complex<double>> A (a.begin(), a.end()), B (b.begin(), b.end());
    fft (A);
    fft (B);
    return std::arg (B[(size_t) bin] / A[(size_t) bin]) * 180.0 / kPi;
}

void testLatencyAndMix()
{
    std::printf ("\n== Engine: latency, dry/wet alignment ==\n");
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        for (int linear = 0; linear < 2; ++linear)
        {
            for (int q = 0; q < kNumQualities; ++q)
            {
                EngineParams p = cleanParams();
                p.linearPhase = linear == 1;
                p.quality = q;
                p.mix = 0.0f; // dry only -> must equal input delayed by the reported latency
                SaturatorEngine eng;
                eng.prepare (fs, 2);
                eng.setParameters (p, true);
                eng.reset();
                const int lat = eng.getLatencySamples();
                const int total = 4096;
                std::vector<float> l ((size_t) total), r ((size_t) total), ref ((size_t) total);
                std::mt19937 rng (7);
                std::uniform_real_distribution<float> ud (-0.5f, 0.5f);
                for (int n = 0; n < total; ++n)
                    ref[(size_t) n] = l[(size_t) n] = r[(size_t) n] = ud (rng);
                float* ptrs[2] = { l.data(), r.data() };
                eng.process (ptrs, 2, total);
                double err = 0.0;
                for (int n = lat; n < total; ++n)
                    err = std::max (err, (double) std::abs (l[(size_t) n] - ref[(size_t) (n - lat)]));
                check (err < 1.0e-6, "mix 0 = input delayed by latency");

                // wet vs dry at low level (Clean, linear region): phase alignment + null depth
                const int N = 8192;
                EngineParams pw = cleanParams();
                pw.linearPhase = p.linearPhase;
                pw.quality = q;
                pw.inputDb = -24.0f;
                pw.autoGain = true;
                double ph[3] {}, nullDb = 0.0;
                const double freqs[3] = { 100.0, 1000.0, 5000.0 };
                for (int k = 0; k < 3; ++k)
                {
                    const int bin = (int) std::lround (freqs[k] * N / fs) | 1;
                    pw.mix = 1.0f;
                    const auto wet = runTone (pw, fs, bin, N, 0.05);
                    pw.mix = 0.0f;
                    const auto dry = runTone (pw, fs, bin, N, 0.05);
                    ph[k] = phaseDiffDeg (dry, wet, bin);
                    if (k == 1)
                    {
                        std::vector<float> diff ((size_t) N);
                        for (int n = 0; n < N; ++n)
                            diff[(size_t) n] = wet[(size_t) n] - dry[(size_t) n];
                        nullDb = db (rms (diff) / rms (dry));
                    }
                }
                std::printf ("  fs %6.0f %s q%d: latency %2d | wet-dry phase 100 Hz %+5.2f  1 kHz %+5.2f  5 kHz %+6.2f deg | 1 kHz null %6.1f dB\n",
                             fs, linear ? "linear " : "minimum", q, lat, ph[0], ph[1], ph[2], nullDb);
                // 10 Hz low cut + 5 Hz DC blocker lead by ~1.1 deg at 1 kHz; everything else must line up
                check (std::abs (ph[1]) < 2.5, "wet/dry phase aligned at 1 kHz");
                if (linear)
                    check (std::abs (ph[2]) < 2.0, "linear phase: wet/dry aligned at 5 kHz");
            }
        }
    }
}

void testHarmonics()
{
    std::printf ("\n== Harmonics: 1 kHz at -18 dBFS RMS (0 VU), 48 kHz, High quality ==\n");
    std::printf ("  %-12s %-6s %-5s %7s %7s %7s %7s %7s\n", "style", "drive", "char", "H2", "H3", "H4", "H5", "THD%");
    const double fs = 48000.0;
    const int N = 65536;
    const int bin = 1365; // ~1 kHz, odd
    const double amp = std::sqrt (2.0) * std::pow (10.0, -18.0 / 20.0);
    for (int s = 0; s < kNumStyles; ++s)
    {
        for (float drive : { 0.0f, 12.0f, 24.0f })
        {
            for (float ch : { 0.0f, 0.35f, 1.0f })
            {
                EngineParams p = cleanParams();
                p.style = s;
                p.driveDb = drive;
                p.character = ch;
                const auto y = runTone (p, fs, bin, N, amp);
                const auto h = analyseHarmonics (y, bin);
                std::printf ("  %-12s %4.0f   %4.2f %7.1f %7.1f %7.1f %7.1f %7.2f\n", styleName (s), drive, ch,
                             h.h[1], h.h[2], h.h[3], h.h[4], h.thd);
                if (ch <= 0.0f)
                    check (h.h[1] < -90.0 && h.h[3] < -90.0, std::string ("character 0 -> no even harmonics: ") + styleName (s));
                else
                    check (h.h[1] > -70.0, std::string ("character > 0 -> even harmonics: ") + styleName (s));
            }
        }
    }
}

void testFrequencyDependence()
{
    std::printf ("\n== Frequency dependence: H3 (dB re fundamental), -18 dBFS, drive +12 dB, character 0 ==\n");
    const double fs = 48000.0;
    const int N = 65536;
    struct F { double hz; int bin; };
    const F freqs[] = { { 60.0, 83 }, { 1000.0, 1365 }, { 5000.0, 6827 } }; // odd bins
    double h3[kNumStyles][3] {};
    const double amp = std::sqrt (2.0) * std::pow (10.0, -18.0 / 20.0);
    for (int s = 0; s < kNumStyles; ++s)
    {
        std::printf ("  %-12s", styleName (s));
        for (int k = 0; k < 3; ++k)
        {
            EngineParams p = cleanParams();
            p.style = s;
            p.driveDb = 12.0f;
            p.character = 0.0f;
            const auto y = runTone (p, fs, freqs[k].bin, N, amp);
            h3[s][k] = analyseHarmonics (y, freqs[k].bin).h[2];
            std::printf ("  %5.0f Hz %6.1f", freqs[k].hz, h3[s][k]);
        }
        std::printf ("\n");
    }
    check (h3[(int) StyleId::Tape][2] > h3[(int) StyleId::Tape][1] + 3.0, "Tape: highs saturate first");
    check (h3[(int) StyleId::Transformer][0] > h3[(int) StyleId::Transformer][1] + 3.0, "Transformer: lows saturate first");
}

void testAutoGain()
{
    std::printf ("\n== Auto Gain: output level change vs input at -18 dBFS, 1 kHz ==\n");
    const double fs = 48000.0;
    const int N = 16384;
    const int bin = 341; // ~1 kHz at 48k with N=16384, odd
    const double amp = std::sqrt (2.0) * std::pow (10.0, -18.0 / 20.0);
    double worst = 0.0;
    for (int s = 0; s < kNumStyles; ++s)
    {
        std::printf ("  %-12s", styleName (s));
        for (float drive : { 0.0f, 6.0f, 12.0f, 18.0f, 24.0f, 30.0f, 36.0f })
        {
            double worstHere = 0.0;
            for (float ch : { 0.0f, 0.5f, 1.0f })
            {
                EngineParams p = cleanParams();
                p.style = s;
                p.driveDb = drive;
                p.character = ch;
                const auto y = runTone (p, fs, bin, N, amp);
                const double change = db (rms (y) / (amp / std::sqrt (2.0)));
                if (std::abs (change) > std::abs (worstHere))
                    worstHere = change;
            }
            std::printf (" %+5.2f", worstHere);
            worst = std::max (worst, std::abs (worstHere));
        }
        std::printf ("  dB (drive 0..36, worst of character 0/0.5/1)\n");
    }
    check (worst < 1.0, "auto gain within 1 dB at the reference level");

    // parallel saturation must stay level-matched too (it is folded into the drive lookup)
    std::printf ("  parallel (Tube/Glow/Clean, drive 6/18, input 0/+12, amount 50/100 %%):\n");
    const char* focusName[] = { "Low", "Flat", "High" };
    double worstPar = 0.0;
    for (int focus = 0; focus < kNumParallelFocus; ++focus)
        for (int alt = 0; alt < 2; ++alt)
        {
            double worstHere = 0.0;
            for (int s : { (int) StyleId::Tube, (int) StyleId::Glow, (int) StyleId::Clean })
                for (float drive : { 6.0f, 18.0f })
                    for (float input : { 0.0f, 12.0f })
                        for (float amount : { 0.5f, 1.0f })
                        {
                            EngineParams p = cleanParams();
                            p.style = s;
                            p.driveDb = drive;
                            p.inputDb = input;
                            p.character = 0.35f;
                            p.parallel = amount;
                            p.parallelFocus = focus;
                            p.parallelAlt = alt == 1;
                            const auto y = runTone (p, fs, bin, N, amp);
                            const double change = db (rms (y) / (amp / std::sqrt (2.0)));
                            if (std::abs (change) > std::abs (worstHere))
                                worstHere = change;
                        }
            std::printf ("    %-4s %s worst %+5.2f dB\n", focusName[focus], alt ? "Alt" : "   ", worstHere);
            worstPar = std::max (worstPar, std::abs (worstHere));
        }
    check (worstPar < 1.0, "auto gain within 1 dB with parallel saturation");
}

void testParallelAndAir()
{
    std::printf ("\n== Parallel saturation focus: H3 / H2 (dB re fundamental), Clean, drive 0, -18 dBFS ==\n");
    const double fs = 48000.0;
    const int N = 65536;
    struct F { double hz; int bin; };
    const F freqs[] = { { 60.0, 83 }, { 1000.0, 1365 }, { 5000.0, 6827 } };
    const double amp = std::sqrt (2.0) * std::pow (10.0, -18.0 / 20.0);
    const char* focusName[] = { "off ", "Low ", "Flat", "High" };
    double thd[4][3] {};
    for (int f = -1; f < kNumParallelFocus; ++f)
    {
        std::printf ("  %s", focusName[f + 1]);
        for (int k = 0; k < 3; ++k)
        {
            EngineParams p = cleanParams();
            p.style = (int) StyleId::Clean;
            p.parallel = f < 0 ? 0.0f : 1.0f;
            p.parallelFocus = std::max (0, f);
            const auto y = runTone (p, fs, freqs[k].bin, N, amp);
            const auto h = analyseHarmonics (y, freqs[k].bin);
            thd[f + 1][k] = h.thd;
            std::printf ("   %5.0f Hz H2 %6.1f H3 %6.1f THD %5.2f%%", freqs[k].hz, h.h[1], h.h[2], h.thd);
        }
        std::printf ("\n");
    }
    check (thd[1][0] > 3.0 * thd[1][2], "Low focus: lows get the harmonics");
    check (thd[3][2] > 3.0 * thd[3][0], "High focus: highs get the harmonics");
    check (thd[2][1] > 2.0 * thd[0][1], "Flat focus adds harmonics");

    std::printf ("\n== Air: level change (dB) vs Air off, linear region ==\n  ");
    double gain[6] {};
    const double hz[6] = { 1000.0, 5000.0, 8000.0, 12000.0, 16000.0, 19000.0 };
    for (int k = 0; k < 6; ++k)
    {
        const int Nk = 16384;
        const int bin = (int) std::lround (hz[k] * Nk / fs) | 1;
        EngineParams p = cleanParams();
        p.inputDb = -24.0f;
        p.air = false;
        const auto off = runTone (p, fs, bin, Nk, 0.05);
        p.air = true;
        const auto on = runTone (p, fs, bin, Nk, 0.05);
        gain[k] = db (rms (on) / rms (off));
        std::printf ("%5.0f Hz %+5.2f   ", hz[k], gain[k]);
    }
    std::printf ("\n");
    check (std::abs (gain[0]) < 0.1, "Air leaves 1 kHz alone");
    check (gain[4] > 1.5 && gain[5] > gain[3], "Air lifts the top end, rising towards 20 kHz");

    // Small-signal response with the parallel path at 100 %: Focus must not act as a big
    // shelf, and the parallel path must not dull the top octave (any quality).
    std::printf ("\n== Parallel 100 %%: small-signal response vs parallel off (dB), Clean, -50 dBFS ==\n");
    const double rhz[7] = { 60.0, 200.0, 1000.0, 5000.0, 10000.0, 15000.0, 19500.0 };
    for (int f = 0; f < kNumParallelFocus; ++f)
        for (int q = 0; q < kNumQualities; q += 2)
        {
            std::printf ("  %s %dx", focusName[f + 1], 2 << q);
            double lo = 1.0e9, hi = -1.0e9;
            for (double h : rhz)
            {
                const int Nk = 16384;
                const int bin = (int) std::lround (h * Nk / fs) | 1;
                EngineParams p = cleanParams();
                p.quality = q;
                p.inputDb = -32.0f; // the tone below is -18 dBFS: -50 dBFS into the plug-in
                const auto off = runTone (p, fs, bin, Nk, 0.177);
                p.parallel = 1.0f;
                p.parallelFocus = f;
                const auto on = runTone (p, fs, bin, Nk, 0.177);
                const double g = analyseHarmonics (on, bin).fundamentalDb - analyseHarmonics (off, bin).fundamentalDb;
                lo = std::min (lo, g);
                hi = std::max (hi, g);
                std::printf ("  %5.0f %+5.2f", h, g);
            }
            std::printf ("   spread %.2f dB\n", hi - lo);
            check (hi - lo < (f == (int) ParallelFocus::Flat ? 0.1 : 2.6), "parallel focus is a gentle emphasis, not a shelf");
        }
}

void testAliasing()
{
    std::printf ("\n== Aliasing: worst in-band (<20 kHz) alias, 7 kHz at -6 dBFS peak, drive +18 dB, 48 kHz ==\n");
    const double fs = 48000.0;
    const int N = 65536;
    const int bin = 9557; // ~7 kHz, odd
    for (int s = 0; s < kNumStyles; ++s)
    {
        std::printf ("  %-12s", styleName (s));
        for (int linear = 0; linear < 2; ++linear)
            for (int q = 0; q < kNumQualities; ++q)
            {
                EngineParams p = cleanParams();
                p.style = s;
                p.driveDb = 18.0f;
                p.character = 0.35f;
                p.quality = q;
                p.linearPhase = linear == 1;
                const auto y = runTone (p, fs, bin, N, 0.5);
                std::printf ("  %s%dx %6.1f", linear ? "LP" : "MP", 2 << q, worstAliasDb (y, bin, fs));
            }
        std::printf (" dB\n");
    }
    std::printf ("  %-12s", "Glow+Par Alt");
    for (int linear = 0; linear < 2; ++linear)
        for (int q = 0; q < kNumQualities; ++q)
        {
            EngineParams p = cleanParams();
            p.style = (int) StyleId::Glow;
            p.driveDb = 18.0f;
            p.character = 0.35f;
            p.parallel = 1.0f;
            p.parallelAlt = true;
            p.quality = q;
            p.linearPhase = linear == 1;
            const auto y = runTone (p, fs, bin, N, 0.5);
            std::printf ("  %s%dx %6.1f", linear ? "LP" : "MP", 2 << q, worstAliasDb (y, bin, fs));
        }
    std::printf (" dB\n");
}

void testStability()
{
    std::printf ("\n== Stability: random automation, noise, extreme settings ==\n");
    std::mt19937 rng (12345);
    std::uniform_real_distribution<float> u01 (0.0f, 1.0f);
    std::normal_distribution<float> nd (0.0f, 1.0f);
    double maxOut = 0.0;
    bool allFinite = true;
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        for (int chans = 1; chans <= 2; ++chans)
        {
            SaturatorEngine eng;
            eng.prepare (fs, chans);
            EngineParams p = cleanParams();
            eng.setParameters (p, true);
            std::vector<float> a (4096), b (4096);
            for (int block = 0; block < 400; ++block)
            {
                p.inputDb = -24.0f + 48.0f * u01 (rng);
                p.driveDb = 36.0f * u01 (rng);
                p.character = u01 (rng);
                p.tone = -1.0f + 2.0f * u01 (rng);
                p.lowCutHz = kMinLowCutHz * std::pow (100.0f, u01 (rng));
                p.highCutHz = kMinHighCutHz * std::pow (20.0f, u01 (rng));
                p.mix = u01 (rng);
                p.outputDb = -24.0f + 48.0f * u01 (rng);
                p.autoGain = u01 (rng) > 0.3f;
                p.style = (int) (u01 (rng) * 3.999f);
                p.quality = (int) (u01 (rng) * 2.999f);
                p.linearPhase = u01 (rng) > 0.5f;
                p.stereoMode = u01 (rng) > 0.5f ? 1 : 0;
                p.bypass = u01 (rng) > 0.9f;
                p.parallel = u01 (rng) > 0.4f ? u01 (rng) : 0.0f;
                p.parallelFocus = (int) (u01 (rng) * 2.999f);
                p.parallelAlt = u01 (rng) > 0.5f;
                p.air = u01 (rng) > 0.5f;
                eng.setParameters (p);

                const int len = 1 + (int) (u01 (rng) * 4095.0f);
                const float level = std::pow (10.0f, (-60.0f + 66.0f * u01 (rng)) / 20.0f);
                for (int n = 0; n < len; ++n)
                {
                    const float x = (block % 17 == 0) ? (n == 0 ? 4.0f : 0.0f) : level * nd (rng);
                    a[(size_t) n] = x;
                    b[(size_t) n] = (block % 5 == 0) ? -x : x * 0.7f;
                }
                float* ptrs[2] = { a.data(), b.data() };
                eng.process (ptrs, chans, len);
                for (int c = 0; c < chans; ++c)
                    for (int n = 0; n < len; ++n)
                    {
                        const float v = ptrs[c][n];
                        allFinite = allFinite && std::isfinite (v);
                        maxOut = std::max (maxOut, (double) std::abs (v));
                    }
            }
        }
    }
    std::printf ("  all finite: %s, max |out| = %.2f\n", allFinite ? "yes" : "NO", maxOut);
    check (allFinite, "no NaN/Inf under random automation");
    check (maxOut < 200.0, "output bounded");

    // silence in -> silence out (no self-oscillation, no DC from bias)
    SaturatorEngine eng;
    eng.prepare (48000.0, 2);
    EngineParams p = cleanParams();
    p.style = (int) StyleId::Tube;
    p.driveDb = 36.0f;
    p.character = 1.0f;
    p.autoGain = false;
    eng.setParameters (p, true);
    std::vector<float> a (48000, 0.0f), b (48000, 0.0f);
    a[0] = 1.0f;
    float* ptrs[2] = { a.data(), b.data() };
    eng.process (ptrs, 2, 48000);
    double tail = 0.0;
    for (int n = 24000; n < 48000; ++n)
        tail = std::max (tail, (double) std::abs (a[(size_t) n]));
    std::printf ("  impulse tail after 0.5 s (Tube, +36 dB, char 100%%, auto off): %.1f dBFS\n", db (tail));
    check (tail < 1.0e-4, "decays to silence");
}

void testSwitchClicks()
{
    std::printf ("\n== Style / quality / phase switching and bypass: click check ==\n");
    const double fs = 48000.0;
    SaturatorEngine eng;
    eng.prepare (fs, 2);
    EngineParams p = cleanParams();
    p.style = 0;
    p.driveDb = 12.0f;
    p.character = 0.5f;
    p.parallel = 0.7f; // so that focus / alt switches are audible in the test
    eng.setParameters (p, true);
    eng.reset();

    const int total = (int) fs * 6;
    std::vector<float> l ((size_t) total), r ((size_t) total);
    for (int n = 0; n < total; ++n)
        l[(size_t) n] = r[(size_t) n] = (float) (0.25 * std::sin (2.0 * kPi * 110.0 * n / fs));

    double maxStep = 0.0;
    float prev = 0.0f;
    const int block = 256;
    int changes = 0;
    for (int pos = 0, k = 0; pos < total; pos += block, ++k)
    {
        if (k % 40 == 39)
        {
            ++changes;
            switch (changes % 8)
            {
                case 0: p.style = (p.style + 1) % kNumStyles; break;
                case 1: p.quality = (p.quality + 1) % kNumQualities; break;
                case 2: p.linearPhase = ! p.linearPhase; break;
                case 3: p.bypass = ! p.bypass; break;
                case 4: p.parallelFocus = (p.parallelFocus + 1) % kNumParallelFocus; break;
                case 5: p.parallelAlt = ! p.parallelAlt; break;
                case 6: p.air = ! p.air; break;
                default: p.stereoMode = 1 - p.stereoMode; break;
            }
            eng.setParameters (p);
        }
        float* ptrs[2] = { l.data() + pos, r.data() + pos };
        eng.process (ptrs, 2, std::min (block, total - pos));
        for (int n = pos; n < std::min (pos + block, total); ++n)
        {
            if (n > (int) fs / 4)
                maxStep = std::max (maxStep, (double) std::abs (l[(size_t) n] - prev));
            prev = l[(size_t) n];
        }
    }
    // a 110 Hz sine at ~0.25 peak changes by at most ~0.004 per sample; allow saturation edges
    std::printf ("  %d changes, largest sample-to-sample step %.4f\n", changes, maxStep);
    check (maxStep < 0.03, "no clicks when switching discrete settings");
}

void testCpu()
{
    std::printf ("\n== CPU: stereo, 48 kHz, 10 s of audio ==\n");
    const double fs = 48000.0;
    const int total = (int) fs * 10;
    std::vector<float> l ((size_t) total), r ((size_t) total);
    std::mt19937 rng (3);
    std::normal_distribution<float> nd (0.0f, 0.1f);
    for (int n = 0; n < total; ++n)
    {
        l[(size_t) n] = nd (rng);
        r[(size_t) n] = nd (rng);
    }
    for (int s = 0; s < kNumStyles; ++s)
    {
        std::printf ("  %-12s", styleName (s));
        for (int linear = 0; linear < 2; ++linear)
            for (int q = 0; q < kNumQualities; ++q)
            {
                SaturatorEngine eng;
                eng.prepare (fs, 2);
                EngineParams p = cleanParams();
                p.style = s;
                p.driveDb = 12.0f;
                p.character = 0.35f;
                p.quality = q;
                p.linearPhase = linear == 1;
                eng.setParameters (p, true);
                std::vector<float> a = l, b = r;
                const auto t0 = std::chrono::steady_clock::now();
                for (int pos = 0; pos < total; pos += 512)
                {
                    float* ptrs[2] = { a.data() + pos, b.data() + pos };
                    eng.process (ptrs, 2, std::min (512, total - pos));
                }
                const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
                std::printf ("  %s%dx %5.2f%%", linear ? "LP" : "MP", 2 << q, 100.0 * sec / 10.0);
            }
        std::printf ("  (of one core)\n");
    }
    std::printf ("  %-12s", "Glow+Par");
    for (int linear = 0; linear < 2; ++linear)
        for (int q = 0; q < kNumQualities; ++q)
        {
            SaturatorEngine eng;
            eng.prepare (fs, 2);
            EngineParams p = cleanParams();
            p.style = (int) StyleId::Glow;
            p.driveDb = 12.0f;
            p.character = 0.35f;
            p.parallel = 1.0f;
            p.air = true;
            p.quality = q;
            p.linearPhase = linear == 1;
            eng.setParameters (p, true);
            std::vector<float> a = l, b = r;
            const auto t0 = std::chrono::steady_clock::now();
            for (int pos = 0; pos < total; pos += 512)
            {
                float* ptrs[2] = { a.data() + pos, b.data() + pos };
                eng.process (ptrs, 2, std::min (512, total - pos));
            }
            const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            std::printf ("  %s%dx %5.2f%%", linear ? "LP" : "MP", 2 << q, 100.0 * sec / 10.0);
        }
    std::printf ("  (of one core)\n");
}
} // namespace

int main (int argc, char** argv)
{
    enableFlushToZero();
    const bool quick = argc > 1 && std::string (argv[1]) == "--quick";
    if (argc > 1 && std::string (argv[1]) == "--cpu")
    {
        testCpu();
        return 0;
    }

    const auto t0 = std::chrono::steady_clock::now();
    (void) AutoGainTable::get();
    std::printf ("auto-gain table built in %.0f ms\n",
                 1000.0 * std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());

    testFilters();
    testOversampler();
    testLatencyAndMix();
    testHarmonics();
    testFrequencyDependence();
    testAutoGain();
    testParallelAndAir();
    testAliasing();
    testStability();
    testSwitchClicks();
    if (! quick)
        testCpu();

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED", failures,
                 failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
