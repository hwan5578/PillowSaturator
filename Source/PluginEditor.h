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
    // Radio pills bound to a choice parameter.
    void addRadioPills (juce::TextButton* buttons, int count, const juce::StringArray& names,
                        const char* paramID, std::unique_ptr<juce::ParameterAttachment>& attachment);
    void updateDisplayState (bool force);
    void timerCallback() override;
    void drawMeter (juce::Graphics&, juce::Rectangle<float> area, float level, const juce::String& label) const;

    PillowSaturatorAudioProcessor& audioProcessor;
    pillow::ui::PillowLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 600 };

    pillow::ui::CurveDisplay curveDisplay;
    std::array<juce::TextButton, (size_t) pillow::dsp::kNumStyles> styleButtons;
    std::array<juce::TextButton, (size_t) pillow::dsp::kNumParallelFocus> focusButtons;
    juce::ToggleButton autoGainButton { "AUTO GAIN" }, linearPhaseButton { "LINEAR PHASE" };
    juce::ToggleButton parAltButton { "ALT" }, airButton;
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
    Knob* parallelKnob = nullptr;

    // attachments are declared after the components they control
    std::unique_ptr<juce::ParameterAttachment> styleAttachment, focusAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoGainAttachment, linearPhaseAttachment,
                                                                          parAltAttachment, airAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> qualityAttachment, stereoAttachment;

    std::atomic<float>* rawStyle = nullptr;
    std::atomic<float>* rawInput = nullptr;
    std::atomic<float>* rawDrive = nullptr;
    std::atomic<float>* rawCharacter = nullptr;
    std::atomic<float>* rawParallel = nullptr;
    std::atomic<float>* rawParFocus = nullptr;
    std::atomic<float>* rawParAlt = nullptr;
    std::atomic<float>* rawAir = nullptr;

    juce::Rectangle<int> meterArea, leftPanel, centrePanel, rightPanel, parallelPanel, airPanel;
    juce::Rectangle<int> parallelTitle, focusTitle, airTitle;
    int shownStyle = -1;
    bool parallelActive = false, airActive = false;
    float inLevel = 0.0f, outLevel = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PillowSaturatorAudioProcessorEditor)
};
