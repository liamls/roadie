#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <vector>
#include <memory>

// Builds the final clip: the arrangement's mix (rendered to a WAV by the
// caller) laid against the video of its regions — each segment already
// shifted by its take's measured sync offset — cropped to 9:16 or 16:9 and
// encoded to H.264/AAC MP4. Stretches without video are black.
//
// macOS: AVFoundation composition + AVAssetExportSession (hardware encoder,
// no FFmpeg to ship). Elsewhere: isVideoExportSupported() is false and only
// the WAV export path (handled by the caller) is available.
struct ClipExportSettings
{
    // The clip's soundtrack; the clip lasts as long as it does.
    juce::File audioFile;

    // A piece of a camera recording placed on the clip's timeline:
    // `sourceStartSeconds` in the movie's own time (negative: the movie
    // starts later, the head of the segment stays black).
    struct VideoSegment
    {
        juce::File file;
        double sourceStartSeconds = 0.0;
        double atSeconds = 0.0;
        double durationSeconds = 0.0;
    };
    std::vector<VideoSegment> segments;

    bool vertical = true;
    bool mirror = false;
    juce::String title;
    bool burnInTitle = false;
    juce::File outputFile;
};

class ClipExporter
{
public:
    ClipExporter();
    ~ClipExporter();

    static bool isVideoExportSupported();

    // Callback on the message thread; `message` is user-facing French text.
    void start (const ClipExportSettings& settings, std::function<void (bool ok, juce::String message)> onDone);
    void cancel();

    bool isRunning() const;
    float getProgress() const;

    struct Impl;

private:
    std::shared_ptr<Impl> impl;
};
