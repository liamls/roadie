#include "Looper.h"

#include <algorithm>

void Looper::prepare (double newSampleRate, double maxLoopSeconds)
{
    // Sample-rate changes mid-session wipe the loop (no resampling for v1) —
    // acceptable: this only happens if the user changes audio device config.
    sampleRate = newSampleRate;
    maxLengthSamples = (int) (maxLoopSeconds * sampleRate);
    buffer.assign ((size_t) maxLengthSamples, 0.0f);
    undoSnapshot.assign ((size_t) maxLengthSamples, 0.0f);
    recordedLength.store (0, std::memory_order_relaxed);
    playhead.store (0, std::memory_order_relaxed);
    undoAvailable = false;
    state.store (State::Empty, std::memory_order_relaxed);
}

void Looper::requestRecord() { recordRequested.store (true, std::memory_order_relaxed); }
void Looper::requestOverdub() { overdubRequested.store (true, std::memory_order_relaxed); }
void Looper::requestPlayStop() { playStopRequested.store (true, std::memory_order_relaxed); }
void Looper::requestUndo() { undoRequested.store (true, std::memory_order_relaxed); }
void Looper::requestClear() { clearRequested.store (true, std::memory_order_relaxed); }

double Looper::getLoopLengthSeconds() const noexcept
{
    return (double) recordedLength.load (std::memory_order_relaxed) / sampleRate;
}

float Looper::getPlayheadFraction() const noexcept
{
    const int len = recordedLength.load (std::memory_order_relaxed);
    if (len <= 0)
        return 0.0f;
    return (float) playhead.load (std::memory_order_relaxed) / (float) len;
}

void Looper::applyPendingRequests()
{
    if (clearRequested.exchange (false, std::memory_order_relaxed))
    {
        recordedLength.store (0, std::memory_order_relaxed);
        playhead.store (0, std::memory_order_relaxed);
        undoAvailable = false;
        state.store (State::Empty, std::memory_order_relaxed);
    }

    if (undoRequested.exchange (false, std::memory_order_relaxed) && undoAvailable)
    {
        const int len = recordedLength.load (std::memory_order_relaxed);
        std::copy (undoSnapshot.begin(), undoSnapshot.begin() + len, buffer.begin());
        undoAvailable = false;
    }

    if (recordRequested.exchange (false, std::memory_order_relaxed))
    {
        const auto current = state.load (std::memory_order_relaxed);
        if (current == State::Empty)
        {
            recordedLength.store (0, std::memory_order_relaxed);
            state.store (State::Recording, std::memory_order_relaxed);
        }
        else if (current == State::Recording)
        {
            playhead.store (0, std::memory_order_relaxed);
            const bool hasContent = recordedLength.load (std::memory_order_relaxed) > 0;
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
            state.store (State::Overdubbing, std::memory_order_relaxed);
        }
        else if (current == State::Overdubbing)
        {
            state.store (State::Playing, std::memory_order_relaxed);
        }
    }

    if (playStopRequested.exchange (false, std::memory_order_relaxed))
    {
        const auto current = state.load (std::memory_order_relaxed);
        if (current == State::Playing || current == State::Overdubbing)
            state.store (State::Stopped, std::memory_order_relaxed);
        else if (current == State::Stopped && recordedLength.load (std::memory_order_relaxed) > 0)
            state.store (State::Playing, std::memory_order_relaxed);
    }
}

void Looper::process (float* samples, int numSamples)
{
    applyPendingRequests();

    auto currentState = state.load (std::memory_order_relaxed);
    int len = recordedLength.load (std::memory_order_relaxed);
    int head = playhead.load (std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i)
    {
        switch (currentState)
        {
            case State::Recording:
                if (len < maxLengthSamples)
                {
                    buffer[(size_t) len] = samples[i];
                    ++len;
                }
                else
                {
                    // Hit the safety cap mid-block: stop recording, start playing what we have.
                    currentState = State::Playing;
                    head = 0;
                }
                break;

            case State::Playing:
                if (len > 0)
                {
                    samples[i] += buffer[(size_t) head];
                    head = (head + 1) % len;
                }
                break;

            case State::Overdubbing:
                if (len > 0)
                {
                    const float mixed = buffer[(size_t) head] * overdubFeedback + samples[i];
                    buffer[(size_t) head] = mixed;
                    samples[i] = mixed;
                    head = (head + 1) % len;
                }
                break;

            case State::Empty:
            case State::Stopped:
            default:
                break;
        }
    }

    recordedLength.store (len, std::memory_order_relaxed);
    playhead.store (head, std::memory_order_relaxed);
    state.store (currentState, std::memory_order_relaxed);
}
