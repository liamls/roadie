#include "MainComponent.h"
#include "I18n.h"
#include "Theme.h"

using namespace roadie;
using Track = Session::Track;

namespace
{
constexpr int loopBarChoices[] = { 0, 1, 2, 4, 8, 16 };
juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }
double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }
constexpr int transitionMs = 200;
const char* workspaceKey (MainComponent::Workspace w) { return w == MainComponent::Workspace::play ? "play" : "edit"; }
} // namespace

//==============================================================================
// First launch: a bubble under the switch says what the two workspaces are.
// A click on it (or the first switch) dismisses it for good.
class MainComponent::Coachmark : public juce::Component
{
public:
    std::function<void()> onDismiss;

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        auto bounds = getLocalBounds().toFloat().reduced (1.0f).withTrimmedTop (8.0f);
        juce::Path bubble;
        bubble.addPath (roadie::theme::squircle (bounds, radiusMd));
        bubble.addTriangle (34.0f, 1.0f, 26.0f, bounds.getY() + 0.5f, 42.0f, bounds.getY() + 0.5f);
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.fillPath (bubble, juce::AffineTransform::translation (0.0f, 3.0f));
        g.setColour (surfaceRaised);
        g.fillPath (bubble);
        g.setColour (exportAccent.withAlpha (0.7f));
        g.strokePath (bubble, juce::PathStrokeType (1.0f));

        auto content = bounds.reduced (16.0f, 12.0f);
        g.setColour (theme::text);
        g.setFont (fonts::display (14.0f));
        g.drawText (tr ("Two workspaces"), content.removeFromTop (20.0f), juce::Justification::centredLeft, false);
        content.removeFromTop (4.0f);
        g.setColour (textMuted);
        g.setFont (fonts::medium (12.5f));
        g.drawFittedText (tr ("Play: the camera, the pedal and the mixer, for recording.\nArrange: a big timeline for editing, camera off.\n⌘1 / ⌘2 to switch between them."),
                          content.removeFromTop (72.0f).toNearestInt(), juce::Justification::topLeft, 4, 1.0f);
        g.setColour (ink (exportAccent));
        g.setFont (fonts::semibold (12.0f));
        g.drawText (tr ("Got it"), content, juce::Justification::bottomRight, false);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (onDismiss)
            onDismiss();
    }
};

//==============================================================================
MainComponent::MainComponent()
{
    auto& settings = session.getSettings();
    // Before the first paint: system (default), or the mode saved by the user.
    {
        const auto mode = getAppearanceMode();
        const bool light = mode == AppearanceMode::light
                           || (mode == AppearanceMode::system && ! juce::Desktop::getInstance().isDarkModeActive());
        theme::setAppearance (light ? theme::Appearance::light : theme::Appearance::dark);
        lookAndFeel.applyTheme();
    }
    juce::Desktop::getInstance().addDarkModeSettingListener (this);

    // Before anything is sized: the window opens at the chosen scale.
    juce::Desktop::getInstance().setGlobalScaleFactor ((float) settings.getDoubleValue ("uiScale", getDefaultUiScale()));

    setLookAndFeel (&lookAndFeel);
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

    topBar.onOpenSettings = [this] { openAudioSettings(); };
    topBar.onOpenExport = [this] { openExportSheet(); };
    topBar.onToggleLibrary = [this] { setLibraryShown (! isLibraryShown()); };
    topBar.onToggleInspector = [this] { setInspectorShown (! isInspectorShown()); };
    topBar.onRecord = [this] { recordTake(); };
    topBar.onWorkspaceChanged = [this] (int index) { setWorkspace (index == 0 ? Workspace::play : Workspace::edit); };
    addAndMakeVisible (topBar);
    library.onImportSong = [this] { chooseSong(); };
    library.onImportYouTube = [this] { chooseYouTubeSong(); };
    library.onShowClip = [this] (int clipIndex)
    {
        const auto& clips = session.getArrangement().clips;
        if (! juce::isPositiveAndBelow (clipIndex, (int) clips.size()))
            return;
        session.selectClip (clipIndex);
        session.locate (clips[(size_t) clipIndex].start);
        selectTrack (Track::amp);
    };
    addAndMakeVisible (library);

    timeline.onTrackSelected = [this] (Track t) { selectTrack (t); };
    timeline.onCameraClicked = [this]
    {
        selectTrack (Track::camera);
        if (workspace == Workspace::play && CameraCapture::isSupported() && ! session.getCamera().isOpen() && ! session.isCameraOpening())
            session.openCamera (session.getSettings().getValue ("cameraDevice"));
    };
    timeline.onExportRequested = [this] { openExportSheet(); };
    timeline.onImportSong = [this] { chooseSong(); };
    timeline.onImportYouTube = [this] { chooseYouTubeSong(); };
    timeline.onTrackHeightsChanged = [this]
    {
        session.getSettings().setValue (juce::String ("layout.") + workspaceKey (workspace) + ".trackHeights", timeline.getTrackHeightsState());
    };
    inspector.onImportSong = [this] { chooseSong(); };
    inspector.onImportYouTube = [this] { chooseYouTubeSong(); };
    timeline.setHeaderWidth (settings.getIntValue ("trackHeaderWidth", Timeline::defaultHeaderWidth));
    timeline.onHeaderWidthChanged = [this] (int width) { session.getSettings().setValue ("trackHeaderWidth", width); };
    addAndMakeVisible (timeline);

    monitor.onCameraClicked = [this] { timeline.onCameraClicked(); };
    monitor.onDoubleClicked = [this] { toggleMaximisedPanel (&monitor); };
    addAndMakeVisible (monitor);

    stage.onBalanceHeightChanged = [this] (int height)
    {
        playLayout.balanceHeight = height;
        saveLayout (Workspace::play);
    };
    addAndMakeVisible (stage);
    addAndMakeVisible (inspector);

    // Dividers: added after the panels (over their borders) and before the
    // export sheet.
    setUpDivider (librarySplitter, Divider::library);
    setUpDivider (sideSplitter, Divider::side);
    setUpDivider (inspectorSplitter, Divider::inspector);
    setUpDivider (stripSplitter, Divider::strip);

    exportSheet.onClose = [this] { grabKeyboardFocus(); };
    addChildComponent (exportSheet);

    loadLayouts();
    workspace = settings.getIntValue ("workspace", 0) == 1 ? Workspace::edit : Workspace::play;
    topBar.setWorkspace (workspace == Workspace::play ? 0 : 1);
    monitor.setPlaybackOnly (workspace == Workspace::edit);
    stage.setBalanceHeight (playLayout.balanceHeight);
    timeline.setTrackHeightsState (settings.getValue (juce::String ("layout.") + workspaceKey (workspace) + ".trackHeights"));

    if (! settings.getBoolValue ("hintWorkspacesSeen", false))
    {
        coachmark = std::make_unique<Coachmark>();
        coachmark->onDismiss = [this]
        {
            session.getSettings().setValue ("hintWorkspacesSeen", true);
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
            {
                if (safe != nullptr)
                    safe->coachmark.reset();
            });
        };
        addAndMakeVisible (*coachmark);
    }

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
    juce::Desktop::getInstance().removeDarkModeSettingListener (this);
    stopTimer();
    animator.cancelAllAnimations (false);
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

    updateCamera();

    topBar.refresh();
    timeline.refresh();
    monitor.setCountIn (session.getCountInBeatsLeft());
    monitor.refresh();
    if (stage.isVisible())
        stage.refresh();
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

void MainComponent::updateCamera()
{
    // Monter has no use for the live picture: the camera is switched off
    // (light off, CPU saved) — once a take in progress has been finalised —
    // and comes back with Jouer.
    if (workspace == Workspace::edit)
    {
        if (session.getCamera().isOpen() && ! session.isRecordingTake() && ! session.isFinishingTake())
            session.suspendCamera();
    }
    else if (session.isCameraSuspended())
    {
        session.resumeCamera();
    }

    // ● pressed in Monter: record once the camera is back (or gave up).
    if (pendingRecord && workspace == Workspace::play)
    {
        const bool cameraReady = session.getCamera().isOpen() || (! session.isCameraOpening() && ! session.isCameraSuspended());
        if (cameraReady || nowSeconds() - pendingRecordSince > 4.0)
        {
            pendingRecord = false;
            if (! session.isRecordingTake())
                session.toggleTakeRecording();
        }
    }
}

void MainComponent::recordTake()
{
    if (session.isRecordingTake() || session.isFinishingTake())
    {
        pendingRecord = false;
        session.toggleTakeRecording();
        return;
    }

    if (workspace == Workspace::edit)
    {
        setWorkspace (Workspace::play);
        pendingRecord = true;
        pendingRecordSince = nowSeconds();
        session.notify (tr ("Back in Play: the take starts as soon as the camera is ready."));
        return;
    }

    session.toggleTakeRecording();
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

MainComponent::AppearanceMode MainComponent::getAppearanceMode() const
{
    const auto saved = const_cast<Session&> (session).getSettings().getValue ("appearance");
    if (saved == "light") return AppearanceMode::light;
    if (saved == "dark") return AppearanceMode::dark;
    return AppearanceMode::system;
}

void MainComponent::setAppearanceMode (AppearanceMode mode)
{
    session.getSettings().setValue ("appearance", mode == AppearanceMode::light ? "light"
                                                  : mode == AppearanceMode::dark ? "dark" : "system");
    applyAppearance();
}

void MainComponent::applyAppearance()
{
    const auto mode = getAppearanceMode();
    const bool light = mode == AppearanceMode::light
                       || (mode == AppearanceMode::system && ! juce::Desktop::getInstance().isDarkModeActive());
    theme::setAppearance (light ? theme::Appearance::light : theme::Appearance::dark);
    lookAndFeel.applyTheme();
    sendLookAndFeelChange();
    repaint();
    menuItemsChanged();
}

void MainComponent::darkModeSettingChanged()
{
    if (getAppearanceMode() == AppearanceMode::system)
        applyAppearance();
}

void MainComponent::setLightAppearance (bool shouldBeLight)
{
    setAppearanceMode (shouldBeLight ? AppearanceMode::light : AppearanceMode::dark);
}

//==============================================================================
void MainComponent::setWorkspace (Workspace newWorkspace, bool animate)
{
    if (coachmark != nullptr && newWorkspace != workspace)
        coachmark->onDismiss();

    if (newWorkspace == workspace)
        return;

    workspace = newWorkspace;
    maximised = nullptr;
    session.getSettings().setValue ("workspace", workspace == Workspace::play ? 0 : 1);
    topBar.setWorkspace (workspace == Workspace::play ? 0 : 1);
    monitor.setPlaybackOnly (workspace == Workspace::edit);
    if (workspace == Workspace::edit)
        pendingRecord = false;
    // Each workspace has its own track heights (thin strip / roomy lanes).
    timeline.setTrackHeightsState (session.getSettings().getValue (juce::String ("layout.") + workspaceKey (workspace) + ".trackHeights",
                                                                   workspace == Workspace::play ? "92,92,40,92" : "92,92,52,92"));
    updateCamera();
    applyLayout (animate);
    menuItemsChanged();
}

MainComponent::Pane& MainComponent::paneFor (Divider divider)
{
    auto& l = layout();
    switch (divider)
    {
        case Divider::library:   return l.library;
        case Divider::inspector: return l.inspector;
        case Divider::side:      return l.side;
        case Divider::strip:
        default:                 return l.strip;
    }
}

void MainComponent::loadLayouts()
{
    // Monter starts with a wider right-hand column (monitor + inspector) and
    // a taller inspector inside it.
    editLayout.side = { "side", 380, 380, 280, 640 };
    editLayout.inspector = { "inspector", 320, 320, 160, 900 };

    auto& settings = session.getSettings();
    for (auto w : { Workspace::play, Workspace::edit })
    {
        auto& l = w == Workspace::play ? playLayout : editLayout;
        for (auto* pane : { &l.library, &l.inspector, &l.side, &l.strip })
        {
            const auto prefix = juce::String ("layout.") + workspaceKey (w) + "." + pane->key;
            pane->size = juce::jlimit (pane->minSize, pane->maxSize, settings.getIntValue (prefix + ".size", pane->defaultSize));
            pane->collapsed = settings.getBoolValue (prefix + ".collapsed", false);
        }
        if (w == Workspace::play)
            l.balanceHeight = settings.getIntValue ("layout.play.balance", StagePanel::defaultBalanceHeight);
    }
}

void MainComponent::saveLayout (Workspace w)
{
    auto& settings = session.getSettings();
    auto& l = w == Workspace::play ? playLayout : editLayout;
    for (auto* pane : { &l.library, &l.inspector, &l.side, &l.strip })
    {
        const auto prefix = juce::String ("layout.") + workspaceKey (w) + "." + pane->key;
        settings.setValue (prefix + ".size", pane->size);
        settings.setValue (prefix + ".collapsed", pane->collapsed);
    }
    if (w == Workspace::play)
        settings.setValue ("layout.play.balance", l.balanceHeight);
}

void MainComponent::resetLayout()
{
    auto& l = layout();
    for (auto* pane : { &l.library, &l.inspector, &l.side, &l.strip })
    {
        pane->size = pane->defaultSize;
        pane->collapsed = false;
    }
    l.balanceHeight = StagePanel::defaultBalanceHeight;
    if (workspace == Workspace::play)
        stage.setBalanceHeight (l.balanceHeight);
    maximised = nullptr;
    saveLayout (workspace);
    applyLayout (true);
    menuItemsChanged();
}

void MainComponent::setUpDivider (ui::Splitter& splitter, Divider divider)
{
    // The library grows to the right; the others grow away from the drag
    // direction (the side column to the left, the bottom panels upwards).
    const int sign = divider == Divider::library ? 1 : -1;

    splitter.onDragStart = [this, divider]
    {
        auto& pane = paneFor (divider);
        sizeAtDragStart = pane.collapsed ? 0 : pane.size;
        maximised = nullptr;
    };
    splitter.onDrag = [this, divider, sign] (int delta)
    {
        auto& pane = paneFor (divider);
        const int wanted = sizeAtDragStart + sign * delta;
        // Pulled below half its minimum: the panel folds away (its size is
        // kept for when it comes back).
        pane.collapsed = wanted < pane.minSize / 2;
        if (! pane.collapsed)
            pane.size = juce::jlimit (pane.minSize, pane.maxSize, wanted);
        applyLayout (false);
    };
    splitter.onDragEnd = [this]
    {
        saveLayout (workspace);
        topBar.setPanelsShown (isLibraryShown(), isInspectorShown());
        menuItemsChanged();
    };
    splitter.onDoubleClick = [this, divider]
    {
        auto& pane = paneFor (divider);
        pane.size = pane.defaultSize;
        pane.collapsed = false;
        saveLayout (workspace);
        applyLayout (false);
        topBar.setPanelsShown (isLibraryShown(), isInspectorShown());
    };
    addAndMakeVisible (splitter);
}

bool MainComponent::isLibraryShown() const noexcept { return ! layout().library.collapsed; }
bool MainComponent::isInspectorShown() const noexcept { return ! layout().inspector.collapsed; }

void MainComponent::setLibraryShown (bool shouldBeShown, bool remember)
{
    layout().library.collapsed = ! shouldBeShown;
    if (remember)
        saveLayout (workspace);
    applyLayout (false);
    menuItemsChanged();
}

void MainComponent::setInspectorShown (bool shouldBeShown, bool remember)
{
    layout().inspector.collapsed = ! shouldBeShown;
    if (remember)
        saveLayout (workspace);
    applyLayout (false);
    menuItemsChanged();
}

juce::Component* MainComponent::panelUnderMouse() const
{
    const auto mouse = getMouseXYRelative();
    for (auto* panel : std::initializer_list<const juce::Component*> { &monitor, &timeline, &library, &inspector, &stage })
        if (panel->isVisible() && panel->getBounds().contains (mouse))
            return const_cast<juce::Component*> (panel);
    return nullptr;
}

void MainComponent::toggleMaximisedPanel (juce::Component* panel)
{
    maximised = (panel == nullptr || panel == maximised) ? nullptr : panel;
    applyLayout (true);
    menuItemsChanged();
}

void MainComponent::applyLayout (bool animate)
{
    if (getWidth() <= 0)
        return;

    animator.cancelAllAnimations (true);

    auto area = getLocalBounds();
    topBar.setBounds (area.removeFromTop (TopBar::preferredHeight));
    exportSheet.setBounds (getLocalBounds());

    // Target bounds of each panel (empty: hidden).
    juce::Rectangle<int> libraryArea, monitorArea, timelineArea, stageArea, inspectorArea;
    auto& l = layout();
    const bool play = workspace == Workspace::play;
    const int t = ui::Splitter::thickness;

    struct DividerPlacement
    {
        bool visible = false, collapsed = false;
        juce::Rectangle<int> bounds;
    };
    DividerPlacement libraryDivider, sideDivider, inspectorDivider, stripDivider;

    if (maximised != nullptr)
    {
        // One panel takes everything below the top bar.
        if (maximised == &library) libraryArea = area;
        else if (maximised == &monitor) monitorArea = area;
        else if (maximised == &timeline) timelineArea = area;
        else if (maximised == &stage) stageArea = area;
        else if (maximised == &inspector) inspectorArea = area;
    }
    else
    {
        // Each panel gets its size, but never squeezes the centre (monitor
        // or timeline) below a usable minimum.
        const int minCentreWidth = 360, minCentreHeight = 160;
        auto fit = [] (const Pane& pane, int available) { return pane.collapsed ? 0 : juce::jlimit (0, std::max (0, available), pane.size); };

        // Jouer: the inspector spans the whole width at the bottom.
        if (play)
        {
            const int h = fit (l.inspector, area.getHeight() - minCentreHeight - 60);
            inspectorArea = area.removeFromBottom (h);
            inspectorDivider = { true, l.inspector.collapsed, { 0, area.getBottom() - t / 2, getWidth(), t } };
        }

        const int libraryWidth = fit (l.library, area.getWidth() - minCentreWidth - (l.side.collapsed ? 0 : l.side.minSize));
        libraryArea = area.removeFromLeft (libraryWidth);
        libraryDivider = { true, l.library.collapsed, { area.getX() - (libraryWidth > 0 ? t / 2 : 0), area.getY(), t, area.getHeight() } };

        const int sideWidth = fit (l.side, area.getWidth() - minCentreWidth);
        auto side = area.removeFromRight (sideWidth);
        sideDivider = { true, l.side.collapsed, { area.getRight() - (sideWidth > 0 ? t / 2 : t), area.getY(), t, area.getHeight() } };

        if (play)
        {
            stageArea = side;
            const int stripHeight = fit (l.strip, area.getHeight() - minCentreHeight);
            timelineArea = area.removeFromBottom (stripHeight);
            // Inset by a divider's width at both ends: the corners belong to
            // the vertical dividers.
            stripDivider = { true, l.strip.collapsed, { area.getX() + t, area.getBottom() - (stripHeight > 0 ? t / 2 : t), area.getWidth() - 2 * t, t } };
            monitorArea = area;
        }
        else
        {
            timelineArea = area;
            if (! side.isEmpty())
            {
                const int h = fit (l.inspector, side.getHeight() - 120);
                inspectorArea = side.removeFromBottom (h);
                inspectorDivider = { true, l.inspector.collapsed,
                                     { side.getX() + t, side.getBottom() - (h > 0 ? t / 2 : t), side.getWidth() - t, t } };
                monitorArea = side;
            }
        }
    }

    const std::pair<juce::Component*, juce::Rectangle<int>> targets[] = {
        { &library, libraryArea }, { &monitor, monitorArea }, { &timeline, timelineArea },
        { &stage, stageArea }, { &inspector, inspectorArea },
    };

    for (const auto& [component, target] : targets)
    {
        const bool show = ! target.isEmpty();
        if (! animate || ! isShowing())
        {
            component->setVisible (show);
            component->setAlpha (1.0f);
            if (show)
                component->setBounds (target);
            continue;
        }

        if (show && component->isVisible())
        {
            // Moves to its new place: the eye can follow it.
            animator.animateComponent (component, target, 1.0f, transitionMs, false, 0.6, 0.2);
        }
        else if (show)
        {
            component->setBounds (target);
            component->setAlpha (0.0f);
            component->setVisible (true);
            animator.animateComponent (component, target, 1.0f, transitionMs, false, 0.6, 0.2);
        }
        else if (component->isVisible())
        {
            animator.fadeOut (component, transitionMs / 2);
            component->setVisible (false);
        }
    }

    const std::pair<ui::Splitter*, DividerPlacement*> dividers[] = {
        { &librarySplitter, &libraryDivider }, { &sideSplitter, &sideDivider },
        { &inspectorSplitter, &inspectorDivider }, { &stripSplitter, &stripDivider },
    };
    for (const auto& [splitter, placement] : dividers)
    {
        splitter->setVisible (placement->visible);
        splitter->setCollapsed (placement->collapsed);
        splitter->setBounds (placement->bounds);
        splitter->toFront (false);
    }

    exportSheet.toFront (false);
    if (coachmark != nullptr)
    {
        const auto anchor = getWorkspaceSwitchBounds();
        coachmark->setBounds (anchor.getX() - 8, anchor.getBottom() + 4, 400, 150);
        coachmark->toFront (false);
    }

    topBar.setPanelsShown (isLibraryShown(), isInspectorShown());
    toastArea = juce::Rectangle<int> (10, 34).withCentre ({ getWidth() / 2, getHeight() - 30 });
    repaint();
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

// Asks for a YouTube link (the clipboard's one is offered), then downloads
// the audio and imports it as the backing track.
void MainComponent::chooseYouTubeSong()
{
    auto* window = new juce::AlertWindow (tr ("Song from YouTube"),
                                          tr ("Paste the video link: the audio is downloaded, then the tempo and parts are detected automatically."),
                                          juce::MessageBoxIconType::NoIcon, this);
    const auto clipboard = juce::SystemClipboard::getTextFromClipboard().trim();
    window->addTextEditor ("url", Session::isYouTubeUrl (clipboard) ? clipboard : juce::String(), tr ("YouTube link"));
    window->addButton (tr ("Download"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window] (int result)
    {
        if (result == 1)
            session.importSongFromUrl (window->getTextEditorContents ("url"));
    }), true);
}

// The language is read once at start-up, so the choice is saved and applies
// the next time Roadie is opened.
void MainComponent::chooseLanguage (i18n::Language language)
{
    if (language == i18n::getLanguage())
        return;

    i18n::saveLanguage (session.getSettings(), language);
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Language"),
                                            language == i18n::Language::french
                                                ? juce::String::fromUTF8 ("Red\xc3\xa9marre Roadie pour appliquer la nouvelle langue.")
                                                : juce::String ("Restart Roadie to apply the new language."),
                                            "OK", this);
}

void MainComponent::selectTrackByName (const juce::String& name)
{
    if (name == "loop")
        selectTrack (Track::loop);
    else if (name == "song")
        selectTrack (Track::song);
    else if (name == "camera")
        selectTrack (Track::camera);
    else
        selectTrack (Track::amp);
}

void MainComponent::selectTrack (Track track)
{
    if (track == Track::loop && ! session.isLooperTrackVisible())
        session.setLooperTrackVisible (true); // 2 on a project without a Looper adds one
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

    // Toast: short confirmations ("Take 3 recorded…") near the bottom.
    if (shownNotice.isEmpty())
        return;

    const auto font = fonts::semibold (13.0f);
    const int width = std::min (getWidth() - 40, (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, shownNotice)) + 48);
    const auto anchor = timeline.isVisible() ? timeline.getBounds() : getLocalBounds();
    toastArea = juce::Rectangle<int> (width, 34).withCentre ({ anchor.getCentreX(), anchor.getBottom() - 30 });

    g.setColour (surfaceRaised.withAlpha (0.97f));
    roadie::theme::fillSquircle (g, toastArea.toFloat(), 17.0f);
    g.setColour (border);
    roadie::theme::drawSquircle (g, toastArea.toFloat().reduced (0.5f), 17.0f, 1.0f);
    g.setColour (text);
    g.setFont (font);
    g.drawText (shownNotice, toastArea.reduced (18, 0), juce::Justification::centred, true);
}

void MainComponent::resized()
{
    applyLayout (false);
}

//==============================================================================
bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const auto code = key.getKeyCode();
    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());

    if (mods.isCommandDown())
    {
        if (code == '1' || c == '&') { setWorkspace (Workspace::play); return true; }
        if (code == '2' || c == 0xe9) { setWorkspace (Workspace::edit); return true; }
        if (code == 'N') { session.newProject(); return true; }
        if (code == 'O') { chooseProjectToOpen(); return true; }
        if (code == 'S' && mods.isShiftDown()) { chooseSaveAs(); return true; }
        if (code == 'S') { if (session.saveProject()) session.notify (tr ("Project saved")); return true; }
        if (code == 'E') { openExportSheet(); return true; }
        if (code == 'I') { if (mods.isShiftDown()) chooseYouTubeSong(); else chooseSong(); return true; }
        if (mods.isAltDown() && code == juce::KeyPress::upKey)   { timeline.zoomTracks (1.25f); return true; }
        if (mods.isAltDown() && code == juce::KeyPress::downKey) { timeline.zoomTracks (0.8f); return true; }
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
    if (c == 't')                                        { recordTake(); return true; }
    if (c == 'm')                                        { session.setMetronomeOn (! session.isMetronomeOn()); return true; }
    if (c == 'y')                                        { setLibraryShown (! isLibraryShown()); return true; }
    if (c == 'b')                                        { setInspectorShown (! isInspectorShown()); return true; }
    if (c == '`' || c == '<' || c == '@')                { toggleMaximisedPanel (maximised != nullptr ? maximised : panelUnderMouse()); return true; }
    if (c == '1')                                        { selectTrack (Track::amp); return true; }
    if (c == '2')                                        { selectTrack (Track::loop); return true; }
    if (c == '3')                                        { selectTrack (Track::camera); return true; }
    if (c == '4')                                        { selectTrack (Track::song); return true; }
    if (key == juce::KeyPress::escapeKey)
    {
        if (maximised != nullptr)
            toggleMaximisedPanel (nullptr);
        else
            session.stopAll();
        return true;
    }

    return false;
}

//==============================================================================
juce::StringArray MainComponent::getMenuBarNames()
{
    return { tr ("File"), tr ("Edit"), "Transport", "Audio", tr ("View") };
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
        item (newProject, tr ("New Project"), utf8 ("\xe2\x8c\x98N"));
        item (openProject, tr ("Open…"), utf8 ("\xe2\x8c\x98O"));
        menu.addSeparator();
        item (saveProject, tr ("Save"), utf8 ("\xe2\x8c\x98S"));
        item (saveProjectAs, tr ("Save As…"), utf8 ("\xe2\x87\xa7\xe2\x8c\x98S"));
        item (revealProject, tr ("Show Project in Finder"), {}, session.hasProjectOnDisk());
        menu.addSeparator();
        item (importSongItem, tr ("Import Song…"), utf8 ("\xe2\x8c\x98I"), ! session.isImportingSong());
        item (importYouTubeItem, tr ("Import from YouTube…"), utf8 ("\xe2\x87\xa7\xe2\x8c\x98I"), ! session.isImportingSong() && ! session.isDownloadingSong());
        item (exportClip, tr ("Export Clip…"), utf8 ("\xe2\x8c\x98" "E"));
    }
    else if (index == 1)
    {
        const bool hasSelection = session.getSelection() != Session::Selection::none;
        item (undoEdit, session.canUndoEdit() ? tr ("Undo: ") + session.getUndoEditName() : tr ("Undo"),
              utf8 ("\xe2\x8c\x98Z"), session.canUndoEdit());
        item (redoEdit, session.canRedoEdit() ? tr ("Redo: ") + session.getRedoEditName() : tr ("Redo"),
              utf8 ("\xe2\x87\xa7\xe2\x8c\x98Z"), session.canRedoEdit());
        menu.addSeparator();
        item (splitRegion, tr ("Split at Playhead"), utf8 ("\xe2\x8c\x98T"));
        item (duplicateRegion, tr ("Duplicate region"), utf8 ("\xe2\x8c\x98" "D"), hasSelection);
        item (deleteRegion, tr ("Delete region"), utf8 ("\xe2\x8c\xab"), hasSelection);
        item (selectNone, tr ("Deselect All"), {}, hasSelection);
        menu.addSeparator();
        item (cycle, tr ("Cycle Zone"), "C", true, session.isCycleOn());
    }
    else if (index == 2)
    {
        const auto state = session.getEngine().getLooper().getState();
        item (playPause, tr ("Play / Pause"), tr ("Space"));
        item (returnToStart, tr ("Return to Start"), tr ("Input"));
        menu.addSeparator();
        item (recordTakeItem, session.isRecordingTake() ? tr ("Stop Take") : tr ("Record Take (audio + camera)"), "T");
        item (countInItem, tr ("One-Bar Count-In Before Take"), {}, true, session.isCountInOn());
        menu.addSeparator();
        {
            const bool looperShown = session.isLooperTrackVisible();
            item (toggleLooperTrack, looperShown ? tr ("Remove Looper Track") : tr ("Add Looper Track"), {},
                  ! looperShown || state == Looper::State::Empty);
        }
        item (loopCountInItem, tr ("Count-In Before Loop"), {}, true, session.isLoopCountInOn());
        {
            juce::PopupMenu lengths;
            for (int i = 0; i < (int) std::size (loopBarChoices); ++i)
                lengths.addItem (makeItem (loopBarsBase + i, loopBarChoices[i] == 0 ? juce::String (tr ("Free"))
                                                                                   : juce::String (loopBarChoices[i]) + (loopBarChoices[i] > 1 ? tr (" bars") : tr (" bar")),
                                           {}, true, session.getLoopBars() == loopBarChoices[i]));
            menu.addSubMenu (tr ("Loop Length"), lengths);
        }
        item (loopRecord, tr ("Loop: Record / Loop"), "R");
        item (overdub, "Overdub", "O", state != Looper::State::Empty && state != Looper::State::Recording);
        item (undoOverdub, tr ("Remove Layer"), "U", session.getEngine().getLooper().isUndoAvailable());
        item (clearLoop, tr ("Clear Loop"), utf8 ("\xe2\x87\xa7\xe2\x8c\xab"), state != Looper::State::Empty);
        menu.addSeparator();
        item (metronome, tr ("Metronome"), "M", true, session.isMetronomeOn());
    }
    else if (index == 3)
    {
        item (audioSettings, tr ("Audio Settings…"), utf8 ("\xe2\x8c\x98,"));
    }
    else
    {
        item (workspacePlay, tr ("Play"), utf8 ("\xe2\x8c\x98" "1"), true, workspace == Workspace::play);
        item (workspaceEdit, tr ("Arrange"), utf8 ("\xe2\x8c\x98" "2"), true, workspace == Workspace::edit);
        menu.addSeparator();
        item (showLibrary, tr ("Library"), "Y", true, isLibraryShown());
        item (showInspector, tr ("Inspector"), "B", true, isInspectorShown());
        item (maximisePanel, maximised != nullptr ? tr ("Restore Panels") : tr ("Enlarge Panel Under Mouse"), "`");
        item (resetLayoutItem, tr ("Reset Layout"), {});
        menu.addSeparator();
        item (magnetItem, tr ("Playhead Magnet"), {}, true, session.isMagnetOn());
        item (zoomIn, tr ("Zoom In"), utf8 ("\xe2\x8c\x98\xe2\x86\x92"));
        item (zoomOut, tr ("Zoom Out"), utf8 ("\xe2\x8c\x98\xe2\x86\x90"));
        item (zoomFit, tr ("Fit Song to Screen"), {});
        item (tracksTaller, tr ("Taller Tracks"), utf8 ("\xe2\x8c\xa5\xe2\x8c\x98\xe2\x86\x91"));
        item (tracksShorter, tr ("Shorter Tracks"), utf8 ("\xe2\x8c\xa5\xe2\x8c\x98\xe2\x86\x93"));
        menu.addSeparator();

        juce::PopupMenu sizes;
        const float current = juce::Desktop::getInstance().getGlobalScaleFactor();
        for (int i = 0; i < (int) std::size (uiScales); ++i)
            sizes.addItem (makeItem (uiScaleBase + i, juce::String (juce::roundToInt (uiScales[i] * 100.0f)) + i18n::percent(), {}, true,
                                     std::abs (uiScales[i] - current) < 0.01f));
        menu.addSubMenu (tr ("Interface Size"), sizes);
        menu.addSeparator();
        juce::PopupMenu themes;
        const auto mode = getAppearanceMode();
        themes.addItem (makeItem (appearanceSystem, tr ("Automatic (System)"), {}, true, mode == AppearanceMode::system));
        themes.addItem (makeItem (appearanceLight, tr ("Light"), {}, true, mode == AppearanceMode::light));
        themes.addItem (makeItem (appearanceDark, tr ("Dark"), {}, true, mode == AppearanceMode::dark));
        menu.addSubMenu (tr ("Theme"), themes);
        juce::PopupMenu languages;
        const auto language = i18n::getLanguage();
        languages.addItem (makeItem (languageEnglish, "English", {}, true, language == i18n::Language::english));
        languages.addItem (makeItem (languageFrench, juce::String::fromUTF8 ("Fran\xc3\xa7" "ais"), {}, true, language == i18n::Language::french));
        menu.addSubMenu (tr ("Language"), languages);
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
        case saveProject:     if (session.saveProject()) session.notify (tr ("Project saved")); break;
        case saveProjectAs:   chooseSaveAs(); break;
        case revealProject:   session.getProject().folder.revealToUser(); break;
        case exportClip:      openExportSheet(); break;
        case importSongItem:  chooseSong(); break;
        case importYouTubeItem: chooseYouTubeSong(); break;
        case loopCountInItem: session.setLoopCountInOn (! session.isLoopCountInOn()); break;
        case magnetItem:      session.setMagnetOn (! session.isMagnetOn()); break;
        case tracksTaller:    timeline.zoomTracks (1.25f); break;
        case tracksShorter:   timeline.zoomTracks (0.8f); break;
        case undoEdit:        session.undoEdit(); break;
        case redoEdit:        session.redoEdit(); break;
        case splitRegion:     session.splitAtPlayhead(); break;
        case duplicateRegion: session.duplicateSelection(); break;
        case deleteRegion:    session.deleteSelection(); break;
        case selectNone:      session.clearSelection(); break;
        case cycle:           session.toggleCycle(); break;
        case playPause:       session.togglePlay(); break;
        case recordTakeItem:  recordTake(); break;
        case countInItem:     session.setCountInOn (! session.isCountInOn()); break;
        case toggleLooperTrack: session.setLooperTrackVisible (! session.isLooperTrackVisible()); break;
        case loopRecord:      session.recordLoop(); break;
        case overdub:         session.toggleOverdub(); break;
        case undoOverdub:     session.undo(); break;
        case clearLoop:       session.clearLoop(); break;
        case returnToStart:   session.returnToStart(); break;
        case metronome:       session.setMetronomeOn (! session.isMetronomeOn()); break;
        case audioSettings:   openAudioSettings(); break;
        case workspacePlay:   setWorkspace (Workspace::play); break;
        case workspaceEdit:   setWorkspace (Workspace::edit); break;
        case showLibrary:     setLibraryShown (! isLibraryShown()); break;
        case showInspector:   setInspectorShown (! isInspectorShown()); break;
        case maximisePanel:   toggleMaximisedPanel (maximised != nullptr ? maximised : (panelUnderMouse() != nullptr ? panelUnderMouse() : (workspace == Workspace::play ? (juce::Component*) &monitor : &timeline))); break;
        case resetLayoutItem: resetLayout(); break;
        case zoomIn:          timeline.zoomBy (1.5); break;
        case zoomOut:         timeline.zoomBy (1.0 / 1.5); break;
        case zoomFit:         timeline.zoomToFit(); break;
        case lightAppearance: setLightAppearance (theme::getAppearance() != theme::Appearance::light); break;
        case appearanceSystem: setAppearanceMode (AppearanceMode::system); break;
        case appearanceLight:  setAppearanceMode (AppearanceMode::light); break;
        case appearanceDark:   setAppearanceMode (AppearanceMode::dark); break;
        case languageEnglish:  chooseLanguage (i18n::Language::english); break;
        case languageFrench:   chooseLanguage (i18n::Language::french); break;
        default:
            if (id >= loopBarsBase && id < loopBarsBase + (int) std::size (loopBarChoices))
                session.setLoopBars (loopBarChoices[id - loopBarsBase]);
            break;
    }
}

//==============================================================================
bool MainComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& path : files)
    {
        const juce::File f (path);
        if (f.hasFileExtension (".nam;.wav;.aif;.aiff;.json;.mp3;.m4a;.aac;.flac;.caf;.mp4") || f.hasFileExtension (Project::extension) || f.isDirectory())
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
        else if (Session::isSongFile (f))
        {
            session.importSong (f);
            selectTrack (Track::song);
        }
        else if (f.hasFileExtension (".wav;.aif;.aiff"))
            session.loadImpulseResponse (f); // a short file: a cab IR
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
namespace
{
// Audio Settings: JUCE's device selector, plus the recording-delay
// correction for drivers that under-report their latency.
class AudioSettingsPanel : public juce::Component,
                           private juce::Timer
{
public:
    explicit AudioSettingsPanel (Session& s)
        : session (s), selector (s.getDeviceManager(), 1, 2, 2, 2, false, false, true, false)
    {
        addAndMakeVisible (selector);

        title.setText (tr ("Latency compensation"), juce::dontSendNotification);
        title.setFont (fonts::semibold (13.0f));
        addAndMakeVisible (title);

        adjustment.setSliderStyle (juce::Slider::LinearHorizontal);
        adjustment.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
        adjustment.setRange (-50.0, 100.0, 0.5);
        adjustment.setTextValueSuffix (" ms");
        adjustment.setDoubleClickReturnValue (true, 0.0);
        adjustment.setValue (session.getLatencyAdjustmentMs(), juce::dontSendNotification);
        adjustment.onValueChange = [this] { session.setLatencyAdjustmentMs (adjustment.getValue()); update(); };
        addAndMakeVisible (adjustment);

        explanation.setFont (fonts::body (12.0f));
        explanation.setColour (juce::Label::textColourId, theme::textMuted);
        explanation.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (explanation);

        update();
        startTimer (500);
        setSize (480, 540);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (0, 8);
        auto bottom = area.removeFromBottom (100).reduced (16, 0);
        selector.setBounds (area);
        title.setBounds (bottom.removeFromTop (22));
        adjustment.setBounds (bottom.removeFromTop (28));
        explanation.setBounds (bottom.withTrimmedTop (4));
    }

private:
    void timerCallback() override { update(); }

    void update()
    {
        const auto& engine = session.getEngine();
        const double total = engine.getRecordLatencySamples() / engine.getSampleRate() * 1000.0;
        explanation.setText (tr ("Round trip reported by the interface: ") + juce::String (engine.getRoundTripLatencyMs(), 1)
                                 + tr (" ms. Takes and loops are shifted by ") + juce::String (total, 1)
                                 + tr (" ms, to where you played them. Add the difference if they still land late."),
                             juce::dontSendNotification);
    }

    Session& session;
    juce::AudioDeviceSelectorComponent selector;
    juce::Label title, explanation;
    juce::Slider adjustment;
};
} // namespace

void MainComponent::openAudioSettings()
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new AudioSettingsPanel (session));
    options.dialogTitle = tr ("Audio Settings");
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
    chooser = std::make_unique<juce::FileChooser> (tr ("Open a Roadie project"), Project::getDefaultProjectsFolder(), "*.roadie;project.json");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f != juce::File())
                                  if (session.openProject (f))
                                      session.notify (tr ("Project opened: ") + session.getProjectName());
                          });
}

void MainComponent::chooseSong()
{
    const auto start = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    chooser = std::make_unique<juce::FileChooser> (tr ("Choose a song to play along to"), start, "*.mp3;*.m4a;*.aac;*.wav;*.aif;*.aiff;*.flac;*.caf");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f.existsAsFile())
        {
            session.importSong (f);
            selectTrack (Track::song);
        }
    });
}

void MainComponent::chooseSaveAs()
{
    const auto start = Project::getDefaultProjectsFolder().getChildFile (session.getProjectName() + Project::extension);
    Project::getDefaultProjectsFolder().createDirectory();
    chooser = std::make_unique<juce::FileChooser> (tr ("Save the project as"), start, "*.roadie");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode, [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f != juce::File())
            session.saveProjectAs (f);
    });
}
