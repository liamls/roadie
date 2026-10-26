#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "AmpEngine.h"
#include "Looper.h"
#include "Metronome.h"
#include "RoadieLookAndFeel.h"

class MainComponent : public juce::AudioAppComponent,
                       private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    // Small rounded status pill (CPU / latency readouts).
    class Pill : public juce::Component
    {
    public:
        void setText (const juce::String& newText) { text = newText; repaint(); }
        void paint (juce::Graphics&) override;

    private:
        juce::String text;
    };

    // Horizontal input-level meter, fed from AmpEngine's live peak reading.
    class LevelMeter : public juce::Component
    {
    public:
        void setLevel (float newLevel) { level = newLevel; repaint(); }
        void paint (juce::Graphics&) override;

    private:
        float level = 0.0f;
    };

    // Linear 0..1 progress bar (loop playhead position), turquoise accent.
    class ProgressBar : public juce::Component
    {
    public:
        void setProgress (float newProgress) { progress = newProgress; repaint(); }
        void setRecording (bool shouldShowAsRecording) { recording = shouldShowAsRecording; repaint(); }
        void paint (juce::Graphics&) override;

    private:
        float progress = 0.0f;
        bool recording = false;
    };

    void timerCallback() override;
    void openNamFileChooser();
    void openIrFileChooser();
    void openAudioSettings();
    void exportLoopToWav();
    void updateKnobLabel (juce::Label& label, double valueDb);
    void updateLooperStatus();

    RoadieLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this };

    juce::Label brandLabel;
    Pill cpuPill, latencyPill;
    juce::TextButton settingsButton { juce::CharPointer_UTF8 ("R\xc3\xa9glages") };

    juce::Label ampTitle, ampSubtitle;
    juce::TextButton loadNamButton { "Charger une capture NAM..." };

    juce::Label cabLabel;
    juce::TextButton loadIrButton { "Charger une IR..." };

    juce::Slider inputGainKnob, outputVolumeKnob;
    juce::Label inputGainTitle, outputVolumeTitle;
    juce::Label inputGainValue, outputVolumeValue;

    juce::Label meterLabel;
    LevelMeter levelMeter;

    juce::Label looperTitle, looperStatusLabel;
    juce::TextButton exportLoopButton { "Exporter (.wav)" };
    juce::TextButton recordButton { juce::CharPointer_UTF8 ("Enregistrer") };
    juce::TextButton overdubButton { "Overdub" };
    juce::TextButton playStopButton { "Lecture / Pause" };
    juce::TextButton undoButton { "Annuler" };
    juce::TextButton clearButton { juce::CharPointer_UTF8 ("Effacer") };
    juce::Slider bpmSlider;
    juce::Label bpmValueLabel;
    juce::ToggleButton metronomeToggle { juce::CharPointer_UTF8 ("M\xc3\xa9tronome") };
    ProgressBar loopProgress;

    AmpEngine ampEngine;
    Looper looper;
    Metronome metronome;
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<juce::FileChooser> irFileChooser;
    std::unique_ptr<juce::FileChooser> exportFileChooser;

    juce::Rectangle<int> ampCardArea;
    juce::Rectangle<int> looperCardArea;
};
