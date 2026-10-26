#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <memory>

// Streams the performance mix to a WAV file while a take is being recorded.
// The audio thread only pushes into a lock-free FIFO (ThreadedWriter); a
// background thread does the disk writes.
class TakeRecorder
{
public:
    TakeRecorder();
    ~TakeRecorder();

    // Message thread.
    bool start (const juce::File& wavFile, double sampleRate);
    void stop();

    bool isRecording() const noexcept { return active.load (std::memory_order_acquire); }
    juce::File getCurrentFile() const { return currentFile; }

    double getRecordedSeconds() const noexcept;

    // Host-clock time (seconds, same clock as the camera timestamps) of the
    // first recorded sample, or 0 until the first block has been written.
    double getFirstSampleHostSeconds() const noexcept { return firstSampleHostSeconds.load (std::memory_order_acquire); }

    // Audio thread. `blockHostSeconds` is the capture time of samples[0].
    void push (const float* samples, int numSamples, double blockHostSeconds) noexcept;

private:
    juce::TimeSliceThread writerThread { "Roadie take writer" };
    juce::SpinLock writerLock;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter;
    std::atomic<bool> active { false };
    std::atomic<double> firstSampleHostSeconds { 0.0 };
    std::atomic<std::int64_t> samplesWritten { 0 };
    double currentSampleRate = 48000.0;
    juce::File currentFile;
};
