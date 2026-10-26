#include "Metronome.h"

#include <cmath>

void Metronome::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    clickLengthSamples = (int) (0.02 * sampleRate); // 20 ms click
    samplesUntilNextClick = 0.0;
    clickEnvelopePos = -1;
}

void Metronome::process (float* samples, int numSamples)
{
    if (! enabled.load (std::memory_order_relaxed))
        return;

    const double bpm = bpmAtomic.load (std::memory_order_relaxed);
    const double intervalSamples = (60.0 / bpm) * sampleRate;

    for (int i = 0; i < numSamples; ++i)
    {
        if (samplesUntilNextClick <= 0.0)
        {
            clickEnvelopePos = 0;
            samplesUntilNextClick += intervalSamples;
        }

        if (clickEnvelopePos >= 0 && clickEnvelopePos < clickLengthSamples)
        {
            const double t = (double) clickEnvelopePos / sampleRate;
            const float envelope = (float) std::exp (-t * 80.0);
            const float click = envelope * 0.3f * (float) std::sin (2.0 * 3.14159265358979 * 1000.0 * t);
            samples[i] += click;
            ++clickEnvelopePos;
        }
        else
        {
            clickEnvelopePos = -1;
        }

        samplesUntilNextClick -= 1.0;
    }
}
