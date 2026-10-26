#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "RoadieLookAndFeel.h"
#include "Session.h"
#include "UI/ExportSheet.h"
#include "UI/Inspector.h"
#include "UI/LibraryPanel.h"
#include "UI/Timeline.h"
#include "UI/TopBar.h"

// The main window content, laid out like the mockup:
//
//   ┌──────────────── TopBar (transport, LCD, meters, Exporter) ────────────────┐
//   │ Library │ Timeline: ruler + Guitare / Ampli / Looper / Caméra + monitor   │
//   ├─────────┴──────────── Inspector (selected track) ─────────────────────────┤
//
// It owns the Session and routes keyboard shortcuts, menus and drops to it.
class MainComponent : public juce::Component,
                      public juce::MenuBarModel,
                      public juce::FileDragAndDropTarget,
                      private juce::Timer,
                      private juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;

    std::function<void (const juce::String&)> onTitleChanged;

    // Used by the --snapshot verification tool (see Main.cpp).
    void selectTrackByName (const juce::String& name);
    void showExportSheet() { openExportSheet(); }
    void setLightAppearance (bool shouldBeLight);
    Session& getSession() noexcept { return session; }

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;

private:
    enum MenuIds
    {
        newProject = 1, openProject, saveProject, saveProjectAs, revealProject, exportClip,
        playPause, recordTake, loopRecord, overdub, undoOverdub, clearLoop, returnToStart, metronome,
        audioSettings, lightAppearance
    };

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void selectTrack (Session::Track track);
    void openAudioSettings();
    void openExportSheet();
    void chooseProjectToOpen();
    void chooseSaveAs();
    void updateTitle();

    RoadieLookAndFeel lookAndFeel; // first: must outlive every child
    Session session;
    juce::TooltipWindow tooltipWindow { this, 700 };

    TopBar topBar { session };
    LibraryPanel library { session };
    Timeline timeline { session };
    Inspector inspector { session };
    ExportSheet exportSheet { session };

    std::unique_ptr<juce::FileChooser> chooser;
    juce::String shownNotice, shownTitle;
    juce::Rectangle<int> toastArea;
    bool dropHighlight = false;
};
