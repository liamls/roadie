#include "AudioEngine.h"

#include <iostream>

namespace
{
double hostSecondsNow() noexcept
{
    // mach_absolute_time on macOS: the clock CoreAudio and AVFoundation stamp with.
    return (double) juce::Time::getHighResolutionTicks() / (double) juce::Time::getHighResolutionTicksPerSecond();
}

float peakOf (const float* samples, int numSamples) noexcept
{
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        peak = std::max (peak, std::abs (samples[i]));
    return peak;
}
} // namespace

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine()
{
    takeRecorder.stop();
}

//==============================================================================
void AudioEngine::togglePlay() noexcept
{
    transportStopped.store (! transportStopped.load (std::memory_order_relaxed), std::memory_order_relaxed);
}

void AudioEngine::locate (std::int64_t position) noexcept
{
    locateRequest.store (std::max<std::int64_t> (0, position), std::memory_order_relaxed);
}

void AudioEngine::recordLoop()
{
    // One button cycles like a loop pedal: record → play → overdub → play…
    // A pedal never waits for a play button: it starts the transport.
    play();
    switch (looper.getState())
    {
        case Looper::State::Empty:
        case Looper::State::Recording:
            looper.requestRecord();
            break;
        case Looper::State::Playing:
        case Looper::State::Stopped:
        case Looper::State::Overdubbing:
            looper.requestOverdub();
            break;
    }
}

void AudioEngine::stopAll()
{
    const auto state = looper.getState();
    if (state == Looper::State::Recording)
        looper.requestRecord(); // keep what was played rather than throwing it away
    else if (state == Looper::State::Overdubbing)
        looper.requestOverdub();
    stop();
}

void AudioEngine::setCycle (bool enabled, std::int64_t start, std::int64_t end) noexcept
{
    cycleStart.store (start, std::memory_order_relaxed);
    cycleEnd.store (end, std::memory_order_relaxed);
    cycleEnabled.store (enabled && end > start, std::memory_order_relaxed);
}

void AudioEngine::setPlaybackClips (std::shared_ptr<const PlaybackList> clips)
{
    const juce::SpinLock::ScopedLockType sl (playbackLock);
    retiredClips.reset(); // the audio thread is done with it (it only retires under this lock)
    pendingClips = std::move (clips);
    hasPendingClips = true;
}

void AudioEngine::setLoopPlacement (std::int64_t anchor, std::int64_t regionStart, std::int64_t regionEnd) noexcept
{
    loopAnchor.store (anchor, std::memory_order_relaxed);
    loopRegionStart.store (regionStart, std::memory_order_relaxed);
    loopRegionEnd.store (regionEnd, std::memory_order_relaxed);
}

//==============================================================================
bool AudioEngine::startTake (const juce::File& wavFile)
{
    takeStartPos.store (getTransportPosition(), std::memory_order_release);
    takeStartPending.store (true, std::memory_order_release);
    return takeRecorder.start (wavFile, getSampleRate());
}

void AudioEngine::stopTake()
{
    takeRecorder.stop();
}

//==============================================================================
void AudioEngine::startCalibrationCapture (double seconds)
{
    calibrationTarget.store (0, std::memory_order_release);
    calibrationWritten.store (0, std::memory_order_relaxed);
    calibrationFirstHostSeconds.store (0.0, std::memory_order_relaxed);

    const int wanted = (int) (seconds * getSampleRate());
    calibrationTarget.store (juce::jlimit (0, (int) calibrationBuffer.size(), wanted), std::memory_order_release);
}

bool AudioEngine::isCalibrationCaptureComplete() const noexcept
{
    const int target = calibrationTarget.load (std::memory_order_acquire);
    return target > 0 && calibrationWritten.load (std::memory_order_acquire) >= target;
}

std::vector<float> AudioEngine::getCalibrationCapture (double& firstSampleHostSeconds) const
{
    const int written = calibrationWritten.load (std::memory_order_acquire);
    firstSampleHostSeconds = calibrationFirstHostSeconds.load (std::memory_order_acquire);
    return std::vector<float> (calibrationBuffer.begin(), calibrationBuffer.begin() + written);
}

//==============================================================================
void AudioEngine::loadLoop (const std::vector<float>& samples, int layers)
{
    const juce::SpinLock::ScopedLockType sl (processLock);
    looper.setContent (samples.data(), (int) samples.size(), layers);
}

//==============================================================================
void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const juce::SpinLock::ScopedLockType sl (processLock);

    const double sr = device->getCurrentSampleRate();
    const int bs = device->getCurrentBufferSizeSamples();

    sampleRate.store (sr, std::memory_order_relaxed);
    blockSize.store (bs, std::memory_order_relaxed);
    inputLatencySamples.store (device->getInputLatencyInSamples(), std::memory_order_relaxed);

    amp.prepare (sr, bs);
    looper.prepare (sr);
    metronome.prepare (sr);

    // Some backends hand over bigger blocks than announced (xrun recovery):
    // the callback chunks to this capacity instead of trusting `bs`.
    const size_t capacity = (size_t) std::max (bs, 512);
    liveBuffer.assign (capacity, 0.0f);
    loopBuffer.assign (capacity, 0.0f);
    clipBuffer.assign (capacity, 0.0f);
    mixBuffer.assign (capacity, 0.0f);
    coveredBuffer.assign (capacity, 0);
    historySlotSamples.store (std::max (1, (int) std::round (sr / 100.0)), std::memory_order_relaxed);

    calibrationBuffer.assign ((size_t) (sr * 8.0), 0.0f);
    calibrationTarget.store (0, std::memory_order_relaxed);
    calibrationWritten.store (0, std::memory_order_relaxed);

    const double rt = (bs + device->getInputLatencyInSamples() + device->getOutputLatencyInSamples()) / sr * 1000.0;
    roundTripMs.store (rt, std::memory_order_relaxed);

    std::cerr << "[Roadie] device=" << device->getName()
              << " sampleRate=" << sr
              << " bufferSize=" << bs
              << " inputLatencySamples=" << device->getInputLatencyInSamples()
              << " outputLatencySamples=" << device->getOutputLatencyInSamples()
              << " estRoundTripMs=" << rt
              << std::endl;
}

void AudioEngine::audioDeviceStopped()
{
}

double AudioEngine::estimateInputHostTime (const juce::AudioIODeviceCallbackContext& context, int numSamples) const noexcept
{
    const double sr = getSampleRate();
    const double inputLatency = inputLatencySamples.load (std::memory_order_relaxed);

    // With output channels open, CoreAudio stamps the time the first *output*
    // sample reaches the device; the matching input was captured roughly two
    // buffers earlier, plus the converter latency. Whatever this estimate
    // misses is a constant the clap calibration measures and removes.
    if (context.hostTimeNs != nullptr)
        return (double) *context.hostTimeNs * 1.0e-9 - (2.0 * numSamples + inputLatency) / sr;

    return hostSecondsNow() - (numSamples + inputLatency) / sr;
}

void AudioEngine::updateTransport() noexcept
{
    const auto request = locateRequest.exchange (-1, std::memory_order_relaxed);
    if (request >= 0)
    {
        transportPos.store (request, std::memory_order_relaxed);
        historySlot = -1;
    }

    transportRunning.store (! transportStopped.load (std::memory_order_relaxed), std::memory_order_relaxed);
}

void AudioEngine::pickUpPlaybackClips() noexcept
{
    const juce::SpinLock::ScopedTryLockType sl (playbackLock);
    if (! sl.isLocked() || ! hasPendingClips || retiredClips != nullptr)
        return;

    // Moves only: the old list is released later by the message thread.
    retiredClips = std::move (activeClips);
    activeClips = std::move (pendingClips);
    hasPendingClips = false;
}

int AudioEngine::chunkLength (std::int64_t pos, int wanted, bool cycleActive) const noexcept
{
    // Split blocks where something changes mid-block, so every chunk is one
    // continuous stretch of timeline: the cycle end and the loop region edges.
    std::int64_t limit = pos + wanted;
    auto clampTo = [&] (std::int64_t boundary)
    {
        if (boundary > pos && boundary < limit)
            limit = boundary;
    };

    if (cycleActive)
        clampTo (cycleEnd.load (std::memory_order_relaxed));
    clampTo (loopRegionStart.load (std::memory_order_relaxed));
    clampTo (loopRegionEnd.load (std::memory_order_relaxed));
    return (int) (limit - pos);
}

void AudioEngine::renderClips (std::int64_t pos, int numSamples, float* out) noexcept
{
    juce::FloatVectorOperations::clear (out, numSamples);
    if (activeClips == nullptr || activeClips->empty())
        return;

    // Top-most region wins where regions overlap.
    std::fill (coveredBuffer.begin(), coveredBuffer.begin() + numSamples, (char) 0);
    for (auto it = activeClips->rbegin(); it != activeClips->rend(); ++it)
    {
        const auto& clip = *it;
        const auto from = std::max (pos, clip.start);
        const auto to = std::min (pos + numSamples, clip.start + clip.length);
        if (from >= to || clip.audio == nullptr)
            continue;

        const auto& audio = *clip.audio;
        for (auto t = from; t < to; ++t)
        {
            const auto i = (size_t) (t - pos);
            if (coveredBuffer[i] != 0)
                continue;
            coveredBuffer[i] = 1;
            const auto source = clip.sourceStart + (t - clip.start);
            if (source >= 0 && source < (std::int64_t) audio.size())
                out[i] = audio[(size_t) source];
        }
    }
}

void AudioEngine::renderLoop (const float* live, float* loopOut, std::int64_t pos, int numSamples, bool running) noexcept
{
    const auto before = looper.getState();

    if (! running)
    {
        looper.update();
        juce::FloatVectorOperations::clear (loopOut, numSamples);
        return;
    }

    const auto regionStart = loopRegionStart.load (std::memory_order_relaxed);
    const auto regionEnd = loopRegionEnd.load (std::memory_order_relaxed);
    const bool inRegion = pos >= regionStart && (regionEnd < 0 || pos < regionEnd);

    if (before == Looper::State::Empty || before == Looper::State::Recording || inRegion)
    {
        looper.setPlayhead ((int) ((pos - loopAnchor.load (std::memory_order_relaxed))
                                   % std::max (1, looper.getRecordedLengthSamples())));
        looper.process (live, loopOut, numSamples);
    }
    else
    {
        looper.update();
        juce::FloatVectorOperations::clear (loopOut, numSamples);
    }

    // A new loop: its first sample is here, it plays from here on.
    if (before != Looper::State::Recording && looper.getState() == Looper::State::Recording)
    {
        setLoopPlacement (pos, pos, -1);
        loopRecordCount.fetch_add (1, std::memory_order_relaxed);
    }
}

void AudioEngine::writeHistory (const float* live, std::int64_t pos, int numSamples) noexcept
{
    const int slotSamples = historySlotSamples.load (std::memory_order_relaxed);
    for (int i = 0; i < numSamples; ++i)
    {
        const auto slot = (pos + i) / slotSamples;
        if (slot != historySlot)
        {
            historySlot = slot;
            historyPeakInSlot = 0.0f;
        }
        historyPeakInSlot = std::max (historyPeakInSlot, std::abs (live[i]));
        history[(size_t) (slot & (historyCapacity - 1))].store (historyPeakInSlot, std::memory_order_relaxed);
    }
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                                    float* const* outputChannelData, int numOutputChannels,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext& context)
{
    const juce::SpinLock::ScopedTryLockType sl (processLock);
    if (! sl.isLocked() || liveBuffer.empty())
    {
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputChannelData[ch] != nullptr)
                juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);
        return;
    }

    const double blockHostSeconds = estimateInputHostTime (context, numSamples);
    const double sr = getSampleRate();
    const bool muteGuitar = guitarMuted.load (std::memory_order_relaxed);
    const bool muteLoop = loopMuted.load (std::memory_order_relaxed);
    const float gGain = guitarGain.load (std::memory_order_relaxed);
    const float lGain = loopGain.load (std::memory_order_relaxed);
    const float* rawInput = numInputChannels > 0 ? inputChannelData[0] : nullptr;

    updateTransport();
    pickUpPlaybackClips();

    const bool running = transportRunning.load (std::memory_order_relaxed);
    const bool recordingTake = takeRecorder.isRecording();
    const bool cycleActive = cycleEnabled.load (std::memory_order_relaxed) && ! recordingTake;

    float blockInputPeak = 0.0f, blockOutputPeak = 0.0f, blockGuitarPeak = 0.0f, blockLoopPeak = 0.0f;
    const int capacity = (int) liveBuffer.size();

    for (int offset = 0; offset < numSamples;)
    {
        auto pos = transportPos.load (std::memory_order_relaxed);
        if (running && cycleActive && pos == cycleEnd.load (std::memory_order_relaxed))
        {
            pos = cycleStart.load (std::memory_order_relaxed);
            transportPos.store (pos, std::memory_order_relaxed);
        }

        int n = std::min (capacity, numSamples - offset);
        if (running)
            n = chunkLength (pos, n, cycleActive);

        float* live = liveBuffer.data();
        float* loop = loopBuffer.data();
        float* clips = clipBuffer.data();
        float* mix = mixBuffer.data();

        if (rawInput != nullptr)
            juce::FloatVectorOperations::copy (live, rawInput + offset, n);
        else
            juce::FloatVectorOperations::clear (live, n);

        blockInputPeak = std::max (blockInputPeak, peakOf (live, n));

        // Calibration listens to the raw input: the amp adds no latency, and
        // a dry clap/strum transient is sharper than an amped one.
        const int target = calibrationTarget.load (std::memory_order_acquire);
        int written = calibrationWritten.load (std::memory_order_relaxed);
        if (written < target)
        {
            if (written == 0)
                calibrationFirstHostSeconds.store (blockHostSeconds + offset / sr, std::memory_order_relaxed);
            const int toCopy = std::min (n, target - written);
            std::copy (live, live + toCopy, calibrationBuffer.begin() + written);
            calibrationWritten.store (written + toCopy, std::memory_order_release);
        }

        amp.process (live, n);
        renderLoop (live, loop, pos, n, running);

        if (running)
            renderClips (pos, n, clips);
        else
            juce::FloatVectorOperations::clear (clips, n);

        if (takeStartPending.exchange (false, std::memory_order_acq_rel))
            takeStartPos.store (pos, std::memory_order_release);
        takeRecorder.push (live, n, blockHostSeconds + offset / sr);

        for (int i = 0; i < n; ++i)
        {
            const float guitar = muteGuitar ? 0.0f : (live[i] + clips[i]) * gGain;
            const float looped = muteLoop ? 0.0f : loop[i] * lGain;
            blockGuitarPeak = std::max (blockGuitarPeak, std::abs (guitar));
            blockLoopPeak = std::max (blockLoopPeak, std::abs (looped));
            mix[i] = guitar + looped;
        }

        if (running)
            writeHistory (live, pos, n);

        blockOutputPeak = std::max (blockOutputPeak, peakOf (mix, n));
        metronome.process (mix, n, pos, running);

        if (running)
            transportPos.store (pos + n, std::memory_order_relaxed);

        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputChannelData[ch] != nullptr)
                juce::FloatVectorOperations::copy (outputChannelData[ch] + offset, mix, n);

        offset += n;
    }

    inputPeak.store (blockInputPeak, std::memory_order_relaxed);
    outputPeak.store (blockOutputPeak, std::memory_order_relaxed);
    guitarPeak.store (blockGuitarPeak, std::memory_order_relaxed);
    loopPeak.store (blockLoopPeak, std::memory_order_relaxed);
}
