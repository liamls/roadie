#pragma once

#include "../Session.h"
#include "Widgets.h"

// Top transport bar (.topbar): brand, transport cluster, bar/tempo LCD,
// input meter, CPU and latency pills, settings and the Export button.
class TopBar : public juce::Component
{
public:
    explicit TopBar (Session& session);

    std::function<void()> onOpenSettings;
    std::function<void()> onOpenExport;

    // Called by the main window's UI timer.
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // Bar.beat / tempo / signature readout. Drag the tempo up or down,
    // double-click it to type a value; click the signature to cycle it.
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
        int getIdealWidth() const { return 3 * cellWidth; }

    private:
        juce::Rectangle<int> cell (int index) const { return { index * cellWidth, 0, cellWidth, getHeight() }; }
        Session& session;
        juce::String position = "1.1";
        double dragStartBpm = 0.0;
        bool draggingTempo = false;
        static constexpr int cellWidth = 76;
    };

    Session& session;

    roadie::ui::IconButton previousButton { "Previous", roadie::icons::Icon::previous };
    roadie::ui::IconButton playButton { "Play", roadie::icons::Icon::play };
    roadie::ui::IconButton stopButton { "Stop", roadie::icons::Icon::stop };
    roadie::ui::IconButton recordButton { "Record take", roadie::icons::Icon::record, roadie::ui::IconButton::Style::record };
    roadie::ui::IconButton loopButton { "Loop", roadie::icons::Icon::loop };
    Lcd lcd;
    roadie::ui::Meter inputMeter;
    roadie::ui::StatPill cpuPill, latencyPill;
    roadie::ui::IconButton settingsButton { "Audio settings", roadie::icons::Icon::settings, roadie::ui::IconButton::Style::square };
    roadie::ui::PillButton exportButton;

    juce::Rectangle<int> brandArea, clusterArea, meterIconArea;
};
