#include "AmpEngine.h"

#include "NAM/get_dsp.h"

#include <cmath>
#include <filesystem>

void AmpEngine::prepare (double sampleRate, int maxBlockSize)
{
    currentSampleRate = sampleRate;
    currentMaxBlockSize = maxBlockSize;
    toneStack.prepare (sampleRate);
    gate.prepare (sampleRate);

    // The audio callback is not running here (AudioEngine holds its lock).
    if (model != nullptr)
        model->Reset (currentSampleRate, currentMaxBlockSize);

    {
        const juce::SpinLock::ScopedLockType sl (handoffLock);
        if (pendingModel != nullptr)
            pendingModel->Reset (currentSampleRate, currentMaxBlockSize);
    }

    irConvolution.prepare ({ sampleRate, (juce::uint32) maxBlockSize, 1 });
}

void AmpEngine::pickUpPendingModel() noexcept
{
    const juce::SpinLock::ScopedTryLockType sl (handoffLock);
    if (! sl.isLocked() || pendingModel == nullptr || retiredModel != nullptr)
        return; // next block

    // Moves only: the old capture is freed later, off the audio thread.
    retiredModel = std::move (model);
    model = std::move (pendingModel);
    loudnessCompensation.store (pendingCompensation, std::memory_order_relaxed);
}

bool AmpEngine::loadModel (const juce::File& namFile, juce::String& errorMessage)
{
    try
    {
        auto newModel = nam::get_dsp (std::filesystem::path (namFile.getFullPathName().toStdString()));
        newModel->Reset (currentSampleRate, currentMaxBlockSize);

        constexpr double targetLoudnessDb = -18.0;
        const float compensation = newModel->HasLoudness()
                                       ? juce::Decibels::decibelsToGain ((float) (targetLoudnessDb - newModel->GetLoudness()))
                                       : 1.0f;

        std::unique_ptr<nam::DSP> retired, replaced;
        {
            const juce::SpinLock::ScopedLockType sl (handoffLock);
            retired = std::move (retiredModel);  // done with by the audio thread
            replaced = std::move (pendingModel); // never picked up
            pendingModel = std::move (newModel);
            pendingCompensation = compensation;
        } // both freed here, outside the lock

        modelName = namFile.getFileNameWithoutExtension();
        modelFile = namFile;
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
    auto outVolume = outputVolume.load (std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i)
        samples[i] *= inGain;
    gate.process (samples, numSamples);

    // Never hand NAM/Convolution more frames than they were Reset()/prepare()d
    // with: some backends (e.g. PipeWire recovering from an xrun) can grow the
    // callback block size without a matching prepareToPlay(), and their internal
    // buffers are sized to that max — asserts that would catch an overrun are
    // compiled out in Release (NDEBUG), so this must hold even without them.
    const int maxChunk = juce::jmax (1, currentMaxBlockSize.load (std::memory_order_relaxed));

    pickUpPendingModel();
    const bool ampActive = model != nullptr && ! isBypassed (Block::capture);
    const bool eqActive = ! isBypassed (Block::eq);
    const bool cabActive = irLoadedFlag.load (std::memory_order_relaxed) && ! isBypassed (Block::cab);

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

        if (eqActive)
            toneStack.process (chunkPtr, chunk);

        if (cabActive)
        {
            float* channelPtrs[1] = { chunkPtr };
            juce::dsp::AudioBlock<float> block (channelPtrs, 1, (size_t) chunk);
            juce::dsp::ProcessContextReplacing<float> context (block);
            irConvolution.process (context);
        }

        offset += chunk;
    }

    if (ampActive)
        outVolume *= loudnessCompensation.load (std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i)
        samples[i] *= outVolume;
}

void AmpEngine::loadImpulseResponse (const juce::File& newIrFile)
{
    irConvolution.loadImpulseResponse (newIrFile,
                                        juce::dsp::Convolution::Stereo::no,
                                        juce::dsp::Convolution::Trim::yes,
                                        0);
    irName = newIrFile.getFileNameWithoutExtension();
    irFile = newIrFile;
    irLoadedFlag.store (true, std::memory_order_release);
}

void AmpEngine::clearImpulseResponse()
{
    irLoadedFlag.store (false, std::memory_order_release);
    irName = {};
    irFile = juce::File();
}
