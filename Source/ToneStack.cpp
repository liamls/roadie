#include "ToneStack.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace
{
constexpr double pi = 3.14159265358979323846;

// Centre/corner frequencies and Q per band, voiced like a classic amp
// tone stack rather than a surgical EQ.
struct BandShape
{
    enum Kind { lowShelf, peak, highShelf } kind;
    double frequency;
    double q;
};

constexpr BandShape shapes[] = {
    { BandShape::lowShelf, 120.0, 0.707 },   // Grave
    { BandShape::peak, 750.0, 0.8 },         // Mid
    { BandShape::highShelf, 2500.0, 0.707 }, // Aigu
    { BandShape::peak, 4800.0, 0.7 },        // Presence
};
} // namespace

void ToneStack::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    dirty.store (true, std::memory_order_relaxed);
    updateCoefficients();
    reset();
}

void ToneStack::reset() noexcept
{
    for (auto& f : filters)
        f.z1 = f.z2 = 0.0;
}

void ToneStack::setGainDb (Band band, float db) noexcept
{
    targetGainDb[(size_t) band].store (std::clamp (db, -maxGainDb, maxGainDb), std::memory_order_relaxed);
    dirty.store (true, std::memory_order_release);
}

float ToneStack::getGainDb (Band band) const noexcept
{
    return targetGainDb[(size_t) band].load (std::memory_order_relaxed);
}

void ToneStack::updateCoefficients() noexcept
{
    for (size_t i = 0; i < (size_t) numBands; ++i)
    {
        const float gainDb = targetGainDb[i].load (std::memory_order_relaxed);
        appliedGainDb[i] = gainDb;

        const auto& shape = shapes[i];
        const double a = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * pi * std::min (shape.frequency, sampleRate * 0.45) / sampleRate;
        const double cosW = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * shape.q);

        double b0, b1, b2, a0, a1, a2;

        switch (shape.kind)
        {
            case BandShape::lowShelf:
            {
                const double sqrtA2alpha = 2.0 * std::sqrt (a) * alpha;
                b0 = a * ((a + 1.0) - (a - 1.0) * cosW + sqrtA2alpha);
                b1 = 2.0 * a * ((a - 1.0) - (a + 1.0) * cosW);
                b2 = a * ((a + 1.0) - (a - 1.0) * cosW - sqrtA2alpha);
                a0 = (a + 1.0) + (a - 1.0) * cosW + sqrtA2alpha;
                a1 = -2.0 * ((a - 1.0) + (a + 1.0) * cosW);
                a2 = (a + 1.0) + (a - 1.0) * cosW - sqrtA2alpha;
                break;
            }
            case BandShape::highShelf:
            {
                const double sqrtA2alpha = 2.0 * std::sqrt (a) * alpha;
                b0 = a * ((a + 1.0) + (a - 1.0) * cosW + sqrtA2alpha);
                b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cosW);
                b2 = a * ((a + 1.0) + (a - 1.0) * cosW - sqrtA2alpha);
                a0 = (a + 1.0) - (a - 1.0) * cosW + sqrtA2alpha;
                a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cosW);
                a2 = (a + 1.0) - (a - 1.0) * cosW - sqrtA2alpha;
                break;
            }
            case BandShape::peak:
            default:
                b0 = 1.0 + alpha * a;
                b1 = -2.0 * cosW;
                b2 = 1.0 - alpha * a;
                a0 = 1.0 + alpha / a;
                a1 = -2.0 * cosW;
                a2 = 1.0 - alpha / a;
                break;
        }

        auto& f = filters[i];
        f.b0 = b0 / a0;
        f.b1 = b1 / a0;
        f.b2 = b2 / a0;
        f.a1 = a1 / a0;
        f.a2 = a2 / a0;
    }
}

void ToneStack::process (float* samples, int numSamples) noexcept
{
    if (dirty.exchange (false, std::memory_order_acquire))
        updateCoefficients();

    // Skip the whole chain when every band is flat: the default state costs nothing.
    if (std::all_of (appliedGainDb.begin(), appliedGainDb.end(), [] (float g) { return std::abs (g) < 0.01f; }))
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        float x = samples[i];
        for (auto& f : filters)
            x = f.process (x);
        samples[i] = x;
    }
}

double ToneStack::getMagnitudeAt (double hz) const noexcept
{
    const double w = 2.0 * pi * hz / sampleRate;
    const std::complex<double> z1 = std::polar (1.0, -w);
    const std::complex<double> z2 = z1 * z1;

    double magnitude = 1.0;
    for (const auto& f : filters)
    {
        const auto num = f.b0 + f.b1 * z1 + f.b2 * z2;
        const auto den = 1.0 + f.a1 * z1 + f.a2 * z2;
        magnitude *= std::abs (num / den);
    }
    return magnitude;
}
