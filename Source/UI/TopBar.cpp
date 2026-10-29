#include "TopBar.h"

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
    g.fillRoundedRectangle (bounds, radiusSm);
    g.setColour (border);
    g.drawRoundedRectangle (bounds, radiusSm, 1.0f);

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
            g.fillRoundedRectangle (c.reduced (2.0f), radiusSm - 2.0f);
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

        g.setColour (i == 1 && draggingTempo ? exportAccent : text);
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
                           : (index == 2 ? utf8 ("Clique pour changer le nombre de temps par mesure")
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
        const int next = session.getBeatsPerBar() >= 7 ? 2 : session.getBeatsPerBar() + 1;
        session.setBeatsPerBar (next);
        repaint();
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
TopBar::TopBar (Session& s)
    : session (s),
      lcd (s),
      exportButton ("Exporter", ui::PillButton::Variant::tinted, theme::exportAccent, icons::Icon::exportArrow)
{
    previousButton.setTooltip (utf8 ("Retour au d\xc3\xa9" "but de la boucle (Entr\xc3\xa9" "e)"));
    playButton.setTooltip (utf8 ("Lecture / pause (Espace)"));
    stopButton.setTooltip (utf8 ("Tout arr\xc3\xaa" "ter"));
    recordButton.setTooltip (utf8 ("Enregistrer une prise : audio + cam\xc3\xa9ra (T)"));
    loopButton.setTooltip (utf8 ("Looper : enregistrer \xe2\x86\x92 boucler \xe2\x86\x92 overdub (R)"));
    loopButton.setAccent (theme::loopAccent);
    cycleButton.setTooltip (utf8 ("Zone de cycle : la lecture r\xc3\xa9p\xc3\xa8te la bande jaune de la r\xc3\xa8gle (C)"));
    cycleButton.setAccent (theme::exportAccent);

    previousButton.onClick = [this] { session.returnToStart(); };
    playButton.onClick = [this] { session.togglePlay(); };
    stopButton.onClick = [this] { session.stopAll(); };
    recordButton.onClick = [this] { session.toggleTakeRecording(); };
    loopButton.onClick = [this] { session.recordLoop(); };
    cycleButton.onClick = [this] { session.toggleCycle(); };

    for (auto* b : { &previousButton, &playButton, &stopButton, &recordButton, &loopButton, &cycleButton })
        addAndMakeVisible (b);

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

    inputMeter.setTooltip (utf8 ("Niveau d'entr\xc3\xa9" "e de la guitare"));
    addAndMakeVisible (inputMeter);
    addAndMakeVisible (cpuPill);
    addAndMakeVisible (latencyPill);

    settingsButton.setTooltip (utf8 ("Carte son, fr\xc3\xa9quence et taille de buffer"));
    settingsButton.onClick = [this] { if (onOpenSettings) onOpenSettings(); };
    addAndMakeVisible (settingsButton);

    exportButton.setFullyRounded (true);
    exportButton.setFontSize (12.5f);
    exportButton.setTooltip (utf8 ("Exporter un clip vid\xc3\xa9o ou l'audio d'une prise (\xe2\x8c\x98" "E)"));
    exportButton.onClick = [this] { if (onOpenExport) onOpenExport(); };
    addAndMakeVisible (exportButton);

    cpuPill.setContent ("CPU", "--", theme::ok);
    latencyPill.setContent ("Latence", "--", theme::ok);
    latencyPill.setTooltip (utf8 ("Latence aller-retour estim\xc3\xa9" "e : de la guitare jusqu'aux oreilles"));
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

    loopButton.setOn (state == Looper::State::Recording || state == Looper::State::Overdubbing);
    loopButton.setAccent (state == Looper::State::Recording ? theme::rec : theme::loopAccent);
    loopButton.setBlinking (state == Looper::State::Recording);
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
    const juce::String cpuWords[] = { "Faible", "Normal", utf8 ("\xc3\x89lev\xc3\xa9") };
    const juce::Colour cpuColours[] = { theme::ok, theme::warn, theme::rec };
    const auto level = (int) cpuLoad.getLevel();
    cpuPill.setContent ("CPU", cpuWords[level], cpuColours[level]);
    cpuPill.setTooltip (utf8 ("Charge du traitement audio : ") + juce::String (juce::roundToInt (cpuLoad.getAverage() * 100.0))
                        + utf8 ("\xc2\xa0% en moyenne sur 2\xc2\xa0s (") + juce::String (juce::roundToInt (cpu * 100.0))
                        + utf8 ("\xc2\xa0% \xc3\xa0 l'instant)"));

    const auto latency = engine.getRoundTripLatencyMs();
    if (session.getDeviceManager().getCurrentAudioDevice() != nullptr)
        latencyPill.setContent (compact ? juce::String() : "Latence", juce::String (latency, 1) + " ms", latency < 12.0 ? theme::ok : theme::warn);
    else
        latencyPill.setContent ("Audio", utf8 ("coup\xc3\xa9"), theme::rec);

    for (auto* b : { &previousButton, &playButton, &stopButton, &recordButton, &loopButton })
        b->repaint();
}

void TopBar::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (surface);
    g.setColour (border);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    // Brand mark: rounded orange square with a dark ring, then the wordmark.
    auto brand = brandArea.toFloat();
    auto mark = brand.removeFromLeft (20.0f).withSizeKeepingCentre (20.0f, 20.0f);
    g.setGradientFill (juce::ColourGradient (ampAccent, mark.getX(), mark.getY(), juce::Colour (0xffc2501b), mark.getRight(), mark.getBottom(), false));
    g.fillRoundedRectangle (mark, 6.0f);
    g.setColour (juce::Colour (0xff0e0f12).withAlpha (0.75f));
    g.drawEllipse (mark.reduced (4.5f), 1.5f);

    if (! compact)
    {
        brand.removeFromLeft (7.0f);
        g.setColour (text);
        g.setFont (fonts::display (14.0f));
        g.drawText ("Roadie", brand, juce::Justification::centredLeft, false);
    }

    // Transport cluster capsule.
    auto cluster = clusterArea.toFloat().reduced (0.5f);
    g.setColour (surfaceSunken);
    g.fillRoundedRectangle (cluster, cluster.getHeight() * 0.5f);
    g.setColour (border);
    g.drawRoundedRectangle (cluster, cluster.getHeight() * 0.5f, 1.0f);

    icons::draw (g, icons::Icon::gauge, meterIconArea.toFloat(), textFaint);
}

void TopBar::resized()
{
    auto area = getLocalBounds().reduced (14, 0);
    constexpr int button = 28, gap = 2;

    // Below ~1120 px (a small window, or a large interface scale) everything
    // would not fit: drop the wordmark and the latency label (its tooltip stays).
    const bool wasCompact = compact;
    compact = getWidth() < 1120;
    if (compact != wasCompact)
        repaint();

    const int brandWidth = compact ? 20 : 84;
    brandArea = area.removeFromLeft (brandWidth).withSizeKeepingCentre (brandWidth, 22);
    area.removeFromLeft (6);
    libraryButton.setBounds (area.removeFromLeft (28).withSizeKeepingCentre (28, 28));
    area.removeFromLeft (2);
    inspectorButton.setBounds (area.removeFromLeft (28).withSizeKeepingCentre (28, 28));
    area.removeFromLeft (14);

    const int clusterWidth = 6 * button + 5 * gap + 8;
    clusterArea = area.removeFromLeft (clusterWidth).withSizeKeepingCentre (clusterWidth, button + 8);
    {
        auto c = clusterArea.reduced (4, 4);
        for (auto* b : { &previousButton, &playButton, &stopButton, &recordButton, &loopButton, &cycleButton })
        {
            b->setBounds (c.removeFromLeft (button));
            c.removeFromLeft (gap);
        }
    }
    area.removeFromLeft (12);

    lcd.setBounds (area.removeFromLeft (lcd.getIdealWidth()).withSizeKeepingCentre (lcd.getIdealWidth(), 34));
    area.removeFromLeft (6);
    metronomeButton.setBounds (area.removeFromLeft (button).withSizeKeepingCentre (button, button));

    // Right side, from the edge inwards.
    exportButton.setBounds (area.removeFromRight (exportButton.getIdealWidth()).withSizeKeepingCentre (exportButton.getIdealWidth(), 28));
    area.removeFromRight (8);
    settingsButton.setBounds (area.removeFromRight (28).withSizeKeepingCentre (28, 28));
    area.removeFromRight (10);
    const int latencyWidth = compact ? 84 : std::max (124, latencyPill.getIdealWidth());
    latencyPill.setBounds (area.removeFromRight (latencyWidth).withSizeKeepingCentre (latencyWidth, 24));
    area.removeFromRight (6);
    const int cpuWidth = 104; // fits "CPU Normal" / "CPU Élevé" without jumping
    cpuPill.setBounds (area.removeFromRight (cpuWidth).withSizeKeepingCentre (cpuWidth, 24));
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
}
