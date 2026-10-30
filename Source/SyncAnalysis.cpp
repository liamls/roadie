#include "SyncAnalysis.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace roadie::sync
{
int findOnset (const float* samples, int numSamples, double sampleRate)
{
    if (samples == nullptr || numSamples <= 0 || sampleRate <= 0.0)
        return -1;

    // 1 ms peak envelope: fine enough for sync, coarse enough to ignore single-sample spikes.
    const int window = std::max (1, (int) (sampleRate * 0.001));
    const int numWindows = numSamples / window;
    if (numWindows < 8)
        return -1;

    std::vector<float> envelope ((size_t) numWindows);
    for (int w = 0; w < numWindows; ++w)
    {
        float peak = 0.0f;
        for (int i = w * window; i < (w + 1) * window; ++i)
            peak = std::max (peak, std::abs (samples[i]));
        envelope[(size_t) w] = peak;
    }

    auto sorted = envelope;
    std::nth_element (sorted.begin(), sorted.begin() + numWindows / 2, sorted.end());
    const float noiseFloor = sorted[(size_t) numWindows / 2];

    const auto loudest = std::max_element (envelope.begin(), envelope.end());
    const float peak = *loudest;

    // Demand a clear event: ~18 dB above the median and not just quiet hiss.
    if (peak < 0.02f || peak < noiseFloor * 8.0f)
        return -1;

    // The onset is where the envelope first rises a third of the way from
    // the floor to the peak, searched backwards from the peak so an earlier
    // unrelated bump can't win.
    const float threshold = noiseFloor + (peak - noiseFloor) * 0.3f;
    int onsetWindow = (int) std::distance (envelope.begin(), loudest);
    while (onsetWindow > 0 && envelope[(size_t) onsetWindow - 1] >= threshold)
        --onsetWindow;

    // Refine to the first sample crossing the threshold inside that window.
    const int start = std::max (0, (onsetWindow - 1) * window);
    const int end = std::min (numSamples, (onsetWindow + 1) * window);
    for (int i = start; i < end; ++i)
        if (std::abs (samples[i]) >= threshold)
            return i;

    return onsetWindow * window;
}

Result measureLag (const Capture& interfaceAudio, const Capture& cameraAudio)
{
    Result result;

    const int a = findOnset (interfaceAudio.samples, interfaceAudio.numSamples, interfaceAudio.sampleRate);
    const int b = findOnset (cameraAudio.samples, cameraAudio.numSamples, cameraAudio.sampleRate);
    if (a < 0 || b < 0)
        return result;

    const double interfaceEventTime = interfaceAudio.firstSampleHostSeconds + a / interfaceAudio.sampleRate;
    const double cameraEventTime = cameraAudio.firstSampleHostSeconds + b / cameraAudio.sampleRate;

    result.interfaceLagSeconds = interfaceEventTime - cameraEventTime;

    // More than half a second apart means we matched two different events
    // (or the camera clock conversion failed): don't trust it.
    result.ok = std::abs (result.interfaceLagSeconds) < 0.5;
    return result;
}

namespace
{
constexpr double envelopeRate = 500.0; // 2 ms steps

// Positive jumps of the log loudness, on a grid of absolute host time
// (index 0 = `origin`): what a strum looks like, whatever the room does to it.
std::vector<float> onsetEnvelope (const Capture& c, double origin, int length)
{
    std::vector<float> out ((size_t) length, 0.0f);
    const int hop = std::max (1, (int) (c.sampleRate / envelopeRate));
    std::vector<float> loudness;
    for (int i = 0; i + hop <= c.numSamples; i += hop)
    {
        double sum = 0.0;
        for (int k = 0; k < hop; ++k)
            sum += (double) c.samples[i + k] * c.samples[i + k];
        loudness.push_back ((float) std::log (1.0e-6 + std::sqrt (sum / hop)));
    }
    const double first = c.firstSampleHostSeconds - origin;
    for (size_t i = 1; i < loudness.size(); ++i)
    {
        const int index = (int) std::lround ((first + (double) i * hop / c.sampleRate) * envelopeRate);
        if (index >= 0 && index < length)
            out[(size_t) index] = std::max (0.0f, loudness[i] - loudness[i - 1]);
    }
    // A little smoothing: the room blurs attacks by a few milliseconds.
    std::vector<float> smooth (out.size(), 0.0f);
    for (size_t i = 2; i + 2 < out.size(); ++i)
        smooth[i] = 0.1f * out[i - 2] + 0.2f * out[i - 1] + 0.4f * out[i] + 0.2f * out[i + 1] + 0.1f * out[i + 2];
    return smooth;
}
} // namespace

Result measureLagFromPlaying (const Capture& interfaceAudio, const Capture& cameraAudio)
{
    Result result;
    if (interfaceAudio.samples == nullptr || cameraAudio.samples == nullptr || interfaceAudio.numSamples < 1 || cameraAudio.numSamples < 1
        || interfaceAudio.firstSampleHostSeconds <= 0.0 || cameraAudio.firstSampleHostSeconds <= 0.0
        || interfaceAudio.sampleRate <= 0.0 || cameraAudio.sampleRate <= 0.0)
        return result;

    const double origin = std::min (interfaceAudio.firstSampleHostSeconds, cameraAudio.firstSampleHostSeconds);
    const double end = std::max (interfaceAudio.firstSampleHostSeconds + interfaceAudio.numSamples / interfaceAudio.sampleRate,
                                 cameraAudio.firstSampleHostSeconds + cameraAudio.numSamples / cameraAudio.sampleRate);
    const int length = (int) ((end - origin) * envelopeRate) + 1;
    if (length < (int) (2.0 * envelopeRate) || length > (int) (30.0 * envelopeRate))
        return result;

    const auto a = onsetEnvelope (interfaceAudio, origin, length);
    const auto b = onsetEnvelope (cameraAudio, origin, length);

    double energyA = 0.0, energyB = 0.0;
    for (int i = 0; i < length; ++i)
    {
        energyA += (double) a[(size_t) i] * a[(size_t) i];
        energyB += (double) b[(size_t) i] * b[(size_t) i];
    }
    if (energyA < 1.0e-6 || energyB < 1.0e-6)
        return result;
    const double norm = std::sqrt (energyA * energyB);

    const int maxLag = (int) (0.4 * envelopeRate);
    std::vector<double> correlation ((size_t) (2 * maxLag + 1), 0.0);
    for (int lag = -maxLag; lag <= maxLag; ++lag)
    {
        double sum = 0.0;
        for (int t = std::max (0, lag); t < std::min (length, length + lag); ++t)
            sum += (double) a[(size_t) t] * b[(size_t) (t - lag)];
        correlation[(size_t) (lag + maxLag)] = sum / norm;
    }

    const auto peak = std::max_element (correlation.begin(), correlation.end());
    double mean = 0.0;
    for (auto v : correlation)
        mean += v;
    mean /= (double) correlation.size();

    // One lag must stand clearly out of the others, and match for real.
    if (*peak < 0.12 || *peak < mean * 3.0)
        return result;

    // Parabolic refinement between the 2 ms steps.
    const size_t index = (size_t) std::distance (correlation.begin(), peak);
    double lag = (double) ((int) index - maxLag);
    if (index > 0 && index + 1 < correlation.size())
    {
        const double y0 = correlation[index - 1], y1 = correlation[index], y2 = correlation[index + 1];
        const double d = y0 - 2.0 * y1 + y2;
        if (std::abs (d) > 1.0e-12)
            lag += 0.5 * (y0 - y2) / d;
    }

    result.interfaceLagSeconds = lag / envelopeRate;
    result.ok = true;
    return result;
}
} // namespace roadie::sync
