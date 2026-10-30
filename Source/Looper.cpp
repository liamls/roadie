#include "Looper.h"

#include <algorithm>
#include <cmath>

void Looper::prepare (double newSampleRate, double maxLoopSeconds)
{
    const int newMaxLength = (int) (maxLoopSeconds * newSampleRate);

    // Same rate and capacity: the device was merely restarted (buffer size
    // change, settings dialog), keep the loop the player already has.
    if (std::abs (newSampleRate - sampleRate) < 0.5 && newMaxLength == maxLengthSamples)
        return;

    // Sample-rate changes mid-session wipe the loop (no resampling for v1) —
    // acceptable: this only happens if the user changes audio device config.
    sampleRate = newSampleRate;
    maxLengthSamples = newMaxLength;
    buffer.assign ((size_t) maxLengthSamples, 0.0f);
    undoSnapshot.assign ((size_t) maxLengthSamples, 0.0f);
    recordedLength.store (0, std::memory_order_relaxed);
    playhead.store (0, std::memory_order_relaxed);
    layerCount.store (0, std::memory_order_relaxed);
    undoAvailable = false;
    state.store (State::Empty, std::memory_order_relaxed);
}

void Looper::requestRecord() { recordRequested.store (true, std::memory_order_relaxed); }
void Looper::requestOverdub() { overdubRequested.store (true, std::memory_order_relaxed); }
void Looper::requestStop() { stopRequested.store (true, std::memory_order_relaxed); }
void Looper::requestRestart() { restartRequested.store (true, std::memory_order_relaxed); }
void Looper::requestUndo() { undoRequested.store (true, std::memory_order_relaxed); }
void Looper::requestClear() { clearRequested.store (true, std::memory_order_relaxed); }

double Looper::getLoopLengthSeconds() const noexcept
{
    return sampleRate > 0.0 ? (double) recordedLength.load (std::memory_order_relaxed) / sampleRate : 0.0;
}

float Looper::getPlayheadFraction() const noexcept
{
    const int len = recordedLength.load (std::memory_order_relaxed);
    if (len <= 0)
        return 0.0f;
    return (float) playhead.load (std::memory_order_relaxed) / (float) len;
}

void Looper::getOverview (float* peaks, int numPeaks) const noexcept
{
    const int len = recordedLength.load (std::memory_order_relaxed);
    for (int p = 0; p < numPeaks; ++p)
    {
        float peak = 0.0f;
        if (len > 0)
        {
            const int start = (int) ((long long) p * len / numPeaks);
            const int end = std::max (start + 1, (int) ((long long) (p + 1) * len / numPeaks));
            for (int i = start; i < end && i < len; ++i)
                peak = std::max (peak, std::abs (buffer[(size_t) i]));
        }
        peaks[p] = peak;
    }
}

void Looper::setContent (const float* data, int numSamples, int layers)
{
    const int len = std::clamp (numSamples, 0, maxLengthSamples);
    std::copy (data, data + len, buffer.begin());
    recordedLength.store (len, std::memory_order_relaxed);
    playhead.store (0, std::memory_order_relaxed);
    layerCount.store (len > 0 ? std::max (1, layers) : 0, std::memory_order_relaxed);
    undoAvailable = false;
    state.store (len > 0 ? State::Playing : State::Empty, std::memory_order_relaxed);
}

void Looper::setPlayhead (int sample) noexcept
{
    const int len = recordedLength.load (std::memory_order_relaxed);
    if (len > 0 && state.load (std::memory_order_relaxed) != State::Recording)
        playhead.store (((sample % len) + len) % len, std::memory_order_relaxed);
}

void Looper::applyPendingRequests()
{
    if (clearRequested.exchange (false, std::memory_order_relaxed))
    {
        recordedLength.store (0, std::memory_order_relaxed);
        playhead.store (0, std::memory_order_relaxed);
        layerCount.store (0, std::memory_order_relaxed);
        undoAvailable = false;
        state.store (State::Empty, std::memory_order_relaxed);
    }

    if (undoRequested.exchange (false, std::memory_order_relaxed) && undoAvailable
        && state.load (std::memory_order_relaxed) != State::Overdubbing)
    {
        const int len = recordedLength.load (std::memory_order_relaxed);
        std::copy (undoSnapshot.begin(), undoSnapshot.begin() + len, buffer.begin());
        undoAvailable = false;
        layerCount.store (std::max (1, layerCount.load (std::memory_order_relaxed) - 1), std::memory_order_relaxed);
    }

    if (restartRequested.exchange (false, std::memory_order_relaxed))
        playhead.store (0, std::memory_order_relaxed);

    if (recordRequested.exchange (false, std::memory_order_relaxed))
    {
        const auto current = state.load (std::memory_order_relaxed);
        if (current == State::Empty)
        {
            recordedLength.store (0, std::memory_order_relaxed);
            playhead.store (0, std::memory_order_relaxed);
            state.store (State::Recording, std::memory_order_relaxed);
        }
        else if (current == State::Recording)
        {
            playhead.store (0, std::memory_order_relaxed);
            const bool hasContent = recordedLength.load (std::memory_order_relaxed) > 0;
            layerCount.store (hasContent ? 1 : 0, std::memory_order_relaxed);
            state.store (hasContent ? State::Playing : State::Empty, std::memory_order_relaxed);
        }
    }

    if (overdubRequested.exchange (false, std::memory_order_relaxed))
    {
        const auto current = state.load (std::memory_order_relaxed);
        const int len = recordedLength.load (std::memory_order_relaxed);

        if ((current == State::Playing || current == State::Stopped) && len > 0)
        {
            std::copy (buffer.begin(), buffer.begin() + len, undoSnapshot.begin());
            undoAvailable = true;
            layerCount.fetch_add (1, std::memory_order_relaxed);
            state.store (State::Overdubbing, std::memory_order_relaxed);
        }
        else if (current == State::Overdubbing)
        {
            state.store (State::Playing, std::memory_order_relaxed);
        }
    }

    if (stopRequested.exchange (false, std::memory_order_relaxed))
    {
        const auto current = state.load (std::memory_order_relaxed);
        if (current == State::Playing || current == State::Overdubbing)
            state.store (State::Stopped, std::memory_order_relaxed);
    }
}

void Looper::process (const float* input, float* loopOutput, int numSamples)
{
    applyPendingRequests();

    auto currentState = state.load (std::memory_order_relaxed);
    int len = recordedLength.load (std::memory_order_relaxed);
    int head = playhead.load (std::memory_order_relaxed);
    const int delay = len > 0 ? recordDelay.load (std::memory_order_relaxed) % len : 0;

    for (int i = 0; i < numSamples; ++i)
    {
        float out = 0.0f;

        switch (currentState)
        {
            case State::Recording:
                if (len < maxLengthSamples)
                {
                    buffer[(size_t) len] = input[i];
                    ++len;
                }
                else
                {
                    // Hit the safety cap mid-block: stop recording, start playing what we have.
                    currentState = State::Playing;
                    layerCount.store (1, std::memory_order_relaxed);
                    head = 0;
                }
                break;

            case State::Playing:
                if (len > 0)
                {
                    out = buffer[(size_t) head];
                    head = (head + 1) % len;
                }
                break;

            case State::Overdubbing:
                if (len > 0)
                {
                    // The previous layers fade a little each pass; the new
                    // playing lands where it was heard, `delay` behind the head.
                    out = buffer[(size_t) head];
                    auto& slot = buffer[(size_t) ((head - delay + len) % len)];
                    slot = slot * overdubFeedback + input[i];
                    head = (head + 1) % len;
                }
                break;

            case State::Empty:
            case State::Stopped:
            default:
                break;
        }

        loopOutput[i] = out;
    }

    recordedLength.store (len, std::memory_order_relaxed);
    playhead.store (head, std::memory_order_relaxed);
    state.store (currentState, std::memory_order_relaxed);
}
