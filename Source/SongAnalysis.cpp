#include "SongAnalysis.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace roadie::song
{
namespace
{
constexpr double frameSeconds = 0.01;
constexpr double pi = 3.14159265358979323846;

struct Envelopes
{
    double frameRate = 100.0;
    std::vector<float> onset, bassOnset; // half-wave rectified log-energy rises
    std::vector<float> full, low, high;  // frame energies, for the sections
};

float onePoleCoefficient (double cutoff, double sampleRate)
{
    return (float) (1.0 - std::exp (-2.0 * pi * cutoff / sampleRate));
}

Envelopes computeEnvelopes (const float* x, int n, double sampleRate)
{
    Envelopes e;
    const int hop = std::max (1, (int) std::lround (sampleRate * frameSeconds));
    e.frameRate = sampleRate / hop;
    const int frames = n / hop;
    if (frames <= 0)
        return e;

    const float aLow = onePoleCoefficient (150.0, sampleRate), aHigh = onePoleCoefficient (2000.0, sampleRate);
    float lowState = 0.0f, highState = 0.0f;
    e.full.resize ((size_t) frames);
    e.low.resize ((size_t) frames);
    e.high.resize ((size_t) frames);

    for (int f = 0; f < frames; ++f)
    {
        double full = 0.0, low = 0.0, high = 0.0;
        for (int i = f * hop; i < (f + 1) * hop; ++i)
        {
            lowState += aLow * (x[i] - lowState);
            highState += aHigh * (x[i] - highState);
            const float h = x[i] - highState;
            full += (double) x[i] * x[i];
            low += (double) lowState * lowState;
            high += (double) h * h;
        }
        e.full[(size_t) f] = (float) (full / hop);
        e.low[(size_t) f] = (float) (low / hop);
        e.high[(size_t) f] = (float) (high / hop);
    }

    auto rise = [] (const std::vector<float>& energy, size_t f)
    {
        constexpr float floor = 1.0e-9f;
        return f == 0 ? 0.0f : std::max (0.0f, std::log (energy[f] + floor) - std::log (energy[f - 1] + floor));
    };
    e.onset.resize ((size_t) frames);
    e.bassOnset.resize ((size_t) frames);
    for (size_t f = 0; f < (size_t) frames; ++f)
    {
        // Quiet frames rise a lot in log terms for nothing: weigh each band's
        // rise by how loud the band is.
        // The bass onset is linear (amplitude), not logarithmic: what tells the
        // one from the three is that the kick there is louder.
        e.bassOnset[f] = f == 0 ? 0.0f : std::max (0.0f, std::sqrt (e.low[f]) - std::sqrt (e.low[f - 1]));
        e.onset[f] = rise (e.full, f) + rise (e.low, f) * 0.7f + rise (e.high, f) * 0.7f;
    }

    // Normalise against loud passages only.
    const auto loud = *std::max_element (e.full.begin(), e.full.end());
    for (size_t f = 0; f < (size_t) frames; ++f)
        if (e.full[f] < loud * 1.0e-5f)
            e.onset[f] = e.bassOnset[f] = 0.0f;
    return e;
}

float at (const std::vector<float>& v, double index)
{
    const auto i = (size_t) index;
    if (index < 0.0 || i + 1 >= v.size())
        return 0.0f;
    const float frac = (float) (index - (double) i);
    return v[i] * (1.0f - frac) + v[i + 1] * frac;
}

// Sum of the envelope on a beat grid, and the best phase for it.
double gridScore (const std::vector<float>& env, double period, double& bestPhase)
{
    double best = -1.0;
    for (double phase = 0.0; phase < period; phase += 0.5)
    {
        double sum = 0.0;
        for (double t = phase; t < (double) env.size(); t += period)
            sum += std::max ({ at (env, t - 1.0) * 0.5f, at (env, t), at (env, t + 1.0) * 0.5f });
        if (sum > best)
        {
            best = sum;
            bestPhase = phase;
        }
    }
    return best;
}

double estimateTempo (const std::vector<float>& env, double frameRate, double& confidence)
{
    const double mean = std::accumulate (env.begin(), env.end(), 0.0) / std::max<size_t> (1, env.size());
    std::vector<float> centred (env.size());
    for (size_t i = 0; i < env.size(); ++i)
        centred[i] = env[i] - (float) mean;

    const int minLag = (int) std::floor (60.0 * frameRate / 200.0), maxLag = (int) std::ceil (60.0 * frameRate / 55.0);
    std::vector<double> ac ((size_t) maxLag * 2 + 2, 0.0);
    for (int lag = minLag; lag <= std::min ((int) ac.size() - 1, (int) env.size() - 1); ++lag)
    {
        double sum = 0.0;
        for (size_t i = (size_t) lag; i < centred.size(); ++i)
            sum += (double) centred[i] * centred[i - (size_t) lag];
        ac[(size_t) lag] = sum / (double) (centred.size() - (size_t) lag);
    }

    // Prefer tempi around 110 BPM (a broad prior: one octave either side).
    double best = -1.0e30, sumScores = 0.0;
    int bestLag = minLag, counted = 0;
    for (int lag = minLag; lag <= maxLag && lag < (int) ac.size(); ++lag)
    {
        const double bpm = 60.0 * frameRate / lag;
        const double octave = std::log2 (bpm / 110.0);
        const double prior = std::exp (-0.5 * octave * octave / (0.9 * 0.9));
        const double harmonic = 2 * lag < (int) ac.size() ? 0.5 * ac[(size_t) (2 * lag)] : 0.0;
        const double score = prior * (ac[(size_t) lag] + harmonic);
        sumScores += std::max (0.0, score);
        ++counted;
        if (score > best)
        {
            best = score;
            bestLag = lag;
        }
    }
    confidence = counted > 0 && sumScores > 0.0 ? std::min (1.0, best / (sumScores / counted) / 8.0) : 0.0;

    // Parabolic interpolation around the peak.
    double lag = bestLag;
    if (bestLag > minLag && bestLag + 1 < (int) ac.size())
    {
        const double a = ac[(size_t) bestLag - 1], b = ac[(size_t) bestLag], c = ac[(size_t) bestLag + 1];
        const double denominator = a - 2.0 * b + c;
        if (std::abs (denominator) > 1.0e-12)
            lag += 0.5 * (a - c) / denominator;
    }
    return 60.0 * frameRate / lag;
}

// Fits the beat grid over the whole song: the tempo to 0.01 BPM, and its phase.
double refineTempo (const std::vector<float>& env, double frameRate, double bpm, double& phase)
{
    double bestScore = -1.0, bestBpm = bpm;
    for (double candidate = bpm * 0.98; candidate <= bpm * 1.02; candidate += 0.02)
    {
        double p = 0.0;
        const double score = gridScore (env, 60.0 * frameRate / candidate, p);
        if (score > bestScore)
        {
            bestScore = score;
            bestBpm = candidate;
            phase = p;
        }
    }
    return bestBpm;
}
} // namespace

std::vector<Section> findSections (const float* mono, int numSamples, double sampleRate,
                                   double bpm, double firstDownbeatSeconds, int beatsPerBar)
{
    std::vector<Section> sections;
    if (mono == nullptr || numSamples <= 0 || sampleRate <= 0.0 || bpm <= 0.0 || beatsPerBar < 1)
        return sections;

    const auto e = computeEnvelopes (mono, numSamples, sampleRate);
    if (e.full.empty())
        return sections;
    const double barSeconds = 60.0 / bpm * beatsPerBar;
    const double songSeconds = numSamples / sampleRate;

    // One feature vector per bar: loudness, and the bass / treble balance.
    struct Bar { double loud, bass, bright; };
    std::vector<Bar> bars;
    for (double t = firstDownbeatSeconds; t + barSeconds <= songSeconds + 0.01; t += barSeconds)
    {
        const auto f0 = (size_t) std::max (0.0, t * e.frameRate), f1 = std::min (e.full.size(), (size_t) std::max (0.0, (t + barSeconds) * e.frameRate));
        double full = 1.0e-12, low = 1.0e-12, high = 1.0e-12;
        for (size_t f = f0; f < f1; ++f)
        {
            full += e.full[f];
            low += e.low[f];
            high += e.high[f];
        }
        bars.push_back ({ 10.0 * std::log10 (full / std::max<size_t> (1, f1 - f0)), 10.0 * std::log10 (low / full), 10.0 * std::log10 (high / full) });
    }

    const int count = (int) bars.size();
    auto boundaryAt = [&] (int bar) { return bar == 0 ? 0.0 : firstDownbeatSeconds + bar * barSeconds; };
    if (count < 8)
    {
        sections.push_back ({ 0.0, songSeconds, "A" });
        return sections;
    }

    // Novelty: how different the four bars after each bar line are from the
    // four before it.
    constexpr int window = 4;
    std::vector<double> novelty ((size_t) count, 0.0);
    for (int b = window; b <= count - window; ++b)
    {
        Bar before {}, after {};
        for (int k = 0; k < window; ++k)
        {
            before.loud += bars[(size_t) (b - 1 - k)].loud / window;
            before.bass += bars[(size_t) (b - 1 - k)].bass / window;
            before.bright += bars[(size_t) (b - 1 - k)].bright / window;
            after.loud += bars[(size_t) (b + k)].loud / window;
            after.bass += bars[(size_t) (b + k)].bass / window;
            after.bright += bars[(size_t) (b + k)].bright / window;
        }
        novelty[(size_t) b] = std::hypot (after.loud - before.loud, 0.7 * (after.bass - before.bass), 0.7 * (after.bright - before.bright));
    }

    const double mean = std::accumulate (novelty.begin(), novelty.end(), 0.0) / count;
    double variance = 0.0;
    for (auto v : novelty)
        variance += (v - mean) * (v - mean);
    const double threshold = std::max (2.0, mean + std::sqrt (variance / count)); // at least a 2 dB change

    // Peaks, at least four bars apart, preferring bar lines on a multiple of four.
    std::vector<int> boundaries { 0 };
    for (int b = window; b <= count - window; ++b)
    {
        const double v = novelty[(size_t) b] * (b % 4 == 0 ? 1.15 : 1.0);
        bool isPeak = novelty[(size_t) b] >= threshold;
        for (int k = std::max (0, b - 2); k <= std::min (count - 1, b + 2) && isPeak; ++k)
            if (k != b && novelty[(size_t) k] * (k % 4 == 0 ? 1.15 : 1.0) > v)
                isPeak = false;
        if (isPeak && b - boundaries.back() >= 4)
            boundaries.push_back (b);
    }
    boundaries.push_back (count);

    // Label the parts: alike parts share a letter; a quiet first or last part
    // is the intro / outro.
    struct Part { int from, to; double loud, bass, bright; std::string label; };
    std::vector<Part> parts;
    for (size_t i = 0; i + 1 < boundaries.size(); ++i)
    {
        Part p { boundaries[i], boundaries[i + 1], 0.0, 0.0, 0.0, {} };
        for (int b = p.from; b < p.to; ++b)
        {
            p.loud += bars[(size_t) b].loud / (p.to - p.from);
            p.bass += bars[(size_t) b].bass / (p.to - p.from);
            p.bright += bars[(size_t) b].bright / (p.to - p.from);
        }
        parts.push_back (p);
    }

    std::vector<double> louds;
    for (const auto& p : parts)
        louds.push_back (p.loud);
    std::sort (louds.begin(), louds.end());
    const double medianLoud = louds[louds.size() / 2];

    std::vector<const Part*> lettered;
    char nextLetter = 'A';
    for (size_t i = 0; i < parts.size(); ++i)
    {
        auto& p = parts[i];
        const bool quiet = p.loud < medianLoud - 4.0;
        if (parts.size() >= 2 && i == 0 && quiet)
            p.label = "Intro";
        else if (parts.size() > 2 && i == parts.size() - 1 && quiet)
            p.label = "Outro";
        else
        {
            for (const auto* other : lettered)
                if (std::hypot (p.loud - other->loud, 0.7 * (p.bass - other->bass), 0.7 * (p.bright - other->bright)) < 2.5)
                {
                    p.label = other->label;
                    break;
                }
            if (p.label.empty())
            {
                p.label = std::string (1, nextLetter);
                nextLetter = nextLetter < 'Z' ? (char) (nextLetter + 1) : nextLetter;
                lettered.push_back (&p);
            }
        }
        sections.push_back ({ boundaryAt (p.from), p.to == count ? songSeconds : boundaryAt (p.to), p.label });
    }
    return sections;
}

Analysis analyse (const float* mono, int numSamples, double sampleRate, int beatsPerBar)
{
    Analysis result;
    if (mono == nullptr || sampleRate <= 0.0 || beatsPerBar < 1 || numSamples < (int) (sampleRate * 4.0))
        return result;

    const auto e = computeEnvelopes (mono, numSamples, sampleRate);
    double confidence = 0.0, phase = 0.0;
    const double coarse = estimateTempo (e.onset, e.frameRate, confidence);
    const double bpm = refineTempo (e.onset, e.frameRate, coarse, phase);
    const double period = 60.0 * e.frameRate / bpm;

    // Where the music starts: the first frame above -40 dB of the loudest.
    const float loudest = *std::max_element (e.full.begin(), e.full.end());
    size_t start = 0;
    while (start < e.full.size() && e.full[start] < loudest * 1.0e-4f)
        ++start;

    // First beat of the grid at (or just before) that point…
    double firstBeat = phase;
    while (firstBeat + period * 0.25 < (double) start)
        firstBeat += period;

    // …and which of the bar's beats is the one: the one with the most bass.
    int downbeat = 0;
    double bestBass = -1.0;
    for (int offset = 0; offset < beatsPerBar; ++offset)
    {
        double sum = 0.0;
        for (double t = firstBeat + offset * period; t < (double) e.bassOnset.size(); t += period * beatsPerBar)
            sum += std::max ({ at (e.bassOnset, t - 1.0), at (e.bassOnset, t), at (e.bassOnset, t + 1.0) });
        if (sum > bestBass)
        {
            bestBass = sum;
            downbeat = offset;
        }
    }

    result.ok = true;
    result.bpm = std::round (bpm * 100.0) / 100.0;
    result.firstDownbeatSeconds = (firstBeat + downbeat * period) / e.frameRate;
    result.confidence = confidence;
    result.sections = findSections (mono, numSamples, sampleRate, result.bpm, result.firstDownbeatSeconds, beatsPerBar);
    return result;
}

double tempoFromTaps (const std::vector<double>& tapSeconds)
{
    if (tapSeconds.size() < 4)
        return 0.0;
    // The median interval of the last eight taps: one late tap doesn't count.
    std::vector<double> intervals;
    for (size_t i = tapSeconds.size() > 9 ? tapSeconds.size() - 9 : 1; i < tapSeconds.size(); ++i)
        intervals.push_back (tapSeconds[i] - tapSeconds[i - 1]);
    std::sort (intervals.begin(), intervals.end());
    const double median = intervals[intervals.size() / 2];
    return median > 0.0 ? std::round (600.0 / median) / 10.0 : 0.0;
}
} // namespace roadie::song
