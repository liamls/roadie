#pragma once

// Audio/video sync calibration helpers. Volontairement sans dépendance JUCE
// (testable seul, voir Tests/SyncTests.cpp).
//
// Calibration records the same percussive event (a clap, or a dead-muted
// strum) twice: through the audio interface, and through the camera's own
// microphone, whose timestamps AVFoundation keeps aligned with the video
// frames. Both captures carry the host-clock time of their first sample, so
// the difference between the two onsets is exactly the correction to apply
// to the interface audio when placing it against the video.
namespace roadie::sync
{
// Index of the first sample of the strongest transient in `samples`, or -1
// when nothing stands clearly above the background noise (no clap heard).
int findOnset (const float* samples, int numSamples, double sampleRate);

struct Capture
{
    const float* samples = nullptr;
    int numSamples = 0;
    double sampleRate = 48000.0;
    double firstSampleHostSeconds = 0.0;
};

struct Result
{
    bool ok = false;
    // How late the interface audio's timestamps are compared to the camera
    // clock: positive means the interface audio must be moved earlier.
    double interfaceLagSeconds = 0.0;
};

Result measureLag (const Capture& interfaceAudio, const Capture& cameraAudio);

// The same measure without a deliberate clap, from a stretch of ordinary
// playing: the loudness envelopes of both captures are cross-correlated
// (±400 ms), so every strum votes for the same lag. `ok` only when one lag
// clearly stands out; a take played with headphones, where the camera's
// microphone hears too little, simply fails and leaves the calibration alone.
Result measureLagFromPlaying (const Capture& interfaceAudio, const Capture& cameraAudio);
} // namespace roadie::sync
