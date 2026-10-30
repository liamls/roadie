#include "TimeStretch.h"

#include <algorithm>
#include <cmath>

namespace roadie::stretch
{
namespace
{
constexpr double pi = 3.14159265358979323846;

// WSOLA on two channels at once: lengthens by `factor` (> 1 slower), pitch
// unchanged.
void wsola (const std::vector<float>& left, const std::vector<float>& right, double sampleRate, double factor,
            std::vector<float>& outLeft, std::vector<float>& outRight)
{
    const int window = std::max (256, (int) (0.046 * sampleRate)) & ~1;
    const int hop = window / 2;                                   // synthesis hop
    const int tolerance = std::max (16, (int) (0.006 * sampleRate)); // search ±
    const double analysisHop = hop / factor;
    const auto inLength = (std::int64_t) left.size();
    const auto outLength = (std::int64_t) std::ceil ((double) inLength * factor);

    outLeft.assign ((size_t) outLength + (size_t) window, 0.0f);
    outRight.assign (outLeft.size(), 0.0f);

    std::vector<float> hann ((size_t) window);
    for (int i = 0; i < window; ++i)
        hann[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * pi * i / window));

    // Mono, decimated by 4, for the search: fast enough for a whole song.
    constexpr int decimation = 4;
    std::vector<float> guide ((size_t) (inLength / decimation + 1));
    for (std::int64_t i = 0; i + decimation <= inLength; i += decimation)
    {
        float sum = 0.0f;
        for (int k = 0; k < decimation; ++k)
            sum += left[(size_t) (i + k)] + right[(size_t) (i + k)];
        guide[(size_t) (i / decimation)] = sum;
    }
    auto guideAt = [&] (std::int64_t sample) { return sample >= 0 && sample / decimation < (std::int64_t) guide.size() ? guide[(size_t) (sample / decimation)] : 0.0f; };

    std::int64_t previous = 0; // where the last window was read from
    for (std::int64_t out = 0, frame = 0; out < outLength; out += hop, ++frame)
    {
        const auto nominal = (std::int64_t) std::llround (frame * analysisHop);
        std::int64_t best = nominal;

        if (frame > 0)
        {
            // The natural continuation of the previous window, a hop later.
            const auto natural = previous + hop;
            double bestScore = -1.0e30;
            for (int delta = -tolerance; delta <= tolerance; delta += decimation)
            {
                const auto candidate = nominal + delta;
                if (candidate < 0 || candidate + window > inLength)
                    continue;
                double score = 0.0;
                for (int i = 0; i < hop; i += decimation)
                    score += (double) guideAt (candidate + i) * guideAt (natural + i);
                if (score > bestScore)
                {
                    bestScore = score;
                    best = candidate;
                }
            }
        }
        best = std::clamp<std::int64_t> (best, 0, std::max<std::int64_t> (0, inLength - window));
        previous = best;

        for (int i = 0; i < window && out + i < (std::int64_t) outLeft.size(); ++i)
        {
            const auto source = best + i;
            if (source >= inLength)
                break;
            outLeft[(size_t) (out + i)] += left[(size_t) source] * hann[(size_t) i];
            outRight[(size_t) (out + i)] += right[(size_t) source] * hann[(size_t) i];
        }
    }

    outLeft.resize ((size_t) outLength);
    outRight.resize ((size_t) outLength);
}

// Cubic (Catmull-Rom) resampling: plays `input` `ratio` times faster.
std::vector<float> resample (const std::vector<float>& input, double ratio)
{
    const auto length = (size_t) std::floor ((double) input.size() / ratio);
    std::vector<float> output (length);
    auto at = [&] (std::int64_t i) { return i < 0 || i >= (std::int64_t) input.size() ? 0.0f : input[(size_t) i]; };
    for (size_t n = 0; n < length; ++n)
    {
        const double position = (double) n * ratio;
        const auto i = (std::int64_t) position;
        const float t = (float) (position - (double) i);
        const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
        output[n] = y1 + 0.5f * t * (y2 - y0 + t * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3 + t * (3.0f * (y1 - y2) + y3 - y0)));
    }
    return output;
}
} // namespace

void process (const std::vector<float>& left, const std::vector<float>& right, double sampleRate,
              double speed, int semitones, std::vector<float>& outLeft, std::vector<float>& outRight)
{
    if (sampleRate <= 0.0 || left.size() != right.size())
    {
        outLeft = left;
        outRight = right;
        return;
    }
    speed = std::clamp (speed, 0.25, 1.0);
    const double pitch = std::pow (2.0, std::clamp (semitones, -12, 12) / 12.0);
    if (std::abs (speed - 1.0) < 1.0e-6 && semitones == 0)
    {
        outLeft = left;
        outRight = right;
        return;
    }

    // Stretch by pitch / speed, then play `pitch` times faster: the length
    // ends up divided by `speed`, the pitch multiplied by `pitch`.
    std::vector<float> stretchedLeft, stretchedRight;
    wsola (left, right, sampleRate, pitch / speed, stretchedLeft, stretchedRight);
    if (semitones == 0)
    {
        outLeft = std::move (stretchedLeft);
        outRight = std::move (stretchedRight);
        return;
    }
    outLeft = resample (stretchedLeft, pitch);
    outRight = resample (stretchedRight, pitch);
}
} // namespace roadie::stretch
