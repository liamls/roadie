#pragma once

#include "../Session.h"
#include "Widgets.h"

// The tracks, laid out like an NLE over a DAW:
//
//   ┌ zoom ─────────┬ ruler: cycle band (yellow) + bar numbers ───────────┐
//   │ Vidéo    M    │ thin band: the video of the Guitare regions (linked) │
//   │ Guitare  M S  │ regions of the takes (waveform), red while recording │
//   │ Looper   M S  │ the loop region, notched at each repeat              │
//   │ Morceau  M S  │ the backing track, its parts (Intro, A, B…) on top   │
//   │ Master  ──○── │ (the whole output: fader, stereo meter)              │
//   └───────────────┴ scrollbar ─────────────────────────────────────────────┘
//
// Each track's height is dragged by the bottom edge of its header
// (double-click: default) and all of them zoom together (⌥⌘↑ / ⌥⌘↓); when
// they don't fit, they shrink together.
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
    std::function<void()> onImportSong;      // "Choisir un morceau…"
    std::function<void()> onImportYouTube;   // "Importer depuis YouTube…"

    void setSelectedTrack (Track track);
    Track getSelectedTrack() const noexcept { return selected; }

    // Called by the main window's UI timer (~30 Hz).
    void refresh();

    // Horizontal zoom around the playhead (or the view's centre when the
    // playhead is out of view); factor > 1 zooms in.
    void zoomBy (double factor);
    void zoomToFit();

    // Track heights (the desired ones; they shrink together when the panel
    // is too short). Reported on mouse up, to be saved.
    void setTrackHeight (Track track, int height);
    int getTrackHeight (Track track) const;
    void zoomTracks (float factor);
    juce::String getTrackHeightsState() const;
    void setTrackHeightsState (const juce::String& state);
    std::function<void()> onTrackHeightsChanged;
    static int defaultTrackHeight (Track track) { return track == Track::camera ? 52 : 92; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

    static constexpr int rulerHeight = 30;
    static constexpr int scrollbarHeight = 12;

    // Width of the track headers, dragged by the divider between the headers
    // and the lanes (double-click: default). Reported on mouse up, to be saved.
    static constexpr int defaultHeaderWidth = 184, minHeaderWidth = 150, maxHeaderWidth = 300;
    int getHeaderWidth() const noexcept { return headerWidth; }
    void setHeaderWidth (int width);
    std::function<void (int)> onHeaderWidthChanged;

    // For the --selftest-edit tool: where a time falls in a lane, or in the
    // ruler (its cycle band or its numbers), in this component's coordinates.
    juce::Point<int> getLanePoint (Track track, double seconds) const;
    juce::Point<int> getRulerPoint (double seconds, bool cycleBand) const;
    double getPixelsPerSecond() const noexcept { return pixelsPerSecond; }
    int getTimelineLaneHeight (Track track) const noexcept { return rowHeightOf (track); }

private:
    class TrackHeader;
    class MasterHeader;
    class ZoomControl;
    class Ruler;
    class ArrangeView;
    class Overlay;

    // View mapping, in lane coordinates (x = 0 at the left of the lanes).
    float timeToX (double seconds) const noexcept { return (float) ((seconds - viewStart) * pixelsPerSecond); }
    double xToTime (float x) const noexcept { return viewStart + (double) x / pixelsPerSecond; }
    double getVisibleLength() const noexcept;
    double getGridStep() const;
    double snap (double seconds, bool enabled) const;
    // Where a click puts the playhead: on the grid when it lands within a few
    // pixels of it (the magnet, if on), exactly where it was clicked otherwise.
    double snapPlayhead (double seconds, bool freely) const;
    int rowTop (Track track) const noexcept;
    int rowHeightOf (Track track) const noexcept;
    int lanesBottom() const noexcept;
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
    int headerWidth = defaultHeaderWidth, headerWidthAtDragStart = defaultHeaderWidth;
    std::array<int, 4> trackHeights { 92, 92, 52, 92 }; // by Track value: amp, loop, camera, song
    std::array<int, 4> rowHeights {}, rowTops {};

    double viewStart = 0.0, pixelsPerSecond = 40.0;
    bool viewInitialised = false;
    double lastUserScroll = -10.0;
    float hoverX = -1.0f;
    std::vector<double> lastSignature;
    juce::String shownSong;

    std::vector<std::unique_ptr<TrackHeader>> headers;
    std::unique_ptr<MasterHeader> master;
    std::unique_ptr<ZoomControl> zoom;
    std::unique_ptr<Ruler> ruler;
    std::unique_ptr<ArrangeView> arrange;
    std::unique_ptr<Overlay> overlay;
    juce::ScrollBar scrollbar { false };
    // The Looper track is not there by default: this adds it.
    roadie::ui::PillButton addLooperButton { "Piste Looper", roadie::ui::PillButton::Variant::tinted, roadie::theme::loopAccent, roadie::icons::Icon::plus };
    bool isShown (Track track) const;
public:
    void confirmRemoveLooperTrack();
private:
    static constexpr int addRowHeight = 38;
    roadie::ui::Splitter headerSplitter { roadie::ui::Splitter::Orientation::vertical };
};
