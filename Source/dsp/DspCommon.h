#pragma once

// Pillow Saturator - framework-independent DSP core.
// Everything under Source/dsp is plain C++17 with no JUCE dependency, so it can be
// unit-tested and measured without a plug-in host (see tests/).

#include <algorithm>
#include <cmath>

namespace pillow::dsp
{
constexpr double kPi = 3.14159265358979323846;

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float gain) noexcept { return 20.0f * std::log10 (std::max (gain, 1.0e-9f)); }

template <typename T>
constexpr T clampValue (T v, T lo, T hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}
} // namespace pillow::dsp
