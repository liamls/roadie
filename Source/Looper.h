#pragma once

#include <algorithm>
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

    // Re-allocates (and wipes the loop) only when the sample rate or the
    // capacity actually changes, so a device restart keeps the current loop.
    void prepare (double sampleRate, double maxLoopSeconds = 60.0);

    // Request transitions from the message thread (UI button clicks).
    void requestRecord();   // Empty -> Recording; Recording -> Playing (fixes loop length)
    void requestOverdub();  // Playing/Stopped -> Overdubbing; Overdubbing -> Playing
    void requestStop();     // Playing/Overdubbing -> Stopped (no toggle)
    void requestRestart();  // playhead back to the start of the loop
    void requestUndo();     // reverts the last overdub pass, if any
    void requestClear();    // back to Empty

    State getState() const noexcept { return state.load (std::memory_order_relaxed); }
    double getLoopLengthSeconds() const noexcept;
    float getPlayheadFraction() const noexcept;
    double getSampleRate() const noexcept { return sampleRate; }
    int getRecordedLengthSamples() const noexcept { return recordedLength.load (std::memory_order_relaxed); }
    bool isUndoAvailable() const noexcept { return undoAvailable.load (std::memory_order_relaxed); }

    // 1 after the first recording, +1 per overdub pass, -1 per undo.
    int getLayerCount() const noexcept { return layerCount.load (std::memory_order_relaxed); }

    // Safe to read from the message thread only when getState() is Playing,
    // Stopped or Empty (i.e. never while Recording/Overdubbing is mutating it
    // on the audio thread) — callers must check first.
    const std::vector<float>& getBufferForExport() const noexcept { return buffer; }

    // Fills `peaks` with the loop's peak envelope (same "callers must check
    // the state first" rule as getBufferForExport()).
    void getOverview (float* peaks, int numPeaks) const noexcept;

    // Replaces the loop with `numSamples` of `data`, ready to play with the
    // transport (Playing).
    // Not real-time safe: the caller must guarantee process() isn't running
    // concurrently (AudioEngine holds its processing lock around this).
    void setContent (const float* data, int numSamples, int layers);

    // Audio-thread only. Records `input` into the loop if recording or
    // overdubbing and writes the loop's own playback (only the loop, never
    // the live input) into `loopOutput`, so the caller can mute or solo
    // each one separately.
    void process (const float* input, float* loopOutput, int numSamples);

    // Audio-thread only. Applies pending requests without producing audio —
    // for blocks where the transport is stopped or outside the loop region.
    void update() { applyPendingRequests(); }

    // Audio-thread only. Puts the loop's read/write head at `sample`, so the
    // loop follows the timeline (cycle jumps, locate) instead of free-running.
    void setPlayhead (int sample) noexcept;

    // Round-trip latency to compensate while overdubbing: the input arriving
    // now was played against what the player heard `samples` ago, so it is
    // written that far behind the read head (any thread).
    void setRecordDelay (int samples) noexcept { recordDelay.store (std::max (0, samples), std::memory_order_relaxed); }

private:
    void applyPendingRequests();

    std::vector<float> buffer;
    std::vector<float> undoSnapshot;
    std::atomic<bool> undoAvailable { false }; // written on the audio thread, read from the UI thread

    std::atomic<int> recordedLength { 0 }; // samples; fixes the loop length once recording stops
    std::atomic<int> playhead { 0 };
    std::atomic<int> layerCount { 0 };
    std::atomic<int> recordDelay { 0 };
    std::atomic<State> state { State::Empty };

    std::atomic<bool> recordRequested { false };
    std::atomic<bool> overdubRequested { false };
    std::atomic<bool> stopRequested { false };
    std::atomic<bool> restartRequested { false };
    std::atomic<bool> undoRequested { false };
    std::atomic<bool> clearRequested { false };

    int maxLengthSamples = 0;
    double sampleRate = 0.0;

    static constexpr float overdubFeedback = 0.98f;
};
