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
} // namespace roadie::sync
