#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <atomic>
#include <limits>
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
//                               └─► Looper × loop ─────────┴─► centre ─► Metronome ─┐
//                                                                                   ├─► L/R out
//                                                       (stereo sources) ───────────┘
//
// Guitar, loop and click are mono and sit in the centre of a stereo mix; a
// stereo source (the backing track) adds its own left and right.
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
    // A loop recording that starts by itself when the transport reaches
    // `startAt` (after a count-in) and, if `endAt` is set, closes there: an
    // exact number of bars. Both are transport samples.
    void scheduleLoopRecord (std::int64_t startAt, std::int64_t endAt);
    void cancelLoopSchedule() noexcept;
    bool isLoopScheduled() const noexcept { return loopStartAt.load (std::memory_order_relaxed) != noLocate; }
    std::int64_t getLoopStartAt() const noexcept { return loopStartAt.load (std::memory_order_relaxed); }
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
    // Backing track: stereo, at the engine's sample rate, its first sample at
    // `start` on the timeline (negative: it begins before the timeline).
    // Handed over like the regions, without blocking the audio thread.
    struct SongAudio
    {
        std::vector<float> left, right;
    };
    void setSong (std::shared_ptr<const SongAudio> audio);
    void setSongStart (std::int64_t start) noexcept { songStart.store (start, std::memory_order_relaxed); }
    void setSongMuted (bool shouldBeMuted) noexcept { songMuted.store (shouldBeMuted, std::memory_order_relaxed); }
    void setSongGain (float linear) noexcept { songGain.store (linear, std::memory_order_relaxed); }
    float getSongPeak() const noexcept { return songPeak.load (std::memory_order_relaxed); }

    //==============================================================================
    // Mix: per-track mute (from mute/solo) and volume.
    void setGuitarMuted (bool shouldBeMuted) noexcept { guitarMuted.store (shouldBeMuted, std::memory_order_relaxed); }
    void setLoopMuted (bool shouldBeMuted) noexcept { loopMuted.store (shouldBeMuted, std::memory_order_relaxed); }
    void setGuitarGain (float linear) noexcept { guitarGain.store (linear, std::memory_order_relaxed); }
    // Master: the whole output (the click too), after the sum.
    void setMasterGain (float linear) noexcept { masterGain.store (linear, std::memory_order_relaxed); }
    float getOutputPeakLeft() const noexcept { return outputPeakLeft.load (std::memory_order_relaxed); }
    float getOutputPeakRight() const noexcept { return outputPeakRight.load (std::memory_order_relaxed); }
    void setLoopGain (float linear) noexcept { loopGain.store (linear, std::memory_order_relaxed); }

    float getInputPeak() const noexcept { return inputPeak.load (std::memory_order_relaxed); }
    float getOutputPeak() const noexcept { return outputPeak.load (std::memory_order_relaxed); }
    float getGuitarPeak() const noexcept { return guitarPeak.load (std::memory_order_relaxed); }
    float getLoopPeak() const noexcept { return loopPeak.load (std::memory_order_relaxed); }

    // The last samples of the raw input (before the amp), for the tuner.
    static constexpr int recentInputCapacity = 1 << 13;
    void copyRecentInput (float* destination, int numSamples) const noexcept;

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
    // timeline position of its first sample — where the player heard the
    // arrangement when playing it, i.e. compensated for the round trip (may
    // be negative for a take started at 0).
    // `locateTo` (≥ 0) moves the playhead there first, in the same audio
    // block as the take's first sample (the count-in backs up a bar).
    // `locateTo` may be negative (a count-in plays before the timeline's first
    // bar); noLocate leaves the transport where it is.
    static constexpr std::int64_t noLocate = std::numeric_limits<std::int64_t>::min();
    bool startTake (const juce::File& wavFile, std::int64_t locateTo = noLocate, const juce::File& dryWavFile = {});
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

    // Round trip from the input jack to the speakers, as the device reports
    // it (CoreAudio's figures each include their buffer). Takes, loops and
    // overdubs are placed this much earlier on the timeline, like a DAW's
    // recording-delay compensation. The adjustment (settings) corrects a
    // driver that under-reports.
    double getRoundTripLatencyMs() const noexcept { return roundTripMs.load (std::memory_order_relaxed); }
    int getRecordLatencySamples() const noexcept { return recordLatency.load (std::memory_order_relaxed); }
    void setLatencyAdjustmentSamples (int samples) noexcept;

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
    void renderSong (std::int64_t pos, int numSamples, float* left, float* right, float& peak) noexcept;
    int chunkLength (std::int64_t pos, int wanted, bool cycleActive) const noexcept;
    void renderClips (std::int64_t pos, int numSamples, float* out) noexcept;
    void renderLoop (const float* live, float* loopOut, std::int64_t pos, int numSamples, bool running) noexcept;
    void writeHistory (const float* live, std::int64_t pos, int numSamples) noexcept;

    AmpEngine amp;
    Looper looper;
    Metronome metronome;
    TakeRecorder takeRecorder;
    TakeRecorder dryRecorder; // the guitar before the amp, to change the amp after the take

    juce::SpinLock processLock;

    std::vector<float> liveBuffer, loopBuffer, clipBuffer, mixBuffer, leftBuffer, rightBuffer;
    std::vector<char> coveredBuffer;

    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<int> blockSize { 64 };
    std::atomic<int> inputLatencySamples { 0 };
    std::atomic<double> roundTripMs { 0.0 };
    std::atomic<int> deviceRoundTrip { 0 }, latencyAdjustment { 0 }, recordLatency { 0 };

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

    std::shared_ptr<const SongAudio> pendingSong, activeSong, retiredSong;
    bool hasPendingSong = false;
    std::atomic<std::int64_t> songStart { 0 };
    std::atomic<bool> songMuted { false };
    std::atomic<float> songGain { 1.0f }, songPeak { 0.0f };

    std::atomic<std::int64_t> loopAnchor { 0 }, loopRegionStart { 0 }, loopRegionEnd { -1 };
    std::atomic<int> loopRecordCount { 0 };

    std::atomic<bool> guitarMuted { false }, loopMuted { false };
    std::atomic<float> guitarGain { 1.0f }, loopGain { 1.0f };

    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f }, guitarPeak { 0.0f }, loopPeak { 0.0f };
    std::atomic<float> masterGain { 1.0f }, outputPeakLeft { 0.0f }, outputPeakRight { 0.0f };

    std::array<std::atomic<float>, historyCapacity> history {};
    std::array<std::atomic<float>, recentInputCapacity> recentInput {};
    std::atomic<int> recentInputWrite { 0 };
    std::atomic<int> historySlotSamples { 480 };
    std::int64_t historySlot = -1;
    float historyPeakInSlot = 0.0f;

    std::atomic<bool> takeStartPending { false };
    std::atomic<std::int64_t> takeLocate { noLocate };
    std::atomic<std::int64_t> loopStartAt { noLocate }, loopEndAt { noLocate };
    bool takeRolling = false; // audio thread: the take has its first block
    std::atomic<std::int64_t> takeStartPos { 0 };

    std::vector<float> calibrationBuffer;
    std::atomic<int> calibrationWritten { 0 };
    std::atomic<int> calibrationTarget { 0 };
    std::atomic<double> calibrationFirstHostSeconds { 0.0 };
};
