#include "MainComponent.h"
#include "Theme.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <iostream>

MainComponent::MainComponent()
{
    setLookAndFeel (&lookAndFeel);

    brandLabel.setText ("Roadie", juce::dontSendNotification);
    brandLabel.setFont (juce::Font (18.0f, juce::Font::bold));
    addAndMakeVisible (brandLabel);

    cpuPill.setText ("-- % CPU");
    latencyPill.setText ("-- ms");
    addAndMakeVisible (cpuPill);
    addAndMakeVisible (latencyPill);

    settingsButton.onClick = [this] { openAudioSettings(); };
    settingsButton.setTooltip (juce::CharPointer_UTF8 ("Choisir la carte son et la taille de buffer"));
    addAndMakeVisible (settingsButton);

    ampTitle.setText ("Ampli", juce::dontSendNotification);
    ampTitle.setFont (juce::Font (16.0f, juce::Font::bold));
    addAndMakeVisible (ampTitle);

    ampSubtitle.setText (juce::CharPointer_UTF8 ("Aucune capture charg\xc3\xa9" "e \xe2\x80\x94 signal brut"),
                          juce::dontSendNotification);
    ampSubtitle.setFont (13.0f);
    ampSubtitle.setColour (juce::Label::textColourId, roadie::theme::textMuted);
    addAndMakeVisible (ampSubtitle);

    loadNamButton.onClick = [this] { openNamFileChooser(); };
    loadNamButton.setTooltip (juce::CharPointer_UTF8 ("Charger une capture d'ampli (.nam)"));
    addAndMakeVisible (loadNamButton);

    cabLabel.setText (juce::CharPointer_UTF8 ("Pas de cabinet (IR) \xe2\x80\x94 sortie directe"),
                       juce::dontSendNotification);
    cabLabel.setFont (13.0f);
    cabLabel.setColour (juce::Label::textColourId, roadie::theme::textMuted);
    addAndMakeVisible (cabLabel);

    loadIrButton.onClick = [this] { openIrFileChooser(); };
    loadIrButton.setTooltip (juce::CharPointer_UTF8 ("Charger une r\xc3\xa9ponse impulsionnelle de cabinet (.wav)"));
    addAndMakeVisible (loadIrButton);

    for (auto* knob : { &inputGainKnob, &outputVolumeKnob })
    {
        knob->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        knob->setRange (-24.0, 24.0, 0.1);
        knob->setValue (0.0, juce::dontSendNotification);
        knob->setDoubleClickReturnValue (true, 0.0);
        addAndMakeVisible (knob);
    }
    inputGainKnob.setTooltip (juce::CharPointer_UTF8 ("Gain avant l'ampli, en dB (double-clic pour revenir \xc3\xa0 0)"));
    outputVolumeKnob.setTooltip (juce::CharPointer_UTF8 ("Volume de sortie, en dB (double-clic pour revenir \xc3\xa0 0)"));

    inputGainTitle.setText (juce::CharPointer_UTF8 ("Gain d'entr\xc3\xa9" "e"), juce::dontSendNotification);
    outputVolumeTitle.setText ("Volume", juce::dontSendNotification);
    for (auto* l : { &inputGainTitle, &outputVolumeTitle })
    {
        l->setJustificationType (juce::Justification::centred);
        l->setFont (12.0f);
        l->setColour (juce::Label::textColourId, roadie::theme::textMuted);
        addAndMakeVisible (l);
    }

    for (auto* l : { &inputGainValue, &outputVolumeValue })
    {
        l->setJustificationType (juce::Justification::centred);
        l->setFont (12.0f);
        l->setColour (juce::Label::textColourId, roadie::theme::textFaint);
        addAndMakeVisible (l);
    }
    inputGainValue.setText ("0.0 dB", juce::dontSendNotification);
    outputVolumeValue.setText ("0.0 dB", juce::dontSendNotification);

    inputGainKnob.onValueChange = [this]
    {
        ampEngine.setInputGainDb ((float) inputGainKnob.getValue());
        updateKnobLabel (inputGainValue, inputGainKnob.getValue());
    };
    outputVolumeKnob.onValueChange = [this]
    {
        ampEngine.setOutputVolumeDb ((float) outputVolumeKnob.getValue());
        updateKnobLabel (outputVolumeValue, outputVolumeKnob.getValue());
    };

    meterLabel.setText (juce::CharPointer_UTF8 ("Niveau d'entr\xc3\xa9" "e"), juce::dontSendNotification);
    meterLabel.setFont (11.0f);
    meterLabel.setColour (juce::Label::textColourId, roadie::theme::textFaint);
    addAndMakeVisible (meterLabel);
    addAndMakeVisible (levelMeter);

    looperTitle.setText ("Looper", juce::dontSendNotification);
    looperTitle.setFont (juce::Font (16.0f, juce::Font::bold));
    addAndMakeVisible (looperTitle);

    looperStatusLabel.setText ("Vide", juce::dontSendNotification);
    looperStatusLabel.setFont (13.0f);
    looperStatusLabel.setColour (juce::Label::textColourId, roadie::theme::textMuted);
    looperStatusLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (looperStatusLabel);

    exportLoopButton.onClick = [this] { exportLoopToWav(); };
    exportLoopButton.setTooltip (juce::CharPointer_UTF8 ("Exporter la boucle actuelle en fichier .wav"));
    addAndMakeVisible (exportLoopButton);

    recordButton.onClick = [this] { looper.requestRecord(); };
    recordButton.setTooltip (juce::CharPointer_UTF8 ("D\xc3\xa9" "marrer / arr\xc3\xaa"
                                                        "ter l'enregistrement de la boucle (R)"));
    overdubButton.onClick = [this] { looper.requestOverdub(); };
    overdubButton.setTooltip (juce::CharPointer_UTF8 ("Ajouter une couche par-dessus la boucle existante (O)"));
    playStopButton.onClick = [this] { looper.requestPlayStop(); };
    playStopButton.setTooltip (juce::CharPointer_UTF8 ("Mettre la boucle en lecture ou en pause (Espace)"));
    undoButton.onClick = [this] { looper.requestUndo(); };
    undoButton.setTooltip (juce::CharPointer_UTF8 ("Annuler le dernier overdub (U) \xe2\x80\x94 un seul niveau"));
    clearButton.onClick = [this] { looper.requestClear(); };
    clearButton.setTooltip (juce::CharPointer_UTF8 ("Effacer la boucle et repartir de z\xc3\xa9ro (Maj+Retour arri\xc3\xa8re)"));
    for (auto* b : { &recordButton, &overdubButton, &playStopButton, &undoButton, &clearButton })
        addAndMakeVisible (b);

    bpmSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    bpmSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    bpmSlider.setRange (40.0, 220.0, 1.0);
    bpmSlider.setValue (100.0, juce::dontSendNotification);
    bpmSlider.setDoubleClickReturnValue (true, 100.0);
    bpmSlider.setTooltip (juce::CharPointer_UTF8 ("Tempo du m\xc3\xa9tronome (double-clic pour revenir \xc3\xa0 100)"));
    bpmSlider.onValueChange = [this]
    {
        metronome.setBpm (bpmSlider.getValue());
        bpmValueLabel.setText (juce::String ((int) bpmSlider.getValue()) + " BPM", juce::dontSendNotification);
    };
    addAndMakeVisible (bpmSlider);

    bpmValueLabel.setText ("100 BPM", juce::dontSendNotification);
    bpmValueLabel.setFont (12.0f);
    bpmValueLabel.setJustificationType (juce::Justification::centredLeft);
    bpmValueLabel.setColour (juce::Label::textColourId, roadie::theme::textMuted);
    addAndMakeVisible (bpmValueLabel);

    metronomeToggle.onClick = [this] { metronome.setEnabled (metronomeToggle.getToggleState()); };
    metronomeToggle.setTooltip (juce::CharPointer_UTF8 ("Clic audible, jamais enregistr\xc3\xa9 dans la boucle"));
    addAndMakeVisible (metronomeToggle);

    addAndMakeVisible (loopProgress);
    updateLooperStatus(); // correct enabled/disabled state from the first frame, don't wait for the timer

    setWantsKeyboardFocus (true);

    setAudioChannels (1, 2);

    // The NAM model itself costs single-digit % CPU even at tiny buffers
    // (benchmarked separately), so the default negotiated buffer size is the
    // real latency driver. Ask for something small; the backend will clamp
    // to its nearest supported value if 128 isn't exactly available.
    {
        auto setup = deviceManager.getAudioDeviceSetup();
        setup.bufferSize = 64;
        deviceManager.setAudioDeviceSetup (setup, true);
    }

    setSize (520, 612);
    startTimerHz (15);
    grabKeyboardFocus();
}

MainComponent::~MainComponent()
{
    stopTimer();
    setLookAndFeel (nullptr);
    shutdownAudio();
}

void MainComponent::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    ampEngine.prepare (sampleRate, samplesPerBlockExpected);
    looper.prepare (sampleRate);
    metronome.prepare (sampleRate);

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        auto roundTripMs = ((double) samplesPerBlockExpected + device->getInputLatencyInSamples()
                             + device->getOutputLatencyInSamples())
                          / sampleRate * 1000.0;
        std::cerr << "[Roadie] device=" << device->getName()
                   << " sampleRate=" << sampleRate
                   << " bufferSize=" << samplesPerBlockExpected
                   << " inputLatencySamples=" << device->getInputLatencyInSamples()
                   << " outputLatencySamples=" << device->getOutputLatencyInSamples()
                   << " estRoundTripMs=" << roundTripMs
                   << std::endl;
    }
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    auto* buffer = bufferToFill.buffer;
    auto numChannels = buffer->getNumChannels();
    if (numChannels < 1)
        return;

    // AudioAppComponent already copies the live input into channel 0 before this
    // call. Run it through the amp model, then mirror the result to the other
    // output channels so a mono guitar monitors in stereo.
    auto* channelData = buffer->getWritePointer (0, bufferToFill.startSample);
    ampEngine.process (channelData, bufferToFill.numSamples);
    looper.process (channelData, bufferToFill.numSamples);
    metronome.process (channelData, bufferToFill.numSamples);

    for (int channel = 1; channel < numChannels; ++channel)
        buffer->copyFrom (channel, bufferToFill.startSample,
                           *buffer, 0, bufferToFill.startSample,
                           bufferToFill.numSamples);
}

void MainComponent::releaseResources()
{
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    // Looper shortcuts so the player doesn't need the mouse mid-performance.
    if (key == juce::KeyPress::spaceKey)
    {
        looper.requestPlayStop();
        return true;
    }

    const auto character = key.getTextCharacter();

    if (character == 'r' || character == 'R')
    {
        looper.requestRecord();
        return true;
    }
    if (character == 'o' || character == 'O')
    {
        looper.requestOverdub();
        return true;
    }
    if (character == 'u' || character == 'U')
    {
        looper.requestUndo();
        return true;
    }
    if (key == juce::KeyPress (juce::KeyPress::backspaceKey, juce::ModifierKeys::shiftModifier, 0))
    {
        looper.requestClear();
        return true;
    }

    return false;
}

void MainComponent::timerCallback()
{
    auto cpu = deviceManager.getCpuUsage();
    cpuPill.setText (juce::String (juce::roundToInt (cpu * 100)) + "% CPU");

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        auto bufferSize = device->getCurrentBufferSizeSamples();
        auto sampleRate = device->getCurrentSampleRate();
        auto totalSamples = bufferSize + device->getOutputLatencyInSamples() + device->getInputLatencyInSamples();
        auto latencyMs = sampleRate > 0.0 ? ((double) totalSamples / sampleRate) * 1000.0 : 0.0;
        latencyPill.setText (juce::String (latencyMs, 1) + " ms");
    }
    else
    {
        latencyPill.setText ("-- ms");
    }

    levelMeter.setLevel (ampEngine.getPeakLevel());
    updateLooperStatus();
}

void MainComponent::updateLooperStatus()
{
    using State = Looper::State;
    const auto s = looper.getState();
    const bool hasContent = looper.getRecordedLengthSamples() > 0;

    juce::String text;
    switch (s)
    {
        case State::Empty:        text = "Vide"; break;
        case State::Recording:    text = juce::CharPointer_UTF8 ("Enregistrement\xe2\x80\xa6"); break;
        case State::Playing:      text = juce::CharPointer_UTF8 ("Lecture \xe2\x80\x94 "); break;
        case State::Overdubbing:  text = juce::CharPointer_UTF8 ("Overdub\xe2\x80\xa6 \xe2\x80\x94 "); break;
        case State::Stopped:      text = juce::CharPointer_UTF8 ("Pause \xe2\x80\x94 "); break;
    }
    if (hasContent && s != State::Recording)
        text << juce::String (looper.getLoopLengthSeconds(), 1) << " s";

    looperStatusLabel.setText (text, juce::dontSendNotification);
    loopProgress.setProgress (looper.getPlayheadFraction());
    loopProgress.setRecording (s == State::Recording);

    // Buttons reflect what's actually legal to do from the current engine
    // state, rather than always being clickable and silently no-op-ing.
    using namespace roadie::theme;

    recordButton.setButtonText (s == State::Recording ? juce::String (juce::CharPointer_UTF8 ("Stop"))
                                                        : juce::String (juce::CharPointer_UTF8 ("Enregistrer")));
    recordButton.setEnabled (s == State::Empty || s == State::Recording);
    recordButton.setColour (juce::TextButton::buttonColourId, s == State::Recording ? rec : surfaceRaised);

    overdubButton.setButtonText (s == State::Overdubbing ? juce::String ("Stop overdub") : juce::String ("Overdub"));
    overdubButton.setEnabled (hasContent && (s == State::Playing || s == State::Overdubbing || s == State::Stopped));
    overdubButton.setColour (juce::TextButton::buttonColourId,
                              s == State::Overdubbing ? loopAccent.withAlpha (0.35f) : surfaceRaised);

    playStopButton.setButtonText (s == State::Playing || s == State::Overdubbing
                                       ? juce::String (juce::CharPointer_UTF8 ("Pause"))
                                       : juce::String (juce::CharPointer_UTF8 ("Lecture")));
    playStopButton.setEnabled (hasContent && s != State::Recording && s != State::Overdubbing);

    undoButton.setEnabled (looper.isUndoAvailable() && s != State::Overdubbing);
    clearButton.setEnabled (s != State::Empty);
    exportLoopButton.setEnabled (hasContent && s != State::Recording && s != State::Overdubbing);

    // Component-level alpha so disabled buttons are unmistakably dimmed
    // (background-colour tricks alone read too close to the enabled state
    // when every surface in this theme is already dark).
    for (auto* b : { &recordButton, &overdubButton, &playStopButton, &undoButton, &clearButton, &exportLoopButton })
        b->setAlpha (b->isEnabled() ? 1.0f : 0.35f);
}

void MainComponent::openNamFileChooser()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Choisis une capture NAM (.nam)",
                                                         juce::File(),
                                                         "*.nam");

    constexpr auto flags = juce::FileBrowserComponent::openMode
                          | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File())
            return;

        juce::String error;
        if (ampEngine.loadModel (file, error))
            ampSubtitle.setText (ampEngine.getModelName(), juce::dontSendNotification);
        else
            ampSubtitle.setText ("Erreur : " + error, juce::dontSendNotification);
    });
}

void MainComponent::openIrFileChooser()
{
    irFileChooser = std::make_unique<juce::FileChooser> ("Choisis une IR de cabinet (.wav)",
                                                           juce::File(),
                                                           "*.wav");

    constexpr auto flags = juce::FileBrowserComponent::openMode
                          | juce::FileBrowserComponent::canSelectFiles;

    irFileChooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File())
            return;

        ampEngine.loadImpulseResponse (file);
        cabLabel.setText (ampEngine.getIrName(), juce::dontSendNotification);
    });
}

void MainComponent::exportLoopToWav()
{
    const auto loopState = looper.getState();
    if (loopState == Looper::State::Recording || loopState == Looper::State::Overdubbing)
    {
        looperStatusLabel.setText (juce::CharPointer_UTF8 ("Arr\xc3\xaa"
                                                             "te l'enregistrement avant d'exporter"),
                                    juce::dontSendNotification);
        return;
    }

    const int numSamples = looper.getRecordedLengthSamples();
    if (numSamples <= 0)
        return;

    exportFileChooser = std::make_unique<juce::FileChooser> ("Exporter la boucle en WAV",
                                                               juce::File::getSpecialLocation (
                                                                   juce::File::userDocumentsDirectory),
                                                               "*.wav");

    constexpr auto flags = juce::FileBrowserComponent::saveMode
                          | juce::FileBrowserComponent::warnAboutOverwriting;

    exportFileChooser->launchAsync (flags, [this, numSamples] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File())
            return;

        file.deleteFile();
        std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
        if (stream == nullptr)
            return;

        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wavFormat.createWriterFor (stream.get(), looper.getSampleRate(), 1, 24, {}, 0));

        if (writer == nullptr)
            return;

        stream.release(); // the writer now owns the stream

        const auto& data = looper.getBufferForExport();
        const float* channels[1] = { data.data() };
        writer->writeFromFloatArrays (channels, 1, numSamples);
    });
}

void MainComponent::openAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (deviceManager, 1, 2, 2, 2, false, false, true, false);
    selector->setSize (460, 420);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector);
    options.dialogTitle = juce::CharPointer_UTF8 ("R\xc3\xa9glages audio");
    options.dialogBackgroundColour = roadie::theme::surface;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

void MainComponent::updateKnobLabel (juce::Label& label, double valueDb)
{
    label.setText (juce::String (valueDb, 1) + " dB", juce::dontSendNotification);
}

void MainComponent::Pill::paint (juce::Graphics& g)
{
    using namespace roadie::theme;
    auto bounds = getLocalBounds().toFloat();
    g.setColour (surfaceSunken);
    g.fillRoundedRectangle (bounds, bounds.getHeight() * 0.5f);
    g.setColour (border);
    g.drawRoundedRectangle (bounds.reduced (0.5f), bounds.getHeight() * 0.5f, 1.0f);
    g.setColour (textMuted);
    g.setFont (12.0f);
    g.drawText (text, getLocalBounds(), juce::Justification::centred);
}

void MainComponent::LevelMeter::paint (juce::Graphics& g)
{
    using namespace roadie::theme;
    auto bounds = getLocalBounds().toFloat();
    g.setColour (surfaceSunken);
    g.fillRoundedRectangle (bounds, bounds.getHeight() * 0.5f);

    auto db = juce::Decibels::gainToDecibels (level, -60.0f);
    auto proportion = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);

    if (proportion > 0.0f)
    {
        auto fill = bounds.withWidth (bounds.getWidth() * proportion);
        g.setColour (ampAccent);
        g.fillRoundedRectangle (fill, bounds.getHeight() * 0.5f);
    }
}

void MainComponent::ProgressBar::paint (juce::Graphics& g)
{
    using namespace roadie::theme;
    auto bounds = getLocalBounds().toFloat();
    g.setColour (surfaceSunken);
    g.fillRoundedRectangle (bounds, bounds.getHeight() * 0.5f);

    if (recording)
    {
        // Loop length isn't fixed yet while recording, so a fraction would be
        // meaningless — show the whole bar lit instead, signalling "capturing".
        g.setColour (rec);
        g.fillRoundedRectangle (bounds, bounds.getHeight() * 0.5f);
    }
    else if (progress > 0.0f)
    {
        auto fill = bounds.withWidth (bounds.getWidth() * juce::jlimit (0.0f, 1.0f, progress));
        g.setColour (loopAccent);
        g.fillRoundedRectangle (fill, bounds.getHeight() * 0.5f);
    }
}

void MainComponent::paint (juce::Graphics& g)
{
    using namespace roadie::theme;
    g.fillAll (background);

    for (auto* cardArea : { &ampCardArea, &looperCardArea })
    {
        g.setColour (surface);
        g.fillRoundedRectangle (cardArea->toFloat(), 16.0f);
        g.setColour (border);
        g.drawRoundedRectangle (cardArea->toFloat().reduced (0.5f), 16.0f, 1.0f);
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto header = area.removeFromTop (34);
    brandLabel.setBounds (header.removeFromLeft (90));
    settingsButton.setBounds (header.removeFromRight (96));
    header.removeFromRight (8);
    latencyPill.setBounds (header.removeFromRight (100));
    header.removeFromRight (6);
    cpuPill.setBounds (header.removeFromRight (84));

    area.removeFromTop (16);

    ampCardArea = area.removeFromTop (332);
    auto card = ampCardArea.reduced (18);

    auto titleRow = card.removeFromTop (28);
    ampTitle.setBounds (titleRow.removeFromLeft (120));
    loadNamButton.setBounds (titleRow.removeFromRight (190));

    ampSubtitle.setBounds (card.removeFromTop (20));
    card.removeFromTop (10);

    auto cabRow = card.removeFromTop (24);
    loadIrButton.setBounds (cabRow.removeFromRight (140));
    cabRow.removeFromRight (8);
    cabLabel.setBounds (cabRow);
    card.removeFromTop (14);

    auto knobRow = card.removeFromTop (118);
    auto knobWidth = knobRow.getWidth() / 2;
    auto inputArea = knobRow.removeFromLeft (knobWidth);
    auto outputArea = knobRow;

    auto layoutKnob = [] (juce::Rectangle<int> a, juce::Slider& knob, juce::Label& title, juce::Label& value)
    {
        title.setBounds (a.removeFromTop (16));
        auto knobBounds = a.removeFromTop (76).withSizeKeepingCentre (76, 76);
        knob.setBounds (knobBounds);
        value.setBounds (a);
    };

    layoutKnob (inputArea.reduced (20, 0), inputGainKnob, inputGainTitle, inputGainValue);
    layoutKnob (outputArea.reduced (20, 0), outputVolumeKnob, outputVolumeTitle, outputVolumeValue);

    card.removeFromTop (14);
    meterLabel.setBounds (card.removeFromTop (16));
    card.removeFromTop (4);
    levelMeter.setBounds (card.removeFromTop (10));

    area.removeFromTop (16);

    looperCardArea = area.removeFromTop (180);
    auto loopCard = looperCardArea.reduced (18);

    auto loopTitleRow = loopCard.removeFromTop (24);
    looperTitle.setBounds (loopTitleRow.removeFromLeft (80));
    exportLoopButton.setBounds (loopTitleRow.removeFromRight (120));
    loopTitleRow.removeFromRight (8);
    looperStatusLabel.setBounds (loopTitleRow);
    loopCard.removeFromTop (10);

    auto transportRow = loopCard.removeFromTop (34);
    auto transportWidth = transportRow.getWidth() / 5;
    recordButton.setBounds (transportRow.removeFromLeft (transportWidth).reduced (3, 0));
    overdubButton.setBounds (transportRow.removeFromLeft (transportWidth).reduced (3, 0));
    playStopButton.setBounds (transportRow.removeFromLeft (transportWidth).reduced (3, 0));
    undoButton.setBounds (transportRow.removeFromLeft (transportWidth).reduced (3, 0));
    clearButton.setBounds (transportRow.reduced (3, 0));
    loopCard.removeFromTop (14);

    auto bpmRow = loopCard.removeFromTop (26);
    metronomeToggle.setBounds (bpmRow.removeFromRight (130));
    bpmRow.removeFromRight (10);
    bpmValueLabel.setBounds (bpmRow.removeFromLeft (70));
    bpmSlider.setBounds (bpmRow);
    loopCard.removeFromTop (12);

    loopProgress.setBounds (loopCard.removeFromTop (8));
}
