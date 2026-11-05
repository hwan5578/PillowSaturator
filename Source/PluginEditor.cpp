#include "PluginEditor.h"

namespace pid = pillow::params::id;
namespace col = pillow::ui::colours;

namespace
{
constexpr int editorWidth = 1000;
constexpr int editorHeight = 540;

juce::ComboBox& fillFromChoices (juce::ComboBox& box, juce::RangedAudioParameter* param)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param))
        box.addItemList (choice->choices, 1);
    return box;
}
} // namespace

//==============================================================================
PillowSaturatorAudioProcessorEditor::PillowSaturatorAudioProcessorEditor (PillowSaturatorAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&lookAndFeel);
    auto& apvts = audioProcessor.apvts;

    addAndMakeVisible (curveDisplay);

    // ---- style selector (radio pills bound to the "style" choice parameter)
    const auto& names = pillow::params::styleNames();
    for (size_t i = 0; i < styleButtons.size(); ++i)
    {
        auto& b = styleButtons[i];
        b.setButtonText (names[(int) i].toUpperCase());
        b.setClickingTogglesState (false);
        b.onClick = [this, i] { styleAttachment->setValueAsCompleteGesture ((float) i); };
        addAndMakeVisible (b);
    }
    styleAttachment = std::make_unique<juce::ParameterAttachment> (
        *apvts.getParameter (pid::style),
        [this] (float value)
        {
            const int index = juce::roundToInt (value);
            for (size_t i = 0; i < styleButtons.size(); ++i)
                styleButtons[i].setToggleState ((int) i == index, juce::dontSendNotification);
        });
    styleAttachment->sendInitialUpdate();

    // ---- knobs
    driveKnob     = &addKnob (pid::drive, "DRIVE");
    characterKnob = &addKnob (pid::character, "CHARACTER");
    toneKnob      = &addKnob (pid::tone, "TONE", true);
    inputKnob     = &addKnob (pid::input, "INPUT", true);
    outputKnob    = &addKnob (pid::output, "OUTPUT", true);
    lowCutKnob    = &addKnob (pid::lowCut, "LOW CUT");
    highCutKnob   = &addKnob (pid::highCut, "HIGH CUT");
    mixKnob       = &addKnob (pid::mix, "MIX");
    driveKnob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 110, 22);

    // ---- toggles and menus
    addAndMakeVisible (autoGainButton);
    addAndMakeVisible (linearPhaseButton);
    autoGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, pid::autoGain, autoGainButton);
    linearPhaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, pid::linearPhase, linearPhaseButton);
    autoGainButton.setTooltip ("Keeps the output level matched to the input (calibrated at -18 dBFS = 0 VU).");
    linearPhaseButton.setTooltip ("Linear-phase oversampling filters (adds about 1.5 ms latency).");

    addAndMakeVisible (fillFromChoices (qualityBox, apvts.getParameter (pid::quality)));
    addAndMakeVisible (fillFromChoices (stereoBox, apvts.getParameter (pid::stereoMode)));
    qualityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, pid::quality, qualityBox);
    stereoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, pid::stereoMode, stereoBox);

    rawStyle = apvts.getRawParameterValue (pid::style);
    rawInput = apvts.getRawParameterValue (pid::input);
    rawDrive = apvts.getRawParameterValue (pid::drive);
    rawCharacter = apvts.getRawParameterValue (pid::character);

    setSize (editorWidth, editorHeight);
    startTimerHz (30);
}

PillowSaturatorAudioProcessorEditor::~PillowSaturatorAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

PillowSaturatorAudioProcessorEditor::Knob& PillowSaturatorAudioProcessorEditor::addKnob (const char* paramID,
                                                                                       const juce::String& title,
                                                                                       bool bipolar)
{
    auto knob = std::make_unique<Knob>();
    auto& s = knob->slider;
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 92, 20);
    s.getProperties().set ("bipolar", bipolar);
    knob->title = title;
    addAndMakeVisible (s);

    knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.apvts, paramID, s);
    if (auto* param = audioProcessor.apvts.getParameter (paramID))
        s.setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));

    knobs.push_back (std::move (knob));
    return *knobs.back();
}

void PillowSaturatorAudioProcessorEditor::placeKnob (Knob& knob, juce::Rectangle<int> area)
{
    knob.titleBounds = area.removeFromTop (18);
    knob.slider.setBounds (area);
}

//==============================================================================
void PillowSaturatorAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (64).reduced (24, 16);

    // header (right side): quality, linear phase, stereo mode
    stereoBox.setBounds (header.removeFromRight (120));
    header.removeFromRight (16);
    linearPhaseButton.setBounds (header.removeFromRight (150));
    header.removeFromRight (12);
    qualityBox.setBounds (header.removeFromRight (150));

    area = area.reduced (20, 8).withTrimmedBottom (14);
    meterArea = area.removeFromRight (40);
    area.removeFromRight (10);

    leftPanel = area.removeFromLeft (400);
    area.removeFromLeft (14);
    centrePanel = area.removeFromLeft (240);
    area.removeFromLeft (14);
    rightPanel = area;

    // left: curve + 2 x 2 style pills
    {
        constexpr int pillH = 34, gap = 8;
        auto r = leftPanel;
        auto pills = r.removeFromBottom (pillH * 2 + gap);
        r.removeFromBottom (12);
        curveDisplay.setBounds (r);

        auto rowA = pills.removeFromTop (pillH);
        pills.removeFromTop (gap);
        auto rowB = pills;
        const int w = (rowA.getWidth() - gap) / 2;
        styleButtons[0].setBounds (rowA.removeFromLeft (w));
        styleButtons[1].setBounds (rowA.withTrimmedLeft (gap));
        styleButtons[2].setBounds (rowB.removeFromLeft (w));
        styleButtons[3].setBounds (rowB.withTrimmedLeft (gap));
    }

    // centre: big drive knob, character + tone below
    {
        auto r = centrePanel.reduced (12, 14);
        placeKnob (*driveKnob, r.removeFromTop (240));
        r.removeFromTop (14);
        auto lower = r;
        placeKnob (*characterKnob, lower.removeFromLeft (lower.getWidth() / 2));
        placeKnob (*toneKnob, lower);
    }

    // right: 2 x 3 grid with breathing room between rows
    {
        constexpr int rowGap = 10;
        auto r = rightPanel.reduced (12, 14);
        const int rowH = (r.getHeight() - 2 * rowGap) / 3;
        auto row1 = r.removeFromTop (rowH);
        r.removeFromTop (rowGap);
        auto row2 = r.removeFromTop (rowH);
        r.removeFromTop (rowGap);
        auto row3 = r;
        placeKnob (*inputKnob, row1.removeFromLeft (row1.getWidth() / 2));
        placeKnob (*outputKnob, row1);
        placeKnob (*lowCutKnob, row2.removeFromLeft (row2.getWidth() / 2));
        placeKnob (*highCutKnob, row2);
        placeKnob (*mixKnob, row3.removeFromLeft (row3.getWidth() / 2));
        autoGainButton.setBounds (row3.withSizeKeepingCentre (row3.getWidth() - 6, 58)); // stacked toggle
    }
}

//==============================================================================
void PillowSaturatorAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::background);

    // header
    {
        auto header = getLocalBounds().removeFromTop (64).reduced (26, 0);
        g.setColour (col::text);
        g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
        g.drawText ("PILLOW", header.removeFromLeft (98), juce::Justification::centredLeft, false);
        g.setColour (col::accent);
        g.setFont (juce::FontOptions (22.0f));
        g.drawText ("SATURATOR", header.removeFromLeft (150), juce::Justification::centredLeft, false);
        g.setColour (col::outline);
        g.fillRect (juce::Rectangle<int> (20, 63, getWidth() - 40, 1));
    }

    // panels behind the knob sections
    for (const auto& panel : { centrePanel, rightPanel })
    {
        g.setColour (col::panel.withAlpha (0.55f));
        g.fillRoundedRectangle (panel.toFloat(), 16.0f);
        g.setColour (col::outline.withAlpha (0.7f));
        g.drawRoundedRectangle (panel.toFloat().reduced (0.5f), 16.0f, 1.0f);
    }

    // knob titles
    for (const auto& k : knobs)
    {
        const bool big = k.get() == driveKnob;
        g.setColour (big ? col::text : col::textDim);
        g.setFont (juce::FontOptions (big ? 14.0f : 11.5f, juce::Font::bold));
        g.drawText (k->title, k->titleBounds, juce::Justification::centred, false);
    }

    // meters
    auto m = meterArea.toFloat();
    const float w = (m.getWidth() - 6.0f) * 0.5f;
    drawMeter (g, m.removeFromLeft (w), inLevel, "IN");
    m.removeFromLeft (6.0f);
    drawMeter (g, m.removeFromLeft (w), outLevel, "OUT");
}

void PillowSaturatorAudioProcessorEditor::drawMeter (juce::Graphics& g, juce::Rectangle<float> area, float level,
                                                    const juce::String& label) const
{
    auto labelArea = area.removeFromBottom (16.0f);
    g.setColour (col::textDim);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (label, labelArea, juce::Justification::centred, false);

    area.removeFromBottom (4.0f);
    g.setColour (col::panel);
    g.fillRoundedRectangle (area, 4.0f);

    constexpr float minDb = -48.0f, maxDb = 6.0f;
    const float db = juce::Decibels::gainToDecibels (level, minDb);
    const float t = juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
    if (t > 0.0f)
    {
        auto fill = area.reduced (2.0f);
        fill = fill.removeFromBottom (fill.getHeight() * t);
        g.setColour (db > 0.0f ? juce::Colour (0xffe8664f) : col::accent);
        g.fillRoundedRectangle (fill, 3.0f);
    }

    // 0 dBFS tick
    const float zeroY = area.getBottom() - area.getHeight() * ((0.0f - minDb) / (maxDb - minDb));
    g.setColour (col::text.withAlpha (0.5f));
    g.fillRect (juce::Rectangle<float> (area.getX(), zeroY, area.getWidth(), 1.0f));
}

void PillowSaturatorAudioProcessorEditor::timerCallback()
{
    auto ballistics = [] (float current, float incoming) { return juce::jmax (incoming, current * 0.86f); };
    inLevel = ballistics (inLevel, audioProcessor.inputPeak.exchange (0.0f));
    outLevel = ballistics (outLevel, audioProcessor.outputPeak.exchange (0.0f));
    repaint (meterArea);

    curveDisplay.setState (juce::roundToInt (rawStyle->load()),
                           rawInput->load() + rawDrive->load(),
                           rawCharacter->load() * 0.01f,
                           audioProcessor.oversamplingFactor.load());
}
