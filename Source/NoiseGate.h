#pragma once

#include <atomic>

// Noise gate at the input of the chain, before the capture: a high-gain
// capture turns the guitar's hiss and hum into a roar between phrases.
// Opens fast (1 ms) above the threshold, holds 60 ms, closes over ~80 ms
// once the level falls 6 dB below it (hysteresis: no chatter on a decaying
// note). Volontairement sans dépendance JUCE, comme ToneStack.
class NoiseGate
{
public:
    void prepare (double sampleRate);

    void setEnabled (bool shouldBeEnabled) noexcept { enabled.store (shouldBeEnabled, std::memory_order_relaxed); }
    bool isEnabled() const noexcept { return enabled.load (std::memory_order_relaxed); }
    void setThresholdDb (float db) noexcept { thresholdDb.store (db, std::memory_order_relaxed); }
    float getThresholdDb() const noexcept { return thresholdDb.load (std::memory_order_relaxed); }

    // Audio thread. Processes in place.
    void process (float* samples, int numSamples) noexcept;

    // How open the gate is (0 closed, 1 open), for the UI.
    float getOpenness() const noexcept { return openness.load (std::memory_order_relaxed); }

private:
    std::atomic<bool> enabled { false };
    std::atomic<float> thresholdDb { -60.0f };
    std::atomic<float> openness { 1.0f };

    double sampleRate = 48000.0;
    float envelope = 0.0f, gain = 1.0f;
    float envelopeRelease = 0.0f, attackStep = 0.0f, releaseCoefficient = 0.0f;
    int holdSamples = 0, holdLeft = 0;
    bool open = true;
};
