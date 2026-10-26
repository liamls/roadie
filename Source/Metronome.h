#pragma once

#include <atomic>

// Free-running click generator, mixed into the monitor output only — never
// recorded into the Looper (guide click, not part of the take).
class Metronome
{
public:
    void prepare (double sampleRate);

    void setBpm (double bpm) noexcept { bpmAtomic.store (bpm, std::memory_order_relaxed); }
    double getBpm() const noexcept { return bpmAtomic.load (std::memory_order_relaxed); }

    void setEnabled (bool shouldBeEnabled) noexcept { enabled.store (shouldBeEnabled, std::memory_order_relaxed); }
    bool isEnabled() const noexcept { return enabled.load (std::memory_order_relaxed); }

    // Audio-thread only. Adds click samples on top of `samples`.
    void process (float* samples, int numSamples);

private:
    double sampleRate = 44100.0;
    double samplesUntilNextClick = 0.0;
    int clickEnvelopePos = -1;
    int clickLengthSamples = 0;

    std::atomic<double> bpmAtomic { 100.0 };
    std::atomic<bool> enabled { false };
};
