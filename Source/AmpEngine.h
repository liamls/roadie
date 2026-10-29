#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <memory>

#include "NAM/dsp.h"
#include "NoiseGate.h"
#include "ToneStack.h"

class AmpEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize);

    // Loads a .nam capture. Must be called off the audio thread (it allocates).
    bool loadModel (const juce::File& namFile, juce::String& errorMessage);

    bool isLoaded() const noexcept { return loaded.load (std::memory_order_acquire); }
    juce::String getModelName() const { return modelName; }
    juce::File getModelFile() const { return modelFile; }

    // Loads a cabinet impulse response (.wav), from the message thread. JUCE's
    // convolution loads it in the background and keeps playing the previous
    // one until the new one is ready: no lock, no gap.
    void loadImpulseResponse (const juce::File& file);
    bool isIrLoaded() const noexcept { return irLoadedFlag.load (std::memory_order_acquire); }
    // The convolution has swapped the new response in (it does so from process()).
    bool isIrReady() const noexcept { return irConvolution.getCurrentIRSize() > 0; }
    juce::String getIrName() const { return irName; }
    juce::File getIrFile() const { return irFile; }
    void clearImpulseResponse();

    void setInputGainDb (float db) noexcept { inputGain.store (juce::Decibels::decibelsToGain (db), std::memory_order_relaxed); }
    void setOutputVolumeDb (float db) noexcept { outputVolume.store (juce::Decibels::decibelsToGain (db), std::memory_order_relaxed); }

    // Grave / Médium / Aigu / Présence, applied between the capture and the cab.
    ToneStack& getToneStack() noexcept { return toneStack; }
    // Gate at the input, before the capture.
    NoiseGate& getGate() noexcept { return gate; }

    // The chain, block by block: Entrée (gain + gate) → Capture → EQ →
    // Baffle → Sortie. Each of the middle three can be bypassed (any thread).
    enum class Block { capture, eq, cab };
    void setBypassed (Block block, bool bypassed) noexcept { bypass[(size_t) block].store (bypassed, std::memory_order_relaxed); }
    bool isBypassed (Block block) const noexcept { return bypass[(size_t) block].load (std::memory_order_relaxed); }

    // Peak of the last processed block, linear 0..~1+. Safe to poll from the message thread.
    float getPeakLevel() const noexcept { return peakLevel.load (std::memory_order_relaxed); }

    // Audio-thread only. Processes numSamples in place; runs the dry signal
    // through input/output trim even if no model is loaded or a swap is in
    // progress, so the knobs stay meaningful while auditioning raw guitar.
    void process (float* samples, int numSamples);

private:
    void pickUpPendingModel() noexcept;

    // The audio thread owns `model`. A new capture is handed over through
    // `pending` and the one it replaces comes back through `retired`, freed
    // by the next load — the audio thread never waits, never frees, and
    // never plays a block without a capture while switching.
    std::unique_ptr<nam::DSP> model;
    juce::SpinLock handoffLock;
    std::unique_ptr<nam::DSP> pendingModel, retiredModel;
    float pendingCompensation = 1.0f;
    std::atomic<bool> loaded { false };
    juce::String modelName;
    juce::File modelFile;

    // Brings every capture to the same perceived level (NAM's own -18 dB
    // convention), so switching captures in the library doesn't jump in volume.
    std::atomic<float> loudnessCompensation { 1.0f };

    ToneStack toneStack;
    NoiseGate gate;
    std::array<std::atomic<bool>, 3> bypass {};

    juce::dsp::Convolution irConvolution;
    std::atomic<bool> irLoadedFlag { false };
    juce::String irName;
    juce::File irFile;

    std::atomic<float> inputGain { 1.0f };
    std::atomic<float> outputVolume { 1.0f };
    std::atomic<float> peakLevel { 0.0f };

    double currentSampleRate = 44100.0;
    // Read on the audio thread (process() chunks to this size), written when
    // prepare()/loadModel() run — must be atomic.
    std::atomic<int> currentMaxBlockSize { 512 };
};
