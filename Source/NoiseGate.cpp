#include "NoiseGate.h"

#include <algorithm>
#include <cmath>

void NoiseGate::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    envelopeRelease = (float) std::exp (-1.0 / (0.02 * sampleRate)); // the level detector's own decay
    attackStep = (float) (1.0 / (0.001 * sampleRate));                // fully open in 1 ms
    releaseCoefficient = (float) std::exp (-1.0 / (0.08 * sampleRate));
    holdSamples = (int) (0.06 * sampleRate);
    envelope = 0.0f;
    gain = 1.0f;
    holdLeft = 0;
    open = true;
}

void NoiseGate::process (float* samples, int numSamples) noexcept
{
    if (! enabled.load (std::memory_order_relaxed))
    {
        gain = 1.0f;
        open = true;
        openness.store (1.0f, std::memory_order_relaxed);
        return;
    }

    const float openAt = std::pow (10.0f, thresholdDb.load (std::memory_order_relaxed) / 20.0f);
    const float closeAt = openAt * 0.5f; // 6 dB of hysteresis

    for (int i = 0; i < numSamples; ++i)
    {
        const float level = std::abs (samples[i]);
        envelope = std::max (level, envelope * envelopeRelease);

        if (envelope >= openAt)
        {
            open = true;
            holdLeft = holdSamples;
        }
        else if (open && envelope < closeAt)
        {
            if (holdLeft > 0)
                --holdLeft;
            else
                open = false;
        }

        gain = open ? std::min (1.0f, gain + attackStep) : gain * releaseCoefficient;
        samples[i] *= gain;
    }
    openness.store (gain, std::memory_order_relaxed);
}
