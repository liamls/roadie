#include "Metronome.h"

#include <cmath>

void Metronome::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    clickLengthSamples = (int) (0.02 * sampleRate); // 20 ms click
    lastBeatIndex = -1;
    clickEnvelopePos = -1;
}

double Metronome::getSamplesPerBeat() const noexcept
{
    return (60.0 / bpmAtomic.load (std::memory_order_relaxed)) * sampleRate;
}

void Metronome::process (float* samples, int numSamples, std::int64_t transportStart, bool transportRunning)
{
    const bool on = enabled.load (std::memory_order_relaxed);
    const double samplesPerBeat = getSamplesPerBeat();
    const int beatsPerBar = beatsPerBarAtomic.load (std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i)
    {
        if (transportRunning)
        {
            const auto beat = (std::int64_t) std::floor ((double) (transportStart + i) / samplesPerBeat);

            // A new beat, or the transport jumped back (loop restart): click.
            if (beat != lastBeatIndex)
            {
                lastBeatIndex = beat;
                if (on)
                {
                    clickEnvelopePos = 0;
                    accentedClick = beat % beatsPerBar == 0;
                }
            }
        }

        if (clickEnvelopePos >= 0 && clickEnvelopePos < clickLengthSamples)
        {
            const double t = (double) clickEnvelopePos / sampleRate;
            const float envelope = (float) std::exp (-t * 80.0);
            const double pitch = accentedClick ? 1500.0 : 1000.0;
            const float level = accentedClick ? 0.38f : 0.26f;
            samples[i] += envelope * level * (float) std::sin (2.0 * 3.14159265358979 * pitch * t);
            ++clickEnvelopePos;
        }
        else
        {
            clickEnvelopePos = -1;
        }
    }

    if (! transportRunning)
        lastBeatIndex = -1; // the next start clicks immediately on its first beat
}
