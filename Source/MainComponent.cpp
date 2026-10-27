#include "MainComponent.h"
#include "Theme.h"

using namespace roadie;
using Track = Session::Track;

namespace
{
juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }

constexpr int topBarHeight = 54;
constexpr int libraryWidth = 212;
} // namespace

MainComponent::MainComponent()
{
    if (session.getSettings().getValue ("appearance") == "light")
    {
        theme::setAppearance (theme::Appearance::light);
        lookAndFeel.applyTheme();
    }

    setLookAndFeel (&lookAndFeel);
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

    topBar.onOpenSettings = [this] { openAudioSettings(); };
    topBar.onOpenExport = [this] { openExportSheet(); };
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
    addAndMakeVisible (timeline);
    addAndMakeVisible (inspector);

    exportSheet.onClose = [this] { grabKeyboardFocus(); };
    addChildComponent (exportSheet);

    selectTrack (Track::amp);
    session.addChangeListener (this);

   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (this);
   #endif

    setWantsKeyboardFocus (true);
    setSize (1180, 720);
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
    toastArea = juce::Rectangle<int> (width, 34).withCentre ({ libraryWidth + (getWidth() - libraryWidth) / 2, inspector.getY() - 30 });

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
    topBar.setBounds (area.removeFromTop (topBarHeight));
    inspector.setBounds (area.removeFromBottom (Inspector::preferredHeight));
    library.setBounds (area.removeFromLeft (libraryWidth));
    timeline.setBounds (area);
    exportSheet.setBounds (getLocalBounds());
    toastArea = juce::Rectangle<int> (10, 34).withCentre ({ libraryWidth + (getWidth() - libraryWidth) / 2, inspector.getY() - 30 });
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
    if (c == '1')                                        { selectTrack (Track::amp); return true; }
    if (c == '2')                                        { selectTrack (Track::loop); return true; }
    if (c == '3')                                        { selectTrack (Track::camera); return true; }
    if (key == juce::KeyPress::escapeKey)                { session.stopAll(); return true; }

    return false;
}

//==============================================================================
juce::StringArray MainComponent::getMenuBarNames()
{
    return { "Fichier", "Transport", "Audio", "Affichage" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu menu;

    auto item = [&menu] (int id, const juce::String& text, const juce::String& shortcut, bool enabled = true, bool ticked = false)
    {
        juce::PopupMenu::Item i (text);
        i.itemID = id;
        i.shortcutKeyDescription = shortcut;
        i.isEnabled = enabled;
        i.isTicked = ticked;
        menu.addItem (i);
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
    else if (index == 2)
    {
        item (audioSettings, utf8 ("R\xc3\xa9glages audio\xe2\x80\xa6"), utf8 ("\xe2\x8c\x98,"));
    }
    else
    {
        item (lightAppearance, utf8 ("Th\xc3\xa8me clair"), utf8 ("\xe2\x87\xa7\xe2\x8c\x98L"), true,
              theme::getAppearance() == theme::Appearance::light);
    }

    return menu;
}

void MainComponent::menuItemSelected (int id, int)
{
    switch (id)
    {
        case newProject:    session.newProject(); break;
        case openProject:   chooseProjectToOpen(); break;
        case saveProject:   if (session.saveProject()) session.notify (utf8 ("Projet enregistr\xc3\xa9")); break;
        case saveProjectAs: chooseSaveAs(); break;
        case revealProject: session.getProject().folder.revealToUser(); break;
        case exportClip:    openExportSheet(); break;
        case playPause:     session.togglePlay(); break;
        case recordTake:    session.toggleTakeRecording(); break;
        case loopRecord:    session.recordLoop(); break;
        case overdub:       session.toggleOverdub(); break;
        case undoOverdub:   session.undo(); break;
        case clearLoop:     session.clearLoop(); break;
        case returnToStart: session.returnToStart(); break;
        case metronome:     session.setMetronomeOn (! session.isMetronomeOn()); break;
        case audioSettings: openAudioSettings(); break;
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
