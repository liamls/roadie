#pragma once

#include <atomic>
#include <vector>

// Real-time-safe single-track looper (record / play / overdub / undo / clear).
// Buffer is pre-allocated at prepare() and never resized afterwards; state
// changes are requested from the message thread via atomic triggers and
// applied on the audio thread inside process(), so no lock is needed.
class Looper
{
public:
    enum class State
    {
        Empty,
        Recording,
        Playing,
        Overdubbing,
        Stopped
    };

    void prepare (double sampleRate, double maxLoopSeconds = 60.0);

    // Request transitions from the message thread (UI button clicks).
    void requestRecord();   // Empty -> Recording; Recording -> Playing (fixes loop length)
    void requestOverdub();  // Playing/Stopped -> Overdubbing; Overdubbing -> Playing
    void requestPlayStop(); // Playing/Overdubbing -> Stopped; Stopped -> Playing
    void requestUndo();     // reverts the last overdub pass, if any
    void requestClear();    // back to Empty

    State getState() const noexcept { return state.load (std::memory_order_relaxed); }
    double getLoopLengthSeconds() const noexcept;
    float getPlayheadFraction() const noexcept;
    double getSampleRate() const noexcept { return sampleRate; }
    int getRecordedLengthSamples() const noexcept { return recordedLength.load (std::memory_order_relaxed); }
    bool isUndoAvailable() const noexcept { return undoAvailable.load (std::memory_order_relaxed); }

    // Safe to read from the message thread only when getState() is Playing,
    // Stopped or Empty (i.e. never while Recording/Overdubbing is mutating it
    // on the audio thread) — callers must check first.
    const std::vector<float>& getBufferForExport() const noexcept { return buffer; }

    // Audio-thread only. Reads the dry/amped signal in `samples`, records it
    // into the loop if recording/overdubbing, and mixes the loop's own
    // playback on top so the player hears both at once.
    void process (float* samples, int numSamples);

private:
    void applyPendingRequests();

    std::vector<float> buffer;
    std::vector<float> undoSnapshot;
    std::atomic<bool> undoAvailable { false }; // written on the audio thread, read from the UI thread

    std::atomic<int> recordedLength { 0 }; // samples; fixes the loop length once recording stops
    std::atomic<int> playhead { 0 };
    std::atomic<State> state { State::Empty };

    std::atomic<bool> recordRequested { false };
    std::atomic<bool> overdubRequested { false };
    std::atomic<bool> playStopRequested { false };
    std::atomic<bool> undoRequested { false };
    std::atomic<bool> clearRequested { false };

    int maxLengthSamples = 0;
    double sampleRate = 44100.0;

    static constexpr float overdubFeedback = 0.98f;
};
