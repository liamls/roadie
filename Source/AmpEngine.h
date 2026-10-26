#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <memory>

#include "NAM/dsp.h"
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

    // Loads a cabinet impulse response (.wav). Must be called off the audio
    // thread (JUCE loads it on a background thread, but re-preparing to make
    // it active involves a lock swap here).
    void loadImpulseResponse (const juce::File& file);
    bool isIrLoaded() const noexcept { return irLoadedFlag.load (std::memory_order_acquire); }
    juce::String getIrName() const { return irName; }
    juce::File getIrFile() const { return irFile; }
    void clearImpulseResponse();

    void setInputGainDb (float db) noexcept { inputGain.store (juce::Decibels::decibelsToGain (db), std::memory_order_relaxed); }
    void setOutputVolumeDb (float db) noexcept { outputVolume.store (juce::Decibels::decibelsToGain (db), std::memory_order_relaxed); }

    // Grave / Médium / Aigu / Présence, applied between the capture and the cab.
    ToneStack& getToneStack() noexcept { return toneStack; }

    // Peak of the last processed block, linear 0..~1+. Safe to poll from the message thread.
    float getPeakLevel() const noexcept { return peakLevel.load (std::memory_order_relaxed); }

    // Audio-thread only. Processes numSamples in place; runs the dry signal
    // through input/output trim even if no model is loaded or a swap is in
    // progress, so the knobs stay meaningful while auditioning raw guitar.
    void process (float* samples, int numSamples);

private:
    juce::SpinLock modelLock;
    std::unique_ptr<nam::DSP> model;
    std::atomic<bool> loaded { false };
    juce::String modelName;
    juce::File modelFile;

    // Brings every capture to the same perceived level (NAM's own -18 dB
    // convention), so switching captures in the library doesn't jump in volume.
    std::atomic<float> loudnessCompensation { 1.0f };

    ToneStack toneStack;

    juce::SpinLock irLock;
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
