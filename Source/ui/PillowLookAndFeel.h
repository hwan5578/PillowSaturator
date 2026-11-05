#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace pillow::ui
{
namespace colours
{
    inline const juce::Colour background { 0xff1b1916 };
    inline const juce::Colour panel      { 0xff25221e };
    inline const juce::Colour panelHi    { 0xff2f2b26 };
    inline const juce::Colour outline    { 0xff3a352f };
    inline const juce::Colour grid       { 0xff312d28 };
    inline const juce::Colour track      { 0xff433d36 };
    inline const juce::Colour text       { 0xffece3d4 };
    inline const juce::Colour textDim    { 0xff9b9184 };
    inline const juce::Colour accent     { 0xfff0a36b }; // warm peach
    inline const juce::Colour faceTop    { 0xfff3ebdd }; // soft "pillow" cream
    inline const juce::Colour faceBottom { 0xffd9cdb9 };
    inline const juce::Colour pointer    { 0xff2a2622 };
} // namespace colours

//==============================================================================
class PillowLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    PillowLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, colours::background);

        setColour (juce::Slider::textBoxTextColourId, colours::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, colours::accent.withAlpha (0.35f));

        setColour (juce::Label::textColourId, colours::text);
        setColour (juce::TextEditor::backgroundColourId, colours::panelHi);
        setColour (juce::TextEditor::textColourId, colours::text);
        setColour (juce::TextEditor::highlightColourId, colours::accent.withAlpha (0.35f));
        setColour (juce::TextEditor::outlineColourId, colours::outline);
        setColour (juce::TextEditor::focusedOutlineColourId, colours::accent);
        setColour (juce::CaretComponent::caretColourId, colours::accent);

        setColour (juce::TextButton::textColourOffId, colours::text);
        setColour (juce::TextButton::textColourOnId, colours::background);

        setColour (juce::ComboBox::textColourId, colours::text);
        setColour (juce::ComboBox::backgroundColourId, colours::panel);
        setColour (juce::ComboBox::outlineColourId, colours::outline);
        setColour (juce::ComboBox::arrowColourId, colours::textDim);

        setColour (juce::PopupMenu::backgroundColourId, colours::panelHi);
        setColour (juce::PopupMenu::textColourId, colours::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accent);
        setColour (juce::PopupMenu::highlightedTextColourId, colours::background);

        setColour (juce::TooltipWindow::backgroundColourId, colours::panelHi);
        setColour (juce::TooltipWindow::textColourId, colours::text);
        setColour (juce::TooltipWindow::outlineColourId, colours::outline);
    }

    //==============================================================================
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
        const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
        const auto centre = bounds.getCentre();
        const float radius = size * 0.5f;
        const float lineW = juce::jmax (2.5f, radius * 0.075f);
        const float arcRadius = radius - lineW;
        const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        const bool bipolar = (bool) slider.getProperties().getWithDefault ("bipolar", false);
        const juce::PathStrokeType stroke (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        juce::Path trackPath;
        trackPath.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (colours::track);
        g.strokePath (trackPath, stroke);

        const float from = bipolar ? 0.5f * (rotaryStartAngle + rotaryEndAngle) : rotaryStartAngle;
        if (std::abs (angle - from) > 0.01f)
        {
            juce::Path valuePath;
            valuePath.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                     juce::jmin (from, angle), juce::jmax (from, angle), true);
            g.setColour (slider.isEnabled() ? colours::accent : colours::textDim);
            g.strokePath (valuePath, stroke);
        }

        // soft knob face
        const float faceRadius = arcRadius - lineW * 2.2f;
        const auto face = juce::Rectangle<float> (faceRadius * 2.0f, faceRadius * 2.0f).withCentre (centre);
        {
            juce::Path shadowPath;
            shadowPath.addEllipse (face.translated (0.0f, faceRadius * 0.10f));
            juce::DropShadow (juce::Colours::black.withAlpha (0.55f), juce::jmax (4, (int) (faceRadius * 0.45f)), {})
                .drawForPath (g, shadowPath);
        }
        g.setGradientFill (juce::ColourGradient (colours::faceTop, centre.x, face.getY(), colours::faceBottom, centre.x, face.getBottom(), false));
        g.fillEllipse (face);
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.drawEllipse (face.reduced (0.75f), 1.0f);

        // pointer
        const float pw = juce::jmax (2.5f, faceRadius * 0.10f);
        juce::Path pointerPath;
        pointerPath.addRoundedRectangle (-pw * 0.5f, -faceRadius * 0.86f, pw, faceRadius * 0.42f, pw * 0.5f);
        g.setColour (colours::pointer);
        g.fillPath (pointerPath, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
    }

    //==============================================================================
    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                               bool isHighlighted, bool isDown) override
    {
        const auto r = button.getLocalBounds().toFloat().reduced (1.5f);
        const float corner = r.getHeight() * 0.5f;
        const bool on = button.getToggleState();

        g.setColour (on ? colours::accent : (isHighlighted ? colours::panelHi : colours::panel));
        g.fillRoundedRectangle (r, corner);
        if (! on)
        {
            g.setColour (colours::outline);
            g.drawRoundedRectangle (r, corner, 1.0f);
        }
        if (isDown)
        {
            g.setColour (juce::Colours::black.withAlpha (0.15f));
            g.fillRoundedRectangle (r, corner);
        }
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return juce::FontOptions (juce::jmin (13.5f, (float) buttonHeight * 0.42f), juce::Font::bold);
    }

    //==============================================================================
    // Pill switch + label. Buttons taller than 44 px stack the label under the switch.
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool isHighlighted, bool) override
    {
        const auto r = button.getLocalBounds().toFloat().reduced (2.0f);
        const bool on = button.getToggleState();

        if (r.getHeight() >= 44.0f)
        {
            const float h = 18.0f;
            const auto sw = juce::Rectangle<float> (h * 1.8f, h).withCentre ({ r.getCentreX(), r.getY() + h * 0.5f + 4.0f });
            drawSwitch (g, sw, on);
            g.setColour (isHighlighted || on ? colours::text : colours::textDim);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (button.getButtonText(), r.withTrimmedTop (h + 12.0f), juce::Justification::centredTop, false);
            return;
        }

        const float h = juce::jmin (18.0f, r.getHeight());
        const auto sw = juce::Rectangle<float> (r.getX(), r.getCentreY() - h * 0.5f, h * 1.8f, h);
        drawSwitch (g, sw, on);

        g.setColour (isHighlighted || on ? colours::text : colours::textDim);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (button.getButtonText(), r.withLeft (sw.getRight() + 8.0f), juce::Justification::centredLeft, false);
    }

    static void drawSwitch (juce::Graphics& g, juce::Rectangle<float> sw, bool on)
    {
        const float h = sw.getHeight();

        g.setColour (on ? colours::accent : colours::track);
        g.fillRoundedRectangle (sw, h * 0.5f);
        const float d = h - 4.0f;
        const auto dot = juce::Rectangle<float> (d, d).withCentre ({ on ? sw.getRight() - h * 0.5f : sw.getX() + h * 0.5f,
                                                                     sw.getCentreY() });
        g.setColour (colours::faceTop);
        g.fillEllipse (dot);
    }

    //==============================================================================
    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (1.0f);
        const float corner = r.getHeight() * 0.5f;
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, corner);
        g.setColour (box.hasKeyboardFocus (true) ? colours::accent : colours::outline);
        g.drawRoundedRectangle (r, corner, 1.0f);

        juce::Path arrow;
        const float ax = r.getRight() - 15.0f, ay = r.getCentreY();
        arrow.startNewSubPath (ax - 4.0f, ay - 2.0f);
        arrow.lineTo (ax, ay + 2.5f);
        arrow.lineTo (ax + 4.0f, ay - 2.0f);
        g.setColour (colours::textDim);
        g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (13.0f, juce::Font::bold); }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (10, 0, box.getWidth() - 30, box.getHeight());
        label.setFont (getComboBoxFont (box));
    }

    juce::Font getPopupMenuFont() override { return juce::FontOptions (14.0f); }

    juce::Font getLabelFont (juce::Label&) override { return juce::FontOptions (13.0f); }
};
} // namespace pillow::ui
