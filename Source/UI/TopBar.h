#pragma once

#include "../CpuLoad.h"
#include "../Session.h"
#include "Widgets.h"

// Top bar: the Jouer | Monter switch first (the one place that says which
// workspace this is), the Bibliothèque / Inspecteur toggles; transport
// (début, lecture, stop, prise, cycle), the bar/tempo LCD and the metronome;
// the project's name and state in the middle; input meter, one status pill
// (latency · CPU, which opens the audio settings) and Exporter on the right.
// The looper is not a transport control: its pedal lives in Jouer.
class TopBar : public juce::Component
{
public:
    explicit TopBar (Session& session);

    std::function<void()> onOpenSettings;
    std::function<void()> onOpenExport;
    std::function<void()> onToggleLibrary;
    std::function<void()> onToggleInspector;
    std::function<void()> onRecord; // ● : the main window routes it (Monter goes back to Jouer first)
    std::function<void (int)> onWorkspaceChanged; // 0 = Jouer, 1 = Monter

    void setWorkspace (int index) { workspaceSwitch.setSelected (index); }
    // Where the switch is, for the first-launch hint.
    juce::Rectangle<int> getWorkspaceSwitchBounds() const { return workspaceSwitch.getBounds(); }

    // Lights the Bibliothèque / Inspecteur buttons.
    void setPanelsShown (bool library, bool inspector);

    // Called by the main window's UI timer.
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int preferredHeight = 46;

private:
    // Bar.beat / tempo / signature readout. Drag the tempo up or down,
    // double-click it to type a value; click the signature for its menu.
    class Lcd : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit Lcd (Session& s);
        void setPosition (const juce::String& p) { if (p != position) { position = p; repaint(); } }
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        int getIdealWidth() const { return 3 * cellWidth; }

    private:
        juce::Rectangle<int> cell (int index) const { return { index * cellWidth, 0, cellWidth, getHeight() }; }
        Session& session;
        juce::String position = "1.1";
        double dragStartBpm = 0.0;
        bool draggingTempo = false;
        int hoveredCell = -1;
        roadie::ui::HoverFade cellFades[3] { roadie::ui::HoverFade (*this), roadie::ui::HoverFade (*this), roadie::ui::HoverFade (*this) };
        static constexpr int cellWidth = 70;
    };

    Session& session;

    roadie::ui::IconButton previousButton { "Previous", roadie::icons::Icon::previous };
    roadie::ui::IconButton playButton { "Play", roadie::icons::Icon::play };
    roadie::ui::IconButton stopButton { "Stop", roadie::icons::Icon::stop };
    roadie::ui::IconButton recordButton { "Record take", roadie::icons::Icon::record, roadie::ui::IconButton::Style::record };
    roadie::ui::IconButton cycleButton { "Cycle", roadie::icons::Icon::cycle };
    roadie::ui::IconButton libraryButton { "Library", roadie::icons::Icon::library, roadie::ui::IconButton::Style::square };
    roadie::ui::IconButton inspectorButton { "Inspector", roadie::icons::Icon::inspector, roadie::ui::IconButton::Style::square };
    Lcd lcd;
    roadie::ui::IconButton metronomeButton { "Metronome", roadie::icons::Icon::metronome };
    roadie::ui::IconButton tunerButton { "Tuner", roadie::icons::Icon::tuner };
    void showTuner();
    roadie::ui::Meter inputMeter;
    roadie::ui::StatPill statusPill;
    roadie::ui::SegmentedControl workspaceSwitch;
    roadie::ui::IconButton settingsButton { "Audio settings", roadie::icons::Icon::settings, roadie::ui::IconButton::Style::square };
    roadie::ui::PillButton exportButton;

    CpuLoad cpuLoad;
    double lastCpuReading = 0.0;
    bool compact = false; // narrow window: no project name

    juce::Rectangle<int> clusterArea, meterIconArea, projectArea;
    juce::String shownProject;
};
