#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "AmpEngine.h"
#include "Looper.h"
#include "Metronome.h"
#include "TakeRecorder.h"

// The whole real-time chain, registered directly as the device callback
// (rather than through AudioAppComponent) so it receives CoreAudio's host
// timestamps — the reference the camera frames are aligned against.
//
//   input ─► AmpEngine ─► live ─┬──────────────────────────► TakeRecorder
//                               ├─► (+ regions) × guitar ──┐
//                               └─► Looper × loop ─────────┴─► mix ─► Metronome ─► out
//
// The transport is a linear timeline (samples from the start of the
// arrangement), like a DAW: the regions play at their place, the loop
// follows the playhead from its anchor, and the cycle zone wraps it.
class AudioEngine : public juce::AudioIODeviceCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    AmpEngine& getAmp() noexcept { return amp; }
    const AmpEngine& getAmp() const noexcept { return amp; }
    Looper& getLooper() noexcept { return looper; }
    const Looper& getLooper() const noexcept { return looper; }
    Metronome& getMetronome() noexcept { return metronome; }
    const Metronome& getMetronome() const noexcept { return metronome; }
    TakeRecorder& getTakeRecorder() noexcept { return takeRecorder; }

    //==============================================================================
    // Transport (message thread).
    void play() noexcept { transportStopped.store (false, std::memory_order_relaxed); }
    void stop() noexcept { transportStopped.store (true, std::memory_order_relaxed); }
    void togglePlay() noexcept;
    // Moves the playhead (applied at the next block).
    void locate (std::int64_t position) noexcept;
    void returnToStart() noexcept { locate (0); }

    // Looper pedal: record → play → overdub → play… Starts the transport.
    void recordLoop();
    // Stops the transport; a loop being recorded is closed, not thrown away.
    void stopAll();

    std::int64_t getTransportPosition() const noexcept { return transportPos.load (std::memory_order_relaxed); }
    bool isTransportRunning() const noexcept { return transportRunning.load (std::memory_order_relaxed); }
    double getSampleRate() const noexcept { return sampleRate.load (std::memory_order_relaxed); }
    int getBlockSize() const noexcept { return blockSize.load (std::memory_order_relaxed); }

    // Cycle zone: the playhead jumps back to `start` when it reaches `end`
    // (unless a take is being recorded).
    void setCycle (bool enabled, std::int64_t start, std::int64_t end) noexcept;

    //==============================================================================
    // Arrangement playback. The message thread builds an immutable list of
    // regions (each sharing its take's decoded audio) and hands it over; the
    // audio thread picks it up at the next block without blocking.
    struct PlaybackClip
    {
        std::shared_ptr<const std::vector<float>> audio; // at the engine's sample rate
        std::int64_t start = 0, sourceStart = 0, length = 0;
    };
    using PlaybackList = std::vector<PlaybackClip>;
    void setPlaybackClips (std::shared_ptr<const PlaybackList> clips);

    // Loop region on the timeline (end < 0: open-ended) and the position of
    // the loop's first sample, which fixes its phase.
    void setLoopPlacement (std::int64_t anchor, std::int64_t regionStart, std::int64_t regionEnd) noexcept;
    std::int64_t getLoopAnchor() const noexcept { return loopAnchor.load (std::memory_order_relaxed); }
    // Increments each time a new loop starts recording (its anchor is then
    // the transport position of its first sample).
    int getLoopRecordCount() const noexcept { return loopRecordCount.load (std::memory_order_relaxed); }

    //==============================================================================
    // Mix: per-track mute (from mute/solo) and volume.
    void setGuitarMuted (bool shouldBeMuted) noexcept { guitarMuted.store (shouldBeMuted, std::memory_order_relaxed); }
    void setLoopMuted (bool shouldBeMuted) noexcept { loopMuted.store (shouldBeMuted, std::memory_order_relaxed); }
    void setGuitarGain (float linear) noexcept { guitarGain.store (linear, std::memory_order_relaxed); }
    void setLoopGain (float linear) noexcept { loopGain.store (linear, std::memory_order_relaxed); }

    float getInputPeak() const noexcept { return inputPeak.load (std::memory_order_relaxed); }
    float getOutputPeak() const noexcept { return outputPeak.load (std::memory_order_relaxed); }
    float getGuitarPeak() const noexcept { return guitarPeak.load (std::memory_order_relaxed); }
    float getLoopPeak() const noexcept { return loopPeak.load (std::memory_order_relaxed); }

    // Peak history of the amped guitar, one value per 10 ms of timeline,
    // written as the playhead passes: draws the region being recorded.
    static constexpr int historyCapacity = 1 << 15; // ~5.5 minutes
    int getHistorySlotSamples() const noexcept { return historySlotSamples.load (std::memory_order_relaxed); }
    float getHistoryPeak (std::int64_t slot) const noexcept
    {
        return slot < 0 ? 0.0f : history[(size_t) (slot & (historyCapacity - 1))].load (std::memory_order_relaxed);
    }

    //==============================================================================
    // Takes: the amped guitar (no loop, no click) streamed to disk, and the
    // timeline position of its first sample.
    bool startTake (const juce::File& wavFile);
    void stopTake();
    std::int64_t getTakeStartPosition() const noexcept { return takeStartPos.load (std::memory_order_acquire); }

    //==============================================================================
    // Sync calibration: grabs a few seconds of raw interface input with the
    // host time of its first sample.
    void startCalibrationCapture (double seconds);
    bool isCalibrationCaptureComplete() const noexcept;
    std::vector<float> getCalibrationCapture (double& firstSampleHostSeconds) const;

    //==============================================================================
    // Replaces the loop content (project load). Briefly holds the processing
    // lock: the audio thread outputs silence for that block instead of waiting.
    void loadLoop (const std::vector<float>& samples, int layers);

    // Current estimate of the round-trip latency, for the "Latence" pill.
    double getRoundTripLatencyMs() const noexcept { return roundTripMs.load (std::memory_order_relaxed); }

    //==============================================================================
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    double estimateInputHostTime (const juce::AudioIODeviceCallbackContext& context, int numSamples) const noexcept;
    void updateTransport() noexcept;
    void pickUpPlaybackClips() noexcept;
    int chunkLength (std::int64_t pos, int wanted, bool cycleActive) const noexcept;
    void renderClips (std::int64_t pos, int numSamples, float* out) noexcept;
    void renderLoop (const float* live, float* loopOut, std::int64_t pos, int numSamples, bool running) noexcept;
    void writeHistory (const float* live, std::int64_t pos, int numSamples) noexcept;

    AmpEngine amp;
    Looper looper;
    Metronome metronome;
    TakeRecorder takeRecorder;

    juce::SpinLock processLock;

    std::vector<float> liveBuffer, loopBuffer, clipBuffer, mixBuffer;
    std::vector<char> coveredBuffer;

    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<int> blockSize { 64 };
    std::atomic<int> inputLatencySamples { 0 };
    std::atomic<double> roundTripMs { 0.0 };

    std::atomic<std::int64_t> transportPos { 0 };
    std::atomic<bool> transportRunning { false };
    std::atomic<bool> transportStopped { true };
    std::atomic<std::int64_t> locateRequest { -1 };

    std::atomic<bool> cycleEnabled { false };
    std::atomic<std::int64_t> cycleStart { 0 }, cycleEnd { 0 };

    // Playback list hand-over: pending (set by the UI) → active (audio
    // thread) → retired (freed by the UI, never on the audio thread).
    juce::SpinLock playbackLock;
    std::shared_ptr<const PlaybackList> pendingClips, activeClips, retiredClips;
    bool hasPendingClips = false;

    std::atomic<std::int64_t> loopAnchor { 0 }, loopRegionStart { 0 }, loopRegionEnd { -1 };
    std::atomic<int> loopRecordCount { 0 };

    std::atomic<bool> guitarMuted { false }, loopMuted { false };
    std::atomic<float> guitarGain { 1.0f }, loopGain { 1.0f };

    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f }, guitarPeak { 0.0f }, loopPeak { 0.0f };

    std::array<std::atomic<float>, historyCapacity> history {};
    std::atomic<int> historySlotSamples { 480 };
    std::int64_t historySlot = -1;
    float historyPeakInSlot = 0.0f;

    std::atomic<bool> takeStartPending { false };
    std::atomic<std::int64_t> takeStartPos { 0 };

    std::vector<float> calibrationBuffer;
    std::atomic<int> calibrationWritten { 0 };
    std::atomic<int> calibrationTarget { 0 };
    std::atomic<double> calibrationFirstHostSeconds { 0.0 };
};
