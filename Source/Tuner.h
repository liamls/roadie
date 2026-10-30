#pragma once

#include <string>

// Guitar tuner: the pitch of the dry input (YIN), the nearest note and how
// far from it, in cents. Volontairement sans dépendance JUCE (testable seul,
// voir Tests/DspTests.cpp).
namespace roadie::tuner
{
struct Reading
{
    bool ok = false;      // a clear, steady pitch was heard
    double hz = 0.0;
    std::string note;     // "E", "A", "D", "G", "B", "C#"…
    int octave = 0;       // scientific: low E is E2
    double cents = 0.0;   // −50 … +50 from that note
    double clarity = 0.0; // 0 … 1: how periodic the sound is (1 − YIN's dip)
};

// `samples` should hold at least 80 ms (six periods of a low E); 4096
// samples at 48 kHz is plenty. Pitches from 60 Hz to 1.2 kHz.
Reading detect (const float* samples, int numSamples, double sampleRate, double referenceA4 = 440.0);
} // namespace roadie::tuner
