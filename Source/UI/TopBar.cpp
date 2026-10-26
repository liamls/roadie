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
        if (i > 0)
        {
            g.setColour (border);
            g.drawVerticalLine ((int) c.getX(), bounds.getY() + 1.0f, bounds.getBottom() - 1.0f);
        }

        c = c.reduced (6.0f, 5.0f);
        g.setColour (textFaint);
        g.setFont (fonts::semibold (9.5f).withExtraKerningFactor (0.06f));
        g.drawText (labels[i].toUpperCase(), c.removeFromTop (12.0f), juce::Justification::centred, false);

        g.setColour (i == 1 && draggingTempo ? exportAccent : text);
        g.setFont (fonts::display (14.0f));
        fonts::drawTabular (g, values[i], c, juce::Justification::centred);
    }
}

void TopBar::Lcd::mouseMove (const juce::MouseEvent& e)
{
    const int index = e.x / cellWidth;
    setMouseCursor (index == 1 ? juce::MouseCursor::UpDownResizeCursor
                               : (index == 2 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor));
    setTooltip (index == 1 ? utf8 ("Tempo : glisse vers le haut/bas, double-clic pour saisir")
                           : (index == 2 ? utf8 ("Clique pour changer le nombre de temps par mesure")
                                         : utf8 ("Position dans la mesure (mesure.temps)")));
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
    editor->setFont (fonts::display (14.0f));
    editor->setText (juce::String (juce::roundToInt (session.getBpm())), false);
    editor->selectAll();
    editor->setBounds (cell (1).reduced (6, 14).withY (16).withHeight (getHeight() - 20));

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

    previousButton.onClick = [this] { session.returnToStart(); };
    playButton.onClick = [this] { session.togglePlay(); };
    stopButton.onClick = [this] { session.stopAll(); };
    recordButton.onClick = [this] { session.toggleTakeRecording(); };
    loopButton.onClick = [this] { session.recordLoop(); };

    for (auto* b : { &previousButton, &playButton, &stopButton, &recordButton, &loopButton })
        addAndMakeVisible (b);

    addAndMakeVisible (lcd);
    inputMeter.setTooltip (utf8 ("Niveau d'entr\xc3\xa9" "e de la guitare"));
    addAndMakeVisible (inputMeter);
    addAndMakeVisible (cpuPill);
    addAndMakeVisible (latencyPill);

    settingsButton.setTooltip (utf8 ("Carte son, fr\xc3\xa9quence et taille de buffer"));
    settingsButton.onClick = [this] { if (onOpenSettings) onOpenSettings(); };
    addAndMakeVisible (settingsButton);

    exportButton.setFullyRounded (true);
    exportButton.setTooltip (utf8 ("Exporter un clip vid\xc3\xa9o ou l'audio d'une prise (\xe2\x8c\x98" "E)"));
    exportButton.onClick = [this] { if (onOpenExport) onOpenExport(); };
    addAndMakeVisible (exportButton);

    cpuPill.setContent ("CPU", "--", theme::ok);
    latencyPill.setContent ("Latence", "--", theme::ok);
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

    // Bar.beat from the transport position.
    const double samplesPerBeat = engine.getMetronome().getSamplesPerBeat();
    const auto pos = engine.getTransportPosition();
    const auto beatIndex = (std::int64_t) (samplesPerBeat > 0.0 ? (double) pos / samplesPerBeat : 0.0);
    const int beatsPerBar = session.getBeatsPerBar();
    lcd.setPosition (juce::String (beatIndex / beatsPerBar + 1) + "." + juce::String (beatIndex % beatsPerBar + 1));
    lcd.repaint();

    inputMeter.setLevel (engine.getInputPeak());

    const auto cpu = session.getDeviceManager().getCpuUsage();
    cpuPill.setContent ("CPU", juce::String (juce::roundToInt (cpu * 100.0)) + "%",
                        cpu < 0.5 ? theme::ok : (cpu < 0.8 ? theme::warn : theme::rec));

    const auto latency = engine.getRoundTripLatencyMs();
    if (session.getDeviceManager().getCurrentAudioDevice() != nullptr)
        latencyPill.setContent ("Latence", juce::String (latency, 1) + " ms", latency < 12.0 ? theme::ok : theme::warn);
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
    auto mark = brand.removeFromLeft (22.0f).withSizeKeepingCentre (22.0f, 22.0f);
    g.setGradientFill (juce::ColourGradient (ampAccent, mark.getX(), mark.getY(), juce::Colour (0xffc2501b), mark.getRight(), mark.getBottom(), false));
    g.fillRoundedRectangle (mark, 7.0f);
    g.setColour (juce::Colour (0xff0e0f12).withAlpha (0.75f));
    g.drawEllipse (mark.reduced (5.0f), 1.5f);

    brand.removeFromLeft (8.0f);
    g.setColour (text);
    g.setFont (fonts::display (15.0f));
    g.drawText ("Roadie", brand, juce::Justification::centredLeft, false);

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
    auto area = getLocalBounds().reduced (16, 0);

    brandArea = area.removeFromLeft (92).withSizeKeepingCentre (92, 24);
    area.removeFromLeft (14);

    clusterArea = area.removeFromLeft (5 * 32 + 4 * 2 + 10).withSizeKeepingCentre (5 * 32 + 4 * 2 + 10, 42);
    {
        auto c = clusterArea.reduced (5, 5);
        for (auto* b : { &previousButton, &playButton, &stopButton, &recordButton, &loopButton })
        {
            b->setBounds (c.removeFromLeft (32));
            c.removeFromLeft (2);
        }
    }
    area.removeFromLeft (18);

    lcd.setBounds (area.removeFromLeft (lcd.getIdealWidth()).withSizeKeepingCentre (lcd.getIdealWidth(), 40));

    // Right side, from the edge inwards.
    exportButton.setBounds (area.removeFromRight (exportButton.getIdealWidth()).withSizeKeepingCentre (exportButton.getIdealWidth(), 32));
    area.removeFromRight (10);
    settingsButton.setBounds (area.removeFromRight (30).withSizeKeepingCentre (30, 30));
    area.removeFromRight (12);
    const int latencyWidth = std::max (118, latencyPill.getIdealWidth());
    latencyPill.setBounds (area.removeFromRight (latencyWidth).withSizeKeepingCentre (latencyWidth, 26));
    area.removeFromRight (8);
    cpuPill.setBounds (area.removeFromRight (88).withSizeKeepingCentre (88, 26));
    area.removeFromRight (16);

    const bool roomForMeter = area.getWidth() > 130;
    inputMeter.setVisible (roomForMeter);
    if (roomForMeter)
    {
        inputMeter.setBounds (area.removeFromRight (90).withSizeKeepingCentre (90, 8));
        area.removeFromRight (8);
        meterIconArea = area.removeFromRight (14).withSizeKeepingCentre (14, 14);
    }
    else
    {
        meterIconArea = {};
    }
}
