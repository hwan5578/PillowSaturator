#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace pillow::params
{
// Parameter IDs are part of saved sessions/presets: never rename them once released.
namespace id
{
    inline constexpr const char* input       = "input";
    inline constexpr const char* drive       = "drive";
    inline constexpr const char* style       = "style";
    inline constexpr const char* character   = "character";
    inline constexpr const char* tone        = "tone";
    inline constexpr const char* lowCut      = "lowcut";
    inline constexpr const char* highCut     = "highcut";
    inline constexpr const char* mix         = "mix";
    inline constexpr const char* output      = "output";
    inline constexpr const char* autoGain    = "autogain";
    inline constexpr const char* quality     = "quality";
    inline constexpr const char* linearPhase = "linearphase";
    inline constexpr const char* stereoMode  = "stereomode";
} // namespace id

inline const juce::StringArray& styleNames()
{
    static const juce::StringArray names { "Tube", "Tape", "Transformer", "Clean" };
    return names;
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
} // namespace pillow::params
