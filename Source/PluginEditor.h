#pragma once

#include <array>
#include <memory>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "ui/CurveDisplay.h"
#include "ui/PillowLookAndFeel.h"

class PillowSaturatorAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                  private juce::Timer
{
public:
    explicit PillowSaturatorAudioProcessorEditor (PillowSaturatorAudioProcessor&);
    ~PillowSaturatorAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::String title;
        juce::Rectangle<int> titleBounds;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment; // after slider
    };

    Knob& addKnob (const char* paramID, const juce::String& title, bool bipolar = false);
    static void placeKnob (Knob& knob, juce::Rectangle<int> area);
    void timerCallback() override;
    void drawMeter (juce::Graphics&, juce::Rectangle<float> area, float level, const juce::String& label) const;

    PillowSaturatorAudioProcessor& audioProcessor;
    pillow::ui::PillowLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 600 };

    pillow::ui::CurveDisplay curveDisplay;
    std::array<juce::TextButton, 4> styleButtons;
    juce::ToggleButton autoGainButton { "AUTO GAIN" }, linearPhaseButton { "LINEAR PHASE" };
    juce::ComboBox qualityBox, stereoBox;
    std::vector<std::unique_ptr<Knob>> knobs;
    Knob* driveKnob = nullptr;
    Knob* characterKnob = nullptr;
    Knob* toneKnob = nullptr;
    Knob* inputKnob = nullptr;
    Knob* outputKnob = nullptr;
    Knob* lowCutKnob = nullptr;
    Knob* highCutKnob = nullptr;
    Knob* mixKnob = nullptr;

    // attachments are declared after the components they control
    std::unique_ptr<juce::ParameterAttachment> styleAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoGainAttachment, linearPhaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> qualityAttachment, stereoAttachment;

    std::atomic<float>* rawStyle = nullptr;
    std::atomic<float>* rawInput = nullptr;
    std::atomic<float>* rawDrive = nullptr;
    std::atomic<float>* rawCharacter = nullptr;

    juce::Rectangle<int> meterArea, leftPanel, centrePanel, rightPanel;
    float inLevel = 0.0f, outLevel = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PillowSaturatorAudioProcessorEditor)
};
