#include "Inspector.h"

using namespace roadie;
using Track = Session::Track;
using Knob = Session::Knob;

namespace
{
juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }

juce::String formatBars (double bars)
{
    const double rounded = std::round (bars * 2.0) / 2.0;
    return std::abs (rounded - std::round (rounded)) < 0.01 ? juce::String ((int) std::round (rounded))
                                                             : juce::String (rounded, 1).replaceCharacter ('.', ',');
}

// Title + subtitle block shared by the three panels (.insp-head).
void drawHead (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title, const juce::String& subtitle)
{
    auto a = area.withSizeKeepingCentre (area.getWidth(), 56);
    g.setColour (theme::text);
    g.setFont (fonts::display (15.0f));
    g.drawText (title, a.removeFromTop (22), juce::Justification::topLeft, true);
    g.setColour (theme::textMuted);
    g.setFont (fonts::medium (12.0f));
    g.drawFittedText (subtitle, a, juce::Justification::topLeft, 2, 1.0f);
}
} // namespace

//==============================================================================
class Inspector::AmpPanel : public juce::Component
{
public:
    explicit AmpPanel (Session& s)
        : session (s),
          loadIrButton (utf8 ("Charger une IR\xe2\x80\xa6"), ui::PillButton::Variant::neutral, theme::ampAccent, icons::Icon::folder)
    {
        const std::pair<Knob, const char*> specs[] = {
            { Knob::gain, "Gain" },
            { Knob::bass, "Grave" },
            { Knob::mid, "M\xc3\xa9" "dium" },
            { Knob::treble, "Aigu" },
            { Knob::presence, "Pr\xc3\xa9sence" },
            { Knob::volume, "Volume" },
        };

        for (const auto& [id, caption] : specs)
        {
            auto knob = std::make_unique<ui::Knob> (utf8 (caption), theme::ampAccent);
            knob->setRange (Session::getKnobRange (id), 0.0f);
            knob->setValue (session.getKnob (id));
            knob->onChange = [this, knobId = id] (float v) { session.setKnob (knobId, v); };
            addAndMakeVisible (*knob);
            knobs.push_back ({ id, std::move (knob) });
        }

        knobs[0].second->setTooltip (utf8 ("Gain d'entr\xc3\xa9" "e avant la capture : plus de saturation (double-clic : 0)"));
        knobs.back().second->setTooltip (utf8 ("Volume de sortie (double-clic : 0 dB)"));

        loadIrButton.setFontSize (12.0f);
        loadIrButton.setTooltip (utf8 ("Charger une r\xc3\xa9ponse impulsionnelle de baffle (.wav)"));
        loadIrButton.onClick = [this] { chooseIr(); };
        addAndMakeVisible (loadIrButton);

        clearIrButton.setTooltip (utf8 ("Retirer l'IR (sortie directe de la capture)"));
        clearIrButton.onClick = [this] { session.clearImpulseResponse(); };
        addChildComponent (clearIrButton);
    }

    void refresh()
    {
        for (auto& [id, knob] : knobs)
            if (! knob->isMouseButtonDown (true))
                knob->setValue (session.getKnob (id));

        const auto irName = session.getIrName();
        clearIrButton.setVisible (irName.isNotEmpty());
        if (irName != shownIr || session.getCaptureName() != shownCapture || session.isLoadingCapture() != shownLoading)
        {
            shownIr = irName;
            shownCapture = session.getCaptureName();
            shownLoading = session.isLoadingCapture();
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        juce::String subtitle;
        if (session.isLoadingCapture())
            subtitle = utf8 ("Chargement de la capture\xe2\x80\xa6");
        else if (shownCapture.isNotEmpty())
            subtitle = shownCapture + (session.getCaptureTag().isNotEmpty() ? utf8 (" \xc2\xb7 ") + session.getCaptureTag() : juce::String());
        else
            subtitle = utf8 ("Aucune capture \xe2\x80\x94 choisis-en une \xc3\xa0 gauche");
        drawHead (g, headArea, "Ampli", subtitle);

        g.setColour (theme::border);
        g.drawVerticalLine (irDivider, (float) irLabelArea.getY(), (float) irLabelArea.getY() + 76.0f);
        ui::drawSectionLabel (g, "Baffle (IR)", irLabelArea.toFloat());
        g.setColour (shownIr.isNotEmpty() ? theme::text : theme::textFaint);
        g.setFont (fonts::semibold (12.5f));
        g.drawText (shownIr.isNotEmpty() ? shownIr : utf8 ("Aucune \xe2\x80\x94 sortie directe"), irNameArea, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        headArea = area.removeFromLeft (std::min (190, area.getWidth() / 5));
        area.removeFromLeft (8);

        // Knobs follow the inspector's height (52 px at the default size).
        const int knobSize = juce::jlimit (40, 84, area.getHeight() - 36), gap = 16;
        const int count = (int) knobs.size();
        const int bankWidth = count * (knobSize + 8) + (count - 1) * (gap - 8);

        // IR block right after the knobs, separated by a hairline.
        auto irArea = area.withTrimmedLeft (std::min (bankWidth + 36, area.getWidth() - 200)).withWidth (220);
        irDivider = irArea.getX() - 18;
        irArea = irArea.withSizeKeepingCentre (irArea.getWidth(), 76);
        irLabelArea = irArea.removeFromTop (16);
        irArea.removeFromTop (2);
        irNameArea = irArea.removeFromTop (22);
        irArea.removeFromTop (4);
        auto buttons = irArea.removeFromTop (30);
        clearIrButton.setBounds (buttons.removeFromRight (28).withSizeKeepingCentre (26, 26));
        buttons.removeFromRight (6);
        loadIrButton.setBounds (buttons.withWidth (std::min (buttons.getWidth(), loadIrButton.getIdealWidth())));

        const int width = std::min (knobSize, (area.getWidth() - gap * (count - 1)) / count);
        auto bank = area.withSizeKeepingCentre (std::min (area.getWidth(), count * width + (count - 1) * gap), width + 22);
        bank.setX (area.getX());
        for (auto& [id, knob] : knobs)
        {
            knob->setBounds (bank.removeFromLeft (width + 8).withSizeKeepingCentre (width + 8, width + 22));
            bank.removeFromLeft (gap - 8);
        }
    }

private:
    void chooseIr()
    {
        chooser = std::make_unique<juce::FileChooser> ("Choisis une IR de baffle (.wav)", session.getProject().irFile.getParentDirectory(), "*.wav;*.aif;*.aiff");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult().existsAsFile())
                                      session.loadImpulseResponse (fc.getResult());
                              });
    }

    Session& session;
    std::vector<std::pair<Knob, std::unique_ptr<ui::Knob>>> knobs;
    ui::PillButton loadIrButton;
    ui::IconButton clearIrButton { "Remove IR", icons::Icon::close, ui::IconButton::Style::square };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> headArea, irLabelArea, irNameArea;
    int irDivider = 0;
    juce::String shownIr, shownCapture;
    bool shownLoading = false;
};

//==============================================================================
class Inspector::LooperPanel : public juce::Component
{
public:
    explicit LooperPanel (Session& s)
        : session (s),
          primaryButton ("Enregistrer", ui::PillButton::Variant::tinted, theme::loopAccent, icons::Icon::record),
          undoButton ("Annuler", ui::PillButton::Variant::neutral, theme::loopAccent, icons::Icon::undo),
          clearButton ("Effacer", ui::PillButton::Variant::neutral, theme::loopAccent, icons::Icon::clear),
          exportButton (".wav", ui::PillButton::Variant::neutral, theme::loopAccent, icons::Icon::exportArrow),
          metronomeButton (utf8 ("M\xc3\xa9tronome"), ui::PillButton::Variant::neutral, theme::loopAccent, icons::Icon::metronome)
    {
        primaryButton.onClick = [this] { session.recordLoop(); };
        undoButton.onClick = [this] { session.undo(); };
        clearButton.onClick = [this] { session.clearLoop(); };
        exportButton.onClick = [this] { exportLoop(); };
        metronomeButton.onClick = [this] { session.setMetronomeOn (! session.isMetronomeOn()); };

        primaryButton.setTooltip (utf8 ("Enregistrer \xe2\x86\x92 boucler \xe2\x86\x92 overdub, comme une p\xc3\xa9" "dale (R)"));
        undoButton.setTooltip (utf8 ("Annuler le dernier overdub (U)"));
        clearButton.setTooltip (utf8 ("Effacer la boucle (Maj + Retour arri\xc3\xa8re)"));
        exportButton.setTooltip (utf8 ("Exporter la boucle en .wav"));
        metronomeButton.setTooltip (utf8 ("Clic de m\xc3\xa9tronome au casque, jamais enregistr\xc3\xa9 (M)"));

        for (auto* b : { &primaryButton, &undoButton, &clearButton, &exportButton, &metronomeButton })
        {
            b->setFontSize (12.5f);
            addAndMakeVisible (b);
        }
    }

    void refresh()
    {
        const auto& looper = session.getEngine().getLooper();
        const auto state = looper.getState();
        const bool hasLoop = looper.getRecordedLengthSamples() > 0;

        switch (state)
        {
            case Looper::State::Empty:
                primaryButton.setButtonText ("Enregistrer");
                primaryButton.setIcon (icons::Icon::record);
                primaryButton.setAccent (theme::loopAccent);
                break;
            case Looper::State::Recording:
                primaryButton.setButtonText ("Boucler");
                primaryButton.setIcon (icons::Icon::loop);
                primaryButton.setAccent (theme::rec);
                break;
            case Looper::State::Overdubbing:
                primaryButton.setButtonText ("Fin d'overdub");
                primaryButton.setIcon (icons::Icon::check);
                primaryButton.setAccent (theme::loopAccent);
                break;
            case Looper::State::Playing:
            case Looper::State::Stopped:
                primaryButton.setButtonText ("Overdub");
                primaryButton.setIcon (icons::Icon::plus);
                primaryButton.setAccent (theme::loopAccent);
                break;
        }
        primaryButton.setToggleState (state == Looper::State::Recording || state == Looper::State::Overdubbing, juce::dontSendNotification);

        undoButton.setEnabled (looper.isUndoAvailable() && state != Looper::State::Overdubbing);
        clearButton.setEnabled (state != Looper::State::Empty);
        exportButton.setEnabled (hasLoop && state != Looper::State::Recording);
        metronomeButton.setToggleState (session.isMetronomeOn(), juce::dontSendNotification);

        const auto signature = juce::String (looper.getLayerCount()) + "|" + juce::String (looper.getRecordedLengthSamples())
                               + "|" + juce::String (session.getBpm()) + "|" + juce::String ((int) state);
        if (signature != lastSignature)
        {
            lastSignature = signature;
            resized();
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& looper = session.getEngine().getLooper();
        juce::String subtitle;
        if (looper.getRecordedLengthSamples() > 0 && looper.getState() != Looper::State::Recording)
        {
            const int layers = looper.getLayerCount();
            subtitle = juce::String (layers) + (layers > 1 ? " couches" : " couche") + utf8 (" \xc2\xb7 ")
                       + formatBars (looper.getRecordedLengthSamples() / std::max (1.0, session.getSamplesPerBar())) + " mesures";
        }
        else if (looper.getState() == Looper::State::Recording)
            subtitle = utf8 ("Enregistrement en cours\xe2\x80\xa6");
        else
            subtitle = utf8 ("Vide \xe2\x80\x94 R pour lancer une boucle");
        drawHead (g, headArea, "Looper", subtitle);

        // BPM block (.bpm-block): drag to change, double-click resets to 100.
        g.setColour (draggingBpm ? theme::loopAccent : theme::text);
        g.setFont (fonts::display (26.0f));
        fonts::drawTabular (g, juce::String (juce::roundToInt (session.getBpm())), bpmArea.toFloat().withTrimmedBottom (16.0f), juce::Justification::centredBottom);
        g.setColour (theme::textFaint);
        g.setFont (fonts::semibold (10.5f).withExtraKerningFactor (0.05f));
        g.drawText ("BPM", bpmArea.withTop (bpmArea.getBottom() - 16), juce::Justification::centredTop, false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (bpmArea.contains (e.getPosition()))
        {
            draggingBpm = true;
            dragStart = session.getBpm();
            repaint();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (draggingBpm)
        {
            session.setBpm (std::round (dragStart - e.getDistanceFromDragStartY() / (e.mods.isShiftDown() ? 12.0 : 3.0)));
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        draggingBpm = false;
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (bpmArea.contains (e.getPosition()))
            session.setBpm (100.0);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (bpmArea.contains (e.getPosition()) ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        headArea = area.removeFromLeft (std::min (190, area.getWidth() / 5));
        area.removeFromLeft (10);
        bpmArea = area.removeFromLeft (72).withSizeKeepingCentre (72, 58);
        area.removeFromLeft (14);

        auto row = area.withSizeKeepingCentre (area.getWidth(), 36);
        metronomeButton.setBounds (row.removeFromLeft (metronomeButton.getIdealWidth()));
        row.removeFromLeft (22);
        for (auto* b : { &primaryButton, &undoButton, &clearButton, &exportButton })
        {
            b->setBounds (row.removeFromLeft (b->getIdealWidth()));
            row.removeFromLeft (10);
        }
    }

private:
    void exportLoop()
    {
        chooser = std::make_unique<juce::FileChooser> ("Exporter la boucle en WAV",
                                                       session.getProject().getExportsFolder().getChildFile ("Boucle.wav"), "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
                              {
                                  auto file = fc.getResult();
                                  if (file == juce::File())
                                      return;
                                  juce::String error;
                                  if (session.exportLoopTo (file.withFileExtension (".wav"), error))
                                      session.notify (utf8 ("Boucle export\xc3\xa9" "e : ") + file.getFileName());
                                  else
                                      session.notify (error);
                              });
    }

    Session& session;
    ui::PillButton primaryButton, undoButton, clearButton, exportButton, metronomeButton;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> headArea, bpmArea;
    juce::String lastSignature;
    bool draggingBpm = false;
    double dragStart = 100.0;
};

//==============================================================================
class Inspector::CameraPanel : public juce::Component
{
public:
    explicit CameraPanel (Session& s)
        : session (s),
          powerButton ("Activer", ui::PillButton::Variant::neutral, theme::camAccent, icons::Icon::camera),
          calibrateButton ("Calibrer", ui::PillButton::Variant::tinted, theme::camAccent)
    {
        devices.setTextWhenNothingSelected (utf8 ("Choisir une cam\xc3\xa9ra"));
        devices.setTextWhenNoChoicesAvailable (utf8 ("Aucune cam\xc3\xa9ra"));
        devices.setWantsKeyboardFocus (false);
        devices.onChange = [this]
        {
            const auto name = devices.getText();
            if (name.isNotEmpty() && name != session.getCamera().getDeviceName())
                session.openCamera (name);
        };
        addAndMakeVisible (devices);

        powerButton.setFontSize (12.5f);
        powerButton.onClick = [this]
        {
            if (session.getCamera().isOpen())
                session.closeCamera();
            else
                session.openCamera (devices.getText());
        };
        addAndMakeVisible (powerButton);

        calibrateButton.setFontSize (13.0f);
        calibrateButton.setTooltip (utf8 ("Mesure le d\xc3\xa9" "calage entre la carte son et la cam\xc3\xa9ra"));
        calibrateButton.onClick = [this] { session.startCalibration(); };
        addAndMakeVisible (calibrateButton);

        refreshDevices();
    }

    void refreshDevices()
    {
        const auto names = CameraCapture::getDeviceNames();
        if (names != knownDevices)
        {
            knownDevices = names;
            devices.clear (juce::dontSendNotification);
            devices.addItemList (names, 1);
        }
        const auto current = session.getCamera().getDeviceName();
        if (current.isNotEmpty())
            devices.setText (current, juce::dontSendNotification);
        else if (devices.getSelectedId() == 0 && names.size() > 0)
            devices.setSelectedId (1, juce::dontSendNotification);
    }

    void refresh()
    {
        const bool open = session.getCamera().isOpen();
        powerButton.setButtonText (open ? "Couper" : "Activer");
        powerButton.setEnabled (! session.isCameraOpening() && CameraCapture::isSupported());
        devices.setEnabled (CameraCapture::isSupported());

        const auto state = session.getCalibrationState();
        const bool busy = state == Session::CalibrationState::starting || state == Session::CalibrationState::listening;
        calibrateButton.setEnabled (open && ! busy && ! session.isRecordingTake());
        calibrateButton.setButtonText (busy ? utf8 ("\xc3\x89" "coute\xe2\x80\xa6") : juce::String ("Calibrer"));

        // Re-list cameras now and then: plugging a webcam or an iPhone in shouldn't need a restart.
        if (++ticks % 60 == 0 && ! open)
            refreshDevices();

        const auto signature = session.getCameraDescription() + session.getCalibrationMessage() + juce::String ((int) state)
                               + juce::String (session.getSyncLagMs()) + session.getCameraError();
        if (signature != lastSignature)
        {
            lastSignature = signature;
            refreshDevices();
            resized();
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        drawHead (g, headArea, utf8 ("Cam\xc3\xa9ra"), session.getCameraDescription());

        // Sync pill (.sync-pill).
        const auto ms = juce::roundToInt (session.getSyncLagMs());
        const auto label = session.isCalibrated() ? utf8 ("\xe2\x97\x8f Synchronis\xc3\xa9" "e \xc2\xb7 ") + (ms >= 0 ? "+" : "") + juce::String (ms) + " ms"
                                                  : utf8 ("\xe2\x97\x8b Non calibr\xc3\xa9" "e");
        auto pill = syncArea.toFloat().reduced (0.5f);
        const auto colour = session.isCalibrated() ? camAccent : warn;
        g.setColour (colour.withAlpha (0.12f));
        g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
        g.setColour (colour.withAlpha (0.4f));
        g.drawRoundedRectangle (pill, pill.getHeight() * 0.5f, 1.0f);
        g.setColour (colour);
        g.setFont (fonts::display (12.0f));
        g.drawText (label, pill, juce::Justification::centred, false);

        // Steps (.cam-steps), or the live calibration message.
        const auto state = session.getCalibrationState();
        if (state != Session::CalibrationState::idle)
        {
            const auto messageColour = state == Session::CalibrationState::failed ? warn
                                       : state == Session::CalibrationState::done ? ok
                                                                                  : text;
            g.setColour (messageColour);
            g.setFont (fonts::semibold (12.5f));
            g.drawFittedText (session.getCalibrationMessage(), stepsArea, juce::Justification::centredLeft, 3);
            return;
        }

        if (session.getCameraError().isNotEmpty() && ! session.getCamera().isOpen())
        {
            g.setColour (warn);
            g.setFont (fonts::medium (12.0f));
            g.drawFittedText (session.getCameraError(), stepsArea, juce::Justification::centredLeft, 3);
            return;
        }

        const char* steps[] = {
            "Place-toi dans le cadre, bien visible.",
            "Frappe un accord \xc3\xa9touff\xc3\xa9 bien sec, une seule fois.",
            "Roadie corrige le d\xc3\xa9" "calage automatiquement.",
        };
        auto area = stepsArea;
        const int stepWidth = area.getWidth() / 3;
        for (int i = 0; i < 3; ++i)
        {
            auto step = area.removeFromLeft (stepWidth).reduced (0, 0).withTrimmedRight (12);
            g.setColour (camAccent);
            g.setFont (fonts::display (12.0f));
            g.drawText (juce::String (i + 1), step.removeFromLeft (14), juce::Justification::centredLeft, false);
            g.setColour (textMuted);
            g.setFont (fonts::medium (12.0f));
            g.drawFittedText (utf8 (steps[i]), step, juce::Justification::centredLeft, 3, 0.9f);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        headArea = area.removeFromLeft (std::min (190, area.getWidth() / 5));
        area.removeFromLeft (8);

        auto controls = area.removeFromLeft (200);
        auto top = controls.removeFromTop (controls.getHeight() / 2).withTrimmedBottom (4);
        devices.setBounds (top.withSizeKeepingCentre (top.getWidth(), 28).withY (top.getBottom() - 28));
        auto bottom = controls.withTrimmedTop (4);
        powerButton.setBounds (bottom.removeFromTop (30).withWidth (powerButton.getIdealWidth() + 6));
        area.removeFromLeft (18);

        calibrateButton.setBounds (area.removeFromRight (calibrateButton.getIdealWidth() + 8).withSizeKeepingCentre (calibrateButton.getIdealWidth() + 8, 36));
        area.removeFromRight (18);

        const auto font = fonts::display (12.0f);
        const int syncWidth = std::max (150, (int) juce::GlyphArrangement::getStringWidth (font, utf8 ("\xe2\x97\x8f Synchronis\xc3\xa9" "e \xc2\xb7 +000 ms")) + 22);
        syncArea = area.removeFromLeft (syncWidth).withSizeKeepingCentre (syncWidth, 26);
        area.removeFromLeft (18);
        stepsArea = area;
    }

private:
    Session& session;
    juce::ComboBox devices;
    ui::PillButton powerButton, calibrateButton;
    juce::StringArray knownDevices;
    juce::Rectangle<int> headArea, syncArea, stepsArea;
    juce::String lastSignature;
    int ticks = 0;
};

//==============================================================================
Inspector::Inspector (Session& s) : session (s)
{
    ampPanel = std::make_unique<AmpPanel> (session);
    looperPanel = std::make_unique<LooperPanel> (session);
    cameraPanel = std::make_unique<CameraPanel> (session);
    addAndMakeVisible (*ampPanel);
    addChildComponent (*looperPanel);
    addChildComponent (*cameraPanel);
}

Inspector::~Inspector() = default;

void Inspector::show (Track track)
{
    shown = track;
    ampPanel->setVisible (track == Track::amp);
    looperPanel->setVisible (track == Track::loop);
    cameraPanel->setVisible (track == Track::camera);
    repaint();
}

void Inspector::refresh()
{
    if (ampPanel->isVisible())
        ampPanel->refresh();
    if (looperPanel->isVisible())
        looperPanel->refresh();
    if (cameraPanel->isVisible())
        cameraPanel->refresh();
}

void Inspector::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (surface);
    g.setColour (border);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());

    const auto accent = shown == Track::loop ? loopAccent : (shown == Track::camera ? camAccent : ampAccent);
    g.setColour (accent);
    g.fillRect (0, 1, 3, getHeight() - 1);
}

void Inspector::resized()
{
    const auto area = getLocalBounds().reduced (18, 14).withTrimmedTop (2);
    ampPanel->setBounds (area);
    looperPanel->setBounds (area);
    cameraPanel->setBounds (area);
}
