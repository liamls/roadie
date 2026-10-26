#pragma once

#include <juce_graphics/juce_graphics.h>

#include <functional>
#include <memory>
#include <vector>

// Webcam capture built directly on AVFoundation (CameraCapture_mac.mm)
// instead of juce::CameraDevice, which records video only, stamps the
// recording with the wall-clock time of the *request* rather than of the
// first frame, and has no Linux backend anyway.
//
// Every frame and every calibration-mic buffer carries its host-clock
// capture time (mach_absolute_time, in seconds) — the same clock CoreAudio
// stamps the guitar audio with — which is what makes the sync exact.
//
// On platforms without a backend (CameraCapture_stub.cpp), isSupported()
// returns false and every call fails gracefully.
class CameraCapture
{
public:
    CameraCapture();
    ~CameraCapture();

    static bool isSupported();
    static juce::StringArray getDeviceNames();

    // Asks for camera permission if needed, then starts the device. The
    // callback runs on the message thread; `error` is user-facing French text.
    void open (const juce::String& deviceName, std::function<void (bool ok, juce::String error)> onDone);
    void close();
    bool isOpen() const;
    juce::String getDeviceName() const;

    // Native capture size (0×0 until the first frame arrives).
    int getFrameWidth() const;
    int getFrameHeight() const;

    // Live preview, downscaled (≤ 480 px wide). The counter increments on
    // every new frame so the UI repaints only when something changed.
    int getFrameCounter() const;
    juce::Image getLatestFrame() const;

    // Records H.264 into a QuickTime file. Frames are written from the first
    // one that arrives after the call.
    bool startRecording (const juce::File& movFile);
    // `firstFrameHostSeconds` is the host-clock capture time of the first
    // frame in the file. Callback on the message thread.
    void stopRecording (std::function<void (bool ok, double firstFrameHostSeconds)> onFinished);
    bool isRecording() const;
    double getRecordedSeconds() const;
    // About one thumbnail per second of the current/last recording.
    std::vector<juce::Image> getRecordingThumbnails() const;

    // Calibration: records the camera-side microphone (the default system
    // input), whose timestamps AVFoundation keeps aligned with the frames.
    void startMicCapture (double seconds, std::function<void (bool ok, juce::String error)> onStarted);
    bool isMicCaptureComplete() const;
    std::vector<float> getMicCapture (double& firstSampleHostSeconds, double& sampleRate) const;
    void stopMicCapture();

    struct Impl;

private:
    std::shared_ptr<Impl> impl;
};
