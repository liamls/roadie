#pragma once

#include "../Session.h"
#include "Widgets.h"

// Left sidebar (.library): a browser with three tabs, one search box.
//   Captures — the NAM captures of the library folders; a click loads one.
//   Morceaux — songs imported before and those in Musique/Roadie/Morceaux;
//              a click makes it the backing track.
//   Prises   — the project's takes; a click goes to the take on the
//              timeline (or puts it back at the playhead when it was removed).
class LibraryPanel : public juce::Component,
                     private juce::ListBoxModel
{
public:
    explicit LibraryPanel (Session& session);

    enum class Tab { captures, songs, takes };
    void setTab (Tab tab);
    Tab getTab() const noexcept { return tab; }

    std::function<void()> onImportSong; // "Importer un morceau…"
    std::function<void()> onImportYouTube; // "Depuis YouTube…"
    std::function<void (int clipIndex)> onShowClip; // a take was picked: show its region

    // Rebuilds the filtered list (library rescanned, capture loaded…).
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;
    // Also receives the list's mouse events, for the row hover.
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    // One line of the list, whatever the tab.
    struct Row
    {
        juce::String title, subtitle, tooltip;
        juce::Colour colour;
        bool active = false;
        std::function<void()> action;
        juce::String key; // file path of a capture or a song
        int takeIndex = -1;
        bool hidden = false;
    };

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    juce::String getTooltipForRow (int row) override;

    std::vector<Row> buildRows() const;
    void applyFilter();
    void chooseFolder();
    void chooseFile();
    juce::String placeholder() const;

    Session& session;
    Tab tab = Tab::captures;
    roadie::ui::SegmentedControl tabs;
    juce::TextEditor search;
    juce::ListBox list { "Library", this };
    // The row under the mouse fades in, the one it left fades out.
    int hoveredRow = -1, fadingRow = -1;
    roadie::ui::HoverFade rowFadeIn { list }, rowFadeOut { list };
    int rowAt (const juce::MouseEvent&);
    void setHoveredRow (int row);
    roadie::ui::IconButton addFolderButton { "Add folder", roadie::icons::Icon::plus, roadie::ui::IconButton::Style::square };
    roadie::ui::PillButton openFileButton, youtubeButton, hiddenButton;
    // Entries put aside are out of the list, unless this is on (to bring them back).
    bool showHidden = false;
    juce::String kindOfTab() const { return tab == Tab::captures ? "captures" : tab == Tab::songs ? "songs" : "takes"; }
    void updateHiddenButton();
    void showRowMenu (int row);
    std::vector<Row> visible;
    // Musique/Roadie/Morceaux, scanned at most every few seconds.
    mutable juce::Array<juce::File> songFolderFiles;
    mutable double songFolderScanTime = -100.0;
    juce::String visibleSignature;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> headerArea, searchIconArea;
};
