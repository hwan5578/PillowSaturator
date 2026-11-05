#pragma once

#include "DspCommon.h"

namespace pillow::dsp
{
// Linear ramp towards a target over a fixed time. Cheap, allocation-free, audio-thread safe.
class LinearSmoother
{
public:
    void prepare (double sampleRate, double rampSeconds) noexcept
    {
        rampLength = std::max (1, (int) std::lround (sampleRate * rampSeconds));
        setImmediate (target);
    }

    void setImmediate (float value) noexcept
    {
        current = target = value;
        step = 0.0f;
        countdown = 0;
    }

    void setTarget (float value) noexcept
    {
        if (! (value < target || value > target)) // unchanged
            return;

        target = value;
        countdown = rampLength;
        step = (target - current) / (float) rampLength;
    }

    float next() noexcept
    {
        if (countdown <= 0)
            return current;

        --countdown;
        current = (countdown == 0) ? target : current + step;
        return current;
    }

    // Advances by several samples at once (for block-rate consumers).
    float skip (int numSteps) noexcept
    {
        if (countdown <= 0)
            return current;
        if (numSteps >= countdown)
        {
            countdown = 0;
            current = target;
            return current;
        }
        countdown -= numSteps;
        current += step * (float) numSteps;
        return current;
    }

    bool isSmoothing() const noexcept { return countdown > 0; }
    float getCurrent() const noexcept { return current; }
    float getTarget() const noexcept { return target; }

private:
    float current = 0.0f, target = 0.0f, step = 0.0f;
    int countdown = 0, rampLength = 1;
};
} // namespace pillow::dsp
