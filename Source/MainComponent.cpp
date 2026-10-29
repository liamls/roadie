#include "MainComponent.h"
#include "Theme.h"

using namespace roadie;
using Track = Session::Track;

namespace
{
juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }
} // namespace

MainComponent::MainComponent()
{
    auto& settings = session.getSettings();
    if (settings.getValue ("appearance") == "light")
    {
        theme::setAppearance (theme::Appearance::light);
        lookAndFeel.applyTheme();
    }

    // Before anything is sized: the window opens at the chosen scale.
    juce::Desktop::getInstance().setGlobalScaleFactor ((float) settings.getDoubleValue ("uiScale", getDefaultUiScale()));

    setLookAndFeel (&lookAndFeel);
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

    topBar.onOpenSettings = [this] { openAudioSettings(); };
    topBar.onOpenExport = [this] { openExportSheet(); };
    topBar.onToggleLibrary = [this] { setLibraryShown (! libraryShown); };
    topBar.onToggleInspector = [this] { setInspectorShown (! inspectorShown); };
    addAndMakeVisible (topBar);
    addAndMakeVisible (library);

    timeline.onTrackSelected = [this] (Track t) { selectTrack (t); };
    timeline.onCameraClicked = [this]
    {
        selectTrack (Track::camera);
        if (CameraCapture::isSupported() && ! session.getCamera().isOpen() && ! session.isCameraOpening())
            session.openCamera (session.getSettings().getValue ("cameraDevice"));
    };
    timeline.onExportRequested = [this] { openExportSheet(); };
    timeline.setHeaderWidth (settings.getIntValue ("trackHeaderWidth", Timeline::defaultHeaderWidth));
    timeline.onHeaderWidthChanged = [this] (int width) { session.getSettings().setValue ("trackHeaderWidth", width); };
    addAndMakeVisible (timeline);
    addAndMakeVisible (inspector);

    // Dividers: drag to resize, double-click for the default size. Added
    // after the panels (over their borders) and before the export sheet.
    librarySplitter.onDragStart = [this] { sizeAtDragStart = libraryWidth; };
    librarySplitter.onDrag = [this] (int delta) { setLibraryWidth (sizeAtDragStart + delta, false); };
    librarySplitter.onDragEnd = [this] { setLibraryWidth (libraryWidth, true); };
    librarySplitter.onDoubleClick = [this] { setLibraryWidth (defaultLibraryWidth, true); };
    inspectorSplitter.onDragStart = [this] { sizeAtDragStart = inspectorHeight; };
    inspectorSplitter.onDrag = [this] (int delta) { setInspectorHeight (sizeAtDragStart - delta, false); };
    inspectorSplitter.onDragEnd = [this] { setInspectorHeight (inspectorHeight, true); };
    inspectorSplitter.onDoubleClick = [this] { setInspectorHeight (Inspector::preferredHeight, true); };
    addAndMakeVisible (librarySplitter);
    addAndMakeVisible (inspectorSplitter);

    libraryWidth = juce::jlimit (minLibraryWidth, maxLibraryWidth, settings.getIntValue ("libraryWidth", defaultLibraryWidth));
    inspectorHeight = juce::jlimit (Inspector::minHeight, Inspector::maxHeight, settings.getIntValue ("inspectorHeight", Inspector::preferredHeight));
    libraryShown = settings.getBoolValue ("libraryShown", true);
    inspectorShown = settings.getBoolValue ("inspectorShown", true);
    library.setVisible (libraryShown);
    inspector.setVisible (inspectorShown);
    topBar.setPanelsShown (libraryShown, inspectorShown);

    exportSheet.onClose = [this] { grabKeyboardFocus(); };
    addChildComponent (exportSheet);

    selectTrack (Track::amp);
    session.addChangeListener (this);

   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (this);
   #endif

    setWantsKeyboardFocus (true);

    // As large as the screen comfortably allows (in the scaled coordinates,
    // so a smaller interface opens a window that shows more).
    auto screen = juce::Rectangle<int> (0, 0, 1280, 800);
    if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        screen = display->userBounds.toNearestInt();
    setSize (juce::jlimit (980, 1440, screen.getWidth() * 92 / 100), juce::jlimit (600, 900, screen.getHeight() * 90 / 100));

    startTimerHz (30);
    updateTitle();
}

MainComponent::~MainComponent()
{
    stopTimer();
    session.removeChangeListener (this);
   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (nullptr);
   #endif
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

//==============================================================================
void MainComponent::timerCallback()
{
    // Keep the shortcuts alive: give focus back to the window once nothing
    // else (a text field, the export sheet) is holding it.
    if (isShowing() && juce::Component::getCurrentlyFocusedComponent() == nullptr)
        grabKeyboardFocus();

    topBar.refresh();
    timeline.refresh();
    inspector.refresh();
    if (exportSheet.isVisible())
        exportSheet.refresh();

    if (session.getNotice() != shownNotice)
    {
        repaint (toastArea.expanded (40, 20));
        shownNotice = session.getNotice();
        repaint (toastArea.expanded (40, 20));
    }
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    library.refresh();
    updateTitle();
    menuItemsChanged();
}

void MainComponent::updateTitle()
{
    const auto title = session.getProjectName() + utf8 (" \xe2\x80\x94 Roadie");
    if (title != shownTitle)
    {
        shownTitle = title;
        if (onTitleChanged)
            onTitleChanged (title);
    }
}

void MainComponent::setLightAppearance (bool shouldBeLight)
{
    theme::setAppearance (shouldBeLight ? theme::Appearance::light : theme::Appearance::dark);
    lookAndFeel.applyTheme();
    session.getSettings().setValue ("appearance", shouldBeLight ? "light" : "dark");
    sendLookAndFeelChange();
    repaint();
    menuItemsChanged();
}

//==============================================================================
void MainComponent::setLibraryShown (bool shouldBeShown, bool remember)
{
    libraryShown = shouldBeShown;
    library.setVisible (shouldBeShown);
    if (remember)
        session.getSettings().setValue ("libraryShown", shouldBeShown);
    topBar.setPanelsShown (libraryShown, inspectorShown);
    resized();
    menuItemsChanged();
}

void MainComponent::setInspectorShown (bool shouldBeShown, bool remember)
{
    inspectorShown = shouldBeShown;
    inspector.setVisible (shouldBeShown);
    if (remember)
        session.getSettings().setValue ("inspectorShown", shouldBeShown);
    topBar.setPanelsShown (libraryShown, inspectorShown);
    resized();
    menuItemsChanged();
}

int MainComponent::getMaxLibraryWidth() const
{
    // Always leave the timeline (headers + a few bars) usable.
    return juce::jlimit (minLibraryWidth, maxLibraryWidth, getWidth() - timeline.getHeaderWidth() - 360);
}

void MainComponent::setLibraryWidth (int width, bool save)
{
    libraryWidth = juce::jlimit (minLibraryWidth, getMaxLibraryWidth(), width);
    if (save)
        session.getSettings().setValue ("libraryWidth", libraryWidth);
    resized();
}

void MainComponent::setInspectorHeight (int height, bool save)
{
    inspectorHeight = juce::jlimit (Inspector::minHeight, Inspector::maxHeight, height);
    if (save)
        session.getSettings().setValue ("inspectorHeight", inspectorHeight);
    resized();
}

float MainComponent::getDefaultUiScale()
{
    // logicalBounds is divided by the current scale: undo it to get points.
    if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        return display->logicalBounds.getWidth() * juce::Desktop::getInstance().getGlobalScaleFactor() <= 1440.0f ? 0.9f : 1.0f;
    return 1.0f;
}

void MainComponent::setUiScale (float scale)
{
    // The window keeps its size on screen; its content gets more (or fewer)
    // logical pixels, so a smaller interface shows more of the timeline.
    juce::Desktop::getInstance().setGlobalScaleFactor (scale);
    session.getSettings().setValue ("uiScale", (double) scale);
    menuItemsChanged();
}

void MainComponent::selectTrackByName (const juce::String& name)
{
    if (name == "loop")
        selectTrack (Track::loop);
    else if (name == "camera")
        selectTrack (Track::camera);
    else
        selectTrack (Track::amp);
}

void MainComponent::selectTrack (Track track)
{
    timeline.setSelectedTrack (track);
    inspector.show (track);
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void MainComponent::paintOverChildren (juce::Graphics& g)
{
    using namespace theme;

    if (dropHighlight)
    {
        g.setColour (exportAccent.withAlpha (0.08f));
        g.fillRect (getLocalBounds());
        g.setColour (exportAccent);
        g.drawRect (getLocalBounds(), 2);
    }

    // Toast: short confirmations ("Prise 3 enregistrée…") above the inspector.
    if (shownNotice.isEmpty())
        return;

    const auto font = fonts::semibold (13.0f);
    const int width = std::min (getWidth() - 40, (int) juce::GlyphArrangement::getStringWidth (font, shownNotice) + 36);
    toastArea = juce::Rectangle<int> (width, 34).withCentre ({ timeline.getBounds().getCentreX(), timeline.getBottom() - 30 });

    g.setColour (surfaceRaised.withAlpha (0.97f));
    g.fillRoundedRectangle (toastArea.toFloat(), 17.0f);
    g.setColour (border);
    g.drawRoundedRectangle (toastArea.toFloat().reduced (0.5f), 17.0f, 1.0f);
    g.setColour (text);
    g.setFont (font);
    g.drawText (shownNotice, toastArea.reduced (18, 0), juce::Justification::centred, true);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    topBar.setBounds (area.removeFromTop (TopBar::preferredHeight));

    // The inspector never takes more than a third of the window.
    const int shownInspectorHeight = std::min (inspectorHeight, std::max (Inspector::minHeight, area.getHeight() / 3));
    if (inspectorShown)
        inspector.setBounds (area.removeFromBottom (shownInspectorHeight));
    inspectorSplitter.setVisible (inspectorShown);
    inspectorSplitter.setBounds (0, area.getBottom() - roadie::ui::Splitter::thickness / 2, getWidth(), roadie::ui::Splitter::thickness);

    const int shownLibraryWidth = std::min (libraryWidth, getMaxLibraryWidth());
    if (libraryShown)
        library.setBounds (area.removeFromLeft (shownLibraryWidth));
    librarySplitter.setVisible (libraryShown);
    librarySplitter.setBounds (area.getX() - roadie::ui::Splitter::thickness / 2, area.getY(), roadie::ui::Splitter::thickness, area.getHeight());

    timeline.setBounds (area);
    exportSheet.setBounds (getLocalBounds());
    toastArea = juce::Rectangle<int> (10, 34).withCentre ({ timeline.getBounds().getCentreX(), timeline.getBottom() - 30 });
}

//==============================================================================
bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const auto code = key.getKeyCode();
    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());

    if (mods.isCommandDown())
    {
        if (code == 'N') { session.newProject(); return true; }
        if (code == 'O') { chooseProjectToOpen(); return true; }
        if (code == 'S' && mods.isShiftDown()) { chooseSaveAs(); return true; }
        if (code == 'S') { if (session.saveProject()) session.notify (utf8 ("Projet enregistr\xc3\xa9")); return true; }
        if (code == 'E') { openExportSheet(); return true; }
        if (code == ',') { openAudioSettings(); return true; }
        if (code == 'L' && mods.isShiftDown()) { setLightAppearance (theme::getAppearance() != theme::Appearance::light); return true; }

        // Editing (the timeline's regions).
        if (code == 'Z' && mods.isShiftDown()) { session.redoEdit(); return true; }
        if (code == 'Z') { session.undoEdit(); return true; }
        if (code == 'T') { session.splitAtPlayhead(); return true; }
        if (code == 'D') { session.duplicateSelection(); return true; }
        if (code == juce::KeyPress::leftKey)  { timeline.zoomBy (1.0 / 1.5); return true; }
        if (code == juce::KeyPress::rightKey) { timeline.zoomBy (1.5); return true; }
        return false;
    }

    // Performance shortcuts: everything is reachable without the mouse
    // while both hands are on the guitar… or one foot near the keyboard.
    if (key == juce::KeyPress::spaceKey)                 { session.togglePlay(); return true; }
    if (key == juce::KeyPress::returnKey
        || key == juce::KeyPress::homeKey)               { session.returnToStart(); return true; }
    if (key == juce::KeyPress (juce::KeyPress::backspaceKey, juce::ModifierKeys::shiftModifier, 0))
                                                         { session.clearLoop(); return true; }
    if (key == juce::KeyPress::backspaceKey
        || key == juce::KeyPress::deleteKey)             { session.deleteSelection(); return true; }
    if (key == juce::KeyPress::leftKey)                  { session.jumpBars (-1); return true; }
    if (key == juce::KeyPress::rightKey)                 { session.jumpBars (1); return true; }
    if (c == 'c')                                        { session.toggleCycle(); return true; }
    if (c == 'r')                                        { session.recordLoop(); return true; }
    if (c == 'o')                                        { session.toggleOverdub(); return true; }
    if (c == 'u')                                        { session.undo(); return true; }
    if (c == 't')                                        { session.toggleTakeRecording(); return true; }
    if (c == 'm')                                        { session.setMetronomeOn (! session.isMetronomeOn()); return true; }
    if (c == 'y')                                        { setLibraryShown (! libraryShown); return true; }
    if (c == 'b')                                        { setInspectorShown (! inspectorShown); return true; }
    if (c == '1')                                        { selectTrack (Track::amp); return true; }
    if (c == '2')                                        { selectTrack (Track::loop); return true; }
    if (c == '3')                                        { selectTrack (Track::camera); return true; }
    if (key == juce::KeyPress::escapeKey)                { session.stopAll(); return true; }

    return false;
}

//==============================================================================
juce::StringArray MainComponent::getMenuBarNames()
{
    return { "Fichier", utf8 ("\xc3\x89" "dition"), "Transport", "Audio", "Affichage" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu menu;

    auto makeItem = [] (int id, const juce::String& text, const juce::String& shortcut, bool enabled, bool ticked)
    {
        juce::PopupMenu::Item i (text);
        i.itemID = id;
        i.shortcutKeyDescription = shortcut;
        i.isEnabled = enabled;
        i.isTicked = ticked;
        return i;
    };
    auto item = [&menu, &makeItem] (int id, const juce::String& text, const juce::String& shortcut, bool enabled = true, bool ticked = false)
    {
        menu.addItem (makeItem (id, text, shortcut, enabled, ticked));
    };

    if (index == 0)
    {
        item (newProject, "Nouveau projet", utf8 ("\xe2\x8c\x98N"));
        item (openProject, utf8 ("Ouvrir\xe2\x80\xa6"), utf8 ("\xe2\x8c\x98O"));
        menu.addSeparator();
        item (saveProject, "Enregistrer", utf8 ("\xe2\x8c\x98S"));
        item (saveProjectAs, utf8 ("Enregistrer sous\xe2\x80\xa6"), utf8 ("\xe2\x87\xa7\xe2\x8c\x98S"));
        item (revealProject, "Afficher le projet dans le Finder", {}, session.hasProjectOnDisk());
        menu.addSeparator();
        item (exportClip, utf8 ("Exporter le clip\xe2\x80\xa6"), utf8 ("\xe2\x8c\x98" "E"));
    }
    else if (index == 1)
    {
        const bool hasSelection = session.getSelection() != Session::Selection::none;
        item (undoEdit, session.canUndoEdit() ? "Annuler : " + session.getUndoEditName() : juce::String ("Annuler"),
              utf8 ("\xe2\x8c\x98Z"), session.canUndoEdit());
        item (redoEdit, session.canRedoEdit() ? utf8 ("R\xc3\xa9tablir : ") + session.getRedoEditName() : utf8 ("R\xc3\xa9tablir"),
              utf8 ("\xe2\x87\xa7\xe2\x8c\x98Z"), session.canRedoEdit());
        menu.addSeparator();
        item (splitRegion, utf8 ("Couper \xc3\xa0 la t\xc3\xaa" "te de lecture"), utf8 ("\xe2\x8c\x98T"));
        item (duplicateRegion, utf8 ("Dupliquer la r\xc3\xa9gion"), utf8 ("\xe2\x8c\x98" "D"), hasSelection);
        item (deleteRegion, utf8 ("Supprimer la r\xc3\xa9gion"), utf8 ("\xe2\x8c\xab"), hasSelection);
        item (selectNone, utf8 ("Tout d\xc3\xa9s\xc3\xa9lectionner"), {}, hasSelection);
        menu.addSeparator();
        item (cycle, "Zone de cycle", "C", true, session.isCycleOn());
    }
    else if (index == 2)
    {
        const auto state = session.getEngine().getLooper().getState();
        item (playPause, "Lecture / pause", "Espace");
        item (returnToStart, utf8 ("Retour au d\xc3\xa9" "but"), utf8 ("Entr\xc3\xa9" "e"));
        menu.addSeparator();
        item (recordTake, session.isRecordingTake() ? utf8 ("Arr\xc3\xaa" "ter la prise") : utf8 ("Enregistrer une prise (audio + cam\xc3\xa9ra)"), "T");
        menu.addSeparator();
        item (loopRecord, "Boucle : enregistrer / boucler", "R");
        item (overdub, "Overdub", "O", state != Looper::State::Empty && state != Looper::State::Recording);
        item (undoOverdub, "Annuler le dernier overdub", "U", session.getEngine().getLooper().isUndoAvailable());
        item (clearLoop, "Effacer la boucle", utf8 ("\xe2\x87\xa7\xe2\x8c\xab"), state != Looper::State::Empty);
        menu.addSeparator();
        item (metronome, utf8 ("M\xc3\xa9tronome"), "M", true, session.isMetronomeOn());
    }
    else if (index == 3)
    {
        item (audioSettings, utf8 ("R\xc3\xa9glages audio\xe2\x80\xa6"), utf8 ("\xe2\x8c\x98,"));
    }
    else
    {
        item (showLibrary, utf8 ("Biblioth\xc3\xa8que"), "Y", true, libraryShown);
        item (showInspector, "Inspecteur", "B", true, inspectorShown);
        menu.addSeparator();
        item (zoomIn, "Zoom avant", utf8 ("\xe2\x8c\x98\xe2\x86\x92"));
        item (zoomOut, utf8 ("Zoom arri\xc3\xa8re"), utf8 ("\xe2\x8c\x98\xe2\x86\x90"));
        item (zoomFit, utf8 ("Tout le morceau \xc3\xa0 l'\xc3\xa9" "cran"), {});
        menu.addSeparator();

        juce::PopupMenu sizes;
        const float current = juce::Desktop::getInstance().getGlobalScaleFactor();
        for (int i = 0; i < (int) std::size (uiScales); ++i)
            sizes.addItem (makeItem (uiScaleBase + i, juce::String (juce::roundToInt (uiScales[i] * 100.0f)) + utf8 ("\xc2\xa0%"), {}, true,
                                     std::abs (uiScales[i] - current) < 0.01f));
        menu.addSubMenu ("Taille de l'interface", sizes);
        menu.addSeparator();
        item (lightAppearance, utf8 ("Th\xc3\xa8me clair"), utf8 ("\xe2\x87\xa7\xe2\x8c\x98L"), true,
              theme::getAppearance() == theme::Appearance::light);
    }

    return menu;
}

void MainComponent::menuItemSelected (int id, int)
{
    if (id >= uiScaleBase && id < uiScaleBase + (int) std::size (uiScales))
    {
        setUiScale (uiScales[id - uiScaleBase]);
        return;
    }

    switch (id)
    {
        case newProject:      session.newProject(); break;
        case openProject:     chooseProjectToOpen(); break;
        case saveProject:     if (session.saveProject()) session.notify (utf8 ("Projet enregistr\xc3\xa9")); break;
        case saveProjectAs:   chooseSaveAs(); break;
        case revealProject:   session.getProject().folder.revealToUser(); break;
        case exportClip:      openExportSheet(); break;
        case undoEdit:        session.undoEdit(); break;
        case redoEdit:        session.redoEdit(); break;
        case splitRegion:     session.splitAtPlayhead(); break;
        case duplicateRegion: session.duplicateSelection(); break;
        case deleteRegion:    session.deleteSelection(); break;
        case selectNone:      session.clearSelection(); break;
        case cycle:           session.toggleCycle(); break;
        case playPause:       session.togglePlay(); break;
        case recordTake:      session.toggleTakeRecording(); break;
        case loopRecord:      session.recordLoop(); break;
        case overdub:         session.toggleOverdub(); break;
        case undoOverdub:     session.undo(); break;
        case clearLoop:       session.clearLoop(); break;
        case returnToStart:   session.returnToStart(); break;
        case metronome:       session.setMetronomeOn (! session.isMetronomeOn()); break;
        case audioSettings:   openAudioSettings(); break;
        case showLibrary:     setLibraryShown (! libraryShown); break;
        case showInspector:   setInspectorShown (! inspectorShown); break;
        case zoomIn:          timeline.zoomBy (1.5); break;
        case zoomOut:         timeline.zoomBy (1.0 / 1.5); break;
        case zoomFit:         timeline.zoomToFit(); break;
        case lightAppearance: setLightAppearance (theme::getAppearance() != theme::Appearance::light); break;
        default: break;
    }
}

//==============================================================================
bool MainComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& path : files)
    {
        const juce::File f (path);
        if (f.hasFileExtension (".nam;.wav;.aif;.aiff;.json") || f.hasFileExtension (Project::extension) || f.isDirectory())
        {
            dropHighlight = true;
            repaint();
            return true;
        }
    }
    return false;
}

void MainComponent::filesDropped (const juce::StringArray& files, int, int)
{
    dropHighlight = false;
    repaint();

    for (const auto& path : files)
    {
        const juce::File f (path);
        if (f.hasFileExtension (".nam"))
            session.loadCapture (f);
        else if (f.hasFileExtension (".wav;.aif;.aiff"))
            session.loadImpulseResponse (f);
        else if (f.hasFileExtension (Project::extension) || f.getFileName() == "project.json")
            session.openProject (f);
        else if (f.isDirectory())
            session.addCaptureFolder (f);
    }
}

void MainComponent::fileDragExit (const juce::StringArray&)
{
    dropHighlight = false;
    repaint();
}

//==============================================================================
void MainComponent::openAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (session.getDeviceManager(), 1, 2, 2, 2, false, false, true, false);
    selector->setSize (480, 440);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector);
    options.dialogTitle = utf8 ("R\xc3\xa9glages audio");
    options.dialogBackgroundColour = theme::surface;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

void MainComponent::openExportSheet()
{
    exportSheet.open();
}

void MainComponent::chooseProjectToOpen()
{
    chooser = std::make_unique<juce::FileChooser> ("Ouvrir un projet Roadie", Project::getDefaultProjectsFolder(), "*.roadie;project.json");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f != juce::File())
                                  if (session.openProject (f))
                                      session.notify (utf8 ("Projet ouvert : ") + session.getProjectName());
                          });
}

void MainComponent::chooseSaveAs()
{
    const auto start = Project::getDefaultProjectsFolder().getChildFile (session.getProjectName() + Project::extension);
    Project::getDefaultProjectsFolder().createDirectory();
    chooser = std::make_unique<juce::FileChooser> ("Enregistrer le projet sous", start, "*.roadie");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode, [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f != juce::File())
            session.saveProjectAs (f);
    });
}
