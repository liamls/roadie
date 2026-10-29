#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "RoadieLookAndFeel.h"
#include "Session.h"
#include "UI/ExportSheet.h"
#include "UI/Inspector.h"
#include "UI/LibraryPanel.h"
#include "UI/Timeline.h"
#include "UI/TopBar.h"

// The main window content, laid out like GarageBand:
//
//   ┌──────────────── TopBar (transport, LCD, meters, Exporter) ────────────────┐
//   │ Library ┃ Timeline: ruler + Guitare / Looper / Caméra + camera monitor    │
//   ├━━━━━━━━━┻━━━━━━━━━━━━ Inspector (selected track) ━━━━━━━━━━━━━━━━━━━━━━━━━┤
//
// The library (Y) and the inspector (B) fold away; the ┃ ━ dividers are
// dragged to resize them, and their sizes are kept in the settings.
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
    // Used by the --selftest-edit tool (see Main.cpp).
    Timeline& getTimeline() noexcept { return timeline; }
    // A tooltip is a window of its own: over a click, it would swallow it.
    void disableTooltips() { tooltipWindow.setMillisecondsBeforeTipAppears (std::numeric_limits<int>::max()); }
    juce::Rectangle<int> getLibraryBounds() const { return library.isVisible() ? library.getBounds() : juce::Rectangle<int>(); }
    juce::Rectangle<int> getInspectorBounds() const { return inspector.isVisible() ? inspector.getBounds() : juce::Rectangle<int>(); }

    // Bibliothèque (Y) and Inspecteur (B), shown or folded away.
    void setLibraryShown (bool shouldBeShown, bool remember = true);
    void setInspectorShown (bool shouldBeShown, bool remember = true);
    bool isLibraryShown() const noexcept { return libraryShown; }
    bool isInspectorShown() const noexcept { return inspectorShown; }

    // Affichage > Taille de l'interface: the whole UI scaled (Desktop's
    // global scale factor), 80 to 125 %. Without a saved choice: 90 % on
    // screens of 1440 pt or less (13" laptops), 100 % otherwise.
    void setUiScale (float scale);
    static float getDefaultUiScale();
    static constexpr float uiScales[] = { 0.8f, 0.9f, 1.0f, 1.1f, 1.25f };

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
        undoEdit, redoEdit, splitRegion, duplicateRegion, deleteRegion, selectNone, cycle,
        playPause, recordTake, loopRecord, overdub, undoOverdub, clearLoop, returnToStart, metronome,
        audioSettings,
        showLibrary, showInspector, zoomIn, zoomOut, zoomFit, lightAppearance,
        uiScaleBase = 100 // + index in uiScales
    };

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void selectTrack (Session::Track track);
    void openAudioSettings();
    void openExportSheet();
    void chooseProjectToOpen();
    void chooseSaveAs();
    void updateTitle();
    void setLibraryWidth (int width, bool save);
    void setInspectorHeight (int height, bool save);
    int getMaxLibraryWidth() const;

    RoadieLookAndFeel lookAndFeel; // first: must outlive every child
    Session session;
    juce::TooltipWindow tooltipWindow { this, 700 };

    TopBar topBar { session };
    LibraryPanel library { session };
    Timeline timeline { session };
    Inspector inspector { session };
    ExportSheet exportSheet { session };
    roadie::ui::Splitter librarySplitter { roadie::ui::Splitter::Orientation::vertical };
    roadie::ui::Splitter inspectorSplitter { roadie::ui::Splitter::Orientation::horizontal };

    bool libraryShown = true, inspectorShown = true;
    int libraryWidth = defaultLibraryWidth, inspectorHeight = Inspector::preferredHeight;
    int sizeAtDragStart = 0;
    static constexpr int defaultLibraryWidth = 212, minLibraryWidth = 170, maxLibraryWidth = 360;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::String shownNotice, shownTitle;
    juce::Rectangle<int> toastArea;
    bool dropHighlight = false;
};
