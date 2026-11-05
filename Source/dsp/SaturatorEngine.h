#pragma once

// Pillow Saturator engine: the complete audio path, independent of any plug-in framework.
//
//   in -> Input gain -> [M/S encode] -> Low Cut -> style pre-EQ -> Drive -> envelope/bias
//      -> upsample -> curve (ADAA) -> downsample (+ exact latency padding) -> DC block
//      -> style post-EQ -> Tone (tilt) -> High Cut -> Auto Gain -> quality padding
//      -> [M/S decode] -> Mix with latency-aligned dry -> Output gain -> bypass / switch fades
//
// Threading: prepare()/reset() from a non-audio thread while audio is stopped;
// setParameters()/process() from the audio thread. No allocation or locking in process().

#include <array>
#include "DspCommon.h"
#include "Filters.h"
#include "Oversampler.h"
#include "Shapers.h"
#include "Smoothing.h"
#include "Styles.h"

namespace pillow::dsp
{
enum class StereoMode : int { LeftRight = 0, MidSide = 1 };

constexpr int kNumQualities = 3;          // Normal (2x), High (4x), Ultra (8x) at 44.1/48 kHz
constexpr float kHighCutOffHz = 19990.0f; // at/above this the High Cut is switched off
constexpr float kMinLowCutHz = 10.0f, kMaxLowCutHz = 1000.0f;
constexpr float kMinHighCutHz = 1000.0f, kMaxHighCutHz = 20000.0f;
constexpr double kEnvAttackMs = 8.0, kEnvReleaseMs = 120.0;
constexpr double kTapeHfMaxHz = 19000.0, kTapeHfMinHz = 5000.0;

struct EngineParams
{
    float inputDb = 0.0f;       // -24 .. +24
    float driveDb = 6.0f;       //   0 .. +36
    float character = 0.35f;    //   0 .. 1   (0 = odd harmonics only)
    float tone = 0.0f;          //  -1 .. +1  (dark .. bright tilt)
    float lowCutHz = kMinLowCutHz;
    float highCutHz = kMaxHighCutHz;
    float mix = 1.0f;           //   0 .. 1
    float outputDb = 0.0f;      // -24 .. +24
    bool autoGain = true;
    int style = 0;              // StyleId
    int quality = 1;            // 0..2
    bool linearPhase = false;
    int stereoMode = 0;         // StereoMode
    bool bypass = false;
};

//==============================================================================
// Static loudness compensation, measured once per process: for every style / drive /
// character, the RMS ratio of a -18 dBFS (0 VU) 1 kHz sine before and after the style
// (pre-EQ, curve incl. steady-state sag, post-EQ). With Auto Gain on the output level
// therefore matches the input level at the reference, whatever the drive.
class AutoGainTable
{
public:
    static constexpr float minDriveDb = -24.0f;
    static constexpr float driveStepDb = 2.0f;
    static constexpr int numDrive = 43; // -24 .. +60 dB (input + drive)
    static constexpr int numChar = 6;   // 0, 0.2 .. 1.0
    static constexpr double referenceDbfs = -18.0;

    static const AutoGainTable& get()
    {
        static const AutoGainTable table;
        return table;
    }

    float lookupDb (int style, float driveDb, float character) const noexcept
    {
        const auto& t = table[(size_t) clampValue (style, 0, kNumStyles - 1)];
        const float dPos = clampValue ((driveDb - minDriveDb) / driveStepDb, 0.0f, (float) (numDrive - 1));
        const int d0 = std::min ((int) dPos, numDrive - 2);
        const float df = dPos - (float) d0;
        const float cPos = clampValue (character, 0.0f, 1.0f) * (float) (numChar - 1);
        const int c0 = std::min ((int) cPos, numChar - 2);
        const float cf = cPos - (float) c0;

        const auto& r0 = t[(size_t) d0];
        const auto& r1 = t[(size_t) d0 + 1];
        const float a = r0[(size_t) c0] + (r1[(size_t) c0] - r0[(size_t) c0]) * df;
        const float b = r0[(size_t) c0 + 1] + (r1[(size_t) c0 + 1] - r0[(size_t) c0 + 1]) * df;
        return a + (b - a) * cf;
    }

    // Steady-state measurement of one entry (also used by the tests).
    static float measureCompensationDb (int style, float driveDb, float character)
    {
        const auto& spec = getStyleSpec ((StyleId) style);
        constexpr double fs = 48000.0;
        constexpr int period = 48; // 1 kHz
        constexpr int warmup = period * 50, measure = period * 10;

        StaticFilter pre[2], post[3];
        for (int i = 0; i < 2; ++i)
            pre[i].setup (spec.pre[i], fs);
        for (int i = 0; i < 3; ++i)
            post[i].setup (spec.post[i], fs);
        EnvelopeFollower env;
        env.prepare (fs, kEnvAttackMs, kEnvReleaseMs);
        OnePole hfLoss;

        const double amp = std::sqrt (2.0) * std::pow (10.0, referenceDbfs / 20.0);
        const double g = std::pow (10.0, driveDb / 20.0) * spec.sensitivity;
        const double bStatic = spec.biasScale * character;
        const double norm = 1.0 / curveSlope (spec.curve, bStatic);
        const double sag = spec.sagAmount * character;

        double sumY = 0.0, sumY2 = 0.0, sumX2 = 0.0;
        for (int n = 0; n < warmup + measure; ++n)
        {
            const double x = amp * std::sin (2.0 * kPi * (double) (n % period) / (double) period);
            float v = (float) x;
            v = pre[0].process (v);
            v = pre[1].process (v);
            const double u = (double) v * g;
            const float e = env.process ((float) u);
            const double b = clampValue (bStatic + sag * (double) e, -1.2, 1.2);
            float y = (float) (norm * (curveF (spec.curve, u + b) - curveF (spec.curve, b)) / spec.sensitivity);
            y = post[0].process (y);
            y = post[1].process (y);
            y = post[2].process (y);
            if (spec.hfLossAmount > 0.0f)
            {
                hfLoss.setCutoff (std::max (kTapeHfMinHz, kTapeHfMaxHz / (1.0 + spec.hfLossAmount * e)), fs);
                y = hfLoss.lowpass (y);
            }
            if (n >= warmup)
            {
                sumY += y;
                sumY2 += (double) y * y;
                sumX2 += x * x;
            }
        }
        const double meanY = sumY / measure;
        const double varY = std::max (1.0e-30, sumY2 / measure - meanY * meanY);
        const double ratio = std::sqrt ((sumX2 / measure) / varY);
        return (float) clampValue (20.0 * std::log10 (ratio), -72.0, 24.0);
    }

private:
    AutoGainTable()
    {
        for (int s = 0; s < kNumStyles; ++s)
            for (int d = 0; d < numDrive; ++d)
                for (int c = 0; c < numChar; ++c)
                    table[(size_t) s][(size_t) d][(size_t) c] = measureCompensationDb (
                        s, minDriveDb + driveStepDb * (float) d, (float) c / (float) (numChar - 1));
    }

    std::array<std::array<std::array<float, numChar>, numDrive>, kNumStyles> table {};
};

//==============================================================================
class SaturatorEngine
{
public:
    static constexpr int kChunk = 64;     // internal block size (any host block size works)
    static constexpr int kMaxChannels = 2;
    static constexpr int kMaxDelay = 256; // >= max latency in samples

    SaturatorEngine() : autoGainTable (&AutoGainTable::get())
    {
        (void) halfband::Designs::get();
    }

    //==============================================================================
    void prepare (double sampleRate, int numChannels)
    {
        fs = sampleRate;
        numCh = clampValue (numChannels, 1, kMaxChannels);

        inputDb.prepare (fs, 0.05);
        driveDb.prepare (fs, 0.05);
        character.prepare (fs, 0.05);
        tone.prepare (fs, 0.05);
        lowCutLog.prepare (fs, 0.05);
        highCutLog.prepare (fs, 0.05);
        highCutOn.prepare (fs, 0.03);
        mix.prepare (fs, 0.05);
        outputDb.prepare (fs, 0.05);
        autoAmount.prepare (fs, 0.05);

        fadeStep = 1.0f / (float) std::max (1.0, fs * 0.006);    // 6 ms switch fades
        bypassStep = 1.0f / (float) std::max (1.0, fs * 0.012);  // 12 ms bypass crossfade
        settleSamples = (int) (fs * 0.002) + kChunk;

        for (auto& c : channels)
        {
            c.os.prepare (kChunk);
            c.dry.prepare (kMaxDelay);
            c.wetPad.prepare (kMaxDelay);
            c.dc.prepare (fs, 5.0);
            c.env.prepare (fs, kEnvAttackMs, kEnvReleaseMs);
        }

        // latency of every quality at this sample rate, per phase mode
        for (int ph = 0; ph < 2; ++ph)
        {
            modeLatency[(size_t) ph] = 0;
            for (int q = 0; q < kNumQualities; ++q)
            {
                const int lat = Oversampler::latencyFor (stagesFor (q), ph == 1);
                qualityLatency[(size_t) ph][(size_t) q] = lat;
                modeLatency[(size_t) ph] = std::max (modeLatency[(size_t) ph], lat);
            }
        }

        prepared = true;
        setParameters (lastParams, true); // applies the discrete settings immediately
        reset();
    }

    void reset() noexcept
    {
        for (LinearSmoother* s : { &inputDb, &driveDb, &character, &tone, &lowCutLog, &highCutLog,
                                   &highCutOn, &mix, &outputDb, &autoAmount })
            s->setImmediate (s->getTarget());

        resetWet();
        for (auto& c : channels)
            c.dry.reset();

        duck = 1.0f;
        duckState = DuckState::Idle;
        holdCount = 0;

        bypassAmount = bypassTarget ? 1.0f : 0.0f;
        bypassState = bypassTarget ? BypassState::Bypassed : BypassState::Active;
        preRollCount = 0;
        coeffsDirty = true;
    }

    //==============================================================================
    void setParameters (const EngineParams& p, bool immediate = false) noexcept
    {
        lastParams = p;

        auto set = [immediate] (LinearSmoother& s, float v)
        {
            if (immediate)
                s.setImmediate (v);
            else
                s.setTarget (v);
        };

        set (inputDb, clampValue (p.inputDb, -24.0f, 24.0f));
        set (driveDb, clampValue (p.driveDb, 0.0f, 36.0f));
        set (character, clampValue (p.character, 0.0f, 1.0f));
        set (tone, clampValue (p.tone, -1.0f, 1.0f));
        set (lowCutLog, std::log (clampValue (p.lowCutHz, kMinLowCutHz, kMaxLowCutHz)));
        set (highCutLog, std::log (clampValue (p.highCutHz, kMinHighCutHz, kMaxHighCutHz)));
        set (highCutOn, p.highCutHz >= kHighCutOffHz ? 0.0f : 1.0f);
        set (mix, clampValue (p.mix, 0.0f, 1.0f));
        set (outputDb, clampValue (p.outputDb, -24.0f, 24.0f));
        set (autoAmount, p.autoGain ? 1.0f : 0.0f);
        bypassTarget = p.bypass;

        Discrete d;
        d.style = clampValue (p.style, 0, kNumStyles - 1);
        d.quality = clampValue (p.quality, 0, kNumQualities - 1);
        d.linear = p.linearPhase;
        d.stereo = clampValue (p.stereoMode, 0, 1);
        pending = d;

        if (immediate)
        {
            coeffsDirty = true;
            if (prepared)
            {
                applyDiscrete (pending);
                duck = 1.0f;
                duckState = DuckState::Idle;
            }
        }
    }

    //==============================================================================
    // In-place processing of up to two channels; any block size.
    void process (float* const* io, int numChannels, int numSamples) noexcept
    {
        if (! prepared || numSamples <= 0 || numChannels <= 0)
            return;

        const int nch = std::min (numChannels, numCh);
        for (int start = 0; start < numSamples; start += kChunk)
        {
            const int n = std::min (kChunk, numSamples - start);
            float* ptrs[kMaxChannels] = { io[0] + start, nch > 1 ? io[1] + start : nullptr };
            processChunk (ptrs, nch, n);
        }
    }

    int getLatencySamples() const noexcept { return modeLatency[active.linear ? 1 : 0]; }
    int getOversamplingFactor() const noexcept { return 1 << stagesFor (active.quality); }
    int getActiveStyle() const noexcept { return active.style; }

    // Oversampling stages for a quality setting, reduced at high base rates (minimum 2x).
    int stagesFor (int quality) const noexcept
    {
        const int shift = fs >= 160000.0 ? 2 : (fs >= 80000.0 ? 1 : 0);
        return clampValue (quality + 1 - shift, 1, Oversampler::maxStages);
    }

    // Static transfer curve for UI display: output for input x (full scale = 1).
    static float evaluateStaticCurve (int style, float x, float totalDriveDb, float characterAmount) noexcept
    {
        const auto& spec = getStyleSpec ((StyleId) style);
        const double g = std::pow (10.0, totalDriveDb / 20.0) * spec.sensitivity;
        const double b = spec.biasScale * clampValue (characterAmount, 0.0f, 1.0f);
        const double norm = 1.0 / curveSlope (spec.curve, b);
        return (float) (norm * (curveF (spec.curve, x * g + b) - curveF (spec.curve, b)) / spec.sensitivity);
    }

private:
    struct Discrete
    {
        int style = 0, quality = 1, stereo = 0;
        bool linear = false;
        bool operator== (const Discrete& o) const noexcept
        {
            return style == o.style && quality == o.quality && stereo == o.stereo && linear == o.linear;
        }
    };

    enum class DuckState { Idle, Out, Hold, In };
    enum class BypassState { Active, Bypassing, Bypassed, PreRoll };

    struct Channel
    {
        Oversampler os;
        DelayLine dry, wetPad;
        SvfState lowCut, toneLow, toneHigh, highCut;
        StaticFilter pre[2], post[3];
        OnePole tapeLP;
        DcBlocker dc;
        EnvelopeFollower env;
        AdaaState adaa;

        std::array<float, kChunk> wet {}, dryOut {}, envBuf {}, biasBuf {}, offBuf {};
    };

    //==============================================================================
    void resetWet() noexcept
    {
        for (auto& c : channels)
        {
            c.os.reset();
            c.wetPad.reset();
            c.lowCut.reset();
            c.toneLow.reset();
            c.toneHigh.reset();
            c.highCut.reset();
            for (auto& f : c.pre)
                f.reset();
            for (auto& f : c.post)
                f.reset();
            c.tapeLP.reset();
            c.dc.reset();
            c.env.reset();
            c.adaa.reset();
        }
    }

    void applyDiscrete (const Discrete& d) noexcept
    {
        active = d;
        spec = &getStyleSpec ((StyleId) active.style);

        const int stages = stagesFor (active.quality);
        const int ph = active.linear ? 1 : 0;
        const int pad = modeLatency[(size_t) ph] - qualityLatency[(size_t) ph][(size_t) active.quality];

        for (auto& c : channels)
        {
            for (int i = 0; i < 2; ++i)
                c.pre[i].setup (spec->pre[i], fs);
            for (int i = 0; i < 3; ++i)
                c.post[i].setup (spec->post[i], fs);
            c.os.configure (stages, active.linear);
            c.wetPad.setDelay (pad);
            c.dry.setDelay (modeLatency[(size_t) ph]); // the jump happens while muted
        }
        resetWet();
    }

    // Filter cutoffs are updated at chunk rate (64 samples); the smoothers advance by `n`.
    void updateCoefficients (int n) noexcept
    {
        const bool force = coeffsDirty;
        coeffsDirty = false;

        const bool lcMoving = lowCutLog.isSmoothing();
        const float lc = lowCutLog.skip (n);
        if (lcMoving || force)
            lowCutCoeffs = svf::highpass (std::exp ((double) lc), 0.70710678, fs);

        const bool hcMoving = highCutLog.isSmoothing();
        const float hc = highCutLog.skip (n);
        if (hcMoving || force)
            highCutCoeffs = svf::lowpass (std::exp ((double) hc), 0.70710678, fs);

        const bool toneMoving = tone.isSmoothing();
        const float t = tone.skip (n);
        if (toneMoving || force)
        {
            toneLowCoeffs = svf::lowShelf (700.0, 0.5, -4.5 * (double) t, fs);
            toneHighCoeffs = svf::highShelf (700.0, 0.5, 4.5 * (double) t, fs);
        }
    }

    // Switch fade: out (6 ms) -> apply change -> hold muted until the new path is filled -> in.
    void advanceDuck (int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            switch (duckState)
            {
                case DuckState::Idle: break;
                case DuckState::Out:  duck = std::max (0.0f, duck - fadeStep); break;
                case DuckState::Hold:
                    duck = 0.0f;
                    if (--holdCount <= 0)
                        duckState = DuckState::In;
                    break;
                case DuckState::In:
                    duck = std::min (1.0f, duck + fadeStep);
                    if (duck >= 1.0f)
                        duckState = DuckState::Idle;
                    break;
            }
            ctlDuck[(size_t) i] = duck;
        }
    }

    // Bypass crossfade; when leaving a full bypass the wet path pre-rolls (muted) first.
    void advanceBypass (int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            switch (bypassState)
            {
                case BypassState::Active:
                    bypassAmount = std::max (0.0f, bypassAmount - bypassStep);
                    break;
                case BypassState::Bypassing:
                    bypassAmount = std::min (1.0f, bypassAmount + bypassStep);
                    break;
                case BypassState::Bypassed:
                    bypassAmount = 1.0f;
                    break;
                case BypassState::PreRoll:
                    bypassAmount = 1.0f;
                    if (--preRollCount <= 0)
                        bypassState = BypassState::Active;
                    break;
            }
            ctlBypass[(size_t) i] = bypassAmount;
        }
    }

    void processChunk (float* const* io, int nch, int n) noexcept
    {
        // ---- discrete changes (style / quality / phase / stereo): fade out, switch, hold, fade in
        if (duckState == DuckState::Out && duck <= 0.0f)
        {
            applyDiscrete (pending);
            duckState = DuckState::Hold;
            holdCount = getLatencySamples() + settleSamples;
        }
        if (duckState == DuckState::Idle && ! (pending == active))
            duckState = DuckState::Out;
        else if (duckState == DuckState::In && ! (pending == active))
            duckState = DuckState::Out;

        // ---- bypass state transitions
        if (bypassTarget)
        {
            if (bypassState == BypassState::Active || bypassState == BypassState::PreRoll)
                bypassState = BypassState::Bypassing;
            else if (bypassState == BypassState::Bypassing && bypassAmount >= 1.0f)
                bypassState = BypassState::Bypassed;
        }
        else if (bypassState == BypassState::Bypassing)
        {
            bypassState = BypassState::Active; // wet path still running: just fade back
        }
        else if (bypassState == BypassState::Bypassed)
        {
            resetWet();
            bypassState = BypassState::PreRoll;
            preRollCount = getLatencySamples() + settleSamples;
        }

        advanceDuck (n);
        advanceBypass (n);
        updateCoefficients (n);

        // ---- dry path (always running so the delay line stays filled)
        for (int c = 0; c < nch; ++c)
            for (int i = 0; i < n; ++i)
                channels[(size_t) c].dryOut[(size_t) i] = channels[(size_t) c].dry.process (io[c][i]);

        // ---- fully bypassed: skip the wet path entirely
        if (bypassState == BypassState::Bypassed)
        {
            for (int i = 0; i < n; ++i)
            {
                // keep the smoothers moving so nothing jumps when coming back
                inputDb.next(); driveDb.next(); character.next(); highCutOn.next();
                mix.next(); outputDb.next(); autoAmount.next();
            }
            for (int c = 0; c < nch; ++c)
                for (int i = 0; i < n; ++i)
                    io[c][i] = channels[(size_t) c].dryOut[(size_t) i] * ctlDuck[(size_t) i];
            return;
        }

        // ---- shared per-sample controls
        const CurveId curve = spec->curve;
        const float sens = spec->sensitivity;
        for (int i = 0; i < n; ++i)
        {
            const float inDb = inputDb.next();
            const float drDb = driveDb.next();
            const float ch = character.next();
            ctlIn[(size_t) i] = dbToGain (inDb);
            ctlDrive[(size_t) i] = dbToGain (drDb) * sens;
            ctlChar[(size_t) i] = ch;

            const float bStatic = spec->biasScale * ch;
            ctlBias[(size_t) i] = bStatic;
            ctlNorm[(size_t) i] = (float) (1.0 / curveSlope (curve, bStatic));
            ctlOffset[(size_t) i] = (float) curveF (curve, bStatic);

            const float compDb = autoGainTable->lookupDb (active.style, inDb + drDb, ch);
            ctlOutScale[(size_t) i] = dbToGain (autoAmount.next() * compDb) / sens;
            ctlMix[(size_t) i] = mix.next();
            ctlOut[(size_t) i] = dbToGain (outputDb.next());
            ctlHcOn[(size_t) i] = highCutOn.next();
        }

        const bool midSide = (active.stereo == (int) StereoMode::MidSide) && nch == 2;
        const bool sag = spec->sagAmount > 0.0f;
        const float sagAmount = spec->sagAmount;
        const bool hfLoss = spec->hfLossAmount > 0.0f;
        const double hfAmount = spec->hfLossAmount;

        // ---- input, M/S encode, low cut, pre-EQ, drive, envelope, bias  (base rate)
        for (int i = 0; i < n; ++i)
        {
            float l = io[0][i] * ctlIn[(size_t) i];
            float r = nch > 1 ? io[1][i] * ctlIn[(size_t) i] : 0.0f;
            if (midSide)
            {
                const float m = 0.5f * (l + r), s = 0.5f * (l - r);
                l = m;
                r = s;
            }
            const float in[kMaxChannels] = { l, r };

            for (int c = 0; c < nch; ++c)
            {
                auto& st = channels[(size_t) c];
                float x = st.lowCut.process (in[c], lowCutCoeffs);
                x = st.pre[0].process (x);
                x = st.pre[1].process (x);
                const float u = x * ctlDrive[(size_t) i];
                const float e = st.env.process (u);
                st.envBuf[(size_t) i] = e;

                if (sag)
                {
                    const float b = clampValue (ctlBias[(size_t) i] + sagAmount * ctlChar[(size_t) i] * e, -1.2f, 1.2f);
                    st.biasBuf[(size_t) i] = b;
                    st.offBuf[(size_t) i] = (float) curveF (curve, b);
                }
                else
                {
                    st.biasBuf[(size_t) i] = ctlBias[(size_t) i];
                    st.offBuf[(size_t) i] = ctlOffset[(size_t) i];
                }
                st.wet[(size_t) i] = u;
            }
        }

        // ---- oversampled curve
        for (int c = 0; c < nch; ++c)
        {
            auto& st = channels[(size_t) c];
            float* hi = st.os.processUp (st.wet.data(), n);
            processCurveBlock (curve, hi, n, st.os.getFactor(), st.biasBuf.data(), ctlNorm.data(),
                               st.offBuf.data(), st.adaa);
            st.os.processDown (st.wet.data(), n);
        }

        // ---- post: DC block, style post-EQ, tone, high cut, auto gain, quality padding
        for (int c = 0; c < nch; ++c)
        {
            auto& st = channels[(size_t) c];
            for (int i = 0; i < n; ++i)
            {
                float y = st.dc.process (st.wet[(size_t) i]);
                y = st.post[0].process (y);
                y = st.post[1].process (y);
                y = st.post[2].process (y);
                if (hfLoss)
                {
                    st.tapeLP.setCutoff (std::max (kTapeHfMinHz, kTapeHfMaxHz / (1.0 + hfAmount * st.envBuf[(size_t) i])), fs);
                    y = st.tapeLP.lowpass (y);
                }
                y = st.toneLow.process (y, toneLowCoeffs);
                y = st.toneHigh.process (y, toneHighCoeffs);
                const float yh = st.highCut.process (y, highCutCoeffs);
                y += ctlHcOn[(size_t) i] * (yh - y);
                y *= ctlOutScale[(size_t) i];
                st.wet[(size_t) i] = st.wetPad.process (y);
            }
        }

        // ---- safety: never let a non-finite value escape
        bool finite = true;
        for (int c = 0; c < nch; ++c)
            for (int i = 0; i < n; ++i)
                finite = finite && std::isfinite (channels[(size_t) c].wet[(size_t) i]);
        if (! finite)
        {
            resetWet();
            for (int c = 0; c < nch; ++c)
                channels[(size_t) c].wet.fill (0.0f);
        }

        // ---- M/S decode, mix, output, bypass + switch fades
        for (int i = 0; i < n; ++i)
        {
            float wl = channels[0].wet[(size_t) i];
            float wr = nch > 1 ? channels[1].wet[(size_t) i] : 0.0f;
            if (midSide)
            {
                const float l = wl + wr, r = wl - wr;
                wl = l;
                wr = r;
            }
            const float m = ctlMix[(size_t) i], g = ctlOut[(size_t) i];
            const float bm = ctlBypass[(size_t) i], dk = ctlDuck[(size_t) i];

            const float dl = channels[0].dryOut[(size_t) i];
            io[0][i] = dk * ((1.0f - bm) * g * (dl + m * (wl - dl)) + bm * dl);
            if (nch > 1)
            {
                const float dr = channels[1].dryOut[(size_t) i];
                io[1][i] = dk * ((1.0f - bm) * g * (dr + m * (wr - dr)) + bm * dr);
            }
        }
    }

    //==============================================================================
    const AutoGainTable* autoGainTable = nullptr;
    const StyleSpec* spec = &getStyleSpec (StyleId::Tube);

    double fs = 48000.0;
    int numCh = 2;
    bool prepared = false;

    LinearSmoother inputDb, driveDb, character, tone, lowCutLog, highCutLog, highCutOn,
                   mix, outputDb, autoAmount;
    bool coeffsDirty = true;

    SvfCoeffs lowCutCoeffs, highCutCoeffs, toneLowCoeffs, toneHighCoeffs;

    Discrete active, pending;
    DuckState duckState = DuckState::Idle;
    float duck = 1.0f, fadeStep = 0.004f;
    int holdCount = 0, settleSamples = 160;

    bool bypassTarget = false;
    BypassState bypassState = BypassState::Active;
    float bypassAmount = 0.0f, bypassStep = 0.002f;
    int preRollCount = 0;

    std::array<int, 2> modeLatency {};
    std::array<std::array<int, kNumQualities>, 2> qualityLatency {};

    EngineParams lastParams;
    std::array<Channel, kMaxChannels> channels;

    std::array<float, kChunk> ctlDuck {}, ctlIn {}, ctlDrive {}, ctlChar {}, ctlBias {}, ctlNorm {},
                              ctlOffset {}, ctlOutScale {}, ctlMix {}, ctlOut {}, ctlBypass {}, ctlHcOn {};
};
} // namespace pillow::dsp
