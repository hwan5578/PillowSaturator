#pragma once

#include <array>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PillowLookAndFeel.h"
#include "../dsp/SaturatorEngine.h"

namespace pillow::ui
{
// Static transfer curve of the current style / drive / character, normalised to the panel.
// Asymmetry (Character) is visible as a curve that bends differently above and below zero.
class CurveDisplay final : public juce::Component
{
public:
    void setState (int newStyle, float newTotalDriveDb, float newCharacter, int newFactor)
    {
        if (newStyle == style && std::abs (newTotalDriveDb - totalDriveDb) < 0.01f
            && std::abs (newCharacter - character) < 0.001f && newFactor == factor)
            return;

        style = newStyle;
        totalDriveDb = newTotalDriveDb;
        character = newCharacter;
        factor = newFactor;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, 14.0f);
        g.setColour (colours::outline);
        g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, 1.0f);

        const auto plot = r.reduced (22.0f, 30.0f).withTrimmedTop (4.0f);

        g.setColour (colours::grid);
        for (int i = 1; i < 4; ++i)
        {
            const float gx = plot.getX() + plot.getWidth() * (float) i / 4.0f;
            const float gy = plot.getY() + plot.getHeight() * (float) i / 4.0f;
            g.drawVerticalLine (juce::roundToInt (gx), plot.getY(), plot.getBottom());
            g.drawHorizontalLine (juce::roundToInt (gy), plot.getX(), plot.getRight());
        }

        g.setColour (colours::track);
        g.drawLine (plot.getX(), plot.getBottom(), plot.getRight(), plot.getY(), 1.0f);

        std::array<float, numPoints + 1> ys {};
        float maxAbs = 1.0e-6f;
        for (int i = 0; i <= numPoints; ++i)
        {
            const float x = -1.0f + 2.0f * (float) i / (float) numPoints;
            ys[(size_t) i] = pillow::dsp::SaturatorEngine::evaluateStaticCurve (style, x, totalDriveDb, character);
            maxAbs = juce::jmax (maxAbs, std::abs (ys[(size_t) i]));
        }

        juce::Path curve;
        for (int i = 0; i <= numPoints; ++i)
        {
            const float px = plot.getX() + plot.getWidth() * (float) i / (float) numPoints;
            const float py = plot.getCentreY() - (ys[(size_t) i] / maxAbs) * plot.getHeight() * 0.5f;
            if (i == 0)
                curve.startNewSubPath (px, py);
            else
                curve.lineTo (px, py);
        }

        g.setColour (colours::accent.withAlpha (0.16f));
        g.strokePath (curve, juce::PathStrokeType (8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colours::accent);
        g.strokePath (curve, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.setColour (colours::textDim);
        const auto labels = r.reduced (16.0f, 9.0f);
        g.drawText ("TRANSFER", labels, juce::Justification::topLeft, false);
        g.drawText (juce::String (factor) + "x OVERSAMPLING", labels, juce::Justification::topRight, false);
        g.drawText (character < 0.01f ? "ODD" : "EVEN + ODD", labels, juce::Justification::bottomRight, false);
    }

private:
    static constexpr int numPoints = 160;
    int style = 0, factor = 4;
    float totalDriveDb = 6.0f, character = 0.35f;
};
} // namespace pillow::ui
