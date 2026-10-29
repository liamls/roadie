#pragma once

#include <algorithm>

// The audio CPU load as a word for the top bar (Faible / Normal / Élevé):
// a moving average over about two seconds, then two thresholds (35 % and
// 70 %), each with a small dead band so the word doesn't flicker when the
// load sits on a boundary. JUCE-free, tested in DspTests.
class CpuLoad
{
public:
    enum class Level { low, normal, high };

    // `load` in [0, 1], `elapsedSeconds` since the previous reading.
    void push (double load, double elapsedSeconds)
    {
        if (average < 0.0)
            average = load;
        else
            average += (load - average) * std::clamp (elapsedSeconds / averagingSeconds, 0.0, 1.0);
        level = classify (average, level);
    }

    double getAverage() const noexcept { return std::max (0.0, average); }
    Level getLevel() const noexcept { return level; }

    static Level classify (double average, Level previous)
    {
        const double up = deadBand, down = -deadBand;
        switch (previous)
        {
            case Level::low:
                return average > highThreshold + up ? Level::high : (average > lowThreshold + up ? Level::normal : Level::low);
            case Level::high:
                return average < lowThreshold + down ? Level::low : (average < highThreshold + down ? Level::normal : Level::high);
            case Level::normal:
            default:
                return average > highThreshold + up ? Level::high : (average < lowThreshold + down ? Level::low : Level::normal);
        }
    }

    static constexpr double averagingSeconds = 2.0;
    static constexpr double lowThreshold = 0.35, highThreshold = 0.70, deadBand = 0.03;

private:
    double average = -1.0;
    Level level = Level::low;
};
