#pragma once

#include <array>
#include <atomic>

// Post-amp tone controls (Bass / Mid / Treble / Presence) as four RBJ
// biquads in series. Deliberately free of JUCE dependencies, comme Looper, pour
// rester testable avec un simple compilateur (voir Tests/ToneStackTests.cpp).
//
// Gains are set from any thread through atomics; the audio thread notices a
// change at the top of process() and recomputes the coefficients in place
// (a handful of trig calls, no allocation).
class ToneStack
{
public:
    enum Band
    {
        bass,
        mid,
        treble,
        presence,
        numBands
    };

    static constexpr float maxGainDb = 12.0f;

    void prepare (double sampleRate);
    void reset() noexcept;

    void setGainDb (Band band, float db) noexcept;
    float getGainDb (Band band) const noexcept;

    // Audio-thread only. Processes in place.
    void process (float* samples, int numSamples) noexcept;

    // Magnitude (linear) of the whole chain at `hz` for the coefficients
    // currently applied — for tests and a future EQ curve display.
    double getMagnitudeAt (double hz) const noexcept;

private:
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;

        float process (float x) noexcept
        {
            const double in = x;
            const double out = b0 * in + z1;
            z1 = b1 * in - a1 * out + z2;
            z2 = b2 * in - a2 * out;
            return (float) out;
        }
    };

    void updateCoefficients() noexcept;

    std::array<Biquad, numBands> filters;
    std::array<std::atomic<float>, numBands> targetGainDb {};
    std::array<float, numBands> appliedGainDb {};
    std::atomic<bool> dirty { true };
    double sampleRate = 48000.0;
};
