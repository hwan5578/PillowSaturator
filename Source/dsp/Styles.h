#pragma once

// The styles. Every style shares one topology:
//   pre-EQ -> drive -> [oversampled curve(s) with bias] -> DC block -> post-EQ
// and differs only in its recipe (curve, EQ around the curve, bias and dynamic behaviour).
// Pre/post shelves are exact inverses, so at low level each style is (nearly) flat and the
// EQ only decides which frequencies hit the curve first.
// Glow chains two stages: a firm, symmetric first stage (Drive) into a soft, biased second
// stage whose drive / asymmetry / sag follow Character.

#include "Filters.h"
#include "Shapers.h"

namespace pillow::dsp
{
enum class StyleId : int { Tube = 0, Tape, Transformer, Clean, Glow };
constexpr int kNumStyles = 5;

struct FilterSpec
{
    enum class Type : int { None = 0, HP1, LP1, HP2, Bell, LowShelf, HighShelf };
    Type type = Type::None;
    float hz = 1000.0f;
    float q = 0.7071f;
    float db = 0.0f;
};

struct StyleSpec
{
    const char* name;
    CurveId curve;
    float sensitivity;   // signal scaling into the curve (calibrated at -18 dBFS = 0 VU)
    float biasScale;     // static bias at Character = 100 %  (even harmonics)
    float sagAmount;     // dynamic bias per unit of envelope at Character = 100 % (tube "sag");
                         // applies to the second stage when twoStage is set
    float hfLossAmount;  // level-dependent HF loss depth (tape), 0 = off
    FilterSpec pre[2];
    FilterSpec post[3];

    // optional second stage (Glow)
    bool twoStage = false;
    CurveId curve2 = CurveId::Soft;
    float stage2MaxDriveDb = 0.0f; // stage-2 drive at Character = 100 %
    float stage2BiasScale = 0.0f;  // stage-2 bias at Character = 100 %
};

//==============================================================================
// Parallel saturation: a band-selected copy of the (pre-drive) signal is driven hard into
// its own curve and summed into the main path *before* the main curve, so the added
// harmonics also go through the style. Two voicings; Alt is hotter and more asymmetric.
// Amount crossfades the focused band from clean to saturated and adds a gentle linear
// emphasis of that band (kParallelLinear), so Focus changes *where* the harmonics are made
// rather than acting as a big shelf EQ:
//   main += amount * (kParallelLinear * band + (sat(band) - band))
enum class ParallelFocus : int { Low = 0, Flat, High };
constexpr int kNumParallelFocus = 3;
constexpr double kParallelLowHz = 350.0, kParallelHighHz = 2200.0;
constexpr float kParallelLinear = 0.25f; // +1.9 dB emphasis of the focused band at 100 %

struct ParallelVoicing
{
    double driveDb;
    double bias;
};

inline const ParallelVoicing& getParallelVoicing (bool alt) noexcept
{
    static const ParallelVoicing voicings[2] = { { 12.0, 0.15 }, { 18.0, 0.32 } };
    return voicings[alt ? 1 : 0];
}

// Air: gentle lift from ~10 kHz rising towards 20 kHz, after the curve.
constexpr double kAirHz = 14000.0, kAirQ = 0.5, kAirDb = 4.0;

// Band limit (at the oversampled rate) between cascaded curves: after the parallel curve and
// between the two Glow stages. Like a real tube stage's bandwidth, it stops ultrasonic
// harmonics of one curve from intermodulating in the next and folding into the audio band.
constexpr double kInterstageHz = 30000.0;

inline const StyleSpec& getStyleSpec (StyleId id) noexcept
{
    using T = FilterSpec::Type;
    static const StyleSpec specs[kNumStyles] = {
        // Tube: firm tanh knee, bias -> 2nd harmonic, envelope pushes the bias further (sag)
        { "Tube", CurveId::Tanh, 1.00f, 0.30f, 0.35f, 0.0f,
          { { T::HP1, 22.0f, 0.7071f, 0.0f }, {} },
          { { T::LP1, 16000.0f, 0.7071f, 0.0f }, { T::Bell, 180.0f, 0.8f, 0.6f }, {} } },

        // Tape: soft knee, HF pre-/de-emphasis (highs saturate first), head bump, HF loss when hot
        { "Tape", CurveId::Soft, 0.85f, 0.20f, 0.0f, 0.55f,
          { { T::HighShelf, 3000.0f, 0.5f, 7.0f }, {} },
          { { T::HighShelf, 3000.0f, 0.5f, -7.0f }, { T::Bell, 55.0f, 1.0f, 1.5f }, { T::HP2, 18.0f, 0.6f, 0.0f } } },

        // Transformer: LF pre-/de-emphasis (iron saturates in the lows), small HF resonance
        { "Transformer", CurveId::Tanh, 1.00f, 0.35f, 0.0f, 0.0f,
          { { T::LowShelf, 160.0f, 0.5f, 9.0f }, {} },
          { { T::LowShelf, 160.0f, 0.5f, -9.0f }, { T::Bell, 17000.0f, 0.8f, 0.7f }, { T::HP1, 12.0f, 0.7071f, 0.0f } } },

        // Clean: cubic soft clip, no EQ - transparent until it gets loud
        { "Clean", CurveId::Cubic, 0.90f, 0.25f, 0.0f, 0.0f,
          { {}, {} },
          { {}, {}, {} } },

        // Glow: two tube-like stages in series. Stage 1 (Drive) is firm and symmetric and
        // makes harmonics early; stage 2 (Character) is soft, biased and sags when pushed.
        // Lows are emphasised into the stages and restored after (iron "weight").
        { "Glow", CurveId::Tanh, 1.00f, 0.0f, 0.30f, 0.0f,
          { { T::HP1, 20.0f, 0.7071f, 0.0f }, { T::LowShelf, 110.0f, 0.5f, 3.0f } },
          { { T::LowShelf, 110.0f, 0.5f, -3.0f }, { T::Bell, 90.0f, 0.7f, 0.7f }, {} },
          true, CurveId::Soft, 15.0f, 0.30f },
    };
    return specs[clampValue ((int) id, 0, kNumStyles - 1)];
}

//==============================================================================
// A fixed filter from a FilterSpec (one-pole for 6 dB/oct types, SVF otherwise).
struct StaticFilter
{
    FilterSpec::Type type = FilterSpec::Type::None;
    SvfCoeffs coeffs;
    SvfState svfState;
    OnePole onePole;

    void setup (const FilterSpec& spec, double fs) noexcept
    {
        using T = FilterSpec::Type;
        type = spec.type;
        switch (type)
        {
            case T::None:      break;
            case T::HP1:
            case T::LP1:       onePole.setCutoff (spec.hz, fs); break;
            case T::HP2:       coeffs = svf::highpass (spec.hz, spec.q, fs); break;
            case T::Bell:      coeffs = svf::bell (spec.hz, spec.q, spec.db, fs); break;
            case T::LowShelf:  coeffs = svf::lowShelf (spec.hz, spec.q, spec.db, fs); break;
            case T::HighShelf: coeffs = svf::highShelf (spec.hz, spec.q, spec.db, fs); break;
        }
    }

    void reset() noexcept
    {
        svfState.reset();
        onePole.reset();
    }

    float process (float x) noexcept
    {
        using T = FilterSpec::Type;
        switch (type)
        {
            case T::None:      return x;
            case T::HP1:       return onePole.highpass (x);
            case T::LP1:       return onePole.lowpass (x);
            case T::HP2:
            case T::Bell:
            case T::LowShelf:
            case T::HighShelf: return svfState.process (x, coeffs);
        }
        return x;
    }
};
} // namespace pillow::dsp
