#include "TopBar.h"

#include "../Tuner.h"

#include <algorithm>

using namespace roadie;

namespace
{
juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }
} // namespace

TopBar::Lcd::Lcd (Session& s) : session (s)
{
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
}

void TopBar::Lcd::paint (juce::Graphics& g)
{
    using namespace theme;
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (surfaceSunken);
    roadie::theme::fillSquircle (g, bounds, radiusSm);
    g.setColour (border);
    roadie::theme::drawSquircle (g, bounds, radiusSm, 1.0f);

    const juce::String labels[] = { "Mesure", "Tempo", "Signature" };
    const juce::String values[] = { position,
                                    juce::String (juce::roundToInt (session.getBpm())),
                                    juce::String (session.getBeatsPerBar()) + "/4" };

    for (int i = 0; i < 3; ++i)
    {
        auto c = cell (i).toFloat();

        // Tempo and signature are editable: they light up under the mouse.
        const float hover = cellFades[i].get (i > 0 && (hoveredCell == i || (i == 1 && draggingTempo)));
        if (hover > 0.0f)
        {
            g.setColour (text.withAlpha (0.05f * hover));
            roadie::theme::fillSquircle (g, c.reduced (2.0f), radiusSm - 2.0f);
        }

        if (i > 0)
        {
            g.setColour (border);
            g.drawVerticalLine ((int) c.getX(), bounds.getY() + 1.0f, bounds.getBottom() - 1.0f);
        }

        c = c.reduced (6.0f, 4.0f);
        g.setColour (textFaint.interpolatedWith (textMuted, hover));
        g.setFont (fonts::semibold (8.5f).withExtraKerningFactor (0.06f));
        g.drawText (labels[i].toUpperCase(), c.removeFromTop (10.0f), juce::Justification::centred, false);

        g.setColour (i == 1 && draggingTempo ? ink (exportAccent) : text);
        g.setFont (fonts::display (13.0f));
        fonts::drawTabular (g, values[i], c, juce::Justification::centred);
    }
}

void TopBar::Lcd::mouseMove (const juce::MouseEvent& e)
{
    const int index = e.x / cellWidth;
    if (index != hoveredCell)
    {
        hoveredCell = index;
        repaint();
    }
    setMouseCursor (index == 1 ? juce::MouseCursor::UpDownResizeCursor
                               : (index == 2 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor));
    setTooltip (index == 1 ? utf8 ("Tempo : glisse vers le haut/bas, double-clic pour saisir")
                           : (index == 2 ? utf8 ("Clique pour choisir la signature")
                                         : utf8 ("Position dans la mesure (mesure.temps)")));
}

void TopBar::Lcd::mouseExit (const juce::MouseEvent&)
{
    hoveredCell = -1;
    repaint();
}

void TopBar::Lcd::mouseDown (const juce::MouseEvent& e)
{
    const int index = e.x / cellWidth;
    if (index == 1)
    {
        draggingTempo = true;
        dragStartBpm = session.getBpm();
        repaint();
    }
    else if (index == 2)
    {
        juce::PopupMenu menu;
        for (int beats = 2; beats <= 7; ++beats)
            menu.addItem (beats, juce::String (beats) + "/4", true, beats == session.getBeatsPerBar());
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (cell (2))),
                            [this] (int result)
                            {
                                if (result > 0)
                                    session.setBeatsPerBar (result);
                                repaint();
                            });
    }
}

void TopBar::Lcd::mouseDrag (const juce::MouseEvent& e)
{
    if (! draggingTempo)
        return;
    const double delta = -e.getDistanceFromDragStartY() / (e.mods.isShiftDown() ? 12.0 : 3.0);
    session.setBpm (std::round (dragStartBpm + delta));
    repaint();
}

void TopBar::Lcd::mouseUp (const juce::MouseEvent&)
{
    draggingTempo = false;
    repaint();
}

void TopBar::Lcd::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.x / cellWidth != 1)
        return;

    auto* editor = new juce::TextEditor();
    editor->setInputRestrictions (3, "0123456789");
    editor->setJustification (juce::Justification::centred);
    editor->setFont (fonts::display (13.0f));
    editor->setText (juce::String (juce::roundToInt (session.getBpm())), false);
    editor->selectAll();
    editor->setBounds (cell (1).reduced (6, 0).withY (13).withHeight (getHeight() - 16));

    auto commit = [this, editor]
    {
        const int value = editor->getText().getIntValue();
        if (value > 0)
            session.setBpm (value);
        juce::MessageManager::callAsync ([editor] { delete editor; });
        repaint();
    };
    editor->onReturnKey = commit;
    editor->onFocusLost = commit;
    editor->onEscapeKey = [editor] { juce::MessageManager::callAsync ([editor] { delete editor; }); };

    addAndMakeVisible (editor);
    editor->grabKeyboardFocus();
}

//==============================================================================
// The tuner, in a bubble under its button: the note, a needle on ±50 cents,
// green within 3 cents. Reads the dry input while it is open.
class TunerView : public juce::Component,
                  private juce::Timer
{
public:
    explicit TunerView (Session& s) : session (s)
    {
        setSize (300, 190);
        startTimerHz (20);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        auto area = getLocalBounds().toFloat().reduced (16.0f, 12.0f);
        ui::drawSectionLabel (g, "Accordeur", area.removeFromTop (16.0f));

        const bool inTune = has && std::abs (cents) < 3.0;
        const auto colour = ! has ? textFaint : inTune ? ok : warn;

        // Note.
        auto noteArea = area.removeFromTop (64.0f);
        g.setColour (has ? ink (colour) : textFaint);
        g.setFont (fonts::display (48.0f));
        g.drawText (has ? note : utf8 ("\xe2\x80\x93"), noteArea, juce::Justification::centred, false);
        if (has)
        {
            g.setFont (fonts::semibold (13.0f));
            g.setColour (textMuted);
            g.drawText (juce::String (octave), noteArea.withTrimmedLeft (noteArea.getWidth() * 0.5f + 26.0f).withTrimmedTop (30.0f),
                        juce::Justification::centredLeft, false);
        }

        // Scale: −50 … 0 … +50, the needle on it.
        auto scale = area.removeFromTop (34.0f).reduced (10.0f, 0.0f);
        const float mid = scale.getCentreX(), y = scale.getCentreY();
        for (int c = -50; c <= 50; c += 10)
        {
            const float x = mid + (float) c / 50.0f * scale.getWidth() * 0.5f;
            g.setColour (c == 0 ? textMuted : border);
            g.fillRect (juce::Rectangle<float> (x - 0.5f, y - (c == 0 ? 10.0f : 5.0f), 1.0f, c == 0 ? 20.0f : 10.0f));
        }
        g.setColour (ok.withAlpha (0.15f));
        roadie::theme::fillSquircle (g, juce::Rectangle<float> (scale.getWidth() * 0.06f, 22.0f).withCentre ({ mid, y }), 4.0f);
        if (has)
        {
            const float x = mid + (float) juce::jlimit (-50.0, 50.0, cents) / 50.0f * scale.getWidth() * 0.5f;
            g.setColour (colour);
            roadie::theme::fillSquircle (g, juce::Rectangle<float> (4.0f, 26.0f).withCentre ({ x, y }), 2.0f);
        }

        g.setColour (has && ! inTune ? ink (warn) : textMuted);
        g.setFont (fonts::medium (12.0f));
        const auto detail = has ? juce::String (hz, 1).replaceCharacter ('.', ',') + utf8 (" Hz \xc2\xb7 ")
                                      + (cents >= 0.0 ? "+" : juce::String::fromUTF8 ("\xe2\x88\x92")) + juce::String (std::abs (cents), 0) + " cents"
                                : utf8 ("Joue une corde \xc3\xa0 vide");
        g.drawText (detail, area, juce::Justification::centred, false);
    }

private:
    void timerCallback() override
    {
        // 4096 samples (85 ms): several periods even of a low E, so the
        // reading holds still instead of trembling with each pluck.
        std::vector<float> samples (4096);
        auto& engine = session.getEngine();
        engine.copyRecentInput (samples.data(), (int) samples.size());
        const auto reading = roadie::tuner::detect (samples.data(), (int) samples.size(), engine.getSampleRate());
        if (reading.ok)
        {
            silentFrames = 0;
            const auto name = juce::String (reading.note);

            // A new note must be heard three times in a row before the
            // display jumps to it (a wobbly attack never flips the note).
            if (! has || (name == note && reading.octave == octave))
            {
                candidateFrames = 0;
            }
            else
            {
                const bool sameCandidate = name == candidate && reading.octave == candidateOctave;
                candidateFrames = sameCandidate ? candidateFrames + 1 : 1;
                candidate = name;
                candidateOctave = reading.octave;
                if (candidateFrames < 3)
                {
                    repaint();
                    return;
                }
                has = false;
                recentCents.clear();
            }

            // Median of the last few readings, then a glide: steady needle.
            recentCents.push_back (reading.cents);
            if (recentCents.size() > 5)
                recentCents.erase (recentCents.begin());
            auto sorted = recentCents;
            std::sort (sorted.begin(), sorted.end());
            const double median = sorted[sorted.size() / 2];
            cents = has ? cents * 0.5 + median * 0.5 : median;
            hz = has ? hz * 0.5 + reading.hz * 0.5 : reading.hz;
            note = name;
            octave = reading.octave;
            has = true;
        }
        else if (++silentFrames > 12)
        {
            has = false;
            recentCents.clear();
            candidateFrames = 0;
        }
        repaint();
    }

    Session& session;
    bool has = false;
    juce::String note;
    int octave = 0, silentFrames = 0, candidateFrames = 0, candidateOctave = 0;
    juce::String candidate;
    std::vector<double> recentCents;
    double cents = 0.0, hz = 0.0;
};

void TopBar::showTuner()
{
    juce::CallOutBox::launchAsynchronously (std::make_unique<TunerView> (session), tunerButton.getScreenBounds(), nullptr);
}

//==============================================================================
TopBar::TopBar (Session& s)
    : session (s),
      lcd (s),
      workspaceSwitch ({ { "Jouer", icons::Icon::guitar, utf8 ("Jouer : moniteur, p\xc3\xa9" "dale, balance \xe2\x80\x94 pour enregistrer (\xe2\x8c\x98" "1)") },
                         { "Monter", icons::Icon::timeline, utf8 ("Monter : la timeline en grand, la cam\xc3\xa9ra coup\xc3\xa9" "e (\xe2\x8c\x98" "2)") } }),
      exportButton ("Exporter", ui::PillButton::Variant::tinted, theme::exportAccent, icons::Icon::exportArrow)
{
    previousButton.setTooltip (utf8 ("Retour au d\xc3\xa9" "but de la boucle (Entr\xc3\xa9" "e)"));
    playButton.setTooltip (utf8 ("Lecture / pause (Espace)"));
    stopButton.setTooltip (utf8 ("Tout arr\xc3\xaa" "ter"));
    recordButton.setTooltip (utf8 ("Enregistrer une prise : audio + cam\xc3\xa9ra (T)"));
    cycleButton.setTooltip (utf8 ("Zone de cycle : la lecture r\xc3\xa9p\xc3\xa8te la bande jaune de la r\xc3\xa8gle (C)"));
    cycleButton.setAccent (theme::exportAccent);

    previousButton.onClick = [this] { session.returnToStart(); };
    playButton.onClick = [this] { session.togglePlay(); };
    stopButton.onClick = [this] { session.stopAll(); };
    recordButton.onClick = [this] { if (onRecord) onRecord(); else session.toggleTakeRecording(); };
    cycleButton.onClick = [this] { session.toggleCycle(); };

    for (auto* b : { &previousButton, &playButton, &stopButton, &recordButton, &cycleButton })
        addAndMakeVisible (b);

    workspaceSwitch.onChange = [this] (int index) { if (onWorkspaceChanged) onWorkspaceChanged (index); };
    addAndMakeVisible (workspaceSwitch);

    libraryButton.setTooltip (utf8 ("Afficher / masquer la biblioth\xc3\xa8que (Y)"));
    libraryButton.onClick = [this] { if (onToggleLibrary) onToggleLibrary(); };
    inspectorButton.setTooltip (utf8 ("Afficher / masquer l'inspecteur (B)"));
    inspectorButton.onClick = [this] { if (onToggleInspector) onToggleInspector(); };
    addAndMakeVisible (libraryButton);
    addAndMakeVisible (inspectorButton);

    addAndMakeVisible (lcd);

    metronomeButton.setTooltip (utf8 ("M\xc3\xa9tronome, cal\xc3\xa9 sur le transport (M)"));
    metronomeButton.onClick = [this] { session.setMetronomeOn (! session.isMetronomeOn()); };
    addAndMakeVisible (metronomeButton);

    tunerButton.setTooltip (utf8 ("Accordeur"));
    tunerButton.onClick = [this] { showTuner(); };
    addAndMakeVisible (tunerButton);

    inputMeter.setTooltip (utf8 ("Niveau d'entr\xc3\xa9" "e de la guitare"));
    addAndMakeVisible (inputMeter);
    statusPill.onClick = [this] { if (onOpenSettings) onOpenSettings(); };
    addAndMakeVisible (statusPill);

    settingsButton.setTooltip (utf8 ("Carte son, fr\xc3\xa9quence, taille de buffer et compensation de latence (\xe2\x8c\x98,)"));
    settingsButton.onClick = [this] { if (onOpenSettings) onOpenSettings(); };
    addAndMakeVisible (settingsButton);

    exportButton.setFullyRounded (true);
    exportButton.setFontSize (12.5f);
    exportButton.setTooltip (utf8 ("Exporter un clip vid\xc3\xa9o ou l'audio d'une prise (\xe2\x8c\x98" "E)"));
    exportButton.onClick = [this] { if (onOpenExport) onOpenExport(); };
    addAndMakeVisible (exportButton);

    statusPill.setContent ({}, "--", theme::ok);
}

void TopBar::setPanelsShown (bool library, bool inspector)
{
    libraryButton.setOn (library);
    inspectorButton.setOn (inspector);
}

void TopBar::refresh()
{
    auto& engine = session.getEngine();
    const auto& looper = engine.getLooper();
    const auto state = looper.getState();

    const bool playing = engine.isTransportRunning()
                         && (state == Looper::State::Playing || state == Looper::State::Overdubbing
                             || (state == Looper::State::Empty && engine.isTransportRunning()));
    playButton.setIcon (playing ? icons::Icon::pause : icons::Icon::play);
    playButton.setOn (playing);

    recordButton.setOn (session.isRecordingTake());
    recordButton.setBlinking (session.isRecordingTake());
    recordButton.setEnabled (! session.isFinishingTake());

    cycleButton.setOn (session.isCycleOn());
    metronomeButton.setOn (session.isMetronomeOn());

    // Bar.beat from the transport position.
    const double samplesPerBeat = engine.getMetronome().getSamplesPerBeat();
    const auto pos = engine.getTransportPosition();
    const auto beatIndex = (std::int64_t) (samplesPerBeat > 0.0 ? (double) pos / samplesPerBeat : 0.0);
    const int beatsPerBar = session.getBeatsPerBar();
    lcd.setPosition (juce::String (beatIndex / beatsPerBar + 1) + "." + juce::String (beatIndex % beatsPerBar + 1));
    lcd.repaint();

    inputMeter.setLevel (engine.getInputPeak());

    const auto cpu = session.getDeviceManager().getCpuUsage();
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    cpuLoad.push (cpu, lastCpuReading > 0.0 ? now - lastCpuReading : 0.0);
    lastCpuReading = now;
    // One pill: the latency the player feels, then the CPU in a word. The
    // dot is the worse of the two.
    const juce::String cpuWords[] = { "CPU faible", "CPU normal", utf8 ("CPU \xc3\xa9lev\xc3\xa9") };
    const juce::Colour colours[] = { theme::ok, theme::warn, theme::rec };
    const auto level = (int) cpuLoad.getLevel();
    const auto latency = engine.getRoundTripLatencyMs();
    const int latencyLevel = latency < 12.0 ? 0 : (latency < 25.0 ? 1 : 2);
    if (session.getDeviceManager().getCurrentAudioDevice() != nullptr)
        statusPill.setContent ({}, juce::String (juce::roundToInt (latency)) + utf8 (" ms \xc2\xb7 ") + cpuWords[level], colours[std::max (level, latencyLevel)]);
    else
        statusPill.setContent ({}, utf8 ("Audio coup\xc3\xa9"), theme::rec);
    statusPill.setTooltip (utf8 ("Latence aller-retour (guitare \xe2\x86\x92 oreilles) : ") + juce::String (latency, 1)
                           + utf8 (" ms. Charge audio : ") + juce::String (juce::roundToInt (cpuLoad.getAverage() * 100.0))
                           + utf8 ("\xc2\xa0% en moyenne (") + juce::String (juce::roundToInt (cpu * 100.0))
                           + utf8 ("\xc2\xa0% \xc3\xa0 l'instant). Clique pour les r\xc3\xa9glages audio."));

    const auto project = session.getProjectName() + (session.hasUnsavedChanges() ? utf8 (" \xc2\xb7 modifi\xc3\xa9") : utf8 (" \xc2\xb7 enregistr\xc3\xa9"));
    if (project != shownProject)
    {
        shownProject = project;
        repaint (projectArea);
    }

    for (auto* b : { &previousButton, &playButton, &stopButton, &recordButton })
        b->repaint();
}

void TopBar::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (surface);
    g.setColour (border);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    // Project: its name, and whether it has unsaved changes.
    if (! compact && ! projectArea.isEmpty())
    {
        const auto name = session.getProjectName();
        const auto state = session.hasUnsavedChanges() ? utf8 ("modifi\xc3\xa9") : utf8 ("enregistr\xc3\xa9");
        const auto nameFont = fonts::semibold (13.0f), stateFont = fonts::medium (11.5f);
        const float stateWidth = juce::GlyphArrangement::getStringWidth (stateFont, state) + 16.0f;
        auto area = projectArea.toFloat();
        const float nameWidth = std::min (area.getWidth() - stateWidth, juce::GlyphArrangement::getStringWidth (nameFont, name) + 2.0f);
        auto row = area.withSizeKeepingCentre (nameWidth + stateWidth, area.getHeight());
        g.setColour (text);
        g.setFont (nameFont);
        g.drawText (name, row.removeFromLeft (nameWidth), juce::Justification::centredLeft, true);
        row.removeFromLeft (8.0f);
        g.setColour (session.hasUnsavedChanges() ? ink (warn) : textFaint);
        g.fillEllipse (row.removeFromLeft (5.0f).withSizeKeepingCentre (5.0f, 5.0f));
        row.removeFromLeft (3.0f);
        g.setColour (textMuted);
        g.setFont (stateFont);
        g.drawText (state, row, juce::Justification::centredLeft, false);
    }

    // Transport cluster capsule.
    auto cluster = clusterArea.toFloat().reduced (0.5f);
    g.setColour (surfaceSunken);
    roadie::theme::fillSquircle (g, cluster, cluster.getHeight() * 0.5f);
    g.setColour (border);
    roadie::theme::drawSquircle (g, cluster, cluster.getHeight() * 0.5f, 1.0f);

    icons::draw (g, icons::Icon::gauge, meterIconArea.toFloat(), textFaint);
}

void TopBar::resized()
{
    auto area = getLocalBounds().reduced (14, 0);
    constexpr int button = 28, gap = 2;

    // Below ~1180 px (a small window, or a large interface scale) everything
    // would not fit: the project name goes (the window title still has it).
    const bool wasCompact = compact;
    compact = getWidth() < 1180;
    if (compact != wasCompact)
        repaint();

    const int switchWidth = workspaceSwitch.getIdealWidth();
    workspaceSwitch.setBounds (area.removeFromLeft (switchWidth).withSizeKeepingCentre (switchWidth, 30));
    area.removeFromLeft (10);
    libraryButton.setBounds (area.removeFromLeft (28).withSizeKeepingCentre (28, 28));
    area.removeFromLeft (2);
    inspectorButton.setBounds (area.removeFromLeft (28).withSizeKeepingCentre (28, 28));
    area.removeFromLeft (14);

    const int clusterWidth = 5 * button + 4 * gap + 8;
    clusterArea = area.removeFromLeft (clusterWidth).withSizeKeepingCentre (clusterWidth, button + 8);
    {
        auto c = clusterArea.reduced (4, 4);
        for (auto* b : { &previousButton, &playButton, &stopButton, &recordButton, &cycleButton })
        {
            b->setBounds (c.removeFromLeft (button));
            c.removeFromLeft (gap);
        }
    }
    area.removeFromLeft (12);

    lcd.setBounds (area.removeFromLeft (lcd.getIdealWidth()).withSizeKeepingCentre (lcd.getIdealWidth(), 34));
    area.removeFromLeft (6);
    metronomeButton.setBounds (area.removeFromLeft (button).withSizeKeepingCentre (button, button));
    area.removeFromLeft (2);
    tunerButton.setBounds (area.removeFromLeft (button).withSizeKeepingCentre (button, button));

    // Right side, from the edge inwards.
    exportButton.setBounds (area.removeFromRight (exportButton.getIdealWidth()).withSizeKeepingCentre (exportButton.getIdealWidth(), 28));
    area.removeFromRight (8);
    settingsButton.setBounds (area.removeFromRight (28).withSizeKeepingCentre (28, 28));
    area.removeFromRight (10);
    const int statusWidth = 168; // fits "46 ms · CPU élevé" without jumping
    statusPill.setBounds (area.removeFromRight (statusWidth).withSizeKeepingCentre (statusWidth, 24));
    area.removeFromRight (14);

    const bool roomForMeter = area.getWidth() > 120;
    inputMeter.setVisible (roomForMeter);
    if (roomForMeter)
    {
        inputMeter.setBounds (area.removeFromRight (80).withSizeKeepingCentre (80, 7));
        area.removeFromRight (7);
        meterIconArea = area.removeFromRight (14).withSizeKeepingCentre (14, 14);
    }
    else
    {
        meterIconArea = {};
    }
    area.removeFromRight (12);
    projectArea = area.reduced (12, 0);
}
