#pragma once

#include <vector>

// Slower or transposed backing tracks, for practice: WSOLA time-stretching
// (overlap-add of 46 ms windows, each slid by up to ±6 ms to the place that
// continues the previous one best), then resampling for the transposition.
// Rendered offline in the background, both channels with the same splices
// so the stereo image holds. Good on a full mix for practice; not mastering.
// Deliberately free of JUCE dependencies (testable seul, voir Tests/DspTests.cpp).
namespace roadie::stretch
{
// `speed` 0.25 … 1.0 (the app offers 0.5 … 1.0), `semitones` −12 … +12 (the
// app offers ±6). Both channels must have the same length, otherwise they are
// returned as they are. The length is the input's divided by `speed`.
void process (const std::vector<float>& left, const std::vector<float>& right, double sampleRate,
              double speed, int semitones, std::vector<float>& outLeft, std::vector<float>& outRight);
} // namespace roadie::stretch
