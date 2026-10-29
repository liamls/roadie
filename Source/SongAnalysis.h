#pragma once

#include <string>
#include <vector>

// Backing-track analysis: tempo, where the first bar starts, and the song's
// parts. Volontairement sans dépendance JUCE (testable seul, voir
// Tests/DspTests.cpp).
//
// Tempo comes from the autocorrelation of an onset envelope (three bands,
// 10 ms frames), refined by fitting a fixed beat grid over the whole song;
// the first downbeat is the grid phase whose beats carry the most bass
// (kick drums land on the one). It assumes a steady tempo — a song played
// without a click drifts against the grid: the sound stays right, the grid
// and the snapping only approximate it.
namespace roadie::song
{
struct Section
{
    double startSeconds = 0.0, endSeconds = 0.0; // in the song file
    std::string label;                           // "Intro", "A", "B"… "Outro"
};

struct Analysis
{
    bool ok = false;
    double bpm = 0.0;
    double firstDownbeatSeconds = 0.0; // first beat of the first full bar, in the song file
    double confidence = 0.0;           // 0..1, how clearly the grid fits
    std::vector<Section> sections;
};

Analysis analyse (const float* mono, int numSamples, double sampleRate, int beatsPerBar = 4);

// Parts of the song on a given grid (after a ×2 / ÷2 or tapped tempo).
std::vector<Section> findSections (const float* mono, int numSamples, double sampleRate,
                                   double bpm, double firstDownbeatSeconds, int beatsPerBar);

// Tap tempo: the BPM from tap times (seconds), 0 until there are four.
double tempoFromTaps (const std::vector<double>& tapSeconds);
} // namespace roadie::song
