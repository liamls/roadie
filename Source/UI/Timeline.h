#pragma once

#include "../Session.h"
#include "Widgets.h"

// Centre of the window (.timeline): bar ruler, the three tracks (Guitare —
// DI + amp processing + its take regions, Looper, Caméra), the playhead and
// the floating camera monitor. The window shows the loop twice (first pass +
// repeat) or eight bars of tempo grid when there is no loop yet.
class Timeline : public juce::Component
{
public:
    explicit Timeline (Session& session);
    ~Timeline() override;

    using Track = Session::Track;

    std::function<void (Track)> onTrackSelected;
    std::function<void()> onCameraClicked;

    void setSelectedTrack (Track track);
    Track getSelectedTrack() const noexcept { return selected; }

    // Called by the main window's UI timer (~30 Hz).
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int headWidth = 176;
    static constexpr int rulerHeight = 26;

private:
    class TrackRow;
    class PlayheadOverlay;
    class CameraMonitor;

    float getPlayheadProportion() const;

    Session& session;
    Track selected = Track::amp;
    std::vector<std::unique_ptr<TrackRow>> rows;
    std::unique_ptr<PlayheadOverlay> playhead;
    std::unique_ptr<CameraMonitor> monitor;
    bool monitorPlaced = false;
};
