#include "AmpEngine.h"

#include "NAM/get_dsp.h"

#include <cmath>
#include <filesystem>

namespace
{
constexpr juce::int64 maxCaptureBytes = 256 * 1024 * 1024;
}

void AmpEngine::prepare (double sampleRate, int maxBlockSize)
{
    currentSampleRate.store (sampleRate);
    currentMaxBlockSize.store (maxBlockSize);
    toneStack.prepare (sampleRate);
    gate.prepare (sampleRate);

    // The audio callback is not running here (AudioEngine holds its lock).
    if (model != nullptr)
        model->Reset (sampleRate, maxBlockSize);

    {
        const juce::SpinLock::ScopedLockType sl (handoffLock);
        if (pendingModel != nullptr)
            pendingModel->Reset (sampleRate, maxBlockSize);
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
        // A capture is a few hundred KB of JSON; anything far beyond that is not one.
        if (namFile.getSize() > maxCaptureBytes)
        {
            errorMessage = juce::String ("This file is too large to be a NAM capture.");
            return false;
        }

        auto newModel = nam::get_dsp (std::filesystem::path (namFile.getFullPathName().toStdString()));
        if (newModel == nullptr)
        {
            errorMessage = juce::String ("Unsupported capture.");
            return false;
        }
        newModel->Reset (currentSampleRate.load(), currentMaxBlockSize.load());

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
    catch (...)
    {
        errorMessage = juce::String ("Unsupported capture.");
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

    // A diverging capture must never reach the speakers as NaN / infinity.
    for (int i = 0; i < numSamples; ++i)
    {
        const float v = samples[i] * outVolume;
        samples[i] = std::isfinite (v) ? v : 0.0f;
    }
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
