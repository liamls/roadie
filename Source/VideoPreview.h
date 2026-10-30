#pragma once

#include <juce_graphics/juce_graphics.h>

#include <memory>

// Decodes single frames of a take's movie for the monitor while the
// arrangement plays back or is scrubbed. Requests are coalesced: while one
// frame is decoding, only the latest wanted time is kept, so the preview
// never lags behind the playhead — it drops frames instead.
//
// macOS: AVAssetImageGenerator on a background queue (VideoPreview_mac.mm).
class VideoPreview
{
public:
    VideoPreview();
    ~VideoPreview();

    // Message thread. `seconds` is a time in the movie's own timeline.
    void request (const juce::File& movie, double seconds);
    void clear();

    juce::Image getFrame() const;
    int getFrameCounter() const;

    struct Impl;

private:
    std::shared_ptr<Impl> impl;
};
