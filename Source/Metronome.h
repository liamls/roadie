#pragma once

#include <limits>

#include <atomic>
#include <cstdint>

// Click generator locked to the transport position, mixed into the monitor
// output only — never recorded into the Looper or a take (guide click, not
// part of the performance). The first beat of each bar is accented.
class Metronome
{
public:
    void prepare (double sampleRate);

    void setBpm (double bpm) noexcept
    {
        // Finite and positive: the click position divides by it.
        bpmAtomic.store (bpm >= 20.0 && bpm <= 400.0 ? bpm : 100.0, std::memory_order_relaxed);
    }
    double getBpm() const noexcept { return bpmAtomic.load (std::memory_order_relaxed); }

    void setBeatsPerBar (int beats) noexcept { beatsPerBarAtomic.store (beats < 1 ? 1 : beats, std::memory_order_relaxed); }
    int getBeatsPerBar() const noexcept { return beatsPerBarAtomic.load (std::memory_order_relaxed); }

    void setEnabled (bool shouldBeEnabled) noexcept { enabled.store (shouldBeEnabled, std::memory_order_relaxed); }
    bool isEnabled() const noexcept { return enabled.load (std::memory_order_relaxed); }

    // Count-in: clicks until the transport reaches `sample`, even with the
    // metronome off (−1: none).
    void setCountInEnd (std::int64_t sample) noexcept { countInEnd.store (sample, std::memory_order_relaxed); }

    double getSamplesPerBeat() const noexcept;

    // Audio-thread only. Adds clicks on top of `samples` for every beat
    // boundary between transportStart and transportStart + numSamples.
    void process (float* samples, int numSamples, std::int64_t transportStart, bool transportRunning);

private:
    double sampleRate = 48000.0;
    std::int64_t lastBeatIndex = std::numeric_limits<std::int64_t>::min();
    int clickEnvelopePos = -1;
    int clickLengthSamples = 0;
    bool accentedClick = false;

    std::atomic<double> bpmAtomic { 100.0 };
    std::atomic<int> beatsPerBarAtomic { 4 };
    std::atomic<bool> enabled { false };
    std::atomic<std::int64_t> countInEnd { -1 };
};
