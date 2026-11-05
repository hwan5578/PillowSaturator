#include "Parameters.h"
#include "dsp/SaturatorEngine.h"

namespace pillow::params
{
namespace
{
    constexpr int version = 1; // ParameterID version hint (AU / VST3 parameter ordering)

    juce::NormalisableRange<float> logRange (float minValue, float maxValue)
    {
        return juce::NormalisableRange<float> (
            minValue, maxValue,
            [] (float start, float end, float t) { return start * std::pow (end / start, t); },
            [] (float start, float end, float v) { return std::log (v / start) / std::log (end / start); },
            [] (float start, float end, float v) { return juce::jlimit (start, end, v); });
    }

    juce::String formatDb (float v, int)
    {
        return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB";
    }

    juce::String formatPercent (float v, int)
    {
        return juce::String (juce::roundToInt (v)) + " %";
    }

    juce::String formatHz (float v)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz"
                            : juce::String (juce::roundToInt (v)) + " Hz";
    }

    float parseNumber (const juce::String& text)
    {
        return text.retainCharacters ("0123456789.,-+").replaceCharacter (',', '.').getFloatValue();
    }

    float parseHz (const juce::String& text)
    {
        const auto t = text.trim().toLowerCase();
        if (t.startsWith ("off"))
            return pillow::dsp::kMaxHighCutHz;
        const float v = parseNumber (t);
        return t.containsChar ('k') ? v * 1000.0f : v;
    }

    std::unique_ptr<juce::AudioParameterFloat> makeDb (const char* pid, const char* name, float lo, float hi, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { pid, version }, name, juce::NormalisableRange<float> (lo, hi, 0.01f), def,
            juce::AudioParameterFloatAttributes()
                .withLabel ("dB")
                .withStringFromValueFunction (formatDb)
                .withValueFromStringFunction (parseNumber));
    }

    std::unique_ptr<juce::AudioParameterFloat> makePercent (const char* pid, const char* name, float lo, float hi, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { pid, version }, name, juce::NormalisableRange<float> (lo, hi, 0.1f), def,
            juce::AudioParameterFloatAttributes()
                .withLabel ("%")
                .withStringFromValueFunction (formatPercent)
                .withValueFromStringFunction (parseNumber));
    }
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace pillow::dsp;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (makeDb (id::input, "Input", -24.0f, 24.0f, 0.0f));

    {
        auto range = juce::NormalisableRange<float> (0.0f, 36.0f, 0.01f);
        range.setSkewForCentre (12.0f);
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id::drive, version }, "Drive", range, 6.0f,
            juce::AudioParameterFloatAttributes()
                .withLabel ("dB")
                .withStringFromValueFunction (formatDb)
                .withValueFromStringFunction (parseNumber)));
    }

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { id::style, version }, "Style", styleNames(), 0));

    layout.add (makePercent (id::character, "Character", 0.0f, 100.0f, 35.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::tone, version }, "Tone", juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("%")
            .withStringFromValueFunction ([] (float v, int)
            {
                const int r = juce::roundToInt (v);
                if (r == 0)
                    return juce::String ("Flat");
                return (r < 0 ? "Dark " : "Bright ") + juce::String (std::abs (r));
            })
            .withValueFromStringFunction ([] (const juce::String& t)
            {
                const auto s = t.trim().toLowerCase();
                if (s.startsWith ("flat"))
                    return 0.0f;
                const float v = std::abs (parseNumber (s));
                return s.startsWith ("dark") || s.startsWith ("-") ? -v : v;
            })));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::lowCut, version }, "Low Cut", logRange (kMinLowCutHz, kMaxLowCutHz), kMinLowCutHz,
        juce::AudioParameterFloatAttributes()
            .withLabel ("Hz")
            .withStringFromValueFunction ([] (float v, int) { return formatHz (v); })
            .withValueFromStringFunction (parseHz)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::highCut, version }, "High Cut", logRange (kMinHighCutHz, kMaxHighCutHz), kMaxHighCutHz,
        juce::AudioParameterFloatAttributes()
            .withLabel ("Hz")
            .withStringFromValueFunction ([] (float v, int)
            {
                return v >= kHighCutOffHz ? juce::String ("Off") : formatHz (v);
            })
            .withValueFromStringFunction (parseHz)));

    layout.add (makePercent (id::mix, "Mix", 0.0f, 100.0f, 100.0f));
    layout.add (makeDb (id::output, "Output", -24.0f, 24.0f, 0.0f));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { id::autoGain, version }, "Auto Gain", true));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { id::quality, version }, "Quality",
        juce::StringArray { "Normal (2x)", "High (4x)", "Ultra (8x)" }, 1));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { id::linearPhase, version }, "Linear Phase", false));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { id::stereoMode, version }, "Stereo Mode",
        juce::StringArray { "Stereo", "Mid/Side" }, 0));

    return layout;
}
} // namespace pillow::params
