// Fallbacks for platforms without an AVFoundation backend (Linux, Windows
// for now): the app still builds and runs, with the camera and video export
// reported as unavailable. A Windows Media Foundation backend would slot in
// next to CameraCapture_mac.mm / ClipExporter_mac.mm.

#include "CameraCapture.h"
#include "I18n.h"
#include "ClipExporter.h"
#include "VideoPreview.h"

#include <juce_events/juce_events.h>

struct CameraCapture::Impl {};

CameraCapture::CameraCapture() = default;
CameraCapture::~CameraCapture() = default;

bool CameraCapture::isSupported() { return false; }
juce::StringArray CameraCapture::getDeviceNames() { return {}; }

void CameraCapture::open (const juce::String&, std::function<void (bool, juce::String)> onDone)
{
    juce::MessageManager::callAsync ([onDone]
    {
        onDone (false, tr ("Camera not available on this system (macOS only for now)."));
    });
}

void CameraCapture::close() {}
bool CameraCapture::isOpen() const { return false; }
juce::String CameraCapture::getDeviceName() const { return {}; }
int CameraCapture::getFrameWidth() const { return 0; }
int CameraCapture::getFrameHeight() const { return 0; }
int CameraCapture::getFrameCounter() const { return 0; }
juce::Image CameraCapture::getLatestFrame() const { return {}; }
bool CameraCapture::startRecording (const juce::File&) { return false; }

void CameraCapture::stopRecording (std::function<void (bool, double)> onFinished)
{
    juce::MessageManager::callAsync ([onFinished] { onFinished (false, 0.0); });
}

bool CameraCapture::isRecording() const { return false; }
double CameraCapture::getRecordedSeconds() const { return 0.0; }
std::vector<juce::Image> CameraCapture::getRecordingThumbnails() const { return {}; }

void CameraCapture::startMicCapture (double, std::function<void (bool, juce::String)> onStarted)
{
    juce::MessageManager::callAsync ([onStarted] { onStarted (false, tr ("Camera not available.")); });
}

bool CameraCapture::isMicCaptureComplete() const { return false; }

std::vector<float> CameraCapture::getMicCapture (double& firstSampleHostSeconds, double& sampleRate) const
{
    firstSampleHostSeconds = 0.0;
    sampleRate = 0.0;
    return {};
}

void CameraCapture::stopMicCapture() {}

//==============================================================================
struct ClipExporter::Impl {};

ClipExporter::ClipExporter() = default;
ClipExporter::~ClipExporter() = default;

bool ClipExporter::isVideoExportSupported() { return false; }

void ClipExporter::start (const ClipExportSettings&, std::function<void (bool, juce::String)> onDone)
{
    juce::MessageManager::callAsync ([onDone]
    {
        onDone (false, tr ("Video export is not available on this system."));
    });
}

void ClipExporter::cancel() {}
bool ClipExporter::isRunning() const { return false; }
float ClipExporter::getProgress() const { return 0.0f; }

//==============================================================================
struct VideoPreview::Impl {};

VideoPreview::VideoPreview() = default;
VideoPreview::~VideoPreview() = default;
void VideoPreview::request (const juce::File&, double) {}
void VideoPreview::clear() {}
juce::Image VideoPreview::getFrame() const { return {}; }
int VideoPreview::getFrameCounter() const { return 0; }
