#include "AmpEngine.h"

#include "NAM/get_dsp.h"

#include <cmath>
#include <filesystem>

void AmpEngine::prepare (double sampleRate, int maxBlockSize)
{
    currentSampleRate = sampleRate;
    currentMaxBlockSize = maxBlockSize;

    {
        const juce::SpinLock::ScopedLockType sl (modelLock);
        if (model != nullptr)
            model->Reset (currentSampleRate, currentMaxBlockSize);
    }

    {
        const juce::SpinLock::ScopedLockType sl (irLock);
        irConvolution.prepare ({ sampleRate, (juce::uint32) maxBlockSize, 1 });
    }
}

bool AmpEngine::loadModel (const juce::File& namFile, juce::String& errorMessage)
{
    try
    {
        auto newModel = nam::get_dsp (std::filesystem::path (namFile.getFullPathName().toStdString()));
        newModel->Reset (currentSampleRate, currentMaxBlockSize);

        const juce::SpinLock::ScopedLockType sl (modelLock);
        model = std::move (newModel);
        modelName = namFile.getFileNameWithoutExtension();
        loaded.store (true, std::memory_order_release);
        return true;
    }
    catch (const std::exception& e)
    {
        errorMessage = juce::String (e.what());
        return false;
    }
}

void AmpEngine::process (float* samples, int numSamples)
{
    const auto inGain = inputGain.load (std::memory_order_relaxed);
    const auto outVolume = outputVolume.load (std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i)
        samples[i] *= inGain;

    // Never hand NAM/Convolution more frames than they were Reset()/prepare()d
    // with: some backends (e.g. PipeWire recovering from an xrun) can grow the
    // callback block size without a matching prepareToPlay(), and their internal
    // buffers are sized to that max — asserts that would catch an overrun are
    // compiled out in Release (NDEBUG), so this must hold even without them.
    const int maxChunk = juce::jmax (1, currentMaxBlockSize.load (std::memory_order_relaxed));

    const juce::SpinLock::ScopedTryLockType ampLock (modelLock);
    const juce::SpinLock::ScopedTryLockType cabLock (irLock);
    const bool ampActive = ampLock.isLocked() && model != nullptr;
    const bool cabActive = cabLock.isLocked() && irLoadedFlag.load (std::memory_order_relaxed);

    int offset = 0;
    while (offset < numSamples)
    {
        const int chunk = juce::jmin (numSamples - offset, maxChunk);
        float* chunkPtr = samples + offset;

        if (ampActive)
        {
            NAM_SAMPLE* in[1] = { chunkPtr };
            NAM_SAMPLE* out[1] = { chunkPtr };
            model->process (in, out, chunk);
        }

        if (cabActive)
        {
            float* channelPtrs[1] = { chunkPtr };
            juce::dsp::AudioBlock<float> block (channelPtrs, 1, (size_t) chunk);
            juce::dsp::ProcessContextReplacing<float> context (block);
            irConvolution.process (context);
        }

        offset += chunk;
    }

    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        samples[i] *= outVolume;
        peak = std::max (peak, std::abs (samples[i]));
    }
    peakLevel.store (peak, std::memory_order_relaxed);
}

void AmpEngine::loadImpulseResponse (const juce::File& irFile)
{
    const juce::SpinLock::ScopedLockType sl (irLock);
    irConvolution.loadImpulseResponse (irFile,
                                        juce::dsp::Convolution::Stereo::no,
                                        juce::dsp::Convolution::Trim::yes,
                                        0);
    irConvolution.prepare ({ currentSampleRate, (juce::uint32) currentMaxBlockSize.load(), 1 });
    irName = irFile.getFileNameWithoutExtension();
    irLoadedFlag.store (true, std::memory_order_release);
}
