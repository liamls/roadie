#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "I18n.h"
#include "RoadieLookAndFeel.h"
#include "Session.h"
#include "UI/ExportSheet.h"
#include "UI/Inspector.h"
#include "UI/LibraryPanel.h"
#include "UI/Monitor.h"
#include "UI/StagePanel.h"
#include "UI/Timeline.h"
#include "UI/TopBar.h"

// The main window content: one window, two workspaces, chosen by the
// Jouer | Monter switch at the top left (⌘1 / ⌘2, Affichage menu).
//
// Jouer — record, with the camera live:
//   ┌──────────────── TopBar ───────────────────────────────────────────────┐
//   │ Library ┃ Monitor (big, live, 9:16 / 16:9 guides)    ┃ Stage: Looper   │
//   │         ┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫ pedal, Balance  │
//   │         ┃ Timeline strip (the tracks, thin)          ┃                 │
//   ├━━━━━━━━━┻━━━━━━━━━━━━━━ Inspector (selected track) ━━┻━━━━━━━━━━━━━━━━━┤
//
// Monter — edit, the camera switched off:
//   │ Library ┃ Timeline (dominant)                        ┃ Monitor (playback)│
//   │         ┃                                            ┣━━━━━━━━━━━━━━━━━━━┫
//   │         ┃                                            ┃ Inspector         │
//
// Every ┃ ━ divider is dragged to resize (double-click: default size);
// dragged below its minimum a panel folds away, leaving a grip to pull it
// back. Sizes are kept per workspace (View › Reset
// disposition). ` (or the menu) enlarges the panel under the mouse.
// It owns the Session and routes keyboard shortcuts, menus and drops to it.
class MainComponent : public juce::Component,
                      public juce::MenuBarModel,
                      public juce::FileDragAndDropTarget,
                      private juce::Timer,
                      private juce::ChangeListener,
                      private juce::DarkModeSettingListener
{
public:
    MainComponent();
    ~MainComponent() override;

    std::function<void (const juce::String&)> onTitleChanged;

    enum class Workspace
    {
        play,
        edit
    };
    void setWorkspace (Workspace workspace, bool animate = true);
    Workspace getWorkspace() const noexcept { return workspace; }

    // ● from anywhere: in Monter it goes back to Jouer, waits for the camera,
    // then records.
    void recordTake();

    // Used by the --snapshot verification tool (see Main.cpp).
    void selectTrackByName (const juce::String& name);
    void showExportSheet() { openExportSheet(); }
    void chooseYouTubeSong();
    void chooseLanguage (i18n::Language language);
    void showLibraryTab (LibraryPanel::Tab tab) { library.setTab (tab); }
    // Forces the light or dark appearance (the --light / --dark snapshot flags).
    void setLightAppearance (bool shouldBeLight);
    Session& getSession() noexcept { return session; }
    // Used by the --selftest-edit tool (see Main.cpp).
    Timeline& getTimeline() noexcept { return timeline; }
    Monitor& getMonitor() noexcept { return monitor; }
    // A tooltip is a window of its own: over a click, it would swallow it.
    void disableTooltips() { tooltipWindow.setMillisecondsBeforeTipAppears (std::numeric_limits<int>::max()); }
    juce::Rectangle<int> getLibraryBounds() const { return library.isVisible() ? library.getBounds() : juce::Rectangle<int>(); }
    juce::Rectangle<int> getInspectorBounds() const { return inspector.isVisible() ? inspector.getBounds() : juce::Rectangle<int>(); }
    juce::Rectangle<int> getStageBounds() const { return stage.isVisible() ? stage.getBounds() : juce::Rectangle<int>(); }
    juce::Rectangle<int> getMonitorBounds() const { return monitor.isVisible() ? monitor.getBounds() : juce::Rectangle<int>(); }
    juce::Rectangle<int> getTimelineBounds() const { return timeline.isVisible() ? timeline.getBounds() : juce::Rectangle<int>(); }
    juce::Rectangle<int> getWorkspaceSwitchBounds() const { return topBar.getWorkspaceSwitchBounds() + topBar.getPosition(); }

    // Library (Y) and Inspector (B), shown or folded away, in the
    // current workspace.
    void setLibraryShown (bool shouldBeShown, bool remember = true);
    void setInspectorShown (bool shouldBeShown, bool remember = true);
    bool isLibraryShown() const noexcept;
    bool isInspectorShown() const noexcept;

    // ` : the panel under the mouse takes the whole window, again to restore.
    void toggleMaximisedPanel (juce::Component* panel);
    juce::Component* getMaximisedPanel() const noexcept { return maximised; }
    // View › Reset Layout (of the current workspace).
    void resetLayout();

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
        newProject = 1, openProject, saveProject, saveProjectAs, revealProject, exportClip, importSongItem, importYouTubeItem, loopCountInItem, magnetItem,
        undoEdit, redoEdit, splitRegion, duplicateRegion, deleteRegion, selectNone, cycle,
        playPause, recordTakeItem, countInItem, toggleLooperTrack, loopRecord, overdub, undoOverdub, clearLoop, returnToStart, metronome,
        audioSettings,
        workspacePlay, workspaceEdit, showLibrary, showInspector, maximisePanel, resetLayoutItem,
        zoomIn, zoomOut, zoomFit, tracksTaller, tracksShorter, lightAppearance,
        appearanceSystem, appearanceLight, appearanceDark,
        languageEnglish, languageFrench,
        loopBarsBase = 60, // + 0 (free) … 5 (16 bars), see loopBarChoices
        uiScaleBase = 100 // + index in uiScales
    };

    // "system" (default) follows the OS and switches live; "light" / "dark" pin it.
    enum class AppearanceMode { system, light, dark };
    AppearanceMode getAppearanceMode() const;
    void setAppearanceMode (AppearanceMode mode);
    void applyAppearance();
    void darkModeSettingChanged() override;

    // One resizable panel: its size along its divider's axis, per workspace.
    struct Pane
    {
        const char* key;
        int size, defaultSize, minSize, maxSize;
        bool collapsed = false;
    };
    struct Layout
    {
        Pane library { "library", 212, 212, 170, 360 };
        Pane inspector { "inspector", 260, 260, 120, 440 };
        Pane side { "side", 300, 300, 240, 640 };  // Jouer: the stage column; Monter: monitor + inspector
        Pane strip { "strip", 230, 230, 110, 900 }; // Jouer: the timeline under the monitor
        int balanceHeight = StagePanel::defaultBalanceHeight;
    };
    enum class Divider { library, inspector, side, strip };

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    Layout& layout() noexcept { return workspace == Workspace::play ? playLayout : editLayout; }
    const Layout& layout() const noexcept { return workspace == Workspace::play ? playLayout : editLayout; }
    Pane& paneFor (Divider divider);
    void loadLayouts();
    void saveLayout (Workspace w);
    void setUpDivider (roadie::ui::Splitter& splitter, Divider divider);
    void applyLayout (bool animate);
    void updateCamera();

    void selectTrack (Session::Track track);
    void openAudioSettings();
    void openExportSheet();
    void chooseProjectToOpen();
    void chooseSaveAs();
    void chooseSong();
    void updateTitle();
    juce::Component* panelUnderMouse() const;

    RoadieLookAndFeel lookAndFeel; // first: must outlive every child
    Session session;
    juce::TooltipWindow tooltipWindow { this, 700 };

    TopBar topBar { session };
    LibraryPanel library { session };
    Timeline timeline { session };
    Monitor monitor { session };
    StagePanel stage { session };
    Inspector inspector { session };
    ExportSheet exportSheet { session };
    roadie::ui::Splitter librarySplitter { roadie::ui::Splitter::Orientation::vertical };
    roadie::ui::Splitter sideSplitter { roadie::ui::Splitter::Orientation::vertical };
    roadie::ui::Splitter inspectorSplitter { roadie::ui::Splitter::Orientation::horizontal };
    roadie::ui::Splitter stripSplitter { roadie::ui::Splitter::Orientation::horizontal };

    class Coachmark;
    std::unique_ptr<Coachmark> coachmark;

    Workspace workspace = Workspace::play;
    Layout playLayout, editLayout;
    int sizeAtDragStart = 0;
    juce::Component* maximised = nullptr;
    juce::ComponentAnimator animator;

    bool pendingRecord = false;
    double pendingRecordSince = 0.0;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::String shownNotice, shownTitle;
    juce::Rectangle<int> toastArea;
    bool dropHighlight = false;
};
