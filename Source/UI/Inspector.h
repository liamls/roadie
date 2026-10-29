#pragma once

#include "../Session.h"
#include "Widgets.h"

// Bottom contextual panel (.inspector): the controls of the selected track,
// with that module's accent bar on the left.
class Inspector : public juce::Component
{
public:
    explicit Inspector (Session& session);
    ~Inspector() override;

    void show (Session::Track track);
    std::function<void()> onImportSong; // "Choisir un morceau…"
    std::function<void()> onImportYouTube; // "Depuis YouTube…"
    Session::Track getShown() const noexcept { return shown; }

    // Called by the main window's UI timer.
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

    // Resizable by its top divider; the knobs grow with the height.
    static constexpr int preferredHeight = 118, minHeight = 100, maxHeight = 200;

private:
    class AmpPanel;
    class LooperPanel;
    class CameraPanel;
    class SongPanel;

    // In a narrow column the panels stack their parts and may be taller
    // than the inspector: they scroll.
    int getNarrowHeight() const;

    Session& session;
    Session::Track shown = Session::Track::amp;
    juce::Viewport viewport;
    juce::Component holder;
    std::unique_ptr<AmpPanel> ampPanel;
    std::unique_ptr<LooperPanel> looperPanel;
    std::unique_ptr<CameraPanel> cameraPanel;
    std::unique_ptr<SongPanel> songPanel;
};
