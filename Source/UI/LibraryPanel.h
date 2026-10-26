#pragma once

#include "../Session.h"
#include "Widgets.h"

// Left sidebar (.library): searchable list of the NAM captures found in the
// library folders. One click loads a capture.
class LibraryPanel : public juce::Component,
                     private juce::ListBoxModel
{
public:
    explicit LibraryPanel (Session& session);

    // Rebuilds the filtered list (library rescanned, capture loaded…).
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    juce::String getTooltipForRow (int row) override;

    void applyFilter();
    void chooseFolder();
    void chooseFile();

    Session& session;
    juce::TextEditor search;
    juce::ListBox list { "Captures", this };
    roadie::ui::IconButton addFolderButton { "Add folder", roadie::icons::Icon::plus, roadie::ui::IconButton::Style::square };
    roadie::ui::PillButton openFileButton;
    std::vector<CaptureInfo> visible;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> headerArea, searchIconArea;
    juce::String lastLoadedPath;
};
