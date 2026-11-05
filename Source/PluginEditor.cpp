#include "PluginEditor.h"

namespace pid = pillow::params::id;
namespace col = pillow::ui::colours;

namespace
{
constexpr int editorWidth = 1000;
constexpr int editorHeight = 664;

juce::ComboBox& fillFromChoices (juce::ComboBox& box, juce::RangedAudioParameter* param)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param))
        box.addItemList (choice->choices, 1);
    return box;
}

const char* styleTooltip (int style)
{
    switch (style)
    {
        case 0:  return "Tube: firm knee, bias adds even harmonics, sags when pushed.";
        case 1:  return "Tape: soft knee, highs saturate first, head bump, HF loss when hot.";
        case 2:  return "Transformer: the lows saturate first, small top-end resonance.";
        case 3:  return "Clean: transparent until it gets loud.";
        case 4:  return "Glow: two tube stages in series. Drive pushes the first, 2nd Stage the second.";
        default: return "";
    }
}
} // namespace

//==============================================================================
PillowSaturatorAudioProcessorEditor::PillowSaturatorAudioProcessorEditor (PillowSaturatorAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&lookAndFeel);
    auto& apvts = audioProcessor.apvts;

    addAndMakeVisible (curveDisplay);

    // ---- style selector and parallel focus (radio pills bound to choice parameters)
    addRadioPills (styleButtons.data(), (int) styleButtons.size(), pillow::params::styleNames(), pid::style,
                   styleAttachment);
    for (size_t i = 0; i < styleButtons.size(); ++i)
        styleButtons[i].setTooltip (styleTooltip ((int) i));

    addRadioPills (focusButtons.data(), (int) focusButtons.size(), pillow::params::parallelFocusNames(),
                   pid::parFocus, focusAttachment);
    focusButtons[0].setTooltip ("The parallel path hears the lows (below about 350 Hz).");
    focusButtons[1].setTooltip ("The parallel path hears the full range.");
    focusButtons[2].setTooltip ("The parallel path hears the highs (above about 2.2 kHz).");

    // ---- knobs
    driveKnob     = &addKnob (pid::drive, "DRIVE");
    characterKnob = &addKnob (pid::character, "CHARACTER");
    toneKnob      = &addKnob (pid::tone, "TONE", true);
    inputKnob     = &addKnob (pid::input, "INPUT", true);
    outputKnob    = &addKnob (pid::output, "OUTPUT", true);
    lowCutKnob    = &addKnob (pid::lowCut, "LOW CUT");
    highCutKnob   = &addKnob (pid::highCut, "HIGH CUT");
    mixKnob       = &addKnob (pid::mix, "MIX");
    parallelKnob  = &addKnob (pid::parallel, "AMOUNT");
    driveKnob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 110, 22);
    parallelKnob->slider.setTooltip ("Parallel saturation: a hard-driven copy of the focused band is blended in "
                                     "before the main stage (Auto Gain keeps the level).");

    // ---- toggles and menus
    for (auto* b : { &autoGainButton, &linearPhaseButton, &parAltButton, &airButton })
        addAndMakeVisible (b);
    autoGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, pid::autoGain, autoGainButton);
    linearPhaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, pid::linearPhase, linearPhaseButton);
    parAltAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, pid::parAlt, parAltButton);
    airAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, pid::air, airButton);
    autoGainButton.setTooltip ("Keeps the output level matched to the input (calibrated at -18 dBFS = 0 VU).");
    linearPhaseButton.setTooltip ("Linear-phase oversampling filters (adds about 1.5 ms latency).");
    parAltButton.setTooltip ("Alternative parallel voicing: hotter and more asymmetric.");
    airButton.setTooltip ("Soft high-shelf lift after the saturation, rising above about 10 kHz.");
    airButton.setTitle ("Air"); // the switch has no visible text

    addAndMakeVisible (fillFromChoices (qualityBox, apvts.getParameter (pid::quality)));
    addAndMakeVisible (fillFromChoices (stereoBox, apvts.getParameter (pid::stereoMode)));
    qualityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, pid::quality, qualityBox);
    stereoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, pid::stereoMode, stereoBox);

    rawStyle = apvts.getRawParameterValue (pid::style);
    rawInput = apvts.getRawParameterValue (pid::input);
    rawDrive = apvts.getRawParameterValue (pid::drive);
    rawCharacter = apvts.getRawParameterValue (pid::character);
    rawParallel = apvts.getRawParameterValue (pid::parallel);
    rawParFocus = apvts.getRawParameterValue (pid::parFocus);
    rawParAlt = apvts.getRawParameterValue (pid::parAlt);
    rawAir = apvts.getRawParameterValue (pid::air);

    // The children were created before they had this editor (and its look-and-feel) as parent,
    // so let them rebuild their parts (slider text boxes etc.) with the Pillow look-and-feel.
    sendLookAndFeelChange();

    setSize (editorWidth, editorHeight);
    updateDisplayState (true);
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

void PillowSaturatorAudioProcessorEditor::addRadioPills (juce::TextButton* buttons, int count,
                                                         const juce::StringArray& names, const char* paramID,
                                                         std::unique_ptr<juce::ParameterAttachment>& attachment)
{
    for (int i = 0; i < count; ++i)
    {
        auto& b = buttons[i];
        b.setButtonText (names[i].toUpperCase());
        b.setClickingTogglesState (false);
        b.onClick = [&attachment, i] { attachment->setValueAsCompleteGesture ((float) i); };
        addAndMakeVisible (b);
    }
    attachment = std::make_unique<juce::ParameterAttachment> (
        *audioProcessor.apvts.getParameter (paramID),
        [buttons, count] (float value)
        {
            const int index = juce::roundToInt (value);
            for (int i = 0; i < count; ++i)
                buttons[i].setToggleState (i == index, juce::dontSendNotification);
        });
    attachment->sendInitialUpdate();
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

    auto bottom = area.removeFromBottom (110);
    area.removeFromBottom (14);

    leftPanel = area.removeFromLeft (400);
    area.removeFromLeft (14);
    centrePanel = area.removeFromLeft (240);
    area.removeFromLeft (14);
    rightPanel = area;

    parallelPanel = bottom.removeFromLeft (rightPanel.getX() - 14 - bottom.getX());
    bottom.removeFromLeft (14);
    airPanel = bottom;

    // left: curve + style pills (3 + 2)
    {
        constexpr int pillH = 34, gap = 8;
        auto r = leftPanel;
        auto pills = r.removeFromBottom (pillH * 2 + gap);
        r.removeFromBottom (12);
        curveDisplay.setBounds (r);

        auto rowA = pills.removeFromTop (pillH);
        pills.removeFromTop (gap);
        auto rowB = pills;
        const int w3 = (rowA.getWidth() - 2 * gap) / 3;
        styleButtons[0].setBounds (rowA.removeFromLeft (w3));
        rowA.removeFromLeft (gap);
        styleButtons[1].setBounds (rowA.removeFromLeft (w3));
        rowA.removeFromLeft (gap);
        styleButtons[2].setBounds (rowA);
        const int w2 = (rowB.getWidth() - gap) / 2;
        styleButtons[3].setBounds (rowB.removeFromLeft (w2));
        styleButtons[4].setBounds (rowB.withTrimmedLeft (gap));
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

    // bottom left: parallel saturation (title, amount, focus pills, alt)
    {
        auto r = parallelPanel.reduced (18, 6);
        parallelTitle = r.removeFromLeft (120);
        placeKnob (*parallelKnob, r.removeFromLeft (100));
        r.removeFromLeft (28);

        auto focus = r.removeFromLeft (240);
        focusTitle = focus.removeFromTop (18);
        auto pills = focus.withSizeKeepingCentre (focus.getWidth(), 32);
        constexpr int gap = 6;
        const int w = (pills.getWidth() - 2 * gap) / 3;
        for (auto& b : focusButtons)
        {
            b.setBounds (pills.removeFromLeft (w));
            pills.removeFromLeft (gap);
        }

        r.removeFromLeft (28);
        parAltButton.setBounds (r.removeFromLeft (80).withSizeKeepingCentre (80, 58)); // stacked toggle
    }

    // bottom right: air (title + switch)
    {
        auto r = airPanel.reduced (18, 6);
        airButton.setBounds (r.removeFromRight (44).withSizeKeepingCentre (44, 28));
        airTitle = r;
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
    for (const auto& panel : { centrePanel, rightPanel, parallelPanel, airPanel })
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

    // section titles (accent while the section is doing something)
    auto drawSectionTitle = [&g] (juce::Rectangle<int> r, const juce::String& title, const juce::String& caption,
                                  bool active)
    {
        auto top = r.removeFromTop (r.getHeight() / 2);
        g.setColour (active ? col::accent : col::text);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (title, top.withTrimmedBottom (1), juce::Justification::bottomLeft, false);
        g.setColour (col::textDim);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (caption, r.withTrimmedTop (3), juce::Justification::topLeft, false);
    };
    drawSectionTitle (parallelTitle, "PARALLEL", "SATURATION", parallelActive);
    drawSectionTitle (airTitle, "AIR", "SOFT LIFT ABOVE 10 kHz", airActive);

    g.setColour (col::textDim);
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.drawText ("FOCUS", focusTitle, juce::Justification::centred, false);

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
    auto labelArea = area.removeFromBottom (16.0f).expanded (8.0f, 0.0f); // labels may be wider than a meter
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

//==============================================================================
// Labels and section states that follow the parameters, plus the curve display.
void PillowSaturatorAudioProcessorEditor::updateDisplayState (bool force)
{
    const int style = juce::roundToInt (rawStyle->load());
    if (force || style != shownStyle)
    {
        shownStyle = style;
        const bool glow = style == (int) pillow::dsp::StyleId::Glow;
        characterKnob->title = glow ? "2ND STAGE" : "CHARACTER";
        characterKnob->slider.setTooltip (glow ? "Glow's second stage: pushes its drive, asymmetry and sag."
                                               : "Asymmetry: adds even harmonics (0 % = odd only).");
        repaint (characterKnob->titleBounds);
    }

    const float parallel = rawParallel->load() * 0.01f;
    const bool parallelOn = parallel > 0.005f;
    if (force || parallelOn != parallelActive)
    {
        parallelActive = parallelOn;
        // focus / alt only matter with some parallel amount: show them as inactive otherwise
        for (auto& b : focusButtons)
            b.setAlpha (parallelOn ? 1.0f : 0.45f);
        parAltButton.setAlpha (parallelOn ? 1.0f : 0.45f);
        repaint (parallelTitle);
    }

    const bool airOn = rawAir->load() > 0.5f;
    if (force || airOn != airActive)
    {
        airActive = airOn;
        repaint (airTitle);
    }

    pillow::ui::CurveDisplay::State s;
    s.style = style;
    s.factor = audioProcessor.oversamplingFactor.load();
    s.inputDb = rawInput->load();
    s.driveDb = rawDrive->load();
    s.character = rawCharacter->load() * 0.01f;
    s.parallel = parallel;
    s.focus = juce::roundToInt (rawParFocus->load());
    s.alt = rawParAlt->load() > 0.5f;
    s.air = airOn;
    curveDisplay.setState (s);
}

void PillowSaturatorAudioProcessorEditor::timerCallback()
{
    auto ballistics = [] (float current, float incoming) { return juce::jmax (incoming, current * 0.86f); };
    inLevel = ballistics (inLevel, audioProcessor.inputPeak.exchange (0.0f));
    outLevel = ballistics (outLevel, audioProcessor.outputPeak.exchange (0.0f));
    repaint (meterArea);

    updateDisplayState (false);
}
