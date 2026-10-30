#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_data_structures/juce_data_structures.h>

#include "AudioEngine.h"
#include "Autosave.h"
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

    enum class Knob { gain, bass, mid, treble, presence, volume, gate };
    void setKnob (Knob knob, float db);
    float getKnob (Knob knob) const;
    static juce::Range<float> getKnobRange (Knob knob);

    // The signal chain: the gate at the input, and the blocks that can be
    // bypassed (Capture, EQ, Baffle).
    void setGateOn (bool shouldBeOn);
    bool isGateOn() const noexcept { return project.gateOn; }
    float getGateOpenness() { return engine.getAmp().getGate().getOpenness(); }
    void setBypassed (AmpEngine::Block block, bool bypassed);
    bool isBypassed (AmpEngine::Block block) const { return engine.getAmp().isBypassed (block); }

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
    // The pedal. From an empty loop it can wait for a count-in (a bar of
    // clicks) and close by itself after a number of bars; pressing during the
    // count-in cancels it.
    void recordLoop();
    void setLoopCountInOn (bool shouldBeOn);
    bool isLoopCountInOn() const noexcept { return loopCountIn; }
    void setLoopBars (int bars); // 0: free, closed by hand
    int getLoopBars() const noexcept { return loopBars; }
    // Deletes the Looper track along with its loop.
    void removeLooperTrack();

    // Magnet: a click on the timeline puts the playhead on the grid when it
    // lands close enough, and leaves it where it is otherwise.
    void setMagnetOn (bool shouldBeOn);
    bool isMagnetOn() const noexcept { return magnetOn; }

    // Library entries are deleted, not put aside: a capture or a song file
    // goes to the Trash; a take leaves the project (its regions with it) and
    // its files go to the Trash. Not undoable, so the panel asks first.
    bool deleteTake (int takeIndex);
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

    // Cycle zone (repeat zone, C): the playhead loops between start and end.
    void setCycleRange (double start, double end);
    void setCycleOn (bool shouldBeOn);
    // Forgets the zone altogether (the yellow band leaves the ruler).
    void clearCycle();
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

    // Count-in: a bar of clicks before the take, shown big on the monitor.
    // The take starts recording with it (the camera too, so it has time to
    // settle) and its region begins where the count-in ends — bar 2 when
    // recording from the start, where a song's first bar is.
    void setCountInOn (bool shouldBeOn);
    bool isCountInOn() const noexcept { return countInOn; }
    int getCountInBeatsLeft() const;

    //==============================================================================
    // Tracks: Guitar (the amp + the take regions), Looper, Camera, Song.
    enum class Track { amp, loop, camera, song };
    void setMuted (Track track, bool muted);
    bool isMuted (Track track) const;
    void setSolo (Track track, bool solo);
    bool isSolo (Track track) const;
    void setTrackVolume (Track track, float db);
    float getTrackVolume (Track track) const;
    void setMasterVolume (float db);
    float getMasterVolume() const noexcept { return project.masterVolumeDb; }

    // Record arming: ● records the armed tracks — Guitare (the take's
    // audio, needed for a take) and Video (the camera).
    void setArmed (Track track, bool armed);
    bool isArmed (Track track) const;

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

    // What is selected: one region of a track (the Guitare / Video regions are
    // `clip`), or a stretch of a track.
    enum class Selection { none, clip, loop, song, range };
    Selection getSelection() const noexcept { return selection; }
    int getSelectedClip() const noexcept { return selection == Selection::clip ? selectedClip : -1; }
    // The selected region of `track` (-1 if the selection is elsewhere).
    int getSelectedPiece (Track track) const noexcept;
    void selectPiece (Track track, int index);
    void selectClip (int index) { selectPiece (Track::amp, index); }
    void clearSelection();

    // A stretch of a track's regions picked by dragging over them, like
    // selecting text: it can be deleted (leaving a gap, or closing it) or
    // separated from its neighbours so it moves on its own.
    void selectRange (Track track, double start, double end);
    juce::Range<double> getSelectedRange() const noexcept { return selection == Selection::range ? selectedRange : juce::Range<double>(); }
    Track getSelectedRangeTrack() const noexcept { return selectedRangeTrack; }
    // The timeline stretch of the selection (a region or a range), empty if none.
    juce::Range<double> getSelectionExtent() const;

    // The regions of a track: Guitare and Video share theirs.
    static std::vector<Clip>& piecesOf (Arrangement& a, Track track) noexcept;
    static const std::vector<Clip>& piecesOf (const Arrangement& a, Track track) noexcept;

    // Editing commands on the selection (or what is under the playhead).
    // With a range selected, the cut is made at both of its edges.
    bool splitAtPlayhead();
    bool deleteSelection();
    bool deleteSelectedRange (bool closeGap);
    bool duplicateSelection();
    void insertTake (int takeIndex, double atSeconds);
    // Looper: the loop played once from `atSeconds` (after its regions were
    // deleted), or stretched so it repeats until the end of the arrangement.
    void placeLoopAt (double atSeconds);
    void extendLoopToEnd (int pieceIndex);
    // The song put back on the timeline, whole, after its regions were deleted.
    void placeSongAt (double atSeconds);

    // Takes not used by any region (deleted from the timeline, still on disk).
    bool isTakeUsed (int takeIndex) const;

    bool hasLoop() const;
    // The Looper track is not there by default: it appears when added by the
    // player, or as soon as a loop is recorded. It can be removed while empty.
    bool isLooperTrackVisible() const;
    void setLooperTrackVisible (bool shouldBeVisible);
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
    // Morceau: a backing track to play along with. Imported in the
    // background (decoded, copied into the project, analysed), then placed so
    // that its first bar starts on bar 2 — bar 1 is the count-in — with the
    // project's tempo set to the song's and the metronome off.
    static bool isSongFile (const juce::File& file);
    // Re-amping: the take's dry guitar, played again through the amp as it is
    // set now (capture, EQ, gate, cabinet). Takes recorded before this existed
    // have no dry file and can't.
    bool canReampTake (int takeIndex) const;
    void reampTake (int takeIndex);

    void importSong (const juce::File& audioFile);
    // The backing track from a YouTube link: downloads the audio with yt-dlp
    // (embedded in the app bundle), then imports it like any file —
    // tempo, first bar and parts included.
    void importSongFromUrl (const juce::String& url);
    bool isDownloadingSong() const noexcept { return downloadingSong; }
    // 0-100 while the audio comes down, -1 while the size is still unknown.
    int getSongDownloadPercent() const noexcept { return songDownloadPercent; }
    static bool isYouTubeUrl (const juce::String& text);
    bool isImportingSong() const noexcept { return importingSong; }
    void removeSong();
    bool hasSong() const noexcept { return project.song.isSet(); }
    const Song& getSong() const noexcept { return project.song; }
    double getSongStart() const noexcept { return project.arrangement.songStart; }

    // The song as it plays on the timeline (slowed down: longer). The
    // stored analysis is in the file's own time.
    double getSongSpeed() const noexcept { return project.song.speed; }
    double getSongLength() const noexcept { return project.song.duration / project.song.speed; }
    double getSongDownbeat() const noexcept { return project.song.firstDownbeat / project.song.speed; }

    // Practice: slower (50 … 100 %) and/or transposed (±6 semitones),
    // rendered in the background; the grid follows the slower tempo.
    void setSongSpeed (double speed);
    void setSongSemitones (int semitones);
    bool isRenderingSong() const noexcept { return renderingSong; }

    // Tempo corrections, keeping the first bar on bar 2: ×2, ÷2, or tapped
    // along (four taps or more, Taper). In the song's own tempo (100 %).
    void setSongTempo (double bpm);
    void tapSongTempo();
    int getTapCount() const noexcept { return (int) taps.size(); }
    // Moves the song by a few milliseconds against the grid (⌘Z-able).
    void nudgeSong (double seconds);
    // Puts the song's first bar back on bar 2.
    void placeSongOnBar2();

    struct SongMedia
    {
        std::shared_ptr<const AudioEngine::SongAudio> audio;    // at the engine's sample rate, as it plays
        std::shared_ptr<const AudioEngine::SongAudio> original; // the file as is (100 %, no transposition)
        double sampleRate = 0.0;
        std::vector<float> peaks; // 100 per second, both channels
        static constexpr double peaksPerSecond = 100.0;
    };
    const SongMedia* getSongMedia() const { return songMedia.audio != nullptr ? &songMedia : nullptr; }

    //==============================================================================
    // Camera
    void openCamera (const juce::String& deviceName);
    void closeCamera();
    // Monter switches the camera off without forgetting it was on: resume
    // reopens the same device (no-op when it was not suspended).
    void suspendCamera();
    void resumeCamera();
    bool isCameraSuspended() const noexcept { return cameraSuspended; }
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
        bool includeSong = true; // "Guitare seule" for platforms that mute copyrighted music
    };
    bool hasVideoBetween (double start, double end) const;
    void exportClip (const ExportRequest& request, std::function<void (bool ok, juce::String message, juce::File file)> onDone);
    juce::File exportAudio (const ExportRequest& request, juce::String& error);
    bool exportLoopTo (const juce::File& wavFile, juce::String& error);

    // Offline stereo mixdown of [start, end): what the player hears, minus the click.
    struct StereoMix
    {
        std::vector<float> left, right;
    };
    StereoMix renderMix (double start, double end, double sampleRate, bool includeSong = true) const;

    // Correction added to the device's reported round trip (Audio Settings).
    void setLatencyAdjustmentMs (double ms);
    double getLatencyAdjustmentMs() const noexcept { return latencyAdjustmentMs; }

    //==============================================================================
    // Project
    void newProject();
    bool openProject (const juce::File& folderOrJson);
    bool saveProject();
    bool saveProjectAs (const juce::File& destination);
    juce::String getProjectName() const { return project.name; }
    // Something changed since the project was last opened or saved.
    bool hasUnsavedChanges() const noexcept { return modified; }
    bool hasProjectOnDisk() const { return project.getProjectFile().existsAsFile(); }

    //==============================================================================
    // A short user-facing notice (toast), cleared after a few seconds.
    juce::String getNotice() const;
    // seconds: how long the toast stays; errors linger longer than the default.
    void notify (const juce::String& message, double seconds = 4.0);

    juce::PropertiesFile& getSettings() { return *settings.getUserSettings(); }
    // ~/Library/Application Support/Roadie/Roadie.settings on macOS.
    static juce::PropertiesFile::Options getSettingsOptions();

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
    void pushSongPiecesToEngine();
    void normaliseSongStart();
    static void shiftSong (Arrangement& a, double delta);
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

    std::array<float, 7> knobs {};
    bool armedGuitar = true, armedVideo = true;
    double bpm = 100.0;
    int beatsPerBar = 4;
    bool metronomeOn = false;

    std::array<bool, 4> muted {}, soloed {};

    void loadSongMedia();
    void pushSongToEngine();
    SongMedia songMedia;
    bool importingSong = false, renderingSong = false;
    int songRenderGeneration = 0;
    void renderSongVariant();
    int songLoadGeneration = 0;
    std::vector<double> taps;

    bool recordingTake = false, finishingTake = false;
    Take pendingTake;
    double takeStartedAt = 0.0;
    double pendingTakeStart = 0.0;
    bool countInOn = true;
    double countInTarget = -1.0; // timeline seconds where the take's region starts

    struct EditState
    {
        Arrangement arrangement;
        int loopGeneration = 0;
        juce::String name;
    };
    std::vector<EditState> undoStack, redoStack;
    int loopGeneration = 0; // bumps when a new loop is recorded: older loop placements no longer apply
    int knownLoopRecordCount = 0;

    bool loopCountIn = true, magnetOn = true;
    int loopBars = 0;
    bool downloadingSong = false;
    int songDownloadPercent = -1;
    bool reamping = false;
    Selection selection = Selection::none;
    int selectedClip = -1;
    juce::Range<double> selectedRange;
    Track selectedRangeTrack = Track::amp;
    bool loopRegionPending = false; // a loop is recording: its region gets its length when it closes

    std::vector<TakeMedia> takeMedia;
    double mediaSampleRate = 0.0;

    VideoPreview preview;
    bool previewing = false;
    double lastLocateTime = -10.0;

    bool cameraOpening = false, cameraSuspended = false;
    juce::String suspendedCameraDevice;
    bool modified = false;
    AutosavePolicy autosave;
    void markModified();
    juce::String cameraError;

    // Calibration listening while a take is recorded (no clap needed).
    bool autoCalibration = false;
    void startAutoCalibration();
    CalibrationState calibrationState = CalibrationState::idle;
    juce::String calibrationMessage;
    double calibrationStartedAt = 0.0;
    bool calibrated = false;
    double syncLagSeconds = 0.0;
    double latencyAdjustmentMs = 0.0;

    juce::String notice;
    double noticeTime = 0.0;
    double noticeLifetime = 4.0;
};
