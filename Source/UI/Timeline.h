#pragma once

#include "../Session.h"
#include "Widgets.h"

// Centre of the window, laid out like GarageBand's tracks area:
//
//   ┌ zoom ─────────┬ ruler: cycle band (yellow) + bar numbers ───────────┐
//   │ Guitare  M S  │ regions of the takes (waveform), red while recording │
//   │ Looper   M S  │ the loop region, notched at each repeat              │
//   │ Caméra   M    │ the video of the Guitare regions (linked)            │
//   └───────────────┴ scrollbar ──────────────────────── camera monitor ───┘
//
// Regions are moved by dragging (snapped to the grid, ⌘ to move freely,
// ⌥ to duplicate) and trimmed by their edges; right-click for the rest.
// The view zooms (slider, ⌘← / ⌘→, pinch, ⌘ + scroll) and scrolls
// horizontally, and follows the playhead while it plays.
class Timeline : public juce::Component,
                 private juce::ScrollBar::Listener
{
public:
    explicit Timeline (Session& session);
    ~Timeline() override;

    using Track = Session::Track;

    std::function<void (Track)> onTrackSelected;
    std::function<void()> onCameraClicked;
    std::function<void()> onExportRequested; // "Exporter cette région…"

    void setSelectedTrack (Track track);
    Track getSelectedTrack() const noexcept { return selected; }

    // Called by the main window's UI timer (~30 Hz).
    void refresh();

    // Horizontal zoom around the playhead (or the view's centre when the
    // playhead is out of view); factor > 1 zooms in.
    void zoomBy (double factor);
    void zoomToFit();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

    static constexpr int rulerHeight = 30;
    static constexpr int scrollbarHeight = 12;
    int getHeaderWidth() const noexcept { return headerWidth; }

private:
    class TrackHeader;
    class MiniFader;
    class ZoomControl;
    class Ruler;
    class ArrangeView;
    class Overlay;
    class CameraMonitor;

    // View mapping, in lane coordinates (x = 0 at the left of the lanes).
    float timeToX (double seconds) const noexcept { return (float) ((seconds - viewStart) * pixelsPerSecond); }
    double xToTime (float x) const noexcept { return viewStart + (double) x / pixelsPerSecond; }
    double getVisibleLength() const noexcept;
    double getGridStep() const;
    double snap (double seconds, bool enabled) const;
    int getRowHeight() const noexcept { return rowHeight; }
    int laneWidth() const noexcept { return std::max (1, getWidth() - headerWidth); }

    void setView (double start, double pps);
    void zoomAround (double anchorSeconds, float anchorX, double factor);
    void userScrolled();
    void updateScrollbar();
    double getScrollableLength() const;
    void followPlayhead();
    void setHoverX (float x);
    void repaintView();
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;

    static constexpr double minPixelsPerSecond = 2.0, maxPixelsPerSecond = 600.0;

    Session& session;
    Track selected = Track::amp;
    int headerWidth = 184;
    int rowHeight = 80;

    double viewStart = 0.0, pixelsPerSecond = 40.0;
    bool viewInitialised = false;
    double lastUserScroll = -10.0;
    float hoverX = -1.0f;
    std::vector<double> lastSignature;

    std::vector<std::unique_ptr<TrackHeader>> headers;
    std::unique_ptr<ZoomControl> zoom;
    std::unique_ptr<Ruler> ruler;
    std::unique_ptr<ArrangeView> arrange;
    std::unique_ptr<Overlay> overlay;
    std::unique_ptr<CameraMonitor> monitor;
    juce::ScrollBar scrollbar { false };
    bool monitorPlaced = false;
};
