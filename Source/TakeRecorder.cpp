#include "TakeRecorder.h"

TakeRecorder::TakeRecorder()
{
    writerThread.startThread();
}

TakeRecorder::~TakeRecorder()
{
    stop();
    writerThread.stopThread (2000);
}

bool TakeRecorder::start (const juce::File& wavFile, double sampleRate)
{
    stop();

    wavFile.getParentDirectory().createDirectory();
    wavFile.deleteFile();

    std::unique_ptr<juce::OutputStream> stream = wavFile.createOutputStream();
    if (stream == nullptr)
        return false;

    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (sampleRate)
                                                   .withNumChannels (1)
                                                   .withBitsPerSample (24));
    if (writer == nullptr)
        return false;

    currentSampleRate = sampleRate;
    currentFile = wavFile;
    firstSampleHostSeconds.store (0.0, std::memory_order_relaxed);
    samplesWritten.store (0, std::memory_order_relaxed);

    // ~3 s of slack at 48 kHz: plenty for the disk thread to keep up.
    auto newWriter = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer.release(), writerThread, 1 << 17);

    {
        const juce::SpinLock::ScopedLockType sl (writerLock);
        threadedWriter = std::move (newWriter);
    }
    active.store (true, std::memory_order_release);
    return true;
}

void TakeRecorder::stop()
{
    active.store (false, std::memory_order_release);

    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> finished;
    {
        const juce::SpinLock::ScopedLockType sl (writerLock);
        finished = std::move (threadedWriter);
    }
    // Destroying it flushes whatever is still queued and closes the file.
    finished.reset();
}

double TakeRecorder::getRecordedSeconds() const noexcept
{
    return (double) samplesWritten.load (std::memory_order_relaxed) / currentSampleRate;
}

void TakeRecorder::push (const float* samples, int numSamples, double blockHostSeconds) noexcept
{
    if (! active.load (std::memory_order_acquire))
        return;

    // Never wait on the message thread: if it's swapping writers right now,
    // this one block is simply not part of any take.
    const juce::SpinLock::ScopedTryLockType sl (writerLock);
    if (! sl.isLocked() || threadedWriter == nullptr)
        return;

    if (samplesWritten.load (std::memory_order_relaxed) == 0)
        firstSampleHostSeconds.store (blockHostSeconds, std::memory_order_release);

    const float* channels[1] = { samples };
    if (threadedWriter->write (channels, numSamples))
        samplesWritten.fetch_add (numSamples, std::memory_order_relaxed);
}
