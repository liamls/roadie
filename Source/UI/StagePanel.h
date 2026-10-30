#pragma once

#include "../Session.h"
#include "Widgets.h"

// Right-hand column of Jouer: what a player touches between two takes.
//
//   ┌ Looper ─────────────────┐   the pedal: one big target cycling
//   │      ( Enregistrer )    │   record → loop → overdub, its ring
//   │ Retirer la couche · Eff.│   following the loop
//   ├━━━━━━━━━━━━━━━━━━━━━━━━━┤   ← divider, dragged (double-click: default)
//   │ Balance  Morceau Guitare│   one fader + meter per source, M to mute
//   │          Boucle         │
//   └─────────────────────────┘
//
// Targets are at least 44 pt: it is used with a guitar on the lap.
class StagePanel : public juce::Component
{
public:
    explicit StagePanel (Session& session);
    ~StagePanel() override;

    // Called by the main window's UI timer.
    void refresh();

    // Height of the Balance part, dragged by the divider above it.
    void setBalanceHeight (int height);
    std::function<void (int)> onBalanceHeightChanged;
    static constexpr int defaultBalanceHeight = 250, minBalanceHeight = 150;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Pedal;
    class Strip;

    Session& session;
    std::unique_ptr<Pedal> pedal;
    roadie::ui::PillButton undoLayerButton, clearButton, addLooperButton, countInButton, barsButton;
    bool looperShown = true;
    std::vector<std::unique_ptr<Strip>> strips;
    roadie::ui::Splitter splitter { roadie::ui::Splitter::Orientation::horizontal };
    int balanceHeight = defaultBalanceHeight, heightAtDragStart = 0;
    juce::Rectangle<int> looperHeader, balanceHeader;
};
