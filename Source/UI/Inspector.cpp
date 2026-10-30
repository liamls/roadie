#include "Inspector.h"
#include "../I18n.h"

#include <numeric>

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
                                                             : juce::String (rounded, 1).replaceCharacter ('.', i18n::decimalPoint());
}

// Narrow (the right-hand column of Monter): the panels stack their parts
// instead of lining them up.
bool isNarrow (const juce::Component& c) { return c.getWidth() < 560; }

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
// A block's on/off switch: lit in the block's colour when it plays,
// grey when it is bypassed.
class BlockSwitch : public juce::Button
{
public:
    explicit BlockSwitch (juce::Colour a) : juce::Button ("Bypass"), accent (a)
    {
        setClickingTogglesState (true);
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool isOver, bool) override
    {
        const bool on = getToggleState();
        auto bounds = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (on ? accent.withAlpha (0.2f + (isOver ? 0.08f : 0.0f)) : theme::text.withAlpha (isOver ? 0.08f : 0.04f));
        g.fillEllipse (bounds);
        icons::draw (g, icons::Icon::power, bounds.reduced (bounds.getWidth() * 0.22f), on ? theme::ink (accent) : theme::textFaint, 2.6f);
    }

private:
    juce::Colour accent;
};

//==============================================================================
// The amp as a signal chain, the way a pedalboard reads:
//   Input (gain, gate) → Capture → EQ → Cab → Output
// Capture, EQ and Baffle can each be switched off to hear what they do.
class Inspector::AmpPanel : public juce::Component
{
public:
    explicit AmpPanel (Session& s)
        : session (s),
          loadIrButton (tr ("Load…"), ui::PillButton::Variant::neutral, theme::ampAccent, icons::Icon::folder),
          gateSwitch (theme::ampAccent), captureSwitch (theme::ampAccent), eqSwitch (theme::ampAccent), cabSwitch (theme::ampAccent)
    {
        const std::pair<Knob, juce::String> specs[] = {
            { Knob::gain, "Gain" },
            { Knob::gate, tr ("Threshold") },
            { Knob::bass, tr ("Bass") },
            { Knob::mid, tr ("Mid") },
            { Knob::treble, tr ("Treble") },
            { Knob::presence, tr ("Presence") },
            { Knob::volume, "Volume" },
        };

        for (const auto& [id, caption] : specs)
        {
            auto knob = std::make_unique<ui::Knob> (caption, theme::ampAccent);
            knob->setRange (Session::getKnobRange (id), id == Knob::gate ? -60.0f : 0.0f);
            knob->setValue (session.getKnob (id));
            knob->onChange = [this, knobId = id] (float v) { session.setKnob (knobId, v); };
            addAndMakeVisible (*knob);
            knobs.push_back ({ id, std::move (knob) });
        }

        knob (Knob::gain).setTooltip (tr ("Input gain before the capture: more saturation (⌥-click: 0, ⇧: fine adjust)"));
        knob (Knob::gate).setTooltip (tr ("Noise gate threshold: below this level, hiss is cut between phrases"));
        knob (Knob::volume).setTooltip (tr ("Output volume (⌥-click: 0 dB)"));

        gateSwitch.setTooltip (tr ("Noise gate: cuts the hiss of a saturated capture between phrases"));
        gateSwitch.onClick = [this] { session.setGateOn (gateSwitch.getToggleState()); };
        captureSwitch.setTooltip (tr ("Capture on (off: the guitar bypasses the amp)"));
        captureSwitch.onClick = [this] { session.setBypassed (AmpEngine::Block::capture, ! captureSwitch.getToggleState()); };
        eqSwitch.setTooltip (tr ("EQ on (off: flat sound)"));
        eqSwitch.onClick = [this] { session.setBypassed (AmpEngine::Block::eq, ! eqSwitch.getToggleState()); };
        cabSwitch.setTooltip (tr ("Cab on (off: direct capture output)"));
        cabSwitch.onClick = [this] { session.setBypassed (AmpEngine::Block::cab, ! cabSwitch.getToggleState()); };
        for (auto* b : { &gateSwitch, &captureSwitch, &eqSwitch, &cabSwitch })
            addAndMakeVisible (b);

        loadIrButton.setFontSize (12.0f);
        loadIrButton.setTooltip (tr ("Load a cab impulse response (.wav)"));
        loadIrButton.onClick = [this] { chooseIr(); };
        addAndMakeVisible (loadIrButton);

        clearIrButton.setTooltip (tr ("Remove the IR (direct capture output)"));
        clearIrButton.onClick = [this] { session.clearImpulseResponse(); };
        addChildComponent (clearIrButton);
    }

    void refresh()
    {
        for (auto& [id, k] : knobs)
            if (! k->isMouseButtonDown (true))
                k->setValue (session.getKnob (id));

        gateSwitch.setToggleState (session.isGateOn(), juce::dontSendNotification);
        captureSwitch.setToggleState (! session.isBypassed (AmpEngine::Block::capture), juce::dontSendNotification);
        eqSwitch.setToggleState (! session.isBypassed (AmpEngine::Block::eq), juce::dontSendNotification);
        cabSwitch.setToggleState (! session.isBypassed (AmpEngine::Block::cab), juce::dontSendNotification);
        knob (Knob::gate).setAlpha (session.isGateOn() ? 1.0f : 0.45f);
        for (auto id : { Knob::bass, Knob::mid, Knob::treble, Knob::presence })
            knob (id).setAlpha (session.isBypassed (AmpEngine::Block::eq) ? 0.45f : 1.0f);

        const auto irName = session.getIrName();
        clearIrButton.setVisible (irName.isNotEmpty());
        const float openness = session.isGateOn() ? session.getGateOpenness() : 1.0f;
        if (irName != shownIr || session.getCaptureName() != shownCapture || session.isLoadingCapture() != shownLoading
            || std::abs (openness - shownOpenness) > 0.05f)
        {
            shownIr = irName;
            shownCapture = session.getCaptureName();
            shownLoading = session.isLoadingCapture();
            shownOpenness = openness;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        const bool narrow = isNarrow (*this);
        if (! narrow)
            drawHead (g, headArea, tr ("Amp"), tr ("Input → Capture → EQ → Cab → Output"));
        else
            drawHead (g, headArea, tr ("Amp"), shownCapture.isNotEmpty() ? shownCapture : tr ("No capture"));

        // Blocks: a card each, dimmed when bypassed, arrows between them.
        const struct { juce::Rectangle<int> area; juce::String title; bool on; } blocks[] = {
            { inputArea, tr ("Input"), true },
            { captureArea, "Capture", ! session.isBypassed (AmpEngine::Block::capture) },
            { eqArea, "EQ", ! session.isBypassed (AmpEngine::Block::eq) },
            { cabArea, tr ("Cab"), ! session.isBypassed (AmpEngine::Block::cab) },
            { outputArea, tr ("Output"), true },
        };
        for (size_t i = 0; i < std::size (blocks); ++i)
        {
            const auto& b = blocks[i];
            if (b.area.isEmpty())
                continue;
            auto card = b.area.toFloat();
            g.setColour (b.on ? surfaceRaised.withAlpha (0.55f) : surfaceSunken.withAlpha (0.6f));
            roadie::theme::fillSquircle (g, card, radiusSm);
            g.setColour (b.on ? border : border.withAlpha (0.5f));
            roadie::theme::drawSquircle (g, card.reduced (0.5f), radiusSm, 1.0f);
            ui::drawSectionLabel (g, b.title, card.reduced (10.0f, 0.0f).withHeight (24.0f));

            if (i + 1 < std::size (blocks) && ! narrow)
                icons::draw (g, icons::Icon::arrow, juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ card.getRight() + 7.0f, card.getCentreY() }), textFaint, 2.0f);
        }

        // Gate light: green while open, dim while closed.
        if (! gateLight.isEmpty())
        {
            g.setColour (session.isGateOn() ? ok.withAlpha (0.25f + 0.75f * shownOpenness) : textFaint.withAlpha (0.3f));
            g.fillEllipse (gateLight.toFloat());
        }

        // Capture name.
        if (! captureText.isEmpty())
        {
            g.setColour (shownCapture.isNotEmpty() ? theme::text : textFaint);
            g.setFont (fonts::semibold (12.5f));
            const auto name = session.isLoadingCapture() ? tr ("Loading…")
                              : shownCapture.isNotEmpty() ? shownCapture
                                                          : tr ("None — pick one in the library");
            g.drawFittedText (name, captureText, juce::Justification::topLeft, 3, 0.9f);
            if (session.getCaptureTag().isNotEmpty())
            {
                g.setColour (textMuted);
                g.setFont (fonts::medium (11.0f));
                g.drawText (session.getCaptureTag(), captureText.withTop (captureText.getBottom() - 16), juce::Justification::bottomLeft, true);
            }
        }

        g.setColour (shownIr.isNotEmpty() ? theme::text : textFaint);
        g.setFont (fonts::semibold (12.0f));
        g.drawFittedText (shownIr.isNotEmpty() ? shownIr : tr ("None — direct output"), irNameArea, juce::Justification::topLeft, 2, 0.9f);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        const bool narrow = isNarrow (*this);
        headArea = narrow ? area.removeFromTop (52) : area.removeFromLeft (std::min (150, area.getWidth() / 7));
        if (narrow)
            area.removeFromTop (4);
        else
            area.removeFromLeft (10);

        constexpr int titleHeight = 24, gap = 14;
        const int knobSize = juce::jlimit (34, 72, (narrow ? 64 : area.getHeight() - titleHeight - 26));

        auto layoutKnobs = [&] (juce::Rectangle<int> r, std::initializer_list<Knob> ids)
        {
            const int w = std::min (knobSize + 8, r.getWidth() / (int) ids.size());
            auto row = r.withSizeKeepingCentre (w * (int) ids.size(), std::min (r.getHeight(), knobSize + 22));
            for (auto id : ids)
                knob (id).setBounds (row.removeFromLeft (w));
        };
        auto placeSwitch = [] (BlockSwitch& b, juce::Rectangle<int> card) { b.setBounds (card.getRight() - 28, card.getY() + 3, 22, 22); };

        // Block widths: knob columns, the capture and the cab a fixed text width.
        const int knobColumn = knobSize + 8;
        int widths[] = { 2 * knobColumn + 16, 170, 4 * knobColumn + 16, 180, knobColumn + 16 };
        juce::Rectangle<int>* areas[] = { &inputArea, &captureArea, &eqArea, &cabArea, &outputArea };

        if (narrow)
        {
            // Stacked: each block a row.
            const int heights[] = { knobSize + 22 + titleHeight + 4, 74, knobSize + 22 + titleHeight + 4, 92, knobSize + 22 + titleHeight + 4 };
            for (int i = 0; i < 5; ++i)
            {
                *areas[i] = area.removeFromTop (heights[i]);
                area.removeFromTop (8);
            }
        }
        else
        {
            const int total = std::accumulate (std::begin (widths), std::end (widths), 0) + 4 * gap;
            if (total > area.getWidth()) // squeeze the text blocks first
            {
                const int excess = total - area.getWidth();
                widths[1] = std::max (110, widths[1] - excess / 2);
                widths[3] = std::max (120, widths[3] - excess / 2);
            }
            for (int i = 0; i < 5; ++i)
            {
                *areas[i] = area.removeFromLeft (widths[i]);
                area.removeFromLeft (gap);
            }
        }

        auto content = [] (juce::Rectangle<int> card) { return card.withTrimmedTop (titleHeight).reduced (8, 4); };

        layoutKnobs (content (inputArea), { Knob::gain, Knob::gate });
        placeSwitch (gateSwitch, inputArea);
        gateLight = juce::Rectangle<int> (7, 7).withCentre ({ gateSwitch.getX() - 10, gateSwitch.getBounds().getCentreY() });

        placeSwitch (captureSwitch, captureArea);
        captureText = content (captureArea);

        layoutKnobs (content (eqArea), { Knob::bass, Knob::mid, Knob::treble, Knob::presence });
        placeSwitch (eqSwitch, eqArea);

        placeSwitch (cabSwitch, cabArea);
        auto cab = content (cabArea);
        auto buttons = cab.removeFromBottom (28);
        irNameArea = cab;
        clearIrButton.setBounds (buttons.removeFromRight (26).withSizeKeepingCentre (24, 24));
        buttons.removeFromRight (4);
        loadIrButton.setBounds (buttons.withWidth (std::min (buttons.getWidth(), loadIrButton.getIdealWidth())));

        layoutKnobs (content (outputArea), { Knob::volume });
    }

private:
    ui::Knob& knob (Knob id)
    {
        for (auto& [k, widget] : knobs)
            if (k == id)
                return *widget;
        return *knobs.front().second;
    }

    void chooseIr()
    {
        chooser = std::make_unique<juce::FileChooser> (tr ("Choose a cab IR (.wav)"), session.getProject().irFile.getParentDirectory(), "*.wav;*.aif;*.aiff");
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
    BlockSwitch gateSwitch, captureSwitch, eqSwitch, cabSwitch;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> headArea, inputArea, captureArea, eqArea, cabArea, outputArea, gateLight, captureText, irNameArea;
    juce::String shownIr, shownCapture;
    bool shownLoading = false;
    float shownOpenness = 1.0f;
};

//==============================================================================
class Inspector::LooperPanel : public juce::Component
{
public:
    explicit LooperPanel (Session& s)
        : session (s),
          primaryButton (tr ("Record"), ui::PillButton::Variant::tinted, theme::loopAccent, icons::Icon::record),
          undoButton (tr ("Remove Layer"), ui::PillButton::Variant::neutral, theme::loopAccent, icons::Icon::undo),
          clearButton (tr ("Clear"), ui::PillButton::Variant::neutral, theme::loopAccent, icons::Icon::clear),
          exportButton (".wav", ui::PillButton::Variant::neutral, theme::loopAccent, icons::Icon::exportArrow)
    {
        // Tempo and metronome live in the top bar only: one place each.
        primaryButton.onClick = [this] { session.recordLoop(); };
        undoButton.onClick = [this] { session.undo(); };
        clearButton.onClick = [this] { session.clearLoop(); };
        exportButton.onClick = [this] { exportLoop(); };

        primaryButton.setTooltip (tr ("Record → loop → overdub, like a pedal (R)"));
        undoButton.setTooltip (tr ("Removes the last overdub layer (U)"));
        clearButton.setTooltip (tr ("Clear the loop (Shift + Backspace)"));
        exportButton.setTooltip (tr ("Export the loop as .wav"));

        for (auto* b : { &primaryButton, &undoButton, &clearButton, &exportButton })
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
                primaryButton.setButtonText (tr ("Record"));
                primaryButton.setIcon (icons::Icon::record);
                primaryButton.setAccent (theme::loopAccent);
                break;
            case Looper::State::Recording:
                primaryButton.setButtonText (tr ("Close loop"));
                primaryButton.setIcon (icons::Icon::loop);
                primaryButton.setAccent (theme::rec);
                break;
            case Looper::State::Overdubbing:
                primaryButton.setButtonText (tr ("End overdub"));
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
            subtitle = juce::String (layers) + (layers > 1 ? tr (" layers") : tr (" layer")) + utf8 (" \xc2\xb7 ")
                       + formatBars (looper.getRecordedLengthSamples() / std::max (1.0, session.getSamplesPerBar())) + tr (" bars");
        }
        else if (looper.getState() == Looper::State::Recording)
            subtitle = tr ("Recording…");
        else
            subtitle = tr ("Empty — press R to start a loop");
        drawHead (g, headArea, "Looper", subtitle);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        const bool narrow = isNarrow (*this);
        if (narrow)
        {
            headArea = area.removeFromTop (52);
            area.removeFromTop (8);
        }
        else
        {
            headArea = area.removeFromLeft (std::min (190, area.getWidth() / 5));
            area.removeFromLeft (14);
            area = area.withSizeKeepingCentre (area.getWidth(), 36);
        }

        // Buttons flow left to right, wrapping in a narrow column.
        auto row = area.removeFromTop (36);
        for (auto* b : { &primaryButton, &undoButton, &clearButton, &exportButton })
        {
            const int w = b->getIdealWidth();
            if (row.getWidth() < w && narrow)
            {
                area.removeFromTop (8);
                row = area.removeFromTop (36);
            }
            b->setBounds (row.removeFromLeft (w));
            row.removeFromLeft (10);
        }
    }

private:
    void exportLoop()
    {
        chooser = std::make_unique<juce::FileChooser> (tr ("Export the loop as WAV"),
                                                       session.getProject().getExportsFolder().getChildFile ("Boucle.wav"), "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
                              {
                                  auto file = fc.getResult();
                                  if (file == juce::File())
                                      return;
                                  juce::String error;
                                  if (session.exportLoopTo (file.withFileExtension (".wav"), error))
                                      session.notify (tr ("Loop exported: ") + file.getFileName());
                                  else
                                      session.notify (error);
                              });
    }

    Session& session;
    ui::PillButton primaryButton, undoButton, clearButton, exportButton;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> headArea;
    juce::String lastSignature;
};

//==============================================================================
class Inspector::CameraPanel : public juce::Component
{
public:
    explicit CameraPanel (Session& s)
        : session (s),
          powerButton (tr ("Turn on"), ui::PillButton::Variant::neutral, theme::camAccent, icons::Icon::camera),
          calibrateButton (tr ("Calibrate"), ui::PillButton::Variant::tinted, theme::camAccent)
    {
        devices.setTextWhenNothingSelected (tr ("Choose a camera"));
        devices.setTextWhenNoChoicesAvailable (tr ("No camera"));
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
        calibrateButton.setTooltip (tr ("Measures the offset between the audio interface and the camera"));
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
        powerButton.setButtonText (open ? tr ("Turn off") : tr ("Turn on"));
        powerButton.setEnabled (! session.isCameraOpening() && CameraCapture::isSupported());
        devices.setEnabled (CameraCapture::isSupported());

        const auto state = session.getCalibrationState();
        const bool busy = state == Session::CalibrationState::starting || state == Session::CalibrationState::listening;
        calibrateButton.setEnabled (open && ! busy && ! session.isRecordingTake());
        calibrateButton.setButtonText (busy ? tr ("Listening…") : juce::String (tr ("Calibrate")));

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
        drawHead (g, headArea, tr ("Camera"), session.getCameraDescription());

        // Sync pill (.sync-pill).
        const auto ms = juce::roundToInt (session.getSyncLagMs());
        const auto label = session.isCalibrated() ? tr ("● Synced · ") + (ms >= 0 ? "+" : "") + juce::String (ms) + " ms"
                                                  : tr ("○ Not calibrated");
        auto pill = syncArea.toFloat().reduced (0.5f);
        const auto colour = session.isCalibrated() ? camAccent : warn;
        g.setColour (colour.withAlpha (0.12f));
        roadie::theme::fillSquircle (g, pill, pill.getHeight() * 0.5f);
        g.setColour (colour.withAlpha (0.4f));
        roadie::theme::drawSquircle (g, pill, pill.getHeight() * 0.5f, 1.0f);
        g.setColour (ink (colour));
        g.setFont (fonts::display (12.0f));
        g.drawText (label, pill, juce::Justification::centred, false);

        // Calibration is a one-off wizard: its steps only show while it
        // runs, the current one lit; otherwise a single line says where it stands.
        const auto state = session.getCalibrationState();
        const bool running = state == Session::CalibrationState::starting || state == Session::CalibrationState::listening;

        if (state == Session::CalibrationState::done || state == Session::CalibrationState::failed)
        {
            g.setColour (ink (state == Session::CalibrationState::failed ? warn : ok));
            g.setFont (fonts::semibold (12.5f));
            g.drawFittedText (session.getCalibrationMessage(), stepsArea, juce::Justification::centredLeft, 3);
            return;
        }

        if (session.getCameraError().isNotEmpty() && ! session.getCamera().isOpen())
        {
            g.setColour (ink (warn));
            g.setFont (fonts::medium (12.0f));
            g.drawFittedText (session.getCameraError(), stepsArea, juce::Justification::centredLeft, 3);
            return;
        }

        if (! running)
        {
            g.setColour (textMuted);
            g.setFont (fonts::medium (12.0f));
            g.drawFittedText (session.isCalibrated() ? tr ("Picture and sound are aligned. Only recalibrate if you change camera or interface.")
                                                     : tr ("Do this once: Calibrate, then play one dry, muted chord. Ten seconds."),
                              stepsArea, juce::Justification::centredLeft, 3);
            return;
        }

        const juce::String steps[] = {
            tr ("Stand in the frame, clearly visible."),
            tr ("Strike one dry, muted chord, just once."),
            tr ("Roadie corrects the offset automatically."),
        };
        const int current = state == Session::CalibrationState::starting ? 0 : 1;
        auto area = stepsArea;
        const bool narrow = isNarrow (*this);
        const int stepWidth = area.getWidth() / 3, stepHeight = area.getHeight() / 3;
        for (int i = 0; i < 3; ++i)
        {
            auto step = narrow ? area.removeFromTop (stepHeight) : area.removeFromLeft (stepWidth).withTrimmedRight (12);
            const bool lit = i == current;
            g.setColour (lit ? ink (camAccent) : textFaint);
            g.setFont (fonts::display (12.0f));
            g.drawText (juce::String (i + 1), step.removeFromLeft (14), juce::Justification::centredLeft, false);
            g.setColour (lit ? theme::text : textFaint);
            g.setFont (lit ? fonts::semibold (12.0f) : fonts::medium (12.0f));
            g.drawFittedText (steps[i], step, juce::Justification::centredLeft, 3, 0.9f);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        const auto font = fonts::display (12.0f);
        const int syncWidth = std::max (150, (int) juce::GlyphArrangement::getStringWidth (font, tr ("● Synced · +000 ms")) + 22);

        if (isNarrow (*this))
        {
            headArea = area.removeFromTop (52);
            area.removeFromTop (6);
            devices.setBounds (area.removeFromTop (28));
            area.removeFromTop (8);
            auto row = area.removeFromTop (32);
            powerButton.setBounds (row.removeFromLeft (powerButton.getIdealWidth() + 6));
            row.removeFromLeft (8);
            calibrateButton.setBounds (row.removeFromLeft (calibrateButton.getIdealWidth() + 8));
            area.removeFromTop (10);
            syncArea = area.removeFromTop (26).withWidth (std::min (area.getWidth(), syncWidth));
            area.removeFromTop (10);
            stepsArea = area.withHeight (std::min (area.getHeight(), 120));
            return;
        }

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
class Inspector::SongPanel : public juce::Component
{
public:
    explicit SongPanel (Inspector& o, Session& s)
        : owner (o), session (s),
          chooseButton (tr ("Choose a Song…"), ui::PillButton::Variant::tinted, theme::songAccent, icons::Icon::song),
          youtubeButton ("YouTube\xe2\x80\xa6", ui::PillButton::Variant::neutral, theme::songAccent, icons::Icon::song),
          doubleButton (utf8 ("\xc3\x97" "2"), ui::PillButton::Variant::neutral, theme::songAccent),
          halfButton (utf8 ("\xc3\xb7" "2"), ui::PillButton::Variant::neutral, theme::songAccent),
          tapButton (tr ("Tap"), ui::PillButton::Variant::neutral, theme::songAccent),
          earlierButton (utf8 ("\xe2\x88\x92" "10 ms"), ui::PillButton::Variant::neutral, theme::songAccent),
          laterButton ("+10 ms", ui::PillButton::Variant::neutral, theme::songAccent),
          placeButton (tr ("Bar 2"), ui::PillButton::Variant::neutral, theme::songAccent),
          removeButton (tr ("Remove"), ui::PillButton::Variant::neutral, theme::songAccent, icons::Icon::close),
          slowerButton (utf8 ("\xe2\x88\x92" "5") + i18n::percent(), ui::PillButton::Variant::neutral, theme::songAccent),
          fasterButton ("+5" + i18n::percent(), ui::PillButton::Variant::neutral, theme::songAccent),
          downButton (utf8 ("\xe2\x88\x92" "1"), ui::PillButton::Variant::neutral, theme::songAccent),
          upButton ("+1", ui::PillButton::Variant::neutral, theme::songAccent)
    {
        chooseButton.onClick = [this] { if (owner.onImportSong) owner.onImportSong(); };
        youtubeButton.onClick = [this] { if (owner.onImportYouTube) owner.onImportYouTube(); };
        youtubeButton.setTooltip (tr ("Paste a YouTube link: the audio is downloaded and analysed automatically"));
        doubleButton.onClick = [this] { session.setSongTempo (session.getSong().bpm * 2.0); };
        halfButton.onClick = [this] { session.setSongTempo (session.getSong().bpm / 2.0); };
        tapButton.onClick = [this] { session.tapSongTempo(); };
        earlierButton.onClick = [this] { session.nudgeSong (-0.01); };
        laterButton.onClick = [this] { session.nudgeSong (0.01); };
        placeButton.onClick = [this] { session.placeSongOnBar2(); };
        removeButton.onClick = [this] { session.removeSong(); };
        slowerButton.onClick = [this] { session.setSongSpeed (session.getSongSpeed() - 0.05); };
        fasterButton.onClick = [this] { session.setSongSpeed (session.getSongSpeed() + 0.05); };
        downButton.onClick = [this] { session.setSongSemitones (session.getSong().semitones - 1); };
        upButton.onClick = [this] { session.setSongSemitones (session.getSong().semitones + 1); };
        slowerButton.setTooltip (tr ("Slow the song down without changing its pitch (down to 50\u00a0%)"));
        fasterButton.setTooltip (tr ("Go back toward the original speed"));
        downButton.setTooltip (tr ("Transpose down a semitone, without changing the tempo"));
        upButton.setTooltip (tr ("Transpose up a semitone, without changing the tempo"));

        doubleButton.setTooltip (tr ("The detected tempo is half the real one: double it"));
        halfButton.setTooltip (tr ("The detected tempo is double the real one: halve it"));
        tapButton.setTooltip (tr ("Tap at least four times in rhythm during playback"));
        earlierButton.setTooltip (tr ("Move the song 10 ms earlier against the grid"));
        laterButton.setTooltip (tr ("Move the song 10 ms later against the grid"));
        placeButton.setTooltip (tr ("Put the song's first bar back on bar 2 (bar 1 is the count-in)"));
        removeButton.setTooltip (tr ("Remove the song from the project"));

        for (auto* b : { &chooseButton, &youtubeButton, &doubleButton, &halfButton, &tapButton, &earlierButton, &laterButton, &placeButton, &removeButton,
                         &slowerButton, &fasterButton, &downButton, &upButton })
        {
            b->setFontSize (12.5f);
            addAndMakeVisible (b);
        }
    }

    void refresh()
    {
        const bool has = session.hasSong();
        chooseButton.setButtonText (has ? tr ("Replace…") : tr ("Choose a Song…"));
        chooseButton.setEnabled (! session.isImportingSong());
        youtubeButton.setEnabled (! session.isImportingSong() && ! session.isDownloadingSong());
        for (auto* b : { &doubleButton, &halfButton, &tapButton, &earlierButton, &laterButton, &placeButton, &removeButton,
                         &slowerButton, &fasterButton, &downButton, &upButton })
            b->setVisible (has);
        slowerButton.setEnabled (session.getSongSpeed() > 0.501);
        fasterButton.setEnabled (session.getSongSpeed() < 0.999);
        downButton.setEnabled (session.getSong().semitones > -6);
        upButton.setEnabled (session.getSong().semitones < 6);
        tapButton.setButtonText (session.getTapCount() > 0 && session.getTapCount() < 4 ? tr ("Tap ") + juce::String (session.getTapCount()) + "/4"
                                                                                       : juce::String (tr ("Tap")));

        const auto signature = session.getSong().title + juce::String (session.getSong().bpm) + juce::String ((int) session.isImportingSong()) + juce::String (session.isDownloadingSong() ? session.getSongDownloadPercent() + 2 : 0)
                               + juce::String (session.getSongStart(), 3) + juce::String ((int) has) + juce::String (session.getSongSpeed())
                               + juce::String (session.getSong().semitones) + juce::String ((int) session.isRenderingSong());
        if (signature != lastSignature)
        {
            lastSignature = signature;
            resized();
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& song = session.getSong();
        juce::String subtitle;
        if (session.isDownloadingSong())
            subtitle = tr ("YouTube download… ") + (session.getSongDownloadPercent() >= 0 ? juce::String (session.getSongDownloadPercent()) + i18n::percent() : juce::String());
        else if (session.isImportingSong())
            subtitle = tr ("Analysing tempo and parts…");
        else if (song.isSet())
            subtitle = song.title;
        else
            subtitle = tr ("Play along to a real track: drop an MP3, M4A, WAV, AIFF or FLAC onto the window");
        drawHead (g, headArea, tr ("Song"), subtitle);

        if (! song.isSet())
            return;

        // Tempo block: the BPM in use, and what was detected.
        g.setColour (theme::text);
        g.setFont (fonts::display (22.0f));
        fonts::drawTabular (g, juce::String (song.bpm, 1).replaceCharacter ('.', i18n::decimalPoint()), tempoArea.toFloat().withTrimmedBottom (16.0f), juce::Justification::centredLeft);
        g.setColour (theme::textFaint);
        g.setFont (fonts::semibold (10.5f));
        const bool corrected = std::abs (song.bpm - song.detectedBpm) > 0.05;
        g.drawText (song.speed < 0.999 ? utf8 ("BPM \xc2\xb7 ") + juce::String (song.bpm * song.speed, 1).replaceCharacter ('.', i18n::decimalPoint()) + tr (" at ")
                                             + juce::String (juce::roundToInt (song.speed * 100.0)) + i18n::percent()
                    : corrected ? tr ("BPM · detected ") + juce::String (song.detectedBpm, 1).replaceCharacter ('.', i18n::decimalPoint())
                                : tr ("BPM detected"),
                    tempoArea.withTop (tempoArea.getBottom() - 16), juce::Justification::centredLeft, true);

        ui::drawSectionLabel (g, "Tempo", tempoLabel.toFloat());
        ui::drawSectionLabel (g, tr ("Alignment"), placeLabel.toFloat());
        ui::drawSectionLabel (g, tr ("Practise"), practiceLabel.toFloat());

        // Speed and key, between their − / + buttons.
        g.setColour (theme::text);
        g.setFont (fonts::semibold (12.5f));
        g.drawText (session.isRenderingSong() ? tr ("computing…") : juce::String (juce::roundToInt (song.speed * 100.0)) + i18n::percent(),
                    speedValue, juce::Justification::centred, false);
        g.drawText ((song.semitones > 0 ? "+" : "") + juce::String (song.semitones) + tr (" st"), keyValue, juce::Justification::centred, false);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        const bool narrow = isNarrow (*this);
        headArea = narrow ? area.removeFromTop (52) : area.removeFromLeft (std::min (150, area.getWidth() / 8));
        if (narrow)
            area.removeFromTop (6);
        else
            area.removeFromLeft (14);

        auto place = [] (juce::Rectangle<int>& row, ui::PillButton& b, int gap = 8)
        {
            const int w = b.getIdealWidth();
            b.setBounds (row.removeFromLeft (w));
            row.removeFromLeft (gap);
        };

        if (! session.hasSong())
        {
            auto row = narrow ? area.removeFromTop (36) : area.withSizeKeepingCentre (area.getWidth(), 36);
            place (row, chooseButton);
            place (row, youtubeButton);
            tempoArea = tempoLabel = placeLabel = practiceLabel = speedValue = keyValue = {};
            return;
        }

        if (narrow)
        {
            tempoLabel = area.removeFromTop (16);
            tempoArea = area.removeFromTop (40);
            auto row = area.removeFromTop (32);
            for (auto* b : { &doubleButton, &halfButton, &tapButton })
                place (row, *b);
            area.removeFromTop (12);
            placeLabel = area.removeFromTop (16);
            row = area.removeFromTop (32);
            for (auto* b : { &earlierButton, &laterButton, &placeButton })
                place (row, *b);
            area.removeFromTop (12);
            practiceLabel = area.removeFromTop (16);
            row = area.removeFromTop (32);
            place (row, slowerButton, 0);
            speedValue = row.removeFromLeft (64);
            place (row, fasterButton, 18);
            place (row, downButton, 0);
            keyValue = row.removeFromLeft (48);
            place (row, upButton);
            area.removeFromTop (12);
            row = area.removeFromTop (32);
            place (row, chooseButton);
            place (row, youtubeButton);
            place (row, removeButton);
            return;
        }

        // Wide: tempo | calage | file, side by side.
        auto column = [&] (int width) { auto c = area.removeFromLeft (width); area.removeFromLeft (24); return c.withSizeKeepingCentre (width, 86); };
        auto tempo = column (190);
        tempoLabel = tempo.removeFromTop (16);
        tempoArea = tempo.removeFromTop (38);
        auto row = tempo.removeFromTop (30);
        for (auto* b : { &doubleButton, &halfButton, &tapButton })
            place (row, *b, 6);

        auto calage = column (240);
        placeLabel = calage.removeFromTop (16);
        calage.removeFromTop (38);
        row = calage.removeFromTop (30);
        for (auto* b : { &earlierButton, &laterButton, &placeButton })
            place (row, *b, 6);

        auto practice = column (345);
        practiceLabel = practice.removeFromTop (16);
        practice.removeFromTop (38);
        row = practice.removeFromTop (30);
        place (row, slowerButton, 0);
        speedValue = row.removeFromLeft (58);
        place (row, fasterButton, 14);
        place (row, downButton, 0);
        keyValue = row.removeFromLeft (44);
        place (row, upButton, 0);

        auto file = area.withSizeKeepingCentre (area.getWidth(), 86).withTrimmedTop (54);
        place (file, chooseButton);
        place (file, youtubeButton);
        place (file, removeButton);
    }

private:
    Inspector& owner;
    Session& session;
    ui::PillButton chooseButton, youtubeButton, doubleButton, halfButton, tapButton, earlierButton, laterButton, placeButton, removeButton;
    ui::PillButton slowerButton, fasterButton, downButton, upButton;
    juce::Rectangle<int> headArea, tempoArea, tempoLabel, placeLabel, practiceLabel, speedValue, keyValue;
    juce::String lastSignature;
};

//==============================================================================
Inspector::Inspector (Session& s) : session (s)
{
    ampPanel = std::make_unique<AmpPanel> (session);
    looperPanel = std::make_unique<LooperPanel> (session);
    cameraPanel = std::make_unique<CameraPanel> (session);
    songPanel = std::make_unique<SongPanel> (*this, session);
    holder.addChildComponent (*songPanel);
    holder.addAndMakeVisible (*ampPanel);
    holder.addChildComponent (*looperPanel);
    holder.addChildComponent (*cameraPanel);
    viewport.setViewedComponent (&holder, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    viewport.setWantsKeyboardFocus (false);
    addAndMakeVisible (viewport);
}

Inspector::~Inspector() = default;

void Inspector::show (Track track)
{
    shown = track;
    ampPanel->setVisible (track == Track::amp);
    looperPanel->setVisible (track == Track::loop);
    cameraPanel->setVisible (track == Track::camera);
    songPanel->setVisible (track == Track::song);
    viewport.setViewPosition (0, 0);
    resized();
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
    if (songPanel->isVisible())
        songPanel->refresh();
}

void Inspector::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (surface);
    g.setColour (border);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());

    const auto accent = shown == Track::loop ? loopAccent : shown == Track::camera ? camAccent : shown == Track::song ? songAccent : ampAccent;
    g.setColour (accent);
    g.fillRect (0, 1, 3, getHeight() - 1);
}

void Inspector::resized()
{
    const auto area = getLocalBounds().reduced (18, 14).withTrimmedTop (2).withTrimmedRight (1);
    viewport.setBounds (area);
    const bool narrow = area.getWidth() < 560;
    const int height = narrow ? std::max (area.getHeight(), getNarrowHeight()) : area.getHeight();
    holder.setSize (area.getWidth() - (height > area.getHeight() ? viewport.getScrollBarThickness() + 4 : 0), height);
    for (auto* panel : std::initializer_list<juce::Component*> { ampPanel.get(), looperPanel.get(), cameraPanel.get(), songPanel.get() })
        panel->setBounds (holder.getLocalBounds());
}

int Inspector::getNarrowHeight() const
{
    switch (shown)
    {
        case Track::amp:    return 56 + 3 * (64 + 22 + 24 + 4) + 74 + 92 + 4 * 8;
        case Track::loop:   return 52 + 8 + 2 * 36 + 8;
        case Track::camera: return 52 + 6 + 28 + 8 + 32 + 10 + 26 + 10 + 120;
        case Track::song:   return 52 + 6 + 16 + 40 + 32 + 12 + 16 + 32 + 12 + 16 + 32 + 12 + 32;
        default:            return 0;
    }
}
