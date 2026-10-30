#include "Tuner.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace roadie::tuner
{
Reading detect (const float* samples, int numSamples, double sampleRate, double referenceA4)
{
    Reading reading;
    if (samples == nullptr || numSamples < 256 || sampleRate <= 0.0)
        return reading;

    // Remove the DC offset (some interfaces have one, and it swamps a low E),
    // then see whether there is anything to tune: below about −55 dBFS, no.
    double mean = 0.0;
    for (int i = 0; i < numSamples; ++i)
        mean += samples[i];
    mean /= numSamples;
    double energy = 0.0;
    for (int i = 0; i < numSamples; ++i)
        energy += (samples[i] - mean) * (samples[i] - mean);
    if (std::sqrt (energy / numSamples) < 0.0018)
        return reading;

    // Low-pass at 1.5 kHz (a pick's click and the string's upper partials
    // only confuse the period), then decimate to ~24 kHz: four times less work.
    const int step = std::max (1, (int) (sampleRate / 24000.0));
    std::vector<float> x;
    x.reserve ((size_t) (numSamples / step));
    {
        const double w = 2.0 * 3.14159265358979 * 1500.0 / sampleRate;
        const double k = std::tan (w * 0.5), norm = 1.0 / (1.0 + std::sqrt (2.0) * k + k * k);
        const double b0 = k * k * norm, b1 = 2.0 * b0, b2 = b0;
        const double a1 = 2.0 * (k * k - 1.0) * norm, a2 = (1.0 - std::sqrt (2.0) * k + k * k) * norm;
        double z1 = 0.0, z2 = 0.0;
        for (int i = 0; i < numSamples; ++i)
        {
            const double in = samples[i] - mean;
            const double out = b0 * in + z1;
            z1 = b1 * in - a1 * out + z2;
            z2 = b2 * in - a2 * out;
            // The first 8 ms are the filter settling: not used.
            if (i % step == step - 1 && i > (int) (0.008 * sampleRate))
                x.push_back ((float) out);
        }
    }
    const double rate = sampleRate / step;

    const int tauMin = (int) (rate / 1200.0), tauMax = std::min ((int) (rate / 60.0), (int) x.size() / 2);
    const int window = (int) x.size() - tauMax;
    if (tauMax <= tauMin + 2 || window < 32)
        return reading;

    // YIN: the difference function, cumulative-mean normalised, then the
    // first dip under the threshold.
    std::vector<double> d ((size_t) tauMax + 1, 0.0);
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        double sum = 0.0;
        for (int i = 0; i < window; ++i)
        {
            const double diff = (double) x[(size_t) i] - x[(size_t) (i + tau)];
            sum += diff * diff;
        }
        d[(size_t) tau] = sum;
    }
    std::vector<double> cmnd ((size_t) tauMax + 1, 1.0);
    double running = 0.0;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        running += d[(size_t) tau];
        cmnd[(size_t) tau] = running > 0.0 ? d[(size_t) tau] * tau / running : 1.0;
    }

    int best = -1;
    for (int tau = std::max (2, tauMin); tau < tauMax; ++tau)
    {
        if (cmnd[(size_t) tau] < 0.15)
        {
            while (tau + 1 < tauMax && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau])
                ++tau;
            best = tau;
            break;
        }
    }
    // No clean dip (a worn string, a weak fundamental): take the deepest one
    // if it is still clearly periodic, rather than showing nothing.
    if (best < 0)
    {
        double deepest = 0.3;
        for (int tau = std::max (2, tauMin); tau < tauMax - 1; ++tau)
            if (cmnd[(size_t) tau] < deepest && cmnd[(size_t) tau] <= cmnd[(size_t) tau - 1] && cmnd[(size_t) tau] <= cmnd[(size_t) tau + 1])
            {
                deepest = cmnd[(size_t) tau];
                best = tau;
            }
    }
    if (best < 0 || best + 1 > tauMax)
        return reading;
    reading.clarity = std::max (0.0, 1.0 - cmnd[(size_t) best]);

    // Parabolic interpolation around the dip.
    double tau = best;
    {
        const double a = cmnd[(size_t) best - 1], b = cmnd[(size_t) best], c = cmnd[(size_t) best + 1];
        const double denominator = a - 2.0 * b + c;
        if (std::abs (denominator) > 1.0e-12)
            tau += 0.5 * (a - c) / denominator;
    }

    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    reading.hz = rate / tau;
    const double midi = 69.0 + 12.0 * std::log2 (reading.hz / referenceA4);
    const int nearest = (int) std::lround (midi);
    reading.cents = (midi - nearest) * 100.0;
    reading.note = names[((nearest % 12) + 12) % 12];
    reading.octave = nearest / 12 - 1;
    reading.ok = true;
    return reading;
}
} // namespace roadie::tuner
