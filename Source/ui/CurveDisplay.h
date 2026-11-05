#pragma once

#include <array>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PillowLookAndFeel.h"
#include "../Parameters.h"
#include "../dsp/SaturatorEngine.h"

namespace pillow::ui
{
// Static transfer curve of the current settings, normalised to the panel.
// Asymmetry (Character, parallel bias) is visible as a curve that bends differently above and
// below zero. The parallel path is drawn for focus Low / Flat (High has no static curve).
class CurveDisplay final : public juce::Component
{
public:
    struct State
    {
        int style = 0, factor = 4, focus = 1;
        float inputDb = 0.0f, driveDb = 6.0f, character = 0.35f, parallel = 0.0f;
        bool alt = false, air = false;

        bool approxEquals (const State& o) const noexcept
        {
            return style == o.style && factor == o.factor && focus == o.focus && alt == o.alt && air == o.air
                && std::abs (inputDb - o.inputDb) < 0.01f && std::abs (driveDb - o.driveDb) < 0.01f
                && std::abs (character - o.character) < 0.001f && std::abs (parallel - o.parallel) < 0.001f;
        }
    };

    void setState (const State& newState)
    {
        if (newState.approxEquals (state))
            return;
        state = newState;
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

        // Input gain is applied before the parallel path, Drive after it.
        const float inGain = juce::Decibels::decibelsToGain (state.inputDb);
        std::array<float, numPoints + 1> ys {};
        float maxAbs = 1.0e-6f;
        for (int i = 0; i <= numPoints; ++i)
        {
            const float x = -1.0f + 2.0f * (float) i / (float) numPoints;
            ys[(size_t) i] = pillow::dsp::SaturatorEngine::evaluateStaticCurve (
                state.style, x * inGain, state.driveDb, state.character, state.parallel, state.focus, state.alt);
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
        g.drawText (juce::String (state.factor) + "x OVERSAMPLING", labels, juce::Justification::topRight, false);

        const bool parallelOn = state.parallel > 0.005f;
        const bool even = state.character >= 0.01f || parallelOn; // the parallel curve is biased
        g.drawText (even ? "EVEN + ODD" : "ODD", labels, juce::Justification::bottomRight, false);

        juce::StringArray info;
        if (pillow::dsp::getStyleSpec ((pillow::dsp::StyleId) state.style).twoStage)
            info.add ("2 STAGES");
        if (parallelOn)
            info.add ("PARALLEL " + pillow::params::parallelFocusNames()[state.focus].toUpperCase()
                      + (state.alt ? " ALT" : ""));
        if (state.air)
            info.add ("AIR");
        g.setColour (colours::accent.withAlpha (0.85f));
        g.drawText (info.joinIntoString (" + "), labels, juce::Justification::bottomLeft, false);
    }

private:
    static constexpr int numPoints = 160;
    State state;
};
} // namespace pillow::ui
