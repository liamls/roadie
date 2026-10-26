#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_data_structures/juce_data_structures.h>

#include "AudioEngine.h"
#include "CameraCapture.h"
#include "CaptureLibrary.h"
#include "ClipExporter.h"
#include "Project.h"
#include "VideoPreview.h"

// Everything the UI drives, in one non-visual place: the audio device and
// engine, the camera, the capture library, the current project (its takes
// and the arrangement the timeline edits) and the persistent settings.
// Components call into it and listen for changes (sendChangeMessage)
// instead of talking to each other.
class Session : public juce::ChangeBroadcaster,
                private juce::Timer
{
public:
    Session();
    ~Session() override;

    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
    AudioEngine& getEngine() noexcept { return engine; }
    CameraCapture& getCamera() noexcept { return camera; }
    CaptureLibrary& getLibrary() noexcept { return library; }
    ClipExporter& getExporter() noexcept { return exporter; }
    const Project& getProject() const noexcept { return project; }

    //==============================================================================
    // Ampli
    void loadCapture (const juce::File& namFile);
    bool isLoadingCapture() const noexcept { return loadingCapture; }
    juce::String getCaptureName() const;
    juce::String getCaptureTag() const;
    void loadImpulseResponse (const juce::File& irFile);
    void clearImpulseResponse();
    juce::String getIrName() const;
    void addCaptureFolder (const juce::File& folder);

    enum class Knob { gain, bass, mid, treble, presence, volume };
    void setKnob (Knob knob, float db);
    float getKnob (Knob knob) const;
    static juce::Range<float> getKnobRange (Knob knob);

    //==============================================================================
    // Tempo
    void setBpm (double bpm);
    double getBpm() const;
    void setBeatsPerBar (int beats);
    int getBeatsPerBar() const;
    void setMetronomeOn (bool shouldBeOn);
    bool isMetronomeOn() const;
    double getSamplesPerBar() const;

    //==============================================================================
    // Transport: a linear timeline, in seconds.
    void recordLoop();
    void togglePlay();
    void stopAll();
    void returnToStart();
    void toggleOverdub();
    void undo();
    void clearLoop();

    bool isPlaying() const noexcept { return engine.isTransportRunning(); }
    double getPlayheadSeconds() const;
    void locate (double seconds);
    void jumpBars (int bars); // ← / → by one bar, snapped to the bar grid
    // The playhead was moved by hand recently (the monitor then previews the edit).
    bool wasRecentlyLocated() const;

    // Cycle zone (zone de répétition, C): the playhead loops between start and end.
    void setCycleRange (double start, double end);
    void setCycleOn (bool shouldBeOn);
    void toggleCycle() { setCycleOn (! project.cycleOn); }
    bool isCycleOn() const noexcept { return project.cycleOn; }
    juce::Range<double> getCycleRange() const { return { project.cycleStart, project.cycleEnd }; }

    double getSecondsPerBar() const { return 60.0 / bpm * beatsPerBar; }
    double getSecondsPerBeat() const { return 60.0 / bpm; }

    // A take = the amped guitar to WAV + the camera to MOV, started together;
    // it lands on the timeline as a region where it was played.
    void toggleTakeRecording();
    bool isRecordingTake() const noexcept { return recordingTake; }
    bool isFinishingTake() const noexcept { return finishingTake; }
    double getTakeSeconds() const;
    double getTakeStartSeconds() const; // timeline position of the take being recorded

    //==============================================================================
    // Tracks: Guitare (the amp + the take regions), Looper, Caméra.
    enum class Track { amp, loop, camera };
    void setMuted (Track track, bool muted);
    bool isMuted (Track track) const;
    void setSolo (Track track, bool solo);
    bool isSolo (Track track) const;
    void setTrackVolume (Track track, float db);
    float getTrackVolume (Track track) const;

    //==============================================================================
    // Arrangement editing. Every edit goes through applyEdit() so it can be
    // undone (⌘Z) and redone (⇧⌘Z).
    const Arrangement& getArrangement() const noexcept { return project.arrangement; }
    void applyEdit (const Arrangement& edited, const juce::String& actionName);
    bool canUndoEdit() const noexcept { return ! undoStack.empty(); }
    bool canRedoEdit() const noexcept { return ! redoStack.empty(); }
    juce::String getUndoEditName() const { return undoStack.empty() ? juce::String() : undoStack.back().name; }
    juce::String getRedoEditName() const { return redoStack.empty() ? juce::String() : redoStack.back().name; }
    void undoEdit();
    void redoEdit();

    enum class Selection { none, clip, loop };
    Selection getSelection() const noexcept { return selection; }
    int getSelectedClip() const noexcept { return selection == Selection::clip ? selectedClip : -1; }
    void selectClip (int index);
    void selectLoopRegion();
    void clearSelection();

    // Editing commands on the selection (or what is under the playhead).
    bool splitAtPlayhead();
    bool deleteSelection();
    bool duplicateSelection();
    void insertTake (int takeIndex, double atSeconds);
    void setLoopOpenEnded();

    // Takes not used by any region (deleted from the timeline, still on disk).
    bool isTakeUsed (int takeIndex) const;

    bool hasLoop() const;
    double getLoopLengthSeconds() const;
    // End of the arrangement's content, in seconds (0 when empty).
    double getContentEnd() const;

    // Decoded take audio and its peak envelope (100 peaks per second), for
    // drawing regions. Null when the take could not be read.
    struct TakeMedia
    {
        std::shared_ptr<const std::vector<float>> audio; // at the engine's sample rate
        double sampleRate = 0.0;
        std::vector<float> peaks;
        static constexpr double peaksPerSecond = 100.0;
    };
    const TakeMedia* getTakeMedia (int takeIndex) const;
    double getTakeLength (int takeIndex) const;

    //==============================================================================
    // Camera
    void openCamera (const juce::String& deviceName);
    void closeCamera();
    bool isCameraOpening() const noexcept { return cameraOpening; }
    juce::String getCameraError() const { return cameraError; }
    juce::String getCameraDescription() const;

    enum class CalibrationState { idle, starting, listening, done, failed };
    void startCalibration();
    CalibrationState getCalibrationState() const noexcept { return calibrationState; }
    juce::String getCalibrationMessage() const { return calibrationMessage; }
    bool isCalibrated() const noexcept { return calibrated; }
    double getSyncLagMs() const noexcept { return syncLagSeconds * 1000.0; }

    // The monitor shows the arrangement's video under the playhead while it
    // plays back (or while the camera is off, or right after a locate).
    bool isPreviewingArrangement() const noexcept { return previewing; }
    juce::Image getPreviewFrame() const { return preview.getFrame(); }
    int getPreviewFrameCounter() const { return preview.getFrameCounter(); }

    //==============================================================================
    // Takes and export
    const std::vector<Take>& getTakes() const noexcept { return project.takes; }

    // A stretch of the timeline rendered to MP4 (the arrangement's mix and
    // the video of its regions) or to WAV.
    struct ExportRequest
    {
        double startSeconds = 0.0, endSeconds = 0.0;
        bool vertical = true;
        juce::String title;
        bool burnInTitle = false;
        bool mirror = false;
    };
    bool hasVideoBetween (double start, double end) const;
    void exportClip (const ExportRequest& request, std::function<void (bool ok, juce::String message, juce::File file)> onDone);
    juce::File exportAudio (const ExportRequest& request, juce::String& error);
    bool exportLoopTo (const juce::File& wavFile, juce::String& error);

    // Offline mixdown of [start, end): what the player hears, minus the click.
    std::vector<float> renderMix (double start, double end, double sampleRate) const;

    //==============================================================================
    // Project
    void newProject();
    bool openProject (const juce::File& folderOrJson);
    bool saveProject();
    bool saveProjectAs (const juce::File& destination);
    juce::String getProjectName() const { return project.name; }
    bool hasProjectOnDisk() const { return project.getProjectFile().existsAsFile(); }

    //==============================================================================
    // A short user-facing notice (toast), cleared after a few seconds.
    juce::String getNotice() const;
    void notify (const juce::String& message);

    juce::PropertiesFile& getSettings() { return *settings.getUserSettings(); }

private:
    void timerCallback() override;
    void applyProjectToEngine();
    void captureEngineStateIntoProject();
    void updateMuteState();
    void finishTake (bool videoOk, double firstFrameHostSeconds);
    void finishCalibration();
    std::vector<float> copyLoop (double& sampleRate) const;
    void saveSettings();

    void loadTakeMedia (int takeIndex);
    void reloadAllTakeMedia();
    void pushArrangementToEngine();
    void pushCycleToEngine();
    void syncNewLoop();
    void updatePreview();
    std::vector<ClipExportSettings::VideoSegment> getVideoSegments (double start, double end) const;
    std::int64_t toSamples (double seconds) const;

    juce::ApplicationProperties settings;
    juce::AudioDeviceManager deviceManager;
    AudioEngine engine;
    CameraCapture camera;
    CaptureLibrary library;
    ClipExporter exporter;
    Project project;

    juce::ThreadPool loaderPool { juce::ThreadPoolOptions{}.withNumberOfThreads (1).withThreadName ("Roadie loader") };
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    bool loadingCapture = false;
    juce::String captureTag;

    std::array<float, 6> knobs {};
    double bpm = 100.0;
    int beatsPerBar = 4;
    bool metronomeOn = false;

    std::array<bool, 3> muted {}, soloed {};

    bool recordingTake = false, finishingTake = false;
    Take pendingTake;
    double takeStartedAt = 0.0;
    double pendingTakeStart = 0.0;

    struct EditState
    {
        Arrangement arrangement;
        int loopGeneration = 0;
        juce::String name;
    };
    std::vector<EditState> undoStack, redoStack;
    int loopGeneration = 0; // bumps when a new loop is recorded: older loop placements no longer apply
    int knownLoopRecordCount = 0;

    Selection selection = Selection::none;
    int selectedClip = -1;

    std::vector<TakeMedia> takeMedia;
    double mediaSampleRate = 0.0;

    VideoPreview preview;
    bool previewing = false;
    double lastLocateTime = -10.0;

    bool cameraOpening = false;
    juce::String cameraError;

    CalibrationState calibrationState = CalibrationState::idle;
    juce::String calibrationMessage;
    double calibrationStartedAt = 0.0;
    bool calibrated = false;
    double syncLagSeconds = 0.0;

    juce::String notice;
    double noticeTime = 0.0;
};
